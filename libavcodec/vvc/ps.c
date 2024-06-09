/*
 * VVC parameter set parser
 *
 * Copyright (C) 2023 Nuo Mi
 * Copyright (C) 2022 Xu Mu
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

#include "libavcodec/cbs_h266.h"
#include "libavutil/mem.h"
#include "libavutil/pixdesc.h"
#include "libavcodec/refstruct.h"
#include "data.h"
#include "ps.h"
#include "dec.h"

static int sps_map_pixel_format(VVCSPS *sps, void *log_ctx)
{
    const H266RawSPS *r = sps->r;
    const AVPixFmtDescriptor *desc;

    switch (sps->bit_depth) {
    case 8:
        if (r->sps_chroma_format_idc == 0) sps->pix_fmt = AV_PIX_FMT_GRAY8;
        if (r->sps_chroma_format_idc == 1) sps->pix_fmt = AV_PIX_FMT_YUV420P;
        if (r->sps_chroma_format_idc == 2) sps->pix_fmt = AV_PIX_FMT_YUV422P;
        if (r->sps_chroma_format_idc == 3) sps->pix_fmt = AV_PIX_FMT_YUV444P;
       break;
    case 10:
        if (r->sps_chroma_format_idc == 0) sps->pix_fmt = AV_PIX_FMT_GRAY10;
        if (r->sps_chroma_format_idc == 1) sps->pix_fmt = AV_PIX_FMT_YUV420P10;
        if (r->sps_chroma_format_idc == 2) sps->pix_fmt = AV_PIX_FMT_YUV422P10;
        if (r->sps_chroma_format_idc == 3) sps->pix_fmt = AV_PIX_FMT_YUV444P10;
        break;
    case 12:
        if (r->sps_chroma_format_idc == 0) sps->pix_fmt = AV_PIX_FMT_GRAY12;
        if (r->sps_chroma_format_idc == 1) sps->pix_fmt = AV_PIX_FMT_YUV420P12;
        if (r->sps_chroma_format_idc == 2) sps->pix_fmt = AV_PIX_FMT_YUV422P12;
        if (r->sps_chroma_format_idc == 3) sps->pix_fmt = AV_PIX_FMT_YUV444P12;
        break;
    default:
        av_log(log_ctx, AV_LOG_ERROR,
               "The following bit-depths are currently specified: 8, 10, 12 bits, "
               "chroma_format_idc is %d, depth is %d\n",
               r->sps_chroma_format_idc, sps->bit_depth);
        return AVERROR_INVALIDDATA;
    }

    desc = av_pix_fmt_desc_get(sps->pix_fmt);
    if (!desc)
        return AVERROR(EINVAL);

    sps->hshift[0] = sps->vshift[0] = 0;
    sps->hshift[2] = sps->hshift[1] = desc->log2_chroma_w;
    sps->vshift[2] = sps->vshift[1] = desc->log2_chroma_h;

    sps->pixel_shift = sps->bit_depth > 8;

    return 0;
}

static int sps_bit_depth(VVCSPS *sps, void *log_ctx)
{
    const H266RawSPS *r = sps->r;

    sps->bit_depth = r->sps_bitdepth_minus8 + 8;
    sps->qp_bd_offset = 6 * (sps->bit_depth - 8);
    sps->log2_transform_range =
        r->sps_extended_precision_flag ? FFMAX(15, FFMIN(20, sps->bit_depth + 6)) : 15;
    return sps_map_pixel_format(sps, log_ctx);
}

static int sps_chroma_qp_table(VVCSPS *sps)
{
    const H266RawSPS *r = sps->r;
    const int num_qp_tables = r->sps_same_qp_table_for_chroma_flag ?
        1 : (r->sps_joint_cbcr_enabled_flag ? 3 : 2);

    for (int i = 0; i < num_qp_tables; i++) {
        int num_points_in_qp_table;
        int8_t qp_in[VVC_MAX_POINTS_IN_QP_TABLE], qp_out[VVC_MAX_POINTS_IN_QP_TABLE];
        unsigned int delta_qp_in[VVC_MAX_POINTS_IN_QP_TABLE];
        int off = sps->qp_bd_offset;

        num_points_in_qp_table = r->sps_num_points_in_qp_table_minus1[i] + 1;

        qp_out[0] = qp_in[0] = r->sps_qp_table_start_minus26[i] + 26;
        for (int j = 0; j < num_points_in_qp_table; j++ ) {
            const uint8_t delta_qp_out = (r->sps_delta_qp_in_val_minus1[i][j] ^ r->sps_delta_qp_diff_val[i][j]);
            delta_qp_in[j] = r->sps_delta_qp_in_val_minus1[i][j] + 1;
            // Note: we cannot check qp_{in,out}[j+1] here as qp_*[j] + delta_qp_*
            //       may not fit in an 8-bit signed integer.
            if (qp_in[j] + delta_qp_in[j] > 63 || qp_out[j] + delta_qp_out > 63)
                return AVERROR(EINVAL);
            qp_in[j+1] = qp_in[j] + delta_qp_in[j];
            qp_out[j+1] = qp_out[j] + delta_qp_out;
        }
        sps->chroma_qp_table[i][qp_in[0] + off] = qp_out[0];
        for (int k = qp_in[0] - 1 + off; k >= 0; k--)
            sps->chroma_qp_table[i][k] = av_clip(sps->chroma_qp_table[i][k+1]-1, -off, 63);

        for (int j  = 0; j < num_points_in_qp_table; j++) {
            int sh = delta_qp_in[j] >> 1;
            for (int k = qp_in[j] + 1 + off, m = 1; k <= qp_in[j+1] + off; k++, m++) {
                sps->chroma_qp_table[i][k] = sps->chroma_qp_table[i][qp_in[j] + off] +
                    ((qp_out[j+1] - qp_out[j]) * m + sh) / delta_qp_in[j];
            }
        }
        for (int k = qp_in[num_points_in_qp_table] + 1 + off; k <= 63 + off; k++)
            sps->chroma_qp_table[i][k]  = av_clip(sps->chroma_qp_table[i][k-1] + 1, -sps->qp_bd_offset, 63);
    }
    if (r->sps_same_qp_table_for_chroma_flag) {
        memcpy(&sps->chroma_qp_table[1], &sps->chroma_qp_table[0], sizeof(sps->chroma_qp_table[0]));
        memcpy(&sps->chroma_qp_table[2], &sps->chroma_qp_table[0], sizeof(sps->chroma_qp_table[0]));
    }

    return 0;
}

static void sps_poc(VVCSPS *sps)
{
    sps->max_pic_order_cnt_lsb = 1 << (sps->r->sps_log2_max_pic_order_cnt_lsb_minus4 + 4);
}

static void sps_inter(VVCSPS *sps)
{
    const H266RawSPS *r = sps->r;

    sps->max_num_merge_cand     = 6 - r->sps_six_minus_max_num_merge_cand;
    sps->max_num_ibc_merge_cand = 6 - r->sps_six_minus_max_num_ibc_merge_cand;

    if (sps->r->sps_gpm_enabled_flag) {
        sps->max_num_gpm_merge_cand = 2;
        if (sps->max_num_merge_cand >= 3)
            sps->max_num_gpm_merge_cand = sps->max_num_merge_cand - r->sps_max_num_merge_cand_minus_max_num_gpm_cand;
    }

    sps->log2_parallel_merge_level = r->sps_log2_parallel_merge_level_minus2 + 2;
}

static void sps_partition_constraints(VVCSPS* sps)
{
    const H266RawSPS *r = sps->r;

    sps->ctb_log2_size_y    = r->sps_log2_ctu_size_minus5 + 5;
    sps->ctb_size_y         = 1 << sps->ctb_log2_size_y;
    sps->min_cb_log2_size_y = r->sps_log2_min_luma_coding_block_size_minus2 + 2;
    sps->min_cb_size_y      = 1 << sps->min_cb_log2_size_y;
    sps->max_tb_size_y      = 1 << (r->sps_max_luma_transform_size_64_flag ? 6 : 5);
    sps->max_ts_size        = 1 << (r->sps_log2_transform_skip_max_size_minus2 + 2);
}

static void sps_ladf(VVCSPS* sps)
{
    const H266RawSPS *r = sps->r;

    if (r->sps_ladf_enabled_flag) {
        sps->num_ladf_intervals = r->sps_num_ladf_intervals_minus2 + 2;
        sps->ladf_interval_lower_bound[0] = 0;
        for (int i = 0; i < sps->num_ladf_intervals - 1; i++) {
            sps->ladf_interval_lower_bound[i + 1] =
                sps->ladf_interval_lower_bound[i] + r->sps_ladf_delta_threshold_minus1[i] + 1;
        }
    }
}

static int sps_derive(VVCSPS *sps, void *log_ctx)
{
    int ret;
    const H266RawSPS *r = sps->r;

    ret = sps_bit_depth(sps, log_ctx);
    if (ret < 0)
        return ret;
    sps_poc(sps);
    sps_inter(sps);
    sps_partition_constraints(sps);
    sps_ladf(sps);
    if (r->sps_chroma_format_idc != 0) {
        ret = sps_chroma_qp_table(sps);
        if (ret < 0)
            return ret;
    }

    return 0;
}

static void sps_free(FFRefStructOpaque opaque, void *obj)
{
    VVCSPS *sps = obj;
    ff_refstruct_unref(&sps->r);
}

static const VVCSPS *sps_alloc(const H266RawSPS *rsps, void *log_ctx)
{
    int ret;
    VVCSPS *sps = ff_refstruct_alloc_ext(sizeof(*sps), 0, NULL, sps_free);

    if (!sps)
        return NULL;

    ff_refstruct_replace(&sps->r, rsps);

    ret = sps_derive(sps, log_ctx);
    if (ret < 0)
        goto fail;

    return sps;

fail:
    ff_refstruct_unref(&sps);
    return NULL;
}

static int decode_sps(VVCParamSets *ps, const H266RawSPS *rsps, void *log_ctx, int is_clvss)
{
    const int sps_id        = rsps->sps_seq_parameter_set_id;
    const VVCSPS *old_sps   = ps->sps_list[sps_id];
    const VVCSPS *sps;

    if (is_clvss) {
        ps->sps_id_used = 0;
    }

    if (old_sps) {
        if (old_sps->r == rsps || !memcmp(old_sps->r, rsps, sizeof(*old_sps->r)))
            return 0;
        else if (ps->sps_id_used & (1 << sps_id))
            return AVERROR_INVALIDDATA;
    }

    sps = sps_alloc(rsps, log_ctx);
    if (!sps)
        return AVERROR(ENOMEM);

    ff_refstruct_unref(&ps->sps_list[sps_id]);
    ps->sps_list[sps_id] = sps;
    ps->sps_id_used |= (1 << sps_id);

    return 0;
}

static void pps_chroma_qp_offset(VVCPPS *pps)
{
    pps->chroma_qp_offset[CB - 1]   = pps->r->pps_cb_qp_offset;
    pps->chroma_qp_offset[CR - 1]   = pps->r->pps_cr_qp_offset;
    pps->chroma_qp_offset[JCBCR - 1]= pps->r->pps_joint_cbcr_qp_offset_value;
    for (int i = 0; i < 6; i++) {
        pps->chroma_qp_offset_list[i][CB - 1]   = pps->r->pps_cb_qp_offset_list[i];
        pps->chroma_qp_offset_list[i][CR - 1]   = pps->r->pps_cr_qp_offset_list[i];
        pps->chroma_qp_offset_list[i][JCBCR - 1]= pps->r->pps_joint_cbcr_qp_offset_list[i];
    }
}

static void pps_width_height(VVCPPS *pps, const VVCSPS *sps)
{
    const H266RawPPS *r = pps->r;

    pps->width          = r->pps_pic_width_in_luma_samples;
    pps->height         = r->pps_pic_height_in_luma_samples;

    pps->ctb_width      = AV_CEIL_RSHIFT(pps->width,  sps->ctb_log2_size_y);
    pps->ctb_height     = AV_CEIL_RSHIFT(pps->height, sps->ctb_log2_size_y);
    pps->ctb_count      = pps->ctb_width * pps->ctb_height;

    pps->min_cb_width   = pps->width  >> sps->min_cb_log2_size_y;
    pps->min_cb_height  = pps->height >> sps->min_cb_log2_size_y;

    pps->min_pu_width   = pps->width  >> MIN_PU_LOG2;
    pps->min_pu_height  = pps->height >> MIN_PU_LOG2;
    pps->min_tu_width   = pps->width  >> MIN_TU_LOG2;
    pps->min_tu_height  = pps->height >> MIN_TU_LOG2;

    pps->width32        = AV_CEIL_RSHIFT(pps->width,  5);
    pps->height32       = AV_CEIL_RSHIFT(pps->height, 5);
    pps->width64        = AV_CEIL_RSHIFT(pps->width,  6);
    pps->height64       = AV_CEIL_RSHIFT(pps->height, 6);
}

static int pps_bd(VVCPPS *pps)
{
    const H266RawPPS *r = pps->r;

    pps->col_bd        = av_calloc(r->num_tile_columns  + 1, sizeof(*pps->col_bd));
    pps->row_bd        = av_calloc(r->num_tile_rows  + 1,    sizeof(*pps->row_bd));
    pps->ctb_to_col_bd = av_calloc(pps->ctb_width  + 1,      sizeof(*pps->ctb_to_col_bd));
    pps->ctb_to_row_bd = av_calloc(pps->ctb_height + 1,      sizeof(*pps->ctb_to_col_bd));
    if (!pps->col_bd || !pps->row_bd || !pps->ctb_to_col_bd || !pps->ctb_to_row_bd)
        return AVERROR(ENOMEM);

    for (int i = 0, j = 0; i < r->num_tile_columns; i++) {
        pps->col_bd[i] = j;
        j += r->col_width_val[i];
        for (int k = pps->col_bd[i]; k < j; k++)
            pps->ctb_to_col_bd[k] = pps->col_bd[i];
    }
    pps->col_bd[r->num_tile_columns] = pps->ctb_to_col_bd[pps->ctb_width] = pps->ctb_width;

    for (int i = 0, j = 0; i < r->num_tile_rows; i++) {
        pps->row_bd[i] = j;
        j += r->row_height_val[i];
        for (int k = pps->row_bd[i]; k < j; k++)
            pps->ctb_to_row_bd[k] = pps->row_bd[i];
    }
    pps->row_bd[r->num_tile_rows] = pps->ctb_to_row_bd[pps->ctb_height] = pps->ctb_height;

    return 0;
}


static int next_tile_idx(int tile_idx, const int i, const H266RawPPS *r)
{
    if (r->pps_tile_idx_delta_present_flag) {
        tile_idx += r->pps_tile_idx_delta_val[i];
    } else {
        tile_idx += r->pps_slice_width_in_tiles_minus1[i] + 1;
        if (tile_idx % r->num_tile_columns == 0)
            tile_idx += (r->pps_slice_height_in_tiles_minus1[i]) * r->num_tile_columns;
    }
    return tile_idx;
}

static void tile_xy(int *tile_x, int *tile_y, const int tile_idx, const VVCPPS *pps)
{
    *tile_x = tile_idx % pps->r->num_tile_columns;
    *tile_y = tile_idx / pps->r->num_tile_columns;
}

static void ctu_xy(int *rx, int *ry, const int tile_x, const int tile_y, const VVCPPS *pps)
{
    *rx = pps->col_bd[tile_x];
    *ry = pps->row_bd[tile_y];
}

static int ctu_rs(const int rx, const int ry, const VVCPPS *pps)
{
    return pps->ctb_width * ry + rx;
}

static int pps_add_ctus(VVCPPS *pps, int *off, const int rx, const int ry,
    const int w, const int h)
{
    int start = *off;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            pps->ctb_addr_in_slice[*off] = ctu_rs(rx + x, ry + y, pps);
            (*off)++;
        }
    }
    return *off - start;
}

static void pps_single_slice_picture(VVCPPS *pps, int *off)
{
    for (int j = 0; j < pps->r->num_tile_rows; j++) {
        for (int i = 0; i < pps->r->num_tile_columns; i++) {
            pps->num_ctus_in_slice[0] = pps_add_ctus(pps, off,
                pps->col_bd[i], pps->row_bd[j],
                pps->r->col_width_val[i], pps->r->row_height_val[j]);
        }
    }
}

static void subpic_tiles(int *tile_x, int *tile_y, int *tile_x_end, int *tile_y_end,
    const VVCSPS *sps, const VVCPPS *pps,  const int i)
{
    const int rx = sps->r->sps_subpic_ctu_top_left_x[i];
    const int ry = sps->r->sps_subpic_ctu_top_left_y[i];

    *tile_x = *tile_y = 0;

    while (pps->col_bd[*tile_x] != rx)
        (*tile_x)++;

    while (pps->row_bd[*tile_y] != ry)
        (*tile_y)++;

    *tile_x_end = (*tile_x);
    *tile_y_end = (*tile_y);

    while (pps->col_bd[*tile_x_end] < rx + sps->r->sps_subpic_width_minus1[i] + 1)
        (*tile_x_end)++;

    while (pps->row_bd[*tile_y_end] < ry + sps->r->sps_subpic_height_minus1[i] + 1)
        (*tile_y_end)++;
}

static void pps_subpic_less_than_one_tile_slice(VVCPPS *pps, const VVCSPS *sps, const int i, const int tx, const int ty, int *off)
{
    pps->num_ctus_in_slice[i] = pps_add_ctus(pps, off,
        pps->col_bd[tx], pps->row_bd[ty],
        pps->r->col_width_val[tx], sps->r->sps_subpic_height_minus1[i] + 1);
}

static void pps_subpic_one_or_more_tiles_slice(VVCPPS *pps, const int tile_x, const int tile_y, const int x_end, const int y_end, const int i, int *off)
{
    for (int ty = tile_y; ty < y_end; ty++) {
        for (int tx = tile_x; tx < x_end; tx++) {
            pps->num_ctus_in_slice[i] += pps_add_ctus(pps, off,
                pps->col_bd[tx], pps->row_bd[ty],
                pps->r->col_width_val[tx], pps->r->row_height_val[ty]);
        }
    }
}

static void pps_subpic_slice(VVCPPS *pps, const VVCSPS *sps, const int i, int *off)
{
    int tx, ty, x_end, y_end;

    pps->slice_start_offset[i] = *off;
    pps->num_ctus_in_slice[i] = 0;

    subpic_tiles(&tx, &ty, &x_end, &y_end, sps, pps, i);
    if (ty + 1 == y_end && sps->r->sps_subpic_height_minus1[i] + 1 < pps->r->row_height_val[ty])
        pps_subpic_less_than_one_tile_slice(pps, sps, i, tx, ty, off);
    else
        pps_subpic_one_or_more_tiles_slice(pps, tx, ty, x_end, y_end, i, off);
}

static void pps_single_slice_per_subpic(VVCPPS *pps, const VVCSPS *sps, int *off)
{
    if (!sps->r->sps_subpic_info_present_flag) {
        pps_single_slice_picture(pps, off);
    } else {
        for (int i = 0; i < pps->r->pps_num_slices_in_pic_minus1 + 1; i++)
            pps_subpic_slice(pps, sps, i, off);
    }
}

static int pps_one_tile_slices(VVCPPS *pps, const int tile_idx, int i, int *off)
{
    const H266RawPPS *r = pps->r;
    int rx, ry, ctu_y_end, tile_x, tile_y;

    tile_xy(&tile_x, &tile_y, tile_idx, pps);
    ctu_xy(&rx, &ry, tile_x, tile_y, pps);
    ctu_y_end = ry + r->row_height_val[tile_y];
    while (ry < ctu_y_end) {
        pps->slice_start_offset[i] = *off;
        pps->num_ctus_in_slice[i] = pps_add_ctus(pps, off, rx, ry,
            r->col_width_val[tile_x], r->slice_height_in_ctus[i]);
        ry += r->slice_height_in_ctus[i++];
    }
    i--;
    return i;
}

static void pps_multi_tiles_slice(VVCPPS *pps, const int tile_idx, const int i, int *off)
{
    const H266RawPPS *r = pps->r;
    int rx, ry, tile_x, tile_y;

    tile_xy(&tile_x, &tile_y, tile_idx, pps);
    pps->slice_start_offset[i] = *off;
    pps->num_ctus_in_slice[i] = 0;
    for (int ty = tile_y; ty <= tile_y + r->pps_slice_height_in_tiles_minus1[i]; ty++) {
        for (int tx = tile_x; tx <= tile_x + r->pps_slice_width_in_tiles_minus1[i]; tx++) {
            ctu_xy(&rx, &ry, tx, ty, pps);
            pps->num_ctus_in_slice[i] += pps_add_ctus(pps, off, rx, ry,
                r->col_width_val[tx], r->row_height_val[ty]);
        }
    }
}

static void pps_rect_slice(VVCPPS *pps, const VVCSPS *sps)
{
    const H266RawPPS *r = pps->r;
    int tile_idx = 0, off = 0;

    if (r->pps_single_slice_per_subpic_flag) {
        pps_single_slice_per_subpic(pps, sps, &off);
        return;
    }

    for (int i = 0; i < r->pps_num_slices_in_pic_minus1 + 1; i++) {
        if (!r->pps_slice_width_in_tiles_minus1[i] &&
            !r->pps_slice_height_in_tiles_minus1[i]) {
            i = pps_one_tile_slices(pps, tile_idx, i, &off);
        } else {
            pps_multi_tiles_slice(pps, tile_idx, i, &off);
        }
        tile_idx = next_tile_idx(tile_idx, i, r);
    }
}

static void pps_no_rect_slice(VVCPPS* pps)
{
    const H266RawPPS* r = pps->r;
    int rx, ry, off = 0;

    for (int tile_y = 0; tile_y < r->num_tile_rows; tile_y++) {
        for (int tile_x = 0; tile_x < r->num_tile_columns; tile_x++) {
            ctu_xy(&rx, &ry, tile_x, tile_y, pps);
            pps_add_ctus(pps, &off, rx, ry, r->col_width_val[tile_x], r->row_height_val[tile_y]);
        }
    }
}

static int pps_slice_map(VVCPPS *pps, const VVCSPS *sps)
{
    pps->ctb_addr_in_slice = av_calloc(pps->ctb_count, sizeof(*pps->ctb_addr_in_slice));
    if (!pps->ctb_addr_in_slice)
        return AVERROR(ENOMEM);

    if (pps->r->pps_rect_slice_flag)
        pps_rect_slice(pps, sps);
    else
        pps_no_rect_slice(pps);

    return 0;
}

static void pps_ref_wraparound_offset(VVCPPS *pps, const VVCSPS *sps)
{
    const H266RawPPS *r = pps->r;

    if (r->pps_ref_wraparound_enabled_flag)
        pps->ref_wraparound_offset = (pps->width / sps->min_cb_size_y) - r->pps_pic_width_minus_wraparound_offset;
}

static void pps_subpic(VVCPPS *pps, const VVCSPS *sps)
{
    const H266RawSPS *rsps = sps->r;
    for (int i = 0; i < rsps->sps_num_subpics_minus1 + 1; i++) {
        if (rsps->sps_subpic_treated_as_pic_flag[i]) {
            pps->subpic_x[i]      = rsps->sps_subpic_ctu_top_left_x[i] << sps->ctb_log2_size_y;
            pps->subpic_y[i]      = rsps->sps_subpic_ctu_top_left_y[i] << sps->ctb_log2_size_y;
            pps->subpic_width[i]  = FFMIN(pps->width  - pps->subpic_x[i], (rsps->sps_subpic_width_minus1[i]  + 1) << sps->ctb_log2_size_y);
            pps->subpic_height[i] = FFMIN(pps->height - pps->subpic_y[i], (rsps->sps_subpic_height_minus1[i] + 1) << sps->ctb_log2_size_y);
        } else {
            pps->subpic_x[i]      = 0;
            pps->subpic_y[i]      = 0;
            pps->subpic_width[i]  = pps->width;
            pps->subpic_height[i] = pps->height;
        }
    }
}

static int pps_derive(VVCPPS *pps, const VVCSPS *sps)
{
    int ret;

    pps_chroma_qp_offset(pps);
    pps_width_height(pps, sps);

    ret = pps_bd(pps);
    if (ret < 0)
        return ret;

    ret = pps_slice_map(pps, sps);
    if (ret < 0)
        return ret;

    pps_ref_wraparound_offset(pps, sps);
    pps_subpic(pps, sps);

    return 0;
}

static void pps_free(FFRefStructOpaque opaque, void *obj)
{
    VVCPPS *pps = obj;

    ff_refstruct_unref(&pps->r);

    av_freep(&pps->col_bd);
    av_freep(&pps->row_bd);
    av_freep(&pps->ctb_to_col_bd);
    av_freep(&pps->ctb_to_row_bd);
    av_freep(&pps->ctb_addr_in_slice);
}

static const VVCPPS *pps_alloc(const H266RawPPS *rpps, const VVCSPS *sps)
{
    int ret;
    VVCPPS *pps = ff_refstruct_alloc_ext(sizeof(*pps), 0, NULL, pps_free);

    if (!pps)
        return NULL;

    ff_refstruct_replace(&pps->r, rpps);

    ret = pps_derive(pps, sps);
    if (ret < 0)
        goto fail;

    return pps;

fail:
    ff_refstruct_unref(&pps);
    return NULL;
}

static int decode_pps(VVCParamSets *ps, const H266RawPPS *rpps)
{
    int ret                 = 0;
    const int pps_id        = rpps->pps_pic_parameter_set_id;
    const int sps_id        = rpps->pps_seq_parameter_set_id;
    const VVCPPS *old_pps   = ps->pps_list[pps_id];
    const VVCPPS *pps;

    if (old_pps && old_pps->r == rpps)
        return 0;

    pps = pps_alloc(rpps, ps->sps_list[sps_id]);
    if (!pps)
        return AVERROR(ENOMEM);

    ff_refstruct_unref(&ps->pps_list[pps_id]);
    ps->pps_list[pps_id] = pps;

    return ret;
}

static int decode_ps(VVCParamSets *ps, const CodedBitstreamH266Context *h266, void *log_ctx, int is_clvss)
{
    const H266RawPictureHeader *ph = h266->ph;
    const H266RawPPS *rpps;
    const H266RawSPS *rsps;
    int ret;

    if (!ph)
        return AVERROR_INVALIDDATA;

    rpps = h266->pps[ph->ph_pic_parameter_set_id];
    if (!rpps)
        return AVERROR_INVALIDDATA;

    rsps = h266->sps[rpps->pps_seq_parameter_set_id];
    if (!rsps)
        return AVERROR_INVALIDDATA;

    ret = decode_sps(ps, rsps, log_ctx, is_clvss);
    if (ret < 0)
        return ret;

    ret = decode_pps(ps, rpps);
    if (ret < 0)
        return ret;

    return 0;
}

#define WEIGHT_TABLE(x)                                                                                 \
    w->nb_weights[L##x] = r->num_weights_l##x;                                                          \
    for (int i = 0; i < w->nb_weights[L##x]; i++) {                                                     \
        w->weight_flag[L##x][LUMA][i]     = r->luma_weight_l##x##_flag[i];                              \
        w->weight_flag[L##x][CHROMA][i]   = r->chroma_weight_l##x##_flag[i];                            \
        w->weight[L##x][LUMA][i]          = denom[LUMA] + r->delta_luma_weight_l##x[i];                 \
        w->offset[L##x][LUMA][i]          = r->luma_offset_l##x[i];                                     \
        for (int j = CB; j <= CR; j++) {                                                                \
            w->weight[L##x][j][i]         = denom[CHROMA] + r->delta_chroma_weight_l##x[i][j - 1];      \
            w->offset[L##x][j][i]         = 128 + r->delta_chroma_offset_l##x[i][j - 1];                \
            w->offset[L##x][j][i]        -= (128 * w->weight[L##x][j][i]) >> w->log2_denom[CHROMA];     \
            w->offset[L##x][j][i]         = av_clip_intp2(w->offset[L##x][j][i], 7);                    \
        }                                                                                               \
    }                                                                                                   \

static void pred_weight_table(PredWeightTable *w, const H266RawPredWeightTable *r)
{
    int denom[2];

    w->log2_denom[LUMA] = r->luma_log2_weight_denom;
    w->log2_denom[CHROMA] = w->log2_denom[LUMA] + r->delta_chroma_log2_weight_denom;
    denom[LUMA] = 1 << w->log2_denom[LUMA];
    denom[CHROMA] = 1 << w->log2_denom[CHROMA];
    WEIGHT_TABLE(0)
    WEIGHT_TABLE(1)
}

// 8.3.1 Decoding process for picture order count
static int ph_compute_poc(const H266RawPictureHeader *ph, const H266RawSPS *sps, const int poc_tid0, const int is_clvss)
{
    const int max_poc_lsb       = 1 << (sps->sps_log2_max_pic_order_cnt_lsb_minus4 + 4);
    const int prev_poc_lsb      = poc_tid0 % max_poc_lsb;
    const int prev_poc_msb      = poc_tid0 - prev_poc_lsb;
    const int poc_lsb           = ph->ph_pic_order_cnt_lsb;
    int poc_msb;

    if (ph->ph_poc_msb_cycle_present_flag) {
        poc_msb = ph->ph_poc_msb_cycle_val * max_poc_lsb;
    } else if (is_clvss) {
        poc_msb = 0;
    } else {
        if (poc_lsb < prev_poc_lsb && prev_poc_lsb - poc_lsb >= max_poc_lsb / 2)
            poc_msb = prev_poc_msb + max_poc_lsb;
        else if (poc_lsb > prev_poc_lsb && poc_lsb - prev_poc_lsb > max_poc_lsb / 2)
            poc_msb = prev_poc_msb - max_poc_lsb;
        else
            poc_msb = prev_poc_msb;
    }

    return poc_msb + poc_lsb;
}

static av_always_inline uint16_t lmcs_derive_lut_sample(uint16_t sample,
    uint16_t *pivot1, uint16_t *pivot2, uint16_t *scale_coeff, const int idx, const int max)
{
    const int lut_sample =
        pivot1[idx] + ((scale_coeff[idx] * (sample - pivot2[idx]) + (1<< 10)) >> 11);
    return av_clip(lut_sample, 0, max - 1);
}

//8.8.2.2 Inverse mapping process for a luma sample
static int lmcs_derive_lut(VVCLMCS *lmcs, const H266RawAPS *rlmcs, const H266RawSPS *sps)
{
    const int bit_depth = (sps->sps_bitdepth_minus8 + 8);
    const int max       = (1 << bit_depth);
    const int org_cw    = max / LMCS_MAX_BIN_SIZE;
    const int shift     = av_log2(org_cw);
    const int off       = 1 << (shift - 1);
    int cw[LMCS_MAX_BIN_SIZE];
    uint16_t input_pivot[LMCS_MAX_BIN_SIZE];
    uint16_t scale_coeff[LMCS_MAX_BIN_SIZE];
    uint16_t inv_scale_coeff[LMCS_MAX_BIN_SIZE];
    int i, delta_crs;
    if (bit_depth > LMCS_MAX_BIT_DEPTH)
        return AVERROR_PATCHWELCOME;

    if (!rlmcs)
        return AVERROR_INVALIDDATA;

    lmcs->min_bin_idx = rlmcs->lmcs_min_bin_idx;
    lmcs->max_bin_idx = LMCS_MAX_BIN_SIZE - 1 - rlmcs->lmcs_min_bin_idx;

    memset(cw, 0, sizeof(cw));
    for (int i = lmcs->min_bin_idx; i <= lmcs->max_bin_idx; i++)
        cw[i] = org_cw + (1 - 2 * rlmcs->lmcs_delta_sign_cw_flag[i]) * rlmcs->lmcs_delta_abs_cw[i];

    delta_crs = (1 - 2 * rlmcs->lmcs_delta_sign_crs_flag) * rlmcs->lmcs_delta_abs_crs;

    lmcs->pivot[0] = 0;
    for (i = 0; i < LMCS_MAX_BIN_SIZE; i++) {
        input_pivot[i]        = i * org_cw;
        lmcs->pivot[i + 1] = lmcs->pivot[i] + cw[i];
        scale_coeff[i]        = (cw[i] * (1 << 11) +  off) >> shift;
        if (cw[i] == 0) {
            inv_scale_coeff[i] = 0;
            lmcs->chroma_scale_coeff[i] = (1 << 11);
        } else {
            inv_scale_coeff[i] = org_cw * (1 << 11) / cw[i];
            lmcs->chroma_scale_coeff[i] = org_cw * (1 << 11) / (cw[i] + delta_crs);
        }
    }

    //derive lmcs_fwd_lut
    for (uint16_t sample = 0; sample < max; sample++) {
        const int idx_y = sample / org_cw;
        const uint16_t fwd_sample = lmcs_derive_lut_sample(sample, lmcs->pivot,
            input_pivot, scale_coeff, idx_y, max);
        if (bit_depth > 8)
            lmcs->fwd_lut.u16[sample] = fwd_sample;
        else
            lmcs->fwd_lut.u8 [sample] = fwd_sample;

    }

    //derive lmcs_inv_lut
    i = lmcs->min_bin_idx;
    for (uint16_t sample = 0; sample < max; sample++) {
        uint16_t inv_sample;
        while (i <= lmcs->max_bin_idx && sample >= lmcs->pivot[i + 1])
            i++;

        inv_sample = lmcs_derive_lut_sample(sample, input_pivot, lmcs->pivot,
            inv_scale_coeff, i, max);

        if (bit_depth > 8)
            lmcs->inv_lut.u16[sample] = inv_sample;
        else
            lmcs->inv_lut.u8 [sample] = inv_sample;
    }

    return 0;
}

static int ph_max_num_subblock_merge_cand(const H266RawSPS *sps, const H266RawPictureHeader *ph)
{
    if (sps->sps_affine_enabled_flag)
        return 5 - sps->sps_five_minus_max_num_subblock_merge_cand;
    return sps->sps_sbtmvp_enabled_flag && ph->ph_temporal_mvp_enabled_flag;
}

static int ph_derive(VVCPH *ph, const H266RawSPS *sps, const H266RawPPS *pps, const int poc_tid0, const int is_clvss)
{
    ph->max_num_subblock_merge_cand = ph_max_num_subblock_merge_cand(sps, ph->r);

    ph->poc = ph_compute_poc(ph->r, sps, poc_tid0, is_clvss);

    if (pps->pps_wp_info_in_ph_flag)
        pred_weight_table(&ph->pwt, &ph->r->ph_pred_weight_table);

    return 0;
}

static int decode_ph(VVCFrameParamSets *fps, const H266RawPictureHeader *rph, void *rph_ref,
    const int poc_tid0, const int is_clvss)
{
    int ret;
    VVCPH *ph = &fps->ph;
    const H266RawSPS *sps = fps->sps->r;
    const H266RawPPS *pps = fps->pps->r;

    ph->r = rph;
    ff_refstruct_replace(&ph->rref, rph_ref);
    ret = ph_derive(ph, sps, pps, poc_tid0, is_clvss);
    if (ret < 0)
        return ret;

    return 0;
}

static int decode_frame_ps(VVCFrameParamSets *fps, const VVCParamSets *ps,
    const CodedBitstreamH266Context *h266, const int poc_tid0, const int is_clvss)
{
    const H266RawPictureHeader *ph = h266->ph;
    const H266RawPPS *rpps;
    int ret;

    if (!ph)
        return AVERROR_INVALIDDATA;

    rpps = h266->pps[ph->ph_pic_parameter_set_id];
    if (!rpps)
        return AVERROR_INVALIDDATA;

    ff_refstruct_replace(&fps->sps, ps->sps_list[rpps->pps_seq_parameter_set_id]);
    ff_refstruct_replace(&fps->pps, ps->pps_list[rpps->pps_pic_parameter_set_id]);

    ret = decode_ph(fps, ph, h266->ph_ref, poc_tid0, is_clvss);
    if (ret < 0)
        return ret;

    if (ph->ph_explicit_scaling_list_enabled_flag)
        ff_refstruct_replace(&fps->sl, ps->scaling_list[ph->ph_scaling_list_aps_id]);

    if (ph->ph_lmcs_enabled_flag) {
        ret = lmcs_derive_lut(&fps->lmcs, ps->lmcs_list[ph->ph_lmcs_aps_id], fps->sps->r);
        if (ret < 0)
            return ret;
    }

    for (int i = 0; i < FF_ARRAY_ELEMS(fps->alf_list); i++)
        ff_refstruct_replace(&fps->alf_list[i], ps->alf_list[i]);

    return 0;
}

static void decode_recovery_flag(VVCContext *s)
{
    if (IS_IDR(s))
        s->no_output_before_recovery_flag = 1;
    else if (IS_CRA(s) || IS_GDR(s))
        s->no_output_before_recovery_flag = s->last_eos;
}

static void decode_recovery_poc(VVCContext *s, const VVCPH *ph)
{
    if (s->no_output_before_recovery_flag) {
        if (IS_GDR(s))
            s->gdr_recovery_point_poc = ph->poc + ph->r->ph_recovery_poc_cnt;
        if (!GDR_IS_RECOVERED(s) && s->gdr_recovery_point_poc <= ph->poc)
            GDR_SET_RECOVERED(s);
    }
}

int ff_vvc_decode_frame_ps(VVCFrameParamSets *fps, struct VVCContext *s)
{
    int ret = 0;
    VVCParamSets *ps                        = &s->ps;
    const CodedBitstreamH266Context *h266   = s->cbc->priv_data;
    int is_clvss;

    decode_recovery_flag(s);
    is_clvss = IS_CLVSS(s);

    ret = decode_ps(ps, h266, s->avctx, is_clvss);
    if (ret < 0)
        return ret;

    ret = decode_frame_ps(fps, ps, h266, s->poc_tid0, is_clvss);
    decode_recovery_poc(s, &fps->ph);
    return ret;
}

void ff_vvc_frame_ps_free(VVCFrameParamSets *fps)
{
    ff_refstruct_unref(&fps->sps);
    ff_refstruct_unref(&fps->pps);
    ff_refstruct_unref(&fps->ph.rref);
    ff_refstruct_unref(&fps->sl);
    for (int i = 0; i < FF_ARRAY_ELEMS(fps->alf_list); i++)
        ff_refstruct_unref(&fps->alf_list[i]);
}

void ff_vvc_ps_uninit(VVCParamSets *ps)
{
    for (int i = 0; i < FF_ARRAY_ELEMS(ps->scaling_list); i++)
        ff_refstruct_unref(&ps->scaling_list[i]);
    for (int i = 0; i < FF_ARRAY_ELEMS(ps->lmcs_list); i++)
        ff_refstruct_unref(&ps->lmcs_list[i]);
    for (int i = 0; i < FF_ARRAY_ELEMS(ps->alf_list); i++)
        ff_refstruct_unref(&ps->alf_list[i]);
    for (int i = 0; i < FF_ARRAY_ELEMS(ps->sps_list); i++)
        ff_refstruct_unref(&ps->sps_list[i]);
    for (int i = 0; i < FF_ARRAY_ELEMS(ps->pps_list); i++)
        ff_refstruct_unref(&ps->pps_list[i]);
}

static void alf_coeff(int16_t *coeff,
    const uint8_t *abs, const uint8_t *sign, const int size)
{
    for (int i = 0; i < size; i++)
        coeff[i] = (1 - 2 * sign[i]) * abs[i];
}

static void alf_coeff_cc(int16_t *coeff,
    const uint8_t *mapped_abs, const uint8_t *sign)
{
    for (int i = 0; i < ALF_NUM_COEFF_CC; i++) {
        int c = mapped_abs[i];
        if (c)
            c = (1 - 2 * sign[i]) * (1 << (c - 1));
        coeff[i] = c;
    }
}

static void alf_luma(VVCALF *alf, const H266RawAPS *aps)
{
    if (!aps->alf_luma_filter_signal_flag)
        return;

    for (int i = 0; i < ALF_NUM_FILTERS_LUMA; i++) {
        const int ref       = aps->alf_luma_coeff_delta_idx[i];
        const uint8_t *abs  = aps->alf_luma_coeff_abs[ref];
        const uint8_t *sign = aps->alf_luma_coeff_sign[ref];

        alf_coeff(alf->luma_coeff[i], abs, sign, ALF_NUM_COEFF_LUMA);
        memcpy(alf->luma_clip_idx[i], aps->alf_luma_clip_idx[ref],
            sizeof(alf->luma_clip_idx[i]));
    }
}

static void alf_chroma(VVCALF *alf, const H266RawAPS *aps)
{
    if (!aps->alf_chroma_filter_signal_flag)
        return;

    alf->num_chroma_filters  = aps->alf_chroma_num_alt_filters_minus1 + 1;
    for (int i = 0; i < alf->num_chroma_filters; i++) {
        const uint8_t *abs  = aps->alf_chroma_coeff_abs[i];
        const uint8_t *sign = aps->alf_chroma_coeff_sign[i];

        alf_coeff(alf->chroma_coeff[i], abs, sign, ALF_NUM_COEFF_CHROMA);
        memcpy(alf->chroma_clip_idx[i], aps->alf_chroma_clip_idx[i],
            sizeof(alf->chroma_clip_idx[i]));
    }
}

static void alf_cc(VVCALF *alf, const H266RawAPS *aps)
{
    const uint8_t (*abs[])[ALF_NUM_COEFF_CC] =
        { aps->alf_cc_cb_mapped_coeff_abs, aps->alf_cc_cr_mapped_coeff_abs };
    const uint8_t (*sign[])[ALF_NUM_COEFF_CC] =
        {aps->alf_cc_cb_coeff_sign, aps->alf_cc_cr_coeff_sign };
    const int signaled[] = { aps->alf_cc_cb_filter_signal_flag, aps->alf_cc_cr_filter_signal_flag};

    alf->num_cc_filters[0] = aps->alf_cc_cb_filters_signalled_minus1 + 1;
    alf->num_cc_filters[1] = aps->alf_cc_cr_filters_signalled_minus1 + 1;

    for (int idx = 0; idx < 2; idx++) {
        if (signaled[idx]) {
            for (int i = 0; i < alf->num_cc_filters[idx]; i++)
                alf_coeff_cc(alf->cc_coeff[idx][i], abs[idx][i], sign[idx][i]);
        }
    }
}

static void alf_derive(VVCALF *alf, const H266RawAPS *aps)
{
    alf_luma(alf, aps);
    alf_chroma(alf, aps);
    alf_cc(alf, aps);
}

static int aps_decode_alf(const VVCALF **alf, const H266RawAPS *aps)
{
    VVCALF *a = ff_refstruct_allocz(sizeof(*a));
    if (!a)
        return AVERROR(ENOMEM);

    alf_derive(a, aps);
    ff_refstruct_replace(alf, a);
    ff_refstruct_unref(&a);

    return 0;
}

static int is_luma_list(const int id)
{
    return id % VVC_MAX_SAMPLE_ARRAYS == SL_START_4x4 || id == SL_START_64x64 + 1;
}

static int derive_matrix_size(const int id)
{
    return id < SL_START_4x4 ? 2 : (id < SL_START_8x8 ? 4 : 8);
}

// 7.4.3.20 Scaling list data semantics
static void scaling_derive(VVCScalingList *sl, const H266RawAPS *aps)
{
    for (int id = 0; id < SL_MAX_ID; id++) {
        const int matrix_size   = derive_matrix_size(id);
        const int log2_size     = av_log2(matrix_size);
        const int list_size     = matrix_size * matrix_size;
        int coeff[SL_MAX_MATRIX_SIZE * SL_MAX_MATRIX_SIZE];
        const uint8_t *pred;
        const int *scaling_list;
        int dc = 0;

        if (aps->aps_chroma_present_flag || is_luma_list(id)) {
            if (!aps->scaling_list_copy_mode_flag[id]) {
                int next_coef = 0;

                if (id >= SL_START_16x16)
                    dc = next_coef = aps->scaling_list_dc_coef[id - SL_START_16x16];

                for (int i = 0; i < list_size; i++) {
                    const int x = ff_vvc_diag_scan_x[3][3][i];
                    const int y = ff_vvc_diag_scan_y[3][3][i];

                    if (!(id >= SL_START_64x64 && x >= 4 && y >= 4))
                        next_coef += aps->scaling_list_delta_coef[id][i];
                    coeff[i] = next_coef;
                }
            }
        }

        //dc
        if (id >= SL_START_16x16) {
            if (!aps->scaling_list_copy_mode_flag[id] && !aps->scaling_list_pred_mode_flag[id]) {
                sl->scaling_matrix_dc_rec[id - SL_START_16x16] = 8;
            } else if (!aps->scaling_list_pred_id_delta[id]) {
                sl->scaling_matrix_dc_rec[id - SL_START_16x16] = 16;
            } else {
                const int ref_id = id - aps->scaling_list_pred_id_delta[id];
                if (ref_id >= SL_START_16x16)
                    dc += sl->scaling_matrix_dc_rec[ref_id - SL_START_16x16];
                else
                    dc += sl->scaling_matrix_rec[ref_id][0];
                sl->scaling_matrix_dc_rec[id - SL_START_16x16] = dc & 255;
            }
        }

        //ac
        scaling_list = aps->scaling_list_copy_mode_flag[id] ? ff_vvc_scaling_list0 : coeff;
        if (!aps->scaling_list_copy_mode_flag[id] && !aps->scaling_list_pred_mode_flag[id])
            pred = ff_vvc_scaling_pred_8;
        else if (!aps->scaling_list_pred_id_delta[id])
            pred = ff_vvc_scaling_pred_16;
        else
            pred = sl->scaling_matrix_rec[id - aps->scaling_list_pred_id_delta[id]];
        for (int i = 0; i < list_size; i++) {
            const int x = ff_vvc_diag_scan_x[log2_size][log2_size][i];
            const int y = ff_vvc_diag_scan_y[log2_size][log2_size][i];
            const int off = y * matrix_size + x;
            sl->scaling_matrix_rec[id][off] = (pred[off] + scaling_list[i]) & 255;
        }
    }
}

static int aps_decode_scaling(const VVCScalingList **scaling, const H266RawAPS *aps)
{
    VVCScalingList *sl = ff_refstruct_allocz(sizeof(*sl));
    if (!sl)
        return AVERROR(ENOMEM);

    scaling_derive(sl, aps);
    ff_refstruct_replace(scaling, sl);
    ff_refstruct_unref(&sl);

    return 0;
}

int ff_vvc_decode_aps(VVCParamSets *ps, const CodedBitstreamUnit *unit)
{
    const H266RawAPS *aps = unit->content_ref;
    int ret               = 0;

    if (!aps)
        return AVERROR_INVALIDDATA;

    switch (aps->aps_params_type) {
        case VVC_ASP_TYPE_ALF:
            ret = aps_decode_alf(&ps->alf_list[aps->aps_adaptation_parameter_set_id], aps);
            break;
        case VVC_ASP_TYPE_LMCS:
            ff_refstruct_replace(&ps->lmcs_list[aps->aps_adaptation_parameter_set_id], aps);
            break;
        case VVC_ASP_TYPE_SCALING:
            ret = aps_decode_scaling(&ps->scaling_list[aps->aps_adaptation_parameter_set_id], aps);
            break;
    }

    return ret;
}

static int sh_alf_aps(const VVCSH *sh, const VVCFrameParamSets *fps)
{
    if (!sh->r->sh_alf_enabled_flag)
        return 0;

    for (int i = 0; i < sh->r->sh_num_alf_aps_ids_luma; i++) {
        const VVCALF *alf_aps_luma = fps->alf_list[sh->r->sh_alf_aps_id_luma[i]];
        if (!alf_aps_luma)
            return AVERROR_INVALIDDATA;
    }

    if (sh->r->sh_alf_cb_enabled_flag || sh->r->sh_alf_cr_enabled_flag) {
        const VVCALF *alf_aps_chroma = fps->alf_list[sh->r->sh_alf_aps_id_chroma];
        if (!alf_aps_chroma)
            return AVERROR_INVALIDDATA;
    }

    if (fps->sps->r->sps_ccalf_enabled_flag) {
        if (sh->r->sh_alf_cc_cb_enabled_flag) {
            const VVCALF *alf_aps_cc_cr = fps->alf_list[sh->r->sh_alf_cc_cb_aps_id];
            if (!alf_aps_cc_cr)
                return AVERROR_INVALIDDATA;
        }
        if (sh->r->sh_alf_cc_cr_enabled_flag) {
            const VVCALF *alf_aps_cc_cr = fps->alf_list[sh->r->sh_alf_cc_cr_aps_id];
            if (!alf_aps_cc_cr)
                return AVERROR_INVALIDDATA;
        }
    }

    return 0;
}

static void sh_slice_address(VVCSH *sh, const H266RawSPS *sps, const VVCPPS *pps)
{
    const int slice_address     = sh->r->sh_slice_address;

    if (pps->r->pps_rect_slice_flag) {
        int pic_level_slice_idx = slice_address;
        for (int j = 0; j < sh->r->curr_subpic_idx; j++)
            pic_level_slice_idx += pps->r->num_slices_in_subpic[j];
        sh->ctb_addr_in_curr_slice = pps->ctb_addr_in_slice + pps->slice_start_offset[pic_level_slice_idx];
        sh->num_ctus_in_curr_slice = pps->num_ctus_in_slice[pic_level_slice_idx];
    } else {
        int tile_x = slice_address % pps->r->num_tile_columns;
        int tile_y = slice_address / pps->r->num_tile_columns;
        const int slice_start_ctb = pps->row_bd[tile_y] * pps->ctb_width + pps->col_bd[tile_x] * pps->r->row_height_val[tile_y];

        sh->ctb_addr_in_curr_slice = pps->ctb_addr_in_slice + slice_start_ctb;

        sh->num_ctus_in_curr_slice = 0;
        for (int tile_idx = slice_address; tile_idx <= slice_address + sh->r->sh_num_tiles_in_slice_minus1; tile_idx++) {
            tile_x = tile_idx % pps->r->num_tile_columns;
            tile_y = tile_idx / pps->r->num_tile_columns;
            sh->num_ctus_in_curr_slice += pps->r->row_height_val[tile_y] * pps->r->col_width_val[tile_x];
        }
    }
}

static void sh_qp_y(VVCSH *sh, const H266RawPPS *pps, const H266RawPictureHeader *ph)
{
    const int init_qp = pps->pps_init_qp_minus26 + 26;

    if (!pps->pps_qp_delta_info_in_ph_flag)
        sh->slice_qp_y = init_qp + sh->r->sh_qp_delta;
    else
        sh->slice_qp_y = init_qp + ph->ph_qp_delta;
}

static void sh_inter(VVCSH *sh, const H266RawSPS *sps, const H266RawPPS *pps)
{
    const H266RawSliceHeader *rsh = sh->r;

    if (!pps->pps_wp_info_in_ph_flag &&
        ((pps->pps_weighted_pred_flag && IS_P(rsh)) ||
         (pps->pps_weighted_bipred_flag && IS_B(rsh))))
        pred_weight_table(&sh->pwt, &rsh->sh_pred_weight_table);
}

static void sh_deblock_offsets(VVCSH *sh)
{
    const H266RawSliceHeader *r = sh->r;

    if (!r->sh_deblocking_filter_disabled_flag) {
        sh->deblock.beta_offset[LUMA] = r->sh_luma_beta_offset_div2 * 2;
        sh->deblock.tc_offset[LUMA]   = r->sh_luma_tc_offset_div2 * 2;
        sh->deblock.beta_offset[CB]   = r->sh_cb_beta_offset_div2 * 2;
        sh->deblock.tc_offset[CB]     = r->sh_cb_tc_offset_div2 * 2;
        sh->deblock.beta_offset[CR]   = r->sh_cr_beta_offset_div2 * 2;
        sh->deblock.tc_offset[CR]     = r->sh_cr_tc_offset_div2 * 2;
    }
}

static void sh_partition_constraints(VVCSH *sh, const H266RawSPS *sps, const H266RawPictureHeader *ph)
{
    const int min_cb_log2_size_y = sps->sps_log2_min_luma_coding_block_size_minus2 + 2;
    int min_qt_log2_size_y[2];

    if (IS_I(sh->r)) {
        min_qt_log2_size_y[LUMA]        = (min_cb_log2_size_y + ph->ph_log2_diff_min_qt_min_cb_intra_slice_luma);
        min_qt_log2_size_y[CHROMA]      = (min_cb_log2_size_y + ph->ph_log2_diff_min_qt_min_cb_intra_slice_chroma);

        sh->max_bt_size[LUMA]           = 1 << (min_qt_log2_size_y[LUMA]  + ph->ph_log2_diff_max_bt_min_qt_intra_slice_luma);
        sh->max_bt_size[CHROMA]         = 1 << (min_qt_log2_size_y[CHROMA]+ ph->ph_log2_diff_max_bt_min_qt_intra_slice_chroma);

        sh->max_tt_size[LUMA]           = 1 << (min_qt_log2_size_y[LUMA]  + ph->ph_log2_diff_max_tt_min_qt_intra_slice_luma);
        sh->max_tt_size[CHROMA]         = 1 << (min_qt_log2_size_y[CHROMA]+ ph->ph_log2_diff_max_tt_min_qt_intra_slice_chroma);

        sh->max_mtt_depth[LUMA]         = ph->ph_max_mtt_hierarchy_depth_intra_slice_luma;
        sh->max_mtt_depth[CHROMA]       = ph->ph_max_mtt_hierarchy_depth_intra_slice_chroma;

        sh->cu_qp_delta_subdiv          = ph->ph_cu_qp_delta_subdiv_intra_slice;
        sh->cu_chroma_qp_offset_subdiv  = ph->ph_cu_chroma_qp_offset_subdiv_intra_slice;
    } else {
        for (int i = LUMA; i <= CHROMA; i++)  {
            min_qt_log2_size_y[i]        = (min_cb_log2_size_y + ph->ph_log2_diff_min_qt_min_cb_inter_slice);
            sh->max_bt_size[i]           = 1 << (min_qt_log2_size_y[i]  + ph->ph_log2_diff_max_bt_min_qt_inter_slice);
            sh->max_tt_size[i]           = 1 << (min_qt_log2_size_y[i]  + ph->ph_log2_diff_max_tt_min_qt_inter_slice);
            sh->max_mtt_depth[i]         = ph->ph_max_mtt_hierarchy_depth_inter_slice;
        }

        sh->cu_qp_delta_subdiv          = ph->ph_cu_qp_delta_subdiv_inter_slice;
        sh->cu_chroma_qp_offset_subdiv  = ph->ph_cu_chroma_qp_offset_subdiv_inter_slice;
    }

    sh->min_qt_size[LUMA]   = 1 << min_qt_log2_size_y[LUMA];
    sh->min_qt_size[CHROMA] = 1 << min_qt_log2_size_y[CHROMA];
}

static void sh_entry_points(VVCSH *sh, const H266RawSPS *sps, const VVCPPS *pps)
{
    if (sps->sps_entry_point_offsets_present_flag) {
        for (int i = 1, j = 0; i < sh->num_ctus_in_curr_slice; i++) {
            const int pre_ctb_addr_x = sh->ctb_addr_in_curr_slice[i - 1] % pps->ctb_width;
            const int pre_ctb_addr_y = sh->ctb_addr_in_curr_slice[i - 1] / pps->ctb_width;
            const int ctb_addr_x     = sh->ctb_addr_in_curr_slice[i] % pps->ctb_width;
            const int ctb_addr_y     = sh->ctb_addr_in_curr_slice[i] / pps->ctb_width;
            if (pps->ctb_to_row_bd[ctb_addr_y] != pps->ctb_to_row_bd[pre_ctb_addr_y] ||
                pps->ctb_to_col_bd[ctb_addr_x] != pps->ctb_to_col_bd[pre_ctb_addr_x] ||
                (ctb_addr_y != pre_ctb_addr_y && sps->sps_entropy_coding_sync_enabled_flag)) {
                sh->entry_point_start_ctu[j++] = i;
            }
        }
    }
}

static int sh_derive(VVCSH *sh, const VVCFrameParamSets *fps)
{
    const H266RawSPS *sps           = fps->sps->r;
    const H266RawPPS *pps           = fps->pps->r;
    const H266RawPictureHeader *ph  = fps->ph.r;
    int ret;

    sh_slice_address(sh, sps, fps->pps);
    ret = sh_alf_aps(sh, fps);
    if (ret < 0)
        return ret;
    sh_inter(sh, sps, pps);
    sh_qp_y(sh, pps, ph);
    sh_deblock_offsets(sh);
    sh_partition_constraints(sh, sps, ph);
    sh_entry_points(sh, sps, fps->pps);

    return 0;
}

int ff_vvc_decode_sh(VVCSH *sh, const VVCFrameParamSets *fps, const CodedBitstreamUnit *unit)
{
    int ret;

    if (!fps->sps || !fps->pps)
        return AVERROR_INVALIDDATA;

    ff_refstruct_replace(&sh->r, unit->content_ref);

    ret = sh_derive(sh, fps);
    if (ret < 0)
        return ret;

    return 0;
}
