/*
 * ARM NEON optimised DSP functions
 * Copyright (c) 2008 Mans Rullgard <mans@mansr.com>
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

#include <stdint.h>

#include "libavcodec/avcodec.h"
#include "libavcodec/dsputil.h"

void ff_put_pixels16_neon(uint8_t *, const uint8_t *, int, int);
void ff_put_pixels16_x2_neon(uint8_t *, const uint8_t *, int, int);
void ff_put_pixels16_y2_neon(uint8_t *, const uint8_t *, int, int);
void ff_put_pixels16_xy2_neon(uint8_t *, const uint8_t *, int, int);
void ff_put_pixels8_neon(uint8_t *, const uint8_t *, int, int);
void ff_put_pixels8_x2_neon(uint8_t *, const uint8_t *, int, int);
void ff_put_pixels8_y2_neon(uint8_t *, const uint8_t *, int, int);
void ff_put_pixels8_xy2_neon(uint8_t *, const uint8_t *, int, int);
void ff_put_pixels16_x2_no_rnd_neon(uint8_t *, const uint8_t *, int, int);
void ff_put_pixels16_y2_no_rnd_neon(uint8_t *, const uint8_t *, int, int);
void ff_put_pixels16_xy2_no_rnd_neon(uint8_t *, const uint8_t *, int, int);
void ff_put_pixels8_x2_no_rnd_neon(uint8_t *, const uint8_t *, int, int);
void ff_put_pixels8_y2_no_rnd_neon(uint8_t *, const uint8_t *, int, int);
void ff_put_pixels8_xy2_no_rnd_neon(uint8_t *, const uint8_t *, int, int);

void ff_avg_pixels16_neon(uint8_t *, const uint8_t *, int, int);

void ff_put_h264_qpel16_mc00_neon(uint8_t *, uint8_t *, int);
void ff_put_h264_qpel16_mc10_neon(uint8_t *, uint8_t *, int);
void ff_put_h264_qpel16_mc20_neon(uint8_t *, uint8_t *, int);
void ff_put_h264_qpel16_mc30_neon(uint8_t *, uint8_t *, int);
void ff_put_h264_qpel16_mc01_neon(uint8_t *, uint8_t *, int);
void ff_put_h264_qpel16_mc11_neon(uint8_t *, uint8_t *, int);
void ff_put_h264_qpel16_mc21_neon(uint8_t *, uint8_t *, int);
void ff_put_h264_qpel16_mc31_neon(uint8_t *, uint8_t *, int);
void ff_put_h264_qpel16_mc02_neon(uint8_t *, uint8_t *, int);
void ff_put_h264_qpel16_mc12_neon(uint8_t *, uint8_t *, int);
void ff_put_h264_qpel16_mc22_neon(uint8_t *, uint8_t *, int);
void ff_put_h264_qpel16_mc32_neon(uint8_t *, uint8_t *, int);
void ff_put_h264_qpel16_mc03_neon(uint8_t *, uint8_t *, int);
void ff_put_h264_qpel16_mc13_neon(uint8_t *, uint8_t *, int);
void ff_put_h264_qpel16_mc23_neon(uint8_t *, uint8_t *, int);
void ff_put_h264_qpel16_mc33_neon(uint8_t *, uint8_t *, int);

void ff_put_h264_qpel8_mc00_neon(uint8_t *, uint8_t *, int);
void ff_put_h264_qpel8_mc10_neon(uint8_t *, uint8_t *, int);
void ff_put_h264_qpel8_mc20_neon(uint8_t *, uint8_t *, int);
void ff_put_h264_qpel8_mc30_neon(uint8_t *, uint8_t *, int);
void ff_put_h264_qpel8_mc01_neon(uint8_t *, uint8_t *, int);
void ff_put_h264_qpel8_mc11_neon(uint8_t *, uint8_t *, int);
void ff_put_h264_qpel8_mc21_neon(uint8_t *, uint8_t *, int);
void ff_put_h264_qpel8_mc31_neon(uint8_t *, uint8_t *, int);
void ff_put_h264_qpel8_mc02_neon(uint8_t *, uint8_t *, int);
void ff_put_h264_qpel8_mc12_neon(uint8_t *, uint8_t *, int);
void ff_put_h264_qpel8_mc22_neon(uint8_t *, uint8_t *, int);
void ff_put_h264_qpel8_mc32_neon(uint8_t *, uint8_t *, int);
void ff_put_h264_qpel8_mc03_neon(uint8_t *, uint8_t *, int);
void ff_put_h264_qpel8_mc13_neon(uint8_t *, uint8_t *, int);
void ff_put_h264_qpel8_mc23_neon(uint8_t *, uint8_t *, int);
void ff_put_h264_qpel8_mc33_neon(uint8_t *, uint8_t *, int);

void ff_avg_h264_qpel16_mc00_neon(uint8_t *, uint8_t *, int);

void ff_put_h264_chroma_mc8_neon(uint8_t *, uint8_t *, int, int, int, int);
void ff_put_h264_chroma_mc4_neon(uint8_t *, uint8_t *, int, int, int, int);

void ff_avg_h264_chroma_mc8_neon(uint8_t *, uint8_t *, int, int, int, int);
void ff_avg_h264_chroma_mc4_neon(uint8_t *, uint8_t *, int, int, int, int);

void ff_h264_v_loop_filter_luma_neon(uint8_t *pix, int stride, int alpha,
                                     int beta, int8_t *tc0);
void ff_h264_h_loop_filter_luma_neon(uint8_t *pix, int stride, int alpha,
                                     int beta, int8_t *tc0);
void ff_h264_v_loop_filter_chroma_neon(uint8_t *pix, int stride, int alpha,
                                       int beta, int8_t *tc0);
void ff_h264_h_loop_filter_chroma_neon(uint8_t *pix, int stride, int alpha,
                                       int beta, int8_t *tc0);

void ff_dsputil_init_neon(DSPContext *c, AVCodecContext *avctx)
{
    c->put_pixels_tab[0][0] = ff_put_pixels16_neon;
    c->put_pixels_tab[0][1] = ff_put_pixels16_x2_neon;
    c->put_pixels_tab[0][2] = ff_put_pixels16_y2_neon;
    c->put_pixels_tab[0][3] = ff_put_pixels16_xy2_neon;
    c->put_pixels_tab[1][0] = ff_put_pixels8_neon;
    c->put_pixels_tab[1][1] = ff_put_pixels8_x2_neon;
    c->put_pixels_tab[1][2] = ff_put_pixels8_y2_neon;
    c->put_pixels_tab[1][3] = ff_put_pixels8_xy2_neon;

    c->put_no_rnd_pixels_tab[0][0] = ff_put_pixels16_neon;
    c->put_no_rnd_pixels_tab[0][1] = ff_put_pixels16_x2_no_rnd_neon;
    c->put_no_rnd_pixels_tab[0][2] = ff_put_pixels16_y2_no_rnd_neon;
    c->put_no_rnd_pixels_tab[0][3] = ff_put_pixels16_xy2_no_rnd_neon;
    c->put_no_rnd_pixels_tab[1][0] = ff_put_pixels8_neon;
    c->put_no_rnd_pixels_tab[1][1] = ff_put_pixels8_x2_no_rnd_neon;
    c->put_no_rnd_pixels_tab[1][2] = ff_put_pixels8_y2_no_rnd_neon;
    c->put_no_rnd_pixels_tab[1][3] = ff_put_pixels8_xy2_no_rnd_neon;

    c->avg_pixels_tab[0][0] = ff_avg_pixels16_neon;

    c->put_h264_chroma_pixels_tab[0] = ff_put_h264_chroma_mc8_neon;
    c->put_h264_chroma_pixels_tab[1] = ff_put_h264_chroma_mc4_neon;

    c->avg_h264_chroma_pixels_tab[0] = ff_avg_h264_chroma_mc8_neon;
    c->avg_h264_chroma_pixels_tab[1] = ff_avg_h264_chroma_mc4_neon;

    c->put_h264_qpel_pixels_tab[0][ 0] = ff_put_h264_qpel16_mc00_neon;
    c->put_h264_qpel_pixels_tab[0][ 1] = ff_put_h264_qpel16_mc10_neon;
    c->put_h264_qpel_pixels_tab[0][ 2] = ff_put_h264_qpel16_mc20_neon;
    c->put_h264_qpel_pixels_tab[0][ 3] = ff_put_h264_qpel16_mc30_neon;
    c->put_h264_qpel_pixels_tab[0][ 4] = ff_put_h264_qpel16_mc01_neon;
    c->put_h264_qpel_pixels_tab[0][ 5] = ff_put_h264_qpel16_mc11_neon;
    c->put_h264_qpel_pixels_tab[0][ 6] = ff_put_h264_qpel16_mc21_neon;
    c->put_h264_qpel_pixels_tab[0][ 7] = ff_put_h264_qpel16_mc31_neon;
    c->put_h264_qpel_pixels_tab[0][ 8] = ff_put_h264_qpel16_mc02_neon;
    c->put_h264_qpel_pixels_tab[0][ 9] = ff_put_h264_qpel16_mc12_neon;
    c->put_h264_qpel_pixels_tab[0][10] = ff_put_h264_qpel16_mc22_neon;
    c->put_h264_qpel_pixels_tab[0][11] = ff_put_h264_qpel16_mc32_neon;
    c->put_h264_qpel_pixels_tab[0][12] = ff_put_h264_qpel16_mc03_neon;
    c->put_h264_qpel_pixels_tab[0][13] = ff_put_h264_qpel16_mc13_neon;
    c->put_h264_qpel_pixels_tab[0][14] = ff_put_h264_qpel16_mc23_neon;
    c->put_h264_qpel_pixels_tab[0][15] = ff_put_h264_qpel16_mc33_neon;

    c->put_h264_qpel_pixels_tab[1][ 0] = ff_put_h264_qpel8_mc00_neon;
    c->put_h264_qpel_pixels_tab[1][ 1] = ff_put_h264_qpel8_mc10_neon;
    c->put_h264_qpel_pixels_tab[1][ 2] = ff_put_h264_qpel8_mc20_neon;
    c->put_h264_qpel_pixels_tab[1][ 3] = ff_put_h264_qpel8_mc30_neon;
    c->put_h264_qpel_pixels_tab[1][ 4] = ff_put_h264_qpel8_mc01_neon;
    c->put_h264_qpel_pixels_tab[1][ 5] = ff_put_h264_qpel8_mc11_neon;
    c->put_h264_qpel_pixels_tab[1][ 6] = ff_put_h264_qpel8_mc21_neon;
    c->put_h264_qpel_pixels_tab[1][ 7] = ff_put_h264_qpel8_mc31_neon;
    c->put_h264_qpel_pixels_tab[1][ 8] = ff_put_h264_qpel8_mc02_neon;
    c->put_h264_qpel_pixels_tab[1][ 9] = ff_put_h264_qpel8_mc12_neon;
    c->put_h264_qpel_pixels_tab[1][10] = ff_put_h264_qpel8_mc22_neon;
    c->put_h264_qpel_pixels_tab[1][11] = ff_put_h264_qpel8_mc32_neon;
    c->put_h264_qpel_pixels_tab[1][12] = ff_put_h264_qpel8_mc03_neon;
    c->put_h264_qpel_pixels_tab[1][13] = ff_put_h264_qpel8_mc13_neon;
    c->put_h264_qpel_pixels_tab[1][14] = ff_put_h264_qpel8_mc23_neon;
    c->put_h264_qpel_pixels_tab[1][15] = ff_put_h264_qpel8_mc33_neon;

    c->avg_h264_qpel_pixels_tab[0][ 0] = ff_avg_h264_qpel16_mc00_neon;

    c->h264_v_loop_filter_luma = ff_h264_v_loop_filter_luma_neon;
    c->h264_h_loop_filter_luma = ff_h264_h_loop_filter_luma_neon;
    c->h264_v_loop_filter_chroma = ff_h264_v_loop_filter_chroma_neon;
    c->h264_h_loop_filter_chroma = ff_h264_h_loop_filter_chroma_neon;
}
