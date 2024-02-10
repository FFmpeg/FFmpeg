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
#include "libavcodec/internal.h"
#include "avformat.h"
#include "demux.h"
#include "internal.h"

static int probe(const AVProbeData *p)
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
    uint32_t data_offset;

    if (!st)
        return AVERROR(ENOMEM);

    st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codecpar->codec_id = AV_CODEC_ID_ADPCM_MS;

    avio_rl32(s->pb);
    avio_rl32(s->pb);
    st->codecpar->sample_rate = avio_rl32(s->pb);
    st->codecpar->ch_layout.nb_channels    = avio_rl32(s->pb);
    if (st->codecpar->ch_layout.nb_channels > FF_SANE_NB_CHANNELS ||
        st->codecpar->ch_layout.nb_channels <= 0)
        return AVERROR(ENOSYS);
    ffformatcontext(s)->data_offset = data_offset = avio_rl32(s->pb);
    avio_r8(s->pb);
    st->codecpar->block_align = avio_rl32(s->pb);
    if (st->codecpar->block_align > INT_MAX / FF_SANE_NB_CHANNELS || st->codecpar->block_align <= 0)
        return AVERROR_INVALIDDATA;
    st->codecpar->block_align *= st->codecpar->ch_layout.nb_channels;

    avio_seek(s->pb, data_offset, SEEK_SET);

    return 0;
}

static int read_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVStream *st = s->streams[0];

    return av_get_packet(s->pb, pkt, st->codecpar->block_align);
}

const FFInputFormat ff_boa_demuxer = {
    .p.name         = "boa",
    .p.long_name    = NULL_IF_CONFIG_SMALL("Black Ops Audio"),
    .p.flags        = AVFMT_GENERIC_INDEX,
    .read_probe     = probe,
    .read_header    = read_header,
    .read_packet    = read_packet,
};
