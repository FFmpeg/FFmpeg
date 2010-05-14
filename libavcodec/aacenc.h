/*
 * AAC encoder
 * Copyright (C) 2008 Konstantin Shishkov
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

#ifndef AVCODEC_AACENC_H
#define AVCODEC_AACENC_H

#include "avcodec.h"
#include "put_bits.h"
#include "dsputil.h"

#include "aac.h"

#include "psymodel.h"

struct AACEncContext;

typedef struct AACCoefficientsEncoder {
    void (*search_for_quantizers)(AVCodecContext *avctx, struct AACEncContext *s,
                                  SingleChannelElement *sce, const float lambda);
    void (*encode_window_bands_info)(struct AACEncContext *s, SingleChannelElement *sce,
                                     int win, int group_len, const float lambda);
    void (*quantize_and_encode_band)(struct AACEncContext *s, PutBitContext *pb, const float *in, int size,
                                     int scale_idx, int cb, const float lambda);
    void (*search_for_ms)(struct AACEncContext *s, ChannelElement *cpe, const float lambda);
} AACCoefficientsEncoder;

extern AACCoefficientsEncoder ff_aac_coders[];

/**
 * AAC encoder context
 */
typedef struct AACEncContext {
    PutBitContext pb;
    FFTContext mdct1024;                         ///< long (1024 samples) frame transform context
    FFTContext mdct128;                          ///< short (128 samples) frame transform context
    DSPContext  dsp;
    DECLARE_ALIGNED(16, FFTSample, output)[2048]; ///< temporary buffer for MDCT input coefficients
    int16_t* samples;                            ///< saved preprocessed input

    int samplerate_index;                        ///< MPEG-4 samplerate index

    ChannelElement *cpe;                         ///< channel elements
    FFPsyContext psy;
    struct FFPsyPreprocessContext* psypp;
    AACCoefficientsEncoder *coder;
    int cur_channel;
    int last_frame;
    float lambda;
    DECLARE_ALIGNED(16, int,   qcoefs)[96];      ///< quantized coefficients
    DECLARE_ALIGNED(16, float, scoefs)[1024];    ///< scaled coefficients
} AACEncContext;

#endif /* AVCODEC_AACENC_H */
