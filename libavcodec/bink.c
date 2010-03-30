/*
 * Bink video decoder
 * Copyright (c) 2009 Konstantin Shishkov
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

#include "avcodec.h"
#include "dsputil.h"
#include "binkdata.h"
#include "mathops.h"

#define ALT_BITSTREAM_READER_LE
#include "get_bits.h"

#define BINK_FLAG_ALPHA 0x00100000
#define BINK_FLAG_GRAY  0x00020000

static VLC bink_trees[16];

/**
 * IDs for different data types used in Bink video codec
 */
enum Sources {
    BINK_SRC_BLOCK_TYPES = 0, ///< 8x8 block types
    BINK_SRC_SUB_BLOCK_TYPES, ///< 16x16 block types (a subset of 8x8 block types)
    BINK_SRC_COLORS,          ///< pixel values used for different block types
    BINK_SRC_PATTERN,         ///< 8-bit values for 2-colour pattern fill
    BINK_SRC_X_OFF,           ///< X components of motion value
    BINK_SRC_Y_OFF,           ///< Y components of motion value
    BINK_SRC_INTRA_DC,        ///< DC values for intrablocks with DCT
    BINK_SRC_INTER_DC,        ///< DC values for interblocks with DCT
    BINK_SRC_RUN,             ///< run lengths for special fill block

    BINK_NB_SRC
};

/**
 * data needed to decode 4-bit Huffman-coded value
 */
typedef struct Tree {
    int     vlc_num;  ///< tree number (in bink_trees[])
    uint8_t syms[16]; ///< leaf value to symbol mapping
} Tree;

#define GET_HUFF(gb, tree)  (tree).syms[get_vlc2(gb, bink_trees[(tree).vlc_num].table,\
                                                 bink_trees[(tree).vlc_num].bits, 1)]

/**
 * data structure used for decoding single Bink data type
 */
typedef struct Bundle {
    int     len;       ///< length of number of entries to decode (in bits)
    Tree    tree;      ///< Huffman tree-related data
    uint8_t *data;     ///< buffer for decoded symbols
    uint8_t *data_end; ///< buffer end
    uint8_t *cur_dec;  ///< pointer to the not yet decoded part of the buffer
    uint8_t *cur_ptr;  ///< pointer to the data that is not read from buffer yet
} Bundle;

/*
 * Decoder context
 */
typedef struct BinkContext {
    AVCodecContext *avctx;
    DSPContext     dsp;
    AVFrame        pic, last;
    int            version;              ///< internal Bink file version
    int            has_alpha;
    int            swap_planes;
    ScanTable      scantable;            ///< permutated scantable for DCT coeffs decoding

    Bundle         bundle[BINK_NB_SRC];  ///< bundles for decoding all data types
    Tree           col_high[16];         ///< trees for decoding high nibble in "colours" data type
    int            col_lastval;          ///< value of last decoded high nibble in "colours" data type
} BinkContext;

/**
 * Bink video block types
 */
enum BlockTypes {
    SKIP_BLOCK = 0, ///< skipped block
    SCALED_BLOCK,   ///< block has size 16x16
    MOTION_BLOCK,   ///< block is copied from previous frame with some offset
    RUN_BLOCK,      ///< block is composed from runs of colours with custom scan order
    RESIDUE_BLOCK,  ///< motion block with some difference added
    INTRA_BLOCK,    ///< intra DCT block
    FILL_BLOCK,     ///< block is filled with single colour
    INTER_BLOCK,    ///< motion block with DCT applied to the difference
    PATTERN_BLOCK,  ///< block is filled with two colours following custom pattern
    RAW_BLOCK,      ///< uncoded 8x8 block
};

/**
 * Initializes length length in all bundles.
 *
 * @param c     decoder context
 * @param width plane width
 * @param bw    plane width in 8x8 blocks
 */
static void init_lengths(BinkContext *c, int width, int bw)
{
    c->bundle[BINK_SRC_BLOCK_TYPES].len = av_log2((width >> 3) + 511) + 1;

    c->bundle[BINK_SRC_SUB_BLOCK_TYPES].len = av_log2((width >> 4) + 511) + 1;

    c->bundle[BINK_SRC_COLORS].len = av_log2((width >> 3)*64 + 511) + 1;

    c->bundle[BINK_SRC_INTRA_DC].len =
    c->bundle[BINK_SRC_INTER_DC].len =
    c->bundle[BINK_SRC_X_OFF].len =
    c->bundle[BINK_SRC_Y_OFF].len = av_log2((width >> 3) + 511) + 1;

    c->bundle[BINK_SRC_PATTERN].len = av_log2((bw << 3) + 511) + 1;

    c->bundle[BINK_SRC_RUN].len = av_log2((width >> 3)*48 + 511) + 1;
}

