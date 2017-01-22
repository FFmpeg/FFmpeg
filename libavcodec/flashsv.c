/*
 * Flash Screen Video decoder
 * Copyright (C) 2004 Alex Beregszaszi
 * Copyright (C) 2006 Benjamin Larsson
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
 * Flash Screen Video decoder
 * @author Alex Beregszaszi
 * @author Benjamin Larsson
 * @author Daniel Verkamp
 * @author Konstantin Shishkov
 *
 * A description of the bitstream format for Flash Screen Video version 1/2
 * is part of the SWF File Format Specification (version 10), which can be
 * downloaded from http://www.adobe.com/devnet/swf.html.
 */

#include <stdio.h>
#include <stdlib.h>
#include <zlib.h>

#include "libavutil/intreadwrite.h"
#include "avcodec.h"
#include "bytestream.h"
#include "get_bits.h"
#include "internal.h"

typedef struct BlockInfo {
    uint8_t *pos;
    int      size;
} BlockInfo;

typedef struct FlashSVContext {
    AVCodecContext *avctx;
    AVFrame        *frame;
    int             image_width, image_height;
    int             block_width, block_height;
    uint8_t        *tmpblock;
    int             block_size;
    z_stream        zstream;
    int             ver;
    const uint32_t *pal;
    int             is_keyframe;
    uint8_t        *keyframedata;
    uint8_t        *keyframe;
    BlockInfo      *blocks;
    uint8_t        *deflate_block;
    int             deflate_block_size;
    int             color_depth;
    int             zlibprime_curr, zlibprime_prev;
    int             diff_start, diff_height;
} FlashSVContext;

static int decode_hybrid(const uint8_t *sptr, const uint8_t *sptr_end, uint8_t *dptr, int dx, int dy,
                         int h, int w, int stride, const uint32_t *pal)
{
    int x, y;
    const uint8_t *orig_src = sptr;

    for (y = dx + h; y > dx; y--) {
        uint8_t *dst = dptr + (y * stride) + dy * 3;
        for (x = 0; x < w; x++) {
            if (sptr >= sptr_end)
                return AVERROR_INVALIDDATA;
            if (*sptr & 0x80) {
                /* 15-bit color */
                unsigned c = AV_RB16(sptr) & ~0x8000;
                unsigned b =  c        & 0x1F;
                unsigned g = (c >>  5) & 0x1F;
                unsigned r =  c >> 10;
                /* 000aaabb -> aaabbaaa  */
                *dst++ = (b << 3) | (b >> 2);
                *dst++ = (g << 3) | (g >> 2);
                *dst++ = (r << 3) | (r >> 2);
                sptr += 2;
            } else {
                /* palette index */
                uint32_t c = pal[*sptr++];
                bytestream_put_le24(&dst, c);
            }
        }
    }
    return sptr - orig_src;
}

static av_cold int flashsv_decode_end(AVCodecContext *avctx)
{
    FlashSVContext *s = avctx->priv_data;
    inflateEnd(&s->zstream);
    /* release the frame if needed */
    av_frame_free(&s->frame);

    /* free the tmpblock */
    av_freep(&s->tmpblock);

    return 0;
}

static av_cold int flashsv_decode_init(AVCodecContext *avctx)
{
    FlashSVContext *s = avctx->priv_data;
    int zret; // Zlib return code

    s->avctx          = avctx;
    s->zstream.zalloc = Z_NULL;
    s->zstream.zfree  = Z_NULL;
    s->zstream.opaque = Z_NULL;
    zret = inflateInit(&s->zstream);
    if (zret != Z_OK) {
        av_log(avctx, AV_LOG_ERROR, "Inflate init error: %d\n", zret);
        return 1;
    }
    avctx->pix_fmt = AV_PIX_FMT_BGR24;

    s->frame = av_frame_alloc();
    if (!s->frame) {
        flashsv_decode_end(avctx);
        return AVERROR(ENOMEM);
    }

    return 0;
}

