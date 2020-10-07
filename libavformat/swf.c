/*
 * Flash Compatible Streaming Format
 * Copyright (c) 2000 Fabrice Bellard
 * Copyright (c) 2003 Tinic Uro
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

#include "internal.h"

const AVCodecTag ff_swf_codec_tags[] = {
    { AV_CODEC_ID_FLV1,     0x02 },
    { AV_CODEC_ID_FLASHSV,  0x03 },
    { AV_CODEC_ID_VP6F,     0x04 },
    { AV_CODEC_ID_VP6A,     0x05 },
    { AV_CODEC_ID_NONE,        0 },
};
