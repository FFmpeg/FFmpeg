/*
 * Copyright (c) 2026 Zhao Zhili <quinkblack@foxmail.com>
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


#include "libavutil/mem_internal.h"
#include "libavcodec/pngdsp.h"

#include "checkasm.h"

#define BUF_SIZE 4096

#define randomize_buf(buf, size)        \
    do {                                \
        for (int i = 0; i < size; i++)  \
            buf[i] = (uint8_t)rnd();    \
    } while (0)

static void check_add_bytes_l2(const PNGDSPContext *c)
{
    LOCAL_ALIGNED_16(uint8_t, dst0, [BUF_SIZE]);
    LOCAL_ALIGNED_16(uint8_t, dst1, [BUF_SIZE]);
    LOCAL_ALIGNED_16(uint8_t, src, [2], [BUF_SIZE]);

    declare_func_emms(AV_CPU_FLAG_MMX, void, uint8_t * dst, const uint8_t *src1,
                      const uint8_t *src2, int w);

    randomize_buf(dst0, BUF_SIZE);
    memcpy(dst1, dst0, BUF_SIZE);
    randomize_buf(src[0], BUF_SIZE);
    randomize_buf(src[1], BUF_SIZE);

    const int size[] = {15, 2043, 4096};
    for (int i = 0; i < FF_ARRAY_ELEMS(size); i++) {
        if (check_func(c->add_bytes_l2, "add_bytes_l2_%d", size[i])) {
            call_ref(dst0, src[0], src[1], size[i]);
            call_new(dst1, src[0], src[1], size[i]);
            checkasm_check(uint8_t, dst0, BUF_SIZE, dst1, BUF_SIZE, BUF_SIZE, 1, "dst");
            if (size[i] == BUF_SIZE)
                bench_new(dst1, src[0], src[1], BUF_SIZE);
        }
    }
}

static void check_add_paeth_prediction(const PNGDSPContext *c)
{
    LOCAL_ALIGNED_16(uint8_t, dst0_buf, [BUF_SIZE]);
    LOCAL_ALIGNED_16(uint8_t, dst1_buf, [BUF_SIZE]);
    LOCAL_ALIGNED_16(uint8_t, src, [BUF_SIZE]);
    LOCAL_ALIGNED_16(uint8_t, top_buf, [BUF_SIZE]);

    randomize_buf(dst0_buf, BUF_SIZE);
    randomize_buf(src, BUF_SIZE);
    randomize_buf(top_buf, BUF_SIZE);

    declare_func_emms(AV_CPU_FLAG_MMX, void, uint8_t * dst, const uint8_t *src,
                      const uint8_t *top, int w, int bpp);

    const int bpps[] = {3, 4, 6, 8};
    for (int i = 0; i < FF_ARRAY_ELEMS(bpps); i++) {
        int bpp = bpps[i];
        if (check_func(c->add_paeth_prediction, "add_paeth_prediction_%d", bpp)) {
            // add_paeth_prediction reads start from (dst - bpp) and (top - bpp).
            uint8_t *dst0 = &dst0_buf[bpp];
            uint8_t *dst1 = &dst1_buf[bpp];
            uint8_t *top = &top_buf[bpp];
            int w = (BUF_SIZE - bpp) / bpp * bpp;

            // dst buffer is both read and written, so dst0 and dst1 must remain the same before test
            memcpy(dst1_buf, dst0_buf, BUF_SIZE);

            call_ref(dst0, src, top, w, bpp);
            call_new(dst1, src, top, w, bpp);

            /* This match the use case in ff_png_filter_row, that x86 asm version of
             * add_paeth_prediction doesn't write last two bytes for bpp = 3 and 6.
             * The C function takes care to rewrite the last 3 bytes.
             */
            if (bpp & 3) {
                for (int j = w - 3; j < w; j++)
                    dst1[j] = dst0[j];
            }
            // check dst_buf to ensure there is no overwrite
            checkasm_check(uint8_t, dst0_buf, 0, dst1_buf, 0, BUF_SIZE, 1, "dst");
            bench_new(dst1, src, top, w, bpp);
        }
    }
}

void checkasm_check_png(void)
{
    PNGDSPContext c;

    ff_pngdsp_init(&c);

    check_add_bytes_l2(&c);
    report("add_bytes_l2");
    check_add_paeth_prediction(&c);
    report("add_paeth_prediction");
}
