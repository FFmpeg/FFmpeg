/*
 * G.729 decoder
 * Copyright (c) 2008 Vladimir Voroshilov
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
#include <stdlib.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#include "avcodec.h"
#include "libavutil/avutil.h"
#include "bitstream.h"

/**
 * minimum quantized LSF value (3.2.4)
 * 0.005 in Q13
 */
#define LSFQ_MIN                   40

/**
 * maximum quantized LSF value (3.2.4)
 * 3.135 in Q13
 */
#define LSFQ_MAX                   25681

/**
 * minimum LSF distance (3.2.4)
 * 0.0391 in Q13
 */
#define LSFQ_DIFF_MIN              321

/**
 * minimum gain pitch value (3.8, Equation 47)
 * 0.2 in (1.14)
 */
#define SHARP_MIN                  3277

/**
 * maximum gain pitch value (3.8, Equation 47)
 * (EE) This does not comply with the specification.
 * Specification says about 0.8, which should be
 * 13107 in (1.14), but reference C code uses
 * 13017 (equals to 0.7945) instead of it.
 */
#define SHARP_MAX                  13017

/**
 * \brief pseudo random number generator
 */
static inline uint16_t g729_random(uint16_t value)
{
    return 31821 * value + 13849;
}

/**
 * Get parity bit of bit 2..7
 */
static inline int g729_get_parity(uint8_t value)
{
   return (0x6996966996696996ULL >> (value >> 2)) & 1;
}

AVCodec g729_decoder =
{
    "g729",
    CODEC_TYPE_AUDIO,
    CODEC_ID_G729,
    sizeof(G729_Context),
    ff_g729_decoder_init,
    NULL,
    NULL,
    ff_g729_decode_frame,
    .long_name = NULL_IF_CONFIG_SMALL("G.729"),
};
