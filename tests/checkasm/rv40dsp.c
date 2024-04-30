/*
 * Copyright (c) 2024 Institue of Software Chinese Academy of Sciences (ISCAS).
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
#include "libavcodec/rv40dsp.c"
#include "libavutil/mem_internal.h"

#define randomize_buffers()                  \
    do {                                     \
        for (int i = 0; i < 16*18*2; i++)    \
            src[i] = rnd() & 0x3;            \
    } while (0)

static void check_chroma_mc(void)
{
    RV34DSPContext h;
    LOCAL_ALIGNED_32(uint8_t, src,  [16 * 18 * 2]);
    LOCAL_ALIGNED_32(uint8_t, dst0, [16 * 18 * 2]);
    LOCAL_ALIGNED_32(uint8_t, dst1, [16 * 18 * 2]);

    declare_func_emms(AV_CPU_FLAG_MMX, void, uint8_t *dst, const uint8_t *src,
                      ptrdiff_t stride, int h, int x, int y);

    ff_rv40dsp_init(&h);
    randomize_buffers();
    for (int size = 0; size < 2; size++) {

#define CHECK_CHROMA_MC(name)                                                                     \
        do {                                                                                      \
            if (check_func(h.name## _pixels_tab[size], #name "_mc%d", 1 << (3 - size))) {         \
                for (int x = 0; x < 2; x++) {                                                     \
                    for (int y = 0; y < 2; y++) {                                                 \
                        memcpy(dst0, src, 16 * 18);                                               \
                        memcpy(dst1, src, 16 * 18);                                               \
                        call_ref(dst0, src, 16, 16, x, y);                                        \
                        call_new(dst1, src, 16, 16, x, y);                                        \
                        if (memcmp(dst0, dst1, 16 * 16)) {                                        \
                            fprintf(stderr, #name ": x:%i, y:%i\n", x, y);                        \
                            fail();                                                               \
                        }                                                                         \
                        bench_new(dst1, src, 16, 16, x, y);                                       \
                    }                                                                             \
                }                                                                                 \
            }                                                                                     \
        } while (0)

        CHECK_CHROMA_MC(put_chroma);
        CHECK_CHROMA_MC(avg_chroma);
    }
}

void checkasm_check_rv40dsp(void)
{
    check_chroma_mc();
    report("chroma_mc");
}
