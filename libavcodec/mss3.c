/*
 * Microsoft Screen 3 (aka Microsoft ATC Screen) decoder
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
 * Microsoft Screen 3 (aka Microsoft ATC Screen) decoder
 */

#include "avcodec.h"
#include "bytestream.h"
#include "codec_internal.h"
#include "decode.h"
#include "mathops.h"
#include "mss34dsp.h"

#define HEADER_SIZE 27

#define MODEL2_SCALE       13
#define MODEL_SCALE        15
#define MODEL256_SEC_SCALE  9

typedef struct Model2 {
    int      upd_val, till_rescale;
    unsigned zero_freq,  zero_weight;
    unsigned total_freq, total_weight;
} Model2;

typedef struct Model {
    int weights[16], freqs[16];
    int num_syms;
    int tot_weight;
    int upd_val, max_upd_val, till_rescale;
} Model;

typedef struct Model256 {
    int weights[256], freqs[256];
    int tot_weight;
    int secondary[68];
    int sec_size;
    int upd_val, max_upd_val, till_rescale;
} Model256;

#define RAC_BOTTOM 0x01000000
typedef struct RangeCoder {
    const uint8_t *src, *src_end;

    uint32_t range, low;
    int got_error;
} RangeCoder;

enum BlockType {
    FILL_BLOCK = 0,
    IMAGE_BLOCK,
    DCT_BLOCK,
    HAAR_BLOCK,
    SKIP_BLOCK
};

typedef struct BlockTypeContext {
    int      last_type;
    Model    bt_model[5];
} BlockTypeContext;

typedef struct FillBlockCoder {
    int      fill_val;
    Model    coef_model;
} FillBlockCoder;

typedef struct ImageBlockCoder {
    Model256 esc_model, vec_entry_model;
    Model    vec_size_model;
    Model    vq_model[125];
} ImageBlockCoder;

typedef struct DCTBlockCoder {
    int      *prev_dc;
    ptrdiff_t prev_dc_stride;
    int      prev_dc_height;
    int      quality;
    uint16_t qmat[64];
    Model    dc_model;
    Model2   sign_model;
    Model256 ac_model;
} DCTBlockCoder;

typedef struct HaarBlockCoder {
    int      quality, scale;
    Model256 coef_model;
    Model    coef_hi_model;
} HaarBlockCoder;

typedef struct MSS3Context {
    AVCodecContext   *avctx;
    AVFrame          *pic;

    int              got_error;
    RangeCoder       coder;
    BlockTypeContext btype[3];
    FillBlockCoder   fill_coder[3];
    ImageBlockCoder  image_coder[3];
    DCTBlockCoder    dct_coder[3];
    HaarBlockCoder   haar_coder[3];

    int              dctblock[64];
    int              hblock[16 * 16];
} MSS3Context;


static void model2_reset(Model2 *m)
{
    m->zero_weight  = 1;
    m->total_weight = 2;
    m->zero_freq    = 0x1000;
    m->total_freq   = 0x2000;
    m->upd_val      = 4;
    m->till_rescale = 4;
}

static void model2_update(Model2 *m, int bit)
{
    unsigned scale;

    if (!bit)
        m->zero_weight++;
    m->till_rescale--;
    if (m->till_rescale)
        return;

    m->total_weight += m->upd_val;
    if (m->total_weight > 0x2000) {
        m->total_weight = (m->total_weight + 1) >> 1;
        m->zero_weight  = (m->zero_weight  + 1) >> 1;
        if (m->total_weight == m->zero_weight)
            m->total_weight = m->zero_weight + 1;
    }
    m->upd_val = m->upd_val * 5 >> 2;
    if (m->upd_val > 64)
        m->upd_val = 64;
    scale = 0x80000000u / m->total_weight;
    m->zero_freq    = m->zero_weight  * scale >> 18;
    m->total_freq   = m->total_weight * scale >> 18;
    m->till_rescale = m->upd_val;
}

static void model_update(Model *m, int val)
{
    int i, sum = 0;
    unsigned scale;

    m->weights[val]++;
    m->till_rescale--;
    if (m->till_rescale)
        return;
    m->tot_weight += m->upd_val;

    if (m->tot_weight > 0x8000) {
        m->tot_weight = 0;
        for (i = 0; i < m->num_syms; i++) {
            m->weights[i]  = (m->weights[i] + 1) >> 1;
            m->tot_weight +=  m->weights[i];
        }
    }
    scale = 0x80000000u / m->tot_weight;
    for (i = 0; i < m->num_syms; i++) {
        m->freqs[i] = sum * scale >> 16;
        sum += m->weights[i];
    }

    m->upd_val = m->upd_val * 5 >> 2;
    if (m->upd_val > m->max_upd_val)
        m->upd_val = m->max_upd_val;
    m->till_rescale = m->upd_val;
}

