/*
 * Apple ProRes encoder
 *
 * Copyright (c) 2011 Anatoliy Wasserman
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

#include "libavutil/mem.h"
#include "libavutil/mem_internal.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avcodec.h"
#include "codec_internal.h"
#include "encode.h"
#include "fdctdsp.h"
#include "put_bits.h"
#include "profiles.h"
#include "bytestream.h"
#include "proresdata.h"
#include "proresenc_kostya_common.h"

#define TRELLIS_WIDTH 16
#define SCORE_LIMIT   INT_MAX / 2

struct TrellisNode {
    int prev_node;
    int quant;
    int bits;
    int score;
};

typedef struct ProresThreadData {
    DECLARE_ALIGNED(16, int16_t, blocks)[MAX_PLANES][64 * 4 * MAX_MBS_PER_SLICE];
    DECLARE_ALIGNED(16, uint16_t, emu_buf)[16 * 16];
    int16_t custom_q[64];
    int16_t custom_chroma_q[64];
    struct TrellisNode *nodes;
} ProresThreadData;

static void get_slice_data(ProresContext *ctx, const uint16_t *src,
                           ptrdiff_t linesize, int x, int y, int w, int h,
                           int16_t *blocks, uint16_t *emu_buf,
                           int mbs_per_slice, int blocks_per_mb, int is_chroma)
{
    const uint16_t *esrc;
    const int mb_width = 4 * blocks_per_mb;
    ptrdiff_t elinesize;
    int i, j, k;

    for (i = 0; i < mbs_per_slice; i++, src += mb_width) {
        if (x >= w) {
            memset(blocks, 0, 64 * (mbs_per_slice - i) * blocks_per_mb
                              * sizeof(*blocks));
            return;
        }
        if (x + mb_width <= w && y + 16 <= h) {
            esrc      = src;
            elinesize = linesize;
        } else {
            int bw, bh, pix;

            esrc      = emu_buf;
            elinesize = 16 * sizeof(*emu_buf);

            bw = FFMIN(w - x, mb_width);
            bh = FFMIN(h - y, 16);

            for (j = 0; j < bh; j++) {
                memcpy(emu_buf + j * 16,
                       (const uint8_t*)src + j * linesize,
                       bw * sizeof(*src));
                pix = emu_buf[j * 16 + bw - 1];
                for (k = bw; k < mb_width; k++)
                    emu_buf[j * 16 + k] = pix;
            }
            for (; j < 16; j++)
                memcpy(emu_buf + j * 16,
                       emu_buf + (bh - 1) * 16,
                       mb_width * sizeof(*emu_buf));
        }
        if (!is_chroma) {
            ctx->fdct(&ctx->fdsp, esrc, elinesize, blocks);
            blocks += 64;
            if (blocks_per_mb > 2) {
                ctx->fdct(&ctx->fdsp, esrc + 8, elinesize, blocks);
                blocks += 64;
            }
            ctx->fdct(&ctx->fdsp, esrc + elinesize * 4, elinesize, blocks);
            blocks += 64;
            if (blocks_per_mb > 2) {
                ctx->fdct(&ctx->fdsp, esrc + elinesize * 4 + 8, elinesize, blocks);
                blocks += 64;
            }
        } else {
            ctx->fdct(&ctx->fdsp, esrc, elinesize, blocks);
            blocks += 64;
            ctx->fdct(&ctx->fdsp, esrc + elinesize * 4, elinesize, blocks);
            blocks += 64;
            if (blocks_per_mb > 2) {
                ctx->fdct(&ctx->fdsp, esrc + 8, elinesize, blocks);
                blocks += 64;
                ctx->fdct(&ctx->fdsp, esrc + elinesize * 4 + 8, elinesize, blocks);
                blocks += 64;
            }
        }

        x += mb_width;
    }
}

static void get_alpha_data(ProresContext *ctx, const uint16_t *src,
                           ptrdiff_t linesize, int x, int y, int w, int h,
                           uint16_t *blocks, int mbs_per_slice, int abits)
{
    const int slice_width = 16 * mbs_per_slice;
    int i, j, copy_w, copy_h;

    copy_w = FFMIN(w - x, slice_width);
    copy_h = FFMIN(h - y, 16);
    for (i = 0; i < copy_h; i++) {
        memcpy(blocks, src, copy_w * sizeof(*src));
        if (abits == 8)
            for (j = 0; j < copy_w; j++)
                blocks[j] >>= 2;
        else
            for (j = 0; j < copy_w; j++)
                blocks[j] = (blocks[j] << 6) | (blocks[j] >> 4);
        for (j = copy_w; j < slice_width; j++)
            blocks[j] = blocks[copy_w - 1];
        blocks += slice_width;
        src    += linesize >> 1;
    }
    for (; i < 16; i++) {
        memcpy(blocks, blocks - slice_width, slice_width * sizeof(*blocks));
        blocks += slice_width;
    }
}

/**
 * Write an unsigned rice/exp golomb codeword.
 */
