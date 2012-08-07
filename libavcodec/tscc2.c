/*
 * TechSmith Screen Codec 2 (aka Dora) decoder
 * Copyright (c) 2012 Konstantin Shishkov
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
 * TechSmith Screen Codec 2 decoder
 */

#define BITSTREAM_READER_LE
#include "avcodec.h"
#include "get_bits.h"
#include "bytestream.h"
#include "tscc2data.h"

typedef struct TSCC2Context {
    AVCodecContext *avctx;
    AVFrame        pic;
    int            mb_width, mb_height;
    uint8_t        *slice_quants;
    int            quant[2];
    int            q[2][3];
    GetBitContext  gb;

    VLC            dc_vlc, nc_vlc[NUM_VLC_SETS], ac_vlc[NUM_VLC_SETS];
    int            block[16];
} TSCC2Context;

static av_cold void free_vlcs(TSCC2Context *c)
{
    int i;

    ff_free_vlc(&c->dc_vlc);
    for (i = 0; i < NUM_VLC_SETS; i++) {
        ff_free_vlc(c->nc_vlc + i);
        ff_free_vlc(c->ac_vlc + i);
    }
}

static av_cold int init_vlcs(TSCC2Context *c)
{
    int i, ret;

    ret = ff_init_vlc_sparse(&c->dc_vlc, 9, DC_VLC_COUNT,
                             tscc2_dc_vlc_bits,  1, 1,
                             tscc2_dc_vlc_codes, 2, 2,
                             tscc2_dc_vlc_syms,  2, 2, INIT_VLC_LE);
    if (ret)
        return ret;

    for (i = 0; i < NUM_VLC_SETS; i++) {
        ret = ff_init_vlc_sparse(c->nc_vlc + i, 9, 16,
                                 tscc2_nc_vlc_bits[i],  1, 1,
                                 tscc2_nc_vlc_codes[i], 2, 2,
                                 tscc2_nc_vlc_syms,     1, 1, INIT_VLC_LE);
        if (ret) {
            free_vlcs(c);
            return ret;
        }
        ret = ff_init_vlc_sparse(c->ac_vlc + i, 9, tscc2_ac_vlc_sizes[i],
                                 tscc2_ac_vlc_bits[i],  1, 1,
                                 tscc2_ac_vlc_codes[i], 2, 2,
                                 tscc2_ac_vlc_syms[i],  2, 2, INIT_VLC_LE);
        if (ret) {
            free_vlcs(c);
            return ret;
        }
    }

    return 0;
}

#define DEQUANT(val, q) ((q * val + 0x80) >> 8)
#define DCT1D(d0, d1, d2, d3, s0, s1, s2, s3, OP) \
    OP(d0, 5 * ((s0) + (s1) + (s2)) + 2 * (s3));  \
    OP(d1, 5 * ((s0) - (s2) - (s3)) + 2 * (s1));  \
    OP(d2, 5 * ((s0) - (s2) + (s3)) - 2 * (s1));  \
    OP(d3, 5 * ((s0) - (s1) + (s2)) - 2 * (s3));  \

#define COL_OP(a, b)  a = b
#define ROW_OP(a, b)  a = ((b) + 0x20) >> 6

static void tscc2_idct4_put(int *in, int q[3], uint8_t *dst, int stride)
{
    int i;
    int tblk[4 * 4];
    int t0, t1, t2, t3;

    for (i = 0; i < 4; i++) {
        t0 = DEQUANT(q[0 + (i & 1)], in[0 * 4 + i]);
        t1 = DEQUANT(q[1 + (i & 1)], in[1 * 4 + i]);
        t2 = DEQUANT(q[0 + (i & 1)], in[2 * 4 + i]);
        t3 = DEQUANT(q[1 + (i & 1)], in[3 * 4 + i]);
        DCT1D(tblk[0 * 4 + i], tblk[1 * 4 + i],
              tblk[2 * 4 + i], tblk[3 * 4 + i],
              t0, t1, t2, t3, COL_OP);
    }
    for (i = 0; i < 4; i++) {
        DCT1D(dst[0], dst[1], dst[2], dst[3],
              tblk[i * 4 + 0], tblk[i * 4 + 1],
              tblk[i * 4 + 2], tblk[i * 4 + 3], ROW_OP);
        dst += stride;
    }
}

