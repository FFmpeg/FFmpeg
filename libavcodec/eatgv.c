/*
 * Electronic Arts TGV Video Decoder
 * Copyright (c) 2007-2008 Peter Ross
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

/**
 * @file
 * Electronic Arts TGV Video Decoder
 * by Peter Ross (pross@xvid.org)
 *
 * Technical details here:
 * http://wiki.multimedia.cx/index.php?title=Electronic_Arts_TGV
 */

#include "avcodec.h"
#define BITSTREAM_READER_LE
#include "get_bits.h"
#include "internal.h"
#include "libavutil/imgutils.h"
#include "libavutil/mem.h"

#define EA_PREAMBLE_SIZE    8
#define kVGT_TAG MKTAG('k', 'V', 'G', 'T')

typedef struct TgvContext {
    AVCodecContext *avctx;
    AVFrame *last_frame;
    uint8_t *frame_buffer;
    int width,height;
    uint32_t palette[AVPALETTE_COUNT];

    int (*mv_codebook)[2];
    uint8_t (*block_codebook)[16];
    int num_mvs;           ///< current length of mv_codebook
    int num_blocks_packed; ///< current length of block_codebook
} TgvContext;

static av_cold int tgv_decode_init(AVCodecContext *avctx)
{
    TgvContext *s = avctx->priv_data;
    s->avctx         = avctx;
    avctx->time_base = (AVRational){1, 15};
    avctx->pix_fmt   = AV_PIX_FMT_PAL8;

    s->last_frame = av_frame_alloc();
    if (!s->last_frame)
        return AVERROR(ENOMEM);

    return 0;
}

/**
 * Unpack buffer
 * @return 0 on success, -1 on critical buffer underflow
 */
static int unpack(const uint8_t *src, const uint8_t *src_end,
                  uint8_t *dst, int width, int height)
{
    uint8_t *dst_end = dst + width*height;
    int size, size1, size2, offset, run;
    uint8_t *dst_start = dst;

    if (src[0] & 0x01)
        src += 5;
    else
        src += 2;

    if (src_end - src < 3)
        return AVERROR_INVALIDDATA;
    size = AV_RB24(src);
    src += 3;

    while (size > 0 && src < src_end) {

        /* determine size1 and size2 */
        size1 = (src[0] & 3);
        if (src[0] & 0x80) {  // 1
            if (src[0] & 0x40 ) {  // 11
                if (src[0] & 0x20) {  // 111
                    if (src[0] < 0xFC)  // !(111111)
                        size1 = (((src[0] & 31) + 1) << 2);
                    src++;
                    size2 = 0;
                } else {  // 110
                    offset = ((src[0] & 0x10) << 12) + AV_RB16(&src[1]) + 1;
                    size2  = ((src[0] & 0xC) << 6) + src[3] + 5;
                    src   += 4;
                }
            } else {  // 10
                size1  = ((src[1] & 0xC0) >> 6);
                offset = (AV_RB16(&src[1]) & 0x3FFF) + 1;
                size2  = (src[0] & 0x3F) + 4;
                src   += 3;
            }
        } else {  // 0
            offset = ((src[0] & 0x60) << 3) + src[1] + 1;
            size2  = ((src[0] & 0x1C) >> 2) + 3;
            src   += 2;
        }


        /* fetch strip from src */
        if (size1 > src_end - src)
            break;

        if (size1 > 0) {
            size -= size1;
            run   = FFMIN(size1, dst_end - dst);
            memcpy(dst, src, run);
            dst += run;
            src += run;
        }

        if (size2 > 0) {
            if (dst - dst_start < offset)
                return 0;
            size -= size2;
            run   = FFMIN(size2, dst_end - dst);
            av_memcpy_backptr(dst, offset, run);
            dst += run;
        }
    }

    return 0;
}

/**
 * Decode inter-frame
 * @return 0 on success, -1 on critical buffer underflow
 */
