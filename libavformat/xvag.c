/*
 * XVAG demuxer
 * Copyright (c) 2015 Paul B Mahol
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

#include "libavutil/bswap.h"
#include "avformat.h"
#include "internal.h"

static int xvag_probe(AVProbeData *p)
{
    if (memcmp(p->buf, "XVAG", 4) ||
        memcmp(p->buf+32, "fmat", 4))
        return 0;

    return AVPROBE_SCORE_MAX;
}

static int xvag_read_header(AVFormatContext *s)
{
    unsigned offset, big_endian, codec;
    AVStream *st;

    avio_skip(s->pb, 4);

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    st->codec->codec_type = AVMEDIA_TYPE_AUDIO;

    offset     = avio_rl32(s->pb);
    big_endian = offset > av_bswap32(offset);
    if (big_endian) {
        offset                 = av_bswap32(offset);
        avio_skip(s->pb, 28);
        codec                  = avio_rb32(s->pb);
        st->codec->channels    = avio_rb32(s->pb);
        avio_skip(s->pb, 4);
        st->duration           = avio_rb32(s->pb);
        avio_skip(s->pb, 8);
        st->codec->sample_rate = avio_rb32(s->pb);
    } else {
        avio_skip(s->pb, 28);
        codec                  = avio_rl32(s->pb);
        st->codec->channels    = avio_rl32(s->pb);
        avio_skip(s->pb, 4);
        st->duration           = avio_rl32(s->pb);
        avio_skip(s->pb, 8);
        st->codec->sample_rate = avio_rl32(s->pb);
    }

    if (st->codec->sample_rate <= 0)
        return AVERROR_INVALIDDATA;
    if (st->codec->channels <= 0)
        return AVERROR_INVALIDDATA;

    switch (codec) {
    case 0x1c:
        st->codec->codec_id    = AV_CODEC_ID_ADPCM_PSX;
        st->codec->block_align = 16 * st->codec->channels;
        break;
    default:
        avpriv_request_sample(s, "codec %X", codec);
        return AVERROR_PATCHWELCOME;
    };

    avio_skip(s->pb, offset - avio_tell(s->pb));

    if (avio_rb16(s->pb) == 0xFFFB) {
        st->codec->codec_id    = AV_CODEC_ID_MP3;
        st->codec->block_align = 0x1000;
        st->need_parsing       = AVSTREAM_PARSE_FULL_RAW;
    }

    avio_skip(s->pb, -2);
    avpriv_set_pts_info(st, 64, 1, st->codec->sample_rate);

    return 0;
}

static int xvag_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVCodecContext *codec = s->streams[0]->codec;

    return av_get_packet(s->pb, pkt, codec->block_align);
}

AVInputFormat ff_xvag_demuxer = {
    .name           = "xvag",
    .long_name      = NULL_IF_CONFIG_SMALL("Sony PS3 XVAG"),
    .read_probe     = xvag_probe,
    .read_header    = xvag_read_header,
    .read_packet    = xvag_read_packet,
    .extensions     = "xvag",
};
