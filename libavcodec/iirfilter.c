/*
 * IIR filter
 * Copyright (c) 2008 Konstantin Shishkov
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

/**
 * @file libavcodec/iirfilter.c
 * different IIR filters implementation
 */

#include "iirfilter.h"
#include <math.h>

/**
 * IIR filter global parameters
 */
typedef struct FFIIRFilterCoeffs{
    int   order;
    float gain;
    int   *cx;
    float *cy;
}FFIIRFilterCoeffs;

/**
 * IIR filter state
 */
typedef struct FFIIRFilterState{
    float x[1];
}FFIIRFilterState;

/// maximum supported filter order
#define MAXORDER 30

av_cold struct FFIIRFilterCoeffs* ff_iir_filter_init_coeffs(enum IIRFilterType filt_type,
                                                    enum IIRFilterMode filt_mode,
                                                    int order, float cutoff_ratio,
                                                    float stopband, float ripple)
{
    int i, j;
    FFIIRFilterCoeffs *c;
    double wa;
    double p[MAXORDER + 1][2];

    if(filt_type != FF_FILTER_TYPE_BUTTERWORTH || filt_mode != FF_FILTER_MODE_LOWPASS)
        return NULL;
    if(order <= 1 || (order & 1) || order > MAXORDER || cutoff_ratio >= 1.0)
        return NULL;

    c = av_malloc(sizeof(FFIIRFilterCoeffs));
    c->cx = av_malloc(sizeof(c->cx[0]) * ((order >> 1) + 1));
    c->cy = av_malloc(sizeof(c->cy[0]) * order);
    c->order = order;

    wa = 2 * tan(M_PI * 0.5 * cutoff_ratio);

    c->cx[0] = 1;
    for(i = 1; i < (order >> 1) + 1; i++)
        c->cx[i] = c->cx[i - 1] * (order - i + 1LL) / i;

    p[0][0] = 1.0;
    p[0][1] = 0.0;
    for(i = 1; i <= order; i++)
        p[i][0] = p[i][1] = 0.0;
    for(i = 0; i < order; i++){
        double zp[2];
        double th = (i + (order >> 1) + 0.5) * M_PI / order;
        double a_re, a_im, c_re, c_im;
        zp[0] = cos(th) * wa;
        zp[1] = sin(th) * wa;
        a_re = zp[0] + 2.0;
        c_re = zp[0] - 2.0;
        a_im =
        c_im = zp[1];
        zp[0] = (a_re * c_re + a_im * c_im) / (c_re * c_re + c_im * c_im);
        zp[1] = (a_im * c_re - a_re * c_im) / (c_re * c_re + c_im * c_im);

        for(j = order; j >= 1; j--)
        {
            a_re = p[j][0];
            a_im = p[j][1];
            p[j][0] = a_re*zp[0] - a_im*zp[1] + p[j-1][0];
            p[j][1] = a_re*zp[1] + a_im*zp[0] + p[j-1][1];
        }
        a_re    = p[0][0]*zp[0] - p[0][1]*zp[1];
        p[0][1] = p[0][0]*zp[1] + p[0][1]*zp[0];
        p[0][0] = a_re;
    }
    c->gain = p[order][0];
    for(i = 0; i < order; i++){
        c->gain += p[i][0];
        c->cy[i] = (-p[i][0] * p[order][0] + -p[i][1] * p[order][1]) /
                   (p[order][0] * p[order][0] + p[order][1] * p[order][1]);
    }
    c->gain /= 1 << order;

    return c;
}

av_cold struct FFIIRFilterState* ff_iir_filter_init_state(int order)
{
    FFIIRFilterState* s = av_mallocz(sizeof(FFIIRFilterState) + sizeof(s->x[0]) * (order - 1));
    return s;
}

#define FILTER(i0, i1, i2, i3)                    \
    in =   *src * c->gain                         \
         + c->cy[0]*s->x[i0] + c->cy[1]*s->x[i1]  \
         + c->cy[2]*s->x[i2] + c->cy[3]*s->x[i3]; \
    res =  (s->x[i0] + in      )*1                \
         + (s->x[i1] + s->x[i3])*4                \
         +  s->x[i2]            *6;               \
    *dst = av_clip_int16(lrintf(res));            \
    s->x[i0] = in;                                \
    src += sstep;                                 \
    dst += dstep;                                 \

void ff_iir_filter(const struct FFIIRFilterCoeffs *c, struct FFIIRFilterState *s, int size, const int16_t *src, int sstep, int16_t *dst, int dstep)
{
    int i;

    if(c->order == 4){
        for(i = 0; i < size; i += 4){
            float in, res;

            FILTER(0, 1, 2, 3);
            FILTER(1, 2, 3, 0);
            FILTER(2, 3, 0, 1);
            FILTER(3, 0, 1, 2);
        }
    }else{
        for(i = 0; i < size; i++){
            int j;
            float in, res;
            in = *src * c->gain;
            for(j = 0; j < c->order; j++)
                in += c->cy[j] * s->x[j];
            res = s->x[0] + in + s->x[c->order >> 1] * c->cx[c->order >> 1];
            for(j = 1; j < c->order >> 1; j++)
                res += (s->x[j] + s->x[c->order - j]) * c->cx[j];
            for(j = 0; j < c->order - 1; j++)
                s->x[j] = s->x[j + 1];
            *dst = av_clip_int16(lrintf(res));
            s->x[c->order - 1] = in;
            src += sstep;
            dst += sstep;
        }
    }
}

av_cold void ff_iir_filter_free_state(struct FFIIRFilterState *state)
{
    av_free(state);
}

av_cold void ff_iir_filter_free_coeffs(struct FFIIRFilterCoeffs *coeffs)
{
    if(coeffs){
        av_free(coeffs->cx);
        av_free(coeffs->cy);
    }
    av_free(coeffs);
}

#ifdef TEST
#define FILT_ORDER 4
#define SIZE 1024
int main(void)
{
    struct FFIIRFilterCoeffs *fcoeffs = NULL;
    struct FFIIRFilterState  *fstate  = NULL;
    float cutoff_coeff = 0.4;
    int16_t x[SIZE], y[SIZE];
    int i;
    FILE* fd;

    fcoeffs = ff_iir_filter_init_coeffs(FF_FILTER_TYPE_BUTTERWORTH,
                                        FF_FILTER_MODE_LOWPASS, FILT_ORDER,
                                        cutoff_coeff, 0.0, 0.0);
    fstate  = ff_iir_filter_init_state(FILT_ORDER);

    for (i = 0; i < SIZE; i++) {
        x[i] = lrint(0.75 * INT16_MAX * sin(0.5*M_PI*i*i/SIZE));
    }

    ff_iir_filter(fcoeffs, fstate, SIZE, x, 1, y, 1);

    fd = fopen("in.bin", "w");
    fwrite(x, sizeof(x[0]), SIZE, fd);
    fclose(fd);

    fd = fopen("out.bin", "w");
    fwrite(y, sizeof(y[0]), SIZE, fd);
    fclose(fd);

    ff_iir_filter_free_coeffs(fcoeffs);
    ff_iir_filter_free_state(fstate);
    return 0;
}
#endif /* TEST */
