/*
 * Wing Commander/Xan Video Decoder
 * Copyright (c) 2003 The FFmpeg Project
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
 * Xan video decoder for Wing Commander III computer game
 * by Mario Brito (mbrito@student.dei.uc.pt)
 * and Mike Melanson (melanson@pcisys.net)
 *
 * The xan_wc3 decoder outputs PAL8 data.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"
#include "avcodec.h"
#include "bytestream.h"
#define BITSTREAM_READER_LE
#include "get_bits.h"
#include "internal.h"

#define RUNTIME_GAMMA 0

#define VGA__TAG MKTAG('V', 'G', 'A', ' ')
#define PALT_TAG MKTAG('P', 'A', 'L', 'T')
#define SHOT_TAG MKTAG('S', 'H', 'O', 'T')
#define PALETTE_COUNT 256
#define PALETTE_SIZE (PALETTE_COUNT * 3)
#define PALETTES_MAX 256

typedef struct XanContext {

    AVCodecContext *avctx;
    AVFrame *last_frame;

    const uint8_t *buf;
    int size;

    /* scratch space */
    uint8_t *buffer1;
    int buffer1_size;
    uint8_t *buffer2;
    int buffer2_size;

    unsigned *palettes;
    int palettes_count;
    int cur_palette;

    int frame_size;

} XanContext;

static av_cold int xan_decode_end(AVCodecContext *avctx)
{
    XanContext *s = avctx->priv_data;

    av_frame_free(&s->last_frame);

    av_freep(&s->buffer1);
    av_freep(&s->buffer2);
    av_freep(&s->palettes);

    return 0;
}

static av_cold int xan_decode_init(AVCodecContext *avctx)
{
    XanContext *s = avctx->priv_data;

    s->avctx = avctx;
    s->frame_size = 0;

    avctx->pix_fmt = AV_PIX_FMT_PAL8;

    s->buffer1_size = avctx->width * avctx->height;
    s->buffer1 = av_malloc(s->buffer1_size);
    if (!s->buffer1)
        return AVERROR(ENOMEM);
    s->buffer2_size = avctx->width * avctx->height;
    s->buffer2 = av_malloc(s->buffer2_size + 130);
    if (!s->buffer2) {
        av_freep(&s->buffer1);
        return AVERROR(ENOMEM);
    }

    s->last_frame = av_frame_alloc();
    if (!s->last_frame) {
        xan_decode_end(avctx);
        return AVERROR(ENOMEM);
    }

    return 0;
}

static int xan_huffman_decode(uint8_t *dest, int dest_len,
                              const uint8_t *src, int src_len)
{
    uint8_t byte = *src++;
    uint8_t ival = byte + 0x16;
    const uint8_t * ptr = src + byte*2;
    int ptr_len = src_len - 1 - byte*2;
    uint8_t val = ival;
    uint8_t *dest_end = dest + dest_len;
    uint8_t *dest_start = dest;
    int ret;
    GetBitContext gb;

    if ((ret = init_get_bits8(&gb, ptr, ptr_len)) < 0)
        return ret;

    while (val != 0x16) {
        unsigned idx = val - 0x17 + get_bits1(&gb) * byte;
        if (idx >= 2 * byte)
            return AVERROR_INVALIDDATA;
        val = src[idx];

        if (val < 0x16) {
            if (dest >= dest_end)
                return dest_len;
            *dest++ = val;
            val = ival;
        }
    }

    return dest - dest_start;
}

/**
 * unpack simple compression
 *
 * @param dest destination buffer of dest_len, must be padded with at least 130 bytes
 */
static void xan_unpack(uint8_t *dest, int dest_len,
                       const uint8_t *src, int src_len)
{
    uint8_t opcode;
    int size;
    uint8_t *dest_org = dest;
    uint8_t *dest_end = dest + dest_len;
    GetByteContext ctx;

    bytestream2_init(&ctx, src, src_len);
    while (dest < dest_end && bytestream2_get_bytes_left(&ctx)) {
        opcode = bytestream2_get_byte(&ctx);

        if (opcode < 0xe0) {
            int size2, back;
            if ((opcode & 0x80) == 0) {
                size = opcode & 3;

                back  = ((opcode & 0x60) << 3) + bytestream2_get_byte(&ctx) + 1;
                size2 = ((opcode & 0x1c) >> 2) + 3;
            } else if ((opcode & 0x40) == 0) {
                size = bytestream2_peek_byte(&ctx) >> 6;

                back  = (bytestream2_get_be16(&ctx) & 0x3fff) + 1;
                size2 = (opcode & 0x3f) + 4;
            } else {
                size = opcode & 3;

                back  = ((opcode & 0x10) << 12) + bytestream2_get_be16(&ctx) + 1;
                size2 = ((opcode & 0x0c) <<  6) + bytestream2_get_byte(&ctx) + 5;
            }

            if (dest_end - dest < size + size2 ||
                dest + size - dest_org < back ||
                bytestream2_get_bytes_left(&ctx) < size)
                return;
            bytestream2_get_buffer(&ctx, dest, size);
            dest += size;
            av_memcpy_backptr(dest, back, size2);
            dest += size2;
        } else {
            int finish = opcode >= 0xfc;
            size = finish ? opcode & 3 : ((opcode & 0x1f) << 2) + 4;

            if (dest_end - dest < size || bytestream2_get_bytes_left(&ctx) < size)
                return;
            bytestream2_get_buffer(&ctx, dest, size);
            dest += size;
            if (finish)
                return;
        }
    }
}

