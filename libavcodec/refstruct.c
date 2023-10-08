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

#include "internal.h"
#include "refstruct.h"

#include "libavutil/avassert.h"
#include "libavutil/macros.h"
#include "libavutil/mem.h"

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

#if __STDC_VERSION__ >= 201112L
#define REFCOUNT_OFFSET FFALIGN(sizeof(RefCount), FFMAX3(STRIDE_ALIGN, 16, _Alignof(max_align_t)))
#else
#define REFCOUNT_OFFSET FFALIGN(sizeof(RefCount), FFMAX(STRIDE_ALIGN, 16))
#endif

typedef struct RefCount {
    /**
     * An uintptr_t is big enough to hold the address of every reference,
     * so no overflow can happen when incrementing the refcount as long as
     * the user does not throw away references.
     */
    atomic_uintptr_t  refcount;
    FFRefStructOpaque opaque;
    void (*free_cb)(FFRefStructOpaque opaque, void *obj);

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

static void refcount_init(RefCount *ref, FFRefStructOpaque opaque,
                          void (*free_cb)(FFRefStructOpaque opaque, void *obj))
{
    atomic_init(&ref->refcount, 1);
    ref->opaque  = opaque;
    ref->free_cb = free_cb;

#if REFSTRUCT_CHECKED
    ref->cookie  = REFSTRUCT_COOKIE;
#endif
}

void *ff_refstruct_alloc_ext_c(size_t size, unsigned flags, FFRefStructOpaque opaque,
                               void (*free_cb)(FFRefStructOpaque opaque, void *obj))
{
    void *buf, *obj;

    if (size > SIZE_MAX - REFCOUNT_OFFSET)
        return NULL;
    buf = av_malloc(size + REFCOUNT_OFFSET);
    if (!buf)
        return NULL;
    refcount_init(buf, opaque, free_cb);
    obj = get_userdata(buf);
    if (!(flags & FF_REFSTRUCT_FLAG_NO_ZEROING))
        memset(obj, 0, size);

    return obj;
}

void ff_refstruct_unref(void *objp)
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
        av_free(ref);
    }

    return;
}

void *ff_refstruct_ref(void *obj)
{
    RefCount *ref = get_refcount(obj);

    atomic_fetch_add_explicit(&ref->refcount, 1, memory_order_relaxed);

    return obj;
}

const void *ff_refstruct_ref_c(const void *obj)
{
    /* Casting const away here is fine, as it is only supposed
     * to apply to the user's data and not our bookkeeping data. */
    RefCount *ref = get_refcount((void*)obj);

    atomic_fetch_add_explicit(&ref->refcount, 1, memory_order_relaxed);

    return obj;
}

void ff_refstruct_replace(void *dstp, const void *src)
{
    const void *dst;
    memcpy(&dst, dstp, sizeof(dst));

    if (src == dst)
        return;
    ff_refstruct_unref(dstp);
    if (src) {
        dst = ff_refstruct_ref_c(src);
        memcpy(dstp, &dst, sizeof(dst));
    }
}

int ff_refstruct_exclusive(const void *obj)
{
    const RefCount *ref = cget_refcount(obj);
    /* Casting const away here is safe, because it is a load.
     * It is necessary because atomic_load_explicit() does not
     * accept const atomics in C11 (see also N1807). */
    return atomic_load_explicit((atomic_uintptr_t*)&ref->refcount, memory_order_acquire) == 1;
}
