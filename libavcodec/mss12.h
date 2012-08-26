/*
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
 * Common header for Microsoft Screen 1 and 2
 */

#ifndef AVCODEC_MSS12_H
#define AVCODEC_MSS12_H

#include "avcodec.h"
#include "get_bits.h"

#define MODEL_MIN_SYMS    2
#define MODEL_MAX_SYMS  256
#define THRESH_ADAPTIVE  -1
#define THRESH_LOW       15
#define THRESH_HIGH      50

typedef struct Model {
    int cum_prob[MODEL_MAX_SYMS + 1];
    int weights[MODEL_MAX_SYMS + 1];
    int idx2sym[MODEL_MAX_SYMS + 1];
    int sym2idx[MODEL_MAX_SYMS + 1];
    int num_syms;
    int thr_weight, threshold;
} Model;

typedef struct ArithCoder {
    int low, high, value;
    GetBitContext *gb;
    int (*get_model_sym)(struct ArithCoder *c, Model *m);
    int (*get_number)   (struct ArithCoder *c, int n);
} ArithCoder;

typedef struct PixContext {
    int cache_size, num_syms;
    uint8_t cache[12];
    Model cache_model, full_model;
    Model sec_models[4][8][4];
} PixContext;

typedef struct MSS12Context {
    AVCodecContext *avctx;
    uint8_t        *pic_start;
    int            pic_stride;
    uint8_t        *mask;
    int            mask_linesize;
    uint32_t       pal[256];
    int            free_colours;
    int            keyframe;
    Model          intra_region, inter_region;
    Model          pivot, edge_mode, split_mode;
    PixContext     intra_pix_ctx, inter_pix_ctx;
    int            corrupted;
} MSS12Context;

int ff_mss12_decode_rect(MSS12Context *ctx, ArithCoder *acoder,
                         int x, int y, int width, int height);
void ff_mss12_model_update(Model *m, int val);
void ff_mss12_codec_reset(MSS12Context *ctx);
av_cold int ff_mss12_decode_init(AVCodecContext *avctx, int version);
av_cold int ff_mss12_decode_end(AVCodecContext *avctx);

#endif /* AVCODEC_MSS12_H */
