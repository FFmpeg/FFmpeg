/*
 * Smacker decoder
 * Copyright (c) 2006 Konstantin Shishkov
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
 * Smacker decoder
 */

/*
 * Based on http://wiki.multimedia.cx/index.php?title=Smacker
 */

#include <stdio.h>
#include <stdlib.h>

#include "libavutil/channel_layout.h"
#include "avcodec.h"
#include "internal.h"
#include "mathops.h"

#define BITSTREAM_READER_LE
#include "get_bits.h"
#include "bytestream.h"

#define SMKTREE_BITS 9
#define SMK_NODE 0x80000000

/*
 * Decoder context
 */
typedef struct SmackVContext {
    AVCodecContext *avctx;
    AVFrame pic;

    int *mmap_tbl, *mclr_tbl, *full_tbl, *type_tbl;
    int mmap_last[3], mclr_last[3], full_last[3], type_last[3];
} SmackVContext;

/**
 * Context used for code reconstructing
 */
typedef struct HuffContext {
    int length;
    int maxlength;
    int current;
    uint32_t *bits;
    int *lengths;
    int *values;
} HuffContext;

/* common parameters used for decode_bigtree */
typedef struct DBCtx {
    VLC *v1, *v2;
    int *recode1, *recode2;
    int escapes[3];
    int *last;
    int lcur;
} DBCtx;

/* possible runs of blocks */
static const int block_runs[64] = {
      1,    2,    3,    4,    5,    6,    7,    8,
      9,   10,   11,   12,   13,   14,   15,   16,
     17,   18,   19,   20,   21,   22,   23,   24,
     25,   26,   27,   28,   29,   30,   31,   32,
     33,   34,   35,   36,   37,   38,   39,   40,
     41,   42,   43,   44,   45,   46,   47,   48,
     49,   50,   51,   52,   53,   54,   55,   56,
     57,   58,   59,  128,  256,  512, 1024, 2048 };

enum SmkBlockTypes {
    SMK_BLK_MONO = 0,
    SMK_BLK_FULL = 1,
    SMK_BLK_SKIP = 2,
    SMK_BLK_FILL = 3 };

/**
 * Decode local frame tree
 */
static int smacker_decode_tree(GetBitContext *gb, HuffContext *hc, uint32_t prefix, int length)
{
    if(length > 32 || length > 3*SMKTREE_BITS) {
        av_log(NULL, AV_LOG_ERROR, "length too long\n");
        return AVERROR_INVALIDDATA;
    }
    if(!get_bits1(gb)){ //Leaf
        if(hc->current >= 256){
            av_log(NULL, AV_LOG_ERROR, "Tree size exceeded!\n");
            return AVERROR_INVALIDDATA;
        }
        if(length){
            hc->bits[hc->current] = prefix;
            hc->lengths[hc->current] = length;
        } else {
            hc->bits[hc->current] = 0;
            hc->lengths[hc->current] = 0;
        }
        hc->values[hc->current] = get_bits(gb, 8);
        hc->current++;
        if(hc->maxlength < length)
            hc->maxlength = length;
        return 0;
    } else { //Node
        int r;
        length++;
        r = smacker_decode_tree(gb, hc, prefix, length);
        if(r)
            return r;
        return smacker_decode_tree(gb, hc, prefix | (1 << (length - 1)), length);
    }
}

/**
 * Decode header tree
 */
