/*
 * Chinese AVS video (AVS1-P2, JiZhun profile) decoder.
 * Copyright (c) 2006  Stefan Gehrer <stefan.gehrer@gmx.de>
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
 * Chinese AVS video (AVS1-P2, JiZhun profile) decoder
 * @author Stefan Gehrer <stefan.gehrer@gmx.de>
 */

#include "avcodec.h"
#include "get_bits.h"
#include "golomb.h"
#include "h264chroma.h"
#include "mathops.h"
#include "cavs.h"

static const uint8_t alpha_tab[64] = {
     0,  0,  0,  0,  0,  0,  1,  1,  1,  1,  1,  2,  2,  2,  3,  3,
     4,  4,  5,  5,  6,  7,  8,  9, 10, 11, 12, 13, 15, 16, 18, 20,
    22, 24, 26, 28, 30, 33, 33, 35, 35, 36, 37, 37, 39, 39, 42, 44,
    46, 48, 50, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64
};

static const uint8_t beta_tab[64] = {
     0,  0,  0,  0,  0,  0,  1,  1,  1,  1,  1,  1,  1,  2,  2,  2,
     2,  2,  3,  3,  3,  3,  4,  4,  4,  4,  5,  5,  5,  5,  6,  6,
     6,  7,  7,  7,  8,  8,  8,  9,  9, 10, 10, 11, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 20, 21, 22, 23, 23, 24, 24, 25, 25, 26, 27
};

static const uint8_t tc_tab[64] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2,
    2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4,
    5, 5, 5, 6, 6, 6, 7, 7, 7, 7, 8, 8, 8, 9, 9, 9
};

/** mark block as unavailable, i.e. out of picture
 *  or not yet decoded */
static const cavs_vector un_mv = { 0, 0, 1, NOT_AVAIL };

static const int8_t left_modifier_l[8] = {  0, -1,  6, -1, -1, 7, 6, 7 };
static const int8_t top_modifier_l[8]  = { -1,  1,  5, -1, -1, 5, 7, 7 };
static const int8_t left_modifier_c[7] = {  5, -1,  2, -1,  6, 5, 6 };
static const int8_t top_modifier_c[7]  = {  4,  1, -1, -1,  4, 6, 6 };

/*****************************************************************************
 *
 * in-loop deblocking filter
 *
 ****************************************************************************/

static inline int get_bs(cavs_vector *mvP, cavs_vector *mvQ, int b)
{
    if ((mvP->ref == REF_INTRA) || (mvQ->ref == REF_INTRA))
        return 2;
    if ((abs(mvP->x - mvQ->x) >= 4) || (abs(mvP->y - mvQ->y) >= 4))
        return 1;
    if (b) {
        mvP += MV_BWD_OFFS;
        mvQ += MV_BWD_OFFS;
        if ((abs(mvP->x - mvQ->x) >= 4) || (abs(mvP->y - mvQ->y) >= 4))
            return 1;
    } else {
        if (mvP->ref != mvQ->ref)
            return 1;
    }
    return 0;
}

#define SET_PARAMS                                                \
    alpha = alpha_tab[av_clip(qp_avg + h->alpha_offset, 0, 63)];  \
    beta  =  beta_tab[av_clip(qp_avg + h->beta_offset,  0, 63)];  \
    tc    =    tc_tab[av_clip(qp_avg + h->alpha_offset, 0, 63)];

/**
 * in-loop deblocking filter for a single macroblock
 *
 * boundary strength (bs) mapping:
 *
 * --4---5--
 * 0   2   |
 * | 6 | 7 |
 * 1   3   |
 * ---------
 *
 */
