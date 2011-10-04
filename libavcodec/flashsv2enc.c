/*
 * Flash Screen Video Version 2 encoder
 * Copyright (C) 2009 Joshua Warner
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
 * Flash Screen Video Version 2 encoder
 * @author Joshua Warner
 */

/* Differences from version 1 stream:
 * NOTE: Currently, the only player that supports version 2 streams is Adobe Flash Player itself.
 * * Supports sending only a range of scanlines in a block,
 *   indicating a difference from the corresponding block in the last keyframe.
 * * Supports initializing the zlib dictionary with data from the corresponding
 *   block in the last keyframe, to improve compression.
 * * Supports a hybrid 15-bit rgb / 7-bit palette color space.
 */

/* TODO:
 * Don't keep Block structures for both current frame and keyframe.
 * Make better heuristics for deciding stream parameters (optimum_* functions).  Currently these return constants.
 * Figure out how to encode palette information in the stream, choose an optimum palette at each keyframe.
 * Figure out how the zlibPrimeCompressCurrent flag works, implement support.
 * Find other sample files (that weren't generated here), develop a decoder.
 */

#include <stdio.h>
#include <stdlib.h>
#include <zlib.h>

#include "libavutil/imgutils.h"
#include "avcodec.h"
#include "put_bits.h"
#include "bytestream.h"

#define HAS_IFRAME_IMAGE 0x02
#define HAS_PALLET_INFO 0x01

#define COLORSPACE_BGR 0x00
#define COLORSPACE_15_7 0x10
#define HAS_DIFF_BLOCKS 0x04
#define ZLIB_PRIME_COMPRESS_CURRENT 0x02
#define ZLIB_PRIME_COMPRESS_PREVIOUS 0x01

// Disables experimental "smart" parameter-choosing code, as well as the statistics that it depends on.
// At the moment, the "smart" code is a great example of how the parameters *shouldn't* be chosen.
#define FLASHSV2_DUMB

typedef struct Block {
    uint8_t *enc;
    uint8_t *sl_begin, *sl_end;
    int enc_size;
    uint8_t *data;
    unsigned long data_size;

    uint8_t start, len;
    uint8_t dirty;
    uint8_t col, row, width, height;
    uint8_t flags;
} Block;

typedef struct Palette {
    unsigned colors[128];
    uint8_t index[1 << 15];
} Palette;

typedef struct FlashSV2Context {
    AVCodecContext *avctx;
    uint8_t *current_frame;
    uint8_t *key_frame;
    AVFrame frame;
    uint8_t *encbuffer;
    uint8_t *keybuffer;
    uint8_t *databuffer;

    Block *frame_blocks;
    Block *key_blocks;
    int frame_size;
    int blocks_size;

    int use15_7, dist, comp;

    int rows, cols;

    int last_key_frame;

    int image_width, image_height;
    int block_width, block_height;
    uint8_t flags;
    uint8_t use_custom_palette;
    uint8_t palette_type;       ///< 0=>default, 1=>custom - changed when palette regenerated.
    Palette palette;
#ifndef FLASHSV2_DUMB
    double tot_blocks;          ///< blocks encoded since last keyframe
    double diff_blocks;         ///< blocks that were different since last keyframe
    double tot_lines;           ///< total scanlines in image since last keyframe
    double diff_lines;          ///< scanlines that were different since last keyframe
    double raw_size;            ///< size of raw frames since last keyframe
    double comp_size;           ///< size of compressed data since last keyframe
    double uncomp_size;         ///< size of uncompressed data since last keyframe

    double total_bits;          ///< total bits written to stream so far
#endif
} FlashSV2Context;

static av_cold void cleanup(FlashSV2Context * s)
{
    av_freep(&s->encbuffer);
    av_freep(&s->keybuffer);
    av_freep(&s->databuffer);
    av_freep(&s->current_frame);
    av_freep(&s->key_frame);

    av_freep(&s->frame_blocks);
    av_freep(&s->key_blocks);
}

