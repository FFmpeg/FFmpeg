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
 * @file flashsv.c
 * Flash Screen Video decoder
 * @author Alex Beregszaszi
 * @author Benjamin Larsson
 */

/* Bitstream description
 * The picture is divided into blocks that are zlib compressed.
 *
 * The decoder is fed complete frames, the frameheader contains:
 * 4bits of block width
 * 12bits of frame width
 * 4bits of block height
 * 12bits of frame height
 *
 * Directly after the header are the compressed blocks. The blocks
 * have their compressed size represented with 16bits in the beginnig.
 * If the size = 0 then the block is unchanged from the previous frame.
 * All blocks are decompressed until the buffer is consumed.
 *
 * Encoding ideas, a basic encoder would just use a fixed block size.
 * Block sizes can be multipels of 16, from 16 to 256. The blocks don't
 * have to be quadratic. A brute force search with a set of diffrent
 * block sizes should give a better result then to just use a fixed size.
 */

#include <stdio.h>
#include <stdlib.h>

#include "common.h"
#include "avcodec.h"
#include "bitstream.h"

#include <zlib.h>

typedef struct FlashSVContext {
    AVCodecContext *avctx;
    AVFrame frame;
    int image_width, image_height;
    int block_width, block_height;
    uint8_t* tmpblock;
    int block_size;
    z_stream zstream;
} FlashSVContext;


static void copy_region(uint8_t *sptr, uint8_t *dptr,
        int dx, int dy, int h, int w, int stride)
{
    int i;

    for (i = dx+h; i > dx; i--)
    {
        memcpy(dptr+(i*stride)+dy*3, sptr, w*3);
        sptr += w*3;
    }
}


static int flashsv_decode_init(AVCodecContext *avctx)
{
    FlashSVContext *s = (FlashSVContext *)avctx->priv_data;
    int zret; // Zlib return code

    s->avctx = avctx;
    s->zstream.zalloc = Z_NULL;
    s->zstream.zfree = Z_NULL;
    s->zstream.opaque = Z_NULL;
    zret = inflateInit(&(s->zstream));
    if (zret != Z_OK) {
        av_log(avctx, AV_LOG_ERROR, "Inflate init error: %d\n", zret);
        return 1;
    }
    avctx->pix_fmt = PIX_FMT_BGR24;
    avctx->has_b_frames = 0;
    s->frame.data[0] = NULL;

    return 0;
}


