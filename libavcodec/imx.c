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

#include "libavutil/common.h"
#include "avcodec.h"
#include "bytestream.h"
#include "codec_internal.h"
#include "decode.h"

typedef struct SimbiosisIMXContext {
    AVFrame *frame;
    uint32_t pal[256];
    uint8_t history[32768];
    int pos;
} SimbiosisIMXContext;

static av_cold int imx_decode_init(AVCodecContext *avctx)
{
    SimbiosisIMXContext *imx = avctx->priv_data;

    avctx->pix_fmt = AV_PIX_FMT_PAL8;
    avctx->width   = 320;
    avctx->height  = 160;

    imx->frame = av_frame_alloc();
    if (!imx->frame)
        return AVERROR(ENOMEM);

    return 0;
}

static int imx_decode_frame(AVCodecContext *avctx, AVFrame *rframe,
                            int *got_frame, AVPacket *avpkt)
{
    SimbiosisIMXContext *imx = avctx->priv_data;
    int ret, x, y;
    AVFrame *frame = imx->frame;
    GetByteContext gb;

    if ((ret = ff_reget_buffer(avctx, frame, 0)) < 0)
        return ret;

    if (ff_copy_palette(imx->pal, avpkt, avctx)) {
        frame->palette_has_changed = 1;
        frame->key_frame = 1;
    } else {
        frame->key_frame = 0;
        frame->palette_has_changed = 0;
    }

    bytestream2_init(&gb, avpkt->data, avpkt->size);

    memcpy(frame->data[1], imx->pal, AVPALETTE_SIZE);

    x = 0, y = 0;
    while (bytestream2_get_bytes_left(&gb) > 0 &&
           x < 320 && y < 160) {
        int b = bytestream2_get_byte(&gb);
        int len = b & 0x3f;
        int op = b >> 6;
        int fill;

        switch (op) {
        case 3:
            len = len * 64 + bytestream2_get_byte(&gb);
        case 0:
            while (len > 0) {
                x++;
                len--;
                if (x >= 320) {
                    x = 0;
                    y++;
                }
                if (y >= 160)
                    break;
            }

            frame->key_frame = 0;
            break;
        case 1:
            if (len == 0) {
                int offset = bytestream2_get_le16(&gb);

                if (offset < 0 || offset >= 32768)
                    return AVERROR_INVALIDDATA;

                len = bytestream2_get_byte(&gb);
                while (len > 0 && offset < 32768) {
                    frame->data[0][x + y * frame->linesize[0]] = imx->history[offset++];
                    x++;
                    len--;
                    if (x >= 320) {
                        x = 0;
                        y++;
                    }
                    if (y >= 160)
                        break;
                }

                frame->key_frame = 0;
            } else {
                while (len > 0) {
                    fill = bytestream2_get_byte(&gb);
                    frame->data[0][x + y * frame->linesize[0]] = fill;
                    if (imx->pos < 32768)
                        imx->history[imx->pos++] = fill;
                    x++;
                    len--;
                    if (x >= 320) {
                        x = 0;
                        y++;
                    }
                    if (y >= 160)
                        break;
                }
            }
            break;
        case 2:
            fill = bytestream2_get_byte(&gb);

            while (len > 0) {
                frame->data[0][x + y * frame->linesize[0]] = fill;
                x++;
                len--;
                if (x >= 320) {
                    x = 0;
                    y++;
                }
                if (y >= 160)
                    break;
            }
            break;
        }
    }

    frame->pict_type = frame->key_frame ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_P;

    if ((ret = av_frame_ref(rframe, frame)) < 0)
        return ret;

    *got_frame = 1;

    return avpkt->size;
}

static void imx_decode_flush(AVCodecContext *avctx)
{
    SimbiosisIMXContext *imx = avctx->priv_data;

    av_frame_unref(imx->frame);
    imx->pos = 0;
    memset(imx->pal, 0, sizeof(imx->pal));
    memset(imx->history, 0, sizeof(imx->history));
}

static int imx_decode_close(AVCodecContext *avctx)
{
    SimbiosisIMXContext *imx = avctx->priv_data;

    av_frame_free(&imx->frame);

    return 0;
}

const FFCodec ff_simbiosis_imx_decoder = {
    .p.name         = "simbiosis_imx",
    CODEC_LONG_NAME("Simbiosis Interactive IMX Video"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_SIMBIOSIS_IMX,
    .priv_data_size = sizeof(SimbiosisIMXContext),
    .init           = imx_decode_init,
    FF_CODEC_DECODE_CB(imx_decode_frame),
    .close          = imx_decode_close,
    .flush          = imx_decode_flush,
    .p.capabilities = AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
};
