/*
 * RedSpark demuxer
 * Copyright (c) 2013 James Almer
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

#include "libavcodec/bytestream.h"
#include "libavutil/intreadwrite.h"
#include "avformat.h"
#include "avio.h"
#include "internal.h"

#define HEADER_SIZE 4096

typedef struct RedSparkContext {
    int         samples_count;
} RedSparkContext;

static int redspark_probe(AVProbeData *p)
{
    uint32_t key, data;
    uint8_t header[8];

    /* Decrypt first 8 bytes of the header */
    data = AV_RB32(p->buf);
    data = data ^ (key = data ^ 0x52656453);
    AV_WB32(header, data);
    key = (key << 11) | (key >> 21);

    data = AV_RB32(p->buf + 4) ^ (((key << 3) | (key >> 29)) + key);
    AV_WB32(header + 4, data);

    if (AV_RB64(header) == AV_RB64("RedSpark"))
        return AVPROBE_SCORE_MAX;

    return 0;
}

static int redspark_read_header(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    RedSparkContext *redspark = s->priv_data;
    AVCodecContext *codec;
    GetByteContext gbc;
    int i, coef_off, ret = 0;
    uint32_t key, data;
    uint8_t *header, *pbc;
    AVStream *st;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);
    codec = st->codec;

    header = av_malloc(HEADER_SIZE + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!header)
        return AVERROR(ENOMEM);
    pbc = header;

    /* Decrypt header */
    data = avio_rb32(pb);
    data = data ^ (key = data ^ 0x52656453);
    bytestream_put_be32(&pbc, data);
    key = (key << 11) | (key >> 21);

    for (i = 4; i < HEADER_SIZE; i += 4) {
        data = avio_rb32(pb) ^ (key = ((key << 3) | (key >> 29)) + key);
        bytestream_put_be32(&pbc, data);
    }

    codec->codec_id    = AV_CODEC_ID_ADPCM_THP;
    codec->codec_type  = AVMEDIA_TYPE_AUDIO;

    bytestream2_init(&gbc, header, HEADER_SIZE);
    bytestream2_seek(&gbc, 0x3c, SEEK_SET);
    codec->sample_rate = bytestream2_get_be32u(&gbc);
    if (codec->sample_rate <= 0 || codec->sample_rate > 96000) {
        av_log(s, AV_LOG_ERROR, "Invalid sample rate: %d\n", codec->sample_rate);
        ret = AVERROR_INVALIDDATA;
        goto fail;
    }

    st->duration = bytestream2_get_be32u(&gbc) * 14;
    redspark->samples_count = 0;
    bytestream2_skipu(&gbc, 10);
    codec->channels = bytestream2_get_byteu(&gbc);
    if (!codec->channels) {
        ret = AVERROR_INVALIDDATA;
        goto fail;
    }

    coef_off = 0x54 + codec->channels * 8;
    if (bytestream2_get_byteu(&gbc)) // Loop flag
        coef_off += 16;

    if (coef_off + codec->channels * (32 + 14) > HEADER_SIZE) {
        ret = AVERROR_INVALIDDATA;
        goto fail;
    }

    if (ff_alloc_extradata(codec, 32 * codec->channels)) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    /* Get the ADPCM table */
    bytestream2_seek(&gbc, coef_off, SEEK_SET);
    for (i = 0; i < codec->channels; i++) {
        if (bytestream2_get_bufferu(&gbc, codec->extradata + i * 32, 32) != 32) {
            ret = AVERROR_INVALIDDATA;
            goto fail;
        }
        bytestream2_skipu(&gbc, 14);
    }

    avpriv_set_pts_info(st, 64, 1, codec->sample_rate);

fail:
    av_free(header);

    return ret;
}

static int redspark_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVCodecContext *codec = s->streams[0]->codec;
    RedSparkContext *redspark = s->priv_data;
    uint32_t size = 8 * codec->channels;
    int ret;

    if (avio_feof(s->pb) || redspark->samples_count == s->streams[0]->duration)
        return AVERROR_EOF;

    ret = av_get_packet(s->pb, pkt, size);
    if (ret != size) {
        av_packet_unref(pkt);
        return AVERROR(EIO);
    }

    pkt->duration = 14;
    redspark->samples_count += pkt->duration;
    pkt->stream_index = 0;

    return ret;
}

AVInputFormat ff_redspark_demuxer = {
    .name           =   "redspark",
    .long_name      =   NULL_IF_CONFIG_SMALL("RedSpark"),
    .priv_data_size =   sizeof(RedSparkContext),
    .read_probe     =   redspark_probe,
    .read_header    =   redspark_read_header,
    .read_packet    =   redspark_read_packet,
    .extensions     =   "rsd",
};
