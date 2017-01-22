/*
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
 * Common functions for Microsoft Screen 1 and 2
 */

#include <inttypes.h>

#include "libavutil/intfloat.h"
#include "libavutil/intreadwrite.h"
#include "avcodec.h"
#include "mss12.h"

enum SplitMode {
    SPLIT_VERT = 0,
    SPLIT_HOR,
    SPLIT_NONE
};

static const int sec_order_sizes[4] = { 1, 7, 6, 1 };

enum ContextDirection {
    TOP_LEFT = 0,
    TOP,
    TOP_RIGHT,
    LEFT
};

static int model_calc_threshold(Model *m)
{
    int thr;

    thr = 2 * m->weights[m->num_syms] - 1;
    thr = ((thr >> 1) + 4 * m->cum_prob[0]) / thr;

    return FFMIN(thr, 0x3FFF);
}

static void model_reset(Model *m)
{
    int i;

    for (i = 0; i <= m->num_syms; i++) {
        m->weights[i]  = 1;
        m->cum_prob[i] = m->num_syms - i;
    }
    m->weights[0] = 0;
    for (i = 0; i < m->num_syms; i++)
        m->idx2sym[i + 1] = i;
}

static av_cold void model_init(Model *m, int num_syms, int thr_weight)
{
    m->num_syms   = num_syms;
    m->thr_weight = thr_weight;
    m->threshold  = num_syms * thr_weight;
}

static void model_rescale_weights(Model *m)
{
    int i;
    int cum_prob;

    if (m->thr_weight == THRESH_ADAPTIVE)
        m->threshold = model_calc_threshold(m);
    while (m->cum_prob[0] > m->threshold) {
        cum_prob = 0;
        for (i = m->num_syms; i >= 0; i--) {
            m->cum_prob[i] = cum_prob;
            m->weights[i]  = (m->weights[i] + 1) >> 1;
            cum_prob      += m->weights[i];
        }
    }
}

void ff_mss12_model_update(Model *m, int val)
{
    int i;

    if (m->weights[val] == m->weights[val - 1]) {
        for (i = val; m->weights[i - 1] == m->weights[val]; i--);
        if (i != val) {
            int sym1, sym2;

            sym1 = m->idx2sym[val];
            sym2 = m->idx2sym[i];

            m->idx2sym[val]  = sym2;
            m->idx2sym[i]    = sym1;

            val = i;
        }
    }
    m->weights[val]++;
    for (i = val - 1; i >= 0; i--)
        m->cum_prob[i]++;
    model_rescale_weights(m);
}

static void pixctx_reset(PixContext *ctx)
{
    int i, j;

    if (!ctx->special_initial_cache)
        for (i = 0; i < ctx->cache_size; i++)
            ctx->cache[i] = i;
    else {
        ctx->cache[0] = 1;
        ctx->cache[1] = 2;
        ctx->cache[2] = 4;
    }

    model_reset(&ctx->cache_model);
    model_reset(&ctx->full_model);

    for (i = 0; i < 15; i++)
        for (j = 0; j < 4; j++)
            model_reset(&ctx->sec_models[i][j]);
}

static av_cold void pixctx_init(PixContext *ctx, int cache_size,
                                int full_model_syms, int special_initial_cache)
{
    int i, j, k, idx;

    ctx->cache_size            = cache_size + 4;
    ctx->num_syms              = cache_size;
    ctx->special_initial_cache = special_initial_cache;

    model_init(&ctx->cache_model, ctx->num_syms + 1, THRESH_LOW);
    model_init(&ctx->full_model, full_model_syms, THRESH_HIGH);

    for (i = 0, idx = 0; i < 4; i++)
        for (j = 0; j < sec_order_sizes[i]; j++, idx++)
            for (k = 0; k < 4; k++)
                model_init(&ctx->sec_models[idx][k], 2 + i,
                           i ? THRESH_LOW : THRESH_ADAPTIVE);
}