static void model_reset(Model *m)
{
    int i;

    m->tot_weight   = 0;
    for (i = 0; i < m->num_syms - 1; i++)
        m->weights[i] = 1;
    m->weights[m->num_syms - 1] = 0;

    m->upd_val      = m->num_syms;
    m->till_rescale = 1;
    model_update(m, m->num_syms - 1);
    m->till_rescale =
    m->upd_val      = (m->num_syms + 6) >> 1;
}

static av_cold void model_init(Model *m, int num_syms)
{
    m->num_syms    = num_syms;
    m->max_upd_val = 8 * num_syms + 48;

    model_reset(m);
}

static void model256_update(Model256 *m, int val)
{
    int i, sum = 0;
    unsigned scale;
    int send, sidx = 1;

    m->weights[val]++;
    m->till_rescale--;
    if (m->till_rescale)
        return;
    m->tot_weight += m->upd_val;

    if (m->tot_weight > 0x8000) {
        m->tot_weight = 0;
        for (i = 0; i < 256; i++) {
            m->weights[i]  = (m->weights[i] + 1) >> 1;
            m->tot_weight +=  m->weights[i];
        }
    }
    scale = 0x80000000u / m->tot_weight;
    m->secondary[0] = 0;
    for (i = 0; i < 256; i++) {
        m->freqs[i] = sum * scale >> 16;
        sum += m->weights[i];
        send = m->freqs[i] >> MODEL256_SEC_SCALE;
        while (sidx <= send)
            m->secondary[sidx++] = i - 1;
    }
    while (sidx < m->sec_size)
        m->secondary[sidx++] = 255;

    m->upd_val = m->upd_val * 5 >> 2;
    if (m->upd_val > m->max_upd_val)
        m->upd_val = m->max_upd_val;
    m->till_rescale = m->upd_val;
}

static void model256_reset(Model256 *m)
{
    int i;

    for (i = 0; i < 255; i++)
        m->weights[i] = 1;
    m->weights[255] = 0;

    m->tot_weight   = 0;
    m->upd_val      = 256;
    m->till_rescale = 1;
    model256_update(m, 255);
    m->till_rescale =
    m->upd_val      = (256 + 6) >> 1;
}

static av_cold void model256_init(Model256 *m)
{
    m->max_upd_val = 8 * 256 + 48;
    m->sec_size    = (1 << 6) + 2;

    model256_reset(m);
}

static void rac_init(RangeCoder *c, const uint8_t *src, int size)
{
    int i;

    c->src       = src;
    c->src_end   = src + size;
    c->low       = 0;
    for (i = 0; i < FFMIN(size, 4); i++)
        c->low = (c->low << 8) | *c->src++;
    c->range     = 0xFFFFFFFF;
    c->got_error = 0;
}

static void rac_normalise(RangeCoder *c)
{
    for (;;) {
        c->range <<= 8;
        c->low   <<= 8;
        if (c->src < c->src_end) {
            c->low |= *c->src++;
        } else if (!c->low) {
            c->got_error = 1;
            c->low = 1;
        }
        if (c->low > c->range) {
            c->got_error = 1;
            c->low = 1;
        }
        if (c->range >= RAC_BOTTOM)
            return;
    }
}

static int rac_get_bit(RangeCoder *c)
{
    int bit;

    c->range >>= 1;

    bit = (c->range <= c->low);
    if (bit)
        c->low -= c->range;

    if (c->range < RAC_BOTTOM)
        rac_normalise(c);

    return bit;
}

static int rac_get_bits(RangeCoder *c, int nbits)
{
    int val;

    c->range >>= nbits;
    val = c->low / c->range;
    c->low -= c->range * val;

    if (c->range < RAC_BOTTOM)
        rac_normalise(c);

    return val;
}

