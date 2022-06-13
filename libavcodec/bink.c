/*
 * Bink video decoder
 * Copyright (c) 2009 Konstantin Shishkov
 * Copyright (C) 2011 Peter Ross <pross@xvid.org>
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

#include "libavutil/attributes.h"
#include "libavutil/imgutils.h"
#include "libavutil/internal.h"
#include "libavutil/mem_internal.h"

#define BITSTREAM_READER_LE
#include "avcodec.h"
#include "binkdata.h"
#include "binkdsp.h"
#include "blockdsp.h"
#include "get_bits.h"
#include "hpeldsp.h"
#include "internal.h"
#include "mathops.h"

#define BINK_FLAG_ALPHA 0x00100000
#define BINK_FLAG_GRAY  0x00020000

static VLC bink_trees[16];

/**
 * IDs for different data types used in old version of Bink video codec
 */
enum OldSources {
    BINKB_SRC_BLOCK_TYPES = 0, ///< 8x8 block types
    BINKB_SRC_COLORS,          ///< pixel values used for different block types
    BINKB_SRC_PATTERN,         ///< 8-bit values for 2-colour pattern fill
    BINKB_SRC_X_OFF,           ///< X components of motion value
    BINKB_SRC_Y_OFF,           ///< Y components of motion value
    BINKB_SRC_INTRA_DC,        ///< DC values for intrablocks with DCT
    BINKB_SRC_INTER_DC,        ///< DC values for interblocks with DCT
    BINKB_SRC_INTRA_Q,         ///< quantizer values for intrablocks with DCT
    BINKB_SRC_INTER_Q,         ///< quantizer values for interblocks with DCT
    BINKB_SRC_INTER_COEFS,     ///< number of coefficients for residue blocks

    BINKB_NB_SRC
};

static const int binkb_bundle_sizes[BINKB_NB_SRC] = {
    4, 8, 8, 5, 5, 11, 11, 4, 4, 7
};

static const int binkb_bundle_signed[BINKB_NB_SRC] = {
    0, 0, 0, 1, 1, 0, 1, 0, 0, 0
};

static int32_t binkb_intra_quant[16][64];
static int32_t binkb_inter_quant[16][64];

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
    BlockDSPContext bdsp;
    op_pixels_func put_pixels_tab;
    BinkDSPContext binkdsp;
    AVFrame        *last;
    int            version;              ///< internal Bink file version
    int            has_alpha;
    int            swap_planes;
    unsigned       frame_num;

    Bundle         bundle[BINKB_NB_SRC]; ///< bundles for decoding all data types
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
 * Initialize length in all bundles.
 *
 * @param c     decoder context
 * @param width plane width
 * @param bw    plane width in 8x8 blocks
 */
static void init_lengths(BinkContext *c, int width, int bw)
{
    width = FFALIGN(width, 8);

    c->bundle[BINK_SRC_BLOCK_TYPES].len = av_log2((width >> 3) + 511) + 1;

    c->bundle[BINK_SRC_SUB_BLOCK_TYPES].len = av_log2((width >> 4) + 511) + 1;

    c->bundle[BINK_SRC_COLORS].len = av_log2(bw*64 + 511) + 1;

    c->bundle[BINK_SRC_INTRA_DC].len =
    c->bundle[BINK_SRC_INTER_DC].len =
    c->bundle[BINK_SRC_X_OFF].len =
    c->bundle[BINK_SRC_Y_OFF].len = av_log2((width >> 3) + 511) + 1;

    c->bundle[BINK_SRC_PATTERN].len = av_log2((bw << 3) + 511) + 1;

    c->bundle[BINK_SRC_RUN].len = av_log2(bw*48 + 511) + 1;
}

/**
 * Allocate memory for bundles.
 *
 * @param c decoder context
 */
static av_cold int init_bundles(BinkContext *c)
{
    int bw, bh, blocks;
    uint8_t *tmp;
    int i;

    bw = (c->avctx->width  + 7) >> 3;
    bh = (c->avctx->height + 7) >> 3;
    blocks = bw * bh;

    tmp = av_calloc(blocks, 64 * BINKB_NB_SRC);
    if (!tmp)
        return AVERROR(ENOMEM);
    for (i = 0; i < BINKB_NB_SRC; i++) {
        c->bundle[i].data     = tmp;
        tmp                  += blocks * 64;
        c->bundle[i].data_end = tmp;
    }

    return 0;
}

/**
 * Free memory used by bundles.
 *
 * @param c decoder context
 */
static av_cold void free_bundles(BinkContext *c)
{
    av_freep(&c->bundle[0].data);
}

/**
 * Merge two consequent lists of equal size depending on bits read.
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
 * Read information about Huffman tree used to decode data.
 *
 * @param gb   context for reading bits
 * @param tree pointer for storing tree data
 */
