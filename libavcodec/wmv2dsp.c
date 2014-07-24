/*
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/attributes.h"
#include "libavutil/common.h"
#include "avcodec.h"
#include "idctdsp.h"
#include "mathops.h"
#include "wmv2dsp.h"

#define W0 2048
#define W1 2841 /* 2048*sqrt (2)*cos (1*pi/16) */
#define W2 2676 /* 2048*sqrt (2)*cos (2*pi/16) */
#define W3 2408 /* 2048*sqrt (2)*cos (3*pi/16) */
#define W4 2048 /* 2048*sqrt (2)*cos (4*pi/16) */
#define W5 1609 /* 2048*sqrt (2)*cos (5*pi/16) */
#define W6 1108 /* 2048*sqrt (2)*cos (6*pi/16) */
#define W7 565  /* 2048*sqrt (2)*cos (7*pi/16) */

static void wmv2_idct_row(short * b)
{
    int s1, s2;
    int a0, a1, a2, a3, a4, a5, a6, a7;

    /* step 1 */
    a1 = W1 * b[1] + W7 * b[7];
    a7 = W7 * b[1] - W1 * b[7];
    a5 = W5 * b[5] + W3 * b[3];
    a3 = W3 * b[5] - W5 * b[3];
    a2 = W2 * b[2] + W6 * b[6];
    a6 = W6 * b[2] - W2 * b[6];
    a0 = W0 * b[0] + W0 * b[4];
    a4 = W0 * b[0] - W0 * b[4];

    /* step 2 */
    s1 = (181 * (a1 - a5 + a7 - a3) + 128) >> 8; // 1, 3, 5, 7
    s2 = (181 * (a1 - a5 - a7 + a3) + 128) >> 8;

    /* step 3 */
    b[0] = (a0 + a2 + a1 + a5 + (1 << 7)) >> 8;
    b[1] = (a4 + a6 + s1      + (1 << 7)) >> 8;
    b[2] = (a4 - a6 + s2      + (1 << 7)) >> 8;
    b[3] = (a0 - a2 + a7 + a3 + (1 << 7)) >> 8;
    b[4] = (a0 - a2 - a7 - a3 + (1 << 7)) >> 8;
    b[5] = (a4 - a6 - s2      + (1 << 7)) >> 8;
    b[6] = (a4 + a6 - s1      + (1 << 7)) >> 8;
    b[7] = (a0 + a2 - a1 - a5 + (1 << 7)) >> 8;
}

static void wmv2_idct_col(short * b)
{
    int s1, s2;
    int a0, a1, a2, a3, a4, a5, a6, a7;

    /* step 1, with extended precision */
    a1 = (W1 * b[8 * 1] + W7 * b[8 * 7] + 4) >> 3;
    a7 = (W7 * b[8 * 1] - W1 * b[8 * 7] + 4) >> 3;
    a5 = (W5 * b[8 * 5] + W3 * b[8 * 3] + 4) >> 3;
    a3 = (W3 * b[8 * 5] - W5 * b[8 * 3] + 4) >> 3;
    a2 = (W2 * b[8 * 2] + W6 * b[8 * 6] + 4) >> 3;
    a6 = (W6 * b[8 * 2] - W2 * b[8 * 6] + 4) >> 3;
    a0 = (W0 * b[8 * 0] + W0 * b[8 * 4]    ) >> 3;
    a4 = (W0 * b[8 * 0] - W0 * b[8 * 4]    ) >> 3;

    /* step 2 */
    s1 = (181 * (a1 - a5 + a7 - a3) + 128) >> 8;
    s2 = (181 * (a1 - a5 - a7 + a3) + 128) >> 8;

    /* step 3 */
    b[8 * 0] = (a0 + a2 + a1 + a5 + (1 << 13)) >> 14;
    b[8 * 1] = (a4 + a6 + s1      + (1 << 13)) >> 14;
    b[8 * 2] = (a4 - a6 + s2      + (1 << 13)) >> 14;
    b[8 * 3] = (a0 - a2 + a7 + a3 + (1 << 13)) >> 14;

    b[8 * 4] = (a0 - a2 - a7 - a3 + (1 << 13)) >> 14;
    b[8 * 5] = (a4 - a6 - s2      + (1 << 13)) >> 14;
    b[8 * 6] = (a4 + a6 - s1      + (1 << 13)) >> 14;
    b[8 * 7] = (a0 + a2 - a1 - a5 + (1 << 13)) >> 14;
}