static inline void encode_vlc_codeword(PutBitContext *pb, unsigned codebook, int val)
{
    unsigned int rice_order, exp_order, switch_bits, switch_val;
    int exponent;

    /* number of prefix bits to switch between Rice and expGolomb */
    switch_bits = (codebook & 3) + 1;
    rice_order  =  codebook >> 5;       /* rice code order */
    exp_order   = (codebook >> 2) & 7;  /* exp golomb code order */

    switch_val  = switch_bits << rice_order;

    if (val >= switch_val) {
        val -= switch_val - (1 << exp_order);
        exponent = av_log2(val);

        put_bits(pb, exponent - exp_order + switch_bits, 0);
        put_bits(pb, exponent + 1, val);
    } else {
        exponent = val >> rice_order;

        if (exponent)
            put_bits(pb, exponent, 0);
        put_bits(pb, 1, 1);
        if (rice_order)
            put_sbits(pb, rice_order, val);
    }
}

#define GET_SIGN(x)  ((x) >> 31)
#define MAKE_CODE(x) (((x) * 2) ^ GET_SIGN(x))

static void encode_dcs(PutBitContext *pb, int16_t *blocks,
                       int blocks_per_slice, int scale)
{
    int i;
    int codebook = 5, code, dc, prev_dc, delta, sign, new_sign;

    prev_dc = (blocks[0] - 0x4000) / scale;
    encode_vlc_codeword(pb, FIRST_DC_CB, MAKE_CODE(prev_dc));
    sign     = 0;
    blocks  += 64;

    for (i = 1; i < blocks_per_slice; i++, blocks += 64) {
        dc       = (blocks[0] - 0x4000) / scale;
        delta    = dc - prev_dc;
        new_sign = GET_SIGN(delta);
        delta    = (delta ^ sign) - sign;
        code     = MAKE_CODE(delta);
        encode_vlc_codeword(pb, ff_prores_dc_codebook[codebook], code);
        codebook = FFMIN(code, 6);
        sign     = new_sign;
        prev_dc  = dc;
    }
}

static void encode_acs(PutBitContext *pb, int16_t *blocks,
                       int blocks_per_slice,
                       const uint8_t *scan, const int16_t *qmat)
{
    int idx, i;
    int prev_run = 4;
    int prev_level = 2;
    int run = 0, level;
    int max_coeffs, abs_level;
    max_coeffs = blocks_per_slice << 6;

    for (i = 1; i < 64; i++) {
        for (idx = scan[i]; idx < max_coeffs; idx += 64) {
            level = blocks[idx] / qmat[scan[i]];
            if (level) {
                abs_level = FFABS(level);
                encode_vlc_codeword(pb, ff_prores_run_to_cb[prev_run], run);
                encode_vlc_codeword(pb, ff_prores_level_to_cb[prev_level], abs_level - 1);
                put_sbits(pb, 1, GET_SIGN(level));

                prev_run   = FFMIN(run, 15);
                prev_level = FFMIN(abs_level, 9);
                run        = 0;
            } else {
                run++;
            }
        }
    }
}

static void encode_slice_plane(ProresContext *ctx, PutBitContext *pb,
                              const uint16_t *src, ptrdiff_t linesize,
                              int mbs_per_slice, int16_t *blocks,
                              int blocks_per_mb,
                              const int16_t *qmat)
{
    int blocks_per_slice = mbs_per_slice * blocks_per_mb;

    encode_dcs(pb, blocks, blocks_per_slice, qmat[0]);
    encode_acs(pb, blocks, blocks_per_slice, ctx->scantable, qmat);
}

static void put_alpha_diff(PutBitContext *pb, int cur, int prev, int abits)
{
    const int dbits = (abits == 8) ? 4 : 7;
    const int dsize = 1 << dbits - 1;
    int diff = cur - prev;

    diff = av_zero_extend(diff, abits);
    if (diff >= (1 << abits) - dsize)
        diff -= 1 << abits;
    if (diff < -dsize || diff > dsize || !diff) {
        put_bits(pb, 1, 1);
        put_bits(pb, abits, diff);
    } else {
        put_bits(pb, 1, 0);
        put_bits(pb, dbits - 1, FFABS(diff) - 1);
        put_bits(pb, 1, diff < 0);
    }
}

static void put_alpha_run(PutBitContext *pb, int run)
{
    if (run) {
        put_bits(pb, 1, 0);
        if (run < 0x10)
            put_bits(pb, 4, run);
        else
            put_bits(pb, 15, run);
    } else {
        put_bits(pb, 1, 1);
    }
}