static int smacker_decode_bigtree(GetBitContext *gb, HuffContext *hc, DBCtx *ctx)
{
    if (hc->current + 1 >= hc->length) {
        av_log(NULL, AV_LOG_ERROR, "Tree size exceeded!\n");
        return AVERROR_INVALIDDATA;
    }
    if(!get_bits1(gb)){ //Leaf
        int val, i1, i2;
        i1 = ctx->v1->table ? get_vlc2(gb, ctx->v1->table, SMKTREE_BITS, 3) : 0;
        i2 = ctx->v2->table ? get_vlc2(gb, ctx->v2->table, SMKTREE_BITS, 3) : 0;
        if (i1 < 0 || i2 < 0)
            return AVERROR_INVALIDDATA;
        val = ctx->recode1[i1] | (ctx->recode2[i2] << 8);
        if(val == ctx->escapes[0]) {
            ctx->last[0] = hc->current;
            val = 0;
        } else if(val == ctx->escapes[1]) {
            ctx->last[1] = hc->current;
            val = 0;
        } else if(val == ctx->escapes[2]) {
            ctx->last[2] = hc->current;
            val = 0;
        }

        hc->values[hc->current++] = val;
        return 1;
    } else { //Node
        int r = 0, r_new, t;

        t = hc->current++;
        r = smacker_decode_bigtree(gb, hc, ctx);
        if(r < 0)
            return r;
        hc->values[t] = SMK_NODE | r;
        r++;
        r_new = smacker_decode_bigtree(gb, hc, ctx);
        if (r_new < 0)
            return r_new;
        return r + r_new;
    }
}

/**
 * Store large tree as FFmpeg's vlc codes
 */
static int smacker_decode_header_tree(SmackVContext *smk, GetBitContext *gb, int **recodes, int *last, int size)
{
    int res;
    HuffContext huff;
    HuffContext tmp1, tmp2;
    VLC vlc[2] = { { 0 } };
    int escapes[3];
    DBCtx ctx;
    int err = 0;

    if(size >= UINT_MAX>>4){ // (((size + 3) >> 2) + 3) << 2 must not overflow
        av_log(smk->avctx, AV_LOG_ERROR, "size too large\n");
        return AVERROR_INVALIDDATA;
    }

    tmp1.length = 256;
    tmp1.maxlength = 0;
    tmp1.current = 0;
    tmp1.bits = av_mallocz(256 * 4);
    tmp1.lengths = av_mallocz(256 * sizeof(int));
    tmp1.values = av_mallocz(256 * sizeof(int));

    tmp2.length = 256;
    tmp2.maxlength = 0;
    tmp2.current = 0;
    tmp2.bits = av_mallocz(256 * 4);
    tmp2.lengths = av_mallocz(256 * sizeof(int));
    tmp2.values = av_mallocz(256 * sizeof(int));

    if(get_bits1(gb)) {
        res = smacker_decode_tree(gb, &tmp1, 0, 0);
        if (res < 0)
            return res;
        skip_bits1(gb);
        if(tmp1.current > 1) {
            res = init_vlc(&vlc[0], SMKTREE_BITS, tmp1.length,
                        tmp1.lengths, sizeof(int), sizeof(int),
                        tmp1.bits, sizeof(uint32_t), sizeof(uint32_t), INIT_VLC_LE);
            if(res < 0) {
                av_log(smk->avctx, AV_LOG_ERROR, "Cannot build VLC table\n");
                return AVERROR_INVALIDDATA;
            }
        }
    }
    if (!vlc[0].table) {
        av_log(smk->avctx, AV_LOG_ERROR, "Skipping low bytes tree\n");
    }
    if(get_bits1(gb)){
        res = smacker_decode_tree(gb, &tmp2, 0, 0);
        if (res < 0)
            return res;
        skip_bits1(gb);
        if(tmp2.current > 1) {
            res = init_vlc(&vlc[1], SMKTREE_BITS, tmp2.length,
                        tmp2.lengths, sizeof(int), sizeof(int),
                        tmp2.bits, sizeof(uint32_t), sizeof(uint32_t), INIT_VLC_LE);
            if(res < 0) {
                av_log(smk->avctx, AV_LOG_ERROR, "Cannot build VLC table\n");
                return AVERROR_INVALIDDATA;
            }
        }
    }
    if (!vlc[1].table) {
        av_log(smk->avctx, AV_LOG_ERROR, "Skipping high bytes tree\n");
    }

    escapes[0]  = get_bits(gb, 16);
    escapes[1]  = get_bits(gb, 16);
    escapes[2]  = get_bits(gb, 16);

    last[0] = last[1] = last[2] = -1;

    ctx.escapes[0] = escapes[0];
    ctx.escapes[1] = escapes[1];
    ctx.escapes[2] = escapes[2];
    ctx.v1 = &vlc[0];
    ctx.v2 = &vlc[1];
    ctx.recode1 = tmp1.values;
    ctx.recode2 = tmp2.values;
    ctx.last = last;

    huff.length = ((size + 3) >> 2) + 3;
    huff.maxlength = 0;
    huff.current = 0;
    huff.values = av_mallocz(huff.length * sizeof(int));

    if (smacker_decode_bigtree(gb, &huff, &ctx) < 0)
        err = -1;
    skip_bits1(gb);
    if(ctx.last[0] == -1) ctx.last[0] = huff.current++;
    if(ctx.last[1] == -1) ctx.last[1] = huff.current++;
    if(ctx.last[2] == -1) ctx.last[2] = huff.current++;
    if(huff.current > huff.length){
        ctx.last[0] = ctx.last[1] = ctx.last[2] = 1;
        av_log(smk->avctx, AV_LOG_ERROR, "bigtree damaged\n");
        return AVERROR_INVALIDDATA;
    }

    *recodes = huff.values;

    if(vlc[0].table)
        ff_free_vlc(&vlc[0]);
    if(vlc[1].table)
        ff_free_vlc(&vlc[1]);
    av_free(tmp1.bits);
    av_free(tmp1.lengths);
    av_free(tmp1.values);
    av_free(tmp2.bits);
    av_free(tmp2.lengths);
    av_free(tmp2.values);

    return err;
}