static int tgv_decode_inter(TgvContext *s, AVFrame *frame,
                            const uint8_t *buf, const uint8_t *buf_end)
{
    int num_mvs;
    int num_blocks_raw;
    int num_blocks_packed;
    int vector_bits;
    int i,j,x,y;
    GetBitContext gb;
    int mvbits;
    const uint8_t *blocks_raw;

    if(buf_end - buf < 12)
        return AVERROR_INVALIDDATA;

    num_mvs           = AV_RL16(&buf[0]);
    num_blocks_raw    = AV_RL16(&buf[2]);
    num_blocks_packed = AV_RL16(&buf[4]);
    vector_bits       = AV_RL16(&buf[6]);
    buf += 12;

    if (vector_bits > MIN_CACHE_BITS || !vector_bits) {
        av_log(s->avctx, AV_LOG_ERROR,
               "Invalid value for motion vector bits: %d\n", vector_bits);
        return AVERROR_INVALIDDATA;
    }

    /* allocate codebook buffers as necessary */
    if (num_mvs > s->num_mvs) {
        if (av_reallocp_array(&s->mv_codebook, num_mvs, sizeof(*s->mv_codebook))) {
            s->num_mvs = 0;
            return AVERROR(ENOMEM);
        }
        s->num_mvs = num_mvs;
    }

    if (num_blocks_packed > s->num_blocks_packed) {
        int err;
        if ((err = av_reallocp(&s->block_codebook, num_blocks_packed * 16)) < 0) {
            s->num_blocks_packed = 0;
            return err;
        }
        s->num_blocks_packed = num_blocks_packed;
    }

    /* read motion vectors */
    mvbits = (num_mvs * 2 * 10 + 31) & ~31;

    if (buf_end - buf < (mvbits>>3) + 16*num_blocks_raw + 8*num_blocks_packed)
        return AVERROR_INVALIDDATA;

    init_get_bits(&gb, buf, mvbits);
    for (i = 0; i < num_mvs; i++) {
        s->mv_codebook[i][0] = get_sbits(&gb, 10);
        s->mv_codebook[i][1] = get_sbits(&gb, 10);
    }
    buf += mvbits >> 3;

    /* note ptr to uncompressed blocks */
    blocks_raw = buf;
    buf       += num_blocks_raw * 16;

    /* read compressed blocks */
    init_get_bits(&gb, buf, (buf_end - buf) << 3);
    for (i = 0; i < num_blocks_packed; i++) {
        int tmp[4];
        for (j = 0; j < 4; j++)
            tmp[j] = get_bits(&gb, 8);
        for (j = 0; j < 16; j++)
            s->block_codebook[i][15-j] = tmp[get_bits(&gb, 2)];
    }

    if (get_bits_left(&gb) < vector_bits *
        (s->avctx->height / 4) * (s->avctx->width / 4))
        return AVERROR_INVALIDDATA;

    /* read vectors and build frame */
    for (y = 0; y < s->avctx->height / 4; y++)
        for (x = 0; x < s->avctx->width / 4; x++) {
            unsigned int vector = get_bits(&gb, vector_bits);
            const uint8_t *src;
            int src_stride;

            if (vector < num_mvs) {
                int mx = x * 4 + s->mv_codebook[vector][0];
                int my = y * 4 + s->mv_codebook[vector][1];

                if (mx < 0 || mx + 4 > s->avctx->width ||
                    my < 0 || my + 4 > s->avctx->height) {
                    av_log(s->avctx, AV_LOG_ERROR, "MV %d %d out of picture\n", mx, my);
                    continue;
                }

                src = s->last_frame->data[0] + mx + my * s->last_frame->linesize[0];
                src_stride = s->last_frame->linesize[0];
            } else {
                int offset = vector - num_mvs;
                if (offset < num_blocks_raw)
                    src = blocks_raw + 16*offset;
                else if (offset - num_blocks_raw < num_blocks_packed)
                    src = s->block_codebook[offset - num_blocks_raw];
                else
                    continue;
                src_stride = 4;
            }

            for (j = 0; j < 4; j++)
                for (i = 0; i < 4; i++)
                    frame->data[0][(y * 4 + j) * frame->linesize[0] + (x * 4 + i)] =
                        src[j * src_stride + i];
    }

    return 0;
}

