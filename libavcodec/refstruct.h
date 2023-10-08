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

#ifndef AVCODEC_REFSTRUCT_H
#define AVCODEC_REFSTRUCT_H

#include <stddef.h>

/**
 * RefStruct is an API for creating reference-counted objects
 * with minimal overhead. The API is designed for objects,
 * not buffers like the AVBuffer API. The main differences
 * to the AVBuffer API are as follows:
 *
 * - It uses void* instead of uint8_t* as its base type due to
 *   its focus on objects.
 * - There are no equivalents of AVBuffer and AVBufferRef.
 *   E.g. there is no way to get the usable size of the object:
 *   The user is supposed to know what is at the other end of
 *   the pointer. It also avoids one level of indirection.
 * - Custom allocators are not supported. This allows to simplify
 *   the implementation and reduce the amount of allocations.
 * - It also has the advantage that the user's free callback need
 *   only free the resources owned by the object, but not the
 *   object itself.
 * - Because referencing (and replacing) an object managed by the
 *   RefStruct API does not involve allocations, they can not fail
 *   and therefore need not be checked.
 *
 * @note Referencing and unreferencing the buffers is thread-safe and thus
 * may be done from multiple threads simultaneously without any need for
 * additional locking.
 */

/**
 * This union is used for all opaque parameters in this API to spare the user
 * to cast const away in case the opaque to use is const-qualified.
 *
 * The functions provided by this API with an FFRefStructOpaque come in pairs
 * named foo_c and foo. The foo function accepts void* as opaque and is just
 * a wrapper around the foo_c function; "_c" means "(potentially) const".
 */
typedef union {
    void *nc;
    const void *c;
} FFRefStructOpaque;

/**
 * If this flag is set in ff_refstruct_alloc_ext_c(), the object will not
 * be initially zeroed.
 */
#define FF_REFSTRUCT_FLAG_NO_ZEROING (1 << 0)

/**
 * Allocate a refcounted object of usable size `size` managed via
 * the RefStruct API.
 *
 * By default (in the absence of flags to the contrary),
 * the returned object is initially zeroed.
 *
 * @param size    Desired usable size of the returned object.
 * @param flags   A bitwise combination of FF_REFSTRUCT_FLAG_* flags.
 * @param opaque  A pointer that will be passed to the free_cb callback.
 * @param free_cb A callback for freeing this object's content
 *                when its reference count reaches zero;
 *                it must not free the object itself.
 * @return A pointer to an object of the desired size or NULL on failure.
 */
void *ff_refstruct_alloc_ext_c(size_t size, unsigned flags, FFRefStructOpaque opaque,
                               void (*free_cb)(FFRefStructOpaque opaque, void *obj));

/**
 * A wrapper around ff_refstruct_alloc_ext_c() for the common case
 * of a non-const qualified opaque.
 *
 * @see ff_refstruct_alloc_ext_c()
 */
static inline
void *ff_refstruct_alloc_ext(size_t size, unsigned flags, void *opaque,
                             void (*free_cb)(FFRefStructOpaque opaque, void *obj))
{
    return ff_refstruct_alloc_ext_c(size, flags, (FFRefStructOpaque){.nc = opaque},
                                    free_cb);
}

/**
 * Equivalent to ff_refstruct_alloc_ext(size, 0, NULL, NULL)
 */
static inline
void *ff_refstruct_allocz(size_t size)
{
    return ff_refstruct_alloc_ext(size, 0, NULL, NULL);
}

/**
 * Decrement the reference count of the underlying object and automatically
 * free the object if there are no more references to it.
 *
 * `*objp == NULL` is legal and a no-op.
 *
 * @param objp Pointer to a pointer that is either NULL or points to an object
 *             managed via this API. `*objp` is set to NULL on return.
 */
void ff_refstruct_unref(void *objp);

/**
 * Create a new reference to an object managed via this API,
 * i.e. increment the reference count of the underlying object
 * and return obj.
 * @return a pointer equal to obj.
 */
void *ff_refstruct_ref(void *obj);

/**
 * Analog of ff_refstruct_ref(), but for constant objects.
 * @see ff_refstruct_ref()
 */
const void *ff_refstruct_ref_c(const void *obj);

/**
 * Ensure `*dstp` refers to the same object as src.
 *
 * If `*dstp` is already equal to src, do nothing. Otherwise unreference `*dstp`
 * and replace it with a new reference to src in case `src != NULL` (this
 * involves incrementing the reference count of src's underlying object) or
 * with NULL otherwise.
 *
 * @param dstp Pointer to a pointer that is either NULL or points to an object
 *             managed via this API.
 * @param src  A pointer to an object managed via this API or NULL.
 */
void ff_refstruct_replace(void *dstp, const void *src);

/**
 * Check whether the reference count of an object managed
 * via this API is 1.
 *
 * @param obj A pointer to an object managed via this API.
 * @return 1 if the reference count of obj is 1; 0 otherwise.
 */
int ff_refstruct_exclusive(const void *obj);

#endif /* AVCODEC_REFSTRUCT_H */