static int read_tree(GetBitContext *gb, Tree *tree)
{
    uint8_t tmp1[16] = { 0 }, tmp2[16], *in = tmp1, *out = tmp2;
    int i, t, len;

    if (get_bits_left(gb) < 4)
        return AVERROR_INVALIDDATA;

    tree->vlc_num = get_bits(gb, 4);
    if (!tree->vlc_num) {
        for (i = 0; i < 16; i++)
            tree->syms[i] = i;
        return 0;
    }
    if (get_bits1(gb)) {
        len = get_bits(gb, 3);
        for (i = 0; i <= len; i++) {
            tree->syms[i] = get_bits(gb, 4);
            tmp1[tree->syms[i]] = 1;
        }
        for (i = 0; i < 16 && len < 16 - 1; i++)
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
    return 0;
}

/**
 * Prepare bundle for decoding data.
 *
 * @param gb          context for reading bits
 * @param c           decoder context
 * @param bundle_num  number of the bundle to initialize
 */
static int read_bundle(GetBitContext *gb, BinkContext *c, int bundle_num)
{
    int i;

    if (bundle_num == BINK_SRC_COLORS) {
        for (i = 0; i < 16; i++) {
            int ret = read_tree(gb, &c->col_high[i]);
            if (ret < 0)
                return ret;
        }
        c->col_lastval = 0;
    }
    if (bundle_num != BINK_SRC_INTRA_DC && bundle_num != BINK_SRC_INTER_DC) {
        int ret = read_tree(gb, &c->bundle[bundle_num].tree);
        if (ret < 0)
            return ret;
    }
    c->bundle[bundle_num].cur_dec =
    c->bundle[bundle_num].cur_ptr = c->bundle[bundle_num].data;

    return 0;
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
        return AVERROR_INVALIDDATA;
    }
    if (get_bits_left(gb) < 1)
        return AVERROR_INVALIDDATA;
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
        return AVERROR_INVALIDDATA;
    }
    if (get_bits_left(gb) < 1)
        return AVERROR_INVALIDDATA;
    if (get_bits1(gb)) {
        v = get_bits(gb, 4);
        if (v) {
            sign = -get_bits1(gb);
            v = (v ^ sign) - sign;
        }
        memset(b->cur_dec, v, t);
        b->cur_dec += t;
    } else {
        while (b->cur_dec < dec_end) {
            v = GET_HUFF(gb, b->tree);
            if (v) {
                sign = -get_bits1(gb);
                v = (v ^ sign) - sign;
            }
            *b->cur_dec++ = v;
        }
    }
    return 0;
}

static const uint8_t bink_rlelens[4] = { 4, 8, 12, 32 };

