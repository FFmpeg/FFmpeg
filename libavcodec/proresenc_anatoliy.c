/*
 * Apple ProRes encoder
 *
 * Copyright (c) 2011 Anatoliy Wasserman
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
 * Apple ProRes encoder (Anatoliy Wasserman version)
 * Known FOURCCs: 'ap4h' (444), 'apch' (HQ), 'apcn' (422), 'apcs' (LT), 'acpo' (Proxy)
 */

#include "libavutil/mem_internal.h"
#include "libavutil/opt.h"
#include "avcodec.h"
#include "dct.h"
#include "internal.h"
#include "profiles.h"
#include "proresdata.h"
#include "put_bits.h"
#include "bytestream.h"
#include "fdctdsp.h"

#define DEFAULT_SLICE_MB_WIDTH 8

static const AVProfile profiles[] = {
    { FF_PROFILE_PRORES_PROXY,    "apco"},
    { FF_PROFILE_PRORES_LT,       "apcs"},
    { FF_PROFILE_PRORES_STANDARD, "apcn"},
    { FF_PROFILE_PRORES_HQ,       "apch"},
    { FF_PROFILE_PRORES_4444,     "ap4h"},
    { FF_PROFILE_PRORES_XQ,       "ap4x"},
    { FF_PROFILE_UNKNOWN }
};

static const int qp_start_table[] = {  8, 3, 2, 1, 1, 1};
static const int qp_end_table[]   = { 13, 9, 6, 6, 5, 4};
static const int bitrate_table[]  = { 1000, 2100, 3500, 5400, 7000, 10000};

static const int valid_primaries[]  = { AVCOL_PRI_RESERVED0, AVCOL_PRI_BT709, AVCOL_PRI_UNSPECIFIED, AVCOL_PRI_BT470BG,
                                        AVCOL_PRI_SMPTE170M, AVCOL_PRI_BT2020, AVCOL_PRI_SMPTE431, AVCOL_PRI_SMPTE432, INT_MAX };
static const int valid_trc[]        = { AVCOL_TRC_RESERVED0, AVCOL_TRC_BT709, AVCOL_TRC_UNSPECIFIED, AVCOL_TRC_SMPTE2084,
                                        AVCOL_TRC_ARIB_STD_B67, INT_MAX };
static const int valid_colorspace[] = { AVCOL_SPC_BT709, AVCOL_SPC_UNSPECIFIED, AVCOL_SPC_SMPTE170M,
                                        AVCOL_SPC_BT2020_NCL, INT_MAX };

static const uint8_t QMAT_LUMA[6][64] = {
    {
         4,  7,  9, 11, 13, 14, 15, 63,
         7,  7, 11, 12, 14, 15, 63, 63,
         9, 11, 13, 14, 15, 63, 63, 63,
        11, 11, 13, 14, 63, 63, 63, 63,
        11, 13, 14, 63, 63, 63, 63, 63,
        13, 14, 63, 63, 63, 63, 63, 63,
        13, 63, 63, 63, 63, 63, 63, 63,
        63, 63, 63, 63, 63, 63, 63, 63
    }, {
         4,  5,  6,  7,  9, 11, 13, 15,
         5,  5,  7,  8, 11, 13, 15, 17,
         6,  7,  9, 11, 13, 15, 15, 17,
         7,  7,  9, 11, 13, 15, 17, 19,
         7,  9, 11, 13, 14, 16, 19, 23,
         9, 11, 13, 14, 16, 19, 23, 29,
         9, 11, 13, 15, 17, 21, 28, 35,
        11, 13, 16, 17, 21, 28, 35, 41
    }, {
         4,  4,  5,  5,  6,  7,  7,  9,
         4,  4,  5,  6,  7,  7,  9,  9,
         5,  5,  6,  7,  7,  9,  9, 10,
         5,  5,  6,  7,  7,  9,  9, 10,
         5,  6,  7,  7,  8,  9, 10, 12,
         6,  7,  7,  8,  9, 10, 12, 15,
         6,  7,  7,  9, 10, 11, 14, 17,
         7,  7,  9, 10, 11, 14, 17, 21
    }, {
         4,  4,  4,  4,  4,  4,  4,  4,
         4,  4,  4,  4,  4,  4,  4,  4,
         4,  4,  4,  4,  4,  4,  4,  4,
         4,  4,  4,  4,  4,  4,  4,  5,
         4,  4,  4,  4,  4,  4,  5,  5,
         4,  4,  4,  4,  4,  5,  5,  6,
         4,  4,  4,  4,  5,  5,  6,  7,
         4,  4,  4,  4,  5,  6,  7,  7
    }, { /* 444 */
        4,  4,  4,  4,  4,  4,  4,  4,
        4,  4,  4,  4,  4,  4,  4,  4,
        4,  4,  4,  4,  4,  4,  4,  4,
        4,  4,  4,  4,  4,  4,  4,  5,
        4,  4,  4,  4,  4,  4,  5,  5,
        4,  4,  4,  4,  4,  5,  5,  6,
        4,  4,  4,  4,  5,  5,  6,  7,
        4,  4,  4,  4,  5,  6,  7,  7
    }, { /* 444 XQ */
        2,  2,  2,  2,  2,  2,  2,  2,
        2,  2,  2,  2,  2,  2,  2,  2,
        2,  2,  2,  2,  2,  2,  2,  2,
        2,  2,  2,  2,  2,  2,  2,  3,
        2,  2,  2,  2,  2,  2,  3,  3,
        2,  2,  2,  2,  2,  3,  3,  3,
        2,  2,  2,  2,  3,  3,  3,  4,
        2,  2,  2,  2,  3,  3,  4,  4,
    }
};