static int flashsv2_prime(FlashSVContext *s, uint8_t *src, int size)
{
    z_stream zs;
    int zret; // Zlib return code

    if (!src)
        return AVERROR_INVALIDDATA;

    zs.zalloc = NULL;
    zs.zfree  = NULL;
    zs.opaque = NULL;

    s->zstream.next_in   = src;
    s->zstream.avail_in  = size;
    s->zstream.next_out  = s->tmpblock;
    s->zstream.avail_out = s->block_size * 3;
    inflate(&s->zstream, Z_SYNC_FLUSH);

    if (deflateInit(&zs, 0) != Z_OK)
        return -1;
    zs.next_in   = s->tmpblock;
    zs.avail_in  = s->block_size * 3 - s->zstream.avail_out;
    zs.next_out  = s->deflate_block;
    zs.avail_out = s->deflate_block_size;
    deflate(&zs, Z_SYNC_FLUSH);
    deflateEnd(&zs);

    if ((zret = inflateReset(&s->zstream)) != Z_OK) {
        av_log(s->avctx, AV_LOG_ERROR, "Inflate reset error: %d\n", zret);
        return AVERROR_UNKNOWN;
    }

    s->zstream.next_in   = s->deflate_block;
    s->zstream.avail_in  = s->deflate_block_size - zs.avail_out;
    s->zstream.next_out  = s->tmpblock;
    s->zstream.avail_out = s->block_size * 3;
    inflate(&s->zstream, Z_SYNC_FLUSH);

    return 0;
}

static int flashsv_decode_block(AVCodecContext *avctx, AVPacket *avpkt,
                                GetBitContext *gb, int block_size,
                                int width, int height, int x_pos, int y_pos,
                                int blk_idx)
{
    struct FlashSVContext *s = avctx->priv_data;
    uint8_t *line = s->tmpblock;
    int k;
    int ret = inflateReset(&s->zstream);
    if (ret != Z_OK) {
        av_log(avctx, AV_LOG_ERROR, "Inflate reset error: %d\n", ret);
        return AVERROR_UNKNOWN;
    }
    if (s->zlibprime_curr || s->zlibprime_prev) {
        ret = flashsv2_prime(s,
                             s->blocks[blk_idx].pos,
                             s->blocks[blk_idx].size);
        if (ret < 0)
            return ret;
    }
    s->zstream.next_in   = avpkt->data + get_bits_count(gb) / 8;
    s->zstream.avail_in  = block_size;
    s->zstream.next_out  = s->tmpblock;
    s->zstream.avail_out = s->block_size * 3;
    ret = inflate(&s->zstream, Z_FINISH);
    if (ret == Z_DATA_ERROR) {
        av_log(avctx, AV_LOG_ERROR, "Zlib resync occurred\n");
        inflateSync(&s->zstream);
        ret = inflate(&s->zstream, Z_FINISH);
    }

    if (ret != Z_OK && ret != Z_STREAM_END) {
        //return -1;
    }

    if (s->is_keyframe) {
        s->blocks[blk_idx].pos  = s->keyframedata + (get_bits_count(gb) / 8);
        s->blocks[blk_idx].size = block_size;
    }

    y_pos += s->diff_start;

    if (!s->color_depth) {
        /* Flash Screen Video stores the image upside down, so copy
         * lines to destination in reverse order. */
        for (k = 1; k <= s->diff_height; k++) {
            memcpy(s->frame->data[0] + x_pos * 3 +
                   (s->image_height - y_pos - k) * s->frame->linesize[0],
                   line, width * 3);
            /* advance source pointer to next line */
            line += width * 3;
        }
    } else {
        /* hybrid 15-bit/palette mode */
        ret = decode_hybrid(s->tmpblock, s->zstream.next_out,
                      s->frame->data[0],
                      s->image_height - (y_pos + 1 + s->diff_height),
                      x_pos, s->diff_height, width,
                      s->frame->linesize[0], s->pal);
        if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR, "decode_hybrid failed\n");
            return ret;
        }
    }
    skip_bits_long(gb, 8 * block_size); /* skip the consumed bits */
    return 0;
}

static int calc_deflate_block_size(int tmpblock_size)
{
    z_stream zstream;
    int size;

    zstream.zalloc = Z_NULL;
    zstream.zfree  = Z_NULL;
    zstream.opaque = Z_NULL;
    if (deflateInit(&zstream, 0) != Z_OK)
        return -1;
    size = deflateBound(&zstream, tmpblock_size);
    deflateEnd(&zstream);

    return size;
}

