/*
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

#include "refstruct.h"

#include "avassert.h"
#include "error.h"
#include "macros.h"
#include "mem.h"
#include "mem_internal.h"
#include "thread.h"

#ifndef REFSTRUCT_CHECKED
#ifndef ASSERT_LEVEL
#define ASSERT_LEVEL 0
#endif
#define REFSTRUCT_CHECKED (ASSERT_LEVEL >= 1)
#endif

#if REFSTRUCT_CHECKED
#define ff_assert(cond) av_assert0(cond)
#else
#define ff_assert(cond) ((void)0)
#endif

#define REFSTRUCT_COOKIE AV_NE((uint64_t)MKBETAG('R', 'e', 'f', 'S') << 32 | MKBETAG('t', 'r', 'u', 'c'), \
                               MKTAG('R', 'e', 'f', 'S') | (uint64_t)MKTAG('t', 'r', 'u', 'c') << 32)

#if __STDC_VERSION__ >= 201112L && !defined(_MSC_VER)
#define REFCOUNT_OFFSET FFALIGN(sizeof(RefCount), FFMAX(ALIGN_64, _Alignof(max_align_t)))
#else
#define REFCOUNT_OFFSET FFALIGN(sizeof(RefCount), ALIGN_64)
#endif

typedef struct RefCount {
    /**
     * An uintptr_t is big enough to hold the address of every reference,
     * so no overflow can happen when incrementing the refcount as long as
     * the user does not throw away references.
     */
    atomic_uintptr_t  refcount;
    AVRefStructOpaque opaque;
    void (*free_cb)(AVRefStructOpaque opaque, void *obj);
    void (*free)(void *ref);

#if REFSTRUCT_CHECKED
    uint64_t cookie;
#endif
} RefCount;

static RefCount *get_refcount(void *obj)
{
    RefCount *ref = (RefCount*)((char*)obj - REFCOUNT_OFFSET);
    ff_assert(ref->cookie == REFSTRUCT_COOKIE);
    return ref;
}

static const RefCount *cget_refcount(const void *obj)
{
    const RefCount *ref = (const RefCount*)((const char*)obj - REFCOUNT_OFFSET);
    ff_assert(ref->cookie == REFSTRUCT_COOKIE);
    return ref;
}

static void *get_userdata(void *buf)
{
    return (char*)buf + REFCOUNT_OFFSET;
}

static void refcount_init(RefCount *ref, AVRefStructOpaque opaque,
                          void (*free_cb)(AVRefStructOpaque opaque, void *obj))
{
    atomic_init(&ref->refcount, 1);
    ref->opaque  = opaque;
    ref->free_cb = free_cb;
    ref->free    = av_free;

#if REFSTRUCT_CHECKED
    ref->cookie  = REFSTRUCT_COOKIE;
#endif
}

void *av_refstruct_alloc_ext_c(size_t size, unsigned flags, AVRefStructOpaque opaque,
                               void (*free_cb)(AVRefStructOpaque opaque, void *obj))
{
    void *buf, *obj;

    if (size > SIZE_MAX - REFCOUNT_OFFSET)
        return NULL;
    buf = av_malloc(size + REFCOUNT_OFFSET);
    if (!buf)
        return NULL;
    refcount_init(buf, opaque, free_cb);
    obj = get_userdata(buf);
    if (!(flags & AV_REFSTRUCT_FLAG_NO_ZEROING))
        memset(obj, 0, size);

    return obj;
}

void av_refstruct_unref(void *objp)
{
    void *obj;
    RefCount *ref;

    memcpy(&obj, objp, sizeof(obj));
    if (!obj)
        return;
    memcpy(objp, &(void *){ NULL }, sizeof(obj));

    ref = get_refcount(obj);
    if (atomic_fetch_sub_explicit(&ref->refcount, 1, memory_order_acq_rel) == 1) {
        if (ref->free_cb)
            ref->free_cb(ref->opaque, obj);
        ref->free(ref);
    }

    return;
}

void *av_refstruct_ref(void *obj)
{
    RefCount *ref = get_refcount(obj);

    atomic_fetch_add_explicit(&ref->refcount, 1, memory_order_relaxed);

    return obj;
}

