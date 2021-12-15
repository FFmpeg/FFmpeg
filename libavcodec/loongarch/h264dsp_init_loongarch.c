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
#include "h264dsp_lasx.h"

av_cold void ff_h264dsp_init_loongarch(H264DSPContext *c, const int bit_depth,
                                       const int chroma_format_idc)
{
    int cpu_flags = av_get_cpu_flags();

    if (have_lasx(cpu_flags)) {
        if (bit_depth == 8) {
            c->h264_add_pixels4_clear = ff_h264_add_pixels4_8_lasx;
            c->h264_add_pixels8_clear = ff_h264_add_pixels8_8_lasx;
            c->h264_v_loop_filter_luma = ff_h264_v_lpf_luma_8_lasx;
            c->h264_h_loop_filter_luma = ff_h264_h_lpf_luma_8_lasx;
            c->h264_v_loop_filter_luma_intra = ff_h264_v_lpf_luma_intra_8_lasx;
            c->h264_h_loop_filter_luma_intra = ff_h264_h_lpf_luma_intra_8_lasx;
            c->h264_v_loop_filter_chroma = ff_h264_v_lpf_chroma_8_lasx;

            if (chroma_format_idc <= 1)
                c->h264_h_loop_filter_chroma = ff_h264_h_lpf_chroma_8_lasx;
            c->h264_v_loop_filter_chroma_intra = ff_h264_v_lpf_chroma_intra_8_lasx;

            if (chroma_format_idc <= 1)
                c->h264_h_loop_filter_chroma_intra = ff_h264_h_lpf_chroma_intra_8_lasx;

            /* Weighted MC */
            c->weight_h264_pixels_tab[0] = ff_weight_h264_pixels16_8_lasx;
            c->weight_h264_pixels_tab[1] = ff_weight_h264_pixels8_8_lasx;
            c->weight_h264_pixels_tab[2] = ff_weight_h264_pixels4_8_lasx;

            c->biweight_h264_pixels_tab[0] = ff_biweight_h264_pixels16_8_lasx;
            c->biweight_h264_pixels_tab[1] = ff_biweight_h264_pixels8_8_lasx;
            c->biweight_h264_pixels_tab[2] = ff_biweight_h264_pixels4_8_lasx;
        }
    }
}
