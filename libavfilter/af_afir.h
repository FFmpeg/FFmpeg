/*
 * Copyright (c) 2017 Paul B Mahol
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

#ifndef AVFILTER_AFIR_H
#define AVFILTER_AFIR_H

#include "libavutil/common.h"
#include "libavutil/float_dsp.h"
#include "libavutil/opt.h"
#include "libavcodec/avfft.h"

#include "audio.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"

typedef struct AudioFIRSegment {
    int nb_partitions;
    int part_size;
    int block_size;
    int fft_length;
    int coeff_size;
    int segment_size;

    int *part_index;

    AVFrame *sum;
    AVFrame *block;
    AVFrame *buffer;
    AVFrame *coeff;

    RDFTContext **rdft, **irdft;
} AudioFIRSegment;

typedef struct AudioFIRContext {
    const AVClass *class;

    float wet_gain;
    float dry_gain;
    float length;
    int gtype;
    float ir_gain;
    int ir_format;
    float max_ir_len;
    int response;
    int w, h;
    AVRational frame_rate;
    int ir_channel;
    int minp;
    int maxp;

    float gain;

    int eof_coeffs;
    int have_coeffs;
    int nb_taps;
    int nb_channels;
    int nb_coef_channels;
    int one2many;

    AudioFIRSegment seg[1024];
    int nb_segments;

    AVFrame *in[2];
    AVFrame *video;
    int64_t pts;

    AVFloatDSPContext *fdsp;
    void (*fcmul_add)(float *sum, const float *t, const float *c,
                      ptrdiff_t len);
} AudioFIRContext;

void ff_afir_init_x86(AudioFIRContext *s);

#endif /* AVFILTER_AFIR_H */