static av_always_inline int decode_pixel(ArithCoder *acoder, PixContext *pctx,
                                         uint8_t *ngb, int num_ngb, int any_ngb)
{
    int i, val, pix;

    val = acoder->get_model_sym(acoder, &pctx->cache_model);
    if (val < pctx->num_syms) {
        if (any_ngb) {
            int idx, j;

            idx = 0;
            for (i = 0; i < pctx->cache_size; i++) {
                for (j = 0; j < num_ngb; j++)
                    if (pctx->cache[i] == ngb[j])
                        break;
                if (j == num_ngb) {
                    if (idx == val)
                        break;
                    idx++;
                }
            }
            val = FFMIN(i, pctx->cache_size - 1);
        }
        pix = pctx->cache[val];
    } else {
        pix = acoder->get_model_sym(acoder, &pctx->full_model);
        for (i = 0; i < pctx->cache_size - 1; i++)
            if (pctx->cache[i] == pix)
                break;
        val = i;
    }
    if (val) {
        for (i = val; i > 0; i--)
            pctx->cache[i] = pctx->cache[i - 1];
        pctx->cache[0] = pix;
    }

    return pix;
}

static int decode_pixel_in_context(ArithCoder *acoder, PixContext *pctx,
                                   uint8_t *src, int stride, int x, int y,
                                   int has_right)
{
    uint8_t neighbours[4];
    uint8_t ref_pix[4];
    int nlen;
    int layer = 0, sub;
    int pix;
    int i, j;

    if (!y) {
        memset(neighbours, src[-1], 4);
    } else {
        neighbours[TOP] = src[-stride];
        if (!x) {
            neighbours[TOP_LEFT] = neighbours[LEFT] = neighbours[TOP];
        } else {
            neighbours[TOP_LEFT] = src[-stride - 1];
            neighbours[    LEFT] = src[-1];
        }
        if (has_right)
            neighbours[TOP_RIGHT] = src[-stride + 1];
        else
            neighbours[TOP_RIGHT] = neighbours[TOP];
    }

    sub = 0;
    if (x >= 2 && src[-2] == neighbours[LEFT])
        sub  = 1;
    if (y >= 2 && src[-2 * stride] == neighbours[TOP])
        sub |= 2;

    nlen = 1;
    ref_pix[0] = neighbours[0];
    for (i = 1; i < 4; i++) {
        for (j = 0; j < nlen; j++)
            if (ref_pix[j] == neighbours[i])
                break;
        if (j == nlen)
            ref_pix[nlen++] = neighbours[i];
    }

    switch (nlen) {
    case 1:
        layer = 0;
        break;
    case 2:
        if (neighbours[TOP] == neighbours[TOP_LEFT]) {
            if (neighbours[TOP_RIGHT] == neighbours[TOP_LEFT])
                layer = 1;
            else if (neighbours[LEFT] == neighbours[TOP_LEFT])
                layer = 2;
            else
                layer = 3;
        } else if (neighbours[TOP_RIGHT] == neighbours[TOP_LEFT]) {
            if (neighbours[LEFT] == neighbours[TOP_LEFT])
                layer = 4;
            else
                layer = 5;
        } else if (neighbours[LEFT] == neighbours[TOP_LEFT]) {
            layer = 6;
        } else {
            layer = 7;
        }
        break;
    case 3:
        if (neighbours[TOP] == neighbours[TOP_LEFT])
            layer = 8;
        else if (neighbours[TOP_RIGHT] == neighbours[TOP_LEFT])
            layer = 9;
        else if (neighbours[LEFT] == neighbours[TOP_LEFT])
            layer = 10;
        else if (neighbours[TOP_RIGHT] == neighbours[TOP])
            layer = 11;
        else if (neighbours[TOP] == neighbours[LEFT])
            layer = 12;
        else
            layer = 13;
        break;
    case 4:
        layer = 14;
        break;
    }

    pix = acoder->get_model_sym(acoder,
                                &pctx->sec_models[layer][sub]);
    if (pix < nlen)
        return ref_pix[pix];
    else
        return decode_pixel(acoder, pctx, ref_pix, nlen, 1);
}

