/*
 * Copyright (c) 2015 Tiancheng "Timothy" Gu
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
#include "libavcodec/pixblockdsp.h"
#include "libavutil/common.h"
#include "libavutil/internal.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem_internal.h"

#define BUF_UNITS 8
#define BUF_SIZE (BUF_UNITS * 128 + 8 * BUF_UNITS)

#define randomize_buffers()                 \
    do {                                    \
        int i;                              \
        for (i = 0; i < BUF_SIZE; i += 4) { \
            uint32_t r = rnd();             \
            AV_WN32A(src10 + i, r);         \
            AV_WN32A(src11 + i, r);         \
            r = rnd();                      \
            AV_WN32A(src20 + i, r);         \
            AV_WN32A(src21 + i, r);         \
            r = rnd();                      \
            AV_WN32A(dst0_ + i, r);         \
            AV_WN32A(dst1_ + i, r);         \
        }                                   \
    } while (0)

#define check_get_pixels(type, aligned)                                                    \
    do {                                                                                   \
        int i;                                                                             \
        declare_func(void, int16_t *block, const uint8_t *pixels, ptrdiff_t line_size);    \
                                                                                           \
        for (i = 0; i < BUF_UNITS; i++) {                                              \
            int src_offset = i * 64 * sizeof(type) + (aligned ? 8 : 1) * i;                \
            int dst_offset = i * 64; /* dst must be aligned */                             \
            randomize_buffers();                                                           \
            call_ref(dst0 + dst_offset, src10 + src_offset, 8);                            \
            call_new(dst1 + dst_offset, src11 + src_offset, 8);                            \
            if (memcmp(src10, src11, BUF_SIZE)|| memcmp(dst0, dst1, BUF_SIZE)) \
                fail();                                                                    \
            bench_new(dst1 + dst_offset, src11 + src_offset, 8);                           \
        }                                                                                  \
    } while (0)

#define check_diff_pixels(type, aligned)                                                   \
    do {                                                                                   \
        int i;                                                                             \
        declare_func(void, int16_t *av_restrict block, const uint8_t *s1, const uint8_t *s2, ptrdiff_t stride); \
                                                                                           \
        for (i = 0; i < BUF_UNITS; i++) {                                              \
            int src_offset = i * 64 * sizeof(type) + (aligned ? 8 : 1) * i;                \
            int dst_offset = i * 64; /* dst must be aligned */                             \
            randomize_buffers();                                                           \
            call_ref(dst0 + dst_offset, src10 + src_offset, src20 + src_offset, 8);        \
            call_new(dst1 + dst_offset, src11 + src_offset, src21 + src_offset, 8);        \
            if (memcmp(src10, src11, BUF_SIZE) || memcmp(src20, src21, BUF_SIZE) || memcmp(dst0, dst1, BUF_SIZE)) \
                fail();                                                                    \
            bench_new(dst1 + dst_offset, src11 + src_offset, src21 + src_offset, 8);       \
        }                                                                                  \
    } while (0)

void checkasm_check_pixblockdsp(void)
{
    LOCAL_ALIGNED_16(uint8_t, src10, [BUF_SIZE]);
    LOCAL_ALIGNED_16(uint8_t, src11, [BUF_SIZE]);
    LOCAL_ALIGNED_16(uint8_t, src20, [BUF_SIZE]);
    LOCAL_ALIGNED_16(uint8_t, src21, [BUF_SIZE]);
    LOCAL_ALIGNED_16(uint8_t, dst0_, [BUF_SIZE]);
    LOCAL_ALIGNED_16(uint8_t, dst1_, [BUF_SIZE]);
    uint16_t *dst0 = (uint16_t *)dst0_;
    uint16_t *dst1 = (uint16_t *)dst1_;
    PixblockDSPContext h;
    AVCodecContext avctx = {
        .bits_per_raw_sample = 8,
    };

    ff_pixblockdsp_init(&h, &avctx);

    if (check_func(h.get_pixels, "get_pixels"))
        check_get_pixels(uint8_t, 1);
    if (check_func(h.get_pixels_unaligned, "get_pixels_unaligned"))
        check_get_pixels(uint8_t, 0);

    report("get_pixels");

    if (check_func(h.diff_pixels, "diff_pixels"))
        check_diff_pixels(uint8_t, 1);
    if (check_func(h.diff_pixels_unaligned, "diff_pixels_unaligned"))
        check_diff_pixels(uint8_t, 0);

    report("diff_pixels");
}