static int tscc2_decode_mb(TSCC2Context *c, int *q, int vlc_set,
                           uint8_t *dst, int stride, int plane)
{
    GetBitContext *gb = &c->gb;
    int prev_dc, dc, nc, ac, bpos, val;
    int i, j, k, l;

    if (get_bits1(gb)) {
        if (get_bits1(gb)) {
            val = get_bits(gb, 8);
            for (i = 0; i < 8; i++, dst += stride)
                memset(dst, val, 16);
        } else {
            if (get_bits_left(gb) < 16 * 8 * 8)
                return AVERROR_INVALIDDATA;
            for (i = 0; i < 8; i++) {
                for (j = 0; j < 16; j++)
                    dst[j] = get_bits(gb, 8);
                dst += stride;
            }
        }
        return 0;
    }

    prev_dc = 0;
    for (j = 0; j < 2; j++) {
        for (k = 0; k < 4; k++) {
            if (!(j | k)) {
                dc = get_bits(gb, 8);
            } else {
                dc = get_vlc2(gb, c->dc_vlc.table, 9, 2);
                if (dc == -1)
                    return AVERROR_INVALIDDATA;
                if (dc == 0x100)
                    dc = get_bits(gb, 8);
            }
            dc          = (dc + prev_dc) & 0xFF;
            prev_dc     = dc;
            c->block[0] = dc;

            nc = get_vlc2(gb, c->nc_vlc[vlc_set].table, 9, 1);
            if (nc == -1)
                return AVERROR_INVALIDDATA;

            bpos = 1;
            memset(c->block + 1, 0, 15 * sizeof(*c->block));
            for (l = 0; l < nc; l++) {
                ac = get_vlc2(gb, c->ac_vlc[vlc_set].table, 9, 2);
                if (ac == -1)
                    return AVERROR_INVALIDDATA;
                if (ac == 0x1000)
                    ac = get_bits(gb, 12);
                bpos += ac & 0xF;
                if (bpos >= 64)
                    return AVERROR_INVALIDDATA;
                val = sign_extend(ac >> 4, 8);
                c->block[tscc2_zigzag[bpos++]] = val;
            }
            tscc2_idct4_put(c->block, q, dst + k * 4, stride);
        }
        dst += 4 * stride;
    }
    return 0;
}

static int tscc2_decode_slice(TSCC2Context *c, int mb_y,
                              const uint8_t *buf, int buf_size)
{
    int i, mb_x, q, ret;
    int off;

    init_get_bits(&c->gb, buf, buf_size * 8);

    for (mb_x = 0; mb_x < c->mb_width; mb_x++) {
        q = c->slice_quants[mb_x + c->mb_width * mb_y];

        if (q == 0 || q == 3) // skip block
            continue;
        for (i = 0; i < 3; i++) {
            off = mb_x * 16 + mb_y * 8 * c->pic.linesize[i];
            ret = tscc2_decode_mb(c, c->q[q - 1], c->quant[q - 1] - 2,
                                  c->pic.data[i] + off, c->pic.linesize[i], i);
            if (ret)
                return ret;
        }
    }

    return 0;
}

