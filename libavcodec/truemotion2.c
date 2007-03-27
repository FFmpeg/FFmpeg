/*
 * Duck/ON2 TrueMotion 2 Decoder
 * Copyright (c) 2005 Konstantin Shishkov
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
 *
 */

/**
 * @file truemotion2.c
 * Duck TrueMotion2 decoder.
 */

#include "avcodec.h"
#include "common.h"
#include "bitstream.h"
#include "dsputil.h"

#define TM2_ESCAPE 0x80000000
#define TM2_DELTAS 64
/* Huffman-coded streams of different types of blocks */
enum TM2_STREAMS{ TM2_C_HI = 0, TM2_C_LO, TM2_L_HI, TM2_L_LO,
     TM2_UPD, TM2_MOT, TM2_TYPE, TM2_NUM_STREAMS};
/* Block types */
enum TM2_BLOCKS{ TM2_HI_RES = 0, TM2_MED_RES, TM2_LOW_RES, TM2_NULL_RES,
                 TM2_UPDATE, TM2_STILL, TM2_MOTION};

typedef struct TM2Context{
    AVCodecContext *avctx;
    AVFrame pic;

    GetBitContext gb;
    DSPContext dsp;

    /* TM2 streams */
    int *tokens[TM2_NUM_STREAMS];
    int tok_lens[TM2_NUM_STREAMS];
    int tok_ptrs[TM2_NUM_STREAMS];
    int deltas[TM2_NUM_STREAMS][TM2_DELTAS];
    /* for blocks decoding */
    int D[4];
    int CD[4];
    int *last;
    int *clast;

    /* data for current and previous frame */
    int *Y1, *U1, *V1, *Y2, *U2, *V2;
    int cur;
} TM2Context;

/**
* Huffman codes for each of streams
*/
typedef struct TM2Codes{
    VLC vlc; ///< table for FFmpeg bitstream reader
    int bits;
    int *recode; ///< table for converting from code indexes to values
    int length;
} TM2Codes;

/**
* structure for gathering Huffman codes information
*/
typedef struct TM2Huff{
    int val_bits; ///< length of literal
    int max_bits; ///< maximum length of code
    int min_bits; ///< minimum length of code
    int nodes; ///< total number of nodes in tree
    int num; ///< current number filled
    int max_num; ///< total number of codes
    int *nums; ///< literals
    uint32_t *bits; ///< codes
    int *lens; ///< codelengths
} TM2Huff;

static int tm2_read_tree(TM2Context *ctx, uint32_t prefix, int length, TM2Huff *huff)
{
    if(length > huff->max_bits) {
        av_log(ctx->avctx, AV_LOG_ERROR, "Tree exceeded its given depth (%i)\n", huff->max_bits);
        return -1;
    }

    if(!get_bits1(&ctx->gb)) { /* literal */
        if (length == 0) {
            length = 1;
        }
        if(huff->num >= huff->max_num) {
            av_log(ctx->avctx, AV_LOG_DEBUG, "Too many literals\n");
            return -1;
        }
        huff->nums[huff->num] = get_bits_long(&ctx->gb, huff->val_bits);
        huff->bits[huff->num] = prefix;
        huff->lens[huff->num] = length;
        huff->num++;
        return 0;
    } else { /* non-terminal node */
        if(tm2_read_tree(ctx, prefix << 1, length + 1, huff) == -1)
            return -1;
        if(tm2_read_tree(ctx, (prefix << 1) | 1, length + 1, huff) == -1)
            return -1;
    }
    return 0;
}

