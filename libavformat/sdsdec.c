/*
 * MIDI Sample Dump Standard format demuxer
 * Copyright (c) 2017 Paul B Mahol
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

typedef struct SDSContext {
    uint8_t data[120];
    int bit_depth;
    int size;
    void (*read_block)(const uint8_t *src, uint32_t *dst);
} SDSContext;

static int sds_probe(const AVProbeData *p)
{
    if (AV_RB32(p->buf) == 0xF07E0001 && p->buf[20] == 0xF7 &&
        p->buf[6] >= 8 && p->buf[6] <= 28)
        return AVPROBE_SCORE_EXTENSION;
    return 0;
}

static void byte2_read(const uint8_t *src, uint32_t *dst)
{
    int i;

    for (i = 0; i < 120; i += 2) {
        unsigned sample = ((unsigned)src[i + 0] << 25) + ((unsigned)src[i + 1] << 18);

        dst[i / 2] = sample;
    }
}

static void byte3_read(const uint8_t *src, uint32_t *dst)
{
    int i;

    for (i = 0; i < 120; i += 3) {
        unsigned sample;

        sample = ((unsigned)src[i + 0] << 25) | ((unsigned)src[i + 1] << 18) | ((unsigned)src[i + 2] << 11);
        dst[i / 3] = sample;
    }
}

static void byte4_read(const uint8_t *src, uint32_t *dst)
{
    int i;

    for (i = 0; i < 120; i += 4) {
        unsigned sample;

        sample = ((unsigned)src[i + 0] << 25) | ((unsigned)src[i + 1] << 18) | ((unsigned)src[i + 2] << 11) | ((unsigned)src[i + 3] << 4);
        dst[i / 4] = sample;
    }
}

#define SDS_3BYTE_TO_INT_DECODE(x) (((x) & 0x7F) | (((x) & 0x7F00) >> 1) | (((x) & 0x7F0000) >> 2))

static int sds_read_header(AVFormatContext *ctx)
{
    SDSContext *s = ctx->priv_data;
    unsigned sample_period;
    AVIOContext *pb = ctx->pb;
    AVStream *st;

    st = avformat_new_stream(ctx, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    avio_skip(pb, 4);
    avio_skip(pb, 2);

    s->bit_depth = avio_r8(pb);
    if (s->bit_depth < 8 || s->bit_depth > 28)
        return AVERROR_INVALIDDATA;

    if (s->bit_depth < 14) {
        s->read_block = byte2_read;
        s->size = 60 * 4;
    } else if (s->bit_depth < 21) {
        s->read_block = byte3_read;
        s->size = 40 * 4;
    } else {
        s->read_block = byte4_read;
        s->size = 30 * 4;
    }
    st->codecpar->codec_id = AV_CODEC_ID_PCM_U32LE;

    sample_period = avio_rl24(pb);
    sample_period = SDS_3BYTE_TO_INT_DECODE(sample_period);
    avio_skip(pb, 11);

    st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codecpar->ch_layout.nb_channels = 1;
    st->codecpar->sample_rate = sample_period ? 1000000000 / sample_period : 16000;
    st->duration = av_rescale((avio_size(pb) - 21) / 127,  s->size, 4);

    avpriv_set_pts_info(st, 64, 1, st->codecpar->sample_rate);

    return 0;
}

static int sds_read_packet(AVFormatContext *ctx, AVPacket *pkt)
{
    SDSContext *s = ctx->priv_data;
    AVIOContext *pb = ctx->pb;
    int64_t pos;
    int ret;

    if (avio_feof(pb))
        return AVERROR_EOF;

    pos = avio_tell(pb);
    if (avio_rb16(pb) != 0xF07E)
        return AVERROR_INVALIDDATA;
    avio_skip(pb, 3);

    ret = av_new_packet(pkt, s->size);
    if (ret < 0)
        return ret;

    ret = avio_read(pb, s->data, 120);

    s->read_block(s->data, (uint32_t *)pkt->data);

    avio_skip(pb, 1); // checksum
    if (avio_r8(pb) != 0xF7)
        return AVERROR_INVALIDDATA;

    pkt->flags &= ~AV_PKT_FLAG_CORRUPT;
    pkt->stream_index = 0;
    pkt->pos = pos;

    return ret;
}

const FFInputFormat ff_sds_demuxer = {
    .p.name         = "sds",
    .p.long_name    = NULL_IF_CONFIG_SMALL("MIDI Sample Dump Standard"),
    .p.extensions   = "sds",
    .p.flags        = AVFMT_GENERIC_INDEX,
    .priv_data_size = sizeof(SDSContext),
    .read_probe     = sds_probe,
    .read_header    = sds_read_header,
    .read_packet    = sds_read_packet,
};
