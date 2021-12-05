/*
 * GEM Raster image decoder
 * Copyright (c) 2021 Peter Ross (pross@xvid.org)
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
 * GEM Raster image decoder
 */

#include "avcodec.h"
#include "bytestream.h"
#include "internal.h"

static const uint32_t gem_color_palette[16]={
    0xFFFFFFFF, 0xFFFF0000, 0xFF00FF00, 0xFFFFFF00,
    0xFF0000FF, 0xFFFF00FF, 0xFF00FFFF, 0xFFAEAEAE,
    0xFF555555, 0xFFAE0000, 0xFF00AE00, 0xFFAEAE00,
    0xFF0000AE, 0xFFAE00AE, 0xFF00AEAE, 0xFF000000,
};

static const uint8_t gem_gray[256]={
    0xFF, 0x7F, 0xBF, 0x3F, 0xDF, 0x5F, 0x9F, 0x1F, 0xEF, 0x6F, 0xAF, 0x2F, 0xCF, 0x4F, 0x8F, 0x0F,
    0xF7, 0x77, 0xB7, 0x37, 0xD7, 0x57, 0x97, 0x17, 0xE7, 0x67, 0xA7, 0x27, 0xC7, 0x47, 0x87, 0x07,
    0xFB, 0x7B, 0xBB, 0x3B, 0xDB, 0x5B, 0x9B, 0x1B, 0xEB, 0x6B, 0xAB, 0x2B, 0xCB, 0x4B, 0x8B, 0x0B,
    0xF3, 0x73, 0xB3, 0x33, 0xD3, 0x53, 0x93, 0x13, 0xE3, 0x63, 0xA3, 0x23, 0xC3, 0x43, 0x83, 0x03,
    0xFD, 0x7D, 0xBD, 0x3D, 0xDD, 0x5D, 0x9D, 0x1D, 0xED, 0x6D, 0xAD, 0x2D, 0xCD, 0x4D, 0x8D, 0x0D,
    0xF5, 0x75, 0xB5, 0x35, 0xD5, 0x55, 0x95, 0x15, 0xE5, 0x65, 0xA5, 0x25, 0xC5, 0x45, 0x85, 0x05,
    0xF9, 0x79, 0xB9, 0x39, 0xD9, 0x59, 0x99, 0x19, 0xE9, 0x69, 0xA9, 0x29, 0xC9, 0x49, 0x89, 0x09,
    0xF1, 0x71, 0xB1, 0x31, 0xD1, 0x51, 0x91, 0x11, 0xE1, 0x61, 0xA1, 0x21, 0xC1, 0x41, 0x81, 0x01,
    0xFE, 0x7E, 0xBE, 0x3E, 0xDE, 0x5E, 0x9E, 0x1E, 0xEE, 0x6E, 0xAE, 0x2E, 0xCE, 0x4E, 0x8E, 0x0E,
    0xF6, 0x76, 0xB6, 0x36, 0xD6, 0x56, 0x96, 0x16, 0xE6, 0x66, 0xA6, 0x26, 0xC6, 0x46, 0x86, 0x06,
    0xFA, 0x7A, 0xBA, 0x3A, 0xDA, 0x5A, 0x9A, 0x1A, 0xEA, 0x6A, 0xAA, 0x2A, 0xCA, 0x4A, 0x8A, 0x0A,
    0xF2, 0x72, 0xB2, 0x32, 0xD2, 0x52, 0x92, 0x12, 0xE2, 0x62, 0xA2, 0x22, 0xC2, 0x42, 0x82, 0x02,
    0xFC, 0x7C, 0xBC, 0x3C, 0xDC, 0x5C, 0x9C, 0x1C, 0xEC, 0x6C, 0xAC, 0x2C, 0xCC, 0x4C, 0x8C, 0x0C,
    0xF4, 0x74, 0xB4, 0x34, 0xD4, 0x54, 0x94, 0x14, 0xE4, 0x64, 0xA4, 0x24, 0xC4, 0x44, 0x84, 0x04,
    0xF8, 0x78, 0xB8, 0x38, 0xD8, 0x58, 0x98, 0x18, 0xE8, 0x68, 0xA8, 0x28, 0xC8, 0x48, 0x88, 0x08,
    0xF0, 0x70, 0xB0, 0x30, 0xD0, 0x50, 0x90, 0x10, 0xE0, 0x60, 0xA0, 0x20, 0xC0, 0x40, 0x80, 0x00,
};

