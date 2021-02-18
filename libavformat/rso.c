/*
 * RSO format common data
 * Copyright (c) 2010 Rafael Carre
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

#include "avformat.h"
#include "internal.h"
#include "rso.h"

const AVCodecTag ff_codec_rso_tags[] = {
    { AV_CODEC_ID_PCM_U8,          0x0100 },
    { AV_CODEC_ID_ADPCM_IMA_WAV,   0x0101 },
    { AV_CODEC_ID_NONE, 0 },
};

const AVCodecTag *const ff_rso_codec_tags_list[] = { ff_codec_rso_tags, NULL };
