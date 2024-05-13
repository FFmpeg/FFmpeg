/*
 * Copyright (c) 2024 Institue of Software Chinese Academy of Sciences (ISCAS).
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lervvr General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lervvr General Public License for more details.
 *
 * You should have received a copy of the GNU Lervvr General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/riscv/cpu.h"
#include "libavcodec/vp9dsp.h"
#include "vp9dsp.h"

static av_cold void vp9dsp_mc_init_riscv(VP9DSPContext *dsp, int bpp)
{
#if HAVE_RV
    int flags = av_get_cpu_flags();

# if __riscv_xlen >= 64
    if (bpp == 8 && (flags & AV_CPU_FLAG_RV_MISALIGNED)) {

#define init_fpel(idx1, sz)                                           \
    dsp->mc[idx1][FILTER_8TAP_SMOOTH ][0][0][0] = ff_copy##sz##_rvi;  \
    dsp->mc[idx1][FILTER_8TAP_REGULAR][0][0][0] = ff_copy##sz##_rvi;  \
    dsp->mc[idx1][FILTER_8TAP_SHARP  ][0][0][0] = ff_copy##sz##_rvi;  \
    dsp->mc[idx1][FILTER_BILINEAR    ][0][0][0] = ff_copy##sz##_rvi

    init_fpel(0, 64);
    init_fpel(1, 32);
    init_fpel(2, 16);
    init_fpel(3, 8);
    init_fpel(4, 4);

#undef init_fpel
    }
# endif

#endif
}

static av_cold void vp9dsp_intrapred_init_riscv(VP9DSPContext *dsp, int bpp)
{
#if HAVE_RV
    int flags = av_get_cpu_flags();

# if __riscv_xlen >= 64
    if (bpp == 8 && (flags & AV_CPU_FLAG_RVB_ADDR)) {
        dsp->intra_pred[TX_32X32][VERT_PRED] = ff_v_32x32_rvi;
        dsp->intra_pred[TX_16X16][VERT_PRED] = ff_v_16x16_rvi;
        dsp->intra_pred[TX_8X8][VERT_PRED] = ff_v_8x8_rvi;
    }
# endif
#if HAVE_RVV
    if (bpp == 8 && flags & AV_CPU_FLAG_RVV_I64 && ff_rv_vlen_least(128)) {
        dsp->intra_pred[TX_8X8][DC_PRED] = ff_dc_8x8_rvv;
        dsp->intra_pred[TX_8X8][LEFT_DC_PRED] = ff_dc_left_8x8_rvv;
        dsp->intra_pred[TX_8X8][DC_127_PRED] = ff_dc_127_8x8_rvv;
        dsp->intra_pred[TX_8X8][DC_128_PRED] = ff_dc_128_8x8_rvv;
        dsp->intra_pred[TX_8X8][DC_129_PRED] = ff_dc_129_8x8_rvv;
        dsp->intra_pred[TX_8X8][TOP_DC_PRED] = ff_dc_top_8x8_rvv;
    }

    if (bpp == 8 && flags & AV_CPU_FLAG_RVV_I32 && ff_rv_vlen_least(128)) {
        dsp->intra_pred[TX_32X32][DC_PRED] = ff_dc_32x32_rvv;
        dsp->intra_pred[TX_16X16][DC_PRED] = ff_dc_16x16_rvv;
        dsp->intra_pred[TX_32X32][LEFT_DC_PRED] = ff_dc_left_32x32_rvv;
        dsp->intra_pred[TX_16X16][LEFT_DC_PRED] = ff_dc_left_16x16_rvv;
        dsp->intra_pred[TX_32X32][DC_127_PRED] = ff_dc_127_32x32_rvv;
        dsp->intra_pred[TX_16X16][DC_127_PRED] = ff_dc_127_16x16_rvv;
        dsp->intra_pred[TX_32X32][DC_128_PRED] = ff_dc_128_32x32_rvv;
        dsp->intra_pred[TX_16X16][DC_128_PRED] = ff_dc_128_16x16_rvv;
        dsp->intra_pred[TX_32X32][DC_129_PRED] = ff_dc_129_32x32_rvv;
        dsp->intra_pred[TX_16X16][DC_129_PRED] = ff_dc_129_16x16_rvv;
        dsp->intra_pred[TX_32X32][TOP_DC_PRED] = ff_dc_top_32x32_rvv;
        dsp->intra_pred[TX_16X16][TOP_DC_PRED] = ff_dc_top_16x16_rvv;
        dsp->intra_pred[TX_32X32][HOR_PRED] = ff_h_32x32_rvv;
        dsp->intra_pred[TX_16X16][HOR_PRED] = ff_h_16x16_rvv;
        dsp->intra_pred[TX_8X8][HOR_PRED] = ff_h_8x8_rvv;
    }
#endif
#endif
}

av_cold void ff_vp9dsp_init_riscv(VP9DSPContext *dsp, int bpp, int bitexact)
{
    vp9dsp_intrapred_init_riscv(dsp, bpp);
    vp9dsp_mc_init_riscv(dsp, bpp);
}
