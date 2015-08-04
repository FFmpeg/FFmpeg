/*
 * Copyright (c) 2015 Manojkumar Bhosale (Manojkumar.Bhosale@imgtec.com)
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

/**
 * @file
 * VP8 compatible video decoder
 */

#include "config.h"
#include "libavutil/attributes.h"
#include "libavcodec/vp8dsp.h"
#include "vp8dsp_mips.h"

#define VP8_MC_MIPS_FUNC(IDX, SIZE)            \
    dsp->put_vp8_epel_pixels_tab[IDX][0][1] =  \
        ff_put_vp8_epel##SIZE##_h4_msa;        \
    dsp->put_vp8_epel_pixels_tab[IDX][0][2] =  \
        ff_put_vp8_epel##SIZE##_h6_msa;        \
    dsp->put_vp8_epel_pixels_tab[IDX][1][0] =  \
        ff_put_vp8_epel##SIZE##_v4_msa;        \
    dsp->put_vp8_epel_pixels_tab[IDX][1][1] =  \
        ff_put_vp8_epel##SIZE##_h4v4_msa;      \
    dsp->put_vp8_epel_pixels_tab[IDX][1][2] =  \
        ff_put_vp8_epel##SIZE##_h6v4_msa;      \
    dsp->put_vp8_epel_pixels_tab[IDX][2][0] =  \
        ff_put_vp8_epel##SIZE##_v6_msa;        \
    dsp->put_vp8_epel_pixels_tab[IDX][2][1] =  \
        ff_put_vp8_epel##SIZE##_h4v6_msa;      \
    dsp->put_vp8_epel_pixels_tab[IDX][2][2] =  \
        ff_put_vp8_epel##SIZE##_h6v6_msa

#define VP8_BILINEAR_MC_MIPS_FUNC(IDX, SIZE)       \
    dsp->put_vp8_bilinear_pixels_tab[IDX][0][1] =  \
        ff_put_vp8_bilinear##SIZE##_h_msa;         \
    dsp->put_vp8_bilinear_pixels_tab[IDX][0][2] =  \
        ff_put_vp8_bilinear##SIZE##_h_msa;         \
    dsp->put_vp8_bilinear_pixels_tab[IDX][1][0] =  \
        ff_put_vp8_bilinear##SIZE##_v_msa;         \
    dsp->put_vp8_bilinear_pixels_tab[IDX][1][1] =  \
        ff_put_vp8_bilinear##SIZE##_hv_msa;        \
    dsp->put_vp8_bilinear_pixels_tab[IDX][1][2] =  \
        ff_put_vp8_bilinear##SIZE##_hv_msa;        \
    dsp->put_vp8_bilinear_pixels_tab[IDX][2][0] =  \
        ff_put_vp8_bilinear##SIZE##_v_msa;         \
    dsp->put_vp8_bilinear_pixels_tab[IDX][2][1] =  \
        ff_put_vp8_bilinear##SIZE##_hv_msa;        \
    dsp->put_vp8_bilinear_pixels_tab[IDX][2][2] =  \
        ff_put_vp8_bilinear##SIZE##_hv_msa

#define VP8_MC_MIPS_COPY(IDX, SIZE)                \
    dsp->put_vp8_epel_pixels_tab[IDX][0][0] =      \
        ff_put_vp8_pixels##SIZE##_msa;             \
    dsp->put_vp8_bilinear_pixels_tab[IDX][0][0] =  \
        ff_put_vp8_pixels##SIZE##_msa;

#if HAVE_MSA
static av_cold void vp8dsp_init_msa(VP8DSPContext *dsp)
{
    dsp->vp8_luma_dc_wht = ff_vp8_luma_dc_wht_msa;
    dsp->vp8_idct_add = ff_vp8_idct_add_msa;
    dsp->vp8_idct_dc_add = ff_vp8_idct_dc_add_msa;
    dsp->vp8_idct_dc_add4y = ff_vp8_idct_dc_add4y_msa;
    dsp->vp8_idct_dc_add4uv = ff_vp8_idct_dc_add4uv_msa;

    VP8_MC_MIPS_FUNC(0, 16);
    VP8_MC_MIPS_FUNC(1, 8);
    VP8_MC_MIPS_FUNC(2, 4);

    VP8_BILINEAR_MC_MIPS_FUNC(0, 16);
    VP8_BILINEAR_MC_MIPS_FUNC(1, 8);
    VP8_BILINEAR_MC_MIPS_FUNC(2, 4);

    VP8_MC_MIPS_COPY(0, 16);
    VP8_MC_MIPS_COPY(1, 8);

    dsp->vp8_v_loop_filter16y = ff_vp8_v_loop_filter16_msa;
    dsp->vp8_h_loop_filter16y = ff_vp8_h_loop_filter16_msa;
    dsp->vp8_v_loop_filter8uv = ff_vp8_v_loop_filter8uv_msa;
    dsp->vp8_h_loop_filter8uv = ff_vp8_h_loop_filter8uv_msa;

    dsp->vp8_v_loop_filter16y_inner = ff_vp8_v_loop_filter16_inner_msa;
    dsp->vp8_h_loop_filter16y_inner = ff_vp8_h_loop_filter16_inner_msa;
    dsp->vp8_v_loop_filter8uv_inner = ff_vp8_v_loop_filter8uv_inner_msa;
    dsp->vp8_h_loop_filter8uv_inner = ff_vp8_h_loop_filter8uv_inner_msa;

    dsp->vp8_v_loop_filter_simple = ff_vp8_v_loop_filter_simple_msa;
    dsp->vp8_h_loop_filter_simple = ff_vp8_h_loop_filter_simple_msa;
}
#endif  // #if HAVE_MSA

av_cold void ff_vp8dsp_init_mips(VP8DSPContext *dsp)
{
#if HAVE_MSA
    vp8dsp_init_msa(dsp);
#endif  // #if HAVE_MSA
}