static void init_blocks(FlashSV2Context * s, Block * blocks,
                        uint8_t * encbuf, uint8_t * databuf)
{
    int row, col;
    Block *b;
    for (col = 0; col < s->cols; col++) {
        for (row = 0; row < s->rows; row++) {
            b = blocks + (col + row * s->cols);
            b->width = (col < s->cols - 1) ?
                s->block_width :
                s->image_width - col * s->block_width;

            b->height = (row < s->rows - 1) ?
                s->block_height :
                s->image_height - row * s->block_height;

            b->row   = row;
            b->col   = col;
            b->enc   = encbuf;
            b->data  = databuf;
            encbuf  += b->width * b->height * 3;
            databuf += !databuf ? 0 : b->width * b->height * 6;
        }
    }
}

static void reset_stats(FlashSV2Context * s)
{
#ifndef FLASHSV2_DUMB
    s->diff_blocks = 0.1;
    s->tot_blocks = 1;
    s->diff_lines = 0.1;
    s->tot_lines = 1;
    s->raw_size = s->comp_size = s->uncomp_size = 10;
#endif
}

static av_cold int flashsv2_encode_init(AVCodecContext * avctx)
{
    FlashSV2Context *s = avctx->priv_data;

    s->avctx = avctx;

    s->comp = avctx->compression_level;
    if (s->comp == -1)
        s->comp = 9;
    if (s->comp < 0 || s->comp > 9) {
        av_log(avctx, AV_LOG_ERROR,
               "Compression level should be 0-9, not %d\n", s->comp);
        return -1;
    }


    if ((avctx->width > 4095) || (avctx->height > 4095)) {
        av_log(avctx, AV_LOG_ERROR,
               "Input dimensions too large, input must be max 4096x4096 !\n");
        return -1;
    }

    if (av_image_check_size(avctx->width, avctx->height, 0, avctx) < 0)
        return -1;


    s->last_key_frame = 0;

    s->image_width  = avctx->width;
    s->image_height = avctx->height;

    s->block_width  = (s->image_width /  12) & ~15;
    s->block_height = (s->image_height / 12) & ~15;

    s->rows = (s->image_height + s->block_height - 1) / s->block_height;
    s->cols = (s->image_width +  s->block_width -  1) / s->block_width;

    s->frame_size  = s->image_width * s->image_height * 3;
    s->blocks_size = s->rows * s->cols * sizeof(Block);

    s->encbuffer     = av_mallocz(s->frame_size);
    s->keybuffer     = av_mallocz(s->frame_size);
    s->databuffer    = av_mallocz(s->frame_size * 6);
    s->current_frame = av_mallocz(s->frame_size);
    s->key_frame     = av_mallocz(s->frame_size);
    s->frame_blocks  = av_mallocz(s->blocks_size);
    s->key_blocks    = av_mallocz(s->blocks_size);

    init_blocks(s, s->frame_blocks, s->encbuffer, s->databuffer);
    init_blocks(s, s->key_blocks,   s->keybuffer, 0);
    reset_stats(s);
#ifndef FLASHSV2_DUMB
    s->total_bits = 1;
#endif

    s->use_custom_palette =  0;
    s->palette_type       = -1;        // so that the palette will be generated in reconfigure_at_keyframe

    if (!s->encbuffer || !s->keybuffer || !s->databuffer
        || !s->current_frame || !s->key_frame || !s->key_blocks
        || !s->frame_blocks) {
        av_log(avctx, AV_LOG_ERROR, "Memory allocation failed.\n");
        cleanup(s);
        return -1;
    }

    return 0;
}

static int new_key_frame(FlashSV2Context * s)
{
    int i;
    memcpy(s->key_blocks, s->frame_blocks, s->blocks_size);
    memcpy(s->key_frame, s->current_frame, s->frame_size);

    for (i = 0; i < s->rows * s->cols; i++) {
        s->key_blocks[i].enc += (s->keybuffer - s->encbuffer);
        s->key_blocks[i].sl_begin = 0;
        s->key_blocks[i].sl_end   = 0;
        s->key_blocks[i].data     = 0;
    }
    FFSWAP(uint8_t * , s->keybuffer, s->encbuffer);

    return 0;
}

