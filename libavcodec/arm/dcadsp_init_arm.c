/*
 * Copyright (c) 2010 Mans Rullgard <mans@mansr.com>
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

#include "libavutil/arm/cpu.h"
#include "libavutil/attributes.h"
#include "libavcodec/dcadsp.h"

void ff_dca_lfe_fir_neon(float *out, const float *in, const float *coefs,
                         int decifactor, float scale);

av_cold void ff_dcadsp_init_arm(DCADSPContext *s)
{
    int cpu_flags = av_get_cpu_flags();

    if (have_neon(cpu_flags))
        s->lfe_fir = ff_dca_lfe_fir_neon;
}