static void wmv2_idct_add_c(uint8_t *dest, int line_size, int16_t *block)
{
    int i;

    for (i = 0; i < 64; i += 8)
        wmv2_idct_row(block + i);
    for (i = 0; i < 8; i++)
        wmv2_idct_col(block + i);

    for (i = 0; i < 8; i++) {
        dest[0] = av_clip_uint8(dest[0] + block[0]);
        dest[1] = av_clip_uint8(dest[1] + block[1]);
        dest[2] = av_clip_uint8(dest[2] + block[2]);
        dest[3] = av_clip_uint8(dest[3] + block[3]);
        dest[4] = av_clip_uint8(dest[4] + block[4]);
        dest[5] = av_clip_uint8(dest[5] + block[5]);
        dest[6] = av_clip_uint8(dest[6] + block[6]);
        dest[7] = av_clip_uint8(dest[7] + block[7]);
        dest += line_size;
        block += 8;
    }
}

static void wmv2_idct_put_c(uint8_t *dest, int line_size, int16_t *block)
{
    int i;

    for (i = 0; i < 64; i += 8)
        wmv2_idct_row(block + i);
    for (i = 0; i < 8; i++)
        wmv2_idct_col(block + i);

    for (i = 0; i < 8; i++) {
        dest[0] = av_clip_uint8(block[0]);
        dest[1] = av_clip_uint8(block[1]);
        dest[2] = av_clip_uint8(block[2]);
        dest[3] = av_clip_uint8(block[3]);
        dest[4] = av_clip_uint8(block[4]);
        dest[5] = av_clip_uint8(block[5]);
        dest[6] = av_clip_uint8(block[6]);
        dest[7] = av_clip_uint8(block[7]);
        dest += line_size;
        block += 8;
    }
}

static void wmv2_mspel8_h_lowpass(uint8_t *dst, const uint8_t *src,
                                  int dstStride, int srcStride, int h)
{
    const uint8_t *cm = ff_crop_tab + MAX_NEG_CROP;
    int i;

    for (i = 0; i < h; i++) {
        dst[0] = cm[(9 * (src[0] + src[1]) - (src[-1] + src[2]) + 8) >> 4];
        dst[1] = cm[(9 * (src[1] + src[2]) - (src[0]  + src[3]) + 8) >> 4];
        dst[2] = cm[(9 * (src[2] + src[3]) - (src[1]  + src[4]) + 8) >> 4];
        dst[3] = cm[(9 * (src[3] + src[4]) - (src[2]  + src[5]) + 8) >> 4];
        dst[4] = cm[(9 * (src[4] + src[5]) - (src[3]  + src[6]) + 8) >> 4];
        dst[5] = cm[(9 * (src[5] + src[6]) - (src[4]  + src[7]) + 8) >> 4];
        dst[6] = cm[(9 * (src[6] + src[7]) - (src[5]  + src[8]) + 8) >> 4];
        dst[7] = cm[(9 * (src[7] + src[8]) - (src[6]  + src[9]) + 8) >> 4];
        dst   += dstStride;
        src   += srcStride;
    }
}