static int write_palette(FlashSV2Context * s, uint8_t * buf, int buf_size)
{
    //this isn't implemented yet!  Default palette only!
    return -1;
}

static int write_header(FlashSV2Context * s, uint8_t * buf, int buf_size)
{
    PutBitContext pb;
    int buf_pos, len;

    if (buf_size < 5)
        return -1;

    init_put_bits(&pb, buf, buf_size * 8);

    put_bits(&pb, 4, (s->block_width  >> 4) - 1);
    put_bits(&pb, 12, s->image_width);
    put_bits(&pb, 4, (s->block_height >> 4) - 1);
    put_bits(&pb, 12, s->image_height);

    flush_put_bits(&pb);
    buf_pos = 4;

    buf[buf_pos++] = s->flags;

    if (s->flags & HAS_PALLET_INFO) {
        len = write_palette(s, buf + buf_pos, buf_size - buf_pos);
        if (len < 0)
            return -1;
        buf_pos += len;
    }

    return buf_pos;
}

static int write_block(Block * b, uint8_t * buf, int buf_size)
{
    int buf_pos = 0;
    unsigned block_size = b->data_size;

    if (b->flags & HAS_DIFF_BLOCKS)
        block_size += 2;
    if (b->flags & ZLIB_PRIME_COMPRESS_CURRENT)
        block_size += 2;
    if (block_size > 0)
        block_size += 1;
    if (buf_size < block_size + 2)
        return -1;

    buf[buf_pos++] = block_size >> 8;
    buf[buf_pos++] = block_size;

    if (block_size == 0)
        return buf_pos;

    buf[buf_pos++] = b->flags;

    if (b->flags & HAS_DIFF_BLOCKS) {
        buf[buf_pos++] = (b->start);
        buf[buf_pos++] = (b->len);
    }

    if (b->flags & ZLIB_PRIME_COMPRESS_CURRENT) {
        //This feature of the format is poorly understood, and as of now, unused.
        buf[buf_pos++] = (b->col);
        buf[buf_pos++] = (b->row);
    }

    memcpy(buf + buf_pos, b->data, b->data_size);

    buf_pos += b->data_size;

    return buf_pos;
}

static int encode_zlib(Block * b, uint8_t * buf, unsigned long *buf_size, int comp)
{
    int res = compress2(buf, buf_size, b->sl_begin, b->sl_end - b->sl_begin, comp);
    return res == Z_OK ? 0 : -1;
}

static int encode_zlibprime(Block * b, Block * prime, uint8_t * buf,
                            int *buf_size, int comp)
{
    z_stream s;
    int res;
    s.zalloc = NULL;
    s.zfree  = NULL;
    s.opaque = NULL;
    res = deflateInit(&s, comp);
    if (res < 0)
        return -1;

    s.next_in  = prime->enc;
    s.avail_in = prime->enc_size;
    while (s.avail_in > 0) {
        s.next_out  = buf;
        s.avail_out = *buf_size;
        res = deflate(&s, Z_SYNC_FLUSH);
        if (res < 0)
            return -1;
    }

    s.next_in   = b->sl_begin;
    s.avail_in  = b->sl_end - b->sl_begin;
    s.next_out  = buf;
    s.avail_out = *buf_size;
    res = deflate(&s, Z_FINISH);
    deflateEnd(&s);
    *buf_size -= s.avail_out;
    if (res != Z_STREAM_END)
        return -1;
    return 0;
}

static int encode_bgr(Block * b, const uint8_t * src, int stride)
{
    int i;
    uint8_t *ptr = b->enc;
    for (i = 0; i < b->start; i++)
        memcpy(ptr + i * b->width * 3, src + i * stride, b->width * 3);
    b->sl_begin = ptr + i * b->width * 3;
    for (; i < b->start + b->len; i++)
        memcpy(ptr + i * b->width * 3, src + i * stride, b->width * 3);
    b->sl_end = ptr + i * b->width * 3;
    for (; i < b->height; i++)
        memcpy(ptr + i * b->width * 3, src + i * stride, b->width * 3);
    b->enc_size = ptr + i * b->width * 3 - b->enc;
    return b->enc_size;
}

