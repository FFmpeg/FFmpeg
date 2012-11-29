/*
 * AST demuxer
 * Copyright (c) 2012 Paul B Mahol
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

#include "libavutil/channel_layout.h"
#include "libavutil/intreadwrite.h"
#include "avformat.h"
#include "internal.h"
#include "ast.h"

static int ast_probe(AVProbeData *p)
{
    if (AV_RL32(p->buf) == MKTAG('S','T','R','M') &&
        AV_RB16(p->buf + 10) &&
        AV_RB16(p->buf + 12) &&
        AV_RB32(p->buf + 16))
        return AVPROBE_SCORE_MAX / 3 * 2;
    return 0;
}

static int ast_read_header(AVFormatContext *s)
{
    int depth;
    AVStream *st;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    avio_skip(s->pb, 8);
    st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codec->codec_id   = ff_codec_get_id(ff_codec_ast_tags, avio_rb16(s->pb));

    depth = avio_rb16(s->pb);
    if (depth != 16) {
        av_log_ask_for_sample(s, "unsupported depth %d\n", depth);
        return AVERROR_INVALIDDATA;
    }

    st->codec->channels = avio_rb16(s->pb);
    if (!st->codec->channels)
        return AVERROR_INVALIDDATA;

    if (st->codec->channels == 2)
        st->codec->channel_layout = AV_CH_LAYOUT_STEREO;
    else if (st->codec->channels == 4)
        st->codec->channel_layout = AV_CH_LAYOUT_4POINT0;

    avio_skip(s->pb, 2);
    st->codec->sample_rate = avio_rb32(s->pb);
    if (st->codec->sample_rate <= 0)
        return AVERROR_INVALIDDATA;
    st->start_time         = 0;
    st->duration           = avio_rb32(s->pb);
    avio_skip(s->pb, 40);
    avpriv_set_pts_info(st, 64, 1, st->codec->sample_rate);

    return 0;
}

static int ast_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    uint32_t type, size;
    int64_t pos;
    int ret;

    if (url_feof(s->pb))
        return AVERROR_EOF;

    pos  = avio_tell(s->pb);
    type = avio_rl32(s->pb);
    size = avio_rb32(s->pb);
    if (size > INT_MAX / s->streams[0]->codec->channels)
        return AVERROR_INVALIDDATA;

    size *= s->streams[0]->codec->channels;
    if ((ret = avio_skip(s->pb, 24)) < 0) // padding
        return ret;

    if (type == MKTAG('B','L','C','K')) {
        ret = av_get_packet(s->pb, pkt, size);
        pkt->stream_index = 0;
        pkt->pos = pos;
    } else {
        av_log(s, AV_LOG_ERROR, "unknown chunk %x\n", type);
        avio_skip(s->pb, size);
        ret = AVERROR_INVALIDDATA;
    }

    return ret;
}

AVInputFormat ff_ast_demuxer = {
    .name           = "ast",
    .long_name      = NULL_IF_CONFIG_SMALL("AST (Audio Stream)"),
    .read_probe     = ast_probe,
    .read_header    = ast_read_header,
    .read_packet    = ast_read_packet,
    .extensions     = "ast",
    .flags          = AVFMT_GENERIC_INDEX,
    .codec_tag      = (const AVCodecTag* const []){ff_codec_ast_tags, 0},
};
