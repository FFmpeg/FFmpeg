/*
 * AST common code
 * Copyright (c) 2012 James Almer
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

#include <stddef.h>
#include "libavcodec/codec_id.h"
#include "internal.h"

const AVCodecTag ff_codec_ast_tags[] = {
    { AV_CODEC_ID_ADPCM_AFC,          0 },
    { AV_CODEC_ID_PCM_S16BE_PLANAR,   1 },
    { AV_CODEC_ID_NONE,               0 },
};

const AVCodecTag *const ff_ast_codec_tags_list[] = { ff_codec_ast_tags, NULL };
