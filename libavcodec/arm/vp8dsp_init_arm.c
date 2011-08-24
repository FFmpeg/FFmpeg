/**
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

#include <stdint.h>
#include "libavcodec/vp8dsp.h"

void ff_vp8_luma_dc_wht_dc_armv6(DCTELEM block[4][4][16], DCTELEM dc[16]);

#define idct_funcs(opt) \
void ff_vp8_luma_dc_wht_ ## opt(DCTELEM block[4][4][16], DCTELEM dc[16]); \
void ff_vp8_idct_add_ ## opt(uint8_t *dst, DCTELEM block[16], int stride); \
void ff_vp8_idct_dc_add_ ## opt(uint8_t *dst, DCTELEM block[16], int stride); \
void ff_vp8_idct_dc_add4y_ ## opt(uint8_t *dst, DCTELEM block[4][16], int stride); \
void ff_vp8_idct_dc_add4uv_ ## opt(uint8_t *dst, DCTELEM block[4][16], int stride)

idct_funcs(neon);
idct_funcs(armv6);

void ff_vp8_v_loop_filter16_neon(uint8_t *dst, int stride,
                                 int flim_E, int flim_I, int hev_thresh);
void ff_vp8_h_loop_filter16_neon(uint8_t *dst, int stride,
                                 int flim_E, int flim_I, int hev_thresh);
void ff_vp8_v_loop_filter8uv_neon(uint8_t *dstU, uint8_t *dstV, int stride,
                                  int flim_E, int flim_I, int hev_thresh);
void ff_vp8_h_loop_filter8uv_neon(uint8_t *dstU, uint8_t *dstV, int stride,
                                  int flim_E, int flim_I, int hev_thresh);

void ff_vp8_v_loop_filter16_inner_neon(uint8_t *dst, int stride,
                                       int flim_E, int flim_I, int hev_thresh);
void ff_vp8_h_loop_filter16_inner_neon(uint8_t *dst, int stride,
                                       int flim_E, int flim_I, int hev_thresh);
void ff_vp8_v_loop_filter8uv_inner_neon(uint8_t *dstU, uint8_t *dstV,
                                        int stride, int flim_E, int flim_I,
                                        int hev_thresh);
void ff_vp8_h_loop_filter8uv_inner_neon(uint8_t *dstU, uint8_t *dstV,
                                        int stride, int flim_E, int flim_I,
                                        int hev_thresh);

void ff_vp8_v_loop_filter_inner_armv6(uint8_t *dst, int stride,
                                      int flim_E, int flim_I,
                                      int hev_thresh, int count);
void ff_vp8_h_loop_filter_inner_armv6(uint8_t *dst, int stride,
                                      int flim_E, int flim_I,
                                      int hev_thresh, int count);
void ff_vp8_v_loop_filter_armv6(uint8_t *dst, int stride,
                                int flim_E, int flim_I,
                                int hev_thresh, int count);
void ff_vp8_h_loop_filter_armv6(uint8_t *dst, int stride,
                                int flim_E, int flim_I,
                                int hev_thresh, int count);

static void ff_vp8_v_loop_filter16_armv6(uint8_t *dst, int stride,
                                         int flim_E, int flim_I, int hev_thresh)
{
    ff_vp8_v_loop_filter_armv6(dst, stride, flim_E, flim_I, hev_thresh, 4);
}

static void ff_vp8_h_loop_filter16_armv6(uint8_t *dst, int stride,
                                         int flim_E, int flim_I, int hev_thresh)
{
    ff_vp8_h_loop_filter_armv6(dst, stride, flim_E, flim_I, hev_thresh, 4);
}

static void ff_vp8_v_loop_filter8uv_armv6(uint8_t *dstU, uint8_t *dstV, int stride,
                                          int flim_E, int flim_I, int hev_thresh)
{
    ff_vp8_v_loop_filter_armv6(dstU, stride, flim_E, flim_I, hev_thresh, 2);
    ff_vp8_v_loop_filter_armv6(dstV, stride, flim_E, flim_I, hev_thresh, 2);
}

static void ff_vp8_h_loop_filter8uv_armv6(uint8_t *dstU, uint8_t *dstV, int stride,
                                          int flim_E, int flim_I, int hev_thresh)
{
    ff_vp8_h_loop_filter_armv6(dstU, stride, flim_E, flim_I, hev_thresh, 2);
    ff_vp8_h_loop_filter_armv6(dstV, stride, flim_E, flim_I, hev_thresh, 2);
}

static void ff_vp8_v_loop_filter16_inner_armv6(uint8_t *dst, int stride,
                                               int flim_E, int flim_I, int hev_thresh)
{
    ff_vp8_v_loop_filter_inner_armv6(dst, stride, flim_E, flim_I, hev_thresh, 4);
}

static void ff_vp8_h_loop_filter16_inner_armv6(uint8_t *dst, int stride,
                                               int flim_E, int flim_I, int hev_thresh)
{
    ff_vp8_h_loop_filter_inner_armv6(dst, stride, flim_E, flim_I, hev_thresh, 4);
}

static void ff_vp8_v_loop_filter8uv_inner_armv6(uint8_t *dstU, uint8_t *dstV,
                                                int stride, int flim_E, int flim_I,
                                                int hev_thresh)
{
    ff_vp8_v_loop_filter_inner_armv6(dstU, stride, flim_E, flim_I, hev_thresh, 2);
    ff_vp8_v_loop_filter_inner_armv6(dstV, stride, flim_E, flim_I, hev_thresh, 2);
}

static void ff_vp8_h_loop_filter8uv_inner_armv6(uint8_t *dstU, uint8_t *dstV,
                                                int stride, int flim_E, int flim_I,
                                                int hev_thresh)
{
    ff_vp8_h_loop_filter_inner_armv6(dstU, stride, flim_E, flim_I, hev_thresh, 2);
    ff_vp8_h_loop_filter_inner_armv6(dstV, stride, flim_E, flim_I, hev_thresh, 2);
}

#define simple_lf_funcs(opt) \
void ff_vp8_v_loop_filter16_simple_ ## opt(uint8_t *dst, int stride, int flim); \
void ff_vp8_h_loop_filter16_simple_ ## opt(uint8_t *dst, int stride, int flim)

simple_lf_funcs(neon);
simple_lf_funcs(armv6);

#define VP8_MC_OPT(n, opt)                                              \
    void ff_put_vp8_##n##_##opt(uint8_t *dst, int dststride,            \
                                uint8_t *src, int srcstride,            \
                                int h, int x, int y)

#define VP8_MC(n) \
    VP8_MC_OPT(n, neon)

#define VP8_EPEL(w)                             \
    VP8_MC(epel ## w ## _h4);                   \
    VP8_MC(epel ## w ## _h6);                   \
    VP8_MC(epel ## w ## _h4v4);                 \
    VP8_MC(epel ## w ## _h6v4);                 \
    VP8_MC(epel ## w ## _v4);                   \
    VP8_MC(epel ## w ## _v6);                   \
    VP8_MC(epel ## w ## _h4v6);                 \
    VP8_MC(epel ## w ## _h6v6)

VP8_EPEL(16);
VP8_MC(pixels16);
VP8_MC_OPT(pixels16, armv6);
VP8_EPEL(8);
VP8_MC(pixels8);
VP8_MC_OPT(pixels8,  armv6);
VP8_EPEL(4);
VP8_MC_OPT(pixels4,  armv6);

VP8_MC(bilin16_h);
VP8_MC(bilin16_v);
VP8_MC(bilin16_hv);
VP8_MC(bilin8_h);
VP8_MC(bilin8_v);
VP8_MC(bilin8_hv);
VP8_MC(bilin4_h);
VP8_MC(bilin4_v);
VP8_MC(bilin4_hv);

#define VP8_V6_MC(n) \
void ff_put_vp8_##n##_armv6(uint8_t *dst, int dststride, uint8_t *src, \
                            int srcstride, int w, int h, int mxy)

VP8_V6_MC(epel_v6);
VP8_V6_MC(epel_h6);
VP8_V6_MC(epel_v4);
VP8_V6_MC(epel_h4);
VP8_V6_MC(bilin_v);
VP8_V6_MC(bilin_h);

#define VP8_EPEL_HV(SIZE, TAPNUMX, TAPNUMY, NAME, HNAME, VNAME, MAXHEIGHT) \
static void ff_put_vp8_##NAME##SIZE##_##HNAME##VNAME##_armv6( \
                                        uint8_t *dst, int dststride, uint8_t *src, \
                                        int srcstride, int h, int mx, int my) \
{ \
    DECLARE_ALIGNED(4, uint8_t, tmp)[SIZE * (MAXHEIGHT + TAPNUMY - 1)]; \
    uint8_t *tmpptr = tmp + SIZE * (TAPNUMY / 2 - 1); \
    src -= srcstride * (TAPNUMY / 2 - 1); \
    ff_put_vp8_ ## NAME ## _ ## HNAME ## _armv6(tmp, SIZE,      src,    srcstride, \
                                                SIZE, h + TAPNUMY - 1,  mx); \
    ff_put_vp8_ ## NAME ## _ ## VNAME ## _armv6(dst, dststride, tmpptr, SIZE, \
                                                SIZE, h,                my); \
}

VP8_EPEL_HV(16, 6, 6, epel,  h6, v6, 16);
VP8_EPEL_HV(16, 2, 2, bilin, h,  v,  16);
VP8_EPEL_HV(8,  6, 6, epel,  h6, v6, 16);
VP8_EPEL_HV(8,  4, 6, epel,  h4, v6, 16);
VP8_EPEL_HV(8,  6, 4, epel,  h6, v4, 16);
VP8_EPEL_HV(8,  4, 4, epel,  h4, v4, 16);
VP8_EPEL_HV(8,  2, 2, bilin, h,  v,  16);
VP8_EPEL_HV(4,  6, 6, epel,  h6, v6, 8);
VP8_EPEL_HV(4,  4, 6, epel,  h4, v6, 8);
VP8_EPEL_HV(4,  6, 4, epel,  h6, v4, 8);
VP8_EPEL_HV(4,  4, 4, epel,  h4, v4, 8);
VP8_EPEL_HV(4,  2, 2, bilin, h,  v,  8);

extern void put_vp8_epel4_v6_c(uint8_t *dst, int d, uint8_t *src, int s, int h, int mx, int my);
#undef printf
#define VP8_EPEL_H_OR_V(SIZE, NAME, HV) \
static void ff_put_vp8_##NAME##SIZE##_##HV##_armv6( \
                                        uint8_t *dst, int dststride, uint8_t *src, \
                                        int srcstride, int h, int mx, int my) \
{ \
    ff_put_vp8_## NAME ## _ ## HV ## _armv6(dst, dststride, src, srcstride, \
                                            SIZE, h, mx | my); \
}

VP8_EPEL_H_OR_V(4,  epel,  h6);
VP8_EPEL_H_OR_V(4,  epel,  h4);
VP8_EPEL_H_OR_V(4,  epel,  v6);
VP8_EPEL_H_OR_V(4,  epel,  v4);
VP8_EPEL_H_OR_V(4,  bilin, v);
VP8_EPEL_H_OR_V(4,  bilin, h);
VP8_EPEL_H_OR_V(8,  epel,  h6);
VP8_EPEL_H_OR_V(8,  epel,  h4);
VP8_EPEL_H_OR_V(8,  epel,  v6);
VP8_EPEL_H_OR_V(8,  epel,  v4);
VP8_EPEL_H_OR_V(8,  bilin, v);
VP8_EPEL_H_OR_V(8,  bilin, h);
VP8_EPEL_H_OR_V(16, epel,  h6);
VP8_EPEL_H_OR_V(16, epel,  v6);
VP8_EPEL_H_OR_V(16, bilin, v);
VP8_EPEL_H_OR_V(16, bilin, h);

av_cold void ff_vp8dsp_init_arm(VP8DSPContext *dsp)
{
#define set_func_ptrs(opt) \
        dsp->vp8_luma_dc_wht    = ff_vp8_luma_dc_wht_##opt; \
        dsp->vp8_luma_dc_wht_dc = ff_vp8_luma_dc_wht_dc_armv6; \
 \
        dsp->vp8_idct_add       = ff_vp8_idct_add_##opt; \
        dsp->vp8_idct_dc_add    = ff_vp8_idct_dc_add_##opt; \
        dsp->vp8_idct_dc_add4y  = ff_vp8_idct_dc_add4y_##opt; \
        dsp->vp8_idct_dc_add4uv = ff_vp8_idct_dc_add4uv_##opt; \
 \
        dsp->vp8_v_loop_filter16y = ff_vp8_v_loop_filter16_##opt; \
        dsp->vp8_h_loop_filter16y = ff_vp8_h_loop_filter16_##opt; \
        dsp->vp8_v_loop_filter8uv = ff_vp8_v_loop_filter8uv_##opt; \
        dsp->vp8_h_loop_filter8uv = ff_vp8_h_loop_filter8uv_##opt; \
 \
        dsp->vp8_v_loop_filter16y_inner = ff_vp8_v_loop_filter16_inner_##opt; \
        dsp->vp8_h_loop_filter16y_inner = ff_vp8_h_loop_filter16_inner_##opt; \
        dsp->vp8_v_loop_filter8uv_inner = ff_vp8_v_loop_filter8uv_inner_##opt; \
        dsp->vp8_h_loop_filter8uv_inner = ff_vp8_h_loop_filter8uv_inner_##opt; \
 \
        dsp->vp8_v_loop_filter_simple = ff_vp8_v_loop_filter16_simple_##opt; \
        dsp->vp8_h_loop_filter_simple = ff_vp8_h_loop_filter16_simple_##opt; \
 \
        dsp->put_vp8_epel_pixels_tab[0][0][0] = ff_put_vp8_pixels16_##opt; \
        dsp->put_vp8_epel_pixels_tab[0][0][2] = ff_put_vp8_epel16_h6_##opt; \
        dsp->put_vp8_epel_pixels_tab[0][2][0] = ff_put_vp8_epel16_v6_##opt; \
        dsp->put_vp8_epel_pixels_tab[0][2][2] = ff_put_vp8_epel16_h6v6_##opt; \
 \
        dsp->put_vp8_epel_pixels_tab[1][0][0] = ff_put_vp8_pixels8_##opt; \
        dsp->put_vp8_epel_pixels_tab[1][0][1] = ff_put_vp8_epel8_h4_##opt; \
        dsp->put_vp8_epel_pixels_tab[1][0][2] = ff_put_vp8_epel8_h6_##opt; \
        dsp->put_vp8_epel_pixels_tab[1][1][0] = ff_put_vp8_epel8_v4_##opt; \
        dsp->put_vp8_epel_pixels_tab[1][1][1] = ff_put_vp8_epel8_h4v4_##opt; \
        dsp->put_vp8_epel_pixels_tab[1][1][2] = ff_put_vp8_epel8_h6v4_##opt; \
        dsp->put_vp8_epel_pixels_tab[1][2][0] = ff_put_vp8_epel8_v6_##opt; \
        dsp->put_vp8_epel_pixels_tab[1][2][1] = ff_put_vp8_epel8_h4v6_##opt; \
        dsp->put_vp8_epel_pixels_tab[1][2][2] = ff_put_vp8_epel8_h6v6_##opt; \
 \
        dsp->put_vp8_epel_pixels_tab[2][0][0] = ff_put_vp8_pixels4_armv6; \
        dsp->put_vp8_epel_pixels_tab[2][0][1] = ff_put_vp8_epel4_h4_##opt; \
        dsp->put_vp8_epel_pixels_tab[2][0][2] = ff_put_vp8_epel4_h6_##opt; \
        dsp->put_vp8_epel_pixels_tab[2][1][0] = ff_put_vp8_epel4_v4_##opt; \
        dsp->put_vp8_epel_pixels_tab[2][1][1] = ff_put_vp8_epel4_h4v4_##opt; \
        dsp->put_vp8_epel_pixels_tab[2][1][2] = ff_put_vp8_epel4_h6v4_##opt; \
        dsp->put_vp8_epel_pixels_tab[2][2][0] = ff_put_vp8_epel4_v6_##opt; \
        dsp->put_vp8_epel_pixels_tab[2][2][1] = ff_put_vp8_epel4_h4v6_##opt; \
        dsp->put_vp8_epel_pixels_tab[2][2][2] = ff_put_vp8_epel4_h6v6_##opt; \
 \
        dsp->put_vp8_bilinear_pixels_tab[0][0][0] = ff_put_vp8_pixels16_##opt; \
        dsp->put_vp8_bilinear_pixels_tab[0][0][2] = ff_put_vp8_bilin16_h_##opt; \
        dsp->put_vp8_bilinear_pixels_tab[0][2][0] = ff_put_vp8_bilin16_v_##opt; \
        dsp->put_vp8_bilinear_pixels_tab[0][2][2] = ff_put_vp8_bilin16_hv_##opt; \
 \
        dsp->put_vp8_bilinear_pixels_tab[1][0][0] = ff_put_vp8_pixels8_##opt; \
        dsp->put_vp8_bilinear_pixels_tab[1][0][1] = ff_put_vp8_bilin8_h_##opt; \
        dsp->put_vp8_bilinear_pixels_tab[1][0][2] = ff_put_vp8_bilin8_h_##opt; \
        dsp->put_vp8_bilinear_pixels_tab[1][1][0] = ff_put_vp8_bilin8_v_##opt; \
        dsp->put_vp8_bilinear_pixels_tab[1][1][1] = ff_put_vp8_bilin8_hv_##opt; \
        dsp->put_vp8_bilinear_pixels_tab[1][1][2] = ff_put_vp8_bilin8_hv_##opt; \
        dsp->put_vp8_bilinear_pixels_tab[1][2][0] = ff_put_vp8_bilin8_v_##opt; \
        dsp->put_vp8_bilinear_pixels_tab[1][2][1] = ff_put_vp8_bilin8_hv_##opt; \
        dsp->put_vp8_bilinear_pixels_tab[1][2][2] = ff_put_vp8_bilin8_hv_##opt; \
 \
        dsp->put_vp8_bilinear_pixels_tab[2][0][0] = ff_put_vp8_pixels4_armv6; \
        dsp->put_vp8_bilinear_pixels_tab[2][0][1] = ff_put_vp8_bilin4_h_##opt; \
        dsp->put_vp8_bilinear_pixels_tab[2][0][2] = ff_put_vp8_bilin4_h_##opt; \
        dsp->put_vp8_bilinear_pixels_tab[2][1][0] = ff_put_vp8_bilin4_v_##opt; \
        dsp->put_vp8_bilinear_pixels_tab[2][1][1] = ff_put_vp8_bilin4_hv_##opt; \
        dsp->put_vp8_bilinear_pixels_tab[2][1][2] = ff_put_vp8_bilin4_hv_##opt; \
        dsp->put_vp8_bilinear_pixels_tab[2][2][0] = ff_put_vp8_bilin4_v_##opt; \
        dsp->put_vp8_bilinear_pixels_tab[2][2][1] = ff_put_vp8_bilin4_hv_##opt; \
        dsp->put_vp8_bilinear_pixels_tab[2][2][2] = ff_put_vp8_bilin4_hv_##opt
    if (HAVE_NEON) {
        set_func_ptrs(neon);
    } else if (HAVE_ARMV6) {
        set_func_ptrs(armv6);
    }
}