static int decode_header_trees(SmackVContext *smk) {
    GetBitContext gb;
    int mmap_size, mclr_size, full_size, type_size;

    mmap_size = AV_RL32(smk->avctx->extradata);
    mclr_size = AV_RL32(smk->avctx->extradata + 4);
    full_size = AV_RL32(smk->avctx->extradata + 8);
    type_size = AV_RL32(smk->avctx->extradata + 12);

    init_get_bits(&gb, smk->avctx->extradata + 16, (smk->avctx->extradata_size - 16) * 8);

    if(!get_bits1(&gb)) {
        av_log(smk->avctx, AV_LOG_INFO, "Skipping MMAP tree\n");
        smk->mmap_tbl = av_malloc(sizeof(int) * 2);
        smk->mmap_tbl[0] = 0;
        smk->mmap_last[0] = smk->mmap_last[1] = smk->mmap_last[2] = 1;
    } else {
        if (smacker_decode_header_tree(smk, &gb, &smk->mmap_tbl, smk->mmap_last, mmap_size))
            return AVERROR_INVALIDDATA;
    }
    if(!get_bits1(&gb)) {
        av_log(smk->avctx, AV_LOG_INFO, "Skipping MCLR tree\n");
        smk->mclr_tbl = av_malloc(sizeof(int) * 2);
        smk->mclr_tbl[0] = 0;
        smk->mclr_last[0] = smk->mclr_last[1] = smk->mclr_last[2] = 1;
    } else {
        if (smacker_decode_header_tree(smk, &gb, &smk->mclr_tbl, smk->mclr_last, mclr_size))
            return AVERROR_INVALIDDATA;
    }
    if(!get_bits1(&gb)) {
        av_log(smk->avctx, AV_LOG_INFO, "Skipping FULL tree\n");
        smk->full_tbl = av_malloc(sizeof(int) * 2);
        smk->full_tbl[0] = 0;
        smk->full_last[0] = smk->full_last[1] = smk->full_last[2] = 1;
    } else {
        if (smacker_decode_header_tree(smk, &gb, &smk->full_tbl, smk->full_last, full_size))
            return AVERROR_INVALIDDATA;
    }
    if(!get_bits1(&gb)) {
        av_log(smk->avctx, AV_LOG_INFO, "Skipping TYPE tree\n");
        smk->type_tbl = av_malloc(sizeof(int) * 2);
        smk->type_tbl[0] = 0;
        smk->type_last[0] = smk->type_last[1] = smk->type_last[2] = 1;
    } else {
        if (smacker_decode_header_tree(smk, &gb, &smk->type_tbl, smk->type_last, type_size))
            return AVERROR_INVALIDDATA;
    }

    return 0;
}

