/*
 * ClearVideo decoder
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
 * ClearVideo decoder
 */

#include "avcodec.h"
#include "idctdsp.h"
#include "internal.h"
#include "get_bits.h"
#include "bytestream.h"

#define NUM_DC_CODES 127
#define NUM_AC_CODES 103

static const uint8_t clv_dc_codes[NUM_DC_CODES] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x10, 0x11, 0x12, 0x13, 0x13, 0x14, 0x07, 0x0B,
    0x0C, 0x08, 0x08, 0x09, 0x04, 0x06, 0x07, 0x05,
    0x04, 0x05, 0x04, 0x06, 0x05, 0x06, 0x07, 0x05,
    0x06, 0x07, 0x06, 0x07, 0x08, 0x06, 0x07, 0x08,
    0x09, 0x0A, 0x0B, 0x07, 0x08, 0x09, 0x07, 0x08,
    0x06, 0x07, 0x08, 0x06, 0x04, 0x05, 0x02, 0x01,
    0x03, 0x06, 0x07, 0x07, 0x09, 0x0A, 0x0B, 0x09,
    0x0A, 0x0B, 0x0A, 0x0B, 0x0C, 0x0D, 0x0C, 0x09,
    0x0D, 0x0A, 0x0B, 0x08, 0x09, 0x0A, 0x0B, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x06, 0x07, 0x06, 0x08,
    0x07, 0x09, 0x0A, 0x0B, 0x09, 0x0A, 0x0B, 0x0C,
    0x14, 0x0D, 0x0D, 0x0E, 0x0F, 0x15, 0x15, 0x16,
    0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E,
    0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25,
};

static const uint8_t clv_dc_bits[NUM_DC_CODES] = {
    22, 22, 22, 22, 22, 22, 22, 22,
    22, 22, 22, 22, 22, 22, 22, 22,
    22, 22, 22, 21, 22, 22, 19, 20,
    20, 19, 18, 18, 15, 17, 17, 16,
    14, 15, 12, 13, 14, 14, 14, 12,
    12, 12, 11, 11, 11, 10, 10, 10,
    10, 10, 10,  9,  9,  9,  8,  8,
     7,  7,  7,  6,  5,  5,  3,  1,
     3,  5,  5,  6,  7,  7,  7,  8,
     8,  8,  9,  9,  9,  9, 10, 11,
    10, 11, 11, 12, 12, 12, 12, 13,
    14, 14, 14, 14, 15, 15, 16, 17,
    16, 17, 18, 18, 19, 19, 19, 19,
    21, 19, 20, 19, 19, 21, 22, 22,
    22, 22, 22, 22, 22, 22, 22, 22,
    22, 22, 22, 22, 22, 22, 22,
};

static const uint16_t clv_ac_syms[NUM_AC_CODES] = {
    0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006, 0x0007, 0x0008,
    0x0009, 0x000A, 0x000B, 0x000C, 0x0011, 0x0012, 0x0013, 0x0014,
    0x0015, 0x0016, 0x0021, 0x0022, 0x0023, 0x0024, 0x0031, 0x0032,
    0x0033, 0x0041, 0x0042, 0x0043, 0x0051, 0x0052, 0x0053, 0x0061,
    0x0062, 0x0063, 0x0071, 0x0072, 0x0081, 0x0082, 0x0091, 0x0092,
    0x00A1, 0x00A2, 0x00B1, 0x00C1, 0x00D1, 0x00E1, 0x00F1, 0x0101,
    0x0111, 0x0121, 0x0131, 0x0141, 0x0151, 0x0161, 0x0171, 0x0181,
    0x0191, 0x01A1, 0x1001, 0x1002, 0x1003, 0x1011, 0x1012, 0x1021,
    0x1031, 0x1041, 0x1051, 0x1061, 0x1071, 0x1081, 0x1091, 0x10A1,
    0x10B1, 0x10C1, 0x10D1, 0x10E1, 0x10F1, 0x1101, 0x1111, 0x1121,
    0x1131, 0x1141, 0x1151, 0x1161, 0x1171, 0x1181, 0x1191, 0x11A1,
    0x11B1, 0x11C1, 0x11D1, 0x11E1, 0x11F1, 0x1201, 0x1211, 0x1221,
    0x1231, 0x1241, 0x1251, 0x1261, 0x1271, 0x1281, 0x1BFF,
};

