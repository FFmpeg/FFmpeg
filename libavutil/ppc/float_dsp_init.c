/*
 * Copyright (c) 2006 Luca Barbato <lu_zero@gentoo.org>
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

#include "config.h"
#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/float_dsp.h"
#include "libavutil/ppc/cpu.h"
#include "float_dsp_altivec.h"
#include "float_dsp_vsx.h"

av_cold void ff_float_dsp_init_ppc(AVFloatDSPContext *fdsp, int bit_exact)
{
    if (PPC_ALTIVEC(av_get_cpu_flags())) {
        fdsp->vector_fmul = ff_vector_fmul_altivec;
        fdsp->vector_fmul_add = ff_vector_fmul_add_altivec;
        fdsp->vector_fmul_reverse = ff_vector_fmul_reverse_altivec;

        if (!bit_exact) {
            fdsp->vector_fmul_window = ff_vector_fmul_window_altivec;
        }
    }

    // The disabled function below are near identical to altivec and have
    // been disabled to reduce code duplication
    if (PPC_VSX(av_get_cpu_flags())) {
//         fdsp->vector_fmul = ff_vector_fmul_vsx;
        fdsp->vector_fmul_add = ff_vector_fmul_add_vsx;
//         fdsp->vector_fmul_reverse = ff_vector_fmul_reverse_vsx;

//         if (!bit_exact) {
//             fdsp->vector_fmul_window = ff_vector_fmul_window_vsx;
//         }
    }
}
