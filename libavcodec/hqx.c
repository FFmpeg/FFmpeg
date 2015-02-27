/*
 * Canopus HQX decoder
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

#include <inttypes.h>

#include "libavutil/imgutils.h"
#include "libavutil/intreadwrite.h"

#include "avcodec.h"
#include "get_bits.h"
#include "internal.h"

#include "hqx.h"

/* HQX has four modes - 422, 444, 422alpha and 444alpha - all 12-bit */
enum HQXFormat {
    HQX_422 = 0,
    HQX_444,
    HQX_422A,
    HQX_444A,
};

#define HQX_HEADER_SIZE 59

typedef int (*mb_decode_func)(HQXContext *ctx, AVFrame *pic,
                              GetBitContext *gb, int x, int y);

/* macroblock selects a group of 4 possible quants and
 * a block can use any of those four quantisers
 * one column is powers of 2, the other one is powers of 2 * 3,
 * then there is the special one, powers of 2 * 5 */
static const int hqx_quants[16][4] = {
    {  0x1,   0x2,   0x4,   0x8 }, {  0x1,  0x3,   0x6,   0xC },
    {  0x2,   0x4,   0x8,  0x10 }, {  0x3,  0x6,   0xC,  0x18 },
    {  0x4,   0x8,  0x10,  0x20 }, {  0x6,  0xC,  0x18,  0x30 },
    {  0x8,  0x10,  0x20,  0x40 },
                      { 0xA, 0x14, 0x28, 0x50 },
                                   {  0xC, 0x18,  0x30,  0x60 },
    { 0x10,  0x20,  0x40,  0x80 }, { 0x18, 0x30,  0x60,  0xC0 },
    { 0x20,  0x40,  0x80, 0x100 }, { 0x30, 0x60,  0xC0, 0x180 },
    { 0x40,  0x80, 0x100, 0x200 }, { 0x60, 0xC0, 0x180, 0x300 },
    { 0x80, 0x100, 0x200, 0x400 }
};

static const uint8_t hqx_quant_luma[64] = {
    16,  16,  16,  19,  19,  19,  42,  44,
    16,  16,  19,  19,  19,  38,  43,  45,
    16,  19,  19,  19,  40,  41,  45,  48,
    19,  19,  19,  40,  41,  42,  46,  49,
    19,  19,  40,  41,  42,  43,  48, 101,
    19,  38,  41,  42,  43,  44,  98, 104,
    42,  43,  45,  46,  48,  98, 109, 116,
    44,  45,  48,  49, 101, 104, 116, 123,
};

static const uint8_t hqx_quant_chroma[64] = {
    16,  16,  19,  25,  26,  26,  42,  44,
    16,  19,  25,  25,  26,  38,  43,  91,
    19,  25,  26,  27,  40,  41,  91,  96,
    25,  25,  27,  40,  41,  84,  93, 197,
    26,  26,  40,  41,  84,  86, 191, 203,
    26,  38,  41,  84,  86, 177, 197, 209,
    42,  43,  91,  93, 191, 197, 219, 232,
    44,  91,  96, 197, 203, 209, 232, 246,
};

static inline void idct_col(int16_t *blk, const uint8_t *quant)
{
    int t0, t1, t2, t3, t4, t5, t6, t7, t8, t9, tA, tB, tC, tD, tE, tF;
    int t10, t11, t12, t13;
    int s0, s1, s2, s3, s4, s5, s6, s7;

    s0 = (int) blk[0 * 8] * quant[0 * 8];
    s1 = (int) blk[1 * 8] * quant[1 * 8];
    s2 = (int) blk[2 * 8] * quant[2 * 8];
    s3 = (int) blk[3 * 8] * quant[3 * 8];
    s4 = (int) blk[4 * 8] * quant[4 * 8];
    s5 = (int) blk[5 * 8] * quant[5 * 8];
    s6 = (int) blk[6 * 8] * quant[6 * 8];
    s7 = (int) blk[7 * 8] * quant[7 * 8];

    t0  =  (s3 * 19266 + s5 * 12873) >> 15;
    t1  =  (s5 * 19266 - s3 * 12873) >> 15;
    t2  = ((s7 * 4520  + s1 * 22725) >> 15) - t0;
    t3  = ((s1 * 4520  - s7 * 22725) >> 15) - t1;
    t4  = t0 * 2 + t2;
    t5  = t1 * 2 + t3;
    t6  = t2 - t3;
    t7  = t3 * 2 + t6;
    t8  = (t6 * 11585) >> 14;
    t9  = (t7 * 11585) >> 14;
    tA  = (s2 * 8867 - s6 * 21407) >> 14;
    tB  = (s6 * 8867 + s2 * 21407) >> 14;
    tC  = (s0 >> 1) - (s4 >> 1);
    tD  = (s4 >> 1) * 2 + tC;
    tE  = tC - (tA >> 1);
    tF  = tD - (tB >> 1);
    t10 = tF - t5;
    t11 = tE - t8;
    t12 = tE + (tA >> 1) * 2 - t9;
    t13 = tF + (tB >> 1) * 2 - t4;

    blk[0 * 8] = t13 + t4 * 2;
    blk[1 * 8] = t12 + t9 * 2;
    blk[2 * 8] = t11 + t8 * 2;
    blk[3 * 8] = t10 + t5 * 2;
    blk[4 * 8] = t10;
    blk[5 * 8] = t11;
    blk[6 * 8] = t12;
    blk[7 * 8] = t13;
}