static int tm2_build_huff_table(TM2Context *ctx, TM2Codes *code)
{
    TM2Huff huff;
    int res = 0;

    huff.val_bits = get_bits(&ctx->gb, 5);
    huff.max_bits = get_bits(&ctx->gb, 5);
    huff.min_bits = get_bits(&ctx->gb, 5);
    huff.nodes = get_bits_long(&ctx->gb, 17);
    huff.num = 0;

    /* check for correct codes parameters */
    if((huff.val_bits < 1) || (huff.val_bits > 32) ||
       (huff.max_bits < 0) || (huff.max_bits > 32)) {
        av_log(ctx->avctx, AV_LOG_ERROR, "Incorrect tree parameters - literal length: %i, max code length: %i\n",
               huff.val_bits, huff.max_bits);
        return -1;
    }
    if((huff.nodes < 0) || (huff.nodes > 0x10000)) {
        av_log(ctx->avctx, AV_LOG_ERROR, "Incorrect number of Huffman tree nodes: %i\n", huff.nodes);
        return -1;
    }
    /* one-node tree */
    if(huff.max_bits == 0)
        huff.max_bits = 1;

    /* allocate space for codes - it is exactly ceil(nodes / 2) entries */
    huff.max_num = (huff.nodes + 1) >> 1;
    huff.nums = av_mallocz(huff.max_num * sizeof(int));
    huff.bits = av_mallocz(huff.max_num * sizeof(uint32_t));
    huff.lens = av_mallocz(huff.max_num * sizeof(int));

    if(tm2_read_tree(ctx, 0, 0, &huff) == -1)
        res = -1;

    if(huff.num != huff.max_num) {
        av_log(ctx->avctx, AV_LOG_ERROR, "Got less codes than expected: %i of %i\n",
               huff.num, huff.max_num);
        res = -1;
    }

    /* convert codes to vlc_table */
    if(res != -1) {
        int i;

        res = init_vlc(&code->vlc, huff.max_bits, huff.max_num,
                    huff.lens, sizeof(int), sizeof(int),
                    huff.bits, sizeof(uint32_t), sizeof(uint32_t), 0);
        if(res < 0) {
            av_log(ctx->avctx, AV_LOG_ERROR, "Cannot build VLC table\n");
            res = -1;
        } else
            res = 0;
        if(res != -1) {
            code->bits = huff.max_bits;
            code->length = huff.max_num;
            code->recode = av_malloc(code->length * sizeof(int));
            for(i = 0; i < code->length; i++)
                code->recode[i] = huff.nums[i];
        }
    }
    /* free allocated memory */
    av_free(huff.nums);
    av_free(huff.bits);
    av_free(huff.lens);

    return res;
}

static void tm2_free_codes(TM2Codes *code)
{
    if(code->recode)
        av_free(code->recode);
    if(code->vlc.table)
        free_vlc(&code->vlc);
}

static inline int tm2_get_token(GetBitContext *gb, TM2Codes *code)
{
    int val;
    val = get_vlc2(gb, code->vlc.table, code->bits, 1);
    return code->recode[val];
}

static inline int tm2_read_header(TM2Context *ctx, uint8_t *buf)
{
    uint32_t magic;
    uint8_t *obuf;
    int length;

    obuf = buf;

    magic = AV_RL32(buf);
    buf += 4;

    if(magic == 0x00000100) { /* old header */
/*      av_log (ctx->avctx, AV_LOG_ERROR, "TM2 old header: not implemented (yet)\n"); */
        return 40;
    } else if(magic == 0x00000101) { /* new header */
        int w, h, size, flags, xr, yr;

        length = AV_RL32(buf);
        buf += 4;

        init_get_bits(&ctx->gb, buf, 32 * 8);
        size = get_bits_long(&ctx->gb, 31);
        h = get_bits(&ctx->gb, 15);
        w = get_bits(&ctx->gb, 15);
        flags = get_bits_long(&ctx->gb, 31);
        yr = get_bits(&ctx->gb, 9);
        xr = get_bits(&ctx->gb, 9);

        return 40;
    } else {
        av_log (ctx->avctx, AV_LOG_ERROR, "Not a TM2 header: 0x%08X\n", magic);
        return -1;
    }

    return (buf - obuf);
}