static av_always_inline void last_reset(int *recode, int *last) {
    recode[last[0]] = recode[last[1]] = recode[last[2]] = 0;
}

/* get code and update history */
static av_always_inline int smk_get_code(GetBitContext *gb, int *recode, int *last) {
    register int *table = recode;
    int v;

    while(*table & SMK_NODE) {
        if(get_bits1(gb))
            table += (*table) & (~SMK_NODE);
        table++;
    }
    v = *table;

    if(v != recode[last[0]]) {
        recode[last[2]] = recode[last[1]];
        recode[last[1]] = recode[last[0]];
        recode[last[0]] = v;
    }
    return v;
}

static int decode_frame(AVCodecContext *avctx, void *data, int *got_frame,
                        AVPacket *avpkt)
{
    SmackVContext * const smk = avctx->priv_data;
    uint8_t *out;
    uint32_t *pal;
    GetByteContext gb2;
    GetBitContext gb;
    int blocks, blk, bw, bh;
    int i, ret;
    int stride;
    int flags;

    if (avpkt->size <= 769)
        return AVERROR_INVALIDDATA;

    smk->pic.reference = 3;
    smk->pic.buffer_hints = FF_BUFFER_HINTS_VALID | FF_BUFFER_HINTS_PRESERVE | FF_BUFFER_HINTS_REUSABLE;
    if((ret = avctx->reget_buffer(avctx, &smk->pic)) < 0){
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return ret;
    }

    /* make the palette available on the way out */
    pal = (uint32_t*)smk->pic.data[1];
    bytestream2_init(&gb2, avpkt->data, avpkt->size);
    flags = bytestream2_get_byteu(&gb2);
    smk->pic.palette_has_changed = flags & 1;
    smk->pic.key_frame = !!(flags & 2);
    if(smk->pic.key_frame)
        smk->pic.pict_type = AV_PICTURE_TYPE_I;
    else
        smk->pic.pict_type = AV_PICTURE_TYPE_P;

    for(i = 0; i < 256; i++)
        *pal++ = 0xFFU << 24 | bytestream2_get_be24u(&gb2);

    last_reset(smk->mmap_tbl, smk->mmap_last);
    last_reset(smk->mclr_tbl, smk->mclr_last);
    last_reset(smk->full_tbl, smk->full_last);
    last_reset(smk->type_tbl, smk->type_last);
    init_get_bits(&gb, avpkt->data + 769, (avpkt->size - 769) * 8);

    blk = 0;
    bw = avctx->width >> 2;
    bh = avctx->height >> 2;
    blocks = bw * bh;
    out = smk->pic.data[0];
    stride = smk->pic.linesize[0];
    while(blk < blocks) {
        int type, run, mode;
        uint16_t pix;

        type = smk_get_code(&gb, smk->type_tbl, smk->type_last);
        run = block_runs[(type >> 2) & 0x3F];
        switch(type & 3){
        case SMK_BLK_MONO:
            while(run-- && blk < blocks){
                int clr, map;
                int hi, lo;
                clr = smk_get_code(&gb, smk->mclr_tbl, smk->mclr_last);
                map = smk_get_code(&gb, smk->mmap_tbl, smk->mmap_last);
                out = smk->pic.data[0] + (blk / bw) * (stride * 4) + (blk % bw) * 4;
                hi = clr >> 8;
                lo = clr & 0xFF;
                for(i = 0; i < 4; i++) {
                    if(map & 1) out[0] = hi; else out[0] = lo;
                    if(map & 2) out[1] = hi; else out[1] = lo;
                    if(map & 4) out[2] = hi; else out[2] = lo;
                    if(map & 8) out[3] = hi; else out[3] = lo;
                    map >>= 4;
                    out += stride;
                }
                blk++;
            }
            break;
        case SMK_BLK_FULL:
            mode = 0;
            if(avctx->codec_tag == MKTAG('S', 'M', 'K', '4')) { // In case of Smacker v4 we have three modes
                if(get_bits1(&gb)) mode = 1;
                else if(get_bits1(&gb)) mode = 2;
            }
            while(run-- && blk < blocks){
                out = smk->pic.data[0] + (blk / bw) * (stride * 4) + (blk % bw) * 4;
                switch(mode){
                case 0:
                    for(i = 0; i < 4; i++) {
                        pix = smk_get_code(&gb, smk->full_tbl, smk->full_last);
                        AV_WL16(out+2,pix);
                        pix = smk_get_code(&gb, smk->full_tbl, smk->full_last);
                        AV_WL16(out,pix);
                        out += stride;
                    }
                    break;
                case 1:
                    pix = smk_get_code(&gb, smk->full_tbl, smk->full_last);
                    out[0] = out[1] = pix & 0xFF;
                    out[2] = out[3] = pix >> 8;
                    out += stride;
                    out[0] = out[1] = pix & 0xFF;
                    out[2] = out[3] = pix >> 8;
                    out += stride;
                    pix = smk_get_code(&gb, smk->full_tbl, smk->full_last);
                    out[0] = out[1] = pix & 0xFF;
                    out[2] = out[3] = pix >> 8;
                    out += stride;
                    out[0] = out[1] = pix & 0xFF;
                    out[2] = out[3] = pix >> 8;
                    out += stride;
                    break;
                case 2:
                    for(i = 0; i < 2; i++) {
                        uint16_t pix1, pix2;
                        pix2 = smk_get_code(&gb, smk->full_tbl, smk->full_last);
                        pix1 = smk_get_code(&gb, smk->full_tbl, smk->full_last);
                        AV_WL16(out,pix1);
                        AV_WL16(out+2,pix2);
                        out += stride;
                        AV_WL16(out,pix1);
                        AV_WL16(out+2,pix2);
                        out += stride;
                    }
                    break;
                }
                blk++;
            }
            break;
        case SMK_BLK_SKIP:
            while(run-- && blk < blocks)
                blk++;
            break;
        case SMK_BLK_FILL:
            mode = type >> 8;
            while(run-- && blk < blocks){
                uint32_t col;
                out = smk->pic.data[0] + (blk / bw) * (stride * 4) + (blk % bw) * 4;
                col = mode * 0x01010101;
                for(i = 0; i < 4; i++) {
                    *((uint32_t*)out) = col;
                    out += stride;
                }
                blk++;
            }
            break;
        }

    }

    *got_frame = 1;
    *(AVFrame*)data = smk->pic;

    /* always report that the buffer was completely consumed */
    return avpkt->size;
}



