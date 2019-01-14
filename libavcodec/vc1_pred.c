/*
 * VC-1 and WMV3 decoder
 * Copyright (c) 2011 Mashiat Sarker Shakkhar
 * Copyright (c) 2006-2007 Konstantin Shishkov
 * Partly based on vc9.c (c) 2005 Anonymous, Alex Beregszaszi, Michael Niedermayer
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
 * VC-1 and WMV3 block decoding routines
 */

#include "mathops.h"
#include "mpegutils.h"
#include "mpegvideo.h"
#include "vc1.h"
#include "vc1_pred.h"
#include "vc1data.h"

static av_always_inline int scaleforsame_x(VC1Context *v, int n /* MV */, int dir)
{
    int scaledvalue, refdist;
    int scalesame1, scalesame2;
    int scalezone1_x, zone1offset_x;
    int table_index = dir ^ v->second_field;

    if (v->s.pict_type != AV_PICTURE_TYPE_B)
        refdist = v->refdist;
    else
        refdist = dir ? v->brfd : v->frfd;
    if (refdist > 3)
        refdist = 3;
    scalesame1    = ff_vc1_field_mvpred_scales[table_index][1][refdist];
    scalesame2    = ff_vc1_field_mvpred_scales[table_index][2][refdist];
    scalezone1_x  = ff_vc1_field_mvpred_scales[table_index][3][refdist];
    zone1offset_x = ff_vc1_field_mvpred_scales[table_index][5][refdist];

    if (FFABS(n) > 255)
        scaledvalue = n;
    else {
        if (FFABS(n) < scalezone1_x)
            scaledvalue = (n * scalesame1) >> 8;
        else {
            if (n < 0)
                scaledvalue = ((n * scalesame2) >> 8) - zone1offset_x;
            else
                scaledvalue = ((n * scalesame2) >> 8) + zone1offset_x;
        }
    }
    return av_clip(scaledvalue, -v->range_x, v->range_x - 1);
}

static av_always_inline int scaleforsame_y(VC1Context *v, int i, int n /* MV */, int dir)
{
    int scaledvalue, refdist;
    int scalesame1, scalesame2;
    int scalezone1_y, zone1offset_y;
    int table_index = dir ^ v->second_field;

    if (v->s.pict_type != AV_PICTURE_TYPE_B)
        refdist = v->refdist;
    else
        refdist = dir ? v->brfd : v->frfd;
    if (refdist > 3)
        refdist = 3;
    scalesame1    = ff_vc1_field_mvpred_scales[table_index][1][refdist];
    scalesame2    = ff_vc1_field_mvpred_scales[table_index][2][refdist];
    scalezone1_y  = ff_vc1_field_mvpred_scales[table_index][4][refdist];
    zone1offset_y = ff_vc1_field_mvpred_scales[table_index][6][refdist];

    if (FFABS(n) > 63)
        scaledvalue = n;
    else {
        if (FFABS(n) < scalezone1_y)
            scaledvalue = (n * scalesame1) >> 8;
        else {
            if (n < 0)
                scaledvalue = ((n * scalesame2) >> 8) - zone1offset_y;
            else
                scaledvalue = ((n * scalesame2) >> 8) + zone1offset_y;
        }
    }

    if (v->cur_field_type && !v->ref_field_type[dir])
        return av_clip(scaledvalue, -v->range_y / 2 + 1, v->range_y / 2);
    else
        return av_clip(scaledvalue, -v->range_y / 2, v->range_y / 2 - 1);
}

static av_always_inline int scaleforopp_x(VC1Context *v, int n /* MV */)
{
    int scalezone1_x, zone1offset_x;
    int scaleopp1, scaleopp2, brfd;
    int scaledvalue;

    brfd = FFMIN(v->brfd, 3);
    scalezone1_x  = ff_vc1_b_field_mvpred_scales[3][brfd];
    zone1offset_x = ff_vc1_b_field_mvpred_scales[5][brfd];
    scaleopp1     = ff_vc1_b_field_mvpred_scales[1][brfd];
    scaleopp2     = ff_vc1_b_field_mvpred_scales[2][brfd];

    if (FFABS(n) > 255)
        scaledvalue = n;
    else {
        if (FFABS(n) < scalezone1_x)
            scaledvalue = (n * scaleopp1) >> 8;
        else {
            if (n < 0)
                scaledvalue = ((n * scaleopp2) >> 8) - zone1offset_x;
            else
                scaledvalue = ((n * scaleopp2) >> 8) + zone1offset_x;
        }
    }
    return av_clip(scaledvalue, -v->range_x, v->range_x - 1);
}

static av_always_inline int scaleforopp_y(VC1Context *v, int n /* MV */, int dir)
{
    int scalezone1_y, zone1offset_y;
    int scaleopp1, scaleopp2, brfd;
    int scaledvalue;

    brfd = FFMIN(v->brfd, 3);
    scalezone1_y  = ff_vc1_b_field_mvpred_scales[4][brfd];
    zone1offset_y = ff_vc1_b_field_mvpred_scales[6][brfd];
    scaleopp1     = ff_vc1_b_field_mvpred_scales[1][brfd];
    scaleopp2     = ff_vc1_b_field_mvpred_scales[2][brfd];

    if (FFABS(n) > 63)
        scaledvalue = n;
    else {
        if (FFABS(n) < scalezone1_y)
            scaledvalue = (n * scaleopp1) >> 8;
        else {
            if (n < 0)
                scaledvalue = ((n * scaleopp2) >> 8) - zone1offset_y;
            else
                scaledvalue = ((n * scaleopp2) >> 8) + zone1offset_y;
        }
    }
    if (v->cur_field_type && !v->ref_field_type[dir]) {
        return av_clip(scaledvalue, -v->range_y / 2 + 1, v->range_y / 2);
    } else {
        return av_clip(scaledvalue, -v->range_y / 2, v->range_y / 2 - 1);
    }
}

