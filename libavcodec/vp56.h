/**
 * @file vp56.h
 * VP5 and VP6 compatible video decoder (common features)
 *
 * Copyright (C) 2006  Aurelien Jacobs <aurel@gnuage.org>
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef VP56_H
#define VP56_H

#include "vp56data.h"
#include "dsputil.h"
#include "mpegvideo.h"


typedef struct vp56_context vp56_context_t;
typedef struct vp56_mv vp56_mv_t;

typedef void (*vp56_parse_vector_adjustment_t)(vp56_context_t *s,
                                               vp56_mv_t *vect);
typedef int (*vp56_adjust_t)(int v, int t);
typedef void (*vp56_filter_t)(vp56_context_t *s, uint8_t *dst, uint8_t *src,
                              int offset1, int offset2, int stride,
                              vp56_mv_t mv, int mask, int select, int luma);
typedef void (*vp56_parse_coeff_t)(vp56_context_t *s);
typedef void (*vp56_default_models_init_t)(vp56_context_t *s);
typedef void (*vp56_parse_vector_models_t)(vp56_context_t *s);
typedef void (*vp56_parse_coeff_models_t)(vp56_context_t *s);
typedef int (*vp56_parse_header_t)(vp56_context_t *s, uint8_t *buf,
                                   int buf_size, int *golden_frame);

typedef struct {
    int high;
    int bits;
    const uint8_t *buffer;
    unsigned long code_word;
} vp56_range_coder_t;

typedef struct {
    uint8_t not_null_dc;
    vp56_frame_t ref_frame;
    DCTELEM dc_coeff;
} vp56_ref_dc_t;

struct vp56_mv {
    int x;
    int y;
};

typedef struct {
    uint8_t type;
    vp56_mv_t mv;
} vp56_macroblock_t;

struct vp56_context {
    AVCodecContext *avctx;
    DSPContext dsp;
    ScanTable scantable;
    AVFrame frames[3];
    AVFrame *framep[4];
    uint8_t *edge_emu_buffer_alloc;
    uint8_t *edge_emu_buffer;
    vp56_range_coder_t c;
    vp56_range_coder_t cc;
    vp56_range_coder_t *ccp;
    int sub_version;

    /* frame info */
    int plane_width[3];
    int plane_height[3];
    int mb_width;   /* number of horizontal MB */
    int mb_height;  /* number of vertical MB */
    int block_offset[6];

    int quantizer;
    uint16_t dequant_dc;
    uint16_t dequant_ac;

    /* DC predictors management */
    vp56_ref_dc_t *above_blocks;
    vp56_ref_dc_t left_block[4];
    int above_block_idx[6];
    DCTELEM prev_dc[3][3];    /* [plan][ref_frame] */

    /* blocks / macroblock */
    vp56_mb_t mb_type;
    vp56_macroblock_t *macroblocks;
    DECLARE_ALIGNED_16(DCTELEM, block_coeff[6][64]);
    uint8_t coeff_reorder[64];       /* used in vp6 only */
    uint8_t coeff_index_to_pos[64];  /* used in vp6 only */

    /* motion vectors */
    vp56_mv_t mv[6];  /* vectors for each block in MB */
    vp56_mv_t vector_candidate[2];
    int vector_candidate_pos;

    /* filtering hints */
    int filter_header;               /* used in vp6 only */
    int deblock_filtering;
    int filter_selection;
    int filter_mode;
    int max_vector_length;
    int sample_variance_threshold;

    /* AC models */
    uint8_t vector_model_sig[2];           /* delta sign */
    uint8_t vector_model_dct[2];           /* delta coding types */
    uint8_t vector_model_pdi[2][2];        /* predefined delta init */
    uint8_t vector_model_pdv[2][7];        /* predefined delta values */
    uint8_t vector_model_fdv[2][8];        /* 8 bit delta value definition */
    uint8_t mb_type_model[3][10][10];      /* model for decoding MB type */
    uint8_t coeff_model_dccv[2][11];       /* DC coeff value */
    uint8_t coeff_model_ract[2][3][6][11]; /* Run/AC coding type and AC coeff value */
    uint8_t coeff_model_acct[2][3][3][6][5];/* vp5 only AC coding type for coding group < 3 */
    uint8_t coeff_model_dcct[2][36][5];    /* DC coeff coding type */
    uint8_t coeff_model_runv[2][14];       /* run value (vp6 only) */
    uint8_t mb_types_stats[3][10][2];      /* contextual, next MB type stats */
    uint8_t coeff_ctx[4][64];              /* used in vp5 only */
    uint8_t coeff_ctx_last[4];             /* used in vp5 only */

    /* upside-down flipping hints */
    int flip;  /* are we flipping ? */
    int frbi;  /* first row block index in MB */
    int srbi;  /* second row block index in MB */
    int stride[3];  /* stride for each plan */

    const uint8_t *vp56_coord_div;
    vp56_parse_vector_adjustment_t parse_vector_adjustment;
    vp56_adjust_t adjust;
    vp56_filter_t filter;
    vp56_parse_coeff_t parse_coeff;
    vp56_default_models_init_t default_models_init;
    vp56_parse_vector_models_t parse_vector_models;
    vp56_parse_coeff_models_t parse_coeff_models;
    vp56_parse_header_t parse_header;
};


