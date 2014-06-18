/*
 * RTP iLBC Depacketizer, RFC 3952
 * Copyright (c) 2012 Martin Storsjo
 *
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

#include "avformat.h"
#include "rtpdec_formats.h"
#include "libavutil/avstring.h"

static int ilbc_parse_fmtp(AVFormatContext *s,
                           AVStream *stream, PayloadContext *data,
                           const char *attr, const char *value)
{
    if (!strcmp(attr, "mode")) {
        int mode = atoi(value);
        switch (mode) {
        case 20:
            stream->codecpar->block_align = 38;
            break;
        case 30:
            stream->codecpar->block_align = 50;
            break;
        default:
            av_log(s, AV_LOG_ERROR, "Unsupported iLBC mode %d\n", mode);
            return AVERROR(EINVAL);
        }
    }
    return 0;
}

static int ilbc_parse_sdp_line(AVFormatContext *s, int st_index,
                               PayloadContext *data, const char *line)
{
    const char *p;
    AVStream *st;

    if (st_index < 0)
        return 0;
    st = s->streams[st_index];

    if (av_strstart(line, "fmtp:", &p)) {
        int ret = ff_parse_fmtp(s, st, data, p, ilbc_parse_fmtp);
        if (ret < 0)
            return ret;
        if (!st->codecpar->block_align) {
            av_log(s, AV_LOG_ERROR, "No iLBC mode set\n");
            return AVERROR(EINVAL);
        }
    }
    return 0;
}

RTPDynamicProtocolHandler ff_ilbc_dynamic_handler = {
    .enc_name         = "iLBC",
    .codec_type       = AVMEDIA_TYPE_AUDIO,
    .codec_id         = AV_CODEC_ID_ILBC,
    .parse_sdp_a_line = ilbc_parse_sdp_line,
};
