/*
 * Black ops audio demuxer
 * Copyright (c) 2013 Michael Niedermayer
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

static int probe(AVProbeData *p)
{
    if (p->buf_size < 2096)
        return 0;
    if (   AV_RL32(p->buf     ) != 1
        || AV_RL32(p->buf +  8) > 100000
        || AV_RL32(p->buf + 12) > 8
        || AV_RL32(p->buf + 16) != 2096
        ||!AV_RL32(p->buf + 21)
        || AV_RL16(p->buf + 25) != 2096
        || AV_RL32(p->buf + 48) % AV_RL32(p->buf + 21)
        )
        return 0;
    return AVPROBE_SCORE_EXTENSION;
}


static int read_header(AVFormatContext *s)
{
    AVStream *st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codec->codec_id = AV_CODEC_ID_ADPCM_MS;

    avio_rl32(s->pb);
    avio_rl32(s->pb);
    st->codec->sample_rate = avio_rl32(s->pb);
    st->codec->channels    = avio_rl32(s->pb);
    s->data_offset         = avio_rl32(s->pb);
    avio_r8(s->pb);
    st->codec->block_align = st->codec->channels * avio_rl32(s->pb);

    avio_seek(s->pb, s->data_offset, SEEK_SET);

    return 0;
}

static int read_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVStream *st = s->streams[0];

    return av_get_packet(s->pb, pkt, st->codec->block_align);
}

AVInputFormat ff_boa_demuxer = {
    .name           = "boa",
    .long_name      = NULL_IF_CONFIG_SMALL("Black Ops Audio"),
    .read_probe     = probe,
    .read_header    = read_header,
    .read_packet    = read_packet,
    .flags          = AVFMT_GENERIC_INDEX,
};
