/*
 * Quicktime Graphics (SMC) Video Decoder
 * Copyright (C) 2003 The FFmpeg project
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
 * QT SMC Video Decoder by Mike Melanson (melanson@pcisys.net)
 * For more information about the SMC format, visit:
 *   http://www.pcisys.net/~melanson/codecs/
 *
 * The SMC decoder outputs PAL8 colorspace data.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libavutil/intreadwrite.h"
#include "avcodec.h"
#include "bytestream.h"
#include "codec_internal.h"
#include "decode.h"
#include "internal.h"

#define CPAIR 2
#define CQUAD 4
#define COCTET 8

#define COLORS_PER_TABLE 256

typedef struct SmcContext {

    AVCodecContext *avctx;
    AVFrame *frame;

    GetByteContext gb;

    /* SMC color tables */
    uint8_t color_pairs[COLORS_PER_TABLE * CPAIR];
    uint8_t color_quads[COLORS_PER_TABLE * CQUAD];
    uint8_t color_octets[COLORS_PER_TABLE * COCTET];

    uint32_t pal[256];
} SmcContext;

#define GET_BLOCK_COUNT() \
  (opcode & 0x10) ? (1 + bytestream2_get_byte(gb)) : 1 + (opcode & 0x0F);

#define ADVANCE_BLOCK() \
{ \
    pixel_ptr += 4; \
    if (pixel_ptr >= width) \
    { \
        pixel_ptr = 0; \
        row_ptr += stride * 4; \
    } \
    total_blocks--; \
    if (total_blocks < !!n_blocks) \
    { \
        av_log(s->avctx, AV_LOG_ERROR, "block counter just went negative (this should not happen)\n"); \
        return AVERROR_INVALIDDATA; \
    } \
}