static inline void xan_wc3_output_pixel_run(XanContext *s, AVFrame *frame,
    const uint8_t *pixel_buffer, int x, int y, int pixel_count)
{
    int stride;
    int line_inc;
    int index;
    int current_x;
    int width = s->avctx->width;
    uint8_t *palette_plane;

    palette_plane = frame->data[0];
    stride = frame->linesize[0];
    line_inc = stride - width;
    index = y * stride + x;
    current_x = x;
    while (pixel_count && index < s->frame_size) {
        int count = FFMIN(pixel_count, width - current_x);
        memcpy(palette_plane + index, pixel_buffer, count);
        pixel_count  -= count;
        index        += count;
        pixel_buffer += count;
        current_x    += count;

        if (current_x >= width) {
            index += line_inc;
            current_x = 0;
        }
    }
}

static inline void xan_wc3_copy_pixel_run(XanContext *s, AVFrame *frame,
                                          int x, int y,
                                          int pixel_count, int motion_x,
                                          int motion_y)
{
    int stride;
    int line_inc;
    int curframe_index, prevframe_index;
    int curframe_x, prevframe_x;
    int width = s->avctx->width;
    uint8_t *palette_plane, *prev_palette_plane;

    if (y + motion_y < 0 || y + motion_y >= s->avctx->height ||
        x + motion_x < 0 || x + motion_x >= s->avctx->width)
        return;

    palette_plane = frame->data[0];
    prev_palette_plane = s->last_frame->data[0];
    if (!prev_palette_plane)
        prev_palette_plane = palette_plane;
    stride = frame->linesize[0];
    line_inc = stride - width;
    curframe_index = y * stride + x;
    curframe_x = x;
    prevframe_index = (y + motion_y) * stride + x + motion_x;
    prevframe_x = x + motion_x;

    if (prev_palette_plane == palette_plane && FFABS(curframe_index - prevframe_index) < pixel_count) {
         avpriv_request_sample(s->avctx, "Overlapping copy\n");
         return ;
    }

    while (pixel_count &&
           curframe_index  < s->frame_size &&
           prevframe_index < s->frame_size) {
        int count = FFMIN3(pixel_count, width - curframe_x,
                           width - prevframe_x);

        memcpy(palette_plane + curframe_index,
               prev_palette_plane + prevframe_index, count);
        pixel_count     -= count;
        curframe_index  += count;
        prevframe_index += count;
        curframe_x      += count;
        prevframe_x     += count;

        if (curframe_x >= width) {
            curframe_index += line_inc;
            curframe_x = 0;
        }

        if (prevframe_x >= width) {
            prevframe_index += line_inc;
            prevframe_x = 0;
        }
    }
}