static inline void idct_row(int16_t *blk)
{
    int t0, t1, t2, t3, t4, t5, t6, t7, t8, t9, tA, tB, tC, tD, tE, tF;
    int t10, t11, t12, t13;

    t0  =  (blk[3] * 19266 + blk[5] * 12873) >> 14;
    t1  =  (blk[5] * 19266 - blk[3] * 12873) >> 14;
    t2  = ((blk[7] * 4520  + blk[1] * 22725) >> 14) - t0;
    t3  = ((blk[1] * 4520  - blk[7] * 22725) >> 14) - t1;
    t4  = t0 * 2 + t2;
    t5  = t1 * 2 + t3;
    t6  = t2 - t3;
    t7  = t3 * 2 + t6;
    t8  = (t6 * 11585) >> 14;
    t9  = (t7 * 11585) >> 14;
    tA  = (blk[2] * 8867 - blk[6] * 21407) >> 14;
    tB  = (blk[6] * 8867 + blk[2] * 21407) >> 14;
    tC  = blk[0] - blk[4];
    tD  = blk[4] * 2 + tC;
    tE  = tC - tA;
    tF  = tD - tB;
    t10 = tF - t5;
    t11 = tE - t8;
    t12 = tE + tA * 2 - t9;
    t13 = tF + tB * 2 - t4;

    blk[0] = (t13 + t4 * 2 + 4) >> 3;
    blk[1] = (t12 + t9 * 2 + 4) >> 3;
    blk[2] = (t11 + t8 * 2 + 4) >> 3;
    blk[3] = (t10 + t5 * 2 + 4) >> 3;
    blk[4] = (t10          + 4) >> 3;
    blk[5] = (t11          + 4) >> 3;
    blk[6] = (t12          + 4) >> 3;
    blk[7] = (t13          + 4) >> 3;
}

static void hqx_idct(int16_t *block, const uint8_t *quant)
{
    int i;

    for (i = 0; i < 8; i++)
        idct_col(block + i, quant + i);
    for (i = 0; i < 8; i++)
        idct_row(block + i * 8);
}

static void hqx_idct_put(uint16_t *dst, ptrdiff_t stride,
                         int16_t *block, const uint8_t *quant)
{
    int i, j;

    hqx_idct(block, quant);

    for (i = 0; i < 8; i++) {
        for (j = 0; j < 8; j++) {
            int v = av_clip(block[j + i * 8] + 0x800, 0, 0x1000);
            dst[j] = (v << 4) | (v >> 8);
        }
        dst += stride >> 1;
    }
}

static inline void put_blocks(AVFrame *pic, int plane,
                              int x, int y, int ilace,
                              int16_t *block0, int16_t *block1,
                              const uint8_t *quant)
{
    int fields = ilace ? 2 : 1;
    int lsize = pic->linesize[plane];
    uint8_t *p = pic->data[plane] + x * 2;

    hqx_idct_put((uint16_t *)(p + y * lsize), lsize * fields, block0, quant);
    hqx_idct_put((uint16_t *)(p + (y + (ilace ? 1 : 8)) * lsize),
                 lsize * fields, block1, quant);
}

