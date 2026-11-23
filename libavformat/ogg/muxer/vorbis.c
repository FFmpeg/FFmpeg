/*
 * Ogg Vorbis muxer mapping
 * Copyright (c) 2007 Baptiste Coudurier <baptiste dot coudurier at free dot fr>
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

#include "libavformat/ogg/muxer.h"

static int vorbis_init(AVFormatContext *s, AVStream *st)
{
    return ff_ogg_build_xiph_headers(s, st, 30, 3, "vorbis", 1);
}

const OGGMuxerMapping ff_ogg_vorbis_muxer_mapping = {
    .codec_id  = AV_CODEC_ID_VORBIS,
    .new_links = 1,
    .init      = vorbis_init,
};
