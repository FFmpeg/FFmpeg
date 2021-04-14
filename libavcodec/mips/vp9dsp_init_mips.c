/*
 * Copyright (c) 2015 Shivraj Patil (Shivraj.Patil@imgtec.com)
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
#include "libavutil/mips/cpu.h"
#include "config.h"
#include "libavutil/common.h"
#include "libavcodec/vp9dsp.h"
#include "vp9dsp_mips.h"

#if HAVE_MSA
static av_cold void vp9dsp_intrapred_init_msa(VP9DSPContext *dsp, int bpp)
{
    if (bpp == 8) {
#define init_intra_pred_msa(tx, sz)                             \
    dsp->intra_pred[tx][VERT_PRED]    = ff_vert_##sz##_msa;     \
    dsp->intra_pred[tx][HOR_PRED]     = ff_hor_##sz##_msa;      \
    dsp->intra_pred[tx][DC_PRED]      = ff_dc_##sz##_msa;       \
    dsp->intra_pred[tx][LEFT_DC_PRED] = ff_dc_left_##sz##_msa;  \
    dsp->intra_pred[tx][TOP_DC_PRED]  = ff_dc_top_##sz##_msa;   \
    dsp->intra_pred[tx][DC_128_PRED]  = ff_dc_128_##sz##_msa;   \
    dsp->intra_pred[tx][DC_127_PRED]  = ff_dc_127_##sz##_msa;   \
    dsp->intra_pred[tx][DC_129_PRED]  = ff_dc_129_##sz##_msa;   \
    dsp->intra_pred[tx][TM_VP8_PRED]  = ff_tm_##sz##_msa;       \

    init_intra_pred_msa(TX_16X16, 16x16);
    init_intra_pred_msa(TX_32X32, 32x32);
#undef init_intra_pred_msa

#define init_intra_pred_msa(tx, sz)                             \
    dsp->intra_pred[tx][DC_PRED]      = ff_dc_##sz##_msa;       \
    dsp->intra_pred[tx][LEFT_DC_PRED] = ff_dc_left_##sz##_msa;  \
    dsp->intra_pred[tx][TOP_DC_PRED]  = ff_dc_top_##sz##_msa;   \
    dsp->intra_pred[tx][TM_VP8_PRED]  = ff_tm_##sz##_msa;       \

    init_intra_pred_msa(TX_4X4, 4x4);
    init_intra_pred_msa(TX_8X8, 8x8);
#undef init_intra_pred_msa
    }
}

static av_cold void vp9dsp_itxfm_init_msa(VP9DSPContext *dsp, int bpp)
{
    if (bpp == 8) {
#define init_itxfm(tx, sz)                                         \
    dsp->itxfm_add[tx][DCT_DCT]   = ff_idct_idct_##sz##_add_msa;   \
    dsp->itxfm_add[tx][DCT_ADST]  = ff_iadst_idct_##sz##_add_msa;  \
    dsp->itxfm_add[tx][ADST_DCT]  = ff_idct_iadst_##sz##_add_msa;  \
    dsp->itxfm_add[tx][ADST_ADST] = ff_iadst_iadst_##sz##_add_msa  \

#define init_idct(tx, nm)                        \
    dsp->itxfm_add[tx][DCT_DCT]   =              \
    dsp->itxfm_add[tx][ADST_DCT]  =              \
    dsp->itxfm_add[tx][DCT_ADST]  =              \
    dsp->itxfm_add[tx][ADST_ADST] = nm##_add_msa

    init_itxfm(TX_4X4, 4x4);
    init_itxfm(TX_8X8, 8x8);
    init_itxfm(TX_16X16, 16x16);
    init_idct(TX_32X32, ff_idct_idct_32x32);
#undef init_itxfm
#undef init_idct
    }
}

static av_cold void vp9dsp_mc_init_msa(VP9DSPContext *dsp, int bpp)
{
    if (bpp == 8) {
#define init_fpel(idx1, idx2, sz, type)                                    \
    dsp->mc[idx1][FILTER_8TAP_SMOOTH ][idx2][0][0] = ff_##type##sz##_msa;  \
    dsp->mc[idx1][FILTER_8TAP_REGULAR][idx2][0][0] = ff_##type##sz##_msa;  \
    dsp->mc[idx1][FILTER_8TAP_SHARP  ][idx2][0][0] = ff_##type##sz##_msa;  \
    dsp->mc[idx1][FILTER_BILINEAR    ][idx2][0][0] = ff_##type##sz##_msa

#define init_copy_avg(idx, sz)    \
    init_fpel(idx, 0, sz, copy);  \
    init_fpel(idx, 1, sz, avg)

#define init_avg(idx, sz)  \
    init_fpel(idx, 1, sz, avg)

    init_copy_avg(0, 64);
    init_copy_avg(1, 32);
    init_copy_avg(2, 16);
    init_copy_avg(3,  8);
    init_avg(4,  4);

#undef init_copy_avg
#undef init_avg
#undef init_fpel

#define init_subpel1(idx1, idx2, idxh, idxv, sz, dir, type)  \
    dsp->mc[idx1][FILTER_BILINEAR    ][idx2][idxh][idxv] =   \
        ff_##type##_bilin_##sz##dir##_msa;                   \
    dsp->mc[idx1][FILTER_8TAP_SMOOTH ][idx2][idxh][idxv] =   \
        ff_##type##_8tap_smooth_##sz##dir##_msa;             \
    dsp->mc[idx1][FILTER_8TAP_REGULAR][idx2][idxh][idxv] =   \
        ff_##type##_8tap_regular_##sz##dir##_msa;            \
    dsp->mc[idx1][FILTER_8TAP_SHARP  ][idx2][idxh][idxv] =   \
        ff_##type##_8tap_sharp_##sz##dir##_msa;

#define init_subpel2(idx, idxh, idxv, dir, type)      \
    init_subpel1(0, idx, idxh, idxv, 64, dir, type);  \
    init_subpel1(1, idx, idxh, idxv, 32, dir, type);  \
    init_subpel1(2, idx, idxh, idxv, 16, dir, type);  \
    init_subpel1(3, idx, idxh, idxv,  8, dir, type);  \
    init_subpel1(4, idx, idxh, idxv,  4, dir, type)

#define init_subpel3(idx, type)         \
    init_subpel2(idx, 1, 1, hv, type);  \
    init_subpel2(idx, 0, 1, v, type);   \
    init_subpel2(idx, 1, 0, h, type)

    init_subpel3(0, put);
    init_subpel3(1, avg);

#undef init_subpel1
#undef init_subpel2
#undef init_subpel3
    }
}

static av_cold void vp9dsp_loopfilter_init_msa(VP9DSPContext *dsp, int bpp)
{
    if (bpp == 8) {
        dsp->loop_filter_8[0][0] = ff_loop_filter_h_4_8_msa;
        dsp->loop_filter_8[0][1] = ff_loop_filter_v_4_8_msa;
        dsp->loop_filter_8[1][0] = ff_loop_filter_h_8_8_msa;
        dsp->loop_filter_8[1][1] = ff_loop_filter_v_8_8_msa;
        dsp->loop_filter_8[2][0] = ff_loop_filter_h_16_8_msa;
        dsp->loop_filter_8[2][1] = ff_loop_filter_v_16_8_msa;

        dsp->loop_filter_16[0] = ff_loop_filter_h_16_16_msa;
        dsp->loop_filter_16[1] = ff_loop_filter_v_16_16_msa;

        dsp->loop_filter_mix2[0][0][0] = ff_loop_filter_h_44_16_msa;
        dsp->loop_filter_mix2[0][0][1] = ff_loop_filter_v_44_16_msa;
        dsp->loop_filter_mix2[0][1][0] = ff_loop_filter_h_48_16_msa;
        dsp->loop_filter_mix2[0][1][1] = ff_loop_filter_v_48_16_msa;
        dsp->loop_filter_mix2[1][0][0] = ff_loop_filter_h_84_16_msa;
        dsp->loop_filter_mix2[1][0][1] = ff_loop_filter_v_84_16_msa;
        dsp->loop_filter_mix2[1][1][0] = ff_loop_filter_h_88_16_msa;
        dsp->loop_filter_mix2[1][1][1] = ff_loop_filter_v_88_16_msa;
    }
}

static av_cold void vp9dsp_init_msa(VP9DSPContext *dsp, int bpp)
{
    vp9dsp_intrapred_init_msa(dsp, bpp);
    vp9dsp_itxfm_init_msa(dsp, bpp);
    vp9dsp_mc_init_msa(dsp, bpp);
    vp9dsp_loopfilter_init_msa(dsp, bpp);
}
#endif  // #if HAVE_MSA

#if HAVE_MMI
static av_cold void vp9dsp_mc_init_mmi(VP9DSPContext *dsp)
{
#define init_subpel1(idx1, idx2, idxh, idxv, sz, dir, type)  \
    dsp->mc[idx1][FILTER_8TAP_SMOOTH ][idx2][idxh][idxv] =   \
        ff_##type##_8tap_smooth_##sz##dir##_mmi;             \
    dsp->mc[idx1][FILTER_8TAP_REGULAR][idx2][idxh][idxv] =   \
        ff_##type##_8tap_regular_##sz##dir##_mmi;            \
    dsp->mc[idx1][FILTER_8TAP_SHARP  ][idx2][idxh][idxv] =   \
        ff_##type##_8tap_sharp_##sz##dir##_mmi;

#define init_subpel2(idx, idxh, idxv, dir, type)      \
    init_subpel1(0, idx, idxh, idxv, 64, dir, type);  \
    init_subpel1(1, idx, idxh, idxv, 32, dir, type);  \
    init_subpel1(2, idx, idxh, idxv, 16, dir, type);  \
    init_subpel1(3, idx, idxh, idxv,  8, dir, type);  \
    init_subpel1(4, idx, idxh, idxv,  4, dir, type)

#define init_subpel3(idx, type)         \
    init_subpel2(idx, 1, 1, hv, type);  \
    init_subpel2(idx, 0, 1, v, type);   \
    init_subpel2(idx, 1, 0, h, type)

    init_subpel3(0, put);
    init_subpel3(1, avg);

#undef init_subpel1
#undef init_subpel2
#undef init_subpel3
}

static av_cold void vp9dsp_init_mmi(VP9DSPContext *dsp, int bpp)
{
    if (bpp == 8) {
        vp9dsp_mc_init_mmi(dsp);
    }
}
#endif  // #if HAVE_MMI

av_cold void ff_vp9dsp_init_mips(VP9DSPContext *dsp, int bpp)
{
#if HAVE_MSA || HAVE_MMI
    int cpu_flags = av_get_cpu_flags();
#endif

#if HAVE_MMI
    if (have_mmi(cpu_flags))
        vp9dsp_init_mmi(dsp, bpp);
#endif

#if HAVE_MSA
    if (have_msa(cpu_flags))
        vp9dsp_init_msa(dsp, bpp);
#endif
}
