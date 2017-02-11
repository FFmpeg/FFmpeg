/*
 * Opus decoder/demuxer common functions
 * Copyright (c) 2012 Andrew D'Addesio
 * Copyright (c) 2013-2014 Mozilla Corporation
 * Copyright (c) 2016 Rostislav Pehlivanov <atomnuker@gmail.com>
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

#ifndef AVCODEC_OPUS_CELT_H
#define AVCODEC_OPUS_CELT_H

#include "opus.h"

#include "mdct15.h"
#include "libavutil/float_dsp.h"
#include "libavutil/libm.h"

#define CELT_VECTORS                 11
#define CELT_ALLOC_STEPS             6
#define CELT_FINE_OFFSET             21
#define CELT_MAX_FINE_BITS           8
#define CELT_NORM_SCALE              16384
#define CELT_QTHETA_OFFSET           4
#define CELT_QTHETA_OFFSET_TWOPHASE  16
#define CELT_DEEMPH_COEFF            0.85000610f
#define CELT_POSTFILTER_MINPERIOD    15
#define CELT_ENERGY_SILENCE          (-28.0f)

enum CeltSpread {
    CELT_SPREAD_NONE,
    CELT_SPREAD_LIGHT,
    CELT_SPREAD_NORMAL,
    CELT_SPREAD_AGGRESSIVE
};

typedef struct CeltFrame {
    float energy[CELT_MAX_BANDS];
    float prev_energy[2][CELT_MAX_BANDS];

    uint8_t collapse_masks[CELT_MAX_BANDS];

    /* buffer for mdct output + postfilter */
    DECLARE_ALIGNED(32, float, buf)[2048];

    /* postfilter parameters */
    int pf_period_new;
    float pf_gains_new[3];
    int pf_period;
    float pf_gains[3];
    int pf_period_old;
    float pf_gains_old[3];

    float deemph_coeff;
} CeltFrame;

struct CeltContext {
    // constant values that do not change during context lifetime
    AVCodecContext    *avctx;
    MDCT15Context     *imdct[4];
    AVFloatDSPContext  *dsp;
    int output_channels;

    // values that have inter-frame effect and must be reset on flush
    CeltFrame frame[2];
    uint32_t seed;
    int flushed;

    // values that only affect a single frame
    int coded_channels;
    int framebits;
    int duration;

    /* number of iMDCT blocks in the frame */
    int blocks;
    /* size of each block */
    int blocksize;

    int startband;
    int endband;
    int codedbands;

    int anticollapse_bit;

    int intensitystereo;
    int dualstereo;
    enum CeltSpread spread;

    int remaining;
    int remaining2;
    int fine_bits    [CELT_MAX_BANDS];
    int fine_priority[CELT_MAX_BANDS];
    int pulses       [CELT_MAX_BANDS];
    int tf_change    [CELT_MAX_BANDS];

    DECLARE_ALIGNED(32, float, coeffs)[2][CELT_MAX_FRAME_SIZE];
    DECLARE_ALIGNED(32, float, scratch)[22 * 8]; // MAX(ff_celt_freq_range) * 1<<CELT_MAX_LOG_BLOCKS
};

/* LCG for noise generation */
static av_always_inline uint32_t celt_rng(CeltContext *s)
{
    s->seed = 1664525 * s->seed + 1013904223;
    return s->seed;
}

static av_always_inline void celt_renormalize_vector(float *X, int N, float gain)
{
    int i;
    float g = 1e-15f;
    for (i = 0; i < N; i++)
        g += X[i] * X[i];
    g = gain / sqrtf(g);

    for (i = 0; i < N; i++)
        X[i] *= g;
}

#endif /* AVCODEC_OPUS_CELT_H */
