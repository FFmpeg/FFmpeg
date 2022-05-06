/*
 * AIX demuxer
 * Copyright (c) 2016 Paul B Mahol
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
#include "demux.h"
#include "internal.h"

static int aix_probe(const AVProbeData *p)
{
    if (AV_RL32(p->buf) != MKTAG('A','I','X','F') ||
        AV_RB32(p->buf +  8) != 0x01000014 ||
        AV_RB32(p->buf + 12) != 0x00000800)
        return 0;

    return AVPROBE_SCORE_MAX;
}

static int aix_read_header(AVFormatContext *s)
{
    unsigned nb_streams, first_offset, nb_segments;
    unsigned stream_list_offset;
    unsigned segment_list_offset = 0x20;
    unsigned segment_list_entry_size = 0x10;
    unsigned size;
    int i;

    avio_skip(s->pb, 4);
    first_offset = avio_rb32(s->pb) + 8;
    avio_skip(s->pb, 16);
    nb_segments = avio_rb16(s->pb);
    if (nb_segments == 0)
        return AVERROR_INVALIDDATA;
    stream_list_offset = segment_list_offset + segment_list_entry_size * nb_segments + 0x10;
    if (stream_list_offset >= first_offset)
        return AVERROR_INVALIDDATA;
    avio_seek(s->pb, stream_list_offset, SEEK_SET);
    nb_streams = avio_r8(s->pb);
    if (nb_streams == 0)
        return AVERROR_INVALIDDATA;
    avio_skip(s->pb, 7);
    for (i = 0; i < nb_streams; i++) {
        AVStream *st = avformat_new_stream(s, NULL);

        if (!st)
            return AVERROR(ENOMEM);
        st->codecpar->codec_type  = AVMEDIA_TYPE_AUDIO;
        st->codecpar->codec_id    = AV_CODEC_ID_ADPCM_ADX;
        st->codecpar->sample_rate = avio_rb32(s->pb);
        st->codecpar->ch_layout.nb_channels = avio_r8(s->pb);
        avpriv_set_pts_info(st, 64, 1, st->codecpar->sample_rate);
        avio_skip(s->pb, 3);
    }

    avio_seek(s->pb, first_offset, SEEK_SET);
    for (i = 0; i < nb_streams; i++) {
        if (avio_rl32(s->pb) != MKTAG('A','I','X','P'))
            return AVERROR_INVALIDDATA;
        size = avio_rb32(s->pb);
        if (size <= 8)
            return AVERROR_INVALIDDATA;
        avio_skip(s->pb, 8);
        ff_get_extradata(s, s->streams[i]->codecpar, s->pb, size - 8);
    }

    return 0;
}

static int aix_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    unsigned size, index, duration, chunk;
    int64_t pos;
    int sequence, ret, i;

    pos = avio_tell(s->pb);
    if (avio_feof(s->pb))
        return AVERROR_EOF;
    chunk = avio_rl32(s->pb);
    size = avio_rb32(s->pb);
    if (chunk == MKTAG('A','I','X','E')) {
        avio_skip(s->pb, size);
        for (i = 0; i < s->nb_streams; i++) {
            if (avio_feof(s->pb))
                return AVERROR_EOF;
            chunk = avio_rl32(s->pb);
            size = avio_rb32(s->pb);
            avio_skip(s->pb, size);
        }
        pos = avio_tell(s->pb);
        chunk = avio_rl32(s->pb);
        size = avio_rb32(s->pb);
    }

    if (chunk != MKTAG('A','I','X','P'))
        return AVERROR_INVALIDDATA;
    if (size <= 8)
        return AVERROR_INVALIDDATA;
    index = avio_r8(s->pb);
    if (avio_r8(s->pb) != s->nb_streams || index >= s->nb_streams)
        return AVERROR_INVALIDDATA;
    duration = avio_rb16(s->pb);
    sequence = avio_rb32(s->pb);
    if (sequence < 0) {
        avio_skip(s->pb, size - 8);
        return 0;
    }

    ret = av_get_packet(s->pb, pkt, size - 8);
    pkt->stream_index = index;
    pkt->duration = duration;
    pkt->pos = pos;
    return ret;
}

const AVInputFormat ff_aix_demuxer = {
    .name        = "aix",
    .long_name   = NULL_IF_CONFIG_SMALL("CRI AIX"),
    .read_probe  = aix_probe,
    .read_header = aix_read_header,
    .read_packet = aix_read_packet,
    .extensions  = "aix",
    .flags       = AVFMT_GENERIC_INDEX,
};
