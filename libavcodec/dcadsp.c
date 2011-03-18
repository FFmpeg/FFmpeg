/*
 * Copyright (c) 2004 Gildas Bazin
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
#include "dcadsp.h"

static void dca_lfe_fir_c(float *out, const float *in, const float *coefs,
                          int decifactor, float scale)
{
    float *out2 = out + decifactor;
    const float *cf0 = coefs;
    const float *cf1 = coefs + 256;
    int j, k;

    /* One decimated sample generates 2*decifactor interpolated ones */
    for (k = 0; k < decifactor; k++) {
        float v0 = 0.0;
        float v1 = 0.0;
        for (j = 0; j < 256 / decifactor; j++) {
            float s = in[-j];
            v0 += s * *cf0++;
            v1 += s * *--cf1;
        }
        *out++  = v0 * scale;
        *out2++ = v1 * scale;
    }
}

void ff_dcadsp_init(DCADSPContext *s)
{
    s->lfe_fir = dca_lfe_fir_c;
    if (ARCH_ARM) ff_dcadsp_init_arm(s);
}
