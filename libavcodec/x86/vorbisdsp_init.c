/*
 * Copyright (C) 2006 Loren Merritt <lorenm@u.washington.edu>
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

#include "config.h"
#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/vorbisdsp.h"

void ff_vorbis_inverse_coupling_3dnow(float *mag, float *ang,
                                      intptr_t blocksize);
void ff_vorbis_inverse_coupling_sse(float *mag, float *ang,
                                    intptr_t blocksize);

av_cold void ff_vorbisdsp_init_x86(VorbisDSPContext *dsp)
{
    int cpu_flags = av_get_cpu_flags();

#if ARCH_X86_32
    if (EXTERNAL_AMD3DNOW(cpu_flags))
        dsp->vorbis_inverse_coupling = ff_vorbis_inverse_coupling_3dnow;
#endif /* ARCH_X86_32 */
    if (EXTERNAL_SSE(cpu_flags))
        dsp->vorbis_inverse_coupling = ff_vorbis_inverse_coupling_sse;
}