static int rac_get_model2_sym(RangeCoder *c, Model2 *m)
{
    int bit, helper;

    helper = m->zero_freq * (c->range >> MODEL2_SCALE);
    bit    = (c->low >= helper);
    if (bit) {
        c->low   -= helper;
        c->range -= helper;
    } else {
        c->range  = helper;
    }

    if (c->range < RAC_BOTTOM)
        rac_normalise(c);

    model2_update(m, bit);

    return bit;
}

static int rac_get_model_sym(RangeCoder *c, Model *m)
{
    int val;
    int end, end2;
    unsigned prob, prob2, helper;

    prob       = 0;
    prob2      = c->range;
    c->range >>= MODEL_SCALE;
    val        = 0;
    end        = m->num_syms >> 1;
    end2       = m->num_syms;
    do {
        helper = m->freqs[end] * c->range;
        if (helper <= c->low) {
            val   = end;
            prob  = helper;
        } else {
            end2  = end;
            prob2 = helper;
        }
        end = (end2 + val) >> 1;
    } while (end != val);
    c->low  -= prob;
    c->range = prob2 - prob;
    if (c->range < RAC_BOTTOM)
        rac_normalise(c);

    model_update(m, val);

    return val;
}

static int rac_get_model256_sym(RangeCoder *c, Model256 *m)
{
    int val;
    int start, end;
    int ssym;
    unsigned prob, prob2, helper;

    prob2      = c->range;
    c->range >>= MODEL_SCALE;

    helper     = c->low / c->range;
    ssym       = helper >> MODEL256_SEC_SCALE;
    val        = m->secondary[ssym];

    end = start = m->secondary[ssym + 1] + 1;
    while (end > val + 1) {
        ssym = (end + val) >> 1;
        if (m->freqs[ssym] <= helper) {
            end = start;
            val = ssym;
        } else {
            end   = (end + val) >> 1;
            start = ssym;
        }
    }
    prob = m->freqs[val] * c->range;
    if (val != 255)
        prob2 = m->freqs[val + 1] * c->range;

    c->low  -= prob;
    c->range = prob2 - prob;
    if (c->range < RAC_BOTTOM)
        rac_normalise(c);

    model256_update(m, val);

    return val;
}

static int decode_block_type(RangeCoder *c, BlockTypeContext *bt)
{
    bt->last_type = rac_get_model_sym(c, &bt->bt_model[bt->last_type]);

    return bt->last_type;
}

static int decode_coeff(RangeCoder *c, Model *m)
{
    int val, sign;

    val = rac_get_model_sym(c, m);
    if (val) {
        sign = rac_get_bit(c);
        if (val > 1) {
            val--;
            val = (1 << val) + rac_get_bits(c, val);
        }
        if (!sign)
            val = -val;
    }

    return val;
}

static void decode_fill_block(RangeCoder *c, FillBlockCoder *fc,
                              uint8_t *dst, ptrdiff_t stride, int block_size)
{
    int i;

    fc->fill_val += decode_coeff(c, &fc->coef_model);

    for (i = 0; i < block_size; i++, dst += stride)
        memset(dst, fc->fill_val, block_size);
}

static void decode_image_block(RangeCoder *c, ImageBlockCoder *ic,
                               uint8_t *dst, ptrdiff_t stride, int block_size)
{
    int i, j;
    int vec_size;
    int vec[4];
    int prev_line[16];
    int A, B, C;

    vec_size = rac_get_model_sym(c, &ic->vec_size_model) + 2;
    for (i = 0; i < vec_size; i++)
        vec[i] = rac_get_model256_sym(c, &ic->vec_entry_model);
    for (; i < 4; i++)
        vec[i] = 0;
    memset(prev_line, 0, sizeof(prev_line));

    for (j = 0; j < block_size; j++) {
        A = 0;
        B = 0;
        for (i = 0; i < block_size; i++) {
            C = B;
            B = prev_line[i];
            A = rac_get_model_sym(c, &ic->vq_model[A + B * 5 + C * 25]);

            prev_line[i] = A;
            if (A < 4)
               dst[i] = vec[A];
            else
               dst[i] = rac_get_model256_sym(c, &ic->esc_model);
        }
        dst += stride;
    }
}

