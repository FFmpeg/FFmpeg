/*
 * Microsoft Screen 4 (aka Microsoft Expression Encoder Screen) decoder
 * Copyright (c) 2012 Konstantin Shishkov
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

/**
 * @file
 * Microsoft Screen 4 (aka Microsoft Titanium Screen 2,
 * aka Microsoft Expression Encoder Screen) decoder
 */

#include "avcodec.h"
#include "bytestream.h"
#include "dsputil.h"
#include "get_bits.h"
#include "internal.h"
#include "mss34dsp.h"
#include "unary.h"

#define HEADER_SIZE 8

enum FrameType {
    INTRA_FRAME = 0,
    INTER_FRAME,
    SKIP_FRAME
};

enum BlockType {
    SKIP_BLOCK = 0,
    DCT_BLOCK,
    IMAGE_BLOCK,
};

enum CachePos {
    LEFT = 0,
    TOP_LEFT,
    TOP,
};

static const uint8_t mss4_dc_vlc_lens[2][16] = {
    { 0, 1, 5, 1, 1, 1, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 3, 1, 1, 1, 1, 1, 1, 1, 2, 0, 0, 0, 0, 0, 0 }
};

static const uint8_t mss4_ac_vlc_lens[2][16] = {
    { 0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 125 },
    { 0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 119 }
};

static const uint8_t mss4_ac_vlc_syms[2][162] = {
  { 0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12,
    0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07,
    0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xA1, 0x08,
    0x23, 0x42, 0xB1, 0xC1, 0x15, 0x52, 0xD1, 0xF0,
    0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0A, 0x16,
    0x17, 0x18, 0x19, 0x1A, 0x25, 0x26, 0x27, 0x28,
    0x29, 0x2A, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
    0x3A, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
    0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
    0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
    0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79,
    0x7A, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
    0x8A, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98,
    0x99, 0x9A, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7,
    0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6,
    0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3, 0xC4, 0xC5,
    0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xD2, 0xD3, 0xD4,
    0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xE1, 0xE2,
    0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA,
    0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8,
    0xF9, 0xFA  },
  { 0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21,
    0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71,
    0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91,
    0xA1, 0xB1, 0xC1, 0x09, 0x23, 0x33, 0x52, 0xF0,
    0x15, 0x62, 0x72, 0xD1, 0x0A, 0x16, 0x24, 0x34,
    0xE1, 0x25, 0xF1, 0x17, 0x18, 0x19, 0x1A, 0x26,
    0x27, 0x28, 0x29, 0x2A, 0x35, 0x36, 0x37, 0x38,
    0x39, 0x3A, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
    0x49, 0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
    0x59, 0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
    0x69, 0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78,
    0x79, 0x7A, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
    0x88, 0x89, 0x8A, 0x92, 0x93, 0x94, 0x95, 0x96,
    0x97, 0x98, 0x99, 0x9A, 0xA2, 0xA3, 0xA4, 0xA5,
    0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4,
    0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3,
    0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xD2,
    0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA,
    0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9,
    0xEA, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8,
    0xF9, 0xFA  }
};

static const uint8_t vec_len_syms[2][4] = {
    { 4, 2, 3, 1 },
    { 4, 1, 2, 3 }
};

static const uint8_t mss4_vec_entry_vlc_lens[2][16] = {
    { 0, 2, 2, 3, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 1, 5, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }
};

static const uint8_t mss4_vec_entry_vlc_syms[2][9] = {
    { 0, 7, 6, 5, 8, 4, 3, 1, 2 },
    { 0, 2, 3, 4, 5, 6, 7, 1, 8 }
};

#define MAX_ENTRIES  162

typedef struct MSS4Context {
    AVFrame   *pic;

    VLC        dc_vlc[2], ac_vlc[2];
    VLC        vec_entry_vlc[2];
    int        block[64];
    uint8_t    imgbuf[3][16 * 16];

    int        quality;
    uint16_t   quant_mat[2][64];

    int        *prev_dc[3];
    int        dc_stride[3];
    int        dc_cache[4][4];

    int        prev_vec[3][4];
} MSS4Context;

