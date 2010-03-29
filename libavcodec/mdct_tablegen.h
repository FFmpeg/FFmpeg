/*
 * Header file for hardcoded MDCT tables
 *
 * Copyright (c) 2009 Reimar Döffinger <Reimar.Doeffinger@gmx.de>
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

#include <assert.h>
// do not use libavutil/mathematics.h since this is compiled both
// for the host and the target and config.h is only valid for the target
#include <math.h>
#include "../libavutil/attributes.h"

#if !CONFIG_HARDCODED_TABLES
SINETABLE(  32);
SINETABLE(  64);
SINETABLE( 128);
SINETABLE( 256);
SINETABLE( 512);
SINETABLE(1024);
SINETABLE(2048);
SINETABLE(4096);
#else
#include "libavcodec/mdct_tables.h"
#endif

SINETABLE_CONST float * const ff_sine_windows[] = {
    NULL, NULL, NULL, NULL, NULL, // unused
    ff_sine_32 , ff_sine_64 ,
    ff_sine_128, ff_sine_256, ff_sine_512, ff_sine_1024, ff_sine_2048, ff_sine_4096
};

// Generate a sine window.
av_cold void ff_sine_window_init(float *window, int n) {
    int i;
    for(i = 0; i < n; i++)
        window[i] = sinf((i + 0.5) * (M_PI / (2.0 * n)));
}

av_cold void ff_init_ff_sine_windows(int index) {
    assert(index >= 0 && index < FF_ARRAY_ELEMS(ff_sine_windows));
#if !CONFIG_HARDCODED_TABLES
    ff_sine_window_init(ff_sine_windows[index], 1 << index);
#endif
}