static int flashsv_decode_frame(AVCodecContext *avctx,
                                    void *data, int *data_size,
                                    uint8_t *buf, int buf_size)
{
    FlashSVContext *s = (FlashSVContext *)avctx->priv_data;
    int h_blocks, v_blocks, h_part, v_part, i, j;
    GetBitContext gb;

    /* no supplementary picture */
    if (buf_size == 0)
        return 0;

    if(s->frame.data[0])
            avctx->release_buffer(avctx, &s->frame);

    init_get_bits(&gb, buf, buf_size * 8);

    /* start to parse the bitstream */
    s->block_width = 16* (get_bits(&gb, 4)+1);
    s->image_width =     get_bits(&gb,12);
    s->block_height= 16* (get_bits(&gb, 4)+1);
    s->image_height=     get_bits(&gb,12);

    /* calculate amount of blocks and the size of the border blocks */
    h_blocks = s->image_width / s->block_width;
    h_part = s->image_width % s->block_width;
    v_blocks = s->image_height / s->block_height;
    v_part = s->image_height % s->block_height;

    /* the block size could change between frames, make sure the buffer
     * is large enough, if not, get a larger one */
    if(s->block_size < s->block_width*s->block_height) {
        if (s->tmpblock != NULL)
            av_free(s->tmpblock);
        if ((s->tmpblock = av_malloc(3*s->block_width*s->block_height)) == NULL) {
            av_log(avctx, AV_LOG_ERROR, "Can't allocate decompression buffer.\n");
            return -1;
        }
    }
    s->block_size = s->block_width*s->block_height;

    /* init the image size once */
    if((avctx->width==0) && (avctx->height==0)){
        avctx->width = s->image_width;
        avctx->height = s->image_height;
    }

    /* check for changes of image width and image height */
    if ((avctx->width != s->image_width) || (avctx->height != s->image_height)) {
        av_log(avctx, AV_LOG_ERROR, "Frame width or height differs from first frames!\n");
        av_log(avctx, AV_LOG_ERROR, "fh = %d, fv %d  vs  ch = %d, cv = %d\n",avctx->height,
        avctx->width,s->image_height,s->image_width);
        return -1;
    }

    av_log(avctx, AV_LOG_DEBUG, "image: %dx%d block: %dx%d num: %dx%d part: %dx%d\n",
        s->image_width, s->image_height, s->block_width, s->block_height,
        h_blocks, v_blocks, h_part, v_part);

    s->frame.reference = 1;
    s->frame.buffer_hints = FF_BUFFER_HINTS_VALID;
    if (avctx->get_buffer(avctx, &s->frame) < 0) {
        av_log(s->avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return -1;
    }

    /* loop over all block columns */
    for (j = 0; j < v_blocks + (v_part?1:0); j++)
    {

        int hp = j*s->block_height; // horiz position in frame
        int hs = (j<v_blocks)?s->block_height:v_part; // size of block


        /* loop over all block rows */
        for (i = 0; i < h_blocks + (h_part?1:0); i++)
        {
            int wp = i*s->block_width; // vert position in frame
            int ws = (i<h_blocks)?s->block_width:h_part; // size of block

            /* get the size of the compressed zlib chunk */
            int size = get_bits(&gb, 16);

            if (size == 0) {
                /* no change, don't do anything */
            } else {
                /* decompress block */
                int ret = inflateReset(&(s->zstream));
                if (ret != Z_OK)
                {
                    av_log(avctx, AV_LOG_ERROR, "error in decompression (reset) of block %dx%d\n", i, j);
                    /* return -1; */
                }
                s->zstream.next_in = buf+(get_bits_count(&gb)/8);
                s->zstream.avail_in = size;
                s->zstream.next_out = s->tmpblock;
                s->zstream.avail_out = s->block_size*3;
                ret = inflate(&(s->zstream), Z_FINISH);
                if (ret == Z_DATA_ERROR)
                {
                    av_log(avctx, AV_LOG_ERROR, "Zlib resync occured\n");
                    inflateSync(&(s->zstream));
                    ret = inflate(&(s->zstream), Z_FINISH);
                }

                if ((ret != Z_OK) && (ret != Z_STREAM_END))
                {
                    av_log(avctx, AV_LOG_ERROR, "error in decompression of block %dx%d: %d\n", i, j, ret);
                    /* return -1; */
                }
                copy_region(s->tmpblock, s->frame.data[0], s->image_height-(hp+hs+1), wp, hs, ws, s->frame.linesize[0]);
                skip_bits(&gb, 8*size);   /* skip the consumed bits */
            }
        }
    }

    *data_size = sizeof(AVFrame);
    *(AVFrame*)data = s->frame;

    if ((get_bits_count(&gb)/8) != buf_size)
        av_log(avctx, AV_LOG_ERROR, "buffer not fully consumed (%d != %d)\n",
            buf_size, (get_bits_count(&gb)/8));

    /* report that the buffer was completely consumed */
    return buf_size;
}


static int flashsv_decode_end(AVCodecContext *avctx)
{
    FlashSVContext *s = (FlashSVContext *)avctx->priv_data;
    inflateEnd(&(s->zstream));
    /* release the frame if needed */
    if (s->frame.data[0])
        avctx->release_buffer(avctx, &s->frame);

    /* free the tmpblock */
    if (s->tmpblock != NULL)
        av_free(s->tmpblock);

    return 0;
}


AVCodec flashsv_decoder = {
    "flashsv",
    CODEC_TYPE_VIDEO,
    CODEC_ID_FLASHSV,
    sizeof(FlashSVContext),
    flashsv_decode_init,
    NULL,
    flashsv_decode_end,
    flashsv_decode_frame,
    CODEC_CAP_DR1,
    .pix_fmts = (enum PixelFormat[]){PIX_FMT_BGR24, -1},
};