static inline unsigned pixel_color15(const uint8_t * src)
{
    return (src[0] >> 3) | ((src[1] & 0xf8) << 2) | ((src[2] & 0xf8) << 7);
}

static inline unsigned int chroma_diff(unsigned int c1, unsigned int c2)
{
    unsigned int t1 = (c1 & 0x000000ff) + ((c1 & 0x0000ff00) >> 8) + ((c1 & 0x00ff0000) >> 16);
    unsigned int t2 = (c2 & 0x000000ff) + ((c2 & 0x0000ff00) >> 8) + ((c2 & 0x00ff0000) >> 16);

    return abs(t1 - t2) + abs((c1 & 0x000000ff) - (c2 & 0x000000ff)) +
        abs(((c1 & 0x0000ff00) >> 8) - ((c2 & 0x0000ff00) >> 8)) +
        abs(((c1 & 0x00ff0000) >> 16) - ((c2 & 0x00ff0000) >> 16));
}

static inline int pixel_color7_fast(Palette * palette, unsigned c15)
{
    return palette->index[c15];
}

static int pixel_color7_slow(Palette * palette, unsigned color)
{
    int i, min = 0x7fffffff;
    int minc = -1;
    for (i = 0; i < 128; i++) {
        int c1 = palette->colors[i];
        int diff = chroma_diff(c1, color);
        if (diff < min) {
            min = diff;
            minc = i;
        }
    }
    return minc;
}

static inline unsigned pixel_bgr(const uint8_t * src)
{
    return (src[0]) | (src[1] << 8) | (src[2] << 16);
}

static int write_pixel_15_7(Palette * palette, uint8_t * dest, const uint8_t * src,
                            int dist)
{
    unsigned c15 = pixel_color15(src);
    unsigned color = pixel_bgr(src);
    int d15 = chroma_diff(color, color & 0x00f8f8f8);
    int c7 = pixel_color7_fast(palette, c15);
    int d7 = chroma_diff(color, palette->colors[c7]);
    if (dist + d15 >= d7) {
        dest[0] = c7;
        return 1;
    } else {
        dest[0] = 0x80 | (c15 >> 8);
        dest[1] = c15 & 0xff;
        return 2;
    }
}

static int update_palette_index(Palette * palette)
{
    int r, g, b;
    unsigned int bgr, c15, index;
    for (r = 4; r < 256; r += 8) {
        for (g = 4; g < 256; g += 8) {
            for (b = 4; b < 256; b += 8) {
                bgr = b | (g << 8) | (r << 16);
                c15 = (b >> 3) | ((g & 0xf8) << 2) | ((r & 0xf8) << 7);
                index = pixel_color7_slow(palette, bgr);

                palette->index[c15] = index;
            }
        }
    }
    return 0;
}