static int tm2_read_deltas(TM2Context *ctx, int stream_id) {
    int d, mb;
    int i, v;

    d = get_bits(&ctx->gb, 9);
    mb = get_bits(&ctx->gb, 5);

    if((d < 1) || (d > TM2_DELTAS) || (mb < 1) || (mb > 32)) {
        av_log(ctx->avctx, AV_LOG_ERROR, "Incorrect delta table: %i deltas x %i bits\n", d, mb);
        return -1;
    }

    for(i = 0; i < d; i++) {
        v = get_bits_long(&ctx->gb, mb);
        if(v & (1 << (mb - 1)))
            ctx->deltas[stream_id][i] = v - (1 << mb);
        else
            ctx->deltas[stream_id][i] = v;
    }
    for(; i < TM2_DELTAS; i++)
        ctx->deltas[stream_id][i] = 0;

    return 0;
}

static int tm2_read_stream(TM2Context *ctx, uint8_t *buf, int stream_id) {
    int i;
    int cur = 0;
    int skip = 0;
    int len, toks;
    TM2Codes codes;

    /* get stream length in dwords */
    len = AV_RB32(buf); buf += 4; cur += 4;
    skip = len * 4 + 4;

    if(len == 0)
        return 4;

    toks = AV_RB32(buf); buf += 4; cur += 4;
    if(toks & 1) {
        len = AV_RB32(buf); buf += 4; cur += 4;
        if(len == TM2_ESCAPE) {
            len = AV_RB32(buf); buf += 4; cur += 4;
        }
        if(len > 0) {
            init_get_bits(&ctx->gb, buf, (skip - cur) * 8);
            if(tm2_read_deltas(ctx, stream_id) == -1)
                return -1;
            buf += ((get_bits_count(&ctx->gb) + 31) >> 5) << 2;
            cur += ((get_bits_count(&ctx->gb) + 31) >> 5) << 2;
        }
    }
    /* skip unused fields */
    if(AV_RB32(buf) == TM2_ESCAPE) {
        buf += 4; cur += 4; /* some unknown length - could be escaped too */
    }
    buf += 4; cur += 4;
    buf += 4; cur += 4; /* unused by decoder */

    init_get_bits(&ctx->gb, buf, (skip - cur) * 8);
    if(tm2_build_huff_table(ctx, &codes) == -1)
        return -1;
    buf += ((get_bits_count(&ctx->gb) + 31) >> 5) << 2;
    cur += ((get_bits_count(&ctx->gb) + 31) >> 5) << 2;

    toks >>= 1;
    /* check if we have sane number of tokens */
    if((toks < 0) || (toks > 0xFFFFFF)){
        av_log(ctx->avctx, AV_LOG_ERROR, "Incorrect number of tokens: %i\n", toks);
        tm2_free_codes(&codes);
        return -1;
    }
    ctx->tokens[stream_id] = av_realloc(ctx->tokens[stream_id], toks * sizeof(int));
    ctx->tok_lens[stream_id] = toks;
    len = AV_RB32(buf); buf += 4; cur += 4;
    if(len > 0) {
        init_get_bits(&ctx->gb, buf, (skip - cur) * 8);
        for(i = 0; i < toks; i++)
            ctx->tokens[stream_id][i] = tm2_get_token(&ctx->gb, &codes);
    } else {
        for(i = 0; i < toks; i++)
            ctx->tokens[stream_id][i] = codes.recode[0];
    }
    tm2_free_codes(&codes);

    return skip;
}

static inline int GET_TOK(TM2Context *ctx,int type) {
    if(ctx->tok_ptrs[type] >= ctx->tok_lens[type]) {
        av_log(ctx->avctx, AV_LOG_ERROR, "Read token from stream %i out of bounds (%i>=%i)\n", type, ctx->tok_ptrs[type], ctx->tok_lens[type]);
        return 0;
    }
    if(type <= TM2_MOT)
        return ctx->deltas[type][ctx->tokens[type][ctx->tok_ptrs[type]++]];
    return ctx->tokens[type][ctx->tok_ptrs[type]++];
}

/* blocks decoding routines */

/* common Y, U, V pointers initialisation */
#define TM2_INIT_POINTERS() \
    int *last, *clast; \
    int *Y, *U, *V;\
    int Ystride, Ustride, Vstride;\
