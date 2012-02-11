/*
 * Cirrus Logic AccuPak (CLJR) codec
 * Copyright (c) 2003 Alex Beregszaszi
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * Cirrus Logic AccuPak codec.
 */

#include "avcodec.h"
#include "get_bits.h"
#include "internal.h"
#include "put_bits.h"

typedef struct CLJRContext {
    AVFrame         picture;
} CLJRContext;

static av_cold int common_init(AVCodecContext *avctx)
{
    CLJRContext * const a = avctx->priv_data;

    avctx->coded_frame = &a->picture;

    return 0;
}

#if CONFIG_CLJR_DECODER
static int decode_frame(AVCodecContext *avctx,
                        void *data, int *data_size,
                        AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size       = avpkt->size;
    CLJRContext * const a = avctx->priv_data;
    GetBitContext gb;
    AVFrame *picture = data;
    AVFrame * const p = &a->picture;
    int x, y;

    if (p->data[0])
        avctx->release_buffer(avctx, p);

    if (avctx->height <= 0 || avctx->width <= 0) {
        av_log(avctx, AV_LOG_ERROR, "Invalid width or height\n");
        return AVERROR_INVALIDDATA;
    }

    if (buf_size < avctx->height * avctx->width) {
        av_log(avctx, AV_LOG_ERROR,
               "Resolution larger than buffer size. Invalid header?\n");
        return AVERROR_INVALIDDATA;
    }

    p->reference = 0;
    if (avctx->get_buffer(avctx, p) < 0) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return -1;
    }
    p->pict_type = AV_PICTURE_TYPE_I;
    p->key_frame = 1;

    init_get_bits(&gb, buf, buf_size * 8);

    for (y = 0; y < avctx->height; y++) {
        uint8_t *luma = &a->picture.data[0][y * a->picture.linesize[0]];
        uint8_t *cb   = &a->picture.data[1][y * a->picture.linesize[1]];
        uint8_t *cr   = &a->picture.data[2][y * a->picture.linesize[2]];
        for (x = 0; x < avctx->width; x += 4) {
            luma[3] = get_bits(&gb, 5) << 3;
            luma[2] = get_bits(&gb, 5) << 3;
            luma[1] = get_bits(&gb, 5) << 3;
            luma[0] = get_bits(&gb, 5) << 3;
            luma += 4;
            *(cb++) = get_bits(&gb, 6) << 2;
            *(cr++) = get_bits(&gb, 6) << 2;
        }
    }

    *picture   = a->picture;
    *data_size = sizeof(AVPicture);

    return buf_size;
}

static av_cold int decode_init(AVCodecContext *avctx)
{
    avctx->pix_fmt = PIX_FMT_YUV411P;
    return common_init(avctx);
}

static av_cold int decode_end(AVCodecContext *avctx)
{
    CLJRContext *a = avctx->priv_data;

    if (a->picture.data[0])
        avctx->release_buffer(avctx, &a->picture);
    return 0;
}

AVCodec ff_cljr_decoder = {
    .name           = "cljr",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_CLJR,
    .priv_data_size = sizeof(CLJRContext),
    .init           = decode_init,
    .close          = decode_end,
    .decode         = decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name      = NULL_IF_CONFIG_SMALL("Cirrus Logic AccuPak"),
};
#endif

#if CONFIG_CLJR_ENCODER
static int encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                        const AVFrame *p, int *got_packet)
{
    PutBitContext pb;
    int x, y, ret;

    if ((ret = ff_alloc_packet(pkt, 32*avctx->height*avctx->width/4)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "Error getting output packet.\n");
        return ret;
    }

    avctx->coded_frame->pict_type = AV_PICTURE_TYPE_I;
    avctx->coded_frame->key_frame = 1;

    init_put_bits(&pb, pkt->data, pkt->size);

    for (y = 0; y < avctx->height; y++) {
        uint8_t *luma = &p->data[0][y * p->linesize[0]];
        uint8_t *cb   = &p->data[1][y * p->linesize[1]];
        uint8_t *cr   = &p->data[2][y * p->linesize[2]];
        for (x = 0; x < avctx->width; x += 4) {
            put_bits(&pb, 5, luma[3] >> 3);
            put_bits(&pb, 5, luma[2] >> 3);
            put_bits(&pb, 5, luma[1] >> 3);
            put_bits(&pb, 5, luma[0] >> 3);
            luma += 4;
            put_bits(&pb, 6, *(cb++) >> 2);
            put_bits(&pb, 6, *(cr++) >> 2);
        }
    }

    flush_put_bits(&pb);

    pkt->size   = put_bits_count(&pb) / 8;
    pkt->flags |= AV_PKT_FLAG_KEY;
    *got_packet = 1;
    return 0;
}

AVCodec ff_cljr_encoder = {
    .name           = "cljr",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_CLJR,
    .priv_data_size = sizeof(CLJRContext),
    .init           = common_init,
    .encode2        = encode_frame,
    .pix_fmts       = (const enum PixelFormat[]) { PIX_FMT_YUV411P,
                                                   PIX_FMT_NONE },
    .long_name      = NULL_IF_CONFIG_SMALL("Cirrus Logic AccuPak"),
};
#endif