static int xan_wc3_decode_frame(XanContext *s, AVFrame *frame)
{

    int width  = s->avctx->width;
    int height = s->avctx->height;
    int total_pixels = width * height;
    uint8_t opcode;
    uint8_t flag = 0;
    int size = 0;
    int motion_x, motion_y;
    int x, y, ret;

    uint8_t *opcode_buffer = s->buffer1;
    uint8_t *opcode_buffer_end = s->buffer1 + s->buffer1_size;
    int opcode_buffer_size = s->buffer1_size;
    const uint8_t *imagedata_buffer = s->buffer2;

    /* pointers to segments inside the compressed chunk */
    const uint8_t *huffman_segment;
    GetByteContext       size_segment;
    GetByteContext       vector_segment;
    const uint8_t *imagedata_segment;
    int huffman_offset, size_offset, vector_offset, imagedata_offset,
        imagedata_size;

    if (s->size < 8)
        return AVERROR_INVALIDDATA;

    huffman_offset    = AV_RL16(&s->buf[0]);
    size_offset       = AV_RL16(&s->buf[2]);
    vector_offset     = AV_RL16(&s->buf[4]);
    imagedata_offset  = AV_RL16(&s->buf[6]);

    if (huffman_offset   >= s->size ||
        size_offset      >= s->size ||
        vector_offset    >= s->size ||
        imagedata_offset >= s->size)
        return AVERROR_INVALIDDATA;

    huffman_segment   = s->buf + huffman_offset;
    bytestream2_init(&size_segment,   s->buf + size_offset,   s->size - size_offset);
    bytestream2_init(&vector_segment, s->buf + vector_offset, s->size - vector_offset);
    imagedata_segment = s->buf + imagedata_offset;

    if ((ret = xan_huffman_decode(opcode_buffer, opcode_buffer_size,
                                  huffman_segment, s->size - huffman_offset)) < 0)
        return AVERROR_INVALIDDATA;
    opcode_buffer_end = opcode_buffer + ret;

    if (imagedata_segment[0] == 2) {
        xan_unpack(s->buffer2, s->buffer2_size,
                   &imagedata_segment[1], s->size - imagedata_offset - 1);
        imagedata_size = s->buffer2_size;
    } else {
        imagedata_size = s->size - imagedata_offset - 1;
        imagedata_buffer = &imagedata_segment[1];
    }

    /* use the decoded data segments to build the frame */
    x = y = 0;
    while (total_pixels && opcode_buffer < opcode_buffer_end) {

        opcode = *opcode_buffer++;
        size = 0;

        switch (opcode) {

        case 0:
            flag ^= 1;
            continue;

        case 1:
        case 2:
        case 3:
        case 4:
        case 5:
        case 6:
        case 7:
        case 8:
            size = opcode;
            break;

        case 12:
        case 13:
        case 14:
        case 15:
        case 16:
        case 17:
        case 18:
            size += (opcode - 10);
            break;

        case 9:
        case 19:
            if (bytestream2_get_bytes_left(&size_segment) < 1) {
                av_log(s->avctx, AV_LOG_ERROR, "size_segment overread\n");
                return AVERROR_INVALIDDATA;
            }
            size = bytestream2_get_byte(&size_segment);
            break;

        case 10:
        case 20:
            if (bytestream2_get_bytes_left(&size_segment) < 2) {
                av_log(s->avctx, AV_LOG_ERROR, "size_segment overread\n");
                return AVERROR_INVALIDDATA;
            }
            size = bytestream2_get_be16(&size_segment);
            break;

        case 11:
        case 21:
            if (bytestream2_get_bytes_left(&size_segment) < 3) {
                av_log(s->avctx, AV_LOG_ERROR, "size_segment overread\n");
                return AVERROR_INVALIDDATA;
            }
            size = bytestream2_get_be24(&size_segment);
            break;
        }

        if (size > total_pixels)
            break;

        if (opcode < 12) {
            flag ^= 1;
            if (flag) {
                /* run of (size) pixels is unchanged from last frame */
                xan_wc3_copy_pixel_run(s, frame, x, y, size, 0, 0);
            } else {
                /* output a run of pixels from imagedata_buffer */
                if (imagedata_size < size)
                    break;
                xan_wc3_output_pixel_run(s, frame, imagedata_buffer, x, y, size);
                imagedata_buffer += size;
                imagedata_size -= size;
            }
        } else {
            uint8_t vector;
            if (bytestream2_get_bytes_left(&vector_segment) <= 0) {
                av_log(s->avctx, AV_LOG_ERROR, "vector_segment overread\n");
                return AVERROR_INVALIDDATA;
            }
            /* run-based motion compensation from last frame */
            vector = bytestream2_get_byte(&vector_segment);
            motion_x = sign_extend(vector >> 4,  4);
            motion_y = sign_extend(vector & 0xF, 4);

            /* copy a run of pixels from the previous frame */
            xan_wc3_copy_pixel_run(s, frame, x, y, size, motion_x, motion_y);

            flag = 0;
        }

        /* coordinate accounting */
        total_pixels -= size;
        y += (x + size) / width;
        x  = (x + size) % width;
    }
    return 0;
}

#if RUNTIME_GAMMA
static inline unsigned mul(unsigned a, unsigned b)
{
    return (a * b) >> 16;
}

static inline unsigned pow4(unsigned a)
{
    unsigned square = mul(a, a);
    return mul(square, square);
}