// todo alpha quantisation for high quants
static void encode_alpha_plane(ProresContext *ctx, PutBitContext *pb,
                              int mbs_per_slice, uint16_t *blocks,
                              int quant)
{
    const int abits = ctx->alpha_bits;
    const int mask  = (1 << abits) - 1;
    const int num_coeffs = mbs_per_slice * 256;
    int prev = mask, cur;
    int idx = 0;
    int run = 0;

    cur = blocks[idx++];
    put_alpha_diff(pb, cur, prev, abits);
    prev = cur;
    do {
        cur = blocks[idx++];
        if (cur != prev) {
            put_alpha_run (pb, run);
            put_alpha_diff(pb, cur, prev, abits);
            prev = cur;
            run  = 0;
        } else {
            run++;
        }
    } while (idx < num_coeffs);
    put_alpha_run(pb, run);
}

static int encode_slice(AVCodecContext *avctx, const AVFrame *pic,
                        PutBitContext *pb,
                        int sizes[4], int x, int y, int quant,
                        int mbs_per_slice)
{
    ProresContext *ctx = avctx->priv_data;
    int i, xp, yp;
    int total_size = 0;
    const uint16_t *src;
    int num_cblocks, pwidth, line_add;
    ptrdiff_t linesize;
    int is_chroma;
    uint16_t *qmat;
    uint16_t *qmat_chroma;

    if (ctx->pictures_per_frame == 1)
        line_add = 0;
    else
        line_add = ctx->cur_picture_idx ^ !(pic->flags & AV_FRAME_FLAG_TOP_FIELD_FIRST);

    if (ctx->force_quant) {
        qmat = ctx->quants[0];
        qmat_chroma = ctx->quants_chroma[0];
    } else if (quant < MAX_STORED_Q) {
        qmat = ctx->quants[quant];
        qmat_chroma = ctx->quants_chroma[quant];
    } else {
        qmat = ctx->custom_q;
        qmat_chroma = ctx->custom_chroma_q;
        for (i = 0; i < 64; i++) {
            qmat[i] = ctx->quant_mat[i] * quant;
            qmat_chroma[i] = ctx->quant_chroma_mat[i] * quant;
        }
    }

    for (i = 0; i < ctx->num_planes; i++) {
        is_chroma    = (i == 1 || i == 2);
        if (!is_chroma || ctx->chroma_factor == CFACTOR_Y444) {
            xp          = x << 4;
            yp          = y << 4;
            num_cblocks = 4;
            pwidth      = avctx->width;
        } else {
            xp          = x << 3;
            yp          = y << 4;
            num_cblocks = 2;
            pwidth      = avctx->width >> 1;
        }

        linesize = pic->linesize[i] * ctx->pictures_per_frame;
        src = (const uint16_t*)(pic->data[i] + yp * linesize +
                                line_add * pic->linesize[i]) + xp;

        if (i < 3) {
            get_slice_data(ctx, src, linesize, xp, yp,
                           pwidth, avctx->height / ctx->pictures_per_frame,
                           ctx->blocks[0], ctx->emu_buf,
                           mbs_per_slice, num_cblocks, is_chroma);
            if (!is_chroma) {/* luma quant */
                encode_slice_plane(ctx, pb, src, linesize,
                                   mbs_per_slice, ctx->blocks[0],
                                   num_cblocks, qmat);
            } else { /* chroma plane */
                encode_slice_plane(ctx, pb, src, linesize,
                                   mbs_per_slice, ctx->blocks[0],
                                   num_cblocks, qmat_chroma);
            }
        } else {
            get_alpha_data(ctx, src, linesize, xp, yp,
                           pwidth, avctx->height / ctx->pictures_per_frame,
                           ctx->blocks[0], mbs_per_slice, ctx->alpha_bits);
            encode_alpha_plane(ctx, pb, mbs_per_slice, ctx->blocks[0], quant);
        }
        flush_put_bits(pb);
        sizes[i]   = put_bytes_output(pb) - total_size;
        total_size = put_bytes_output(pb);
    }
    return total_size;
}

static inline int estimate_vlc(unsigned codebook, int val)
{
    unsigned int rice_order, exp_order, switch_bits, switch_val;
    int exponent;

    /* number of prefix bits to switch between Rice and expGolomb */
    switch_bits = (codebook & 3) + 1;
    rice_order  =  codebook >> 5;       /* rice code order */
    exp_order   = (codebook >> 2) & 7;  /* exp golomb code order */

    switch_val  = switch_bits << rice_order;

    if (val >= switch_val) {
        val -= switch_val - (1 << exp_order);
        exponent = av_log2(val);

        return exponent * 2 - exp_order + switch_bits + 1;
    } else {
        return (val >> rice_order) + rice_order + 1;
    }
}

static int estimate_dcs(int *error, int16_t *blocks, int blocks_per_slice,
                        int scale)
{
    int i;
    int codebook = 5, code, dc, prev_dc, delta, sign, new_sign;
    int bits;

    prev_dc  = (blocks[0] - 0x4000) / scale;
    bits     = estimate_vlc(FIRST_DC_CB, MAKE_CODE(prev_dc));
    sign     = 0;
    blocks  += 64;
    *error  += FFABS(blocks[0] - 0x4000) % scale;

    for (i = 1; i < blocks_per_slice; i++, blocks += 64) {
        dc       = (blocks[0] - 0x4000) / scale;
        *error  += FFABS(blocks[0] - 0x4000) % scale;
        delta    = dc - prev_dc;
        new_sign = GET_SIGN(delta);
        delta    = (delta ^ sign) - sign;
        code     = MAKE_CODE(delta);
        bits    += estimate_vlc(ff_prores_dc_codebook[codebook], code);
        codebook = FFMIN(code, 6);
        sign     = new_sign;
        prev_dc  = dc;
    }

    return bits;
}

