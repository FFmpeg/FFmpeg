/*
 * APAC demuxer
 * Copyright (c) 2022 Paul B Mahol
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
#include "rawdec.h"

static int apac_probe(const AVProbeData *p)
{
    if (AV_RB32(p->buf) == MKBETAG('A','P','A','C') &&
        AV_RB32(p->buf + 8) == MKBETAG('P','R','O','F') &&
        AV_RB32(p->buf + 12) == MKBETAG('N','A','D',' '))
        return AVPROBE_SCORE_MAX;

    return 0;
}

static int apac_read_header(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    uint32_t chunk_size;
    AVStream *st;
    int64_t pos;

    avio_skip(pb, 16);
    chunk_size = avio_rl32(pb);
    avio_skip(pb, chunk_size);
    if (avio_rb32(pb) != MKBETAG('P','F','M','T'))
        return AVERROR_INVALIDDATA;
    chunk_size = avio_rl32(pb);
    pos = avio_tell(pb);
    avio_skip(pb, 2);
    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);
    st->codecpar->codec_type  = AVMEDIA_TYPE_AUDIO;
    st->codecpar->codec_id    = AV_CODEC_ID_APAC;
    st->codecpar->ch_layout.nb_channels = avio_rl16(pb);
    st->codecpar->sample_rate = avio_rl32(pb);
    if (st->codecpar->ch_layout.nb_channels <= 0 ||
        st->codecpar->ch_layout.nb_channels >  2 ||
        st->codecpar->sample_rate <= 0)
        return AVERROR_INVALIDDATA;
    avio_skip(pb, 2);
    st->codecpar->bits_per_coded_sample = avio_rl16(pb);
    avpriv_set_pts_info(st, 64, 1, st->codecpar->sample_rate);
    avio_skip(pb, (chunk_size + pos) - avio_tell(pb) + (chunk_size & 1));
    if (avio_rb32(pb) != MKBETAG('P','A','D',' '))
        return AVERROR_INVALIDDATA;
    avio_skip(pb, 4);

    return 0;
}

const FFInputFormat ff_apac_demuxer = {
    .p.name         = "apac",
    .p.long_name    = NULL_IF_CONFIG_SMALL("raw APAC"),
    .p.extensions   = "apc",
    .p.flags        = AVFMT_NOBINSEARCH | AVFMT_NOGENSEARCH | AVFMT_NO_BYTE_SEEK | AVFMT_NOTIMESTAMPS,
    .p.priv_class   = &ff_raw_demuxer_class,
    .read_probe     = apac_probe,
    .read_header    = apac_read_header,
    .read_packet    = ff_raw_read_partial_packet,
    .raw_codec_id   = AV_CODEC_ID_APAC,
    .priv_data_size = sizeof(FFRawDemuxerContext),
};
