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

static av_cold void vp9dsp_intrapred_init_rvv(VP9DSPContext *dsp, int bpp)
{
    #if HAVE_RVV
        int flags = av_get_cpu_flags();

        if (bpp == 8 && flags & AV_CPU_FLAG_RVV_I64 && ff_get_rv_vlenb() >= 16) {
            dsp->intra_pred[TX_8X8][DC_PRED] = ff_dc_8x8_rvv;
            dsp->intra_pred[TX_8X8][LEFT_DC_PRED] = ff_dc_left_8x8_rvv;
            dsp->intra_pred[TX_8X8][DC_127_PRED] = ff_dc_127_8x8_rvv;
            dsp->intra_pred[TX_8X8][DC_128_PRED] = ff_dc_128_8x8_rvv;
            dsp->intra_pred[TX_8X8][DC_129_PRED] = ff_dc_129_8x8_rvv;
            dsp->intra_pred[TX_8X8][TOP_DC_PRED] = ff_dc_top_8x8_rvv;
        }

        if (bpp == 8 && flags & AV_CPU_FLAG_RVV_I32 && ff_get_rv_vlenb() >= 16) {
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
        }
    #endif
}

av_cold void ff_vp9dsp_init_riscv(VP9DSPContext *dsp, int bpp, int bitexact)
{
    vp9dsp_intrapred_init_rvv(dsp, bpp);
}
