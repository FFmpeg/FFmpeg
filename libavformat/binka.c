/*
 * Copyright (c) 2021 Paul B Mahol
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
#include "avio_internal.h"
#include "demux.h"
#include "internal.h"

typedef struct BinkaDemuxContext {
    int is_ueba;
} BinkaDemuxContext;

static int binka_probe(const AVProbeData *p)
{
    if (AV_RB32(p->buf) == MKBETAG('1', 'F', 'C', 'B') &&
        (p->buf[4] == 1 || p->buf[4] == 2))
        return AVPROBE_SCORE_MAX;
    if (AV_RL32(p->buf) == MKBETAG('U','E','B','A') && p->buf[4] == 1 &&
        (p->buf[5] == 1 || p->buf[5] == 2) && AV_RL32(p->buf + 8))
        return AVPROBE_SCORE_MAX;
    return 0;
}

static int binka_read_header(AVFormatContext *s)
{
    BinkaDemuxContext *binka = s->priv_data;
    AVIOContext *pb = s->pb;
    AVStream *st;
    int entries, offset;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    binka->is_ueba = avio_rl32(pb) == MKBETAG('U','E','B','A');

    avio_skip(pb, 1);

    st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codecpar->codec_id = AV_CODEC_ID_BINKAUDIO_DCT;
    st->codecpar->ch_layout.nb_channels = avio_r8(pb);
    if (binka->is_ueba) {
        avio_skip(pb, 2);
        st->codecpar->sample_rate = avio_rl32(pb);
        if (st->codecpar->sample_rate <= 0)
            return AVERROR_INVALIDDATA;
    } else
        st->codecpar->sample_rate = avio_rl16(pb);
    st->duration = avio_rl32(pb);

    avio_skip(pb, 8);
    entries = avio_rl16(pb);

    offset = entries * 2 + 2;
    avio_skip(pb, offset);

    avpriv_set_pts_info(st, 64, 1, st->codecpar->sample_rate);

    if (binka->is_ueba) {
        int ret = ff_alloc_extradata(st->codecpar, 1);
        if (ret < 0)
            return ret;
        st->codecpar->extradata[0] = '2';
    }

    return 0;
}

static int binka_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    BinkaDemuxContext *binka = s->priv_data;
    AVIOContext *pb = s->pb;
    AVStream *st = s->streams[0];
    int64_t pos, duration;
    int pkt_size;
    int ret;

    pos = avio_tell(pb);
    avio_skip(pb, 2);

    if (avio_feof(pb))
        return AVERROR_EOF;

    pkt_size = avio_rl16(pb);
    if (!pkt_size)
        return AVERROR_INVALIDDATA;

    if (binka->is_ueba && pkt_size == 0xFFFF) {
        pkt_size = avio_rl16(pb);
        duration = avio_rl16(pb);
    } else
        duration = av_get_audio_frame_duration2(st->codecpar, 0);

    ret = av_new_packet(pkt, pkt_size + 4);
    if (ret < 0)
        return ret;

    ret = ffio_read_size(pb, pkt->data + 4, pkt_size);
    if (ret < 0)
        return ret;
    AV_WL32(pkt->data, pkt_size);

    pkt->pos = pos;
    pkt->stream_index = 0;
    pkt->duration = duration;

    return 0;
}

const FFInputFormat ff_binka_demuxer = {
    .p.name         = "binka",
    .p.long_name    = NULL_IF_CONFIG_SMALL("Bink Audio"),
    .p.flags        = AVFMT_GENERIC_INDEX | AVFMT_NOTIMESTAMPS,
    .p.extensions   = "binka",
    .priv_data_size = sizeof(BinkaDemuxContext),
    .read_probe     = binka_probe,
    .read_header    = binka_read_header,
    .read_packet    = binka_read_packet,
};
