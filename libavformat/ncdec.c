/*
 * NC camera feed demuxer
 * Copyright (c) 2009  Nicolas Martin (martinic at iro dot umontreal dot ca)
 *                     Edouard Auvinet
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

#include "libavutil/intreadwrite.h"
#include "avformat.h"

#define NC_VIDEO_FLAG 0x1A5

static int nc_probe(AVProbeData *probe_packet)
{
    int size;

    if (AV_RB32(probe_packet->buf) != NC_VIDEO_FLAG)
        return 0;

    size = AV_RL16(probe_packet->buf + 5);

    if (size + 20 > probe_packet->buf_size)
        return AVPROBE_SCORE_MAX/4;

    if (AV_RB32(probe_packet->buf+16+size) == NC_VIDEO_FLAG)
        return AVPROBE_SCORE_MAX;

    return 0;
}

static int nc_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    AVStream *st = av_new_stream(s, 0);

    if (!st)
        return AVERROR(ENOMEM);

    st->codec->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codec->codec_id   = CODEC_ID_MPEG4;
    st->need_parsing      = AVSTREAM_PARSE_FULL;

    av_set_pts_info(st, 64, 1, 100);

    return 0;
}

static int nc_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int size;
    int ret;

    uint32_t state=-1;
    while (state != NC_VIDEO_FLAG) {
        if (url_feof(s->pb))
            return AVERROR(EIO);
        state = (state<<8) + get_byte(s->pb);
    }

    get_byte(s->pb);
    size = get_le16(s->pb);
    url_fskip(s->pb, 9);

    if (size == 0) {
        av_log(s, AV_LOG_DEBUG, "Next packet size is zero\n");
        return AVERROR(EAGAIN);
    }

    ret = av_get_packet(s->pb, pkt, size);
    if (ret != size) {
        if (ret > 0) av_free_packet(pkt);
        return AVERROR(EIO);
    }

    pkt->stream_index = 0;
    return size;
}

AVInputFormat nc_demuxer = {
    "nc",
    NULL_IF_CONFIG_SMALL("NC camera feed format"),
    0,
    nc_probe,
    nc_read_header,
    nc_read_packet,
    .extensions = "v",
};