void ff_cavs_filter(AVSContext *h, enum cavs_mb mb_type)
{
    uint8_t bs[8];
    int qp_avg, alpha, beta, tc;
    int i;

    /* save un-deblocked lines */
    h->topleft_border_y = h->top_border_y[h->mbx * 16 + 15];
    h->topleft_border_u = h->top_border_u[h->mbx * 10 + 8];
    h->topleft_border_v = h->top_border_v[h->mbx * 10 + 8];
    memcpy(&h->top_border_y[h->mbx * 16],     h->cy + 15 * h->l_stride, 16);
    memcpy(&h->top_border_u[h->mbx * 10 + 1], h->cu +  7 * h->c_stride, 8);
    memcpy(&h->top_border_v[h->mbx * 10 + 1], h->cv +  7 * h->c_stride, 8);
    for (i = 0; i < 8; i++) {
        h->left_border_y[i * 2 + 1] = *(h->cy + 15 + (i * 2 + 0) * h->l_stride);
        h->left_border_y[i * 2 + 2] = *(h->cy + 15 + (i * 2 + 1) * h->l_stride);
        h->left_border_u[i + 1]     = *(h->cu + 7  +  i          * h->c_stride);
        h->left_border_v[i + 1]     = *(h->cv + 7  +  i          * h->c_stride);
    }
    if (!h->loop_filter_disable) {
        /* determine bs */
        if (mb_type == I_8X8)
            memset(bs, 2, 8);
        else {
            memset(bs, 0, 8);
            if (ff_cavs_partition_flags[mb_type] & SPLITV) {
                bs[2] = get_bs(&h->mv[MV_FWD_X0], &h->mv[MV_FWD_X1], mb_type > P_8X8);
                bs[3] = get_bs(&h->mv[MV_FWD_X2], &h->mv[MV_FWD_X3], mb_type > P_8X8);
            }
            if (ff_cavs_partition_flags[mb_type] & SPLITH) {
                bs[6] = get_bs(&h->mv[MV_FWD_X0], &h->mv[MV_FWD_X2], mb_type > P_8X8);
                bs[7] = get_bs(&h->mv[MV_FWD_X1], &h->mv[MV_FWD_X3], mb_type > P_8X8);
            }
            bs[0] = get_bs(&h->mv[MV_FWD_A1], &h->mv[MV_FWD_X0], mb_type > P_8X8);
            bs[1] = get_bs(&h->mv[MV_FWD_A3], &h->mv[MV_FWD_X2], mb_type > P_8X8);
            bs[4] = get_bs(&h->mv[MV_FWD_B2], &h->mv[MV_FWD_X0], mb_type > P_8X8);
            bs[5] = get_bs(&h->mv[MV_FWD_B3], &h->mv[MV_FWD_X1], mb_type > P_8X8);
        }
        if (AV_RN64(bs)) {
            if (h->flags & A_AVAIL) {
                qp_avg = (h->qp + h->left_qp + 1) >> 1;
                SET_PARAMS;
                h->cdsp.cavs_filter_lv(h->cy, h->l_stride, alpha, beta, tc, bs[0], bs[1]);
                h->cdsp.cavs_filter_cv(h->cu, h->c_stride, alpha, beta, tc, bs[0], bs[1]);
                h->cdsp.cavs_filter_cv(h->cv, h->c_stride, alpha, beta, tc, bs[0], bs[1]);
            }
            qp_avg = h->qp;
            SET_PARAMS;
            h->cdsp.cavs_filter_lv(h->cy + 8,               h->l_stride, alpha, beta, tc, bs[2], bs[3]);
            h->cdsp.cavs_filter_lh(h->cy + 8 * h->l_stride, h->l_stride, alpha, beta, tc, bs[6], bs[7]);

            if (h->flags & B_AVAIL) {
                qp_avg = (h->qp + h->top_qp[h->mbx] + 1) >> 1;
                SET_PARAMS;
                h->cdsp.cavs_filter_lh(h->cy, h->l_stride, alpha, beta, tc, bs[4], bs[5]);
                h->cdsp.cavs_filter_ch(h->cu, h->c_stride, alpha, beta, tc, bs[4], bs[5]);
                h->cdsp.cavs_filter_ch(h->cv, h->c_stride, alpha, beta, tc, bs[4], bs[5]);
            }
        }
    }
    h->left_qp        = h->qp;
    h->top_qp[h->mbx] = h->qp;
}

#undef SET_PARAMS

/*****************************************************************************
 *
 * spatial intra prediction
 *
 ****************************************************************************/

