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
#include "bytestream.h"
#define BITSTREAM_READER_LE
#include "get_bits.h"
// for av_memcpy_backptr
#include "libavutil/lzo.h"

typedef struct XanContext {
    AVCodecContext *avctx;
    AVFrame pic;

    uint8_t *y_buffer;
    uint8_t *scratch_buffer;
    int     buffer_size;
} XanContext;

static av_cold int xan_decode_init(AVCodecContext *avctx)
{
    XanContext *s = avctx->priv_data;

    s->avctx = avctx;

    avctx->pix_fmt = PIX_FMT_YUV420P;

    s->buffer_size = avctx->width * avctx->height;
    s->y_buffer = av_malloc(s->buffer_size);
    if (!s->y_buffer)
        return AVERROR(ENOMEM);
    s->scratch_buffer = av_malloc(s->buffer_size + 130);
    if (!s->scratch_buffer) {
        av_freep(&s->y_buffer);
        return AVERROR(ENOMEM);
    }

    return 0;
}

static int xan_unpack_luma(const uint8_t *src, const int src_size,
                           uint8_t *dst, const int dst_size)
{
   int tree_size, eof;
   const uint8_t *tree;
   int bits, mask;
   int tree_root, node;
   const uint8_t *dst_end = dst + dst_size;
   const uint8_t *src_end = src + src_size;

   tree_size = *src++;
   eof       = *src++;
   tree      = src - eof * 2 - 2;
   tree_root = eof + tree_size;
   src += tree_size * 2;

   node = tree_root;
   bits = *src++;
   mask = 0x80;
   for (;;) {
       int bit = !!(bits & mask);
       mask >>= 1;
       node = tree[node*2 + bit];
       if (node == eof)
           break;
       if (node < eof) {
           *dst++ = node;
           if (dst > dst_end)
               break;
           node = tree_root;
       }
       if (!mask) {
           bits = *src++;
           if (src > src_end)
               break;
           mask = 0x80;
       }
   }
   return dst != dst_end;
}

/* almost the same as in xan_wc3 decoder */
static int xan_unpack(uint8_t *dest, const int dest_len,
                      const uint8_t *src, const int src_len)
{
    uint8_t opcode;
    int size;
    uint8_t *orig_dest = dest;
    const uint8_t *src_end = src + src_len;
    const uint8_t *dest_end = dest + dest_len;

    while (dest < dest_end) {
        opcode = *src++;

        if (opcode < 0xe0) {
            int size2, back;
            if ((opcode & 0x80) == 0) {
                size  = opcode & 3;
                back  = ((opcode & 0x60) << 3) + *src++ + 1;
                size2 = ((opcode & 0x1c) >> 2) + 3;
            } else if ((opcode & 0x40) == 0) {
                size  = *src >> 6;
                back  = (bytestream_get_be16(&src) & 0x3fff) + 1;
                size2 = (opcode & 0x3f) + 4;
            } else {
                size  = opcode & 3;
                back  = ((opcode & 0x10) << 12) + bytestream_get_be16(&src) + 1;
                size2 = ((opcode & 0x0c) <<  6) + *src++ + 5;
                if (size + size2 > dest_end - dest)
                    break;
            }
            if (src + size > src_end || dest + size + size2 > dest_end)
                return -1;
            bytestream_get_buffer(&src, dest, size);
            dest += size;
            av_memcpy_backptr(dest, back, size2);
            dest += size2;
        } else {
            int finish = opcode >= 0xfc;

            size = finish ? opcode & 3 : ((opcode & 0x1f) << 2) + 4;
            if (src + size > src_end || dest + size > dest_end)
                return -1;
            bytestream_get_buffer(&src, dest, size);
            dest += size;
            if (finish)
                break;
        }
    }
    return dest - orig_dest;
}

static int xan_decode_chroma(AVCodecContext *avctx, AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    XanContext *s = avctx->priv_data;
    uint8_t *U, *V;
    unsigned chroma_off;
    int val, uval, vval;
    int i, j;
    const uint8_t *src, *src_end;
    const uint8_t *table;
    int mode, offset, dec_size;

    chroma_off = AV_RL32(buf + 4);
    if (!chroma_off)
        return 0;
    if (chroma_off + 10 >= avpkt->size) {
        av_log(avctx, AV_LOG_ERROR, "Invalid chroma block position\n");
        return -1;
    }
    src    = avpkt->data + 4 + chroma_off;
    table  = src + 2;
    mode   = bytestream_get_le16(&src);
    offset = bytestream_get_le16(&src) * 2;

    if (src - avpkt->data >= avpkt->size - offset) {
        av_log(avctx, AV_LOG_ERROR, "Invalid chroma block offset\n");
        return -1;
    }

    memset(s->scratch_buffer, 0, s->buffer_size);
    dec_size = xan_unpack(s->scratch_buffer, s->buffer_size, src + offset,
                          avpkt->size - offset - (src - avpkt->data));
    if (dec_size < 0) {
        av_log(avctx, AV_LOG_ERROR, "Chroma unpacking failed\n");
        return -1;
    }

    U = s->pic.data[1];
    V = s->pic.data[2];
    src     = s->scratch_buffer;
    src_end = src + dec_size;
    if (mode) {
        for (j = 0; j < avctx->height >> 1; j++) {
            for (i = 0; i < avctx->width >> 1; i++) {
                val = *src++;
                if (val) {
                    val  = AV_RL16(table + (val << 1));
                    uval = (val >> 3) & 0xF8;
                    vval = (val >> 8) & 0xF8;
                    U[i] = uval | (uval >> 5);
                    V[i] = vval | (vval >> 5);
                }
                if (src == src_end)
                    return 0;
            }
            U += s->pic.linesize[1];
            V += s->pic.linesize[2];
        }
    } else {
        uint8_t *U2 = U + s->pic.linesize[1];
        uint8_t *V2 = V + s->pic.linesize[2];

        for (j = 0; j < avctx->height >> 2; j++) {
            for (i = 0; i < avctx->width >> 1; i += 2) {
                val = *src++;
                if (val) {
                    val  = AV_RL16(table + (val << 1));
                    uval = (val >> 3) & 0xF8;
                    vval = (val >> 8) & 0xF8;
                    U[i] = U[i+1] = U2[i] = U2[i+1] = uval | (uval >> 5);
                    V[i] = V[i+1] = V2[i] = V2[i+1] = vval | (vval >> 5);
                }
            }
            U  += s->pic.linesize[1] * 2;
            V  += s->pic.linesize[2] * 2;
            U2 += s->pic.linesize[1] * 2;
            V2 += s->pic.linesize[2] * 2;
        }
    }

    return 0;
}