static int decode_region(ArithCoder *acoder, uint8_t *dst, uint8_t *rgb_pic,
                         int x, int y, int width, int height, int stride,
                         int rgb_stride, PixContext *pctx, const uint32_t *pal)
{
    int i, j, p;
    uint8_t *rgb_dst = rgb_pic + x * 3 + y * rgb_stride;

    dst += x + y * stride;

    for (j = 0; j < height; j++) {
        for (i = 0; i < width; i++) {
            if (!i && !j)
                p = decode_pixel(acoder, pctx, NULL, 0, 0);
            else
                p = decode_pixel_in_context(acoder, pctx, dst + i, stride,
                                            i, j, width - i - 1);
            dst[i] = p;

            if (rgb_pic)
                AV_WB24(rgb_dst + i * 3, pal[p]);
        }
        dst     += stride;
        rgb_dst += rgb_stride;
    }

    return 0;
}

static void copy_rectangles(MSS12Context const *c,
                            int x, int y, int width, int height)
{
    int j;

    if (c->last_rgb_pic)
        for (j = y; j < y + height; j++) {
            memcpy(c->rgb_pic      + j * c->rgb_stride + x * 3,
                   c->last_rgb_pic + j * c->rgb_stride + x * 3,
                   width * 3);
            memcpy(c->pal_pic      + j * c->pal_stride + x,
                   c->last_pal_pic + j * c->pal_stride + x,
                   width);
        }
}

static int motion_compensation(MSS12Context const *c,
                               int x, int y, int width, int height)
{
    if (x + c->mvX < 0 || x + c->mvX + width  > c->avctx->width  ||
        y + c->mvY < 0 || y + c->mvY + height > c->avctx->height ||
        !c->rgb_pic)
        return -1;
    else {
        uint8_t *dst     = c->pal_pic + x     + y * c->pal_stride;
        uint8_t *rgb_dst = c->rgb_pic + x * 3 + y * c->rgb_stride;
        uint8_t *src;
        uint8_t *rgb_src;
        int j;
        x += c->mvX;
        y += c->mvY;
        if (c->last_rgb_pic) {
            src     = c->last_pal_pic + x +     y * c->pal_stride;
            rgb_src = c->last_rgb_pic + x * 3 + y * c->rgb_stride;
        } else {
            src     = c->pal_pic + x     + y * c->pal_stride;
            rgb_src = c->rgb_pic + x * 3 + y * c->rgb_stride;
        }
        for (j = 0; j < height; j++) {
            memmove(dst, src, width);
            memmove(rgb_dst, rgb_src, width * 3);
            dst     += c->pal_stride;
            src     += c->pal_stride;
            rgb_dst += c->rgb_stride;
            rgb_src += c->rgb_stride;
        }
    }
    return 0;
}

static int decode_region_masked(MSS12Context const *c, ArithCoder *acoder,
                                uint8_t *dst, int stride, uint8_t *mask,
                                int mask_stride, int x, int y,
                                int width, int height,
                                PixContext *pctx)
{
    int i, j, p;
    uint8_t *rgb_dst = c->rgb_pic + x * 3 + y * c->rgb_stride;

    dst  += x + y * stride;
    mask += x + y * mask_stride;

    for (j = 0; j < height; j++) {
        for (i = 0; i < width; i++) {
            if (c->avctx->err_recognition & AV_EF_EXPLODE &&
                ( c->rgb_pic && mask[i] != 0x01 && mask[i] != 0x02 && mask[i] != 0x04 ||
                 !c->rgb_pic && mask[i] != 0x80 && mask[i] != 0xFF))
                return -1;

            if (mask[i] == 0x02) {
                copy_rectangles(c, x + i, y + j, 1, 1);
            } else if (mask[i] == 0x04) {
                if (motion_compensation(c, x + i, y + j, 1, 1))
                    return -1;
            } else if (mask[i] != 0x80) {
                if (!i && !j)
                    p = decode_pixel(acoder, pctx, NULL, 0, 0);
                else
                    p = decode_pixel_in_context(acoder, pctx, dst + i, stride,
                                                i, j, width - i - 1);
                dst[i] = p;
                if (c->rgb_pic)
                    AV_WB24(rgb_dst + i * 3, c->pal[p]);
            }
        }
        dst     += stride;
        mask    += mask_stride;
        rgb_dst += c->rgb_stride;
    }

    return 0;
}