static av_always_inline int scaleforsame(VC1Context *v, int i, int n /* MV */,
                                         int dim, int dir)
{
    int brfd, scalesame;
    int hpel = 1 - v->s.quarter_sample;

    n >>= hpel;
    if (v->s.pict_type != AV_PICTURE_TYPE_B || v->second_field || !dir) {
        if (dim)
            n = scaleforsame_y(v, i, n, dir) * (1 << hpel);
        else
            n = scaleforsame_x(v, n, dir) * (1 << hpel);
        return n;
    }
    brfd      = FFMIN(v->brfd, 3);
    scalesame = ff_vc1_b_field_mvpred_scales[0][brfd];

    n = (n * scalesame >> 8) << hpel;
    return n;
}

static av_always_inline int scaleforopp(VC1Context *v, int n /* MV */,
                                        int dim, int dir)
{
    int refdist, scaleopp;
    int hpel = 1 - v->s.quarter_sample;

    n >>= hpel;
    if (v->s.pict_type == AV_PICTURE_TYPE_B && !v->second_field && dir == 1) {
        if (dim)
            n = scaleforopp_y(v, n, dir) << hpel;
        else
            n = scaleforopp_x(v, n) << hpel;
        return n;
    }
    if (v->s.pict_type != AV_PICTURE_TYPE_B)
        refdist = FFMIN(v->refdist, 3);
    else
        refdist = dir ? v->brfd : v->frfd;
    scaleopp = ff_vc1_field_mvpred_scales[dir ^ v->second_field][0][refdist];

    n = (n * scaleopp >> 8) * (1 << hpel);
    return n;
}

/** Predict and set motion vector
 */
