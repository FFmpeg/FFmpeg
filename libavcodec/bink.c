/*
 * Bink video decoder
 * Copyright (c) 2009 Konstantin Shishkov
 * Copyright (C) 2011 Peter Ross <pross@xvid.org>
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

#include "libavutil/attributes.h"
#include "libavutil/imgutils.h"
#include "libavutil/internal.h"

#define BITSTREAM_READER_LE
#include "avcodec.h"
#include "binkdata.h"
#include "binkdsp.h"
#include "bitstream.h"
#include "blockdsp.h"
#include "hpeldsp.h"
#include "internal.h"
#include "mathops.h"
#include "vlc.h"

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

#define GET_HUFF(bc, tree)                                                \
    (tree).syms[bitstream_read_vlc(bc, bink_trees[(tree).vlc_num].table,  \
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
    HpelDSPContext hdsp;
    BinkDSPContext binkdsp;
    AVFrame        *last;
    int            version;              ///< internal Bink file version
    int            has_alpha;
    int            swap_planes;

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
 * Initialize length length in all bundles.
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
static av_cold void init_bundles(BinkContext *c)
{
    int bw, bh, blocks;
    int i;

    bw = (c->avctx->width  + 7) >> 3;
    bh = (c->avctx->height + 7) >> 3;
    blocks = bw * bh;

    for (i = 0; i < BINKB_NB_SRC; i++) {
        c->bundle[i].data = av_malloc(blocks * 64);
        c->bundle[i].data_end = c->bundle[i].data + blocks * 64;
    }
}

/**
 * Free memory used by bundles.
 *
 * @param c decoder context
 */
static av_cold void free_bundles(BinkContext *c)
{
    int i;
    for (i = 0; i < BINKB_NB_SRC; i++)
        av_freep(&c->bundle[i].data);
}

/**
 * Merge two consequent lists of equal size depending on bits read.
 *
 * @param bc   context for reading bits
 * @param dst  buffer where merged list will be written to
 * @param src  pointer to the head of the first list (the second lists starts at src+size)
 * @param size input lists size
 */