static int estimate_acs(int *error, int16_t *blocks, int blocks_per_slice,
                        const uint8_t *scan, const int16_t *qmat)
{
    int idx, i;
    int prev_run = 4;
    int prev_level = 2;
    int run, level;
    int max_coeffs, abs_level;
    int bits = 0;

    max_coeffs = blocks_per_slice << 6;
    run        = 0;

    for (i = 1; i < 64; i++) {
        for (idx = scan[i]; idx < max_coeffs; idx += 64) {
            level   = blocks[idx] / qmat[scan[i]];
            *error += FFABS(blocks[idx]) % qmat[scan[i]];
            if (level) {
                abs_level = FFABS(level);
                bits += estimate_vlc(ff_prores_run_to_cb[prev_run], run);
                bits += estimate_vlc(ff_prores_level_to_cb[prev_level],
                                     abs_level - 1) + 1;
                prev_run   = FFMIN(run, 15);
                prev_level = FFMIN(abs_level, 9);
                run    = 0;
            } else {
                run++;
            }
        }
    }

    return bits;
}

static int estimate_slice_plane(ProresContext *ctx, int *error, int plane,
                                const uint16_t *src, ptrdiff_t linesize,
                                int mbs_per_slice,
                                int blocks_per_mb,
                                const int16_t *qmat, ProresThreadData *td)
{
    int blocks_per_slice;
    int bits;

    blocks_per_slice = mbs_per_slice * blocks_per_mb;

    bits  = estimate_dcs(error, td->blocks[plane], blocks_per_slice, qmat[0]);
    bits += estimate_acs(error, td->blocks[plane], blocks_per_slice, ctx->scantable, qmat);

    return FFALIGN(bits, 8);
}

static int est_alpha_diff(int cur, int prev, int abits)
{
    const int dbits = (abits == 8) ? 4 : 7;
    const int dsize = 1 << dbits - 1;
    int diff = cur - prev;

    diff = av_zero_extend(diff, abits);
    if (diff >= (1 << abits) - dsize)
        diff -= 1 << abits;
    if (diff < -dsize || diff > dsize || !diff)
        return abits + 1;
    else
        return dbits + 1;
}

static int estimate_alpha_plane(ProresContext *ctx,
                                const uint16_t *src, ptrdiff_t linesize,
                                int mbs_per_slice, int16_t *blocks)
{
    const int abits = ctx->alpha_bits;
    const int mask  = (1 << abits) - 1;
    const int num_coeffs = mbs_per_slice * 256;
    int prev = mask, cur;
    int idx = 0;
    int run = 0;
    int bits;

    cur = blocks[idx++];
    bits = est_alpha_diff(cur, prev, abits);
    prev = cur;
    do {
        cur = blocks[idx++];
        if (cur != prev) {
            if (!run)
                bits++;
            else if (run < 0x10)
                bits += 4;
            else
                bits += 15;
            bits += est_alpha_diff(cur, prev, abits);
            prev = cur;
            run  = 0;
        } else {
            run++;
        }
    } while (idx < num_coeffs);

    if (run) {
        if (run < 0x10)
            bits += 4;
        else
            bits += 15;
    }

    return bits;
}

