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

    qp_i = av_clip(qp_y + offset, 0, 57);
    if (s->sps->chroma_format_idc == 1) {
        if (qp_i < 30)
            qp = qp_i;
        else if (qp_i > 43)
            qp = qp_i - 6;
        else
            qp = qp_c[qp_i - 30];
    } else {
        qp = av_clip(qp_i, 0, 51);
    }

    idxt = av_clip(qp + DEFAULT_INTRA_TC_OFFSET + tc_offset, 0, 53);
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

static void restore_tqb_pixels(HEVCContext *s, int x0, int y0, int width, int height, int c_idx)
{
    if ( s->pps->transquant_bypass_enable_flag ||
            (s->sps->pcm.loop_filter_disable_flag && s->sps->pcm_enabled_flag)) {
        int x, y;
        ptrdiff_t stride = s->frame->linesize[c_idx];
        int min_pu_size  = 1 << s->sps->log2_min_pu_size;
        int hshift       = s->sps->hshift[c_idx];
        int vshift       = s->sps->vshift[c_idx];
        int x_min        = ((x0         ) >> s->sps->log2_min_pu_size);
        int y_min        = ((y0         ) >> s->sps->log2_min_pu_size);
        int x_max        = ((x0 + width ) >> s->sps->log2_min_pu_size);
        int y_max        = ((y0 + height) >> s->sps->log2_min_pu_size);
        int len          = min_pu_size >> hshift;
        for (y = y_min; y < y_max; y++) {
            for (x = x_min; x < x_max; x++) {
                if (s->is_pcm[y * s->sps->min_pu_width + x]) {
                    int n;
                    uint8_t *src = &s->frame->data[c_idx][    ((y << s->sps->log2_min_pu_size) >> vshift) * stride + (((x << s->sps->log2_min_pu_size) >> hshift) << s->sps->pixel_shift)];
                    uint8_t *dst = &s->sao_frame->data[c_idx][((y << s->sps->log2_min_pu_size) >> vshift) * stride + (((x << s->sps->log2_min_pu_size) >> hshift) << s->sps->pixel_shift)];
                    for (n = 0; n < (min_pu_size >> vshift); n++) {
                        memcpy(dst, src, len);
                        src += stride;
                        dst += stride;
                    }
                }
            }
        }
    }
}

#define CTB(tab, x, y) ((tab)[(y) * s->sps->ctb_width + (x)])

