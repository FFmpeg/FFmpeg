/*
 * Wing Commander/Xan Video Decoder
 * Copyright (C) 2011 Konstantin Shishkov
 * based on work by Mike Melanson
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

#include "avcodec.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"
#include "bytestream.h"
#define BITSTREAM_READER_LE
#include "get_bits.h"
#include "internal.h"

typedef struct XanContext {
    AVCodecContext *avctx;
    AVFrame *pic;

    uint8_t *y_buffer;
    uint8_t *scratch_buffer;
    int     buffer_size;
    GetByteContext gb;
} XanContext;

static av_cold int xan_decode_end(AVCodecContext *avctx)
{
    XanContext *s = avctx->priv_data;

    av_frame_free(&s->pic);

    av_freep(&s->y_buffer);
    av_freep(&s->scratch_buffer);

    return 0;
}

static av_cold int xan_decode_init(AVCodecContext *avctx)
{
    XanContext *s = avctx->priv_data;

    s->avctx = avctx;

    avctx->pix_fmt = AV_PIX_FMT_YUV420P;

    if (avctx->height < 8) {
        av_log(avctx, AV_LOG_ERROR, "Invalid frame height: %d.\n", avctx->height);
        return AVERROR(EINVAL);
    }
    if (avctx->width & 1) {
        av_log(avctx, AV_LOG_ERROR, "Invalid frame width: %d.\n", avctx->width);
        return AVERROR(EINVAL);
    }

    s->buffer_size = avctx->width * avctx->height;
    s->y_buffer = av_malloc(s->buffer_size);
    if (!s->y_buffer)
        return AVERROR(ENOMEM);
    s->scratch_buffer = av_malloc(s->buffer_size + 130);
    if (!s->scratch_buffer) {
        av_freep(&s->y_buffer);
        return AVERROR(ENOMEM);
    }

    s->pic = av_frame_alloc();
    if (!s->pic) {
        xan_decode_end(avctx);
        return AVERROR(ENOMEM);
    }

    return 0;
}

static int xan_unpack_luma(XanContext *s,
                           uint8_t *dst, const int dst_size)
{
    int tree_size, eof;
    int bits, mask;
    int tree_root, node;
    const uint8_t *dst_end = dst + dst_size;
    GetByteContext tree = s->gb;
    int start_off = bytestream2_tell(&tree);

    tree_size = bytestream2_get_byte(&s->gb);
    eof       = bytestream2_get_byte(&s->gb);
    tree_root = eof + tree_size;
    bytestream2_skip(&s->gb, tree_size * 2);

    node = tree_root;
    bits = bytestream2_get_byte(&s->gb);
    mask = 0x80;
    for (;;) {
        int bit = !!(bits & mask);
        mask >>= 1;
        bytestream2_seek(&tree, start_off + node*2 + bit - eof * 2, SEEK_SET);
        node = bytestream2_get_byte(&tree);
        if (node == eof)
            break;
        if (node < eof) {
            *dst++ = node;
            if (dst > dst_end)
                break;
            node = tree_root;
        }
        if (!mask) {
            if (bytestream2_get_bytes_left(&s->gb) <= 0)
                break;
            bits = bytestream2_get_byteu(&s->gb);
            mask = 0x80;
        }
    }
    return dst != dst_end ? AVERROR_INVALIDDATA : 0;
}

/* almost the same as in xan_wc3 decoder */
static int xan_unpack(XanContext *s,
                      uint8_t *dest, const int dest_len)
{
    uint8_t opcode;
    int size;
    uint8_t *orig_dest = dest;
    const uint8_t *dest_end = dest + dest_len;

    while (dest < dest_end) {
        if (bytestream2_get_bytes_left(&s->gb) <= 0)
            return AVERROR_INVALIDDATA;

        opcode = bytestream2_get_byteu(&s->gb);

        if (opcode < 0xe0) {
            int size2, back;
            if ((opcode & 0x80) == 0) {
                size  = opcode & 3;
                back  = ((opcode & 0x60) << 3) + bytestream2_get_byte(&s->gb) + 1;
                size2 = ((opcode & 0x1c) >> 2) + 3;
            } else if ((opcode & 0x40) == 0) {
                size  = bytestream2_peek_byte(&s->gb) >> 6;
                back  = (bytestream2_get_be16(&s->gb) & 0x3fff) + 1;
                size2 = (opcode & 0x3f) + 4;
            } else {
                size  = opcode & 3;
                back  = ((opcode & 0x10) << 12) + bytestream2_get_be16(&s->gb) + 1;
                size2 = ((opcode & 0x0c) <<  6) + bytestream2_get_byte(&s->gb) + 5;
                if (size + size2 > dest_end - dest)
                    break;
            }
            if (dest + size + size2 > dest_end ||
                dest - orig_dest + size < back)
                return AVERROR_INVALIDDATA;
            bytestream2_get_buffer(&s->gb, dest, size);
            dest += size;
            av_memcpy_backptr(dest, back, size2);
            dest += size2;
        } else {
            int finish = opcode >= 0xfc;

            size = finish ? opcode & 3 : ((opcode & 0x1f) << 2) + 4;
            if (dest_end - dest < size)
                return AVERROR_INVALIDDATA;
            bytestream2_get_buffer(&s->gb, dest, size);
            dest += size;
            if (finish)
                break;
        }
    }
    return dest - orig_dest;
}

