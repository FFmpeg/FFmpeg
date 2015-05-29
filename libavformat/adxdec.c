/*
 * Copyright (c) 2011 Justin Ruggles
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

/**
 * @file
 * CRI ADX demuxer
 */

#include "libavutil/intreadwrite.h"
#include "avformat.h"
#include "internal.h"

#define BLOCK_SIZE    18
#define BLOCK_SAMPLES 32

typedef struct ADXDemuxerContext {
    int header_size;
} ADXDemuxerContext;

static int adx_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    ADXDemuxerContext *c = s->priv_data;
    AVCodecContext *avctx = s->streams[0]->codec;
    int ret, size;

    if (avctx->channels <= 0) {
        av_log(s, AV_LOG_ERROR, "invalid number of channels %d\n", avctx->channels);
        return AVERROR_INVALIDDATA;
    }

    size = BLOCK_SIZE * avctx->channels;

    pkt->pos = avio_tell(s->pb);
    pkt->stream_index = 0;

    ret = av_get_packet(s->pb, pkt, size);
    if (ret != size) {
        av_free_packet(pkt);
        return ret < 0 ? ret : AVERROR(EIO);
    }
    if (AV_RB16(pkt->data) & 0x8000) {
        av_free_packet(pkt);
        return AVERROR_EOF;
    }
    pkt->size     = size;
    pkt->duration = 1;
    pkt->pts      = (pkt->pos - c->header_size) / size;

    return 0;
}

static int adx_read_header(AVFormatContext *s)
{
    ADXDemuxerContext *c = s->priv_data;
    AVCodecContext *avctx;

    AVStream *st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);
    avctx = s->streams[0]->codec;

    if (avio_rb16(s->pb) != 0x8000)
        return AVERROR_INVALIDDATA;
    c->header_size = avio_rb16(s->pb) + 4;
    avio_seek(s->pb, -4, SEEK_CUR);

    if (ff_get_extradata(avctx, s->pb, c->header_size) < 0)
        return AVERROR(ENOMEM);

    if (avctx->extradata_size < 12) {
        av_log(s, AV_LOG_ERROR, "Invalid extradata size.\n");
        return AVERROR_INVALIDDATA;
    }
    avctx->channels    = AV_RB8(avctx->extradata + 7);
    avctx->sample_rate = AV_RB32(avctx->extradata + 8);

    if (avctx->channels <= 0) {
        av_log(s, AV_LOG_ERROR, "invalid number of channels %d\n", avctx->channels);
        return AVERROR_INVALIDDATA;
    }

    st->codec->codec_type  = AVMEDIA_TYPE_AUDIO;
    st->codec->codec_id    = s->iformat->raw_codec_id;

    avpriv_set_pts_info(st, 64, BLOCK_SAMPLES, avctx->sample_rate);

    return 0;
}

AVInputFormat ff_adx_demuxer = {
    .name           = "adx",
    .long_name      = NULL_IF_CONFIG_SMALL("CRI ADX"),
    .priv_data_size = sizeof(ADXDemuxerContext),
    .read_header    = adx_read_header,
    .read_packet    = adx_read_packet,
    .extensions     = "adx",
    .raw_codec_id   = AV_CODEC_ID_ADPCM_ADX,
    .flags          = AVFMT_GENERIC_INDEX,
};
