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
    uint8_t *ch[SWR_CH_MAX];    ///< samples buffer per channel
    uint8_t *data;              ///< samples buffer
    int ch_count;               ///< number of channels
    int bps;                    ///< bytes per sample
    int count;                  ///< number of samples
    int planar;                 ///< 1 if planar audio, 0 otherwise
} AudioData;

typedef struct SwrContext {
    const AVClass *av_class;                        ///< AVClass used for AVOption and av_log()
    int log_level_offset;                           ///< logging level offset
    void *log_ctx;                                  ///< parent logging context
    enum AVSampleFormat  in_sample_fmt;             ///< input sample format
    enum AVSampleFormat int_sample_fmt;             ///< internal sample format (AV_SAMPLE_FMT_FLT or AV_SAMPLE_FMT_S16)
    enum AVSampleFormat out_sample_fmt;             ///< output sample format
    int64_t  in_ch_layout;                          ///< input channel layout
    int64_t out_ch_layout;                          ///< output channel layout
    int      in_sample_rate;                        ///< input sample rate
    int     out_sample_rate;                        ///< output sample rate
    int flags;                                      ///< miscellaneous flags such as SWR_FLAG_RESAMPLE
    float slev;                                     ///< surround mixing level
    float clev;                                     ///< center mixing level
    float rematrix_volume;                          ///< rematrixing volume coefficient
    const int *channel_map;                         ///< channel index (or -1 if muted channel) map
    int used_ch_count;                              ///< number of used input channels (mapped channel count if channel_map, otherwise in.ch_count)

    int int_bps;                                    ///< internal bytes per sample
    int resample_first;                             ///< 1 if resampling must come first, 0 if rematrixing
    int rematrix;                                   ///< flag to indicate if rematrixing is needed (basically if input and output layouts mismatch)

    AudioData in;                                   ///< input audio data
    AudioData postin;                               ///< post-input audio data: used for rematrix/resample
    AudioData midbuf;                               ///< intermediate audio data (postin/preout)
    AudioData preout;                               ///< pre-output audio data: used for rematrix/resample
    AudioData out;                                  ///< converted output audio data
    AudioData in_buffer;                            ///< cached audio data (convert and resample purpose)
    int in_buffer_index;                            ///< cached buffer position
    int in_buffer_count;                            ///< cached buffer length
    int resample_in_constraint;                     ///< 1 if the input end was reach before the output end, 0 otherwise

    struct AudioConvert *in_convert;                ///< input conversion context
    struct AudioConvert *out_convert;               ///< output conversion context
    struct AudioConvert *full_convert;              ///< full conversion context (single conversion for input and output)
    struct ResampleContext *resample;               ///< resampling context

    float matrix[SWR_CH_MAX][SWR_CH_MAX];           ///< floating point rematrixing coefficients
    int32_t matrix32[SWR_CH_MAX][SWR_CH_MAX];       ///< 17.15 fixed point rematrixing coefficients
    uint8_t matrix_ch[SWR_CH_MAX][SWR_CH_MAX+1];    ///< Lists of input channels per output channel that have non zero rematrixing coefficients

    /* TODO: callbacks for ASM optimizations */
}SwrContext;

struct ResampleContext *swri_resample_init(struct ResampleContext *, int out_rate, int in_rate, int filter_size, int phase_shift, int linear, double cutoff);
void swri_resample_free(struct ResampleContext **c);
int swri_multiple_resample(struct ResampleContext *c, AudioData *dst, int dst_size, AudioData *src, int src_size, int *consumed);
void swri_resample_compensate(struct ResampleContext *c, int sample_delta, int compensation_distance);
int swri_resample(struct ResampleContext *c, int16_t *dst, const int16_t *src, int *consumed, int src_size, int dst_size, int update_ctx);

int swri_rematrix_init(SwrContext *s);
int swri_rematrix(SwrContext *s, AudioData *out, AudioData *in, int len, int mustcopy);

#endif