static int find_slice_quant(AVCodecContext *avctx,
                            int trellis_node, int x, int y, int mbs_per_slice,
                            ProresThreadData *td)
{
    ProresContext *ctx = avctx->priv_data;
    int i, q, pq, xp, yp;
    const uint16_t *src;
    int num_cblocks[MAX_PLANES], pwidth;
    int is_chroma[MAX_PLANES];
    const int min_quant = ctx->profile_info->min_quant;
    const int max_quant = ctx->profile_info->max_quant;
    int error, bits, bits_limit;
    int mbs, prev, cur, new_score;
    int slice_bits[TRELLIS_WIDTH], slice_score[TRELLIS_WIDTH];
    int overquant;
    uint16_t *qmat;
    uint16_t *qmat_chroma;
    int linesize[4], line_add;
    int alpha_bits = 0;

    if (ctx->pictures_per_frame == 1)
        line_add = 0;
    else
        line_add = ctx->cur_picture_idx ^ !(ctx->pic->flags & AV_FRAME_FLAG_TOP_FIELD_FIRST);
    mbs = x + mbs_per_slice;

    for (i = 0; i < ctx->num_planes; i++) {
        is_chroma[i]    = (i == 1 || i == 2);
        if (!is_chroma[i] || ctx->chroma_factor == CFACTOR_Y444) {
            xp             = x << 4;
            yp             = y << 4;
            num_cblocks[i] = 4;
            pwidth         = avctx->width;
        } else {
            xp             = x << 3;
            yp             = y << 4;
            num_cblocks[i] = 2;
            pwidth         = avctx->width >> 1;
        }

        linesize[i] = ctx->pic->linesize[i] * ctx->pictures_per_frame;
        src = (const uint16_t *)(ctx->pic->data[i] + yp * linesize[i] +
                                 line_add * ctx->pic->linesize[i]) + xp;

        if (i < 3) {
            get_slice_data(ctx, src, linesize[i], xp, yp,
                           pwidth, avctx->height / ctx->pictures_per_frame,
                           td->blocks[i], td->emu_buf,
                           mbs_per_slice, num_cblocks[i], is_chroma[i]);
        } else {
            get_alpha_data(ctx, src, linesize[i], xp, yp,
                           pwidth, avctx->height / ctx->pictures_per_frame,
                           td->blocks[i], mbs_per_slice, ctx->alpha_bits);
        }
    }

    for (q = min_quant; q < max_quant + 2; q++) {
        td->nodes[trellis_node + q].prev_node = -1;
        td->nodes[trellis_node + q].quant     = q;
    }

    if (ctx->alpha_bits)
        alpha_bits = estimate_alpha_plane(ctx, src, linesize[3],
                                          mbs_per_slice, td->blocks[3]);
    // todo: maybe perform coarser quantising to fit into frame size when needed
    for (q = min_quant; q <= max_quant; q++) {
        bits  = alpha_bits;
        error = 0;
        bits += estimate_slice_plane(ctx, &error, 0,
                                     src, linesize[0],
                                     mbs_per_slice,
                                     num_cblocks[0],
                                     ctx->quants[q], td); /* estimate luma plane */
        for (i = 1; i < ctx->num_planes - !!ctx->alpha_bits; i++) { /* estimate chroma plane */
            bits += estimate_slice_plane(ctx, &error, i,
                                         src, linesize[i],
                                         mbs_per_slice,
                                         num_cblocks[i],
                                         ctx->quants_chroma[q], td);
        }
        if (bits > 65000 * 8)
            error = SCORE_LIMIT;

        slice_bits[q]  = bits;
        slice_score[q] = error;
    }
    if (slice_bits[max_quant] <= ctx->bits_per_mb * mbs_per_slice) {
        slice_bits[max_quant + 1]  = slice_bits[max_quant];
        slice_score[max_quant + 1] = slice_score[max_quant] + 1;
        overquant = max_quant;
    } else {
        for (q = max_quant + 1; q < 128; q++) {
            bits  = alpha_bits;
            error = 0;
            if (q < MAX_STORED_Q) {
                qmat = ctx->quants[q];
                qmat_chroma = ctx->quants_chroma[q];
            } else {
                qmat = td->custom_q;
                qmat_chroma = td->custom_chroma_q;
                for (i = 0; i < 64; i++) {
                    qmat[i] = ctx->quant_mat[i] * q;
                    qmat_chroma[i] = ctx->quant_chroma_mat[i] * q;
                }
            }
            bits += estimate_slice_plane(ctx, &error, 0,
                                         src, linesize[0],
                                         mbs_per_slice,
                                         num_cblocks[0],
                                         qmat, td);/* estimate luma plane */
            for (i = 1; i < ctx->num_planes - !!ctx->alpha_bits; i++) { /* estimate chroma plane */
                bits += estimate_slice_plane(ctx, &error, i,
                                             src, linesize[i],
                                             mbs_per_slice,
                                             num_cblocks[i],
                                             qmat_chroma, td);
            }
            if (bits <= ctx->bits_per_mb * mbs_per_slice)
                break;
        }

        slice_bits[max_quant + 1]  = bits;
        slice_score[max_quant + 1] = error;
        overquant = q;
    }
    td->nodes[trellis_node + max_quant + 1].quant = overquant;

    bits_limit = mbs * ctx->bits_per_mb;
    for (pq = min_quant; pq < max_quant + 2; pq++) {
        prev = trellis_node - TRELLIS_WIDTH + pq;

        for (q = min_quant; q < max_quant + 2; q++) {
            cur = trellis_node + q;
            bits  = td->nodes[prev].bits + slice_bits[q];
            error = slice_score[q];
            if (bits > bits_limit)
                error = SCORE_LIMIT;

            if (td->nodes[prev].score < SCORE_LIMIT && error < SCORE_LIMIT)
                new_score = td->nodes[prev].score + error;
            else
                new_score = SCORE_LIMIT;
            if (td->nodes[cur].prev_node == -1 ||
                td->nodes[cur].score >= new_score) {

                td->nodes[cur].bits      = bits;
                td->nodes[cur].score     = new_score;
                td->nodes[cur].prev_node = prev;
            }
        }
    }

    error = td->nodes[trellis_node + min_quant].score;
    pq    = trellis_node + min_quant;
    for (q = min_quant + 1; q < max_quant + 2; q++) {
        if (td->nodes[trellis_node + q].score <= error) {
            error = td->nodes[trellis_node + q].score;
            pq    = trellis_node + q;
        }
    }

    return pq;
}