static av_cold void slicecontext_init(SliceContext *sc,
                                      int version, int full_model_syms)
{
    model_init(&sc->intra_region, 2, THRESH_ADAPTIVE);
    model_init(&sc->inter_region, 2, THRESH_ADAPTIVE);
    model_init(&sc->split_mode,   3, THRESH_HIGH);
    model_init(&sc->edge_mode,    2, THRESH_HIGH);
    model_init(&sc->pivot,        3, THRESH_LOW);

    pixctx_init(&sc->intra_pix_ctx, 8, full_model_syms, 0);

    pixctx_init(&sc->inter_pix_ctx, version ? 3 : 2,
                full_model_syms, version ? 1 : 0);
}

void ff_mss12_slicecontext_reset(SliceContext *sc)
{
    model_reset(&sc->intra_region);
    model_reset(&sc->inter_region);
    model_reset(&sc->split_mode);
    model_reset(&sc->edge_mode);
    model_reset(&sc->pivot);
    pixctx_reset(&sc->intra_pix_ctx);
    pixctx_reset(&sc->inter_pix_ctx);
}

static int decode_pivot(SliceContext *sc, ArithCoder *acoder, int base)
{
    int val, inv;

    inv = acoder->get_model_sym(acoder, &sc->edge_mode);
    val = acoder->get_model_sym(acoder, &sc->pivot) + 1;

    if (val > 2) {
        if ((base + 1) / 2 - 2 <= 0)
            return -1;

        val = acoder->get_number(acoder, (base + 1) / 2 - 2) + 3;
    }

    if ((unsigned)val >= base)
        return -1;

    return inv ? base - val : val;
}

static int decode_region_intra(SliceContext *sc, ArithCoder *acoder,
                               int x, int y, int width, int height)
{
    MSS12Context const *c = sc->c;
    int mode;

    mode = acoder->get_model_sym(acoder, &sc->intra_region);

    if (!mode) {
        int i, j, pix, rgb_pix;
        int stride       = c->pal_stride;
        int rgb_stride   = c->rgb_stride;
        uint8_t *dst     = c->pal_pic + x     + y * stride;
        uint8_t *rgb_dst = c->rgb_pic + x * 3 + y * rgb_stride;

        pix     = decode_pixel(acoder, &sc->intra_pix_ctx, NULL, 0, 0);
        rgb_pix = c->pal[pix];
        for (i = 0; i < height; i++, dst += stride, rgb_dst += rgb_stride) {
            memset(dst, pix, width);
            if (c->rgb_pic)
                for (j = 0; j < width * 3; j += 3)
                    AV_WB24(rgb_dst + j, rgb_pix);
        }
    } else {
        return decode_region(acoder, c->pal_pic, c->rgb_pic,
                             x, y, width, height, c->pal_stride, c->rgb_stride,
                             &sc->intra_pix_ctx, &c->pal[0]);
    }

    return 0;
}

