/*
 * RTP SPEEX Depacketizer, RFC 5574
 * Copyright (c) 2012 Dmitry Samonenko
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
#include "rtpdec_formats.h"
#include "libavutil/avstring.h"

static int speex_parse_sdp_line(AVFormatContext *s, int st_index,
                               PayloadContext *data, const char *line)
{
    av_log(s, AV_LOG_WARNING, "fmtp line parsing is not implemented yet\n");

    return 0;
}

RTPDynamicProtocolHandler ff_speex_dynamic_handler = {
    .enc_name         = "speex",
    .codec_type       = AVMEDIA_TYPE_AUDIO,
    .codec_id         = AV_CODEC_ID_SPEEX,
    .parse_sdp_a_line = speex_parse_sdp_line,
};