static const uint8_t QMAT_CHROMA[6][64] = {
    {
         4,  7,  9, 11, 13, 14, 63, 63,
         7,  7, 11, 12, 14, 63, 63, 63,
         9, 11, 13, 14, 63, 63, 63, 63,
        11, 11, 13, 14, 63, 63, 63, 63,
        11, 13, 14, 63, 63, 63, 63, 63,
        13, 14, 63, 63, 63, 63, 63, 63,
        13, 63, 63, 63, 63, 63, 63, 63,
        63, 63, 63, 63, 63, 63, 63, 63
    }, {
         4,  5,  6,  7,  9, 11, 13, 15,
         5,  5,  7,  8, 11, 13, 15, 17,
         6,  7,  9, 11, 13, 15, 15, 17,
         7,  7,  9, 11, 13, 15, 17, 19,
         7,  9, 11, 13, 14, 16, 19, 23,
         9, 11, 13, 14, 16, 19, 23, 29,
         9, 11, 13, 15, 17, 21, 28, 35,
        11, 13, 16, 17, 21, 28, 35, 41
    }, {
         4,  4,  5,  5,  6,  7,  7,  9,
         4,  4,  5,  6,  7,  7,  9,  9,
         5,  5,  6,  7,  7,  9,  9, 10,
         5,  5,  6,  7,  7,  9,  9, 10,
         5,  6,  7,  7,  8,  9, 10, 12,
         6,  7,  7,  8,  9, 10, 12, 15,
         6,  7,  7,  9, 10, 11, 14, 17,
         7,  7,  9, 10, 11, 14, 17, 21
    }, {
         4,  4,  4,  4,  4,  4,  4,  4,
         4,  4,  4,  4,  4,  4,  4,  4,
         4,  4,  4,  4,  4,  4,  4,  4,
         4,  4,  4,  4,  4,  4,  4,  5,
         4,  4,  4,  4,  4,  4,  5,  5,
         4,  4,  4,  4,  4,  5,  5,  6,
         4,  4,  4,  4,  5,  5,  6,  7,
         4,  4,  4,  4,  5,  6,  7,  7
    }, { /* 444 */
        4,  4,  4,  4,  4,  4,  4,  4,
        4,  4,  4,  4,  4,  4,  4,  4,
        4,  4,  4,  4,  4,  4,  4,  4,
        4,  4,  4,  4,  4,  4,  4,  5,
        4,  4,  4,  4,  4,  4,  5,  5,
        4,  4,  4,  4,  4,  5,  5,  6,
        4,  4,  4,  4,  5,  5,  6,  7,
        4,  4,  4,  4,  5,  6,  7,  7
    }, { /* 444 xq */
        4,  4,  4,  4,  4,  4,  4,  4,
        4,  4,  4,  4,  4,  4,  4,  4,
        4,  4,  4,  4,  4,  4,  4,  4,
        4,  4,  4,  4,  4,  4,  4,  5,
        4,  4,  4,  4,  4,  4,  5,  5,
        4,  4,  4,  4,  4,  5,  5,  6,
        4,  4,  4,  4,  5,  5,  6,  7,
        4,  4,  4,  4,  5,  6,  7,  7
    }
};


typedef struct {
    AVClass *class;
    FDCTDSPContext fdsp;
    uint8_t* fill_y;
    uint8_t* fill_u;
    uint8_t* fill_v;
    uint8_t* fill_a;

    int qmat_luma[16][64];
    int qmat_chroma[16][64];
    const uint8_t *scantable;

    int is_422;
    int need_alpha;
    int is_interlaced;

    char *vendor;
} ProresContext;

static void encode_codeword(PutBitContext *pb, int val, int codebook)
{
    unsigned int rice_order, exp_order, switch_bits, first_exp, exp, zeros;

    /* number of bits to switch between rice and exp golomb */
    switch_bits = codebook & 3;
    rice_order  = codebook >> 5;
    exp_order   = (codebook >> 2) & 7;

    first_exp = ((switch_bits + 1) << rice_order);

    if (val >= first_exp) { /* exp golomb */
        val -= first_exp;
        val += (1 << exp_order);
        exp = av_log2(val);
        zeros = exp - exp_order + switch_bits + 1;
        put_bits(pb, zeros, 0);
        put_bits(pb, exp + 1, val);
    } else if (rice_order) {
        put_bits(pb, (val >> rice_order), 0);
        put_bits(pb, 1, 1);
        put_sbits(pb, rice_order, val);
    } else {
        put_bits(pb, val, 0);
        put_bits(pb, 1, 1);
    }
}

#define QSCALE(qmat,ind,val) ((val) / ((qmat)[ind]))
#define TO_GOLOMB(val) (((val) * 2) ^ ((val) >> 31))
#define DIFF_SIGN(val, sign) (((val) >> 31) ^ (sign))
#define IS_NEGATIVE(val) ((((val) >> 31) ^ -1) + 1)
#define TO_GOLOMB2(val,sign) ((val)==0 ? 0 : ((val) << 1) + (sign))

static av_always_inline int get_level(int val)
{
    int sign = (val >> 31);
    return (val ^ sign) - sign;
}

#define FIRST_DC_CB 0xB8

static const uint8_t dc_codebook[7] = { 0x04, 0x28, 0x28, 0x4D, 0x4D, 0x70, 0x70};

