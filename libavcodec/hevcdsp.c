/*
 * HEVC video decoder
 *
 * Copyright (C) 2012 - 2013 Guillaume Martres
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

#include "hevcdsp.h"

static const int8_t transform[32][32] = {
    { 64,  64,  64,  64,  64,  64,  64,  64,  64,  64,  64,  64,  64,  64,  64,  64,
      64,  64,  64,  64,  64,  64,  64,  64,  64,  64,  64,  64,  64,  64,  64,  64 },
    { 90,  90,  88,  85,  82,  78,  73,  67,  61,  54,  46,  38,  31,  22,  13,   4,
      -4, -13, -22, -31, -38, -46, -54, -61, -67, -73, -78, -82, -85, -88, -90, -90 },
    { 90,  87,  80,  70,  57,  43,  25,   9,  -9, -25, -43, -57, -70, -80, -87, -90,
     -90, -87, -80, -70, -57, -43, -25,  -9,   9,  25,  43,  57,  70,  80,  87,  90 },
    { 90,  82,  67,  46,  22,  -4, -31, -54, -73, -85, -90, -88, -78, -61, -38, -13,
      13,  38,  61,  78,  88,  90,  85,  73,  54,  31,   4, -22, -46, -67, -82, -90 },
    { 89,  75,  50,  18, -18, -50, -75, -89, -89, -75, -50, -18,  18,  50,  75,  89,
      89,  75,  50,  18, -18, -50, -75, -89, -89, -75, -50, -18,  18,  50,  75,  89 },
    { 88,  67,  31, -13, -54, -82, -90, -78, -46, -4,   38,  73,  90,  85,  61,  22,
     -22, -61, -85, -90, -73, -38,   4,  46,  78,  90,  82,  54,  13, -31, -67, -88 },
    { 87,  57,   9, -43, -80, -90, -70, -25,  25,  70,  90,  80,  43,  -9, -57, -87,
     -87, -57,  -9,  43,  80,  90,  70,  25, -25, -70, -90, -80, -43,   9,  57,  87 },
    { 85,  46, -13, -67, -90, -73, -22,  38,  82,  88,  54,  -4, -61, -90, -78, -31,
      31,  78,  90,  61,   4, -54, -88, -82, -38,  22,  73,  90,  67,  13, -46, -85 },
    { 83,  36, -36, -83, -83, -36,  36,  83,  83,  36, -36, -83, -83, -36,  36,  83,
      83,  36, -36, -83, -83, -36,  36,  83,  83,  36, -36, -83, -83, -36,  36,  83 },
    { 82,  22, -54, -90, -61,  13,  78,  85,  31, -46, -90, -67,   4,  73,  88,  38,
     -38, -88, -73,  -4,  67,  90,  46, -31, -85, -78, -13,  61,  90,  54, -22, -82 },
    { 80,   9, -70, -87, -25,  57,  90,  43, -43, -90, -57,  25,  87,  70,  -9, -80,
     -80,  -9,  70,  87,  25, -57, -90, -43,  43,  90,  57, -25, -87, -70,   9,  80 },
    { 78,  -4, -82, -73,  13,  85,  67, -22, -88, -61,  31,  90,  54, -38, -90, -46,
      46,  90,  38, -54, -90, -31,  61,  88,  22, -67, -85, -13,  73,  82,   4, -78 },
    { 75, -18, -89, -50,  50,  89,  18, -75, -75,  18,  89,  50, -50, -89, -18,  75,
      75, -18, -89, -50,  50,  89,  18, -75, -75,  18,  89,  50, -50, -89, -18,  75 },
    { 73, -31, -90, -22,  78,  67, -38, -90, -13,  82,  61, -46, -88,  -4,  85,  54,
     -54, -85,   4,  88,  46, -61, -82,  13,  90,  38, -67, -78,  22,  90,  31, -73 },
    { 70, -43, -87,   9,  90,  25, -80, -57,  57,  80, -25, -90,  -9,  87,  43, -70,
     -70,  43,  87,  -9, -90, -25,  80,  57, -57, -80,  25,  90,   9, -87, -43,  70 },
    { 67, -54, -78,  38,  85, -22, -90,   4,  90,  13, -88, -31,  82,  46, -73, -61,
      61,  73, -46, -82,  31,  88, -13, -90,  -4,  90,  22, -85, -38,  78,  54, -67 },
    { 64, -64, -64,  64,  64, -64, -64,  64,  64, -64, -64,  64,  64, -64, -64,  64,
      64, -64, -64,  64,  64, -64, -64,  64,  64, -64, -64,  64,  64, -64, -64,  64 },
    { 61, -73, -46,  82,  31, -88, -13,  90,  -4, -90,  22,  85, -38, -78,  54,  67,
     -67, -54,  78,  38, -85, -22,  90,   4, -90,  13,  88, -31, -82,  46,  73, -61 },
    { 57, -80, -25,  90,  -9, -87,  43,  70, -70, -43,  87,   9, -90,  25,  80, -57,
     -57,  80,  25, -90,   9,  87, -43, -70,  70,  43, -87,  -9,  90, -25, -80,  57 },
    { 54, -85,  -4,  88, -46, -61,  82,  13, -90,  38,  67, -78, -22,  90, -31, -73,
      73,  31, -90,  22,  78, -67, -38,  90, -13, -82,  61,  46, -88,   4,  85, -54 },
    { 50, -89,  18,  75, -75, -18,  89, -50, -50,  89, -18, -75,  75,  18, -89,  50,
      50, -89,  18,  75, -75, -18,  89, -50, -50,  89, -18, -75,  75,  18, -89,  50 },
    { 46, -90,  38,  54, -90,  31,  61, -88,  22,  67, -85,  13,  73, -82,   4,  78,
     -78,  -4,  82, -73, -13,  85, -67, -22,  88, -61, -31,  90, -54, -38,  90, -46 },
    { 43, -90,  57,  25, -87,  70,   9, -80,  80,  -9, -70,  87, -25, -57,  90, -43,
     -43,  90, -57, -25,  87, -70,  -9,  80, -80,   9,  70, -87,  25,  57, -90,  43 },
    { 38, -88,  73,  -4, -67,  90, -46, -31,  85, -78,  13,  61, -90,  54,  22, -82,
      82, -22, -54,  90, -61, -13,  78, -85,  31,  46, -90,  67,   4, -73,  88, -38 },
    { 36, -83,  83, -36, -36,  83, -83,  36,  36, -83,  83, -36, -36,  83, -83,  36,
      36, -83,  83, -36, -36,  83, -83,  36,  36, -83,  83, -36, -36,  83, -83,  36 },
    { 31, -78,  90, -61,   4,  54, -88,  82, -38, -22,  73, -90,  67, -13, -46,  85,
     -85,  46,  13, -67,  90, -73,  22,  38, -82,  88, -54,  -4,  61, -90,  78, -31 },
    { 25, -70,  90, -80,  43,   9, -57,  87, -87,  57,  -9, -43,  80, -90,  70, -25,
     -25,  70, -90,  80, -43,  -9,  57, -87,  87, -57,   9,  43, -80,  90, -70,  25 },
    { 22, -61,  85, -90,  73, -38,  -4,  46, -78,  90, -82,  54, -13, -31,  67, -88,
      88, -67,  31,  13, -54,  82, -90,  78, -46,   4,  38, -73,  90, -85,  61, -22 },
    { 18, -50,  75, -89,  89, -75,  50, -18, -18,  50, -75,  89, -89,  75, -50,  18,
      18, -50,  75, -89,  89, -75,  50, -18, -18,  50, -75,  89, -89,  75, -50,  18 },
    { 13, -38,  61, -78,  88, -90,  85, -73,  54, -31,   4,  22, -46,  67, -82,  90,
     -90,  82, -67,  46, -22,  -4,  31, -54,  73, -85,  90, -88,  78, -61,  38, -13 },
    {  9, -25,  43, -57,  70, -80,  87, -90,  90, -87,  80, -70,  57, -43,  25, -9,
      -9,  25, -43,  57, -70,  80, -87,  90, -90,  87, -80,  70, -57,  43, -25,   9 },
    {  4, -13,  22, -31,  38, -46,  54, -61,  67, -73,  78, -82,  85, -88,  90, -90,
      90, -90,  88, -85,  82, -78,  73, -67,  61, -54,  46, -38,  31, -22,  13,  -4 },
};

DECLARE_ALIGNED(16, const int8_t, ff_hevc_epel_filters[7][16]) = {
    { -2, 58, 10, -2, -2, 58, 10, -2, -2, 58, 10, -2, -2, 58, 10, -2 },
    { -4, 54, 16, -2, -4, 54, 16, -2, -4, 54, 16, -2, -4, 54, 16, -2 },
    { -6, 46, 28, -4, -6, 46, 28, -4, -6, 46, 28, -4, -6, 46, 28, -4 },
    { -4, 36, 36, -4, -4, 36, 36, -4, -4, 36, 36, -4, -4, 36, 36, -4 },
    { -4, 28, 46, -6, -4, 28, 46, -6, -4, 28, 46, -6, -4, 28, 46, -6 },
    { -2, 16, 54, -4, -2, 16, 54, -4, -2, 16, 54, -4, -2, 16, 54, -4 },
    { -2, 10, 58, -2, -2, 10, 58, -2, -2, 10, 58, -2, -2, 10, 58, -2 },
};

#define BIT_DEPTH 8
#include "hevcdsp_template.c"
#undef BIT_DEPTH

#define BIT_DEPTH 9
#include "hevcdsp_template.c"
#undef BIT_DEPTH

#define BIT_DEPTH 10
#include "hevcdsp_template.c"
#undef BIT_DEPTH

void ff_hevc_dsp_init(HEVCDSPContext *hevcdsp, int bit_depth)
{
#undef FUNC
#define FUNC(a, depth) a ## _ ## depth

#define HEVC_DSP(depth)                                                     \
    hevcdsp->put_pcm                = FUNC(put_pcm, depth);                 \
    hevcdsp->transquant_bypass[0]   = FUNC(transquant_bypass4x4, depth);    \
    hevcdsp->transquant_bypass[1]   = FUNC(transquant_bypass8x8, depth);    \
    hevcdsp->transquant_bypass[2]   = FUNC(transquant_bypass16x16, depth);  \
    hevcdsp->transquant_bypass[3]   = FUNC(transquant_bypass32x32, depth);  \
    hevcdsp->transform_skip         = FUNC(transform_skip, depth);          \
    hevcdsp->transform_4x4_luma_add = FUNC(transform_4x4_luma_add, depth);  \
    hevcdsp->transform_add[0]       = FUNC(transform_4x4_add, depth);       \
    hevcdsp->transform_add[1]       = FUNC(transform_8x8_add, depth);       \
    hevcdsp->transform_add[2]       = FUNC(transform_16x16_add, depth);     \
    hevcdsp->transform_add[3]       = FUNC(transform_32x32_add, depth);     \
                                                                            \
    hevcdsp->sao_band_filter[0] = FUNC(sao_band_filter_0, depth);           \
    hevcdsp->sao_band_filter[1] = FUNC(sao_band_filter_1, depth);           \
    hevcdsp->sao_band_filter[2] = FUNC(sao_band_filter_2, depth);           \
    hevcdsp->sao_band_filter[3] = FUNC(sao_band_filter_3, depth);           \
                                                                            \
    hevcdsp->sao_edge_filter[0] = FUNC(sao_edge_filter_0, depth);           \
    hevcdsp->sao_edge_filter[1] = FUNC(sao_edge_filter_1, depth);           \
    hevcdsp->sao_edge_filter[2] = FUNC(sao_edge_filter_2, depth);           \
    hevcdsp->sao_edge_filter[3] = FUNC(sao_edge_filter_3, depth);           \
                                                                            \
    hevcdsp->put_hevc_qpel[0][0] = FUNC(put_hevc_qpel_pixels, depth);       \
    hevcdsp->put_hevc_qpel[0][1] = FUNC(put_hevc_qpel_h1, depth);           \
    hevcdsp->put_hevc_qpel[0][2] = FUNC(put_hevc_qpel_h2, depth);           \
    hevcdsp->put_hevc_qpel[0][3] = FUNC(put_hevc_qpel_h3, depth);           \
    hevcdsp->put_hevc_qpel[1][0] = FUNC(put_hevc_qpel_v1, depth);           \
    hevcdsp->put_hevc_qpel[1][1] = FUNC(put_hevc_qpel_h1v1, depth);         \
    hevcdsp->put_hevc_qpel[1][2] = FUNC(put_hevc_qpel_h2v1, depth);         \
    hevcdsp->put_hevc_qpel[1][3] = FUNC(put_hevc_qpel_h3v1, depth);         \
    hevcdsp->put_hevc_qpel[2][0] = FUNC(put_hevc_qpel_v2, depth);           \
    hevcdsp->put_hevc_qpel[2][1] = FUNC(put_hevc_qpel_h1v2, depth);         \
    hevcdsp->put_hevc_qpel[2][2] = FUNC(put_hevc_qpel_h2v2, depth);         \
    hevcdsp->put_hevc_qpel[2][3] = FUNC(put_hevc_qpel_h3v2, depth);         \
    hevcdsp->put_hevc_qpel[3][0] = FUNC(put_hevc_qpel_v3, depth);           \
    hevcdsp->put_hevc_qpel[3][1] = FUNC(put_hevc_qpel_h1v3, depth);         \
    hevcdsp->put_hevc_qpel[3][2] = FUNC(put_hevc_qpel_h2v3, depth);         \
    hevcdsp->put_hevc_qpel[3][3] = FUNC(put_hevc_qpel_h3v3, depth);         \
                                                                            \
    hevcdsp->put_hevc_epel[0][0] = FUNC(put_hevc_epel_pixels, depth);       \
    hevcdsp->put_hevc_epel[0][1] = FUNC(put_hevc_epel_h, depth);            \
    hevcdsp->put_hevc_epel[1][0] = FUNC(put_hevc_epel_v, depth);            \
    hevcdsp->put_hevc_epel[1][1] = FUNC(put_hevc_epel_hv, depth);           \
                                                                            \
    hevcdsp->put_unweighted_pred   = FUNC(put_unweighted_pred, depth);      \
    hevcdsp->put_weighted_pred_avg = FUNC(put_weighted_pred_avg, depth);    \
                                                                            \
    hevcdsp->weighted_pred         = FUNC(weighted_pred, depth);            \
    hevcdsp->weighted_pred_avg     = FUNC(weighted_pred_avg, depth);        \
                                                                            \
    hevcdsp->hevc_h_loop_filter_luma     = FUNC(hevc_h_loop_filter_luma, depth);   \
    hevcdsp->hevc_v_loop_filter_luma     = FUNC(hevc_v_loop_filter_luma, depth);   \
    hevcdsp->hevc_h_loop_filter_chroma   = FUNC(hevc_h_loop_filter_chroma, depth); \
    hevcdsp->hevc_v_loop_filter_chroma   = FUNC(hevc_v_loop_filter_chroma, depth); \
    hevcdsp->hevc_h_loop_filter_luma_c   = FUNC(hevc_h_loop_filter_luma, depth);   \
    hevcdsp->hevc_v_loop_filter_luma_c   = FUNC(hevc_v_loop_filter_luma, depth);   \
    hevcdsp->hevc_h_loop_filter_chroma_c = FUNC(hevc_h_loop_filter_chroma, depth); \
    hevcdsp->hevc_v_loop_filter_chroma_c = FUNC(hevc_v_loop_filter_chroma, depth);

    switch (bit_depth) {
    case 9:
        HEVC_DSP(9);
        break;
    case 10:
        HEVC_DSP(10);
        break;
    default:
        HEVC_DSP(8);
        break;
    }
}
