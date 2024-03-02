/*
 * VVC filters
 *
 * Copyright (C) 2021 Nuo Mi
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
#include "libavutil/frame.h"

#include "vvc_ctu.h"
#include "vvc_data.h"
#include "vvc_filter.h"
#include "vvc_refs.h"

#define LEFT        0
#define TOP         1
#define RIGHT       2
#define BOTTOM      3
#define MAX_EDGES   4

#define DEFAULT_INTRA_TC_OFFSET 2

//Table 43 Derivation of threshold variables beta' and tc' from input Q
static const uint16_t tctable[66] = {
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   3,   4,   4,   4,   4,   5,   5,   5,   5,   7,   7,   8,   9,  10,
     10,  11,  13,  14,  15,  17,  19,  21,  24,  25,  29,  33,  36,  41,  45,  51,
     57,  64,  71,  80,  89, 100, 112, 125, 141, 157, 177, 198, 222, 250, 280, 314,
    352, 395,
};

//Table 43 Derivation of threshold variables beta' and tc' from input Q
static const uint8_t betatable[64] = {
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      6,   7,   8,   9,  10,  11,  12,  13,  14,  15,  16,  17,  18,  20,  22,  24,
     26,  28,  30,  32,  34,  36,  38,  40,  42,  44,  46,  48,  50,  52,  54,  56,
     58,  60,  62,  64,  66,  68,  70,  72,  74,  76,  78,  80,  82,  84,  86,  88,
};

static int get_qPc(const VVCFrameContext *fc, const int x0, const int y0, const int chroma)
{
    const int x            = x0 >> MIN_TU_LOG2;
    const int y            = y0 >> MIN_TU_LOG2;
    const int min_tu_width = fc->ps.pps->min_tu_width;
    return fc->tab.qp[chroma][x + y * min_tu_width];
}

static void copy_ctb(uint8_t *dst, const uint8_t *src, const int width, const int height,
    const ptrdiff_t dst_stride, const ptrdiff_t src_stride)
{
    for (int y = 0; y < height; y++) {
        memcpy(dst, src, width);

        dst += dst_stride;
        src += src_stride;
    }
}

static void copy_pixel(uint8_t *dst, const uint8_t *src, const int pixel_shift)
{
    if (pixel_shift)
        *(uint16_t *)dst = *(uint16_t *)src;
    else
        *dst = *src;
}

static void copy_vert(uint8_t *dst, const uint8_t *src, const int pixel_shift, const int height,
    const ptrdiff_t dst_stride, const ptrdiff_t src_stride)
{
    int i;
    if (pixel_shift == 0) {
        for (i = 0; i < height; i++) {
            *dst = *src;
            dst += dst_stride;
            src += src_stride;
        }
    } else {
        for (i = 0; i < height; i++) {
            *(uint16_t *)dst = *(uint16_t *)src;
            dst += dst_stride;
            src += src_stride;
        }
    }
}

static void copy_ctb_to_hv(VVCFrameContext *fc, const uint8_t *src,
    const ptrdiff_t src_stride, const int x, const int y, const int width, const int height,
    const int c_idx, const int x_ctb, const int y_ctb, const int top)
{
    const int ps = fc->ps.sps->pixel_shift;
    const int w  = fc->ps.pps->width >> fc->ps.sps->hshift[c_idx];
    const int h  = fc->ps.pps->height >> fc->ps.sps->vshift[c_idx];

    if (top) {
        /* top */
        memcpy(fc->tab.sao_pixel_buffer_h[c_idx] + (((2 * y_ctb) * w + x) << ps),
            src, width << ps);
    } else {
        /* bottom */
        memcpy(fc->tab.sao_pixel_buffer_h[c_idx] + (((2 * y_ctb + 1) * w + x) << ps),
            src + src_stride * (height - 1), width << ps);

        /* copy vertical edges */
        copy_vert(fc->tab.sao_pixel_buffer_v[c_idx] + (((2 * x_ctb) * h + y) << ps), src, ps, height, 1 << ps, src_stride);
        copy_vert(fc->tab.sao_pixel_buffer_v[c_idx] + (((2 * x_ctb + 1) * h + y) << ps), src + ((width - 1) << ps), ps, height, 1 << ps, src_stride);
    }
}

static void sao_copy_ctb_to_hv(VVCLocalContext *lc, const int rx, const int ry, const int top)
{
    VVCFrameContext *fc  = lc->fc;
    const int ctb_size_y = fc->ps.sps->ctb_size_y;
    const int x0         = rx << fc->ps.sps->ctb_log2_size_y;
    const int y0         = ry << fc->ps.sps->ctb_log2_size_y;

    for (int c_idx = 0; c_idx < (fc->ps.sps->r->sps_chroma_format_idc ? 3 : 1); c_idx++) {
        const int x                = x0 >> fc->ps.sps->hshift[c_idx];
        const int y                = y0 >> fc->ps.sps->vshift[c_idx];
        const ptrdiff_t src_stride = fc->frame->linesize[c_idx];
        const int ctb_size_h       = ctb_size_y >> fc->ps.sps->hshift[c_idx];
        const int ctb_size_v       = ctb_size_y >> fc->ps.sps->vshift[c_idx];
        const int width            = FFMIN(ctb_size_h, (fc->ps.pps->width  >> fc->ps.sps->hshift[c_idx]) - x);
        const int height           = FFMIN(ctb_size_v, (fc->ps.pps->height >> fc->ps.sps->vshift[c_idx]) - y);
        const uint8_t *src          = &fc->frame->data[c_idx][y * src_stride + (x << fc->ps.sps->pixel_shift)];
        copy_ctb_to_hv(fc, src, src_stride, x, y, width, height, c_idx, rx, ry, top);
    }
}

void ff_vvc_sao_copy_ctb_to_hv(VVCLocalContext *lc, const int rx, const int ry, const int last_row)
{
    if (ry)
        sao_copy_ctb_to_hv(lc, rx, ry - 1, 0);

    sao_copy_ctb_to_hv(lc, rx, ry, 1);

    if (last_row)
        sao_copy_ctb_to_hv(lc, rx, ry, 0);
}