void ff_vc1_pred_mv(VC1Context *v, int n, int dmv_x, int dmv_y,
                    int mv1, int r_x, int r_y, uint8_t* is_intra,
                    int pred_flag, int dir)
{
    MpegEncContext *s = &v->s;
    int xy, wrap, off = 0;
    int16_t *A, *B, *C;
    int px, py;
    int sum;
    int mixedmv_pic, num_samefield = 0, num_oppfield = 0;
    int opposite, a_f, b_f, c_f;
    int16_t field_predA[2];
    int16_t field_predB[2];
    int16_t field_predC[2];
    int a_valid, b_valid, c_valid;
    int hybridmv_thresh, y_bias = 0;

    if (v->mv_mode == MV_PMODE_MIXED_MV ||
        ((v->mv_mode == MV_PMODE_INTENSITY_COMP) && (v->mv_mode2 == MV_PMODE_MIXED_MV)))
        mixedmv_pic = 1;
    else
        mixedmv_pic = 0;
    /* scale MV difference to be quad-pel */
    if (!s->quarter_sample) {
        dmv_x *= 2;
        dmv_y *= 2;
    }

    wrap = s->b8_stride;
    xy   = s->block_index[n];

    if (s->mb_intra) {
        s->mv[0][n][0] = s->current_picture.motion_val[0][xy + v->blocks_off][0] = 0;
        s->mv[0][n][1] = s->current_picture.motion_val[0][xy + v->blocks_off][1] = 0;
        s->current_picture.motion_val[1][xy + v->blocks_off][0] = 0;
        s->current_picture.motion_val[1][xy + v->blocks_off][1] = 0;
        if (mv1) { /* duplicate motion data for 1-MV block */
            s->current_picture.motion_val[0][xy + 1 + v->blocks_off][0]        = 0;
            s->current_picture.motion_val[0][xy + 1 + v->blocks_off][1]        = 0;
            s->current_picture.motion_val[0][xy + wrap + v->blocks_off][0]     = 0;
            s->current_picture.motion_val[0][xy + wrap + v->blocks_off][1]     = 0;
            s->current_picture.motion_val[0][xy + wrap + 1 + v->blocks_off][0] = 0;
            s->current_picture.motion_val[0][xy + wrap + 1 + v->blocks_off][1] = 0;
            v->luma_mv[s->mb_x][0] = v->luma_mv[s->mb_x][1] = 0;
            s->current_picture.motion_val[1][xy + 1 + v->blocks_off][0]        = 0;
            s->current_picture.motion_val[1][xy + 1 + v->blocks_off][1]        = 0;
            s->current_picture.motion_val[1][xy + wrap + v->blocks_off][0]     = 0;
            s->current_picture.motion_val[1][xy + wrap + v->blocks_off][1]     = 0;
            s->current_picture.motion_val[1][xy + wrap + 1 + v->blocks_off][0] = 0;
            s->current_picture.motion_val[1][xy + wrap + 1 + v->blocks_off][1] = 0;
        }
        return;
    }

    a_valid = !s->first_slice_line || (n == 2 || n == 3);
    b_valid = a_valid;
    c_valid = s->mb_x || (n == 1 || n == 3);
    if (mv1) {
        if (v->field_mode && mixedmv_pic)
            off = (s->mb_x == (s->mb_width - 1)) ? -2 : 2;
        else
            off = (s->mb_x == (s->mb_width - 1)) ? -1 : 2;
        b_valid = b_valid && s->mb_width > 1;
    } else {
        //in 4-MV mode different blocks have different B predictor position
        switch (n) {
        case 0:
            if (v->res_rtm_flag)
                off = s->mb_x ? -1 : 1;
            else
                off = s->mb_x ? -1 : 2 * s->mb_width - wrap - 1;
            break;
        case 1:
            off = (s->mb_x == (s->mb_width - 1)) ? -1 : 1;
            break;
        case 2:
            off = 1;
            break;
        case 3:
            off = -1;
        }
        if (v->field_mode && s->mb_width == 1)
            b_valid = b_valid && c_valid;
    }

    if (v->field_mode) {
        a_valid = a_valid && !is_intra[xy - wrap];
        b_valid = b_valid && !is_intra[xy - wrap + off];
        c_valid = c_valid && !is_intra[xy - 1];
    }

    if (a_valid) {
        A = s->current_picture.motion_val[dir][xy - wrap + v->blocks_off];
        a_f = v->mv_f[dir][xy - wrap + v->blocks_off];
        num_oppfield  += a_f;
        num_samefield += 1 - a_f;
        field_predA[0] = A[0];
        field_predA[1] = A[1];
    } else {
        field_predA[0] = field_predA[1] = 0;
        a_f = 0;
    }
    if (b_valid) {
        B = s->current_picture.motion_val[dir][xy - wrap + off + v->blocks_off];
        b_f = v->mv_f[dir][xy - wrap + off + v->blocks_off];
        num_oppfield  += b_f;
        num_samefield += 1 - b_f;
        field_predB[0] = B[0];
        field_predB[1] = B[1];
    } else {
        field_predB[0] = field_predB[1] = 0;
        b_f = 0;
    }
    if (c_valid) {
        C = s->current_picture.motion_val[dir][xy - 1 + v->blocks_off];
        c_f = v->mv_f[dir][xy - 1 + v->blocks_off];
        num_oppfield  += c_f;
        num_samefield += 1 - c_f;
        field_predC[0] = C[0];
        field_predC[1] = C[1];
    } else {
        field_predC[0] = field_predC[1] = 0;
        c_f = 0;
    }

    if (v->field_mode) {
        if (!v->numref)
            // REFFIELD determines if the last field or the second-last field is
            // to be used as reference
            opposite = 1 - v->reffield;
        else {
            if (num_samefield <= num_oppfield)
                opposite = 1 - pred_flag;
            else
                opposite = pred_flag;
        }
    } else
        opposite = 0;
    if (opposite) {
        v->mv_f[dir][xy + v->blocks_off] = 1;
        v->ref_field_type[dir] = !v->cur_field_type;
        if (a_valid && !a_f) {
            field_predA[0] = scaleforopp(v, field_predA[0], 0, dir);
            field_predA[1] = scaleforopp(v, field_predA[1], 1, dir);
        }
        if (b_valid && !b_f) {
            field_predB[0] = scaleforopp(v, field_predB[0], 0, dir);
            field_predB[1] = scaleforopp(v, field_predB[1], 1, dir);
        }
        if (c_valid && !c_f) {
            field_predC[0] = scaleforopp(v, field_predC[0], 0, dir);
            field_predC[1] = scaleforopp(v, field_predC[1], 1, dir);
        }
    } else {
        v->mv_f[dir][xy + v->blocks_off] = 0;
        v->ref_field_type[dir] = v->cur_field_type;
        if (a_valid && a_f) {
            field_predA[0] = scaleforsame(v, n, field_predA[0], 0, dir);
            field_predA[1] = scaleforsame(v, n, field_predA[1], 1, dir);
        }
        if (b_valid && b_f) {
            field_predB[0] = scaleforsame(v, n, field_predB[0], 0, dir);
            field_predB[1] = scaleforsame(v, n, field_predB[1], 1, dir);
        }
        if (c_valid && c_f) {
            field_predC[0] = scaleforsame(v, n, field_predC[0], 0, dir);
            field_predC[1] = scaleforsame(v, n, field_predC[1], 1, dir);
        }
    }

    if (a_valid) {
        px = field_predA[0];
        py = field_predA[1];
    } else if (c_valid) {
        px = field_predC[0];
        py = field_predC[1];
    } else if (b_valid) {
        px = field_predB[0];
        py = field_predB[1];
    } else {
        px = 0;
        py = 0;
    }

    if (num_samefield + num_oppfield > 1) {
        px = mid_pred(field_predA[0], field_predB[0], field_predC[0]);
        py = mid_pred(field_predA[1], field_predB[1], field_predC[1]);
    }

    /* Pullback MV as specified in 8.3.5.3.4 */
    if (!v->field_mode) {
        int qx, qy, X, Y;
        int MV = mv1 ? -60 : -28;
        qx = (s->mb_x << 6) + ((n == 1 || n == 3) ? 32 : 0);
        qy = (s->mb_y << 6) + ((n == 2 || n == 3) ? 32 : 0);
        X  = (s->mb_width  << 6) - 4;
        Y  = (s->mb_height << 6) - 4;
        if (qx + px < MV) px = MV - qx;
        if (qy + py < MV) py = MV - qy;
        if (qx + px > X) px = X - qx;
        if (qy + py > Y) py = Y - qy;
    }

    if (!v->field_mode || s->pict_type != AV_PICTURE_TYPE_B) {
        /* Calculate hybrid prediction as specified in 8.3.5.3.5 (also 10.3.5.4.3.5) */
        hybridmv_thresh = 32;
        if (a_valid && c_valid) {
            if (is_intra[xy - wrap])
                sum = FFABS(px) + FFABS(py);
            else
                sum = FFABS(px - field_predA[0]) + FFABS(py - field_predA[1]);
            if (sum > hybridmv_thresh) {
                if (get_bits1(&s->gb)) {     // read HYBRIDPRED bit
                    px = field_predA[0];
                    py = field_predA[1];
                } else {
                    px = field_predC[0];
                    py = field_predC[1];
                }
            } else {
                if (is_intra[xy - 1])
                    sum = FFABS(px) + FFABS(py);
                else
                    sum = FFABS(px - field_predC[0]) + FFABS(py - field_predC[1]);
                if (sum > hybridmv_thresh) {
                    if (get_bits1(&s->gb)) {
                        px = field_predA[0];
                        py = field_predA[1];
                    } else {
                        px = field_predC[0];
                        py = field_predC[1];
                    }
                }
            }
        }
    }

    if (v->field_mode && v->numref)
        r_y >>= 1;
    if (v->field_mode && v->cur_field_type && v->ref_field_type[dir] == 0)
        y_bias = 1;
    /* store MV using signed modulus of MV range defined in 4.11 */
    s->mv[dir][n][0] = s->current_picture.motion_val[dir][xy + v->blocks_off][0] = ((px + dmv_x + r_x) & ((r_x << 1) - 1)) - r_x;
    s->mv[dir][n][1] = s->current_picture.motion_val[dir][xy + v->blocks_off][1] = ((py + dmv_y + r_y - y_bias) & ((r_y << 1) - 1)) - r_y + y_bias;
    if (mv1) { /* duplicate motion data for 1-MV block */
        s->current_picture.motion_val[dir][xy +    1 +     v->blocks_off][0] = s->current_picture.motion_val[dir][xy + v->blocks_off][0];
        s->current_picture.motion_val[dir][xy +    1 +     v->blocks_off][1] = s->current_picture.motion_val[dir][xy + v->blocks_off][1];
        s->current_picture.motion_val[dir][xy + wrap +     v->blocks_off][0] = s->current_picture.motion_val[dir][xy + v->blocks_off][0];
        s->current_picture.motion_val[dir][xy + wrap +     v->blocks_off][1] = s->current_picture.motion_val[dir][xy + v->blocks_off][1];
        s->current_picture.motion_val[dir][xy + wrap + 1 + v->blocks_off][0] = s->current_picture.motion_val[dir][xy + v->blocks_off][0];
        s->current_picture.motion_val[dir][xy + wrap + 1 + v->blocks_off][1] = s->current_picture.motion_val[dir][xy + v->blocks_off][1];
        v->mv_f[dir][xy +    1 + v->blocks_off] = v->mv_f[dir][xy +            v->blocks_off];
        v->mv_f[dir][xy + wrap + v->blocks_off] = v->mv_f[dir][xy + wrap + 1 + v->blocks_off] = v->mv_f[dir][xy + v->blocks_off];
    }
}

