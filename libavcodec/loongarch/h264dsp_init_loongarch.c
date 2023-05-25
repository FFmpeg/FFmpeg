/*
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 * Contributed by Shiyou Yin <yinshiyou-hf@loongson.cn>
 *                Xiwei  Gu  <guxiwei-hf@loongson.cn>
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

#include "libavutil/loongarch/cpu.h"
#include "h264dsp_loongarch.h"

av_cold void ff_h264dsp_init_loongarch(H264DSPContext *c, const int bit_depth,
                                       const int chroma_format_idc)
{
    int cpu_flags = av_get_cpu_flags();

    if (have_lsx(cpu_flags)) {
        if (chroma_format_idc <= 1)
            c->h264_loop_filter_strength = ff_h264_loop_filter_strength_lsx;
        if (bit_depth == 8) {
            c->h264_idct_add     = ff_h264_idct_add_8_lsx;
            c->h264_idct8_add    = ff_h264_idct8_add_8_lsx;
            c->h264_idct_dc_add  = ff_h264_idct_dc_add_8_lsx;
            c->h264_idct8_dc_add = ff_h264_idct8_dc_add_8_lsx;

            if (chroma_format_idc <= 1) {
                c->h264_idct_add8 = ff_h264_idct_add8_8_lsx;
                c->h264_h_loop_filter_chroma = ff_h264_h_lpf_chroma_8_lsx;
                c->h264_h_loop_filter_chroma_intra = ff_h264_h_lpf_chroma_intra_8_lsx;
            } else
                c->h264_idct_add8 = ff_h264_idct_add8_422_8_lsx;

            c->h264_idct_add16 = ff_h264_idct_add16_8_lsx;
            c->h264_idct8_add4 = ff_h264_idct8_add4_8_lsx;
            c->h264_luma_dc_dequant_idct = ff_h264_luma_dc_dequant_idct_8_lsx;
            c->h264_idct_add16intra = ff_h264_idct_add16_intra_8_lsx;

            c->h264_add_pixels4_clear = ff_h264_add_pixels4_8_lsx;
            c->h264_add_pixels8_clear = ff_h264_add_pixels8_8_lsx;
            c->h264_v_loop_filter_luma = ff_h264_v_lpf_luma_8_lsx;
            c->h264_h_loop_filter_luma = ff_h264_h_lpf_luma_8_lsx;
            c->h264_v_loop_filter_luma_intra = ff_h264_v_lpf_luma_intra_8_lsx;
            c->h264_h_loop_filter_luma_intra = ff_h264_h_lpf_luma_intra_8_lsx;
            c->h264_v_loop_filter_chroma = ff_h264_v_lpf_chroma_8_lsx;

            c->h264_v_loop_filter_chroma_intra = ff_h264_v_lpf_chroma_intra_8_lsx;

            c->biweight_h264_pixels_tab[0] = ff_biweight_h264_pixels16_8_lsx;
            c->biweight_h264_pixels_tab[1] = ff_biweight_h264_pixels8_8_lsx;
            c->biweight_h264_pixels_tab[2] = ff_biweight_h264_pixels4_8_lsx;
            c->weight_h264_pixels_tab[0]   = ff_weight_h264_pixels16_8_lsx;
            c->weight_h264_pixels_tab[1] = ff_weight_h264_pixels8_8_lsx;
            c->weight_h264_pixels_tab[2] = ff_weight_h264_pixels4_8_lsx;
            c->h264_idct8_add    = ff_h264_idct8_add_8_lsx;
            c->h264_idct8_dc_add = ff_h264_idct8_dc_add_8_lsx;
        }
    }
#if HAVE_LASX
    if (have_lasx(cpu_flags)) {
        if (chroma_format_idc <= 1)
            c->h264_loop_filter_strength = ff_h264_loop_filter_strength_lasx;
        if (bit_depth == 8) {
            c->h264_add_pixels4_clear = ff_h264_add_pixels4_8_lasx;
            c->h264_add_pixels8_clear = ff_h264_add_pixels8_8_lasx;
            c->h264_v_loop_filter_luma = ff_h264_v_lpf_luma_8_lasx;
            c->h264_h_loop_filter_luma = ff_h264_h_lpf_luma_8_lasx;
            c->h264_v_loop_filter_luma_intra = ff_h264_v_lpf_luma_intra_8_lasx;
            c->h264_h_loop_filter_luma_intra = ff_h264_h_lpf_luma_intra_8_lasx;

            /* Weighted MC */
            c->weight_h264_pixels_tab[0] = ff_weight_h264_pixels16_8_lasx;
            c->weight_h264_pixels_tab[1] = ff_weight_h264_pixels8_8_lasx;

            c->biweight_h264_pixels_tab[0] = ff_biweight_h264_pixels16_8_lasx;
            c->biweight_h264_pixels_tab[1] = ff_biweight_h264_pixels8_8_lasx;

            c->h264_idct8_add    = ff_h264_idct8_add_8_lasx;
            c->h264_idct8_dc_add = ff_h264_idct8_dc_add_8_lasx;
            c->h264_idct8_add4   = ff_h264_idct8_add4_8_lasx;
        }
    }
#endif // #if HAVE_LASX
}
