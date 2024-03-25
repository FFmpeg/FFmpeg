/*
 * MidiVid MV30 decoder
 *
 * Copyright (c) 2020 Paul B Mahol
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

#include <stddef.h>
#include <string.h>

#include "libavutil/mem.h"
#include "libavutil/thread.h"

#include "avcodec.h"
#include "bytestream.h"
#include "codec_internal.h"
#include "copy_block.h"
#include "decode.h"
#include "mathops.h"
#include "blockdsp.h"
#include "get_bits.h"
#include "aandcttab.h"

#define CBP_VLC_BITS  9

typedef struct MV30Context {
    GetBitContext  gb;

    int intra_quant;
    int inter_quant;
    int is_inter;
    int mode_size;
    int nb_mvectors;

    int      block[6][64];
    int16_t *mvectors;
    unsigned int mvectors_size;
    int16_t *coeffs;
    unsigned int coeffs_size;

    int16_t intraq_tab[2][64];
    int16_t interq_tab[2][64];

    BlockDSPContext bdsp;
    AVFrame *prev_frame;
} MV30Context;

static VLCElem cbp_tab[1 << CBP_VLC_BITS];

static const uint8_t luma_tab[] = {
    12, 12, 15, 19, 25, 34, 40, 48,
    12, 12, 18, 22, 27, 44, 47, 46,
    17, 18, 21, 26, 35, 46, 52, 47,
    18, 20, 24, 28, 40, 61, 59, 51,
    20, 24, 32, 43, 50, 72, 72, 63,
    25, 31, 42, 48, 58, 72, 81, 75,
    38, 46, 54, 61, 71, 84, 88, 85,
    50, 61, 65, 68, 79, 78, 86, 91,
};

static const uint8_t chroma_tab[] = {
    12, 16, 24, 47, 99, 99, 99, 99,
    16, 21, 26, 66, 99, 99, 99, 99,
    24, 26, 56, 99, 99, 99, 99, 99,
    47, 66, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
};

static const uint8_t zigzag[] = {
     0,  1,  8,  9, 16,  2,  3, 10,
    17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63,
};

static void get_qtable(int16_t *table, int quant, const uint8_t *quant_tab)
{
    int factor = quant < 50 ? 5000 / FFMAX(quant, 1) : 200 - FFMIN(quant, 100) * 2;

    for (int i = 0; i < 64; i++) {
        table[i] = av_clip((quant_tab[i] * factor + 0x32) / 100, 1, 0x7fff);
        table[i] = ((int)ff_aanscales[i] * (int)table[i] + 0x800) >> 12;
    }
}

static inline void idct_1d(unsigned *blk, int step)
{
    const unsigned t0 = blk[0 * step] + blk[4 * step];
    const unsigned t1 = blk[0 * step] - blk[4 * step];
    const unsigned t2 = blk[2 * step] + blk[6 * step];
    const unsigned t3 = ((int)((blk[2 * step] - blk[6 * step]) * 362U) >> 8) - t2;
    const unsigned t4 = t0 + t2;
    const unsigned t5 = t0 - t2;
    const unsigned t6 = t1 + t3;
    const unsigned t7 = t1 - t3;
    const unsigned t8 = blk[5 * step] + blk[3 * step];
    const unsigned t9 = blk[5 * step] - blk[3 * step];
    const unsigned tA = blk[1 * step] + blk[7 * step];
    const unsigned tB = blk[1 * step] - blk[7 * step];
    const unsigned tC = t8 + tA;
    const unsigned tD = (int)((tB + t9) * 473U) >> 8;
    const unsigned tE = (((int)(t9 * -669U) >> 8) - tC) + tD;
    const unsigned tF = ((int)((tA - t8) * 362U) >> 8) - tE;
    const unsigned t10 = (((int)(tB * 277U) >> 8) - tD) + tF;

    blk[0 * step] = t4 + tC;
    blk[1 * step] = t6 + tE;
    blk[2 * step] = t7 + tF;
    blk[3 * step] = t5 - t10;
    blk[4 * step] = t5 + t10;
    blk[5 * step] = t7 - tF;
    blk[6 * step] = t6 - tE;
    blk[7 * step] = t4 - tC;
}

static void idct_put(uint8_t *dst, int stride, int *block)
{
    for (int i = 0; i < 8; i++) {
        if ((block[0x08 + i] |
             block[0x10 + i] |
             block[0x18 + i] |
             block[0x20 + i] |
             block[0x28 + i] |
             block[0x30 + i] |
             block[0x38 + i]) == 0) {
            block[0x08 + i] = block[i];
            block[0x10 + i] = block[i];
            block[0x18 + i] = block[i];
            block[0x20 + i] = block[i];
            block[0x28 + i] = block[i];
            block[0x30 + i] = block[i];
            block[0x38 + i] = block[i];
        } else {
            idct_1d(block + i, 8);
        }
    }

    for (int i = 0; i < 8; i++) {
        idct_1d(block, 1);
        for (int j = 0; j < 8; j++)
            dst[j] = av_clip_uint8((block[j] >> 5) + 128);
        block += 8;
        dst += stride;
    }
}

static void idct_add(uint8_t *dst, int stride,
                     const uint8_t *src, int in_linesize, int *block)
{
    for (int i = 0; i < 8; i++) {
        if ((block[0x08 + i] |
             block[0x10 + i] |
             block[0x18 + i] |
             block[0x20 + i] |
             block[0x28 + i] |
             block[0x30 + i] |
             block[0x38 + i]) == 0) {
            block[0x08 + i] = block[i];
            block[0x10 + i] = block[i];
            block[0x18 + i] = block[i];
            block[0x20 + i] = block[i];
            block[0x28 + i] = block[i];
            block[0x30 + i] = block[i];
            block[0x38 + i] = block[i];
        } else {
            idct_1d(block + i, 8);
        }
    }

    for (int i = 0; i < 8; i++) {
        idct_1d(block, 1);
        for (int j = 0; j < 8; j++)
            dst[j] = av_clip_uint8((block[j] >> 5) + src[j]);
        block += 8;
        dst += stride;
        src += in_linesize;
    }
}

static inline void idct2_1d(int *blk, int step)
{
    const unsigned int  t0 = blk[0 * step];
    const unsigned int t1 = blk[1 * step];
    const unsigned int t2 = (int)(t1 * 473U) >> 8;
    const unsigned int t3 = t2 - t1;
    const unsigned int t4 =  ((int)(t1 * 362U) >> 8) - t3;
    const unsigned int t5 = (((int)(t1 * 277U) >> 8) - t2) + t4;

    blk[0 * step] = t1 + t0;
    blk[1 * step] = t0 + t3;
    blk[2 * step] = t4 + t0;
    blk[3 * step] = t0 - t5;
    blk[4 * step] = t5 + t0;
    blk[5 * step] = t0 - t4;
    blk[6 * step] = t0 - t3;
    blk[7 * step] = t0 - t1;
}

static void idct2_put(uint8_t *dst, int stride, int *block)
{
    for (int i = 0; i < 2; i++) {
        if ((block[0x08 + i]) == 0) {
            block[0x08 + i] = block[i];
            block[0x10 + i] = block[i];
            block[0x18 + i] = block[i];
            block[0x20 + i] = block[i];
            block[0x28 + i] = block[i];
            block[0x30 + i] = block[i];
            block[0x38 + i] = block[i];
        } else {
            idct2_1d(block + i, 8);
        }
    }

    for (int i = 0; i < 8; i++) {
        if (block[1] == 0) {
            for (int j = 0; j < 8; j++)
                dst[j] = av_clip_uint8((block[0] >> 5) + 128);
        } else {
            idct2_1d(block, 1);
            for (int j = 0; j < 8; j++)
                dst[j] = av_clip_uint8((block[j] >> 5) + 128);
        }
        block += 8;
        dst += stride;
    }
}

static void idct2_add(uint8_t *dst, int stride,
                      const uint8_t *src, int in_linesize,
                      int *block)
{
    for (int i = 0; i < 2; i++) {
        if ((block[0x08 + i]) == 0) {
            block[0x08 + i] = block[i];
            block[0x10 + i] = block[i];
            block[0x18 + i] = block[i];
            block[0x20 + i] = block[i];
            block[0x28 + i] = block[i];
            block[0x30 + i] = block[i];
            block[0x38 + i] = block[i];
        } else {
            idct2_1d(block + i, 8);
        }
    }

    for (int i = 0; i < 8; i++) {
        if (block[1] == 0) {
            for (int j = 0; j < 8; j++)
                dst[j] = av_clip_uint8((block[0] >> 5) + src[j]);
        } else {
            idct2_1d(block, 1);
            for (int j = 0; j < 8; j++)
                dst[j] = av_clip_uint8((block[j] >> 5) + src[j]);
        }
        block += 8;
        dst += stride;
        src += in_linesize;
    }
}

static void update_inter_block(uint8_t *dst, int stride,
                               const uint8_t *src, int in_linesize,
                               int block)
{
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++)
            dst[j] = av_clip_uint8(block + src[j]);
        dst += stride;
        src += in_linesize;
    }
}

static int decode_intra_block(AVCodecContext *avctx, int mode,
                              GetByteContext *gbyte, int16_t *qtab,
                              int *block, int *pfill,
                              uint8_t *dst, int linesize)
{
    MV30Context *s = avctx->priv_data;
    int fill;

    switch (mode) {
    case 0:
        s->bdsp.fill_block_tab[1](dst, 128, linesize, 8);
        break;
    case 1:
        fill = sign_extend(bytestream2_get_ne16(gbyte), 16);
        pfill[0] += fill;
        block[0] = ((int)((unsigned)pfill[0] * qtab[0]) >> 5) + 128;
        s->bdsp.fill_block_tab[1](dst, block[0], linesize, 8);
        break;
    case 2:
        memset(block, 0, sizeof(*block) * 64);
        fill = sign_extend(bytestream2_get_ne16(gbyte), 16);
        pfill[0] += fill;
        block[0] = (unsigned)pfill[0] * qtab[0];
        block[1] = sign_extend(bytestream2_get_ne16(gbyte), 16) * qtab[1];
        block[8] = sign_extend(bytestream2_get_ne16(gbyte), 16) * qtab[8];
        block[9] = sign_extend(bytestream2_get_ne16(gbyte), 16) * qtab[9];
        idct2_put(dst, linesize, block);
        break;
    case 3:
        fill = sign_extend(bytestream2_get_ne16(gbyte), 16);
        pfill[0] += fill;
        block[0] = (unsigned)pfill[0] * qtab[0];
        for (int i = 1; i < 64; i++)
            block[zigzag[i]] = sign_extend(bytestream2_get_ne16(gbyte), 16) * qtab[zigzag[i]];
        idct_put(dst, linesize, block);
        break;
    }

    return 0;
}

static int decode_inter_block(AVCodecContext *avctx, int mode,
                              GetByteContext *gbyte, int16_t *qtab,
                              int *block, int *pfill,
                              uint8_t *dst, int linesize,
                              const uint8_t *src, int in_linesize)
{
    int fill;

    switch (mode) {
    case 0:
        copy_block8(dst, src, linesize, in_linesize, 8);
        break;
    case 1:
        fill = sign_extend(bytestream2_get_ne16(gbyte), 16);
        pfill[0] += fill;
        block[0] = (int)((unsigned)pfill[0] * qtab[0]) >> 5;
        update_inter_block(dst, linesize, src, in_linesize, block[0]);
        break;
    case 2:
        memset(block, 0, sizeof(*block) * 64);
        fill = sign_extend(bytestream2_get_ne16(gbyte), 16);
        pfill[0] += fill;
        block[0] = (unsigned)pfill[0] * qtab[0];
        block[1] = sign_extend(bytestream2_get_ne16(gbyte), 16) * qtab[1];
        block[8] = sign_extend(bytestream2_get_ne16(gbyte), 16) * qtab[8];
        block[9] = sign_extend(bytestream2_get_ne16(gbyte), 16) * qtab[9];
        idct2_add(dst, linesize, src, in_linesize, block);
        break;
    case 3:
        fill = sign_extend(bytestream2_get_ne16(gbyte), 16);
        pfill[0] += fill;
        block[0] = (unsigned)pfill[0] * qtab[0];
        for (int i = 1; i < 64; i++)
            block[zigzag[i]] = sign_extend(bytestream2_get_ne16(gbyte), 16) * qtab[zigzag[i]];
        idct_add(dst, linesize, src, in_linesize, block);
        break;
    }

    return 0;
}

static int decode_coeffs(GetBitContext *gb, int16_t *coeffs, int nb_codes)
{
    memset(coeffs, 0, nb_codes * sizeof(*coeffs));

    for (int i = 0; i < nb_codes;) {
        int value = get_vlc2(gb, cbp_tab, CBP_VLC_BITS, 1);

        if (value > 0) {
            int x = get_bits(gb, value);

            if (x < (1 << value) / 2) {
                x =  (1 << (value - 1)) + (x & ((1 << value) - 1 >> 1));
            } else {
                x = -(1 << (value - 1)) - (x & ((1 << value) - 1 >> 1));
            }
            coeffs[i++] = x;
        } else {
            int flag = get_bits1(gb);

            i += get_bits(gb, 3 + flag * 3) + 1 + flag * 8;
        }
    }

    return 0;
}

static int decode_intra(AVCodecContext *avctx, GetBitContext *gb, AVFrame *frame)
{
    MV30Context *s = avctx->priv_data;
    GetBitContext mgb;
    uint8_t *dst[6];
    int linesize[6];
    int ret;

    mgb = *gb;
    if (get_bits_left(gb) < s->mode_size * 8)
        return AVERROR_INVALIDDATA;

    skip_bits_long(gb, s->mode_size * 8);

    linesize[0] = frame->linesize[0];
    linesize[1] = frame->linesize[0];
    linesize[2] = frame->linesize[0];
    linesize[3] = frame->linesize[0];
    linesize[4] = frame->linesize[1];
    linesize[5] = frame->linesize[2];

    for (int y = 0; y < avctx->height; y += 16) {
        GetByteContext gbyte;
        int pfill[3][1] = { {0} };
        int nb_codes = get_bits(gb, 16);

        av_fast_padded_malloc(&s->coeffs, &s->coeffs_size, nb_codes * sizeof(*s->coeffs));
        if (!s->coeffs)
            return AVERROR(ENOMEM);
        ret = decode_coeffs(gb, s->coeffs, nb_codes);
        if (ret < 0)
            return ret;

        bytestream2_init(&gbyte, (uint8_t *)s->coeffs, nb_codes * sizeof(*s->coeffs));

        for (int x = 0; x < avctx->width; x += 16) {
            dst[0] = frame->data[0] + linesize[0] * y + x;
            dst[1] = frame->data[0] + linesize[0] * y + x + 8;
            dst[2] = frame->data[0] + linesize[0] * (y + 8) + x;
            dst[3] = frame->data[0] + linesize[0] * (y + 8) + x + 8;
            dst[4] = frame->data[1] + linesize[4] * (y >> 1) + (x >> 1);
            dst[5] = frame->data[2] + linesize[5] * (y >> 1) + (x >> 1);

            for (int b = 0; b < 6; b++) {
                int mode = get_bits_le(&mgb, 2);

                ret = decode_intra_block(avctx, mode, &gbyte, s->intraq_tab[b >= 4],
                                         s->block[b],
                                         pfill[(b >= 4) + (b >= 5)],
                                         dst[b], linesize[b]);
                if (ret < 0)
                    return ret;
            }
        }
    }

    return 0;
}

static int decode_inter(AVCodecContext *avctx, GetBitContext *gb,
                        AVFrame *frame, AVFrame *prev)
{
    MV30Context *s = avctx->priv_data;
    GetBitContext mask;
    GetBitContext mgb;
    GetByteContext mv;
    const int mask_size = ((avctx->height >> 4) * (avctx->width >> 4) * 2 + 7) / 8;
    uint8_t *dst[6], *src[6];
    int in_linesize[6];
    int linesize[6];
    int ret, cnt = 0;
    int flags = 0;

    in_linesize[0] = prev->linesize[0];
    in_linesize[1] = prev->linesize[0];
    in_linesize[2] = prev->linesize[0];
    in_linesize[3] = prev->linesize[0];
    in_linesize[4] = prev->linesize[1];
    in_linesize[5] = prev->linesize[2];

    linesize[0] = frame->linesize[0];
    linesize[1] = frame->linesize[0];
    linesize[2] = frame->linesize[0];
    linesize[3] = frame->linesize[0];
    linesize[4] = frame->linesize[1];
    linesize[5] = frame->linesize[2];

    av_fast_padded_malloc(&s->mvectors, &s->mvectors_size, 2 * s->nb_mvectors * sizeof(*s->mvectors));
    if (!s->mvectors) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    mask = *gb;
    skip_bits_long(gb, mask_size * 8);
    mgb = *gb;
    skip_bits_long(gb, s->mode_size * 8);

    ret = decode_coeffs(gb, s->mvectors, 2 * s->nb_mvectors);
    if (ret < 0)
        goto fail;

    bytestream2_init(&mv, (uint8_t *)s->mvectors, 2 * s->nb_mvectors * sizeof(*s->mvectors));

    for (int y = 0; y < avctx->height; y += 16) {
        GetByteContext gbyte;
        int pfill[3][1] = { {0} };
        int nb_codes = get_bits(gb, 16);

        skip_bits(gb, 8);
        if (get_bits_left(gb) < 0) {
            ret = AVERROR_INVALIDDATA;
            goto fail;
        }

        av_fast_padded_malloc(&s->coeffs, &s->coeffs_size, nb_codes * sizeof(*s->coeffs));
        if (!s->coeffs) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        ret = decode_coeffs(gb, s->coeffs, nb_codes);
        if (ret < 0)
            goto fail;

        bytestream2_init(&gbyte, (uint8_t *)s->coeffs, nb_codes * sizeof(*s->coeffs));

        for (int x = 0; x < avctx->width; x += 16) {
            if (cnt >= 4)
                cnt = 0;
            if (cnt == 0) {
                if (get_bits_left(&mask) < 8) {
                    ret = AVERROR_INVALIDDATA;
                    goto fail;
                }
                flags = get_bits(&mask, 8);
            }

            dst[0] = frame->data[0] + linesize[0] * y + x;
            dst[1] = frame->data[0] + linesize[0] * y + x + 8;
            dst[2] = frame->data[0] + linesize[0] * (y + 8) + x;
            dst[3] = frame->data[0] + linesize[0] * (y + 8) + x + 8;
            dst[4] = frame->data[1] + linesize[4] * (y >> 1) + (x >> 1);
            dst[5] = frame->data[2] + linesize[5] * (y >> 1) + (x >> 1);

            if ((flags >> (cnt)) & 1) {
                int mv_x = sign_extend(bytestream2_get_ne16(&mv), 16);
                int mv_y = sign_extend(bytestream2_get_ne16(&mv), 16);

                int px = x + mv_x;
                int py = y + mv_y;

                if (px < 0 || px > FFALIGN(avctx->width , 16) - 16 ||
                    py < 0 || py > FFALIGN(avctx->height, 16) - 16)
                    return AVERROR_INVALIDDATA;

                src[0] = prev->data[0] + in_linesize[0] * py + px;
                src[1] = prev->data[0] + in_linesize[0] * py + px + 8;
                src[2] = prev->data[0] + in_linesize[0] * (py + 8) + px;
                src[3] = prev->data[0] + in_linesize[0] * (py + 8) + px + 8;
                src[4] = prev->data[1] + in_linesize[4] * (py >> 1) + (px >> 1);
                src[5] = prev->data[2] + in_linesize[5] * (py >> 1) + (px >> 1);

                if ((flags >> (cnt + 4)) & 1) {
                    for (int b = 0; b < 6; b++)
                        copy_block8(dst[b], src[b], linesize[b], in_linesize[b], 8);
                } else {
                    for (int b = 0; b < 6; b++) {
                        int mode = get_bits_le(&mgb, 2);

                        ret = decode_inter_block(avctx, mode, &gbyte, s->interq_tab[b >= 4],
                                                 s->block[b],
                                                 pfill[(b >= 4) + (b >= 5)],
                                                 dst[b], linesize[b],
                                                 src[b], in_linesize[b]);
                        if (ret < 0)
                            goto fail;
                    }
                }
            } else {
                for (int b = 0; b < 6; b++) {
                    int mode = get_bits_le(&mgb, 2);

                    ret = decode_intra_block(avctx, mode, &gbyte, s->intraq_tab[b >= 4],
                                             s->block[b],
                                             pfill[(b >= 4) + (b >= 5)],
                                             dst[b], linesize[b]);
                    if (ret < 0)
                        goto fail;
                }
            }

            cnt++;
        }
    }

fail:
    return ret;
}

static int decode_frame(AVCodecContext *avctx, AVFrame *frame,
                        int *got_frame, AVPacket *avpkt)
{
    MV30Context *s = avctx->priv_data;
    GetBitContext *gb = &s->gb;
    int ret;

    if ((ret = init_get_bits8(gb, avpkt->data, avpkt->size)) < 0)
        return ret;

    if ((ret = ff_get_buffer(avctx, frame, AV_GET_BUFFER_FLAG_REF)) < 0)
        return ret;

    s->intra_quant = get_bits(gb, 8);
    s->inter_quant = s->intra_quant + get_sbits(gb, 8);
    s->is_inter = get_bits_le(gb, 16);
    s->mode_size = get_bits_le(gb, 16);
    if (s->is_inter)
        s->nb_mvectors = get_bits_le(gb, 16);

    get_qtable(s->intraq_tab[0], s->intra_quant, luma_tab);
    get_qtable(s->intraq_tab[1], s->intra_quant, chroma_tab);

    if (s->is_inter == 0) {
        frame->flags |= AV_FRAME_FLAG_KEY;
        ret = decode_intra(avctx, gb, frame);
        if (ret < 0)
            return ret;
    } else {
        get_qtable(s->interq_tab[0], s->inter_quant, luma_tab);
        get_qtable(s->interq_tab[1], s->inter_quant, chroma_tab);

        if (!s->prev_frame->data[0]) {
            av_log(avctx, AV_LOG_ERROR, "Missing reference frame.\n");
            return AVERROR_INVALIDDATA;
        }

        frame->flags &= ~AV_FRAME_FLAG_KEY;
        ret = decode_inter(avctx, gb, frame, s->prev_frame);
        if (ret < 0)
            return ret;
    }

    if ((ret = av_frame_replace(s->prev_frame, frame)) < 0)
        return ret;

    *got_frame = 1;

    return avpkt->size;
}

static const uint8_t cbp_bits[] = {
    2, 2, 3, 3, 3, 4, 5, 6, 7, 8, 9, 9,
};

static av_cold void init_static_data(void)
{
    VLC_INIT_STATIC_TABLE_FROM_LENGTHS(cbp_tab, CBP_VLC_BITS,
                                       FF_ARRAY_ELEMS(cbp_bits),
                                       cbp_bits, 1, NULL, 0, 0, 0, 0);
}

static av_cold int decode_init(AVCodecContext *avctx)
{
    MV30Context *s = avctx->priv_data;
    static AVOnce init_static_once = AV_ONCE_INIT;

    avctx->pix_fmt = AV_PIX_FMT_YUV420P;
    avctx->color_range = AVCOL_RANGE_JPEG;

    ff_blockdsp_init(&s->bdsp);

    s->prev_frame = av_frame_alloc();
    if (!s->prev_frame)
        return AVERROR(ENOMEM);

    ff_thread_once(&init_static_once, init_static_data);

    return 0;
}

static void decode_flush(AVCodecContext *avctx)
{
    MV30Context *s = avctx->priv_data;

    av_frame_unref(s->prev_frame);
}

static av_cold int decode_close(AVCodecContext *avctx)
{
    MV30Context *s = avctx->priv_data;

    av_frame_free(&s->prev_frame);
    av_freep(&s->coeffs);
    s->coeffs_size = 0;
    av_freep(&s->mvectors);
    s->mvectors_size = 0;

    return 0;
}

const FFCodec ff_mv30_decoder = {
    .p.name           = "mv30",
    CODEC_LONG_NAME("MidiVid 3.0"),
    .p.type           = AVMEDIA_TYPE_VIDEO,
    .p.id             = AV_CODEC_ID_MV30,
    .priv_data_size   = sizeof(MV30Context),
    .init             = decode_init,
    .close            = decode_close,
    FF_CODEC_DECODE_CB(decode_frame),
    .flush            = decode_flush,
    .p.capabilities   = AV_CODEC_CAP_DR1,
    .caps_internal    = FF_CODEC_CAP_INIT_CLEANUP,
};