typedef struct {
    int y, pl, x, vdup;
} State;

static void put_lines_bits(AVCodecContext *avctx, int planes, int row_width, int pixel_size, State * state, uint8_t * row, AVFrame *p)
{
    int pl_byte = state->pl / 8;
    int pl_bit  = state->pl & 7;
    for (int y = 0; y < state->vdup && (state->y + y) < avctx->height; y++)
        for (int x = 0; x < row_width; x++)
            for (int i = 7; i >= 0 && x * 8 + 7 - i < avctx->width; i--)
                p->data[0][ (state->y + y) * p->linesize[0] + (x * 8 + 7 - i) * pixel_size + pl_byte] |= !!(row[x] & (1 << i)) << pl_bit;

    state->pl++;
    if (state->pl >= planes) {
        state->pl = 0;
        state->y += state->vdup;
        state->vdup = 1;
    }
}

static void put_lines_bytes(AVCodecContext *avctx, int planes, int row_width, int pixel_size, State * state, uint8_t * row, AVFrame *p)
{
    for (int y = 0; y < state->vdup && (state->y + y) < avctx->height; y++)
        memcpy(p->data[0] + (state->y + y) * p->linesize[0], row, avctx->width * pixel_size);

    state->y += state->vdup;
    state->vdup = 1;
}