/** Predict and set motion vector for interlaced frame picture MBs
 */
void ff_vc1_pred_mv_intfr(VC1Context *v, int n, int dmv_x, int dmv_y,
                          int mvn, int r_x, int r_y, uint8_t* is_intra, int dir)
{
    MpegEncContext *s = &v->s;
    int xy, wrap, off = 0;
    int A[2], B[2], C[2];
    int px = 0, py = 0;
    int a_valid = 0, b_valid = 0, c_valid = 0;
    int field_a, field_b, field_c; // 0: same, 1: opposite
    int total_valid, num_samefield, num_oppfield;
    int pos_c, pos_b, n_adj;

    wrap = s->b8_stride;
    xy = s->block_index[n];

    if (s->mb_intra) {
        s->mv[0][n][0] = s->current_picture.motion_val[0][xy][0] = 0;
        s->mv[0][n][1] = s->current_picture.motion_val[0][xy][1] = 0;
        s->current_picture.motion_val[1][xy][0] = 0;
        s->current_picture.motion_val[1][xy][1] = 0;
        if (mvn == 1) { /* duplicate motion data for 1-MV block */
            s->current_picture.motion_val[0][xy + 1][0]        = 0;
            s->current_picture.motion_val[0][xy + 1][1]        = 0;
            s->current_picture.motion_val[0][xy + wrap][0]     = 0;
            s->current_picture.motion_val[0][xy + wrap][1]     = 0;
            s->current_picture.motion_val[0][xy + wrap + 1][0] = 0;
            s->current_picture.motion_val[0][xy + wrap + 1][1] = 0;
            v->luma_mv[s->mb_x][0] = v->luma_mv[s->mb_x][1] = 0;
            s->current_picture.motion_val[1][xy + 1][0]        = 0;
            s->current_picture.motion_val[1][xy + 1][1]        = 0;
            s->current_picture.motion_val[1][xy + wrap][0]     = 0;
            s->current_picture.motion_val[1][xy + wrap][1]     = 0;
            s->current_picture.motion_val[1][xy + wrap + 1][0] = 0;
            s->current_picture.motion_val[1][xy + wrap + 1][1] = 0;
        }
        return;
    }

    off = ((n == 0) || (n == 1)) ? 1 : -1;
    /* predict A */
    if (s->mb_x || (n == 1) || (n == 3)) {
        if ((v->blk_mv_type[xy]) // current block (MB) has a field MV
            || (!v->blk_mv_type[xy] && !v->blk_mv_type[xy - 1])) { // or both have frame MV
            A[0] = s->current_picture.motion_val[dir][xy - 1][0];
            A[1] = s->current_picture.motion_val[dir][xy - 1][1];
            a_valid = 1;
        } else { // current block has frame mv and cand. has field MV (so average)
            A[0] = (s->current_picture.motion_val[dir][xy - 1][0]
                    + s->current_picture.motion_val[dir][xy - 1 + off * wrap][0] + 1) >> 1;
            A[1] = (s->current_picture.motion_val[dir][xy - 1][1]
                    + s->current_picture.motion_val[dir][xy - 1 + off * wrap][1] + 1) >> 1;
            a_valid = 1;
        }
        if (!(n & 1) && v->is_intra[s->mb_x - 1]) {
            a_valid = 0;
            A[0] = A[1] = 0;
        }
    } else
        A[0] = A[1] = 0;
    /* Predict B and C */
    B[0] = B[1] = C[0] = C[1] = 0;
    if (n == 0 || n == 1 || v->blk_mv_type[xy]) {
        if (!s->first_slice_line) {
            if (!v->is_intra[s->mb_x - s->mb_stride]) {
                b_valid = 1;
                n_adj   = n | 2;
                pos_b   = s->block_index[n_adj] - 2 * wrap;
                if (v->blk_mv_type[pos_b] && v->blk_mv_type[xy]) {
                    n_adj = (n & 2) | (n & 1);
                }
                B[0] = s->current_picture.motion_val[dir][s->block_index[n_adj] - 2 * wrap][0];
                B[1] = s->current_picture.motion_val[dir][s->block_index[n_adj] - 2 * wrap][1];
                if (v->blk_mv_type[pos_b] && !v->blk_mv_type[xy]) {
                    B[0] = (B[0] + s->current_picture.motion_val[dir][s->block_index[n_adj ^ 2] - 2 * wrap][0] + 1) >> 1;
                    B[1] = (B[1] + s->current_picture.motion_val[dir][s->block_index[n_adj ^ 2] - 2 * wrap][1] + 1) >> 1;
                }
            }
            if (s->mb_width > 1) {
                if (!v->is_intra[s->mb_x - s->mb_stride + 1]) {
                    c_valid = 1;
                    n_adj   = 2;
                    pos_c   = s->block_index[2] - 2 * wrap + 2;
                    if (v->blk_mv_type[pos_c] && v->blk_mv_type[xy]) {
                        n_adj = n & 2;
                    }
                    C[0] = s->current_picture.motion_val[dir][s->block_index[n_adj] - 2 * wrap + 2][0];
                    C[1] = s->current_picture.motion_val[dir][s->block_index[n_adj] - 2 * wrap + 2][1];
                    if (v->blk_mv_type[pos_c] && !v->blk_mv_type[xy]) {
                        C[0] = (1 + C[0] + (s->current_picture.motion_val[dir][s->block_index[n_adj ^ 2] - 2 * wrap + 2][0])) >> 1;
                        C[1] = (1 + C[1] + (s->current_picture.motion_val[dir][s->block_index[n_adj ^ 2] - 2 * wrap + 2][1])) >> 1;
                    }
                    if (s->mb_x == s->mb_width - 1) {
                        if (!v->is_intra[s->mb_x - s->mb_stride - 1]) {
                            c_valid = 1;
                            n_adj   = 3;
                            pos_c   = s->block_index[3] - 2 * wrap - 2;
                            if (v->blk_mv_type[pos_c] && v->blk_mv_type[xy]) {
                                n_adj = n | 1;
                            }
                            C[0] = s->current_picture.motion_val[dir][s->block_index[n_adj] - 2 * wrap - 2][0];
                            C[1] = s->current_picture.motion_val[dir][s->block_index[n_adj] - 2 * wrap - 2][1];
                            if (v->blk_mv_type[pos_c] && !v->blk_mv_type[xy]) {
                                C[0] = (1 + C[0] + s->current_picture.motion_val[dir][s->block_index[1] - 2 * wrap - 2][0]) >> 1;
                                C[1] = (1 + C[1] + s->current_picture.motion_val[dir][s->block_index[1] - 2 * wrap - 2][1]) >> 1;
                            }
                        } else
                            c_valid = 0;
                    }
                }
            }
        }
    } else {
        pos_b   = s->block_index[1];
        b_valid = 1;
        B[0]    = s->current_picture.motion_val[dir][pos_b][0];
        B[1]    = s->current_picture.motion_val[dir][pos_b][1];
        pos_c   = s->block_index[0];
        c_valid = 1;
        C[0]    = s->current_picture.motion_val[dir][pos_c][0];
        C[1]    = s->current_picture.motion_val[dir][pos_c][1];
    }

    total_valid = a_valid + b_valid + c_valid;
    // check if predictor A is out of bounds
    if (!s->mb_x && !(n == 1 || n == 3)) {
        A[0] = A[1] = 0;
    }
    // check if predictor B is out of bounds
    if ((s->first_slice_line && v->blk_mv_type[xy]) || (s->first_slice_line && !(n & 2))) {
        B[0] = B[1] = C[0] = C[1] = 0;
    }
    if (!v->blk_mv_type[xy]) {
        if (s->mb_width == 1) {
            px = B[0];
            py = B[1];
        } else {
            if (total_valid >= 2) {
                px = mid_pred(A[0], B[0], C[0]);
                py = mid_pred(A[1], B[1], C[1]);
            } else if (total_valid) {
                if      (a_valid) { px = A[0]; py = A[1]; }
                else if (b_valid) { px = B[0]; py = B[1]; }
                else              { px = C[0]; py = C[1]; }
            }
        }
    } else {
        if (a_valid)
            field_a = (A[1] & 4) ? 1 : 0;
        else
            field_a = 0;
        if (b_valid)
            field_b = (B[1] & 4) ? 1 : 0;
        else
            field_b = 0;
        if (c_valid)
            field_c = (C[1] & 4) ? 1 : 0;
        else
            field_c = 0;

        num_oppfield  = field_a + field_b + field_c;
        num_samefield = total_valid - num_oppfield;
        if (total_valid == 3) {
            if ((num_samefield == 3) || (num_oppfield == 3)) {
                px = mid_pred(A[0], B[0], C[0]);
                py = mid_pred(A[1], B[1], C[1]);
            } else if (num_samefield >= num_oppfield) {
                /* take one MV from same field set depending on priority
                the check for B may not be necessary */
                px = !field_a ? A[0] : B[0];
                py = !field_a ? A[1] : B[1];
            } else {
                px =  field_a ? A[0] : B[0];
                py =  field_a ? A[1] : B[1];
            }
        } else if (total_valid == 2) {
            if (num_samefield >= num_oppfield) {
                if (!field_a && a_valid) {
                    px = A[0];
                    py = A[1];
                } else if (!field_b && b_valid) {
                    px = B[0];
                    py = B[1];
                } else /*if (c_valid)*/ {
                    av_assert1(c_valid);
                    px = C[0];
                    py = C[1];
                }
            } else {
                if (field_a && a_valid) {
                    px = A[0];
                    py = A[1];
                } else /*if (field_b && b_valid)*/ {
                    av_assert1(field_b && b_valid);
                    px = B[0];
                    py = B[1];
                }
            }
        } else if (total_valid == 1) {
            px = (a_valid) ? A[0] : ((b_valid) ? B[0] : C[0]);
            py = (a_valid) ? A[1] : ((b_valid) ? B[1] : C[1]);
        }
    }

    /* store MV using signed modulus of MV range defined in 4.11 */
    s->mv[dir][n][0] = s->current_picture.motion_val[dir][xy][0] = ((px + dmv_x + r_x) & ((r_x << 1) - 1)) - r_x;
    s->mv[dir][n][1] = s->current_picture.motion_val[dir][xy][1] = ((py + dmv_y + r_y) & ((r_y << 1) - 1)) - r_y;
    if (mvn == 1) { /* duplicate motion data for 1-MV block */
        s->current_picture.motion_val[dir][xy +    1    ][0] = s->current_picture.motion_val[dir][xy][0];
        s->current_picture.motion_val[dir][xy +    1    ][1] = s->current_picture.motion_val[dir][xy][1];
        s->current_picture.motion_val[dir][xy + wrap    ][0] = s->current_picture.motion_val[dir][xy][0];
        s->current_picture.motion_val[dir][xy + wrap    ][1] = s->current_picture.motion_val[dir][xy][1];
        s->current_picture.motion_val[dir][xy + wrap + 1][0] = s->current_picture.motion_val[dir][xy][0];
        s->current_picture.motion_val[dir][xy + wrap + 1][1] = s->current_picture.motion_val[dir][xy][1];
    } else if (mvn == 2) { /* duplicate motion data for 2-Field MV block */
        s->current_picture.motion_val[dir][xy + 1][0] = s->current_picture.motion_val[dir][xy][0];
        s->current_picture.motion_val[dir][xy + 1][1] = s->current_picture.motion_val[dir][xy][1];
        s->mv[dir][n + 1][0] = s->mv[dir][n][0];
        s->mv[dir][n + 1][1] = s->mv[dir][n][1];
    }
}