static av_cold int mss4_init_vlc(VLC *vlc, const uint8_t *lens,
                                 const uint8_t *syms, int num_syms)
{
    uint8_t  bits[MAX_ENTRIES];
    uint16_t codes[MAX_ENTRIES];
    int i, j;
    int prefix = 0, max_bits = 0, idx = 0;

    for (i = 0; i < 16; i++) {
        for (j = 0; j < lens[i]; j++) {
            bits[idx]  = i + 1;
            codes[idx] = prefix++;
            max_bits   = i + 1;
            idx++;
        }
        prefix <<= 1;
    }

    return ff_init_vlc_sparse(vlc, FFMIN(max_bits, 9), num_syms, bits, 1, 1,
                              codes, 2, 2, syms, 1, 1, 0);
}

static av_cold int mss4_init_vlcs(MSS4Context *ctx)
{
    int ret, i;

    for (i = 0; i < 2; i++) {
        ret = mss4_init_vlc(&ctx->dc_vlc[i], mss4_dc_vlc_lens[i], NULL, 12);
        if (ret)
            return ret;
        ret = mss4_init_vlc(&ctx->ac_vlc[i], mss4_ac_vlc_lens[i],
                            mss4_ac_vlc_syms[i], 162);
        if (ret)
            return ret;
        ret = mss4_init_vlc(&ctx->vec_entry_vlc[i], mss4_vec_entry_vlc_lens[i],
                            mss4_vec_entry_vlc_syms[i], 9);
        if (ret)
            return ret;
    }
    return 0;
}

static av_cold void mss4_free_vlcs(MSS4Context *ctx)
{
    int i;

    for (i = 0; i < 2; i++) {
        ff_free_vlc(&ctx->dc_vlc[i]);
        ff_free_vlc(&ctx->ac_vlc[i]);
        ff_free_vlc(&ctx->vec_entry_vlc[i]);
    }
}

/* This function returns values in the range
 * (-range + 1; -range/2] U [range/2; range - 1)
 * i.e.
 * nbits = 0 -> 0
 * nbits = 1 -> -1, 1
 * nbits = 2 -> -3, -2, 2, 3
 */
static av_always_inline int get_coeff_bits(GetBitContext *gb, int nbits)
{
    int val;

    if (!nbits)
        return 0;

    val = get_bits(gb, nbits);
    if (val < (1 << (nbits - 1)))
        val -= (1 << nbits) - 1;

    return val;
}

static inline int get_coeff(GetBitContext *gb, VLC *vlc)
{
    int val = get_vlc2(gb, vlc->table, vlc->bits, 2);

    return get_coeff_bits(gb, val);
}

static int mss4_decode_dct(GetBitContext *gb, VLC *dc_vlc, VLC *ac_vlc,
                           int *block, int *dc_cache,
                           int bx, int by, uint16_t *quant_mat)
{
    int skip, val, pos = 1, zz_pos, dc;

    memset(block, 0, sizeof(*block) * 64);

    dc = get_coeff(gb, dc_vlc);
    // DC prediction is the same as in MSS3
    if (by) {
        if (bx) {
            int l, tl, t;

            l  = dc_cache[LEFT];
            tl = dc_cache[TOP_LEFT];
            t  = dc_cache[TOP];

            if (FFABS(t - tl) <= FFABS(l - tl))
                dc += l;
            else
                dc += t;
        } else {
            dc += dc_cache[TOP];
        }
    } else if (bx) {
        dc += dc_cache[LEFT];
    }
    dc_cache[LEFT] = dc;
    block[0]       = dc * quant_mat[0];

    while (pos < 64) {
        val = get_vlc2(gb, ac_vlc->table, 9, 2);
        if (!val)
            return 0;
        if (val == -1)
            return -1;
        if (val == 0xF0) {
            pos += 16;
            continue;
        }
        skip = val >> 4;
        val  = get_coeff_bits(gb, val & 0xF);
        pos += skip;
        if (pos >= 64)
            return -1;

        zz_pos = ff_zigzag_direct[pos];
        block[zz_pos] = val * quant_mat[zz_pos];
        pos++;
    }

    return pos == 64 ? 0 : -1;
}