void vp56_init(vp56_context_t *s, AVCodecContext *avctx, int flip);
int vp56_free(AVCodecContext *avctx);
void vp56_init_dequant(vp56_context_t *s, int quantizer);
int vp56_decode_frame(AVCodecContext *avctx, void *data, int *data_size,
                      uint8_t *buf, int buf_size);


/**
 * vp56 specific range coder implementation
 */

static inline void vp56_init_range_decoder(vp56_range_coder_t *c,
                                           const uint8_t *buf, int buf_size)
{
    c->high = 255;
    c->bits = 8;
    c->buffer = buf;
    c->code_word = *c->buffer++ << 8;
    c->code_word |= *c->buffer++;
}

static inline int vp56_rac_get_prob(vp56_range_coder_t *c, uint8_t prob)
{
    unsigned int low = 1 + (((c->high - 1) * prob) / 256);
    unsigned int low_shift = low << 8;
    int bit = c->code_word >= low_shift;

    if (bit) {
        c->high -= low;
        c->code_word -= low_shift;
    } else {
        c->high = low;
    }

    /* normalize */
    while (c->high < 128) {
        c->high <<= 1;
        c->code_word <<= 1;
        if (--c->bits == 0) {
            c->bits = 8;
            c->code_word |= *c->buffer++;
        }
    }
    return bit;
}

static inline int vp56_rac_get(vp56_range_coder_t *c)
{
    /* equiprobable */
    int low = (c->high + 1) >> 1;
    unsigned int low_shift = low << 8;
    int bit = c->code_word >= low_shift;
    if (bit) {
        c->high = (c->high - low) << 1;
        c->code_word -= low_shift;
    } else {
        c->high = low << 1;
    }

    /* normalize */
    c->code_word <<= 1;
    if (--c->bits == 0) {
        c->bits = 8;
        c->code_word |= *c->buffer++;
    }
    return bit;
}

static inline int vp56_rac_gets(vp56_range_coder_t *c, int bits)
{
    int value = 0;

    while (bits--) {
        value = (value << 1) | vp56_rac_get(c);
    }

    return value;
}

static inline int vp56_rac_gets_nn(vp56_range_coder_t *c, int bits)
{
    int v = vp56_rac_gets(c, 7) << 1;
    return v + !v;
}

static inline int vp56_rac_get_tree(vp56_range_coder_t *c,
                                    const vp56_tree_t *tree,
                                    const uint8_t *probs)
{
    while (tree->val > 0) {
        if (vp56_rac_get_prob(c, probs[tree->prob_idx]))
            tree += tree->val;
        else
            tree++;
    }
    return -tree->val;
}

#endif /* VP56_H */
