/*
 * extract pts as timecode v2, as defined by mkvtoolnix
 * Copyright (c) 2009 David Conrad
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

static int write_header(AVFormatContext *s)
{
    static const char *header = "# timecode format v2\n";
    put_buffer(s->pb, header, strlen(header));
    av_set_pts_info(s->streams[0], 64, 1, 1000);
    return 0;
}

static int write_packet(AVFormatContext *s, AVPacket *pkt)
{
    char buf[256];
    if (pkt->stream_index)
        av_log(s, AV_LOG_WARNING, "More than one stream unsupported\n");
    snprintf(buf, sizeof(buf), "%" PRId64 "\n", pkt->dts);
    put_buffer(s->pb, buf, strlen(buf));
    put_flush_packet(s->pb);
    return 0;
}

AVOutputFormat ff_mkvtimestamp_v2_muxer = {
    "mkvtimestamp_v2",
    NULL_IF_CONFIG_SMALL("extract pts as timecode v2 format, as defined by mkvtoolnix"),
    NULL,
    "",
    0,
    CODEC_ID_NONE,
    CODEC_ID_RAWVIDEO,
    write_header,
    write_packet,
};
