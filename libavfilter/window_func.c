/*
 * Copyright (c) 2015 Paul B Mahol
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

#include <math.h>

#include "libavutil/avassert.h"
#include "window_func.h"

void ff_generate_window_func(float *lut, int N, int win_func, float *overlap)
{
    int n;

    switch (win_func) {
    case WFUNC_RECT:
        for (n = 0; n < N; n++)
            lut[n] = 1.;
        *overlap = 0.;
        break;
    case WFUNC_BARTLETT:
        for (n = 0; n < N; n++)
            lut[n] = 1.-fabs((n-(N-1)/2.)/((N-1)/2.));
        *overlap = 0.5;
        break;
    case WFUNC_HANNING:
        for (n = 0; n < N; n++)
            lut[n] = .5*(1-cos(2*M_PI*n/(N-1)));
        *overlap = 0.5;
        break;
    case WFUNC_HAMMING:
        for (n = 0; n < N; n++)
            lut[n] = .54-.46*cos(2*M_PI*n/(N-1));
        *overlap = 0.5;
        break;
    case WFUNC_BLACKMAN:
        for (n = 0; n < N; n++)
            lut[n] = .42659-.49656*cos(2*M_PI*n/(N-1))+.076849*cos(4*M_PI*n/(N-1));
        *overlap = 0.661;
        break;
    case WFUNC_WELCH:
        for (n = 0; n < N; n++)
            lut[n] = 1.-(n-(N-1)/2.)/((N-1)/2.)*(n-(N-1)/2.)/((N-1)/2.);
        *overlap = 0.293;
        break;
    case WFUNC_FLATTOP:
        for (n = 0; n < N; n++)
            lut[n] = 1.-1.985844164102*cos( 2*M_PI*n/(N-1))+1.791176438506*cos( 4*M_PI*n/(N-1))-
                        1.282075284005*cos( 6*M_PI*n/(N-1))+0.667777530266*cos( 8*M_PI*n/(N-1))-
                        0.240160796576*cos(10*M_PI*n/(N-1))+0.056656381764*cos(12*M_PI*n/(N-1))-
                        0.008134974479*cos(14*M_PI*n/(N-1))+0.000624544650*cos(16*M_PI*n/(N-1))-
                        0.000019808998*cos(18*M_PI*n/(N-1))+0.000000132974*cos(20*M_PI*n/(N-1));
        *overlap = 0.841;
        break;
    case WFUNC_BHARRIS:
        for (n = 0; n < N; n++)
            lut[n] = 0.35875-0.48829*cos(2*M_PI*n/(N-1))+0.14128*cos(4*M_PI*n/(N-1))-0.01168*cos(6*M_PI*n/(N-1));
        *overlap = 0.661;
        break;
    case WFUNC_BNUTTALL:
        for (n = 0; n < N; n++)
            lut[n] = 0.3635819-0.4891775*cos(2*M_PI*n/(N-1))+0.1365995*cos(4*M_PI*n/(N-1))-0.0106411*cos(6*M_PI*n/(N-1));
        *overlap = 0.661;
        break;
    case WFUNC_BHANN:
        for (n = 0; n < N; n++)
            lut[n] = 0.62-0.48*fabs(n/(double)(N-1)-.5)-0.38*cos(2*M_PI*n/(N-1));
        *overlap = 0.5;
        break;
    case WFUNC_SINE:
        for (n = 0; n < N; n++)
            lut[n] = sin(M_PI*n/(N-1));
        *overlap = 0.75;
        break;
    case WFUNC_NUTTALL:
        for (n = 0; n < N; n++)
            lut[n] = 0.355768-0.487396*cos(2*M_PI*n/(N-1))+0.144232*cos(4*M_PI*n/(N-1))-0.012604*cos(6*M_PI*n/(N-1));
        *overlap = 0.663;
        break;
    case WFUNC_LANCZOS:
#define SINC(x) (!(x)) ? 1 : sin(M_PI * (x))/(M_PI * (x));
        for (n = 0; n < N; n++)
            lut[n] = SINC((2.*n)/(N-1)-1);
        *overlap = 0.75;
        break;
    case WFUNC_GAUSS:
#define SQR(x) ((x)*(x))
        for (n = 0; n < N; n++)
            lut[n] = exp(-0.5 * SQR((n-(N-1)/2)/(0.4*(N-1)/2.f)));
        *overlap = 0.75;
        break;
    case WFUNC_TUKEY:
        for (n = 0; n < N; n++) {
            float M = (N-1)/2.;

            if (FFABS(n - M) >= 0.3 * M) {
                lut[n] = 0.5 * (1 + cos((M_PI*(FFABS(n - M) - 0.3 * M))/((1 - 0.3) * M)));
            } else {
                lut[n] = 1;
            }
        }
        *overlap = 0.33;
        break;
    default:
        av_assert0(0);
    }
}