static inline void hqx_get_ac(GetBitContext *gb, const HQXAC *ac,
                              int *run, int *lev)
{
    int val;

    val = show_bits(gb, ac->lut_bits);
    if (ac->lut[val].bits == -1) {
        GetBitContext gb2 = *gb;
        skip_bits(&gb2, ac->lut_bits);
        val = ac->lut[val].lev + show_bits(&gb2, ac->extra_bits);
    }
    *run = ac->lut[val].run;
    *lev = ac->lut[val].lev;
    skip_bits(gb, ac->lut[val].bits);
}

static int decode_block(GetBitContext *gb, VLC *vlc,
                        const int *quants, int dcb,
                        int16_t block[64], int *last_dc)
{
    int q, dc;
    int ac_idx;
    int run, lev, pos = 1;

    memset(block, 0, 64 * sizeof(*block));
    dc = get_vlc2(gb, vlc->table, HQX_DC_VLC_BITS, 2);
    if (dc < 0)
        return AVERROR_INVALIDDATA;
    *last_dc += dc;

    block[0] = sign_extend(*last_dc << (12 - dcb), 12);

    q = quants[get_bits(gb, 2)];
    if (q >= 128)
        ac_idx = HQX_AC_Q128;
    else if (q >= 64)
        ac_idx = HQX_AC_Q64;
    else if (q >= 32)
        ac_idx = HQX_AC_Q32;
    else if (q >= 16)
        ac_idx = HQX_AC_Q16;
    else if (q >= 8)
        ac_idx = HQX_AC_Q8;
    else
        ac_idx = HQX_AC_Q0;

    do {
        hqx_get_ac(gb, &ff_hqx_ac[ac_idx], &run, &lev);
        pos += run;
        if (pos >= 64)
            break;
        block[ff_zigzag_direct[pos++]] = lev * q;
    } while (pos < 64);

    return 0;
}

static int hqx_decode_422(HQXContext *ctx, AVFrame *pic,
                          GetBitContext *gb, int x, int y)
{
    const int *quants;
    int flag;
    int last_dc;
    int i, ret;

    if (ctx->interlaced)
        flag = get_bits1(gb);
    else
        flag = 0;

    quants = hqx_quants[get_bits(gb, 4)];

    for (i = 0; i < 8; i++) {
        int vlc_index = ctx->dcb - 9;
        if (i == 0 || i == 4 || i == 6)
            last_dc = 0;
        ret = decode_block(gb, &ctx->dc_vlc[vlc_index], quants,
                           ctx->dcb, ctx->block[i], &last_dc);
        if (ret < 0)
            return ret;
    }

    put_blocks(pic, 0, x,      y, flag, ctx->block[0], ctx->block[2], hqx_quant_luma);
    put_blocks(pic, 0, x + 8,  y, flag, ctx->block[1], ctx->block[3], hqx_quant_luma);
    put_blocks(pic, 2, x >> 1, y, flag, ctx->block[4], ctx->block[5], hqx_quant_chroma);
    put_blocks(pic, 1, x >> 1, y, flag, ctx->block[6], ctx->block[7], hqx_quant_chroma);

    return 0;
}

static int hqx_decode_422a(HQXContext *ctx, AVFrame *pic,
                           GetBitContext *gb, int x, int y)
{
    const int *quants;
    int flag = 0;
    int last_dc;
    int i, ret;
    int cbp;

    cbp = get_vlc2(gb, ctx->cbp_vlc.table, ctx->cbp_vlc.bits, 1);

    for (i = 0; i < 12; i++)
        memset(ctx->block[i], 0, sizeof(**ctx->block) * 64);
    for (i = 0; i < 12; i++)
        ctx->block[i][0] = -0x800;
    if (cbp) {
        if (ctx->interlaced)
            flag = get_bits1(gb);

        quants = hqx_quants[get_bits(gb, 4)];

        cbp |= cbp << 4; // alpha CBP
        if (cbp & 0x3)   // chroma CBP - top
            cbp |= 0x500;
        if (cbp & 0xC)   // chroma CBP - bottom
            cbp |= 0xA00;
        for (i = 0; i < 12; i++) {
            if (i == 0 || i == 4 || i == 8 || i == 10)
                last_dc = 0;
            if (cbp & (1 << i)) {
                int vlc_index = ctx->dcb - 9;
                ret = decode_block(gb, &ctx->dc_vlc[vlc_index], quants,
                                   ctx->dcb, ctx->block[i], &last_dc);
                if (ret < 0)
                    return ret;
            }
        }
    }

    put_blocks(pic, 3, x,      y, flag, ctx->block[ 0], ctx->block[ 2], hqx_quant_luma);
    put_blocks(pic, 3, x + 8,  y, flag, ctx->block[ 1], ctx->block[ 3], hqx_quant_luma);
    put_blocks(pic, 0, x,      y, flag, ctx->block[ 4], ctx->block[ 6], hqx_quant_luma);
    put_blocks(pic, 0, x + 8,  y, flag, ctx->block[ 5], ctx->block[ 7], hqx_quant_luma);
    put_blocks(pic, 2, x >> 1, y, flag, ctx->block[ 8], ctx->block[ 9], hqx_quant_chroma);
    put_blocks(pic, 1, x >> 1, y, flag, ctx->block[10], ctx->block[11], hqx_quant_chroma);

    return 0;
}