\
    Ystride = ctx->avctx->width;\
    Vstride = (ctx->avctx->width + 1) >> 1;\
    Ustride = (ctx->avctx->width + 1) >> 1;\
    Y = (ctx->cur?ctx->Y2:ctx->Y1) + by * 4 * Ystride + bx * 4;\
    V = (ctx->cur?ctx->V2:ctx->V1) + by * 2 * Vstride + bx * 2;\
    U = (ctx->cur?ctx->U2:ctx->U1) + by * 2 * Ustride + bx * 2;\
    last = ctx->last + bx * 4;\
    clast = ctx->clast + bx * 4;

#define TM2_INIT_POINTERS_2() \
    int *Yo, *Uo, *Vo;\
    int oYstride, oUstride, oVstride;\
\
    TM2_INIT_POINTERS();\
    oYstride = Ystride;\
    oVstride = Vstride;\
    oUstride = Ustride;\
    Yo = (ctx->cur?ctx->Y1:ctx->Y2) + by * 4 * oYstride + bx * 4;\
    Vo = (ctx->cur?ctx->V1:ctx->V2) + by * 2 * oVstride + bx * 2;\
    Uo = (ctx->cur?ctx->U1:ctx->U2) + by * 2 * oUstride + bx * 2;

/* recalculate last and delta values for next blocks */
#define TM2_RECALC_BLOCK(CHR, stride, last, CD) {\
    CD[0] = (CHR[1] - 128) - last[1];\
    CD[1] = (int)CHR[stride + 1] - (int)CHR[1];\
    last[0] = (int)CHR[stride + 0] - 128;\
    last[1] = (int)CHR[stride + 1] - 128;}

/* common operations - add deltas to 4x4 block of luma or 2x2 blocks of chroma */
static inline void tm2_apply_deltas(TM2Context *ctx, int* Y, int stride, int *deltas, int *last)
{
    int ct, d;
    int i, j;

    for(j = 0; j < 4; j++){
        ct = ctx->D[j];
        for(i = 0; i < 4; i++){
            d = deltas[i + j * 4];
            ct += d;
            last[i] += ct;
            Y[i] = av_clip_uint8(last[i]);
        }
        Y += stride;
        ctx->D[j] = ct;
    }
}

static inline void tm2_high_chroma(int *data, int stride, int *last, int *CD, int *deltas)
{
    int i, j;
    for(j = 0; j < 2; j++){
        for(i = 0; i < 2; i++){
            CD[j] += deltas[i + j * 2];
            last[i] += CD[j];
            data[i] = last[i] + 128;
        }
        data += stride;
    }
}

static inline void tm2_low_chroma(int *data, int stride, int *clast, int *CD, int *deltas, int bx)
{
    int t;
    int l;
    int prev;

    if(bx > 0)
        prev = clast[-3];
    else
        prev = 0;
    t = (CD[0] + CD[1]) >> 1;
    l = (prev - CD[0] - CD[1] + clast[1]) >> 1;
    CD[1] = CD[0] + CD[1] - t;
    CD[0] = t;
    clast[0] = l;

    tm2_high_chroma(data, stride, clast, CD, deltas);
}

static inline void tm2_hi_res_block(TM2Context *ctx, AVFrame *pic, int bx, int by)
{
    int i;
    int deltas[16];
    TM2_INIT_POINTERS();

    /* hi-res chroma */
    for(i = 0; i < 4; i++) {
        deltas[i] = GET_TOK(ctx, TM2_C_HI);
        deltas[i + 4] = GET_TOK(ctx, TM2_C_HI);
    }
    tm2_high_chroma(U, Ustride, clast, ctx->CD, deltas);
    tm2_high_chroma(V, Vstride, clast + 2, ctx->CD + 2, deltas + 4);

    /* hi-res luma */
    for(i = 0; i < 16; i++)
        deltas[i] = GET_TOK(ctx, TM2_L_HI);

    tm2_apply_deltas(ctx, Y, Ystride, deltas, last);
}