const void *av_refstruct_ref_c(const void *obj)
{
    /* Casting const away here is fine, as it is only supposed
     * to apply to the user's data and not our bookkeeping data. */
    RefCount *ref = get_refcount((void*)obj);

    atomic_fetch_add_explicit(&ref->refcount, 1, memory_order_relaxed);

    return obj;
}

void av_refstruct_replace(void *dstp, const void *src)
{
    const void *dst;
    memcpy(&dst, dstp, sizeof(dst));

    if (src == dst)
        return;
    av_refstruct_unref(dstp);
    if (src) {
        dst = av_refstruct_ref_c(src);
        memcpy(dstp, &dst, sizeof(dst));
    }
}

int av_refstruct_exclusive(const void *obj)
{
    const RefCount *ref = cget_refcount(obj);
    /* Casting const away here is safe, because it is a load.
     * It is necessary because atomic_load_explicit() does not
     * accept const atomics in C11 (see also N1807). */
    return atomic_load_explicit((atomic_uintptr_t*)&ref->refcount, memory_order_acquire) == 1;
}

struct AVRefStructPool {
    size_t size;
    AVRefStructOpaque opaque;
    int  (*init_cb)(AVRefStructOpaque opaque, void *obj);
    void (*reset_cb)(AVRefStructOpaque opaque, void *obj);
    void (*free_entry_cb)(AVRefStructOpaque opaque, void *obj);
    void (*free_cb)(AVRefStructOpaque opaque);

    int uninited;
    unsigned entry_flags;
    unsigned pool_flags;

    /** The number of outstanding entries not in available_entries. */
    atomic_uintptr_t refcount;
    /**
     * This is a linked list of available entries;
     * the RefCount's opaque pointer is used as next pointer
     * for available entries.
     * While the entries are in use, the opaque is a pointer
     * to the corresponding AVRefStructPool.
     */
    RefCount *available_entries;
    AVMutex mutex;
};

static void pool_free(AVRefStructPool *pool)
{
    ff_mutex_destroy(&pool->mutex);
    if (pool->free_cb)
        pool->free_cb(pool->opaque);
    av_free(get_refcount(pool));
}

static void pool_free_entry(AVRefStructPool *pool, RefCount *ref)
{
    if (pool->free_entry_cb)
        pool->free_entry_cb(pool->opaque, get_userdata(ref));
    av_free(ref);
}

static void pool_return_entry(void *ref_)
{
    RefCount *ref = ref_;
    AVRefStructPool *pool = ref->opaque.nc;

    ff_mutex_lock(&pool->mutex);
    if (!pool->uninited) {
        ref->opaque.nc = pool->available_entries;
        pool->available_entries = ref;
        ref = NULL;
    }
    ff_mutex_unlock(&pool->mutex);

    if (ref)
        pool_free_entry(pool, ref);

    if (atomic_fetch_sub_explicit(&pool->refcount, 1, memory_order_acq_rel) == 1)
        pool_free(pool);
}

static void pool_reset_entry(AVRefStructOpaque opaque, void *entry)
{
    AVRefStructPool *pool = opaque.nc;

    pool->reset_cb(pool->opaque, entry);
}

static int refstruct_pool_get_ext(void *datap, AVRefStructPool *pool)
{
    void *ret = NULL;

    memcpy(datap, &(void *){ NULL }, sizeof(void*));

    ff_mutex_lock(&pool->mutex);
    ff_assert(!pool->uninited);
    if (pool->available_entries) {
        RefCount *ref = pool->available_entries;
        ret = get_userdata(ref);
        pool->available_entries = ref->opaque.nc;
        ref->opaque.nc = pool;
        atomic_init(&ref->refcount, 1);
    }
    ff_mutex_unlock(&pool->mutex);

    if (!ret) {
        RefCount *ref;
        ret = av_refstruct_alloc_ext(pool->size, pool->entry_flags, pool,
                                     pool->reset_cb ? pool_reset_entry : NULL);
        if (!ret)
            return AVERROR(ENOMEM);
        ref = get_refcount(ret);
        ref->free = pool_return_entry;
        if (pool->init_cb) {
            int err = pool->init_cb(pool->opaque, ret);
            if (err < 0) {
                if (pool->pool_flags & AV_REFSTRUCT_POOL_FLAG_RESET_ON_INIT_ERROR)
                    pool->reset_cb(pool->opaque, ret);
                if (pool->pool_flags & AV_REFSTRUCT_POOL_FLAG_FREE_ON_INIT_ERROR)
                    pool->free_entry_cb(pool->opaque, ret);
                av_free(ref);
                return err;
            }
        }
    }
    atomic_fetch_add_explicit(&pool->refcount, 1, memory_order_relaxed);

    if (pool->pool_flags & AV_REFSTRUCT_POOL_FLAG_ZERO_EVERY_TIME)
        memset(ret, 0, pool->size);

    memcpy(datap, &ret, sizeof(ret));

    return 0;
}