/**
 * Allocates memory for bundles.
 *
 * @param c decoder context
 */
static av_cold void init_bundles(BinkContext *c)
{
    int bw, bh, blocks;
    int i;

    bw = (c->avctx->width  + 7) >> 3;
    bh = (c->avctx->height + 7) >> 3;
    blocks = bw * bh;

    for (i = 0; i < BINK_NB_SRC; i++) {
        c->bundle[i].data = av_malloc(blocks * 64);
        c->bundle[i].data_end = c->bundle[i].data + blocks * 64;
    }
}

/**
 * Frees memory used by bundles.
 *
 * @param c decoder context
 */
static av_cold void free_bundles(BinkContext *c)
{
    int i;
    for (i = 0; i < BINK_NB_SRC; i++)
        av_freep(&c->bundle[i].data);
}

/**
 * Merges two consequent lists of equal size depending on bits read.
 *
 * @param gb   context for reading bits
 * @param dst  buffer where merged list will be written to
 * @param src  pointer to the head of the first list (the second lists starts at src+size)
 * @param size input lists size
 */
static void merge(GetBitContext *gb, uint8_t *dst, uint8_t *src, int size)
{
    uint8_t *src2 = src + size;
    int size2 = size;

    do {
        if (!get_bits1(gb)) {
            *dst++ = *src++;
            size--;
        } else {
            *dst++ = *src2++;
            size2--;
        }
    } while (size && size2);

    while (size--)
        *dst++ = *src++;
    while (size2--)
        *dst++ = *src2++;
}

/**
 * Reads information about Huffman tree used to decode data.
 *
 * @param gb   context for reading bits
 * @param tree pointer for storing tree data
 */
static void read_tree(GetBitContext *gb, Tree *tree)
{
    uint8_t tmp1[16], tmp2[16], *in = tmp1, *out = tmp2;
    int i, t, len;

    tree->vlc_num = get_bits(gb, 4);
    if (!tree->vlc_num) {
        for (i = 0; i < 16; i++)
            tree->syms[i] = i;
        return;
    }
    if (get_bits1(gb)) {
        len = get_bits(gb, 3);
        memset(tmp1, 0, sizeof(tmp1));
        for (i = 0; i <= len; i++) {
            tree->syms[i] = get_bits(gb, 4);
            tmp1[tree->syms[i]] = 1;
        }
        for (i = 0; i < 16; i++)
            if (!tmp1[i])
                tree->syms[++len] = i;
    } else {
        len = get_bits(gb, 2);
        for (i = 0; i < 16; i++)
            in[i] = i;
        for (i = 0; i <= len; i++) {
            int size = 1 << i;
            for (t = 0; t < 16; t += size << 1)
                merge(gb, out + t, in + t, size);
            FFSWAP(uint8_t*, in, out);
        }
        memcpy(tree->syms, in, 16);
    }
}

/**
 * Prepares bundle for decoding data.
 *
 * @param gb          context for reading bits
 * @param c           decoder context
 * @param bundle_num  number of the bundle to initialize
 */
static void read_bundle(GetBitContext *gb, BinkContext *c, int bundle_num)
{
    int i;

    if (bundle_num == BINK_SRC_COLORS) {
        for (i = 0; i < 16; i++)
            read_tree(gb, &c->col_high[i]);
        c->col_lastval = 0;
    }
    if (bundle_num != BINK_SRC_INTRA_DC && bundle_num != BINK_SRC_INTER_DC)
        read_tree(gb, &c->bundle[bundle_num].tree);
    c->bundle[bundle_num].cur_dec =
    c->bundle[bundle_num].cur_ptr = c->bundle[bundle_num].data;
}

/**
 * common check before starting decoding bundle data
 *
 * @param gb context for reading bits
 * @param b  bundle
 * @param t  variable where number of elements to decode will be stored
 */
#define CHECK_READ_VAL(gb, b, t) \
    if (!b->cur_dec || (b->cur_dec > b->cur_ptr)) \
        return 0; \
    t = get_bits(gb, b->len); \
    if (!t) { \
        b->cur_dec = NULL; \
        return 0; \
    } \