static int mss4_decode_dct_block(MSS4Context *c, GetBitContext *gb,
                                 uint8_t *dst[3], int mb_x, int mb_y)
{
    int i, j, k, ret;
    uint8_t *out = dst[0];

    for (j = 0; j < 2; j++) {
        for (i = 0; i < 2; i++) {
            int xpos = mb_x * 2 + i;
            c->dc_cache[j][TOP_LEFT] = c->dc_cache[j][TOP];
            c->dc_cache[j][TOP]      = c->prev_dc[0][mb_x * 2 + i];
            ret = mss4_decode_dct(gb, c->dc_vlc, c->ac_vlc, c->block,
                                  c->dc_cache[j],
                                  xpos, mb_y * 2 + j, c->quant_mat[0]);
            if (ret)
                return ret;
            c->prev_dc[0][mb_x * 2 + i] = c->dc_cache[j][LEFT];

            ff_mss34_dct_put(out + xpos * 8, c->pic->linesize[0],
                             c->block);
        }
        out += 8 * c->pic->linesize[0];
    }

    for (i = 1; i < 3; i++) {
        c->dc_cache[i + 1][TOP_LEFT] = c->dc_cache[i + 1][TOP];
        c->dc_cache[i + 1][TOP]      = c->prev_dc[i][mb_x];
        ret = mss4_decode_dct(gb, c->dc_vlc + 1, c->ac_vlc + 1,
                              c->block, c->dc_cache[i + 1], mb_x, mb_y,
                              c->quant_mat[1]);
        if (ret)
            return ret;
        c->prev_dc[i][mb_x] = c->dc_cache[i + 1][LEFT];

        ff_mss34_dct_put(c->imgbuf[i], 8, c->block);
        out = dst[i] + mb_x * 16;
        // Since the DCT block is coded as YUV420 and the whole frame as YUV444,
        // we need to scale chroma.
        for (j = 0; j < 16; j++) {
            for (k = 0; k < 8; k++)
                AV_WN16A(out + k * 2, c->imgbuf[i][k + (j & ~1) * 4] * 0x101);
            out += c->pic->linesize[i];
        }
    }

    return 0;
}

static void read_vec_pos(GetBitContext *gb, int *vec_pos, int *sel_flag,
                         int *sel_len, int *prev)
{
    int i, y_flag = 0;

    for (i = 2; i >= 0; i--) {
        if (!sel_flag[i]) {
            vec_pos[i] = 0;
            continue;
        }
        if ((!i && !y_flag) || get_bits1(gb)) {
            if (sel_len[i] > 0) {
                int pval = prev[i];
                vec_pos[i] = get_bits(gb, sel_len[i]);
                if (vec_pos[i] >= pval)
                    vec_pos[i]++;
            } else {
                vec_pos[i] = !prev[i];
            }
            y_flag = 1;
        } else {
            vec_pos[i] = prev[i];
        }
    }
}

static int get_value_cached(GetBitContext *gb, int vec_pos, uint8_t *vec,
                            int vec_size, int component, int shift, int *prev)
{
    if (vec_pos < vec_size)
        return vec[vec_pos];
    if (!get_bits1(gb))
        return prev[component];
    prev[component] = get_bits(gb, 8 - shift) << shift;
    return prev[component];
}

#define MKVAL(vals)  (vals[0] | (vals[1] << 3) | (vals[2] << 6))

