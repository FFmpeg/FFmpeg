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

#ifndef SWR_H
#define SWR_H

#include <inttypes.h>
#include "libavutil/samplefmt.h"

#define LIBSWRESAMPLE_VERSION_MAJOR 0
#define LIBSWRESAMPLE_VERSION_MINOR 0
#define LIBSWRESAMPLE_VERSION_MICRO 0

#define SWR_CH_MAX 16

#define SWR_FLAG_RESAMPLE 1///< Force resampling even if equal sample rate
//TODO use int resample ?
//long term TODO can we enable this dynamically?


struct SwrContext;

/**
 * Allocate SwrContext.
 * @see swr_init(),swr_free()
 * @return NULL on error
 */
struct SwrContext *swr_alloc(void);

/**
 * Initialize context after user parameters have been set.
 * @return negativo n error
 */
int swr_init(struct SwrContext *s);

/**
 * Allocate SwrContext.
 * @see swr_init(),swr_free()
 * @return NULL on error
 */
struct SwrContext *swr_alloc2(struct SwrContext *s, int64_t out_ch_layout, enum AVSampleFormat out_sample_fmt, int out_sample_rate,
                              int64_t  in_ch_layout, enum AVSampleFormat  in_sample_fmt, int  in_sample_rate,
                              int log_offset, void *log_ctx);

/**
 * Free the given SwrContext.
 * And set the pointer to NULL
 */
void swr_free(struct SwrContext **s);

/**
 * Convert audio.
 * @param  in_count Number of input samples available in one channel.
 * @param out_count Amount of space available for output in samples per channel.
 * @return number of samples output per channel
 */
int swr_convert(struct SwrContext *s, uint8_t *out[SWR_CH_MAX], int out_count,
                                const uint8_t *in [SWR_CH_MAX], int in_count);

void swr_compensate(struct SwrContext *s, int sample_delta, int compensation_distance);

#endif