void ff_vvc_sao_filter(VVCLocalContext *lc, int x, int y)
{
    VVCFrameContext *fc  = lc->fc;
    const int ctb_size_y = fc->ps.sps->ctb_size_y;
    static const uint8_t sao_tab[16] = { 0, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8 };
    int c_idx;
    int edges[4];  // 0 left 1 top 2 right 3 bottom
    const int x_ctb      = x >> fc->ps.sps->ctb_log2_size_y;
    const int y_ctb      = y >> fc->ps.sps->ctb_log2_size_y;
    const SAOParams *sao = &CTB(fc->tab.sao, x_ctb, y_ctb);
    // flags indicating unfilterable edges
    uint8_t vert_edge[]          = { 0, 0 };
    uint8_t horiz_edge[]         = { 0, 0 };
    uint8_t diag_edge[]          = { 0, 0, 0, 0 };
    const uint8_t lfase          = fc->ps.pps->r->pps_loop_filter_across_slices_enabled_flag;
    const uint8_t no_tile_filter = fc->ps.pps->r->num_tiles_in_pic > 1 &&
                               !fc->ps.pps->r->pps_loop_filter_across_tiles_enabled_flag;
    const uint8_t restore        = no_tile_filter || !lfase;
    uint8_t left_tile_edge   = 0;
    uint8_t right_tile_edge  = 0;
    uint8_t up_tile_edge     = 0;
    uint8_t bottom_tile_edge = 0;

    edges[LEFT]   = x_ctb == 0;
    edges[TOP]    = y_ctb == 0;
    edges[RIGHT]  = x_ctb == fc->ps.pps->ctb_width  - 1;
    edges[BOTTOM] = y_ctb == fc->ps.pps->ctb_height - 1;

    if (restore) {
        if (!edges[LEFT]) {
            left_tile_edge  = no_tile_filter && fc->ps.pps->ctb_to_col_bd[x_ctb] == x_ctb;
            vert_edge[0]    = (!lfase && CTB(fc->tab.slice_idx, x_ctb, y_ctb) != CTB(fc->tab.slice_idx, x_ctb - 1, y_ctb)) || left_tile_edge;
        }
        if (!edges[RIGHT]) {
            right_tile_edge = no_tile_filter && fc->ps.pps->ctb_to_col_bd[x_ctb] != fc->ps.pps->ctb_to_col_bd[x_ctb + 1];
            vert_edge[1]    = (!lfase && CTB(fc->tab.slice_idx, x_ctb, y_ctb) != CTB(fc->tab.slice_idx, x_ctb + 1, y_ctb)) || right_tile_edge;
        }
        if (!edges[TOP]) {
            up_tile_edge     = no_tile_filter && fc->ps.pps->ctb_to_row_bd[y_ctb] == y_ctb;
            horiz_edge[0]    = (!lfase && CTB(fc->tab.slice_idx, x_ctb, y_ctb) != CTB(fc->tab.slice_idx, x_ctb, y_ctb - 1)) || up_tile_edge;
        }
        if (!edges[BOTTOM]) {
            bottom_tile_edge = no_tile_filter && fc->ps.pps->ctb_to_row_bd[y_ctb] != fc->ps.pps->ctb_to_row_bd[y_ctb + 1];
            horiz_edge[1]    = (!lfase && CTB(fc->tab.slice_idx, x_ctb, y_ctb) != CTB(fc->tab.slice_idx, x_ctb, y_ctb + 1)) || bottom_tile_edge;
        }
        if (!edges[LEFT] && !edges[TOP]) {
            diag_edge[0] = (!lfase && CTB(fc->tab.slice_idx, x_ctb, y_ctb) != CTB(fc->tab.slice_idx, x_ctb - 1, y_ctb - 1)) || left_tile_edge || up_tile_edge;
        }
        if (!edges[TOP] && !edges[RIGHT]) {
            diag_edge[1] = (!lfase && CTB(fc->tab.slice_idx, x_ctb, y_ctb) != CTB(fc->tab.slice_idx, x_ctb + 1, y_ctb - 1)) || right_tile_edge || up_tile_edge;
        }
        if (!edges[RIGHT] && !edges[BOTTOM]) {
            diag_edge[2] = (!lfase && CTB(fc->tab.slice_idx, x_ctb, y_ctb) != CTB(fc->tab.slice_idx, x_ctb + 1, y_ctb + 1)) || right_tile_edge || bottom_tile_edge;
        }
        if (!edges[LEFT] && !edges[BOTTOM]) {
            diag_edge[3] = (!lfase && CTB(fc->tab.slice_idx, x_ctb, y_ctb) != CTB(fc->tab.slice_idx, x_ctb - 1, y_ctb + 1)) || left_tile_edge || bottom_tile_edge;
        }
    }

    for (c_idx = 0; c_idx < (fc->ps.sps->r->sps_chroma_format_idc ? 3 : 1); c_idx++) {
        int x0       = x >> fc->ps.sps->hshift[c_idx];
        int y0       = y >> fc->ps.sps->vshift[c_idx];
        ptrdiff_t src_stride = fc->frame->linesize[c_idx];
        int ctb_size_h = ctb_size_y >> fc->ps.sps->hshift[c_idx];
        int ctb_size_v = ctb_size_y >> fc->ps.sps->vshift[c_idx];
        int width    = FFMIN(ctb_size_h, (fc->ps.pps->width  >> fc->ps.sps->hshift[c_idx]) - x0);
        int height   = FFMIN(ctb_size_v, (fc->ps.pps->height >> fc->ps.sps->vshift[c_idx]) - y0);
        int tab      = sao_tab[(FFALIGN(width, 8) >> 3) - 1];
        uint8_t *src = &fc->frame->data[c_idx][y0 * src_stride + (x0 << fc->ps.sps->pixel_shift)];
        ptrdiff_t dst_stride;
        uint8_t *dst;

        switch (sao->type_idx[c_idx]) {
        case SAO_BAND:
            fc->vvcdsp.sao.band_filter[tab](src, src, src_stride, src_stride,
                sao->offset_val[c_idx], sao->band_position[c_idx], width, height);
            break;
        case SAO_EDGE:
        {
            const int w = fc->ps.pps->width >> fc->ps.sps->hshift[c_idx];
            const int h = fc->ps.pps->height >> fc->ps.sps->vshift[c_idx];
            const int sh = fc->ps.sps->pixel_shift;

            dst_stride = 2*MAX_PB_SIZE + AV_INPUT_BUFFER_PADDING_SIZE;
            dst = lc->sao_buffer + dst_stride + AV_INPUT_BUFFER_PADDING_SIZE;

            if (!edges[TOP]) {
                const int left = 1 - edges[LEFT];
                const int right = 1 - edges[RIGHT];
                const uint8_t *src1;
                uint8_t *dst1;
                int pos = 0;

                dst1 = dst - dst_stride - (left << sh);
                src1 = fc->tab.sao_pixel_buffer_h[c_idx] + (((2 * y_ctb - 1) * w + x0 - left) << sh);
                if (left) {
                    copy_pixel(dst1, src1, sh);
                    pos += (1 << sh);
                }
                memcpy(dst1 + pos, src1 + pos, width << sh);
                if (right) {
                    pos += width << sh;
                    copy_pixel(dst1 + pos, src1 + pos, sh);
                }
            }
            if (!edges[BOTTOM]) {
                const int left = 1 - edges[LEFT];
                const int right = 1 - edges[RIGHT];
                const uint8_t *src1;
                uint8_t *dst1;
                int pos = 0;

                dst1 = dst + height * dst_stride - (left << sh);
                src1 = fc->tab.sao_pixel_buffer_h[c_idx] + (((2 * y_ctb + 2) * w + x0 - left) << sh);
                if (left) {
                    copy_pixel(dst1, src1, sh);
                    pos += (1 << sh);
                }
                memcpy(dst1 + pos, src1 + pos, width << sh);
                if (right) {
                    pos += width << sh;
                    copy_pixel(dst1 + pos, src1 + pos, sh);
                }
            }
            if (!edges[LEFT]) {
                copy_vert(dst - (1 << sh),
                    fc->tab.sao_pixel_buffer_v[c_idx] + (((2 * x_ctb - 1) * h + y0) << sh),
                    sh, height, dst_stride, 1 << sh);
            }
            if (!edges[RIGHT]) {
                copy_vert(dst + (width << sh),
                    fc->tab.sao_pixel_buffer_v[c_idx] + (((2 * x_ctb + 2) * h + y0) << sh),
                    sh, height, dst_stride, 1 << sh);
            }

            copy_ctb(dst, src,  width << sh, height, dst_stride, src_stride);
            fc->vvcdsp.sao.edge_filter[tab](src, dst, src_stride, sao->offset_val[c_idx],
                sao->eo_class[c_idx], width, height);
            fc->vvcdsp.sao.edge_restore[restore](src, dst, src_stride, dst_stride,
                sao, edges, width, height, c_idx, vert_edge, horiz_edge, diag_edge);
            break;
        }
        }
    }
}

#define TAB_BS(t, x, y)       (t)[((y) >> 2) * (fc->tab.sz.bs_width) + ((x) >> 2)]
#define TAB_MAX_LEN(t, x, y)  (t)[((y) >> 2) * (fc->tab.sz.bs_width) + ((x) >> 2)]

//8 samples a time
#define DEBLOCK_STEP            8
#define LUMA_GRID               4
#define CHROMA_GRID             8

