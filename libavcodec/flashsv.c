/*
 * Flash Screen Video decoder
 * Copyright (C) 2004 Alex Beregszaszi
 * Copyright (C) 2006 Benjamin Larsson
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

/**
 * @file
 * Flash Screen Video decoder
 * @author Alex Beregszaszi
 * @author Benjamin Larsson
 *
 * A description of the bitstream format for Flash Screen Video version 1/2
 * is part of the SWF File Format Specification (version 10), which can be
 * downloaded from http://www.adobe.com/devnet/swf.html.
 */

#include <stdio.h>
#include <stdlib.h>
#include <zlib.h>

#include "avcodec.h"
#include "get_bits.h"

typedef struct FlashSVContext {
    AVCodecContext *avctx;
    AVFrame         frame;
    int             image_width, image_height;
    int             block_width, block_height;
    uint8_t        *tmpblock;
    int             block_size;
    z_stream        zstream;
} FlashSVContext;


static void copy_region(uint8_t *sptr, uint8_t *dptr,
                        int dx, int dy, int h, int w, int stride)
{
    int i;

    for (i = dx + h; i > dx; i--) {
        memcpy(dptr + i * stride + dy * 3, sptr, w * 3);
        sptr += w * 3;
    }
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
    avctx->pix_fmt = PIX_FMT_BGR24;
    s->frame.data[0] = NULL;

    return 0;
}


