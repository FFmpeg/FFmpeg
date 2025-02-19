/*
 * Tiertex Limited SEQ Video Decoder
 * Copyright (c) 2006 Gregory Montoir (cyx@users.sourceforge.net)
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
 * Tiertex Limited SEQ video decoder
 */

#define BITSTREAM_READER_LE
#include "avcodec.h"
#include "codec_internal.h"
#include "decode.h"
#include "get_bits.h"


typedef struct SeqVideoContext {
    AVCodecContext *avctx;
    AVFrame *frame;
} SeqVideoContext;


static const unsigned char *seq_unpack_rle_block(const unsigned char *src,
                                                 const unsigned char *src_end,
                                                 unsigned char *dst, int dst_size)
{
    int i, len, sz;
    GetBitContext gb;
    int code_table[64];

    /* get the rle codes */
    init_get_bits(&gb, src, (src_end - src) * 8);
    for (i = 0, sz = 0; i < 64 && sz < dst_size; i++) {
        if (get_bits_left(&gb) < 4)
            return NULL;
        code_table[i] = get_sbits(&gb, 4);
        sz += FFABS(code_table[i]);
    }
    src += (get_bits_count(&gb) + 7) / 8;

    /* do the rle unpacking */
    for (i = 0; i < 64 && dst_size > 0; i++) {
        len = code_table[i];
        if (len < 0) {
            len = -len;
            if (src_end - src < 1)
                return NULL;
            memset(dst, *src++, FFMIN(len, dst_size));
        } else {
            if (src_end - src < len)
                return NULL;
            memcpy(dst, src, FFMIN(len, dst_size));
            src += len;
        }
        dst += len;
        dst_size -= len;
    }
    return src;
}

static const unsigned char *seq_decode_op1(SeqVideoContext *seq,
                                           const unsigned char *src,
                                           const unsigned char *src_end,
                                           unsigned char *dst)
{
    const unsigned char *color_table;
    int b, i, len, bits;
    GetBitContext gb;
    unsigned char block[8 * 8];

    if (src_end - src < 1)
        return NULL;
    len = *src++;
    if (len & 0x80) {
        switch (len & 3) {
        case 1:
            src = seq_unpack_rle_block(src, src_end, block, sizeof(block));
            for (b = 0; b < 8; b++) {
                memcpy(dst, &block[b * 8], 8);
                dst += seq->frame->linesize[0];
            }
            break;
        case 2:
            src = seq_unpack_rle_block(src, src_end, block, sizeof(block));
            for (i = 0; i < 8; i++) {
                for (b = 0; b < 8; b++)
                    dst[b * seq->frame->linesize[0]] = block[i * 8 + b];
                ++dst;
            }
            break;
        }
    } else {
        if (len <= 0)
            return NULL;
        bits = ff_log2_tab[len - 1] + 1;
        if (src_end - src < len + 8 * bits)
            return NULL;
        color_table = src;
        src += len;
        init_get_bits(&gb, src, bits * 8 * 8); src += bits * 8;
        for (b = 0; b < 8; b++) {
            for (i = 0; i < 8; i++)
                dst[i] = color_table[get_bits(&gb, bits)];
            dst += seq->frame->linesize[0];
        }
    }

    return src;
}

static const unsigned char *seq_decode_op2(SeqVideoContext *seq,
                                           const unsigned char *src,
                                           const unsigned char *src_end,
                                           unsigned char *dst)
{
    int i;

    if (src_end - src < 8 * 8)
        return NULL;

    for (i = 0; i < 8; i++) {
        memcpy(dst, src, 8);
        src += 8;
        dst += seq->frame->linesize[0];
    }

    return src;
}

static const unsigned char *seq_decode_op3(SeqVideoContext *seq,
                                           const unsigned char *src,
                                           const unsigned char *src_end,
                                           unsigned char *dst)
{
    int pos, offset;

    do {
        if (src_end - src < 2)
            return NULL;
        pos = *src++;
        offset = ((pos >> 3) & 7) * seq->frame->linesize[0] + (pos & 7);
        dst[offset] = *src++;
    } while (!(pos & 0x80));

    return src;
}

static int seqvideo_decode(SeqVideoContext *seq, const unsigned char *data, int data_size)
{
    const unsigned char *data_end = data + data_size;
    GetBitContext gb;
    int flags, i, j, x, y, op;
    unsigned char c[3];
    unsigned char *dst;
    uint32_t *palette;

    flags = *data++;

    if (flags & 1) {
        palette = (uint32_t *)seq->frame->data[1];
        if (data_end - data < 256 * 3)
            return AVERROR_INVALIDDATA;
        for (i = 0; i < 256; i++) {
            for (j = 0; j < 3; j++, data++)
                c[j] = (*data << 2) | (*data >> 4);
            palette[i] = 0xFFU << 24 | AV_RB24(c);
        }
    }

    if (flags & 2) {
        if (data_end - data < 128)
            return AVERROR_INVALIDDATA;
        init_get_bits(&gb, data, 128 * 8); data += 128;
        for (y = 0; y < 128; y += 8)
            for (x = 0; x < 256; x += 8) {
                dst = &seq->frame->data[0][y * seq->frame->linesize[0] + x];
                op = get_bits(&gb, 2);
                switch (op) {
                case 1:
                    data = seq_decode_op1(seq, data, data_end, dst);
                    break;
                case 2:
                    data = seq_decode_op2(seq, data, data_end, dst);
                    break;
                case 3:
                    data = seq_decode_op3(seq, data, data_end, dst);
                    break;
                }
                if (!data)
                    return AVERROR_INVALIDDATA;
            }
    }
    return 0;
}

static av_cold int seqvideo_decode_init(AVCodecContext *avctx)
{
    SeqVideoContext *seq = avctx->priv_data;
    int ret;

    seq->avctx = avctx;
    avctx->pix_fmt = AV_PIX_FMT_PAL8;

    ret = ff_set_dimensions(avctx, 256, 128);
    if (ret < 0)
        return ret;

    seq->frame = av_frame_alloc();
    if (!seq->frame)
        return AVERROR(ENOMEM);

    return 0;
}

static int seqvideo_decode_frame(AVCodecContext *avctx, AVFrame *rframe,
                                 int *got_frame, AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    int ret;

    SeqVideoContext *seq = avctx->priv_data;

    if ((ret = ff_reget_buffer(avctx, seq->frame, 0)) < 0)
        return ret;

    if (seqvideo_decode(seq, buf, buf_size))
        return AVERROR_INVALIDDATA;

    if ((ret = av_frame_ref(rframe, seq->frame)) < 0)
        return ret;
    *got_frame       = 1;

    return buf_size;
}

static av_cold int seqvideo_decode_end(AVCodecContext *avctx)
{
    SeqVideoContext *seq = avctx->priv_data;

    av_frame_free(&seq->frame);

    return 0;
}

const FFCodec ff_tiertexseqvideo_decoder = {
    .p.name         = "tiertexseqvideo",
    CODEC_LONG_NAME("Tiertex Limited SEQ video"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_TIERTEXSEQVIDEO,
    .priv_data_size = sizeof(SeqVideoContext),
    .init           = seqvideo_decode_init,
    .close          = seqvideo_decode_end,
    FF_CODEC_DECODE_CB(seqvideo_decode_frame),
    .p.capabilities = AV_CODEC_CAP_DR1,
};