static int read_runs(AVCodecContext *avctx, GetBitContext *gb, Bundle *b)
{
    int t, v;
    const uint8_t *dec_end;

    CHECK_READ_VAL(gb, b, t);
    dec_end = b->cur_dec + t;
    if (dec_end > b->data_end) {
        av_log(avctx, AV_LOG_ERROR, "Run value went out of bounds\n");
        return -1;
    }
    if (get_bits1(gb)) {
        v = get_bits(gb, 4);
        memset(b->cur_dec, v, t);
        b->cur_dec += t;
    } else {
        while (b->cur_dec < dec_end)
            *b->cur_dec++ = GET_HUFF(gb, b->tree);
    }
    return 0;
}

static int read_motion_values(AVCodecContext *avctx, GetBitContext *gb, Bundle *b)
{
    int t, sign, v;
    const uint8_t *dec_end;

    CHECK_READ_VAL(gb, b, t);
    dec_end = b->cur_dec + t;
    if (dec_end > b->data_end) {
        av_log(avctx, AV_LOG_ERROR, "Too many motion values\n");
        return -1;
    }
    if (get_bits1(gb)) {
        v = get_bits(gb, 4);
        if (v) {
            sign = -get_bits1(gb);
            v = (v ^ sign) - sign;
        }
        memset(b->cur_dec, v, t);
        b->cur_dec += t;
    } else {
        do {
            v = GET_HUFF(gb, b->tree);
            if (v) {
                sign = -get_bits1(gb);
                v = (v ^ sign) - sign;
            }
            *b->cur_dec++ = v;
        } while (b->cur_dec < dec_end);
    }
    return 0;
}

const uint8_t bink_rlelens[4] = { 4, 8, 12, 32 };

static int read_block_types(AVCodecContext *avctx, GetBitContext *gb, Bundle *b)
{
    int t, v;
    int last = 0;
    const uint8_t *dec_end;

    CHECK_READ_VAL(gb, b, t);
    dec_end = b->cur_dec + t;
    if (dec_end > b->data_end) {
        av_log(avctx, AV_LOG_ERROR, "Too many block type values\n");
        return -1;
    }
    if (get_bits1(gb)) {
        v = get_bits(gb, 4);
        memset(b->cur_dec, v, t);
        b->cur_dec += t;
    } else {
        do {
            v = GET_HUFF(gb, b->tree);
            if (v < 12) {
                last = v;
                *b->cur_dec++ = v;
            } else {
                int run = bink_rlelens[v - 12];

                memset(b->cur_dec, last, run);
                b->cur_dec += run;
            }
        } while (b->cur_dec < dec_end);
    }
    return 0;
}

static int read_patterns(AVCodecContext *avctx, GetBitContext *gb, Bundle *b)
{
    int t, v;
    const uint8_t *dec_end;

    CHECK_READ_VAL(gb, b, t);
    dec_end = b->cur_dec + t;
    if (dec_end > b->data_end) {
        av_log(avctx, AV_LOG_ERROR, "Too many pattern values\n");
        return -1;
    }
    while (b->cur_dec < dec_end) {
        v  = GET_HUFF(gb, b->tree);
        v |= GET_HUFF(gb, b->tree) << 4;
        *b->cur_dec++ = v;
    }

    return 0;
}

static int read_colors(GetBitContext *gb, Bundle *b, BinkContext *c)
{
    int t, sign, v;
    const uint8_t *dec_end;

    CHECK_READ_VAL(gb, b, t);
    dec_end = b->cur_dec + t;
    if (dec_end > b->data_end) {
        av_log(c->avctx, AV_LOG_ERROR, "Too many color values\n");
        return -1;
    }
    if (get_bits1(gb)) {
        c->col_lastval = GET_HUFF(gb, c->col_high[c->col_lastval]);
        v = GET_HUFF(gb, b->tree);
        v = (c->col_lastval << 4) | v;
        if (c->version < 'i') {
            sign = ((int8_t) v) >> 7;
            v = ((v & 0x7F) ^ sign) - sign;
            v += 0x80;
        }
        memset(b->cur_dec, v, t);
        b->cur_dec += t;
    } else {
        while (b->cur_dec < dec_end) {
            c->col_lastval = GET_HUFF(gb, c->col_high[c->col_lastval]);
            v = GET_HUFF(gb, b->tree);
            v = (c->col_lastval << 4) | v;
            if (c->version < 'i') {
                sign = ((int8_t) v) >> 7;
                v = ((v & 0x7F) ^ sign) - sign;
                v += 0x80;
            }
            *b->cur_dec++ = v;
        }
    }
    return 0;
}