void ff_vc1_pred_b_mv(VC1Context *v, int dmv_x[2], int dmv_y[2],
                      int direct, int mvtype)
{
    MpegEncContext *s = &v->s;
    int xy, wrap, off = 0;
    int16_t *A, *B, *C;
    int px, py;
    int sum;
    int r_x, r_y;
    const uint8_t *is_intra = v->mb_type[0];

    av_assert0(!v->field_mode);

    r_x = v->range_x;
    r_y = v->range_y;
    /* scale MV difference to be quad-pel */
    if (!s->quarter_sample) {
        dmv_x[0] *= 2;
        dmv_y[0] *= 2;
        dmv_x[1] *= 2;
        dmv_y[1] *= 2;
    }

    wrap = s->b8_stride;
    xy = s->block_index[0];

    if (s->mb_intra) {
        s->current_picture.motion_val[0][xy][0] =
        s->current_picture.motion_val[0][xy][1] =
        s->current_picture.motion_val[1][xy][0] =
        s->current_picture.motion_val[1][xy][1] = 0;
        return;
    }
        if (direct && s->next_picture_ptr->field_picture)
            av_log(s->avctx, AV_LOG_WARNING, "Mixed frame/field direct mode not supported\n");

        s->mv[0][0][0] = scale_mv(s->next_picture.motion_val[1][xy][0], v->bfraction, 0, s->quarter_sample);
        s->mv[0][0][1] = scale_mv(s->next_picture.motion_val[1][xy][1], v->bfraction, 0, s->quarter_sample);
        s->mv[1][0][0] = scale_mv(s->next_picture.motion_val[1][xy][0], v->bfraction, 1, s->quarter_sample);
        s->mv[1][0][1] = scale_mv(s->next_picture.motion_val[1][xy][1], v->bfraction, 1, s->quarter_sample);

        /* Pullback predicted motion vectors as specified in 8.4.5.4 */
        s->mv[0][0][0] = av_clip(s->mv[0][0][0], -60 - (s->mb_x << 6), (s->mb_width  << 6) - 4 - (s->mb_x << 6));
        s->mv[0][0][1] = av_clip(s->mv[0][0][1], -60 - (s->mb_y << 6), (s->mb_height << 6) - 4 - (s->mb_y << 6));
        s->mv[1][0][0] = av_clip(s->mv[1][0][0], -60 - (s->mb_x << 6), (s->mb_width  << 6) - 4 - (s->mb_x << 6));
        s->mv[1][0][1] = av_clip(s->mv[1][0][1], -60 - (s->mb_y << 6), (s->mb_height << 6) - 4 - (s->mb_y << 6));
    if (direct) {
        s->current_picture.motion_val[0][xy][0] = s->mv[0][0][0];
        s->current_picture.motion_val[0][xy][1] = s->mv[0][0][1];
        s->current_picture.motion_val[1][xy][0] = s->mv[1][0][0];
        s->current_picture.motion_val[1][xy][1] = s->mv[1][0][1];
        return;
    }

    if ((mvtype == BMV_TYPE_FORWARD) || (mvtype == BMV_TYPE_INTERPOLATED)) {
        C   = s->current_picture.motion_val[0][xy - 2];
        A   = s->current_picture.motion_val[0][xy - wrap * 2];
        off = (s->mb_x == (s->mb_width - 1)) ? -2 : 2;
        B   = s->current_picture.motion_val[0][xy - wrap * 2 + off];

        if (!s->mb_x) C[0] = C[1] = 0;
        if (!s->first_slice_line) { // predictor A is not out of bounds
            if (s->mb_width == 1) {
                px = A[0];
                py = A[1];
            } else {
                px = mid_pred(A[0], B[0], C[0]);
                py = mid_pred(A[1], B[1], C[1]);
            }
        } else if (s->mb_x) { // predictor C is not out of bounds
            px = C[0];
            py = C[1];
        } else {
            px = py = 0;
        }
        /* Pullback MV as specified in 8.3.5.3.4 */
        {
            int qx, qy, X, Y;
            int sh = v->profile < PROFILE_ADVANCED ? 5 : 6;
            int MV = 4 - (1 << sh);
            qx = (s->mb_x << sh);
            qy = (s->mb_y << sh);
            X  = (s->mb_width  << sh) - 4;
            Y  = (s->mb_height << sh) - 4;
            if (qx + px < MV) px = MV - qx;
            if (qy + py < MV) py = MV - qy;
            if (qx + px > X) px = X - qx;
            if (qy + py > Y) py = Y - qy;
        }
        /* Calculate hybrid prediction as specified in 8.3.5.3.5 */
        if (0 && !s->first_slice_line && s->mb_x) {
            if (is_intra[xy - wrap])
                sum = FFABS(px) + FFABS(py);
            else
                sum = FFABS(px - A[0]) + FFABS(py - A[1]);
            if (sum > 32) {
                if (get_bits1(&s->gb)) {
                    px = A[0];
                    py = A[1];
                } else {
                    px = C[0];
                    py = C[1];
                }
            } else {
                if (is_intra[xy - 2])
                    sum = FFABS(px) + FFABS(py);
                else
                    sum = FFABS(px - C[0]) + FFABS(py - C[1]);
                if (sum > 32) {
                    if (get_bits1(&s->gb)) {
                        px = A[0];
                        py = A[1];
                    } else {
                        px = C[0];
                        py = C[1];
                    }
                }
            }
        }
        /* store MV using signed modulus of MV range defined in 4.11 */
        s->mv[0][0][0] = ((px + dmv_x[0] + r_x) & ((r_x << 1) - 1)) - r_x;
        s->mv[0][0][1] = ((py + dmv_y[0] + r_y) & ((r_y << 1) - 1)) - r_y;
    }
    if ((mvtype == BMV_TYPE_BACKWARD) || (mvtype == BMV_TYPE_INTERPOLATED)) {
        C   = s->current_picture.motion_val[1][xy - 2];
        A   = s->current_picture.motion_val[1][xy - wrap * 2];
        off = (s->mb_x == (s->mb_width - 1)) ? -2 : 2;
        B   = s->current_picture.motion_val[1][xy - wrap * 2 + off];

        if (!s->mb_x)
            C[0] = C[1] = 0;
        if (!s->first_slice_line) { // predictor A is not out of bounds
            if (s->mb_width == 1) {
                px = A[0];
                py = A[1];
            } else {
                px = mid_pred(A[0], B[0], C[0]);
                py = mid_pred(A[1], B[1], C[1]);
            }
        } else if (s->mb_x) { // predictor C is not out of bounds
            px = C[0];
            py = C[1];
        } else {
            px = py = 0;
        }
        /* Pullback MV as specified in 8.3.5.3.4 */
        {
            int qx, qy, X, Y;
            int sh = v->profile < PROFILE_ADVANCED ? 5 : 6;
            int MV = 4 - (1 << sh);
            qx = (s->mb_x << sh);
            qy = (s->mb_y << sh);
            X  = (s->mb_width  << sh) - 4;
            Y  = (s->mb_height << sh) - 4;
            if (qx + px < MV) px = MV - qx;
            if (qy + py < MV) py = MV - qy;
            if (qx + px > X) px = X - qx;
            if (qy + py > Y) py = Y - qy;
        }
        /* Calculate hybrid prediction as specified in 8.3.5.3.5 */
        if (0 && !s->first_slice_line && s->mb_x) {
            if (is_intra[xy - wrap])
                sum = FFABS(px) + FFABS(py);
            else
                sum = FFABS(px - A[0]) + FFABS(py - A[1]);
            if (sum > 32) {
                if (get_bits1(&s->gb)) {
                    px = A[0];
                    py = A[1];
                } else {
                    px = C[0];
                    py = C[1];
                }
            } else {
                if (is_intra[xy - 2])
                    sum = FFABS(px) + FFABS(py);
                else
                    sum = FFABS(px - C[0]) + FFABS(py - C[1]);
                if (sum > 32) {
                    if (get_bits1(&s->gb)) {
                        px = A[0];
                        py = A[1];
                    } else {
                        px = C[0];
                        py = C[1];
                    }
                }
            }
        }
        /* store MV using signed modulus of MV range defined in 4.11 */

        s->mv[1][0][0] = ((px + dmv_x[1] + r_x) & ((r_x << 1) - 1)) - r_x;
        s->mv[1][0][1] = ((py + dmv_y[1] + r_y) & ((r_y << 1) - 1)) - r_y;
    }
    s->current_picture.motion_val[0][xy][0] = s->mv[0][0][0];
    s->current_picture.motion_val[0][xy][1] = s->mv[0][0][1];
    s->current_picture.motion_val[1][xy][0] = s->mv[1][0][0];
    s->current_picture.motion_val[1][xy][1] = s->mv[1][0][1];
}

