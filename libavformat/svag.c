/*
 * SVAG demuxer
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

static int svag_probe(const AVProbeData *p)
{
    if (memcmp(p->buf, "Svag", 4))
        return 0;

    return AVPROBE_SCORE_MAX;
}

static int svag_read_header(AVFormatContext *s)
{
    unsigned size, align;
    AVStream *st;

    avio_skip(s->pb, 4);

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    size                   = avio_rl32(s->pb);
    st->codecpar->codec_type  = AVMEDIA_TYPE_AUDIO;
    st->codecpar->codec_id    = AV_CODEC_ID_ADPCM_PSX;
    st->codecpar->sample_rate = avio_rl32(s->pb);
    if (st->codecpar->sample_rate <= 0)
        return AVERROR_INVALIDDATA;
    st->codecpar->channels    = avio_rl32(s->pb);
    if (st->codecpar->channels <= 0 || st->codecpar->channels > 8)
        return AVERROR_INVALIDDATA;
    st->duration           = size / (16 * st->codecpar->channels) * 28;
    align                  = avio_rl32(s->pb);
    if (align <= 0 || align > INT_MAX / st->codecpar->channels)
        return AVERROR_INVALIDDATA;
    st->codecpar->block_align = align * st->codecpar->channels;
    avio_skip(s->pb, 0x800 - avio_tell(s->pb));
    avpriv_set_pts_info(st, 64, 1, st->codecpar->sample_rate);

    return 0;
}

static int svag_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVCodecParameters *par = s->streams[0]->codecpar;

    return av_get_packet(s->pb, pkt, par->block_align);
}

AVInputFormat ff_svag_demuxer = {
    .name           = "svag",
    .long_name      = NULL_IF_CONFIG_SMALL("Konami PS2 SVAG"),
    .read_probe     = svag_probe,
    .read_header    = svag_read_header,
    .read_packet    = svag_read_packet,
    .extensions     = "svag",
};
