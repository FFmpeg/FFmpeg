/*
 * SDR2 demuxer
 * Copyright (c) 2014 Paul B Mahol
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
#include "internal.h"

static int sdr2_probe(AVProbeData *p)
{
    if (AV_RL32(p->buf) != MKTAG('S', 'R', 'A', 1))
        return 0;

    return AVPROBE_SCORE_EXTENSION;
}

#define FIRST 0xA8

static int sdr2_read_header(AVFormatContext *s)
{
    AVStream *st, *ast;

    ast = avformat_new_stream(s, 0);
    if (!ast)
        return AVERROR(ENOMEM);

    st = avformat_new_stream(s, 0);
    if (!st)
        return AVERROR(ENOMEM);

    avio_skip(s->pb, 20);
    avpriv_set_pts_info(st, 64, 1, avio_rl32(s->pb));
    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codecpar->width      = avio_rl32(s->pb);
    st->codecpar->height     = avio_rl32(s->pb);
    st->codecpar->codec_id   = AV_CODEC_ID_H264;
    st->need_parsing      = AVSTREAM_PARSE_FULL;

    ast->codecpar->codec_type  = AVMEDIA_TYPE_AUDIO;
    ast->codecpar->channels    = 1;
    ast->codecpar->sample_rate = 8000;
    ast->codecpar->codec_id    = AV_CODEC_ID_PCM_S16LE;
    avpriv_set_pts_info(ast, 64, 1, 8000);

    avio_seek(s->pb, FIRST, SEEK_SET);

    return 0;
}

static const uint8_t header[24] = {
    0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x00, 0x1e,
    0xa6, 0x80, 0xb0, 0x7e, 0x40, 0x00, 0x00, 0x00,
    0x01, 0x68, 0xce, 0x38, 0x80, 0x00, 0x00, 0x00
};

static int sdr2_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int64_t pos;
    unsigned next;
    int flags, ret = 0, is_video;

    pos = avio_tell(s->pb);

    flags = avio_rl32(s->pb);
    avio_skip(s->pb, 4);

    next = avio_rl32(s->pb);
    if (next <= 52)
        return AVERROR_INVALIDDATA;

    avio_skip(s->pb, 6);
    is_video = avio_rl32(s->pb);
    avio_skip(s->pb, 30);

    if (pos == FIRST) {
        if (av_new_packet(pkt, next - 52 + 24) < 0)
            return AVERROR(ENOMEM);
        memcpy(pkt->data, header, 24);
        ret = avio_read(s->pb, pkt->data + 24, next - 52);
        if (ret < 0) {
            av_packet_unref(pkt);
            return ret;
        }
        av_shrink_packet(pkt, ret + 24);
    } else {
        ret = av_get_packet(s->pb, pkt, next - 52);
    }
    pkt->stream_index = !!is_video;
    pkt->pos = pos;
    if (flags & (1 << 12))
        pkt->flags |= AV_PKT_FLAG_KEY;

    return ret;
}

AVInputFormat ff_sdr2_demuxer = {
    .name        = "sdr2",
    .long_name   = NULL_IF_CONFIG_SMALL("SDR2"),
    .read_probe  = sdr2_probe,
    .read_header = sdr2_read_header,
    .read_packet = sdr2_read_packet,
    .extensions  = "sdr2",
    .flags       = AVFMT_GENERIC_INDEX,
};
