/*
 * Escape 130 Video Decoder
 * Copyright (C) 2008 Eli Friedman (eli.friedman <at> gmail.com)
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

#include "avcodec.h"

#define BITSTREAM_READER_LE
#include "get_bits.h"
#include "internal.h"


typedef struct Escape130Context {
    AVFrame frame;
    uint8_t *bases;
} Escape130Context;

/**
 * Initialize the decoder
 * @param avctx decoder context
 * @return 0 success, negative on error
 */
static av_cold int escape130_decode_init(AVCodecContext *avctx)
{
    Escape130Context *s = avctx->priv_data;
    avctx->pix_fmt = AV_PIX_FMT_YUV420P;

    if((avctx->width&1) || (avctx->height&1)){
        av_log(avctx, AV_LOG_ERROR, "Dimensions are not a multiple of the block size\n");
        return AVERROR(EINVAL);
    }

    s->bases= av_malloc(avctx->width * avctx->height /4);

    return 0;
}

static av_cold int escape130_decode_close(AVCodecContext *avctx)
{
    Escape130Context *s = avctx->priv_data;

    if (s->frame.data[0])
        avctx->release_buffer(avctx, &s->frame);

    av_freep(&s->bases);

    return 0;
}

static unsigned decode_skip_count(GetBitContext* gb) {
    unsigned value;
    // This function reads a maximum of 27 bits,
    // which is within the padding space
    if (get_bits_left(gb) < 1+3)
        return -1;

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

/**
 * Decode a single frame
 * @param avctx decoder context
 * @param data decoded frame
 * @param got_frame have decoded frame
 * @param buf input buffer
 * @param buf_size input buffer size
 * @return 0 success, -1 on error
 */
static int escape130_decode_frame(AVCodecContext *avctx,
                                  void *data, int *got_frame,
                                  AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    Escape130Context *s = avctx->priv_data;

    GetBitContext gb;
    unsigned i;

    uint8_t *old_y, *old_cb, *old_cr,
            *new_y, *new_cb, *new_cr;
    unsigned old_y_stride, old_cb_stride, old_cr_stride,
             new_y_stride, new_cb_stride, new_cr_stride;
    unsigned total_blocks = avctx->width * avctx->height / 4,
             block_index, row_index = 0;
    unsigned y[4] = {0}, cb = 16, cr = 16;
    unsigned skip = -1;
    unsigned y_base = 0;
    uint8_t *yb= s->bases;

    AVFrame new_frame = { { 0 } };

    init_get_bits(&gb, buf, buf_size * 8);

    if (get_bits_left(&gb) < 128)
        return -1;

    // Header; no useful information in here
    skip_bits_long(&gb, 128);

    new_frame.reference = 3;
    if (ff_get_buffer(avctx, &new_frame)) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return -1;
    }

    new_y = new_frame.data[0];
    new_cb = new_frame.data[1];
    new_cr = new_frame.data[2];
    new_y_stride = new_frame.linesize[0];
    new_cb_stride = new_frame.linesize[1];
    new_cr_stride = new_frame.linesize[2];
    old_y = s->frame.data[0];
    old_cb = s->frame.data[1];
    old_cr = s->frame.data[2];
    old_y_stride = s->frame.linesize[0];
    old_cb_stride = s->frame.linesize[1];
    old_cr_stride = s->frame.linesize[2];

    av_log(avctx, AV_LOG_DEBUG,
           "Strides: %i, %i\n",
           new_y_stride, new_cb_stride);

    for (block_index = 0; block_index < total_blocks; block_index++) {
        // Note that this call will make us skip the rest of the blocks
        // if the frame prematurely ends
        if (skip == -1)
            skip = decode_skip_count(&gb);

        if (skip) {
            if (old_y) {
                y[0] = old_y[0] / 4;
                y[1] = old_y[1] / 4;
                y[2] = old_y[old_y_stride] / 4;
                y[3] = old_y[old_y_stride+1] / 4;
                y_base= yb[0];
                cb = old_cb[0] / 8;
                cr = old_cr[0] / 8;
            } else {
                y_base=y[0] = y[1] = y[2] = y[3] = 0;
                cb = cr = 16;
            }
        } else {
            if (get_bits1(&gb)) {
                static const uint8_t offset_table[] = {2, 4, 10, 20};
                static const int8_t sign_table[64][4] =
                    { {0, 0, 0, 0},
                      {-1, 1, 0, 0},
                      {1, -1, 0, 0},
                      {-1, 0, 1, 0},
                      {-1, 1, 1, 0},
                      {0, -1, 1, 0},
                      {1, -1, 1, 0},
                      {-1, -1, 1, 0},
                      {1, 0, -1, 0},
                      {0, 1, -1, 0},
                      {1, 1, -1, 0},
                      {-1, 1, -1, 0},
                      {1, -1, -1, 0},
                      {-1, 0, 0, 1},
                      {-1, 1, 0, 1},
                      {0, -1, 0, 1},

                      {0, 0, 0, 0},
                      {1, -1, 0, 1},
                      {-1, -1, 0, 1},
                      {-1, 0, 1, 1},
                      {-1, 1, 1, 1},
                      {0, -1, 1, 1},
                      {1, -1, 1, 1},
                      {-1, -1, 1, 1},
                      {0, 0, -1, 1},
                      {1, 0, -1, 1},
                      {-1, 0, -1, 1},
                      {0, 1, -1, 1},
                      {1, 1, -1, 1},
                      {-1, 1, -1, 1},
                      {0, -1, -1, 1},
                      {1, -1, -1, 1},

                      {0, 0, 0, 0},
                      {-1, -1, -1, 1},
                      {1, 0, 0, -1},
                      {0, 1, 0, -1},
                      {1, 1, 0, -1},
                      {-1, 1, 0, -1},
                      {1, -1, 0, -1},
                      {0, 0, 1, -1},
                      {1, 0, 1, -1},
                      {-1, 0, 1, -1},
                      {0, 1, 1, -1},
                      {1, 1, 1, -1},
                      {-1, 1, 1, -1},
                      {0, -1, 1, -1},
                      {1, -1, 1, -1},
                      {-1, -1, 1, -1},

                      {0, 0, 0, 0},
                      {1, 0, -1, -1},
                      {0, 1, -1, -1},
                      {1, 1, -1, -1},
                      {-1, 1, -1, -1},
                      {1, -1, -1, -1} };
                unsigned sign_selector = get_bits(&gb, 6);
                unsigned difference_selector = get_bits(&gb, 2);
                y_base = 2 * get_bits(&gb, 5);
                for (i = 0; i < 4; i++) {
                    y[i] = av_clip((int)y_base + offset_table[difference_selector] *
                                            sign_table[sign_selector][i], 0, 63);
                }
            } else if (get_bits1(&gb)) {
                if (get_bits1(&gb)) {
                    y_base = get_bits(&gb, 6);
                } else {
                    unsigned adjust_index = get_bits(&gb, 3);
                    static const int8_t adjust[] = {-4, -3, -2, -1, 1, 2, 3, 4};
                    y_base = (y_base + adjust[adjust_index]) & 63;
                }
                for (i = 0; i < 4; i++)
                    y[i] = y_base;
            }

            if (get_bits1(&gb)) {
                if (get_bits1(&gb)) {
                    cb = get_bits(&gb, 5);
                    cr = get_bits(&gb, 5);
                } else {
                    unsigned adjust_index = get_bits(&gb, 3);
                    static const int8_t adjust[2][8] =
                        {  { 1, 1, 0, -1, -1, -1,  0,  1 },
                           { 0, 1, 1,  1,  0, -1, -1, -1 } };
                    cb = (cb + adjust[0][adjust_index]) & 31;
                    cr = (cr + adjust[1][adjust_index]) & 31;
                }
            }
        }
        *yb++= y_base;

        new_y[0] = y[0] * 4;
        new_y[1] = y[1] * 4;
        new_y[new_y_stride] = y[2] * 4;
        new_y[new_y_stride + 1] = y[3] * 4;
        *new_cb = cb * 8;
        *new_cr = cr * 8;

        if (old_y)
            old_y += 2, old_cb++, old_cr++;
        new_y += 2, new_cb++, new_cr++;
        row_index++;
        if (avctx->width / 2 == row_index) {
            row_index = 0;
            if (old_y) {
                old_y  += old_y_stride * 2  - avctx->width;
                old_cb += old_cb_stride - avctx->width / 2;
                old_cr += old_cr_stride - avctx->width / 2;
            }
            new_y  += new_y_stride * 2  - avctx->width;
            new_cb += new_cb_stride - avctx->width / 2;
            new_cr += new_cr_stride - avctx->width / 2;
        }

        skip--;
    }

    av_log(avctx, AV_LOG_DEBUG,
           "Escape sizes: %i, %i\n",
           buf_size, get_bits_count(&gb) / 8);

    if (s->frame.data[0])
        avctx->release_buffer(avctx, &s->frame);

    *(AVFrame*)data = s->frame = new_frame;
    *got_frame = 1;

    return buf_size;
}


AVCodec ff_escape130_decoder = {
    .name           = "escape130",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_ESCAPE130,
    .priv_data_size = sizeof(Escape130Context),
    .init           = escape130_decode_init,
    .close          = escape130_decode_close,
    .decode         = escape130_decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name      = NULL_IF_CONFIG_SMALL("Escape 130"),
};
