/*
 * copyright (c) 2008 Michael Niedermayer <michaelni@gmx.at>
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

#ifndef AVCODEC_SYNTH_FILTER_H
#define AVCODEC_SYNTH_FILTER_H

#include "fft.h"

typedef struct SynthFilterContext {
    void (*synth_filter_float)(FFTContext *imdct,
                               float *synth_buf_ptr, int *synth_buf_offset,
                               float synth_buf2[32], const float window[512],
                               float out[32], const float in[32],
                               float scale);
} SynthFilterContext;

void ff_synth_filter_init(SynthFilterContext *c);
void ff_synth_filter_init_arm(SynthFilterContext *c);
void ff_synth_filter_init_x86(SynthFilterContext *c);

#endif /* AVCODEC_SYNTH_FILTER_H */