static int boundary_strength(const VVCLocalContext *lc, const MvField *curr, const MvField *neigh,
    const RefPicList *neigh_rpl)
{
    RefPicList *rpl = lc->sc->rpl;

    if (curr->pred_flag == PF_IBC)
        return FFABS(neigh->mv[0].x - curr->mv[0].x) >= 8 || FFABS(neigh->mv[0].y - curr->mv[0].y) >= 8;

    if (curr->pred_flag == PF_BI &&  neigh->pred_flag == PF_BI) {
        // same L0 and L1
        if (rpl[0].list[curr->ref_idx[0]] == neigh_rpl[0].list[neigh->ref_idx[0]]  &&
            rpl[0].list[curr->ref_idx[0]] == rpl[1].list[curr->ref_idx[1]] &&
            neigh_rpl[0].list[neigh->ref_idx[0]] == neigh_rpl[1].list[neigh->ref_idx[1]]) {
            if ((FFABS(neigh->mv[0].x - curr->mv[0].x) >= 8 || FFABS(neigh->mv[0].y - curr->mv[0].y) >= 8 ||
                 FFABS(neigh->mv[1].x - curr->mv[1].x) >= 8 || FFABS(neigh->mv[1].y - curr->mv[1].y) >= 8) &&
                (FFABS(neigh->mv[1].x - curr->mv[0].x) >= 8 || FFABS(neigh->mv[1].y - curr->mv[0].y) >= 8 ||
                 FFABS(neigh->mv[0].x - curr->mv[1].x) >= 8 || FFABS(neigh->mv[0].y - curr->mv[1].y) >= 8))
                return 1;
            else
                return 0;
        } else if (neigh_rpl[0].list[neigh->ref_idx[0]] == rpl[0].list[curr->ref_idx[0]] &&
                   neigh_rpl[1].list[neigh->ref_idx[1]] == rpl[1].list[curr->ref_idx[1]]) {
            if (FFABS(neigh->mv[0].x - curr->mv[0].x) >= 8 || FFABS(neigh->mv[0].y - curr->mv[0].y) >= 8 ||
                FFABS(neigh->mv[1].x - curr->mv[1].x) >= 8 || FFABS(neigh->mv[1].y - curr->mv[1].y) >= 8)
                return 1;
            else
                return 0;
        } else if (neigh_rpl[1].list[neigh->ref_idx[1]] == rpl[0].list[curr->ref_idx[0]] &&
                   neigh_rpl[0].list[neigh->ref_idx[0]] == rpl[1].list[curr->ref_idx[1]]) {
            if (FFABS(neigh->mv[1].x - curr->mv[0].x) >= 8 || FFABS(neigh->mv[1].y - curr->mv[0].y) >= 8 ||
                FFABS(neigh->mv[0].x - curr->mv[1].x) >= 8 || FFABS(neigh->mv[0].y - curr->mv[1].y) >= 8)
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
            ref_A = rpl[0].list[curr->ref_idx[0]];
        } else {
            A     = curr->mv[1];
            ref_A = rpl[1].list[curr->ref_idx[1]];
        }

        if (neigh->pred_flag & 1) {
            B     = neigh->mv[0];
            ref_B = neigh_rpl[0].list[neigh->ref_idx[0]];
        } else {
            B     = neigh->mv[1];
            ref_B = neigh_rpl[1].list[neigh->ref_idx[1]];
        }

        if (ref_A == ref_B) {
            if (FFABS(A.x - B.x) >= 8 || FFABS(A.y - B.y) >= 8)
                return 1;
            else
                return 0;
        } else
            return 1;
    }

    return 1;
}

//part of 8.8.3.3 Derivation process of transform block boundary
static void derive_max_filter_length_luma(const VVCFrameContext *fc, const int qx, const int qy,
                                          const int is_intra, const int has_subblock, const int vertical, uint8_t *max_len_p, uint8_t *max_len_q)
{
    const int px =  vertical ? qx - 1 : qx;
    const int py = !vertical ? qy - 1 : qy;
    const uint8_t *tb_size = vertical ? fc->tab.tb_width[LUMA] : fc->tab.tb_height[LUMA];
    const int size_p = tb_size[(py >> MIN_TU_LOG2) * fc->ps.pps->min_tu_width + (px >> MIN_TU_LOG2)];
    const int size_q = tb_size[(qy >> MIN_TU_LOG2) * fc->ps.pps->min_tu_width + (qx >> MIN_TU_LOG2)];
    const int min_cb_log2 = fc->ps.sps->min_cb_log2_size_y;
    const int off_p = (py >> min_cb_log2) * fc->ps.pps->min_cb_width + (px >> min_cb_log2);
    if (size_p <= 4 || size_q <= 4) {
        *max_len_p = *max_len_q = 1;
    } else {
        *max_len_p = *max_len_q = 3;
        if (size_p >= 32)
            *max_len_p = 7;
        if (size_q >= 32)
            *max_len_q = 7;
    }
    if (has_subblock)
        *max_len_q = FFMIN(5, *max_len_q);
    if (fc->tab.msf[off_p] || fc->tab.iaf[off_p])
        *max_len_p = FFMIN(5, *max_len_p);
}

static void vvc_deblock_subblock_bs_vertical(const VVCLocalContext *lc,
    const int cb_x, const int cb_y, const int x0, const int y0, const int width, const int height)
{
    const VVCFrameContext  *fc = lc->fc;
    const MvField *tab_mvf     = fc->tab.mvf;
    const RefPicList *rpl      = lc->sc->rpl;
    const int min_pu_width     = fc->ps.pps->min_pu_width;
    const int log2_min_pu_size = MIN_PU_LOG2;

    // bs for TU internal vertical PU boundaries
    for (int j = 0; j < height; j += 4) {
        const int y_pu = (y0 + j) >> log2_min_pu_size;

        for (int i = 8 - ((x0 - cb_x) % 8); i < width; i += 8) {
            const int xp_pu = (x0 + i - 1) >> log2_min_pu_size;
            const int xq_pu = (x0 + i)     >> log2_min_pu_size;
            const MvField *left = &tab_mvf[y_pu * min_pu_width + xp_pu];
            const MvField *curr = &tab_mvf[y_pu * min_pu_width + xq_pu];
            const int x = x0 + i;
            const int y = y0 + j;
            const int bs = boundary_strength(lc, curr, left, rpl);
            uint8_t max_len_p = 0, max_len_q = 0;

            TAB_BS(fc->tab.vertical_bs[LUMA], x, y) = bs;

            if (i == 4 || i == width - 4)
                max_len_p = max_len_q = 1;
            else if (i == 8 || i == width - 8)
                max_len_p = max_len_q = 2;
            else
                max_len_p = max_len_q = 3;

            TAB_MAX_LEN(fc->tab.vertical_p, x, y) = max_len_p;
            TAB_MAX_LEN(fc->tab.vertical_q, x, y) = max_len_q;
        }
    }
}

static void vvc_deblock_subblock_bs_horizontal(const VVCLocalContext *lc,
    const int cb_x, const int cb_y, const int x0, const int y0, const int width, const int height)
{
    const VVCFrameContext  *fc = lc->fc;
    const MvField* tab_mvf     = fc->tab.mvf;
    const RefPicList* rpl      = lc->sc->rpl;
    const int min_pu_width     = fc->ps.pps->min_pu_width;
    const int log2_min_pu_size = MIN_PU_LOG2;

    // bs for TU internal horizontal PU boundaries
    for (int j = 8 - ((y0 - cb_y) % 8); j < height; j += 8) {
        int yp_pu = (y0 + j - 1) >> log2_min_pu_size;
        int yq_pu = (y0 + j)     >> log2_min_pu_size;

        for (int i = 0; i < width; i += 4) {
            const int x_pu = (x0 + i) >> log2_min_pu_size;
            const MvField *top  = &tab_mvf[yp_pu * min_pu_width + x_pu];
            const MvField *curr = &tab_mvf[yq_pu * min_pu_width + x_pu];
            const int x = x0 + i;
            const int y = y0 + j;
            const int bs = boundary_strength(lc, curr, top, rpl);
            uint8_t max_len_p = 0, max_len_q = 0;

            TAB_BS(fc->tab.horizontal_bs[LUMA], x, y) = bs;

            //fixme:
            //edgeTbFlags[ x − sbW ][ y ] is equal to 1
            //edgeTbFlags[ x + sbW ][ y ] is equal to 1
            if (j == 4 || j == height - 4)
                max_len_p = max_len_q = 1;
            else if (j == 8 || j == height - 8)
                max_len_p = max_len_q = 2;
            else
                max_len_p = max_len_q = 3;
            TAB_MAX_LEN(fc->tab.horizontal_p, x, y) = max_len_p;
            TAB_MAX_LEN(fc->tab.horizontal_q, x, y) = max_len_q;
        }
    }
}

static av_always_inline int deblock_bs(const VVCLocalContext *lc,
    const int x_p, const int y_p, const int x_q, const int y_q,
    const RefPicList *rpl_p, const int c_idx, const int off_to_cb, const uint8_t has_sub_block)
{
    const VVCFrameContext *fc  = lc->fc;
    const MvField *tab_mvf     = fc->tab.mvf;
    const int log2_min_pu_size = MIN_PU_LOG2;
    const int log2_min_tu_size = MIN_TU_LOG2;
    const int log2_min_cb_size = fc->ps.sps->min_cb_log2_size_y;
    const int min_pu_width     = fc->ps.pps->min_pu_width;
    const int min_tu_width     = fc->ps.pps->min_tu_width;
    const int min_cb_width     = fc->ps.pps->min_cb_width;
    const int pu_p             = (y_p >> log2_min_pu_size) * min_pu_width  + (x_p >>  log2_min_pu_size);
    const int pu_q             = (y_q >> log2_min_pu_size) * min_pu_width  + (x_q >>  log2_min_pu_size);
    const MvField *mvf_p       = &tab_mvf[pu_p];
    const MvField *mvf_q       = &tab_mvf[pu_q];
    const uint8_t chroma       = !!c_idx;
    const int tu_p             = (y_p >> log2_min_tu_size) * min_tu_width  + (x_p >>  log2_min_tu_size);
    const int tu_q             = (y_q >> log2_min_tu_size) * min_tu_width  + (x_q >>  log2_min_tu_size);
    const uint8_t pcmf         = fc->tab.pcmf[chroma][tu_p] && fc->tab.pcmf[chroma][tu_q];
    const int cb_p             = (y_p >> log2_min_cb_size) * min_cb_width  + (x_p >>  log2_min_cb_size);
    const int cb_q             = (y_q >> log2_min_cb_size) * min_cb_width  + (x_q >>  log2_min_cb_size);
    const uint8_t intra        = fc->tab.cpm[chroma][cb_p] == MODE_INTRA || fc->tab.cpm[chroma][cb_q] == MODE_INTRA;
    const uint8_t same_mode    = fc->tab.cpm[chroma][cb_p] == fc->tab.cpm[chroma][cb_q];

    if (pcmf)
        return 0;

    if (intra || mvf_p->ciip_flag || mvf_q->ciip_flag)
        return 2;

    if (chroma) {
        return fc->tab.tu_coded_flag[c_idx][tu_p] ||
               fc->tab.tu_coded_flag[c_idx][tu_q] ||
               fc->tab.tu_joint_cbcr_residual_flag[tu_p] ||
               fc->tab.tu_joint_cbcr_residual_flag[tu_q];
    }

    if (fc->tab.tu_coded_flag[LUMA][tu_p] || fc->tab.tu_coded_flag[LUMA][tu_q])
        return 1;

    if ((off_to_cb && ((off_to_cb % 8) || !has_sub_block)))
        return 0;                                     // inside a cu, not aligned to 8 or with no subblocks

    if (!same_mode)
        return 1;

    return boundary_strength(lc, mvf_q, mvf_p, rpl_p);
}

