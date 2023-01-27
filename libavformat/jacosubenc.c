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

#include "avformat.h"
#include "mux.h"
#include "rawenc.h"

static int jacosub_write_header(AVFormatContext *s)
{
    const AVCodecParameters *par = s->streams[0]->codecpar;

    if (par->extradata_size) {
        avio_write(s->pb, par->extradata, par->extradata_size);
    }
    return 0;
}

const FFOutputFormat ff_jacosub_muxer = {
    .p.name           = "jacosub",
    .p.long_name      = NULL_IF_CONFIG_SMALL("JACOsub subtitle format"),
    .p.mime_type      = "text/x-jacosub",
    .p.extensions     = "jss,js",
    .p.flags          = AVFMT_TS_NONSTRICT,
    .p.subtitle_codec = AV_CODEC_ID_JACOSUB,
    .write_header   = jacosub_write_header,
    .write_packet   = ff_raw_write_packet,
};
