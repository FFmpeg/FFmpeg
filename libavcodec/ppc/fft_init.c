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

#include "libavutil/cpu.h"
#include "libavutil/ppc/cpu.h"

#include "libavcodec/fft.h"

void ff_fft_calc_interleave_altivec(FFTContext *s, FFTComplex *z);

av_cold void ff_fft_init_ppc(FFTContext *s)
{
#if HAVE_GNU_AS && HAVE_ALTIVEC && HAVE_BIGENDIAN
    if (!PPC_ALTIVEC(av_get_cpu_flags()))
        return;

    s->fft_calc   = ff_fft_calc_interleave_altivec;
#endif /* HAVE_GNU_AS && HAVE_ALTIVEC && HAVE_BIGENDIAN */
}