static int hqx_decode_444(HQXContext *ctx, AVFrame *pic,
                          GetBitContext *gb, int x, int y)
{
    const int *quants;
    int flag;
    int last_dc;
    int i, ret;

    if (ctx->interlaced)
        flag = get_bits1(gb);
    else
        flag = 0;

    quants = hqx_quants[get_bits(gb, 4)];

    for (i = 0; i < 12; i++) {
        int vlc_index = ctx->dcb - 9;
        if (i == 0 || i == 4 || i == 8)
            last_dc = 0;
        ret = decode_block(gb, &ctx->dc_vlc[vlc_index], quants,
                           ctx->dcb, ctx->block[i], &last_dc);
        if (ret < 0)
            return ret;
    }

    put_blocks(pic, 0, x,     y, flag, ctx->block[0], ctx->block[ 2], hqx_quant_luma);
    put_blocks(pic, 0, x + 8, y, flag, ctx->block[1], ctx->block[ 3], hqx_quant_luma);
    put_blocks(pic, 2, x,     y, flag, ctx->block[4], ctx->block[ 6], hqx_quant_chroma);
    put_blocks(pic, 2, x + 8, y, flag, ctx->block[5], ctx->block[ 7], hqx_quant_chroma);
    put_blocks(pic, 1, x,     y, flag, ctx->block[8], ctx->block[10], hqx_quant_chroma);
    put_blocks(pic, 1, x + 8, y, flag, ctx->block[9], ctx->block[11], hqx_quant_chroma);

    return 0;
}

static int hqx_decode_444a(HQXContext *ctx, AVFrame *pic,
                           GetBitContext *gb, int x, int y)
{
    const int *quants;
    int flag = 0;
    int last_dc;
    int i, ret;
    int cbp;

    cbp = get_vlc2(gb, ctx->cbp_vlc.table, ctx->cbp_vlc.bits, 1);

    for (i = 0; i < 16; i++)
        memset(ctx->block[i], 0, sizeof(**ctx->block) * 64);
    for (i = 0; i < 16; i++)
        ctx->block[i][0] = -0x800;
    if (cbp) {
        if (ctx->interlaced)
            flag = get_bits1(gb);

        quants = hqx_quants[get_bits(gb, 4)];

        cbp |= cbp << 4; // alpha CBP
        cbp |= cbp << 8; // chroma CBP
        for (i = 0; i < 16; i++) {
            if (i == 0 || i == 4 || i == 8 || i == 12)
                last_dc = 0;
            if (cbp & (1 << i)) {
                int vlc_index = ctx->dcb - 9;
                ret = decode_block(gb, &ctx->dc_vlc[vlc_index], quants,
                                   ctx->dcb, ctx->block[i], &last_dc);
                if (ret < 0)
                    return ret;
            }
        }
    }

    put_blocks(pic, 3, x,     y, flag, ctx->block[ 0], ctx->block[ 2], hqx_quant_luma);
    put_blocks(pic, 3, x + 8, y, flag, ctx->block[ 1], ctx->block[ 3], hqx_quant_luma);
    put_blocks(pic, 0, x,     y, flag, ctx->block[ 4], ctx->block[ 6], hqx_quant_luma);
    put_blocks(pic, 0, x + 8, y, flag, ctx->block[ 5], ctx->block[ 7], hqx_quant_luma);
    put_blocks(pic, 2, x,     y, flag, ctx->block[ 8], ctx->block[10], hqx_quant_chroma);
    put_blocks(pic, 2, x + 8, y, flag, ctx->block[ 9], ctx->block[11], hqx_quant_chroma);
    put_blocks(pic, 1, x,     y, flag, ctx->block[12], ctx->block[14], hqx_quant_chroma);
    put_blocks(pic, 1, x + 8, y, flag, ctx->block[13], ctx->block[15], hqx_quant_chroma);

    return 0;
}