static void encode_dc_coeffs(PutBitContext *pb, int16_t *in,
        int blocks_per_slice, int *qmat)
{
    int prev_dc, code;
    int i, sign, idx;
    int new_dc, delta, diff_sign, new_code;

    prev_dc = QSCALE(qmat, 0, in[0] - 16384);
    code = TO_GOLOMB(prev_dc);
    encode_codeword(pb, code, FIRST_DC_CB);

    code = 5; sign = 0; idx = 64;
    for (i = 1; i < blocks_per_slice; i++, idx += 64) {
        new_dc    = QSCALE(qmat, 0, in[idx] - 16384);
        delta     = new_dc - prev_dc;
        diff_sign = DIFF_SIGN(delta, sign);
        new_code  = TO_GOLOMB2(get_level(delta), diff_sign);

        encode_codeword(pb, new_code, dc_codebook[FFMIN(code, 6)]);

        code      = new_code;
        sign      = delta >> 31;
        prev_dc   = new_dc;
    }
}

static const uint8_t run_to_cb[16] = { 0x06, 0x06, 0x05, 0x05, 0x04, 0x29,
        0x29, 0x29, 0x29, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x4C };
static const uint8_t lev_to_cb[10] = { 0x04, 0x0A, 0x05, 0x06, 0x04, 0x28,
        0x28, 0x28, 0x28, 0x4C };

static void encode_ac_coeffs(PutBitContext *pb,
        int16_t *in, int blocks_per_slice, int *qmat, const uint8_t ff_prores_scan[64])
{
    int prev_run = 4;
    int prev_level = 2;

    int run = 0, level, code, i, j;
    for (i = 1; i < 64; i++) {
        int indp = ff_prores_scan[i];
        for (j = 0; j < blocks_per_slice; j++) {
            int val = QSCALE(qmat, indp, in[(j << 6) + indp]);
            if (val) {
                encode_codeword(pb, run, run_to_cb[FFMIN(prev_run, 15)]);

                prev_run   = run;
                run        = 0;
                level      = get_level(val);
                code       = level - 1;

                encode_codeword(pb, code, lev_to_cb[FFMIN(prev_level, 9)]);

                prev_level = level;

                put_bits(pb, 1, IS_NEGATIVE(val));
            } else {
                ++run;
            }
        }
    }
}

static void get(uint8_t *pixels, int stride, int16_t* block)
{
    int i;

    for (i = 0; i < 8; i++) {
        AV_WN64(block, AV_RN64(pixels));
        AV_WN64(block+4, AV_RN64(pixels+8));
        pixels += stride;
        block += 8;
    }
}

static void fdct_get(FDCTDSPContext *fdsp, uint8_t *pixels, int stride, int16_t* block)
{
    get(pixels, stride, block);
    fdsp->fdct(block);
}

static void calc_plane_dct(FDCTDSPContext *fdsp, uint8_t *src, int16_t * blocks, int src_stride, int mb_count, int chroma, int is_422)
{
    int16_t *block;
    int i;

    block = blocks;

    if (!chroma) { /* Luma plane */
        for (i = 0; i < mb_count; i++) {
            fdct_get(fdsp, src,                       src_stride, block + (0 << 6));
            fdct_get(fdsp, src + 16,                  src_stride, block + (1 << 6));
            fdct_get(fdsp, src +      8 * src_stride, src_stride, block + (2 << 6));
            fdct_get(fdsp, src + 16 + 8 * src_stride, src_stride, block + (3 << 6));

            block += 256;
            src   += 32;
        }
    } else if (chroma && is_422){ /* chroma plane 422 */
        for (i = 0; i < mb_count; i++) {
            fdct_get(fdsp, src,                  src_stride, block + (0 << 6));
            fdct_get(fdsp, src + 8 * src_stride, src_stride, block + (1 << 6));
            block += (256 >> 1);
            src   += (32  >> 1);
        }
    } else { /* chroma plane 444 */
        for (i = 0; i < mb_count; i++) {
            fdct_get(fdsp, src,                       src_stride, block + (0 << 6));
            fdct_get(fdsp, src +      8 * src_stride, src_stride, block + (1 << 6));
            fdct_get(fdsp, src + 16,                  src_stride, block + (2 << 6));
            fdct_get(fdsp, src + 16 + 8 * src_stride, src_stride, block + (3 << 6));

            block += 256;
            src   += 32;
        }
    }
}

static int encode_slice_plane(int16_t *blocks, int mb_count, uint8_t *buf, unsigned buf_size, int *qmat, int sub_sample_chroma,
                              const uint8_t ff_prores_scan[64])
{
    int blocks_per_slice;
    PutBitContext pb;

    blocks_per_slice = mb_count << (2 - sub_sample_chroma);
    init_put_bits(&pb, buf, buf_size);

    encode_dc_coeffs(&pb, blocks, blocks_per_slice, qmat);
    encode_ac_coeffs(&pb, blocks, blocks_per_slice, qmat, ff_prores_scan);

    flush_put_bits(&pb);
    return put_bits_ptr(&pb) - pb.buf;
}