static inline void tm2_med_res_block(TM2Context *ctx, AVFrame *pic, int bx, int by)
{
    int i;
    int deltas[16];
    TM2_INIT_POINTERS();

    /* low-res chroma */
    deltas[0] = GET_TOK(ctx, TM2_C_LO);
    deltas[1] = deltas[2] = deltas[3] = 0;
    tm2_low_chroma(U, Ustride, clast, ctx->CD, deltas, bx);

    deltas[0] = GET_TOK(ctx, TM2_C_LO);
    deltas[1] = deltas[2] = deltas[3] = 0;
    tm2_low_chroma(V, Vstride, clast + 2, ctx->CD + 2, deltas, bx);

    /* hi-res luma */
    for(i = 0; i < 16; i++)
        deltas[i] = GET_TOK(ctx, TM2_L_HI);

    tm2_apply_deltas(ctx, Y, Ystride, deltas, last);
}

static inline void tm2_low_res_block(TM2Context *ctx, AVFrame *pic, int bx, int by)
{
    int i;
    int t1, t2;
    int deltas[16];
    TM2_INIT_POINTERS();

    /* low-res chroma */
    deltas[0] = GET_TOK(ctx, TM2_C_LO);
    deltas[1] = deltas[2] = deltas[3] = 0;
    tm2_low_chroma(U, Ustride, clast, ctx->CD, deltas, bx);

    deltas[0] = GET_TOK(ctx, TM2_C_LO);
    deltas[1] = deltas[2] = deltas[3] = 0;
    tm2_low_chroma(V, Vstride, clast + 2, ctx->CD + 2, deltas, bx);

    /* low-res luma */
    for(i = 0; i < 16; i++)
        deltas[i] = 0;

    deltas[ 0] = GET_TOK(ctx, TM2_L_LO);
    deltas[ 2] = GET_TOK(ctx, TM2_L_LO);
    deltas[ 8] = GET_TOK(ctx, TM2_L_LO);
    deltas[10] = GET_TOK(ctx, TM2_L_LO);

    if(bx > 0)
        last[0] = (last[-1] - ctx->D[0] - ctx->D[1] - ctx->D[2] - ctx->D[3] + last[1]) >> 1;
    else
        last[0] = (last[1]  - ctx->D[0] - ctx->D[1] - ctx->D[2] - ctx->D[3])>> 1;
    last[2] = (last[1] + last[3]) >> 1;

    t1 = ctx->D[0] + ctx->D[1];
    ctx->D[0] = t1 >> 1;
    ctx->D[1] = t1 - (t1 >> 1);
    t2 = ctx->D[2] + ctx->D[3];
    ctx->D[2] = t2 >> 1;
    ctx->D[3] = t2 - (t2 >> 1);

    tm2_apply_deltas(ctx, Y, Ystride, deltas, last);
}

static inline void tm2_null_res_block(TM2Context *ctx, AVFrame *pic, int bx, int by)
{
    int i;
    int ct;
    int left, right, diff;
    int deltas[16];
    TM2_INIT_POINTERS();

    /* null chroma */
    deltas[0] = deltas[1] = deltas[2] = deltas[3] = 0;
    tm2_low_chroma(U, Ustride, clast, ctx->CD, deltas, bx);

    deltas[0] = deltas[1] = deltas[2] = deltas[3] = 0;
    tm2_low_chroma(V, Vstride, clast + 2, ctx->CD + 2, deltas, bx);

    /* null luma */
    for(i = 0; i < 16; i++)
        deltas[i] = 0;

    ct = ctx->D[0] + ctx->D[1] + ctx->D[2] + ctx->D[3];

    if(bx > 0)
        left = last[-1] - ct;
    else
        left = 0;

    right = last[3];
    diff = right - left;
    last[0] = left + (diff >> 2);
    last[1] = left + (diff >> 1);
    last[2] = right - (diff >> 2);
    last[3] = right;
    {
        int tp = left;

        ctx->D[0] = (tp + (ct >> 2)) - left;
        left += ctx->D[0];
        ctx->D[1] = (tp + (ct >> 1)) - left;
        left += ctx->D[1];
        ctx->D[2] = ((tp + ct) - (ct >> 2)) - left;
        left += ctx->D[2];
        ctx->D[3] = (tp + ct) - left;
    }
    tm2_apply_deltas(ctx, Y, Ystride, deltas, last);
}

