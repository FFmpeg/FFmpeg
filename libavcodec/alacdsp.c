/*
 * ALAC (Apple Lossless Audio Codec) decoder
 * Copyright (c) 2005 David Hammerton
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

#include "libavutil/attributes.h"
#include "alacdsp.h"
#include "config.h"

static void decorrelate_stereo(int32_t *buffer[2], int nb_samples,
                               int decorr_shift, int decorr_left_weight)
{
    int i;

    for (i = 0; i < nb_samples; i++) {
        uint32_t a, b;

        a = buffer[0][i];
        b = buffer[1][i];

        a -= (int)(b * decorr_left_weight) >> decorr_shift;
        b += a;

        buffer[0][i] = b;
        buffer[1][i] = a;
    }
}

static void append_extra_bits(int32_t *buffer[2], int32_t *extra_bits_buffer[2],
                              int extra_bits, int channels, int nb_samples)
{
    int i, ch;

    for (ch = 0; ch < channels; ch++)
        for (i = 0; i < nb_samples; i++)
            buffer[ch][i] = ((unsigned)buffer[ch][i] << extra_bits) | extra_bits_buffer[ch][i];
}

av_cold void ff_alacdsp_init(ALACDSPContext *c)
{
    c->decorrelate_stereo   = decorrelate_stereo;
    c->append_extra_bits[0] =
    c->append_extra_bits[1] = append_extra_bits;

#if ARCH_X86
    ff_alacdsp_init_x86(c);
#endif
}
