/*
 * Copyright (c) 2016 Ronald S. Bultje <rsbultje@gmail.com>
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

#include <string.h>
#include "checkasm.h"
#include "libavfilter/colorspacedsp.h"
#include "libavutil/common.h"
#include "libavutil/internal.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem_internal.h"

#define W 64
#define H 64

#define randomize_buffers()                     \
    do {                                        \
        unsigned mask = bpp_mask[idepth];       \
        int n, m;                               \
        int bpp = 1 + (!!idepth);               \
        int buf_size = W * H * bpp;             \
        for (m = 0; m < 3; m++) {               \
            int ss = m ? ss_w + ss_h : 0;       \
            int plane_sz = buf_size >> ss;      \
            for (n = 0; n < plane_sz; n += 4) { \
                unsigned r = rnd() & mask;      \
                AV_WN32A(&src[m][n], r);        \
            }                                   \
        }                                       \
    } while (0)

static const char *format_string[] = {
    "444", "422", "420"
};

static const unsigned bpp_mask[] = { 0xffffffff, 0x03ff03ff, 0x0fff0fff };

static void check_yuv2yuv(void)
{
    declare_func(void, uint8_t *dst[3], ptrdiff_t dst_stride[3],
                 uint8_t *src[3], ptrdiff_t src_stride[3],
                 int w, int h, const int16_t coeff[3][3][8],
                 const int16_t off[2][8]);
    ColorSpaceDSPContext dsp;
    int idepth, odepth, fmt, n;
    LOCAL_ALIGNED_32(uint8_t, src_y, [W * H * 2]);
    LOCAL_ALIGNED_32(uint8_t, src_u, [W * H * 2]);
    LOCAL_ALIGNED_32(uint8_t, src_v, [W * H * 2]);
    uint8_t *src[3] = { src_y, src_u, src_v };
    LOCAL_ALIGNED_32(uint8_t, dst0_y, [W * H * 2]);
    LOCAL_ALIGNED_32(uint8_t, dst0_u, [W * H * 2]);
    LOCAL_ALIGNED_32(uint8_t, dst0_v, [W * H * 2]);
    LOCAL_ALIGNED_32(uint8_t, dst1_y, [W * H * 2]);
    LOCAL_ALIGNED_32(uint8_t, dst1_u, [W * H * 2]);
    LOCAL_ALIGNED_32(uint8_t, dst1_v, [W * H * 2]);
    uint8_t *dst0[3] = { dst0_y, dst0_u, dst0_v }, *dst1[3] = { dst1_y, dst1_u, dst1_v };
    LOCAL_ALIGNED_32(int16_t, offset_buf, [16]);
    LOCAL_ALIGNED_32(int16_t, coeff_buf, [3 * 3 * 8]);
    int16_t (*offset)[8] = (int16_t(*)[8]) offset_buf;
    int16_t (*coeff)[3][8] = (int16_t(*)[3][8]) coeff_buf;

    ff_colorspacedsp_init(&dsp);
    for (n = 0; n < 8; n++) {
        offset[0][n] = offset[1][n] = 16;

        coeff[0][0][n] = (1 << 14) + (1 << 7) + 1;
        coeff[0][1][n] = (1 << 7) - 1;
        coeff[0][2][n] = -(1 << 8);
        coeff[1][0][n] = coeff[2][0][n] = 0;
        coeff[1][1][n] = (1 << 14) + (1 << 7);
        coeff[1][2][n] = -(1 << 7);
        coeff[2][2][n] = (1 << 14) - (1 << 6);
        coeff[2][1][n] = 1 << 6;
    }
    for (idepth = 0; idepth < 3; idepth++) {
        for (odepth = 0; odepth < 3; odepth++) {
            for (fmt = 0; fmt < 3; fmt++) {
                if (check_func(dsp.yuv2yuv[idepth][odepth][fmt],
                               "ff_colorspacedsp_yuv2yuv_%sp%dto%d",
                               format_string[fmt],
                               idepth * 2 + 8, odepth * 2 + 8)) {
                    int ss_w = !!fmt, ss_h = fmt == 2;
                    int y_src_stride = W << !!idepth, y_dst_stride = W << !!odepth;
                    int uv_src_stride = y_src_stride >> ss_w, uv_dst_stride = y_dst_stride >> ss_w;

                    randomize_buffers();
                    call_ref(dst0, (ptrdiff_t[3]) { y_dst_stride, uv_dst_stride, uv_dst_stride },
                             src, (ptrdiff_t[3]) { y_src_stride, uv_src_stride, uv_src_stride },
                             W, H, coeff, offset);
                    call_new(dst1, (ptrdiff_t[3]) { y_dst_stride, uv_dst_stride, uv_dst_stride },
                             src, (ptrdiff_t[3]) { y_src_stride, uv_src_stride, uv_src_stride },
                             W, H, coeff, offset);
                    if (memcmp(dst0[0], dst1[0], y_dst_stride * H) ||
                        memcmp(dst0[1], dst1[1], uv_dst_stride * H >> ss_h) ||
                        memcmp(dst0[2], dst1[2], uv_dst_stride * H >> ss_h)) {
                        fail();
                    }
                }
            }
        }
    }

    report("yuv2yuv");
}

static void check_yuv2rgb(void)
{
    declare_func(void, int16_t *dst[3], ptrdiff_t dst_stride,
                 uint8_t *src[3], ptrdiff_t src_stride[3],
                 int w, int h, const int16_t coeff[3][3][8],
                 const int16_t off[8]);
    ColorSpaceDSPContext dsp;
    int idepth, fmt, n;
    LOCAL_ALIGNED_32(uint8_t, src_y, [W * H * 2]);
    LOCAL_ALIGNED_32(uint8_t, src_u, [W * H * 2]);
    LOCAL_ALIGNED_32(uint8_t, src_v, [W * H * 2]);
    uint8_t *src[3] = { src_y, src_u, src_v };
    LOCAL_ALIGNED_32(int16_t, dst0_y, [W * H]);
    LOCAL_ALIGNED_32(int16_t, dst0_u, [W * H]);
    LOCAL_ALIGNED_32(int16_t, dst0_v, [W * H]);
    LOCAL_ALIGNED_32(int16_t, dst1_y, [W * H]);
    LOCAL_ALIGNED_32(int16_t, dst1_u, [W * H]);
    LOCAL_ALIGNED_32(int16_t, dst1_v, [W * H]);
    int16_t *dst0[3] = { dst0_y, dst0_u, dst0_v }, *dst1[3] = { dst1_y, dst1_u, dst1_v };
    LOCAL_ALIGNED_32(int16_t, offset, [8]);
    LOCAL_ALIGNED_32(int16_t, coeff_buf, [3 * 3 * 8]);
    int16_t (*coeff)[3][8] = (int16_t(*)[3][8]) coeff_buf;

    ff_colorspacedsp_init(&dsp);
    for (n = 0; n < 8; n++) {
        offset[n] = 16;

        coeff[0][0][n] = coeff[1][0][n] = coeff[2][0][n] = (1 << 14) | 1;
        coeff[0][1][n] = coeff[2][2][n] = 0;
        coeff[0][2][n] = 1 << 13;
        coeff[1][1][n] = -(1 << 12);
        coeff[1][2][n] = 1 << 12;
        coeff[2][1][n] = 1 << 11;
    }
    for (idepth = 0; idepth < 3; idepth++) {
        for (fmt = 0; fmt < 3; fmt++) {
            if (check_func(dsp.yuv2rgb[idepth][fmt],
                           "ff_colorspacedsp_yuv2rgb_%sp%d",
                           format_string[fmt], idepth * 2 + 8)) {
                int ss_w = !!fmt, ss_h = fmt == 2;
                int y_src_stride = W << !!idepth;
                int uv_src_stride = y_src_stride >> ss_w;

                randomize_buffers();
                call_ref(dst0, W, src,
                         (ptrdiff_t[3]) { y_src_stride, uv_src_stride, uv_src_stride },
                         W, H, coeff, offset);
                call_new(dst1, W, src,
                         (ptrdiff_t[3]) { y_src_stride, uv_src_stride, uv_src_stride },
                         W, H, coeff, offset);
                if (memcmp(dst0[0], dst1[0], W * H * sizeof(int16_t)) ||
                    memcmp(dst0[1], dst1[1], W * H * sizeof(int16_t)) ||
                    memcmp(dst0[2], dst1[2], W * H * sizeof(int16_t))) {
                    fail();
                }
            }
        }
    }

    report("yuv2rgb");
}

#undef randomize_buffers
#define randomize_buffers()                     \
    do {                                        \
        int y, x, p;                            \
        for (p = 0; p < 3; p++) {               \
            for (y = 0; y < H; y++) {           \
                for (x = 0; x < W; x++) {       \
                    int r = rnd() & 0x7fff;     \
                    r -= (32768 - 28672) >> 1;  \
                    src[p][y * W + x] = r;      \
                }                               \
            }                                   \
        }                                       \
    } while (0)

static void check_rgb2yuv(void)
{
    declare_func(void, uint8_t *dst[3], ptrdiff_t dst_stride[3],
                 int16_t *src[3], ptrdiff_t src_stride,
                 int w, int h, const int16_t coeff[3][3][8],
                 const int16_t off[8]);
    ColorSpaceDSPContext dsp;
    int odepth, fmt, n;
    LOCAL_ALIGNED_32(int16_t, src_y, [W * H * 2]);
    LOCAL_ALIGNED_32(int16_t, src_u, [W * H * 2]);
    LOCAL_ALIGNED_32(int16_t, src_v, [W * H * 2]);
    int16_t *src[3] = { src_y, src_u, src_v };
    LOCAL_ALIGNED_32(uint8_t, dst0_y, [W * H * 2]);
    LOCAL_ALIGNED_32(uint8_t, dst0_u, [W * H * 2]);
    LOCAL_ALIGNED_32(uint8_t, dst0_v, [W * H * 2]);
    LOCAL_ALIGNED_32(uint8_t, dst1_y, [W * H * 2]);
    LOCAL_ALIGNED_32(uint8_t, dst1_u, [W * H * 2]);
    LOCAL_ALIGNED_32(uint8_t, dst1_v, [W * H * 2]);
    uint8_t *dst0[3] = { dst0_y, dst0_u, dst0_v }, *dst1[3] = { dst1_y, dst1_u, dst1_v };
    LOCAL_ALIGNED_32(int16_t, offset, [8]);
    LOCAL_ALIGNED_32(int16_t, coeff_buf, [3 * 3 * 8]);
    int16_t (*coeff)[3][8] = (int16_t(*)[3][8]) coeff_buf;

    ff_colorspacedsp_init(&dsp);
    for (n = 0; n < 8; n++) {
        offset[n] = 16;

        // these somewhat resemble bt601/smpte170m coefficients
        coeff[0][0][n] = lrint(0.3 * (1 << 14));
        coeff[0][1][n] = lrint(0.6 * (1 << 14));
        coeff[0][2][n] = lrint(0.1 * (1 << 14));
        coeff[1][0][n] = lrint(-0.15 * (1 << 14));
        coeff[1][1][n] = lrint(-0.35 * (1 << 14));
        coeff[1][2][n] = lrint(0.5 * (1 << 14));
        coeff[2][0][n] = lrint(0.5 * (1 << 14));
        coeff[2][1][n] = lrint(-0.42 * (1 << 14));
        coeff[2][2][n] = lrint(-0.08 * (1 << 14));
    }
    for (odepth = 0; odepth < 3; odepth++) {
        for (fmt = 0; fmt < 3; fmt++) {
            if (check_func(dsp.rgb2yuv[odepth][fmt],
                           "ff_colorspacedsp_rgb2yuv_%sp%d",
                           format_string[fmt], odepth * 2 + 8)) {
                int ss_w = !!fmt, ss_h = fmt == 2;
                int y_dst_stride = W << !!odepth;
                int uv_dst_stride = y_dst_stride >> ss_w;

                randomize_buffers();
                call_ref(dst0, (ptrdiff_t[3]) { y_dst_stride, uv_dst_stride, uv_dst_stride },
                         src, W, W, H, coeff, offset);
                call_new(dst1, (ptrdiff_t[3]) { y_dst_stride, uv_dst_stride, uv_dst_stride },
                         src, W, W, H, coeff, offset);
                if (memcmp(dst0[0], dst1[0], H * y_dst_stride) ||
                    memcmp(dst0[1], dst1[1], H * uv_dst_stride >> ss_h) ||
                    memcmp(dst0[2], dst1[2], H * uv_dst_stride >> ss_h)) {
                    fail();
                }
            }
        }
    }

    report("rgb2yuv");
}

static void check_multiply3x3(void)
{
    declare_func(void, int16_t *data[3], ptrdiff_t stride,
                 int w, int h, const int16_t coeff[3][3][8]);
    ColorSpaceDSPContext dsp;
    LOCAL_ALIGNED_32(int16_t, dst0_y, [W * H]);
    LOCAL_ALIGNED_32(int16_t, dst0_u, [W * H]);
    LOCAL_ALIGNED_32(int16_t, dst0_v, [W * H]);
    LOCAL_ALIGNED_32(int16_t, dst1_y, [W * H]);
    LOCAL_ALIGNED_32(int16_t, dst1_u, [W * H]);
    LOCAL_ALIGNED_32(int16_t, dst1_v, [W * H]);
    int16_t *dst0[3] = { dst0_y, dst0_u, dst0_v }, *dst1[3] = { dst1_y, dst1_u, dst1_v };
    int16_t **src = dst0;
    LOCAL_ALIGNED_32(int16_t, coeff_buf, [3 * 3 * 8]);
    int16_t (*coeff)[3][8] = (int16_t(*)[3][8]) coeff_buf;
    int n;

    ff_colorspacedsp_init(&dsp);
    for (n = 0; n < 8; n++) {
        coeff[0][0][n] = lrint(0.85 * (1 << 14));
        coeff[0][1][n] = lrint(0.10 * (1 << 14));
        coeff[0][2][n] = lrint(0.05 * (1 << 14));
        coeff[1][0][n] = lrint(-0.1 * (1 << 14));
        coeff[1][1][n] = lrint(0.95 * (1 << 14));
        coeff[1][2][n] = lrint(0.15 * (1 << 14));
        coeff[2][0][n] = lrint(-0.2 * (1 << 14));
        coeff[2][1][n] = lrint(0.30 * (1 << 14));
        coeff[2][2][n] = lrint(0.90 * (1 << 14));
    }
    if (check_func(dsp.multiply3x3, "ff_colorspacedsp_multiply3x3")) {
        randomize_buffers();
        memcpy(dst1_y, dst0_y, W * H * sizeof(*dst1_y));
        memcpy(dst1_u, dst0_u, W * H * sizeof(*dst1_u));
        memcpy(dst1_v, dst0_v, W * H * sizeof(*dst1_v));
        call_ref(dst0, W, W, H, coeff);
        call_new(dst1, W, W, H, coeff);
        if (memcmp(dst0[0], dst1[0], H * W * sizeof(*dst0_y)) ||
            memcmp(dst0[1], dst1[1], H * W * sizeof(*dst0_u)) ||
            memcmp(dst0[2], dst1[2], H * W * sizeof(*dst0_v))) {
            fail();
        }
    }

    report("multiply3x3");
}

void checkasm_check_colorspace(void)
{
    check_yuv2yuv();
    check_yuv2rgb();
    check_rgb2yuv();
    check_multiply3x3();
}