static void sao_filter_CTB(HEVCContext *s, int x, int y)
{
    int c_idx;
    int edges[4];  // 0 left 1 top 2 right 3 bottom
    int x_ctb                = x >> s->sps->log2_ctb_size;
    int y_ctb                = y >> s->sps->log2_ctb_size;
    int ctb_addr_rs          = y_ctb * s->sps->ctb_width + x_ctb;
    int ctb_addr_ts          = s->pps->ctb_addr_rs_to_ts[ctb_addr_rs];
    SAOParams *sao           = &CTB(s->sao, x_ctb, y_ctb);
    // flags indicating unfilterable edges
    uint8_t vert_edge[]      = { 0, 0 };
    uint8_t horiz_edge[]     = { 0, 0 };
    uint8_t diag_edge[]      = { 0, 0, 0, 0 };
    uint8_t lfase            = CTB(s->filter_slice_edges, x_ctb, y_ctb);
    uint8_t no_tile_filter   = s->pps->tiles_enabled_flag &&
                               !s->pps->loop_filter_across_tiles_enabled_flag;
    uint8_t restore          = no_tile_filter || !lfase;
    uint8_t left_tile_edge   = 0;
    uint8_t right_tile_edge  = 0;
    uint8_t up_tile_edge     = 0;
    uint8_t bottom_tile_edge = 0;

    edges[0]   = x_ctb == 0;
    edges[1]   = y_ctb == 0;
    edges[2]   = x_ctb == s->sps->ctb_width  - 1;
    edges[3]   = y_ctb == s->sps->ctb_height - 1;

    if (restore) {
        if (!edges[0]) {
            left_tile_edge  = no_tile_filter && s->pps->tile_id[ctb_addr_ts] != s->pps->tile_id[s->pps->ctb_addr_rs_to_ts[ctb_addr_rs-1]];
            vert_edge[0]    = (!lfase && CTB(s->tab_slice_address, x_ctb, y_ctb) != CTB(s->tab_slice_address, x_ctb - 1, y_ctb)) || left_tile_edge;
        }
        if (!edges[2]) {
            right_tile_edge = no_tile_filter && s->pps->tile_id[ctb_addr_ts] != s->pps->tile_id[s->pps->ctb_addr_rs_to_ts[ctb_addr_rs+1]];
            vert_edge[1]    = (!lfase && CTB(s->tab_slice_address, x_ctb, y_ctb) != CTB(s->tab_slice_address, x_ctb + 1, y_ctb)) || right_tile_edge;
        }
        if (!edges[1]) {
            up_tile_edge     = no_tile_filter && s->pps->tile_id[ctb_addr_ts] != s->pps->tile_id[s->pps->ctb_addr_rs_to_ts[ctb_addr_rs - s->sps->ctb_width]];
            horiz_edge[0]    = (!lfase && CTB(s->tab_slice_address, x_ctb, y_ctb) != CTB(s->tab_slice_address, x_ctb, y_ctb - 1)) || up_tile_edge;
        }
        if (!edges[3]) {
            bottom_tile_edge = no_tile_filter && s->pps->tile_id[ctb_addr_ts] != s->pps->tile_id[s->pps->ctb_addr_rs_to_ts[ctb_addr_rs + s->sps->ctb_width]];
            horiz_edge[1]    = (!lfase && CTB(s->tab_slice_address, x_ctb, y_ctb) != CTB(s->tab_slice_address, x_ctb, y_ctb + 1)) || bottom_tile_edge;
        }
        if (!edges[0] && !edges[1]) {
            diag_edge[0] = (!lfase && CTB(s->tab_slice_address, x_ctb, y_ctb) != CTB(s->tab_slice_address, x_ctb - 1, y_ctb - 1)) || left_tile_edge || up_tile_edge;
        }
        if (!edges[1] && !edges[2]) {
            diag_edge[1] = (!lfase && CTB(s->tab_slice_address, x_ctb, y_ctb) != CTB(s->tab_slice_address, x_ctb + 1, y_ctb - 1)) || right_tile_edge || up_tile_edge;
        }
        if (!edges[2] && !edges[3]) {
            diag_edge[2] = (!lfase && CTB(s->tab_slice_address, x_ctb, y_ctb) != CTB(s->tab_slice_address, x_ctb + 1, y_ctb + 1)) || right_tile_edge || bottom_tile_edge;
        }
        if (!edges[0] && !edges[3]) {
            diag_edge[3] = (!lfase && CTB(s->tab_slice_address, x_ctb, y_ctb) != CTB(s->tab_slice_address, x_ctb - 1, y_ctb + 1)) || left_tile_edge || bottom_tile_edge;
        }
    }

    for (c_idx = 0; c_idx < 3; c_idx++) {
        int x0       = x >> s->sps->hshift[c_idx];
        int y0       = y >> s->sps->vshift[c_idx];
        int stride   = s->frame->linesize[c_idx];
        int ctb_size_h = (1 << (s->sps->log2_ctb_size)) >> s->sps->hshift[c_idx];
        int ctb_size_v = (1 << (s->sps->log2_ctb_size)) >> s->sps->vshift[c_idx];
        int width    = FFMIN(ctb_size_h,
                             (s->sps->width  >> s->sps->hshift[c_idx]) - x0);
        int height   = FFMIN(ctb_size_v,
                             (s->sps->height >> s->sps->vshift[c_idx]) - y0);

        uint8_t *src = &s->frame->data[c_idx][y0 * stride + (x0 << s->sps->pixel_shift)];
        uint8_t *dst = &s->sao_frame->data[c_idx][y0 * stride + (x0 << s->sps->pixel_shift)];

        switch (sao->type_idx[c_idx]) {
        case SAO_BAND:
            s->hevcdsp.sao_band_filter(dst, src,
                                       stride,
                                       sao,
                                       edges, width,
                                       height, c_idx);
            restore_tqb_pixels(s, x, y, width, height, c_idx);
            break;
        case SAO_EDGE:
            s->hevcdsp.sao_edge_filter[restore](dst, src,
                                                stride,
                                                sao,
                                                edges, width,
                                                height, c_idx,
                                                vert_edge,
                                                horiz_edge,
                                                diag_edge);
            restore_tqb_pixels(s, x, y, width, height, c_idx);
            break;
        default :
            copy_CTB(dst, src, width << s->sps->pixel_shift, height, stride);
            break;
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
    } else {
        left_tc_offset   = 0;
        left_beta_offset = 0;
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
        int h = 1 << s->sps->hshift[chroma];
        int v = 1 << s->sps->vshift[chroma];
        for (y = y0; y < y_end; y += (8 * v)) {
            for (x = x0 ? x0 : 8 * h; x < x_end; x += (8 * h)) {
                const int bs0 = s->vertical_bs[(x >> 3) + (y             >> 2) * s->bs_width];
                const int bs1 = s->vertical_bs[(x >> 3) + ((y + (4 * v)) >> 2) * s->bs_width];

                if ((bs0 == 2) || (bs1 == 2)) {
                    const int qp0 = (get_qPy(s, x - 1, y)           + get_qPy(s, x, y)           + 1) >> 1;
                    const int qp1 = (get_qPy(s, x - 1, y + (4 * v)) + get_qPy(s, x, y + (4 * v)) + 1) >> 1;

                    c_tc[0] = (bs0 == 2) ? chroma_tc(s, qp0, chroma, tc_offset) : 0;
                    c_tc[1] = (bs1 == 2) ? chroma_tc(s, qp1, chroma, tc_offset) : 0;
                    src       = &s->frame->data[chroma][(y >> s->sps->vshift[chroma]) * s->frame->linesize[chroma] + ((x >> s->sps->hshift[chroma]) << s->sps->pixel_shift)];
                    if (pcmf) {
                        no_p[0] = get_pcm(s, x - 1, y);
                        no_p[1] = get_pcm(s, x - 1, y + (4 * v));
                        no_q[0] = get_pcm(s, x, y);
                        no_q[1] = get_pcm(s, x, y + (4 * v));
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
        int h = 1 << s->sps->hshift[chroma];
        int v = 1 << s->sps->vshift[chroma];
        for (y = y0 ? y0 : 8 * v; y < y_end; y +=  (8 * v)) {
            for (x = x0 - 8; x < x_end; x += (8 * h)) {
                int bs0, bs1;
                // to make sure no memory access over boundary when x = -8
                // TODO: simplify with row based deblocking
                if (x < 0) {
                    bs0 = 0;
                    bs1 = s->horizontal_bs[(x + (4 * h) + y * s->bs_width) >> 2];
                } else if (x >= x_end - 4 * h) {
                    bs0 = s->horizontal_bs[(x +           y * s->bs_width) >> 2];
                    bs1 = 0;
                } else {
                    bs0 = s->horizontal_bs[(x           + y * s->bs_width) >> 2];
                    bs1 = s->horizontal_bs[(x + (4 * h) + y * s->bs_width) >> 2];
                }

                if ((bs0 == 2) || (bs1 == 2)) {
                    const int qp0 = bs0 == 2 ? (get_qPy(s, x,           y - 1) + get_qPy(s, x,           y) + 1) >> 1 : 0;
                    const int qp1 = bs1 == 2 ? (get_qPy(s, x + (4 * h), y - 1) + get_qPy(s, x + (4 * h), y) + 1) >> 1 : 0;

                    tc_offset = x >= x0 ? cur_tc_offset : left_tc_offset;
                    c_tc[0]   = bs0 == 2 ? chroma_tc(s, qp0, chroma, tc_offset)     : 0;
                    c_tc[1]   = bs1 == 2 ? chroma_tc(s, qp1, chroma, cur_tc_offset) : 0;
                    src       = &s->frame->data[chroma][(y >> s->sps->vshift[1]) * s->frame->linesize[chroma] + ((x >> s->sps->hshift[1]) << s->sps->pixel_shift)];
                    if (pcmf) {
                        no_p[0] = get_pcm(s, x,           y - 1);
                        no_p[1] = get_pcm(s, x + (4 * h), y - 1);
                        no_q[0] = get_pcm(s, x,           y);
                        no_q[1] = get_pcm(s, x + (4 * h), y);
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

static int boundary_strength(HEVCContext *s, MvField *curr, MvField *neigh,
                             RefPicList *neigh_refPicList)
{
    if (curr->pred_flag == PF_BI &&  neigh->pred_flag == PF_BI) {
        // same L0 and L1
        if (s->ref->refPicList[0].list[curr->ref_idx[0]] == neigh_refPicList[0].list[neigh->ref_idx[0]]  &&
            s->ref->refPicList[0].list[curr->ref_idx[0]] == s->ref->refPicList[1].list[curr->ref_idx[1]] &&
            neigh_refPicList[0].list[neigh->ref_idx[0]] == neigh_refPicList[1].list[neigh->ref_idx[1]]) {
            if ((FFABS(neigh->mv[0].x - curr->mv[0].x) >= 4 || FFABS(neigh->mv[0].y - curr->mv[0].y) >= 4 ||
                 FFABS(neigh->mv[1].x - curr->mv[1].x) >= 4 || FFABS(neigh->mv[1].y - curr->mv[1].y) >= 4) &&
                (FFABS(neigh->mv[1].x - curr->mv[0].x) >= 4 || FFABS(neigh->mv[1].y - curr->mv[0].y) >= 4 ||
                 FFABS(neigh->mv[0].x - curr->mv[1].x) >= 4 || FFABS(neigh->mv[0].y - curr->mv[1].y) >= 4))
                return 1;
            else
                return 0;
        } else if (neigh_refPicList[0].list[neigh->ref_idx[0]] == s->ref->refPicList[0].list[curr->ref_idx[0]] &&
                   neigh_refPicList[1].list[neigh->ref_idx[1]] == s->ref->refPicList[1].list[curr->ref_idx[1]]) {
            if (FFABS(neigh->mv[0].x - curr->mv[0].x) >= 4 || FFABS(neigh->mv[0].y - curr->mv[0].y) >= 4 ||
                FFABS(neigh->mv[1].x - curr->mv[1].x) >= 4 || FFABS(neigh->mv[1].y - curr->mv[1].y) >= 4)
                return 1;
            else
                return 0;
        } else if (neigh_refPicList[1].list[neigh->ref_idx[1]] == s->ref->refPicList[0].list[curr->ref_idx[0]] &&
                   neigh_refPicList[0].list[neigh->ref_idx[0]] == s->ref->refPicList[1].list[curr->ref_idx[1]]) {
            if (FFABS(neigh->mv[1].x - curr->mv[0].x) >= 4 || FFABS(neigh->mv[1].y - curr->mv[0].y) >= 4 ||
                FFABS(neigh->mv[0].x - curr->mv[1].x) >= 4 || FFABS(neigh->mv[0].y - curr->mv[1].y) >= 4)
                return 1;
            else
                return 0;
        } else {
            return 1;
        }
    } else if ((curr->pred_flag != PF_BI) && (neigh->pred_flag != PF_BI)){ // 1 MV
        Mv A, B;
        int ref_A, ref_B;

        if (curr->pred_flag & 1) {
            A     = curr->mv[0];
            ref_A = s->ref->refPicList[0].list[curr->ref_idx[0]];
        } else {
            A     = curr->mv[1];
            ref_A = s->ref->refPicList[1].list[curr->ref_idx[1]];
        }

        if (neigh->pred_flag & 1) {
            B     = neigh->mv[0];
            ref_B = neigh_refPicList[0].list[neigh->ref_idx[0]];
        } else {
            B     = neigh->mv[1];
            ref_B = neigh_refPicList[1].list[neigh->ref_idx[1]];
        }

        if (ref_A == ref_B) {
            if (FFABS(A.x - B.x) >= 4 || FFABS(A.y - B.y) >= 4)
                return 1;
            else
                return 0;
        } else
            return 1;
    }

    return 1;
}

void ff_hevc_deblocking_boundary_strengths(HEVCContext *s, int x0, int y0,
                                           int log2_trafo_size)
{
    HEVCLocalContext *lc = s->HEVClc;
    MvField *tab_mvf     = s->ref->tab_mvf;
    int log2_min_pu_size = s->sps->log2_min_pu_size;
    int log2_min_tu_size = s->sps->log2_min_tb_size;
    int min_pu_width     = s->sps->min_pu_width;
    int min_tu_width     = s->sps->min_tb_width;
    int is_intra = tab_mvf[(y0 >> log2_min_pu_size) * min_pu_width +
                           (x0 >> log2_min_pu_size)].pred_flag == PF_INTRA;
    int i, j, bs;

    if (y0 > 0 && (y0 & 7) == 0) {
        int bd_ctby = y0 & ((1 << s->sps->log2_ctb_size) - 1);
        int bd_slice = s->sh.slice_loop_filter_across_slices_enabled_flag ||
                       !(lc->slice_or_tiles_up_boundary & 1);
        int bd_tiles = s->pps->loop_filter_across_tiles_enabled_flag ||
                       !(lc->slice_or_tiles_up_boundary & 2);
        if (((bd_slice && bd_tiles)  || bd_ctby)) {
            int yp_pu = (y0 - 1) >> log2_min_pu_size;
            int yq_pu =  y0      >> log2_min_pu_size;
            int yp_tu = (y0 - 1) >> log2_min_tu_size;
            int yq_tu =  y0      >> log2_min_tu_size;
            RefPicList *top_refPicList = ff_hevc_get_ref_list(s, s->ref,
                                                              x0, y0 - 1);

            for (i = 0; i < (1 << log2_trafo_size); i += 4) {
                int x_pu = (x0 + i) >> log2_min_pu_size;
                int x_tu = (x0 + i) >> log2_min_tu_size;
                MvField *top  = &tab_mvf[yp_pu * min_pu_width + x_pu];
                MvField *curr = &tab_mvf[yq_pu * min_pu_width + x_pu];
                uint8_t top_cbf_luma  = s->cbf_luma[yp_tu * min_tu_width + x_tu];
                uint8_t curr_cbf_luma = s->cbf_luma[yq_tu * min_tu_width + x_tu];

                if (curr->pred_flag == PF_INTRA || top->pred_flag == PF_INTRA)
                    bs = 2;
                else if (curr_cbf_luma || top_cbf_luma)
                    bs = 1;
                else
                    bs = boundary_strength(s, curr, top, top_refPicList);
                s->horizontal_bs[((x0 + i) + y0 * s->bs_width) >> 2] = bs;
            }
        }
    }

    // bs for vertical TU boundaries
    if (x0 > 0 && (x0 & 7) == 0) {
        int bd_ctbx = x0 & ((1 << s->sps->log2_ctb_size) - 1);
        int bd_slice = s->sh.slice_loop_filter_across_slices_enabled_flag ||
                       !(lc->slice_or_tiles_left_boundary & 1);
        int bd_tiles = s->pps->loop_filter_across_tiles_enabled_flag ||
                       !(lc->slice_or_tiles_left_boundary & 2);
        if (((bd_slice && bd_tiles)  || bd_ctbx)) {
            int xp_pu = (x0 - 1) >> log2_min_pu_size;
            int xq_pu =  x0      >> log2_min_pu_size;
            int xp_tu = (x0 - 1) >> log2_min_tu_size;
            int xq_tu =  x0      >> log2_min_tu_size;
            RefPicList *left_refPicList = ff_hevc_get_ref_list(s, s->ref,
                                                               x0 - 1, y0);

            for (i = 0; i < (1 << log2_trafo_size); i += 4) {
                int y_pu      = (y0 + i) >> log2_min_pu_size;
                int y_tu      = (y0 + i) >> log2_min_tu_size;
                MvField *left = &tab_mvf[y_pu * min_pu_width + xp_pu];
                MvField *curr = &tab_mvf[y_pu * min_pu_width + xq_pu];
                uint8_t left_cbf_luma = s->cbf_luma[y_tu * min_tu_width + xp_tu];
                uint8_t curr_cbf_luma = s->cbf_luma[y_tu * min_tu_width + xq_tu];

                if (curr->pred_flag == PF_INTRA || left->pred_flag == PF_INTRA)
                    bs = 2;
                else if (curr_cbf_luma || left_cbf_luma)
                    bs = 1;
                else
                    bs = boundary_strength(s, curr, left, left_refPicList);
                s->vertical_bs[(x0 >> 3) + ((y0 + i) >> 2) * s->bs_width] = bs;
            }
        }
    }

    if (log2_trafo_size > log2_min_pu_size && !is_intra) {
        RefPicList *refPicList = ff_hevc_get_ref_list(s, s->ref,
                                                           x0,
                                                           y0);
        // bs for TU internal horizontal PU boundaries
        for (j = 8; j < (1 << log2_trafo_size); j += 8) {
            int yp_pu = (y0 + j - 1) >> log2_min_pu_size;
            int yq_pu = (y0 + j)     >> log2_min_pu_size;

            for (i = 0; i < (1 << log2_trafo_size); i += 4) {
                int x_pu = (x0 + i) >> log2_min_pu_size;
                MvField *top  = &tab_mvf[yp_pu * min_pu_width + x_pu];
                MvField *curr = &tab_mvf[yq_pu * min_pu_width + x_pu];

                bs = boundary_strength(s, curr, top, refPicList);
                s->horizontal_bs[((x0 + i) + (y0 + j) * s->bs_width) >> 2] = bs;
            }
        }

        // bs for TU internal vertical PU boundaries
        for (j = 0; j < (1 << log2_trafo_size); j += 4) {
            int y_pu = (y0 + j) >> log2_min_pu_size;

            for (i = 8; i < (1 << log2_trafo_size); i += 8) {
                int xp_pu = (x0 + i - 1) >> log2_min_pu_size;
                int xq_pu = (x0 + i)     >> log2_min_pu_size;
                MvField *left = &tab_mvf[y_pu * min_pu_width + xp_pu];
                MvField *curr = &tab_mvf[y_pu * min_pu_width + xq_pu];

                bs = boundary_strength(s, curr, left, refPicList);
                s->vertical_bs[((x0 + i) >> 3) + ((y0 + j) >> 2) * s->bs_width] = bs;
            }
        }
    }
}

#undef LUMA
#undef CB
#undef CR

void ff_hevc_hls_filter(HEVCContext *s, int x, int y, int ctb_size)
{
    deblocking_filter_CTB(s, x, y);
    if (s->sps->sao_enabled) {
        int x_end = x >= s->sps->width  - ctb_size;
        int y_end = y >= s->sps->height - ctb_size;
        if (y && x)
            sao_filter_CTB(s, x - ctb_size, y - ctb_size);
        if (x && y_end)
            sao_filter_CTB(s, x - ctb_size, y);
        if (y && x_end) {
            sao_filter_CTB(s, x, y - ctb_size);
            if (s->threads_type & FF_THREAD_FRAME )
                ff_thread_report_progress(&s->ref->tf, y - ctb_size, 0);
        }
        if (x_end && y_end) {
            sao_filter_CTB(s, x , y);
            if (s->threads_type & FF_THREAD_FRAME )
                ff_thread_report_progress(&s->ref->tf, y, 0);
        }
    } else {
        if (y && x >= s->sps->width - ctb_size)
            if (s->threads_type & FF_THREAD_FRAME )
                ff_thread_report_progress(&s->ref->tf, y, 0);
    }
}

void ff_hevc_hls_filters(HEVCContext *s, int x_ctb, int y_ctb, int ctb_size)
{
    int x_end = x_ctb >= s->sps->width  - ctb_size;
    int y_end = y_ctb >= s->sps->height - ctb_size;
    if (y_ctb && x_ctb)
        ff_hevc_hls_filter(s, x_ctb - ctb_size, y_ctb - ctb_size, ctb_size);
    if (y_ctb && x_end)
        ff_hevc_hls_filter(s, x_ctb, y_ctb - ctb_size, ctb_size);
    if (x_ctb && y_end)
        ff_hevc_hls_filter(s, x_ctb - ctb_size, y_ctb, ctb_size);
}