static int decode_region_inter(SliceContext *sc, ArithCoder *acoder,
                               int x, int y, int width, int height)
{
    MSS12Context const *c = sc->c;
    int mode;

    mode = acoder->get_model_sym(acoder, &sc->inter_region);

    if (!mode) {
        mode = decode_pixel(acoder, &sc->inter_pix_ctx, NULL, 0, 0);

        if (c->avctx->err_recognition & AV_EF_EXPLODE &&
            ( c->rgb_pic && mode != 0x01 && mode != 0x02 && mode != 0x04 ||
             !c->rgb_pic && mode != 0x80 && mode != 0xFF))
            return -1;

        if (mode == 0x02)
            copy_rectangles(c, x, y, width, height);
        else if (mode == 0x04)
            return motion_compensation(c, x, y, width, height);
        else if (mode != 0x80)
            return decode_region_intra(sc, acoder, x, y, width, height);
    } else {
        if (decode_region(acoder, c->mask, NULL,
                          x, y, width, height, c->mask_stride, 0,
                          &sc->inter_pix_ctx, &c->pal[0]) < 0)
            return -1;
        return decode_region_masked(c, acoder, c->pal_pic,
                                    c->pal_stride, c->mask,
                                    c->mask_stride,
                                    x, y, width, height,
                                    &sc->intra_pix_ctx);
    }

    return 0;
}

int ff_mss12_decode_rect(SliceContext *sc, ArithCoder *acoder,
                         int x, int y, int width, int height)
{
    int mode, pivot;

    mode = acoder->get_model_sym(acoder, &sc->split_mode);

    switch (mode) {
    case SPLIT_VERT:
        if ((pivot = decode_pivot(sc, acoder, height)) < 1)
            return -1;
        if (ff_mss12_decode_rect(sc, acoder, x, y, width, pivot))
            return -1;
        if (ff_mss12_decode_rect(sc, acoder, x, y + pivot, width, height - pivot))
            return -1;
        break;
    case SPLIT_HOR:
        if ((pivot = decode_pivot(sc, acoder, width)) < 1)
            return -1;
        if (ff_mss12_decode_rect(sc, acoder, x, y, pivot, height))
            return -1;
        if (ff_mss12_decode_rect(sc, acoder, x + pivot, y, width - pivot, height))
            return -1;
        break;
    case SPLIT_NONE:
        if (sc->c->keyframe)
            return decode_region_intra(sc, acoder, x, y, width, height);
        else
            return decode_region_inter(sc, acoder, x, y, width, height);
    default:
        return -1;
    }

    return 0;
}

