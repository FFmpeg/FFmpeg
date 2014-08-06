/*
 * Copyright (c) 2011  Justin Ruggles
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/common.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mathematics.h"
#include "adx.h"

void ff_adx_calculate_coeffs(int cutoff, int sample_rate, int bits, int *coeff)
{
    double a, b, c;

    a = M_SQRT2 - cos(2.0 * M_PI * cutoff / sample_rate);
    b = M_SQRT2 - 1.0;
    c = (a - sqrt((a + b) * (a - b))) / b;

    coeff[0] = lrintf(c * 2.0  * (1 << bits));
    coeff[1] = lrintf(-(c * c) * (1 << bits));
}

int ff_adx_decode_header(AVCodecContext *avctx, const uint8_t *buf,
                         int bufsize, int *header_size, int *coeff)
{
    int offset, cutoff;

    if (bufsize < 24)
        return AVERROR_INVALIDDATA;

    if (AV_RB16(buf) != 0x8000)
        return AVERROR_INVALIDDATA;
    offset = AV_RB16(buf + 2) + 4;

    /* if copyright string is within the provided data, validate it */
    if (bufsize >= offset && offset >= 6 && memcmp(buf + offset - 6, "(c)CRI", 6))
        return AVERROR_INVALIDDATA;

    /* check for encoding=3 block_size=18, sample_size=4 */
    if (buf[4] != 3 || buf[5] != 18 || buf[6] != 4) {
        avpriv_request_sample(avctx, "Support for this ADX format");
        return AVERROR_PATCHWELCOME;
    }

    /* channels */
    avctx->channels = buf[7];
    if (avctx->channels <= 0 || avctx->channels > 2)
        return AVERROR_INVALIDDATA;

    /* sample rate */
    avctx->sample_rate = AV_RB32(buf + 8);
    if (avctx->sample_rate < 1 ||
        avctx->sample_rate > INT_MAX / (avctx->channels * BLOCK_SIZE * 8))
        return AVERROR_INVALIDDATA;

    /* bit rate */
    avctx->bit_rate = avctx->sample_rate * avctx->channels * BLOCK_SIZE * 8 / BLOCK_SAMPLES;

    /* LPC coefficients */
    if (coeff) {
        cutoff = AV_RB16(buf + 16);
        ff_adx_calculate_coeffs(cutoff, avctx->sample_rate, COEFF_BITS, coeff);
    }

    *header_size = offset;
    return 0;
}
