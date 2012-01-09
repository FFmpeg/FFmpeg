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

/**
 * @file
 * libswresample public header
 */

#ifndef SWR_H
#define SWR_H

#include <inttypes.h>
#include "libavutil/samplefmt.h"

#define LIBSWRESAMPLE_VERSION_MAJOR 0
#define LIBSWRESAMPLE_VERSION_MINOR 6
#define LIBSWRESAMPLE_VERSION_MICRO 100

#define LIBSWRESAMPLE_VERSION_INT  AV_VERSION_INT(LIBSWRESAMPLE_VERSION_MAJOR, \
                                                  LIBSWRESAMPLE_VERSION_MINOR, \
                                                  LIBSWRESAMPLE_VERSION_MICRO)

#define SWR_CH_MAX 16   ///< Maximum number of channels

#define SWR_FLAG_RESAMPLE 1 ///< Force resampling even if equal sample rate
//TODO use int resample ?
//long term TODO can we enable this dynamically?


struct SwrContext;

/**
 * Allocate SwrContext.
 *
 * If you use this function you will need to set the parameters (manually or
 * with swr_alloc_set_opts()) before calling swr_init().
 *
 * @see swr_alloc_set_opts(), swr_init(), swr_free()
 * @return NULL on error, allocated context otherwise
 */
struct SwrContext *swr_alloc(void);

/**
 * Initialize context after user parameters have been set.
 *
 * @return AVERROR error code in case of failure.
 */
int swr_init(struct SwrContext *s);

/**
 * Allocate SwrContext if needed and set/reset common parameters.
 *
 * This function does not require s to be allocated with swr_alloc(). On the
 * other hand, swr_alloc() can use swr_alloc_set_opts() to set the parameters
 * on the allocated context.
 *
 * @param s               Swr context, can be NULL
 * @param out_ch_layout   output channel layout (AV_CH_LAYOUT_*)
 * @param out_sample_fmt  output sample format (AV_SAMPLE_FMT_*).
 * @param out_sample_rate output sample rate (frequency in Hz)
 * @param in_ch_layout    input channel layout (AV_CH_LAYOUT_*)
 * @param in_sample_fmt   input sample format (AV_SAMPLE_FMT_*).
 * @param in_sample_rate  input sample rate (frequency in Hz)
 * @param log_offset      logging level offset
 * @param log_ctx         parent logging context, can be NULL
 *
 * @see swr_init(), swr_free()
 * @return NULL on error, allocated context otherwise
 */
struct SwrContext *swr_alloc_set_opts(struct SwrContext *s,
                                      int64_t out_ch_layout, enum AVSampleFormat out_sample_fmt, int out_sample_rate,
                                      int64_t  in_ch_layout, enum AVSampleFormat  in_sample_fmt, int  in_sample_rate,
                                      int log_offset, void *log_ctx);

/**
 * Free the given SwrContext and set the pointer to NULL.
 */
void swr_free(struct SwrContext **s);

/**
 * Convert audio.
 *
 * in and in_count can be set to 0 to flush the last few samples out at the
 * end.
 *
 * @param s         allocated Swr context, with parameters set
 * @param out       output buffers, only the first one need be set in case of packed audio
 * @param out_count amount of space available for output in samples per channel
 * @param in        input buffers, only the first one need to be set in case of packed audio
 * @param in_count  number of input samples available in one channel
 *
 * @return number of samples output per channel
 */
int swr_convert(struct SwrContext *s, uint8_t *out[SWR_CH_MAX], int out_count,
                                const uint8_t *in [SWR_CH_MAX], int in_count);

/**
 * Activate resampling compensation.
 */
int swr_set_compensation(struct SwrContext *s, int sample_delta, int compensation_distance);

/**
 * Set a customized input channel mapping.
 *
 * @param s           allocated Swr context, not yet initialized
 * @param channel_map customized input channel mapping (array of channel
 *                    indexes, -1 for a muted channel)
 * @return AVERROR error code in case of failure.
 */
int swr_set_channel_mapping(struct SwrContext *s, const int *channel_map);

/**
 * Return the LIBSWRESAMPLE_VERSION_INT constant.
 */
unsigned swresample_version(void);

/**
 * Return the swr build-time configuration.
 */
const char *swresample_configuration(void);

/**
 * Return the swr license.
 */
const char *swresample_license(void);

#endif
