/*
 * 8088flex TMV video decoder
 * Copyright (c) 2009 Daniel Verkamp <daniel at drv.nu>
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
 * 8088flex TMV video decoder
 * @file libavcodec/tmv.c
 * @author Daniel Verkamp
 * @sa http://www.oldskool.org/pc/8088_Corruption
 */

#include "avcodec.h"

#include "cga_data.h"

typedef struct TMVContext {
    AVFrame pic;
} TMVContext;

static int tmv_decode_frame(AVCodecContext *avctx, void *data,
                            int *data_size, AVPacket *avpkt)
{
    TMVContext *tmv    = avctx->priv_data;
    const uint8_t *src = avpkt->data;
    uint8_t *dst, *dst_char;
    unsigned char_cols = avctx->width >> 3;
    unsigned char_rows = avctx->height >> 3;
    unsigned x, y, mask, char_y, fg, bg, c;

    if (tmv->pic.data[0])
        avctx->release_buffer(avctx, &tmv->pic);

    if (avctx->get_buffer(avctx, &tmv->pic) < 0) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return -1;
    }

    if (avpkt->size < 2*char_rows*char_cols) {
        av_log(avctx, AV_LOG_ERROR,
               "Input buffer too small, truncated sample?\n");
        *data_size = 0;
        return -1;
    }

    tmv->pic.pict_type = FF_I_TYPE;
    tmv->pic.key_frame = 1;
    dst                = tmv->pic.data[0];

    tmv->pic.palette_has_changed = 1;
    memcpy(tmv->pic.data[1], ff_cga_palette, 16 * 4);

    for (y = 0; y < char_rows; y++) {
        for (x = 0; x < char_cols; x++) {
            c  = *src++ * 8;
            bg = *src  >> 4;
            fg = *src++ & 0xF;

            dst_char = dst + x * 8;
            for (char_y = 0; char_y < 8; char_y++) {
                for (mask = 0x80; mask; mask >>= 1) {
                    *dst_char++ = ff_cga_font[c + char_y] & mask ? fg : bg;
                }
                dst_char += tmv->pic.linesize[0] - 8;
            }
        }
        dst += tmv->pic.linesize[0] * 8;
    }

    *data_size = sizeof(AVFrame);
    *(AVFrame *)data = tmv->pic;
    return avpkt->size;
}

static av_cold int tmv_decode_close(AVCodecContext *avctx)
{
    TMVContext *tmv = avctx->priv_data;

    if (tmv->pic.data[0])
        avctx->release_buffer(avctx, &tmv->pic);

    return 0;
}

AVCodec tmv_decoder = {
    .name           = "tmv",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_TMV,
    .priv_data_size = sizeof(TMVContext),
    .close          = tmv_decode_close,
    .decode         = tmv_decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name      = NULL_IF_CONFIG_SMALL("8088flex TMV"),
};
