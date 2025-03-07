/*
 * Flash Screen Video encoder
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

/* Encoding development sponsored by http://fh-campuswien.ac.at */

/**
 * @file
 * Flash Screen Video encoder
 * @author Alex Beregszaszi
 * @author Benjamin Larsson
 *
 * A description of the bitstream format for Flash Screen Video version 1/2
 * is part of the SWF File Format Specification (version 10), which can be
 * downloaded from http://www.adobe.com/devnet/swf.html.
 */

/*
 * Encoding ideas: A basic encoder would just use a fixed block size.
 * Block sizes can be multiples of 16, from 16 to 256. The blocks don't
 * have to be quadratic. A brute force search with a set of different
 * block sizes should give a better result than to just use a fixed size.
 *
 * TODO:
 * Don't reencode the frame in brute force mode if the frame is a dupe.
 * Speed up. Make the difference check faster.
 */

#include <stdint.h>
#include <zlib.h>

#include "libavutil/buffer.h"

#include "avcodec.h"
#include "codec_internal.h"
#include "encode.h"
#include "put_bits.h"
#include "bytestream.h"

/* These values are hardcoded for now. */
#define BLOCK_WIDTH  (4 * 16U)
#define BLOCK_HEIGHT (4 * 16U)

typedef struct FlashSVContext {
    AVCodecContext *avctx;
    const uint8_t  *previous_frame;
    AVBufferRef    *prev_frame_buf;
    int             image_width, image_height;
    unsigned        packet_size;
    int64_t         last_key_frame;
    uint8_t         tmpblock[3 * 256 * 256];
    int             compression_level;
} FlashSVContext;

static int copy_region_enc(const uint8_t *sptr, uint8_t *dptr, int dx, int dy,
                           int h, int w, int stride, const uint8_t *pfptr)
{
    int i, j;
    int diff = 0;

    for (i = dx + h; i > dx; i--) {
        const uint8_t *nsptr = sptr + i * stride + dy * 3;
        const uint8_t *npfptr = pfptr + i * stride + dy * 3;
        for (j = 0; j < w * 3; j++) {
            diff    |= npfptr[j] ^ nsptr[j];
            dptr[j]  = nsptr[j];
        }
        dptr += w * 3;
    }
    if (diff)
        return 1;
    return 0;
}

static av_cold int flashsv_encode_end(AVCodecContext *avctx)
{
    FlashSVContext *s = avctx->priv_data;

    av_buffer_unref(&s->prev_frame_buf);

    return 0;
}

static av_cold int flashsv_encode_init(AVCodecContext *avctx)
{
    FlashSVContext *s = avctx->priv_data;
    int h_blocks, v_blocks, nb_blocks;

    s->avctx = avctx;

    if (avctx->width > 4095 || avctx->height > 4095) {
        av_log(avctx, AV_LOG_ERROR,
               "Input dimensions too large, input must be max 4095x4095 !\n");
        return AVERROR_INVALIDDATA;
    }

    s->last_key_frame = 0;

    s->image_width  = avctx->width;
    s->image_height = avctx->height;

    h_blocks = (s->image_width  + BLOCK_WIDTH - 1) / BLOCK_WIDTH;
    v_blocks = (s->image_height + BLOCK_WIDTH - 1) / BLOCK_WIDTH;
    nb_blocks = h_blocks * v_blocks;
    s->packet_size = 4 + nb_blocks * (2 + 3 * BLOCK_WIDTH * BLOCK_HEIGHT);

    s->compression_level = avctx->compression_level == FF_COMPRESSION_DEFAULT
                         ? Z_DEFAULT_COMPRESSION
                         : av_clip(avctx->compression_level, 0, 9);

    return 0;
}


