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
#include <complex.h>
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

struct FFIIRFilterCoeffs* ff_iir_filter_init_coeffs(enum IIRFilterType filt_type,
                                                    enum IIRFilterMode filt_mode,
                                                    int order, float cutoff_ratio,
                                                    float stopband, float ripple)
{
    int i, j, size;
    FFIIRFilterCoeffs *c;
    double wa;
    complex p[MAXORDER + 1];

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

    p[0] = 1.0;
    for(i = 1; i <= order; i++)
        p[i] = 0.0;
    for(i = 0; i < order; i++){
        complex zp;
        double th = (i + (order >> 1) + 0.5) * M_PI / order;
        zp = cexp(I*th) * wa;
        zp = (zp + 2.0) / (zp - 2.0);

        for(j = order; j >= 1; j--)
            p[j] = zp*p[j] + p[j - 1];
        p[0] *= zp;
    }
    c->gain = creal(p[order]);
    for(i = 0; i < order; i++){
        c->gain += creal(p[i]);
        c->cy[i] = creal(-p[i] / p[order]);
    }
    c->gain /= 1 << order;

    return c;
}

struct FFIIRFilterState* ff_iir_filter_init_state(int order)
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

void ff_iir_filter_free_state(struct FFIIRFilterState *state)
{
    av_free(state);
}

void ff_iir_filter_free_coeffs(struct FFIIRFilterCoeffs *coeffs)
{
    if(coeffs){
        av_free(coeffs->cx);
        av_free(coeffs->cy);
    }
    av_free(coeffs);
}