static int xan_decode_frame_type0(AVCodecContext *avctx, AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    XanContext *s = avctx->priv_data;
    uint8_t *ybuf, *prev_buf, *src = s->scratch_buffer;
    unsigned  chroma_off, corr_off;
    int cur, last, size;
    int i, j;
    int ret;

    corr_off   = AV_RL32(buf + 8);
    chroma_off = AV_RL32(buf + 4);

    if ((ret = xan_decode_chroma(avctx, avpkt)) != 0)
        return ret;

    size = avpkt->size - 4;
    if (corr_off >= avpkt->size) {
        av_log(avctx, AV_LOG_WARNING, "Ignoring invalid correction block position\n");
        corr_off = 0;
    }
    if (corr_off)
        size = corr_off;
    if (chroma_off)
        size = FFMIN(size, chroma_off);
    ret = xan_unpack_luma(buf + 12, size, src, s->buffer_size >> 1);
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
        int corr_end, dec_size;

        corr_end = avpkt->size;
        if (chroma_off > corr_off)
            corr_end = chroma_off;
        dec_size = xan_unpack(s->scratch_buffer, s->buffer_size,
                              avpkt->data + 8 + corr_off,
                              corr_end - corr_off);
        if (dec_size < 0)
            dec_size = 0;
        for (i = 0; i < dec_size; i++)
            s->y_buffer[i*2+1] = (s->y_buffer[i*2+1] + (s->scratch_buffer[i] << 1)) & 0x3F;
    }

    src  = s->y_buffer;
    ybuf = s->pic.data[0];
    for (j = 0; j < avctx->height; j++) {
        for (i = 0; i < avctx->width; i++)
            ybuf[i] = (src[i] << 2) | (src[i] >> 3);
        src  += avctx->width;
        ybuf += s->pic.linesize[0];
    }

    return 0;
}

static int xan_decode_frame_type1(AVCodecContext *avctx, AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    XanContext *s = avctx->priv_data;
    uint8_t *ybuf, *src = s->scratch_buffer;
    int cur, last;
    int i, j;
    int ret;

    if ((ret = xan_decode_chroma(avctx, avpkt)) != 0)
        return ret;

    ret = xan_unpack_luma(buf + 16, avpkt->size - 16, src,
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
    ybuf = s->pic.data[0];
    for (j = 0; j < avctx->height; j++) {
        for (i = 0; i < avctx->width; i++)
            ybuf[i] = (src[i] << 2) | (src[i] >> 3);
        src  += avctx->width;
        ybuf += s->pic.linesize[0];
    }

    return 0;
}

static int xan_decode_frame(AVCodecContext *avctx,
                            void *data, int *data_size,
                            AVPacket *avpkt)
{
    XanContext *s = avctx->priv_data;
    int ftype;
    int ret;

    s->pic.reference = 1;
    s->pic.buffer_hints = FF_BUFFER_HINTS_VALID |
                          FF_BUFFER_HINTS_PRESERVE |
                          FF_BUFFER_HINTS_REUSABLE;
    if ((ret = avctx->reget_buffer(avctx, &s->pic))) {
        av_log(s->avctx, AV_LOG_ERROR, "reget_buffer() failed\n");
        return ret;
    }

    ftype = AV_RL32(avpkt->data);
    switch (ftype) {
    case 0:
        ret = xan_decode_frame_type0(avctx, avpkt);
        break;
    case 1:
        ret = xan_decode_frame_type1(avctx, avpkt);
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Unknown frame type %d\n", ftype);
        return -1;
    }
    if (ret)
        return ret;

    *data_size = sizeof(AVFrame);
    *(AVFrame*)data = s->pic;

    return avpkt->size;
}

static av_cold int xan_decode_end(AVCodecContext *avctx)
{
    XanContext *s = avctx->priv_data;

    if (s->pic.data[0])
        avctx->release_buffer(avctx, &s->pic);

    av_freep(&s->y_buffer);
    av_freep(&s->scratch_buffer);

    return 0;
}

AVCodec ff_xan_wc4_decoder = {
    .name           = "xan_wc4",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_XAN_WC4,
    .priv_data_size = sizeof(XanContext),
    .init           = xan_decode_init,
    .close          = xan_decode_end,
    .decode         = xan_decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name = NULL_IF_CONFIG_SMALL("Wing Commander IV / Xxan"),
};