static void merge(BitstreamContext *bc, uint8_t *dst, uint8_t *src, int size)
{
    uint8_t *src2 = src + size;
    int size2 = size;

    do {
        if (!bitstream_read_bit(bc)) {
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
 * @param bc   context for reading bits
 * @param tree pointer for storing tree data
 */
static void read_tree(BitstreamContext *bc, Tree *tree)
{
    uint8_t tmp1[16] = { 0 }, tmp2[16], *in = tmp1, *out = tmp2;
    int i, t, len;

    tree->vlc_num = bitstream_read(bc, 4);
    if (!tree->vlc_num) {
        for (i = 0; i < 16; i++)
            tree->syms[i] = i;
        return;
    }
    if (bitstream_read_bit(bc)) {
        len = bitstream_read(bc, 3);
        for (i = 0; i <= len; i++) {
            tree->syms[i] = bitstream_read(bc, 4);
            tmp1[tree->syms[i]] = 1;
        }
        for (i = 0; i < 16 && len < 16 - 1; i++)
            if (!tmp1[i])
                tree->syms[++len] = i;
    } else {
        len = bitstream_read(bc, 2);
        for (i = 0; i < 16; i++)
            in[i] = i;
        for (i = 0; i <= len; i++) {
            int size = 1 << i;
            for (t = 0; t < 16; t += size << 1)
                merge(bc, out + t, in + t, size);
            FFSWAP(uint8_t*, in, out);
        }
        memcpy(tree->syms, in, 16);
    }
}

/**
 * Prepare bundle for decoding data.
 *
 * @param bc          context for reading bits
 * @param c           decoder context
 * @param bundle_num  number of the bundle to initialize
 */
static void read_bundle(BitstreamContext *bc, BinkContext *c, int bundle_num)
{
    int i;

    if (bundle_num == BINK_SRC_COLORS) {
        for (i = 0; i < 16; i++)
            read_tree(bc, &c->col_high[i]);
        c->col_lastval = 0;
    }
    if (bundle_num != BINK_SRC_INTRA_DC && bundle_num != BINK_SRC_INTER_DC)
        read_tree(bc, &c->bundle[bundle_num].tree);
    c->bundle[bundle_num].cur_dec =
    c->bundle[bundle_num].cur_ptr = c->bundle[bundle_num].data;
}

/**
 * common check before starting decoding bundle data
 *
 * @param bc context for reading bits
 * @param b  bundle
 * @param t  variable where number of elements to decode will be stored
 */
#define CHECK_READ_VAL(bc, b, t) \
    if (!b->cur_dec || (b->cur_dec > b->cur_ptr)) \
        return 0; \
    t = bitstream_read(bc, b->len); \
    if (!t) { \
        b->cur_dec = NULL; \
        return 0; \
    } \

static int read_runs(AVCodecContext *avctx, BitstreamContext *bc, Bundle *b)
{
    int t, v;
    const uint8_t *dec_end;

    CHECK_READ_VAL(bc, b, t);
    dec_end = b->cur_dec + t;
    if (dec_end > b->data_end) {
        av_log(avctx, AV_LOG_ERROR, "Run value went out of bounds\n");
        return AVERROR_INVALIDDATA;
    }
    if (bitstream_read_bit(bc)) {
        v = bitstream_read(bc, 4);
        memset(b->cur_dec, v, t);
        b->cur_dec += t;
    } else {
        while (b->cur_dec < dec_end)
            *b->cur_dec++ = GET_HUFF(bc, b->tree);
    }
    return 0;
}

static int read_motion_values(AVCodecContext *avctx, BitstreamContext *bc, Bundle *b)
{
    int t, v;
    const uint8_t *dec_end;

    CHECK_READ_VAL(bc, b, t);
    dec_end = b->cur_dec + t;
    if (dec_end > b->data_end) {
        av_log(avctx, AV_LOG_ERROR, "Too many motion values\n");
        return AVERROR_INVALIDDATA;
    }
    if (bitstream_read_bit(bc)) {
        v = bitstream_read(bc, 4);
        if (v) {
            v = bitstream_apply_sign(bc, v);
        }
        memset(b->cur_dec, v, t);
        b->cur_dec += t;
    } else {
        while (b->cur_dec < dec_end) {
            v = GET_HUFF(bc, b->tree);
            if (v) {
                v = bitstream_apply_sign(bc, v);
            }
            *b->cur_dec++ = v;
        }
    }
    return 0;
}

static const uint8_t bink_rlelens[4] = { 4, 8, 12, 32 };

static int read_block_types(AVCodecContext *avctx, BitstreamContext *bc, Bundle *b)
{
    int t, v;
    int last = 0;
    const uint8_t *dec_end;

    CHECK_READ_VAL(bc, b, t);
    dec_end = b->cur_dec + t;
    if (dec_end > b->data_end) {
        av_log(avctx, AV_LOG_ERROR, "Too many block type values\n");
        return AVERROR_INVALIDDATA;
    }
    if (bitstream_read_bit(bc)) {
        v = bitstream_read(bc, 4);
        memset(b->cur_dec, v, t);
        b->cur_dec += t;
    } else {
        while (b->cur_dec < dec_end) {
            v = GET_HUFF(bc, b->tree);
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

static int read_patterns(AVCodecContext *avctx, BitstreamContext *bc, Bundle *b)
{
    int t, v;
    const uint8_t *dec_end;

    CHECK_READ_VAL(bc, b, t);
    dec_end = b->cur_dec + t;
    if (dec_end > b->data_end) {
        av_log(avctx, AV_LOG_ERROR, "Too many pattern values\n");
        return AVERROR_INVALIDDATA;
    }
    while (b->cur_dec < dec_end) {
        v  = GET_HUFF(bc, b->tree);
        v |= GET_HUFF(bc, b->tree) << 4;
        *b->cur_dec++ = v;
    }

    return 0;
}

static int read_colors(BitstreamContext *bc, Bundle *b, BinkContext *c)
{
    int t, sign, v;
    const uint8_t *dec_end;

    CHECK_READ_VAL(bc, b, t);
    dec_end = b->cur_dec + t;
    if (dec_end > b->data_end) {
        av_log(c->avctx, AV_LOG_ERROR, "Too many color values\n");
        return AVERROR_INVALIDDATA;
    }
    if (bitstream_read_bit(bc)) {
        c->col_lastval = GET_HUFF(bc, c->col_high[c->col_lastval]);
        v = GET_HUFF(bc, b->tree);
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
            c->col_lastval = GET_HUFF(bc, c->col_high[c->col_lastval]);
            v = GET_HUFF(bc, b->tree);
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

static int read_dcs(AVCodecContext *avctx, BitstreamContext *bc, Bundle *b,
                    int start_bits, int has_sign)
{
    int i, j, len, len2, bsize, v, v2;
    int16_t *dst     = (int16_t*)b->cur_dec;
    int16_t *dst_end = (int16_t*)b->data_end;

    CHECK_READ_VAL(bc, b, len);
    v = bitstream_read(bc, start_bits - has_sign);
    if (v && has_sign) {
        v = bitstream_apply_sign(bc, v);
    }
    if (dst_end - dst < 1)
        return AVERROR_INVALIDDATA;
    *dst++ = v;
    len--;
    for (i = 0; i < len; i += 8) {
        len2 = FFMIN(len - i, 8);
        if (dst_end - dst < len2)
            return AVERROR_INVALIDDATA;
        bsize = bitstream_read(bc, 4);
        if (bsize) {
            for (j = 0; j < len2; j++) {
                v2 = bitstream_read(bc, bsize);
                if (v2) {
                    v2 = bitstream_apply_sign(bc, v2);
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

static int binkb_read_bundle(BinkContext *c, BitstreamContext *bc, int bundle_num)
{
    const int bits = binkb_bundle_sizes[bundle_num];
    const int mask = 1 << (bits - 1);
    const int issigned = binkb_bundle_signed[bundle_num];
    Bundle *b = &c->bundle[bundle_num];
    int i, len;

    CHECK_READ_VAL(bc, b, len);
    if (b->data_end - b->cur_dec < len * (1 + (bits > 8)))
        return AVERROR_INVALIDDATA;
    if (bits <= 8) {
        if (!issigned) {
            for (i = 0; i < len; i++)
                *b->cur_dec++ = bitstream_read(bc, bits);
        } else {
            for (i = 0; i < len; i++)
                *b->cur_dec++ = bitstream_read(bc, bits) - mask;
        }
    } else {
        int16_t *dst = (int16_t*)b->cur_dec;

        if (!issigned) {
            for (i = 0; i < len; i++)
                *dst++ = bitstream_read(bc, bits);
        } else {
            for (i = 0; i < len; i++)
                *dst++ = bitstream_read(bc, bits) - mask;
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
 * @param bc       context for reading bits
 * @param block    place for storing coefficients
 * @param scan     scan order table
 * @param quant_matrices quantization matrices
 * @return 0 for success, negative value in other cases
 */
static int read_dct_coeffs(BitstreamContext *bc, int32_t block[64],
                           const uint8_t *scan, int *coef_count_,
                           int coef_idx[64], int q)
{
    int coef_list[128];
    int mode_list[128];
    int i, t, bits, ccoef, mode;
    int list_start = 64, list_end = 64, list_pos;
    int coef_count = 0;
    int quant_idx;

    coef_list[list_end] = 4;  mode_list[list_end++] = 0;
    coef_list[list_end] = 24; mode_list[list_end++] = 0;
    coef_list[list_end] = 44; mode_list[list_end++] = 0;
    coef_list[list_end] = 1;  mode_list[list_end++] = 3;
    coef_list[list_end] = 2;  mode_list[list_end++] = 3;
    coef_list[list_end] = 3;  mode_list[list_end++] = 3;

    for (bits = bitstream_read(bc, 4) - 1; bits >= 0; bits--) {
        list_pos = list_start;
        while (list_pos < list_end) {
            if (!(mode_list[list_pos] | coef_list[list_pos]) || !bitstream_read_bit(bc)) {
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
                    if (bitstream_read_bit(bc)) {
                        coef_list[--list_start] = ccoef;
                        mode_list[  list_start] = 3;
                    } else {
                        if (!bits) {
                            t = 1 - (bitstream_read_bit(bc) << 1);
                        } else {
                            t = bitstream_read(bc, bits) | 1 << bits;
                            t = bitstream_apply_sign(bc, t);
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
                    t = 1 - (bitstream_read_bit(bc) << 1);
                } else {
                    t = bitstream_read(bc, bits) | 1 << bits;
                    t = bitstream_apply_sign(bc, t);
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
        quant_idx = bitstream_read(bc, 4);
    } else {
        quant_idx = q;
    }

    if (quant_idx >= 16)
        return AVERROR_INVALIDDATA;

    *coef_count_ = coef_count;

    return quant_idx;
}

static void unquantize_dct_coeffs(int32_t block[64], const int32_t quant[64],
                                  int coef_count, int coef_idx[64],
                                  const uint8_t *scan)
{
    int i;
    block[0] = (block[0] * quant[0]) >> 11;
    for (i = 0; i < coef_count; i++) {
        int idx = coef_idx[i];
        block[scan[idx]] = (block[scan[idx]] * quant[idx]) >> 11;
    }
}

/**
 * Read 8x8 block with residue after motion compensation.
 *
 * @param bc          context for reading bits
 * @param block       place to store read data
 * @param masks_count number of masks to decode
 * @return 0 on success, negative value in other cases
 */
static int read_residue(BitstreamContext *bc, int16_t block[64], int masks_count)
{
    int coef_list[128];
    int mode_list[128];
    int i, mask, ccoef, mode;
    int list_start = 64, list_end = 64, list_pos;
    int nz_coeff[64];
    int nz_coeff_count = 0;

    coef_list[list_end] =  4; mode_list[list_end++] = 0;
    coef_list[list_end] = 24; mode_list[list_end++] = 0;
    coef_list[list_end] = 44; mode_list[list_end++] = 0;
    coef_list[list_end] =  0; mode_list[list_end++] = 2;

    for (mask = 1 << bitstream_read(bc, 3); mask; mask >>= 1) {
        for (i = 0; i < nz_coeff_count; i++) {
            if (!bitstream_read_bit(bc))
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
            if (!(coef_list[list_pos] | mode_list[list_pos]) || !bitstream_read_bit(bc)) {
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
                    if (bitstream_read_bit(bc)) {
                        coef_list[--list_start] = ccoef;
                        mode_list[  list_start] = 3;
                    } else {
                        nz_coeff[nz_coeff_count++] = bink_scan[ccoef];
                        block[bink_scan[ccoef]] = bitstream_apply_sign(bc, mask);
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
                block[bink_scan[ccoef]] = bitstream_apply_sign(bc, mask);
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

static int binkb_decode_plane(BinkContext *c, AVFrame *frame, BitstreamContext *bc,
                              int plane_idx, int is_key, int is_chroma)
{
    int blk, ret;
    int i, j, bx, by;
    uint8_t *dst, *ref, *ref_start, *ref_end;
    int v, col[2];
    const uint8_t *scan;
    int xoff, yoff;
    LOCAL_ALIGNED_16(int16_t, block, [64]);
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
            if ((ret = binkb_read_bundle(c, bc, i)) < 0)
                return ret;
        }

        dst  = frame->data[plane_idx]  + 8*by*stride;
        for (bx = 0; bx < bw; bx++, dst += 8) {
            blk = binkb_get_value(c, BINKB_SRC_BLOCK_TYPES);
            switch (blk) {
            case 0:
                break;
            case 1:
                scan = bink_patterns[bitstream_read(bc, 4)];
                i = 0;
                do {
                    int mode = bitstream_read_bit(bc);
                    int run  = bitstream_read(bc, binkb_runbits[i]) + 1;

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
                if ((quant_idx = read_dct_coeffs(bc, dctblock, bink_scan, &coef_count, coef_idx, qp)) < 0)
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
                    c->hdsp.put_pixels_tab[1][0](dst, ref, stride, 8);
                } else {
                    put_pixels8x8_overlapped(dst, ref, stride);
                }
                c->bdsp.clear_block(block);
                v = binkb_get_value(c, BINKB_SRC_INTER_COEFS);
                read_residue(bc, block, v);
                c->binkdsp.add_pixels8(dst, block, stride);
                break;
            case 4:
                xoff = binkb_get_value(c, BINKB_SRC_X_OFF);
                yoff = binkb_get_value(c, BINKB_SRC_Y_OFF) + ybias;
                ref = dst + xoff + yoff * stride;
                if (ref < ref_start || ref + 8 * stride > ref_end) {
                    av_log(c->avctx, AV_LOG_WARNING, "Reference block is out of bounds\n");
                } else if (ref + 8*stride < dst || ref >= dst + 8*stride) {
                    c->hdsp.put_pixels_tab[1][0](dst, ref, stride, 8);
                } else {
                    put_pixels8x8_overlapped(dst, ref, stride);
                }
                memset(dctblock, 0, sizeof(*dctblock) * 64);
                dctblock[0] = binkb_get_value(c, BINKB_SRC_INTER_DC);
                qp = binkb_get_value(c, BINKB_SRC_INTER_Q);
                if ((quant_idx = read_dct_coeffs(bc, dctblock, bink_scan, &coef_count, coef_idx, qp)) < 0)
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
                    c->hdsp.put_pixels_tab[1][0](dst, ref, stride, 8);
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
    if (bitstream_tell(bc) & 0x1F) // next plane data starts at 32-bit boundary
        bitstream_skip(bc, 32 - (bitstream_tell(bc) & 0x1F));

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
    c->hdsp.put_pixels_tab[1][0](dst, ref, stride, 8);

    return 0;
}

static int bink_decode_plane(BinkContext *c, AVFrame *frame, BitstreamContext *bc,
                             int plane_idx, int is_chroma)
{
    int blk, ret;
    int i, j, bx, by;
    uint8_t *dst, *prev, *ref_start, *ref_end;
    int v, col[2];
    const uint8_t *scan;
    LOCAL_ALIGNED_16(int16_t, block, [64]);
    LOCAL_ALIGNED_16(uint8_t, ublock, [64]);
    LOCAL_ALIGNED_16(int32_t, dctblock, [64]);
    int coordmap[64], quant_idx, coef_count, coef_idx[64];

    const int stride = frame->linesize[plane_idx];
    int bw = is_chroma ? (c->avctx->width  + 15) >> 4 : (c->avctx->width  + 7) >> 3;
    int bh = is_chroma ? (c->avctx->height + 15) >> 4 : (c->avctx->height + 7) >> 3;
    int width = c->avctx->width >> is_chroma;

    init_lengths(c, FFMAX(width, 8), bw);
    for (i = 0; i < BINK_NB_SRC; i++)
        read_bundle(bc, c, i);

    ref_start = c->last->data[plane_idx] ? c->last->data[plane_idx]
                                         : frame->data[plane_idx];
    ref_end   = ref_start
                + (bw - 1 + c->last->linesize[plane_idx] * (bh - 1)) * 8;

    for (i = 0; i < 64; i++)
        coordmap[i] = (i & 7) + (i >> 3) * stride;

    for (by = 0; by < bh; by++) {
        if ((ret = read_block_types(c->avctx, bc, &c->bundle[BINK_SRC_BLOCK_TYPES])) < 0)
            return ret;
        if ((ret = read_block_types(c->avctx, bc, &c->bundle[BINK_SRC_SUB_BLOCK_TYPES])) < 0)
            return ret;
        if ((ret = read_colors(bc, &c->bundle[BINK_SRC_COLORS], c)) < 0)
            return ret;
        if ((ret = read_patterns(c->avctx, bc, &c->bundle[BINK_SRC_PATTERN])) < 0)
            return ret;
        if ((ret = read_motion_values(c->avctx, bc, &c->bundle[BINK_SRC_X_OFF])) < 0)
            return ret;
        if ((ret = read_motion_values(c->avctx, bc, &c->bundle[BINK_SRC_Y_OFF])) < 0)
            return ret;
        if ((ret = read_dcs(c->avctx, bc, &c->bundle[BINK_SRC_INTRA_DC], DC_START_BITS, 0)) < 0)
            return ret;
        if ((ret = read_dcs(c->avctx, bc, &c->bundle[BINK_SRC_INTER_DC], DC_START_BITS, 1)) < 0)
            return ret;
        if ((ret = read_runs(c->avctx, bc, &c->bundle[BINK_SRC_RUN])) < 0)
            return ret;

        if (by == bh)
            break;
        dst  = frame->data[plane_idx]  + 8*by*stride;
        prev = (c->last->data[plane_idx] ? c->last->data[plane_idx]
                                         : frame->data[plane_idx]) + 8*by*stride;
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
                c->hdsp.put_pixels_tab[1][0](dst, prev, stride, 8);
                break;
            case SCALED_BLOCK:
                blk = get_value(c, BINK_SRC_SUB_BLOCK_TYPES);
                switch (blk) {
                case RUN_BLOCK:
                    scan = bink_patterns[bitstream_read(bc, 4)];
                    i = 0;
                    do {
                        int run = get_value(c, BINK_SRC_RUN) + 1;

                        i += run;
                        if (i > 64) {
                            av_log(c->avctx, AV_LOG_ERROR, "Run went out of bounds\n");
                            return AVERROR_INVALIDDATA;
                        }
                        if (bitstream_read_bit(bc)) {
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
                    if ((quant_idx = read_dct_coeffs(bc, dctblock, bink_scan, &coef_count, coef_idx, -1)) < 0)
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
                scan = bink_patterns[bitstream_read(bc, 4)];
                i = 0;
                do {
                    int run = get_value(c, BINK_SRC_RUN) + 1;

                    i += run;
                    if (i > 64) {
                        av_log(c->avctx, AV_LOG_ERROR, "Run went out of bounds\n");
                        return AVERROR_INVALIDDATA;
                    }
                    if (bitstream_read_bit(bc)) {
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
                v = bitstream_read(bc, 7);
                read_residue(bc, block, v);
                c->binkdsp.add_pixels8(dst, block, stride);
                break;
            case INTRA_BLOCK:
                memset(dctblock, 0, sizeof(*dctblock) * 64);
                dctblock[0] = get_value(c, BINK_SRC_INTRA_DC);
                if ((quant_idx = read_dct_coeffs(bc, dctblock, bink_scan, &coef_count, coef_idx, -1)) < 0)
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
                if ((quant_idx = read_dct_coeffs(bc, dctblock, bink_scan, &coef_count, coef_idx, -1)) < 0)
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
    if (bitstream_tell(bc) & 0x1F) // next plane data starts at 32-bit boundary
        bitstream_skip(bc, 32 - (bitstream_tell(bc) & 0x1F));

    return 0;
}

static int decode_frame(AVCodecContext *avctx, void *data, int *got_frame, AVPacket *pkt)
{
    BinkContext * const c = avctx->priv_data;
    AVFrame *frame = data;
    BitstreamContext bc;
    int plane, plane_idx, ret;
    int bits_count = pkt->size << 3;

    if (c->version > 'b') {
        if ((ret = ff_get_buffer(avctx, frame, AV_GET_BUFFER_FLAG_REF)) < 0) {
            av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
            return ret;
        }
    } else {
        if ((ret = ff_reget_buffer(avctx, c->last)) < 0) {
            av_log(avctx, AV_LOG_ERROR, "reget_buffer() failed\n");
            return ret;
        }
        if ((ret = av_frame_ref(frame, c->last)) < 0)
            return ret;
    }

    bitstream_init(&bc, pkt->data, bits_count);
    if (c->has_alpha) {
        if (c->version >= 'i')
            bitstream_skip(&bc, 32);
        if ((ret = bink_decode_plane(c, frame, &bc, 3, 0)) < 0)
            return ret;
    }
    if (c->version >= 'i')
        bitstream_skip(&bc, 32);

    for (plane = 0; plane < 3; plane++) {
        plane_idx = (!plane || !c->swap_planes) ? plane : (plane ^ 3);

        if (c->version > 'b') {
            if ((ret = bink_decode_plane(c, frame, &bc, plane_idx, !!plane)) < 0)
                return ret;
        } else {
            if ((ret = binkb_decode_plane(c, frame, &bc, plane_idx,
                                          !avctx->frame_number, !!plane)) < 0)
                return ret;
        }
        if (bitstream_tell(&bc) >= bits_count)
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
    double s[64];
    int i, j;

    for (j = 0; j < 8; j++) {
        for (i = 0; i < 8; i++) {
            if (j && j != 4)
               if (i && i != 4)
                   s[j*8 + i] = cos(j * M_PI/16.0) * cos(i * M_PI/16.0) * 2.0;
               else
                   s[j*8 + i] = cos(j * M_PI/16.0) * sqrt(2.0);
            else
               if (i && i != 4)
                   s[j*8 + i] = cos(i * M_PI/16.0) * sqrt(2.0);
               else
                   s[j*8 + i] = 1.0;
        }
    }

    for (i = 0; i < 64; i++)
        inv_bink_scan[bink_scan[i]] = i;

    for (j = 0; j < 16; j++) {
        for (i = 0; i < 64; i++) {
            int k = inv_bink_scan[i];
            if (s[i] == 1.0) {
                binkb_intra_quant[j][k] = (1L << 12) * binkb_intra_seed[i] *
                                          binkb_num[j]/binkb_den[j];
                binkb_inter_quant[j][k] = (1L << 12) * binkb_inter_seed[i] *
                                          binkb_num[j]/binkb_den[j];
            } else {
                binkb_intra_quant[j][k] = (1L << 12) * binkb_intra_seed[i] * s[i] *
                                          binkb_num[j]/(double)binkb_den[j];
                binkb_inter_quant[j][k] = (1L << 12) * binkb_inter_seed[i] * s[i] *
                                          binkb_num[j]/(double)binkb_den[j];
            }
        }
    }
}

static av_cold int decode_init(AVCodecContext *avctx)
{
    BinkContext * const c = avctx->priv_data;
    static VLC_TYPE table[16 * 128][2];
    static int binkb_initialised = 0;
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

    c->last = av_frame_alloc();
    if (!c->last)
        return AVERROR(ENOMEM);

    if ((ret = av_image_check_size(avctx->width, avctx->height, 0, avctx)) < 0)
        return ret;

    avctx->pix_fmt = c->has_alpha ? AV_PIX_FMT_YUVA420P : AV_PIX_FMT_YUV420P;

    ff_blockdsp_init(&c->bdsp);
    ff_hpeldsp_init(&c->hdsp, avctx->flags);
    ff_binkdsp_init(&c->binkdsp);

    init_bundles(c);

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

AVCodec ff_bink_decoder = {
    .name           = "binkvideo",
    .long_name      = NULL_IF_CONFIG_SMALL("Bink video"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_BINKVIDEO,
    .priv_data_size = sizeof(BinkContext),
    .init           = decode_init,
    .close          = decode_end,
    .decode         = decode_frame,
    .capabilities   = AV_CODEC_CAP_DR1,
};
