/*
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

#include "config.h"
#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/idctdsp.h"
#include "libavcodec/xvididct.h"
#include "idct_xvid.h"
#include "idctdsp.h"

av_cold void ff_xvididct_init_x86(IDCTDSPContext *c)
{
    int cpu_flags = av_get_cpu_flags();

    if (INLINE_MMX(cpu_flags)) {
        c->idct_put  = ff_idct_xvid_mmx_put;
        c->idct_add  = ff_idct_xvid_mmx_add;
        c->idct      = ff_idct_xvid_mmx;
        c->perm_type = FF_IDCT_PERM_NONE;
    }

    if (INLINE_MMXEXT(cpu_flags)) {
        c->idct_put  = ff_idct_xvid_mmxext_put;
        c->idct_add  = ff_idct_xvid_mmxext_add;
        c->idct      = ff_idct_xvid_mmxext;
        c->perm_type = FF_IDCT_PERM_NONE;
    }

    if (INLINE_SSE2(cpu_flags)) {
        c->idct_put  = ff_idct_xvid_sse2_put;
        c->idct_add  = ff_idct_xvid_sse2_add;
        c->idct      = ff_idct_xvid_sse2;
        c->perm_type = FF_IDCT_PERM_SSE2;
    }
}