static int find_quant_thread(AVCodecContext *avctx, void *arg,
                             int jobnr, int threadnr)
{
    ProresContext *ctx = avctx->priv_data;
    ProresThreadData *td = ctx->tdata + threadnr;
    int mbs_per_slice = ctx->mbs_per_slice;
    int x, y = jobnr, mb, q = 0;

    for (x = mb = 0; x < ctx->mb_width; x += mbs_per_slice, mb++) {
        while (ctx->mb_width - x < mbs_per_slice)
            mbs_per_slice >>= 1;
        q = find_slice_quant(avctx,
                             (mb + 1) * TRELLIS_WIDTH, x, y,
                             mbs_per_slice, td);
    }

    for (x = ctx->slices_width - 1; x >= 0; x--) {
        ctx->slice_q[x + y * ctx->slices_width] = td->nodes[q].quant;
        q = td->nodes[q].prev_node;
    }

    return 0;
}

static int encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                        const AVFrame *pic, int *got_packet)
{
    ProresContext *ctx = avctx->priv_data;
    uint8_t *orig_buf, *buf, *slice_hdr, *slice_sizes, *tmp;
    uint8_t *picture_size_pos;
    PutBitContext pb;
    int x, y, i, mb, q = 0;
    int sizes[4] = { 0 };
    int slice_hdr_size = 2 * ctx->num_planes;
    int frame_size, picture_size, slice_size;
    int pkt_size, ret;
    int max_slice_size = (ctx->frame_size_upper_bound - 200) / (ctx->pictures_per_frame * ctx->slices_per_picture + 1);
    uint8_t frame_flags;

    ctx->pic = pic;
    pkt_size = ctx->frame_size_upper_bound;

    if ((ret = ff_alloc_packet(avctx, pkt, pkt_size + FF_INPUT_BUFFER_MIN_SIZE)) < 0)
        return ret;

    orig_buf = pkt->data;

    // frame atom
    orig_buf += 4;                              // frame size
    bytestream_put_be32  (&orig_buf, FRAME_ID); // frame container ID
    buf = orig_buf;

    // frame header
    tmp = buf;
    buf += 2;                                   // frame header size will be stored here
    bytestream_put_be16  (&buf, ctx->chroma_factor != CFACTOR_Y422 || ctx->alpha_bits ? 1 : 0);
    bytestream_put_buffer(&buf, ctx->vendor, 4);
    bytestream_put_be16  (&buf, avctx->width);
    bytestream_put_be16  (&buf, avctx->height);

    frame_flags = ctx->chroma_factor << 6;
    if (avctx->flags & AV_CODEC_FLAG_INTERLACED_DCT)
        frame_flags |= (pic->flags & AV_FRAME_FLAG_TOP_FIELD_FIRST) ? 0x04 : 0x08;
    bytestream_put_byte  (&buf, frame_flags);

    bytestream_put_byte  (&buf, 0);             // reserved
    bytestream_put_byte  (&buf, pic->color_primaries);
    bytestream_put_byte  (&buf, pic->color_trc);
    bytestream_put_byte  (&buf, pic->colorspace);
    bytestream_put_byte  (&buf, ctx->alpha_bits >> 3);
    bytestream_put_byte  (&buf, 0);             // reserved
    if (ctx->quant_sel != QUANT_MAT_DEFAULT) {
        bytestream_put_byte  (&buf, 0x03);      // matrix flags - both matrices are present
        bytestream_put_buffer(&buf, ctx->quant_mat, 64);        // luma quantisation matrix
        bytestream_put_buffer(&buf, ctx->quant_chroma_mat, 64); // chroma quantisation matrix
    } else {
        bytestream_put_byte  (&buf, 0x00);      // matrix flags - default matrices are used
    }
    bytestream_put_be16  (&tmp, buf - orig_buf); // write back frame header size

    for (ctx->cur_picture_idx = 0;
         ctx->cur_picture_idx < ctx->pictures_per_frame;
         ctx->cur_picture_idx++) {
        // picture header
        picture_size_pos = buf + 1;
        bytestream_put_byte  (&buf, 0x40);          // picture header size (in bits)
        buf += 4;                                   // picture data size will be stored here
        bytestream_put_be16  (&buf, ctx->slices_per_picture);
        bytestream_put_byte  (&buf, av_log2(ctx->mbs_per_slice) << 4); // slice width and height in MBs

        // seek table - will be filled during slice encoding
        slice_sizes = buf;
        buf += ctx->slices_per_picture * 2;

        // slices
        if (!ctx->force_quant) {
            ret = avctx->execute2(avctx, find_quant_thread, NULL, NULL,
                                  ctx->mb_height);
            if (ret)
                return ret;
        }

        for (y = 0; y < ctx->mb_height; y++) {
            int mbs_per_slice = ctx->mbs_per_slice;
            for (x = mb = 0; x < ctx->mb_width; x += mbs_per_slice, mb++) {
                q = ctx->force_quant ? ctx->force_quant
                                     : ctx->slice_q[mb + y * ctx->slices_width];

                while (ctx->mb_width - x < mbs_per_slice)
                    mbs_per_slice >>= 1;

                bytestream_put_byte(&buf, slice_hdr_size << 3);
                slice_hdr = buf;
                buf += slice_hdr_size - 1;
                if (pkt_size <= buf - orig_buf + 2 * max_slice_size) {
                    uint8_t *start = pkt->data;
                    // Recompute new size according to max_slice_size
                    // and deduce delta
                    int delta = 200 + (ctx->pictures_per_frame *
                                ctx->slices_per_picture + 1) *
                                max_slice_size - pkt_size;

                    delta = FFMAX(delta, 2 * max_slice_size);
                    ctx->frame_size_upper_bound += delta;

                    if (!ctx->warn) {
                        avpriv_request_sample(avctx,
                                              "Packet too small: is %i,"
                                              " needs %i (slice: %i). "
                                              "Correct allocation",
                                              pkt_size, delta, max_slice_size);
                        ctx->warn = 1;
                    }

                    ret = av_grow_packet(pkt, delta);
                    if (ret < 0)
                        return ret;

                    pkt_size += delta;
                    orig_buf         = pkt->data + (orig_buf         - start);
                    buf              = pkt->data + (buf              - start);
                    picture_size_pos = pkt->data + (picture_size_pos - start);
                    slice_sizes      = pkt->data + (slice_sizes      - start);
                    slice_hdr        = pkt->data + (slice_hdr        - start);
                    tmp              = pkt->data + (tmp              - start);
                }
                init_put_bits(&pb, buf, (pkt_size - (buf - orig_buf)));
                ret = encode_slice(avctx, pic, &pb, sizes, x, y, q,
                                   mbs_per_slice);
                if (ret < 0)
                    return ret;

                bytestream_put_byte(&slice_hdr, q);
                slice_size = slice_hdr_size + sizes[ctx->num_planes - 1];
                for (i = 0; i < ctx->num_planes - 1; i++) {
                    bytestream_put_be16(&slice_hdr, sizes[i]);
                    slice_size += sizes[i];
                }
                bytestream_put_be16(&slice_sizes, slice_size);
                buf += slice_size - slice_hdr_size;
                if (max_slice_size < slice_size)
                    max_slice_size = slice_size;
            }
        }

        picture_size = buf - (picture_size_pos - 1);
        bytestream_put_be32(&picture_size_pos, picture_size);
    }

    orig_buf -= 8;
    frame_size = buf - orig_buf;
    bytestream_put_be32(&orig_buf, frame_size);

    pkt->size   = frame_size;
    *got_packet = 1;

    return 0;
}