void *av_refstruct_pool_get(AVRefStructPool *pool)
{
    void *ret;
    refstruct_pool_get_ext(&ret, pool);
    return ret;
}

/**
 * Hint: The content of pool_unref() and refstruct_pool_uninit()
 * could currently be merged; they are only separate functions
 * in case we would ever introduce weak references.
 */
static void pool_unref(void *ref)
{
    AVRefStructPool *pool = get_userdata(ref);
    if (atomic_fetch_sub_explicit(&pool->refcount, 1, memory_order_acq_rel) == 1)
        pool_free(pool);
}

static void refstruct_pool_uninit(AVRefStructOpaque unused, void *obj)
{
    AVRefStructPool *pool = obj;
    RefCount *entry;

    ff_mutex_lock(&pool->mutex);
    ff_assert(!pool->uninited);
    pool->uninited = 1;
    entry = pool->available_entries;
    pool->available_entries = NULL;
    ff_mutex_unlock(&pool->mutex);

    while (entry) {
        void *next = entry->opaque.nc;
        pool_free_entry(pool, entry);
        entry = next;
    }
}

AVRefStructPool *av_refstruct_pool_alloc(size_t size, unsigned flags)
{
    return av_refstruct_pool_alloc_ext(size, flags, NULL, NULL, NULL, NULL, NULL);
}

AVRefStructPool *av_refstruct_pool_alloc_ext_c(size_t size, unsigned flags,
                                               AVRefStructOpaque opaque,
                                               int  (*init_cb)(AVRefStructOpaque opaque, void *obj),
                                               void (*reset_cb)(AVRefStructOpaque opaque, void *obj),
                                               void (*free_entry_cb)(AVRefStructOpaque opaque, void *obj),
                                               void (*free_cb)(AVRefStructOpaque opaque))
{
    AVRefStructPool *pool = av_refstruct_alloc_ext(sizeof(*pool), 0, NULL,
                                                   refstruct_pool_uninit);
    int err;

    if (!pool)
        return NULL;
    get_refcount(pool)->free = pool_unref;

    pool->size          = size;
    pool->opaque        = opaque;
    pool->init_cb       = init_cb;
    pool->reset_cb      = reset_cb;
    pool->free_entry_cb = free_entry_cb;
    pool->free_cb       = free_cb;
#define COMMON_FLAGS AV_REFSTRUCT_POOL_FLAG_NO_ZEROING
    pool->entry_flags   = flags & COMMON_FLAGS;
    // Filter out nonsense combinations to avoid checks later.
    if (!pool->reset_cb)
        flags &= ~AV_REFSTRUCT_POOL_FLAG_RESET_ON_INIT_ERROR;
    if (!pool->free_entry_cb)
        flags &= ~AV_REFSTRUCT_POOL_FLAG_FREE_ON_INIT_ERROR;
    pool->pool_flags    = flags;

    if (flags & AV_REFSTRUCT_POOL_FLAG_ZERO_EVERY_TIME) {
        // We will zero the buffer before every use, so zeroing
        // upon allocating the buffer is unnecessary.
        pool->entry_flags |= AV_REFSTRUCT_FLAG_NO_ZEROING;
    }

    atomic_init(&pool->refcount, 1);

    err = ff_mutex_init(&pool->mutex, NULL);
    if (err) {
        // Don't call av_refstruct_uninit() on pool, as it hasn't been properly
        // set up and is just a POD right now.
        av_free(get_refcount(pool));
        return NULL;
    }
    return pool;
}
