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

typedef void (mix_1_1_func_type)(void *out, const void *in, void *coeffp, int index, int len);
typedef void (mix_2_1_func_type)(void *out, const void *in1, const void *in2, void *coeffp, int index1, int index2, int len);

typedef struct AudioData{
    uint8_t *ch[SWR_CH_MAX];    ///< samples buffer per channel
    uint8_t *data;              ///< samples buffer
    int ch_count;               ///< number of channels
    int bps;                    ///< bytes per sample
    int count;                  ///< number of samples
    int planar;                 ///< 1 if planar audio, 0 otherwise
    enum AVSampleFormat fmt;    ///< sample format
} AudioData;

struct SwrContext {
    const AVClass *av_class;                        ///< AVClass used for AVOption and av_log()
    int log_level_offset;                           ///< logging level offset
    void *log_ctx;                                  ///< parent logging context
    enum AVSampleFormat  in_sample_fmt;             ///< input sample format
    enum AVSampleFormat int_sample_fmt;             ///< internal sample format (AV_SAMPLE_FMT_FLTP or AV_SAMPLE_FMT_S16P)
    enum AVSampleFormat out_sample_fmt;             ///< output sample format
    int64_t  in_ch_layout;                          ///< input channel layout
    int64_t out_ch_layout;                          ///< output channel layout
    int      in_sample_rate;                        ///< input sample rate
    int     out_sample_rate;                        ///< output sample rate
    int flags;                                      ///< miscellaneous flags such as SWR_FLAG_RESAMPLE
    float slev;                                     ///< surround mixing level
    float clev;                                     ///< center mixing level
    float lfe_mix_level;                            ///< LFE mixing level
    float rematrix_volume;                          ///< rematrixing volume coefficient
    const int *channel_map;                         ///< channel index (or -1 if muted channel) map
    int used_ch_count;                              ///< number of used input channels (mapped channel count if channel_map, otherwise in.ch_count)
    enum SwrDitherType dither_method;
    int dither_pos;
    float dither_scale;
    int filter_size;                                /**< length of each FIR filter in the resampling filterbank relative to the cutoff frequency */
    int phase_shift;                                /**< log2 of the number of entries in the resampling polyphase filterbank */
    int linear_interp;                              /**< if 1 then the resampling FIR filter will be linearly interpolated */
    double cutoff;                                  /**< resampling cutoff frequency. 1.0 corresponds to half the output sample rate */

    float min_compensation;                         ///< minimum below which no compensation will happen
    float min_hard_compensation;                    ///< minimum below which no silence inject / sample drop will happen
    float soft_compensation_duration;               ///< duration over which soft compensation is applied
    float max_soft_compensation;                    ///< maximum soft compensation in seconds over soft_compensation_duration

    int resample_first;                             ///< 1 if resampling must come first, 0 if rematrixing
    int rematrix;                                   ///< flag to indicate if rematrixing is needed (basically if input and output layouts mismatch)
    int rematrix_custom;                            ///< flag to indicate that a custom matrix has been defined

    AudioData in;                                   ///< input audio data
    AudioData postin;                               ///< post-input audio data: used for rematrix/resample
    AudioData midbuf;                               ///< intermediate audio data (postin/preout)
    AudioData preout;                               ///< pre-output audio data: used for rematrix/resample
    AudioData out;                                  ///< converted output audio data
    AudioData in_buffer;                            ///< cached audio data (convert and resample purpose)
    AudioData dither;                               ///< noise used for dithering
    int in_buffer_index;                            ///< cached buffer position
    int in_buffer_count;                            ///< cached buffer length
    int resample_in_constraint;                     ///< 1 if the input end was reach before the output end, 0 otherwise
    int flushed;                                    ///< 1 if data is to be flushed and no further input is expected
    int64_t outpts;                                 ///< output PTS
    int drop_output;                                ///< number of output samples to drop

    struct AudioConvert *in_convert;                ///< input conversion context
    struct AudioConvert *out_convert;               ///< output conversion context
    struct AudioConvert *full_convert;              ///< full conversion context (single conversion for input and output)
    struct ResampleContext *resample;               ///< resampling context

    float matrix[SWR_CH_MAX][SWR_CH_MAX];           ///< floating point rematrixing coefficients
    uint8_t *native_matrix;
    uint8_t *native_one;
    int32_t matrix32[SWR_CH_MAX][SWR_CH_MAX];       ///< 17.15 fixed point rematrixing coefficients
    uint8_t matrix_ch[SWR_CH_MAX][SWR_CH_MAX+1];    ///< Lists of input channels per output channel that have non zero rematrixing coefficients
    mix_1_1_func_type *mix_1_1_f;
    mix_2_1_func_type *mix_2_1_f;

    /* TODO: callbacks for ASM optimizations */
};

struct ResampleContext *swri_resample_init(struct ResampleContext *, int out_rate, int in_rate, int filter_size, int phase_shift, int linear, double cutoff, enum AVSampleFormat);
void swri_resample_free(struct ResampleContext **c);
int swri_multiple_resample(struct ResampleContext *c, AudioData *dst, int dst_size, AudioData *src, int src_size, int *consumed);
void swri_resample_compensate(struct ResampleContext *c, int sample_delta, int compensation_distance);
int swri_resample_int16(struct ResampleContext *c, int16_t *dst, const int16_t *src, int *consumed, int src_size, int dst_size, int update_ctx);
int swri_resample_int32(struct ResampleContext *c, int32_t *dst, const int32_t *src, int *consumed, int src_size, int dst_size, int update_ctx);
int swri_resample_float(struct ResampleContext *c, float   *dst, const float   *src, int *consumed, int src_size, int dst_size, int update_ctx);
int swri_resample_double(struct ResampleContext *c,double  *dst, const double  *src, int *consumed, int src_size, int dst_size, int update_ctx);

int swri_rematrix_init(SwrContext *s);
void swri_rematrix_free(SwrContext *s);
int swri_rematrix(SwrContext *s, AudioData *out, AudioData *in, int len, int mustcopy);

void swri_get_dither(SwrContext *s, void *dst, int len, unsigned seed, enum AVSampleFormat out_fmt, enum AVSampleFormat in_fmt);

void swri_audio_convert_init_x86(struct AudioConvert *ac,
                                 enum AVSampleFormat out_fmt,
                                 enum AVSampleFormat in_fmt,
                                 int channels);
#endif
