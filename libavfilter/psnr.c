/*
 * Copyright (c) 2015 Ronald S. Bultje <rsbultje@gmail.com>
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

#include <stddef.h>
#include <stdint.h>

#include "psnr.h"

static uint64_t sse_line_8bit(const uint8_t *main_line,  const uint8_t *ref_line, int outw)
{
    int j;
    unsigned m2 = 0;

    for (j = 0; j < outw; j++) {
        unsigned error = main_line[j] - ref_line[j];

        m2 += error * error;
    }

    return m2;
}

static uint64_t sse_line_16bit(const uint8_t *_main_line, const uint8_t *_ref_line, int outw)
{
    int j;
    uint64_t m2 = 0;
    const uint16_t *main_line = (const uint16_t *) _main_line;
    const uint16_t *ref_line = (const uint16_t *) _ref_line;

    for (j = 0; j < outw; j++) {
        unsigned error = main_line[j] - ref_line[j];

        m2 += error * error;
    }

    return m2;
}

void ff_psnr_init(PSNRDSPContext *dsp, int bpp)
{
    dsp->sse_line = bpp > 8 ? sse_line_16bit : sse_line_8bit;
#if ARCH_X86
    ff_psnr_init_x86(dsp, bpp);
#endif
}
