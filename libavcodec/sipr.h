/*
 * SIPR / ACELP.NET decoder
 *
 * Copyright (c) 2008 Vladimir Voroshilov
 * Copyright (c) 2009 Vitor Sessak
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

#ifndef AVCODEC_SIPR_H
#define AVCODEC_SIPR_H

#include "avcodec.h"
#include "dsputil.h"
#include "acelp_pitch_delay.h"

#define LSFQ_DIFF_MIN        (0.0125 * M_PI)

#define LP_FILTER_ORDER      10

/** Number of past samples needed for excitation interpolation */
#define L_INTERPOL           (LP_FILTER_ORDER + 1)

/**  Subframe size for all modes except 16k */
#define SUBFR_SIZE           48

typedef enum {
    MODE_16k,
    MODE_8k5,
    MODE_6k5,
    MODE_5k0,
    MODE_COUNT
} SiprMode;

typedef struct {
    AVCodecContext *avctx;
    DSPContext dsp;

    SiprMode mode;

    float past_pitch_gain;
    float lsf_history[LP_FILTER_ORDER];

    float excitation[L_INTERPOL + PITCH_DELAY_MAX + 5*SUBFR_SIZE];

    DECLARE_ALIGNED_16(float, synth_buf[LP_FILTER_ORDER + 5*SUBFR_SIZE + 6]);

    float lsp_history[LP_FILTER_ORDER];
    float gain_mem;
    float energy_history[4];
    float highpass_filt_mem[2];
    float postfilter_mem[PITCH_DELAY_MAX + LP_FILTER_ORDER];

    /* 5k0 */
    float tilt_mem;
    float postfilter_agc;
    float postfilter_mem5k0[PITCH_DELAY_MAX + LP_FILTER_ORDER];
    float postfilter_syn5k0[LP_FILTER_ORDER + SUBFR_SIZE*5];
} SiprContext;

typedef struct {
    int vq_indexes[5];
    int pitch_delay[5];        ///< pitch delay
    int gp_index[5];           ///< adaptive-codebook gain indexes
    int16_t fc_indexes[5][10]; ///< fixed-codebook indexes
    int gc_index[5];           ///< fixed-codebook gain indexes
} SiprParameters;

#endif /* AVCODEC_SIPR_H */
