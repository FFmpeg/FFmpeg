/*
 * Copyright (c) 2012 Justin Ruggles <justin.ruggles@gmail.com>
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

#ifndef AVRESAMPLE_INTERNAL_H
#define AVRESAMPLE_INTERNAL_H

#include "libavutil/audio_fifo.h"
#include "libavutil/log.h"
#include "libavutil/opt.h"
#include "libavutil/samplefmt.h"
#include "avresample.h"

typedef struct AudioData AudioData;
typedef struct AudioConvert AudioConvert;
typedef struct AudioMix AudioMix;
typedef struct ResampleContext ResampleContext;

enum RemapPoint {
    REMAP_NONE,
    REMAP_IN_COPY,
    REMAP_IN_CONVERT,
    REMAP_OUT_COPY,
    REMAP_OUT_CONVERT,
};

typedef struct ChannelMapInfo {
    int channel_map[AVRESAMPLE_MAX_CHANNELS];   /**< source index of each output channel, -1 if not remapped */
    int do_remap;                               /**< remap needed */
    int channel_copy[AVRESAMPLE_MAX_CHANNELS];  /**< dest index to copy from */
    int do_copy;                                /**< copy needed */
    int channel_zero[AVRESAMPLE_MAX_CHANNELS];  /**< dest index to zero */
    int do_zero;                                /**< zeroing needed */
    int input_map[AVRESAMPLE_MAX_CHANNELS];     /**< dest index of each input channel */
} ChannelMapInfo;

struct AVAudioResampleContext {
    const AVClass *av_class;        /**< AVClass for logging and AVOptions  */

    uint64_t in_channel_layout;                 /**< input channel layout   */
    enum AVSampleFormat in_sample_fmt;          /**< input sample format    */
    int in_sample_rate;                         /**< input sample rate      */
    uint64_t out_channel_layout;                /**< output channel layout  */
    enum AVSampleFormat out_sample_fmt;         /**< output sample format   */
    int out_sample_rate;                        /**< output sample rate     */
    enum AVSampleFormat internal_sample_fmt;    /**< internal sample format */
    enum AVMixCoeffType mix_coeff_type;         /**< mixing coefficient type */
    double center_mix_level;                    /**< center mix level       */
    double surround_mix_level;                  /**< surround mix level     */
    double lfe_mix_level;                       /**< lfe mix level          */
    int normalize_mix_level;                    /**< enable mix level normalization */
    int force_resampling;                       /**< force resampling       */
    int filter_size;                            /**< length of each FIR filter in the resampling filterbank relative to the cutoff frequency */
    int phase_shift;                            /**< log2 of the number of entries in the resampling polyphase filterbank */
    int linear_interp;                          /**< if 1 then the resampling FIR filter will be linearly interpolated */
    double cutoff;                              /**< resampling cutoff frequency. 1.0 corresponds to half the output sample rate */
    enum AVResampleFilterType filter_type;      /**< resampling filter type */
    int kaiser_beta;                            /**< beta value for Kaiser window (only applicable if filter_type == AV_FILTER_TYPE_KAISER) */
    enum AVResampleDitherMethod dither_method;  /**< dither method          */

    int in_channels;        /**< number of input channels                   */
    int out_channels;       /**< number of output channels                  */
    int resample_channels;  /**< number of channels used for resampling     */
    int downmix_needed;     /**< downmixing is needed                       */
    int upmix_needed;       /**< upmixing is needed                         */
    int mixing_needed;      /**< either upmixing or downmixing is needed    */
    int resample_needed;    /**< resampling is needed                       */
    int in_convert_needed;  /**< input sample format conversion is needed   */
    int out_convert_needed; /**< output sample format conversion is needed  */
    int in_copy_needed;     /**< input data copy is needed                  */

    AudioData *in_buffer;           /**< buffer for converted input         */
    AudioData *resample_out_buffer; /**< buffer for output from resampler   */
    AudioData *out_buffer;          /**< buffer for converted output        */
    AVAudioFifo *out_fifo;          /**< FIFO for output samples            */

    AudioConvert *ac_in;        /**< input sample format conversion context  */
    AudioConvert *ac_out;       /**< output sample format conversion context */
    ResampleContext *resample;  /**< resampling context                      */
    AudioMix *am;               /**< channel mixing context                  */
    enum AVMatrixEncoding matrix_encoding;      /**< matrixed stereo encoding */

    /**
     * mix matrix
     * only used if avresample_set_matrix() is called before avresample_open()
     */
    double *mix_matrix;

    int use_channel_map;
    enum RemapPoint remap_point;
    ChannelMapInfo ch_map_info;
};


void ff_audio_resample_init_aarch64(ResampleContext *c,
                                    enum AVSampleFormat sample_fmt);
#endif /* AVRESAMPLE_INTERNAL_H */