void ff_cavs_load_intra_pred_luma(AVSContext *h, uint8_t *top,
                                  uint8_t **left, int block)
{
    int i;

    switch (block) {
    case 0:
        *left               = h->left_border_y;
        h->left_border_y[0] = h->left_border_y[1];
        memset(&h->left_border_y[17], h->left_border_y[16], 9);
        memcpy(&top[1], &h->top_border_y[h->mbx * 16], 16);
        top[17] = top[16];
        top[0]  = top[1];
        if ((h->flags & A_AVAIL) && (h->flags & B_AVAIL))
            h->left_border_y[0] = top[0] = h->topleft_border_y;
        break;
    case 1:
        *left = h->intern_border_y;
        for (i = 0; i < 8; i++)
            h->intern_border_y[i + 1] = *(h->cy + 7 + i * h->l_stride);
        memset(&h->intern_border_y[9], h->intern_border_y[8], 9);
        h->intern_border_y[0] = h->intern_border_y[1];
        memcpy(&top[1], &h->top_border_y[h->mbx * 16 + 8], 8);
        if (h->flags & C_AVAIL)
            memcpy(&top[9], &h->top_border_y[(h->mbx + 1) * 16], 8);
        else
            memset(&top[9], top[8], 9);
        top[17] = top[16];
        top[0]  = top[1];
        if (h->flags & B_AVAIL)
            h->intern_border_y[0] = top[0] = h->top_border_y[h->mbx * 16 + 7];
        break;
    case 2:
        *left = &h->left_border_y[8];
        memcpy(&top[1], h->cy + 7 * h->l_stride, 16);
        top[17] = top[16];
        top[0]  = top[1];
        if (h->flags & A_AVAIL)
            top[0] = h->left_border_y[8];
        break;
    case 3:
        *left = &h->intern_border_y[8];
        for (i = 0; i < 8; i++)
            h->intern_border_y[i + 9] = *(h->cy + 7 + (i + 8) * h->l_stride);
        memset(&h->intern_border_y[17], h->intern_border_y[16], 9);
        memcpy(&top[0], h->cy + 7 + 7 * h->l_stride, 9);
        memset(&top[9], top[8], 9);
        break;
    }
}

void ff_cavs_load_intra_pred_chroma(AVSContext *h)
{
    /* extend borders by one pixel */
    h->left_border_u[9]              = h->left_border_u[8];
    h->left_border_v[9]              = h->left_border_v[8];
    h->top_border_u[h->mbx * 10 + 9] = h->top_border_u[h->mbx * 10 + 8];
    h->top_border_v[h->mbx * 10 + 9] = h->top_border_v[h->mbx * 10 + 8];
    if (h->mbx && h->mby) {
        h->top_border_u[h->mbx * 10] = h->left_border_u[0] = h->topleft_border_u;
        h->top_border_v[h->mbx * 10] = h->left_border_v[0] = h->topleft_border_v;
    } else {
        h->left_border_u[0]          = h->left_border_u[1];
        h->left_border_v[0]          = h->left_border_v[1];
        h->top_border_u[h->mbx * 10] = h->top_border_u[h->mbx * 10 + 1];
        h->top_border_v[h->mbx * 10] = h->top_border_v[h->mbx * 10 + 1];
    }
}

static void intra_pred_vert(uint8_t *d, uint8_t *top, uint8_t *left, int stride)
{
    int y;
    uint64_t a = AV_RN64(&top[1]);
    for (y = 0; y < 8; y++)
        *((uint64_t *)(d + y * stride)) = a;
}

static void intra_pred_horiz(uint8_t *d, uint8_t *top, uint8_t *left, int stride)
{
    int y;
    uint64_t a;
    for (y = 0; y < 8; y++) {
        a = left[y + 1] * 0x0101010101010101ULL;
        *((uint64_t *)(d + y * stride)) = a;
    }
}

static void intra_pred_dc_128(uint8_t *d, uint8_t *top, uint8_t *left, int stride)
{
    int y;
    uint64_t a = 0x8080808080808080ULL;
    for (y = 0; y < 8; y++)
        *((uint64_t *)(d + y * stride)) = a;
}

static void intra_pred_plane(uint8_t *d, uint8_t *top, uint8_t *left, int stride)
{
    int x, y, ia;
    int ih = 0;
    int iv = 0;
    const uint8_t *cm = ff_cropTbl + MAX_NEG_CROP;

    for (x = 0; x < 4; x++) {
        ih += (x + 1) *  (top[5 + x] -  top[3 - x]);
        iv += (x + 1) * (left[5 + x] - left[3 - x]);
    }
    ia = (top[8] + left[8]) << 4;
    ih = (17 * ih + 16) >> 5;
    iv = (17 * iv + 16) >> 5;
    for (y = 0; y < 8; y++)
        for (x = 0; x < 8; x++)
            d[y * stride + x] = cm[(ia + (x - 3) * ih + (y - 3) * iv + 16) >> 5];
}

#define LOWPASS(ARRAY, INDEX)                                           \
    ((ARRAY[(INDEX) - 1] + 2 * ARRAY[(INDEX)] + ARRAY[(INDEX) + 1] + 2) >> 2)

static void intra_pred_lp(uint8_t *d, uint8_t *top, uint8_t *left, int stride)
{
    int x, y;
    for (y = 0; y < 8; y++)
        for (x = 0; x < 8; x++)
            d[y * stride + x] = (LOWPASS(top, x + 1) + LOWPASS(left, y + 1)) >> 1;
}

static void intra_pred_down_left(uint8_t *d, uint8_t *top, uint8_t *left, int stride)
{
    int x, y;
    for (y = 0; y < 8; y++)
        for (x = 0; x < 8; x++)
            d[y * stride + x] = (LOWPASS(top, x + y + 2) + LOWPASS(left, x + y + 2)) >> 1;
}

