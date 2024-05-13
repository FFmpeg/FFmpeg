/*
 * Copyright (c) 2016 Alexandra Hájková
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

#include "libavutil/common.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"

#include "libavcodec/huffyuvdsp.h"

#include "checkasm.h"

#define randomize_buffers(buf, size)     \
    do {                                 \
        int j;                           \
        for (j = 0; j < size; j++)       \
            buf[j] = rnd() & 0xFFFF;       \
    } while (0)

static void check_add_int16(HuffYUVDSPContext *c, unsigned mask, int width, const char * name)
{
    uint16_t *src0 = av_mallocz(width * sizeof(uint16_t));
    uint16_t *src1 = av_mallocz(width * sizeof(uint16_t));
    uint16_t *dst0 = av_mallocz(width * sizeof(uint16_t));
    uint16_t *dst1 = av_mallocz(width * sizeof(uint16_t));

    declare_func(void, uint16_t *dst, const uint16_t *src, unsigned mask, int w);

    if (!src0 || !src1 || !dst0 || !dst1)
        fail();

    randomize_buffers(src0, width);
    memcpy(src1, src0, width * sizeof(uint16_t));

    if (check_func(c->add_int16, "%s", name)) {
        call_ref(dst0, src0, mask, width);
        call_new(dst1, src1, mask, width);
        if (memcmp(dst0, dst1, width * sizeof(uint16_t)))
            fail();
        bench_new(dst1, src1, mask, width);
    }

    av_free(src0);
    av_free(src1);
    av_free(dst0);
    av_free(dst1);
}

static void check_add_hfyu_left_pred_bgr32(HuffYUVDSPContext *c)
{
#define BUF_SIZE 1080
    uint8_t src[4 * BUF_SIZE], dst0[4 * BUF_SIZE], dst1[4 * BUF_SIZE];
    uint8_t left[4], left0[4], left1[4];

    declare_func(void, uint8_t *d, const uint8_t *s, intptr_t w, uint8_t *l);

    randomize_buffers(src, sizeof (src));
    randomize_buffers(left, sizeof (left));
    memcpy(left0, left, sizeof (left));
    memcpy(left1, left, sizeof (left));

    if (check_func(c->add_hfyu_left_pred_bgr32, "add_hfyu_left_pred_bgr32")) {
        call_ref(dst0, src, BUF_SIZE, left0);
        call_new(dst1, src, BUF_SIZE, left1);

        if (memcmp(dst0, dst1, sizeof (dst0)) != 0 ||
            memcmp(left0, left1, sizeof (left0)) != 0) {
            fail();
        }

        bench_new(dst1, src, BUF_SIZE, left);
    }

    report("add_hfyu_left_pred_bgr32");
}

void checkasm_check_huffyuvdsp(void)
{
    HuffYUVDSPContext c;
    int width = 16 * av_clip(rnd(), 16, 128);

    ff_huffyuvdsp_init(&c, AV_PIX_FMT_YUV422P);

    /*! test width not multiple of mmsize */
    check_add_int16(&c, 65535, width, "add_int16_rnd_width");
    report("add_int16_rnd_width");

    /*! test always with the same size (for perf test) */
    check_add_int16(&c, 65535, 16*128, "add_int16_128");
    report("add_int16_128");

    check_add_hfyu_left_pred_bgr32(&c);
}