/*
 *
 * Init smacker decoder
 *
 */
static av_cold int decode_init(AVCodecContext *avctx)
{
    SmackVContext * const c = avctx->priv_data;

    c->avctx = avctx;

    avctx->pix_fmt = AV_PIX_FMT_PAL8;

    avcodec_get_frame_defaults(&c->pic);

    /* decode huffman trees from extradata */
    if(avctx->extradata_size < 16){
        av_log(avctx, AV_LOG_ERROR, "Extradata missing!\n");
        return AVERROR(EINVAL);
    }

    if (decode_header_trees(c))
        return AVERROR_INVALIDDATA;

    return 0;
}



/*
 *
 * Uninit smacker decoder
 *
 */
static av_cold int decode_end(AVCodecContext *avctx)
{
    SmackVContext * const smk = avctx->priv_data;

    av_freep(&smk->mmap_tbl);
    av_freep(&smk->mclr_tbl);
    av_freep(&smk->full_tbl);
    av_freep(&smk->type_tbl);

    if (smk->pic.data[0])
        avctx->release_buffer(avctx, &smk->pic);

    return 0;
}


static av_cold int smka_decode_init(AVCodecContext *avctx)
{
    if (avctx->channels < 1 || avctx->channels > 2) {
        av_log(avctx, AV_LOG_ERROR, "invalid number of channels\n");
        return AVERROR(EINVAL);
    }
    avctx->channel_layout = (avctx->channels==2) ? AV_CH_LAYOUT_STEREO : AV_CH_LAYOUT_MONO;
    avctx->sample_fmt = avctx->bits_per_coded_sample == 8 ? AV_SAMPLE_FMT_U8 : AV_SAMPLE_FMT_S16;

    return 0;
}