/** number of bits used to store first DC value in bundle */
#define DC_START_BITS 11

static int read_dcs(AVCodecContext *avctx, GetBitContext *gb, Bundle *b,
                    int start_bits, int has_sign)
{
    int i, j, len, len2, bsize, sign, v, v2;
    int16_t *dst = (int16_t*)b->cur_dec;

    CHECK_READ_VAL(gb, b, len);
    v = get_bits(gb, start_bits - has_sign);
    if (v && has_sign) {
        sign = -get_bits1(gb);
        v = (v ^ sign) - sign;
    }
    *dst++ = v;
    len--;
    for (i = 0; i < len; i += 8) {
        len2 = FFMIN(len - i, 8);
        bsize = get_bits(gb, 4);
        if (bsize) {
            for (j = 0; j < len2; j++) {
                v2 = get_bits(gb, bsize);
                if (v2) {
                    sign = -get_bits1(gb);
                    v2 = (v2 ^ sign) - sign;
                }
                v += v2;
                *dst++ = v;
                if (v < -32768 || v > 32767) {
                    av_log(avctx, AV_LOG_ERROR, "DC value went out of bounds: %d\n", v);
                    return -1;
                }
            }
        } else {
            for (j = 0; j < len2; j++)
                *dst++ = v;
        }
    }

    b->cur_dec = (uint8_t*)dst;
    return 0;
}

/**
 * Retrieves next value from bundle.
 *
 * @param c      decoder context
 * @param bundle bundle number
 */
static inline int get_value(BinkContext *c, int bundle)
{
    int16_t ret;

    if (bundle < BINK_SRC_X_OFF || bundle == BINK_SRC_RUN)
        return *c->bundle[bundle].cur_ptr++;
    if (bundle == BINK_SRC_X_OFF || bundle == BINK_SRC_Y_OFF)
        return (int8_t)*c->bundle[bundle].cur_ptr++;
    ret = *(int16_t*)c->bundle[bundle].cur_ptr;
    c->bundle[bundle].cur_ptr += 2;
    return ret;
}

/**
 * Reads 8x8 block of DCT coefficients.
 *
 * @param gb       context for reading bits
 * @param block    place for storing coefficients
 * @param scan     scan order table
 * @param is_intra tells what set of quantizer matrices to use
 * @return 0 for success, negative value in other cases
 */
static int read_dct_coeffs(GetBitContext *gb, DCTELEM block[64], const uint8_t *scan,
                           int is_intra)
{
    int coef_list[128];
    int mode_list[128];
    int i, t, mask, bits, ccoef, mode, sign;
    int list_start = 64, list_end = 64, list_pos;
    int coef_count = 0;
    int coef_idx[64];
    int quant_idx;
    const uint32_t *quant;

    coef_list[list_end] = 4;  mode_list[list_end++] = 0;
    coef_list[list_end] = 24; mode_list[list_end++] = 0;
    coef_list[list_end] = 44; mode_list[list_end++] = 0;
    coef_list[list_end] = 1;  mode_list[list_end++] = 3;
    coef_list[list_end] = 2;  mode_list[list_end++] = 3;
    coef_list[list_end] = 3;  mode_list[list_end++] = 3;

    bits = get_bits(gb, 4) - 1;
    for (mask = 1 << bits; bits >= 0; mask >>= 1, bits--) {
        list_pos = list_start;
        while (list_pos < list_end) {
            if (!(mode_list[list_pos] | coef_list[list_pos]) || !get_bits1(gb)) {
                list_pos++;
                continue;
            }
            ccoef = coef_list[list_pos];
            mode  = mode_list[list_pos];
            switch (mode) {
            case 0:
                coef_list[list_pos] = ccoef + 4;
                mode_list[list_pos] = 1;
            case 2:
                if (mode == 2) {
                    coef_list[list_pos]   = 0;
                    mode_list[list_pos++] = 0;
                }
                for (i = 0; i < 4; i++, ccoef++) {
                    if (get_bits1(gb)) {
                        coef_list[--list_start] = ccoef;
                        mode_list[  list_start] = 3;
                    } else {
                        int t;
                        if (!bits) {
                            t = 1 - (get_bits1(gb) << 1);
                        } else {
                            t = get_bits(gb, bits) | mask;
                            sign = -get_bits1(gb);
                            t = (t ^ sign) - sign;
                        }
                        block[scan[ccoef]] = t;
                        coef_idx[coef_count++] = ccoef;
                    }
                }
                break;
            case 1:
                mode_list[list_pos] = 2;
                for (i = 0; i < 3; i++) {
                    ccoef += 4;
                    coef_list[list_end]   = ccoef;
                    mode_list[list_end++] = 2;
                }
                break;
            case 3:
                if (!bits) {
                    t = 1 - (get_bits1(gb) << 1);
                } else {
                    t = get_bits(gb, bits) | mask;
                    sign = -get_bits1(gb);
                    t = (t ^ sign) - sign;
                }
                block[scan[ccoef]] = t;
                coef_idx[coef_count++] = ccoef;
                coef_list[list_pos]   = 0;
                mode_list[list_pos++] = 0;
                break;
            }
        }
    }

    quant_idx = get_bits(gb, 4);
    quant = is_intra ? bink_intra_quant[quant_idx]
                     : bink_inter_quant[quant_idx];
    block[0] = (block[0] * quant[0]) >> 11;
    for (i = 0; i < coef_count; i++) {
        int idx = coef_idx[i];
        block[scan[idx]] = (block[scan[idx]] * quant[idx]) >> 11;
    }

    return 0;
}