static void intra_pred_down_right(uint8_t *d, uint8_t *top, uint8_t *left, int stride)
{
    int x, y;
    for (y = 0; y < 8; y++)
        for (x = 0; x < 8; x++)
            if (x == y)
                d[y * stride + x] = (left[1] + 2 * top[0] + top[1] + 2) >> 2;
            else if (x > y)
                d[y * stride + x] = LOWPASS(top, x - y);
            else
                d[y * stride + x] = LOWPASS(left, y - x);
}

static void intra_pred_lp_left(uint8_t *d, uint8_t *top, uint8_t *left, int stride)
{
    int x, y;
    for (y = 0; y < 8; y++)
        for (x = 0; x < 8; x++)
            d[y * stride + x] = LOWPASS(left, y + 1);
}

static void intra_pred_lp_top(uint8_t *d, uint8_t *top, uint8_t *left, int stride)
{
    int x, y;
    for (y = 0; y < 8; y++)
        for (x = 0; x < 8; x++)
            d[y * stride + x] = LOWPASS(top, x + 1);
}

#undef LOWPASS

static inline void modify_pred(const int8_t *mod_table, int *mode)
{
    *mode = mod_table[*mode];
    if (*mode < 0) {
        av_log(NULL, AV_LOG_ERROR, "Illegal intra prediction mode\n");
        *mode = 0;
    }
}

void ff_cavs_modify_mb_i(AVSContext *h, int *pred_mode_uv)
{
    /* save pred modes before they get modified */
    h->pred_mode_Y[3]             = h->pred_mode_Y[5];
    h->pred_mode_Y[6]             = h->pred_mode_Y[8];
    h->top_pred_Y[h->mbx * 2 + 0] = h->pred_mode_Y[7];
    h->top_pred_Y[h->mbx * 2 + 1] = h->pred_mode_Y[8];

    /* modify pred modes according to availability of neighbour samples */
    if (!(h->flags & A_AVAIL)) {
        modify_pred(left_modifier_l, &h->pred_mode_Y[4]);
        modify_pred(left_modifier_l, &h->pred_mode_Y[7]);
        modify_pred(left_modifier_c, pred_mode_uv);
    }
    if (!(h->flags & B_AVAIL)) {
        modify_pred(top_modifier_l, &h->pred_mode_Y[4]);
        modify_pred(top_modifier_l, &h->pred_mode_Y[5]);
        modify_pred(top_modifier_c, pred_mode_uv);
    }
}

/*****************************************************************************
 *
 * motion compensation
 *
 ****************************************************************************/

static inline void mc_dir_part(AVSContext *h, AVFrame *pic, int chroma_height,
                               int delta, int list, uint8_t *dest_y,
                               uint8_t *dest_cb, uint8_t *dest_cr,
                               int src_x_offset, int src_y_offset,
                               qpel_mc_func *qpix_op,
                               h264_chroma_mc_func chroma_op, cavs_vector *mv)
{
    const int mx         = mv->x + src_x_offset * 8;
    const int my         = mv->y + src_y_offset * 8;
    const int luma_xy    = (mx & 3) + ((my & 3) << 2);
    uint8_t *src_y       = pic->data[0] + (mx >> 2) + (my >> 2) * h->l_stride;
    uint8_t *src_cb      = pic->data[1] + (mx >> 3) + (my >> 3) * h->c_stride;
    uint8_t *src_cr      = pic->data[2] + (mx >> 3) + (my >> 3) * h->c_stride;
    int extra_width      = 0;
    int extra_height     = extra_width;
    const int full_mx    = mx >> 2;
    const int full_my    = my >> 2;
    const int pic_width  = 16 * h->mb_width;
    const int pic_height = 16 * h->mb_height;
    int emu = 0;

    if (!pic->data[0])
        return;
    if (mx & 7)
        extra_width  -= 3;
    if (my & 7)
        extra_height -= 3;

    if (full_mx < 0 - extra_width ||
        full_my < 0 - extra_height ||
        full_mx + 16 /* FIXME */ > pic_width + extra_width ||
        full_my + 16 /* FIXME */ > pic_height + extra_height) {
        h->vdsp.emulated_edge_mc(h->edge_emu_buffer,
                                 src_y - 2 - 2 * h->l_stride,
                                 h->l_stride, h->l_stride,
                                 16 + 5, 16 + 5 /* FIXME */,
                                 full_mx - 2, full_my - 2,
                                 pic_width, pic_height);
        src_y = h->edge_emu_buffer + 2 + 2 * h->l_stride;
        emu   = 1;
    }

    // FIXME try variable height perhaps?
    qpix_op[luma_xy](dest_y, src_y, h->l_stride);

    if (emu) {
        h->vdsp.emulated_edge_mc(h->edge_emu_buffer, src_cb,
                                 h->c_stride, h->c_stride,
                                 9, 9 /* FIXME */,
                                 mx >> 3, my >> 3,
                                 pic_width >> 1, pic_height >> 1);
        src_cb = h->edge_emu_buffer;
    }
    chroma_op(dest_cb, src_cb, h->c_stride, chroma_height, mx & 7, my & 7);

    if (emu) {
        h->vdsp.emulated_edge_mc(h->edge_emu_buffer, src_cr,
                                 h->c_stride, h->c_stride,
                                 9, 9 /* FIXME */,
                                 mx >> 3, my >> 3,
                                 pic_width >> 1, pic_height >> 1);
        src_cr = h->edge_emu_buffer;
    }
    chroma_op(dest_cr, src_cr, h->c_stride, chroma_height, mx & 7, my & 7);
}