static av_cold int encode_close(AVCodecContext *avctx)
{
    ProresContext *ctx = avctx->priv_data;
    int i;

    if (ctx->tdata) {
        for (i = 0; i < avctx->thread_count; i++)
            av_freep(&ctx->tdata[i].nodes);
    }
    av_freep(&ctx->tdata);
    av_freep(&ctx->slice_q);

    return 0;
}

static void prores_fdct(FDCTDSPContext *fdsp, const uint16_t *src,
                        ptrdiff_t linesize, int16_t *block)
{
    int x, y;
    const uint16_t *tsrc = src;

    for (y = 0; y < 8; y++) {
        for (x = 0; x < 8; x++)
            block[y * 8 + x] = tsrc[x];
        tsrc += linesize >> 1;
    }
    fdsp->fdct(block);
}

static av_cold int encode_init(AVCodecContext *avctx)
{
    ProresContext *ctx = avctx->priv_data;
    int err = 0, i, j, min_quant, max_quant;

    err = ff_prores_kostya_encode_init(avctx, ctx, avctx->pix_fmt);
    if (err < 0)
        return err;

    ctx->fdct      = prores_fdct;
    ff_fdctdsp_init(&ctx->fdsp, avctx);

    if (!ctx->force_quant) {
        min_quant = ctx->profile_info->min_quant;
        max_quant = ctx->profile_info->max_quant;

        ctx->slice_q = av_malloc_array(ctx->slices_per_picture, sizeof(*ctx->slice_q));
        if (!ctx->slice_q)
            return AVERROR(ENOMEM);

        ctx->tdata = av_calloc(avctx->thread_count, sizeof(*ctx->tdata));
        if (!ctx->tdata)
            return AVERROR(ENOMEM);

        for (j = 0; j < avctx->thread_count; j++) {
            ctx->tdata[j].nodes = av_malloc_array(ctx->slices_width + 1,
                                                  TRELLIS_WIDTH
                                                  * sizeof(*ctx->tdata->nodes));
            if (!ctx->tdata[j].nodes)
                return AVERROR(ENOMEM);
            for (i = min_quant; i < max_quant + 2; i++) {
                ctx->tdata[j].nodes[i].prev_node = -1;
                ctx->tdata[j].nodes[i].bits      = 0;
                ctx->tdata[j].nodes[i].score     = 0;
            }
        }
    }

    return 0;
}