static int decode_dct(RangeCoder *c, DCTBlockCoder *bc, int *block,
                      int bx, int by)
{
    int skip, val, sign, pos = 1, zz_pos, dc;
    int blk_pos = bx + by * bc->prev_dc_stride;

    memset(block, 0, sizeof(*block) * 64);

    dc = decode_coeff(c, &bc->dc_model);
    if (by) {
        if (bx) {
            int l, tl, t;

            l  = bc->prev_dc[blk_pos - 1];
            tl = bc->prev_dc[blk_pos - 1 - bc->prev_dc_stride];
            t  = bc->prev_dc[blk_pos     - bc->prev_dc_stride];

            if (FFABS(t - tl) <= FFABS(l - tl))
                dc += l;
            else
                dc += t;
        } else {
            dc += bc->prev_dc[blk_pos - bc->prev_dc_stride];
        }
    } else if (bx) {
        dc += bc->prev_dc[bx - 1];
    }
    bc->prev_dc[blk_pos] = dc;
    block[0]             = dc * bc->qmat[0];

    while (pos < 64) {
        val = rac_get_model256_sym(c, &bc->ac_model);
        if (!val)
            return 0;
        if (val == 0xF0) {
            pos += 16;
            continue;
        }
        skip = val >> 4;
        val  = val & 0xF;
        if (!val)
            return -1;
        pos += skip;
        if (pos >= 64)
            return -1;

        sign = rac_get_model2_sym(c, &bc->sign_model);
        if (val > 1) {
            val--;
            val = (1 << val) + rac_get_bits(c, val);
        }
        if (!sign)
            val = -val;

        zz_pos = ff_zigzag_direct[pos];
        block[zz_pos] = val * bc->qmat[zz_pos];
        pos++;
    }

    return pos == 64 ? 0 : -1;
}

static void decode_dct_block(RangeCoder *c, DCTBlockCoder *bc,
                             uint8_t *dst, ptrdiff_t stride, int block_size,
                             int *block, int mb_x, int mb_y)
{
    int i, j;
    int bx, by;
    int nblocks = block_size >> 3;

    bx = mb_x * nblocks;
    by = mb_y * nblocks;

    for (j = 0; j < nblocks; j++) {
        for (i = 0; i < nblocks; i++) {
            if (decode_dct(c, bc, block, bx + i, by + j)) {
                c->got_error = 1;
                return;
            }
            ff_mss34_dct_put(dst + i * 8, stride, block);
        }
        dst += 8 * stride;
    }
}

static void decode_haar_block(RangeCoder *c, HaarBlockCoder *hc,
                              uint8_t *dst, ptrdiff_t stride,
                              int block_size, int *block)
{
    const int hsize = block_size >> 1;
    int A, B, C, D, t1, t2, t3, t4;
    int i, j;

    for (j = 0; j < block_size; j++) {
        for (i = 0; i < block_size; i++) {
            if (i < hsize && j < hsize)
                block[i] = rac_get_model256_sym(c, &hc->coef_model);
            else
                block[i] = decode_coeff(c, &hc->coef_hi_model);
            block[i] *= hc->scale;
        }
        block += block_size;
    }
    block -= block_size * block_size;

    for (j = 0; j < hsize; j++) {
        for (i = 0; i < hsize; i++) {
            A = block[i];
            B = block[i + hsize];
            C = block[i + hsize * block_size];
            D = block[i + hsize * block_size + hsize];

            t1 = A - B;
            t2 = C - D;
            t3 = A + B;
            t4 = C + D;
            dst[i * 2]              = av_clip_uint8(t1 - t2);
            dst[i * 2 + stride]     = av_clip_uint8(t1 + t2);
            dst[i * 2 + 1]          = av_clip_uint8(t3 - t4);
            dst[i * 2 + 1 + stride] = av_clip_uint8(t3 + t4);
        }
        block += block_size;
        dst   += stride * 2;
    }
}

static void reset_coders(MSS3Context *ctx, int quality)
{
    int i, j;

    for (i = 0; i < 3; i++) {
        ctx->btype[i].last_type = SKIP_BLOCK;
        for (j = 0; j < 5; j++)
            model_reset(&ctx->btype[i].bt_model[j]);
        ctx->fill_coder[i].fill_val = 0;
        model_reset(&ctx->fill_coder[i].coef_model);
        model256_reset(&ctx->image_coder[i].esc_model);
        model256_reset(&ctx->image_coder[i].vec_entry_model);
        model_reset(&ctx->image_coder[i].vec_size_model);
        for (j = 0; j < 125; j++)
            model_reset(&ctx->image_coder[i].vq_model[j]);
        if (ctx->dct_coder[i].quality != quality) {
            ctx->dct_coder[i].quality = quality;
            ff_mss34_gen_quant_mat(ctx->dct_coder[i].qmat, quality, !i);
        }
        memset(ctx->dct_coder[i].prev_dc, 0,
               sizeof(*ctx->dct_coder[i].prev_dc) *
               ctx->dct_coder[i].prev_dc_stride *
               ctx->dct_coder[i].prev_dc_height);
        model_reset(&ctx->dct_coder[i].dc_model);
        model2_reset(&ctx->dct_coder[i].sign_model);
        model256_reset(&ctx->dct_coder[i].ac_model);
        if (ctx->haar_coder[i].quality != quality) {
            ctx->haar_coder[i].quality = quality;
            ctx->haar_coder[i].scale   = 17 - 7 * quality / 50;
        }
        model_reset(&ctx->haar_coder[i].coef_hi_model);
        model256_reset(&ctx->haar_coder[i].coef_model);
    }
}