/**
 * Decode Smacker audio data
 */
static int smka_decode_frame(AVCodecContext *avctx, void *data,
                             int *got_frame_ptr, AVPacket *avpkt)
{
    AVFrame *frame     = data;
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    GetBitContext gb;
    HuffContext h[4] = { { 0 } };
    VLC vlc[4]       = { { 0 } };
    int16_t *samples;
    uint8_t *samples8;
    int val;
    int i, res, ret;
    int unp_size;
    int bits, stereo;
    int pred[2] = {0, 0};

    if (buf_size <= 4) {
        av_log(avctx, AV_LOG_ERROR, "packet is too small\n");
        return AVERROR(EINVAL);
    }

    unp_size = AV_RL32(buf);

    if (unp_size > (1U<<24)) {
        av_log(avctx, AV_LOG_ERROR, "packet is too big\n");
        return AVERROR_INVALIDDATA;
    }

    init_get_bits(&gb, buf + 4, (buf_size - 4) * 8);

    if(!get_bits1(&gb)){
        av_log(avctx, AV_LOG_INFO, "Sound: no data\n");
        *got_frame_ptr = 0;
        return 1;
    }
    stereo = get_bits1(&gb);
    bits = get_bits1(&gb);
    if (stereo ^ (avctx->channels != 1)) {
        av_log(avctx, AV_LOG_ERROR, "channels mismatch\n");
        return AVERROR(EINVAL);
    }
    if (bits && avctx->sample_fmt == AV_SAMPLE_FMT_U8) {
        av_log(avctx, AV_LOG_ERROR, "sample format mismatch\n");
        return AVERROR(EINVAL);
    }

    /* get output buffer */
    frame->nb_samples = unp_size / (avctx->channels * (bits + 1));
    if ((ret = ff_get_buffer(avctx, frame)) < 0) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return ret;
    }
    samples  = (int16_t *)frame->data[0];
    samples8 =            frame->data[0];

    // Initialize
    for(i = 0; i < (1 << (bits + stereo)); i++) {
        h[i].length = 256;
        h[i].maxlength = 0;
        h[i].current = 0;
        h[i].bits = av_mallocz(256 * 4);
        h[i].lengths = av_mallocz(256 * sizeof(int));
        h[i].values = av_mallocz(256 * sizeof(int));
        skip_bits1(&gb);
        res = smacker_decode_tree(&gb, &h[i], 0, 0);
        if (res < 0)
            return res;
        skip_bits1(&gb);
        if(h[i].current > 1) {
            res = init_vlc(&vlc[i], SMKTREE_BITS, h[i].length,
                    h[i].lengths, sizeof(int), sizeof(int),
                    h[i].bits, sizeof(uint32_t), sizeof(uint32_t), INIT_VLC_LE);
            if(res < 0) {
                av_log(avctx, AV_LOG_ERROR, "Cannot build VLC table\n");
                return AVERROR_INVALIDDATA;
            }
        }
    }
    if(bits) { //decode 16-bit data
        for(i = stereo; i >= 0; i--)
            pred[i] = sign_extend(av_bswap16(get_bits(&gb, 16)), 16);
        for(i = 0; i <= stereo; i++)
            *samples++ = pred[i];
        for(; i < unp_size / 2; i++) {
            if(get_bits_left(&gb)<0)
                return AVERROR_INVALIDDATA;
            if(i & stereo) {
                if(vlc[2].table)
                    res = get_vlc2(&gb, vlc[2].table, SMKTREE_BITS, 3);
                else
                    res = 0;
                if (res < 0) {
                    av_log(avctx, AV_LOG_ERROR, "invalid vlc\n");
                    return AVERROR_INVALIDDATA;
                }
                val  = h[2].values[res];
                if(vlc[3].table)
                    res = get_vlc2(&gb, vlc[3].table, SMKTREE_BITS, 3);
                else
                    res = 0;
                if (res < 0) {
                    av_log(avctx, AV_LOG_ERROR, "invalid vlc\n");
                    return AVERROR_INVALIDDATA;
                }
                val |= h[3].values[res] << 8;
                pred[1] += sign_extend(val, 16);
                *samples++ = av_clip_int16(pred[1]);
            } else {
                if(vlc[0].table)
                    res = get_vlc2(&gb, vlc[0].table, SMKTREE_BITS, 3);
                else
                    res = 0;
                if (res < 0) {
                    av_log(avctx, AV_LOG_ERROR, "invalid vlc\n");
                    return AVERROR_INVALIDDATA;
                }
                val  = h[0].values[res];
                if(vlc[1].table)
                    res = get_vlc2(&gb, vlc[1].table, SMKTREE_BITS, 3);
                else
                    res = 0;
                if (res < 0) {
                    av_log(avctx, AV_LOG_ERROR, "invalid vlc\n");
                    return AVERROR_INVALIDDATA;
                }
                val |= h[1].values[res] << 8;
                pred[0] += sign_extend(val, 16);
                *samples++ = av_clip_int16(pred[0]);
            }
        }
    } else { //8-bit data
        for(i = stereo; i >= 0; i--)
            pred[i] = get_bits(&gb, 8);
        for(i = 0; i <= stereo; i++)
            *samples8++ = pred[i];
        for(; i < unp_size; i++) {
            if(get_bits_left(&gb)<0)
                return AVERROR_INVALIDDATA;
            if(i & stereo){
                if(vlc[1].table)
                    res = get_vlc2(&gb, vlc[1].table, SMKTREE_BITS, 3);
                else
                    res = 0;
                if (res < 0) {
                    av_log(avctx, AV_LOG_ERROR, "invalid vlc\n");
                    return AVERROR_INVALIDDATA;
                }
                pred[1] += sign_extend(h[1].values[res], 8);
                *samples8++ = av_clip_uint8(pred[1]);
            } else {
                if(vlc[0].table)
                    res = get_vlc2(&gb, vlc[0].table, SMKTREE_BITS, 3);
                else
                    res = 0;
                if (res < 0) {
                    av_log(avctx, AV_LOG_ERROR, "invalid vlc\n");
                    return AVERROR_INVALIDDATA;
                }
                pred[0] += sign_extend(h[0].values[res], 8);
                *samples8++ = av_clip_uint8(pred[0]);
            }
        }
    }

    for(i = 0; i < 4; i++) {
        if(vlc[i].table)
            ff_free_vlc(&vlc[i]);
        av_free(h[i].bits);
        av_free(h[i].lengths);
        av_free(h[i].values);
    }

    *got_frame_ptr = 1;

    return buf_size;
}

AVCodec ff_smacker_decoder = {
    .name           = "smackvid",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_SMACKVIDEO,
    .priv_data_size = sizeof(SmackVContext),
    .init           = decode_init,
    .close          = decode_end,
    .decode         = decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name      = NULL_IF_CONFIG_SMALL("Smacker video"),
};

AVCodec ff_smackaud_decoder = {
    .name           = "smackaud",
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_SMACKAUDIO,
    .init           = smka_decode_init,
    .decode         = smka_decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name      = NULL_IF_CONFIG_SMALL("Smacker audio"),
};