static av_always_inline unsigned encode_slice_data(AVCodecContext *avctx,
                                                   int16_t * blocks_y, int16_t * blocks_u, int16_t * blocks_v,
                                                   unsigned mb_count, uint8_t *buf, unsigned data_size,
                                                   unsigned* y_data_size, unsigned* u_data_size, unsigned* v_data_size,
                                                   int qp)
{
    ProresContext* ctx = avctx->priv_data;

    *y_data_size = encode_slice_plane(blocks_y, mb_count,
                                      buf, data_size, ctx->qmat_luma[qp - 1], 0, ctx->scantable);

    if (!(avctx->flags & AV_CODEC_FLAG_GRAY)) {
        *u_data_size = encode_slice_plane(blocks_u, mb_count, buf + *y_data_size, data_size - *y_data_size,
                                          ctx->qmat_chroma[qp - 1], ctx->is_422, ctx->scantable);

        *v_data_size = encode_slice_plane(blocks_v, mb_count, buf + *y_data_size + *u_data_size,
                                          data_size - *y_data_size - *u_data_size,
                                          ctx->qmat_chroma[qp - 1], ctx->is_422, ctx->scantable);
    }

    return *y_data_size + *u_data_size + *v_data_size;
}

static void put_alpha_diff(PutBitContext *pb, int cur, int prev)
{
    const int abits = 16;
    const int dbits = 7;
    const int dsize = 1 << dbits - 1;
    int diff = cur - prev;

    diff = av_mod_uintp2(diff, abits);
    if (diff >= (1 << abits) - dsize)
        diff -= 1 << abits;
    if (diff < -dsize || diff > dsize || !diff) {
        put_bits(pb, 1, 1);
        put_bits(pb, abits, diff);
    } else {
        put_bits(pb, 1, 0);
        put_bits(pb, dbits - 1, FFABS(diff) - 1);
        put_bits(pb, 1, diff < 0);
    }
}

static inline void put_alpha_run(PutBitContext *pb, int run)
{
    if (run) {
        put_bits(pb, 1, 0);
        if (run < 0x10)
            put_bits(pb, 4, run);
        else
            put_bits(pb, 15, run);
    } else {
        put_bits(pb, 1, 1);
    }
}

static av_always_inline int encode_alpha_slice_data(AVCodecContext *avctx, int8_t * src_a,
                                                   unsigned mb_count, uint8_t *buf, unsigned data_size, unsigned* a_data_size)
{
    const int abits = 16;
    const int mask  = (1 << abits) - 1;
    const int num_coeffs = mb_count * 256;
    int prev = mask, cur;
    int idx = 0;
    int run = 0;
    int16_t * blocks = (int16_t *)src_a;
    PutBitContext pb;
    init_put_bits(&pb, buf, data_size);

    cur = blocks[idx++];
    put_alpha_diff(&pb, cur, prev);
    prev = cur;
    do {
        cur = blocks[idx++];
        if (cur != prev) {
            put_alpha_run (&pb, run);
            put_alpha_diff(&pb, cur, prev);
            prev = cur;
            run  = 0;
        } else {
            run++;
        }
    } while (idx < num_coeffs);
    if (run)
        put_alpha_run(&pb, run);
    flush_put_bits(&pb);
    *a_data_size = put_bits_count(&pb) >> 3;

    if (put_bits_left(&pb) < 0) {
        av_log(avctx, AV_LOG_ERROR,
               "Underestimated required buffer size.\n");
        return AVERROR_BUG;
    } else {
        return 0;
    }
}

static inline void subimage_with_fill_template(uint16_t *src, unsigned x, unsigned y,
                                               unsigned stride, unsigned width, unsigned height, uint16_t *dst,
                                               unsigned dst_width, unsigned dst_height, int is_alpha_plane,
                                               int is_interlaced, int is_top_field)
{
    int box_width = FFMIN(width - x, dst_width);
    int i, j, src_stride, box_height;
    uint16_t last_pix, *last_line;

    if (!is_interlaced) {
        src_stride = stride >> 1;
        src += y * src_stride + x;
        box_height = FFMIN(height - y, dst_height);
    } else {
        src_stride = stride; /* 2 lines stride */
        src += y * src_stride + x;
        box_height = FFMIN(height/2 - y, dst_height);
        if (!is_top_field)
            src += stride >> 1;
    }

    for (i = 0; i < box_height; ++i) {
        for (j = 0; j < box_width; ++j) {
            if (!is_alpha_plane) {
                dst[j] = src[j];
            } else {
                dst[j] = src[j] << 6; /* alpha 10b to 16b */
            }
        }
        if (!is_alpha_plane) {
            last_pix = dst[j - 1];
        } else {
            last_pix = dst[j - 1] << 6; /* alpha 10b to 16b */
        }
        for (; j < dst_width; j++)
            dst[j] = last_pix;
        src += src_stride;
        dst += dst_width;
    }
    last_line = dst - dst_width;
    for (; i < dst_height; i++) {
        for (j = 0; j < dst_width; ++j) {
            dst[j] = last_line[j];
        }
        dst += dst_width;
    }
}

static void subimage_with_fill(uint16_t *src, unsigned x, unsigned y,
        unsigned stride, unsigned width, unsigned height, uint16_t *dst,
        unsigned dst_width, unsigned dst_height, int is_interlaced, int is_top_field)
{
    subimage_with_fill_template(src, x, y, stride, width, height, dst, dst_width, dst_height, 0, is_interlaced, is_top_field);
}

/* reorganize alpha data and convert 10b -> 16b */
static void subimage_alpha_with_fill(uint16_t *src, unsigned x, unsigned y,
                               unsigned stride, unsigned width, unsigned height, uint16_t *dst,
                               unsigned dst_width, unsigned dst_height, int is_interlaced, int is_top_field)
{
    subimage_with_fill_template(src, x, y, stride, width, height, dst, dst_width, dst_height, 1, is_interlaced, is_top_field);
}

