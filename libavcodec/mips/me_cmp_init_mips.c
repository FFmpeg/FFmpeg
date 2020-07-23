/*
 * Copyright (c) 2015 Parag Salasakar (Parag.Salasakar@imgtec.com)
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

#include "libavutil/mips/cpu.h"
#include "me_cmp_mips.h"

av_cold void ff_me_cmp_init_mips(MECmpContext *c, AVCodecContext *avctx)
{
    int cpu_flags = av_get_cpu_flags();

    if (have_msa(cpu_flags)) {
#if BIT_DEPTH == 8
        c->pix_abs[0][0] = ff_pix_abs16_msa;
        c->pix_abs[0][1] = ff_pix_abs16_x2_msa;
        c->pix_abs[0][2] = ff_pix_abs16_y2_msa;
        c->pix_abs[0][3] = ff_pix_abs16_xy2_msa;
        c->pix_abs[1][0] = ff_pix_abs8_msa;
        c->pix_abs[1][1] = ff_pix_abs8_x2_msa;
        c->pix_abs[1][2] = ff_pix_abs8_y2_msa;
        c->pix_abs[1][3] = ff_pix_abs8_xy2_msa;

        c->hadamard8_diff[0] = ff_hadamard8_diff16_msa;
        c->hadamard8_diff[1] = ff_hadamard8_diff8x8_msa;

        c->hadamard8_diff[4] = ff_hadamard8_intra16_msa;
        c->hadamard8_diff[5] = ff_hadamard8_intra8x8_msa;

        c->sad[0] = ff_pix_abs16_msa;
        c->sad[1] = ff_pix_abs8_msa;
        c->sse[0] = ff_sse16_msa;
        c->sse[1] = ff_sse8_msa;
        c->sse[2] = ff_sse4_msa;
#endif
    }
}
