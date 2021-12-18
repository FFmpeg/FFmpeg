/*
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 * Contributed by Hao Chen <chenhao@loongson.cn>
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
#include "libavutil/attributes.h"
#include "libavcodec/vc1dsp.h"
#include "vc1dsp_loongarch.h"

#define FN_ASSIGN(OP, X, Y, INSN) \
    dsp->OP##vc1_mspel_pixels_tab[1][X+4*Y] = ff_##OP##vc1_mspel_mc##X##Y##INSN; \
    dsp->OP##vc1_mspel_pixels_tab[0][X+4*Y] = ff_##OP##vc1_mspel_mc##X##Y##_16##INSN

#define FN_ASSIGN_V(OP, Y, INSN) \
    dsp->OP##vc1_mspel_pixels_tab[0][4*Y] = ff_##OP##vc1_mspel_mc0##Y##_16##INSN

#define FN_ASSIGN_H(OP, X, INSN) \
    dsp->OP##vc1_mspel_pixels_tab[0][X] = ff_##OP##vc1_mspel_mc##X##0_16##INSN

av_cold void ff_vc1dsp_init_loongarch(VC1DSPContext *dsp)
{
    int cpu_flags = av_get_cpu_flags();

    if (have_lasx(cpu_flags)) {
        dsp->vc1_inv_trans_8x8    = ff_vc1_inv_trans_8x8_lasx;
        dsp->vc1_inv_trans_4x8    = ff_vc1_inv_trans_4x8_lasx;
        dsp->vc1_inv_trans_8x4    = ff_vc1_inv_trans_8x4_lasx;
        dsp->vc1_inv_trans_4x4    = ff_vc1_inv_trans_4x4_lasx;
        dsp->vc1_inv_trans_8x8_dc = ff_vc1_inv_trans_8x8_dc_lasx;
        dsp->vc1_inv_trans_4x8_dc = ff_vc1_inv_trans_4x8_dc_lasx;
        dsp->vc1_inv_trans_8x4_dc = ff_vc1_inv_trans_8x4_dc_lasx;
        dsp->vc1_inv_trans_4x4_dc = ff_vc1_inv_trans_4x4_dc_lasx;
        FN_ASSIGN(put_, 1, 1, _lasx);
        FN_ASSIGN(put_, 1, 2, _lasx);
        FN_ASSIGN(put_, 1, 3, _lasx);
        FN_ASSIGN(put_, 2, 1, _lasx);
        FN_ASSIGN(put_, 2, 2, _lasx);
        FN_ASSIGN(put_, 2, 3, _lasx);
        FN_ASSIGN(put_, 3, 1, _lasx);
        FN_ASSIGN(put_, 3, 2, _lasx);
        FN_ASSIGN(put_, 3, 3, _lasx);
        FN_ASSIGN_V(put_, 1, _lasx);
        FN_ASSIGN_V(put_, 2, _lasx);
        FN_ASSIGN_V(put_, 3, _lasx);
        FN_ASSIGN_H(put_, 1, _lasx);
        FN_ASSIGN_H(put_, 2, _lasx);
        FN_ASSIGN_H(put_, 3, _lasx);
        dsp->put_no_rnd_vc1_chroma_pixels_tab[0] = ff_put_no_rnd_vc1_chroma_mc8_lasx;
    }
}