static const uint8_t clv_ac_codes[NUM_AC_CODES] = {
    0x02, 0x0F, 0x15, 0x17, 0x1F, 0x25, 0x24, 0x21,
    0x20, 0x07, 0x06, 0x20, 0x06, 0x14, 0x1E, 0x0F,
    0x21, 0x50, 0x0E, 0x1D, 0x0E, 0x51, 0x0D, 0x23,
    0x0D, 0x0C, 0x22, 0x52, 0x0B, 0x0C, 0x53, 0x13,
    0x0B, 0x54, 0x12, 0x0A, 0x11, 0x09, 0x10, 0x08,
    0x16, 0x55, 0x15, 0x14, 0x1C, 0x1B, 0x21, 0x20,
    0x1F, 0x1E, 0x1D, 0x1C, 0x1B, 0x1A, 0x22, 0x23,
    0x56, 0x57, 0x07, 0x19, 0x05, 0x0F, 0x04, 0x0E,
    0x0D, 0x0C, 0x13, 0x12, 0x11, 0x10, 0x1A, 0x19,
    0x18, 0x17, 0x16, 0x15, 0x14, 0x13, 0x18, 0x17,
    0x16, 0x15, 0x14, 0x13, 0x12, 0x11, 0x07, 0x06,
    0x05, 0x04, 0x24, 0x25, 0x26, 0x27, 0x58, 0x59,
    0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F, 0x03,
};

static const uint8_t clv_ac_bits[NUM_AC_CODES] = {
     2,  4,  6,  7,  8,  9,  9, 10,
    10, 11, 11, 11,  3,  6,  8, 10,
    11, 12,  4,  8, 10, 12,  5,  9,
    10,  5,  9, 12,  5, 10, 12,  6,
    10, 12,  6, 10,  6, 10,  6, 10,
     7, 12,  7,  7,  8,  8,  9,  9,
     9,  9,  9,  9,  9,  9, 11, 11,
    12, 12,  4,  9, 11,  6, 11,  6,
     6,  6,  7,  7,  7,  7,  8,  8,
     8,  8,  8,  8,  8,  8,  9,  9,
     9,  9,  9,  9,  9,  9, 10, 10,
    10, 10, 11, 11, 11, 11, 12, 12,
    12, 12, 12, 12, 12, 12,  7,
};

typedef struct CLVContext {
    AVCodecContext *avctx;
    IDCTDSPContext idsp;
    AVFrame        *pic;
    GetBitContext  gb;
    int            mb_width, mb_height;
    VLC            dc_vlc, ac_vlc;
    int            luma_dc_quant, chroma_dc_quant, ac_quant;
    DECLARE_ALIGNED(16, int16_t, block)[64];
    int            top_dc[3], left_dc[4];
} CLVContext;

static inline int decode_block(CLVContext *ctx, int16_t *blk, int has_ac,
                               int ac_quant)
{
    GetBitContext *gb = &ctx->gb;
    int idx = 1, last = 0, val, skip;

    memset(blk, 0, sizeof(*blk) * 64);
    blk[0] = get_vlc2(gb, ctx->dc_vlc.table, 9, 3);
    if (blk[0] < 0)
        return AVERROR_INVALIDDATA;
    blk[0] -= 63;

    if (!has_ac)
        return 0;

    while (idx < 64 && !last) {
        val = get_vlc2(gb, ctx->ac_vlc.table, 9, 2);
        if (val < 0)
            return AVERROR_INVALIDDATA;
        if (val != 0x1BFF) {
            last =  val >> 12;
            skip = (val >> 4) & 0xFF;
            val &= 0xF;
            if (get_bits1(gb))
                val = -val;
        } else {
            last = get_bits1(gb);
            skip = get_bits(gb, 6);
            val  = get_sbits(gb, 8);
        }
        if (val) {
            int aval = FFABS(val), sign = val < 0;
            val = ac_quant * (2 * aval + 1);
            if (!(ac_quant & 1))
                val--;
            if (sign)
                val = -val;
        }
        idx += skip;
        if (idx >= 64)
            return AVERROR_INVALIDDATA;
        blk[ff_zigzag_direct[idx++]] = val;
    }

    return (idx <= 64 && last) ? 0 : -1;
}

