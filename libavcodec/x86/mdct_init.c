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

#include "mdct.h"

av_cold void ff_mdct_init_x86(FFTContext *s)
{
    int cpu_flags = av_get_cpu_flags();

#if ARCH_X86_32
    if (EXTERNAL_AMD3DNOW(cpu_flags)) {
        s->imdct_calc = ff_imdct_calc_3dnow;
        s->imdct_half = ff_imdct_half_3dnow;
    }

    if (EXTERNAL_AMD3DNOWEXT(cpu_flags)) {
        s->imdct_calc = ff_imdct_calc_3dnowext;
        s->imdct_half = ff_imdct_half_3dnowext;
    }
#endif /* ARCH_X86_32 */

    if (EXTERNAL_SSE(cpu_flags)) {
        s->imdct_calc  = ff_imdct_calc_sse;
        s->imdct_half  = ff_imdct_half_sse;
    }

    if (EXTERNAL_AVX_FAST(cpu_flags) && s->nbits >= 5) {
        s->imdct_half      = ff_imdct_half_avx;
    }
}
