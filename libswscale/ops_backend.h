/**
 * Copyright (C) 2025 Niklas Haas
 *
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

#ifndef SWSCALE_OPS_BACKEND_H
#define SWSCALE_OPS_BACKEND_H

/**
 * Helper macros for the C-based backend.
 *
 * To use these macros, the following types must be defined:
 *  - PIXEL_TYPE should be one of SWS_PIXEL_*
 *  - pixel_t should be the type of pixels
 *  - block_t should be the type of blocks (groups of pixels)
 */

#include <assert.h>
#include <float.h>
#include <stdint.h>

#include "libavutil/attributes.h"
#include "libavutil/mem.h"

#include "ops_chain.h"

/**
 * Internal context holding per-iter execution data. The data pointers will be
 * directly incremented by the corresponding read/write functions.
 */
typedef struct SwsOpIter {
    const uint8_t *in[4];
    uint8_t *out[4];
    int x, y;
} SwsOpIter;

#ifdef __clang__
#  define SWS_FUNC
#  define SWS_LOOP AV_PRAGMA(clang loop vectorize(assume_safety))
#elif defined(__GNUC__)
#  define SWS_FUNC __attribute__((optimize("tree-vectorize")))
#  define SWS_LOOP AV_PRAGMA(GCC ivdep)
#else
#  define SWS_FUNC
#  define SWS_LOOP
#endif

/* Miscellaneous helpers */
#define bitfn2(name, ext) name ## _ ## ext
#define bitfn(name, ext)  bitfn2(name, ext)

#define FN_SUFFIX AV_JOIN(FMT_CHAR, BIT_DEPTH)
#define fn(name)  bitfn(name, FN_SUFFIX)

#define av_q2pixel(q) ((q).den ? (pixel_t) (q).num / (q).den : 0)

/* Helper macros to make writing common function signatures less painful */
#define DECL_FUNC(NAME, ...)                                                    \
    static av_always_inline void fn(NAME)(SwsOpIter *restrict iter,             \
                                          const SwsOpImpl *restrict impl,       \
                                          block_t x, block_t y,                 \
                                          block_t z, block_t w,                 \
                                          __VA_ARGS__)

#define DECL_READ(NAME, ...)                                                    \
    static av_always_inline void fn(NAME)(SwsOpIter *restrict iter,             \
                                          const SwsOpImpl *restrict impl,       \
                                          const pixel_t *restrict in0,          \
                                          const pixel_t *restrict in1,          \
                                          const pixel_t *restrict in2,          \
                                          const pixel_t *restrict in3,          \
                                          __VA_ARGS__)

#define DECL_WRITE(NAME, ...)                                                   \
    DECL_FUNC(NAME, pixel_t *restrict out0, pixel_t *restrict out1,             \
                    pixel_t *restrict out2, pixel_t *restrict out3,             \
                    __VA_ARGS__)

/* Helper macros to call into functions declared with DECL_FUNC_* */
#define CALL(FUNC, ...) \
    fn(FUNC)(iter, impl, x, y, z, w, __VA_ARGS__)

#define CALL_READ(FUNC, ...)                                                    \
    fn(FUNC)(iter, impl, (const pixel_t *) iter->in[0],                         \
                         (const pixel_t *) iter->in[1],                         \
                         (const pixel_t *) iter->in[2],                         \
                         (const pixel_t *) iter->in[3], __VA_ARGS__)

#define CALL_WRITE(FUNC, ...)                                                   \
    CALL(FUNC, (pixel_t *) iter->out[0], (pixel_t *) iter->out[1],              \
               (pixel_t *) iter->out[2], (pixel_t *) iter->out[3], __VA_ARGS__)

/* Helper macros to declare continuation functions */
#define DECL_IMPL(NAME)                                                         \
    static SWS_FUNC void fn(NAME)(SwsOpIter *restrict iter,                     \
                                  const SwsOpImpl *restrict impl,               \
                                  block_t x, block_t y,                         \
                                  block_t z, block_t w)

#define DECL_IMPL_READ(NAME)                                                    \
    static SWS_FUNC void fn(NAME)(SwsOpIter *restrict iter,                     \
                                  const SwsOpImpl *restrict impl)

/* Helper macro to call into the next continuation with a given type */
#define CONTINUE(TYPE, ...)                                                     \
    ((void (*)(SwsOpIter *, const SwsOpImpl *,                                  \
               TYPE x, TYPE y, TYPE z, TYPE w)) impl->cont)                     \
        (iter, &impl[1], __VA_ARGS__)

/* Helper macros for common op setup code */
#define DECL_SETUP(NAME)                                                        \
    static int fn(NAME)(const SwsOp *op, SwsOpPriv *out)

#define SETUP_MEMDUP(c) ff_setup_memdup(&(c), sizeof(c), out)
static inline int ff_setup_memdup(const void *c, size_t size, SwsOpPriv *out)
{
    out->ptr = av_memdup(c, size);
    return out->ptr ? 0 : AVERROR(ENOMEM);
}

/* Helper macro for declaring op table entries */
#define DECL_ENTRY(NAME, ...)                                                   \
    static const SwsOpEntry fn(op_##NAME) = {                                   \
        .func = (SwsFuncPtr) fn(NAME),                                          \
        .type = PIXEL_TYPE,                                                     \
        __VA_ARGS__                                                             \
    }

/* Helpers to define functions for common subsets of components */
#define DECL_PATTERN(NAME) \
    DECL_FUNC(NAME, const bool X, const bool Y, const bool Z, const bool W)

#define WRAP_PATTERN(FUNC, X, Y, Z, W, ...)                                     \
    DECL_IMPL(FUNC##_##X##Y##Z##W)                                              \
    {                                                                           \
        CALL(FUNC, X, Y, Z, W);                                                 \
    }                                                                           \
                                                                                \
    DECL_ENTRY(FUNC##_##X##Y##Z##W,                                             \
        .unused = { !X, !Y, !Z, !W },                                           \
        __VA_ARGS__                                                             \
    )

#define WRAP_COMMON_PATTERNS(FUNC, ...)                                         \
    WRAP_PATTERN(FUNC, 1, 0, 0, 0, __VA_ARGS__);                                \
    WRAP_PATTERN(FUNC, 1, 0, 0, 1, __VA_ARGS__);                                \
    WRAP_PATTERN(FUNC, 1, 1, 1, 0, __VA_ARGS__);                                \
    WRAP_PATTERN(FUNC, 1, 1, 1, 1, __VA_ARGS__)

#define REF_COMMON_PATTERNS(NAME)                                               \
    &fn(op_##NAME##_1000),                                                      \
    &fn(op_##NAME##_1001),                                                      \
    &fn(op_##NAME##_1110),                                                      \
    &fn(op_##NAME##_1111)

#endif