static int encode_slice(AVCodecContext *avctx, const AVFrame *pic, int mb_x,
        int mb_y, unsigned mb_count, uint8_t *buf, unsigned data_size,
        int unsafe, int *qp, int is_interlaced, int is_top_field)
{
    int luma_stride, chroma_stride, alpha_stride = 0;
    ProresContext* ctx = avctx->priv_data;
    int hdr_size = 6 + (ctx->need_alpha * 2); /* v data size is write when there is alpha */
    int ret = 0, slice_size;
    uint8_t *dest_y, *dest_u, *dest_v;
    unsigned y_data_size = 0, u_data_size = 0, v_data_size = 0, a_data_size = 0;
    FDCTDSPContext *fdsp = &ctx->fdsp;
    int tgt_bits   = (mb_count * bitrate_table[avctx->profile]) >> 2;
    int low_bytes  = (tgt_bits - (tgt_bits >> 3)) >> 3; // 12% bitrate fluctuation
    int high_bytes = (tgt_bits + (tgt_bits >> 3)) >> 3;

    LOCAL_ALIGNED(16, int16_t, blocks_y, [DEFAULT_SLICE_MB_WIDTH << 8]);
    LOCAL_ALIGNED(16, int16_t, blocks_u, [DEFAULT_SLICE_MB_WIDTH << 8]);
    LOCAL_ALIGNED(16, int16_t, blocks_v, [DEFAULT_SLICE_MB_WIDTH << 8]);

    luma_stride   = pic->linesize[0];
    chroma_stride = pic->linesize[1];

    if (ctx->need_alpha)
        alpha_stride = pic->linesize[3];

    if (!is_interlaced) {
        dest_y = pic->data[0] + (mb_y << 4) * luma_stride   + (mb_x << 5);
        dest_u = pic->data[1] + (mb_y << 4) * chroma_stride + (mb_x << (5 - ctx->is_422));
        dest_v = pic->data[2] + (mb_y << 4) * chroma_stride + (mb_x << (5 - ctx->is_422));
    } else {
        dest_y = pic->data[0] + (mb_y << 4) * luma_stride * 2   + (mb_x << 5);
        dest_u = pic->data[1] + (mb_y << 4) * chroma_stride * 2 + (mb_x << (5 - ctx->is_422));
        dest_v = pic->data[2] + (mb_y << 4) * chroma_stride * 2 + (mb_x << (5 - ctx->is_422));
        if (!is_top_field){ /* bottom field, offset dest */
            dest_y += luma_stride;
            dest_u += chroma_stride;
            dest_v += chroma_stride;
        }
    }

    if (unsafe) {
        subimage_with_fill((uint16_t *) pic->data[0], mb_x << 4, mb_y << 4,
                luma_stride, avctx->width, avctx->height,
                (uint16_t *) ctx->fill_y, mb_count << 4, 16, is_interlaced, is_top_field);
        subimage_with_fill((uint16_t *) pic->data[1], mb_x << (4 - ctx->is_422), mb_y << 4,
                           chroma_stride, avctx->width >> ctx->is_422, avctx->height,
                           (uint16_t *) ctx->fill_u, mb_count << (4 - ctx->is_422), 16, is_interlaced, is_top_field);
        subimage_with_fill((uint16_t *) pic->data[2], mb_x << (4 - ctx->is_422), mb_y << 4,
                           chroma_stride, avctx->width >> ctx->is_422, avctx->height,
                           (uint16_t *) ctx->fill_v, mb_count << (4 - ctx->is_422), 16, is_interlaced, is_top_field);

        /* no need for interlaced special case, data already reorganized in subimage_with_fill */
        calc_plane_dct(fdsp, ctx->fill_y, blocks_y, mb_count <<  5,                mb_count, 0, 0);
        calc_plane_dct(fdsp, ctx->fill_u, blocks_u, mb_count << (5 - ctx->is_422), mb_count, 1, ctx->is_422);
        calc_plane_dct(fdsp, ctx->fill_v, blocks_v, mb_count << (5 - ctx->is_422), mb_count, 1, ctx->is_422);

        slice_size = encode_slice_data(avctx, blocks_y, blocks_u, blocks_v,
                          mb_count, buf + hdr_size, data_size - hdr_size,
                          &y_data_size, &u_data_size, &v_data_size,
                          *qp);
    } else {
        if (!is_interlaced) {
            calc_plane_dct(fdsp, dest_y, blocks_y, luma_stride, mb_count, 0, 0);
            calc_plane_dct(fdsp, dest_u, blocks_u, chroma_stride, mb_count, 1, ctx->is_422);
            calc_plane_dct(fdsp, dest_v, blocks_v, chroma_stride, mb_count, 1, ctx->is_422);
        } else {
            calc_plane_dct(fdsp, dest_y, blocks_y, luma_stride   * 2, mb_count, 0, 0);
            calc_plane_dct(fdsp, dest_u, blocks_u, chroma_stride * 2, mb_count, 1, ctx->is_422);
            calc_plane_dct(fdsp, dest_v, blocks_v, chroma_stride * 2, mb_count, 1, ctx->is_422);
        }

        slice_size = encode_slice_data(avctx, blocks_y, blocks_u, blocks_v,
                          mb_count, buf + hdr_size, data_size - hdr_size,
                          &y_data_size, &u_data_size, &v_data_size,
                          *qp);

        if (slice_size > high_bytes && *qp < qp_end_table[avctx->profile]) {
            do {
                *qp += 1;
                slice_size = encode_slice_data(avctx, blocks_y, blocks_u, blocks_v,
                                               mb_count, buf + hdr_size, data_size - hdr_size,
                                               &y_data_size, &u_data_size, &v_data_size,
                                               *qp);
            } while (slice_size > high_bytes && *qp < qp_end_table[avctx->profile]);
        } else if (slice_size < low_bytes && *qp
                > qp_start_table[avctx->profile]) {
            do {
                *qp -= 1;
                slice_size = encode_slice_data(avctx, blocks_y, blocks_u, blocks_v,
                                               mb_count, buf + hdr_size, data_size - hdr_size,
                                               &y_data_size, &u_data_size, &v_data_size,
                                               *qp);
            } while (slice_size < low_bytes && *qp > qp_start_table[avctx->profile]);
        }
    }

    buf[0] = hdr_size << 3;
    buf[1] = *qp;
    AV_WB16(buf + 2, y_data_size);
    AV_WB16(buf + 4, u_data_size);

    if (ctx->need_alpha) {
        AV_WB16(buf + 6, v_data_size); /* write v data size only if there is alpha */

        subimage_alpha_with_fill((uint16_t *) pic->data[3], mb_x << 4, mb_y << 4,
                           alpha_stride, avctx->width, avctx->height,
                           (uint16_t *) ctx->fill_a, mb_count << 4, 16, is_interlaced, is_top_field);
        ret = encode_alpha_slice_data(avctx, ctx->fill_a, mb_count,
                                      buf + hdr_size + slice_size,
                                      data_size - hdr_size - slice_size, &a_data_size);
    }

    if (ret != 0) {
        return ret;
    }
    return hdr_size + y_data_size + u_data_size + v_data_size + a_data_size;
}