static int encode_bitstream(FlashSVContext *s, const AVFrame *p, uint8_t *buf,
                            int buf_size, int block_width, int block_height,
                            const uint8_t *previous_frame, int *I_frame)
{

    PutBitContext pb;
    int h_blocks, v_blocks, h_part, v_part, i, j;
    int buf_pos, res;
    int pred_blocks = 0;

    init_put_bits(&pb, buf, buf_size);

    put_bits(&pb,  4, block_width / 16 - 1);
    put_bits(&pb, 12, s->image_width);
    put_bits(&pb,  4, block_height / 16 - 1);
    put_bits(&pb, 12, s->image_height);
    flush_put_bits(&pb);
    buf_pos = 4;

    h_blocks = s->image_width  / block_width;
    h_part   = s->image_width  % block_width;
    v_blocks = s->image_height / block_height;
    v_part   = s->image_height % block_height;

    /* loop over all block columns */
    for (j = 0; j < v_blocks + (v_part ? 1 : 0); j++) {

        int y_pos = j * block_height; // vertical position in frame
        int cur_blk_height = (j < v_blocks) ? block_height : v_part;

        /* loop over all block rows */
        for (i = 0; i < h_blocks + (h_part ? 1 : 0); i++) {
            int x_pos = i * block_width; // horizontal position in frame
            int cur_blk_width = (i < h_blocks) ? block_width : h_part;
            int ret = Z_OK;
            uint8_t *ptr = buf + buf_pos;

            /* copy the block to the temp buffer before compression
             * (if it differs from the previous frame's block) */
            res = copy_region_enc(p->data[0], s->tmpblock,
                                  s->image_height - (y_pos + cur_blk_height + 1),
                                  x_pos, cur_blk_height, cur_blk_width,
                                  p->linesize[0], previous_frame);

            if (res || *I_frame) {
                unsigned long zsize = 3 * block_width * block_height + 12;
                ret = compress2(ptr + 2, &zsize, s->tmpblock,
                                3 * cur_blk_width * cur_blk_height,
                                s->compression_level);

                if (ret != Z_OK)
                    av_log(s->avctx, AV_LOG_ERROR,
                           "error while compressing block %dx%d\n", i, j);

                bytestream_put_be16(&ptr, zsize);
                buf_pos += zsize + 2;
                ff_dlog(s->avctx, "buf_pos = %d\n", buf_pos);
            } else {
                pred_blocks++;
                bytestream_put_be16(&ptr, 0);
                buf_pos += 2;
            }
        }
    }

    if (pred_blocks)
        *I_frame = 0;
    else
        *I_frame = 1;

    return buf_pos;
}


static int flashsv_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                                const AVFrame *pict, int *got_packet)
{
    FlashSVContext * const s = avctx->priv_data;
    const uint8_t *prev_frame = s->previous_frame;
    int res;
    int I_frame = 0;
    int opt_w = 4, opt_h = 4;

    /* First frame needs to be a keyframe */
    if (!s->previous_frame) {
        prev_frame = pict->data[0];
        I_frame = 1;
    }

    /* Check the placement of keyframes */
    if (avctx->gop_size > 0 &&
        avctx->frame_num >= s->last_key_frame + avctx->gop_size) {
        I_frame = 1;
    }

    res = ff_alloc_packet(avctx, pkt, s->packet_size);
    if (res < 0)
        return res;

    pkt->size = encode_bitstream(s, pict, pkt->data, pkt->size,
                                 opt_w * 16, opt_h * 16,
                                 prev_frame, &I_frame);

    //mark the frame type so the muxer can mux it correctly
    if (I_frame) {
        s->last_key_frame = avctx->frame_num;
        ff_dlog(avctx, "Inserting keyframe at frame %"PRId64"\n", avctx->frame_num);
    }

    if (I_frame)
        pkt->flags |= AV_PKT_FLAG_KEY;
    *got_packet = 1;

    //save the current frame
    res = av_buffer_replace(&s->prev_frame_buf, pict->buf[0]);
    if (res < 0)
        return res;
    s->previous_frame = pict->data[0];

    return 0;
}

const FFCodec ff_flashsv_encoder = {
    .p.name         = "flashsv",
    CODEC_LONG_NAME("Flash Screen Video"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_FLASHSV,
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE,
    .priv_data_size = sizeof(FlashSVContext),
    .init           = flashsv_encode_init,
    FF_CODEC_ENCODE_CB(flashsv_encode_frame),
    .close          = flashsv_encode_end,
    CODEC_PIXFMTS(AV_PIX_FMT_BGR24),
};
