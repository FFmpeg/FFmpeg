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
#include "internal.h"

typedef struct VAGDemuxContext {
    int64_t data_end;
} VAGDemuxContext;

static int vag_probe(AVProbeData *p)
{
    if (memcmp(p->buf, "VAGp\0\0\0", 7))
        return 0;

    return AVPROBE_SCORE_MAX;
}

static int vag_read_header(AVFormatContext *s)
{
    VAGDemuxContext *c = s->priv_data;
    AVStream *st;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    avio_skip(s->pb, 4);
    st->codec->codec_type  = AVMEDIA_TYPE_AUDIO;
    st->codec->codec_id    = AV_CODEC_ID_ADPCM_PSX;
    st->codec->channels    = 1 + (avio_rb32(s->pb) == 0x00000004);
    avio_skip(s->pb, 4);
    c->data_end            = avio_rb32(s->pb);
    st->duration           = (c->data_end - avio_tell(s->pb)) / (16 * st->codec->channels) * 28;
    st->codec->sample_rate = avio_rb32(s->pb);
    if (st->codec->sample_rate <= 0)
        return AVERROR_INVALIDDATA;
    st->codec->block_align = 16 * st->codec->channels;
    avio_skip(s->pb, 28);
    avpriv_set_pts_info(st, 64, 1, st->codec->sample_rate);

    return 0;
}

static int vag_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    VAGDemuxContext *c = s->priv_data;
    AVCodecContext *codec = s->streams[0]->codec;
    int size;

    size = FFMIN(c->data_end - avio_tell(s->pb), codec->block_align);
    if (size <= 0)
        return AVERROR_EOF;

    return av_get_packet(s->pb, pkt, size);
}

AVInputFormat ff_vag_demuxer = {
    .name           = "vag",
    .long_name      = NULL_IF_CONFIG_SMALL("Sony VAG"),
    .priv_data_size = sizeof(VAGDemuxContext),
    .read_probe     = vag_probe,
    .read_header    = vag_read_header,
    .read_packet    = vag_read_packet,
    .extensions     = "vag",
};
