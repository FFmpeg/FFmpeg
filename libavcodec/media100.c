/*
 * Media 100 decoder
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

/**
 * @file
 * Media 100 decoder.
 */

#include <inttypes.h>

#include "libavutil/intreadwrite.h"
#include "avcodec.h"
#include "bytestream.h"
#include "codec_internal.h"

typedef struct Media100Context {
    AVCodecContext *avctx;   // wrapper context for mjpegb
    AVPacket *pkt;
} Media100Context;

static av_cold int media100_decode_init(AVCodecContext *avctx)
{
    Media100Context *ctx = avctx->priv_data;
    const AVCodec *codec;
    int ret;

    codec = avcodec_find_decoder(AV_CODEC_ID_MJPEGB);
    if (!codec)
        return AVERROR_BUG;
    ctx->avctx = avcodec_alloc_context3(codec);
    if (!ctx->avctx)
        return AVERROR(ENOMEM);
    ctx->avctx->thread_count = 1;
    ctx->avctx->flags  = avctx->flags;
    ctx->avctx->flags2 = avctx->flags2;
    ctx->avctx->width  = ctx->avctx->coded_width  = avctx->width;
    ctx->avctx->height = ctx->avctx->coded_height = avctx->height;

    ret = avcodec_open2(ctx->avctx, codec, NULL);
    if (ret < 0)
        return ret;

    ctx->pkt = av_packet_alloc();
    if (!ctx->pkt)
        return AVERROR(ENOMEM);

    return 0;
}