static int flashsv_decode_frame(AVCodecContext *avctx, void *data,
                                int *got_frame, AVPacket *avpkt)
{
    int buf_size = avpkt->size;
    FlashSVContext *s = avctx->priv_data;
    int h_blocks, v_blocks, h_part, v_part, i, j, ret;
    GetBitContext gb;
    int last_blockwidth = s->block_width;
    int last_blockheight= s->block_height;

    /* no supplementary picture */
    if (buf_size == 0)
        return 0;
    if (buf_size < 4)
        return -1;

    if ((ret = init_get_bits8(&gb, avpkt->data, buf_size)) < 0)
        return ret;

    /* start to parse the bitstream */
    s->block_width  = 16 * (get_bits(&gb, 4) + 1);
    s->image_width  = get_bits(&gb, 12);
    s->block_height = 16 * (get_bits(&gb, 4) + 1);
    s->image_height = get_bits(&gb, 12);

    if (   last_blockwidth != s->block_width
        || last_blockheight!= s->block_height)
        av_freep(&s->blocks);

    if (s->ver == 2) {
        skip_bits(&gb, 6);
        if (get_bits1(&gb)) {
            avpriv_request_sample(avctx, "iframe");
            return AVERROR_PATCHWELCOME;
        }
        if (get_bits1(&gb)) {
            avpriv_request_sample(avctx, "Custom palette");
            return AVERROR_PATCHWELCOME;
        }
    }

    /* calculate number of blocks and size of border (partial) blocks */
    h_blocks = s->image_width  / s->block_width;
    h_part   = s->image_width  % s->block_width;
    v_blocks = s->image_height / s->block_height;
    v_part   = s->image_height % s->block_height;

    /* the block size could change between frames, make sure the buffer
     * is large enough, if not, get a larger one */
    if (s->block_size < s->block_width * s->block_height) {
        int tmpblock_size = 3 * s->block_width * s->block_height, err;

        if ((err = av_reallocp(&s->tmpblock, tmpblock_size)) < 0) {
            s->block_size = 0;
            av_log(avctx, AV_LOG_ERROR,
                   "Cannot allocate decompression buffer.\n");
            return err;
        }
        if (s->ver == 2) {
            s->deflate_block_size = calc_deflate_block_size(tmpblock_size);
            if (s->deflate_block_size <= 0) {
                av_log(avctx, AV_LOG_ERROR,
                       "Cannot determine deflate buffer size.\n");
                return -1;
            }
            if ((err = av_reallocp(&s->deflate_block, s->deflate_block_size)) < 0) {
                s->block_size = 0;
                av_log(avctx, AV_LOG_ERROR, "Cannot allocate deflate buffer.\n");
                return err;
            }
        }
    }
    s->block_size = s->block_width * s->block_height;

    /* initialize the image size once */
    if (avctx->width == 0 && avctx->height == 0) {
        if ((ret = ff_set_dimensions(avctx, s->image_width, s->image_height)) < 0)
            return ret;
    }

    /* check for changes of image width and image height */
    if (avctx->width != s->image_width || avctx->height != s->image_height) {
        av_log(avctx, AV_LOG_ERROR,
               "Frame width or height differs from first frame!\n");
        av_log(avctx, AV_LOG_ERROR, "fh = %d, fv %d  vs  ch = %d, cv = %d\n",
               avctx->height, avctx->width, s->image_height, s->image_width);
        return AVERROR_INVALIDDATA;
    }

    /* we care for keyframes only in Screen Video v2 */
    s->is_keyframe = (avpkt->flags & AV_PKT_FLAG_KEY) && (s->ver == 2);
    if (s->is_keyframe) {
        int err;
        if ((err = av_reallocp(&s->keyframedata, avpkt->size)) < 0)
            return err;
        memcpy(s->keyframedata, avpkt->data, avpkt->size);
    }
    if(s->ver == 2 && !s->blocks)
        s->blocks = av_mallocz((v_blocks + !!v_part) * (h_blocks + !!h_part) *
                               sizeof(s->blocks[0]));

    ff_dlog(avctx, "image: %dx%d block: %dx%d num: %dx%d part: %dx%d\n",
            s->image_width, s->image_height, s->block_width, s->block_height,
            h_blocks, v_blocks, h_part, v_part);

    if ((ret = ff_reget_buffer(avctx, s->frame)) < 0)
        return ret;

    /* loop over all block columns */
    for (j = 0; j < v_blocks + (v_part ? 1 : 0); j++) {

        int y_pos  = j * s->block_height; // vertical position in frame
        int cur_blk_height = (j < v_blocks) ? s->block_height : v_part;

        /* loop over all block rows */
        for (i = 0; i < h_blocks + (h_part ? 1 : 0); i++) {
            int x_pos = i * s->block_width; // horizontal position in frame
            int cur_blk_width = (i < h_blocks) ? s->block_width : h_part;
            int has_diff = 0;

            /* get the size of the compressed zlib chunk */
            int size = get_bits(&gb, 16);

            s->color_depth    = 0;
            s->zlibprime_curr = 0;
            s->zlibprime_prev = 0;
            s->diff_start     = 0;
            s->diff_height    = cur_blk_height;

            if (8 * size > get_bits_left(&gb)) {
                av_frame_unref(s->frame);
                return AVERROR_INVALIDDATA;
            }

            if (s->ver == 2 && size) {
                skip_bits(&gb, 3);
                s->color_depth    = get_bits(&gb, 2);
                has_diff          = get_bits1(&gb);
                s->zlibprime_curr = get_bits1(&gb);
                s->zlibprime_prev = get_bits1(&gb);

                if (s->color_depth != 0 && s->color_depth != 2) {
                    av_log(avctx, AV_LOG_ERROR,
                           "%dx%d invalid color depth %d\n",
                           i, j, s->color_depth);
                    return AVERROR_INVALIDDATA;
                }

                if (has_diff) {
                    if (size < 3) {
                        av_log(avctx, AV_LOG_ERROR, "size too small for diff\n");
                        return AVERROR_INVALIDDATA;
                    }
                    if (!s->keyframe) {
                        av_log(avctx, AV_LOG_ERROR,
                               "Inter frame without keyframe\n");
                        return AVERROR_INVALIDDATA;
                    }
                    s->diff_start  = get_bits(&gb, 8);
                    s->diff_height = get_bits(&gb, 8);
                    if (s->diff_start + s->diff_height > cur_blk_height) {
                        av_log(avctx, AV_LOG_ERROR,
                               "Block parameters invalid: %d + %d > %d\n",
                               s->diff_start, s->diff_height, cur_blk_height);
                        return AVERROR_INVALIDDATA;
                    }
                    av_log(avctx, AV_LOG_DEBUG,
                           "%dx%d diff start %d height %d\n",
                           i, j, s->diff_start, s->diff_height);
                    size -= 2;
                }

                if (s->zlibprime_prev)
                    av_log(avctx, AV_LOG_DEBUG, "%dx%d zlibprime_prev\n", i, j);

                if (s->zlibprime_curr) {
                    int col = get_bits(&gb, 8);
                    int row = get_bits(&gb, 8);
                    av_log(avctx, AV_LOG_DEBUG, "%dx%d zlibprime_curr %dx%d\n",
                           i, j, col, row);
                    if (size < 3) {
                        av_log(avctx, AV_LOG_ERROR, "size too small for zlibprime_curr\n");
                        return AVERROR_INVALIDDATA;
                    }
                    size -= 2;
                    avpriv_request_sample(avctx, "zlibprime_curr");
                    return AVERROR_PATCHWELCOME;
                }
                if (!s->blocks && (s->zlibprime_curr || s->zlibprime_prev)) {
                    av_log(avctx, AV_LOG_ERROR,
                           "no data available for zlib priming\n");
                    return AVERROR_INVALIDDATA;
                }
                size--; // account for flags byte
            }

            if (has_diff) {
                int k;
                int off = (s->image_height - y_pos - 1) * s->frame->linesize[0];

                for (k = 0; k < cur_blk_height; k++) {
                    int x = off - k * s->frame->linesize[0] + x_pos * 3;
                    memcpy(s->frame->data[0] + x, s->keyframe + x,
                           cur_blk_width * 3);
                }
            }

            /* skip unchanged blocks, which have size 0 */
            if (size) {
                if (flashsv_decode_block(avctx, avpkt, &gb, size,
                                         cur_blk_width, cur_blk_height,
                                         x_pos, y_pos,
                                         i + j * (h_blocks + !!h_part)))
                    av_log(avctx, AV_LOG_ERROR,
                           "error in decompression of block %dx%d\n", i, j);
            }
        }
    }
    if (s->is_keyframe && s->ver == 2) {
        if (!s->keyframe) {
            s->keyframe = av_malloc(s->frame->linesize[0] * avctx->height);
            if (!s->keyframe) {
                av_log(avctx, AV_LOG_ERROR, "Cannot allocate image data\n");
                return AVERROR(ENOMEM);
            }
        }
        memcpy(s->keyframe, s->frame->data[0],
               s->frame->linesize[0] * avctx->height);
    }

    if ((ret = av_frame_ref(data, s->frame)) < 0)
        return ret;

    *got_frame = 1;

    if ((get_bits_count(&gb) / 8) != buf_size)
        av_log(avctx, AV_LOG_ERROR, "buffer not fully consumed (%d != %d)\n",
               buf_size, (get_bits_count(&gb) / 8));

    /* report that the buffer was completely consumed */
    return buf_size;
}