/**
 * Reads 8x8 block with residue after motion compensation.
 *
 * @param gb          context for reading bits
 * @param block       place to store read data
 * @param masks_count number of masks to decode
 * @return 0 on success, negative value in other cases
 */
static int read_residue(GetBitContext *gb, DCTELEM block[64], int masks_count)
{
    int coef_list[128];
    int mode_list[128];
    int i, sign, mask, ccoef, mode;
    int list_start = 64, list_end = 64, list_pos;
    int nz_coeff[64];
    int nz_coeff_count = 0;

    coef_list[list_end] =  4; mode_list[list_end++] = 0;
    coef_list[list_end] = 24; mode_list[list_end++] = 0;
    coef_list[list_end] = 44; mode_list[list_end++] = 0;
    coef_list[list_end] =  0; mode_list[list_end++] = 2;

    for (mask = 1 << get_bits(gb, 3); mask; mask >>= 1) {
        for (i = 0; i < nz_coeff_count; i++) {
            if (!get_bits1(gb))
                continue;
            if (block[nz_coeff[i]] < 0)
                block[nz_coeff[i]] -= mask;
            else
                block[nz_coeff[i]] += mask;
            masks_count--;
            if (masks_count < 0)
                return 0;
        }
        list_pos = list_start;
        while (list_pos < list_end) {
            if (!(coef_list[list_pos] | mode_list[list_pos]) || !get_bits1(gb)) {
                list_pos++;
                continue;
            }
            ccoef = coef_list[list_pos];
            mode  = mode_list[list_pos];
            switch (mode) {
            case 0:
                coef_list[list_pos] = ccoef + 4;
                mode_list[list_pos] = 1;
            case 2:
                if (mode == 2) {
                    coef_list[list_pos]   = 0;
                    mode_list[list_pos++] = 0;
                }
                for (i = 0; i < 4; i++, ccoef++) {
                    if (get_bits1(gb)) {
                        coef_list[--list_start] = ccoef;
                        mode_list[  list_start] = 3;
                    } else {
                        nz_coeff[nz_coeff_count++] = bink_scan[ccoef];
                        sign = -get_bits1(gb);
                        block[bink_scan[ccoef]] = (mask ^ sign) - sign;
                        masks_count--;
                        if (masks_count < 0)
                            return 0;
                    }
                }
                break;
            case 1:
                mode_list[list_pos] = 2;
                for (i = 0; i < 3; i++) {
                    ccoef += 4;
                    coef_list[list_end]   = ccoef;
                    mode_list[list_end++] = 2;
                }
                break;
            case 3:
                nz_coeff[nz_coeff_count++] = bink_scan[ccoef];
                sign = -get_bits1(gb);
                block[bink_scan[ccoef]] = (mask ^ sign) - sign;
                coef_list[list_pos]   = 0;
                mode_list[list_pos++] = 0;
                masks_count--;
                if (masks_count < 0)
                    return 0;
                break;
            }
        }
    }

    return 0;
}

