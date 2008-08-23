/*
 * Lowpass IIR filter
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
 * @file lowpass.c
 * lowpass filter implementation
 */

#include "lowpass.h"

/**********************
 *         TODO:
 * support filters with order != 4
 * calculate coefficients for filter instead of taking approximate ones from the table
 *********************/

/** filter order */
#define LOWPASS_FILTER_ORDER 4

/**
 * IIR filter global parameters
 */
typedef struct FFLPFilterCoeffs{
    float gain;
    float c[LOWPASS_FILTER_ORDER];
}FFLPFilterCoeffs;

/**
 * filter data for 4th order IIR lowpass Butterworth filter
 */
static const FFLPFilterCoeffs lp_filter_coeffs[] = {
    { 9.398085e-01, { -0.0176648009,  0.0000000000, -0.4860288221,  0.0000000000 } },
    { 6.816645e-01, { -0.4646665999, -2.2127207402, -3.9912017501, -3.2380429984 } },
    { 4.998150e-01, { -0.2498216698, -1.3392807613, -2.7693097862, -2.6386277439 } },
    { 3.103469e-01, { -0.0965076902, -0.5977763360, -1.4972580903, -1.7740085241 } },
    { 2.346995e-01, { -0.0557639007, -0.3623690447, -1.0304538354, -1.3066051440 } },
    { 1.528432e-01, { -0.0261686639, -0.1473794606, -0.6204721225, -0.6514716536 } },
    { 6.917529e-02, { -0.0202414073,  0.0780167640, -0.5277442247,  0.3631641670 } },
    { 6.178391e-02, { -0.0223681543,  0.1069446609, -0.5615167033,  0.4883976841 } },
    { 5.298685e-02, { -0.0261686639,  0.1473794606, -0.6204721225,  0.6514716536 } },
    { 2.229030e-02, { -0.0647354087,  0.4172275190, -1.1412129810,  1.4320761385 } },
    { 1.693903e-02, { -0.0823177861,  0.5192354923, -1.3444768251,  1.6365345642 } },
    { 7.374053e-03, { -0.1481421788,  0.8650973862, -1.9894244796,  2.1544844308 } },
    { 5.541768e-03, { -0.1742301048,  0.9921936565, -2.2090801108,  2.3024482658 } },
};

/** cutoff ratios for lp_filter_data[] */
static const float lp_cutoff_ratios[] = {
    0.5000000000, 0.4535147392, 0.4166666667, 0.3628117914,
    0.3333333333, 0.2916666667, 0.2267573696, 0.2187500000,
    0.2083333333, 0.1587301587, 0.1458333333, 0.1133786848,
    0.1041666667,
};

/**
 * IIR filter state
 */
typedef struct FFLPFilterState{
    float x[LOWPASS_FILTER_ORDER];
}FFLPFilterState;

const struct FFLPFilterCoeffs* ff_lowpass_filter_init_coeffs(int order, float cutoff_ratio)
{
    int i, size;

    //we can create only order-4 filters with cutoff ratio <= 0.5 for now
    if(order != LOWPASS_FILTER_ORDER) return NULL;

    size = sizeof(lp_cutoff_ratios) / sizeof(lp_cutoff_ratios[0]);
    if(cutoff_ratio > lp_cutoff_ratios[0])
        return NULL;
    for(i = 0; i < size; i++){
        if(cutoff_ratio >= lp_cutoff_ratios[i])
            break;
    }
    if(i == size)
        i = size - 1;
    return &lp_filter_coeffs[i];
}

struct FFLPFilterState* ff_lowpass_filter_init_state(int order)
{
    if(order != LOWPASS_FILTER_ORDER) return NULL;
    return av_mallocz(sizeof(FFLPFilterState));
}

#define FILTER(i0, i1, i2, i3)                  \
    in =   *src * c->gain                       \
         + c->c[0]*s->x[i0] + c->c[1]*s->x[i1]  \
         + c->c[2]*s->x[i2] + c->c[3]*s->x[i3]; \
    res =  (s->x[i0] + in      )*1              \
         + (s->x[i1] + s->x[i3])*4              \
         +  s->x[i2]            *6;             \
    *dst = av_clip_int16(lrintf(res));          \
    s->x[i0] = in;                              \
    src += sstep;                               \
    dst += dstep;                               \

void ff_lowpass_filter(const struct FFLPFilterCoeffs *c, struct FFLPFilterState *s, int size, int16_t *src, int sstep, int16_t *dst, int dstep)
{
    int i;

    for(i = 0; i < size; i += 4){
        float in, res;

        FILTER(0, 1, 2, 3);
        FILTER(1, 2, 3, 0);
        FILTER(2, 3, 0, 1);
        FILTER(3, 0, 1, 2);
    }
}
