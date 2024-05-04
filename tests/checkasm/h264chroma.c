/*
 * Copyright (c) Lynne
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
#include <stdint.h>
#include "checkasm.h"
#include "libavcodec/h264chroma.h"
#include "libavutil/mem_internal.h"
#include "libavutil/intreadwrite.h"

#define SIZEOF_PIXEL ((bit_depth + 7) / 8)

#define randomize_buffers(bit_depth)             \
    do {                                         \
        if (bit_depth == 8) {                    \
            for (int i = 0; i < 16*18*2; i++)    \
                src[i] = rnd() & 0x3;            \
        } else {                                 \
            for (int i = 0; i < 16*18; i += 2)   \
                AV_WN16(&src[i], rnd() & 0xFF);  \
        }                                        \
    } while (0)

static void check_chroma_mc(void)
{
    H264ChromaContext h;
    LOCAL_ALIGNED_32(uint8_t, src,  [16 * 18 * 2]);
    LOCAL_ALIGNED_32(uint8_t, dst0, [16 * 18 * 2]);
    LOCAL_ALIGNED_32(uint8_t, dst1, [16 * 18 * 2]);

    declare_func_emms(AV_CPU_FLAG_MMX, void, uint8_t *dst, const uint8_t *src,
                      ptrdiff_t stride, int h, int x, int y);

    for (int bit_depth = 8; bit_depth <= 10; bit_depth++) {
        ff_h264chroma_init(&h, bit_depth);
        randomize_buffers(bit_depth);
        for (int size = 0; size < 4; size++) {

#define CHECK_CHROMA_MC(name)                                                                             \
            do {                                                                                          \
                if (check_func(h.name## _pixels_tab[size], #name "_mc%d_%d", 1 << (3-size), bit_depth)) { \
                    for (int x = 0; x < 2; x++) {                                                         \
                        for (int y = 0; y < 2; y++) {                                                     \
                            memcpy(dst0, src, 16 * 18 * SIZEOF_PIXEL);                                    \
                            memcpy(dst1, src, 16 * 18 * SIZEOF_PIXEL);                                    \
                            call_ref(dst0, src, 16 * SIZEOF_PIXEL, 16, x, y);                             \
                            call_new(dst1, src, 16 * SIZEOF_PIXEL, 16, x, y);                             \
                            if (memcmp(dst0, dst1, 16 * 16 * SIZEOF_PIXEL)) {                             \
                                fprintf(stderr, #name ": x:%i, y:%i\n", x, y);                            \
                                fail();                                                                   \
                            }                                                                             \
                            bench_new(dst1, src, 16 * SIZEOF_PIXEL, 16, x, y);                            \
                        }                                                                                 \
                    }                                                                                     \
                }                                                                                         \
            } while (0)

            CHECK_CHROMA_MC(put_h264_chroma);
            CHECK_CHROMA_MC(avg_h264_chroma);
        }
    }
}

void checkasm_check_h264chroma(void)
{
    check_chroma_mc();
    report("chroma_mc");
}
