/*
 * Cirrus Logic AccuPak (CLJR) codec
 * Copyright (c) 2003 Alex Beregszaszi
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
 * Cirrus Logic AccuPak codec.
 */

#include "avcodec.h"
#include "libavutil/opt.h"
#include "get_bits.h"
#include "internal.h"
#include "put_bits.h"

#if CONFIG_CLJR_DECODER
static int decode_frame(AVCodecContext *avctx,
                        void *data, int *got_frame,
                        AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size       = avpkt->size;
    GetBitContext gb;
    AVFrame * const p = data;
    int x, y, ret;

    if (avctx->height <= 0 || avctx->width <= 0) {
        av_log(avctx, AV_LOG_ERROR, "Invalid width or height\n");
        return AVERROR_INVALIDDATA;
    }

    if (buf_size / avctx->height < avctx->width) {
        av_log(avctx, AV_LOG_ERROR,
               "Resolution larger than buffer size. Invalid header?\n");
        return AVERROR_INVALIDDATA;
    }

    if ((ret = ff_get_buffer(avctx, p, 0)) < 0)
        return ret;
    p->pict_type = AV_PICTURE_TYPE_I;
    p->key_frame = 1;

    init_get_bits(&gb, buf, buf_size * 8);

    for (y = 0; y < avctx->height; y++) {
        uint8_t *luma = &p->data[0][y * p->linesize[0]];
        uint8_t *cb   = &p->data[1][y * p->linesize[1]];
        uint8_t *cr   = &p->data[2][y * p->linesize[2]];
        for (x = 0; x < avctx->width; x += 4) {
            luma[3] = (get_bits(&gb, 5)*33) >> 2;
            luma[2] = (get_bits(&gb, 5)*33) >> 2;
            luma[1] = (get_bits(&gb, 5)*33) >> 2;
            luma[0] = (get_bits(&gb, 5)*33) >> 2;
            luma += 4;
            *(cb++) = get_bits(&gb, 6) << 2;
            *(cr++) = get_bits(&gb, 6) << 2;
        }
    }

    *got_frame = 1;

    return buf_size;
}

static av_cold int decode_init(AVCodecContext *avctx)
{
    avctx->pix_fmt = AV_PIX_FMT_YUV411P;
    return 0;
}

AVCodec ff_cljr_decoder = {
    .name           = "cljr",
    .long_name      = NULL_IF_CONFIG_SMALL("Cirrus Logic AccuPak"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_CLJR,
    .init           = decode_init,
    .decode         = decode_frame,
    .capabilities   = CODEC_CAP_DR1,
};
#endif

#if CONFIG_CLJR_ENCODER
typedef struct CLJRContext {
    AVClass        *avclass;
    int             dither_type;
} CLJRContext;

static av_cold int encode_init(AVCodecContext *avctx)
{
    avctx->coded_frame = av_frame_alloc();
    if (!avctx->coded_frame)
        return AVERROR(ENOMEM);

    return 0;
}

static av_cold int encode_close(AVCodecContext *avctx)
{
    av_frame_free(&avctx->coded_frame);
    return 0;
}

static int encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                        const AVFrame *p, int *got_packet)
{
    CLJRContext *a = avctx->priv_data;
    PutBitContext pb;
    int x, y, ret;
    uint32_t dither= avctx->frame_number;
    static const uint32_t ordered_dither[2][2] =
    {
        { 0x10400000, 0x104F0000 },
        { 0xCB2A0000, 0xCB250000 },
    };

    if ((ret = ff_alloc_packet2(avctx, pkt, 32*avctx->height*avctx->width/4)) < 0)
        return ret;

    avctx->coded_frame->pict_type = AV_PICTURE_TYPE_I;
    avctx->coded_frame->key_frame = 1;

    init_put_bits(&pb, pkt->data, pkt->size);

    for (y = 0; y < avctx->height; y++) {
        uint8_t *luma = &p->data[0][y * p->linesize[0]];
        uint8_t *cb   = &p->data[1][y * p->linesize[1]];
        uint8_t *cr   = &p->data[2][y * p->linesize[2]];
        for (x = 0; x < avctx->width; x += 4) {
            switch (a->dither_type) {
            case 0: dither = 0x492A0000;                       break;
            case 1: dither = dither * 1664525 + 1013904223;    break;
            case 2: dither = ordered_dither[ y&1 ][ (x>>2)&1 ];break;
            }
            put_bits(&pb, 5, (249*(luma[3] +  (dither>>29)   )) >> 11);
            put_bits(&pb, 5, (249*(luma[2] + ((dither>>26)&7))) >> 11);
            put_bits(&pb, 5, (249*(luma[1] + ((dither>>23)&7))) >> 11);
            put_bits(&pb, 5, (249*(luma[0] + ((dither>>20)&7))) >> 11);
            luma += 4;
            put_bits(&pb, 6, (253*(*(cb++) + ((dither>>18)&3))) >> 10);
            put_bits(&pb, 6, (253*(*(cr++) + ((dither>>16)&3))) >> 10);
        }
    }

    flush_put_bits(&pb);

    pkt->size   = put_bits_count(&pb) / 8;
    pkt->flags |= AV_PKT_FLAG_KEY;
    *got_packet = 1;
    return 0;
}

#define OFFSET(x) offsetof(CLJRContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "dither_type",   "Dither type",   OFFSET(dither_type),        AV_OPT_TYPE_INT, { .i64=1 }, 0, 2, VE},
    { NULL },
};

static const AVClass cljr_class = {
    .class_name = "cljr encoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_cljr_encoder = {
    .name           = "cljr",
    .long_name      = NULL_IF_CONFIG_SMALL("Cirrus Logic AccuPak"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_CLJR,
    .priv_data_size = sizeof(CLJRContext),
    .init           = encode_init,
    .encode2        = encode_frame,
    .close          = encode_close,
    .pix_fmts       = (const enum AVPixelFormat[]) { AV_PIX_FMT_YUV411P,
                                                   AV_PIX_FMT_NONE },
    .priv_class     = &cljr_class,
};
#endif