static int smc_decode_stream(SmcContext *s)
{
    GetByteContext *gb = &s->gb;
    int width = s->avctx->width;
    int height = s->avctx->height;
    int stride = s->frame->linesize[0];
    int i;
    int chunk_size;
    int buf_size = bytestream2_size(gb);
    uint8_t opcode;
    int n_blocks;
    unsigned int color_flags;
    unsigned int color_flags_a;
    unsigned int color_flags_b;
    unsigned int flag_mask;

    uint8_t * const pixels = s->frame->data[0];

    int image_size = height * s->frame->linesize[0];
    int row_ptr = 0;
    int pixel_ptr = 0;
    int pixel_x, pixel_y;
    int row_inc = stride - 4;
    int block_ptr;
    int prev_block_ptr;
    int prev_block_ptr1, prev_block_ptr2;
    int prev_block_flag;
    int total_blocks;
    int color_table_index;  /* indexes to color pair, quad, or octet tables */
    int pixel;

    int color_pair_index = 0;
    int color_quad_index = 0;
    int color_octet_index = 0;

    /* make the palette available */
    memcpy(s->frame->data[1], s->pal, AVPALETTE_SIZE);

    bytestream2_skip(gb, 1);
    chunk_size = bytestream2_get_be24(gb);
    if (chunk_size != buf_size)
        av_log(s->avctx, AV_LOG_WARNING, "MOV chunk size != encoded chunk size (%d != %d); using MOV chunk size\n",
            chunk_size, buf_size);

    chunk_size = buf_size;
    total_blocks = ((s->avctx->width + 3) / 4) * ((s->avctx->height + 3) / 4);

    /* traverse through the blocks */
    while (total_blocks) {
        /* sanity checks */
        /* make sure the row pointer hasn't gone wild */
        if (row_ptr >= image_size) {
            av_log(s->avctx, AV_LOG_ERROR, "just went out of bounds (row ptr = %d, height = %d)\n",
                row_ptr, image_size);
            return AVERROR_INVALIDDATA;
        }
        if (bytestream2_get_bytes_left(gb) < 1) {
            av_log(s->avctx, AV_LOG_ERROR, "input too small\n");
            return AVERROR_INVALIDDATA;
        }

        opcode = bytestream2_get_byteu(gb);
        switch (opcode & 0xF0) {
        /* skip n blocks */
        case 0x00:
        case 0x10:
            n_blocks = GET_BLOCK_COUNT();
            while (n_blocks--) {
                ADVANCE_BLOCK();
            }
            break;

        /* repeat last block n times */
        case 0x20:
        case 0x30:
            n_blocks = GET_BLOCK_COUNT();

            /* sanity check */
            if ((row_ptr == 0) && (pixel_ptr == 0)) {
                av_log(s->avctx, AV_LOG_ERROR, "encountered repeat block opcode (%02X) but no blocks rendered yet\n",
                    opcode & 0xF0);
                return AVERROR_INVALIDDATA;
            }

            /* figure out where the previous block started */
            if (pixel_ptr == 0)
                prev_block_ptr1 =
                    (row_ptr - s->avctx->width * 4) + s->avctx->width - 4;
            else
                prev_block_ptr1 = row_ptr + pixel_ptr - 4;

            while (n_blocks--) {
                block_ptr = row_ptr + pixel_ptr;
                prev_block_ptr = prev_block_ptr1;
                for (pixel_y = 0; pixel_y < 4; pixel_y++) {
                    for (pixel_x = 0; pixel_x < 4; pixel_x++) {
                        pixels[block_ptr++] = pixels[prev_block_ptr++];
                    }
                    block_ptr += row_inc;
                    prev_block_ptr += row_inc;
                }
                ADVANCE_BLOCK();
            }
            break;

        /* repeat previous pair of blocks n times */
        case 0x40:
        case 0x50:
            n_blocks = GET_BLOCK_COUNT();
            n_blocks *= 2;

            /* sanity check */
            if ((row_ptr == 0) && (pixel_ptr < 2 * 4)) {
                av_log(s->avctx, AV_LOG_ERROR, "encountered repeat block opcode (%02X) but not enough blocks rendered yet\n",
                    opcode & 0xF0);
                return AVERROR_INVALIDDATA;
            }

            /* figure out where the previous 2 blocks started */
            if (pixel_ptr == 0)
                prev_block_ptr1 = (row_ptr - s->avctx->width * 4) +
                    s->avctx->width - 4 * 2;
            else if (pixel_ptr == 4)
                prev_block_ptr1 = (row_ptr - s->avctx->width * 4) + row_inc;
            else
                prev_block_ptr1 = row_ptr + pixel_ptr - 4 * 2;

            if (pixel_ptr == 0)
                prev_block_ptr2 = (row_ptr - s->avctx->width * 4) + row_inc;
            else
                prev_block_ptr2 = row_ptr + pixel_ptr - 4;

            prev_block_flag = 0;
            while (n_blocks--) {
                block_ptr = row_ptr + pixel_ptr;
                if (prev_block_flag)
                    prev_block_ptr = prev_block_ptr2;
                else
                    prev_block_ptr = prev_block_ptr1;
                prev_block_flag = !prev_block_flag;

                for (pixel_y = 0; pixel_y < 4; pixel_y++) {
                    for (pixel_x = 0; pixel_x < 4; pixel_x++) {
                        pixels[block_ptr++] = pixels[prev_block_ptr++];
                    }
                    block_ptr += row_inc;
                    prev_block_ptr += row_inc;
                }
                ADVANCE_BLOCK();
            }
            break;

        /* 1-color block encoding */
        case 0x60:
        case 0x70:
            n_blocks = GET_BLOCK_COUNT();
            pixel = bytestream2_get_byte(gb);

            while (n_blocks--) {
                block_ptr = row_ptr + pixel_ptr;
                for (pixel_y = 0; pixel_y < 4; pixel_y++) {
                    for (pixel_x = 0; pixel_x < 4; pixel_x++) {
                        pixels[block_ptr++] = pixel;
                    }
                    block_ptr += row_inc;
                }
                ADVANCE_BLOCK();
            }
            break;

        /* 2-color block encoding */
        case 0x80:
        case 0x90:
            n_blocks = (opcode & 0x0F) + 1;

            /* figure out which color pair to use to paint the 2-color block */
            if ((opcode & 0xF0) == 0x80) {
                /* fetch the next 2 colors from bytestream and store in next
                 * available entry in the color pair table */
                for (i = 0; i < CPAIR; i++) {
                    pixel = bytestream2_get_byte(gb);
                    color_table_index = CPAIR * color_pair_index + i;
                    s->color_pairs[color_table_index] = pixel;
                }
                /* this is the base index to use for this block */
                color_table_index = CPAIR * color_pair_index;
                color_pair_index++;
                /* wraparound */
                if (color_pair_index == COLORS_PER_TABLE)
                    color_pair_index = 0;
            } else
                color_table_index = CPAIR * bytestream2_get_byte(gb);

            while (n_blocks--) {
                color_flags = bytestream2_get_be16(gb);
                flag_mask = 0x8000;
                block_ptr = row_ptr + pixel_ptr;
                for (pixel_y = 0; pixel_y < 4; pixel_y++) {
                    for (pixel_x = 0; pixel_x < 4; pixel_x++) {
                        if (color_flags & flag_mask)
                            pixel = color_table_index + 1;
                        else
                            pixel = color_table_index;
                        flag_mask >>= 1;
                        pixels[block_ptr++] = s->color_pairs[pixel];
                    }
                    block_ptr += row_inc;
                }
                ADVANCE_BLOCK();
            }
            break;

        /* 4-color block encoding */
        case 0xA0:
        case 0xB0:
            n_blocks = (opcode & 0x0F) + 1;

            /* figure out which color quad to use to paint the 4-color block */
            if ((opcode & 0xF0) == 0xA0) {
                /* fetch the next 4 colors from bytestream and store in next
                 * available entry in the color quad table */
                for (i = 0; i < CQUAD; i++) {
                    pixel = bytestream2_get_byte(gb);
                    color_table_index = CQUAD * color_quad_index + i;
                    s->color_quads[color_table_index] = pixel;
                }
                /* this is the base index to use for this block */
                color_table_index = CQUAD * color_quad_index;
                color_quad_index++;
                /* wraparound */
                if (color_quad_index == COLORS_PER_TABLE)
                    color_quad_index = 0;
            } else
                color_table_index = CQUAD * bytestream2_get_byte(gb);

            while (n_blocks--) {
                color_flags = bytestream2_get_be32(gb);
                /* flag mask actually acts as a bit shift count here */
                flag_mask = 30;
                block_ptr = row_ptr + pixel_ptr;
                for (pixel_y = 0; pixel_y < 4; pixel_y++) {
                    for (pixel_x = 0; pixel_x < 4; pixel_x++) {
                        pixel = color_table_index +
                            ((color_flags >> flag_mask) & 0x03);
                        flag_mask -= 2;
                        pixels[block_ptr++] = s->color_quads[pixel];
                    }
                    block_ptr += row_inc;
                }
                ADVANCE_BLOCK();
            }
            break;

        /* 8-color block encoding */
        case 0xC0:
        case 0xD0:
            n_blocks = (opcode & 0x0F) + 1;

            /* figure out which color octet to use to paint the 8-color block */
            if ((opcode & 0xF0) == 0xC0) {
                /* fetch the next 8 colors from bytestream and store in next
                 * available entry in the color octet table */
                for (i = 0; i < COCTET; i++) {
                    pixel = bytestream2_get_byte(gb);
                    color_table_index = COCTET * color_octet_index + i;
                    s->color_octets[color_table_index] = pixel;
                }
                /* this is the base index to use for this block */
                color_table_index = COCTET * color_octet_index;
                color_octet_index++;
                /* wraparound */
                if (color_octet_index == COLORS_PER_TABLE)
                    color_octet_index = 0;
            } else
                color_table_index = COCTET * bytestream2_get_byte(gb);

            while (n_blocks--) {
                /*
                  For this input of 6 hex bytes:
                    01 23 45 67 89 AB
                  Mangle it to this output:
                    flags_a = xx012456, flags_b = xx89A37B
                */
                /* build the color flags */
                int val1 = bytestream2_get_be16(gb);
                int val2 = bytestream2_get_be16(gb);
                int val3 = bytestream2_get_be16(gb);
                color_flags_a = ((val1 & 0xFFF0) << 8) | (val2 >> 4);
                color_flags_b = ((val3 & 0xFFF0) << 8) |
                    ((val1 & 0x0F) << 8) | ((val2 & 0x0F) << 4) | (val3 & 0x0F);

                color_flags = color_flags_a;
                /* flag mask actually acts as a bit shift count here */
                flag_mask = 21;
                block_ptr = row_ptr + pixel_ptr;
                for (pixel_y = 0; pixel_y < 4; pixel_y++) {
                    /* reload flags at third row (iteration pixel_y == 2) */
                    if (pixel_y == 2) {
                        color_flags = color_flags_b;
                        flag_mask = 21;
                    }
                    for (pixel_x = 0; pixel_x < 4; pixel_x++) {
                        pixel = color_table_index +
                            ((color_flags >> flag_mask) & 0x07);
                        flag_mask -= 3;
                        pixels[block_ptr++] = s->color_octets[pixel];
                    }
                    block_ptr += row_inc;
                }
                ADVANCE_BLOCK();
            }
            break;

        /* 16-color block encoding (every pixel is a different color) */
        case 0xE0:
        case 0xF0:
            n_blocks = (opcode & 0x0F) + 1;

            while (n_blocks--) {
                block_ptr = row_ptr + pixel_ptr;
                for (pixel_y = 0; pixel_y < 4; pixel_y++) {
                    for (pixel_x = 0; pixel_x < 4; pixel_x++) {
                        pixels[block_ptr++] = bytestream2_get_byte(gb);
                    }
                    block_ptr += row_inc;
                }
                ADVANCE_BLOCK();
            }
            break;
        }
    }

    return 0;
}

