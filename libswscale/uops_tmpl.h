/**
 * Copyright (C) 2026 Niklas Haas
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

#ifndef SWSCALE_UOPS_TMPL_H
#define SWSCALE_UOPS_TMPL_H

/**
 * Helper macros for the C-based backend.
 *
 * To use these macros, `pixel_t` should be defined as the type of pixels.
 */

#include <assert.h>
#include <float.h>
#include <stdint.h>

#include "libavutil/attributes.h"

#include "ops_chain.h"
#include "uops_macros.h"

#ifndef SWS_BLOCK_SIZE
#  define SWS_BLOCK_SIZE 32
#endif

typedef union block_t {
    uint8_t   u8[SWS_BLOCK_SIZE];
    uint16_t u16[SWS_BLOCK_SIZE];
    uint32_t u32[SWS_BLOCK_SIZE];
    float    f32[SWS_BLOCK_SIZE];
} block_t;

#define SIZEOF_BLOCK (sizeof(pixel_t) * SWS_BLOCK_SIZE)

/**
 * Internal context holding per-iter execution data. The data pointers will be
 * directly incremented by the corresponding read/write functions.
 */
typedef struct SwsOpIter {
    uintptr_t in[4];
    uintptr_t out[4];
    int x, y;

    /* Link back to per-slice execution context */
    const SwsOpExec *exec;
} SwsOpIter;

#ifdef __clang__
#  define SWS_LOOP AV_PRAGMA(clang loop vectorize(assume_safety))
#elif defined(__GNUC__)
#  define SWS_LOOP AV_PRAGMA(GCC ivdep)
#else
#  define SWS_LOOP
#endif

/* Miscellaneous helpers */
#define bitfn2(name, ext) name ## _ ## ext
#define bitfn(name, ext)  bitfn2(name, ext)
#define fn(name)          bitfn(name, PX)

#define bump_ptr(ptr, bump) ((pixel_t *) ((uintptr_t) (ptr) + (bump)))

/* Helpers for dealing with component masks */
#define X SWS_COMP_TEST(mask, 0)
#define Y SWS_COMP_TEST(mask, 1)
#define Z SWS_COMP_TEST(mask, 2)
#define W SWS_COMP_TEST(mask, 3)

/* Helper macros to make writing common function signatures less painful */
#define DECL_FUNC(NAME, ...)                                                    \
    av_always_inline static void                                                \
        fn(NAME)(SwsOpIter *restrict iter, const SwsOpImpl *restrict impl,      \
                 pixel_t *restrict x, pixel_t *restrict y,                      \
                 pixel_t *restrict z, pixel_t *restrict w,                      \
                 __VA_ARGS__)

#define DECL_READ(NAME, ...)                                                    \
    DECL_FUNC(NAME, __VA_ARGS__,                                                \
              const pixel_t *restrict in0, const pixel_t *restrict in1,         \
              const pixel_t *restrict in2, const pixel_t *restrict in3)         \

#define DECL_WRITE(NAME, ...)                                                   \
    DECL_FUNC(NAME, __VA_ARGS__,                                                \
              pixel_t *restrict out0, pixel_t *restrict out1,                   \
              pixel_t *restrict out2, pixel_t *restrict out3)                   \

#define CALL(NAME, ...) fn(NAME)(iter, impl, x, y, z, w, __VA_ARGS__)

/* Helper macro to call into the next continuation with a given type */
#define CONTINUE(...)                                                           \
    ((void (*)(SwsOpIter *, const SwsOpImpl *,                                  \
               void *restrict x, void *restrict y,                              \
               void *restrict z, void *restrict w)) impl->cont)                 \
        (iter, &impl[1], __VA_ARGS__)

/* Helper macros for common op setup code */
#define DECL_SETUP(NAME, PARAMS, OUT)                                           \
    av_unused static int fn(NAME)(const SwsImplParams *PARAMS,                  \
                                  SwsImplResult *OUT)

/* Helper macro for declaring kernel entry points */
#define DECL_IMPL(FUNC, NAME, TYPE, UOP, ...)                                   \
    av_flatten static void NAME##_c(SwsOpIter *restrict iter,                   \
                                    const SwsOpImpl *restrict impl,             \
                                    void *restrict x, void *restrict y,         \
                                    void *restrict z, void *restrict w)         \
    {                                                                           \
        CALL(FUNC, __VA_ARGS__);                                                \
    }

#define DECL_IMPL_READ(...)                                                     \
    DECL_IMPL(__VA_ARGS__,                                                      \
              (const pixel_t *) iter->in[0], (const pixel_t *) iter->in[1],     \
              (const pixel_t *) iter->in[2], (const pixel_t *) iter->in[3])

#define DECL_IMPL_WRITE(...)                                                    \
    DECL_IMPL(__VA_ARGS__,                                                      \
              (pixel_t *) iter->out[0], (pixel_t *) iter->out[1],               \
              (pixel_t *) iter->out[2], (pixel_t *) iter->out[3])

#define REF_ENTRY(DUMMY, NAME, ...) &uop_##NAME,
#define DECL_ENTRY(SETUP, NAME, ...)                                            \
    static const SwsUOpEntry uop_##NAME = {                                     \
        .func = (SwsFuncPtr) NAME##_c,                                          \
        __VA_ARGS__,                                                            \
        SETUP                                                                   \
    };

#endif
