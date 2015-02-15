/*
 * Copyright (c) 2015 Peter Meerwald <pmeerw@pmeerw.net>
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

#include "g722dsp.h"
#include "mathops.h"

/*
 * quadrature mirror filter (QMF) coefficients (ITU-T G.722 Table 11) inlined
 * in code below: 3, -11, 12, 32, -210, 951, 3876, -805, 362, -156, 53, -11
 */
static const int16_t qmf_coeffs[12] = {
    3, -11, 12, 32, -210, 951, 3876, -805, 362, -156, 53, -11,
};

static void g722_apply_qmf(const int16_t *prev_samples, int *xout1, int *xout2)
{
    int i;

    *xout1 = 0;
    *xout2 = 0;
    for (i = 0; i < 12; i++) {
        MAC16(*xout2, prev_samples[2*i  ], qmf_coeffs[i   ]);
        MAC16(*xout1, prev_samples[2*i+1], qmf_coeffs[11-i]);
    }
}

av_cold void ff_g722dsp_init(G722DSPContext *c)
{
    c->apply_qmf = g722_apply_qmf;
}
