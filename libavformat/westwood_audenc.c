/*
 * Copyright (c) 2021 Aidan Richmond
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
 * Westwood Studios AUD file muxer
 * by Aidan Richmond (aidan.is@hotmail.co.uk)
 *
 * This muxer supports IMA ADPCM packed in westwoods format.
 *
 * @see http://xhp.xwis.net/documents/aud3.txt
 */

#include "avformat.h"
#include "avio_internal.h"
#include "internal.h"
#include <stdint.h>

#define AUD_CHUNK_SIGNATURE 0x0000DEAF

typedef struct AUDMuxContext {
    int uncomp_size;
    int size;
} AUDMuxContext;

static int wsaud_write_init(AVFormatContext *ctx)
{
    AVStream     *st = ctx->streams[0];
    AVIOContext  *pb = ctx->pb;

    /* Stream must be seekable to correctly write the file. */
    if (!(pb->seekable & AVIO_SEEKABLE_NORMAL)) {
        av_log(ctx->streams[0], AV_LOG_ERROR, "Cannot write Westwood AUD to"
               " non-seekable stream.\n");
        return AVERROR(EINVAL);
    }

    if (st->codecpar->codec_id != AV_CODEC_ID_ADPCM_IMA_WS) {
        av_log(st, AV_LOG_ERROR, "%s codec not supported for Westwood AUD.\n",
               avcodec_get_name(st->codecpar->codec_id));
        return AVERROR(EINVAL);
    }

    if (ctx->nb_streams != 1) {
        av_log(st, AV_LOG_ERROR, "AUD files have exactly one stream\n");
        return AVERROR(EINVAL);
    }

    return 0;
}

static int wsaud_write_header(AVFormatContext *ctx)
{
    AVStream     *st = ctx->streams[0];
    AVIOContext  *pb = ctx->pb;
    AUDMuxContext *a = ctx->priv_data;
    unsigned char flags = 0;

    a->uncomp_size = 0;
    a->size = 0;

    /* Flag if we have stereo data. */
    if (st->codecpar->ch_layout.nb_channels == 2)
        flags |= 1;

    /* This flags that the file contains 16 bit samples rather than 8 bit
       since the encoder only encodes 16 bit samples this should be set. */
    if (av_get_bits_per_sample(st->codecpar->codec_id) == 4)
        flags |= 2;

    avio_wl16(pb, st->codecpar->sample_rate);
    /* We don't know the file size yet, so just zero 8 bytes */
    ffio_fill(pb, 0, 8);
    avio_w8(pb, flags);
    /* 99 indicates the ADPCM format. Other formats not supported. */
    avio_w8(pb, 99);

    return 0;
}

static int wsaud_write_packet(AVFormatContext *ctx, AVPacket *pkt)
{
    AVIOContext  *pb = ctx->pb;
    AUDMuxContext *a = ctx->priv_data;

    if (pkt->size > UINT16_MAX / 4)
        return AVERROR_INVALIDDATA;
    /* Assumes ADPCM since this muxer doesn't support SND1 or PCM format. */
    avio_wl16(pb, pkt->size);
    avio_wl16(pb, pkt->size * 4);
    avio_wl32(pb, AUD_CHUNK_SIGNATURE);
    avio_write(pb, pkt->data, pkt->size);
    a->size += pkt->size + 8;
    a->uncomp_size += pkt->size * 4;

    return 0;
}

static int wsaud_write_trailer(AVFormatContext *ctx)
{
    AVIOContext  *pb = ctx->pb;
    AUDMuxContext *a = ctx->priv_data;

    avio_seek(pb, 2, SEEK_SET);
    avio_wl32(pb, a->size);
    avio_wl32(pb, a->uncomp_size);

    return 0;
}

const AVOutputFormat ff_wsaud_muxer = {
    .name              = "wsaud",
    .long_name         = NULL_IF_CONFIG_SMALL("Westwood Studios audio"),
    .extensions        = "aud",
    .priv_data_size    = sizeof(AUDMuxContext),
    .audio_codec       = AV_CODEC_ID_ADPCM_IMA_WS,
    .video_codec       = AV_CODEC_ID_NONE,
    .init              = wsaud_write_init,
    .write_header      = wsaud_write_header,
    .write_packet      = wsaud_write_packet,
    .write_trailer     = wsaud_write_trailer,
};