static void vvc_deblock_bs_luma_vertical(const VVCLocalContext *lc,
    const int x0, const int y0, const int width, const int height)
{
    const VVCFrameContext *fc  = lc->fc;
    const MvField *tab_mvf     = fc->tab.mvf;
    const int log2_min_pu_size = MIN_PU_LOG2;
    const int min_pu_width     = fc->ps.pps->min_pu_width;
    const int min_cb_log2      = fc->ps.sps->min_cb_log2_size_y;
    const int min_cb_width     = fc->ps.pps->min_cb_width;
    const int is_intra         = tab_mvf[(y0 >> log2_min_pu_size) * min_pu_width +
        (x0 >> log2_min_pu_size)].pred_flag == PF_INTRA;
    int boundary_left;
    int has_vertical_sb = 0;

    const int off_q            = (y0 >> min_cb_log2) * min_cb_width + (x0 >> min_cb_log2);
    const int cb_x             = fc->tab.cb_pos_x[LUMA][off_q];
    const int cb_y             = fc->tab.cb_pos_y[LUMA][off_q];
    const int cb_width         = fc->tab.cb_width[LUMA][off_q];
    const int off_x            = cb_x - x0;

    if (!is_intra) {
        if (fc->tab.msf[off_q] || fc->tab.iaf[off_q])
            has_vertical_sb = cb_width  > 8;
    }

    // bs for vertical TU boundaries
    boundary_left = x0 > 0 && !(x0 & 3);
    if (boundary_left &&
        ((!fc->ps.pps->r->pps_loop_filter_across_slices_enabled_flag &&
            lc->boundary_flags & BOUNDARY_LEFT_SLICE &&
            (x0 % (1 << fc->ps.sps->ctb_log2_size_y)) == 0) ||
            (!fc->ps.pps->r->pps_loop_filter_across_tiles_enabled_flag &&
            lc->boundary_flags & BOUNDARY_LEFT_TILE &&
            (x0 % (1 << fc->ps.sps->ctb_log2_size_y)) == 0)))
        boundary_left = 0;

    if (boundary_left) {
        const RefPicList *rpl_left =
            (lc->boundary_flags & BOUNDARY_LEFT_SLICE) ? ff_vvc_get_ref_list(fc, fc->ref, x0 - 1, y0) : lc->sc->rpl;
        for (int i = 0; i < height; i += 4) {
            uint8_t max_len_p, max_len_q;
            const int bs = deblock_bs(lc, x0 - 1, y0 + i, x0, y0 + i, rpl_left, 0, off_x, has_vertical_sb);

            TAB_BS(fc->tab.vertical_bs[LUMA], x0, (y0 + i)) = bs;

            derive_max_filter_length_luma(fc, x0, y0 + i, is_intra, has_vertical_sb, 1, &max_len_p, &max_len_q);
            TAB_MAX_LEN(fc->tab.vertical_p, x0, y0 + i) = max_len_p;
            TAB_MAX_LEN(fc->tab.vertical_q, x0, y0 + i) = max_len_q;
        }
    }

    if (!is_intra) {
        if (fc->tab.msf[off_q] || fc->tab.iaf[off_q])
            vvc_deblock_subblock_bs_vertical(lc, cb_x, cb_y, x0, y0, width, height);
    }
}

static void vvc_deblock_bs_luma_horizontal(const VVCLocalContext *lc,
    const int x0, const int y0, const int width, const int height)
{
    const VVCFrameContext *fc  = lc->fc;
    const MvField *tab_mvf     = fc->tab.mvf;
    const int log2_min_pu_size = MIN_PU_LOG2;
    const int min_pu_width     = fc->ps.pps->min_pu_width;
    const int min_cb_log2      = fc->ps.sps->min_cb_log2_size_y;
    const int min_cb_width     = fc->ps.pps->min_cb_width;
    const int is_intra = tab_mvf[(y0 >> log2_min_pu_size) * min_pu_width +
                           (x0 >> log2_min_pu_size)].pred_flag == PF_INTRA;
    int boundary_upper;
    int has_horizontal_sb = 0;

    const int off_q            = (y0 >> min_cb_log2) * min_cb_width + (x0 >> min_cb_log2);
    const int cb_x             = fc->tab.cb_pos_x[LUMA][off_q];
    const int cb_y             = fc->tab.cb_pos_y[LUMA][off_q];
    const int cb_height        = fc->tab.cb_height[LUMA][off_q];
    const int off_y            = y0 - cb_y;

    if (!is_intra) {
        if (fc->tab.msf[off_q] || fc->tab.iaf[off_q])
            has_horizontal_sb = cb_height > 8;
    }

    boundary_upper = y0 > 0 && !(y0 & 3);
    if (boundary_upper &&
        ((!fc->ps.pps->r->pps_loop_filter_across_slices_enabled_flag &&
            lc->boundary_flags & BOUNDARY_UPPER_SLICE &&
            (y0 % (1 << fc->ps.sps->ctb_log2_size_y)) == 0) ||
            (!fc->ps.pps->r->pps_loop_filter_across_tiles_enabled_flag &&
            lc->boundary_flags & BOUNDARY_UPPER_TILE &&
            (y0 % (1 << fc->ps.sps->ctb_log2_size_y)) == 0)))
        boundary_upper = 0;

    if (boundary_upper) {
        const RefPicList *rpl_top =
            (lc->boundary_flags & BOUNDARY_UPPER_SLICE) ? ff_vvc_get_ref_list(fc, fc->ref, x0, y0 - 1) : lc->sc->rpl;

        for (int i = 0; i < width; i += 4) {
            uint8_t max_len_p, max_len_q;
            const int bs = deblock_bs(lc, x0 + i, y0 - 1, x0 + i, y0, rpl_top, 0, off_y, has_horizontal_sb);

            TAB_BS(fc->tab.horizontal_bs[LUMA], x0 + i, y0) = bs;

            derive_max_filter_length_luma(fc, x0 + i, y0, is_intra, has_horizontal_sb, 0, &max_len_p, &max_len_q);
            TAB_MAX_LEN(fc->tab.horizontal_p, x0 + i, y0) = max_len_p;
            TAB_MAX_LEN(fc->tab.horizontal_q, x0 + i, y0) = max_len_q;
        }
    }

    if (!is_intra) {
        if (fc->tab.msf[off_q] || fc->tab.iaf[off_q])
            vvc_deblock_subblock_bs_horizontal(lc, cb_x, cb_y, x0, y0, width, height);
    }
}