static inline unsigned pow5(unsigned a)
{
    return mul(pow4(a), a);
}

static uint8_t gamma_corr(uint8_t in) {
    unsigned lo, hi = 0xff40, target;
    int i = 15;
    in = (in << 2) | (in >> 6);
    /*  equivalent float code:
    if (in >= 252)
        return 253;
    return round(pow(in / 256.0, 0.8) * 256);
    */
    lo = target = in << 8;
    do {
        unsigned mid = (lo + hi) >> 1;
        unsigned pow = pow5(mid);
        if (pow > target) hi = mid;
        else lo = mid;
    } while (--i);
    return (pow4((lo + hi) >> 1) + 0x80) >> 8;
}
#else
/**
 * This is a gamma correction that xan3 applies to all palette entries.
 *
 * There is a peculiarity, namely that the values are clamped to 253 -
 * it seems likely that this table was calculated by a buggy fixed-point
 * implementation, the one above under RUNTIME_GAMMA behaves like this for
 * example.
 * The exponent value of 0.8 can be explained by this as well, since 0.8 = 4/5
 * and thus pow(x, 0.8) is still easy to calculate.
 * Also, the input values are first rotated to the left by 2.
 */
static const uint8_t gamma_lookup[256] = {
    0x00, 0x09, 0x10, 0x16, 0x1C, 0x21, 0x27, 0x2C,
    0x31, 0x35, 0x3A, 0x3F, 0x43, 0x48, 0x4C, 0x50,
    0x54, 0x59, 0x5D, 0x61, 0x65, 0x69, 0x6D, 0x71,
    0x75, 0x79, 0x7D, 0x80, 0x84, 0x88, 0x8C, 0x8F,
    0x93, 0x97, 0x9A, 0x9E, 0xA2, 0xA5, 0xA9, 0xAC,
    0xB0, 0xB3, 0xB7, 0xBA, 0xBE, 0xC1, 0xC5, 0xC8,
    0xCB, 0xCF, 0xD2, 0xD5, 0xD9, 0xDC, 0xDF, 0xE3,
    0xE6, 0xE9, 0xED, 0xF0, 0xF3, 0xF6, 0xFA, 0xFD,
    0x03, 0x0B, 0x12, 0x18, 0x1D, 0x23, 0x28, 0x2D,
    0x32, 0x36, 0x3B, 0x40, 0x44, 0x49, 0x4D, 0x51,
    0x56, 0x5A, 0x5E, 0x62, 0x66, 0x6A, 0x6E, 0x72,
    0x76, 0x7A, 0x7D, 0x81, 0x85, 0x89, 0x8D, 0x90,
    0x94, 0x98, 0x9B, 0x9F, 0xA2, 0xA6, 0xAA, 0xAD,
    0xB1, 0xB4, 0xB8, 0xBB, 0xBF, 0xC2, 0xC5, 0xC9,
    0xCC, 0xD0, 0xD3, 0xD6, 0xDA, 0xDD, 0xE0, 0xE4,
    0xE7, 0xEA, 0xED, 0xF1, 0xF4, 0xF7, 0xFA, 0xFD,
    0x05, 0x0D, 0x13, 0x19, 0x1F, 0x24, 0x29, 0x2E,
    0x33, 0x38, 0x3C, 0x41, 0x45, 0x4A, 0x4E, 0x52,
    0x57, 0x5B, 0x5F, 0x63, 0x67, 0x6B, 0x6F, 0x73,
    0x77, 0x7B, 0x7E, 0x82, 0x86, 0x8A, 0x8D, 0x91,
    0x95, 0x99, 0x9C, 0xA0, 0xA3, 0xA7, 0xAA, 0xAE,
    0xB2, 0xB5, 0xB9, 0xBC, 0xBF, 0xC3, 0xC6, 0xCA,
    0xCD, 0xD0, 0xD4, 0xD7, 0xDA, 0xDE, 0xE1, 0xE4,
    0xE8, 0xEB, 0xEE, 0xF1, 0xF5, 0xF8, 0xFB, 0xFD,
    0x07, 0x0E, 0x15, 0x1A, 0x20, 0x25, 0x2A, 0x2F,
    0x34, 0x39, 0x3D, 0x42, 0x46, 0x4B, 0x4F, 0x53,
    0x58, 0x5C, 0x60, 0x64, 0x68, 0x6C, 0x70, 0x74,
    0x78, 0x7C, 0x7F, 0x83, 0x87, 0x8B, 0x8E, 0x92,
    0x96, 0x99, 0x9D, 0xA1, 0xA4, 0xA8, 0xAB, 0xAF,
    0xB2, 0xB6, 0xB9, 0xBD, 0xC0, 0xC4, 0xC7, 0xCB,
    0xCE, 0xD1, 0xD5, 0xD8, 0xDB, 0xDF, 0xE2, 0xE5,
    0xE9, 0xEC, 0xEF, 0xF2, 0xF6, 0xF9, 0xFC, 0xFD
};
#endif