static int media100_decode_frame(AVCodecContext *avctx,
                                 AVFrame *frame, int *got_frame,
                                 AVPacket *avpkt)
{
    Media100Context *ctx = avctx->priv_data;
    unsigned second_field_offset = 0;
    unsigned next_field = 0;
    unsigned dht_offset[2];
    unsigned dqt_offset[2];
    unsigned sod_offset[2];
    unsigned sof_offset[2];
    unsigned sos_offset[2];
    unsigned field = 0;
    GetByteContext gb;
    PutByteContext pb;
    AVPacket *pkt;
    int ret;

    if (avpkt->size + 1024 > ctx->pkt->size) {
        ret = av_grow_packet(ctx->pkt, avpkt->size + 1024 - ctx->pkt->size);
        if (ret < 0)
            return ret;
    }

    ret = av_packet_make_writable(ctx->pkt);
    if (ret < 0)
        return ret;

    bytestream2_init(&gb, avpkt->data, avpkt->size);
    bytestream2_init_writer(&pb, ctx->pkt->data, ctx->pkt->size);

second_field:
    bytestream2_put_be32(&pb, 0);
    bytestream2_put_be32(&pb, AV_RB32("mjpg"));
    bytestream2_put_be32(&pb, 0);
    bytestream2_put_be32(&pb, 0);
    for (int i = 0; i < 6; i++)
        bytestream2_put_be32(&pb, 0);

    sof_offset[field] = bytestream2_tell_p(&pb);
    bytestream2_put_be16(&pb, 17);
    bytestream2_put_byte(&pb, 8);
    bytestream2_put_be16(&pb, avctx->height / 2);
    bytestream2_put_be16(&pb, avctx->width);
    bytestream2_put_byte(&pb, 3);
    bytestream2_put_byte(&pb, 1);
    bytestream2_put_byte(&pb, 0x21);
    bytestream2_put_byte(&pb, 0);
    bytestream2_put_byte(&pb, 2);
    bytestream2_put_byte(&pb, 0x11);
    bytestream2_put_byte(&pb, 1);
    bytestream2_put_byte(&pb, 3);
    bytestream2_put_byte(&pb, 0x11);
    bytestream2_put_byte(&pb, 1);

    sos_offset[field] = bytestream2_tell_p(&pb);
    bytestream2_put_be16(&pb, 12);
    bytestream2_put_byte(&pb, 3);
    bytestream2_put_byte(&pb, 1);
    bytestream2_put_byte(&pb, 0);
    bytestream2_put_byte(&pb, 2);
    bytestream2_put_byte(&pb, 0x11);
    bytestream2_put_byte(&pb, 3);
    bytestream2_put_byte(&pb, 0x11);
    bytestream2_put_byte(&pb, 0);
    bytestream2_put_byte(&pb, 0);
    bytestream2_put_byte(&pb, 0);

    dqt_offset[field] = bytestream2_tell_p(&pb);
    bytestream2_put_be16(&pb, 132);
    bytestream2_put_byte(&pb, 0);
    bytestream2_skip(&gb, 4);
    for (int i = 0; i < 64; i++)
        bytestream2_put_byte(&pb, bytestream2_get_be32(&gb));
    bytestream2_put_byte(&pb, 1);
    for (int i = 0; i < 64; i++)
        bytestream2_put_byte(&pb, bytestream2_get_be32(&gb));

    dht_offset[field] = 0;
    sod_offset[field] = bytestream2_tell_p(&pb);

    for (int i = bytestream2_tell(&gb) + 8; next_field == 0 && i < avpkt->size - 4; i++) {
        if (AV_RB32(avpkt->data + i) == 0x00000001) {
            next_field = i;
            break;
        }
    }

    bytestream2_skip(&gb, 8);
    bytestream2_copy_buffer(&pb, &gb, next_field - bytestream2_tell(&gb));
    bytestream2_put_be64(&pb, 0);

    if (field == 0) {
        field = 1;
        second_field_offset = bytestream2_tell_p(&pb);
        next_field = avpkt->size;
        goto second_field;
    }

    pkt = ctx->pkt;

    AV_WB32(pkt->data +  8, second_field_offset);
    AV_WB32(pkt->data + 12, second_field_offset);
    AV_WB32(pkt->data + 16, second_field_offset);
    AV_WB32(pkt->data + 20, dqt_offset[0]);
    AV_WB32(pkt->data + 24, dht_offset[0]);
    AV_WB32(pkt->data + 28, sof_offset[0]);
    AV_WB32(pkt->data + 32, sos_offset[0]);
    AV_WB32(pkt->data + 36, sod_offset[0]);

    AV_WB32(pkt->data + second_field_offset +  8, bytestream2_tell_p(&pb) - second_field_offset);
    AV_WB32(pkt->data + second_field_offset + 12, bytestream2_tell_p(&pb) - second_field_offset);
    AV_WB32(pkt->data + second_field_offset + 16, 0);
    AV_WB32(pkt->data + second_field_offset + 20, dqt_offset[1] - second_field_offset);
    AV_WB32(pkt->data + second_field_offset + 24, dht_offset[1]);
    AV_WB32(pkt->data + second_field_offset + 28, sof_offset[1] - second_field_offset);
    AV_WB32(pkt->data + second_field_offset + 32, sos_offset[1] - second_field_offset);
    AV_WB32(pkt->data + second_field_offset + 36, sod_offset[1] - second_field_offset);

    pkt->size = bytestream2_tell_p(&pb);

    ret = avcodec_send_packet(ctx->avctx, pkt);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Error submitting a packet for decoding\n");
        return ret;
    }

    ret = avcodec_receive_frame(ctx->avctx, frame);
    if (ret < 0)
        return ret;

    *got_frame = 1;

    return avpkt->size;
}

static av_cold int media100_decode_end(AVCodecContext *avctx)
{
    Media100Context *ctx = avctx->priv_data;

    avcodec_free_context(&ctx->avctx);
    av_packet_free(&ctx->pkt);

    return 0;
}

const FFCodec ff_media100_decoder = {
    .p.name           = "media100",
    CODEC_LONG_NAME("Media 100"),
    .p.type           = AVMEDIA_TYPE_VIDEO,
    .p.id             = AV_CODEC_ID_MEDIA100,
    .priv_data_size   = sizeof(Media100Context),
    .init             = media100_decode_init,
    .close            = media100_decode_end,
    FF_CODEC_DECODE_CB(media100_decode_frame),
    .caps_internal    = FF_CODEC_CAP_INIT_CLEANUP,
};