static void vvc_deblock_bs_chroma_vertical(const VVCLocalContext *lc,
    const int x0, const int y0, const int width, const int height)
{
    const VVCFrameContext *fc = lc->fc;
    int boundary_left;

    // bs for vertical TU boundaries
    boundary_left = x0 > 0 && !(x0 & ((CHROMA_GRID << fc->ps.sps->hshift[1]) - 1));
    if (boundary_left &&
        ((!fc->ps.pps->r->pps_loop_filter_across_slices_enabled_flag &&
          lc->boundary_flags & BOUNDARY_LEFT_SLICE &&
          (x0 % (1 << fc->ps.sps->ctb_log2_size_y)) == 0) ||
         (!fc->ps.pps->r->pps_loop_filter_across_tiles_enabled_flag &&
          lc->boundary_flags & BOUNDARY_LEFT_TILE &&
          (x0 % (1 << fc->ps.sps->ctb_log2_size_y)) == 0)))
        boundary_left = 0;

    if (boundary_left) {
        for (int i = 0; i < height; i += 2) {
            for (int c_idx = CB; c_idx <= CR; c_idx++) {
                const int bs = deblock_bs(lc, x0 - 1, y0 + i, x0, y0 + i, NULL, c_idx, 0, 0);

                TAB_BS(fc->tab.vertical_bs[c_idx], x0, (y0 + i)) = bs;
            }
        }
    }
}

static void vvc_deblock_bs_chroma_horizontal(const VVCLocalContext *lc,
    const int x0, const int y0, const int width, const int height)
{
    const VVCFrameContext *fc = lc->fc;
    int boundary_upper;

    boundary_upper = y0 > 0 && !(y0 & ((CHROMA_GRID << fc->ps.sps->vshift[1]) - 1));
    if (boundary_upper &&
        ((!fc->ps.pps->r->pps_loop_filter_across_slices_enabled_flag &&
            lc->boundary_flags & BOUNDARY_UPPER_SLICE &&
            (y0 % (1 << fc->ps.sps->ctb_log2_size_y)) == 0) ||
            (!fc->ps.pps->r->pps_loop_filter_across_tiles_enabled_flag &&
                lc->boundary_flags & BOUNDARY_UPPER_TILE &&
                (y0 % (1 << fc->ps.sps->ctb_log2_size_y)) == 0)))
        boundary_upper = 0;

    if (boundary_upper) {
        for (int i = 0; i < width; i += 2) {
            for (int c_idx = CB; c_idx <= CR; c_idx++) {
                const int bs = deblock_bs(lc, x0 + i, y0 - 1, x0 + i, y0, NULL, c_idx, 0, 0);

                TAB_BS(fc->tab.horizontal_bs[c_idx], x0 + i, y0) = bs;
            }
        }
    }
}

typedef void (*deblock_bs_fn)(const VVCLocalContext *lc, const int x0, const int y0,
    const int width, const int height);

static void vvc_deblock_bs(const VVCLocalContext *lc, const int x0, const int y0, const int vertical)
{
    const VVCFrameContext *fc = lc->fc;
    const VVCSPS *sps  = fc->ps.sps;
    const VVCPPS *pps  = fc->ps.pps;
    const int ctb_size = sps->ctb_size_y;
    const int x_end    = FFMIN(x0 + ctb_size, pps->width) >> MIN_TU_LOG2;
    const int y_end    = FFMIN(y0 + ctb_size, pps->height) >> MIN_TU_LOG2;
    deblock_bs_fn deblock_bs[2][2] = {
        { vvc_deblock_bs_luma_horizontal, vvc_deblock_bs_chroma_horizontal },
        { vvc_deblock_bs_luma_vertical,   vvc_deblock_bs_chroma_vertical   }
    };

    for (int is_chroma = 0; is_chroma <= 1; is_chroma++) {
        const int hs = sps->hshift[is_chroma];
        const int vs = sps->vshift[is_chroma];
        for (int y = y0 >> MIN_TU_LOG2; y < y_end; y++) {
            for (int x = x0 >> MIN_TU_LOG2; x < x_end; x++) {
                const int off = y * fc->ps.pps->min_tu_width + x;
                if ((fc->tab.tb_pos_x0[is_chroma][off] >> MIN_TU_LOG2) == x && (fc->tab.tb_pos_y0[is_chroma][off] >> MIN_TU_LOG2) == y) {
                    deblock_bs[vertical][is_chroma](lc, x << MIN_TU_LOG2, y << MIN_TU_LOG2,
                        fc->tab.tb_width[is_chroma][off] << hs, fc->tab.tb_height[is_chroma][off] << vs);
                }
            }
        }
    }
}

//part of 8.8.3.3 Derivation process of transform block boundary
static void max_filter_length_luma(const VVCFrameContext *fc, const int qx, const int qy,
                                   const int vertical, uint8_t *max_len_p, uint8_t *max_len_q)
{
    const uint8_t *tab_len_p = vertical ? fc->tab.vertical_p : fc->tab.horizontal_p;
    const uint8_t *tab_len_q = vertical ? fc->tab.vertical_q : fc->tab.horizontal_q;
    *max_len_p = TAB_MAX_LEN(tab_len_p, qx, qy);
    *max_len_q = TAB_MAX_LEN(tab_len_q, qx, qy);
}

//part of 8.8.3.3 Derivation process of transform block boundary
static void max_filter_length_chroma(const VVCFrameContext *fc, const int qx, const int qy,
                                     const int vertical, const int horizontal_ctu_edge, const int bs, uint8_t *max_len_p, uint8_t *max_len_q)
{
    const int px =  vertical ? qx - 1 : qx;
    const int py = !vertical ? qy - 1 : qy;
    const uint8_t *tb_size = vertical ? fc->tab.tb_width[CHROMA] : fc->tab.tb_height[CHROMA];

    const int size_p = tb_size[(py >> MIN_TU_LOG2) * fc->ps.pps->min_tu_width + (px >> MIN_TU_LOG2)];
    const int size_q = tb_size[(qy >> MIN_TU_LOG2) * fc->ps.pps->min_tu_width + (qx >> MIN_TU_LOG2)];
    if (size_p >= 8 && size_q >= 8) {
        *max_len_p = *max_len_q = 3;
        if (horizontal_ctu_edge)
            *max_len_p = 1;
    } else {
        //part of 8.8.3.6.4 Decision process for chroma block edges
        *max_len_p = *max_len_q = (bs == 2);
    }
}

static void max_filter_length(const VVCFrameContext *fc, const int qx, const int qy,
    const int c_idx, const int vertical, const int horizontal_ctu_edge, const int bs, uint8_t *max_len_p, uint8_t *max_len_q)
{
    if (!c_idx)
        max_filter_length_luma(fc, qx, qy, vertical, max_len_p, max_len_q);
    else
        max_filter_length_chroma(fc, qx, qy, vertical, horizontal_ctu_edge, bs, max_len_p, max_len_q);
}

#define TC_CALC(qp, bs)                                                 \
    tctable[av_clip((qp) + DEFAULT_INTRA_TC_OFFSET * ((bs) - 1) +       \
                    (tc_offset & -2),                                   \
                    0, MAX_QP + DEFAULT_INTRA_TC_OFFSET)]

// part of 8.8.3.6.2 Decision process for luma block edges
static int get_qp_y(const VVCFrameContext *fc, const uint8_t *src, const int x, const int y, const int vertical)
{
    const VVCSPS *sps = fc->ps.sps;
    const int qp      = (ff_vvc_get_qPy(fc, x - vertical, y - !vertical) + ff_vvc_get_qPy(fc, x, y) + 1) >> 1;
    int qp_offset     = 0;
    int level;

    if (!sps->r->sps_ladf_enabled_flag)
        return qp;

    level = fc->vvcdsp.lf.ladf_level[vertical](src, fc->frame->linesize[LUMA]);
    qp_offset = sps->r->sps_ladf_lowest_interval_qp_offset;
    for (int i = 0; i < sps->num_ladf_intervals - 1 && level > sps->ladf_interval_lower_bound[i + 1]; i++)
        qp_offset = sps->r->sps_ladf_qp_offset[i];

    return qp + qp_offset;
}

// part of 8.8.3.6.2 Decision process for luma block edges
static int get_qp_c(const VVCFrameContext *fc, const int x, const int y, const int c_idx, const int vertical)
{
    const VVCSPS *sps = fc->ps.sps;
    return (get_qPc(fc, x - vertical, y - !vertical, c_idx) + get_qPc(fc, x, y, c_idx) - 2 * sps->qp_bd_offset + 1) >> 1;
}

static int get_qp(const VVCFrameContext *fc, const uint8_t *src, const int x, const int y, const int c_idx, const int vertical)
{
    if (!c_idx)
        return get_qp_y(fc, src, x, y, vertical);
    return get_qp_c(fc, x, y, c_idx, vertical);
}