static int bink_decode_plane(BinkContext *c, GetBitContext *gb, int plane_idx,
                             int is_chroma)
{
    int blk;
    int i, j, bx, by;
    uint8_t *dst, *prev, *ref, *ref_start, *ref_end;
    int v, col[2];
    const uint8_t *scan;
    int xoff, yoff;
    DECLARE_ALIGNED(16, DCTELEM, block[64]);
    DECLARE_ALIGNED(16, uint8_t, ublock[64]);
    int coordmap[64];

    const int stride = c->pic.linesize[plane_idx];
    int bw = is_chroma ? (c->avctx->width  + 15) >> 4 : (c->avctx->width  + 7) >> 3;
    int bh = is_chroma ? (c->avctx->height + 15) >> 4 : (c->avctx->height + 7) >> 3;
    int width = c->avctx->width >> is_chroma;

    init_lengths(c, FFMAX(width, 8), bw);
    for (i = 0; i < BINK_NB_SRC; i++)
        read_bundle(gb, c, i);

    ref_start = c->last.data[plane_idx];
    ref_end   = c->last.data[plane_idx]
                + (bw - 1 + c->last.linesize[plane_idx] * (bh - 1)) * 8;

    for (i = 0; i < 64; i++)
        coordmap[i] = (i & 7) + (i >> 3) * stride;

    for (by = 0; by < bh; by++) {
        if (read_block_types(c->avctx, gb, &c->bundle[BINK_SRC_BLOCK_TYPES]) < 0)
            return -1;
        if (read_block_types(c->avctx, gb, &c->bundle[BINK_SRC_SUB_BLOCK_TYPES]) < 0)
            return -1;
        if (read_colors(gb, &c->bundle[BINK_SRC_COLORS], c) < 0)
            return -1;
        if (read_patterns(c->avctx, gb, &c->bundle[BINK_SRC_PATTERN]) < 0)
            return -1;
        if (read_motion_values(c->avctx, gb, &c->bundle[BINK_SRC_X_OFF]) < 0)
            return -1;
        if (read_motion_values(c->avctx, gb, &c->bundle[BINK_SRC_Y_OFF]) < 0)
            return -1;
        if (read_dcs(c->avctx, gb, &c->bundle[BINK_SRC_INTRA_DC], DC_START_BITS, 0) < 0)
            return -1;
        if (read_dcs(c->avctx, gb, &c->bundle[BINK_SRC_INTER_DC], DC_START_BITS, 1) < 0)
            return -1;
        if (read_runs(c->avctx, gb, &c->bundle[BINK_SRC_RUN]) < 0)
            return -1;

        if (by == bh)
            break;
        dst  = c->pic.data[plane_idx]  + 8*by*stride;
        prev = c->last.data[plane_idx] + 8*by*stride;
        for (bx = 0; bx < bw; bx++, dst += 8, prev += 8) {
            blk = get_value(c, BINK_SRC_BLOCK_TYPES);
            // 16x16 block type on odd line means part of the already decoded block, so skip it
            if ((by & 1) && blk == SCALED_BLOCK) {
                bx++;
                dst  += 8;
                prev += 8;
                continue;
            }
            switch (blk) {
            case SKIP_BLOCK:
                c->dsp.put_pixels_tab[1][0](dst, prev, stride, 8);
                break;
            case SCALED_BLOCK:
                blk = get_value(c, BINK_SRC_SUB_BLOCK_TYPES);
                switch (blk) {
                case RUN_BLOCK:
                    scan = bink_patterns[get_bits(gb, 4)];
                    i = 0;
                    do {
                        int run = get_value(c, BINK_SRC_RUN) + 1;

                        i += run;
                        if (i > 64) {
                            av_log(c->avctx, AV_LOG_ERROR, "Run went out of bounds\n");
                            return -1;
                        }
                        if (get_bits1(gb)) {
                            v = get_value(c, BINK_SRC_COLORS);
                            for (j = 0; j < run; j++)
                                ublock[*scan++] = v;
                        } else {
                            for (j = 0; j < run; j++)
                                ublock[*scan++] = get_value(c, BINK_SRC_COLORS);
                        }
                    } while (i < 63);
                    if (i == 63)
                        ublock[*scan++] = get_value(c, BINK_SRC_COLORS);
                    break;
                case INTRA_BLOCK:
                    c->dsp.clear_block(block);
                    block[0] = get_value(c, BINK_SRC_INTRA_DC);
                    read_dct_coeffs(gb, block, c->scantable.permutated, 1);
                    c->dsp.idct(block);
                    c->dsp.put_pixels_nonclamped(block, ublock, 8);
                    break;
                case FILL_BLOCK:
                    v = get_value(c, BINK_SRC_COLORS);
                    c->dsp.fill_block_tab[0](dst, v, stride, 16);
                    break;
                case PATTERN_BLOCK:
                    for (i = 0; i < 2; i++)
                        col[i] = get_value(c, BINK_SRC_COLORS);
                    for (j = 0; j < 8; j++) {
                        v = get_value(c, BINK_SRC_PATTERN);
                        for (i = 0; i < 8; i++, v >>= 1)
                            ublock[i + j*8] = col[v & 1];
                    }
                    break;
                case RAW_BLOCK:
                    for (j = 0; j < 8; j++)
                        for (i = 0; i < 8; i++)
                            ublock[i + j*8] = get_value(c, BINK_SRC_COLORS);
                    break;
                default:
                    av_log(c->avctx, AV_LOG_ERROR, "Incorrect 16x16 block type %d\n", blk);
                    return -1;
                }
                if (blk != FILL_BLOCK)
                c->dsp.scale_block(ublock, dst, stride);
                bx++;
                dst  += 8;
                prev += 8;
                break;
            case MOTION_BLOCK:
                xoff = get_value(c, BINK_SRC_X_OFF);
                yoff = get_value(c, BINK_SRC_Y_OFF);
                ref = prev + xoff + yoff * stride;
                if (ref < ref_start || ref > ref_end) {
                    av_log(c->avctx, AV_LOG_ERROR, "Copy out of bounds @%d, %d\n",
                           bx*8 + xoff, by*8 + yoff);
                    return -1;
                }
                c->dsp.put_pixels_tab[1][0](dst, ref, stride, 8);
                break;
            case RUN_BLOCK:
                scan = bink_patterns[get_bits(gb, 4)];
                i = 0;
                do {
                    int run = get_value(c, BINK_SRC_RUN) + 1;

                    i += run;
                    if (i > 64) {
                        av_log(c->avctx, AV_LOG_ERROR, "Run went out of bounds\n");
                        return -1;
                    }
                    if (get_bits1(gb)) {
                        v = get_value(c, BINK_SRC_COLORS);
                        for (j = 0; j < run; j++)
                            dst[coordmap[*scan++]] = v;
                    } else {
                        for (j = 0; j < run; j++)
                            dst[coordmap[*scan++]] = get_value(c, BINK_SRC_COLORS);
                    }
                } while (i < 63);
                if (i == 63)
                    dst[coordmap[*scan++]] = get_value(c, BINK_SRC_COLORS);
                break;
            case RESIDUE_BLOCK:
                xoff = get_value(c, BINK_SRC_X_OFF);
                yoff = get_value(c, BINK_SRC_Y_OFF);
                ref = prev + xoff + yoff * stride;
                if (ref < ref_start || ref > ref_end) {
                    av_log(c->avctx, AV_LOG_ERROR, "Copy out of bounds @%d, %d\n",
                           bx*8 + xoff, by*8 + yoff);
                    return -1;
                }
                c->dsp.put_pixels_tab[1][0](dst, ref, stride, 8);
                c->dsp.clear_block(block);
                v = get_bits(gb, 7);
                read_residue(gb, block, v);
                c->dsp.add_pixels8(dst, block, stride);
                break;
            case INTRA_BLOCK:
                c->dsp.clear_block(block);
                block[0] = get_value(c, BINK_SRC_INTRA_DC);
                read_dct_coeffs(gb, block, c->scantable.permutated, 1);
                c->dsp.idct_put(dst, stride, block);
                break;
            case FILL_BLOCK:
                v = get_value(c, BINK_SRC_COLORS);
                c->dsp.fill_block_tab[1](dst, v, stride, 8);
                break;
            case INTER_BLOCK:
                xoff = get_value(c, BINK_SRC_X_OFF);
                yoff = get_value(c, BINK_SRC_Y_OFF);
                ref = prev + xoff + yoff * stride;
                c->dsp.put_pixels_tab[1][0](dst, ref, stride, 8);
                c->dsp.clear_block(block);
                block[0] = get_value(c, BINK_SRC_INTER_DC);
                read_dct_coeffs(gb, block, c->scantable.permutated, 0);
                c->dsp.idct_add(dst, stride, block);
                break;
            case PATTERN_BLOCK:
                for (i = 0; i < 2; i++)
                    col[i] = get_value(c, BINK_SRC_COLORS);
                for (i = 0; i < 8; i++) {
                    v = get_value(c, BINK_SRC_PATTERN);
                    for (j = 0; j < 8; j++, v >>= 1)
                        dst[i*stride + j] = col[v & 1];
                }
                break;
            case RAW_BLOCK:
                for (i = 0; i < 8; i++)
                    memcpy(dst + i*stride, c->bundle[BINK_SRC_COLORS].cur_ptr + i*8, 8);
                c->bundle[BINK_SRC_COLORS].cur_ptr += 64;
                break;
            default:
                av_log(c->avctx, AV_LOG_ERROR, "Unknown block type %d\n", blk);
                return -1;
            }
        }
    }
    if (get_bits_count(gb) & 0x1F) //next plane data starts at 32-bit boundary
        skip_bits_long(gb, 32 - (get_bits_count(gb) & 0x1F));

    return 0;
}