static inline void mc_part_std(AVSContext *h, int chroma_height, int delta,
                               uint8_t *dest_y,
                               uint8_t *dest_cb,
                               uint8_t *dest_cr,
                               int x_offset, int y_offset,
                               qpel_mc_func *qpix_put,
                               h264_chroma_mc_func chroma_put,
                               qpel_mc_func *qpix_avg,
                               h264_chroma_mc_func chroma_avg,
                               cavs_vector *mv)
{
    qpel_mc_func *qpix_op =  qpix_put;
    h264_chroma_mc_func chroma_op = chroma_put;

    dest_y   += x_offset * 2 + y_offset * h->l_stride * 2;
    dest_cb  += x_offset     + y_offset * h->c_stride;
    dest_cr  += x_offset     + y_offset * h->c_stride;
    x_offset += 8 * h->mbx;
    y_offset += 8 * h->mby;

    if (mv->ref >= 0) {
        AVFrame *ref = h->DPB[mv->ref].f;
        mc_dir_part(h, ref, chroma_height, delta, 0,
                    dest_y, dest_cb, dest_cr, x_offset, y_offset,
                    qpix_op, chroma_op, mv);

        qpix_op   = qpix_avg;
        chroma_op = chroma_avg;
    }

    if ((mv + MV_BWD_OFFS)->ref >= 0) {
        AVFrame *ref = h->DPB[0].f;
        mc_dir_part(h, ref, chroma_height, delta, 1,
                    dest_y, dest_cb, dest_cr, x_offset, y_offset,
                    qpix_op, chroma_op, mv + MV_BWD_OFFS);
    }
}

void ff_cavs_inter(AVSContext *h, enum cavs_mb mb_type)
{
    if (ff_cavs_partition_flags[mb_type] == 0) { // 16x16
        mc_part_std(h, 8, 0, h->cy, h->cu, h->cv, 0, 0,
                    h->cdsp.put_cavs_qpel_pixels_tab[0],
                    h->h264chroma.put_h264_chroma_pixels_tab[0],
                    h->cdsp.avg_cavs_qpel_pixels_tab[0],
                    h->h264chroma.avg_h264_chroma_pixels_tab[0],
                    &h->mv[MV_FWD_X0]);
    } else {
        mc_part_std(h, 4, 0, h->cy, h->cu, h->cv, 0, 0,
                    h->cdsp.put_cavs_qpel_pixels_tab[1],
                    h->h264chroma.put_h264_chroma_pixels_tab[1],
                    h->cdsp.avg_cavs_qpel_pixels_tab[1],
                    h->h264chroma.avg_h264_chroma_pixels_tab[1],
                    &h->mv[MV_FWD_X0]);
        mc_part_std(h, 4, 0, h->cy, h->cu, h->cv, 4, 0,
                    h->cdsp.put_cavs_qpel_pixels_tab[1],
                    h->h264chroma.put_h264_chroma_pixels_tab[1],
                    h->cdsp.avg_cavs_qpel_pixels_tab[1],
                    h->h264chroma.avg_h264_chroma_pixels_tab[1],
                    &h->mv[MV_FWD_X1]);
        mc_part_std(h, 4, 0, h->cy, h->cu, h->cv, 0, 4,
                    h->cdsp.put_cavs_qpel_pixels_tab[1],
                    h->h264chroma.put_h264_chroma_pixels_tab[1],
                    h->cdsp.avg_cavs_qpel_pixels_tab[1],
                    h->h264chroma.avg_h264_chroma_pixels_tab[1],
                    &h->mv[MV_FWD_X2]);
        mc_part_std(h, 4, 0, h->cy, h->cu, h->cv, 4, 4,
                    h->cdsp.put_cavs_qpel_pixels_tab[1],
                    h->h264chroma.put_h264_chroma_pixels_tab[1],
                    h->cdsp.avg_cavs_qpel_pixels_tab[1],
                    h->h264chroma.avg_h264_chroma_pixels_tab[1],
                    &h->mv[MV_FWD_X3]);
    }
}