void ff_vvc_deblock_vertical(const VVCLocalContext *lc, int x0, int y0)
{
    VVCFrameContext *fc = lc->fc;
    const VVCSPS *sps   = fc->ps.sps;
    const int c_end     = sps->r->sps_chroma_format_idc ? VVC_MAX_SAMPLE_ARRAYS : 1;
    uint8_t *src;
    int x, y, qp;

    //not use this yet, may needed by plt.
    const uint8_t no_p[4] = { 0 };
    const uint8_t no_q[4] = { 0 } ;

    const int ctb_log2_size_y = fc->ps.sps->ctb_log2_size_y;
    int x_end, y_end;
    const int ctb_size = 1 << ctb_log2_size_y;
    const int ctb = (x0 >> ctb_log2_size_y) +
        (y0 >> ctb_log2_size_y) * fc->ps.pps->ctb_width;
    const DBParams  *params = fc->tab.deblock + ctb;

    vvc_deblock_bs(lc, x0, y0, 1);

    x_end = x0 + ctb_size;
    if (x_end > fc->ps.pps->width)
        x_end = fc->ps.pps->width;
    y_end = y0 + ctb_size;
    if (y_end > fc->ps.pps->height)
        y_end = fc->ps.pps->height;

    for (int c_idx = 0; c_idx < c_end; c_idx++) {
        const int hs          = sps->hshift[c_idx];
        const int vs          = sps->vshift[c_idx];
        const int grid        = c_idx ? (CHROMA_GRID << hs) : LUMA_GRID;
        const int tc_offset   = params->tc_offset[c_idx];
        const int beta_offset = params->beta_offset[c_idx];

        for (y = y0; y < y_end; y += (DEBLOCK_STEP << vs)) {
            for (x = x0 ? x0 : grid; x < x_end; x += grid) {
                int32_t bs[4], beta[4], tc[4], all_zero_bs = 1;
                uint8_t max_len_p[4], max_len_q[4];

                for (int i = 0; i < DEBLOCK_STEP >> (2 - vs); i++) {
                    const int dy = i << 2;
                    bs[i] = (y + dy < y_end) ? TAB_BS(fc->tab.vertical_bs[c_idx], x, y + dy) : 0;
                    if (bs[i]) {
                        src = &fc->frame->data[c_idx][((y + dy) >> vs) * fc->frame->linesize[c_idx] + ((x >> hs) << fc->ps.sps->pixel_shift)];
                        qp = get_qp(fc, src, x, y + dy, c_idx, 1);

                        beta[i] = betatable[av_clip(qp + beta_offset, 0, MAX_QP)];

                        max_filter_length(fc, x, y + dy, c_idx, 1, 0, bs[i], &max_len_p[i], &max_len_q[i]);
                        all_zero_bs = 0;
                    }
                    tc[i] = bs[i] ? TC_CALC(qp, bs[i]) : 0;
                }

                if (!all_zero_bs) {
                    src = &fc->frame->data[c_idx][(y >> vs) * fc->frame->linesize[c_idx] + ((x >> hs) << fc->ps.sps->pixel_shift)];
                    if (!c_idx) {
                        fc->vvcdsp.lf.filter_luma[1](src, fc->frame->linesize[c_idx],
                            beta, tc, no_p, no_q, max_len_p, max_len_q, 0);
                    } else {
                        fc->vvcdsp.lf.filter_chroma[1](src, fc->frame->linesize[c_idx],
                            beta, tc, no_p, no_q, max_len_p, max_len_q, vs);
                    }
                }
            }
        }
    }
}

void ff_vvc_deblock_horizontal(const VVCLocalContext *lc, int x0, int y0)
{
    VVCFrameContext *fc = lc->fc;
    const VVCSPS *sps   = fc->ps.sps;
    const int c_end     = fc->ps.sps->r->sps_chroma_format_idc ? VVC_MAX_SAMPLE_ARRAYS : 1;
    uint8_t* src;
    int x, y, qp;

    //not use this yet, may needed by plt.
    const uint8_t no_p[4] = { 0 };
    const uint8_t no_q[4] = { 0 } ;

    const int ctb_log2_size_y = fc->ps.sps->ctb_log2_size_y;
    int x_end, y_end;
    const int ctb_size = 1 << ctb_log2_size_y;
    const int ctb = (x0 >> ctb_log2_size_y) +
        (y0 >> ctb_log2_size_y) * fc->ps.pps->ctb_width;
    const DBParams *params = fc->tab.deblock + ctb;

    vvc_deblock_bs(lc, x0, y0, 0);

    x_end = x0 + ctb_size;
    if (x_end > fc->ps.pps->width)
        x_end = fc->ps.pps->width;
    y_end = y0 + ctb_size;
    if (y_end > fc->ps.pps->height)
        y_end = fc->ps.pps->height;

    for (int c_idx = 0; c_idx < c_end; c_idx++) {
        const int hs          = sps->hshift[c_idx];
        const int vs          = sps->vshift[c_idx];
        const int grid        = c_idx ? (CHROMA_GRID << vs) : LUMA_GRID;
        const int beta_offset = params->beta_offset[c_idx];
        const int tc_offset   = params->tc_offset[c_idx];

        for (y = y0; y < y_end; y += grid) {
            const uint8_t horizontal_ctu_edge = !(y % fc->ps.sps->ctb_size_y);
            if (!y)
                continue;

            for (x = x0 ? x0: 0; x < x_end; x += (DEBLOCK_STEP << hs)) {
                int32_t bs[4], beta[4], tc[4], all_zero_bs = 1;
                uint8_t max_len_p[4], max_len_q[4];

                for (int i = 0; i < DEBLOCK_STEP >> (2 - hs); i++) {
                    const int dx = i << 2;

                    bs[i] = (x + dx < x_end) ? TAB_BS(fc->tab.horizontal_bs[c_idx], x + dx, y) : 0;
                    if (bs[i]) {
                        src = &fc->frame->data[c_idx][(y >> vs) * fc->frame->linesize[c_idx] + (((x + dx)>> hs) << fc->ps.sps->pixel_shift)];
                        qp = get_qp(fc, src, x + dx, y, c_idx, 0);

                        beta[i] = betatable[av_clip(qp + beta_offset, 0, MAX_QP)];

                        max_filter_length(fc, x + dx, y, c_idx, 0, horizontal_ctu_edge, bs[i], &max_len_p[i], &max_len_q[i]);
                        all_zero_bs = 0;
                    }
                    tc[i] = bs[i] ? TC_CALC(qp, bs[i]) : 0;
                }
                if (!all_zero_bs) {
                    src = &fc->frame->data[c_idx][(y >> vs) * fc->frame->linesize[c_idx] + ((x >> hs) << fc->ps.sps->pixel_shift)];
                    if (!c_idx) {
                        fc->vvcdsp.lf.filter_luma[0](src, fc->frame->linesize[c_idx],
                            beta, tc, no_p, no_q, max_len_p, max_len_q, horizontal_ctu_edge);
                    } else {
                        fc->vvcdsp.lf.filter_chroma[0](src, fc->frame->linesize[c_idx],
                            beta, tc, no_p, no_q, max_len_p, max_len_q, hs);
                    }
                }
            }
        }
    }
}

static void alf_copy_border(uint8_t *dst, const uint8_t *src,
    const int pixel_shift, int width, const int height, const ptrdiff_t dst_stride, const ptrdiff_t src_stride)
{
    width <<= pixel_shift;
    for (int i = 0; i < height; i++) {
        memcpy(dst, src, width);
        dst += dst_stride;
        src += src_stride;
    }
}

static void alf_extend_vert(uint8_t *_dst, const uint8_t *_src,
    const int pixel_shift, const int width, const int height, ptrdiff_t stride)
{
    if (pixel_shift == 0) {
        for (int i = 0; i < height; i++) {
            memset(_dst, *_src, width);
            _src += stride;
            _dst += stride;
        }
    } else {
        const uint16_t *src = (const uint16_t *)_src;
        uint16_t *dst = (uint16_t *)_dst;
        stride >>= pixel_shift;

        for (int i = 0; i < height; i++) {
            for (int j = 0; j < width; j++)
                dst[j] = *src;
            src += stride;
            dst += stride;
        }
    }
}

static void alf_extend_horz(uint8_t *dst, const uint8_t *src,
    const int pixel_shift, int width, const int height, const ptrdiff_t stride)
{
    width <<= pixel_shift;
    for (int i = 0; i < height; i++) {
        memcpy(dst, src, width);
        dst += stride;
    }
}

