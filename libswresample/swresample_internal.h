/*
 * Copyright (C) 2011 Michael Niedermayer (michaelni@gmx.at)
 *
 * This file is part of libswresample
 *
 * libswresample is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * libswresample is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with libswresample; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef SWR_INTERNAL_H
#define SWR_INTERNAL_H

#include "swresample.h"

typedef struct AudioData{
    uint8_t *ch[SWR_CH_MAX];
    uint8_t *data;
    int ch_count;
    int bps;
    int count;
    int planar;
} AudioData;

typedef struct SwrContext {          //FIXME find unused fields
    const AVClass *av_class;
    int log_level_offset;
    void *log_ctx;
    enum AVSampleFormat  in_sample_fmt;
    enum AVSampleFormat int_sample_fmt; ///<AV_SAMPLE_FMT_FLT OR AV_SAMPLE_FMT_S16
    enum AVSampleFormat out_sample_fmt;
    int64_t  in_ch_layout;
    int64_t out_ch_layout;
    int      in_sample_rate;
    int     out_sample_rate;
    int flags;
    float slev, clev, rematrix_volume;

    //below are private
    int int_bps;
    int resample_first;
    int rematrix;                   ///< flag to indicate if rematrixing is used

    AudioData in, postin, midbuf, preout, out, in_buffer;
    int in_buffer_index;
    int in_buffer_count;
    int resample_in_constraint;

    struct AVAudioConvert *in_convert;
    struct AVAudioConvert *out_convert;
    struct AVAudioConvert *full_convert;
    struct AVResampleContext *resample;

    float matrix[SWR_CH_MAX][SWR_CH_MAX];
    int32_t matrix32[SWR_CH_MAX][SWR_CH_MAX];
    uint8_t matrix_ch[SWR_CH_MAX][SWR_CH_MAX+1];

    //TODO callbacks for asm optims
}SwrContext;

struct AVResampleContext *swr_resample_init(struct AVResampleContext *, int out_rate, int in_rate, int filter_size, int phase_shift, int linear, double cutoff);
void swr_resample_free(struct AVResampleContext **c);
int swr_multiple_resample(struct AVResampleContext *c, AudioData *dst, int dst_size, AudioData *src, int src_size, int *consumed);
void swr_resample_compensate(struct AVResampleContext *c, int sample_delta, int compensation_distance);
int swr_resample(struct AVResampleContext *c, short *dst, const short *src, int *consumed, int src_size, int dst_size, int update_ctx);

int swr_rematrix_init(SwrContext *s);
int swr_rematrix(SwrContext *s, AudioData *out, AudioData *in, int len, int mustcopy);
#endif