static int xan_decode_frame(AVCodecContext *avctx,
                            void *data, int *got_frame,
                            AVPacket *avpkt)
{
    AVFrame *frame = data;
    const uint8_t *buf = avpkt->data;
    int ret, buf_size = avpkt->size;
    XanContext *s = avctx->priv_data;
    GetByteContext ctx;
    int tag = 0;

    bytestream2_init(&ctx, buf, buf_size);
    while (bytestream2_get_bytes_left(&ctx) > 8 && tag != VGA__TAG) {
        unsigned *tmpptr;
        uint32_t new_pal;
        int size;
        int i;
        tag  = bytestream2_get_le32(&ctx);
        size = bytestream2_get_be32(&ctx);
        if (size < 0) {
            av_log(avctx, AV_LOG_ERROR, "Invalid tag size %d\n", size);
            return AVERROR_INVALIDDATA;
        }
        size = FFMIN(size, bytestream2_get_bytes_left(&ctx));
        switch (tag) {
        case PALT_TAG:
            if (size < PALETTE_SIZE)
                return AVERROR_INVALIDDATA;
            if (s->palettes_count >= PALETTES_MAX)
                return AVERROR_INVALIDDATA;
            tmpptr = av_realloc_array(s->palettes,
                                      s->palettes_count + 1, AVPALETTE_SIZE);
            if (!tmpptr)
                return AVERROR(ENOMEM);
            s->palettes = tmpptr;
            tmpptr += s->palettes_count * AVPALETTE_COUNT;
            for (i = 0; i < PALETTE_COUNT; i++) {
#if RUNTIME_GAMMA
                int r = gamma_corr(bytestream2_get_byteu(&ctx));
                int g = gamma_corr(bytestream2_get_byteu(&ctx));
                int b = gamma_corr(bytestream2_get_byteu(&ctx));
#else
                int r = gamma_lookup[bytestream2_get_byteu(&ctx)];
                int g = gamma_lookup[bytestream2_get_byteu(&ctx)];
                int b = gamma_lookup[bytestream2_get_byteu(&ctx)];
#endif
                *tmpptr++ = (0xFFU << 24) | (r << 16) | (g << 8) | b;
            }
            s->palettes_count++;
            break;
        case SHOT_TAG:
            if (size < 4)
                return AVERROR_INVALIDDATA;
            new_pal = bytestream2_get_le32(&ctx);
            if (new_pal < s->palettes_count) {
                s->cur_palette = new_pal;
            } else
                av_log(avctx, AV_LOG_ERROR, "Invalid palette selected\n");
            break;
        case VGA__TAG:
            break;
        default:
            bytestream2_skip(&ctx, size);
            break;
        }
    }
    buf_size = bytestream2_get_bytes_left(&ctx);

    if (s->palettes_count <= 0) {
        av_log(s->avctx, AV_LOG_ERROR, "No palette found\n");
        return AVERROR_INVALIDDATA;
    }

    if ((ret = ff_get_buffer(avctx, frame, AV_GET_BUFFER_FLAG_REF)) < 0)
        return ret;

    if (!s->frame_size)
        s->frame_size = frame->linesize[0] * s->avctx->height;

    memcpy(frame->data[1],
           s->palettes + s->cur_palette * AVPALETTE_COUNT, AVPALETTE_SIZE);

    s->buf = ctx.buffer;
    s->size = buf_size;

    if (xan_wc3_decode_frame(s, frame) < 0)
        return AVERROR_INVALIDDATA;

    av_frame_unref(s->last_frame);
    if ((ret = av_frame_ref(s->last_frame, frame)) < 0)
        return ret;

    *got_frame = 1;

    /* always report that the buffer was completely consumed */
    return buf_size;
}

AVCodec ff_xan_wc3_decoder = {
    .name           = "xan_wc3",
    .long_name      = NULL_IF_CONFIG_SMALL("Wing Commander III / Xan"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_XAN_WC3,
    .priv_data_size = sizeof(XanContext),
    .init           = xan_decode_init,
    .close          = xan_decode_end,
    .decode         = xan_decode_frame,
    .capabilities   = CODEC_CAP_DR1,
};