static inline void tm2_still_block(TM2Context *ctx, AVFrame *pic, int bx, int by)
{
    int i, j;
    TM2_INIT_POINTERS_2();

    /* update chroma */
    for(j = 0; j < 2; j++){
        for(i = 0; i < 2; i++){
            U[i] = Uo[i];
            V[i] = Vo[i];
        }
        U += Ustride; V += Vstride;
        Uo += oUstride; Vo += oVstride;
    }
    U -= Ustride * 2;
    V -= Vstride * 2;
    TM2_RECALC_BLOCK(U, Ustride, clast, ctx->CD);
    TM2_RECALC_BLOCK(V, Vstride, (clast + 2), (ctx->CD + 2));

    /* update deltas */
    ctx->D[0] = Yo[3] - last[3];
    ctx->D[1] = Yo[3 + oYstride] - Yo[3];
    ctx->D[2] = Yo[3 + oYstride * 2] - Yo[3 + oYstride];
    ctx->D[3] = Yo[3 + oYstride * 3] - Yo[3 + oYstride * 2];

    for(j = 0; j < 4; j++){
        for(i = 0; i < 4; i++){
            Y[i] = Yo[i];
            last[i] = Yo[i];
        }
        Y += Ystride;
        Yo += oYstride;
    }
}

static inline void tm2_update_block(TM2Context *ctx, AVFrame *pic, int bx, int by)
{
    int i, j;
    int d;
    TM2_INIT_POINTERS_2();

    /* update chroma */
    for(j = 0; j < 2; j++){
        for(i = 0; i < 2; i++){
            U[i] = Uo[i] + GET_TOK(ctx, TM2_UPD);
            V[i] = Vo[i] + GET_TOK(ctx, TM2_UPD);
        }
        U += Ustride; V += Vstride;
        Uo += oUstride; Vo += oVstride;
    }
    U -= Ustride * 2;
    V -= Vstride * 2;
    TM2_RECALC_BLOCK(U, Ustride, clast, ctx->CD);
    TM2_RECALC_BLOCK(V, Vstride, (clast + 2), (ctx->CD + 2));

    /* update deltas */
    ctx->D[0] = Yo[3] - last[3];
    ctx->D[1] = Yo[3 + oYstride] - Yo[3];
    ctx->D[2] = Yo[3 + oYstride * 2] - Yo[3 + oYstride];
    ctx->D[3] = Yo[3 + oYstride * 3] - Yo[3 + oYstride * 2];

    for(j = 0; j < 4; j++){
        d = last[3];
        for(i = 0; i < 4; i++){
            Y[i] = Yo[i] + GET_TOK(ctx, TM2_UPD);
            last[i] = Y[i];
        }
        ctx->D[j] = last[3] - d;
        Y += Ystride;
        Yo += oYstride;
    }
}

static inline void tm2_motion_block(TM2Context *ctx, AVFrame *pic, int bx, int by)
{
    int i, j;
    int mx, my;
    TM2_INIT_POINTERS_2();

    mx = GET_TOK(ctx, TM2_MOT);
    my = GET_TOK(ctx, TM2_MOT);

    Yo += my * oYstride + mx;
    Uo += (my >> 1) * oUstride + (mx >> 1);
    Vo += (my >> 1) * oVstride + (mx >> 1);

    /* copy chroma */
    for(j = 0; j < 2; j++){
        for(i = 0; i < 2; i++){
            U[i] = Uo[i];
            V[i] = Vo[i];
        }
        U += Ustride; V += Vstride;
        Uo += oUstride; Vo += oVstride;
    }
    U -= Ustride * 2;
    V -= Vstride * 2;
    TM2_RECALC_BLOCK(U, Ustride, clast, ctx->CD);
    TM2_RECALC_BLOCK(V, Vstride, (clast + 2), (ctx->CD + 2));

    /* copy luma */
    for(j = 0; j < 4; j++){
        for(i = 0; i < 4; i++){
            Y[i] = Yo[i];
        }
        Y += Ystride;
        Yo += oYstride;
    }
    /* calculate deltas */
    Y -= Ystride * 4;
    ctx->D[0] = Y[3] - last[3];
    ctx->D[1] = Y[3 + Ystride] - Y[3];
    ctx->D[2] = Y[3 + Ystride * 2] - Y[3 + Ystride];
    ctx->D[3] = Y[3 + Ystride * 3] - Y[3 + Ystride * 2];
    for(i = 0; i < 4; i++)
        last[i] = Y[i + Ystride * 3];
}