static int read_block_types(AVCodecContext *avctx, GetBitContext *gb, Bundle *b)
{
    BinkContext * const c = avctx->priv_data;
    int t, v;
    int last = 0;
    const uint8_t *dec_end;

    CHECK_READ_VAL(gb, b, t);
    if (c->version == 'k') {
        t ^= 0xBBu;
        if (t == 0) {
            b->cur_dec = NULL;
            return 0;
        }
    }
    dec_end = b->cur_dec + t;
    if (dec_end > b->data_end) {
        av_log(avctx, AV_LOG_ERROR, "Too many block type values\n");
        return AVERROR_INVALIDDATA;
    }
    if (get_bits_left(gb) < 1)
        return AVERROR_INVALIDDATA;
    if (get_bits1(gb)) {
        v = get_bits(gb, 4);
        memset(b->cur_dec, v, t);
        b->cur_dec += t;
    } else {
        while (b->cur_dec < dec_end) {
            v = GET_HUFF(gb, b->tree);
            if (v < 12) {
                last = v;
                *b->cur_dec++ = v;
            } else {
                int run = bink_rlelens[v - 12];

                if (dec_end - b->cur_dec < run)
                    return AVERROR_INVALIDDATA;
                memset(b->cur_dec, last, run);
                b->cur_dec += run;
            }
        }
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
        return AVERROR_INVALIDDATA;
    }
    while (b->cur_dec < dec_end) {
        if (get_bits_left(gb) < 2)
            return AVERROR_INVALIDDATA;
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
        return AVERROR_INVALIDDATA;
    }
    if (get_bits_left(gb) < 1)
        return AVERROR_INVALIDDATA;
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
            if (get_bits_left(gb) < 2)
                return AVERROR_INVALIDDATA;
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
    int16_t *dst     = (int16_t*)b->cur_dec;
    int16_t *dst_end = (int16_t*)b->data_end;

    CHECK_READ_VAL(gb, b, len);
    if (get_bits_left(gb) < start_bits - has_sign)
        return AVERROR_INVALIDDATA;
    v = get_bits(gb, start_bits - has_sign);
    if (v && has_sign) {
        sign = -get_bits1(gb);
        v = (v ^ sign) - sign;
    }
    if (dst_end - dst < 1)
        return AVERROR_INVALIDDATA;
    *dst++ = v;
    len--;
    for (i = 0; i < len; i += 8) {
        len2 = FFMIN(len - i, 8);
        if (dst_end - dst < len2)
            return AVERROR_INVALIDDATA;
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
                    return AVERROR_INVALIDDATA;
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
 * Retrieve next value from bundle.
 *
 * @param c      decoder context
 * @param bundle bundle number
 */
static inline int get_value(BinkContext *c, int bundle)
{
    int ret;

    if (bundle < BINK_SRC_X_OFF || bundle == BINK_SRC_RUN)
        return *c->bundle[bundle].cur_ptr++;
    if (bundle == BINK_SRC_X_OFF || bundle == BINK_SRC_Y_OFF)
        return (int8_t)*c->bundle[bundle].cur_ptr++;
    ret = *(int16_t*)c->bundle[bundle].cur_ptr;
    c->bundle[bundle].cur_ptr += 2;
    return ret;
}

static av_cold void binkb_init_bundle(BinkContext *c, int bundle_num)
{
    c->bundle[bundle_num].cur_dec =
    c->bundle[bundle_num].cur_ptr = c->bundle[bundle_num].data;
    c->bundle[bundle_num].len = 13;
}

static av_cold void binkb_init_bundles(BinkContext *c)
{
    int i;
    for (i = 0; i < BINKB_NB_SRC; i++)
        binkb_init_bundle(c, i);
}

static int binkb_read_bundle(BinkContext *c, GetBitContext *gb, int bundle_num)
{
    const int bits = binkb_bundle_sizes[bundle_num];
    const int mask = 1 << (bits - 1);
    const int issigned = binkb_bundle_signed[bundle_num];
    Bundle *b = &c->bundle[bundle_num];
    int i, len;

    CHECK_READ_VAL(gb, b, len);
    if (b->data_end - b->cur_dec < len * (1 + (bits > 8)))
        return AVERROR_INVALIDDATA;
    if (bits <= 8) {
        if (!issigned) {
            for (i = 0; i < len; i++)
                *b->cur_dec++ = get_bits(gb, bits);
        } else {
            for (i = 0; i < len; i++)
                *b->cur_dec++ = get_bits(gb, bits) - mask;
        }
    } else {
        int16_t *dst = (int16_t*)b->cur_dec;

        if (!issigned) {
            for (i = 0; i < len; i++)
                *dst++ = get_bits(gb, bits);
        } else {
            for (i = 0; i < len; i++)
                *dst++ = get_bits(gb, bits) - mask;
        }
        b->cur_dec = (uint8_t*)dst;
    }
    return 0;
}

static inline int binkb_get_value(BinkContext *c, int bundle_num)
{
    int16_t ret;
    const int bits = binkb_bundle_sizes[bundle_num];

    if (bits <= 8) {
        int val = *c->bundle[bundle_num].cur_ptr++;
        return binkb_bundle_signed[bundle_num] ? (int8_t)val : val;
    }
    ret = *(int16_t*)c->bundle[bundle_num].cur_ptr;
    c->bundle[bundle_num].cur_ptr += 2;
    return ret;
}

/**
 * Read 8x8 block of DCT coefficients.
 *
 * @param gb       context for reading bits
 * @param block    place for storing coefficients
 * @param scan     scan order table
 * @param quant_matrices quantization matrices
 * @return 0 for success, negative value in other cases
 */
static int read_dct_coeffs(BinkContext *c, GetBitContext *gb, int32_t block[64],
                           const uint8_t *scan, int *coef_count_,
                           int coef_idx[64], int q)
{
    int coef_list[128];
    int mode_list[128];
    int i, t, bits, ccoef, mode, sign;
    int list_start = 64, list_end = 64, list_pos;
    int coef_count = 0;
    int quant_idx;

    if (get_bits_left(gb) < 4)
        return AVERROR_INVALIDDATA;

    coef_list[list_end] = 4;  mode_list[list_end++] = 0;
    coef_list[list_end] = 24; mode_list[list_end++] = 0;
    coef_list[list_end] = 44; mode_list[list_end++] = 0;
    coef_list[list_end] = 1;  mode_list[list_end++] = 3;
    coef_list[list_end] = 2;  mode_list[list_end++] = 3;
    coef_list[list_end] = 3;  mode_list[list_end++] = 3;

    for (bits = get_bits(gb, 4) - 1; bits >= 0; bits--) {
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
                        if (!bits) {
                            t = 1 - (get_bits1(gb) << 1);
                        } else {
                            t = get_bits(gb, bits) | 1 << bits;
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
                    t = get_bits(gb, bits) | 1 << bits;
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

    if (q == -1) {
        quant_idx = get_bits(gb, 4);
    } else {
        quant_idx = q;
        if (quant_idx > 15U) {
            av_log(c->avctx, AV_LOG_ERROR, "quant_index %d out of range\n", quant_idx);
            return AVERROR_INVALIDDATA;
        }
    }

    *coef_count_ = coef_count;

    return quant_idx;
}

static void unquantize_dct_coeffs(int32_t block[64], const uint32_t quant[64],
                                  int coef_count, int coef_idx[64],
                                  const uint8_t *scan)
{
    int i;
    block[0] = (int)(block[0] * quant[0]) >> 11;
    for (i = 0; i < coef_count; i++) {
        int idx = coef_idx[i];
        block[scan[idx]] = (int)(block[scan[idx]] * quant[idx]) >> 11;
    }
}

/**
 * Read 8x8 block with residue after motion compensation.
 *
 * @param gb          context for reading bits
 * @param block       place to store read data
 * @param masks_count number of masks to decode
 * @return 0 on success, negative value in other cases
 */
static int read_residue(GetBitContext *gb, int16_t block[64], int masks_count)
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

/**
 * Copy 8x8 block from source to destination, where src and dst may be overlapped
 */
static inline void put_pixels8x8_overlapped(uint8_t *dst, uint8_t *src, int stride)
{
    uint8_t tmp[64];
    int i;
    for (i = 0; i < 8; i++)
        memcpy(tmp + i*8, src + i*stride, 8);
    for (i = 0; i < 8; i++)
        memcpy(dst + i*stride, tmp + i*8, 8);
}

static int binkb_decode_plane(BinkContext *c, AVFrame *frame, GetBitContext *gb,
                              int plane_idx, int is_key, int is_chroma)
{
    int blk, ret;
    int i, j, bx, by;
    uint8_t *dst, *ref, *ref_start, *ref_end;
    int v, col[2];
    const uint8_t *scan;
    int xoff, yoff;
    LOCAL_ALIGNED_32(int16_t, block, [64]);
    LOCAL_ALIGNED_16(int32_t, dctblock, [64]);
    int coordmap[64];
    int ybias = is_key ? -15 : 0;
    int qp, quant_idx, coef_count, coef_idx[64];

    const int stride = frame->linesize[plane_idx];
    int bw = is_chroma ? (c->avctx->width  + 15) >> 4 : (c->avctx->width  + 7) >> 3;
    int bh = is_chroma ? (c->avctx->height + 15) >> 4 : (c->avctx->height + 7) >> 3;

    binkb_init_bundles(c);
    ref_start = frame->data[plane_idx];
    ref_end   = frame->data[plane_idx] + (bh * frame->linesize[plane_idx] + bw) * 8;

    for (i = 0; i < 64; i++)
        coordmap[i] = (i & 7) + (i >> 3) * stride;

    for (by = 0; by < bh; by++) {
        for (i = 0; i < BINKB_NB_SRC; i++) {
            if ((ret = binkb_read_bundle(c, gb, i)) < 0)
                return ret;
        }

        dst  = frame->data[plane_idx]  + 8*by*stride;
        for (bx = 0; bx < bw; bx++, dst += 8) {
            blk = binkb_get_value(c, BINKB_SRC_BLOCK_TYPES);
            switch (blk) {
            case 0:
                break;
            case 1:
                scan = bink_patterns[get_bits(gb, 4)];
                i = 0;
                do {
                    int mode, run;

                    mode = get_bits1(gb);
                    run = get_bits(gb, binkb_runbits[i]) + 1;

                    i += run;
                    if (i > 64) {
                        av_log(c->avctx, AV_LOG_ERROR, "Run went out of bounds\n");
                        return AVERROR_INVALIDDATA;
                    }
                    if (mode) {
                        v = binkb_get_value(c, BINKB_SRC_COLORS);
                        for (j = 0; j < run; j++)
                            dst[coordmap[*scan++]] = v;
                    } else {
                        for (j = 0; j < run; j++)
                            dst[coordmap[*scan++]] = binkb_get_value(c, BINKB_SRC_COLORS);
                    }
                } while (i < 63);
                if (i == 63)
                    dst[coordmap[*scan++]] = binkb_get_value(c, BINKB_SRC_COLORS);
                break;
            case 2:
                memset(dctblock, 0, sizeof(*dctblock) * 64);
                dctblock[0] = binkb_get_value(c, BINKB_SRC_INTRA_DC);
                qp = binkb_get_value(c, BINKB_SRC_INTRA_Q);
                if ((quant_idx = read_dct_coeffs(c, gb, dctblock, bink_scan, &coef_count, coef_idx, qp)) < 0)
                    return quant_idx;
                unquantize_dct_coeffs(dctblock, binkb_intra_quant[quant_idx], coef_count, coef_idx, bink_scan);
                c->binkdsp.idct_put(dst, stride, dctblock);
                break;
            case 3:
                xoff = binkb_get_value(c, BINKB_SRC_X_OFF);
                yoff = binkb_get_value(c, BINKB_SRC_Y_OFF) + ybias;
                ref = dst + xoff + yoff * stride;
                if (ref < ref_start || ref + 8*stride > ref_end) {
                    av_log(c->avctx, AV_LOG_WARNING, "Reference block is out of bounds\n");
                } else if (ref + 8*stride < dst || ref >= dst + 8*stride) {
                    c->put_pixels_tab(dst, ref, stride, 8);
                } else {
                    put_pixels8x8_overlapped(dst, ref, stride);
                }
                c->bdsp.clear_block(block);
                v = binkb_get_value(c, BINKB_SRC_INTER_COEFS);
                read_residue(gb, block, v);
                c->binkdsp.add_pixels8(dst, block, stride);
                break;
            case 4:
                xoff = binkb_get_value(c, BINKB_SRC_X_OFF);
                yoff = binkb_get_value(c, BINKB_SRC_Y_OFF) + ybias;
                ref = dst + xoff + yoff * stride;
                if (ref < ref_start || ref + 8 * stride > ref_end) {
                    av_log(c->avctx, AV_LOG_WARNING, "Reference block is out of bounds\n");
                } else if (ref + 8*stride < dst || ref >= dst + 8*stride) {
                    c->put_pixels_tab(dst, ref, stride, 8);
                } else {
                    put_pixels8x8_overlapped(dst, ref, stride);
                }
                memset(dctblock, 0, sizeof(*dctblock) * 64);
                dctblock[0] = binkb_get_value(c, BINKB_SRC_INTER_DC);
                qp = binkb_get_value(c, BINKB_SRC_INTER_Q);
                if ((quant_idx = read_dct_coeffs(c, gb, dctblock, bink_scan, &coef_count, coef_idx, qp)) < 0)
                    return quant_idx;
                unquantize_dct_coeffs(dctblock, binkb_inter_quant[quant_idx], coef_count, coef_idx, bink_scan);
                c->binkdsp.idct_add(dst, stride, dctblock);
                break;
            case 5:
                v = binkb_get_value(c, BINKB_SRC_COLORS);
                c->bdsp.fill_block_tab[1](dst, v, stride, 8);
                break;
            case 6:
                for (i = 0; i < 2; i++)
                    col[i] = binkb_get_value(c, BINKB_SRC_COLORS);
                for (i = 0; i < 8; i++) {
                    v = binkb_get_value(c, BINKB_SRC_PATTERN);
                    for (j = 0; j < 8; j++, v >>= 1)
                        dst[i*stride + j] = col[v & 1];
                }
                break;
            case 7:
                xoff = binkb_get_value(c, BINKB_SRC_X_OFF);
                yoff = binkb_get_value(c, BINKB_SRC_Y_OFF) + ybias;
                ref = dst + xoff + yoff * stride;
                if (ref < ref_start || ref + 8 * stride > ref_end) {
                    av_log(c->avctx, AV_LOG_WARNING, "Reference block is out of bounds\n");
                } else if (ref + 8*stride < dst || ref >= dst + 8*stride) {
                    c->put_pixels_tab(dst, ref, stride, 8);
                } else {
                    put_pixels8x8_overlapped(dst, ref, stride);
                }
                break;
            case 8:
                for (i = 0; i < 8; i++)
                    memcpy(dst + i*stride, c->bundle[BINKB_SRC_COLORS].cur_ptr + i*8, 8);
                c->bundle[BINKB_SRC_COLORS].cur_ptr += 64;
                break;
            default:
                av_log(c->avctx, AV_LOG_ERROR, "Unknown block type %d\n", blk);
                return AVERROR_INVALIDDATA;
            }
        }
    }
    if (get_bits_count(gb) & 0x1F) //next plane data starts at 32-bit boundary
        skip_bits_long(gb, 32 - (get_bits_count(gb) & 0x1F));

    return 0;
}

static int bink_put_pixels(BinkContext *c,
                           uint8_t *dst, uint8_t *prev, int stride,
                           uint8_t *ref_start,
                           uint8_t *ref_end)
{
    int xoff     = get_value(c, BINK_SRC_X_OFF);
    int yoff     = get_value(c, BINK_SRC_Y_OFF);
    uint8_t *ref = prev + xoff + yoff * stride;
    if (ref < ref_start || ref > ref_end) {
        av_log(c->avctx, AV_LOG_ERROR, "Copy out of bounds @%d, %d\n",
               xoff, yoff);
        return AVERROR_INVALIDDATA;
    }
    c->put_pixels_tab(dst, ref, stride, 8);

    return 0;
}

static int bink_decode_plane(BinkContext *c, AVFrame *frame, GetBitContext *gb,
                             int plane_idx, int is_chroma)
{
    int blk, ret;
    int i, j, bx, by;
    uint8_t *dst, *prev, *ref_start, *ref_end;
    int v, col[2];
    const uint8_t *scan;
    LOCAL_ALIGNED_32(int16_t, block, [64]);
    LOCAL_ALIGNED_16(uint8_t, ublock, [64]);
    LOCAL_ALIGNED_16(int32_t, dctblock, [64]);
    int coordmap[64], quant_idx, coef_count, coef_idx[64];

    const int stride = frame->linesize[plane_idx];
    int bw = is_chroma ? (c->avctx->width  + 15) >> 4 : (c->avctx->width  + 7) >> 3;
    int bh = is_chroma ? (c->avctx->height + 15) >> 4 : (c->avctx->height + 7) >> 3;
    int width = c->avctx->width >> is_chroma;
    int height = c->avctx->height >> is_chroma;

    if (c->version == 'k' && get_bits1(gb)) {
        int fill = get_bits(gb, 8);

        dst = frame->data[plane_idx];

        for (i = 0; i < height; i++)
            memset(dst + i * stride, fill, width);
        goto end;
    }

    init_lengths(c, FFMAX(width, 8), bw);
    for (i = 0; i < BINK_NB_SRC; i++) {
        ret = read_bundle(gb, c, i);
        if (ret < 0)
            return ret;
    }

    ref_start = c->last->data[plane_idx] ? c->last->data[plane_idx]
                                         : frame->data[plane_idx];
    ref_end   = ref_start
                + (bw - 1 + c->last->linesize[plane_idx] * (bh - 1)) * 8;

    for (i = 0; i < 64; i++)
        coordmap[i] = (i & 7) + (i >> 3) * stride;

    for (by = 0; by < bh; by++) {
        if ((ret = read_block_types(c->avctx, gb, &c->bundle[BINK_SRC_BLOCK_TYPES])) < 0)
            return ret;
        if ((ret = read_block_types(c->avctx, gb, &c->bundle[BINK_SRC_SUB_BLOCK_TYPES])) < 0)
            return ret;
        if ((ret = read_colors(gb, &c->bundle[BINK_SRC_COLORS], c)) < 0)
            return ret;
        if ((ret = read_patterns(c->avctx, gb, &c->bundle[BINK_SRC_PATTERN])) < 0)
            return ret;
        if ((ret = read_motion_values(c->avctx, gb, &c->bundle[BINK_SRC_X_OFF])) < 0)
            return ret;
        if ((ret = read_motion_values(c->avctx, gb, &c->bundle[BINK_SRC_Y_OFF])) < 0)
            return ret;
        if ((ret = read_dcs(c->avctx, gb, &c->bundle[BINK_SRC_INTRA_DC], DC_START_BITS, 0)) < 0)
            return ret;
        if ((ret = read_dcs(c->avctx, gb, &c->bundle[BINK_SRC_INTER_DC], DC_START_BITS, 1)) < 0)
            return ret;
        if ((ret = read_runs(c->avctx, gb, &c->bundle[BINK_SRC_RUN])) < 0)
            return ret;

        dst  = frame->data[plane_idx]  + 8*by*stride;
        prev = (c->last->data[plane_idx] ? c->last->data[plane_idx]
                                         : frame->data[plane_idx]) + 8*by*stride;
        for (bx = 0; bx < bw; bx++, dst += 8, prev += 8) {
            blk = get_value(c, BINK_SRC_BLOCK_TYPES);
            // 16x16 block type on odd line means part of the already decoded block, so skip it
            if (((by & 1) || (bx & 1)) && blk == SCALED_BLOCK) {
                bx++;
                dst  += 8;
                prev += 8;
                continue;
            }
            switch (blk) {
            case SKIP_BLOCK:
                c->put_pixels_tab(dst, prev, stride, 8);
                break;
            case SCALED_BLOCK:
                blk = get_value(c, BINK_SRC_SUB_BLOCK_TYPES);
                switch (blk) {
                case RUN_BLOCK:
                    if (get_bits_left(gb) < 4)
                        return AVERROR_INVALIDDATA;
                    scan = bink_patterns[get_bits(gb, 4)];
                    i = 0;
                    do {
                        int run = get_value(c, BINK_SRC_RUN) + 1;

                        i += run;
                        if (i > 64) {
                            av_log(c->avctx, AV_LOG_ERROR, "Run went out of bounds\n");
                            return AVERROR_INVALIDDATA;
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
                    memset(dctblock, 0, sizeof(*dctblock) * 64);
                    dctblock[0] = get_value(c, BINK_SRC_INTRA_DC);
                    if ((quant_idx = read_dct_coeffs(c, gb, dctblock, bink_scan, &coef_count, coef_idx, -1)) < 0)
                        return quant_idx;
                    unquantize_dct_coeffs(dctblock, bink_intra_quant[quant_idx], coef_count, coef_idx, bink_scan);
                    c->binkdsp.idct_put(ublock, 8, dctblock);
                    break;
                case FILL_BLOCK:
                    v = get_value(c, BINK_SRC_COLORS);
                    c->bdsp.fill_block_tab[0](dst, v, stride, 16);
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
                    return AVERROR_INVALIDDATA;
                }
                if (blk != FILL_BLOCK)
                c->binkdsp.scale_block(ublock, dst, stride);
                bx++;
                dst  += 8;
                prev += 8;
                break;
            case MOTION_BLOCK:
                ret = bink_put_pixels(c, dst, prev, stride,
                                      ref_start, ref_end);
                if (ret < 0)
                    return ret;
                break;
            case RUN_BLOCK:
                scan = bink_patterns[get_bits(gb, 4)];
                i = 0;
                do {
                    int run = get_value(c, BINK_SRC_RUN) + 1;

                    i += run;
                    if (i > 64) {
                        av_log(c->avctx, AV_LOG_ERROR, "Run went out of bounds\n");
                        return AVERROR_INVALIDDATA;
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
                ret = bink_put_pixels(c, dst, prev, stride,
                                      ref_start, ref_end);
                if (ret < 0)
                    return ret;
                c->bdsp.clear_block(block);
                v = get_bits(gb, 7);
                read_residue(gb, block, v);
                c->binkdsp.add_pixels8(dst, block, stride);
                break;
            case INTRA_BLOCK:
                memset(dctblock, 0, sizeof(*dctblock) * 64);
                dctblock[0] = get_value(c, BINK_SRC_INTRA_DC);
                if ((quant_idx = read_dct_coeffs(c, gb, dctblock, bink_scan, &coef_count, coef_idx, -1)) < 0)
                    return quant_idx;
                unquantize_dct_coeffs(dctblock, bink_intra_quant[quant_idx], coef_count, coef_idx, bink_scan);
                c->binkdsp.idct_put(dst, stride, dctblock);
                break;
            case FILL_BLOCK:
                v = get_value(c, BINK_SRC_COLORS);
                c->bdsp.fill_block_tab[1](dst, v, stride, 8);
                break;
            case INTER_BLOCK:
                ret = bink_put_pixels(c, dst, prev, stride,
                                      ref_start, ref_end);
                if (ret < 0)
                    return ret;
                memset(dctblock, 0, sizeof(*dctblock) * 64);
                dctblock[0] = get_value(c, BINK_SRC_INTER_DC);
                if ((quant_idx = read_dct_coeffs(c, gb, dctblock, bink_scan, &coef_count, coef_idx, -1)) < 0)
                    return quant_idx;
                unquantize_dct_coeffs(dctblock, bink_inter_quant[quant_idx], coef_count, coef_idx, bink_scan);
                c->binkdsp.idct_add(dst, stride, dctblock);
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
                return AVERROR_INVALIDDATA;
            }
        }
    }

end:
    if (get_bits_count(gb) & 0x1F) //next plane data starts at 32-bit boundary
        skip_bits_long(gb, 32 - (get_bits_count(gb) & 0x1F));

    return 0;
}

static int decode_frame(AVCodecContext *avctx, void *data, int *got_frame, AVPacket *pkt)
{
    BinkContext * const c = avctx->priv_data;
    AVFrame *frame = data;
    GetBitContext gb;
    int plane, plane_idx, ret;
    int bits_count = pkt->size << 3;

    if (c->version > 'b') {
        if ((ret = ff_get_buffer(avctx, frame, AV_GET_BUFFER_FLAG_REF)) < 0)
            return ret;
    } else {
        if ((ret = ff_reget_buffer(avctx, c->last, 0)) < 0)
            return ret;
        if ((ret = av_frame_ref(frame, c->last)) < 0)
            return ret;
    }

    init_get_bits(&gb, pkt->data, bits_count);
    if (c->has_alpha) {
        if (c->version >= 'i')
            skip_bits_long(&gb, 32);
        if ((ret = bink_decode_plane(c, frame, &gb, 3, 0)) < 0)
            return ret;
    }
    if (c->version >= 'i')
        skip_bits_long(&gb, 32);

    c->frame_num++;

    for (plane = 0; plane < 3; plane++) {
        plane_idx = (!plane || !c->swap_planes) ? plane : (plane ^ 3);

        if (c->version > 'b') {
            if ((ret = bink_decode_plane(c, frame, &gb, plane_idx, !!plane)) < 0)
                return ret;
        } else {
            if ((ret = binkb_decode_plane(c, frame, &gb, plane_idx,
                                          c->frame_num == 1, !!plane)) < 0)
                return ret;
        }
        if (get_bits_count(&gb) >= bits_count)
            break;
    }
    emms_c();

    if (c->version > 'b') {
        av_frame_unref(c->last);
        if ((ret = av_frame_ref(c->last, frame)) < 0)
            return ret;
    }

    *got_frame = 1;

    /* always report that the buffer was completely consumed */
    return pkt->size;
}

/**
 * Calculate quantization tables for version b
 */
static av_cold void binkb_calc_quant(void)
{
    uint8_t inv_bink_scan[64];
    static const int s[64]={
        1073741824,1489322693,1402911301,1262586814,1073741824, 843633538, 581104888, 296244703,
        1489322693,2065749918,1945893874,1751258219,1489322693,1170153332, 806015634, 410903207,
        1402911301,1945893874,1832991949,1649649171,1402911301,1102260336, 759250125, 387062357,
        1262586814,1751258219,1649649171,1484645031,1262586814, 992008094, 683307060, 348346918,
        1073741824,1489322693,1402911301,1262586814,1073741824, 843633538, 581104888, 296244703,
         843633538,1170153332,1102260336, 992008094, 843633538, 662838617, 456571181, 232757969,
         581104888, 806015634, 759250125, 683307060, 581104888, 456571181, 314491699, 160326478,
         296244703, 410903207, 387062357, 348346918, 296244703, 232757969, 160326478,  81733730,
    };
    int i, j;
#define C (1LL<<30)
    for (i = 0; i < 64; i++)
        inv_bink_scan[bink_scan[i]] = i;

    for (j = 0; j < 16; j++) {
        for (i = 0; i < 64; i++) {
            int k = inv_bink_scan[i];
            binkb_intra_quant[j][k] = binkb_intra_seed[i] * (int64_t)s[i] *
                                        binkb_num[j]/(binkb_den[j] * (C>>12));
            binkb_inter_quant[j][k] = binkb_inter_seed[i] * (int64_t)s[i] *
                                        binkb_num[j]/(binkb_den[j] * (C>>12));
        }
    }
}

static av_cold int decode_init(AVCodecContext *avctx)
{
    BinkContext * const c = avctx->priv_data;
    static VLC_TYPE table[16 * 128][2];
    static int binkb_initialised = 0;
    HpelDSPContext hdsp;
    int i, ret;
    int flags;

    c->version = avctx->codec_tag >> 24;
    if (avctx->extradata_size < 4) {
        av_log(avctx, AV_LOG_ERROR, "Extradata missing or too short\n");
        return AVERROR_INVALIDDATA;
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

    if ((ret = av_image_check_size(avctx->width, avctx->height, 0, avctx)) < 0)
        return ret;

    c->last = av_frame_alloc();
    if (!c->last)
        return AVERROR(ENOMEM);

    avctx->pix_fmt = c->has_alpha ? AV_PIX_FMT_YUVA420P : AV_PIX_FMT_YUV420P;
    avctx->color_range = c->version == 'k' ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;

    ff_blockdsp_init(&c->bdsp, avctx);
    ff_hpeldsp_init(&hdsp, avctx->flags);
    c->put_pixels_tab = hdsp.put_pixels_tab[1][0];
    ff_binkdsp_init(&c->binkdsp);

    if ((ret = init_bundles(c)) < 0)
        return ret;

    if (c->version == 'b') {
        if (!binkb_initialised) {
            binkb_calc_quant();
            binkb_initialised = 1;
        }
    }

    return 0;
}

static av_cold int decode_end(AVCodecContext *avctx)
{
    BinkContext * const c = avctx->priv_data;

    av_frame_free(&c->last);

    free_bundles(c);
    return 0;
}

static void flush(AVCodecContext *avctx)
{
    BinkContext * const c = avctx->priv_data;

    c->frame_num = 0;
}

AVCodec ff_bink_decoder = {
    .name           = "binkvideo",
    .long_name      = NULL_IF_CONFIG_SMALL("Bink video"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_BINKVIDEO,
    .priv_data_size = sizeof(BinkContext),
    .init           = decode_init,
    .close          = decode_end,
    .decode         = decode_frame,
    .flush          = flush,
    .capabilities   = AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
};
