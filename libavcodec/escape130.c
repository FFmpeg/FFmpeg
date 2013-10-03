/*
 * Escape 130 video decoder
 * Copyright (C) 2008 Eli Friedman (eli.friedman <at> gmail.com)
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

#include "libavutil/attributes.h"
#include "libavutil/mem.h"
#include "avcodec.h"
#define BITSTREAM_READER_LE
#include "get_bits.h"
#include "internal.h"

typedef struct Escape130Context {
    uint8_t *old_y_avg;

    uint8_t *new_y, *old_y;
    uint8_t *new_u, *old_u;
    uint8_t *new_v, *old_v;

    uint8_t *buf1, *buf2;
    int     linesize[3];
} Escape130Context;

static const uint8_t offset_table[] = { 2, 4, 10, 20 };
static const int8_t sign_table[64][4] = {
    {  0,  0,  0,  0 },
    { -1,  1,  0,  0 },
    {  1, -1,  0,  0 },
    { -1,  0,  1,  0 },
    { -1,  1,  1,  0 },
    {  0, -1,  1,  0 },
    {  1, -1,  1,  0 },
    { -1, -1,  1,  0 },
    {  1,  0, -1,  0 },
    {  0,  1, -1,  0 },
    {  1,  1, -1,  0 },
    { -1,  1, -1,  0 },
    {  1, -1, -1,  0 },
    { -1,  0,  0,  1 },
    { -1,  1,  0,  1 },
    {  0, -1,  0,  1 },

    {  0,  0,  0,  0 },
    {  1, -1,  0,  1 },
    { -1, -1,  0,  1 },
    { -1,  0,  1,  1 },
    { -1,  1,  1,  1 },
    {  0, -1,  1,  1 },
    {  1, -1,  1,  1 },
    { -1, -1,  1,  1 },
    {  0,  0, -1,  1 },
    {  1,  0, -1,  1 },
    { -1,  0, -1,  1 },
    {  0,  1, -1,  1 },
    {  1,  1, -1,  1 },
    { -1,  1, -1,  1 },
    {  0, -1, -1,  1 },
    {  1, -1, -1,  1 },

    {  0,  0,  0,  0 },
    { -1, -1, -1,  1 },
    {  1,  0,  0, -1 },
    {  0,  1,  0, -1 },
    {  1,  1,  0, -1 },
    { -1,  1,  0, -1 },
    {  1, -1,  0, -1 },
    {  0,  0,  1, -1 },
    {  1,  0,  1, -1 },
    { -1,  0,  1, -1 },
    {  0,  1,  1, -1 },
    {  1,  1,  1, -1 },
    { -1,  1,  1, -1 },
    {  0, -1,  1, -1 },
    {  1, -1,  1, -1 },
    { -1, -1,  1, -1 },

    {  0,  0,  0,  0 },
    {  1,  0, -1, -1 },
    {  0,  1, -1, -1 },
    {  1,  1, -1, -1 },
    { -1,  1, -1, -1 },
    {  1, -1, -1, -1 }
};

static const int8_t luma_adjust[] = { -4, -3, -2, -1, 1, 2, 3, 4 };

static const int8_t chroma_adjust[2][8] = {
    { 1, 1, 0, -1, -1, -1,  0,  1 },
    { 0, 1, 1,  1,  0, -1, -1, -1 }
};

const uint8_t chroma_vals[] = {
     20,  28,  36,  44,  52,  60,  68,  76,
     84,  92, 100, 106, 112, 116, 120, 124,
    128, 132, 136, 140, 144, 150, 156, 164,
    172, 180, 188, 196, 204, 212, 220, 228
};

static av_cold int escape130_decode_init(AVCodecContext *avctx)
{
    Escape130Context *s = avctx->priv_data;
    avctx->pix_fmt = AV_PIX_FMT_YUV420P;

    if ((avctx->width & 1) || (avctx->height & 1)) {
        av_log(avctx, AV_LOG_ERROR,
               "Dimensions should be a multiple of two.\n");
        return AVERROR_INVALIDDATA;
    }

    s->old_y_avg = av_malloc(avctx->width * avctx->height / 4);
    s->buf1      = av_malloc(avctx->width * avctx->height * 3 / 2);
    s->buf2      = av_malloc(avctx->width * avctx->height * 3 / 2);
    if (!s->old_y_avg || !s->buf1 || !s->buf2) {
        av_freep(&s->old_y_avg);
        av_freep(&s->buf1);
        av_freep(&s->buf2);
        av_log(avctx, AV_LOG_ERROR, "Could not allocate buffer.\n");
        return AVERROR(ENOMEM);
    }

    s->linesize[0] = avctx->width;
    s->linesize[1] =
    s->linesize[2] = avctx->width / 2;

    s->new_y = s->buf1;
    s->new_u = s->new_y + avctx->width * avctx->height;
    s->new_v = s->new_u + avctx->width * avctx->height / 4;
    s->old_y = s->buf2;
    s->old_u = s->old_y + avctx->width * avctx->height;
    s->old_v = s->old_u + avctx->width * avctx->height / 4;
    memset(s->old_y, 0,    avctx->width * avctx->height);
    memset(s->old_u, 0x10, avctx->width * avctx->height / 4);
    memset(s->old_v, 0x10, avctx->width * avctx->height / 4);

    return 0;
}

static av_cold int escape130_decode_close(AVCodecContext *avctx)
{
    Escape130Context *s = avctx->priv_data;

    av_freep(&s->old_y_avg);
    av_freep(&s->buf1);
    av_freep(&s->buf2);

    return 0;
}

static int decode_skip_count(GetBitContext* gb)
{
    int value;

    value = get_bits1(gb);
    if (value)
        return 0;

    value = get_bits(gb, 3);
    if (value)
        return value;

    value = get_bits(gb, 8);
    if (value)
        return value + 7;

    value = get_bits(gb, 15);
    if (value)
        return value + 262;

    return -1;
}

static int escape130_decode_frame(AVCodecContext *avctx, void *data,
                                  int *got_frame, AVPacket *avpkt)
{
    const uint8_t *buf  = avpkt->data;
    int buf_size        = avpkt->size;
    Escape130Context *s = avctx->priv_data;
    AVFrame *pic        = data;
    GetBitContext gb;
    int ret;

    uint8_t *old_y, *old_cb, *old_cr,
            *new_y, *new_cb, *new_cr;
    uint8_t *dstY, *dstU, *dstV;
    unsigned old_y_stride, old_cb_stride, old_cr_stride,
             new_y_stride, new_cb_stride, new_cr_stride;
    unsigned total_blocks = avctx->width * avctx->height / 4,
             block_index, block_x = 0;
    unsigned y[4] = { 0 }, cb = 0x10, cr = 0x10;
    int skip = -1, y_avg = 0, i, j;
    uint8_t *ya = s->old_y_avg;

    // first 16 bytes are header; no useful information in here
    if (buf_size <= 16) {
        av_log(avctx, AV_LOG_ERROR, "Insufficient frame data\n");
        return AVERROR_INVALIDDATA;
    }

    if ((ret = ff_get_buffer(avctx, pic, 0)) < 0)
        return ret;

    init_get_bits(&gb, buf + 16, (buf_size - 16) * 8);

    new_y  = s->new_y;
    new_cb = s->new_u;
    new_cr = s->new_v;
    new_y_stride  = s->linesize[0];
    new_cb_stride = s->linesize[1];
    new_cr_stride = s->linesize[2];
    old_y  = s->old_y;
    old_cb = s->old_u;
    old_cr = s->old_v;
    old_y_stride  = s->linesize[0];
    old_cb_stride = s->linesize[1];
    old_cr_stride = s->linesize[2];

    for (block_index = 0; block_index < total_blocks; block_index++) {
        // Note that this call will make us skip the rest of the blocks
        // if the frame ends prematurely.
        if (skip == -1)
            skip = decode_skip_count(&gb);
        if (skip == -1) {
            av_log(avctx, AV_LOG_ERROR, "Error decoding skip value\n");
            return AVERROR_INVALIDDATA;
        }

        if (skip) {
            y[0] = old_y[0];
            y[1] = old_y[1];
            y[2] = old_y[old_y_stride];
            y[3] = old_y[old_y_stride + 1];
            y_avg = ya[0];
            cb = old_cb[0];
            cr = old_cr[0];
        } else {
            if (get_bits1(&gb)) {
                unsigned sign_selector       = get_bits(&gb, 6);
                unsigned difference_selector = get_bits(&gb, 2);
                y_avg = 2 * get_bits(&gb, 5);
                for (i = 0; i < 4; i++) {
                    y[i] = av_clip(y_avg + offset_table[difference_selector] *
                                   sign_table[sign_selector][i], 0, 63);
                }
            } else if (get_bits1(&gb)) {
                if (get_bits1(&gb)) {
                    y_avg = get_bits(&gb, 6);
                } else {
                    unsigned adjust_index = get_bits(&gb, 3);
                    y_avg = (y_avg + luma_adjust[adjust_index]) & 63;
                }
                for (i = 0; i < 4; i++)
                    y[i] = y_avg;
            }

            if (get_bits1(&gb)) {
                if (get_bits1(&gb)) {
                    cb = get_bits(&gb, 5);
                    cr = get_bits(&gb, 5);
                } else {
                    unsigned adjust_index = get_bits(&gb, 3);
                    cb = (cb + chroma_adjust[0][adjust_index]) & 31;
                    cr = (cr + chroma_adjust[1][adjust_index]) & 31;
                }
            }
        }
        *ya++ = y_avg;

        new_y[0]                = y[0];
        new_y[1]                = y[1];
        new_y[new_y_stride]     = y[2];
        new_y[new_y_stride + 1] = y[3];
        *new_cb = cb;
        *new_cr = cr;

        old_y += 2;
        old_cb++;
        old_cr++;
        new_y += 2;
        new_cb++;
        new_cr++;
        block_x++;
        if (block_x * 2 == avctx->width) {
            block_x = 0;
            old_y  += old_y_stride * 2  - avctx->width;
            old_cb += old_cb_stride     - avctx->width / 2;
            old_cr += old_cr_stride     - avctx->width / 2;
            new_y  += new_y_stride * 2  - avctx->width;
            new_cb += new_cb_stride     - avctx->width / 2;
            new_cr += new_cr_stride     - avctx->width / 2;
        }

        skip--;
    }

    new_y  = s->new_y;
    new_cb = s->new_u;
    new_cr = s->new_v;
    dstY   = pic->data[0];
    dstU   = pic->data[1];
    dstV   = pic->data[2];
    for (j = 0; j < avctx->height; j++) {
        for (i = 0; i < avctx->width; i++)
            dstY[i] = new_y[i] << 2;
        dstY  += pic->linesize[0];
        new_y += new_y_stride;
    }
    for (j = 0; j < avctx->height / 2; j++) {
        for (i = 0; i < avctx->width / 2; i++) {
            dstU[i] = chroma_vals[new_cb[i]];
            dstV[i] = chroma_vals[new_cr[i]];
        }
        dstU   += pic->linesize[1];
        dstV   += pic->linesize[2];
        new_cb += new_cb_stride;
        new_cr += new_cr_stride;
    }

    av_dlog(avctx, "Frame data: provided %d bytes, used %d bytes\n",
            buf_size, get_bits_count(&gb) >> 3);

    FFSWAP(uint8_t*, s->old_y, s->new_y);
    FFSWAP(uint8_t*, s->old_u, s->new_u);
    FFSWAP(uint8_t*, s->old_v, s->new_v);

    *got_frame = 1;

    return buf_size;
}

AVCodec ff_escape130_decoder = {
    .name           = "escape130",
    .long_name      = NULL_IF_CONFIG_SMALL("Escape 130"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_ESCAPE130,
    .priv_data_size = sizeof(Escape130Context),
    .init           = escape130_decode_init,
    .close          = escape130_decode_close,
    .decode         = escape130_decode_frame,
    .capabilities   = CODEC_CAP_DR1,
};