#define DCT_TEMPLATE(blk, step, bias, shift, dshift, OP)                \
    const int t0 = OP( 2841 * blk[1 * step] +  565 * blk[7 * step]);    \
    const int t1 = OP(  565 * blk[1 * step] - 2841 * blk[7 * step]);    \
    const int t2 = OP( 1609 * blk[5 * step] + 2408 * blk[3 * step]);    \
    const int t3 = OP( 2408 * blk[5 * step] - 1609 * blk[3 * step]);    \
    const int t4 = OP( 1108 * blk[2 * step] - 2676 * blk[6 * step]);    \
    const int t5 = OP( 2676 * blk[2 * step] + 1108 * blk[6 * step]);    \
    const int t6 = ((blk[0 * step] + blk[4 * step]) * (1 << dshift)) + bias;  \
    const int t7 = ((blk[0 * step] - blk[4 * step]) * (1 << dshift)) + bias;  \
    const int t8 = t0 + t2;                                             \
    const int t9 = t0 - t2;                                             \
    const int tA = (int)(181U * (t9 + (t1 - t3)) + 0x80) >> 8;          \
    const int tB = (int)(181U * (t9 - (t1 - t3)) + 0x80) >> 8;          \
    const int tC = t1 + t3;                                             \
                                                                        \
    blk[0 * step] = (t6 + t5 + t8) >> shift;                            \
    blk[1 * step] = (t7 + t4 + tA) >> shift;                            \
    blk[2 * step] = (t7 - t4 + tB) >> shift;                            \
    blk[3 * step] = (t6 - t5 + tC) >> shift;                            \
    blk[4 * step] = (t6 - t5 - tC) >> shift;                            \
    blk[5 * step] = (t7 - t4 - tB) >> shift;                            \
    blk[6 * step] = (t7 + t4 - tA) >> shift;                            \
    blk[7 * step] = (t6 + t5 - t8) >> shift;                            \

#define ROP(x) x
#define COP(x) (((x) + 4) >> 3)

static void clv_dct(int16_t *block)
{
    int i;
    int16_t *ptr;

    ptr = block;
    for (i = 0; i < 8; i++) {
        DCT_TEMPLATE(ptr, 1, 0x80, 8, 11, ROP);
        ptr += 8;
    }

    ptr = block;
    for (i = 0; i < 8; i++) {
        DCT_TEMPLATE(ptr, 8, 0x2000, 14, 8, COP);
        ptr++;
    }
}

static int decode_mb(CLVContext *c, int x, int y)
{
    int i;
    int has_ac[6];
    int off;

    for (i = 0; i < 6; i++)
        has_ac[i] = get_bits1(&c->gb);

    off = x * 16 + y * 16 * c->pic->linesize[0];
    for (i = 0; i < 4; i++) {
        if (decode_block(c, c->block, has_ac[i], c->ac_quant) < 0)
            return AVERROR_INVALIDDATA;
        if (!x && !(i & 1)) {
            c->block[0] += c->top_dc[0];
            c->top_dc[0] = c->block[0];
        } else {
            c->block[0] += c->left_dc[(i & 2) >> 1];
        }
        c->left_dc[(i & 2) >> 1] = c->block[0];
        c->block[0]             *= c->luma_dc_quant;
        clv_dct(c->block);
        if (i == 2)
            off += c->pic->linesize[0] * 8;
        c->idsp.put_pixels_clamped(c->block, c->pic->data[0] + off + (i & 1) * 8,
                                   c->pic->linesize[0]);
    }

    off = x * 8 + y * 8 * c->pic->linesize[1];
    for (i = 1; i < 3; i++) {
        if (decode_block(c, c->block, has_ac[i + 3], c->ac_quant) < 0)
            return AVERROR_INVALIDDATA;
        if (!x) {
            c->block[0] += c->top_dc[i];
            c->top_dc[i] = c->block[0];
        } else {
            c->block[0] += c->left_dc[i + 1];
        }
        c->left_dc[i + 1] = c->block[0];
        c->block[0]      *= c->chroma_dc_quant;
        clv_dct(c->block);
        c->idsp.put_pixels_clamped(c->block, c->pic->data[i] + off,
                                   c->pic->linesize[i]);
    }

    return 0;
}

