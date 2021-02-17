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
#include "internal.h"

typedef struct SimbiosisIMXContext {
    uint32_t pal[256];
    uint8_t history[32768];
    int pos;
} SimbiosisIMXContext;

static av_cold int imx_decode_init(AVCodecContext *avctx)
{
    avctx->pix_fmt = AV_PIX_FMT_PAL8;
    avctx->width   = 320;
    avctx->height  = 160;
    return 0;
}

static int imx_decode_frame(AVCodecContext *avctx, void *data,
                            int *got_frame, AVPacket *avpkt)
{
    SimbiosisIMXContext *imx = avctx->priv_data;
    int ret, x, y, pal_size;
    const uint8_t *pal = av_packet_get_side_data(avpkt, AV_PKT_DATA_PALETTE, &pal_size);
    AVFrame *frame = data;
    GetByteContext gb;

    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;

    if (pal && pal_size == AVPALETTE_SIZE) {
        memcpy(imx->pal, pal, pal_size);
        frame->palette_has_changed = 1;
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

    *got_frame = 1;

    return avpkt->size;
}

AVCodec ff_simbiosis_imx_decoder = {
    .name           = "simbiosis_imx",
    .long_name      = NULL_IF_CONFIG_SMALL("Simbiosis Interactive IMX Video"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_SIMBIOSIS_IMX,
    .priv_data_size = sizeof(SimbiosisIMXContext),
    .init           = imx_decode_init,
    .decode         = imx_decode_frame,
    .capabilities   = AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE,
};
