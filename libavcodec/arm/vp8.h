/*
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

#ifndef AVCODEC_ARM_VP8_H
#define AVCODEC_ARM_VP8_H

#include <stdint.h>

#include "config.h"
#include "libavcodec/vp56.h"
#include "libavcodec/vp8.h"

#if HAVE_ARMV6_EXTERNAL
#define decode_block_coeffs_internal ff_decode_block_coeffs_armv6
int ff_decode_block_coeffs_armv6(VP56RangeCoder *rc, int16_t block[16],
                                 uint8_t probs[8][3][NUM_DCT_TOKENS-1],
                                 int i, uint8_t *token_prob, int16_t qmul[2]);
#endif

#endif /* AVCODEC_ARM_VP8_H */
