/*
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

#ifndef AVCODEC_ARM_VP8_H
#define AVCODEC_ARM_VP8_H

#include "config.h"

#if HAVE_ARMV6
#define decode_block_coeffs_internal ff_decode_block_coeffs_armv6
int ff_decode_block_coeffs_armv6(VP56RangeCoder *rc, DCTELEM block[16],
                                 uint8_t probs[8][3][NUM_DCT_TOKENS-1],
                                 int i, uint8_t *token_prob, int16_t qmul[2]);
#endif

#endif /* AVCODEC_ARM_VP8_H */