static void alf_copy_ctb_to_hv(VVCFrameContext *fc, const uint8_t *src, const ptrdiff_t src_stride,
    const int x, const int y, const int width, const int height, const int x_ctb, const int y_ctb, const int c_idx)
{
    const int ps            = fc->ps.sps->pixel_shift;
    const int w             = fc->ps.pps->width >> fc->ps.sps->hshift[c_idx];
    const int h             = fc->ps.pps->height >> fc->ps.sps->vshift[c_idx];
    const int border_pixels = (c_idx == 0) ? ALF_BORDER_LUMA : ALF_BORDER_CHROMA;
    const int offset_h[]    = { 0, height - border_pixels };
    const int offset_v[]    = { 0, width  - border_pixels };

    /* copy horizontal edges */
    for (int i = 0; i < FF_ARRAY_ELEMS(offset_h); i++) {
        alf_copy_border(fc->tab.alf_pixel_buffer_h[c_idx][i] + ((border_pixels * y_ctb * w + x)<< ps),
            src + offset_h[i] * src_stride, ps, width, border_pixels, w << ps, src_stride);
    }
    /* copy vertical edges */
    for (int i = 0; i < FF_ARRAY_ELEMS(offset_v); i++) {
        alf_copy_border(fc->tab.alf_pixel_buffer_v[c_idx][i] + ((h * x_ctb + y) * (border_pixels << ps)),
            src + (offset_v[i] << ps), ps, border_pixels, height, border_pixels << ps, src_stride);
    }
}

static void alf_fill_border_h(uint8_t *dst, const ptrdiff_t dst_stride, const uint8_t *src, const ptrdiff_t src_stride,
    const uint8_t *border, const int width, const int border_pixels, const int ps, const int edge)
{
    if (edge)
        alf_extend_horz(dst, border, ps, width, border_pixels, dst_stride);
    else
        alf_copy_border(dst, src, ps, width, border_pixels, dst_stride, src_stride);
}

static void alf_fill_border_v(uint8_t *dst, const ptrdiff_t dst_stride, const uint8_t *src,
    const uint8_t *border, const int border_pixels, const int height, const int pixel_shift, const int *edges, const int edge)
{
    const ptrdiff_t src_stride = (border_pixels << pixel_shift);

    if (edge) {
        alf_extend_vert(dst, border, pixel_shift, border_pixels, height + 2 * border_pixels, dst_stride);
        return;
    }

    //left/right
    alf_copy_border(dst + dst_stride * border_pixels * edges[TOP], src + src_stride * border_pixels * edges[TOP],
        pixel_shift, border_pixels, height + (!edges[TOP] + !edges[BOTTOM]) * border_pixels, dst_stride, src_stride);

    //top left/right
    if (edges[TOP])
        alf_extend_horz(dst, dst + dst_stride * border_pixels, pixel_shift, border_pixels, border_pixels, dst_stride);

    //bottom left/right
    if (edges[BOTTOM]) {
        dst += dst_stride * (border_pixels + height);
        alf_extend_horz(dst, dst - dst_stride, pixel_shift, border_pixels, border_pixels, dst_stride);
    }
}

static void alf_prepare_buffer(VVCFrameContext *fc, uint8_t *_dst, const uint8_t *_src, const int x, const int y,
    const int x_ctb, const int y_ctb, const int width, const int height, const ptrdiff_t dst_stride, const ptrdiff_t src_stride,
    const int c_idx, const int *edges)
{
    const int ps = fc->ps.sps->pixel_shift;
    const int w = fc->ps.pps->width >> fc->ps.sps->hshift[c_idx];
    const int h = fc->ps.pps->height >> fc->ps.sps->vshift[c_idx];
    const int border_pixels = c_idx == 0 ? ALF_BORDER_LUMA : ALF_BORDER_CHROMA;
    uint8_t *dst, *src;

    copy_ctb(_dst, _src, width << ps, height, dst_stride, src_stride);

    //top
    src = fc->tab.alf_pixel_buffer_h[c_idx][1] + (((border_pixels * w) << ps) * (y_ctb - 1) + (x << ps));
    dst = _dst - border_pixels * dst_stride;
    alf_fill_border_h(dst, dst_stride, src, w  << ps, _dst, width, border_pixels, ps, edges[TOP]);

    //bottom
    src = fc->tab.alf_pixel_buffer_h[c_idx][0] + (((border_pixels * w) << ps) * (y_ctb + 1) + (x << ps));
    dst = _dst + height * dst_stride;
    alf_fill_border_h(dst, dst_stride, src, w  << ps, _dst + (height - 1) * dst_stride, width, border_pixels, ps, edges[BOTTOM]);


    //left
    src = fc->tab.alf_pixel_buffer_v[c_idx][1] + (h * (x_ctb - 1) + y - border_pixels) * (border_pixels << ps);
    dst = _dst - (border_pixels << ps) - border_pixels * dst_stride;
    alf_fill_border_v(dst, dst_stride, src,  dst + (border_pixels << ps), border_pixels, height, ps, edges, edges[LEFT]);

    //right
    src = fc->tab.alf_pixel_buffer_v[c_idx][0] + (h * (x_ctb + 1) + y - border_pixels) * (border_pixels << ps);
    dst = _dst + (width << ps) - border_pixels * dst_stride;
    alf_fill_border_v(dst, dst_stride, src,  dst - (1 << ps), border_pixels, height, ps, edges, edges[RIGHT]);
}

#define ALF_MAX_BLOCKS_IN_CTU   (MAX_CTU_SIZE * MAX_CTU_SIZE / ALF_BLOCK_SIZE / ALF_BLOCK_SIZE)
#define ALF_MAX_FILTER_SIZE     (ALF_MAX_BLOCKS_IN_CTU * ALF_NUM_COEFF_LUMA)

static void alf_get_coeff_and_clip(VVCLocalContext *lc, int16_t *coeff, int16_t *clip,
    const uint8_t *src, ptrdiff_t src_stride, int width, int height, int vb_pos, ALFParams *alf)
{
    const VVCFrameContext *fc     = lc->fc;
    const H266RawSliceHeader *rsh = lc->sc->sh.r;
    uint8_t fixed_clip_set[ALF_NUM_FILTERS_LUMA][ALF_NUM_COEFF_LUMA] = { 0 };
    const int16_t *coeff_set;
    const uint8_t *clip_idx_set;
    const uint8_t *class_to_filt;
    const int size = width * height / ALF_BLOCK_SIZE / ALF_BLOCK_SIZE;
    int class_idx[ALF_MAX_BLOCKS_IN_CTU];
    int transpose_idx[ALF_MAX_BLOCKS_IN_CTU];

    if (alf->ctb_filt_set_idx_y < 16) {
        coeff_set         = &ff_vvc_alf_fix_filt_coeff[0][0];
        clip_idx_set      = &fixed_clip_set[0][0];
        class_to_filt     = ff_vvc_alf_class_to_filt_map[alf->ctb_filt_set_idx_y];
    } else {
        const int id      = rsh->sh_alf_aps_id_luma[alf->ctb_filt_set_idx_y - 16];
        const VVCALF *aps = fc->ps.alf_list[id];
        coeff_set         = &aps->luma_coeff[0][0];
        clip_idx_set      = &aps->luma_clip_idx[0][0];
        class_to_filt     = ff_vvc_alf_aps_class_to_filt_map;
    }
    fc->vvcdsp.alf.classify(class_idx, transpose_idx, src, src_stride, width, height,
        vb_pos, lc->alf_gradient_tmp);
    fc->vvcdsp.alf.recon_coeff_and_clip(coeff, clip, class_idx, transpose_idx, size,
        coeff_set, clip_idx_set, class_to_filt);
}

static void alf_filter_luma(VVCLocalContext *lc, uint8_t *dst, const uint8_t *src,
    const ptrdiff_t dst_stride, const ptrdiff_t src_stride, const int x0, const int y0,
    const int width, const int height, const int _vb_pos, ALFParams *alf)
{
    const VVCFrameContext *fc = lc->fc;
    int vb_pos                = _vb_pos - y0;
    int16_t *coeff            = (int16_t*)lc->tmp;
    int16_t *clip             = (int16_t *)lc->tmp1;

    av_assert0(ALF_MAX_FILTER_SIZE <= sizeof(lc->tmp));
    av_assert0(ALF_MAX_FILTER_SIZE * sizeof(int16_t) <= sizeof(lc->tmp1));

    alf_get_coeff_and_clip(lc, coeff, clip, src, src_stride, width, height, vb_pos, alf);
    fc->vvcdsp.alf.filter[LUMA](dst, dst_stride, src, src_stride, width, height, coeff, clip, vb_pos);
}

static int alf_clip_from_idx(const VVCFrameContext *fc, const int idx)
{
    const VVCSPS *sps  = fc->ps.sps;
    const int offset[] = {0, 3, 5, 7};

    return 1 << (sps->bit_depth - offset[idx]);
}

