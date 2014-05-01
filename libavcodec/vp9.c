/*
 * VP9 compatible video decoder
 *
 * Copyright (C) 2013 Ronald S. Bultje <rsbultje gmail com>
 * Copyright (C) 2013 Clément Bœsch <u pkh me>
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

#include "libavutil/avassert.h"

#include "avcodec.h"
#include "get_bits.h"
#include "internal.h"
#include "videodsp.h"
#include "vp56.h"
#include "vp9.h"
#include "vp9data.h"

#define VP9_SYNCCODE 0x498342
#define MAX_PROB 255

static void vp9_decode_flush(AVCodecContext *avctx)
{
    VP9Context *s = avctx->priv_data;
    int i;

    for (i = 0; i < FF_ARRAY_ELEMS(s->refs); i++)
        av_frame_unref(s->refs[i]);
}

static int update_size(AVCodecContext *avctx, int w, int h)
{
    VP9Context *s = avctx->priv_data;
    uint8_t *p;

    if (s->above_partition_ctx && w == avctx->width && h == avctx->height)
        return 0;

    vp9_decode_flush(avctx);

    if (w <= 0 || h <= 0)
        return AVERROR_INVALIDDATA;

    avctx->width  = w;
    avctx->height = h;
    s->sb_cols    = (w + 63) >> 6;
    s->sb_rows    = (h + 63) >> 6;
    s->cols       = (w +  7) >> 3;
    s->rows       = (h +  7) >> 3;

#define assign(var, type, n) var = (type)p; p += s->sb_cols * n * sizeof(*var)
    av_free(s->above_partition_ctx);
    p = av_malloc(s->sb_cols *
                  (240 + sizeof(*s->lflvl) + 16 * sizeof(*s->above_mv_ctx) +
                   64 * s->sb_rows * (1 + sizeof(*s->mv[0]) * 2)));
    if (!p)
        return AVERROR(ENOMEM);
    assign(s->above_partition_ctx, uint8_t *,     8);
    assign(s->above_skip_ctx,      uint8_t *,     8);
    assign(s->above_txfm_ctx,      uint8_t *,     8);
    assign(s->above_mode_ctx,      uint8_t *,    16);
    assign(s->above_y_nnz_ctx,     uint8_t *,    16);
    assign(s->above_uv_nnz_ctx[0], uint8_t *,     8);
    assign(s->above_uv_nnz_ctx[1], uint8_t *,     8);
    assign(s->intra_pred_data[0],  uint8_t *,    64);
    assign(s->intra_pred_data[1],  uint8_t *,    32);
    assign(s->intra_pred_data[2],  uint8_t *,    32);
    assign(s->above_segpred_ctx,   uint8_t *,     8);
    assign(s->above_intra_ctx,     uint8_t *,     8);
    assign(s->above_comp_ctx,      uint8_t *,     8);
    assign(s->above_ref_ctx,       uint8_t *,     8);
    assign(s->above_filter_ctx,    uint8_t *,     8);
    assign(s->lflvl,               VP9Filter *,   1);
    assign(s->above_mv_ctx,        VP56mv(*)[2], 16);
    assign(s->segmentation_map,    uint8_t *,      64 * s->sb_rows);
    assign(s->mv[0],               VP9MVRefPair *, 64 * s->sb_rows);
    assign(s->mv[1],               VP9MVRefPair *, 64 * s->sb_rows);
#undef assign

    return 0;
}

// The sign bit is at the end, not the start, of a bit sequence
static av_always_inline int get_bits_with_sign(GetBitContext *gb, int n)
{
    int v = get_bits(gb, n);
    return get_bits1(gb) ? -v : v;
}

static av_always_inline int inv_recenter_nonneg(int v, int m)
{
    if (v > 2 * m)
        return v;
    if (v & 1)
        return m - ((v + 1) >> 1);
    return m + (v >> 1);
}

// differential forward probability updates
static int update_prob(VP56RangeCoder *c, int p)
{
    static const int inv_map_table[MAX_PROB - 1] = {
          7,  20,  33,  46,  59,  72,  85,  98, 111, 124, 137, 150, 163, 176,
        189, 202, 215, 228, 241, 254,   1,   2,   3,   4,   5,   6,   8,   9,
         10,  11,  12,  13,  14,  15,  16,  17,  18,  19,  21,  22,  23,  24,
         25,  26,  27,  28,  29,  30,  31,  32,  34,  35,  36,  37,  38,  39,
         40,  41,  42,  43,  44,  45,  47,  48,  49,  50,  51,  52,  53,  54,
         55,  56,  57,  58,  60,  61,  62,  63,  64,  65,  66,  67,  68,  69,
         70,  71,  73,  74,  75,  76,  77,  78,  79,  80,  81,  82,  83,  84,
         86,  87,  88,  89,  90,  91,  92,  93,  94,  95,  96,  97,  99, 100,
        101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 112, 113, 114, 115,
        116, 117, 118, 119, 120, 121, 122, 123, 125, 126, 127, 128, 129, 130,
        131, 132, 133, 134, 135, 136, 138, 139, 140, 141, 142, 143, 144, 145,
        146, 147, 148, 149, 151, 152, 153, 154, 155, 156, 157, 158, 159, 160,
        161, 162, 164, 165, 166, 167, 168, 169, 170, 171, 172, 173, 174, 175,
        177, 178, 179, 180, 181, 182, 183, 184, 185, 186, 187, 188, 190, 191,
        192, 193, 194, 195, 196, 197, 198, 199, 200, 201, 203, 204, 205, 206,
        207, 208, 209, 210, 211, 212, 213, 214, 216, 217, 218, 219, 220, 221,
        222, 223, 224, 225, 226, 227, 229, 230, 231, 232, 233, 234, 235, 236,
        237, 238, 239, 240, 242, 243, 244, 245, 246, 247, 248, 249, 250, 251,
        252, 253,
    };
    int d;

    /* This code is trying to do a differential probability update. For a
     * current probability A in the range [1, 255], the difference to a new
     * probability of any value can be expressed differentially as 1-A, 255-A
     * where some part of this (absolute range) exists both in positive as
     * well as the negative part, whereas another part only exists in one
     * half. We're trying to code this shared part differentially, i.e.
     * times two where the value of the lowest bit specifies the sign, and
     * the single part is then coded on top of this. This absolute difference
     * then again has a value of [0, 254], but a bigger value in this range
     * indicates that we're further away from the original value A, so we
     * can code this as a VLC code, since higher values are increasingly
     * unlikely. The first 20 values in inv_map_table[] allow 'cheap, rough'
     * updates vs. the 'fine, exact' updates further down the range, which
     * adds one extra dimension to this differential update model. */

    if (!vp8_rac_get(c)) {
        d = vp8_rac_get_uint(c, 4) + 0;
    } else if (!vp8_rac_get(c)) {
        d = vp8_rac_get_uint(c, 4) + 16;
    } else if (!vp8_rac_get(c)) {
        d = vp8_rac_get_uint(c, 5) + 32;
    } else {
        d = vp8_rac_get_uint(c, 7);
        if (d >= 65) {
            d = (d << 1) - 65 + vp8_rac_get(c);
            d = av_clip(d, 0, MAX_PROB - 65 - 1);
        }
        d += 64;
    }

    return p <= 128
           ?   1 + inv_recenter_nonneg(inv_map_table[d], p - 1)
           : 255 - inv_recenter_nonneg(inv_map_table[d], 255 - p);
}