static int tm2_decode_blocks(TM2Context *ctx, AVFrame *p)
{
    int i, j;
    int bw, bh;
    int type;
    int keyframe = 1;
    uint8_t *Y, *U, *V;
    int *src;

    bw = ctx->avctx->width >> 2;
    bh = ctx->avctx->height >> 2;

    for(i = 0; i < TM2_NUM_STREAMS; i++)
        ctx->tok_ptrs[i] = 0;

    if (ctx->tok_lens[TM2_TYPE]<bw*bh){
        av_log(ctx->avctx,AV_LOG_ERROR,"Got %i tokens for %i blocks\n",ctx->tok_lens[TM2_TYPE],bw*bh);
        return -1;
    }

    memset(ctx->last, 0, 4 * bw * sizeof(int));
    memset(ctx->clast, 0, 4 * bw * sizeof(int));

    for(j = 0; j < bh; j++) {
        memset(ctx->D, 0, 4 * sizeof(int));
        memset(ctx->CD, 0, 4 * sizeof(int));
        for(i = 0; i < bw; i++) {
            type = GET_TOK(ctx, TM2_TYPE);
            switch(type) {
            case TM2_HI_RES:
                tm2_hi_res_block(ctx, p, i, j);
                break;
            case TM2_MED_RES:
                tm2_med_res_block(ctx, p, i, j);
                break;
            case TM2_LOW_RES:
                tm2_low_res_block(ctx, p, i, j);
                break;
            case TM2_NULL_RES:
                tm2_null_res_block(ctx, p, i, j);
                break;
            case TM2_UPDATE:
                tm2_update_block(ctx, p, i, j);
                keyframe = 0;
                break;
            case TM2_STILL:
                tm2_still_block(ctx, p, i, j);
                keyframe = 0;
                break;
            case TM2_MOTION:
                tm2_motion_block(ctx, p, i, j);
                keyframe = 0;
                break;
            default:
                av_log(ctx->avctx, AV_LOG_ERROR, "Skipping unknown block type %i\n", type);
            }
        }
    }

    /* copy data from our buffer to AVFrame */
    Y = p->data[0];
    src = (ctx->cur?ctx->Y2:ctx->Y1);
    for(j = 0; j < ctx->avctx->height; j++){
        for(i = 0; i < ctx->avctx->width; i++){
            Y[i] = av_clip_uint8(*src++);
        }
        Y += p->linesize[0];
    }
    U = p->data[2];
    src = (ctx->cur?ctx->U2:ctx->U1);
    for(j = 0; j < (ctx->avctx->height + 1) >> 1; j++){
        for(i = 0; i < (ctx->avctx->width + 1) >> 1; i++){
            U[i] = av_clip_uint8(*src++);
        }
        U += p->linesize[2];
    }
    V = p->data[1];
    src = (ctx->cur?ctx->V2:ctx->V1);
    for(j = 0; j < (ctx->avctx->height + 1) >> 1; j++){
        for(i = 0; i < (ctx->avctx->width + 1) >> 1; i++){
            V[i] = av_clip_uint8(*src++);
        }
        V += p->linesize[1];
    }

    return keyframe;
}