void ff_vc1_pred_b_mv_intfi(VC1Context *v, int n, int *dmv_x, int *dmv_y,
                            int mv1, int *pred_flag)
{
    int dir = (v->bmvtype == BMV_TYPE_BACKWARD) ? 1 : 0;
    MpegEncContext *s = &v->s;
    int mb_pos = s->mb_x + s->mb_y * s->mb_stride;

    if (v->bmvtype == BMV_TYPE_DIRECT) {
        int total_opp, k, f;
        if (s->next_picture.mb_type[mb_pos + v->mb_off] != MB_TYPE_INTRA) {
            s->mv[0][0][0] = scale_mv(s->next_picture.motion_val[1][s->block_index[0] + v->blocks_off][0],
                                      v->bfraction, 0, s->quarter_sample);
            s->mv[0][0][1] = scale_mv(s->next_picture.motion_val[1][s->block_index[0] + v->blocks_off][1],
                                      v->bfraction, 0, s->quarter_sample);
            s->mv[1][0][0] = scale_mv(s->next_picture.motion_val[1][s->block_index[0] + v->blocks_off][0],
                                      v->bfraction, 1, s->quarter_sample);
            s->mv[1][0][1] = scale_mv(s->next_picture.motion_val[1][s->block_index[0] + v->blocks_off][1],
                                      v->bfraction, 1, s->quarter_sample);

            total_opp = v->mv_f_next[0][s->block_index[0] + v->blocks_off]
                      + v->mv_f_next[0][s->block_index[1] + v->blocks_off]
                      + v->mv_f_next[0][s->block_index[2] + v->blocks_off]
                      + v->mv_f_next[0][s->block_index[3] + v->blocks_off];
            f = (total_opp > 2) ? 1 : 0;
        } else {
            s->mv[0][0][0] = s->mv[0][0][1] = 0;
            s->mv[1][0][0] = s->mv[1][0][1] = 0;
            f = 0;
        }
        v->ref_field_type[0] = v->ref_field_type[1] = v->cur_field_type ^ f;
        for (k = 0; k < 4; k++) {
            s->current_picture.motion_val[0][s->block_index[k] + v->blocks_off][0] = s->mv[0][0][0];
            s->current_picture.motion_val[0][s->block_index[k] + v->blocks_off][1] = s->mv[0][0][1];
            s->current_picture.motion_val[1][s->block_index[k] + v->blocks_off][0] = s->mv[1][0][0];
            s->current_picture.motion_val[1][s->block_index[k] + v->blocks_off][1] = s->mv[1][0][1];
            v->mv_f[0][s->block_index[k] + v->blocks_off] = f;
            v->mv_f[1][s->block_index[k] + v->blocks_off] = f;
        }
        return;
    }
    if (v->bmvtype == BMV_TYPE_INTERPOLATED) {
        ff_vc1_pred_mv(v, 0, dmv_x[0], dmv_y[0],   1, v->range_x, v->range_y, v->mb_type[0], pred_flag[0], 0);
        ff_vc1_pred_mv(v, 0, dmv_x[1], dmv_y[1],   1, v->range_x, v->range_y, v->mb_type[0], pred_flag[1], 1);
        return;
    }
    if (dir) { // backward
        ff_vc1_pred_mv(v, n, dmv_x[1], dmv_y[1], mv1, v->range_x, v->range_y, v->mb_type[0], pred_flag[1], 1);
        if (n == 3 || mv1) {
            ff_vc1_pred_mv(v, 0, dmv_x[0], dmv_y[0],   1, v->range_x, v->range_y, v->mb_type[0], 0, 0);
        }
    } else { // forward
        ff_vc1_pred_mv(v, n, dmv_x[0], dmv_y[0], mv1, v->range_x, v->range_y, v->mb_type[0], pred_flag[0], 0);
        if (n == 3 || mv1) {
            ff_vc1_pred_mv(v, 0, dmv_x[1], dmv_y[1],   1, v->range_x, v->range_y, v->mb_type[0], 0, 1);
        }
    }
}
