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

#include "libavutil/audio_fifo.h"
#include "libavutil/common.h"
#include "libavutil/float_dsp.h"
#include "libavutil/opt.h"
#include "libavcodec/avfft.h"

#include "audio.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"

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

    float gain;

    int eof_coeffs;
    int have_coeffs;
    int nb_coeffs;
    int nb_taps;
    int part_size;
    int part_index;
    int coeff_size;
    int block_size;
    int nb_partitions;
    int nb_channels;
    int ir_length;
    int fft_length;
    int nb_coef_channels;
    int one2many;
    int nb_samples;
    int want_skip;
    int need_padding;

    RDFTContext **rdft, **irdft;
    float **sum;
    float **block;
    FFTComplex **coeff;

    AVAudioFifo *fifo;
    AVFrame *in[2];
    AVFrame *buffer;
    AVFrame *video;
    int64_t pts;
    int index;

    AVFloatDSPContext *fdsp;
    void (*fcmul_add)(float *sum, const float *t, const float *c,
                      ptrdiff_t len);
} AudioFIRContext;

void ff_afir_init_x86(AudioFIRContext *s);

#endif /* AVFILTER_AFIR_H */
