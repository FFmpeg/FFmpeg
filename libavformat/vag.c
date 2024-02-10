/*
 * VAG demuxer
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

#include "libavutil/channel_layout.h"
#include "avformat.h"
#include "demux.h"
#include "internal.h"

static int vag_probe(const AVProbeData *p)
{
    if (memcmp(p->buf, "VAGp\0\0\0", 7))
        return 0;

    return AVPROBE_SCORE_MAX;
}

static int vag_read_header(AVFormatContext *s)
{
    AVStream *st;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    avio_skip(s->pb, 4);
    st->codecpar->codec_type  = AVMEDIA_TYPE_AUDIO;
    st->codecpar->codec_id    = AV_CODEC_ID_ADPCM_PSX;
    st->codecpar->ch_layout.nb_channels = 1 + (avio_rb32(s->pb) == 0x00000004);
    avio_skip(s->pb, 4);
    if (st->codecpar->ch_layout.nb_channels > 1) {
        st->duration       = avio_rb32(s->pb);
    } else {
        st->duration       = avio_rb32(s->pb) / 16 * 28;
    }
    st->codecpar->sample_rate = avio_rb32(s->pb);
    if (st->codecpar->sample_rate <= 0)
        return AVERROR_INVALIDDATA;
    avio_seek(s->pb, 0x1000, SEEK_SET);
    if (avio_rl32(s->pb) == MKTAG('V','A','G','p')) {
        st->codecpar->block_align = 0x1000 * st->codecpar->ch_layout.nb_channels;
        avio_seek(s->pb, 0, SEEK_SET);
        st->duration = st->duration / 16 * 28;
    } else {
        st->codecpar->block_align = 16 * st->codecpar->ch_layout.nb_channels;
        avio_seek(s->pb, st->codecpar->ch_layout.nb_channels > 1 ? 0x80 : 0x30, SEEK_SET);
    }
    avpriv_set_pts_info(st, 64, 1, st->codecpar->sample_rate);

    return 0;
}

static int vag_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVCodecParameters *par = s->streams[0]->codecpar;

    return av_get_packet(s->pb, pkt, par->block_align);
}

const FFInputFormat ff_vag_demuxer = {
    .p.name         = "vag",
    .p.long_name    = NULL_IF_CONFIG_SMALL("Sony PS2 VAG"),
    .p.extensions   = "vag",
    .read_probe     = vag_probe,
    .read_header    = vag_read_header,
    .read_packet    = vag_read_packet,
};