static int clv_decode_frame(AVCodecContext *avctx, void *data,
                            int *got_frame, AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    CLVContext *c = avctx->priv_data;
    GetByteContext gb;
    uint32_t frame_type;
    int i, j;
    int ret;
    int mb_ret = 0;

    bytestream2_init(&gb, buf, buf_size);
    if (avctx->codec_tag == MKTAG('C','L','V','1')) {
        int skip = bytestream2_get_byte(&gb);
        bytestream2_skip(&gb, (skip + 1) * 8);
    }

    frame_type = bytestream2_get_byte(&gb);
    if ((ret = ff_reget_buffer(avctx, c->pic)) < 0)
        return ret;

    c->pic->key_frame = frame_type & 0x20 ? 1 : 0;
    c->pic->pict_type = frame_type & 0x20 ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_P;

    if (frame_type & 0x2) {
        if (buf_size < c->mb_width * c->mb_height) {
            av_log(avctx, AV_LOG_ERROR, "Packet too small\n");
            return AVERROR_INVALIDDATA;
        }

        bytestream2_get_be32(&gb); // frame size;
        c->ac_quant        = bytestream2_get_byte(&gb);
        c->luma_dc_quant   = 32;
        c->chroma_dc_quant = 32;

        if ((ret = init_get_bits8(&c->gb, buf + bytestream2_tell(&gb),
                                  (buf_size - bytestream2_tell(&gb)))) < 0)
            return ret;

        for (i = 0; i < 3; i++)
            c->top_dc[i] = 32;
        for (i = 0; i < 4; i++)
            c->left_dc[i] = 32;

        for (j = 0; j < c->mb_height; j++) {
            for (i = 0; i < c->mb_width; i++) {
                ret = decode_mb(c, i, j);
                if (ret < 0)
                    mb_ret = ret;
            }
        }
    } else {
    }

    if ((ret = av_frame_ref(data, c->pic)) < 0)
        return ret;

    *got_frame = 1;

    return mb_ret < 0 ? mb_ret : buf_size;
}

static av_cold int clv_decode_init(AVCodecContext *avctx)
{
    CLVContext * const c = avctx->priv_data;
    int ret;

    c->avctx = avctx;

    avctx->pix_fmt = AV_PIX_FMT_YUV420P;

    c->pic = av_frame_alloc();
    if (!c->pic)
        return AVERROR(ENOMEM);

    c->mb_width  = FFALIGN(avctx->width,  16) >> 4;
    c->mb_height = FFALIGN(avctx->height, 16) >> 4;

    ff_idctdsp_init(&c->idsp, avctx);
    ret = init_vlc(&c->dc_vlc, 9, NUM_DC_CODES,
                   clv_dc_bits,  1, 1,
                   clv_dc_codes, 1, 1, 0);
    if (ret) {
        av_log(avctx, AV_LOG_ERROR, "Error initialising DC VLC\n");
        return ret;
    }
    ret = ff_init_vlc_sparse(&c->ac_vlc, 9, NUM_AC_CODES,
                             clv_ac_bits,  1, 1,
                             clv_ac_codes, 1, 1,
                             clv_ac_syms,  2, 2, 0);
    if (ret) {
        av_log(avctx, AV_LOG_ERROR, "Error initialising AC VLC\n");
        return ret;
    }

    return 0;
}

static av_cold int clv_decode_end(AVCodecContext *avctx)
{
    CLVContext * const c = avctx->priv_data;

    av_frame_free(&c->pic);

    ff_free_vlc(&c->dc_vlc);
    ff_free_vlc(&c->ac_vlc);

    return 0;
}

AVCodec ff_clearvideo_decoder = {
    .name           = "clearvideo",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_CLEARVIDEO,
    .priv_data_size = sizeof(CLVContext),
    .init           = clv_decode_init,
    .close          = clv_decode_end,
    .decode         = clv_decode_frame,
    .capabilities   = AV_CODEC_CAP_DR1,
    .long_name      = NULL_IF_CONFIG_SMALL("Iterated Systems ClearVideo"),
};
