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

#include "libavutil/float_dsp.h"
#include "avcodec.h"
#include "put_bits.h"

#include "aac.h"
#include "audio_frame_queue.h"
#include "psymodel.h"

typedef enum AACCoder {
    AAC_CODER_FAAC = 0,
    AAC_CODER_ANMR,
    AAC_CODER_TWOLOOP,
    AAC_CODER_FAST,

    AAC_CODER_NB,
}AACCoder;

typedef struct AACEncOptions {
    int stereo_mode;
    int aac_coder;
} AACEncOptions;

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
    AVClass *av_class;
    AACEncOptions options;                       ///< encoding options
    PutBitContext pb;
    FFTContext mdct1024;                         ///< long (1024 samples) frame transform context
    FFTContext mdct128;                          ///< short (128 samples) frame transform context
    AVFloatDSPContext fdsp;
    float *planar_samples[6];                    ///< saved preprocessed input

    int samplerate_index;                        ///< MPEG-4 samplerate index
    int channels;                                ///< channel count
    const uint8_t *chan_map;                     ///< channel configuration map

    ChannelElement *cpe;                         ///< channel elements
    FFPsyContext psy;
    struct FFPsyPreprocessContext* psypp;
    AACCoefficientsEncoder *coder;
    int cur_channel;
    int last_frame;
    float lambda;
    AudioFrameQueue afq;
    DECLARE_ALIGNED(16, int,   qcoefs)[96];      ///< quantized coefficients
    DECLARE_ALIGNED(32, float, scoefs)[1024];    ///< scaled coefficients

    struct {
        float *samples;
    } buffer;
} AACEncContext;

extern float ff_aac_pow34sf_tab[428];

void ff_aac_coder_init_mips(AACEncContext *c);

#endif /* AVCODEC_AACENC_H */
