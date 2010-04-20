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

#include "avcodec.h"
#define ALT_BITSTREAM_READER_LE
#include "get_bits.h"


typedef struct SeqVideoContext {
    AVCodecContext *avctx;
    AVFrame frame;
} SeqVideoContext;


static const unsigned char *seq_unpack_rle_block(const unsigned char *src, unsigned char *dst, int dst_size)
{
    int i, len, sz;
    GetBitContext gb;
    int code_table[64];

    /* get the rle codes (at most 64 bytes) */
    init_get_bits(&gb, src, 64 * 8);
    for (i = 0, sz = 0; i < 64 && sz < dst_size; i++) {
        code_table[i] = get_sbits(&gb, 4);
        sz += FFABS(code_table[i]);
    }
    src += (get_bits_count(&gb) + 7) / 8;

    /* do the rle unpacking */
    for (i = 0; i < 64 && dst_size > 0; i++) {
        len = code_table[i];
        if (len < 0) {
            len = -len;
            memset(dst, *src++, FFMIN(len, dst_size));
        } else {
            memcpy(dst, src, FFMIN(len, dst_size));
            src += len;
        }
        dst += len;
        dst_size -= len;
    }
    return src;
}

static const unsigned char *seq_decode_op1(SeqVideoContext *seq, const unsigned char *src, unsigned char *dst)
{
    const unsigned char *color_table;
    int b, i, len, bits;
    GetBitContext gb;
    unsigned char block[8 * 8];

    len = *src++;
    if (len & 0x80) {
        switch (len & 3) {
        case 1:
            src = seq_unpack_rle_block(src, block, sizeof(block));
            for (b = 0; b < 8; b++) {
                memcpy(dst, &block[b * 8], 8);
                dst += seq->frame.linesize[0];
            }
            break;
        case 2:
            src = seq_unpack_rle_block(src, block, sizeof(block));
            for (i = 0; i < 8; i++) {
                for (b = 0; b < 8; b++)
                    dst[b * seq->frame.linesize[0]] = block[i * 8 + b];
                ++dst;
            }
            break;
        }
    } else {
        color_table = src;
        src += len;
        bits = ff_log2_tab[len - 1] + 1;
        init_get_bits(&gb, src, bits * 8 * 8); src += bits * 8;
        for (b = 0; b < 8; b++) {
            for (i = 0; i < 8; i++)
                dst[i] = color_table[get_bits(&gb, bits)];
            dst += seq->frame.linesize[0];
        }
    }

    return src;
}

static const unsigned char *seq_decode_op2(SeqVideoContext *seq, const unsigned char *src, unsigned char *dst)
{
    int i;

    for (i = 0; i < 8; i++) {
        memcpy(dst, src, 8);
        src += 8;
        dst += seq->frame.linesize[0];
    }

    return src;
}

static const unsigned char *seq_decode_op3(SeqVideoContext *seq, const unsigned char *src, unsigned char *dst)
{
    int pos, offset;

    do {
        pos = *src++;
        offset = ((pos >> 3) & 7) * seq->frame.linesize[0] + (pos & 7);
        dst[offset] = *src++;
    } while (!(pos & 0x80));

    return src;
}

static void seqvideo_decode(SeqVideoContext *seq, const unsigned char *data, int data_size)
{
    GetBitContext gb;
    int flags, i, j, x, y, op;
    unsigned char c[3];
    unsigned char *dst;
    uint32_t *palette;

    flags = *data++;

    if (flags & 1) {
        palette = (uint32_t *)seq->frame.data[1];
        for (i = 0; i < 256; i++) {
            for (j = 0; j < 3; j++, data++)
                c[j] = (*data << 2) | (*data >> 4);
            palette[i] = AV_RB24(c);
        }
        seq->frame.palette_has_changed = 1;
    }

    if (flags & 2) {
        init_get_bits(&gb, data, 128 * 8); data += 128;
        for (y = 0; y < 128; y += 8)
            for (x = 0; x < 256; x += 8) {
                dst = &seq->frame.data[0][y * seq->frame.linesize[0] + x];
                op = get_bits(&gb, 2);
                switch (op) {
                case 1:
                    data = seq_decode_op1(seq, data, dst);
                    break;
                case 2:
                    data = seq_decode_op2(seq, data, dst);
                    break;
                case 3:
                    data = seq_decode_op3(seq, data, dst);
                    break;
                }
            }
    }
}

static av_cold int seqvideo_decode_init(AVCodecContext *avctx)
{
    SeqVideoContext *seq = avctx->priv_data;

    seq->avctx = avctx;
    avctx->pix_fmt = PIX_FMT_PAL8;

    seq->frame.data[0] = NULL;

    return 0;
}

static int seqvideo_decode_frame(AVCodecContext *avctx,
                                 void *data, int *data_size,
                                 AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;

    SeqVideoContext *seq = avctx->priv_data;

    seq->frame.reference = 1;
    seq->frame.buffer_hints = FF_BUFFER_HINTS_VALID | FF_BUFFER_HINTS_PRESERVE | FF_BUFFER_HINTS_REUSABLE;
    if (avctx->reget_buffer(avctx, &seq->frame)) {
        av_log(seq->avctx, AV_LOG_ERROR, "tiertexseqvideo: reget_buffer() failed\n");
        return -1;
    }

    seqvideo_decode(seq, buf, buf_size);

    *data_size = sizeof(AVFrame);
    *(AVFrame *)data = seq->frame;

    return buf_size;
}

static av_cold int seqvideo_decode_end(AVCodecContext *avctx)
{
    SeqVideoContext *seq = avctx->priv_data;

    if (seq->frame.data[0])
        avctx->release_buffer(avctx, &seq->frame);

    return 0;
}

AVCodec tiertexseqvideo_decoder = {
    "tiertexseqvideo",
    AVMEDIA_TYPE_VIDEO,
    CODEC_ID_TIERTEXSEQVIDEO,
    sizeof(SeqVideoContext),
    seqvideo_decode_init,
    NULL,
    seqvideo_decode_end,
    seqvideo_decode_frame,
    CODEC_CAP_DR1,
    .long_name = NULL_IF_CONFIG_SMALL("Tiertex Limited SEQ video"),
};