static int tscc2_decode_frame(AVCodecContext *avctx, void *data,
                              int *data_size, AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    TSCC2Context *c = avctx->priv_data;
    GetByteContext gb;
    uint32_t frame_type, size;
    int i, val, len, pos = 0;
    int num_mb = c->mb_width * c->mb_height;
    int ret;

    bytestream2_init(&gb, buf, buf_size);
    frame_type = bytestream2_get_byte(&gb);
    if (frame_type > 1) {
        av_log(avctx, AV_LOG_ERROR, "Incorrect frame type %d\n", frame_type);
        return AVERROR_INVALIDDATA;
    }

    c->pic.reference    = 3;
    c->pic.buffer_hints = FF_BUFFER_HINTS_VALID | FF_BUFFER_HINTS_PRESERVE |
                          FF_BUFFER_HINTS_REUSABLE;
    if ((ret = avctx->reget_buffer(avctx, &c->pic)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "reget_buffer() failed\n");
        return ret;
    }

    if (frame_type == 0) {
        *data_size      = sizeof(AVFrame);
        *(AVFrame*)data = c->pic;

        return buf_size;
    }

    if (bytestream2_get_bytes_left(&gb) < 4) {
        av_log(avctx, AV_LOG_ERROR, "Frame is too short\n");
        return AVERROR_INVALIDDATA;
    }

    c->quant[0] = bytestream2_get_byte(&gb);
    c->quant[1] = bytestream2_get_byte(&gb);
    if (c->quant[0] < 2 || c->quant[0] > NUM_VLC_SETS + 1 ||
        c->quant[1] < 2 || c->quant[1] > NUM_VLC_SETS + 1) {
        av_log(avctx, AV_LOG_ERROR, "Invalid quantisers %d / %d\n",
               c->quant[0], c->quant[1]);
        return AVERROR_INVALIDDATA;
    }

    for (i = 0; i < 3; i++) {
        c->q[0][i] = tscc2_quants[c->quant[0] - 2][i];
        c->q[1][i] = tscc2_quants[c->quant[1] - 2][i];
    }

    bytestream2_skip(&gb, 1);

    size = bytestream2_get_le32(&gb);
    if (size > bytestream2_get_bytes_left(&gb)) {
        av_log(avctx, AV_LOG_ERROR, "Slice properties chunk is too large\n");
        return AVERROR_INVALIDDATA;
    }

    for (i = 0; i < size; i++) {
        val   = bytestream2_get_byte(&gb);
        len   = val & 0x3F;
        val >>= 6;
        if (pos + len > num_mb) {
            av_log(avctx, AV_LOG_ERROR, "Too many slice properties\n");
            return AVERROR_INVALIDDATA;
        }
        memset(c->slice_quants + pos, val, len);
        pos += len;
    }
    if (pos < num_mb) {
        av_log(avctx, AV_LOG_ERROR, "Too few slice properties (%d / %d)\n",
               pos, num_mb);
        return AVERROR_INVALIDDATA;
    }

    for (i = 0; i < c->mb_height; i++) {
        size = bytestream2_peek_byte(&gb);
        if (size & 1) {
            size = bytestream2_get_byte(&gb) - 1;
        } else {
            size = bytestream2_get_le32(&gb) >> 1;
        }
        if (!size) {
            int skip_row = 1, j, off = i * c->mb_width;
            for (j = 0; j < c->mb_width; j++) {
                if (c->slice_quants[off + j] == 1 ||
                    c->slice_quants[off + j] == 2) {
                    skip_row = 0;
                    break;
                }
            }
            if (!skip_row) {
                av_log(avctx, AV_LOG_ERROR, "Non-skip row with zero size\n");
                return AVERROR_INVALIDDATA;
            }
        }
        if (bytestream2_get_bytes_left(&gb) < size) {
            av_log(avctx, AV_LOG_ERROR, "Invalid slice size (%d/%d)\n",
                   size, bytestream2_get_bytes_left(&gb));
            return AVERROR_INVALIDDATA;
        }
        ret = tscc2_decode_slice(c, i, buf + bytestream2_tell(&gb), size);
        if (ret) {
            av_log(avctx, AV_LOG_ERROR, "Error decoding slice %d\n", i);
            return ret;
        }
        bytestream2_skip(&gb, size);
    }

    *data_size      = sizeof(AVFrame);
    *(AVFrame*)data = c->pic;

    /* always report that the buffer was completely consumed */
    return buf_size;
}

static av_cold int tscc2_decode_init(AVCodecContext *avctx)
{
    TSCC2Context * const c = avctx->priv_data;
    int ret;

    c->avctx = avctx;

    avctx->pix_fmt = PIX_FMT_YUV444P;

    if ((ret = init_vlcs(c)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "Cannot initialise VLCs\n");
        return ret;
    }

    c->mb_width     = FFALIGN(avctx->width,  16) >> 4;
    c->mb_height    = FFALIGN(avctx->height,  8) >> 3;
    c->slice_quants = av_malloc(c->mb_width * c->mb_height);
    if (!c->slice_quants) {
        av_log(avctx, AV_LOG_ERROR, "Cannot allocate slice information\n");
        free_vlcs(c);
        return AVERROR(ENOMEM);
    }

    avctx->coded_frame = &c->pic;

    return 0;
}

static av_cold int tscc2_decode_end(AVCodecContext *avctx)
{
    TSCC2Context * const c = avctx->priv_data;

    if (c->pic.data[0])
        avctx->release_buffer(avctx, &c->pic);
    av_freep(&c->slice_quants);
    free_vlcs(c);

    return 0;
}

AVCodec ff_tscc2_decoder = {
    .name           = "tscc2",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_TSCC2,
    .priv_data_size = sizeof(TSCC2Context),
    .init           = tscc2_decode_init,
    .close          = tscc2_decode_end,
    .decode         = tscc2_decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name      = NULL_IF_CONFIG_SMALL("TechSmith Screen Codec 2"),
};