av_cold int ff_mss12_decode_init(MSS12Context *c, int version,
                                 SliceContext* sc1, SliceContext *sc2)
{
    AVCodecContext *avctx = c->avctx;
    int i;

    if (avctx->extradata_size < 52 + 256 * 3) {
        av_log(avctx, AV_LOG_ERROR, "Insufficient extradata size %d\n",
               avctx->extradata_size);
        return AVERROR_INVALIDDATA;
    }

    if (AV_RB32(avctx->extradata) < avctx->extradata_size) {
        av_log(avctx, AV_LOG_ERROR,
               "Insufficient extradata size: expected %"PRIu32" got %d\n",
               AV_RB32(avctx->extradata),
               avctx->extradata_size);
        return AVERROR_INVALIDDATA;
    }

    avctx->coded_width  = FFMAX(AV_RB32(avctx->extradata + 20), avctx->width);
    avctx->coded_height = FFMAX(AV_RB32(avctx->extradata + 24), avctx->height);
    if (avctx->coded_width > 4096 || avctx->coded_height > 4096) {
        av_log(avctx, AV_LOG_ERROR, "Frame dimensions %dx%d too large",
               avctx->coded_width, avctx->coded_height);
        return AVERROR_INVALIDDATA;
    }
    if (avctx->coded_width < 1 || avctx->coded_height < 1) {
        av_log(avctx, AV_LOG_ERROR, "Frame dimensions %dx%d too small",
               avctx->coded_width, avctx->coded_height);
        return AVERROR_INVALIDDATA;
    }

    av_log(avctx, AV_LOG_DEBUG, "Encoder version %"PRIu32".%"PRIu32"\n",
           AV_RB32(avctx->extradata + 4), AV_RB32(avctx->extradata + 8));
    if (version != AV_RB32(avctx->extradata + 4) > 1) {
        av_log(avctx, AV_LOG_ERROR,
               "Header version doesn't match codec tag\n");
        return -1;
    }

    c->free_colours = AV_RB32(avctx->extradata + 48);
    if ((unsigned)c->free_colours > 256) {
        av_log(avctx, AV_LOG_ERROR,
               "Incorrect number of changeable palette entries: %d\n",
               c->free_colours);
        return AVERROR_INVALIDDATA;
    }
    av_log(avctx, AV_LOG_DEBUG, "%d free colour(s)\n", c->free_colours);

    av_log(avctx, AV_LOG_DEBUG, "Display dimensions %"PRIu32"x%"PRIu32"\n",
           AV_RB32(avctx->extradata + 12), AV_RB32(avctx->extradata + 16));
    av_log(avctx, AV_LOG_DEBUG, "Coded dimensions %dx%d\n",
           avctx->coded_width, avctx->coded_height);
    av_log(avctx, AV_LOG_DEBUG, "%g frames per second\n",
           av_int2float(AV_RB32(avctx->extradata + 28)));
    av_log(avctx, AV_LOG_DEBUG, "Bitrate %"PRIu32" bps\n",
           AV_RB32(avctx->extradata + 32));
    av_log(avctx, AV_LOG_DEBUG, "Max. lead time %g ms\n",
           av_int2float(AV_RB32(avctx->extradata + 36)));
    av_log(avctx, AV_LOG_DEBUG, "Max. lag time %g ms\n",
           av_int2float(AV_RB32(avctx->extradata + 40)));
    av_log(avctx, AV_LOG_DEBUG, "Max. seek time %g ms\n",
           av_int2float(AV_RB32(avctx->extradata + 44)));

    if (version) {
        if (avctx->extradata_size < 60 + 256 * 3) {
            av_log(avctx, AV_LOG_ERROR,
                   "Insufficient extradata size %d for v2\n",
                   avctx->extradata_size);
            return AVERROR_INVALIDDATA;
        }

        c->slice_split = AV_RB32(avctx->extradata + 52);
        av_log(avctx, AV_LOG_DEBUG, "Slice split %d\n", c->slice_split);

        c->full_model_syms = AV_RB32(avctx->extradata + 56);
        if (c->full_model_syms < 2 || c->full_model_syms > 256) {
            av_log(avctx, AV_LOG_ERROR,
                   "Incorrect number of used colours %d\n",
                   c->full_model_syms);
            return AVERROR_INVALIDDATA;
        }
        av_log(avctx, AV_LOG_DEBUG, "Used colours %d\n",
               c->full_model_syms);
    } else {
        c->slice_split     = 0;
        c->full_model_syms = 256;
    }

    for (i = 0; i < 256; i++)
        c->pal[i] = 0xFFU << 24 | AV_RB24(avctx->extradata + 52 +
                            (version ? 8 : 0) + i * 3);

    c->mask_stride = FFALIGN(avctx->width, 16);
    c->mask        = av_malloc_array(c->mask_stride, avctx->height);
    if (!c->mask) {
        av_log(avctx, AV_LOG_ERROR, "Cannot allocate mask plane\n");
        return AVERROR(ENOMEM);
    }

    sc1->c = c;
    slicecontext_init(sc1, version, c->full_model_syms);
    if (c->slice_split) {
        sc2->c = c;
        slicecontext_init(sc2, version, c->full_model_syms);
    }
    c->corrupted = 1;

    return 0;
}

av_cold int ff_mss12_decode_end(MSS12Context *c)
{
    av_freep(&c->mask);

    return 0;
}
