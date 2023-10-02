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

#include <stdint.h>

#include "config.h"
#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavcodec/ac3dsp.h"

void ff_extract_exponents_rvb(uint8_t *exp, int32_t *coef, int nb_coefs);

av_cold void ff_ac3dsp_init_riscv(AC3DSPContext *c)
{
    int flags = av_get_cpu_flags();

    if (flags & AV_CPU_FLAG_RVB_ADDR) {
        if (flags & AV_CPU_FLAG_RVB_BASIC)
            c->extract_exponents = ff_extract_exponents_rvb;
    }
}