#if CONFIG_FLASHSV_DECODER
AVCodec ff_flashsv_decoder = {
    .name           = "flashsv",
    .long_name      = NULL_IF_CONFIG_SMALL("Flash Screen Video v1"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_FLASHSV,
    .priv_data_size = sizeof(FlashSVContext),
    .init           = flashsv_decode_init,
    .close          = flashsv_decode_end,
    .decode         = flashsv_decode_frame,
    .capabilities   = AV_CODEC_CAP_DR1,
    .pix_fmts       = (const enum AVPixelFormat[]) { AV_PIX_FMT_BGR24, AV_PIX_FMT_NONE },
};
#endif /* CONFIG_FLASHSV_DECODER */

#if CONFIG_FLASHSV2_DECODER
static const uint32_t ff_flashsv2_default_palette[128] = {
    0x000000, 0x333333, 0x666666, 0x999999, 0xCCCCCC, 0xFFFFFF,
    0x330000, 0x660000, 0x990000, 0xCC0000, 0xFF0000, 0x003300,
    0x006600, 0x009900, 0x00CC00, 0x00FF00, 0x000033, 0x000066,
    0x000099, 0x0000CC, 0x0000FF, 0x333300, 0x666600, 0x999900,
    0xCCCC00, 0xFFFF00, 0x003333, 0x006666, 0x009999, 0x00CCCC,
    0x00FFFF, 0x330033, 0x660066, 0x990099, 0xCC00CC, 0xFF00FF,
    0xFFFF33, 0xFFFF66, 0xFFFF99, 0xFFFFCC, 0xFF33FF, 0xFF66FF,
    0xFF99FF, 0xFFCCFF, 0x33FFFF, 0x66FFFF, 0x99FFFF, 0xCCFFFF,
    0xCCCC33, 0xCCCC66, 0xCCCC99, 0xCCCCFF, 0xCC33CC, 0xCC66CC,
    0xCC99CC, 0xCCFFCC, 0x33CCCC, 0x66CCCC, 0x99CCCC, 0xFFCCCC,
    0x999933, 0x999966, 0x9999CC, 0x9999FF, 0x993399, 0x996699,
    0x99CC99, 0x99FF99, 0x339999, 0x669999, 0xCC9999, 0xFF9999,
    0x666633, 0x666699, 0x6666CC, 0x6666FF, 0x663366, 0x669966,
    0x66CC66, 0x66FF66, 0x336666, 0x996666, 0xCC6666, 0xFF6666,
    0x333366, 0x333399, 0x3333CC, 0x3333FF, 0x336633, 0x339933,
    0x33CC33, 0x33FF33, 0x663333, 0x993333, 0xCC3333, 0xFF3333,
    0x003366, 0x336600, 0x660033, 0x006633, 0x330066, 0x663300,
    0x336699, 0x669933, 0x993366, 0x339966, 0x663399, 0x996633,
    0x6699CC, 0x99CC66, 0xCC6699, 0x66CC99, 0x9966CC, 0xCC9966,
    0x99CCFF, 0xCCFF99, 0xFF99CC, 0x99FFCC, 0xCC99FF, 0xFFCC99,
    0x111111, 0x222222, 0x444444, 0x555555, 0xAAAAAA, 0xBBBBBB,
    0xDDDDDD, 0xEEEEEE
};

static av_cold int flashsv2_decode_init(AVCodecContext *avctx)
{
    FlashSVContext *s = avctx->priv_data;
    flashsv_decode_init(avctx);
    s->pal = ff_flashsv2_default_palette;
    s->ver = 2;

    return 0;
}

static av_cold int flashsv2_decode_end(AVCodecContext *avctx)
{
    FlashSVContext *s = avctx->priv_data;

    av_freep(&s->keyframedata);
    av_freep(&s->blocks);
    av_freep(&s->keyframe);
    av_freep(&s->deflate_block);
    flashsv_decode_end(avctx);

    return 0;
}

AVCodec ff_flashsv2_decoder = {
    .name           = "flashsv2",
    .long_name      = NULL_IF_CONFIG_SMALL("Flash Screen Video v2"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_FLASHSV2,
    .priv_data_size = sizeof(FlashSVContext),
    .init           = flashsv2_decode_init,
    .close          = flashsv2_decode_end,
    .decode         = flashsv_decode_frame,
    .capabilities   = AV_CODEC_CAP_DR1,
    .pix_fmts       = (const enum AVPixelFormat[]) { AV_PIX_FMT_BGR24, AV_PIX_FMT_NONE },
};
#endif /* CONFIG_FLASHSV2_DECODER */