static const int shuffle_16[16] = {
    0, 5, 11, 14, 2, 7, 9, 13, 1, 4, 10, 15, 3, 6, 8, 12
};

static int decode_slice(HQXContext *ctx, AVFrame *pic, GetBitContext *gb,
                        int slice_no, mb_decode_func decode_func)
{
    int mb_w = (ctx->width  + 15) >> 4;
    int mb_h = (ctx->height + 15) >> 4;
    int grp_w = (mb_w + 4) / 5;
    int grp_h = (mb_h + 4) / 5;
    int grp_h_edge = grp_w * (mb_w / grp_w);
    int grp_v_edge = grp_h * (mb_h / grp_h);
    int grp_v_rest = mb_w - grp_h_edge;
    int grp_h_rest = mb_h - grp_v_edge;
    int num_mbs = mb_w * mb_h;
    int num_tiles = (num_mbs + 479) / 480;
    int std_tile_blocks = num_mbs / (16 * num_tiles);
    int g_tile = slice_no * num_tiles;
    int blk_addr, loc_addr, mb_x, mb_y, pos, loc_row, i;
    int tile_blocks, tile_limit, tile_no;

    for (tile_no = 0; tile_no < num_tiles; tile_no++, g_tile++) {
        tile_blocks = std_tile_blocks;
        tile_limit = -1;
        if (g_tile < num_mbs - std_tile_blocks * 16 * num_tiles) {
            tile_limit = num_mbs / (16 * num_tiles);
            tile_blocks++;
        }
        for (i = 0; i < tile_blocks; i++) {
            if (i == tile_limit)
                blk_addr = g_tile + 16 * num_tiles * i;
            else
                blk_addr = tile_no + 16 * num_tiles * i +
                           num_tiles * shuffle_16[(i + slice_no) & 0xF];
            loc_row  = grp_h * (blk_addr / (grp_h * mb_w));
            loc_addr =          blk_addr % (grp_h * mb_w);
            if (loc_row >= grp_v_edge) {
                mb_x = grp_w * (loc_addr / (grp_h_rest * grp_w));
                pos  =          loc_addr % (grp_h_rest * grp_w);
            } else {
                mb_x = grp_w * (loc_addr / (grp_h * grp_w));
                pos  =          loc_addr % (grp_h * grp_w);
            }
            if (mb_x >= grp_h_edge) {
                mb_x +=            pos % grp_v_rest;
                mb_y  = loc_row + (pos / grp_v_rest);
            } else {
                mb_x +=            pos % grp_w;
                mb_y  = loc_row + (pos / grp_w);
            }
            decode_func(ctx, pic, gb, mb_x * 16, mb_y * 16);
        }
    }

    return 0;
}