static int xan_decode_chroma(AVCodecContext *avctx, unsigned chroma_off)
{
    XanContext *s = avctx->priv_data;
    uint8_t *U, *V;
    int val, uval, vval;
    int i, j;
    const uint8_t *src, *src_end;
    const uint8_t *table;
    int mode, offset, dec_size, table_size;

    if (!chroma_off)
        return 0;
    if (chroma_off + 4 >= bytestream2_get_bytes_left(&s->gb)) {
        av_log(avctx, AV_LOG_ERROR, "Invalid chroma block position\n");
        return AVERROR_INVALIDDATA;
    }
    bytestream2_seek(&s->gb, chroma_off + 4, SEEK_SET);
    mode        = bytestream2_get_le16(&s->gb);
    table       = s->gb.buffer;
    table_size  = bytestream2_get_le16(&s->gb);
    offset      = table_size * 2;
    table_size += 1;

    if (offset >= bytestream2_get_bytes_left(&s->gb)) {
        av_log(avctx, AV_LOG_ERROR, "Invalid chroma block offset\n");
        return AVERROR_INVALIDDATA;
    }

    bytestream2_skip(&s->gb, offset);
    memset(s->scratch_buffer, 0, s->buffer_size);
    dec_size = xan_unpack(s, s->scratch_buffer, s->buffer_size);
    if (dec_size < 0) {
        av_log(avctx, AV_LOG_ERROR, "Chroma unpacking failed\n");
        return dec_size;
    }

    U = s->pic->data[1];
    V = s->pic->data[2];
    src     = s->scratch_buffer;
    src_end = src + dec_size;
    if (mode) {
        for (j = 0; j < avctx->height >> 1; j++) {
            for (i = 0; i < avctx->width >> 1; i++) {
                val = *src++;
                if (val && val < table_size) {
                    val  = AV_RL16(table + (val << 1));
                    uval = (val >> 3) & 0xF8;
                    vval = (val >> 8) & 0xF8;
                    U[i] = uval | (uval >> 5);
                    V[i] = vval | (vval >> 5);
                }
                if (src == src_end)
                    return 0;
            }
            U += s->pic->linesize[1];
            V += s->pic->linesize[2];
        }
        if (avctx->height & 1) {
            memcpy(U, U - s->pic->linesize[1], avctx->width >> 1);
            memcpy(V, V - s->pic->linesize[2], avctx->width >> 1);
        }
    } else {
        uint8_t *U2 = U + s->pic->linesize[1];
        uint8_t *V2 = V + s->pic->linesize[2];

        for (j = 0; j < avctx->height >> 2; j++) {
            for (i = 0; i < avctx->width >> 1; i += 2) {
                val = *src++;
                if (val && val < table_size) {
                    val  = AV_RL16(table + (val << 1));
                    uval = (val >> 3) & 0xF8;
                    vval = (val >> 8) & 0xF8;
                    U[i] = U[i+1] = U2[i] = U2[i+1] = uval | (uval >> 5);
                    V[i] = V[i+1] = V2[i] = V2[i+1] = vval | (vval >> 5);
                }
            }
            U  += s->pic->linesize[1] * 2;
            V  += s->pic->linesize[2] * 2;
            U2 += s->pic->linesize[1] * 2;
            V2 += s->pic->linesize[2] * 2;
        }
        if (avctx->height & 3) {
            int lines = ((avctx->height + 1) >> 1) - (avctx->height >> 2) * 2;

            memcpy(U, U - lines * s->pic->linesize[1], lines * s->pic->linesize[1]);
            memcpy(V, V - lines * s->pic->linesize[2], lines * s->pic->linesize[2]);
        }
    }

    return 0;
}