static int decode_frame_header(AVCodecContext *avctx,
                               const uint8_t *data, int size, int *ref)
{
    VP9Context *s = avctx->priv_data;
    int c, i, j, k, l, m, n, w, h, max, size2, ret, sharp;
    int last_invisible;
    const uint8_t *data2;

    /* general header */
    if ((ret = init_get_bits8(&s->gb, data, size)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to initialize bitstream reader\n");
        return ret;
    }
    if (get_bits(&s->gb, 2) != 0x2) { // frame marker
        av_log(avctx, AV_LOG_ERROR, "Invalid frame marker\n");
        return AVERROR_INVALIDDATA;
    }
    s->profile = get_bits1(&s->gb);
    if (get_bits1(&s->gb)) { // reserved bit
        av_log(avctx, AV_LOG_ERROR, "Reserved bit should be zero\n");
        return AVERROR_INVALIDDATA;
    }
    if (get_bits1(&s->gb)) {
        *ref = get_bits(&s->gb, 3);
        return 0;
    }

    s->last_keyframe = s->keyframe;
    s->keyframe      = !get_bits1(&s->gb);

    last_invisible = s->invisible;
    s->invisible   = !get_bits1(&s->gb);
    s->errorres    = get_bits1(&s->gb);
    // FIXME disable this upon resolution change
    s->use_last_frame_mvs = !s->errorres && !last_invisible;

    if (s->keyframe) {
        if (get_bits_long(&s->gb, 24) != VP9_SYNCCODE) { // synccode
            av_log(avctx, AV_LOG_ERROR, "Invalid sync code\n");
            return AVERROR_INVALIDDATA;
        }
        s->colorspace = get_bits(&s->gb, 3);
        if (s->colorspace == 7) { // RGB = profile 1
            av_log(avctx, AV_LOG_ERROR, "RGB not supported in profile 0\n");
            return AVERROR_INVALIDDATA;
        }
        s->fullrange = get_bits1(&s->gb);
        // for profile 1, here follows the subsampling bits
        s->refreshrefmask = 0xff;
        w = get_bits(&s->gb, 16) + 1;
        h = get_bits(&s->gb, 16) + 1;
        if (get_bits1(&s->gb)) // display size
            skip_bits(&s->gb, 32);
    } else {
        s->intraonly = s->invisible ? get_bits1(&s->gb) : 0;
        s->resetctx  = s->errorres ? 0 : get_bits(&s->gb, 2);
        if (s->intraonly) {
            if (get_bits_long(&s->gb, 24) != VP9_SYNCCODE) { // synccode
                av_log(avctx, AV_LOG_ERROR, "Invalid sync code\n");
                return AVERROR_INVALIDDATA;
            }
            s->refreshrefmask = get_bits(&s->gb, 8);
            w = get_bits(&s->gb, 16) + 1;
            h = get_bits(&s->gb, 16) + 1;
            if (get_bits1(&s->gb)) // display size
                skip_bits(&s->gb, 32);
        } else {
            s->refreshrefmask = get_bits(&s->gb, 8);
            s->refidx[0]      = get_bits(&s->gb, 3);
            s->signbias[0]    = get_bits1(&s->gb);
            s->refidx[1]      = get_bits(&s->gb, 3);
            s->signbias[1]    = get_bits1(&s->gb);
            s->refidx[2]      = get_bits(&s->gb, 3);
            s->signbias[2]    = get_bits1(&s->gb);
            if (!s->refs[s->refidx[0]]->buf[0] ||
                !s->refs[s->refidx[1]]->buf[0] ||
                !s->refs[s->refidx[2]]->buf[0]) {
                av_log(avctx, AV_LOG_ERROR,
                       "Not all references are available\n");
                return AVERROR_INVALIDDATA;
            }
            if (get_bits1(&s->gb)) {
                w = s->refs[s->refidx[0]]->width;
                h = s->refs[s->refidx[0]]->height;
            } else if (get_bits1(&s->gb)) {
                w = s->refs[s->refidx[1]]->width;
                h = s->refs[s->refidx[1]]->height;
            } else if (get_bits1(&s->gb)) {
                w = s->refs[s->refidx[2]]->width;
                h = s->refs[s->refidx[2]]->height;
            } else {
                w = get_bits(&s->gb, 16) + 1;
                h = get_bits(&s->gb, 16) + 1;
            }
            if (get_bits1(&s->gb)) // display size
                skip_bits(&s->gb, 32);
            s->highprecisionmvs = get_bits1(&s->gb);
            s->filtermode       = get_bits1(&s->gb) ? FILTER_SWITCHABLE :
                                  get_bits(&s->gb, 2);
            s->allowcompinter   = s->signbias[0] != s->signbias[1] ||
                                  s->signbias[0] != s->signbias[2];
            if (s->allowcompinter) {
                if (s->signbias[0] == s->signbias[1]) {
                    s->fixcompref    = 2;
                    s->varcompref[0] = 0;
                    s->varcompref[1] = 1;
                } else if (s->signbias[0] == s->signbias[2]) {
                    s->fixcompref    = 1;
                    s->varcompref[0] = 0;
                    s->varcompref[1] = 2;
                } else {
                    s->fixcompref    = 0;
                    s->varcompref[0] = 1;
                    s->varcompref[1] = 2;
                }
            }
        }
    }

    s->refreshctx   = s->errorres ? 0 : get_bits1(&s->gb);
    s->parallelmode = s->errorres ? 1 : get_bits1(&s->gb);
    s->framectxid   = c = get_bits(&s->gb, 2);

    /* loopfilter header data */
    s->filter.level = get_bits(&s->gb, 6);
    sharp           = get_bits(&s->gb, 3);
    /* If sharpness changed, reinit lim/mblim LUTs. if it didn't change,
     * keep the old cache values since they are still valid. */
    if (s->filter.sharpness != sharp)
        memset(s->filter.lim_lut, 0, sizeof(s->filter.lim_lut));
    s->filter.sharpness = sharp;
    if ((s->lf_delta.enabled = get_bits1(&s->gb))) {
        if (get_bits1(&s->gb)) {
            for (i = 0; i < 4; i++)
                if (get_bits1(&s->gb))
                    s->lf_delta.ref[i] = get_bits_with_sign(&s->gb, 6);
            for (i = 0; i < 2; i++)
                if (get_bits1(&s->gb))
                    s->lf_delta.mode[i] = get_bits_with_sign(&s->gb, 6);
        }
    } else {
        memset(&s->lf_delta, 0, sizeof(s->lf_delta));
    }

    /* quantization header data */
    s->yac_qi      = get_bits(&s->gb, 8);
    s->ydc_qdelta  = get_bits1(&s->gb) ? get_bits_with_sign(&s->gb, 4) : 0;
    s->uvdc_qdelta = get_bits1(&s->gb) ? get_bits_with_sign(&s->gb, 4) : 0;
    s->uvac_qdelta = get_bits1(&s->gb) ? get_bits_with_sign(&s->gb, 4) : 0;
    s->lossless    = s->yac_qi == 0 && s->ydc_qdelta == 0 &&
                     s->uvdc_qdelta == 0 && s->uvac_qdelta == 0;

    /* segmentation header info */
    if ((s->segmentation.enabled = get_bits1(&s->gb))) {
        if ((s->segmentation.update_map = get_bits1(&s->gb))) {
            for (i = 0; i < 7; i++)
                s->prob.seg[i] = get_bits1(&s->gb) ?
                                 get_bits(&s->gb, 8) : 255;
            if ((s->segmentation.temporal = get_bits1(&s->gb)))
                for (i = 0; i < 3; i++)
                    s->prob.segpred[i] = get_bits1(&s->gb) ?
                                         get_bits(&s->gb, 8) : 255;
        }

        if (get_bits1(&s->gb)) {
            s->segmentation.absolute_vals = get_bits1(&s->gb);
            for (i = 0; i < 8; i++) {
                if ((s->segmentation.feat[i].q_enabled = get_bits1(&s->gb)))
                    s->segmentation.feat[i].q_val = get_bits_with_sign(&s->gb, 8);
                if ((s->segmentation.feat[i].lf_enabled = get_bits1(&s->gb)))
                    s->segmentation.feat[i].lf_val = get_bits_with_sign(&s->gb, 6);
                if ((s->segmentation.feat[i].ref_enabled = get_bits1(&s->gb)))
                    s->segmentation.feat[i].ref_val = get_bits(&s->gb, 2);
                s->segmentation.feat[i].skip_enabled = get_bits1(&s->gb);
            }
        }
    } else {
        s->segmentation.feat[0].q_enabled    = 0;
        s->segmentation.feat[0].lf_enabled   = 0;
        s->segmentation.feat[0].skip_enabled = 0;
        s->segmentation.feat[0].ref_enabled  = 0;
    }

    // set qmul[] based on Y/UV, AC/DC and segmentation Q idx deltas
    for (i = 0; i < (s->segmentation.enabled ? 8 : 1); i++) {
        int qyac, qydc, quvac, quvdc, lflvl, sh;

        if (s->segmentation.feat[i].q_enabled) {
            if (s->segmentation.absolute_vals)
                qyac = s->segmentation.feat[i].q_val;
            else
                qyac = s->yac_qi + s->segmentation.feat[i].q_val;
        } else {
            qyac = s->yac_qi;
        }
        qydc  = av_clip_uintp2(qyac + s->ydc_qdelta, 8);
        quvdc = av_clip_uintp2(qyac + s->uvdc_qdelta, 8);
        quvac = av_clip_uintp2(qyac + s->uvac_qdelta, 8);
        qyac  = av_clip_uintp2(qyac, 8);

        s->segmentation.feat[i].qmul[0][0] = ff_vp9_dc_qlookup[qydc];
        s->segmentation.feat[i].qmul[0][1] = ff_vp9_ac_qlookup[qyac];
        s->segmentation.feat[i].qmul[1][0] = ff_vp9_dc_qlookup[quvdc];
        s->segmentation.feat[i].qmul[1][1] = ff_vp9_ac_qlookup[quvac];

        sh = s->filter.level >= 32;
        if (s->segmentation.feat[i].lf_enabled) {
            if (s->segmentation.absolute_vals)
                lflvl = s->segmentation.feat[i].lf_val;
            else
                lflvl = s->filter.level + s->segmentation.feat[i].lf_val;
        } else {
            lflvl = s->filter.level;
        }
        s->segmentation.feat[i].lflvl[0][0] =
        s->segmentation.feat[i].lflvl[0][1] =
            av_clip_uintp2(lflvl + (s->lf_delta.ref[0] << sh), 6);
        for (j = 1; j < 4; j++) {
            s->segmentation.feat[i].lflvl[j][0] =
                av_clip_uintp2(lflvl + ((s->lf_delta.ref[j] +
                                         s->lf_delta.mode[0]) << sh), 6);
            s->segmentation.feat[i].lflvl[j][1] =
                av_clip_uintp2(lflvl + ((s->lf_delta.ref[j] +
                                         s->lf_delta.mode[1]) << sh), 6);
        }
    }

    /* tiling info */
    if ((ret = update_size(avctx, w, h)) < 0) {
        av_log(avctx, AV_LOG_ERROR,
               "Failed to initialize decoder for %dx%d\n", w, h);
        return ret;
    }
    for (s->tiling.log2_tile_cols = 0;
         (s->sb_cols >> s->tiling.log2_tile_cols) > 64;
         s->tiling.log2_tile_cols++) ;
    for (max = 0; (s->sb_cols >> max) >= 4; max++) ;
    max = FFMAX(0, max - 1);
    while (max > s->tiling.log2_tile_cols) {
        if (get_bits1(&s->gb))
            s->tiling.log2_tile_cols++;
        else
            break;
    }
    s->tiling.log2_tile_rows = decode012(&s->gb);
    s->tiling.tile_rows      = 1 << s->tiling.log2_tile_rows;
    if (s->tiling.tile_cols != (1 << s->tiling.log2_tile_cols)) {
        s->tiling.tile_cols = 1 << s->tiling.log2_tile_cols;
        s->c_b              = av_fast_realloc(s->c_b, &s->c_b_size,
                                              sizeof(VP56RangeCoder) *
                                              s->tiling.tile_cols);
        if (!s->c_b) {
            av_log(avctx, AV_LOG_ERROR,
                   "Ran out of memory during range coder init\n");
            return AVERROR(ENOMEM);
        }
    }

    if (s->keyframe || s->errorres || s->intraonly) {
        s->prob_ctx[0].p =
        s->prob_ctx[1].p =
        s->prob_ctx[2].p =
        s->prob_ctx[3].p = ff_vp9_default_probs;
        memcpy(s->prob_ctx[0].coef, ff_vp9_default_coef_probs,
               sizeof(ff_vp9_default_coef_probs));
        memcpy(s->prob_ctx[1].coef, ff_vp9_default_coef_probs,
               sizeof(ff_vp9_default_coef_probs));
        memcpy(s->prob_ctx[2].coef, ff_vp9_default_coef_probs,
               sizeof(ff_vp9_default_coef_probs));
        memcpy(s->prob_ctx[3].coef, ff_vp9_default_coef_probs,
               sizeof(ff_vp9_default_coef_probs));
    }

    // next 16 bits is size of the rest of the header (arith-coded)
    size2 = get_bits(&s->gb, 16);
    data2 = align_get_bits(&s->gb);
    if (size2 > size - (data2 - data)) {
        av_log(avctx, AV_LOG_ERROR, "Invalid compressed header size\n");
        return AVERROR_INVALIDDATA;
    }
    ff_vp56_init_range_decoder(&s->c, data2, size2);
    if (vp56_rac_get_prob_branchy(&s->c, 128)) { // marker bit
        av_log(avctx, AV_LOG_ERROR, "Marker bit was set\n");
        return AVERROR_INVALIDDATA;
    }

    if (s->keyframe || s->intraonly)
        memset(s->counts.coef, 0,
               sizeof(s->counts.coef) + sizeof(s->counts.eob));
    else
        memset(&s->counts, 0, sizeof(s->counts));

    /* FIXME is it faster to not copy here, but do it down in the fw updates
     * as explicit copies if the fw update is missing (and skip the copy upon
     * fw update)? */
    s->prob.p = s->prob_ctx[c].p;

    // txfm updates
    if (s->lossless) {
        s->txfmmode = TX_4X4;
    } else {
        s->txfmmode = vp8_rac_get_uint(&s->c, 2);
        if (s->txfmmode == 3)
            s->txfmmode += vp8_rac_get(&s->c);

        if (s->txfmmode == TX_SWITCHABLE) {
            for (i = 0; i < 2; i++)
                if (vp56_rac_get_prob_branchy(&s->c, 252))
                    s->prob.p.tx8p[i] = update_prob(&s->c, s->prob.p.tx8p[i]);
            for (i = 0; i < 2; i++)
                for (j = 0; j < 2; j++)
                    if (vp56_rac_get_prob_branchy(&s->c, 252))
                        s->prob.p.tx16p[i][j] =
                            update_prob(&s->c, s->prob.p.tx16p[i][j]);
            for (i = 0; i < 2; i++)
                for (j = 0; j < 3; j++)
                    if (vp56_rac_get_prob_branchy(&s->c, 252))
                        s->prob.p.tx32p[i][j] =
                            update_prob(&s->c, s->prob.p.tx32p[i][j]);
        }
    }

    // coef updates
    for (i = 0; i < 4; i++) {
        uint8_t (*ref)[2][6][6][3] = s->prob_ctx[c].coef[i];
        if (vp8_rac_get(&s->c)) {
            for (j = 0; j < 2; j++)
                for (k = 0; k < 2; k++)
                    for (l = 0; l < 6; l++)
                        for (m = 0; m < 6; m++) {
                            uint8_t *p = s->prob.coef[i][j][k][l][m];
                            uint8_t *r = ref[j][k][l][m];
                            if (m >= 3 && l == 0) // dc only has 3 pt
                                break;
                            for (n = 0; n < 3; n++) {
                                if (vp56_rac_get_prob_branchy(&s->c, 252))
                                    p[n] = update_prob(&s->c, r[n]);
                                else
                                    p[n] = r[n];
                            }
                            p[3] = 0;
                        }
        } else {
            for (j = 0; j < 2; j++)
                for (k = 0; k < 2; k++)
                    for (l = 0; l < 6; l++)
                        for (m = 0; m < 6; m++) {
                            uint8_t *p = s->prob.coef[i][j][k][l][m];
                            uint8_t *r = ref[j][k][l][m];
                            if (m > 3 && l == 0) // dc only has 3 pt
                                break;
                            memcpy(p, r, 3);
                            p[3] = 0;
                        }
        }
        if (s->txfmmode == i)
            break;
    }

    // mode updates
    for (i = 0; i < 3; i++)
        if (vp56_rac_get_prob_branchy(&s->c, 252))
            s->prob.p.skip[i] = update_prob(&s->c, s->prob.p.skip[i]);
    if (!s->keyframe && !s->intraonly) {
        for (i = 0; i < 7; i++)
            for (j = 0; j < 3; j++)
                if (vp56_rac_get_prob_branchy(&s->c, 252))
                    s->prob.p.mv_mode[i][j] =
                        update_prob(&s->c, s->prob.p.mv_mode[i][j]);

        if (s->filtermode == FILTER_SWITCHABLE)
            for (i = 0; i < 4; i++)
                for (j = 0; j < 2; j++)
                    if (vp56_rac_get_prob_branchy(&s->c, 252))
                        s->prob.p.filter[i][j] =
                            update_prob(&s->c, s->prob.p.filter[i][j]);

        for (i = 0; i < 4; i++)
            if (vp56_rac_get_prob_branchy(&s->c, 252))
                s->prob.p.intra[i] = update_prob(&s->c, s->prob.p.intra[i]);

        if (s->allowcompinter) {
            s->comppredmode = vp8_rac_get(&s->c);
            if (s->comppredmode)
                s->comppredmode += vp8_rac_get(&s->c);
            if (s->comppredmode == PRED_SWITCHABLE)
                for (i = 0; i < 5; i++)
                    if (vp56_rac_get_prob_branchy(&s->c, 252))
                        s->prob.p.comp[i] =
                            update_prob(&s->c, s->prob.p.comp[i]);
        } else {
            s->comppredmode = PRED_SINGLEREF;
        }

        if (s->comppredmode != PRED_COMPREF) {
            for (i = 0; i < 5; i++) {
                if (vp56_rac_get_prob_branchy(&s->c, 252))
                    s->prob.p.single_ref[i][0] =
                        update_prob(&s->c, s->prob.p.single_ref[i][0]);
                if (vp56_rac_get_prob_branchy(&s->c, 252))
                    s->prob.p.single_ref[i][1] =
                        update_prob(&s->c, s->prob.p.single_ref[i][1]);
            }
        }

        if (s->comppredmode != PRED_SINGLEREF) {
            for (i = 0; i < 5; i++)
                if (vp56_rac_get_prob_branchy(&s->c, 252))
                    s->prob.p.comp_ref[i] =
                        update_prob(&s->c, s->prob.p.comp_ref[i]);
        }

        for (i = 0; i < 4; i++)
            for (j = 0; j < 9; j++)
                if (vp56_rac_get_prob_branchy(&s->c, 252))
                    s->prob.p.y_mode[i][j] =
                        update_prob(&s->c, s->prob.p.y_mode[i][j]);

        for (i = 0; i < 4; i++)
            for (j = 0; j < 4; j++)
                for (k = 0; k < 3; k++)
                    if (vp56_rac_get_prob_branchy(&s->c, 252))
                        s->prob.p.partition[3 - i][j][k] =
                            update_prob(&s->c,
                                        s->prob.p.partition[3 - i][j][k]);

        // mv fields don't use the update_prob subexp model for some reason
        for (i = 0; i < 3; i++)
            if (vp56_rac_get_prob_branchy(&s->c, 252))
                s->prob.p.mv_joint[i] = (vp8_rac_get_uint(&s->c, 7) << 1) | 1;

        for (i = 0; i < 2; i++) {
            if (vp56_rac_get_prob_branchy(&s->c, 252))
                s->prob.p.mv_comp[i].sign =
                    (vp8_rac_get_uint(&s->c, 7) << 1) | 1;

            for (j = 0; j < 10; j++)
                if (vp56_rac_get_prob_branchy(&s->c, 252))
                    s->prob.p.mv_comp[i].classes[j] =
                        (vp8_rac_get_uint(&s->c, 7) << 1) | 1;

            if (vp56_rac_get_prob_branchy(&s->c, 252))
                s->prob.p.mv_comp[i].class0 =
                    (vp8_rac_get_uint(&s->c, 7) << 1) | 1;

            for (j = 0; j < 10; j++)
                if (vp56_rac_get_prob_branchy(&s->c, 252))
                    s->prob.p.mv_comp[i].bits[j] =
                        (vp8_rac_get_uint(&s->c, 7) << 1) | 1;
        }

        for (i = 0; i < 2; i++) {
            for (j = 0; j < 2; j++)
                for (k = 0; k < 3; k++)
                    if (vp56_rac_get_prob_branchy(&s->c, 252))
                        s->prob.p.mv_comp[i].class0_fp[j][k] =
                            (vp8_rac_get_uint(&s->c, 7) << 1) | 1;

            for (j = 0; j < 3; j++)
                if (vp56_rac_get_prob_branchy(&s->c, 252))
                    s->prob.p.mv_comp[i].fp[j] =
                        (vp8_rac_get_uint(&s->c, 7) << 1) | 1;
        }

        if (s->highprecisionmvs) {
            for (i = 0; i < 2; i++) {
                if (vp56_rac_get_prob_branchy(&s->c, 252))
                    s->prob.p.mv_comp[i].class0_hp =
                        (vp8_rac_get_uint(&s->c, 7) << 1) | 1;

                if (vp56_rac_get_prob_branchy(&s->c, 252))
                    s->prob.p.mv_comp[i].hp =
                        (vp8_rac_get_uint(&s->c, 7) << 1) | 1;
            }
        }
    }

    return (data2 - data) + size2;
}

static int decode_subblock(AVCodecContext *avctx, int row, int col,
                           VP9Filter *lflvl,
                           ptrdiff_t yoff, ptrdiff_t uvoff, enum BlockLevel bl)
{
    VP9Context *s = avctx->priv_data;
    int c = ((s->above_partition_ctx[col]       >> (3 - bl)) & 1) |
            (((s->left_partition_ctx[row & 0x7] >> (3 - bl)) & 1) << 1);
    int ret;
    const uint8_t *p = s->keyframe ? ff_vp9_default_kf_partition_probs[bl][c]
                                   : s->prob.p.partition[bl][c];
    enum BlockPartition bp;
    ptrdiff_t hbs = 4 >> bl;

    if (bl == BL_8X8) {
        bp  = vp8_rac_get_tree(&s->c, ff_vp9_partition_tree, p);
        ret = ff_vp9_decode_block(avctx, row, col, lflvl, yoff, uvoff, bl, bp);
    } else if (col + hbs < s->cols) {
        if (row + hbs < s->rows) {
            bp = vp8_rac_get_tree(&s->c, ff_vp9_partition_tree, p);
            switch (bp) {
            case PARTITION_NONE:
                ret = ff_vp9_decode_block(avctx, row, col, lflvl, yoff, uvoff,
                                          bl, bp);
                break;
            case PARTITION_H:
                ret = ff_vp9_decode_block(avctx, row, col, lflvl, yoff, uvoff,
                                          bl, bp);
                if (!ret) {
                    yoff  += hbs * 8 * s->cur_frame->linesize[0];
                    uvoff += hbs * 4 * s->cur_frame->linesize[1];
                    ret    = ff_vp9_decode_block(avctx, row + hbs, col, lflvl,
                                                 yoff, uvoff, bl, bp);
                }
                break;
            case PARTITION_V:
                ret = ff_vp9_decode_block(avctx, row, col, lflvl, yoff, uvoff,
                                          bl, bp);
                if (!ret) {
                    yoff  += hbs * 8;
                    uvoff += hbs * 4;
                    ret    = ff_vp9_decode_block(avctx, row, col + hbs, lflvl,
                                                 yoff, uvoff, bl, bp);
                }
                break;
            case PARTITION_SPLIT:
                ret = decode_subblock(avctx, row, col, lflvl,
                                      yoff, uvoff, bl + 1);
                if (!ret) {
                    ret = decode_subblock(avctx, row, col + hbs, lflvl,
                                          yoff + 8 * hbs, uvoff + 4 * hbs,
                                          bl + 1);
                    if (!ret) {
                        yoff  += hbs * 8 * s->cur_frame->linesize[0];
                        uvoff += hbs * 4 * s->cur_frame->linesize[1];
                        ret    = decode_subblock(avctx, row + hbs, col, lflvl,
                                                 yoff, uvoff, bl + 1);
                        if (!ret) {
                            ret = decode_subblock(avctx, row + hbs, col + hbs,
                                                  lflvl, yoff + 8 * hbs,
                                                  uvoff + 4 * hbs, bl + 1);
                        }
                    }
                }
                break;
            default:
                av_log(avctx, AV_LOG_ERROR, "Unexpected partition %d.", bp);
                return AVERROR_INVALIDDATA;
            }
        } else if (vp56_rac_get_prob_branchy(&s->c, p[1])) {
            bp  = PARTITION_SPLIT;
            ret = decode_subblock(avctx, row, col, lflvl, yoff, uvoff, bl + 1);
            if (!ret)
                ret = decode_subblock(avctx, row, col + hbs, lflvl,
                                      yoff + 8 * hbs, uvoff + 4 * hbs, bl + 1);
        } else {
            bp  = PARTITION_H;
            ret = ff_vp9_decode_block(avctx, row, col, lflvl, yoff, uvoff,
                                      bl, bp);
        }
    } else if (row + hbs < s->rows) {
        if (vp56_rac_get_prob_branchy(&s->c, p[2])) {
            bp  = PARTITION_SPLIT;
            ret = decode_subblock(avctx, row, col, lflvl, yoff, uvoff, bl + 1);
            if (!ret) {
                yoff  += hbs * 8 * s->cur_frame->linesize[0];
                uvoff += hbs * 4 * s->cur_frame->linesize[1];
                ret    = decode_subblock(avctx, row + hbs, col, lflvl,
                                         yoff, uvoff, bl + 1);
            }
        } else {
            bp  = PARTITION_V;
            ret = ff_vp9_decode_block(avctx, row, col, lflvl, yoff, uvoff,
                                      bl, bp);
        }
    } else {
        bp  = PARTITION_SPLIT;
        ret = decode_subblock(avctx, row, col, lflvl, yoff, uvoff, bl + 1);
    }
    s->counts.partition[bl][c][bp]++;

    return ret;
}

static void loopfilter_subblock(AVCodecContext *avctx, VP9Filter *lflvl,
                                int row, int col,
                                ptrdiff_t yoff, ptrdiff_t uvoff)
{
    VP9Context *s = avctx->priv_data;
    uint8_t *dst   = s->cur_frame->data[0] + yoff, *lvl = lflvl->level;
    ptrdiff_t ls_y = s->cur_frame->linesize[0], ls_uv = s->cur_frame->linesize[1];
    int y, x, p;

    /* FIXME: In how far can we interleave the v/h loopfilter calls? E.g.
     * if you think of them as acting on a 8x8 block max, we can interleave
     * each v/h within the single x loop, but that only works if we work on
     * 8 pixel blocks, and we won't always do that (we want at least 16px
     * to use SSE2 optimizations, perhaps 32 for AVX2). */

    // filter edges between columns, Y plane (e.g. block1 | block2)
    for (y = 0; y < 8; y += 2, dst += 16 * ls_y, lvl += 16) {
        uint8_t *ptr = dst, *l = lvl, *hmask1 = lflvl->mask[0][0][y];
        uint8_t *hmask2 = lflvl->mask[0][0][y + 1];
        unsigned hm1 = hmask1[0] | hmask1[1] | hmask1[2], hm13 = hmask1[3];
        unsigned hm2 = hmask2[1] | hmask2[2], hm23 = hmask2[3];
        unsigned hm  = hm1 | hm2 | hm13 | hm23;

        for (x = 1; hm & ~(x - 1); x <<= 1, ptr += 8, l++) {
            if (hm1 & x) {
                int L = *l, H = L >> 4;
                int E = s->filter.mblim_lut[L], I = s->filter.lim_lut[L];

                if (col || x > 1) {
                    if (hmask1[0] & x) {
                        if (hmask2[0] & x) {
                            av_assert2(l[8] == L);
                            s->dsp.loop_filter_16[0](ptr, ls_y, E, I, H);
                        } else {
                            s->dsp.loop_filter_8[2][0](ptr, ls_y, E, I, H);
                        }
                    } else if (hm2 & x) {
                        L  = l[8];
                        H |= (L >> 4) << 8;
                        E |= s->filter.mblim_lut[L] << 8;
                        I |= s->filter.lim_lut[L] << 8;
                        s->dsp.loop_filter_mix2[!!(hmask1[1] & x)]
                                               [!!(hmask2[1] & x)]
                                               [0](ptr, ls_y, E, I, H);
                    } else {
                        s->dsp.loop_filter_8[!!(hmask1[1] & x)]
                                            [0](ptr, ls_y, E, I, H);
                    }
                }
            } else if (hm2 & x) {
                int L = l[8], H = L >> 4;
                int E = s->filter.mblim_lut[L], I = s->filter.lim_lut[L];

                if (col || x > 1) {
                    s->dsp.loop_filter_8[!!(hmask2[1] & x)]
                                        [0](ptr + 8 * ls_y, ls_y, E, I, H);
                }
            }
            if (hm13 & x) {
                int L = *l, H = L >> 4;
                int E = s->filter.mblim_lut[L], I = s->filter.lim_lut[L];

                if (hm23 & x) {
                    L  = l[8];
                    H |= (L >> 4) << 8;
                    E |= s->filter.mblim_lut[L] << 8;
                    I |= s->filter.lim_lut[L] << 8;
                    s->dsp.loop_filter_mix2[0][0][0](ptr + 4, ls_y, E, I, H);
                } else {
                    s->dsp.loop_filter_8[0][0](ptr + 4, ls_y, E, I, H);
                }
            } else if (hm23 & x) {
                int L = l[8], H = L >> 4;
                int E = s->filter.mblim_lut[L], I = s->filter.lim_lut[L];

                s->dsp.loop_filter_8[0][0](ptr + 8 * ls_y + 4, ls_y, E, I, H);
            }
        }
    }

    //                                          block1
    // filter edges between rows, Y plane (e.g. ------)
    //                                          block2
    dst = s->cur_frame->data[0] + yoff;
    lvl = lflvl->level;
    for (y = 0; y < 8; y++, dst += 8 * ls_y, lvl += 8) {
        uint8_t *ptr = dst, *l = lvl, *vmask = lflvl->mask[0][1][y];
        unsigned vm = vmask[0] | vmask[1] | vmask[2], vm3 = vmask[3];

        for (x = 1; vm & ~(x - 1); x <<= 2, ptr += 16, l += 2) {
            if (row || y) {
                if (vm & x) {
                    int L = *l, H = L >> 4;
                    int E = s->filter.mblim_lut[L], I = s->filter.lim_lut[L];

                    if (vmask[0] & x) {
                        if (vmask[0] & (x << 1)) {
                            av_assert2(l[1] == L);
                            s->dsp.loop_filter_16[1](ptr, ls_y, E, I, H);
                        } else {
                            s->dsp.loop_filter_8[2][1](ptr, ls_y, E, I, H);
                        }
                    } else if (vm & (x << 1)) {
                        L  = l[1];
                        H |= (L >> 4) << 8;
                        E |= s->filter.mblim_lut[L] << 8;
                        I |= s->filter.lim_lut[L] << 8;
                        s->dsp.loop_filter_mix2[!!(vmask[1] &  x)]
                                               [!!(vmask[1] & (x << 1))]
                                               [1](ptr, ls_y, E, I, H);
                    } else {
                        s->dsp.loop_filter_8[!!(vmask[1] & x)]
                                            [1](ptr, ls_y, E, I, H);
                    }
                } else if (vm & (x << 1)) {
                    int L = l[1], H = L >> 4;
                    int E = s->filter.mblim_lut[L], I = s->filter.lim_lut[L];

                    s->dsp.loop_filter_8[!!(vmask[1] & (x << 1))]
                                        [1](ptr + 8, ls_y, E, I, H);
                }
            }
            if (vm3 & x) {
                int L = *l, H = L >> 4;
                int E = s->filter.mblim_lut[L], I = s->filter.lim_lut[L];

                if (vm3 & (x << 1)) {
                    L  = l[1];
                    H |= (L >> 4) << 8;
                    E |= s->filter.mblim_lut[L] << 8;
                    I |= s->filter.lim_lut[L] << 8;
                    s->dsp.loop_filter_mix2[0][0][1](ptr + ls_y * 4, ls_y, E, I, H);
                } else {
                    s->dsp.loop_filter_8[0][1](ptr + ls_y * 4, ls_y, E, I, H);
                }
            } else if (vm3 & (x << 1)) {
                int L = l[1], H = L >> 4;
                int E = s->filter.mblim_lut[L], I = s->filter.lim_lut[L];

                s->dsp.loop_filter_8[0][1](ptr + ls_y * 4 + 8, ls_y, E, I, H);
            }
        }
    }

    // same principle but for U/V planes
    for (p = 0; p < 2; p++) {
        lvl = lflvl->level;
        dst = s->cur_frame->data[1 + p] + uvoff;
        for (y = 0; y < 8; y += 4, dst += 16 * ls_uv, lvl += 32) {
            uint8_t *ptr = dst, *l = lvl, *hmask1 = lflvl->mask[1][0][y];
            uint8_t *hmask2 = lflvl->mask[1][0][y + 2];
            unsigned hm1 = hmask1[0] | hmask1[1] | hmask1[2];
            unsigned hm2 = hmask2[1] | hmask2[2], hm = hm1 | hm2;

            for (x = 1; hm & ~(x - 1); x <<= 1, ptr += 4) {
                if (col || x > 1) {
                    if (hm1 & x) {
                        int L = *l, H = L >> 4;
                        int E = s->filter.mblim_lut[L];
                        int I = s->filter.lim_lut[L];

                        if (hmask1[0] & x) {
                            if (hmask2[0] & x) {
                                av_assert2(l[16] == L);
                                s->dsp.loop_filter_16[0](ptr, ls_uv, E, I, H);
                            } else {
                                s->dsp.loop_filter_8[2][0](ptr, ls_uv, E, I, H);
                            }
                        } else if (hm2 & x) {
                            L  = l[16];
                            H |= (L >> 4) << 8;
                            E |= s->filter.mblim_lut[L] << 8;
                            I |= s->filter.lim_lut[L] << 8;
                            s->dsp.loop_filter_mix2[!!(hmask1[1] & x)]
                                                   [!!(hmask2[1] & x)]
                                                   [0](ptr, ls_uv, E, I, H);
                        } else {
                            s->dsp.loop_filter_8[!!(hmask1[1] & x)]
                                                [0](ptr, ls_uv, E, I, H);
                        }
                    } else if (hm2 & x) {
                        int L = l[16], H = L >> 4;
                        int E = s->filter.mblim_lut[L];
                        int I = s->filter.lim_lut[L];

                        s->dsp.loop_filter_8[!!(hmask2[1] & x)]
                                            [0](ptr + 8 * ls_uv, ls_uv, E, I, H);
                    }
                }
                if (x & 0xAA)
                    l += 2;
            }
        }
        lvl = lflvl->level;
        dst = s->cur_frame->data[1 + p] + uvoff;
        for (y = 0; y < 8; y++, dst += 4 * ls_uv) {
            uint8_t *ptr = dst, *l = lvl, *vmask = lflvl->mask[1][1][y];
            unsigned vm = vmask[0] | vmask[1] | vmask[2];

            for (x = 1; vm & ~(x - 1); x <<= 4, ptr += 16, l += 4) {
                if (row || y) {
                    if (vm & x) {
                        int L = *l, H = L >> 4;
                        int E = s->filter.mblim_lut[L];
                        int I = s->filter.lim_lut[L];

                        if (vmask[0] & x) {
                            if (vmask[0] & (x << 2)) {
                                av_assert2(l[2] == L);
                                s->dsp.loop_filter_16[1](ptr, ls_uv, E, I, H);
                            } else {
                                s->dsp.loop_filter_8[2][1](ptr, ls_uv, E, I, H);
                            }
                        } else if (vm & (x << 2)) {
                            L  = l[2];
                            H |= (L >> 4) << 8;
                            E |= s->filter.mblim_lut[L] << 8;
                            I |= s->filter.lim_lut[L] << 8;
                            s->dsp.loop_filter_mix2[!!(vmask[1] &  x)]
                                                   [!!(vmask[1] & (x << 2))]
                                                   [1](ptr, ls_uv, E, I, H);
                        } else {
                            s->dsp.loop_filter_8[!!(vmask[1] & x)]
                                                [1](ptr, ls_uv, E, I, H);
                        }
                    } else if (vm & (x << 2)) {
                        int L = l[2], H = L >> 4;
                        int E = s->filter.mblim_lut[L];
                        int I = s->filter.lim_lut[L];

                        s->dsp.loop_filter_8[!!(vmask[1] & (x << 2))]
                                            [1](ptr + 8, ls_uv, E, I, H);
                    }
                }
            }
            if (y & 1)
                lvl += 16;
        }
    }
}

static void set_tile_offset(int *start, int *end, int idx, int log2_n, int n)
{
    int sb_start =  (idx      * n) >> log2_n;
    int sb_end   = ((idx + 1) * n) >> log2_n;
    *start = FFMIN(sb_start, n) << 3;
    *end   = FFMIN(sb_end,   n) << 3;
}

static int vp9_decode_frame(AVCodecContext *avctx, AVFrame *frame,
                            int *got_frame, const uint8_t *data, int size)
{
    VP9Context *s = avctx->priv_data;
    int ret, tile_row, tile_col, i, ref = -1, row, col;
    ptrdiff_t yoff = 0, uvoff = 0;

    ret = decode_frame_header(avctx, data, size, &ref);
    if (ret < 0) {
        return ret;
    } else if (!ret) {
        if (!s->refs[ref]->buf[0]) {
            av_log(avctx, AV_LOG_ERROR,
                   "Requested reference %d not available\n", ref);
            return AVERROR_INVALIDDATA;
        }

        ret = av_frame_ref(frame, s->refs[ref]);
        if (ret < 0)
            return ret;
        *got_frame = 1;
        return 0;
    }
    data += ret;
    size -= ret;

    s->cur_frame = frame;

    av_frame_unref(s->cur_frame);
    if ((ret = ff_get_buffer(avctx, s->cur_frame,
                             s->refreshrefmask ? AV_GET_BUFFER_FLAG_REF : 0)) < 0)
        return ret;
    s->cur_frame->key_frame = s->keyframe;
    s->cur_frame->pict_type = s->keyframe ? AV_PICTURE_TYPE_I
                                          : AV_PICTURE_TYPE_P;

    // main tile decode loop
    memset(s->above_partition_ctx, 0, s->cols);
    memset(s->above_skip_ctx, 0, s->cols);
    if (s->keyframe || s->intraonly)
        memset(s->above_mode_ctx, DC_PRED, s->cols * 2);
    else
        memset(s->above_mode_ctx, NEARESTMV, s->cols);
    memset(s->above_y_nnz_ctx, 0, s->sb_cols * 16);
    memset(s->above_uv_nnz_ctx[0], 0, s->sb_cols * 8);
    memset(s->above_uv_nnz_ctx[1], 0, s->sb_cols * 8);
    memset(s->above_segpred_ctx, 0, s->cols);
    for (tile_row = 0; tile_row < s->tiling.tile_rows; tile_row++) {
        set_tile_offset(&s->tiling.tile_row_start, &s->tiling.tile_row_end,
                        tile_row, s->tiling.log2_tile_rows, s->sb_rows);
        for (tile_col = 0; tile_col < s->tiling.tile_cols; tile_col++) {
            int64_t tile_size;

            if (tile_col == s->tiling.tile_cols - 1 &&
                tile_row == s->tiling.tile_rows - 1) {
                tile_size = size;
            } else {
                tile_size = AV_RB32(data);
                data     += 4;
                size     -= 4;
            }
            if (tile_size > size)
                return AVERROR_INVALIDDATA;
            ff_vp56_init_range_decoder(&s->c_b[tile_col], data, tile_size);
            if (vp56_rac_get_prob_branchy(&s->c_b[tile_col], 128)) // marker bit
                return AVERROR_INVALIDDATA;
            data += tile_size;
            size -= tile_size;
        }

        for (row = s->tiling.tile_row_start;
             row < s->tiling.tile_row_end;
             row += 8, yoff += s->cur_frame->linesize[0] * 64,
             uvoff += s->cur_frame->linesize[1] * 32) {
            VP9Filter *lflvl = s->lflvl;
            ptrdiff_t yoff2 = yoff, uvoff2 = uvoff;

            for (tile_col = 0; tile_col < s->tiling.tile_cols; tile_col++) {
                set_tile_offset(&s->tiling.tile_col_start,
                                &s->tiling.tile_col_end,
                                tile_col, s->tiling.log2_tile_cols, s->sb_cols);

                memset(s->left_partition_ctx, 0, 8);
                memset(s->left_skip_ctx, 0, 8);
                if (s->keyframe || s->intraonly)
                    memset(s->left_mode_ctx, DC_PRED, 16);
                else
                    memset(s->left_mode_ctx, NEARESTMV, 8);
                memset(s->left_y_nnz_ctx, 0, 16);
                memset(s->left_uv_nnz_ctx, 0, 16);
                memset(s->left_segpred_ctx, 0, 8);

                memcpy(&s->c, &s->c_b[tile_col], sizeof(s->c));
                for (col = s->tiling.tile_col_start;
                     col < s->tiling.tile_col_end;
                     col += 8, yoff2 += 64, uvoff2 += 32, lflvl++) {
                    // FIXME integrate with lf code (i.e. zero after each
                    // use, similar to invtxfm coefficients, or similar)
                    memset(lflvl->mask, 0, sizeof(lflvl->mask));

                    if ((ret = decode_subblock(avctx, row, col, lflvl,
                                               yoff2, uvoff2, BL_64X64)) < 0)
                        return ret;
                }
                memcpy(&s->c_b[tile_col], &s->c, sizeof(s->c));
            }

            // backup pre-loopfilter reconstruction data for intra
            // prediction of next row of sb64s
            if (row + 8 < s->rows) {
                memcpy(s->intra_pred_data[0],
                       s->cur_frame->data[0] + yoff +
                       63 * s->cur_frame->linesize[0],
                       8 * s->cols);
                memcpy(s->intra_pred_data[1],
                       s->cur_frame->data[1] + uvoff +
                       31 * s->cur_frame->linesize[1],
                       4 * s->cols);
                memcpy(s->intra_pred_data[2],
                       s->cur_frame->data[2] + uvoff +
                       31 * s->cur_frame->linesize[2],
                       4 * s->cols);
            }

            // loopfilter one row
            if (s->filter.level) {
                yoff2  = yoff;
                uvoff2 = uvoff;
                lflvl  = s->lflvl;
                for (col = 0; col < s->cols;
                     col += 8, yoff2 += 64, uvoff2 += 32, lflvl++)
                    loopfilter_subblock(avctx, lflvl, row, col, yoff2, uvoff2);
            }
        }
    }

    // bw adaptivity (or in case of parallel decoding mode, fw adaptivity
    // probability maintenance between frames)
    if (s->refreshctx) {
        if (s->parallelmode) {
            int j, k, l, m;
            for (i = 0; i < 4; i++) {
                for (j = 0; j < 2; j++)
                    for (k = 0; k < 2; k++)
                        for (l = 0; l < 6; l++)
                            for (m = 0; m < 6; m++)
                                memcpy(s->prob_ctx[s->framectxid].coef[i][j][k][l][m],
                                       s->prob.coef[i][j][k][l][m], 3);
                if (s->txfmmode == i)
                    break;
            }
            s->prob_ctx[s->framectxid].p = s->prob.p;
        } else {
            ff_vp9_adapt_probs(s);
        }
    }
    FFSWAP(VP9MVRefPair *, s->mv[0], s->mv[1]);

    // ref frame setup
    for (i = 0; i < 8; i++)
        if (s->refreshrefmask & (1 << i)) {
            av_frame_unref(s->refs[i]);
            ret = av_frame_ref(s->refs[i], s->cur_frame);
            if (ret < 0)
                return ret;
        }

    if (s->invisible)
        av_frame_unref(s->cur_frame);
    else
        *got_frame = 1;

    return 0;
}

static int vp9_decode_packet(AVCodecContext *avctx, void *frame,
                             int *got_frame, AVPacket *avpkt)
{
    const uint8_t *data = avpkt->data;
    int size            = avpkt->size;
    int marker, ret;

    /* Read superframe index - this is a collection of individual frames
     * that together lead to one visible frame */
    marker = data[size - 1];
    if ((marker & 0xe0) == 0xc0) {
        int nbytes   = 1 + ((marker >> 3) & 0x3);
        int n_frames = 1 + (marker & 0x7);
        int idx_sz   = 2 + n_frames * nbytes;

        if (size >= idx_sz && data[size - idx_sz] == marker) {
            const uint8_t *idx = data + size + 1 - idx_sz;

            while (n_frames--) {
                unsigned sz = AV_RL32(idx);

                if (nbytes < 4)
                    sz &= (1 << (8 * nbytes)) - 1;
                idx += nbytes;

                if (sz > size) {
                    av_log(avctx, AV_LOG_ERROR,
                           "Superframe packet size too big: %u > %d\n",
                           sz, size);
                    return AVERROR_INVALIDDATA;
                }

                ret = vp9_decode_frame(avctx, frame, got_frame, data, sz);
                if (ret < 0)
                    return ret;
                data += sz;
                size -= sz;
            }
            return size;
        }
    }

    /* If we get here, there was no valid superframe index, i.e. this is just
     * one whole single frame. Decode it as such from the complete input buf. */
    if ((ret = vp9_decode_frame(avctx, frame, got_frame, data, size)) < 0)
        return ret;
    return size;
}

static av_cold int vp9_decode_free(AVCodecContext *avctx)
{
    VP9Context *s = avctx->priv_data;
    int i;

    for (i = 0; i < FF_ARRAY_ELEMS(s->refs); i++)
        av_frame_free(&s->refs[i]);

    av_freep(&s->c_b);
    av_freep(&s->above_partition_ctx);

    return 0;
}

static av_cold int vp9_decode_init(AVCodecContext *avctx)
{
    VP9Context *s = avctx->priv_data;
    int i;

    avctx->pix_fmt = AV_PIX_FMT_YUV420P;

    ff_vp9dsp_init(&s->dsp);
    ff_videodsp_init(&s->vdsp, 8);

    for (i = 0; i < FF_ARRAY_ELEMS(s->refs); i++) {
        s->refs[i] = av_frame_alloc();
        if (!s->refs[i]) {
            vp9_decode_free(avctx);
            return AVERROR(ENOMEM);
        }
    }

    s->filter.sharpness = -1;

    return 0;
}

AVCodec ff_vp9_decoder = {
    .name           = "vp9",
    .long_name      = NULL_IF_CONFIG_SMALL("Google VP9"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_VP9,
    .priv_data_size = sizeof(VP9Context),
    .init           = vp9_decode_init,
    .decode         = vp9_decode_packet,
    .flush          = vp9_decode_flush,
    .close          = vp9_decode_free,
    .capabilities   = CODEC_CAP_DR1,
};
