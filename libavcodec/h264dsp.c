/*
 * H.26L/H.264/AVC/JVT/14496-10/... encoder/decoder
 * Copyright (c) 2003-2010 Michael Niedermayer <michaelni@gmx.at>
 *
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

/**
 * @file
 * H.264 / AVC / MPEG4 part10 DSP functions.
 * @author Michael Niedermayer <michaelni@gmx.at>
 */

#include <stdint.h>
#include "avcodec.h"
#include "h264dsp.h"

#include "h264dsp_template.c"

void ff_h264dsp_init(H264DSPContext *c)
{
    c->h264_idct_add= ff_h264_idct_add_c;
    c->h264_idct8_add= ff_h264_idct8_add_c;
    c->h264_idct_dc_add= ff_h264_idct_dc_add_c;
    c->h264_idct8_dc_add= ff_h264_idct8_dc_add_c;
    c->h264_idct_add16     = ff_h264_idct_add16_c;
    c->h264_idct8_add4     = ff_h264_idct8_add4_c;
    c->h264_idct_add8      = ff_h264_idct_add8_c;
    c->h264_idct_add16intra= ff_h264_idct_add16intra_c;
    c->h264_luma_dc_dequant_idct= ff_h264_luma_dc_dequant_idct_c;
    c->h264_chroma_dc_dequant_idct= ff_h264_chroma_dc_dequant_idct_c;

    c->weight_h264_pixels_tab[0]= weight_h264_pixels16x16_c;
    c->weight_h264_pixels_tab[1]= weight_h264_pixels16x8_c;
    c->weight_h264_pixels_tab[2]= weight_h264_pixels8x16_c;
    c->weight_h264_pixels_tab[3]= weight_h264_pixels8x8_c;
    c->weight_h264_pixels_tab[4]= weight_h264_pixels8x4_c;
    c->weight_h264_pixels_tab[5]= weight_h264_pixels4x8_c;
    c->weight_h264_pixels_tab[6]= weight_h264_pixels4x4_c;
    c->weight_h264_pixels_tab[7]= weight_h264_pixels4x2_c;
    c->weight_h264_pixels_tab[8]= weight_h264_pixels2x4_c;
    c->weight_h264_pixels_tab[9]= weight_h264_pixels2x2_c;
    c->biweight_h264_pixels_tab[0]= biweight_h264_pixels16x16_c;
    c->biweight_h264_pixels_tab[1]= biweight_h264_pixels16x8_c;
    c->biweight_h264_pixels_tab[2]= biweight_h264_pixels8x16_c;
    c->biweight_h264_pixels_tab[3]= biweight_h264_pixels8x8_c;
    c->biweight_h264_pixels_tab[4]= biweight_h264_pixels8x4_c;
    c->biweight_h264_pixels_tab[5]= biweight_h264_pixels4x8_c;
    c->biweight_h264_pixels_tab[6]= biweight_h264_pixels4x4_c;
    c->biweight_h264_pixels_tab[7]= biweight_h264_pixels4x2_c;
    c->biweight_h264_pixels_tab[8]= biweight_h264_pixels2x4_c;
    c->biweight_h264_pixels_tab[9]= biweight_h264_pixels2x2_c;

    c->h264_v_loop_filter_luma= h264_v_loop_filter_luma_c;
    c->h264_h_loop_filter_luma= h264_h_loop_filter_luma_c;
    c->h264_h_loop_filter_luma_mbaff= h264_h_loop_filter_luma_mbaff_c;
    c->h264_v_loop_filter_luma_intra= h264_v_loop_filter_luma_intra_c;
    c->h264_h_loop_filter_luma_intra= h264_h_loop_filter_luma_intra_c;
    c->h264_h_loop_filter_luma_mbaff_intra= h264_h_loop_filter_luma_mbaff_intra_c;
    c->h264_v_loop_filter_chroma= h264_v_loop_filter_chroma_c;
    c->h264_h_loop_filter_chroma= h264_h_loop_filter_chroma_c;
    c->h264_h_loop_filter_chroma_mbaff= h264_h_loop_filter_chroma_mbaff_c;
    c->h264_v_loop_filter_chroma_intra= h264_v_loop_filter_chroma_intra_c;
    c->h264_h_loop_filter_chroma_intra= h264_h_loop_filter_chroma_intra_c;
    c->h264_h_loop_filter_chroma_mbaff_intra= h264_h_loop_filter_chroma_mbaff_intra_c;
    c->h264_loop_filter_strength= NULL;

    if (ARCH_ARM) ff_h264dsp_init_arm(c);
    if (HAVE_ALTIVEC) ff_h264dsp_init_ppc(c);
    if (HAVE_MMX) ff_h264dsp_init_x86(c);
}