static int xan_decode_frame_type0(AVCodecContext *avctx)
{
    XanContext *s = avctx->priv_data;
    uint8_t *ybuf, *prev_buf, *src = s->scratch_buffer;
    unsigned  chroma_off, corr_off;
    int cur, last;
    int i, j;
    int ret;

    chroma_off = bytestream2_get_le32(&s->gb);
    corr_off   = bytestream2_get_le32(&s->gb);

    if ((ret = xan_decode_chroma(avctx, chroma_off)) != 0)
        return ret;

    if (corr_off >= (s->gb.buffer_end - s->gb.buffer_start)) {
        av_log(avctx, AV_LOG_WARNING, "Ignoring invalid correction block position\n");
        corr_off = 0;
    }
    bytestream2_seek(&s->gb, 12, SEEK_SET);
    ret = xan_unpack_luma(s, src, s->buffer_size >> 1);
    if (ret) {
        av_log(avctx, AV_LOG_ERROR, "Luma decoding failed\n");
        return ret;
    }

    ybuf = s->y_buffer;
    last = *src++;
    ybuf[0] = last << 1;
    for (j = 1; j < avctx->width - 1; j += 2) {
        cur = (last + *src++) & 0x1F;
        ybuf[j]   = last + cur;
        ybuf[j+1] = cur << 1;
        last = cur;
    }
    ybuf[j]  = last << 1;
    prev_buf = ybuf;
    ybuf += avctx->width;

    for (i = 1; i < avctx->height; i++) {
        last = ((prev_buf[0] >> 1) + *src++) & 0x1F;
        ybuf[0] = last << 1;
        for (j = 1; j < avctx->width - 1; j += 2) {
            cur = ((prev_buf[j + 1] >> 1) + *src++) & 0x1F;
            ybuf[j]   = last + cur;
            ybuf[j+1] = cur << 1;
            last = cur;
        }
        ybuf[j] = last << 1;
        prev_buf = ybuf;
        ybuf += avctx->width;
    }

    if (corr_off) {
        int dec_size;

        bytestream2_seek(&s->gb, 8 + corr_off, SEEK_SET);
        dec_size = xan_unpack(s, s->scratch_buffer, s->buffer_size / 2);
        if (dec_size < 0)
            dec_size = 0;
        for (i = 0; i < dec_size; i++)
            s->y_buffer[i*2+1] = (s->y_buffer[i*2+1] + (s->scratch_buffer[i] << 1)) & 0x3F;
    }

    src  = s->y_buffer;
    ybuf = s->pic->data[0];
    for (j = 0; j < avctx->height; j++) {
        for (i = 0; i < avctx->width; i++)
            ybuf[i] = (src[i] << 2) | (src[i] >> 3);
        src  += avctx->width;
        ybuf += s->pic->linesize[0];
    }

    return 0;
}

static int xan_decode_frame_type1(AVCodecContext *avctx)
{
    XanContext *s = avctx->priv_data;
    uint8_t *ybuf, *src = s->scratch_buffer;
    int cur, last;
    int i, j;
    int ret;

    if ((ret = xan_decode_chroma(avctx, bytestream2_get_le32(&s->gb))) != 0)
        return ret;

    bytestream2_seek(&s->gb, 16, SEEK_SET);
    ret = xan_unpack_luma(s, src,
                          s->buffer_size >> 1);
    if (ret) {
        av_log(avctx, AV_LOG_ERROR, "Luma decoding failed\n");
        return ret;
    }

    ybuf = s->y_buffer;
    for (i = 0; i < avctx->height; i++) {
        last = (ybuf[0] + (*src++ << 1)) & 0x3F;
        ybuf[0] = last;
        for (j = 1; j < avctx->width - 1; j += 2) {
            cur = (ybuf[j + 1] + (*src++ << 1)) & 0x3F;
            ybuf[j]   = (last + cur) >> 1;
            ybuf[j+1] = cur;
            last = cur;
        }
        ybuf[j] = last;
        ybuf += avctx->width;
    }

    src = s->y_buffer;
    ybuf = s->pic->data[0];
    for (j = 0; j < avctx->height; j++) {
        for (i = 0; i < avctx->width; i++)
            ybuf[i] = (src[i] << 2) | (src[i] >> 3);
        src  += avctx->width;
        ybuf += s->pic->linesize[0];
    }

    return 0;
}

static int xan_decode_frame(AVCodecContext *avctx,
                            void *data, int *got_frame,
                            AVPacket *avpkt)
{
    XanContext *s = avctx->priv_data;
    int ftype;
    int ret;

    if ((ret = ff_reget_buffer(avctx, s->pic))) {
        av_log(s->avctx, AV_LOG_ERROR, "reget_buffer() failed\n");
        return ret;
    }

    bytestream2_init(&s->gb, avpkt->data, avpkt->size);
    ftype = bytestream2_get_le32(&s->gb);
    switch (ftype) {
    case 0:
        ret = xan_decode_frame_type0(avctx);
        break;
    case 1:
        ret = xan_decode_frame_type1(avctx);
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Unknown frame type %d\n", ftype);
        return AVERROR_INVALIDDATA;
    }
    if (ret)
        return ret;

    if ((ret = av_frame_ref(data, s->pic)) < 0)
        return ret;

    *got_frame = 1;

    return avpkt->size;
}

AVCodec ff_xan_wc4_decoder = {
    .name           = "xan_wc4",
    .long_name      = NULL_IF_CONFIG_SMALL("Wing Commander IV / Xxan"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_XAN_WC4,
    .priv_data_size = sizeof(XanContext),
    .init           = xan_decode_init,
    .close          = xan_decode_end,
    .decode         = xan_decode_frame,
    .capabilities   = CODEC_CAP_DR1,
};
