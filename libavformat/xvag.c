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
#include "libavcodec/internal.h"
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

    st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;

    offset     = avio_rl32(s->pb);
    big_endian = offset > av_bswap32(offset);
    if (big_endian) {
        offset                 = av_bswap32(offset);
        avio_skip(s->pb, 28);
        codec                  = avio_rb32(s->pb);
        st->codecpar->channels = avio_rb32(s->pb);
        avio_skip(s->pb, 4);
        st->duration           = avio_rb32(s->pb);
        avio_skip(s->pb, 8);
        st->codecpar->sample_rate = avio_rb32(s->pb);
    } else {
        avio_skip(s->pb, 28);
        codec                  = avio_rl32(s->pb);
        st->codecpar->channels = avio_rl32(s->pb);
        avio_skip(s->pb, 4);
        st->duration           = avio_rl32(s->pb);
        avio_skip(s->pb, 8);
        st->codecpar->sample_rate = avio_rl32(s->pb);
    }

    if (st->codecpar->sample_rate <= 0)
        return AVERROR_INVALIDDATA;
    if (st->codecpar->channels <= 0 || st->codecpar->channels > FF_SANE_NB_CHANNELS)
        return AVERROR_INVALIDDATA;

    switch (codec) {
    case 0x1c:
        st->codecpar->codec_id    = AV_CODEC_ID_ADPCM_PSX;
        st->codecpar->block_align = 16 * st->codecpar->channels;
        break;
    default:
        avpriv_request_sample(s, "codec %X", codec);
        return AVERROR_PATCHWELCOME;
    };

    avio_skip(s->pb, offset - avio_tell(s->pb));

    if (avio_rb16(s->pb) == 0xFFFB) {
        st->codecpar->codec_id    = AV_CODEC_ID_MP3;
        st->codecpar->block_align = 0x1000;
        st->need_parsing       = AVSTREAM_PARSE_FULL_RAW;
    }

    avio_skip(s->pb, -2);
    avpriv_set_pts_info(st, 64, 1, st->codecpar->sample_rate);

    return 0;
}

static int xvag_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVCodecParameters *par = s->streams[0]->codecpar;

    return av_get_packet(s->pb, pkt, par->block_align);
}

AVInputFormat ff_xvag_demuxer = {
    .name           = "xvag",
    .long_name      = NULL_IF_CONFIG_SMALL("Sony PS3 XVAG"),
    .read_probe     = xvag_probe,
    .read_header    = xvag_read_header,
    .read_packet    = xvag_read_packet,
    .extensions     = "xvag",
};