static av_cold int smc_decode_init(AVCodecContext *avctx)
{
    SmcContext *s = avctx->priv_data;

    s->avctx = avctx;
    avctx->pix_fmt = AV_PIX_FMT_PAL8;

    s->frame = av_frame_alloc();
    if (!s->frame)
        return AVERROR(ENOMEM);

    return 0;
}

static int smc_decode_frame(AVCodecContext *avctx, AVFrame *rframe,
                            int *got_frame, AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    SmcContext *s = avctx->priv_data;
    int ret;
    int total_blocks = ((s->avctx->width + 3) / 4) * ((s->avctx->height + 3) / 4);

    if (total_blocks / 1024 > avpkt->size)
        return AVERROR_INVALIDDATA;

    bytestream2_init(&s->gb, buf, buf_size);

    if ((ret = ff_reget_buffer(avctx, s->frame, 0)) < 0)
        return ret;

    s->frame->palette_has_changed = ff_copy_palette(s->pal, avpkt, avctx);

    ret = smc_decode_stream(s);
    if (ret < 0)
        return ret;

    *got_frame      = 1;
    if ((ret = av_frame_ref(rframe, s->frame)) < 0)
        return ret;

    /* always report that the buffer was completely consumed */
    return buf_size;
}

static av_cold int smc_decode_end(AVCodecContext *avctx)
{
    SmcContext *s = avctx->priv_data;

    av_frame_free(&s->frame);

    return 0;
}

const FFCodec ff_smc_decoder = {
    .p.name         = "smc",
    .p.long_name    = NULL_IF_CONFIG_SMALL("QuickTime Graphics (SMC)"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_SMC,
    .priv_data_size = sizeof(SmcContext),
    .init           = smc_decode_init,
    .close          = smc_decode_end,
    FF_CODEC_DECODE_CB(smc_decode_frame),
    .p.capabilities = AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE,
};