static av_cold void init_coders(MSS3Context *ctx)
{
    int i, j;

    for (i = 0; i < 3; i++) {
        for (j = 0; j < 5; j++)
            model_init(&ctx->btype[i].bt_model[j], 5);
        model_init(&ctx->fill_coder[i].coef_model, 12);
        model256_init(&ctx->image_coder[i].esc_model);
        model256_init(&ctx->image_coder[i].vec_entry_model);
        model_init(&ctx->image_coder[i].vec_size_model, 3);
        for (j = 0; j < 125; j++)
            model_init(&ctx->image_coder[i].vq_model[j], 5);
        model_init(&ctx->dct_coder[i].dc_model, 12);
        model256_init(&ctx->dct_coder[i].ac_model);
        model_init(&ctx->haar_coder[i].coef_hi_model, 12);
        model256_init(&ctx->haar_coder[i].coef_model);
    }
}

static int mss3_decode_frame(AVCodecContext *avctx, AVFrame *rframe,
                             int *got_frame, AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    MSS3Context *c = avctx->priv_data;
    RangeCoder *acoder = &c->coder;
    GetByteContext gb;
    uint8_t *dst[3];
    int dec_width, dec_height, dec_x, dec_y, quality, keyframe;
    int x, y, i, mb_width, mb_height, blk_size, btype;
    int ret;

    if (buf_size < HEADER_SIZE) {
        av_log(avctx, AV_LOG_ERROR,
               "Frame should have at least %d bytes, got %d instead\n",
               HEADER_SIZE, buf_size);
        return AVERROR_INVALIDDATA;
    }

    bytestream2_init(&gb, buf, buf_size);
    keyframe   = bytestream2_get_be32(&gb);
    if (keyframe & ~0x301) {
        av_log(avctx, AV_LOG_ERROR, "Invalid frame type %X\n", keyframe);
        return AVERROR_INVALIDDATA;
    }
    keyframe   = !(keyframe & 1);
    bytestream2_skip(&gb, 6);
    dec_x      = bytestream2_get_be16(&gb);
    dec_y      = bytestream2_get_be16(&gb);
    dec_width  = bytestream2_get_be16(&gb);
    dec_height = bytestream2_get_be16(&gb);

    if (dec_x + dec_width > avctx->width ||
        dec_y + dec_height > avctx->height ||
        (dec_width | dec_height) & 0xF) {
        av_log(avctx, AV_LOG_ERROR, "Invalid frame dimensions %dx%d +%d,%d\n",
               dec_width, dec_height, dec_x, dec_y);
        return AVERROR_INVALIDDATA;
    }
    bytestream2_skip(&gb, 4);
    quality    = bytestream2_get_byte(&gb);
    if (quality < 1 || quality > 100) {
        av_log(avctx, AV_LOG_ERROR, "Invalid quality setting %d\n", quality);
        return AVERROR_INVALIDDATA;
    }
    bytestream2_skip(&gb, 4);

    if (keyframe && !bytestream2_get_bytes_left(&gb)) {
        av_log(avctx, AV_LOG_ERROR, "Keyframe without data found\n");
        return AVERROR_INVALIDDATA;
    }
    if (!keyframe && c->got_error)
        return buf_size;
    c->got_error = 0;

    if ((ret = ff_reget_buffer(avctx, c->pic, 0)) < 0)
        return ret;
    c->pic->key_frame = keyframe;
    c->pic->pict_type = keyframe ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_P;
    if (!bytestream2_get_bytes_left(&gb)) {
        if ((ret = av_frame_ref(rframe, c->pic)) < 0)
            return ret;
        *got_frame      = 1;

        return buf_size;
    }

    reset_coders(c, quality);

    rac_init(acoder, buf + HEADER_SIZE, buf_size - HEADER_SIZE);

    mb_width  = dec_width  >> 4;
    mb_height = dec_height >> 4;
    dst[0] = c->pic->data[0] + dec_x     +  dec_y      * c->pic->linesize[0];
    dst[1] = c->pic->data[1] + dec_x / 2 + (dec_y / 2) * c->pic->linesize[1];
    dst[2] = c->pic->data[2] + dec_x / 2 + (dec_y / 2) * c->pic->linesize[2];
    for (y = 0; y < mb_height; y++) {
        for (x = 0; x < mb_width; x++) {
            for (i = 0; i < 3; i++) {
                blk_size = 8 << !i;

                btype = decode_block_type(acoder, c->btype + i);
                switch (btype) {
                case FILL_BLOCK:
                    decode_fill_block(acoder, c->fill_coder + i,
                                      dst[i] + x * blk_size,
                                      c->pic->linesize[i], blk_size);
                    break;
                case IMAGE_BLOCK:
                    decode_image_block(acoder, c->image_coder + i,
                                       dst[i] + x * blk_size,
                                       c->pic->linesize[i], blk_size);
                    break;
                case DCT_BLOCK:
                    decode_dct_block(acoder, c->dct_coder + i,
                                     dst[i] + x * blk_size,
                                     c->pic->linesize[i], blk_size,
                                     c->dctblock, x, y);
                    break;
                case HAAR_BLOCK:
                    decode_haar_block(acoder, c->haar_coder + i,
                                      dst[i] + x * blk_size,
                                      c->pic->linesize[i], blk_size,
                                      c->hblock);
                    break;
                }
                if (c->got_error || acoder->got_error) {
                    av_log(avctx, AV_LOG_ERROR, "Error decoding block %d,%d\n",
                           x, y);
                    c->got_error = 1;
                    return AVERROR_INVALIDDATA;
                }
            }
        }
        dst[0] += c->pic->linesize[0] * 16;
        dst[1] += c->pic->linesize[1] * 8;
        dst[2] += c->pic->linesize[2] * 8;
    }

    if ((ret = av_frame_ref(rframe, c->pic)) < 0)
        return ret;

    *got_frame      = 1;

    return buf_size;
}