static const unsigned int default_screen_video_v2_palette[128] = {
    0x00000000, 0x00333333, 0x00666666, 0x00999999, 0x00CCCCCC, 0x00FFFFFF,
    0x00330000, 0x00660000, 0x00990000, 0x00CC0000, 0x00FF0000, 0x00003300,
    0x00006600, 0x00009900, 0x0000CC00, 0x0000FF00, 0x00000033, 0x00000066,
    0x00000099, 0x000000CC, 0x000000FF, 0x00333300, 0x00666600, 0x00999900,
    0x00CCCC00, 0x00FFFF00, 0x00003333, 0x00006666, 0x00009999, 0x0000CCCC,
    0x0000FFFF, 0x00330033, 0x00660066, 0x00990099, 0x00CC00CC, 0x00FF00FF,
    0x00FFFF33, 0x00FFFF66, 0x00FFFF99, 0x00FFFFCC, 0x00FF33FF, 0x00FF66FF,
    0x00FF99FF, 0x00FFCCFF, 0x0033FFFF, 0x0066FFFF, 0x0099FFFF, 0x00CCFFFF,
    0x00CCCC33, 0x00CCCC66, 0x00CCCC99, 0x00CCCCFF, 0x00CC33CC, 0x00CC66CC,
    0x00CC99CC, 0x00CCFFCC, 0x0033CCCC, 0x0066CCCC, 0x0099CCCC, 0x00FFCCCC,
    0x00999933, 0x00999966, 0x009999CC, 0x009999FF, 0x00993399, 0x00996699,
    0x0099CC99, 0x0099FF99, 0x00339999, 0x00669999, 0x00CC9999, 0x00FF9999,
    0x00666633, 0x00666699, 0x006666CC, 0x006666FF, 0x00663366, 0x00669966,
    0x0066CC66, 0x0066FF66, 0x00336666, 0x00996666, 0x00CC6666, 0x00FF6666,
    0x00333366, 0x00333399, 0x003333CC, 0x003333FF, 0x00336633, 0x00339933,
    0x0033CC33, 0x0033FF33, 0x00663333, 0x00993333, 0x00CC3333, 0x00FF3333,
    0x00003366, 0x00336600, 0x00660033, 0x00006633, 0x00330066, 0x00663300,
    0x00336699, 0x00669933, 0x00993366, 0x00339966, 0x00663399, 0x00996633,
    0x006699CC, 0x0099CC66, 0x00CC6699, 0x0066CC99, 0x009966CC, 0x00CC9966,
    0x0099CCFF, 0x00CCFF99, 0x00FF99CC, 0x0099FFCC, 0x00CC99FF, 0x00FFCC99,
    0x00111111, 0x00222222, 0x00444444, 0x00555555, 0x00AAAAAA, 0x00BBBBBB,
    0x00DDDDDD, 0x00EEEEEE
};

static int generate_default_palette(Palette * palette)
{
    memcpy(palette->colors, default_screen_video_v2_palette,
           sizeof(default_screen_video_v2_palette));

    return update_palette_index(palette);
}

static int generate_optimum_palette(Palette * palette, const uint8_t * image,
                                   int width, int height, int stride)
{
    //this isn't implemented yet!  Default palette only!
    return -1;
}

static inline int encode_15_7_sl(Palette * palette, uint8_t * dest,
                                 const uint8_t * src, int width, int dist)
{
    int len = 0, x;
    for (x = 0; x < width; x++) {
        len += write_pixel_15_7(palette, dest + len, src + 3 * x, dist);
    }
    return len;
}

static int encode_15_7(Palette * palette, Block * b, const uint8_t * src,
                       int stride, int dist)
{
    int i;
    uint8_t *ptr = b->enc;
    for (i = 0; i < b->start; i++)
        ptr += encode_15_7_sl(palette, ptr, src + i * stride, b->width, dist);
    b->sl_begin = ptr;
    for (; i < b->start + b->len; i++)
        ptr += encode_15_7_sl(palette, ptr, src + i * stride, b->width, dist);
    b->sl_end = ptr;
    for (; i < b->height; i++)
        ptr += encode_15_7_sl(palette, ptr, src + i * stride, b->width, dist);
    b->enc_size = ptr - b->enc;
    return b->enc_size;
}

static int encode_block(Palette * palette, Block * b, Block * prev,
                        const uint8_t * src, int stride, int comp, int dist,
                        int keyframe)
{
    unsigned buf_size = b->width * b->height * 6;
    uint8_t buf[buf_size];
    int res;
    if (b->flags & COLORSPACE_15_7) {
        encode_15_7(palette, b, src, stride, dist);
    } else {
        encode_bgr(b, src, stride);
    }

    if (b->len > 0) {
        b->data_size = buf_size;
        res = encode_zlib(b, b->data, &b->data_size, comp);
        if (res)
            return res;

        if (!keyframe) {
            res = encode_zlibprime(b, prev, buf, &buf_size, comp);
            if (res)
                return res;

            if (buf_size < b->data_size) {
                b->data_size = buf_size;
                memcpy(b->data, buf, buf_size);
                b->flags |= ZLIB_PRIME_COMPRESS_PREVIOUS;
            }
        }
    } else {
        b->data_size = 0;
    }
    return 0;
}