/*****************************************************************************
 *
 * motion vector prediction
 *
 ****************************************************************************/

static inline void scale_mv(AVSContext *h, int *d_x, int *d_y,
                            cavs_vector *src, int distp)
{
    int den = h->scale_den[src->ref];

    *d_x = (src->x * distp * den + 256 + (src->x >> 31)) >> 9;
    *d_y = (src->y * distp * den + 256 + (src->y >> 31)) >> 9;
}

static inline void mv_pred_median(AVSContext *h,
                                  cavs_vector *mvP,
                                  cavs_vector *mvA,
                                  cavs_vector *mvB,
                                  cavs_vector *mvC)
{
    int ax, ay, bx, by, cx, cy;
    int len_ab, len_bc, len_ca, len_mid;

    /* scale candidates according to their temporal span */
    scale_mv(h, &ax, &ay, mvA, mvP->dist);
    scale_mv(h, &bx, &by, mvB, mvP->dist);
    scale_mv(h, &cx, &cy, mvC, mvP->dist);
    /* find the geometrical median of the three candidates */
    len_ab  = abs(ax - bx) + abs(ay - by);
    len_bc  = abs(bx - cx) + abs(by - cy);
    len_ca  = abs(cx - ax) + abs(cy - ay);
    len_mid = mid_pred(len_ab, len_bc, len_ca);
    if (len_mid == len_ab) {
        mvP->x = cx;
        mvP->y = cy;
    } else if (len_mid == len_bc) {
        mvP->x = ax;
        mvP->y = ay;
    } else {
        mvP->x = bx;
        mvP->y = by;
    }
}

void ff_cavs_mv(AVSContext *h, enum cavs_mv_loc nP, enum cavs_mv_loc nC,
                enum cavs_mv_pred mode, enum cavs_block size, int ref)
{
    cavs_vector *mvP = &h->mv[nP];
    cavs_vector *mvA = &h->mv[nP-1];
    cavs_vector *mvB = &h->mv[nP-4];
    cavs_vector *mvC = &h->mv[nC];
    const cavs_vector *mvP2 = NULL;

    mvP->ref  = ref;
    mvP->dist = h->dist[mvP->ref];
    if (mvC->ref == NOT_AVAIL)
        mvC = &h->mv[nP - 5];  // set to top-left (mvD)
    if (mode == MV_PRED_PSKIP &&
        (mvA->ref == NOT_AVAIL ||
         mvB->ref == NOT_AVAIL ||
         (mvA->x | mvA->y | mvA->ref) == 0 ||
         (mvB->x | mvB->y | mvB->ref) == 0)) {
        mvP2 = &un_mv;
    /* if there is only one suitable candidate, take it */
    } else if (mvA->ref >= 0 && mvB->ref < 0  && mvC->ref < 0) {
        mvP2 = mvA;
    } else if (mvA->ref < 0  && mvB->ref >= 0 && mvC->ref < 0) {
        mvP2 = mvB;
    } else if (mvA->ref < 0  && mvB->ref < 0  && mvC->ref >= 0) {
        mvP2 = mvC;
    } else if (mode == MV_PRED_LEFT     && mvA->ref == ref) {
        mvP2 = mvA;
    } else if (mode == MV_PRED_TOP      && mvB->ref == ref) {
        mvP2 = mvB;
    } else if (mode == MV_PRED_TOPRIGHT && mvC->ref == ref) {
        mvP2 = mvC;
    }
    if (mvP2) {
        mvP->x = mvP2->x;
        mvP->y = mvP2->y;
    } else
        mv_pred_median(h, mvP, mvA, mvB, mvC);

    if (mode < MV_PRED_PSKIP) {
        mvP->x += get_se_golomb(&h->gb);
        mvP->y += get_se_golomb(&h->gb);
    }
    set_mvs(mvP, size);
}

/*****************************************************************************
 *
 * macroblock level
 *
 ****************************************************************************/

/**
 * initialise predictors for motion vectors and intra prediction
 */
