/*
 * CCTV DAT demuxer
 *
 * Copyright (c) 2020 Paul B Mahol
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
#include "avio_internal.h"
#include "avformat.h"
#include "internal.h"

static int dat_probe(const AVProbeData *p)
{
    if (p->buf_size < 0x2080)
        return 0;

    if (memcmp(p->buf, "luo ", 4))
        return 0;

    if (memcmp(p->buf + 0x1ffc, " oulliu ", 8))
        return 0;

    if (!AV_RL32(p->buf + 0x2004))
        return 0;

    if (memcmp(p->buf + 0x207c, " uil", 4))
        return 0;

    return AVPROBE_SCORE_MAX;
}

static int dat_read_header(AVFormatContext *s)
{
    s->ctx_flags |= AVFMTCTX_NOHEADER;

    avio_seek(s->pb, 0x2000, SEEK_SET);

    return 0;
}

static int dat_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVIOContext *pb = s->pb;
    int index, ret, key, stream_id, stream_index, width, height, fps, pkt_size;
    int64_t pts, pos = avio_tell(pb);

    if (avio_feof(pb))
        return AVERROR_EOF;

    if (avio_rb32(pb) != MKBETAG('l', 'i', 'u', ' '))
        return AVERROR_INVALIDDATA;
    stream_id = avio_rl32(pb);
    width     = avio_rl32(pb);
    height    = avio_rl32(pb);
    fps       = avio_rl32(pb);
    avio_skip(pb, 16);
    key       = avio_rl32(pb) == 1;
    avio_skip(pb, 4);
    index     = avio_rl32(pb);
    avio_skip(pb, 4);
    pts       = avio_rl64(pb);
    pkt_size  = avio_rl32(pb);
    avio_skip(pb, 64);

    if (pkt_size == 0)
        return AVERROR_EOF;

    for (stream_index = 0; stream_index < s->nb_streams; stream_index++) {
        if (s->streams[stream_index]->id == stream_id)
            break;
    }

    if (stream_index == s->nb_streams) {
        AVStream *st = avformat_new_stream(s, NULL);

        if (!st)
            return AVERROR(ENOMEM);

        st->id = stream_id;
        st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
        st->codecpar->codec_id   = AV_CODEC_ID_H264;
        st->codecpar->width      = width;
        st->codecpar->height     = height;
        avpriv_set_pts_info(st, 64, 1, fps);
    }

    if (index >= s->nb_streams)
        av_log(s, AV_LOG_WARNING, "Stream index out of range.\n");

    ret = av_get_packet(pb, pkt, pkt_size);
    if (ret < 0)
        return ret;
    pkt->pos = pos;
    pkt->pts = pts;
    pkt->stream_index = stream_index;
    if (key)
        pkt->flags |= AV_PKT_FLAG_KEY;

    return ret;
}

AVInputFormat ff_luodat_demuxer = {
    .name           = "luodat",
    .long_name      = NULL_IF_CONFIG_SMALL("Video CCTV DAT"),
    .read_probe     = dat_probe,
    .read_header    = dat_read_header,
    .read_packet    = dat_read_packet,
    .extensions     = "dat",
    .flags          = AVFMT_GENERIC_INDEX,
};