static int compare_sl(FlashSV2Context * s, Block * b, const uint8_t * src,
                      uint8_t * frame, uint8_t * key, int y, int keyframe)
{
    if (memcmp(src, frame, b->width * 3) != 0) {
        b->dirty = 1;
        memcpy(frame, src, b->width * 3);
#ifndef FLASHSV2_DUMB
        s->diff_lines++;
#endif
    }
    if (memcmp(src, key, b->width * 3) != 0) {
        if (b->len == 0)
            b->start = y;
        b->len = y + 1 - b->start;
    }
    return 0;
}

static int mark_all_blocks(FlashSV2Context * s, const uint8_t * src, int stride,
                           int keyframe)
{
    int sl, rsl, col, pos, possl;
    Block *b;
    for (sl = s->image_height - 1; sl >= 0; sl--) {
        for (col = 0; col < s->cols; col++) {
            rsl = s->image_height - sl - 1;
            b = s->frame_blocks + col + rsl / s->block_height * s->cols;
            possl = stride * sl + col * s->block_width * 3;
            pos = s->image_width * rsl * 3 + col * s->block_width * 3;
            compare_sl(s, b, src + possl, s->current_frame + pos,
                       s->key_frame + pos, rsl % s->block_height, keyframe);
        }
    }
#ifndef FLASHSV2_DUMB
    s->tot_lines += s->image_height * s->cols;
#endif
    return 0;
}

static int encode_all_blocks(FlashSV2Context * s, int keyframe)
{
    int row, col, res;
    uint8_t *data;
    Block *b, *prev;
    for (row = 0; row < s->rows; row++) {
        for (col = 0; col < s->cols; col++) {
            b = s->frame_blocks + (row * s->cols + col);
            prev = s->key_blocks + (row * s->cols + col);
            if (keyframe) {
                b->start = 0;
                b->len = b->height;
                b->flags = s->use15_7 ? COLORSPACE_15_7 : 0;
            } else if (!b->dirty) {
                b->start = 0;
                b->len = 0;
                b->data_size = 0;
                b->flags = s->use15_7 ? COLORSPACE_15_7 : 0;
                continue;
            } else {
                b->flags = s->use15_7 ? COLORSPACE_15_7 | HAS_DIFF_BLOCKS : HAS_DIFF_BLOCKS;
            }
            data = s->current_frame + s->image_width * 3 * s->block_height * row + s->block_width * col * 3;
            res = encode_block(&s->palette, b, prev, data, s->image_width * 3, s->comp, s->dist, keyframe);
#ifndef FLASHSV2_DUMB
            if (b->dirty)
                s->diff_blocks++;
            s->comp_size += b->data_size;
            s->uncomp_size += b->enc_size;
#endif
            if (res)
                return res;
        }
    }
#ifndef FLASHSV2_DUMB
    s->raw_size += s->image_width * s->image_height * 3;
    s->tot_blocks += s->rows * s->cols;
#endif
    return 0;
}

static int write_all_blocks(FlashSV2Context * s, uint8_t * buf,
                            int buf_size)
{
    int row, col, buf_pos = 0, len;
    Block *b;
    for (row = 0; row < s->rows; row++) {
        for (col = 0; col < s->cols; col++) {
            b = s->frame_blocks + row * s->cols + col;
            len = write_block(b, buf + buf_pos, buf_size - buf_pos);
            b->start = b->len = b->dirty = 0;
            if (len < 0)
                return len;
            buf_pos += len;
        }
    }
    return buf_pos;
}