static int prores_encode_picture(AVCodecContext *avctx, const AVFrame *pic,
        uint8_t *buf, const int buf_size, const int picture_index, const int is_top_field)
{
    ProresContext *ctx = avctx->priv_data;
    int mb_width = (avctx->width + 15) >> 4;
    int hdr_size, sl_size, i;
    int mb_y, sl_data_size, qp, mb_height, picture_height, unsafe_mb_height_limit;
    int unsafe_bot, unsafe_right;
    uint8_t *sl_data, *sl_data_sizes;
    int slice_per_line = 0, rem = mb_width;

    if (!ctx->is_interlaced) { /* progressive encoding */
        mb_height = (avctx->height + 15) >> 4;
        unsafe_mb_height_limit = mb_height;
    } else {
        if (is_top_field) {
            picture_height = (avctx->height + 1) / 2;
        } else {
            picture_height = avctx->height / 2;
        }
        mb_height = (picture_height + 15) >> 4;
        unsafe_mb_height_limit = mb_height;
    }

    for (i = av_log2(DEFAULT_SLICE_MB_WIDTH); i >= 0; --i) {
        slice_per_line += rem >> i;
        rem &= (1 << i) - 1;
    }

    qp = qp_start_table[avctx->profile];
    hdr_size = 8; sl_data_size = buf_size - hdr_size;
    sl_data_sizes = buf + hdr_size;
    sl_data = sl_data_sizes + (slice_per_line * mb_height * 2);
    for (mb_y = 0; mb_y < mb_height; mb_y++) {
        int mb_x = 0;
        int slice_mb_count = DEFAULT_SLICE_MB_WIDTH;
        while (mb_x < mb_width) {
            while (mb_width - mb_x < slice_mb_count)
                slice_mb_count >>= 1;

            unsafe_bot = (avctx->height & 0xf) && (mb_y == unsafe_mb_height_limit - 1);
            unsafe_right = (avctx->width & 0xf) && (mb_x + slice_mb_count == mb_width);

            sl_size = encode_slice(avctx, pic, mb_x, mb_y, slice_mb_count,
                    sl_data, sl_data_size, unsafe_bot || unsafe_right, &qp, ctx->is_interlaced, is_top_field);
            if (sl_size < 0){
                return sl_size;
            }

            bytestream_put_be16(&sl_data_sizes, sl_size);
            sl_data           += sl_size;
            sl_data_size      -= sl_size;
            mb_x              += slice_mb_count;
        }
    }

    buf[0] = hdr_size << 3;
    AV_WB32(buf + 1, sl_data - buf);
    AV_WB16(buf + 5, slice_per_line * mb_height); /* picture size */
    buf[7] = av_log2(DEFAULT_SLICE_MB_WIDTH) << 4; /* number of slices */

    return sl_data - buf;
}