#define OFFSET(x) offsetof(ProresContext, x)
#define VE     AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM

static const AVOption options[] = {
    { "mbs_per_slice", "macroblocks per slice", OFFSET(mbs_per_slice),
        AV_OPT_TYPE_INT, { .i64 = 8 }, 1, MAX_MBS_PER_SLICE, VE },
    { "profile",       NULL, OFFSET(profile), AV_OPT_TYPE_INT,
        { .i64 = PRORES_PROFILE_AUTO },
        PRORES_PROFILE_AUTO, PRORES_PROFILE_4444XQ, VE, .unit = "profile" },
    { "auto",         NULL, 0, AV_OPT_TYPE_CONST, { .i64 = PRORES_PROFILE_AUTO },
        0, 0, VE, .unit = "profile" },
    { "proxy",         NULL, 0, AV_OPT_TYPE_CONST, { .i64 = PRORES_PROFILE_PROXY },
        0, 0, VE, .unit = "profile" },
    { "lt",            NULL, 0, AV_OPT_TYPE_CONST, { .i64 = PRORES_PROFILE_LT },
        0, 0, VE, .unit = "profile" },
    { "standard",      NULL, 0, AV_OPT_TYPE_CONST, { .i64 = PRORES_PROFILE_STANDARD },
        0, 0, VE, .unit = "profile" },
    { "hq",            NULL, 0, AV_OPT_TYPE_CONST, { .i64 = PRORES_PROFILE_HQ },
        0, 0, VE, .unit = "profile" },
    { "4444",          NULL, 0, AV_OPT_TYPE_CONST, { .i64 = PRORES_PROFILE_4444 },
        0, 0, VE, .unit = "profile" },
    { "4444xq",        NULL, 0, AV_OPT_TYPE_CONST, { .i64 = PRORES_PROFILE_4444XQ },
        0, 0, VE, .unit = "profile" },
    { "vendor", "vendor ID", OFFSET(vendor),
        AV_OPT_TYPE_STRING, { .str = "Lavc" }, 0, 0, VE },
    { "bits_per_mb", "desired bits per macroblock", OFFSET(bits_per_mb),
        AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 8192, VE },
    { "quant_mat", "quantiser matrix", OFFSET(quant_sel), AV_OPT_TYPE_INT,
        { .i64 = -1 }, -1, QUANT_MAT_DEFAULT, VE, .unit = "quant_mat" },
    { "auto",          NULL, 0, AV_OPT_TYPE_CONST, { .i64 = -1 },
        0, 0, VE, .unit = "quant_mat" },
    { "proxy",         NULL, 0, AV_OPT_TYPE_CONST, { .i64 = QUANT_MAT_PROXY },
        0, 0, VE, .unit = "quant_mat" },
    { "lt",            NULL, 0, AV_OPT_TYPE_CONST, { .i64 = QUANT_MAT_LT },
        0, 0, VE, .unit = "quant_mat" },
    { "standard",      NULL, 0, AV_OPT_TYPE_CONST, { .i64 = QUANT_MAT_STANDARD },
        0, 0, VE, .unit = "quant_mat" },
    { "hq",            NULL, 0, AV_OPT_TYPE_CONST, { .i64 = QUANT_MAT_HQ },
        0, 0, VE, .unit = "quant_mat" },
    { "default",       NULL, 0, AV_OPT_TYPE_CONST, { .i64 = QUANT_MAT_DEFAULT },
        0, 0, VE, .unit = "quant_mat" },
    { "alpha_bits", "bits for alpha plane", OFFSET(alpha_bits), AV_OPT_TYPE_INT,
        { .i64 = 16 }, 0, 16, VE },
    { NULL }
};

static const AVClass proresenc_class = {
    .class_name = "ProRes encoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFCodec ff_prores_ks_encoder = {
    .p.name         = "prores_ks",
    CODEC_LONG_NAME("Apple ProRes (iCodec Pro)"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_PRORES,
    .priv_data_size = sizeof(ProresContext),
    .init           = encode_init,
    .close          = encode_close,
    FF_CODEC_ENCODE_CB(encode_frame),
    .p.capabilities = AV_CODEC_CAP_SLICE_THREADS | AV_CODEC_CAP_FRAME_THREADS |
                      AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE,
    CODEC_PIXFMTS(AV_PIX_FMT_YUV422P10, AV_PIX_FMT_YUV444P10, AV_PIX_FMT_YUVA444P10),
    .color_ranges   = AVCOL_RANGE_MPEG,
    .p.priv_class   = &proresenc_class,
    .p.profiles     = NULL_IF_CONFIG_SMALL(ff_prores_profiles),
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
};
