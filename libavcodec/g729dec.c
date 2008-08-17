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
 * \brief pseudo random number generator
 */
static inline uint16_t g729_random(uint16_t value)
{
    return 31821 * value + 13849;
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
