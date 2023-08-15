/*
 * Copyright (c) 2015 Peter Meerwald <pmeerw@pmeerw.net>
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

#include "g722dsp.h"
#include "mathops.h"

/*
 * quadrature mirror filter (QMF) coefficients (ITU-T G.722 Table 11) inlined
 * in code below: 3, -11, 12, 32, -210, 951, 3876, -805, 362, -156, 53, -11
 */

static void g722_apply_qmf(const int16_t *prev_samples, int xout[2])
{
    xout[1] = MUL16(*prev_samples++, 3);
    xout[0] = MUL16(*prev_samples++, -11);

    MAC16(xout[1], *prev_samples++, -11);
    MAC16(xout[0], *prev_samples++, 53);

    MAC16(xout[1], *prev_samples++, 12);
    MAC16(xout[0], *prev_samples++, -156);

    MAC16(xout[1], *prev_samples++, 32);
    MAC16(xout[0], *prev_samples++, 362);

    MAC16(xout[1], *prev_samples++, -210);
    MAC16(xout[0], *prev_samples++, -805);

    MAC16(xout[1], *prev_samples++, 951);
    MAC16(xout[0], *prev_samples++, 3876);

    MAC16(xout[1], *prev_samples++, 3876);
    MAC16(xout[0], *prev_samples++, 951);

    MAC16(xout[1], *prev_samples++, -805);
    MAC16(xout[0], *prev_samples++, -210);

    MAC16(xout[1], *prev_samples++, 362);
    MAC16(xout[0], *prev_samples++, 32);

    MAC16(xout[1], *prev_samples++, -156);
    MAC16(xout[0], *prev_samples++, 12);

    MAC16(xout[1], *prev_samples++, 53);
    MAC16(xout[0], *prev_samples++, -11);

    MAC16(xout[1], *prev_samples++, -11);
    MAC16(xout[0], *prev_samples++, 3);
}

av_cold void ff_g722dsp_init(G722DSPContext *c)
{
    c->apply_qmf = g722_apply_qmf;

#if ARCH_ARM
    ff_g722dsp_init_arm(c);
#elif ARCH_RISCV
    ff_g722dsp_init_riscv(c);
#elif ARCH_X86
    ff_g722dsp_init_x86(c);
#endif
}
