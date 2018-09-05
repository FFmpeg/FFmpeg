/*
 * Copyright (c) 2016 Tiancheng "Timothy" Gu
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <string.h>
#include "checkasm.h"
#include "libavfilter/blend.h"
#include "libavutil/common.h"
#include "libavutil/internal.h"
#include "libavutil/intreadwrite.h"

#define WIDTH 256
#define HEIGHT 256
#define BUF_UNITS 3
#define SIZE_PER_UNIT (WIDTH * HEIGHT)
#define BUF_SIZE (BUF_UNITS * SIZE_PER_UNIT)

#define randomize_buffers()                   \
    do {                                      \
        int i, j;                             \
        for (i = 0; i < HEIGHT; i++) {        \
            for (j = 0; j < WIDTH; j++) {     \
                top1[i * WIDTH + j] =         \
                top2[i * WIDTH + j] = i;      \
                bot1[i * WIDTH + j] =         \
                bot2[i * WIDTH + j] = j;      \
            }                                 \
        }                                     \
        for (i = 0; i < SIZE_PER_UNIT; i += 4) { \
            uint32_t r = rnd();               \
            AV_WN32A(dst1 + i, r);            \
            AV_WN32A(dst2 + i, r);            \
        }                                     \
        for (; i < BUF_SIZE; i += 4) {        \
            uint32_t r = rnd();               \
            AV_WN32A(top1 + i, r);            \
            AV_WN32A(top2 + i, r);            \
            r = rnd();                        \
            AV_WN32A(bot1 + i, r);            \
            AV_WN32A(bot2 + i, r);            \
            r = rnd();                        \
            AV_WN32A(dst1 + i, r);            \
            AV_WN32A(dst2 + i, r);            \
        }                                     \
    } while (0)

#define check_blend_func(depth)                                                            \
    do {                                                                                   \
        int i, w;                                                                          \
        declare_func(void, const uint8_t *top, ptrdiff_t top_linesize,                     \
                     const uint8_t *bottom, ptrdiff_t bottom_linesize,                     \
                     uint8_t *dst, ptrdiff_t dst_linesize,                                 \
                     ptrdiff_t width, ptrdiff_t height,                                    \
                     struct FilterParams *param, double *values);                          \
        w = WIDTH / depth;                                                                 \
                                                                                           \
        for (i = 0; i < BUF_UNITS - 1; i++) {                                              \
            int src_offset = i * SIZE_PER_UNIT + (BUF_UNITS - 1 - i) * depth; /* Test various alignments */  \
            int dst_offset = i * SIZE_PER_UNIT; /* dst must be aligned */                  \
            randomize_buffers();                                                           \
            call_ref(top1 + src_offset, w, bot1 + src_offset, w,                           \
                     dst1 + dst_offset, w, w, HEIGHT, &param, NULL);                       \
            call_new(top2 + src_offset, w, bot2 + src_offset, w,                           \
                     dst2 + dst_offset, w, w, HEIGHT, &param, NULL);                       \
            if (memcmp(top1, top2, BUF_SIZE) || memcmp(bot1, bot2, BUF_SIZE) || memcmp(dst1, dst2, BUF_SIZE)) \
                fail();                                                                    \
        }                                                                                  \
        bench_new(top2, w / 4, bot2, w / 4, dst2, w / 4,                                   \
                  w / 4, HEIGHT / 4, &param, NULL);                                        \
    } while (0)

void checkasm_check_blend(void)
{
    uint8_t *top1 = av_malloc(BUF_SIZE);
    uint8_t *top2 = av_malloc(BUF_SIZE);
    uint8_t *bot1 = av_malloc(BUF_SIZE);
    uint8_t *bot2 = av_malloc(BUF_SIZE);
    uint8_t *dst1 = av_malloc(BUF_SIZE);
    uint8_t *dst2 = av_malloc(BUF_SIZE);
    FilterParams param = {
        .opacity = 1.0,
    };

#define check_and_report(name, val, depth)        \
    param.mode = val;                             \
    ff_blend_init(&param, depth - 1);             \
    if (check_func(param.blend, #name))           \
        check_blend_func(depth);

    check_and_report(addition, BLEND_ADDITION, 1)
    check_and_report(grainmerge, BLEND_GRAINMERGE, 1)
    check_and_report(and, BLEND_AND, 1)
    check_and_report(average, BLEND_AVERAGE, 1)
    check_and_report(darken, BLEND_DARKEN, 1)
    check_and_report(grainextract, BLEND_GRAINEXTRACT, 1)
    check_and_report(hardmix, BLEND_HARDMIX, 1)
    check_and_report(lighten, BLEND_LIGHTEN, 1)
    check_and_report(multiply, BLEND_MULTIPLY, 1)
    check_and_report(or, BLEND_OR, 1)
    check_and_report(phoenix, BLEND_PHOENIX, 1)
    check_and_report(screen, BLEND_SCREEN, 1)
    check_and_report(subtract, BLEND_SUBTRACT, 1)
    check_and_report(xor, BLEND_XOR, 1)
    check_and_report(difference, BLEND_DIFFERENCE, 1)
    check_and_report(extremity, BLEND_EXTREMITY, 1)
    check_and_report(negation, BLEND_NEGATION, 1)

    report("8bit");

    check_and_report(addition_16, BLEND_ADDITION, 2)
    check_and_report(grainmerge_16, BLEND_GRAINMERGE, 2)
    check_and_report(and_16, BLEND_AND, 2)
    check_and_report(average_16, BLEND_AVERAGE, 2)
    check_and_report(darken_16, BLEND_DARKEN, 2)
    check_and_report(grainextract_16, BLEND_GRAINEXTRACT, 2)
    check_and_report(difference_16, BLEND_DIFFERENCE, 2)
    check_and_report(extremity_16, BLEND_EXTREMITY, 2)
    check_and_report(negation_16, BLEND_NEGATION, 2)
    check_and_report(lighten_16, BLEND_LIGHTEN, 2)
    check_and_report(or_16, BLEND_OR, 2)
    check_and_report(phoenix_16, BLEND_PHOENIX, 2)
    check_and_report(subtract_16, BLEND_SUBTRACT, 2)
    check_and_report(xor_16, BLEND_SUBTRACT, 2)

    report("16bit");

    av_freep(&top1);
    av_freep(&top2);
    av_freep(&bot1);
    av_freep(&bot2);
    av_freep(&dst1);
    av_freep(&dst2);
}