static int write_bitstream(FlashSV2Context * s, const uint8_t * src, int stride,
                           uint8_t * buf, int buf_size, int keyframe)
{
    int buf_pos, res;

    res = mark_all_blocks(s, src, stride, keyframe);
    if (res)
        return res;
    res = encode_all_blocks(s, keyframe);
    if (res)
        return res;

    res = write_header(s, buf, buf_size);
    if (res < 0) {
        return res;
    } else {
        buf_pos = res;
    }
    res = write_all_blocks(s, buf + buf_pos, buf_size - buf_pos);
    if (res < 0)
        return res;
    buf_pos += res;
#ifndef FLASHSV2_DUMB
    s->total_bits += ((double) buf_pos) * 8.0;
#endif

    return buf_pos;
}

static void recommend_keyframe(FlashSV2Context * s, int *keyframe)
{
#ifndef FLASHSV2_DUMB
    double block_ratio, line_ratio, enc_ratio, comp_ratio, data_ratio;
    if (s->avctx->gop_size > 0) {
        block_ratio = s->diff_blocks / s->tot_blocks;
        line_ratio = s->diff_lines / s->tot_lines;
        enc_ratio = s->uncomp_size / s->raw_size;
        comp_ratio = s->comp_size / s->uncomp_size;
        data_ratio = s->comp_size / s->raw_size;

        if ((block_ratio >= 0.5 && line_ratio / block_ratio <= 0.5) || line_ratio >= 0.95) {
            *keyframe = 1;
            return;
        }
    }
#else
    return;
#endif
}

static const double block_size_fraction = 1.0 / 300;
static int optimum_block_width(FlashSV2Context * s)
{
#ifndef FLASHSV2_DUMB
    double save = (1-pow(s->diff_lines/s->diff_blocks/s->block_height, 0.5)) * s->comp_size/s->tot_blocks;
    double width = block_size_fraction * sqrt(0.5 * save * s->rows * s->cols) * s->image_width;
    int pwidth = ((int) width);
    return FFCLIP(pwidth & ~15, 256, 16);
#else
    return 64;
#endif
}

static int optimum_block_height(FlashSV2Context * s)
{
#ifndef FLASHSV2_DUMB
    double save = (1-pow(s->diff_lines/s->diff_blocks/s->block_height, 0.5)) * s->comp_size/s->tot_blocks;
    double height = block_size_fraction * sqrt(0.5 * save * s->rows * s->cols) * s->image_height;
    int pheight = ((int) height);
    return FFCLIP(pheight & ~15, 256, 16);
#else
    return 64;
#endif
}

static const double use15_7_threshold = 8192;

static int optimum_use15_7(FlashSV2Context * s)
{
#ifndef FLASHSV2_DUMB
    double ideal = ((double)(s->avctx->bit_rate * s->avctx->time_base.den * s->avctx->ticks_per_frame)) /
        ((double) s->avctx->time_base.num) * s->avctx->frame_number;
    if (ideal + use15_7_threshold < s->total_bits) {
        return 1;
    } else {
        return 0;
    }
#else
    return s->avctx->global_quality == 0;
#endif
}

static const double color15_7_factor = 100;

static int optimum_dist(FlashSV2Context * s)
{
#ifndef FLASHSV2_DUMB
    double ideal =
        s->avctx->bit_rate * s->avctx->time_base.den *
        s->avctx->ticks_per_frame;
    int dist = pow((s->total_bits / ideal) * color15_7_factor, 3);
    av_log(s->avctx, AV_LOG_DEBUG, "dist: %d\n", dist);
    return dist;
#else
    return 15;
#endif
}


