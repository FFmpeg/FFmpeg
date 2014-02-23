/*
 * HEVC video decoder
 *
 * Copyright (C) 2012 - 2013 Guillaume Martres
 * Copyright (C) 2013 Seppo Tomperi
 * Copyright (C) 2013 Wassim Hamidouche
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

#include "libavutil/common.h"
#include "libavutil/internal.h"

#include "cabac_functions.h"
#include "golomb.h"
#include "hevc.h"

#include "bit_depth_template.c"

#define LUMA 0
#define CB 1
#define CR 2

static const uint8_t tctable[54] = {
    0, 0, 0, 0, 0, 0, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 0, 0, 1, // QP  0...18
    1, 1, 1, 1, 1, 1, 1,  1,  2,  2,  2,  2,  3,  3,  3,  3, 4, 4, 4, // QP 19...37
    5, 5, 6, 6, 7, 8, 9, 10, 11, 13, 14, 16, 18, 20, 22, 24           // QP 38...53
};

static const uint8_t betatable[52] = {
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  6,  7,  8, // QP 0...18
     9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 20, 22, 24, 26, 28, 30, 32, 34, 36, // QP 19...37
    38, 40, 42, 44, 46, 48, 50, 52, 54, 56, 58, 60, 62, 64                      // QP 38...51
};

static int chroma_tc(HEVCContext *s, int qp_y, int c_idx, int tc_offset)
{
    static const int qp_c[] = {
        29, 30, 31, 32, 33, 33, 34, 34, 35, 35, 36, 36, 37, 37
    };
    int qp, qp_i, offset, idxt;

    // slice qp offset is not used for deblocking
    if (c_idx == 1)
        offset = s->pps->cb_qp_offset;
    else
        offset = s->pps->cr_qp_offset;

    qp_i = av_clip_c(qp_y + offset, 0, 57);
    if (qp_i < 30)
        qp = qp_i;
    else if (qp_i > 43)
        qp = qp_i - 6;
    else
        qp = qp_c[qp_i - 30];

    idxt = av_clip_c(qp + DEFAULT_INTRA_TC_OFFSET + tc_offset, 0, 53);
    return tctable[idxt];
}

static int get_qPy_pred(HEVCContext *s, int xC, int yC,
                        int xBase, int yBase, int log2_cb_size)
{
    HEVCLocalContext *lc     = s->HEVClc;
    int ctb_size_mask        = (1 << s->sps->log2_ctb_size) - 1;
    int MinCuQpDeltaSizeMask = (1 << (s->sps->log2_ctb_size -
                                      s->pps->diff_cu_qp_delta_depth)) - 1;
    int xQgBase              = xBase - (xBase & MinCuQpDeltaSizeMask);
    int yQgBase              = yBase - (yBase & MinCuQpDeltaSizeMask);
    int min_cb_width         = s->sps->min_cb_width;
    int x_cb                 = xQgBase >> s->sps->log2_min_cb_size;
    int y_cb                 = yQgBase >> s->sps->log2_min_cb_size;
    int availableA           = (xBase   & ctb_size_mask) &&
                               (xQgBase & ctb_size_mask);
    int availableB           = (yBase   & ctb_size_mask) &&
                               (yQgBase & ctb_size_mask);
    int qPy_pred, qPy_a, qPy_b;

    // qPy_pred
    if (lc->first_qp_group || (!xQgBase && !yQgBase)) {
        lc->first_qp_group = !lc->tu.is_cu_qp_delta_coded;
        qPy_pred = s->sh.slice_qp;
    } else {
        qPy_pred = lc->qPy_pred;
    }

    // qPy_a
    if (availableA == 0)
        qPy_a = qPy_pred;
    else
        qPy_a = s->qp_y_tab[(x_cb - 1) + y_cb * min_cb_width];

    // qPy_b
    if (availableB == 0)
        qPy_b = qPy_pred;
    else
        qPy_b = s->qp_y_tab[x_cb + (y_cb - 1) * min_cb_width];

    av_assert2(qPy_a >= -s->sps->qp_bd_offset && qPy_a < 52);
    av_assert2(qPy_b >= -s->sps->qp_bd_offset && qPy_b < 52);

    return (qPy_a + qPy_b + 1) >> 1;
}

void ff_hevc_set_qPy(HEVCContext *s, int xC, int yC,
                     int xBase, int yBase, int log2_cb_size)
{
    int qp_y = get_qPy_pred(s, xC, yC, xBase, yBase, log2_cb_size);

    if (s->HEVClc->tu.cu_qp_delta != 0) {
        int off = s->sps->qp_bd_offset;
        s->HEVClc->qp_y = FFUMOD(qp_y + s->HEVClc->tu.cu_qp_delta + 52 + 2 * off,
                                 52 + off) - off;
    } else
        s->HEVClc->qp_y = qp_y;
}

static int get_qPy(HEVCContext *s, int xC, int yC)
{
    int log2_min_cb_size  = s->sps->log2_min_cb_size;
    int x                 = xC >> log2_min_cb_size;
    int y                 = yC >> log2_min_cb_size;
    return s->qp_y_tab[x + y * s->sps->min_cb_width];
}

static void copy_CTB(uint8_t *dst, uint8_t *src,
                     int width, int height, int stride)
{
    int i;

    for (i = 0; i < height; i++) {
        memcpy(dst, src, width);
        dst += stride;
        src += stride;
    }
}

#define CTB(tab, x, y) ((tab)[(y) * s->sps->ctb_width + (x)])

static void sao_filter_CTB(HEVCContext *s, int x, int y)
{
    //  TODO: This should be easily parallelizable
    //  TODO: skip CBs when (cu_transquant_bypass_flag || (pcm_loop_filter_disable_flag && pcm_flag))
    int c_idx = 0;
    int class = 1, class_index;
    int edges[4];  // 0 left 1 top 2 right 3 bottom
    SAOParams *sao[4];
    int classes[4];
    int x_shift = 0, y_shift = 0;
    int x_ctb = x >> s->sps->log2_ctb_size;
    int y_ctb = y >> s->sps->log2_ctb_size;
    int ctb_addr_rs = y_ctb * s->sps->ctb_width + x_ctb;
    int ctb_addr_ts = s->pps->ctb_addr_rs_to_ts[ctb_addr_rs];

    // flags indicating unfilterable edges
    uint8_t vert_edge[]  = { 0, 0, 0, 0 };
    uint8_t horiz_edge[] = { 0, 0, 0, 0 };
    uint8_t diag_edge[]  = { 0, 0, 0, 0 };
    uint8_t lfase[3]; // current, above, left
    uint8_t no_tile_filter = s->pps->tiles_enabled_flag &&
                             !s->pps->loop_filter_across_tiles_enabled_flag;
    uint8_t left_tile_edge = 0;
    uint8_t up_tile_edge = 0;

    sao[0]     = &CTB(s->sao, x_ctb, y_ctb);
    edges[0]   = x_ctb == 0;
    edges[1]   = y_ctb == 0;
    edges[2]   = x_ctb == s->sps->ctb_width  - 1;
    edges[3]   = y_ctb == s->sps->ctb_height - 1;
    lfase[0]   = CTB(s->filter_slice_edges, x_ctb, y_ctb);
    classes[0] = 0;

    if (!edges[0]) {
        left_tile_edge = no_tile_filter && s->pps->tile_id[ctb_addr_ts] != s->pps->tile_id[s->pps->ctb_addr_rs_to_ts[ctb_addr_rs-1]];
        sao[class] = &CTB(s->sao, x_ctb - 1, y_ctb);
        vert_edge[0] = (!lfase[0] && CTB(s->tab_slice_address, x_ctb, y_ctb) != CTB(s->tab_slice_address, x_ctb - 1, y_ctb)) || left_tile_edge;
        vert_edge[2] = vert_edge[0];
        lfase[2]     = CTB(s->filter_slice_edges, x_ctb - 1, y_ctb);
        classes[class] = 2;
        class++;
        x_shift = 8;
    }

    if (!edges[1]) {
        up_tile_edge = no_tile_filter && s->pps->tile_id[ctb_addr_ts] != s->pps->tile_id[s->pps->ctb_addr_rs_to_ts[ctb_addr_rs - s->sps->ctb_width]];
        sao[class] = &CTB(s->sao, x_ctb, y_ctb - 1);
        horiz_edge[0] = (!lfase[0] && CTB(s->tab_slice_address, x_ctb, y_ctb) != CTB(s->tab_slice_address, x_ctb, y_ctb - 1)) || up_tile_edge;
        horiz_edge[1] = horiz_edge[0];
        lfase[1] = CTB(s->filter_slice_edges, x_ctb, y_ctb - 1);
        classes[class] = 1;
        class++;
        y_shift = 4;

        if (!edges[0]) {
            classes[class] = 3;
            sao[class] = &CTB(s->sao, x_ctb - 1, y_ctb - 1);
            class++;

            // Tile check here is done current CTB row/col, not above/left like you'd expect,
            //but that is because the tile boundary always extends through the whole pic
            vert_edge[1] = (!lfase[1] && CTB(s->tab_slice_address, x_ctb, y_ctb - 1) != CTB(s->tab_slice_address, x_ctb - 1, y_ctb - 1)) || left_tile_edge;
            vert_edge[3] = vert_edge[1];
            horiz_edge[2] = (!lfase[2] && CTB(s->tab_slice_address, x_ctb - 1, y_ctb) != CTB(s->tab_slice_address, x_ctb - 1, y_ctb - 1)) || up_tile_edge;
            horiz_edge[3] = horiz_edge[2];
            diag_edge[0] = (!lfase[0] && CTB(s->tab_slice_address, x_ctb, y_ctb) != CTB(s->tab_slice_address, x_ctb - 1, y_ctb - 1)) || left_tile_edge || up_tile_edge;
            diag_edge[3] = diag_edge[0];

            // Does left CTB comes after above CTB?
            if (CTB(s->tab_slice_address, x_ctb - 1, y_ctb) >
                CTB(s->tab_slice_address, x_ctb, y_ctb - 1)) {
                diag_edge[2] = !lfase[2] || left_tile_edge || up_tile_edge;
                diag_edge[1] = diag_edge[2];
            } else if (CTB(s->tab_slice_address, x_ctb - 1, y_ctb) <
                       CTB(s->tab_slice_address, x_ctb, y_ctb - 1)) {
                diag_edge[1] = !lfase[1] || left_tile_edge || up_tile_edge;
                diag_edge[2] = diag_edge[1];
            } else {
                // Same slice, only consider tiles
                diag_edge[2] = left_tile_edge || up_tile_edge;
                diag_edge[1] = diag_edge[2];
            }
        }
    }

    for (c_idx = 0; c_idx < 3; c_idx++) {
        int chroma = c_idx ? 1 : 0;
        int x0 = x >> chroma;
        int y0 = y >> chroma;
        int stride = s->frame->linesize[c_idx];
        int ctb_size = (1 << (s->sps->log2_ctb_size)) >> s->sps->hshift[c_idx];
        int width = FFMIN(ctb_size,
                          (s->sps->width >> s->sps->hshift[c_idx]) - x0);
        int height = FFMIN(ctb_size,
                           (s->sps->height >> s->sps->vshift[c_idx]) - y0);

        uint8_t *src = &s->frame->data[c_idx][y0 * stride + (x0 << s->sps->pixel_shift)];
        uint8_t *dst = &s->sao_frame->data[c_idx][y0 * stride + (x0 << s->sps->pixel_shift)];
        int offset = (y_shift >> chroma) * stride + ((x_shift >> chroma) << s->sps->pixel_shift);

        copy_CTB(dst - offset, src - offset,
                 (edges[2] ? width  + (x_shift >> chroma) : width)  << s->sps->pixel_shift,
                 (edges[3] ? height + (y_shift >> chroma) : height), stride);

        for (class_index = 0; class_index < class; class_index++) {

            switch (sao[class_index]->type_idx[c_idx]) {
            case SAO_BAND:
                s->hevcdsp.sao_band_filter[classes[class_index]](dst, src,
                                                                 stride,
                                                                 sao[class_index],
                                                                 edges, width,
                                                                 height, c_idx);
                break;
            case SAO_EDGE:
                s->hevcdsp.sao_edge_filter[classes[class_index]](dst, src,
                                                                 stride,
                                                                 sao[class_index],
                                                                 edges, width,
                                                                 height, c_idx,
                                                                 vert_edge[classes[class_index]],
                                                                 horiz_edge[classes[class_index]],
                                                                 diag_edge[classes[class_index]]);
                break;
            }
        }
    }
}

static int get_pcm(HEVCContext *s, int x, int y)
{
    int log2_min_pu_size = s->sps->log2_min_pu_size;
    int x_pu, y_pu;

    if (x < 0 || y < 0)
        return 2;

    x_pu = x >> log2_min_pu_size;
    y_pu = y >> log2_min_pu_size;

    if (x_pu >= s->sps->min_pu_width || y_pu >= s->sps->min_pu_height)
        return 2;
    return s->is_pcm[y_pu * s->sps->min_pu_width + x_pu];
}

#define TC_CALC(qp, bs)                                                 \
    tctable[av_clip((qp) + DEFAULT_INTRA_TC_OFFSET * ((bs) - 1) +       \
                    (tc_offset >> 1 << 1),                              \
                    0, MAX_QP + DEFAULT_INTRA_TC_OFFSET)]

static void deblocking_filter_CTB(HEVCContext *s, int x0, int y0)
{
    uint8_t *src;
    int x, y;
    int chroma;
    int c_tc[2], beta[2], tc[2];
    uint8_t no_p[2] = { 0 };
    uint8_t no_q[2] = { 0 };

    int log2_ctb_size = s->sps->log2_ctb_size;
    int x_end, y_end;
    int ctb_size        = 1 << log2_ctb_size;
    int ctb             = (x0 >> log2_ctb_size) +
                          (y0 >> log2_ctb_size) * s->sps->ctb_width;
    int cur_tc_offset   = s->deblock[ctb].tc_offset;
    int cur_beta_offset = s->deblock[ctb].beta_offset;
    int left_tc_offset, left_beta_offset;
    int tc_offset, beta_offset;
    int pcmf = (s->sps->pcm_enabled_flag &&
                s->sps->pcm.loop_filter_disable_flag) ||
               s->pps->transquant_bypass_enable_flag;

    if (x0) {
        left_tc_offset   = s->deblock[ctb - 1].tc_offset;
        left_beta_offset = s->deblock[ctb - 1].beta_offset;
    }

    x_end = x0 + ctb_size;
    if (x_end > s->sps->width)
        x_end = s->sps->width;
    y_end = y0 + ctb_size;
    if (y_end > s->sps->height)
        y_end = s->sps->height;

    tc_offset   = cur_tc_offset;
    beta_offset = cur_beta_offset;

    // vertical filtering luma
    for (y = y0; y < y_end; y += 8) {
        for (x = x0 ? x0 : 8; x < x_end; x += 8) {
            const int bs0 = s->vertical_bs[(x >> 3) + (y       >> 2) * s->bs_width];
            const int bs1 = s->vertical_bs[(x >> 3) + ((y + 4) >> 2) * s->bs_width];
            if (bs0 || bs1) {
                const int qp0 = (get_qPy(s, x - 1, y)     + get_qPy(s, x, y)     + 1) >> 1;
                const int qp1 = (get_qPy(s, x - 1, y + 4) + get_qPy(s, x, y + 4) + 1) >> 1;

                beta[0] = betatable[av_clip(qp0 + beta_offset, 0, MAX_QP)];
                beta[1] = betatable[av_clip(qp1 + beta_offset, 0, MAX_QP)];
                tc[0]   = bs0 ? TC_CALC(qp0, bs0) : 0;
                tc[1]   = bs1 ? TC_CALC(qp1, bs1) : 0;
                src     = &s->frame->data[LUMA][y * s->frame->linesize[LUMA] + (x << s->sps->pixel_shift)];
                if (pcmf) {
                    no_p[0] = get_pcm(s, x - 1, y);
                    no_p[1] = get_pcm(s, x - 1, y + 4);
                    no_q[0] = get_pcm(s, x, y);
                    no_q[1] = get_pcm(s, x, y + 4);
                    s->hevcdsp.hevc_v_loop_filter_luma_c(src,
                                                         s->frame->linesize[LUMA],
                                                         beta, tc, no_p, no_q);
                } else
                    s->hevcdsp.hevc_v_loop_filter_luma(src,
                                                       s->frame->linesize[LUMA],
                                                       beta, tc, no_p, no_q);
            }
        }
    }

    // vertical filtering chroma
    for (chroma = 1; chroma <= 2; chroma++) {
        for (y = y0; y < y_end; y += 16) {
            for (x = x0 ? x0 : 16; x < x_end; x += 16) {
                const int bs0 = s->vertical_bs[(x >> 3) + (y       >> 2) * s->bs_width];
                const int bs1 = s->vertical_bs[(x >> 3) + ((y + 8) >> 2) * s->bs_width];
                if ((bs0 == 2) || (bs1 == 2)) {
                    const int qp0 = (get_qPy(s, x - 1, y)     + get_qPy(s, x, y)     + 1) >> 1;
                    const int qp1 = (get_qPy(s, x - 1, y + 8) + get_qPy(s, x, y + 8) + 1) >> 1;

                    c_tc[0] = (bs0 == 2) ? chroma_tc(s, qp0, chroma, tc_offset) : 0;
                    c_tc[1] = (bs1 == 2) ? chroma_tc(s, qp1, chroma, tc_offset) : 0;
                    src     = &s->frame->data[chroma][y / 2 * s->frame->linesize[chroma] + ((x / 2) << s->sps->pixel_shift)];
                    if (pcmf) {
                        no_p[0] = get_pcm(s, x - 1, y);
                        no_p[1] = get_pcm(s, x - 1, y + 8);
                        no_q[0] = get_pcm(s, x, y);
                        no_q[1] = get_pcm(s, x, y + 8);
                        s->hevcdsp.hevc_v_loop_filter_chroma_c(src,
                                                               s->frame->linesize[chroma],
                                                               c_tc, no_p, no_q);
                    } else
                        s->hevcdsp.hevc_v_loop_filter_chroma(src,
                                                             s->frame->linesize[chroma],
                                                             c_tc, no_p, no_q);
                }
            }
        }
    }

    // horizontal filtering luma
    if (x_end != s->sps->width)
        x_end -= 8;
    for (y = y0 ? y0 : 8; y < y_end; y += 8) {
        for (x = x0 ? x0 - 8 : 0; x < x_end; x += 8) {
            const int bs0 = s->horizontal_bs[(x +     y * s->bs_width) >> 2];
            const int bs1 = s->horizontal_bs[(x + 4 + y * s->bs_width) >> 2];
            if (bs0 || bs1) {
                const int qp0 = (get_qPy(s, x, y - 1)     + get_qPy(s, x, y)     + 1) >> 1;
                const int qp1 = (get_qPy(s, x + 4, y - 1) + get_qPy(s, x + 4, y) + 1) >> 1;

                tc_offset   = x >= x0 ? cur_tc_offset : left_tc_offset;
                beta_offset = x >= x0 ? cur_beta_offset : left_beta_offset;

                beta[0] = betatable[av_clip(qp0 + beta_offset, 0, MAX_QP)];
                beta[1] = betatable[av_clip(qp1 + beta_offset, 0, MAX_QP)];
                tc[0]   = bs0 ? TC_CALC(qp0, bs0) : 0;
                tc[1]   = bs1 ? TC_CALC(qp1, bs1) : 0;
                src     = &s->frame->data[LUMA][y * s->frame->linesize[LUMA] + (x << s->sps->pixel_shift)];
                if (pcmf) {
                    no_p[0] = get_pcm(s, x, y - 1);
                    no_p[1] = get_pcm(s, x + 4, y - 1);
                    no_q[0] = get_pcm(s, x, y);
                    no_q[1] = get_pcm(s, x + 4, y);
                    s->hevcdsp.hevc_h_loop_filter_luma_c(src,
                                                         s->frame->linesize[LUMA],
                                                         beta, tc, no_p, no_q);
                } else
                    s->hevcdsp.hevc_h_loop_filter_luma(src,
                                                       s->frame->linesize[LUMA],
                                                       beta, tc, no_p, no_q);
            }
        }
    }

    // horizontal filtering chroma
    for (chroma = 1; chroma <= 2; chroma++) {
        for (y = y0 ? y0 : 16; y < y_end; y += 16) {
            for (x = x0 - 8; x < x_end; x += 16) {
                int bs0, bs1;
                // to make sure no memory access over boundary when x = -8
                // TODO: simplify with row based deblocking
                if (x < 0) {
                    bs0 = 0;
                    bs1 = s->horizontal_bs[(x + 8 + y * s->bs_width) >> 2];
                } else if (x >= x_end - 8) {
                    bs0 = s->horizontal_bs[(x +     y * s->bs_width) >> 2];
                    bs1 = 0;
                } else {
                    bs0 = s->horizontal_bs[(x + y     * s->bs_width) >> 2];
                    bs1 = s->horizontal_bs[(x + 8 + y * s->bs_width) >> 2];
                }

                if ((bs0 == 2) || (bs1 == 2)) {
                    const int qp0 = bs0 == 2 ? (get_qPy(s, x,     y - 1) + get_qPy(s, x,     y) + 1) >> 1 : 0;
                    const int qp1 = bs1 == 2 ? (get_qPy(s, x + 8, y - 1) + get_qPy(s, x + 8, y) + 1) >> 1 : 0;

                    tc_offset = x >= x0 ? cur_tc_offset : left_tc_offset;
                    c_tc[0]   = bs0 == 2 ? chroma_tc(s, qp0, chroma, tc_offset)     : 0;
                    c_tc[1]   = bs1 == 2 ? chroma_tc(s, qp1, chroma, cur_tc_offset) : 0;
                    src       = &s->frame->data[chroma][y / 2 * s->frame->linesize[chroma] + ((x / 2) << s->sps->pixel_shift)];
                    if (pcmf) {
                        no_p[0] = get_pcm(s, x, y - 1);
                        no_p[1] = get_pcm(s, x + 8, y - 1);
                        no_q[0] = get_pcm(s, x, y);
                        no_q[1] = get_pcm(s, x + 8, y);
                        s->hevcdsp.hevc_h_loop_filter_chroma_c(src,
                                                               s->frame->linesize[chroma],
                                                               c_tc, no_p, no_q);
                    } else
                        s->hevcdsp.hevc_h_loop_filter_chroma(src,
                                                             s->frame->linesize[chroma],
                                                             c_tc, no_p, no_q);
                }
            }
        }
    }
}

static int boundary_strength(HEVCContext *s, MvField *curr,
                             uint8_t curr_cbf_luma, MvField *neigh,
                             uint8_t neigh_cbf_luma,
                             RefPicList *neigh_refPicList,
                             int tu_border)
{
    int mvs = curr->pred_flag[0] + curr->pred_flag[1];

    if (tu_border) {
        if (curr->is_intra || neigh->is_intra)
            return 2;
        if (curr_cbf_luma || neigh_cbf_luma)
            return 1;
    }

    if (mvs == neigh->pred_flag[0] + neigh->pred_flag[1]) {
        if (mvs == 2) {
            // same L0 and L1
            if (s->ref->refPicList[0].list[curr->ref_idx[0]] == neigh_refPicList[0].list[neigh->ref_idx[0]]  &&
                s->ref->refPicList[0].list[curr->ref_idx[0]] == s->ref->refPicList[1].list[curr->ref_idx[1]] &&
                neigh_refPicList[0].list[neigh->ref_idx[0]] == neigh_refPicList[1].list[neigh->ref_idx[1]]) {
                if ((abs(neigh->mv[0].x - curr->mv[0].x) >= 4 || abs(neigh->mv[0].y - curr->mv[0].y) >= 4 ||
                     abs(neigh->mv[1].x - curr->mv[1].x) >= 4 || abs(neigh->mv[1].y - curr->mv[1].y) >= 4) &&
                    (abs(neigh->mv[1].x - curr->mv[0].x) >= 4 || abs(neigh->mv[1].y - curr->mv[0].y) >= 4 ||
                     abs(neigh->mv[0].x - curr->mv[1].x) >= 4 || abs(neigh->mv[0].y - curr->mv[1].y) >= 4))
                    return 1;
                else
                    return 0;
            } else if (neigh_refPicList[0].list[neigh->ref_idx[0]] == s->ref->refPicList[0].list[curr->ref_idx[0]] &&
                       neigh_refPicList[1].list[neigh->ref_idx[1]] == s->ref->refPicList[1].list[curr->ref_idx[1]]) {
                if (abs(neigh->mv[0].x - curr->mv[0].x) >= 4 || abs(neigh->mv[0].y - curr->mv[0].y) >= 4 ||
                    abs(neigh->mv[1].x - curr->mv[1].x) >= 4 || abs(neigh->mv[1].y - curr->mv[1].y) >= 4)
                    return 1;
                else
                    return 0;
            } else if (neigh_refPicList[1].list[neigh->ref_idx[1]] == s->ref->refPicList[0].list[curr->ref_idx[0]] &&
                       neigh_refPicList[0].list[neigh->ref_idx[0]] == s->ref->refPicList[1].list[curr->ref_idx[1]]) {
                if (abs(neigh->mv[1].x - curr->mv[0].x) >= 4 || abs(neigh->mv[1].y - curr->mv[0].y) >= 4 ||
                    abs(neigh->mv[0].x - curr->mv[1].x) >= 4 || abs(neigh->mv[0].y - curr->mv[1].y) >= 4)
                    return 1;
                else
                    return 0;
            } else {
                return 1;
            }
        } else { // 1 MV
            Mv A, B;
            int ref_A, ref_B;

            if (curr->pred_flag[0]) {
                A     = curr->mv[0];
                ref_A = s->ref->refPicList[0].list[curr->ref_idx[0]];
            } else {
                A     = curr->mv[1];
                ref_A = s->ref->refPicList[1].list[curr->ref_idx[1]];
            }

            if (neigh->pred_flag[0]) {
                B     = neigh->mv[0];
                ref_B = neigh_refPicList[0].list[neigh->ref_idx[0]];
            } else {
                B     = neigh->mv[1];
                ref_B = neigh_refPicList[1].list[neigh->ref_idx[1]];
            }

            if (ref_A == ref_B) {
                if (abs(A.x - B.x) >= 4 || abs(A.y - B.y) >= 4)
                    return 1;
                else
                    return 0;
            } else
                return 1;
        }
    }

    return 1;
}

void ff_hevc_deblocking_boundary_strengths(HEVCContext *s, int x0, int y0,
                                           int log2_trafo_size,
                                           int slice_or_tiles_up_boundary,
                                           int slice_or_tiles_left_boundary)
{
    MvField *tab_mvf     = s->ref->tab_mvf;
    int log2_min_pu_size = s->sps->log2_min_pu_size;
    int log2_min_tu_size = s->sps->log2_min_tb_size;
    int min_pu_width     = s->sps->min_pu_width;
    int min_tu_width     = s->sps->min_tb_width;
    int is_intra = tab_mvf[(y0 >> log2_min_pu_size) * min_pu_width +
                           (x0 >> log2_min_pu_size)].is_intra;
    int i, j, bs;

    if (y0 > 0 && (y0 & 7) == 0) {
        int yp_pu = (y0 - 1) >> log2_min_pu_size;
        int yq_pu =  y0      >> log2_min_pu_size;
        int yp_tu = (y0 - 1) >> log2_min_tu_size;
        int yq_tu =  y0      >> log2_min_tu_size;

        for (i = 0; i < (1 << log2_trafo_size); i += 4) {
            int x_pu = (x0 + i) >> log2_min_pu_size;
            int x_tu = (x0 + i) >> log2_min_tu_size;
            MvField *top  = &tab_mvf[yp_pu * min_pu_width + x_pu];
            MvField *curr = &tab_mvf[yq_pu * min_pu_width + x_pu];
            uint8_t top_cbf_luma  = s->cbf_luma[yp_tu * min_tu_width + x_tu];
            uint8_t curr_cbf_luma = s->cbf_luma[yq_tu * min_tu_width + x_tu];
            RefPicList *top_refPicList = ff_hevc_get_ref_list(s, s->ref,
                                                              x0 + i, y0 - 1);

            bs = boundary_strength(s, curr, curr_cbf_luma,
                                   top, top_cbf_luma, top_refPicList, 1);
            if (!s->sh.slice_loop_filter_across_slices_enabled_flag &&
                (slice_or_tiles_up_boundary & 1) &&
                (y0 % (1 << s->sps->log2_ctb_size)) == 0)
                bs = 0;
            else if (!s->pps->loop_filter_across_tiles_enabled_flag &&
                     (slice_or_tiles_up_boundary & 2) &&
                     (y0 % (1 << s->sps->log2_ctb_size)) == 0)
                bs = 0;
            if (y0 == 0 || s->sh.disable_deblocking_filter_flag == 1)
                bs = 0;
            if (bs)
                s->horizontal_bs[((x0 + i) + y0 * s->bs_width) >> 2] = bs;
        }
    }

    // bs for TU internal horizontal PU boundaries
    if (log2_trafo_size > s->sps->log2_min_pu_size && !is_intra)
        for (j = 8; j < (1 << log2_trafo_size); j += 8) {
            int yp_pu = (y0 + j - 1) >> log2_min_pu_size;
            int yq_pu = (y0 + j)     >> log2_min_pu_size;
            int yp_tu = (y0 + j - 1) >> log2_min_tu_size;
            int yq_tu = (y0 + j)     >> log2_min_tu_size;

            for (i = 0; i < (1 << log2_trafo_size); i += 4) {
                int x_pu = (x0 + i) >> log2_min_pu_size;
                int x_tu = (x0 + i) >> log2_min_tu_size;
                MvField *top  = &tab_mvf[yp_pu * min_pu_width + x_pu];
                MvField *curr = &tab_mvf[yq_pu * min_pu_width + x_pu];
                uint8_t top_cbf_luma  = s->cbf_luma[yp_tu * min_tu_width + x_tu];
                uint8_t curr_cbf_luma = s->cbf_luma[yq_tu * min_tu_width + x_tu];
                RefPicList *top_refPicList = ff_hevc_get_ref_list(s, s->ref,
                                                                  x0 + i,
                                                                  y0 + j - 1);

                bs = boundary_strength(s, curr, curr_cbf_luma,
                                       top, top_cbf_luma, top_refPicList, 0);
                if (s->sh.disable_deblocking_filter_flag == 1)
                    bs = 0;
                if (bs)
                    s->horizontal_bs[((x0 + i) + (y0 + j) * s->bs_width) >> 2] = bs;
            }
        }

    // bs for vertical TU boundaries
    if (x0 > 0 && (x0 & 7) == 0) {
        int xp_pu = (x0 - 1) >> log2_min_pu_size;
        int xq_pu =  x0      >> log2_min_pu_size;
        int xp_tu = (x0 - 1) >> log2_min_tu_size;
        int xq_tu =  x0      >> log2_min_tu_size;

        for (i = 0; i < (1 << log2_trafo_size); i += 4) {
            int y_pu      = (y0 + i) >> log2_min_pu_size;
            int y_tu      = (y0 + i) >> log2_min_tu_size;
            MvField *left = &tab_mvf[y_pu * min_pu_width + xp_pu];
            MvField *curr = &tab_mvf[y_pu * min_pu_width + xq_pu];

            uint8_t left_cbf_luma = s->cbf_luma[y_tu * min_tu_width + xp_tu];
            uint8_t curr_cbf_luma = s->cbf_luma[y_tu * min_tu_width + xq_tu];
            RefPicList *left_refPicList = ff_hevc_get_ref_list(s, s->ref,
                                                               x0 - 1, y0 + i);

            bs = boundary_strength(s, curr, curr_cbf_luma,
                                   left, left_cbf_luma, left_refPicList, 1);
            if (!s->sh.slice_loop_filter_across_slices_enabled_flag &&
                (slice_or_tiles_left_boundary & 1) &&
                (x0 % (1 << s->sps->log2_ctb_size)) == 0)
                bs = 0;
            else if (!s->pps->loop_filter_across_tiles_enabled_flag &&
                     (slice_or_tiles_left_boundary & 2) &&
                     (x0 % (1 << s->sps->log2_ctb_size)) == 0)
                bs = 0;
            if (x0 == 0 || s->sh.disable_deblocking_filter_flag == 1)
                bs = 0;
            if (bs)
                s->vertical_bs[(x0 >> 3) + ((y0 + i) >> 2) * s->bs_width] = bs;
        }
    }

    // bs for TU internal vertical PU boundaries
    if (log2_trafo_size > log2_min_pu_size && !is_intra)
        for (j = 0; j < (1 << log2_trafo_size); j += 4) {
            int y_pu = (y0 + j) >> log2_min_pu_size;
            int y_tu = (y0 + j) >> log2_min_tu_size;

            for (i = 8; i < (1 << log2_trafo_size); i += 8) {
                int xp_pu = (x0 + i - 1) >> log2_min_pu_size;
                int xq_pu = (x0 + i)     >> log2_min_pu_size;
                int xp_tu = (x0 + i - 1) >> log2_min_tu_size;
                int xq_tu = (x0 + i)     >> log2_min_tu_size;
                MvField *left = &tab_mvf[y_pu * min_pu_width + xp_pu];
                MvField *curr = &tab_mvf[y_pu * min_pu_width + xq_pu];
                uint8_t left_cbf_luma = s->cbf_luma[y_tu * min_tu_width + xp_tu];
                uint8_t curr_cbf_luma = s->cbf_luma[y_tu * min_tu_width + xq_tu];
                RefPicList *left_refPicList = ff_hevc_get_ref_list(s, s->ref,
                                                                   x0 + i - 1,
                                                                   y0 + j);

                bs = boundary_strength(s, curr, curr_cbf_luma,
                                       left, left_cbf_luma, left_refPicList, 0);
                if (s->sh.disable_deblocking_filter_flag == 1)
                    bs = 0;
                if (bs)
                    s->vertical_bs[((x0 + i) >> 3) + ((y0 + j) >> 2) * s->bs_width] = bs;
            }
        }
}

#undef LUMA
#undef CB
#undef CR

void ff_hevc_hls_filter(HEVCContext *s, int x, int y)
{
    deblocking_filter_CTB(s, x, y);
    if (s->sps->sao_enabled)
        sao_filter_CTB(s, x, y);
}

void ff_hevc_hls_filters(HEVCContext *s, int x_ctb, int y_ctb, int ctb_size)
{
    if (y_ctb && x_ctb)
        ff_hevc_hls_filter(s, x_ctb - ctb_size, y_ctb - ctb_size);
    if (y_ctb && x_ctb >= s->sps->width - ctb_size) {
        ff_hevc_hls_filter(s, x_ctb, y_ctb - ctb_size);
        if (s->threads_type == FF_THREAD_FRAME )
            ff_thread_report_progress(&s->ref->tf, y_ctb - ctb_size, 0);
    }
    if (x_ctb && y_ctb >= s->sps->height - ctb_size)
        ff_hevc_hls_filter(s, x_ctb - ctb_size, y_ctb);
}