static int flashsv_decode_frame(AVCodecContext *avctx, void *data,
                                int *data_size, AVPacket *avpkt)
{
    int buf_size       = avpkt->size;
    FlashSVContext *s  = avctx->priv_data;
    int h_blocks, v_blocks, h_part, v_part, i, j;
    GetBitContext gb;

    /* no supplementary picture */
    if (buf_size == 0)
        return 0;
    if (buf_size < 4)
        return -1;

    init_get_bits(&gb, avpkt->data, buf_size * 8);

    /* start to parse the bitstream */
    s->block_width  = 16 * (get_bits(&gb,  4) + 1);
    s->image_width  =       get_bits(&gb, 12);
    s->block_height = 16 * (get_bits(&gb,  4) + 1);
    s->image_height =       get_bits(&gb, 12);

    /* calculate number of blocks and size of border (partial) blocks */
    h_blocks = s->image_width  / s->block_width;
    h_part   = s->image_width  % s->block_width;
    v_blocks = s->image_height / s->block_height;
    v_part   = s->image_height % s->block_height;

    /* the block size could change between frames, make sure the buffer
     * is large enough, if not, get a larger one */
    if (s->block_size < s->block_width * s->block_height) {
        av_free(s->tmpblock);
        if ((s->tmpblock = av_malloc(3 * s->block_width * s->block_height)) == NULL) {
            av_log(avctx, AV_LOG_ERROR, "Can't allocate decompression buffer.\n");
            return AVERROR(ENOMEM);
        }
    }
    s->block_size = s->block_width * s->block_height;

    /* initialize the image size once */
    if (avctx->width == 0 && avctx->height == 0) {
        avctx->width  = s->image_width;
        avctx->height = s->image_height;
    }

    /* check for changes of image width and image height */
    if (avctx->width != s->image_width || avctx->height != s->image_height) {
        av_log(avctx, AV_LOG_ERROR,
               "Frame width or height differs from first frames!\n");
        av_log(avctx, AV_LOG_ERROR, "fh = %d, fv %d  vs  ch = %d, cv = %d\n",
               avctx->height, avctx->width, s->image_height, s->image_width);
        return AVERROR_INVALIDDATA;
    }

    av_dlog(avctx, "image: %dx%d block: %dx%d num: %dx%d part: %dx%d\n",
            s->image_width, s->image_height, s->block_width, s->block_height,
            h_blocks, v_blocks, h_part, v_part);

    s->frame.reference    = 3;
    s->frame.buffer_hints = FF_BUFFER_HINTS_VALID    |
                            FF_BUFFER_HINTS_PRESERVE |
                            FF_BUFFER_HINTS_REUSABLE;
    if (avctx->reget_buffer(avctx, &s->frame) < 0) {
        av_log(avctx, AV_LOG_ERROR, "reget_buffer() failed\n");
        return -1;
    }

    /* loop over all block columns */
    for (j = 0; j < v_blocks + (v_part ? 1 : 0); j++) {

        int y_pos  = j * s->block_height; // vertical position in frame
        int cur_blk_height = (j < v_blocks) ? s->block_height : v_part;

        /* loop over all block rows */
        for (i = 0; i < h_blocks + (h_part ? 1 : 0); i++) {
            int x_pos = i * s->block_width; // horizontal position in frame
            int cur_blk_width = (i < h_blocks) ? s->block_width : h_part;

            /* get the size of the compressed zlib chunk */
            int size = get_bits(&gb, 16);
            if (8 * size > get_bits_left(&gb)) {
                avctx->release_buffer(avctx, &s->frame);
                s->frame.data[0] = NULL;
                return AVERROR_INVALIDDATA;
            }

            /* skip unchanged blocks, which have size 0 */
            if (size) {
                /* decompress block */
                int ret = inflateReset(&s->zstream);
                if (ret != Z_OK) {
                    av_log(avctx, AV_LOG_ERROR,
                           "error in decompression (reset) of block %dx%d\n", i, j);
                    /* return -1; */
                }
                s->zstream.next_in   = avpkt->data + get_bits_count(&gb) / 8;
                s->zstream.avail_in  = size;
                s->zstream.next_out  = s->tmpblock;
                s->zstream.avail_out = s->block_size * 3;
                ret = inflate(&s->zstream, Z_FINISH);
                if (ret == Z_DATA_ERROR) {
                    av_log(avctx, AV_LOG_ERROR, "Zlib resync occurred\n");
                    inflateSync(&s->zstream);
                    ret = inflate(&s->zstream, Z_FINISH);
                }

                if (ret != Z_OK && ret != Z_STREAM_END) {
                    av_log(avctx, AV_LOG_ERROR,
                           "error in decompression of block %dx%d: %d\n", i, j, ret);
                    /* return -1; */
                }
                copy_region(s->tmpblock, s->frame.data[0],
                            s->image_height - (y_pos + cur_blk_height + 1),
                            x_pos, cur_blk_height, cur_blk_width,
                            s->frame.linesize[0]);
                skip_bits_long(&gb, 8 * size);   /* skip the consumed bits */
            }
        }
    }

    *data_size = sizeof(AVFrame);
    *(AVFrame*)data = s->frame;

    if ((get_bits_count(&gb) / 8) != buf_size)
        av_log(avctx, AV_LOG_ERROR, "buffer not fully consumed (%d != %d)\n",
               buf_size, (get_bits_count(&gb) / 8));

    /* report that the buffer was completely consumed */
    return buf_size;
}


static av_cold int flashsv_decode_end(AVCodecContext *avctx)
{
    FlashSVContext *s = avctx->priv_data;
    inflateEnd(&s->zstream);
    /* release the frame if needed */
    if (s->frame.data[0])
        avctx->release_buffer(avctx, &s->frame);

    /* free the tmpblock */
    av_free(s->tmpblock);

    return 0;
}


AVCodec ff_flashsv_decoder = {
    .name           = "flashsv",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_FLASHSV,
    .priv_data_size = sizeof(FlashSVContext),
    .init           = flashsv_decode_init,
    .close          = flashsv_decode_end,
    .decode         = flashsv_decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .pix_fmts       = (const enum PixelFormat[]){PIX_FMT_BGR24, PIX_FMT_NONE},
    .long_name      = NULL_IF_CONFIG_SMALL("Flash Screen Video v1"),
};