/* Image mode - the hardest to comprehend MSS4 coding mode.
 *
 * In this mode all three 16x16 blocks are coded together with a method
 * remotely similar to the methods employed in MSS1-MSS3.
 * The idea is that every component has a vector of 1-4 most common symbols
 * and an escape mode for reading new value from the bitstream. Decoding
 * consists of retrieving pixel values from the vector or reading new ones
 * from the bitstream; depending on flags read from the bitstream, these vector
 * positions can be updated or reused from the state of the previous line
 * or previous pixel.
 */
static int mss4_decode_image_block(MSS4Context *ctx, GetBitContext *gb,
                                   uint8_t *picdst[3], int mb_x, int mb_y)
{
    uint8_t vec[3][4];
    int     vec_len[3];
    int     sel_len[3], sel_flag[3];
    int     i, j, k, mode, split;
    int     prev_vec1 = 0, prev_split = 0;
    int     vals[3] = { 0 };
    int     prev_pix[3] = { 0 };
    int     prev_mode[16] = { 0 };
    uint8_t *dst[3];

    const int val_shift = ctx->quality == 100 ? 0 : 2;

    for (i = 0; i < 3; i++)
        dst[i] = ctx->imgbuf[i];

    for (i = 0; i < 3; i++) {
        vec_len[i] = vec_len_syms[!!i][get_unary(gb, 0, 3)];
        for (j = 0; j < vec_len[i]; j++) {
            vec[i][j]  = get_coeff(gb, &ctx->vec_entry_vlc[!!i]);
            vec[i][j] += ctx->prev_vec[i][j];
            ctx->prev_vec[i][j] = vec[i][j];
        }
        sel_flag[i] = vec_len[i] > 1;
        sel_len[i]  = vec_len[i] > 2 ? vec_len[i] - 2 : 0;
    }

    for (j = 0; j < 16; j++) {
        if (get_bits1(gb)) {
            split = 0;
            if (get_bits1(gb)) {
                prev_mode[0] = 0;
                vals[0] = vals[1] = vals[2] = 0;
                mode = 2;
            } else {
                mode = get_bits1(gb);
                if (mode)
                    split = get_bits(gb, 4);
            }
            for (i = 0; i < 16; i++) {
                if (mode <= 1) {
                    vals[0] =  prev_mode[i]       & 7;
                    vals[1] = (prev_mode[i] >> 3) & 7;
                    vals[2] =  prev_mode[i] >> 6;
                    if (mode == 1 && i == split) {
                        read_vec_pos(gb, vals, sel_flag, sel_len, vals);
                    }
                } else if (mode == 2) {
                    if (get_bits1(gb))
                        read_vec_pos(gb, vals, sel_flag, sel_len, vals);
                }
                for (k = 0; k < 3; k++)
                    *dst[k]++ = get_value_cached(gb, vals[k], vec[k],
                                                 vec_len[k], k,
                                                 val_shift, prev_pix);
                prev_mode[i] = MKVAL(vals);
            }
        } else {
            if (get_bits1(gb)) {
                split = get_bits(gb, 4);
                if (split >= prev_split)
                    split++;
                prev_split = split;
            } else {
                split = prev_split;
            }
            if (split) {
                vals[0] =  prev_mode[0]       & 7;
                vals[1] = (prev_mode[0] >> 3) & 7;
                vals[2] =  prev_mode[0] >> 6;
                for (i = 0; i < 3; i++) {
                    for (k = 0; k < split; k++) {
                        *dst[i]++ = get_value_cached(gb, vals[i], vec[i],
                                                     vec_len[i], i, val_shift,
                                                     prev_pix);
                        prev_mode[k] = MKVAL(vals);
                    }
                }
            }

            if (split != 16) {
                vals[0] =  prev_vec1       & 7;
                vals[1] = (prev_vec1 >> 3) & 7;
                vals[2] =  prev_vec1 >> 6;
                if (get_bits1(gb)) {
                    read_vec_pos(gb, vals, sel_flag, sel_len, vals);
                    prev_vec1 = MKVAL(vals);
                }
                for (i = 0; i < 3; i++) {
                    for (k = 0; k < 16 - split; k++) {
                        *dst[i]++ = get_value_cached(gb, vals[i], vec[i],
                                                     vec_len[i], i, val_shift,
                                                     prev_pix);
                        prev_mode[split + k] = MKVAL(vals);
                    }
                }
            }
        }
    }

    for (i = 0; i < 3; i++)
        for (j = 0; j < 16; j++)
            memcpy(picdst[i] + mb_x * 16 + j * ctx->pic->linesize[i],
                   ctx->imgbuf[i] + j * 16, 16);

    return 0;
}

