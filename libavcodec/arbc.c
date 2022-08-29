/*
 * Gryphon's Anim Compressor decoder
 * Copyright (c) 2019 Paul B Mahol
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

#include "libavutil/internal.h"
#include "libavutil/intreadwrite.h"

#include "avcodec.h"
#include "bytestream.h"
#include "codec_internal.h"
#include "decode.h"

typedef struct ARBCContext {
    GetByteContext gb;

    AVFrame *prev_frame;
} ARBCContext;

static int fill_tile4(AVCodecContext *avctx, int color, AVFrame *frame)
{
    ARBCContext *s = avctx->priv_data;
    GetByteContext *gb = &s->gb;
    int nb_tiles = bytestream2_get_le16(gb);
    int h = avctx->height - 1;
    int pixels_overwritten = 0;

    if ((avctx->width / 4 + 1) * (avctx->height / 4 + 1) < nb_tiles)
        return 0;

    for (int i = 0; i < nb_tiles; i++) {
        int y = bytestream2_get_byte(gb);
        int x = bytestream2_get_byte(gb);
        uint16_t mask = bytestream2_get_le16(gb);
        int start_y = y * 4, start_x = x * 4;
        int end_y = start_y + 4, end_x = start_x + 4;

        for (int j = start_y; j < end_y; j++) {
            for (int k = start_x; k < end_x; k++) {
                if (mask & 0x8000) {
                    if (j >= avctx->height || k >= avctx->width) {
                        mask = mask << 1;
                        continue;
                    }
                    AV_WB24(&frame->data[0][frame->linesize[0] * (h - j) + 3 * k], color);
                    pixels_overwritten ++;
                }
                mask = mask << 1;
            }
        }
    }
    return pixels_overwritten;
}

static int fill_tileX(AVCodecContext *avctx, int tile_width, int tile_height,
                       int color, AVFrame *frame)
{
    ARBCContext *s = avctx->priv_data;
    GetByteContext *gb = &s->gb;
    const int step_h = tile_height / 4;
    const int step_w = tile_width / 4;
    int nb_tiles = bytestream2_get_le16(gb);
    int h = avctx->height - 1;
    int pixels_overwritten = 0;

    if ((avctx->width / tile_width + 1) * (avctx->height / tile_height + 1) < nb_tiles)
        return 0;

    for (int i = 0; i < nb_tiles; i++) {
        int y = bytestream2_get_byte(gb);
        int x = bytestream2_get_byte(gb);
        uint16_t mask = bytestream2_get_le16(gb);
        int start_y = y * tile_height, start_x = x * tile_width;
        int end_y = start_y + tile_height, end_x = start_x + tile_width;

        if (start_x >= avctx->width || start_y >= avctx->height)
            continue;

        for (int j = start_y; j < end_y; j += step_h) {
            for (int k = start_x; k < end_x; k += step_w) {
                if (mask & 0x8000U) {
                    for (int m = 0; m < step_h; m++) {
                        for (int n = 0; n < step_w; n++) {
                            if (j + m >= avctx->height || k + n >= avctx->width)
                                continue;
                            AV_WB24(&frame->data[0][frame->linesize[0] * (h - (j + m)) + 3 * (k + n)], color);
                        }
                    }
                    pixels_overwritten += FFMIN(step_h, avctx->height - j) * FFMIN(step_w, avctx->width - k);
                }
                mask = mask << 1;
            }
        }
    }
    return pixels_overwritten;
}

static int decode_frame(AVCodecContext *avctx, AVFrame *frame,
                        int *got_frame, AVPacket *avpkt)
{
    ARBCContext *s = avctx->priv_data;
    int ret, nb_segments;
    int prev_pixels = avctx->width * avctx->height;

    if (avpkt->size < 10)
        return AVERROR_INVALIDDATA;

    bytestream2_init(&s->gb, avpkt->data, avpkt->size);
    bytestream2_skip(&s->gb, 8);
    nb_segments = bytestream2_get_le16(&s->gb);
    if (nb_segments == 0)
        return avpkt->size;

    if (7 * nb_segments > bytestream2_get_bytes_left(&s->gb))
        return AVERROR_INVALIDDATA;

    if ((ret = ff_get_buffer(avctx, frame, AV_GET_BUFFER_FLAG_REF)) < 0)
        return ret;

    if (s->prev_frame->data[0]) {
        ret = av_frame_copy(frame, s->prev_frame);
        if (ret < 0)
            return ret;
    }

    for (int i = 0; i < nb_segments; i++) {
        int resolution_flag;
        int fill;

        if (bytestream2_get_bytes_left(&s->gb) <= 0)
            return AVERROR_INVALIDDATA;

        fill = bytestream2_get_byte(&s->gb) << 16;
        bytestream2_skip(&s->gb, 1);
        fill |= bytestream2_get_byte(&s->gb) << 8;
        bytestream2_skip(&s->gb, 1);
        fill |= bytestream2_get_byte(&s->gb) << 0;
        bytestream2_skip(&s->gb, 1);
        resolution_flag = bytestream2_get_byte(&s->gb);

        if (resolution_flag & 0x10)
            prev_pixels -= fill_tileX(avctx, 1024, 1024, fill, frame);
        if (resolution_flag & 0x08)
            prev_pixels -= fill_tileX(avctx, 256, 256, fill, frame);
        if (resolution_flag & 0x04)
            prev_pixels -= fill_tileX(avctx, 64, 64, fill, frame);
        if (resolution_flag & 0x02)
            prev_pixels -= fill_tileX(avctx, 16, 16, fill, frame);
        if (resolution_flag & 0x01)
            prev_pixels -= fill_tile4(avctx, fill, frame);
    }

    av_frame_unref(s->prev_frame);
    if ((ret = av_frame_ref(s->prev_frame, frame)) < 0)
        return ret;

    frame->pict_type = prev_pixels <= 0 ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_P;
    frame->key_frame = prev_pixels <= 0;
    *got_frame = 1;

    return avpkt->size;
}

static av_cold int decode_init(AVCodecContext *avctx)
{
    ARBCContext *s = avctx->priv_data;

    avctx->pix_fmt = AV_PIX_FMT_RGB24;

    s->prev_frame = av_frame_alloc();
    if (!s->prev_frame)
        return AVERROR(ENOMEM);

    return 0;
}

static void decode_flush(AVCodecContext *avctx)
{
    ARBCContext *s = avctx->priv_data;

    av_frame_unref(s->prev_frame);
}

static av_cold int decode_close(AVCodecContext *avctx)
{
    ARBCContext *s = avctx->priv_data;

    av_frame_free(&s->prev_frame);

    return 0;
}

const FFCodec ff_arbc_decoder = {
    .p.name         = "arbc",
    CODEC_LONG_NAME("Gryphon's Anim Compressor"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_ARBC,
    .priv_data_size = sizeof(ARBCContext),
    .init           = decode_init,
    FF_CODEC_DECODE_CB(decode_frame),
    .flush          = decode_flush,
    .close          = decode_close,
    .p.capabilities = AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
};