static av_cold int mss3_decode_end(AVCodecContext *avctx)
{
    MSS3Context * const c = avctx->priv_data;
    int i;

    av_frame_free(&c->pic);
    for (i = 0; i < 3; i++)
        av_freep(&c->dct_coder[i].prev_dc);

    return 0;
}

static av_cold int mss3_decode_init(AVCodecContext *avctx)
{
    MSS3Context * const c = avctx->priv_data;
    int i;

    c->avctx = avctx;

    if ((avctx->width & 0xF) || (avctx->height & 0xF)) {
        av_log(avctx, AV_LOG_ERROR,
               "Image dimensions should be a multiple of 16.\n");
        return AVERROR_INVALIDDATA;
    }

    c->got_error = 0;
    for (i = 0; i < 3; i++) {
        int b_width  = avctx->width  >> (2 + !!i);
        int b_height = avctx->height >> (2 + !!i);
        c->dct_coder[i].prev_dc_stride = b_width;
        c->dct_coder[i].prev_dc_height = b_height;
        c->dct_coder[i].prev_dc = av_malloc(sizeof(*c->dct_coder[i].prev_dc) *
                                            b_width * b_height);
        if (!c->dct_coder[i].prev_dc) {
            av_log(avctx, AV_LOG_ERROR, "Cannot allocate buffer\n");
            return AVERROR(ENOMEM);
        }
    }

    c->pic = av_frame_alloc();
    if (!c->pic)
        return AVERROR(ENOMEM);

    avctx->pix_fmt     = AV_PIX_FMT_YUV420P;

    init_coders(c);

    return 0;
}

const FFCodec ff_msa1_decoder = {
    .p.name         = "msa1",
    CODEC_LONG_NAME("MS ATC Screen"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_MSA1,
    .priv_data_size = sizeof(MSS3Context),
    .init           = mss3_decode_init,
    .close          = mss3_decode_end,
    FF_CODEC_DECODE_CB(mss3_decode_frame),
    .p.capabilities = AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
};