static int tgv_decode_frame(AVCodecContext *avctx,
                            void *data, int *got_frame,
                            AVPacket *avpkt)
{
    const uint8_t *buf     = avpkt->data;
    int buf_size           = avpkt->size;
    TgvContext *s          = avctx->priv_data;
    const uint8_t *buf_end = buf + buf_size;
    AVFrame *frame         = data;
    int chunk_type, ret;

    if (buf_end - buf < EA_PREAMBLE_SIZE)
        return AVERROR_INVALIDDATA;

    chunk_type = AV_RL32(&buf[0]);
    buf       += EA_PREAMBLE_SIZE;

    if (chunk_type == kVGT_TAG) {
        int pal_count, i;
        if(buf_end - buf < 12) {
            av_log(avctx, AV_LOG_WARNING, "truncated header\n");
            return AVERROR_INVALIDDATA;
        }

        s->width  = AV_RL16(&buf[0]);
        s->height = AV_RL16(&buf[2]);
        if (s->avctx->width != s->width || s->avctx->height != s->height) {
            av_freep(&s->frame_buffer);
            av_frame_unref(s->last_frame);
            if ((ret = ff_set_dimensions(s->avctx, s->width, s->height)) < 0)
                return ret;
        }

        pal_count = AV_RL16(&buf[6]);
        buf += 12;
        for(i = 0; i < pal_count && i < AVPALETTE_COUNT && buf_end - buf >= 3; i++) {
            s->palette[i] = 0xFFU << 24 | AV_RB24(buf);
            buf += 3;
        }
    }

    if ((ret = ff_get_buffer(avctx, frame, AV_GET_BUFFER_FLAG_REF)) < 0)
        return ret;

    memcpy(frame->data[1], s->palette, AVPALETTE_SIZE);

    if (chunk_type == kVGT_TAG) {
        int y;
        frame->key_frame = 1;
        frame->pict_type = AV_PICTURE_TYPE_I;

        if (!s->frame_buffer &&
            !(s->frame_buffer = av_mallocz(s->width * s->height)))
            return AVERROR(ENOMEM);

        if (unpack(buf, buf_end, s->frame_buffer, s->avctx->width, s->avctx->height) < 0) {
            av_log(avctx, AV_LOG_WARNING, "truncated intra frame\n");
            return AVERROR_INVALIDDATA;
        }
        for (y = 0; y < s->height; y++)
            memcpy(frame->data[0]  + y * frame->linesize[0],
                   s->frame_buffer + y * s->width,
                   s->width);
    } else {
        if (!s->last_frame->data[0]) {
            av_log(avctx, AV_LOG_WARNING, "inter frame without corresponding intra frame\n");
            return buf_size;
        }
        frame->key_frame = 0;
        frame->pict_type = AV_PICTURE_TYPE_P;
        if (tgv_decode_inter(s, frame, buf, buf_end) < 0) {
            av_log(avctx, AV_LOG_WARNING, "truncated inter frame\n");
            return AVERROR_INVALIDDATA;
        }
    }

    av_frame_unref(s->last_frame);
    if ((ret = av_frame_ref(s->last_frame, frame)) < 0)
        return ret;

    *got_frame = 1;

    return buf_size;
}

static av_cold int tgv_decode_end(AVCodecContext *avctx)
{
    TgvContext *s = avctx->priv_data;
    av_frame_free(&s->last_frame);
    av_freep(&s->frame_buffer);
    av_free(s->mv_codebook);
    av_free(s->block_codebook);
    return 0;
}

AVCodec ff_eatgv_decoder = {
    .name           = "eatgv",
    .long_name      = NULL_IF_CONFIG_SMALL("Electronic Arts TGV video"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_TGV,
    .priv_data_size = sizeof(TgvContext),
    .init           = tgv_decode_init,
    .close          = tgv_decode_end,
    .decode         = tgv_decode_frame,
    .capabilities   = CODEC_CAP_DR1,
};