static int prores_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                               const AVFrame *pict, int *got_packet)
{
    ProresContext *ctx = avctx->priv_data;
    int header_size = 148;
    uint8_t *buf;
    int compress_frame_size, pic_size, ret, is_top_field_first = 0;
    uint8_t frame_flags;
    int frame_size = FFALIGN(avctx->width, 16) * FFALIGN(avctx->height, 16)*16 + 500 + AV_INPUT_BUFFER_MIN_SIZE; //FIXME choose tighter limit


    if ((ret = ff_alloc_packet2(avctx, pkt, frame_size + AV_INPUT_BUFFER_MIN_SIZE, 0)) < 0)
        return ret;

    buf = pkt->data;
    compress_frame_size = 8 + header_size;

    bytestream_put_be32(&buf, compress_frame_size);/* frame size will be update after picture(s) encoding */
    bytestream_put_buffer(&buf, "icpf", 4);

    bytestream_put_be16(&buf, header_size);
    bytestream_put_be16(&buf, 0); /* version */
    bytestream_put_buffer(&buf, ctx->vendor, 4);
    bytestream_put_be16(&buf, avctx->width);
    bytestream_put_be16(&buf, avctx->height);
    frame_flags = 0x82; /* 422 not interlaced */
    if (avctx->profile >= FF_PROFILE_PRORES_4444) /* 4444 or 4444 Xq */
        frame_flags |= 0x40; /* 444 chroma */
    if (ctx->is_interlaced) {
        if (pict->top_field_first || !pict->interlaced_frame) { /* tff frame or progressive frame interpret as tff */
            av_log(avctx, AV_LOG_DEBUG, "use interlaced encoding, top field first\n");
            frame_flags |= 0x04; /* interlaced tff */
            is_top_field_first = 1;
        } else {
            av_log(avctx, AV_LOG_DEBUG, "use interlaced encoding, bottom field first\n");
            frame_flags |= 0x08; /* interlaced bff */
        }
    } else {
        av_log(avctx, AV_LOG_DEBUG, "use progressive encoding\n");
    }
    *buf++ = frame_flags;
    *buf++ = 0; /* reserved */
    /* only write color properties, if valid value. set to unspecified otherwise */
    *buf++ = ff_int_from_list_or_default(avctx, "frame color primaries", pict->color_primaries, valid_primaries, 0);
    *buf++ = ff_int_from_list_or_default(avctx, "frame color trc", pict->color_trc, valid_trc, 0);
    *buf++ = ff_int_from_list_or_default(avctx, "frame colorspace", pict->colorspace, valid_colorspace, 0);
    if (avctx->profile >= FF_PROFILE_PRORES_4444) {
        if (avctx->pix_fmt == AV_PIX_FMT_YUV444P10) {
            *buf++ = 0xA0;/* src b64a and no alpha */
        } else {
            *buf++ = 0xA2;/* src b64a and 16b alpha */
        }
    } else {
        *buf++ = 32;/* src v210 and no alpha */
    }
    *buf++ = 0; /* reserved */
    *buf++ = 3; /* luma and chroma matrix present */

    bytestream_put_buffer(&buf, QMAT_LUMA[avctx->profile],   64);
    bytestream_put_buffer(&buf, QMAT_CHROMA[avctx->profile], 64);

    pic_size = prores_encode_picture(avctx, pict, buf,
                                     pkt->size - compress_frame_size, 0, is_top_field_first);/* encode progressive or first field */
    if (pic_size < 0) {
        return pic_size;
    }
    compress_frame_size += pic_size;

    if (ctx->is_interlaced) { /* encode second field */
        pic_size = prores_encode_picture(avctx, pict, pkt->data + compress_frame_size,
                                         pkt->size - compress_frame_size, 1, !is_top_field_first);
        if (pic_size < 0) {
            return pic_size;
        }
        compress_frame_size += pic_size;
    }

    AV_WB32(pkt->data, compress_frame_size);/* update frame size */
    pkt->flags |= AV_PKT_FLAG_KEY;
    pkt->size = compress_frame_size;
    *got_packet = 1;

    return 0;
}

static void scale_mat(const uint8_t* src, int* dst, int scale)
{
    int i;
    for (i = 0; i < 64; i++)
        dst[i] = src[i] * scale;
}

