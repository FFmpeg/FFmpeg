/*
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

#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "libavutil/libm.h"

#include "libavcodec/iirfilter.h"
#include "libavcodec/iirfilter.c"

#define FILT_ORDER 4
#define SIZE 1024

static void iir_filter_int16(const struct FFIIRFilterCoeffs *c,
                             struct FFIIRFilterState *s, int size,
                             const int16_t *src, ptrdiff_t sstep,
                             int16_t *dst, ptrdiff_t dstep)
{
    if (c->order == 2) {
        FILTER_O2(int16_t, S16)
    } else if (c->order == 4) {
        FILTER_BW_O4(int16_t, S16)
    } else {
        FILTER_DIRECT_FORM_II(int16_t, S16)
    }
}

int main(void)
{
    struct FFIIRFilterCoeffs *fcoeffs = NULL;
    struct FFIIRFilterState  *fstate  = NULL;
    float cutoff_coeff = 0.4;
    int16_t x[SIZE], y[SIZE];
    int i;

    fcoeffs = ff_iir_filter_init_coeffs(NULL, FF_FILTER_TYPE_BUTTERWORTH,
                                        FF_FILTER_MODE_LOWPASS, FILT_ORDER,
                                        cutoff_coeff, 0.0, 0.0);
    fstate  = ff_iir_filter_init_state(FILT_ORDER);

    for (i = 0; i < SIZE; i++)
        x[i] = lrint(0.75 * INT16_MAX * sin(0.5 * M_PI * i * i / SIZE));

    iir_filter_int16(fcoeffs, fstate, SIZE, x, 1, y, 1);

    for (i = 0; i < SIZE; i++)
        printf("%6d %6d\n", x[i], y[i]);

    ff_iir_filter_free_coeffsp(&fcoeffs);
    ff_iir_filter_free_statep(&fstate);
    return 0;
}