static int reconfigure_at_keyframe(FlashSV2Context * s, const uint8_t * image,
                                   int stride)
{
    int update_palette = 0;
    int res;
    s->block_width = optimum_block_width(s);
    s->block_height = optimum_block_height(s);

    s->rows = (s->image_height + s->block_height - 1) / s->block_height;
    s->cols = (s->image_width +  s->block_width -  1) / s->block_width;

    if (s->rows * s->cols != s->blocks_size / sizeof(Block)) {
        if (s->rows * s->cols > s->blocks_size / sizeof(Block)) {
            s->frame_blocks = av_realloc(s->frame_blocks, s->rows * s->cols * sizeof(Block));
            s->key_blocks = av_realloc(s->key_blocks, s->cols * s->rows * sizeof(Block));
            if (!s->frame_blocks || !s->key_blocks) {
                av_log(s->avctx, AV_LOG_ERROR, "Memory allocation failed.\n");
                return -1;
            }
            s->blocks_size = s->rows * s->cols * sizeof(Block);
        }
        init_blocks(s, s->frame_blocks, s->encbuffer, s->databuffer);
        init_blocks(s, s->key_blocks, s->keybuffer, 0);

    }

    s->use15_7 = optimum_use15_7(s);
    if (s->use15_7) {
        if ((s->use_custom_palette && s->palette_type != 1) || update_palette) {
            res = generate_optimum_palette(&s->palette, image, s->image_width, s->image_height, stride);
            if (res)
                return res;
            s->palette_type = 1;
            av_log(s->avctx, AV_LOG_DEBUG, "Generated optimum palette\n");
        } else if (!s->use_custom_palette && s->palette_type != 0) {
            res = generate_default_palette(&s->palette);
            if (res)
                return res;
            s->palette_type = 0;
            av_log(s->avctx, AV_LOG_DEBUG, "Generated default palette\n");
        }
    }


    reset_stats(s);

    return 0;
}

static int flashsv2_encode_frame(AVCodecContext * avctx, uint8_t * buf,
                                 int buf_size, void *data)
{
    FlashSV2Context *const s = avctx->priv_data;
    AVFrame *pict = data;
    AVFrame *const p = &s->frame;
    int res;
    int keyframe = 0;

    *p = *pict;

    /* First frame needs to be a keyframe */
    if (avctx->frame_number == 0)
        keyframe = 1;

    /* Check the placement of keyframes */
    if (avctx->gop_size > 0) {
        if (avctx->frame_number >= s->last_key_frame + avctx->gop_size)
            keyframe = 1;
    }

    if (buf_size < s->frame_size) {
        //Conservative upper bound check for compressed data
        av_log(avctx, AV_LOG_ERROR, "buf_size %d <  %d\n", buf_size, s->frame_size);
        return -1;
    }

    if (!keyframe
        && avctx->frame_number > s->last_key_frame + avctx->keyint_min) {
        recommend_keyframe(s, &keyframe);
        if (keyframe)
            av_log(avctx, AV_LOG_DEBUG, "Recommending key frame at frame %d\n", avctx->frame_number);
    }

    if (keyframe) {
        res = reconfigure_at_keyframe(s, p->data[0], p->linesize[0]);
        if (res)
            return res;
    }

    if (s->use15_7)
        s->dist = optimum_dist(s);

    res = write_bitstream(s, p->data[0], p->linesize[0], buf, buf_size, keyframe);

    if (keyframe) {
        new_key_frame(s);
        p->pict_type = FF_I_TYPE;
        p->key_frame = 1;
        s->last_key_frame = avctx->frame_number;
        av_log(avctx, AV_LOG_DEBUG, "Inserting key frame at frame %d\n", avctx->frame_number);
    } else {
        p->pict_type = FF_P_TYPE;
        p->key_frame = 0;
    }

    avctx->coded_frame = p;

    return res;
}

static av_cold int flashsv2_encode_end(AVCodecContext * avctx)
{
    FlashSV2Context *s = avctx->priv_data;

    cleanup(s);

    return 0;
}

AVCodec ff_flashsv2_encoder = {
    "flashsv2",
    AVMEDIA_TYPE_VIDEO,
    CODEC_ID_FLASHSV2,
    sizeof(FlashSV2Context),
    flashsv2_encode_init,
    flashsv2_encode_frame,
    flashsv2_encode_end,
    .pix_fmts = (enum PixelFormat[]) {PIX_FMT_BGR24, PIX_FMT_NONE},
    .long_name = NULL_IF_CONFIG_SMALL("Flash Screen Video Version 2"),
    .capabilities   =  CODEC_CAP_EXPERIMENTAL,
};
