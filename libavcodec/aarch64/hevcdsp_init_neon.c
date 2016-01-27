/*
 *  ARM NEON optimised HEVC for armv8 instruct functions
 *  Copyright (c) 2015 Junhai ZHANG <243186085@qq.com>
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

#include "libavutil/attributes.h"
#include "libavutil/aarch64/cpu.h"
#include "libavcodec/hevcdsp.h"
#include "hevcdsp_aarch64.h"

void ff_hevc_transform_4x4_neon_8(int16_t *coeffs, int col_limit);
void ff_hevc_transform_8x8_neon_8(int16_t *coeffs, int col_limit);
void ff_hevc_transform_16x16_neon_8(int16_t *coeffs, int col_limit);
void ff_hevc_transform_32x32_neon_8(int16_t *coeffs, int col_limit);
void ff_hevc_idct_4x4_dc_neon_8(int16_t *coeffs);
void ff_hevc_idct_8x8_dc_neon_8(int16_t *coeffs);
void ff_hevc_idct_16x16_dc_neon_8(int16_t *coeffs);
void ff_hevc_idct_32x32_dc_neon_8(int16_t *coeffs);
void ff_hevc_transform_luma_4x4_neon_8(int16_t *coeffs);
void ff_hevc_transform_add_4x4_neon_8(uint8_t *_dst, int16_t *coeffs,
                                      ptrdiff_t stride);
void ff_hevc_transform_add_8x8_neon_8(uint8_t *_dst, int16_t *coeffs,
                                      ptrdiff_t stride);
void ff_hevc_transform_add_16x16_neon_8(uint8_t *_dst, int16_t *coeffs,
                                      ptrdiff_t stride);
void ff_hevc_transform_add_32x32_neon_8(uint8_t *_dst, int16_t *coeffs,
                                      ptrdiff_t stride);
#define PUT_PIXELS(name) \
void name(int16_t *dst, uint8_t *src, ptrdiff_t srcstride, int height, \
intptr_t mx, intptr_t my, int width)
PUT_PIXELS(ff_hevc_put_pixels_w2_neon_8);
PUT_PIXELS(ff_hevc_put_pixels_w4_neon_8);
PUT_PIXELS(ff_hevc_put_pixels_w6_neon_8);
PUT_PIXELS(ff_hevc_put_pixels_w8_neon_8);
PUT_PIXELS(ff_hevc_put_pixels_w12_neon_8);
PUT_PIXELS(ff_hevc_put_pixels_w16_neon_8);
PUT_PIXELS(ff_hevc_put_pixels_w24_neon_8);
PUT_PIXELS(ff_hevc_put_pixels_w32_neon_8);
PUT_PIXELS(ff_hevc_put_pixels_w48_neon_8);
PUT_PIXELS(ff_hevc_put_pixels_w64_neon_8);
#undef PUT_PIXELS

static  void (*put_hevc_qpel_neon[4][4])(int16_t *dst, ptrdiff_t dststride, uint8_t *src, ptrdiff_t srcstride,
                                         int height, int width);
static  void (*put_hevc_qpel_uw_neon[4][4])(uint8_t *dst, ptrdiff_t dststride, uint8_t *src, ptrdiff_t srcstride,
                                         int height, int width, int16_t *src2, ptrdiff_t src2stride);
void ff_hevc_put_qpel_neon_wrapper(int16_t *dst, uint8_t *src, ptrdiff_t srcstride,
                                   int height, intptr_t mx, intptr_t my, int width);

void ff_hevc_put_qpel_uni_neon_wrapper(uint8_t *dst, ptrdiff_t dststride, uint8_t *src, ptrdiff_t srcstride,
                                   int height, intptr_t mx, intptr_t my, int width);
void ff_hevc_put_qpel_bi_neon_wrapper(uint8_t *dst, ptrdiff_t dststride, uint8_t *src, ptrdiff_t srcstride,
                                   int16_t *src2, int height, intptr_t mx, intptr_t my, int width);

#define QPEL_FUNC(name) \
    void name(int16_t *dst, ptrdiff_t dststride, uint8_t *src, ptrdiff_t srcstride, \
                int height, int width)
QPEL_FUNC(ff_hevc_put_qpel_v1_neon_8);
QPEL_FUNC(ff_hevc_put_qpel_v2_neon_8);
QPEL_FUNC(ff_hevc_put_qpel_v3_neon_8);
QPEL_FUNC(ff_hevc_put_qpel_h1_neon_8);
QPEL_FUNC(ff_hevc_put_qpel_h2_neon_8);
QPEL_FUNC(ff_hevc_put_qpel_h3_neon_8);
QPEL_FUNC(ff_hevc_put_qpel_h1v1_neon_8);
QPEL_FUNC(ff_hevc_put_qpel_h1v2_neon_8);
QPEL_FUNC(ff_hevc_put_qpel_h1v3_neon_8);
QPEL_FUNC(ff_hevc_put_qpel_h2v1_neon_8);
QPEL_FUNC(ff_hevc_put_qpel_h2v2_neon_8);
QPEL_FUNC(ff_hevc_put_qpel_h2v3_neon_8);
QPEL_FUNC(ff_hevc_put_qpel_h3v1_neon_8);
QPEL_FUNC(ff_hevc_put_qpel_h3v2_neon_8);
QPEL_FUNC(ff_hevc_put_qpel_h3v3_neon_8);
#undef QPEL_FUNC

#define QPEL_FUNC_UW(name) \
    void name(uint8_t *dst, ptrdiff_t dststride, uint8_t *src, ptrdiff_t srcstride, \
            int width, int height, int16_t* src2, ptrdiff_t src2stride)
QPEL_FUNC_UW(ff_hevc_put_qpel_uw_pixels_neon_8);
QPEL_FUNC_UW(ff_hevc_put_qpel_uw_v1_neon_8);
QPEL_FUNC_UW(ff_hevc_put_qpel_uw_v2_neon_8);
QPEL_FUNC_UW(ff_hevc_put_qpel_uw_v3_neon_8);
QPEL_FUNC_UW(ff_hevc_put_qpel_uw_h1_neon_8);
QPEL_FUNC_UW(ff_hevc_put_qpel_uw_h2_neon_8);
QPEL_FUNC_UW(ff_hevc_put_qpel_uw_h3_neon_8);
QPEL_FUNC_UW(ff_hevc_put_qpel_uw_h1v1_neon_8);
QPEL_FUNC_UW(ff_hevc_put_qpel_uw_h1v2_neon_8);
QPEL_FUNC_UW(ff_hevc_put_qpel_uw_h1v3_neon_8);
QPEL_FUNC_UW(ff_hevc_put_qpel_uw_h2v1_neon_8);
QPEL_FUNC_UW(ff_hevc_put_qpel_uw_h2v2_neon_8);
QPEL_FUNC_UW(ff_hevc_put_qpel_uw_h2v3_neon_8);
QPEL_FUNC_UW(ff_hevc_put_qpel_uw_h3v1_neon_8);
QPEL_FUNC_UW(ff_hevc_put_qpel_uw_h3v2_neon_8);
QPEL_FUNC_UW(ff_hevc_put_qpel_uw_h3v3_neon_8);
#undef QPEL_FUNC_UW

void ff_hevc_put_qpel_neon_wrapper(int16_t *dst, uint8_t *src, ptrdiff_t srcstride,
                                   int height, intptr_t mx, intptr_t my, int width){
    put_hevc_qpel_neon[my][mx](dst, MAX_PB_SIZE, src, srcstride, height, width);
}

void ff_hevc_put_qpel_uni_neon_wrapper(uint8_t *dst, ptrdiff_t dststride, uint8_t *src, ptrdiff_t srcstride,
                                   int height, intptr_t mx, intptr_t my, int width){
    put_hevc_qpel_uw_neon[my][mx](dst, dststride, src, srcstride, width, height, NULL, 0);
}

void ff_hevc_put_qpel_bi_neon_wrapper(uint8_t *dst, ptrdiff_t dststride, uint8_t *src, ptrdiff_t srcstride,
                                   int16_t *src2, int height, intptr_t mx, intptr_t my, int width){
    put_hevc_qpel_uw_neon[my][mx](dst, dststride, src, srcstride, width, height, src2, MAX_PB_SIZE);
}

#define QPEL_FUNC_UW_WEIGHT(name) \
void name(uint8_t *dst, ptrdiff_t dststride, uint8_t *_src, ptrdiff_t _srcstride, \
int width, int height, int16_t* src2, \
int denom, int wx0, int wx1, int ox0, int ox1);
QPEL_FUNC_UW_WEIGHT(ff_hevc_put_qpel_uw_weight_v1_neon_8);
QPEL_FUNC_UW_WEIGHT(ff_hevc_put_qpel_uw_weight_v2_neon_8);
QPEL_FUNC_UW_WEIGHT(ff_hevc_put_qpel_uw_weight_v3_neon_8);
QPEL_FUNC_UW_WEIGHT(ff_hevc_put_qpel_uw_weight_h1_neon_8);
QPEL_FUNC_UW_WEIGHT(ff_hevc_put_qpel_uw_weight_h2_neon_8);
QPEL_FUNC_UW_WEIGHT(ff_hevc_put_qpel_uw_weight_h3_neon_8);
QPEL_FUNC_UW_WEIGHT(ff_hevc_put_qpel_uw_weight_h1v1_neon_8);
QPEL_FUNC_UW_WEIGHT(ff_hevc_put_qpel_uw_weight_h1v2_neon_8);
QPEL_FUNC_UW_WEIGHT(ff_hevc_put_qpel_uw_weight_h1v3_neon_8);
QPEL_FUNC_UW_WEIGHT(ff_hevc_put_qpel_uw_weight_h2v1_neon_8);
QPEL_FUNC_UW_WEIGHT(ff_hevc_put_qpel_uw_weight_h2v2_neon_8);
QPEL_FUNC_UW_WEIGHT(ff_hevc_put_qpel_uw_weight_h2v3_neon_8);
QPEL_FUNC_UW_WEIGHT(ff_hevc_put_qpel_uw_weight_h3v1_neon_8);
QPEL_FUNC_UW_WEIGHT(ff_hevc_put_qpel_uw_weight_h3v2_neon_8);
QPEL_FUNC_UW_WEIGHT(ff_hevc_put_qpel_uw_weight_h3v3_neon_8);
#undef QPEL_FUNC_UW_WEIGHT

static void (*put_hevc_qpel_uw_weight_neon[4][4])(uint8_t *dst, ptrdiff_t dststride, uint8_t *_src, ptrdiff_t _srcstride,
                                                  int width, int height, int16_t* src2,
                                                  int denom, int wx0, int wx1, int ox0, int ox1);

static void ff_hevc_put_qpel_uni_weight_neon_wrapper(uint8_t *dst, ptrdiff_t dststride, uint8_t *src, ptrdiff_t srcstride,
                                                     int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width)
{
    put_hevc_qpel_uw_weight_neon[my][mx](dst, dststride, src, srcstride, width, height, NULL, denom, wx, 0, ox, 0);
}

static void ff_hevc_put_qpel_bi_weight_neon_wrapper(uint8_t *dst, ptrdiff_t dststride, uint8_t *src, ptrdiff_t srcstride,
                                                    int16_t *src2, int height, int denom, int wx0, int wx1, int ox0, int ox1,
                                                    intptr_t mx, intptr_t my, int width)
{
    put_hevc_qpel_uw_weight_neon[my][mx](dst, dststride, src, srcstride, width, height, src2, denom, wx0, wx1, ox0, ox1);
}

void ff_hevc_put_qpel_bi_w_neon_8(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *src1, ptrdiff_t _srcstride,
                                 int16_t *src2, int height, int denom, int wx0, int wx1,
                                 int ox0, int ox1, intptr_t mx, intptr_t my, int width);
void ff_hevc_put_qpel_uni_w_neon_8(uint8_t *_dst, ptrdiff_t _dststride, uint8_t *src1, ptrdiff_t _srcstride,
                                  int height, int denom, int wx0,
                                  int ox0, intptr_t mx, intptr_t my, int width);


av_cold void ff_hevcdsp_init_neon(HEVCDSPContext *c, const int bit_depth)
{
    if (bit_depth == 8) {
        int x;
        c->idct[0]                          = ff_hevc_transform_4x4_neon_8;
        c->idct[1]                          = ff_hevc_transform_8x8_neon_8;
        c->idct[2]                          = ff_hevc_transform_16x16_neon_8;
        c->idct[3]                          = ff_hevc_transform_32x32_neon_8;
        c->idct_dc[0]                       = ff_hevc_idct_4x4_dc_neon_8;
        c->idct_dc[1]                       = ff_hevc_idct_8x8_dc_neon_8;
        c->idct_dc[2]                       = ff_hevc_idct_16x16_dc_neon_8;
        c->idct_dc[3]                       = ff_hevc_idct_32x32_dc_neon_8;
        c->transform_add[0]                 = ff_hevc_transform_add_4x4_neon_8;
        c->transform_add[1]                 = ff_hevc_transform_add_8x8_neon_8;
        c->transform_add[2]                 = ff_hevc_transform_add_16x16_neon_8;
        c->transform_add[3]                 = ff_hevc_transform_add_32x32_neon_8;
        c->idct_4x4_luma                    = ff_hevc_transform_luma_4x4_neon_8;
        
        put_hevc_qpel_neon[1][0]            = ff_hevc_put_qpel_v1_neon_8;
        put_hevc_qpel_neon[2][0]            = ff_hevc_put_qpel_v2_neon_8;
        put_hevc_qpel_neon[3][0]            = ff_hevc_put_qpel_v3_neon_8;
        put_hevc_qpel_neon[0][1]            = ff_hevc_put_qpel_h1_neon_8;
        put_hevc_qpel_neon[0][2]            = ff_hevc_put_qpel_h2_neon_8;
        put_hevc_qpel_neon[0][3]            = ff_hevc_put_qpel_h3_neon_8;
        put_hevc_qpel_neon[1][1]            = ff_hevc_put_qpel_h1v1_neon_8;
        put_hevc_qpel_neon[1][2]            = ff_hevc_put_qpel_h2v1_neon_8;
        put_hevc_qpel_neon[1][3]            = ff_hevc_put_qpel_h3v1_neon_8;
        put_hevc_qpel_neon[2][1]            = ff_hevc_put_qpel_h1v2_neon_8;
        put_hevc_qpel_neon[2][2]            = ff_hevc_put_qpel_h2v2_neon_8;
        put_hevc_qpel_neon[2][3]            = ff_hevc_put_qpel_h3v2_neon_8;
        put_hevc_qpel_neon[3][1]            = ff_hevc_put_qpel_h1v3_neon_8;
        put_hevc_qpel_neon[3][2]            = ff_hevc_put_qpel_h2v3_neon_8;
        put_hevc_qpel_neon[3][3]            = ff_hevc_put_qpel_h3v3_neon_8;
        put_hevc_qpel_uw_neon[1][0]            = ff_hevc_put_qpel_uw_v1_neon_8;
        put_hevc_qpel_uw_neon[2][0]            = ff_hevc_put_qpel_uw_v2_neon_8;
        put_hevc_qpel_uw_neon[3][0]            = ff_hevc_put_qpel_uw_v3_neon_8;
        put_hevc_qpel_uw_neon[0][1]            = ff_hevc_put_qpel_uw_h1_neon_8;
        put_hevc_qpel_uw_neon[0][2]            = ff_hevc_put_qpel_uw_h2_neon_8;
        put_hevc_qpel_uw_neon[0][3]            = ff_hevc_put_qpel_uw_h3_neon_8;
        put_hevc_qpel_uw_neon[1][1]            = ff_hevc_put_qpel_uw_h1v1_neon_8;
        put_hevc_qpel_uw_neon[1][2]            = ff_hevc_put_qpel_uw_h2v1_neon_8;
        put_hevc_qpel_uw_neon[1][3]            = ff_hevc_put_qpel_uw_h3v1_neon_8;
        put_hevc_qpel_uw_neon[2][1]            = ff_hevc_put_qpel_uw_h1v2_neon_8;
        put_hevc_qpel_uw_neon[2][2]            = ff_hevc_put_qpel_uw_h2v2_neon_8;
        put_hevc_qpel_uw_neon[2][3]            = ff_hevc_put_qpel_uw_h3v2_neon_8;
        put_hevc_qpel_uw_neon[3][1]            = ff_hevc_put_qpel_uw_h1v3_neon_8;
        put_hevc_qpel_uw_neon[3][2]            = ff_hevc_put_qpel_uw_h2v3_neon_8;
        put_hevc_qpel_uw_neon[3][3]            = ff_hevc_put_qpel_uw_h3v3_neon_8;
        put_hevc_qpel_uw_weight_neon[1][0]      = ff_hevc_put_qpel_uw_weight_v1_neon_8;
        put_hevc_qpel_uw_weight_neon[2][0]      = ff_hevc_put_qpel_uw_weight_v2_neon_8;
        put_hevc_qpel_uw_weight_neon[3][0]      = ff_hevc_put_qpel_uw_weight_v3_neon_8;
        put_hevc_qpel_uw_weight_neon[0][1]      = ff_hevc_put_qpel_uw_weight_h1_neon_8;
        put_hevc_qpel_uw_weight_neon[0][2]      = ff_hevc_put_qpel_uw_weight_h2_neon_8;
        put_hevc_qpel_uw_weight_neon[0][3]      = ff_hevc_put_qpel_uw_weight_h3_neon_8;
        put_hevc_qpel_uw_weight_neon[1][1]      = ff_hevc_put_qpel_uw_weight_h1v1_neon_8;
        put_hevc_qpel_uw_weight_neon[1][2]      = ff_hevc_put_qpel_uw_weight_h2v1_neon_8;
        put_hevc_qpel_uw_weight_neon[1][3]      = ff_hevc_put_qpel_uw_weight_h3v1_neon_8;
        put_hevc_qpel_uw_weight_neon[2][1]      = ff_hevc_put_qpel_uw_weight_h1v2_neon_8;
        put_hevc_qpel_uw_weight_neon[2][2]      = ff_hevc_put_qpel_uw_weight_h2v2_neon_8;
        put_hevc_qpel_uw_weight_neon[2][3]      = ff_hevc_put_qpel_uw_weight_h3v2_neon_8;
        put_hevc_qpel_uw_weight_neon[3][1]      = ff_hevc_put_qpel_uw_weight_h1v3_neon_8;
        put_hevc_qpel_uw_weight_neon[3][2]      = ff_hevc_put_qpel_uw_weight_h2v3_neon_8;
        put_hevc_qpel_uw_weight_neon[3][3]      = ff_hevc_put_qpel_uw_weight_h3v3_neon_8;
        for (x = 0; x < 10; x++) {
            c->put_hevc_qpel[x][1][0]       = ff_hevc_put_qpel_neon_wrapper;
            c->put_hevc_qpel[x][0][1]       = ff_hevc_put_qpel_neon_wrapper;
            c->put_hevc_qpel[x][1][1]       = ff_hevc_put_qpel_neon_wrapper;
            c->put_hevc_qpel_uni[x][1][0]   = ff_hevc_put_qpel_uni_neon_wrapper;
            c->put_hevc_qpel_uni[x][0][1]   = ff_hevc_put_qpel_uni_neon_wrapper;
            c->put_hevc_qpel_uni[x][1][1]   = ff_hevc_put_qpel_uni_neon_wrapper;
            c->put_hevc_qpel_bi[x][1][0]    = ff_hevc_put_qpel_bi_neon_wrapper;
            c->put_hevc_qpel_bi[x][0][1]    = ff_hevc_put_qpel_bi_neon_wrapper;
            c->put_hevc_qpel_bi[x][1][1]    = ff_hevc_put_qpel_bi_neon_wrapper;
            c->put_hevc_qpel_uni_w[x][0][0] = ff_hevc_put_qpel_uni_w_neon_8;
            c->put_hevc_qpel_uni_w[x][1][0] = ff_hevc_put_qpel_uni_weight_neon_wrapper;
            c->put_hevc_qpel_uni_w[x][0][1] = ff_hevc_put_qpel_uni_weight_neon_wrapper;
            c->put_hevc_qpel_uni_w[x][1][1] = ff_hevc_put_qpel_uni_weight_neon_wrapper;
            c->put_hevc_qpel_bi_w[x][0][0]  = ff_hevc_put_qpel_bi_w_neon_8;
            c->put_hevc_qpel_bi_w[x][1][0]  = ff_hevc_put_qpel_bi_weight_neon_wrapper;
            c->put_hevc_qpel_bi_w[x][0][1]  = ff_hevc_put_qpel_bi_weight_neon_wrapper;
            c->put_hevc_qpel_bi_w[x][1][1]  = ff_hevc_put_qpel_bi_weight_neon_wrapper;
        }       
        c->put_hevc_qpel[0][0][0]  = ff_hevc_put_pixels_w2_neon_8;
        c->put_hevc_qpel[1][0][0]  = ff_hevc_put_pixels_w4_neon_8;
        c->put_hevc_qpel[2][0][0]  = ff_hevc_put_pixels_w6_neon_8;
        c->put_hevc_qpel[3][0][0]  = ff_hevc_put_pixels_w8_neon_8;
        c->put_hevc_qpel[4][0][0]  = ff_hevc_put_pixels_w12_neon_8;
        c->put_hevc_qpel[5][0][0]  = ff_hevc_put_pixels_w16_neon_8;
        c->put_hevc_qpel[6][0][0]  = ff_hevc_put_pixels_w24_neon_8;
        c->put_hevc_qpel[7][0][0]  = ff_hevc_put_pixels_w32_neon_8;
        c->put_hevc_qpel[8][0][0]  = ff_hevc_put_pixels_w48_neon_8;
        c->put_hevc_qpel[9][0][0]  = ff_hevc_put_pixels_w64_neon_8;
    }
}