static av_cold int prores_encode_init(AVCodecContext *avctx)
{
    int i;
    ProresContext* ctx = avctx->priv_data;

    avctx->bits_per_raw_sample = 10;
    ctx->need_alpha = 0;
    ctx->is_interlaced = !!(avctx->flags & AV_CODEC_FLAG_INTERLACED_DCT);
    if (ctx->is_interlaced) {
        ctx->scantable = ff_prores_interlaced_scan;
    } else {
        ctx->scantable = ff_prores_progressive_scan;
    }

    if (avctx->width & 0x1) {
        av_log(avctx, AV_LOG_ERROR,
                "frame width needs to be multiple of 2\n");
        return AVERROR(EINVAL);
    }

    if (avctx->width > 65534 || avctx->height > 65535) {
        av_log(avctx, AV_LOG_ERROR,
                "The maximum dimensions are 65534x65535\n");
        return AVERROR(EINVAL);
    }

    if (strlen(ctx->vendor) != 4) {
        av_log(avctx, AV_LOG_ERROR, "vendor ID should be 4 bytes\n");
        return AVERROR(EINVAL);
    }

    if (avctx->profile == FF_PROFILE_UNKNOWN) {
        if (avctx->pix_fmt == AV_PIX_FMT_YUV422P10) {
            avctx->profile = FF_PROFILE_PRORES_STANDARD;
            av_log(avctx, AV_LOG_INFO,
                "encoding with ProRes standard (apcn) profile\n");
        } else if (avctx->pix_fmt == AV_PIX_FMT_YUV444P10) {
            avctx->profile = FF_PROFILE_PRORES_4444;
            av_log(avctx, AV_LOG_INFO,
                   "encoding with ProRes 4444 (ap4h) profile\n");
        } else if (avctx->pix_fmt == AV_PIX_FMT_YUVA444P10) {
            avctx->profile = FF_PROFILE_PRORES_4444;
            av_log(avctx, AV_LOG_INFO,
                   "encoding with ProRes 4444+ (ap4h) profile\n");
        } else {
            av_log(avctx, AV_LOG_ERROR, "Unknown pixel format\n");
            return AVERROR(EINVAL);
        }
    } else if (avctx->profile < FF_PROFILE_PRORES_PROXY
            || avctx->profile > FF_PROFILE_PRORES_XQ) {
        av_log(
                avctx,
                AV_LOG_ERROR,
                "unknown profile %d, use [0 - apco, 1 - apcs, 2 - apcn (default), 3 - apch, 4 - ap4h, 5 - ap4x]\n",
                avctx->profile);
        return AVERROR(EINVAL);
    } else if ((avctx->pix_fmt == AV_PIX_FMT_YUV422P10) && (avctx->profile > FF_PROFILE_PRORES_HQ)){
        av_log(avctx, AV_LOG_ERROR,
               "encoding with ProRes 444/Xq (ap4h/ap4x) profile, need YUV444P10 input\n");
        return AVERROR(EINVAL);
    }  else if ((avctx->pix_fmt == AV_PIX_FMT_YUV444P10 || avctx->pix_fmt == AV_PIX_FMT_YUVA444P10)
                && (avctx->profile < FF_PROFILE_PRORES_4444)){
        av_log(avctx, AV_LOG_ERROR,
               "encoding with ProRes Proxy/LT/422/422 HQ (apco, apcs, apcn, ap4h) profile, need YUV422P10 input\n");
        return AVERROR(EINVAL);
    }

    if (avctx->profile < FF_PROFILE_PRORES_4444) { /* 422 versions */
        ctx->is_422 = 1;
        if ((avctx->height & 0xf) || (avctx->width & 0xf)) {
            ctx->fill_y = av_malloc(4 * (DEFAULT_SLICE_MB_WIDTH << 8));
            if (!ctx->fill_y)
                return AVERROR(ENOMEM);
            ctx->fill_u = ctx->fill_y + (DEFAULT_SLICE_MB_WIDTH << 9);
            ctx->fill_v = ctx->fill_u + (DEFAULT_SLICE_MB_WIDTH << 8);
        }
    } else { /* 444 */
        ctx->is_422 = 0;
        if ((avctx->height & 0xf) || (avctx->width & 0xf)) {
            ctx->fill_y = av_malloc(3 * (DEFAULT_SLICE_MB_WIDTH << 9));
            if (!ctx->fill_y)
                return AVERROR(ENOMEM);
            ctx->fill_u = ctx->fill_y + (DEFAULT_SLICE_MB_WIDTH << 9);
            ctx->fill_v = ctx->fill_u + (DEFAULT_SLICE_MB_WIDTH << 9);
        }
        if (avctx->pix_fmt == AV_PIX_FMT_YUVA444P10) {
            ctx->need_alpha = 1;
            ctx->fill_a = av_malloc(DEFAULT_SLICE_MB_WIDTH << 9); /* 8 blocks x 16px x 16px x sizeof (uint16) */
            if (!ctx->fill_a)
                return AVERROR(ENOMEM);
        }
    }

    ff_fdctdsp_init(&ctx->fdsp, avctx);

    avctx->codec_tag = AV_RL32((const uint8_t*)profiles[avctx->profile].name);

    for (i = 1; i <= 16; i++) {
        scale_mat(QMAT_LUMA[avctx->profile]  , ctx->qmat_luma[i - 1]  , i);
        scale_mat(QMAT_CHROMA[avctx->profile], ctx->qmat_chroma[i - 1], i);
    }

    return 0;
}

static av_cold int prores_encode_close(AVCodecContext *avctx)
{
    ProresContext* ctx = avctx->priv_data;
    av_freep(&ctx->fill_y);
    av_freep(&ctx->fill_a);

    return 0;
}

#define OFFSET(x) offsetof(ProresContext, x)
#define VE     AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM

static const AVOption options[] = {
    { "vendor", "vendor ID", OFFSET(vendor), AV_OPT_TYPE_STRING, { .str = "fmpg" }, 0, 0, VE },
    { NULL }
};

static const AVClass proresaw_enc_class = {
    .class_name = "ProResAw encoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const AVClass prores_enc_class = {
    .class_name = "ProRes encoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_prores_aw_encoder = {
    .name           = "prores_aw",
    .long_name      = NULL_IF_CONFIG_SMALL("Apple ProRes"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_PRORES,
    .priv_data_size = sizeof(ProresContext),
    .init           = prores_encode_init,
    .close          = prores_encode_close,
    .encode2        = prores_encode_frame,
    .pix_fmts       = (const enum AVPixelFormat[]){AV_PIX_FMT_YUV422P10, AV_PIX_FMT_YUV444P10, AV_PIX_FMT_YUVA444P10, AV_PIX_FMT_NONE},
    .capabilities   = AV_CODEC_CAP_FRAME_THREADS,
    .priv_class     = &proresaw_enc_class,
    .profiles       = NULL_IF_CONFIG_SMALL(ff_prores_profiles),
};

AVCodec ff_prores_encoder = {
    .name           = "prores",
    .long_name      = NULL_IF_CONFIG_SMALL("Apple ProRes"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_PRORES,
    .priv_data_size = sizeof(ProresContext),
    .init           = prores_encode_init,
    .close          = prores_encode_close,
    .encode2        = prores_encode_frame,
    .pix_fmts       = (const enum AVPixelFormat[]){AV_PIX_FMT_YUV422P10, AV_PIX_FMT_YUV444P10, AV_PIX_FMT_YUVA444P10, AV_PIX_FMT_NONE},
    .capabilities   = AV_CODEC_CAP_FRAME_THREADS,
    .priv_class     = &prores_enc_class,
    .profiles       = NULL_IF_CONFIG_SMALL(ff_prores_profiles),
};