void ff_cavs_init_mb(AVSContext *h)
{
    int i;

    /* copy predictors from top line (MB B and C) into cache */
    for (i = 0; i < 3; i++) {
        h->mv[MV_FWD_B2 + i] = h->top_mv[0][h->mbx * 2 + i];
        h->mv[MV_BWD_B2 + i] = h->top_mv[1][h->mbx * 2 + i];
    }
    h->pred_mode_Y[1] = h->top_pred_Y[h->mbx * 2 + 0];
    h->pred_mode_Y[2] = h->top_pred_Y[h->mbx * 2 + 1];
    /* clear top predictors if MB B is not available */
    if (!(h->flags & B_AVAIL)) {
        h->mv[MV_FWD_B2]  = un_mv;
        h->mv[MV_FWD_B3]  = un_mv;
        h->mv[MV_BWD_B2]  = un_mv;
        h->mv[MV_BWD_B3]  = un_mv;
        h->pred_mode_Y[1] = h->pred_mode_Y[2] = NOT_AVAIL;
        h->flags         &= ~(C_AVAIL | D_AVAIL);
    } else if (h->mbx) {
        h->flags |= D_AVAIL;
    }
    if (h->mbx == h->mb_width - 1) // MB C not available
        h->flags &= ~C_AVAIL;
    /* clear top-right predictors if MB C is not available */
    if (!(h->flags & C_AVAIL)) {
        h->mv[MV_FWD_C2] = un_mv;
        h->mv[MV_BWD_C2] = un_mv;
    }
    /* clear top-left predictors if MB D is not available */
    if (!(h->flags & D_AVAIL)) {
        h->mv[MV_FWD_D3] = un_mv;
        h->mv[MV_BWD_D3] = un_mv;
    }
}

/**
 * save predictors for later macroblocks and increase
 * macroblock address
 * @return 0 if end of frame is reached, 1 otherwise
 */
int ff_cavs_next_mb(AVSContext *h)
{
    int i;

    h->flags |= A_AVAIL;
    h->cy    += 16;
    h->cu    += 8;
    h->cv    += 8;
    /* copy mvs as predictors to the left */
    for (i = 0; i <= 20; i += 4)
        h->mv[i] = h->mv[i + 2];
    /* copy bottom mvs from cache to top line */
    h->top_mv[0][h->mbx * 2 + 0] = h->mv[MV_FWD_X2];
    h->top_mv[0][h->mbx * 2 + 1] = h->mv[MV_FWD_X3];
    h->top_mv[1][h->mbx * 2 + 0] = h->mv[MV_BWD_X2];
    h->top_mv[1][h->mbx * 2 + 1] = h->mv[MV_BWD_X3];
    /* next MB address */
    h->mbidx++;
    h->mbx++;
    if (h->mbx == h->mb_width) { // New mb line
        h->flags = B_AVAIL | C_AVAIL;
        /* clear left pred_modes */
        h->pred_mode_Y[3] = h->pred_mode_Y[6] = NOT_AVAIL;
        /* clear left mv predictors */
        for (i = 0; i <= 20; i += 4)
            h->mv[i] = un_mv;
        h->mbx = 0;
        h->mby++;
        /* re-calculate sample pointers */
        h->cy = h->cur.f->data[0] + h->mby * 16 * h->l_stride;
        h->cu = h->cur.f->data[1] + h->mby * 8 * h->c_stride;
        h->cv = h->cur.f->data[2] + h->mby * 8 * h->c_stride;
        if (h->mby == h->mb_height) { // Frame end
            return 0;
        }
    }
    return 1;
}

/*****************************************************************************
 *
 * frame level
 *
 ****************************************************************************/

void ff_cavs_init_pic(AVSContext *h)
{
    int i;

    /* clear some predictors */
    for (i = 0; i <= 20; i += 4)
        h->mv[i] = un_mv;
    h->mv[MV_BWD_X0] = ff_cavs_dir_mv;
    set_mvs(&h->mv[MV_BWD_X0], BLK_16X16);
    h->mv[MV_FWD_X0] = ff_cavs_dir_mv;
    set_mvs(&h->mv[MV_FWD_X0], BLK_16X16);
    h->pred_mode_Y[3] = h->pred_mode_Y[6] = NOT_AVAIL;
    h->cy             = h->cur.f->data[0];
    h->cu             = h->cur.f->data[1];
    h->cv             = h->cur.f->data[2];
    h->l_stride       = h->cur.f->linesize[0];
    h->c_stride       = h->cur.f->linesize[1];
    h->luma_scan[2]   = 8 * h->l_stride;
    h->luma_scan[3]   = 8 * h->l_stride + 8;
    h->mbx            = h->mby = h->mbidx = 0;
    h->flags          = 0;
}