static void wmv2_mspel8_v_lowpass(uint8_t *dst, const uint8_t *src,
                                  int dstStride, int srcStride, int w)
{
    const uint8_t *cm = ff_crop_tab + MAX_NEG_CROP;
    int i;

    for (i = 0; i < w; i++) {
        const int src_1 = src[-srcStride];
        const int src0  = src[0];
        const int src1  = src[srcStride];
        const int src2  = src[2 * srcStride];
        const int src3  = src[3 * srcStride];
        const int src4  = src[4 * srcStride];
        const int src5  = src[5 * srcStride];
        const int src6  = src[6 * srcStride];
        const int src7  = src[7 * srcStride];
        const int src8  = src[8 * srcStride];
        const int src9  = src[9 * srcStride];
        dst[0 * dstStride] = cm[(9 * (src0 + src1) - (src_1 + src2) + 8) >> 4];
        dst[1 * dstStride] = cm[(9 * (src1 + src2) - (src0  + src3) + 8) >> 4];
        dst[2 * dstStride] = cm[(9 * (src2 + src3) - (src1  + src4) + 8) >> 4];
        dst[3 * dstStride] = cm[(9 * (src3 + src4) - (src2  + src5) + 8) >> 4];
        dst[4 * dstStride] = cm[(9 * (src4 + src5) - (src3  + src6) + 8) >> 4];
        dst[5 * dstStride] = cm[(9 * (src5 + src6) - (src4  + src7) + 8) >> 4];
        dst[6 * dstStride] = cm[(9 * (src6 + src7) - (src5  + src8) + 8) >> 4];
        dst[7 * dstStride] = cm[(9 * (src7 + src8) - (src6  + src9) + 8) >> 4];
        src++;
        dst++;
    }
}

static void put_mspel8_mc10_c(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)
{
    uint8_t half[64];

    wmv2_mspel8_h_lowpass(half, src, 8, stride, 8);
    ff_put_pixels8_l2_8(dst, src, half, stride, stride, 8, 8);
}

static void put_mspel8_mc20_c(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)
{
    wmv2_mspel8_h_lowpass(dst, src, stride, stride, 8);
}

static void put_mspel8_mc30_c(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)
{
    uint8_t half[64];

    wmv2_mspel8_h_lowpass(half, src, 8, stride, 8);
    ff_put_pixels8_l2_8(dst, src + 1, half, stride, stride, 8, 8);
}

static void put_mspel8_mc02_c(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)
{
    wmv2_mspel8_v_lowpass(dst, src, stride, stride, 8);
}

static void put_mspel8_mc12_c(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)
{
    uint8_t halfH[88];
    uint8_t halfV[64];
    uint8_t halfHV[64];

    wmv2_mspel8_h_lowpass(halfH, src - stride, 8, stride, 11);
    wmv2_mspel8_v_lowpass(halfV, src, 8, stride, 8);
    wmv2_mspel8_v_lowpass(halfHV, halfH + 8, 8, 8, 8);
    ff_put_pixels8_l2_8(dst, halfV, halfHV, stride, 8, 8, 8);
}

static void put_mspel8_mc32_c(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)
{
    uint8_t halfH[88];
    uint8_t halfV[64];
    uint8_t halfHV[64];

    wmv2_mspel8_h_lowpass(halfH, src - stride, 8, stride, 11);
    wmv2_mspel8_v_lowpass(halfV, src + 1, 8, stride, 8);
    wmv2_mspel8_v_lowpass(halfHV, halfH + 8, 8, 8, 8);
    ff_put_pixels8_l2_8(dst, halfV, halfHV, stride, 8, 8, 8);
}

static void put_mspel8_mc22_c(uint8_t *dst, const uint8_t *src, ptrdiff_t stride)
{
    uint8_t halfH[88];

    wmv2_mspel8_h_lowpass(halfH, src - stride, 8, stride, 11);
    wmv2_mspel8_v_lowpass(dst, halfH + 8, stride, 8, 8);
}

av_cold void ff_wmv2dsp_init(WMV2DSPContext *c)
{
    c->idct_add  = wmv2_idct_add_c;
    c->idct_put  = wmv2_idct_put_c;
    c->idct_perm = FF_IDCT_PERM_NONE;

    c->put_mspel_pixels_tab[0] = ff_put_pixels8x8_c;
    c->put_mspel_pixels_tab[1] = put_mspel8_mc10_c;
    c->put_mspel_pixels_tab[2] = put_mspel8_mc20_c;
    c->put_mspel_pixels_tab[3] = put_mspel8_mc30_c;
    c->put_mspel_pixels_tab[4] = put_mspel8_mc02_c;
    c->put_mspel_pixels_tab[5] = put_mspel8_mc12_c;
    c->put_mspel_pixels_tab[6] = put_mspel8_mc22_c;
    c->put_mspel_pixels_tab[7] = put_mspel8_mc32_c;
}