static int hqx_decode_frame(AVCodecContext *avctx, void *data,
                            int *got_picture_ptr, AVPacket *avpkt)
{
    HQXContext *ctx = avctx->priv_data;
    AVFrame *pic = data;
    uint8_t *src = avpkt->data;
    uint32_t info_tag, info_offset;
    int data_start;
    unsigned data_size;
    GetBitContext gb;
    int i, ret;
    int slice;
    uint32_t slice_off[17];
    mb_decode_func decode_func = 0;

    if (avpkt->size < 8)
        return AVERROR_INVALIDDATA;

    /* Skip the INFO header if present */
    info_offset = 0;
    info_tag    = AV_RL32(src);
    if (info_tag == MKTAG('I', 'N', 'F', 'O')) {
        info_offset = AV_RL32(src + 4);
        if (info_offset > UINT32_MAX - 8 || info_offset + 8 > avpkt->size) {
            av_log(avctx, AV_LOG_ERROR,
                   "Invalid INFO header offset: 0x%08"PRIX32" is too large.\n",
                   info_offset);
            return AVERROR_INVALIDDATA;
        }

        info_offset += 8;
        src         += info_offset;

        av_log(avctx, AV_LOG_DEBUG, "Skipping INFO chunk.\n");
    }

    data_start = src - avpkt->data;
    data_size  = avpkt->size - data_start;

    if (data_size < HQX_HEADER_SIZE) {
        av_log(avctx, AV_LOG_ERROR, "Frame too small.\n");
        return AVERROR_INVALIDDATA;
    }

    if (src[0] != 'H' || src[1] != 'Q') {
        av_log(avctx, AV_LOG_ERROR, "Not an HQX frame.\n");
        return AVERROR_INVALIDDATA;
    }
    ctx->interlaced = !(src[2] & 0x80);
    ctx->format     = src[2] & 7;
    ctx->dcb        = (src[3] & 3) + 8;
    ctx->width      = AV_RB16(src + 4);
    ctx->height     = AV_RB16(src + 6);
    for (i = 0; i < 17; i++)
        slice_off[i] = AV_RB24(src + 8 + i * 3);

    if (ctx->dcb == 8) {
        av_log(avctx, AV_LOG_ERROR, "Invalid DC precision %d.\n", ctx->dcb);
        return AVERROR_INVALIDDATA;
    }
    ret = av_image_check_size(ctx->width, ctx->height, 0, avctx);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Invalid stored dimensions %dx%d.\n",
               ctx->width, ctx->height);
        return AVERROR_INVALIDDATA;
    }

    avctx->coded_width         = FFALIGN(ctx->width,  16);
    avctx->coded_height        = FFALIGN(ctx->height, 16);
    avctx->width               = ctx->width;
    avctx->height              = ctx->height;
    avctx->bits_per_raw_sample = 10;

    switch (ctx->format) {
    case HQX_422:
        avctx->pix_fmt = AV_PIX_FMT_YUV422P16;
        decode_func = hqx_decode_422;
        break;
    case HQX_444:
        avctx->pix_fmt = AV_PIX_FMT_YUV444P16;
        decode_func = hqx_decode_444;
        break;
    case HQX_422A:
        avctx->pix_fmt = AV_PIX_FMT_YUVA422P16;
        decode_func = hqx_decode_422a;
        break;
    case HQX_444A:
        avctx->pix_fmt = AV_PIX_FMT_YUVA444P16;
        decode_func = hqx_decode_444a;
        break;
    }
    if (!decode_func) {
        av_log(avctx, AV_LOG_ERROR, "Invalid format: %d.\n", ctx->format);
        return AVERROR_INVALIDDATA;
    }

    ret = ff_get_buffer(avctx, pic, 0);
    if (ret < 0)
        return ret;

    for (slice = 0; slice < 16; slice++) {
        if (slice_off[slice] < HQX_HEADER_SIZE ||
            slice_off[slice] >= slice_off[slice + 1] ||
            slice_off[slice + 1] > data_size) {
            av_log(avctx, AV_LOG_ERROR, "Invalid slice size.\n");
            break;
        }
        ret = init_get_bits8(&gb, src + slice_off[slice],
                             slice_off[slice + 1] - slice_off[slice]);
        if (ret < 0)
            return ret;
        ret = decode_slice(ctx, pic, &gb, slice, decode_func);
        if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR, "Error decoding slice %d.\n", slice);
        }
    }

    pic->key_frame = 1;
    pic->pict_type = AV_PICTURE_TYPE_I;

    *got_picture_ptr = 1;

    return avpkt->size;
}

static av_cold int hqx_decode_close(AVCodecContext *avctx)
{
    int i;
    HQXContext *ctx = avctx->priv_data;

    ff_free_vlc(&ctx->cbp_vlc);
    for (i = 0; i < 3; i++) {
        ff_free_vlc(&ctx->dc_vlc[i]);
    }

    return 0;
}

static av_cold int hqx_decode_init(AVCodecContext *avctx)
{
    HQXContext *ctx = avctx->priv_data;
    int ret = ff_hqx_init_vlcs(ctx);
    if (ret < 0)
        hqx_decode_close(avctx);
    return ret;
}

AVCodec ff_hqx_decoder = {
    .name           = "hqx",
    .long_name      = NULL_IF_CONFIG_SMALL("Canopus HQX"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_HQX,
    .priv_data_size = sizeof(HQXContext),
    .init           = hqx_decode_init,
    .decode         = hqx_decode_frame,
    .close          = hqx_decode_close,
    .capabilities   = CODEC_CAP_DR1,
};
