/*
 * DSD Stream File (DSF) demuxer
 * Copyright (c) 2014 Peter Ross
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
#include "internal.h"
#include "id3v2.h"

typedef struct {
    uint64_t data_end;
} DSFContext;

static int dsf_probe(AVProbeData *p)
{
    if (p->buf_size < 12 || memcmp(p->buf, "DSD ", 4) || AV_RL64(p->buf + 4) != 28)
        return 0;
    return AVPROBE_SCORE_MAX;
}

static const uint64_t dsf_channel_layout[] = {
    0,
    AV_CH_LAYOUT_MONO,
    AV_CH_LAYOUT_STEREO,
    AV_CH_LAYOUT_SURROUND,
    AV_CH_LAYOUT_QUAD,
    AV_CH_LAYOUT_4POINT0,
    AV_CH_LAYOUT_5POINT0_BACK,
    AV_CH_LAYOUT_5POINT1_BACK,
};

static void read_id3(AVFormatContext *s, uint64_t id3pos)
{
    ID3v2ExtraMeta *id3v2_extra_meta = NULL;
    if (avio_seek(s->pb, id3pos, SEEK_SET) < 0)
        return;

    ff_id3v2_read(s, ID3v2_DEFAULT_MAGIC, &id3v2_extra_meta, 0);
    if (id3v2_extra_meta)
        ff_id3v2_parse_apic(s, &id3v2_extra_meta);
    ff_id3v2_free_extra_meta(&id3v2_extra_meta);
}

static int dsf_read_header(AVFormatContext *s)
{
    DSFContext *dsf = s->priv_data;
    AVIOContext *pb = s->pb;
    AVStream *st;
    uint64_t id3pos;
    unsigned int channel_type;

    avio_skip(pb, 4);
    if (avio_rl64(pb) != 28)
        return AVERROR_INVALIDDATA;

    /* create primary stream before any id3 coverart streams */
    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    avio_skip(pb, 8);
    id3pos = avio_rl64(pb);
    if (pb->seekable) {
        read_id3(s, id3pos);
        avio_seek(pb, 28, SEEK_SET);
    }

    /* fmt chunk */

    if (avio_rl32(pb) != MKTAG('f', 'm', 't', ' ') || avio_rl64(pb) != 52)
        return AVERROR_INVALIDDATA;

    if (avio_rl32(pb) != 1) {
        avpriv_request_sample(s, "unknown format version");
        return AVERROR_INVALIDDATA;
    }

    if (avio_rl32(pb)) {
        avpriv_request_sample(s, "unknown format id");
        return AVERROR_INVALIDDATA;
    }

    channel_type = avio_rl32(pb);
    if (channel_type < FF_ARRAY_ELEMS(dsf_channel_layout))
        st->codecpar->channel_layout = dsf_channel_layout[channel_type];
    if (!st->codecpar->channel_layout)
        avpriv_request_sample(s, "channel type %i", channel_type);

    st->codecpar->codec_type   = AVMEDIA_TYPE_AUDIO;
    st->codecpar->channels     = avio_rl32(pb);
    st->codecpar->sample_rate  = avio_rl32(pb) / 8;

    if (st->codecpar->channels <= 0)
        return AVERROR_INVALIDDATA;

    switch(avio_rl32(pb)) {
    case 1: st->codecpar->codec_id = AV_CODEC_ID_DSD_LSBF_PLANAR; break;
    case 8: st->codecpar->codec_id = AV_CODEC_ID_DSD_MSBF_PLANAR; break;
    default:
        avpriv_request_sample(s, "unknown most significant bit");
        return AVERROR_INVALIDDATA;
    }

    avio_skip(pb, 8);
    st->codecpar->block_align = avio_rl32(pb);
    if (st->codecpar->block_align > INT_MAX / st->codecpar->channels) {
        avpriv_request_sample(s, "block_align overflow");
        return AVERROR_INVALIDDATA;
    }
    st->codecpar->block_align *= st->codecpar->channels;
    st->codecpar->bit_rate = st->codecpar->channels * st->codecpar->sample_rate * 8LL;
    avio_skip(pb, 4);

    /* data chunk */

    dsf->data_end = avio_tell(pb);
    if (avio_rl32(pb) != MKTAG('d', 'a', 't', 'a'))
        return AVERROR_INVALIDDATA;
    dsf->data_end += avio_rl64(pb);

    return 0;
}

static int dsf_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    DSFContext *dsf = s->priv_data;
    AVIOContext *pb = s->pb;
    AVStream *st = s->streams[0];
    int64_t pos = avio_tell(pb);

    if (pos >= dsf->data_end)
        return AVERROR_EOF;

    pkt->stream_index = 0;
    return av_get_packet(pb, pkt, FFMIN(dsf->data_end - pos, st->codecpar->block_align));
}

AVInputFormat ff_dsf_demuxer = {
    .name           = "dsf",
    .long_name      = NULL_IF_CONFIG_SMALL("DSD Stream File (DSF)"),
    .priv_data_size = sizeof(DSFContext),
    .read_probe     = dsf_probe,
    .read_header    = dsf_read_header,
    .read_packet    = dsf_read_packet,
    .flags          = AVFMT_GENERIC_INDEX | AVFMT_NO_BYTE_SEEK,
};