static inline void mss4_update_dc_cache(MSS4Context *c, int mb_x)
{
    int i;

    c->dc_cache[0][TOP]  = c->prev_dc[0][mb_x * 2 + 1];
    c->dc_cache[0][LEFT] = 0;
    c->dc_cache[1][TOP]  = 0;
    c->dc_cache[1][LEFT] = 0;

    for (i = 0; i < 2; i++)
        c->prev_dc[0][mb_x * 2 + i] = 0;

    for (i = 1; i < 3; i++) {
        c->dc_cache[i + 1][TOP]  = c->prev_dc[i][mb_x];
        c->dc_cache[i + 1][LEFT] = 0;
        c->prev_dc[i][mb_x]      = 0;
    }
}

static int mss4_decode_frame(AVCodecContext *avctx, void *data, int *got_frame,
                             AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    MSS4Context *c = avctx->priv_data;
    GetBitContext gb;
    GetByteContext bc;
    uint8_t *dst[3];
    int width, height, quality, frame_type;
    int x, y, i, mb_width, mb_height, blk_type;
    int ret;

    if (buf_size < HEADER_SIZE) {
        av_log(avctx, AV_LOG_ERROR,
               "Frame should have at least %d bytes, got %d instead\n",
               HEADER_SIZE, buf_size);
        return AVERROR_INVALIDDATA;
    }

    bytestream2_init(&bc, buf, buf_size);
    width      = bytestream2_get_be16(&bc);
    height     = bytestream2_get_be16(&bc);
    bytestream2_skip(&bc, 2);
    quality    = bytestream2_get_byte(&bc);
    frame_type = bytestream2_get_byte(&bc);

    if (width > avctx->width ||
        height != avctx->height) {
        av_log(avctx, AV_LOG_ERROR, "Invalid frame dimensions %dx%d\n",
               width, height);
        return AVERROR_INVALIDDATA;
    }
    if (quality < 1 || quality > 100) {
        av_log(avctx, AV_LOG_ERROR, "Invalid quality setting %d\n", quality);
        return AVERROR_INVALIDDATA;
    }
    if ((frame_type & ~3) || frame_type == 3) {
        av_log(avctx, AV_LOG_ERROR, "Invalid frame type %d\n", frame_type);
        return AVERROR_INVALIDDATA;
    }

    if (frame_type != SKIP_FRAME && !bytestream2_get_bytes_left(&bc)) {
        av_log(avctx, AV_LOG_ERROR,
               "Empty frame found but it is not a skip frame.\n");
        return AVERROR_INVALIDDATA;
    }

    if ((ret = ff_reget_buffer(avctx, c->pic)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "reget_buffer() failed\n");
        return ret;
    }
    c->pic->key_frame = (frame_type == INTRA_FRAME);
    c->pic->pict_type = (frame_type == INTRA_FRAME) ? AV_PICTURE_TYPE_I
                                                   : AV_PICTURE_TYPE_P;
    if (frame_type == SKIP_FRAME) {
        *got_frame      = 1;
        if ((ret = av_frame_ref(data, c->pic)) < 0)
            return ret;

        return buf_size;
    }

    if (c->quality != quality) {
        c->quality = quality;
        for (i = 0; i < 2; i++)
            ff_mss34_gen_quant_mat(c->quant_mat[i], quality, !i);
    }

    init_get_bits(&gb, buf + HEADER_SIZE, (buf_size - HEADER_SIZE) * 8);

    mb_width  = FFALIGN(width,  16) >> 4;
    mb_height = FFALIGN(height, 16) >> 4;
    dst[0] = c->pic->data[0];
    dst[1] = c->pic->data[1];
    dst[2] = c->pic->data[2];

    memset(c->prev_vec, 0, sizeof(c->prev_vec));
    for (y = 0; y < mb_height; y++) {
        memset(c->dc_cache, 0, sizeof(c->dc_cache));
        for (x = 0; x < mb_width; x++) {
            blk_type = decode012(&gb);
            switch (blk_type) {
            case DCT_BLOCK:
                if (mss4_decode_dct_block(c, &gb, dst, x, y) < 0) {
                    av_log(avctx, AV_LOG_ERROR,
                           "Error decoding DCT block %d,%d\n",
                           x, y);
                    return AVERROR_INVALIDDATA;
                }
                break;
            case IMAGE_BLOCK:
                if (mss4_decode_image_block(c, &gb, dst, x, y) < 0) {
                    av_log(avctx, AV_LOG_ERROR,
                           "Error decoding VQ block %d,%d\n",
                           x, y);
                    return AVERROR_INVALIDDATA;
                }
                break;
            case SKIP_BLOCK:
                if (frame_type == INTRA_FRAME) {
                    av_log(avctx, AV_LOG_ERROR, "Skip block in intra frame\n");
                    return AVERROR_INVALIDDATA;
                }
                break;
            }
            if (blk_type != DCT_BLOCK)
                mss4_update_dc_cache(c, x);
        }
        dst[0] += c->pic->linesize[0] * 16;
        dst[1] += c->pic->linesize[1] * 16;
        dst[2] += c->pic->linesize[2] * 16;
    }

    if ((ret = av_frame_ref(data, c->pic)) < 0)
        return ret;

    *got_frame      = 1;

    return buf_size;
}

