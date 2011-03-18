/**
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

#include <stdint.h>
#include "libavcodec/vp8dsp.h"

void ff_vp8_luma_dc_wht_neon(DCTELEM block[4][4][16], DCTELEM dc[16]);
void ff_vp8_luma_dc_wht_dc_neon(DCTELEM block[4][4][16], DCTELEM dc[16]);

void ff_vp8_idct_add_neon(uint8_t *dst, DCTELEM block[16], int stride);
void ff_vp8_idct_dc_add_neon(uint8_t *dst, DCTELEM block[16], int stride);
void ff_vp8_idct_dc_add4y_neon(uint8_t *dst, DCTELEM block[4][16], int stride);
void ff_vp8_idct_dc_add4uv_neon(uint8_t *dst, DCTELEM block[4][16], int stride);

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

void ff_vp8_v_loop_filter16_simple_neon(uint8_t *dst, int stride, int flim);
void ff_vp8_h_loop_filter16_simple_neon(uint8_t *dst, int stride, int flim);


#define VP8_MC(n)                                                       \
    void ff_put_vp8_##n##_neon(uint8_t *dst, int dststride,             \
                               uint8_t *src, int srcstride,             \
                               int h, int x, int y)

#define VP8_EPEL(w)                             \
    VP8_MC(pixels ## w);                        \
    VP8_MC(epel ## w ## _h4);                   \
    VP8_MC(epel ## w ## _h6);                   \
    VP8_MC(epel ## w ## _v4);                   \
    VP8_MC(epel ## w ## _h4v4);                 \
    VP8_MC(epel ## w ## _h6v4);                 \
    VP8_MC(epel ## w ## _v6);                   \
    VP8_MC(epel ## w ## _h4v6);                 \
    VP8_MC(epel ## w ## _h6v6)

VP8_EPEL(16);
VP8_EPEL(8);
VP8_EPEL(4);

VP8_MC(bilin16_h);
VP8_MC(bilin16_v);
VP8_MC(bilin16_hv);
VP8_MC(bilin8_h);
VP8_MC(bilin8_v);
VP8_MC(bilin8_hv);
VP8_MC(bilin4_h);
VP8_MC(bilin4_v);
VP8_MC(bilin4_hv);

av_cold void ff_vp8dsp_init_arm(VP8DSPContext *dsp)
{
    if (HAVE_NEON) {
        dsp->vp8_luma_dc_wht    = ff_vp8_luma_dc_wht_neon;
        dsp->vp8_luma_dc_wht_dc = ff_vp8_luma_dc_wht_dc_neon;

        dsp->vp8_idct_add       = ff_vp8_idct_add_neon;
        dsp->vp8_idct_dc_add    = ff_vp8_idct_dc_add_neon;
        dsp->vp8_idct_dc_add4y  = ff_vp8_idct_dc_add4y_neon;
        dsp->vp8_idct_dc_add4uv = ff_vp8_idct_dc_add4uv_neon;

        dsp->vp8_v_loop_filter16y = ff_vp8_v_loop_filter16_neon;
        dsp->vp8_h_loop_filter16y = ff_vp8_h_loop_filter16_neon;
        dsp->vp8_v_loop_filter8uv = ff_vp8_v_loop_filter8uv_neon;
        dsp->vp8_h_loop_filter8uv = ff_vp8_h_loop_filter8uv_neon;

        dsp->vp8_v_loop_filter16y_inner = ff_vp8_v_loop_filter16_inner_neon;
        dsp->vp8_h_loop_filter16y_inner = ff_vp8_h_loop_filter16_inner_neon;
        dsp->vp8_v_loop_filter8uv_inner = ff_vp8_v_loop_filter8uv_inner_neon;
        dsp->vp8_h_loop_filter8uv_inner = ff_vp8_h_loop_filter8uv_inner_neon;

        dsp->vp8_v_loop_filter_simple = ff_vp8_v_loop_filter16_simple_neon;
        dsp->vp8_h_loop_filter_simple = ff_vp8_h_loop_filter16_simple_neon;

        dsp->put_vp8_epel_pixels_tab[0][0][0] = ff_put_vp8_pixels16_neon;
        dsp->put_vp8_epel_pixels_tab[0][0][2] = ff_put_vp8_epel16_h6_neon;
        dsp->put_vp8_epel_pixels_tab[0][2][0] = ff_put_vp8_epel16_v6_neon;
        dsp->put_vp8_epel_pixels_tab[0][2][2] = ff_put_vp8_epel16_h6v6_neon;

        dsp->put_vp8_epel_pixels_tab[1][0][0] = ff_put_vp8_pixels8_neon;
        dsp->put_vp8_epel_pixels_tab[1][0][1] = ff_put_vp8_epel8_h4_neon;
        dsp->put_vp8_epel_pixels_tab[1][0][2] = ff_put_vp8_epel8_h6_neon;
        dsp->put_vp8_epel_pixels_tab[1][1][0] = ff_put_vp8_epel8_v4_neon;
        dsp->put_vp8_epel_pixels_tab[1][1][1] = ff_put_vp8_epel8_h4v4_neon;
        dsp->put_vp8_epel_pixels_tab[1][1][2] = ff_put_vp8_epel8_h6v4_neon;
        dsp->put_vp8_epel_pixels_tab[1][2][0] = ff_put_vp8_epel8_v6_neon;
        dsp->put_vp8_epel_pixels_tab[1][2][1] = ff_put_vp8_epel8_h4v6_neon;
        dsp->put_vp8_epel_pixels_tab[1][2][2] = ff_put_vp8_epel8_h6v6_neon;

        dsp->put_vp8_epel_pixels_tab[2][0][0] = ff_put_vp8_pixels4_neon;
        dsp->put_vp8_epel_pixels_tab[2][0][1] = ff_put_vp8_epel4_h4_neon;
        dsp->put_vp8_epel_pixels_tab[2][0][2] = ff_put_vp8_epel4_h6_neon;
        dsp->put_vp8_epel_pixels_tab[2][1][0] = ff_put_vp8_epel4_v4_neon;
        dsp->put_vp8_epel_pixels_tab[2][1][1] = ff_put_vp8_epel4_h4v4_neon;
        dsp->put_vp8_epel_pixels_tab[2][1][2] = ff_put_vp8_epel4_h6v4_neon;
        dsp->put_vp8_epel_pixels_tab[2][2][0] = ff_put_vp8_epel4_v6_neon;
        dsp->put_vp8_epel_pixels_tab[2][2][1] = ff_put_vp8_epel4_h4v6_neon;
        dsp->put_vp8_epel_pixels_tab[2][2][2] = ff_put_vp8_epel4_h6v6_neon;

        dsp->put_vp8_bilinear_pixels_tab[0][0][0] = ff_put_vp8_pixels16_neon;
        dsp->put_vp8_bilinear_pixels_tab[0][0][1] = ff_put_vp8_bilin16_h_neon;
        dsp->put_vp8_bilinear_pixels_tab[0][0][2] = ff_put_vp8_bilin16_h_neon;
        dsp->put_vp8_bilinear_pixels_tab[0][1][0] = ff_put_vp8_bilin16_v_neon;
        dsp->put_vp8_bilinear_pixels_tab[0][1][1] = ff_put_vp8_bilin16_hv_neon;
        dsp->put_vp8_bilinear_pixels_tab[0][1][2] = ff_put_vp8_bilin16_hv_neon;
        dsp->put_vp8_bilinear_pixels_tab[0][2][0] = ff_put_vp8_bilin16_v_neon;
        dsp->put_vp8_bilinear_pixels_tab[0][2][1] = ff_put_vp8_bilin16_hv_neon;
        dsp->put_vp8_bilinear_pixels_tab[0][2][2] = ff_put_vp8_bilin16_hv_neon;

        dsp->put_vp8_bilinear_pixels_tab[1][0][0] = ff_put_vp8_pixels8_neon;
        dsp->put_vp8_bilinear_pixels_tab[1][0][1] = ff_put_vp8_bilin8_h_neon;
        dsp->put_vp8_bilinear_pixels_tab[1][0][2] = ff_put_vp8_bilin8_h_neon;
        dsp->put_vp8_bilinear_pixels_tab[1][1][0] = ff_put_vp8_bilin8_v_neon;
        dsp->put_vp8_bilinear_pixels_tab[1][1][1] = ff_put_vp8_bilin8_hv_neon;
        dsp->put_vp8_bilinear_pixels_tab[1][1][2] = ff_put_vp8_bilin8_hv_neon;
        dsp->put_vp8_bilinear_pixels_tab[1][2][0] = ff_put_vp8_bilin8_v_neon;
        dsp->put_vp8_bilinear_pixels_tab[1][2][1] = ff_put_vp8_bilin8_hv_neon;
        dsp->put_vp8_bilinear_pixels_tab[1][2][2] = ff_put_vp8_bilin8_hv_neon;

        dsp->put_vp8_bilinear_pixels_tab[2][0][0] = ff_put_vp8_pixels4_neon;
        dsp->put_vp8_bilinear_pixels_tab[2][0][1] = ff_put_vp8_bilin4_h_neon;
        dsp->put_vp8_bilinear_pixels_tab[2][0][2] = ff_put_vp8_bilin4_h_neon;
        dsp->put_vp8_bilinear_pixels_tab[2][1][0] = ff_put_vp8_bilin4_v_neon;
        dsp->put_vp8_bilinear_pixels_tab[2][1][1] = ff_put_vp8_bilin4_hv_neon;
        dsp->put_vp8_bilinear_pixels_tab[2][1][2] = ff_put_vp8_bilin4_hv_neon;
        dsp->put_vp8_bilinear_pixels_tab[2][2][0] = ff_put_vp8_bilin4_v_neon;
        dsp->put_vp8_bilinear_pixels_tab[2][2][1] = ff_put_vp8_bilin4_hv_neon;
        dsp->put_vp8_bilinear_pixels_tab[2][2][2] = ff_put_vp8_bilin4_hv_neon;
    }
}