static int gem_decode_frame(AVCodecContext *avctx,
                            void *data, int *got_frame,
                            AVPacket *avpkt)
{
    const uint8_t *buf     = avpkt->data;
    int buf_size           = avpkt->size;
    const uint8_t *buf_end = buf + buf_size;
    AVFrame *p             = data;
    int header_size, planes, pattern_size, tag = 0, count_scalar = 1, ret;
    unsigned int x, count, v;
    GetByteContext gb;
    uint32_t  *palette;
    const uint8_t * b;
    uint8_t * row;
    int row_width, pixel_size;
    State state = {.y = 0, .pl = 0, .x = 0, .vdup = 1};
    void (*put_lines)(AVCodecContext *avctx, int planes, int row_width, int pixel_size, State * state, uint8_t * row, AVFrame *p);
    int width, height;

    if (buf_size <= 16)
        return AVERROR_INVALIDDATA;

    bytestream2_init(&gb, buf + 2, buf_size - 2);
    header_size = bytestream2_get_be16(&gb);
    if (header_size < 8 || buf_size <= header_size * 2)
        return AVERROR_INVALIDDATA;

    planes = bytestream2_get_be16(&gb);
    pattern_size = bytestream2_get_be16(&gb);
    avctx->sample_aspect_ratio.num = bytestream2_get_be16(&gb);
    avctx->sample_aspect_ratio.den = bytestream2_get_be16(&gb);
    width  = bytestream2_get_be16(&gb);
    height = bytestream2_get_be16(&gb);
    ret = ff_set_dimensions(avctx, width, height);
    if (ret < 0)
        return ret;

    row_width = (avctx->width + 7) / 8;
    put_lines = put_lines_bits;

    if (header_size == 9) {
        count_scalar = bytestream2_get_be16(&gb);
        if (count_scalar != 3) {
            avpriv_request_sample(avctx, "count_scalar=%d", count_scalar);
            return AVERROR_PATCHWELCOME;
        }
        planes = 24;
        avctx->pix_fmt = AV_PIX_FMT_BGR24;
        pixel_size = 3;
    } else if (planes == 15) {
#if HAVE_BIGENDIAN
        avctx->pix_fmt = AV_PIX_FMT_BGR555BE;
#else
        avctx->pix_fmt = AV_PIX_FMT_BGR555LE;
#endif
        pixel_size = 2;
    } else if (planes == 16) {
        avctx->pix_fmt = AV_PIX_FMT_RGB565BE;
        pixel_size = 2;
    } else if (planes == 24) {
        avctx->pix_fmt = AV_PIX_FMT_RGB24;
        pixel_size = 3;
    } else if (planes == 32) {
        avctx->pix_fmt = AV_PIX_FMT_0RGB;
        pixel_size = 4;
    } else {
        avctx->pix_fmt = AV_PIX_FMT_PAL8;
        pixel_size = 1;
    }

    if (header_size >= 11)
        tag = bytestream2_peek_be32(&gb);

    if (tag == AV_RB32("STTT")) {
        if (planes != 4) {
            avpriv_request_sample(avctx, "STTT planes=%d", planes);
            return AVERROR_PATCHWELCOME;
        }
    } else if (tag == AV_RB32("TIMG")) {
        if (planes != 15) {
            avpriv_request_sample(avctx, "TIMG planes=%d", planes);
            return AVERROR_PATCHWELCOME;
        }
    } else if (tag == AV_RB32("XIMG")) {
        if (planes != 1 && planes != 2 && planes != 4 && planes != 8 && planes != 16 && planes != 24 && planes != 32) {
            avpriv_request_sample(avctx, "XIMG planes=%d", planes);
            return AVERROR_PATCHWELCOME;
        }
    } else if (planes != 1 && planes != 2 && planes != 3 && planes != 4 && planes != 8 && planes != 16 && planes != 24) {
        avpriv_request_sample(avctx, "planes=%d", planes);
        return AVERROR_PATCHWELCOME;
    }

    if ((ret = ff_get_buffer(avctx, p, 0)) < 0)
        return ret;

    p->pict_type = AV_PICTURE_TYPE_I;
    p->key_frame = 1;
    p->palette_has_changed = 1;
    palette = (uint32_t  *)p->data[1];

    if (tag == AV_RB32("STTT")) {
        bytestream2_skip(&gb, 6);
        if (planes == 4) {
            for (int i = 0; i < (1 << planes); i++) {
                int v = bytestream2_get_be16(&gb);
                int r = ((v >> 8) & 0x7) << 5;
                int g = ((v >> 4) & 0x7) << 5;
                int b = ((v     ) & 0x7) << 5;
                palette[i] = 0xFF000000 | r << 16 | g << 8 | b;
            }
        } else {
            av_assert0(0);
        }
    } else if (tag == AV_RB32("TIMG")) {
        bytestream2_skip(&gb, 4);
        if (planes != 15) {
            av_assert0(0);
        }
    } else if (tag == AV_RB32("XIMG")) {
        bytestream2_skip(&gb, 6);
        if (planes == 1 || planes == 2 || planes == 4 || planes == 8) {
            for (int i = 0; i < (1 << planes); i++) {
                int r = (bytestream2_get_be16(&gb) * 51 + 100) / 200;
                int g = (bytestream2_get_be16(&gb) * 51 + 100) / 200;
                int b = (bytestream2_get_be16(&gb) * 51 + 100) / 200;
                palette[i] = 0xFF000000 | r << 16 | g << 8 | b;
            }
        } else if (planes == 16) {
            planes = 1;
            row_width = ((avctx->width + 7)/8)*8 * pixel_size;
            put_lines = put_lines_bytes;
        } else if (planes == 24) {
            planes = 1;
            row_width = ((avctx->width + 15)/16)*16 * pixel_size;
            put_lines = put_lines_bytes;
        } else if (planes == 32) {
            planes = 1;
            row_width = avctx->width * pixel_size;
            put_lines = put_lines_bytes;
        } else {
            av_assert0(0);
        }
    } else if (planes == 1) {
        palette[0] = 0xFFFFFFFF;
        palette[1] = 0xFF000000;
    } else if (planes == 2 || planes == 3 || planes == 4) {
        if (header_size == 9 + (1 << planes)) {
            bytestream2_skip(&gb, 2);
            for (int i = 0; i < (1 << planes); i++) {
                int v = bytestream2_get_be16(&gb);
                int r = ((v >> 8) & 0x7) << 5;
                int g = ((v >> 4) & 0x7) << 5;
                int b = ((v     ) & 0x7) << 5;
                palette[i] = 0xFF000000 | r << 16 | g << 8 | b;
            }
        } else
            memcpy(palette, gem_color_palette, sizeof(gem_color_palette));
    } else if (planes == 8) {
        for (int i = 0; i < 256; i++)
            palette[i] = 0xFF000000 | (gem_gray[i]<<16) | (gem_gray[i]<<8) | gem_gray[i];
    } else if (planes == 16) {
        planes = 1;
        row_width = avctx->width * pixel_size;
        put_lines = put_lines_bytes;
    } else if (planes == 24) {
        planes = 1;
        row_width = avctx->width * pixel_size;
        put_lines = put_lines_bytes;
    } else
        av_assert0(0);

    ret = av_reallocp_array(&avctx->priv_data, planes, row_width);
    if (ret < 0)
        return ret;
    row = avctx->priv_data;

    memset(p->data[0], 0, avctx->height * p->linesize[0]);
    b = buf + header_size * 2;
    x = 0;

#define SKIP \
do { \
    x++; \
    if (x >= row_width) { \
        put_lines(avctx, planes, row_width, pixel_size, &state, row + state.pl * row_width, p); \
        if (state.y >= avctx->height) goto abort; \
        x = 0; \
    } \
} while(0)

#define PUT(v) \
do { \
    row[state.pl * row_width + x++] = v; \
    if (x >= row_width) { \
        put_lines(avctx, planes, row_width, pixel_size, &state, row + state.pl * row_width, p); \
        if (state.y >= avctx->height) goto abort; \
        x = 0; \
    } \
} while(0)

    while(b < buf_end) {
        int opcode = *b++;
        if (opcode == 0x80) { /* copy */
            if (b >= buf_end)
                goto abort;
            count = *b++;
            if (!count)
                count = 256;
            count *= count_scalar;
            for (int j = 0; j < count; j++) {
                if (b >= buf_end)
                    goto abort;
                PUT(*b++);
            }
        } else if (opcode) { /* run */
            count = opcode & 0x7f;
            if (!count)
                count = 256;
            count *= count_scalar;
            v = opcode & 0x80 ? 0xFF : 0x00;
            for (int i = 0; i < count; i++)
                PUT(v);
        } else {
            if (b >= buf_end)
                goto abort;
            count = *b++;
            if (count) { /* pattern */
                if (b > buf_end - pattern_size)
                    goto abort;

                count *= count_scalar;
                for (int j = 0; j < count; j++)
                    for (int k = 0; k < pattern_size; k++)
                        PUT(b[k]);

                b += pattern_size;
            } else {
                if (b >= buf_end)
                    goto abort;
                count = *b++;
                if (count == 0xFF) { /* vertical duplication */
                    if (b >= buf_end)
                        goto abort;
                    state.vdup = *b++;
                    if (!state.vdup)
                        state.vdup = 256;
                } else { /* horizontal duplication */
                    for (int i = 0; i < count + 1; i++)
                        SKIP;
                }
            }
        }
    }

abort:

    *got_frame = 1;
    return buf_size;
}

static av_cold int gem_close(AVCodecContext *avctx)
{
    av_freep(&avctx->priv_data);
    return 0;
}

const AVCodec ff_gem_decoder = {
    .name           = "gem",
    .long_name      = NULL_IF_CONFIG_SMALL("GEM Raster image"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_GEM,
    .decode         = gem_decode_frame,
    .close          = gem_close,
    .capabilities   = AV_CODEC_CAP_DR1,
};