static int decode_frame(AVCodecContext *avctx,
                        void *data, int *data_size,
                        uint8_t *buf, int buf_size)
{
    TM2Context * const l = avctx->priv_data;
    AVFrame * const p= (AVFrame*)&l->pic;
    int skip, t;

    p->reference = 1;
    p->buffer_hints = FF_BUFFER_HINTS_VALID | FF_BUFFER_HINTS_PRESERVE | FF_BUFFER_HINTS_REUSABLE;
    if(avctx->reget_buffer(avctx, p) < 0){
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return -1;
    }

    l->dsp.bswap_buf((uint32_t*)buf, (uint32_t*)buf, buf_size >> 2);
    skip = tm2_read_header(l, buf);

    if(skip == -1)
        return -1;

    t = tm2_read_stream(l, buf + skip, TM2_C_HI);
    if(t == -1)
        return -1;
    skip += t;
    t = tm2_read_stream(l, buf + skip, TM2_C_LO);
    if(t == -1)
        return -1;
    skip += t;
    t = tm2_read_stream(l, buf + skip, TM2_L_HI);
    if(t == -1)
        return -1;
    skip += t;
    t = tm2_read_stream(l, buf + skip, TM2_L_LO);
    if(t == -1)
        return -1;
    skip += t;
    t = tm2_read_stream(l, buf + skip, TM2_UPD);
    if(t == -1)
        return -1;
    skip += t;
    t = tm2_read_stream(l, buf + skip, TM2_MOT);
    if(t == -1)
        return -1;
    skip += t;
    t = tm2_read_stream(l, buf + skip, TM2_TYPE);
    if(t == -1)
        return -1;
    p->key_frame = tm2_decode_blocks(l, p);
    if(p->key_frame)
        p->pict_type = FF_I_TYPE;
    else
        p->pict_type = FF_P_TYPE;

    l->cur = !l->cur;
    *data_size = sizeof(AVFrame);
    *(AVFrame*)data = l->pic;

    return buf_size;
}

static int decode_init(AVCodecContext *avctx){
    TM2Context * const l = avctx->priv_data;
    int i;

    if (avcodec_check_dimensions(avctx, avctx->width, avctx->height) < 0) {
        return -1;
    }
    if((avctx->width & 3) || (avctx->height & 3)){
        av_log(avctx, AV_LOG_ERROR, "Width and height must be multiple of 4\n");
        return -1;
    }

    l->avctx = avctx;
    l->pic.data[0]=NULL;
    avctx->pix_fmt = PIX_FMT_YUV420P;

    dsputil_init(&l->dsp, avctx);

    l->last = av_malloc(4 * sizeof(int) * (avctx->width >> 2));
    l->clast = av_malloc(4 * sizeof(int) * (avctx->width >> 2));

    for(i = 0; i < TM2_NUM_STREAMS; i++) {
        l->tokens[i] = NULL;
        l->tok_lens[i] = 0;
    }

    l->Y1 = av_malloc(sizeof(int) * avctx->width * avctx->height);
    l->U1 = av_malloc(sizeof(int) * ((avctx->width + 1) >> 1) * ((avctx->height + 1) >> 1));
    l->V1 = av_malloc(sizeof(int) * ((avctx->width + 1) >> 1) * ((avctx->height + 1) >> 1));
    l->Y2 = av_malloc(sizeof(int) * avctx->width * avctx->height);
    l->U2 = av_malloc(sizeof(int) * ((avctx->width + 1) >> 1) * ((avctx->height + 1) >> 1));
    l->V2 = av_malloc(sizeof(int) * ((avctx->width + 1) >> 1) * ((avctx->height + 1) >> 1));
    l->cur = 0;

    return 0;
}

static int decode_end(AVCodecContext *avctx){
    TM2Context * const l = avctx->priv_data;
    int i;

    if(l->last)
        av_free(l->last);
    if(l->clast)
        av_free(l->clast);
    for(i = 0; i < TM2_NUM_STREAMS; i++)
        if(l->tokens[i])
            av_free(l->tokens[i]);
    if(l->Y1){
        av_free(l->Y1);
        av_free(l->U1);
        av_free(l->V1);
        av_free(l->Y2);
        av_free(l->U2);
        av_free(l->V2);
    }
    return 0;
}

AVCodec truemotion2_decoder = {
    "truemotion2",
    CODEC_TYPE_VIDEO,
    CODEC_ID_TRUEMOTION2,
    sizeof(TM2Context),
    decode_init,
    NULL,
    decode_end,
    decode_frame,
    CODEC_CAP_DR1,
};
