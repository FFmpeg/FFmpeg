/*
 * AMR storage format helpers
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

#ifndef AVFORMAT_AMR_H
#define AVFORMAT_AMR_H

#include <stdint.h>

#include "libavcodec/codec_id.h"

/**
 * Size of an RFC 4867 storage format frame, including its header byte.
 *
 * @return the size in bytes, or 0 for a reserved frame type
 */
static inline int ff_amr_frame_size(enum AVCodecID codec_id, int frame_type)
{
    static const uint8_t frame_size[2][16] = {
        { 13, 14, 16, 18, 20, 21, 27, 32,  6, 0, 0, 0, 0, 0, 0, 1 },
        { 18, 24, 33, 37, 41, 47, 51, 59, 61, 6, 0, 0, 0, 0, 1, 1 },
    };
    return frame_size[codec_id == AV_CODEC_ID_AMR_WB][frame_type & 0x0F];
}

#endif /* AVFORMAT_AMR_H */