static av_cold int mss4_decode_end(AVCodecContext *avctx)
{
    MSS4Context * const c = avctx->priv_data;
    int i;

    av_frame_free(&c->pic);
    for (i = 0; i < 3; i++)
        av_freep(&c->prev_dc[i]);
    mss4_free_vlcs(c);

    return 0;
}

static av_cold int mss4_decode_init(AVCodecContext *avctx)
{
    MSS4Context * const c = avctx->priv_data;
    int i;

    if (mss4_init_vlcs(c)) {
        av_log(avctx, AV_LOG_ERROR, "Cannot initialise VLCs\n");
        mss4_free_vlcs(c);
        return AVERROR(ENOMEM);
    }
    for (i = 0; i < 3; i++) {
        c->dc_stride[i] = FFALIGN(avctx->width, 16) >> (2 + !!i);
        c->prev_dc[i]   = av_malloc(sizeof(**c->prev_dc) * c->dc_stride[i]);
        if (!c->prev_dc[i]) {
            av_log(avctx, AV_LOG_ERROR, "Cannot allocate buffer\n");
            mss4_free_vlcs(c);
            return AVERROR(ENOMEM);
        }
    }

    c->pic = av_frame_alloc();
    if (!c->pic) {
        mss4_decode_end(avctx);
        return AVERROR(ENOMEM);
    }

    avctx->pix_fmt     = AV_PIX_FMT_YUV444P;

    return 0;
}

AVCodec ff_mts2_decoder = {
    .name           = "mts2",
    .long_name      = NULL_IF_CONFIG_SMALL("MS Expression Encoder Screen"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_MTS2,
    .priv_data_size = sizeof(MSS4Context),
    .init           = mss4_decode_init,
    .close          = mss4_decode_end,
    .decode         = mss4_decode_frame,
    .capabilities   = CODEC_CAP_DR1,
};
