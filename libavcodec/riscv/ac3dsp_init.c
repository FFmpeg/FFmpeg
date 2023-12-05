/*
 * Copyright © 2023 Rémi Denis-Courmont.
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

#include <stddef.h>
#include <stdint.h>

#include "config.h"
#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavcodec/ac3dsp.h"

void ff_extract_exponents_rvb(uint8_t *exp, int32_t *coef, int nb_coefs);
void ff_float_to_fixed24_rvv(int32_t *dst, const float *src, size_t len);

av_cold void ff_ac3dsp_init_riscv(AC3DSPContext *c)
{
#if HAVE_RV
    int flags = av_get_cpu_flags();

    if (flags & AV_CPU_FLAG_RVB_ADDR) {
        if (flags & AV_CPU_FLAG_RVB_BASIC)
            c->extract_exponents = ff_extract_exponents_rvb;
        if (flags & AV_CPU_FLAG_RVV_F32)
            c->float_to_fixed24 = ff_float_to_fixed24_rvv;
    }
#endif
}