static void alf_filter_chroma(VVCLocalContext *lc, uint8_t *dst, const uint8_t *src,
    const ptrdiff_t dst_stride, const ptrdiff_t src_stride, const int c_idx,
    const int width, const int height, const int vb_pos, ALFParams *alf)
{
    VVCFrameContext *fc           = lc->fc;
    const H266RawSliceHeader *rsh = lc->sc->sh.r;
    const VVCALF *aps             = fc->ps.alf_list[rsh->sh_alf_aps_id_chroma];
    const int idx                 = alf->alf_ctb_filter_alt_idx[c_idx - 1];
    const int16_t *coeff          = aps->chroma_coeff[idx];
    int16_t clip[ALF_NUM_COEFF_CHROMA];

    for (int i = 0; i < ALF_NUM_COEFF_CHROMA; i++)
        clip[i] = alf_clip_from_idx(fc, aps->chroma_clip_idx[idx][i]);

    fc->vvcdsp.alf.filter[CHROMA](dst, dst_stride, src, src_stride, width, height, coeff, clip, vb_pos);
}

static void alf_filter_cc(VVCLocalContext *lc, uint8_t *dst, const uint8_t *luma,
    const ptrdiff_t dst_stride, const ptrdiff_t luma_stride, const int c_idx,
    const int width, const int height, const int hs, const int vs, const int vb_pos, ALFParams *alf)
{
    const VVCFrameContext *fc     = lc->fc;
    const H266RawSliceHeader *rsh = lc->sc->sh.r;
    const int idx                 = c_idx - 1;
    const int cc_aps_id           = c_idx == CB ? rsh->sh_alf_cc_cb_aps_id : rsh->sh_alf_cc_cr_aps_id;
    const VVCALF *aps             = fc->ps.alf_list[cc_aps_id];

    if (aps) {
        const int16_t *coeff = aps->cc_coeff[idx][alf->ctb_cc_idc[idx] - 1];

        fc->vvcdsp.alf.filter_cc(dst, dst_stride, luma, luma_stride, width, height, hs, vs, coeff, vb_pos);
    }
}

void ff_vvc_alf_copy_ctu_to_hv(VVCLocalContext* lc, const int x0, const int y0)
{
    VVCFrameContext *fc  = lc->fc;
    const int x_ctb      = x0 >> fc->ps.sps->ctb_log2_size_y;
    const int y_ctb      = y0 >> fc->ps.sps->ctb_log2_size_y;
    const int ctb_size_y = fc->ps.sps->ctb_size_y;
    const int ps         = fc->ps.sps->pixel_shift;
    const int c_end      = fc->ps.sps->r->sps_chroma_format_idc ? VVC_MAX_SAMPLE_ARRAYS : 1;

    for (int c_idx = 0; c_idx < c_end; c_idx++) {
        const int hs     = fc->ps.sps->hshift[c_idx];
        const int vs     = fc->ps.sps->vshift[c_idx];
        const int x      = x0 >> hs;
        const int y      = y0 >> vs;
        const int width  = FFMIN(fc->ps.pps->width - x0, ctb_size_y) >> hs;
        const int height = FFMIN(fc->ps.pps->height - y0, ctb_size_y) >> vs;

        const int src_stride = fc->frame->linesize[c_idx];
        uint8_t* src = &fc->frame->data[c_idx][y * src_stride + (x << ps)];

        alf_copy_ctb_to_hv(fc, src, src_stride, x, y, width, height, x_ctb, y_ctb, c_idx);
    }
}

void ff_vvc_alf_filter(VVCLocalContext *lc, const int x0, const int y0)
{
    VVCFrameContext *fc     = lc->fc;
    const VVCPPS *pps       = fc->ps.pps;
    const int x_ctb         = x0 >> fc->ps.sps->ctb_log2_size_y;
    const int y_ctb         = y0 >> fc->ps.sps->ctb_log2_size_y;
    const int ctb_size_y    = fc->ps.sps->ctb_size_y;
    const int ps            = fc->ps.sps->pixel_shift;
    const int padded_stride = EDGE_EMU_BUFFER_STRIDE << ps;
    const int padded_offset = padded_stride * ALF_PADDING_SIZE + (ALF_PADDING_SIZE << ps);
    const int c_end         = fc->ps.sps->r->sps_chroma_format_idc ? VVC_MAX_SAMPLE_ARRAYS : 1;
    ALFParams *alf          = &CTB(fc->tab.alf, x_ctb, y_ctb);
    int edges[MAX_EDGES]    = { x_ctb == 0, y_ctb == 0, x_ctb == pps->ctb_width - 1, y_ctb == pps->ctb_height - 1 };

    if (!pps->r->pps_loop_filter_across_tiles_enabled_flag) {
        edges[LEFT]   = edges[LEFT] || (lc->boundary_flags & BOUNDARY_LEFT_TILE);
        edges[TOP]    = edges[TOP] || (lc->boundary_flags & BOUNDARY_UPPER_TILE);
        edges[RIGHT]  = edges[RIGHT] || pps->ctb_to_col_bd[x_ctb] != pps->ctb_to_col_bd[x_ctb + 1];
        edges[BOTTOM] = edges[BOTTOM] || pps->ctb_to_row_bd[y_ctb] != pps->ctb_to_row_bd[y_ctb + 1];
    }

    if (!pps->r->pps_loop_filter_across_slices_enabled_flag) {
        edges[LEFT]   = edges[LEFT] || (lc->boundary_flags & BOUNDARY_LEFT_SLICE);
        edges[TOP]    = edges[TOP] || (lc->boundary_flags & BOUNDARY_UPPER_SLICE);
        edges[RIGHT]  = edges[RIGHT] || CTB(fc->tab.slice_idx, x_ctb, y_ctb) != CTB(fc->tab.slice_idx, x_ctb + 1, y_ctb);
        edges[BOTTOM] = edges[BOTTOM] || CTB(fc->tab.slice_idx, x_ctb, y_ctb) != CTB(fc->tab.slice_idx, x_ctb, y_ctb + 1);
    }

    for (int c_idx = 0; c_idx < c_end; c_idx++) {
        const int hs = fc->ps.sps->hshift[c_idx];
        const int vs = fc->ps.sps->vshift[c_idx];
        const int ctb_size_h = ctb_size_y >> hs;
        const int ctb_size_v = ctb_size_y >> vs;
        const int x = x0 >> hs;
        const int y = y0 >> vs;
        const int pic_width = fc->ps.pps->width >> hs;
        const int pic_height = fc->ps.pps->height >> vs;
        const int width  = FFMIN(pic_width  - x, ctb_size_h);
        const int height = FFMIN(pic_height - y, ctb_size_v);
        const int src_stride = fc->frame->linesize[c_idx];
        uint8_t *src = &fc->frame->data[c_idx][y * src_stride + (x << ps)];
        uint8_t *padded;

        if (alf->ctb_flag[c_idx] || (!c_idx && (alf->ctb_cc_idc[0] || alf->ctb_cc_idc[1]))) {
            padded = (c_idx ? lc->alf_buffer_chroma : lc->alf_buffer_luma) + padded_offset;
            alf_prepare_buffer(fc, padded, src, x, y, x_ctb, y_ctb, width, height,
                padded_stride, src_stride, c_idx, edges);
        }
        if (alf->ctb_flag[c_idx]) {
            if (!c_idx)  {
                alf_filter_luma(lc, src, padded, src_stride, padded_stride, x, y,
                    width, height, y + ctb_size_v - ALF_VB_POS_ABOVE_LUMA, alf);
            } else {
                alf_filter_chroma(lc, src, padded, src_stride, padded_stride, c_idx,
                    width, height, ctb_size_v - ALF_VB_POS_ABOVE_CHROMA, alf);
            }
        }
        if (c_idx && alf->ctb_cc_idc[c_idx - 1]) {
            padded = lc->alf_buffer_luma + padded_offset;
            alf_filter_cc(lc, src, padded, src_stride, padded_stride, c_idx,
                width, height, hs, vs, (ctb_size_v << vs) - ALF_VB_POS_ABOVE_LUMA, alf);
        }

        alf->applied[c_idx] = 1;
    }
}


void ff_vvc_lmcs_filter(const VVCLocalContext *lc, const int x, const int y)
{
    const SliceContext *sc = lc->sc;
    const VVCFrameContext *fc = lc->fc;
    const int ctb_size = fc->ps.sps->ctb_size_y;
    const int width    = FFMIN(fc->ps.pps->width  - x, ctb_size);
    const int height   = FFMIN(fc->ps.pps->height - y, ctb_size);
    uint8_t *data      = fc->frame->data[LUMA] + y * fc->frame->linesize[LUMA] + (x << fc->ps.sps->pixel_shift);
    if (sc->sh.r->sh_lmcs_used_flag)
        fc->vvcdsp.lmcs.filter(data, fc->frame->linesize[LUMA], width, height, &fc->ps.lmcs.inv_lut);
}
