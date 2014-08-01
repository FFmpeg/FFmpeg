/*
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

#include "config.h"
#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/idctdsp.h"
#include "libavcodec/xvididct.h"
#include "idct_xvid.h"
#include "idctdsp.h"

static const uint8_t idct_sse2_row_perm[8] = { 0, 4, 1, 5, 2, 6, 3, 7 };

static av_cold void init_scantable_permutation_sse2(uint8_t *idct_permutation,
                                                    enum idct_permutation_type perm_type)
{
    int i;

    for (i = 0; i < 64; i++)
        idct_permutation[i] = (i & 0x38) | idct_sse2_row_perm[i & 7];
}

av_cold void ff_xvididct_init_x86(IDCTDSPContext *c)
{
    int cpu_flags = av_get_cpu_flags();

    if (INLINE_MMX(cpu_flags)) {
        c->idct_put  = ff_idct_xvid_mmx_put;
        c->idct_add  = ff_idct_xvid_mmx_add;
        c->idct      = ff_idct_xvid_mmx;
    }

    if (INLINE_MMXEXT(cpu_flags)) {
        c->idct_put  = ff_idct_xvid_mmxext_put;
        c->idct_add  = ff_idct_xvid_mmxext_add;
        c->idct      = ff_idct_xvid_mmxext;
    }

    if (INLINE_SSE2(cpu_flags)) {
        c->idct_put  = ff_idct_xvid_sse2_put;
        c->idct_add  = ff_idct_xvid_sse2_add;
        c->idct      = ff_idct_xvid_sse2;
        c->perm_type = FF_IDCT_PERM_SSE2;

        init_scantable_permutation_sse2(c->idct_permutation, c->perm_type);
    }
}
