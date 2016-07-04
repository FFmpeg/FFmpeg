/*
 * HEVC video decoder
 *
 * Copyright (C) 2012 - 2013 Guillaume Martres
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

DECLARE_ALIGNED(16, const int16_t, ff_hevc_epel_coeffs[7][16]) = {
    { -2, 58, 10, -2, -2, 58, 10, -2, -2, 58, 10, -2, -2, 58, 10, -2 },
    { -4, 54, 16, -2, -4, 54, 16, -2, -4, 54, 16, -2, -4, 54, 16, -2 },
    { -6, 46, 28, -4, -6, 46, 28, -4, -6, 46, 28, -4, -6, 46, 28, -4 },
    { -4, 36, 36, -4, -4, 36, 36, -4, -4, 36, 36, -4, -4, 36, 36, -4 },
    { -4, 28, 46, -6, -4, 28, 46, -6, -4, 28, 46, -6, -4, 28, 46, -6 },
    { -2, 16, 54, -4, -2, 16, 54, -4, -2, 16, 54, -4, -2, 16, 54, -4 },
    { -2, 10, 58, -2, -2, 10, 58, -2, -2, 10, 58, -2, -2, 10, 58, -2 },
};

DECLARE_ALIGNED(16, const int8_t, ff_hevc_epel_coeffs8[7][16]) = {
    { -2, 58, 10, -2, -2, 58, 10, -2, -2, 58, 10, -2, -2, 58, 10, -2 },
    { -4, 54, 16, -2, -4, 54, 16, -2, -4, 54, 16, -2, -4, 54, 16, -2 },
    { -6, 46, 28, -4, -6, 46, 28, -4, -6, 46, 28, -4, -6, 46, 28, -4 },
    { -4, 36, 36, -4, -4, 36, 36, -4, -4, 36, 36, -4, -4, 36, 36, -4 },
    { -4, 28, 46, -6, -4, 28, 46, -6, -4, 28, 46, -6, -4, 28, 46, -6 },
    { -2, 16, 54, -4, -2, 16, 54, -4, -2, 16, 54, -4, -2, 16, 54, -4 },
    { -2, 10, 58, -2, -2, 10, 58, -2, -2, 10, 58, -2, -2, 10, 58, -2 },
};

DECLARE_ALIGNED(16, const int16_t, ff_hevc_qpel_coeffs[3][8]) = {
    { -1, 4, -10, 58, 17, -5,  1,  0 },
    { -1, 4, -11, 40, 40, -11, 4, -1 },
    {  0, 1,  -5, 17, 58, -10, 4, -1 },
};

DECLARE_ALIGNED(16, const int8_t, ff_hevc_qpel_coeffs8[3][16]) = {
    { -1, 4, -10, 58, 17, -5,  1,  0, -1, 4, -10, 58, 17, -5,  1,  0 },
    { -1, 4, -11, 40, 40, -11, 4, -1, -1, 4, -11, 40, 40, -11, 4, -1 },
    {  0, 1,  -5, 17, 58, -10, 4, -1,  0, 1,  -5, 17, 58, -10, 4, -1 },
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

#define QPEL_FUNC(i, width, depth)                                                  \
    hevcdsp->put_hevc_qpel[0][0][i] = FUNC(put_hevc_qpel_pixels_ ## width, depth);  \
    hevcdsp->put_hevc_qpel[0][1][i] = FUNC(put_hevc_qpel_h_      ## width, depth);  \
    hevcdsp->put_hevc_qpel[1][0][i] = FUNC(put_hevc_qpel_v_      ## width, depth);  \
    hevcdsp->put_hevc_qpel[1][1][i] = FUNC(put_hevc_qpel_hv_     ## width, depth);  \

#define EPEL_FUNC(i, width, depth)                                                  \
    hevcdsp->put_hevc_epel[0][0][i] = FUNC(put_hevc_epel_pixels_ ## width, depth);  \
    hevcdsp->put_hevc_epel[0][1][i] = FUNC(put_hevc_epel_h_      ## width, depth);  \
    hevcdsp->put_hevc_epel[1][0][i] = FUNC(put_hevc_epel_v_      ## width, depth);  \
    hevcdsp->put_hevc_epel[1][1][i] = FUNC(put_hevc_epel_hv_     ## width, depth);  \

#define PRED_FUNC(i, width, depth)                                                        \
    hevcdsp->put_unweighted_pred[i]     = FUNC(put_unweighted_pred_ ## width, depth);     \
    hevcdsp->put_unweighted_pred_avg[i] = FUNC(put_unweighted_pred_avg_ ## width, depth); \
    hevcdsp->weighted_pred[i]           = FUNC(put_weighted_pred_ ## width, depth);       \
    hevcdsp->weighted_pred_avg[i]       = FUNC(put_weighted_pred_avg_ ## width, depth);   \

#define PRED_FUNC_CHROMA(i, width, depth)                                                        \
    hevcdsp->put_unweighted_pred_chroma[i]     = FUNC(put_unweighted_pred_ ## width, depth);     \
    hevcdsp->put_unweighted_pred_avg_chroma[i] = FUNC(put_unweighted_pred_avg_ ## width, depth); \
    hevcdsp->weighted_pred_chroma[i]           = FUNC(put_weighted_pred_ ## width, depth);       \
    hevcdsp->weighted_pred_avg_chroma[i]       = FUNC(put_weighted_pred_avg_ ## width, depth);   \

#define HEVC_DSP(depth)                                                     \
    hevcdsp->put_pcm                = FUNC(put_pcm, depth);                 \
    hevcdsp->add_residual[0]        = FUNC(add_residual4x4, depth);         \
    hevcdsp->add_residual[1]        = FUNC(add_residual8x8, depth);         \
    hevcdsp->add_residual[2]        = FUNC(add_residual16x16, depth);       \
    hevcdsp->add_residual[3]        = FUNC(add_residual32x32, depth);       \
    hevcdsp->dequant                = FUNC(dequant, depth);                 \
    hevcdsp->transform_4x4_luma     = FUNC(transform_4x4_luma, depth);      \
    hevcdsp->idct[0]                = FUNC(idct_4x4, depth);                \
    hevcdsp->idct[1]                = FUNC(idct_8x8, depth);                \
    hevcdsp->idct[2]                = FUNC(idct_16x16, depth);              \
    hevcdsp->idct[3]                = FUNC(idct_32x32, depth);              \
                                                                            \
    hevcdsp->idct_dc[0]             = FUNC(idct_4x4_dc, depth);             \
    hevcdsp->idct_dc[1]             = FUNC(idct_8x8_dc, depth);             \
    hevcdsp->idct_dc[2]             = FUNC(idct_16x16_dc, depth);           \
    hevcdsp->idct_dc[3]             = FUNC(idct_32x32_dc, depth);           \
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
    QPEL_FUNC(0, 4,  depth);                                                \
    QPEL_FUNC(1, 8,  depth);                                                \
    QPEL_FUNC(2, 12, depth);                                                \
    QPEL_FUNC(3, 16, depth);                                                \
    QPEL_FUNC(4, 24, depth);                                                \
    QPEL_FUNC(5, 32, depth);                                                \
    QPEL_FUNC(6, 48, depth);                                                \
    QPEL_FUNC(7, 64, depth);                                                \
                                                                            \
    EPEL_FUNC(0, 2,  depth);                                                \
    EPEL_FUNC(1, 4,  depth);                                                \
    EPEL_FUNC(2, 6, depth);                                                 \
    EPEL_FUNC(3, 8, depth);                                                 \
    EPEL_FUNC(4, 12, depth);                                                \
    EPEL_FUNC(5, 16, depth);                                                \
    EPEL_FUNC(6, 24, depth);                                                \
    EPEL_FUNC(7, 32, depth);                                                \
                                                                            \
    PRED_FUNC(0, 4,  depth);                                                \
    PRED_FUNC(1, 8,  depth);                                                \
    PRED_FUNC(2, 12, depth);                                                \
    PRED_FUNC(3, 16, depth);                                                \
    PRED_FUNC(4, 24, depth);                                                \
    PRED_FUNC(5, 32, depth);                                                \
    PRED_FUNC(6, 48, depth);                                                \
    PRED_FUNC(7, 64, depth);                                                \
    PRED_FUNC_CHROMA(0, 2,  depth);                                         \
    PRED_FUNC_CHROMA(1, 4,  depth);                                         \
    PRED_FUNC_CHROMA(2, 6, depth);                                          \
    PRED_FUNC_CHROMA(3, 8, depth);                                          \
    PRED_FUNC_CHROMA(4, 12, depth);                                         \
    PRED_FUNC_CHROMA(5, 16, depth);                                         \
    PRED_FUNC_CHROMA(6, 24, depth);                                         \
    PRED_FUNC_CHROMA(7, 32, depth);                                         \
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

    if (ARCH_X86)
        ff_hevc_dsp_init_x86(hevcdsp, bit_depth);
}
