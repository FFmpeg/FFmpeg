/*
 * DC STR demuxer
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

#include "avformat.h"
#include "internal.h"

static int dcstr_probe(const AVProbeData *p)
{
    if (p->buf_size < 224 || memcmp(p->buf + 213, "Sega Stream", 11))
        return 0;

    return AVPROBE_SCORE_MAX;
}

static int dcstr_read_header(AVFormatContext *s)
{
    unsigned codec, align;
    int mult;
    AVStream *st;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    st->codecpar->codec_type  = AVMEDIA_TYPE_AUDIO;
    st->codecpar->channels    = avio_rl32(s->pb);
    st->codecpar->sample_rate = avio_rl32(s->pb);
    if (st->codecpar->sample_rate <= 0)
        return AVERROR_INVALIDDATA;
    codec                  = avio_rl32(s->pb);
    align                  = avio_rl32(s->pb);
    avio_skip(s->pb, 4);
    st->duration           = avio_rl32(s->pb);
    mult                   = avio_rl32(s->pb);
    if (st->codecpar->channels <= 0 || mult <= 0 || mult > INT_MAX / st->codecpar->channels) {
        av_log(s, AV_LOG_ERROR, "invalid number of channels %d x %d\n", st->codecpar->channels, mult);
        return AVERROR_INVALIDDATA;
    }
    st->codecpar->channels *= mult;
    if (!align || align > INT_MAX / st->codecpar->channels)
        return AVERROR_INVALIDDATA;
    st->codecpar->block_align = align * st->codecpar->channels;

    switch (codec) {
    case  4: st->codecpar->codec_id = AV_CODEC_ID_ADPCM_AICA;       break;
    case 16: st->codecpar->codec_id = AV_CODEC_ID_PCM_S16LE_PLANAR; break;
    default: avpriv_request_sample(s, "codec %X", codec);
             return AVERROR_PATCHWELCOME;
    }

    avio_skip(s->pb, 0x800 - avio_tell(s->pb));
    avpriv_set_pts_info(st, 64, 1, st->codecpar->sample_rate);

    return 0;
}

static int dcstr_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVCodecParameters *par    = s->streams[0]->codecpar;
    return av_get_packet(s->pb, pkt, par->block_align);
}

AVInputFormat ff_dcstr_demuxer = {
    .name           = "dcstr",
    .long_name      = NULL_IF_CONFIG_SMALL("Sega DC STR"),
    .read_probe     = dcstr_probe,
    .read_header    = dcstr_read_header,
    .read_packet    = dcstr_read_packet,
    .extensions     = "str",
    .flags          = AVFMT_GENERIC_INDEX | AVFMT_NO_BYTE_SEEK | AVFMT_NOBINSEARCH,
};
