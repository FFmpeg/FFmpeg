/*
 * Copyright (c) 2008 Siarhei Siamashka <ssvb@users.sourceforge.net>
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

#include "libavutil/attributes.h"
#include "libavutil/float_dsp.h"
#include "cpu.h"
#include "float_dsp_arm.h"

void ff_vector_fmul_vfp(float *dst, const float *src0, const float *src1,
                        int len);

void ff_vector_fmul_reverse_vfp(float *dst, const float *src0,
                                const float *src1, int len);

av_cold void ff_float_dsp_init_vfp(AVFloatDSPContext *fdsp, int cpu_flags)
{
    if (!have_vfpv3(cpu_flags))
        fdsp->vector_fmul = ff_vector_fmul_vfp;
    fdsp->vector_fmul_reverse = ff_vector_fmul_reverse_vfp;
}