static int decode_frame(AVCodecContext *avctx, void *data, int *data_size, AVPacket *pkt)
{
    BinkContext * const c = avctx->priv_data;
    GetBitContext gb;
    int plane, plane_idx;
    int bits_count = pkt->size << 3;

    if(c->pic.data[0])
        avctx->release_buffer(avctx, &c->pic);

    if(avctx->get_buffer(avctx, &c->pic) < 0){
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return -1;
    }

    init_get_bits(&gb, pkt->data, bits_count);
    if (c->has_alpha) {
        if (c->version >= 'i')
            skip_bits_long(&gb, 32);
        if (bink_decode_plane(c, &gb, 3, 0) < 0)
            return -1;
    }
    if (c->version >= 'i')
        skip_bits_long(&gb, 32);

    for (plane = 0; plane < 3; plane++) {
        plane_idx = (!plane || !c->swap_planes) ? plane : (plane ^ 3);

        if (bink_decode_plane(c, &gb, plane_idx, !!plane) < 0)
            return -1;
        if (get_bits_count(&gb) >= bits_count)
            break;
    }
    emms_c();

    *data_size = sizeof(AVFrame);
    *(AVFrame*)data = c->pic;

    FFSWAP(AVFrame, c->pic, c->last);

    /* always report that the buffer was completely consumed */
    return pkt->size;
}