/*****************************************************************************
 *
 * headers and interface
 *
 ****************************************************************************/

/**
 * some predictions require data from the top-neighbouring macroblock.
 * this data has to be stored for one complete row of macroblocks
 * and this storage space is allocated here
 */
void ff_cavs_init_top_lines(AVSContext *h)
{
    /* alloc top line of predictors */
    h->top_qp       = av_mallocz(h->mb_width);
    h->top_mv[0]    = av_mallocz((h->mb_width * 2 + 1) * sizeof(cavs_vector));
    h->top_mv[1]    = av_mallocz((h->mb_width * 2 + 1) * sizeof(cavs_vector));
    h->top_pred_Y   = av_mallocz(h->mb_width * 2 * sizeof(*h->top_pred_Y));
    h->top_border_y = av_mallocz((h->mb_width + 1) * 16);
    h->top_border_u = av_mallocz(h->mb_width * 10);
    h->top_border_v = av_mallocz(h->mb_width * 10);

    /* alloc space for co-located MVs and types */
    h->col_mv        = av_mallocz(h->mb_width * h->mb_height * 4 *
                                  sizeof(cavs_vector));
    h->col_type_base = av_mallocz(h->mb_width * h->mb_height);
    h->block         = av_mallocz(64 * sizeof(int16_t));
}

av_cold int ff_cavs_init(AVCodecContext *avctx)
{
    AVSContext *h = avctx->priv_data;

    ff_dsputil_init(&h->dsp, avctx);
    ff_h264chroma_init(&h->h264chroma, 8);
    ff_videodsp_init(&h->vdsp, 8);
    ff_cavsdsp_init(&h->cdsp, avctx);
    ff_init_scantable_permutation(h->dsp.idct_permutation,
                                  h->cdsp.idct_perm);
    ff_init_scantable(h->dsp.idct_permutation, &h->scantable, ff_zigzag_direct);

    h->avctx       = avctx;
    avctx->pix_fmt = AV_PIX_FMT_YUV420P;

    h->cur.f    = av_frame_alloc();
    h->DPB[0].f = av_frame_alloc();
    h->DPB[1].f = av_frame_alloc();
    if (!h->cur.f || !h->DPB[0].f || !h->DPB[1].f) {
        ff_cavs_end(avctx);
        return AVERROR(ENOMEM);
    }

    h->luma_scan[0]                     = 0;
    h->luma_scan[1]                     = 8;
    h->intra_pred_l[INTRA_L_VERT]       = intra_pred_vert;
    h->intra_pred_l[INTRA_L_HORIZ]      = intra_pred_horiz;
    h->intra_pred_l[INTRA_L_LP]         = intra_pred_lp;
    h->intra_pred_l[INTRA_L_DOWN_LEFT]  = intra_pred_down_left;
    h->intra_pred_l[INTRA_L_DOWN_RIGHT] = intra_pred_down_right;
    h->intra_pred_l[INTRA_L_LP_LEFT]    = intra_pred_lp_left;
    h->intra_pred_l[INTRA_L_LP_TOP]     = intra_pred_lp_top;
    h->intra_pred_l[INTRA_L_DC_128]     = intra_pred_dc_128;
    h->intra_pred_c[INTRA_C_LP]         = intra_pred_lp;
    h->intra_pred_c[INTRA_C_HORIZ]      = intra_pred_horiz;
    h->intra_pred_c[INTRA_C_VERT]       = intra_pred_vert;
    h->intra_pred_c[INTRA_C_PLANE]      = intra_pred_plane;
    h->intra_pred_c[INTRA_C_LP_LEFT]    = intra_pred_lp_left;
    h->intra_pred_c[INTRA_C_LP_TOP]     = intra_pred_lp_top;
    h->intra_pred_c[INTRA_C_DC_128]     = intra_pred_dc_128;
    h->mv[7]                            = un_mv;
    h->mv[19]                           = un_mv;
    return 0;
}

av_cold int ff_cavs_end(AVCodecContext *avctx)
{
    AVSContext *h = avctx->priv_data;

    av_frame_free(&h->cur.f);
    av_frame_free(&h->DPB[0].f);
    av_frame_free(&h->DPB[1].f);

    av_free(h->top_qp);
    av_free(h->top_mv[0]);
    av_free(h->top_mv[1]);
    av_free(h->top_pred_Y);
    av_free(h->top_border_y);
    av_free(h->top_border_u);
    av_free(h->top_border_v);
    av_free(h->col_mv);
    av_free(h->col_type_base);
    av_free(h->block);
    av_freep(&h->edge_emu_buffer);
    return 0;
}
