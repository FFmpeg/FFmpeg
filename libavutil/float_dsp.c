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

#include "float_dsp.h"

static void vector_fmul_c(float *dst, const float *src0, const float *src1,
                          int len)
{
    int i;
    for (i = 0; i < len; i++)
        dst[i] = src0[i] * src1[i];
}

static void vector_fmac_scalar_c(float *dst, const float *src, float mul,
                                 int len)
{
    int i;
    for (i = 0; i < len; i++)
        dst[i] += src[i] * mul;
}

void avpriv_float_dsp_init(AVFloatDSPContext *fdsp, int bit_exact)
{
    fdsp->vector_fmul = vector_fmul_c;
    fdsp->vector_fmac_scalar = vector_fmac_scalar_c;

#if ARCH_ARM
    ff_float_dsp_init_arm(fdsp);
#elif ARCH_PPC
    ff_float_dsp_init_ppc(fdsp, bit_exact);
#elif ARCH_X86
    ff_float_dsp_init_x86(fdsp);
#endif
}