static av_cold int decode_init(AVCodecContext *avctx)
{
    BinkContext * const c = avctx->priv_data;
    static VLC_TYPE table[16 * 128][2];
    int i;
    int flags;

    c->version = avctx->codec_tag >> 24;
    if (c->version < 'c') {
        av_log(avctx, AV_LOG_ERROR, "Too old version '%c'\n", c->version);
        return -1;
    }
    if (avctx->extradata_size < 4) {
        av_log(avctx, AV_LOG_ERROR, "Extradata missing or too short\n");
        return -1;
    }
    flags = AV_RL32(avctx->extradata);
    c->has_alpha = flags & BINK_FLAG_ALPHA;
    c->swap_planes = c->version >= 'h';
    if (!bink_trees[15].table) {
        for (i = 0; i < 16; i++) {
            const int maxbits = bink_tree_lens[i][15];
            bink_trees[i].table = table + i*128;
            bink_trees[i].table_allocated = 1 << maxbits;
            init_vlc(&bink_trees[i], maxbits, 16,
                     bink_tree_lens[i], 1, 1,
                     bink_tree_bits[i], 1, 1, INIT_VLC_USE_NEW_STATIC | INIT_VLC_LE);
        }
    }
    c->avctx = avctx;

    c->pic.data[0] = NULL;

    if (avcodec_check_dimensions(avctx, avctx->width, avctx->height) < 0) {
        return 1;
    }

    avctx->pix_fmt = c->has_alpha ? PIX_FMT_YUVA420P : PIX_FMT_YUV420P;

    avctx->idct_algo = FF_IDCT_BINK;
    dsputil_init(&c->dsp, avctx);
    ff_init_scantable(c->dsp.idct_permutation, &c->scantable, bink_scan);

    init_bundles(c);

    return 0;
}

static av_cold int decode_end(AVCodecContext *avctx)
{
    BinkContext * const c = avctx->priv_data;

    if (c->pic.data[0])
        avctx->release_buffer(avctx, &c->pic);
    if (c->last.data[0])
        avctx->release_buffer(avctx, &c->last);

    free_bundles(c);
    return 0;
}

AVCodec bink_decoder = {
    "binkvideo",
    AVMEDIA_TYPE_VIDEO,
    CODEC_ID_BINKVIDEO,
    sizeof(BinkContext),
    decode_init,
    NULL,
    decode_end,
    decode_frame,
    .long_name = NULL_IF_CONFIG_SMALL("Bink video"),
};
