/*
 * ADX ADPCM codecs
 * Copyright (c) 2001,2003 BERO
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

/**
 * @file
 * SEGA CRI adx codecs.
 *
 * Reference documents:
 * http://ku-www.ss.titech.ac.jp/~yatsushi/adx.html
 * adx2wav & wav2adx http://www.geocities.co.jp/Playtown/2004/
 */

#ifndef AVCODEC_ADX_H
#define AVCODEC_ADX_H

#include <stdint.h>

#include "avcodec.h"

typedef struct ADXChannelState {
    int s1,s2;
} ADXChannelState;

typedef struct ADXContext {
    int channels;
    ADXChannelState prev[2];
    int header_parsed;
    int eof;
    int cutoff;
    int coeff[2];
} ADXContext;

#define COEFF_BITS  12

#define BLOCK_SIZE      18
#define BLOCK_SAMPLES   32

/**
 * Calculate LPC coefficients based on cutoff frequency and sample rate.
 *
 * @param cutoff       cutoff frequency
 * @param sample_rate  sample rate
 * @param bits         number of bits used to quantize coefficients
 * @param[out] coeff   2 quantized LPC coefficients
 */
void ff_adx_calculate_coeffs(int cutoff, int sample_rate, int bits, int *coeff);

/**
 * Decode ADX stream header.
 * Sets avctx->channels and avctx->sample_rate.
 *
 * @param      avctx        codec context
 * @param      buf          header data
 * @param      bufsize      data size, should be at least 24 bytes
 * @param[out] header_size  size of ADX header
 * @param[out] coeff        2 LPC coefficients, can be NULL
 * @return data offset or negative error code if header is invalid
 */
int ff_adx_decode_header(AVCodecContext *avctx, const uint8_t *buf,
                         int bufsize, int *header_size, int *coeff);

#if LIBAVCODEC_VERSION_MAJOR < 56
int avpriv_adx_decode_header(AVCodecContext *avctx, const uint8_t *buf,
                             int bufsize, int *header_size, int *coeff);
#endif

#endif /* AVCODEC_ADX_H */
