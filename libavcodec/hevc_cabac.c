/*
 * HEVC CABAC decoding
 *
 * Copyright (C) 2012 - 2013 Guillaume Martres
 * Copyright (C) 2012 - 2013 Gildas Cocherel
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

#include "libavutil/attributes.h"
#include "libavutil/common.h"

#include "cabac_functions.h"
#include "hevc_data.h"
#include "hevc.h"
#include "hevcdec.h"

#define CABAC_MAX_BIN 31

/**
 * number of bin by SyntaxElement.
 */
static const int8_t num_bins_in_se[] = {
     1, // sao_merge_flag
     1, // sao_type_idx
     0, // sao_eo_class
     0, // sao_band_position
     0, // sao_offset_abs
     0, // sao_offset_sign
     0, // end_of_slice_flag
     3, // split_coding_unit_flag
     1, // cu_transquant_bypass_flag
     3, // skip_flag
     3, // cu_qp_delta
     1, // pred_mode
     4, // part_mode
     0, // pcm_flag
     1, // prev_intra_luma_pred_mode
     0, // mpm_idx
     0, // rem_intra_luma_pred_mode
     2, // intra_chroma_pred_mode
     1, // merge_flag
     1, // merge_idx
     5, // inter_pred_idc
     2, // ref_idx_l0
     2, // ref_idx_l1
     2, // abs_mvd_greater0_flag
     2, // abs_mvd_greater1_flag
     0, // abs_mvd_minus2
     0, // mvd_sign_flag
     1, // mvp_lx_flag
     1, // no_residual_data_flag
     3, // split_transform_flag
     2, // cbf_luma
     5, // cbf_cb, cbf_cr
     2, // transform_skip_flag[][]
     2, // explicit_rdpcm_flag[][]
     2, // explicit_rdpcm_dir_flag[][]
    18, // last_significant_coeff_x_prefix
    18, // last_significant_coeff_y_prefix
     0, // last_significant_coeff_x_suffix
     0, // last_significant_coeff_y_suffix
     4, // significant_coeff_group_flag
    44, // significant_coeff_flag
    24, // coeff_abs_level_greater1_flag
     6, // coeff_abs_level_greater2_flag
     0, // coeff_abs_level_remaining
     0, // coeff_sign_flag
     8, // log2_res_scale_abs
     2, // res_scale_sign_flag
     1, // cu_chroma_qp_offset_flag
     1, // cu_chroma_qp_offset_idx
};

/**
 * Offset to ctxIdx 0 in init_values and states, indexed by SyntaxElement.
 */
static const int elem_offset[sizeof(num_bins_in_se)] = {
    0, // sao_merge_flag
    1, // sao_type_idx
    2, // sao_eo_class
    2, // sao_band_position
    2, // sao_offset_abs
    2, // sao_offset_sign
    2, // end_of_slice_flag
    2, // split_coding_unit_flag
    5, // cu_transquant_bypass_flag
    6, // skip_flag
    9, // cu_qp_delta
    12, // pred_mode
    13, // part_mode
    17, // pcm_flag
    17, // prev_intra_luma_pred_mode
    18, // mpm_idx
    18, // rem_intra_luma_pred_mode
    18, // intra_chroma_pred_mode
    20, // merge_flag
    21, // merge_idx
    22, // inter_pred_idc
    27, // ref_idx_l0
    29, // ref_idx_l1
    31, // abs_mvd_greater0_flag
    33, // abs_mvd_greater1_flag
    35, // abs_mvd_minus2
    35, // mvd_sign_flag
    35, // mvp_lx_flag
    36, // no_residual_data_flag
    37, // split_transform_flag
    40, // cbf_luma
    42, // cbf_cb, cbf_cr
    47, // transform_skip_flag[][]
    49, // explicit_rdpcm_flag[][]
    51, // explicit_rdpcm_dir_flag[][]
    53, // last_significant_coeff_x_prefix
    71, // last_significant_coeff_y_prefix
    89, // last_significant_coeff_x_suffix
    89, // last_significant_coeff_y_suffix
    89, // significant_coeff_group_flag
    93, // significant_coeff_flag
    137, // coeff_abs_level_greater1_flag
    161, // coeff_abs_level_greater2_flag
    167, // coeff_abs_level_remaining
    167, // coeff_sign_flag
    167, // log2_res_scale_abs
    175, // res_scale_sign_flag
    177, // cu_chroma_qp_offset_flag
    178, // cu_chroma_qp_offset_idx
};

#define CNU 154
/**
 * Indexed by init_type
 */
static const uint8_t init_values[3][HEVC_CONTEXTS] = {
    { // sao_merge_flag
      153,
      // sao_type_idx
      200,
      // split_coding_unit_flag
      139, 141, 157,
      // cu_transquant_bypass_flag
      154,
      // skip_flag
      CNU, CNU, CNU,
      // cu_qp_delta
      154, 154, 154,
      // pred_mode
      CNU,
      // part_mode
      184, CNU, CNU, CNU,
      // prev_intra_luma_pred_mode
      184,
      // intra_chroma_pred_mode
      63, 139,
      // merge_flag
      CNU,
      // merge_idx
      CNU,
      // inter_pred_idc
      CNU, CNU, CNU, CNU, CNU,
      // ref_idx_l0
      CNU, CNU,
      // ref_idx_l1
      CNU, CNU,
      // abs_mvd_greater1_flag
      CNU, CNU,
      // abs_mvd_greater1_flag
      CNU, CNU,
      // mvp_lx_flag
      CNU,
      // no_residual_data_flag
      CNU,
      // split_transform_flag
      153, 138, 138,
      // cbf_luma
      111, 141,
      // cbf_cb, cbf_cr
      94, 138, 182, 154, 154,
      // transform_skip_flag
      139, 139,
      // explicit_rdpcm_flag
      139, 139,
      // explicit_rdpcm_dir_flag
      139, 139,
      // last_significant_coeff_x_prefix
      110, 110, 124, 125, 140, 153, 125, 127, 140, 109, 111, 143, 127, 111,
       79, 108, 123,  63,
      // last_significant_coeff_y_prefix
      110, 110, 124, 125, 140, 153, 125, 127, 140, 109, 111, 143, 127, 111,
       79, 108, 123,  63,
      // significant_coeff_group_flag
      91, 171, 134, 141,
      // significant_coeff_flag
      111, 111, 125, 110, 110,  94, 124, 108, 124, 107, 125, 141, 179, 153,
      125, 107, 125, 141, 179, 153, 125, 107, 125, 141, 179, 153, 125, 140,
      139, 182, 182, 152, 136, 152, 136, 153, 136, 139, 111, 136, 139, 111,
      141, 111,
      // coeff_abs_level_greater1_flag
      140,  92, 137, 138, 140, 152, 138, 139, 153,  74, 149,  92, 139, 107,
      122, 152, 140, 179, 166, 182, 140, 227, 122, 197,
      // coeff_abs_level_greater2_flag
      138, 153, 136, 167, 152, 152,
      // log2_res_scale_abs
      154, 154, 154, 154, 154, 154, 154, 154,
      // res_scale_sign_flag
      154, 154,
      // cu_chroma_qp_offset_flag
      154,
      // cu_chroma_qp_offset_idx
      154,
    },
    { // sao_merge_flag
      153,
      // sao_type_idx
      185,
      // split_coding_unit_flag
      107, 139, 126,
      // cu_transquant_bypass_flag
      154,
      // skip_flag
      197, 185, 201,
      // cu_qp_delta
      154, 154, 154,
      // pred_mode
      149,
      // part_mode
      154, 139, 154, 154,
      // prev_intra_luma_pred_mode
      154,
      // intra_chroma_pred_mode
      152, 139,
      // merge_flag
      110,
      // merge_idx
      122,
      // inter_pred_idc
      95, 79, 63, 31, 31,
      // ref_idx_l0
      153, 153,
      // ref_idx_l1
      153, 153,
      // abs_mvd_greater1_flag
      140, 198,
      // abs_mvd_greater1_flag
      140, 198,
      // mvp_lx_flag
      168,
      // no_residual_data_flag
      79,
      // split_transform_flag
      124, 138, 94,
      // cbf_luma
      153, 111,
      // cbf_cb, cbf_cr
      149, 107, 167, 154, 154,
      // transform_skip_flag
      139, 139,
      // explicit_rdpcm_flag
      139, 139,
      // explicit_rdpcm_dir_flag
      139, 139,
      // last_significant_coeff_x_prefix
      125, 110,  94, 110,  95,  79, 125, 111, 110,  78, 110, 111, 111,  95,
       94, 108, 123, 108,
      // last_significant_coeff_y_prefix
      125, 110,  94, 110,  95,  79, 125, 111, 110,  78, 110, 111, 111,  95,
       94, 108, 123, 108,
      // significant_coeff_group_flag
      121, 140, 61, 154,
      // significant_coeff_flag
      155, 154, 139, 153, 139, 123, 123,  63, 153, 166, 183, 140, 136, 153,
      154, 166, 183, 140, 136, 153, 154, 166, 183, 140, 136, 153, 154, 170,
      153, 123, 123, 107, 121, 107, 121, 167, 151, 183, 140, 151, 183, 140,
      140, 140,
      // coeff_abs_level_greater1_flag
      154, 196, 196, 167, 154, 152, 167, 182, 182, 134, 149, 136, 153, 121,
      136, 137, 169, 194, 166, 167, 154, 167, 137, 182,
      // coeff_abs_level_greater2_flag
      107, 167, 91, 122, 107, 167,
      // log2_res_scale_abs
      154, 154, 154, 154, 154, 154, 154, 154,
      // res_scale_sign_flag
      154, 154,
      // cu_chroma_qp_offset_flag
      154,
      // cu_chroma_qp_offset_idx
      154,
    },
    { // sao_merge_flag
      153,
      // sao_type_idx
      160,
      // split_coding_unit_flag
      107, 139, 126,
      // cu_transquant_bypass_flag
      154,
      // skip_flag
      197, 185, 201,
      // cu_qp_delta
      154, 154, 154,
      // pred_mode
      134,
      // part_mode
      154, 139, 154, 154,
      // prev_intra_luma_pred_mode
      183,
      // intra_chroma_pred_mode
      152, 139,
      // merge_flag
      154,
      // merge_idx
      137,
      // inter_pred_idc
      95, 79, 63, 31, 31,
      // ref_idx_l0
      153, 153,
      // ref_idx_l1
      153, 153,
      // abs_mvd_greater1_flag
      169, 198,
      // abs_mvd_greater1_flag
      169, 198,
      // mvp_lx_flag
      168,
      // no_residual_data_flag
      79,
      // split_transform_flag
      224, 167, 122,
      // cbf_luma
      153, 111,
      // cbf_cb, cbf_cr
      149, 92, 167, 154, 154,
      // transform_skip_flag
      139, 139,
      // explicit_rdpcm_flag
      139, 139,
      // explicit_rdpcm_dir_flag
      139, 139,
      // last_significant_coeff_x_prefix
      125, 110, 124, 110,  95,  94, 125, 111, 111,  79, 125, 126, 111, 111,
       79, 108, 123,  93,
      // last_significant_coeff_y_prefix
      125, 110, 124, 110,  95,  94, 125, 111, 111,  79, 125, 126, 111, 111,
       79, 108, 123,  93,
      // significant_coeff_group_flag
      121, 140, 61, 154,
      // significant_coeff_flag
      170, 154, 139, 153, 139, 123, 123,  63, 124, 166, 183, 140, 136, 153,
      154, 166, 183, 140, 136, 153, 154, 166, 183, 140, 136, 153, 154, 170,
      153, 138, 138, 122, 121, 122, 121, 167, 151, 183, 140, 151, 183, 140,
      140, 140,
      // coeff_abs_level_greater1_flag
      154, 196, 167, 167, 154, 152, 167, 182, 182, 134, 149, 136, 153, 121,
      136, 122, 169, 208, 166, 167, 154, 152, 167, 182,
      // coeff_abs_level_greater2_flag
      107, 167, 91, 107, 107, 167,
      // log2_res_scale_abs
      154, 154, 154, 154, 154, 154, 154, 154,
      // res_scale_sign_flag
      154, 154,
      // cu_chroma_qp_offset_flag
      154,
      // cu_chroma_qp_offset_idx
      154,
    },
};

static const uint8_t scan_1x1[1] = {
    0,
};

static const uint8_t horiz_scan2x2_x[4] = {
    0, 1, 0, 1,
};

static const uint8_t horiz_scan2x2_y[4] = {
    0, 0, 1, 1
};

static const uint8_t horiz_scan4x4_x[16] = {
    0, 1, 2, 3,
    0, 1, 2, 3,
    0, 1, 2, 3,
    0, 1, 2, 3,
};

static const uint8_t horiz_scan4x4_y[16] = {
    0, 0, 0, 0,
    1, 1, 1, 1,
    2, 2, 2, 2,
    3, 3, 3, 3,
};

static const uint8_t horiz_scan8x8_inv[8][8] = {
    {  0,  1,  2,  3, 16, 17, 18, 19, },
    {  4,  5,  6,  7, 20, 21, 22, 23, },
    {  8,  9, 10, 11, 24, 25, 26, 27, },
    { 12, 13, 14, 15, 28, 29, 30, 31, },
    { 32, 33, 34, 35, 48, 49, 50, 51, },
    { 36, 37, 38, 39, 52, 53, 54, 55, },
    { 40, 41, 42, 43, 56, 57, 58, 59, },
    { 44, 45, 46, 47, 60, 61, 62, 63, },
};

static const uint8_t diag_scan2x2_x[4] = {
    0, 0, 1, 1,
};

static const uint8_t diag_scan2x2_y[4] = {
    0, 1, 0, 1,
};

static const uint8_t diag_scan2x2_inv[2][2] = {
    { 0, 2, },
    { 1, 3, },
};

static const uint8_t diag_scan4x4_inv[4][4] = {
    { 0,  2,  5,  9, },
    { 1,  4,  8, 12, },
    { 3,  7, 11, 14, },
    { 6, 10, 13, 15, },
};

static const uint8_t diag_scan8x8_inv[8][8] = {
    {  0,  2,  5,  9, 14, 20, 27, 35, },
    {  1,  4,  8, 13, 19, 26, 34, 42, },
    {  3,  7, 12, 18, 25, 33, 41, 48, },
    {  6, 11, 17, 24, 32, 40, 47, 53, },
    { 10, 16, 23, 31, 39, 46, 52, 57, },
    { 15, 22, 30, 38, 45, 51, 56, 60, },
    { 21, 29, 37, 44, 50, 55, 59, 62, },
    { 28, 36, 43, 49, 54, 58, 61, 63, },
};

void ff_hevc_save_states(HEVCContext *s, int ctb_addr_ts)
{
    if (s->ps.pps->entropy_coding_sync_enabled_flag &&
        (ctb_addr_ts % s->ps.sps->ctb_width == 2 ||
         (s->ps.sps->ctb_width == 2 &&
          ctb_addr_ts % s->ps.sps->ctb_width == 0))) {
        memcpy(s->cabac_state, s->HEVClc->cabac_state, HEVC_CONTEXTS);
    }
}

static void load_states(HEVCContext *s)
{
    memcpy(s->HEVClc->cabac_state, s->cabac_state, HEVC_CONTEXTS);
}

static int cabac_reinit(HEVCLocalContext *lc)
{
    return skip_bytes(&lc->cc, 0) == NULL ? AVERROR_INVALIDDATA : 0;
}

static int cabac_init_decoder(HEVCContext *s)
{
    GetBitContext *gb = &s->HEVClc->gb;
    skip_bits(gb, 1);
    align_get_bits(gb);
    return ff_init_cabac_decoder(&s->HEVClc->cc,
                          gb->buffer + get_bits_count(gb) / 8,
                          (get_bits_left(gb) + 7) / 8);
}

static void cabac_init_state(HEVCContext *s)
{
    int init_type = 2 - s->sh.slice_type;
    int i;

    if (s->sh.cabac_init_flag && s->sh.slice_type != HEVC_SLICE_I)
        init_type ^= 3;

    for (i = 0; i < HEVC_CONTEXTS; i++) {
        int init_value = init_values[init_type][i];
        int m = (init_value >> 4) * 5 - 45;
        int n = ((init_value & 15) << 3) - 16;
        int pre = 2 * (((m * av_clip(s->sh.slice_qp, 0, 51)) >> 4) + n) - 127;

        pre ^= pre >> 31;
        if (pre > 124)
            pre = 124 + (pre & 1);
        s->HEVClc->cabac_state[i] = pre;
    }

    for (i = 0; i < 4; i++)
        s->HEVClc->stat_coeff[i] = 0;
}

int ff_hevc_cabac_init(HEVCContext *s, int ctb_addr_ts)
{
    if (ctb_addr_ts == s->ps.pps->ctb_addr_rs_to_ts[s->sh.slice_ctb_addr_rs]) {
        int ret = cabac_init_decoder(s);
        if (ret < 0)
            return ret;
        if (s->sh.dependent_slice_segment_flag == 0 ||
            (s->ps.pps->tiles_enabled_flag &&
             s->ps.pps->tile_id[ctb_addr_ts] != s->ps.pps->tile_id[ctb_addr_ts - 1]))
            cabac_init_state(s);

        if (!s->sh.first_slice_in_pic_flag &&
            s->ps.pps->entropy_coding_sync_enabled_flag) {
            if (ctb_addr_ts % s->ps.sps->ctb_width == 0) {
                if (s->ps.sps->ctb_width == 1)
                    cabac_init_state(s);
                else if (s->sh.dependent_slice_segment_flag == 1)
                    load_states(s);
            }
        }
    } else {
        if (s->ps.pps->tiles_enabled_flag &&
            s->ps.pps->tile_id[ctb_addr_ts] != s->ps.pps->tile_id[ctb_addr_ts - 1]) {
            int ret;
            if (s->threads_number == 1)
                ret = cabac_reinit(s->HEVClc);
            else {
                ret = cabac_init_decoder(s);
            }
            if (ret < 0)
                return ret;
            cabac_init_state(s);
        }
        if (s->ps.pps->entropy_coding_sync_enabled_flag) {
            if (ctb_addr_ts % s->ps.sps->ctb_width == 0) {
                int ret;
                get_cabac_terminate(&s->HEVClc->cc);
                if (s->threads_number == 1)
                    ret = cabac_reinit(s->HEVClc);
                else {
                    ret = cabac_init_decoder(s);
                }
                if (ret < 0)
                    return ret;

                if (s->ps.sps->ctb_width == 1)
                    cabac_init_state(s);
                else
                    load_states(s);
            }
        }
    }
    return 0;
}

#define GET_CABAC(ctx) get_cabac(&s->HEVClc->cc, &s->HEVClc->cabac_state[ctx])

int ff_hevc_sao_merge_flag_decode(HEVCContext *s)
{
    return GET_CABAC(elem_offset[SAO_MERGE_FLAG]);
}

int ff_hevc_sao_type_idx_decode(HEVCContext *s)
{
    if (!GET_CABAC(elem_offset[SAO_TYPE_IDX]))
        return 0;

    if (!get_cabac_bypass(&s->HEVClc->cc))
        return SAO_BAND;
    return SAO_EDGE;
}

int ff_hevc_sao_band_position_decode(HEVCContext *s)
{
    int i;
    int value = get_cabac_bypass(&s->HEVClc->cc);

    for (i = 0; i < 4; i++)
        value = (value << 1) | get_cabac_bypass(&s->HEVClc->cc);
    return value;
}

int ff_hevc_sao_offset_abs_decode(HEVCContext *s)
{
    int i = 0;
    int length = (1 << (FFMIN(s->ps.sps->bit_depth, 10) - 5)) - 1;

    while (i < length && get_cabac_bypass(&s->HEVClc->cc))
        i++;
    return i;
}

int ff_hevc_sao_offset_sign_decode(HEVCContext *s)
{
    return get_cabac_bypass(&s->HEVClc->cc);
}

int ff_hevc_sao_eo_class_decode(HEVCContext *s)
{
    int ret = get_cabac_bypass(&s->HEVClc->cc) << 1;
    ret    |= get_cabac_bypass(&s->HEVClc->cc);
    return ret;
}

int ff_hevc_end_of_slice_flag_decode(HEVCContext *s)
{
    return get_cabac_terminate(&s->HEVClc->cc);
}

int ff_hevc_cu_transquant_bypass_flag_decode(HEVCContext *s)
{
    return GET_CABAC(elem_offset[CU_TRANSQUANT_BYPASS_FLAG]);
}

int ff_hevc_skip_flag_decode(HEVCContext *s, int x0, int y0, int x_cb, int y_cb)
{
    int min_cb_width = s->ps.sps->min_cb_width;
    int inc = 0;
    int x0b = av_mod_uintp2(x0, s->ps.sps->log2_ctb_size);
    int y0b = av_mod_uintp2(y0, s->ps.sps->log2_ctb_size);

    if (s->HEVClc->ctb_left_flag || x0b)
        inc = !!SAMPLE_CTB(s->skip_flag, x_cb - 1, y_cb);
    if (s->HEVClc->ctb_up_flag || y0b)
        inc += !!SAMPLE_CTB(s->skip_flag, x_cb, y_cb - 1);

    return GET_CABAC(elem_offset[SKIP_FLAG] + inc);
}

int ff_hevc_cu_qp_delta_abs(HEVCContext *s)
{
    int prefix_val = 0;
    int suffix_val = 0;
    int inc = 0;

    while (prefix_val < 5 && GET_CABAC(elem_offset[CU_QP_DELTA] + inc)) {
        prefix_val++;
        inc = 1;
    }
    if (prefix_val >= 5) {
        int k = 0;
        while (k < 7 && get_cabac_bypass(&s->HEVClc->cc)) {
            suffix_val += 1 << k;
            k++;
        }
        if (k == 7) {
            av_log(s->avctx, AV_LOG_ERROR, "CABAC_MAX_BIN : %d\n", k);
            return AVERROR_INVALIDDATA;
        }

        while (k--)
            suffix_val += get_cabac_bypass(&s->HEVClc->cc) << k;
    }
    return prefix_val + suffix_val;
}

int ff_hevc_cu_qp_delta_sign_flag(HEVCContext *s)
{
    return get_cabac_bypass(&s->HEVClc->cc);
}

int ff_hevc_cu_chroma_qp_offset_flag(HEVCContext *s)
{
    return GET_CABAC(elem_offset[CU_CHROMA_QP_OFFSET_FLAG]);
}

int ff_hevc_cu_chroma_qp_offset_idx(HEVCContext *s)
{
    int c_max= FFMAX(5, s->ps.pps->chroma_qp_offset_list_len_minus1);
    int i = 0;

    while (i < c_max && GET_CABAC(elem_offset[CU_CHROMA_QP_OFFSET_IDX]))
        i++;

    return i;
}

int ff_hevc_pred_mode_decode(HEVCContext *s)
{
    return GET_CABAC(elem_offset[PRED_MODE_FLAG]);
}

int ff_hevc_split_coding_unit_flag_decode(HEVCContext *s, int ct_depth, int x0, int y0)
{
    int inc = 0, depth_left = 0, depth_top = 0;
    int x0b  = av_mod_uintp2(x0, s->ps.sps->log2_ctb_size);
    int y0b  = av_mod_uintp2(y0, s->ps.sps->log2_ctb_size);
    int x_cb = x0 >> s->ps.sps->log2_min_cb_size;
    int y_cb = y0 >> s->ps.sps->log2_min_cb_size;

    if (s->HEVClc->ctb_left_flag || x0b)
        depth_left = s->tab_ct_depth[(y_cb) * s->ps.sps->min_cb_width + x_cb - 1];
    if (s->HEVClc->ctb_up_flag || y0b)
        depth_top = s->tab_ct_depth[(y_cb - 1) * s->ps.sps->min_cb_width + x_cb];

    inc += (depth_left > ct_depth);
    inc += (depth_top  > ct_depth);

    return GET_CABAC(elem_offset[SPLIT_CODING_UNIT_FLAG] + inc);
}

int ff_hevc_part_mode_decode(HEVCContext *s, int log2_cb_size)
{
    if (GET_CABAC(elem_offset[PART_MODE])) // 1
        return PART_2Nx2N;
    if (log2_cb_size == s->ps.sps->log2_min_cb_size) {
        if (s->HEVClc->cu.pred_mode == MODE_INTRA) // 0
            return PART_NxN;
        if (GET_CABAC(elem_offset[PART_MODE] + 1)) // 01
            return PART_2NxN;
        if (log2_cb_size == 3) // 00
            return PART_Nx2N;
        if (GET_CABAC(elem_offset[PART_MODE] + 2)) // 001
            return PART_Nx2N;
        return PART_NxN; // 000
    }

    if (!s->ps.sps->amp_enabled_flag) {
        if (GET_CABAC(elem_offset[PART_MODE] + 1)) // 01
            return PART_2NxN;
        return PART_Nx2N;
    }

    if (GET_CABAC(elem_offset[PART_MODE] + 1)) { // 01X, 01XX
        if (GET_CABAC(elem_offset[PART_MODE] + 3)) // 011
            return PART_2NxN;
        if (get_cabac_bypass(&s->HEVClc->cc)) // 0101
            return PART_2NxnD;
        return PART_2NxnU; // 0100
    }

    if (GET_CABAC(elem_offset[PART_MODE] + 3)) // 001
        return PART_Nx2N;
    if (get_cabac_bypass(&s->HEVClc->cc)) // 0001
        return PART_nRx2N;
    return PART_nLx2N;  // 0000
}

int ff_hevc_pcm_flag_decode(HEVCContext *s)
{
    return get_cabac_terminate(&s->HEVClc->cc);
}

int ff_hevc_prev_intra_luma_pred_flag_decode(HEVCContext *s)
{
    return GET_CABAC(elem_offset[PREV_INTRA_LUMA_PRED_FLAG]);
}

int ff_hevc_mpm_idx_decode(HEVCContext *s)
{
    int i = 0;
    while (i < 2 && get_cabac_bypass(&s->HEVClc->cc))
        i++;
    return i;
}

int ff_hevc_rem_intra_luma_pred_mode_decode(HEVCContext *s)
{
    int i;
    int value = get_cabac_bypass(&s->HEVClc->cc);

    for (i = 0; i < 4; i++)
        value = (value << 1) | get_cabac_bypass(&s->HEVClc->cc);
    return value;
}

int ff_hevc_intra_chroma_pred_mode_decode(HEVCContext *s)
{
    int ret;
    if (!GET_CABAC(elem_offset[INTRA_CHROMA_PRED_MODE]))
        return 4;

    ret  = get_cabac_bypass(&s->HEVClc->cc) << 1;
    ret |= get_cabac_bypass(&s->HEVClc->cc);
    return ret;
}

int ff_hevc_merge_idx_decode(HEVCContext *s)
{
    int i = GET_CABAC(elem_offset[MERGE_IDX]);

    if (i != 0) {
        while (i < s->sh.max_num_merge_cand-1 && get_cabac_bypass(&s->HEVClc->cc))
            i++;
    }
    return i;
}

int ff_hevc_merge_flag_decode(HEVCContext *s)
{
    return GET_CABAC(elem_offset[MERGE_FLAG]);
}

int ff_hevc_inter_pred_idc_decode(HEVCContext *s, int nPbW, int nPbH)
{
    if (nPbW + nPbH == 12)
        return GET_CABAC(elem_offset[INTER_PRED_IDC] + 4);
    if (GET_CABAC(elem_offset[INTER_PRED_IDC] + s->HEVClc->ct_depth))
        return PRED_BI;

    return GET_CABAC(elem_offset[INTER_PRED_IDC] + 4);
}

int ff_hevc_ref_idx_lx_decode(HEVCContext *s, int num_ref_idx_lx)
{
    int i = 0;
    int max = num_ref_idx_lx - 1;
    int max_ctx = FFMIN(max, 2);

    while (i < max_ctx && GET_CABAC(elem_offset[REF_IDX_L0] + i))
        i++;
    if (i == 2) {
        while (i < max && get_cabac_bypass(&s->HEVClc->cc))
            i++;
    }

    return i;
}

int ff_hevc_mvp_lx_flag_decode(HEVCContext *s)
{
    return GET_CABAC(elem_offset[MVP_LX_FLAG]);
}

int ff_hevc_no_residual_syntax_flag_decode(HEVCContext *s)
{
    return GET_CABAC(elem_offset[NO_RESIDUAL_DATA_FLAG]);
}

static av_always_inline int abs_mvd_greater0_flag_decode(HEVCContext *s)
{
    return GET_CABAC(elem_offset[ABS_MVD_GREATER0_FLAG]);
}

static av_always_inline int abs_mvd_greater1_flag_decode(HEVCContext *s)
{
    return GET_CABAC(elem_offset[ABS_MVD_GREATER1_FLAG] + 1);
}

static av_always_inline int mvd_decode(HEVCContext *s)
{
    int ret = 2;
    int k = 1;

    while (k < CABAC_MAX_BIN && get_cabac_bypass(&s->HEVClc->cc)) {
        ret += 1U << k;
        k++;
    }
    if (k == CABAC_MAX_BIN) {
        av_log(s->avctx, AV_LOG_ERROR, "CABAC_MAX_BIN : %d\n", k);
        return 0;
    }
    while (k--)
        ret += get_cabac_bypass(&s->HEVClc->cc) << k;
    return get_cabac_bypass_sign(&s->HEVClc->cc, -ret);
}

static av_always_inline int mvd_sign_flag_decode(HEVCContext *s)
{
    return get_cabac_bypass_sign(&s->HEVClc->cc, -1);
}

int ff_hevc_split_transform_flag_decode(HEVCContext *s, int log2_trafo_size)
{
    return GET_CABAC(elem_offset[SPLIT_TRANSFORM_FLAG] + 5 - log2_trafo_size);
}

int ff_hevc_cbf_cb_cr_decode(HEVCContext *s, int trafo_depth)
{
    return GET_CABAC(elem_offset[CBF_CB_CR] + trafo_depth);
}

int ff_hevc_cbf_luma_decode(HEVCContext *s, int trafo_depth)
{
    return GET_CABAC(elem_offset[CBF_LUMA] + !trafo_depth);
}

static int hevc_transform_skip_flag_decode(HEVCContext *s, int c_idx)
{
    return GET_CABAC(elem_offset[TRANSFORM_SKIP_FLAG] + !!c_idx);
}

static int explicit_rdpcm_flag_decode(HEVCContext *s, int c_idx)
{
    return GET_CABAC(elem_offset[EXPLICIT_RDPCM_FLAG] + !!c_idx);
}

static int explicit_rdpcm_dir_flag_decode(HEVCContext *s, int c_idx)
{
    return GET_CABAC(elem_offset[EXPLICIT_RDPCM_DIR_FLAG] + !!c_idx);
}

int ff_hevc_log2_res_scale_abs(HEVCContext *s, int idx) {
    int i =0;

    while (i < 4 && GET_CABAC(elem_offset[LOG2_RES_SCALE_ABS] + 4 * idx + i))
        i++;

    return i;
}

int ff_hevc_res_scale_sign_flag(HEVCContext *s, int idx) {
    return GET_CABAC(elem_offset[RES_SCALE_SIGN_FLAG] + idx);
}

static av_always_inline void last_significant_coeff_xy_prefix_decode(HEVCContext *s, int c_idx,
                                                   int log2_size, int *last_scx_prefix, int *last_scy_prefix)
{
    int i = 0;
    int max = (log2_size << 1) - 1;
    int ctx_offset, ctx_shift;

    if (!c_idx) {
        ctx_offset = 3 * (log2_size - 2)  + ((log2_size - 1) >> 2);
        ctx_shift = (log2_size + 1) >> 2;
    } else {
        ctx_offset = 15;
        ctx_shift = log2_size - 2;
    }
    while (i < max &&
           GET_CABAC(elem_offset[LAST_SIGNIFICANT_COEFF_X_PREFIX] + (i >> ctx_shift) + ctx_offset))
        i++;
    *last_scx_prefix = i;

    i = 0;
    while (i < max &&
           GET_CABAC(elem_offset[LAST_SIGNIFICANT_COEFF_Y_PREFIX] + (i >> ctx_shift) + ctx_offset))
        i++;
    *last_scy_prefix = i;
}

static av_always_inline int last_significant_coeff_suffix_decode(HEVCContext *s,
                                                 int last_significant_coeff_prefix)
{
    int i;
    int length = (last_significant_coeff_prefix >> 1) - 1;
    int value = get_cabac_bypass(&s->HEVClc->cc);

    for (i = 1; i < length; i++)
        value = (value << 1) | get_cabac_bypass(&s->HEVClc->cc);
    return value;
}

static av_always_inline int significant_coeff_group_flag_decode(HEVCContext *s, int c_idx, int ctx_cg)
{
    int inc;

    inc = FFMIN(ctx_cg, 1) + (c_idx>0 ? 2 : 0);

    return GET_CABAC(elem_offset[SIGNIFICANT_COEFF_GROUP_FLAG] + inc);
}
static av_always_inline int significant_coeff_flag_decode(HEVCContext *s, int x_c, int y_c,
                                           int offset, const uint8_t *ctx_idx_map)
{
    int inc = ctx_idx_map[(y_c << 2) + x_c] + offset;
    return GET_CABAC(elem_offset[SIGNIFICANT_COEFF_FLAG] + inc);
}

static av_always_inline int significant_coeff_flag_decode_0(HEVCContext *s, int c_idx, int offset)
{
    return GET_CABAC(elem_offset[SIGNIFICANT_COEFF_FLAG] + offset);
}

static av_always_inline int coeff_abs_level_greater1_flag_decode(HEVCContext *s, int c_idx, int inc)
{

    if (c_idx > 0)
        inc += 16;

    return GET_CABAC(elem_offset[COEFF_ABS_LEVEL_GREATER1_FLAG] + inc);
}

static av_always_inline int coeff_abs_level_greater2_flag_decode(HEVCContext *s, int c_idx, int inc)
{
    if (c_idx > 0)
        inc += 4;

    return GET_CABAC(elem_offset[COEFF_ABS_LEVEL_GREATER2_FLAG] + inc);
}

static av_always_inline int coeff_abs_level_remaining_decode(HEVCContext *s, int rc_rice_param)
{
    int prefix = 0;
    int suffix = 0;
    int last_coeff_abs_level_remaining;
    int i;

    while (prefix < CABAC_MAX_BIN && get_cabac_bypass(&s->HEVClc->cc))
        prefix++;

    if (prefix < 3) {
        for (i = 0; i < rc_rice_param; i++)
            suffix = (suffix << 1) | get_cabac_bypass(&s->HEVClc->cc);
        last_coeff_abs_level_remaining = (prefix << rc_rice_param) + suffix;
    } else {
        int prefix_minus3 = prefix - 3;

        if (prefix == CABAC_MAX_BIN || prefix_minus3 + rc_rice_param >= 31) {
            av_log(s->avctx, AV_LOG_ERROR, "CABAC_MAX_BIN : %d\n", prefix);
            return 0;
        }

        for (i = 0; i < prefix_minus3 + rc_rice_param; i++)
            suffix = (suffix << 1) | get_cabac_bypass(&s->HEVClc->cc);
        last_coeff_abs_level_remaining = (((1 << prefix_minus3) + 3 - 1)
                                              << rc_rice_param) + suffix;
    }
    return last_coeff_abs_level_remaining;
}

static av_always_inline int coeff_sign_flag_decode(HEVCContext *s, uint8_t nb)
{
    int i;
    int ret = 0;

    for (i = 0; i < nb; i++)
        ret = (ret << 1) | get_cabac_bypass(&s->HEVClc->cc);
    return ret;
}

void ff_hevc_hls_residual_coding(HEVCContext *s, int x0, int y0,
                                int log2_trafo_size, enum ScanType scan_idx,
                                int c_idx)
{
#define GET_COORD(offset, n)                                    \
    do {                                                        \
        x_c = (x_cg << 2) + scan_x_off[n];                      \
        y_c = (y_cg << 2) + scan_y_off[n];                      \
    } while (0)
    HEVCLocalContext *lc = s->HEVClc;
    int transform_skip_flag = 0;

    int last_significant_coeff_x, last_significant_coeff_y;
    int last_scan_pos;
    int n_end;
    int num_coeff = 0;
    int greater1_ctx = 1;

    int num_last_subset;
    int x_cg_last_sig, y_cg_last_sig;

    const uint8_t *scan_x_cg, *scan_y_cg, *scan_x_off, *scan_y_off;

    ptrdiff_t stride = s->frame->linesize[c_idx];
    int hshift = s->ps.sps->hshift[c_idx];
    int vshift = s->ps.sps->vshift[c_idx];
    uint8_t *dst = &s->frame->data[c_idx][(y0 >> vshift) * stride +
                                          ((x0 >> hshift) << s->ps.sps->pixel_shift)];
    int16_t *coeffs = (int16_t*)(c_idx ? lc->edge_emu_buffer2 : lc->edge_emu_buffer);
    uint8_t significant_coeff_group_flag[8][8] = {{0}};
    int explicit_rdpcm_flag = 0;
    int explicit_rdpcm_dir_flag;

    int trafo_size = 1 << log2_trafo_size;
    int i;
    int qp,shift,add,scale,scale_m;
    static const uint8_t level_scale[] = { 40, 45, 51, 57, 64, 72 };
    const uint8_t *scale_matrix = NULL;
    uint8_t dc_scale;
    int pred_mode_intra = (c_idx == 0) ? lc->tu.intra_pred_mode :
                                         lc->tu.intra_pred_mode_c;

    memset(coeffs, 0, trafo_size * trafo_size * sizeof(int16_t));

    // Derive QP for dequant
    if (!lc->cu.cu_transquant_bypass_flag) {
        static const int qp_c[] = { 29, 30, 31, 32, 33, 33, 34, 34, 35, 35, 36, 36, 37, 37 };
        static const uint8_t rem6[51 + 4 * 6 + 1] = {
            0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, 0, 1, 2,
            3, 4, 5, 0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5,
            0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, 0, 1, 2, 3,
            4, 5, 0, 1, 2, 3, 4, 5, 0, 1
        };

        static const uint8_t div6[51 + 4 * 6 + 1] = {
            0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3,  3,  3,
            3, 3, 3, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 6, 6, 6, 6,  6,  6,
            7, 7, 7, 7, 7, 7, 8, 8, 8, 8, 8, 8, 9, 9, 9, 9, 9, 9, 10, 10, 10, 10,
            10, 10, 11, 11, 11, 11, 11, 11, 12, 12
        };
        int qp_y = lc->qp_y;

        if (s->ps.pps->transform_skip_enabled_flag &&
            log2_trafo_size <= s->ps.pps->log2_max_transform_skip_block_size) {
            transform_skip_flag = hevc_transform_skip_flag_decode(s, c_idx);
        }

        if (c_idx == 0) {
            qp = qp_y + s->ps.sps->qp_bd_offset;
        } else {
            int qp_i, offset;

            if (c_idx == 1)
                offset = s->ps.pps->cb_qp_offset + s->sh.slice_cb_qp_offset +
                         lc->tu.cu_qp_offset_cb;
            else
                offset = s->ps.pps->cr_qp_offset + s->sh.slice_cr_qp_offset +
                         lc->tu.cu_qp_offset_cr;

            qp_i = av_clip(qp_y + offset, - s->ps.sps->qp_bd_offset, 57);
            if (s->ps.sps->chroma_format_idc == 1) {
                if (qp_i < 30)
                    qp = qp_i;
                else if (qp_i > 43)
                    qp = qp_i - 6;
                else
                    qp = qp_c[qp_i - 30];
            } else {
                if (qp_i > 51)
                    qp = 51;
                else
                    qp = qp_i;
            }

            qp += s->ps.sps->qp_bd_offset;
        }

        shift    = s->ps.sps->bit_depth + log2_trafo_size - 5;
        add      = 1 << (shift-1);
        scale    = level_scale[rem6[qp]] << (div6[qp]);
        scale_m  = 16; // default when no custom scaling lists.
        dc_scale = 16;

        if (s->ps.sps->scaling_list_enable_flag && !(transform_skip_flag && log2_trafo_size > 2)) {
            const ScalingList *sl = s->ps.pps->scaling_list_data_present_flag ?
            &s->ps.pps->scaling_list : &s->ps.sps->scaling_list;
            int matrix_id = lc->cu.pred_mode != MODE_INTRA;

            matrix_id = 3 * matrix_id + c_idx;

            scale_matrix = sl->sl[log2_trafo_size - 2][matrix_id];
            if (log2_trafo_size >= 4)
                dc_scale = sl->sl_dc[log2_trafo_size - 4][matrix_id];
        }
    } else {
        shift        = 0;
        add          = 0;
        scale        = 0;
        dc_scale     = 0;
    }

    if (lc->cu.pred_mode == MODE_INTER && s->ps.sps->explicit_rdpcm_enabled_flag &&
        (transform_skip_flag || lc->cu.cu_transquant_bypass_flag)) {
        explicit_rdpcm_flag = explicit_rdpcm_flag_decode(s, c_idx);
        if (explicit_rdpcm_flag) {
            explicit_rdpcm_dir_flag = explicit_rdpcm_dir_flag_decode(s, c_idx);
        }
    }

    last_significant_coeff_xy_prefix_decode(s, c_idx, log2_trafo_size,
                                           &last_significant_coeff_x, &last_significant_coeff_y);

    if (last_significant_coeff_x > 3) {
        int suffix = last_significant_coeff_suffix_decode(s, last_significant_coeff_x);
        last_significant_coeff_x = (1 << ((last_significant_coeff_x >> 1) - 1)) *
        (2 + (last_significant_coeff_x & 1)) +
        suffix;
    }

    if (last_significant_coeff_y > 3) {
        int suffix = last_significant_coeff_suffix_decode(s, last_significant_coeff_y);
        last_significant_coeff_y = (1 << ((last_significant_coeff_y >> 1) - 1)) *
        (2 + (last_significant_coeff_y & 1)) +
        suffix;
    }

    if (scan_idx == SCAN_VERT)
        FFSWAP(int, last_significant_coeff_x, last_significant_coeff_y);

    x_cg_last_sig = last_significant_coeff_x >> 2;
    y_cg_last_sig = last_significant_coeff_y >> 2;

    switch (scan_idx) {
    case SCAN_DIAG: {
        int last_x_c = last_significant_coeff_x & 3;
        int last_y_c = last_significant_coeff_y & 3;

        scan_x_off = ff_hevc_diag_scan4x4_x;
        scan_y_off = ff_hevc_diag_scan4x4_y;
        num_coeff = diag_scan4x4_inv[last_y_c][last_x_c];
        if (trafo_size == 4) {
            scan_x_cg = scan_1x1;
            scan_y_cg = scan_1x1;
        } else if (trafo_size == 8) {
            num_coeff += diag_scan2x2_inv[y_cg_last_sig][x_cg_last_sig] << 4;
            scan_x_cg = diag_scan2x2_x;
            scan_y_cg = diag_scan2x2_y;
        } else if (trafo_size == 16) {
            num_coeff += diag_scan4x4_inv[y_cg_last_sig][x_cg_last_sig] << 4;
            scan_x_cg = ff_hevc_diag_scan4x4_x;
            scan_y_cg = ff_hevc_diag_scan4x4_y;
        } else { // trafo_size == 32
            num_coeff += diag_scan8x8_inv[y_cg_last_sig][x_cg_last_sig] << 4;
            scan_x_cg = ff_hevc_diag_scan8x8_x;
            scan_y_cg = ff_hevc_diag_scan8x8_y;
        }
        break;
    }
    case SCAN_HORIZ:
        scan_x_cg = horiz_scan2x2_x;
        scan_y_cg = horiz_scan2x2_y;
        scan_x_off = horiz_scan4x4_x;
        scan_y_off = horiz_scan4x4_y;
        num_coeff = horiz_scan8x8_inv[last_significant_coeff_y][last_significant_coeff_x];
        break;
    default: //SCAN_VERT
        scan_x_cg = horiz_scan2x2_y;
        scan_y_cg = horiz_scan2x2_x;
        scan_x_off = horiz_scan4x4_y;
        scan_y_off = horiz_scan4x4_x;
        num_coeff = horiz_scan8x8_inv[last_significant_coeff_x][last_significant_coeff_y];
        break;
    }
    num_coeff++;
    num_last_subset = (num_coeff - 1) >> 4;

    for (i = num_last_subset; i >= 0; i--) {
        int n, m;
        int x_cg, y_cg, x_c, y_c, pos;
        int implicit_non_zero_coeff = 0;
        int64_t trans_coeff_level;
        int prev_sig = 0;
        int offset = i << 4;
        int rice_init = 0;

        uint8_t significant_coeff_flag_idx[16];
        uint8_t nb_significant_coeff_flag = 0;

        x_cg = scan_x_cg[i];
        y_cg = scan_y_cg[i];

        if ((i < num_last_subset) && (i > 0)) {
            int ctx_cg = 0;
            if (x_cg < (1 << (log2_trafo_size - 2)) - 1)
                ctx_cg += significant_coeff_group_flag[x_cg + 1][y_cg];
            if (y_cg < (1 << (log2_trafo_size - 2)) - 1)
                ctx_cg += significant_coeff_group_flag[x_cg][y_cg + 1];

            significant_coeff_group_flag[x_cg][y_cg] =
                significant_coeff_group_flag_decode(s, c_idx, ctx_cg);
            implicit_non_zero_coeff = 1;
        } else {
            significant_coeff_group_flag[x_cg][y_cg] =
            ((x_cg == x_cg_last_sig && y_cg == y_cg_last_sig) ||
             (x_cg == 0 && y_cg == 0));
        }

        last_scan_pos = num_coeff - offset - 1;

        if (i == num_last_subset) {
            n_end = last_scan_pos - 1;
            significant_coeff_flag_idx[0] = last_scan_pos;
            nb_significant_coeff_flag = 1;
        } else {
            n_end = 15;
        }

        if (x_cg < ((1 << log2_trafo_size) - 1) >> 2)
            prev_sig = !!significant_coeff_group_flag[x_cg + 1][y_cg];
        if (y_cg < ((1 << log2_trafo_size) - 1) >> 2)
            prev_sig += (!!significant_coeff_group_flag[x_cg][y_cg + 1] << 1);

        if (significant_coeff_group_flag[x_cg][y_cg] && n_end >= 0) {
            static const uint8_t ctx_idx_map[] = {
                0, 1, 4, 5, 2, 3, 4, 5, 6, 6, 8, 8, 7, 7, 8, 8, // log2_trafo_size == 2
                1, 1, 1, 0, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, // prev_sig == 0
                2, 2, 2, 2, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, // prev_sig == 1
                2, 1, 0, 0, 2, 1, 0, 0, 2, 1, 0, 0, 2, 1, 0, 0, // prev_sig == 2
                2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2  // default
            };
            const uint8_t *ctx_idx_map_p;
            int scf_offset = 0;
            if (s->ps.sps->transform_skip_context_enabled_flag &&
                (transform_skip_flag || lc->cu.cu_transquant_bypass_flag)) {
                ctx_idx_map_p = (uint8_t*) &ctx_idx_map[4 * 16];
                if (c_idx == 0) {
                    scf_offset = 40;
                } else {
                    scf_offset = 14 + 27;
                }
            } else {
                if (c_idx != 0)
                    scf_offset = 27;
                if (log2_trafo_size == 2) {
                    ctx_idx_map_p = (uint8_t*) &ctx_idx_map[0];
                } else {
                    ctx_idx_map_p = (uint8_t*) &ctx_idx_map[(prev_sig + 1) << 4];
                    if (c_idx == 0) {
                        if ((x_cg > 0 || y_cg > 0))
                            scf_offset += 3;
                        if (log2_trafo_size == 3) {
                            scf_offset += (scan_idx == SCAN_DIAG) ? 9 : 15;
                        } else {
                            scf_offset += 21;
                        }
                    } else {
                        if (log2_trafo_size == 3)
                            scf_offset += 9;
                        else
                            scf_offset += 12;
                    }
                }
            }
            for (n = n_end; n > 0; n--) {
                x_c = scan_x_off[n];
                y_c = scan_y_off[n];
                if (significant_coeff_flag_decode(s, x_c, y_c, scf_offset, ctx_idx_map_p)) {
                    significant_coeff_flag_idx[nb_significant_coeff_flag] = n;
                    nb_significant_coeff_flag++;
                    implicit_non_zero_coeff = 0;
                }
            }
            if (implicit_non_zero_coeff == 0) {
                if (s->ps.sps->transform_skip_context_enabled_flag &&
                    (transform_skip_flag || lc->cu.cu_transquant_bypass_flag)) {
                    if (c_idx == 0) {
                        scf_offset = 42;
                    } else {
                        scf_offset = 16 + 27;
                    }
                } else {
                    if (i == 0) {
                        if (c_idx == 0)
                            scf_offset = 0;
                        else
                            scf_offset = 27;
                    } else {
                        scf_offset = 2 + scf_offset;
                    }
                }
                if (significant_coeff_flag_decode_0(s, c_idx, scf_offset) == 1) {
                    significant_coeff_flag_idx[nb_significant_coeff_flag] = 0;
                    nb_significant_coeff_flag++;
                }
            } else {
                significant_coeff_flag_idx[nb_significant_coeff_flag] = 0;
                nb_significant_coeff_flag++;
            }
        }

        n_end = nb_significant_coeff_flag;


        if (n_end) {
            int first_nz_pos_in_cg;
            int last_nz_pos_in_cg;
            int c_rice_param = 0;
            int first_greater1_coeff_idx = -1;
            uint8_t coeff_abs_level_greater1_flag[8];
            uint16_t coeff_sign_flag;
            int sum_abs = 0;
            int sign_hidden;
            int sb_type;


            // initialize first elem of coeff_bas_level_greater1_flag
            int ctx_set = (i > 0 && c_idx == 0) ? 2 : 0;

            if (s->ps.sps->persistent_rice_adaptation_enabled_flag) {
                if (!transform_skip_flag && !lc->cu.cu_transquant_bypass_flag)
                    sb_type = 2 * (c_idx == 0 ? 1 : 0);
                else
                    sb_type = 2 * (c_idx == 0 ? 1 : 0) + 1;
                c_rice_param = lc->stat_coeff[sb_type] / 4;
            }

            if (!(i == num_last_subset) && greater1_ctx == 0)
                ctx_set++;
            greater1_ctx = 1;
            last_nz_pos_in_cg = significant_coeff_flag_idx[0];

            for (m = 0; m < (n_end > 8 ? 8 : n_end); m++) {
                int inc = (ctx_set << 2) + greater1_ctx;
                coeff_abs_level_greater1_flag[m] =
                    coeff_abs_level_greater1_flag_decode(s, c_idx, inc);
                if (coeff_abs_level_greater1_flag[m]) {
                    greater1_ctx = 0;
                    if (first_greater1_coeff_idx == -1)
                        first_greater1_coeff_idx = m;
                } else if (greater1_ctx > 0 && greater1_ctx < 3) {
                    greater1_ctx++;
                }
            }
            first_nz_pos_in_cg = significant_coeff_flag_idx[n_end - 1];

            if (lc->cu.cu_transquant_bypass_flag ||
                (lc->cu.pred_mode ==  MODE_INTRA  &&
                 s->ps.sps->implicit_rdpcm_enabled_flag  &&  transform_skip_flag  &&
                 (pred_mode_intra == 10 || pred_mode_intra  ==  26 )) ||
                 explicit_rdpcm_flag)
                sign_hidden = 0;
            else
                sign_hidden = (last_nz_pos_in_cg - first_nz_pos_in_cg >= 4);

            if (first_greater1_coeff_idx != -1) {
                coeff_abs_level_greater1_flag[first_greater1_coeff_idx] += coeff_abs_level_greater2_flag_decode(s, c_idx, ctx_set);
            }
            if (!s->ps.pps->sign_data_hiding_flag || !sign_hidden ) {
                coeff_sign_flag = coeff_sign_flag_decode(s, nb_significant_coeff_flag) << (16 - nb_significant_coeff_flag);
            } else {
                coeff_sign_flag = coeff_sign_flag_decode(s, nb_significant_coeff_flag - 1) << (16 - (nb_significant_coeff_flag - 1));
            }

            for (m = 0; m < n_end; m++) {
                n = significant_coeff_flag_idx[m];
                GET_COORD(offset, n);
                if (m < 8) {
                    trans_coeff_level = 1 + coeff_abs_level_greater1_flag[m];
                    if (trans_coeff_level == ((m == first_greater1_coeff_idx) ? 3 : 2)) {
                        int last_coeff_abs_level_remaining = coeff_abs_level_remaining_decode(s, c_rice_param);

                        trans_coeff_level += last_coeff_abs_level_remaining;
                        if (trans_coeff_level > (3 << c_rice_param))
                            c_rice_param = s->ps.sps->persistent_rice_adaptation_enabled_flag ? c_rice_param + 1 : FFMIN(c_rice_param + 1, 4);
                        if (s->ps.sps->persistent_rice_adaptation_enabled_flag && !rice_init) {
                            int c_rice_p_init = lc->stat_coeff[sb_type] / 4;
                            if (last_coeff_abs_level_remaining >= (3 << c_rice_p_init))
                                lc->stat_coeff[sb_type]++;
                            else if (2 * last_coeff_abs_level_remaining < (1 << c_rice_p_init))
                                if (lc->stat_coeff[sb_type] > 0)
                                    lc->stat_coeff[sb_type]--;
                            rice_init = 1;
                        }
                    }
                } else {
                    int last_coeff_abs_level_remaining = coeff_abs_level_remaining_decode(s, c_rice_param);

                    trans_coeff_level = 1 + last_coeff_abs_level_remaining;
                    if (trans_coeff_level > (3 << c_rice_param))
                        c_rice_param = s->ps.sps->persistent_rice_adaptation_enabled_flag ? c_rice_param + 1 : FFMIN(c_rice_param + 1, 4);
                    if (s->ps.sps->persistent_rice_adaptation_enabled_flag && !rice_init) {
                        int c_rice_p_init = lc->stat_coeff[sb_type] / 4;
                        if (last_coeff_abs_level_remaining >= (3 << c_rice_p_init))
                            lc->stat_coeff[sb_type]++;
                        else if (2 * last_coeff_abs_level_remaining < (1 << c_rice_p_init))
                            if (lc->stat_coeff[sb_type] > 0)
                                lc->stat_coeff[sb_type]--;
                        rice_init = 1;
                    }
                }
                if (s->ps.pps->sign_data_hiding_flag && sign_hidden) {
                    sum_abs += trans_coeff_level;
                    if (n == first_nz_pos_in_cg && (sum_abs&1))
                        trans_coeff_level = -trans_coeff_level;
                }
                if (coeff_sign_flag >> 15)
                    trans_coeff_level = -trans_coeff_level;
                coeff_sign_flag <<= 1;
                if(!lc->cu.cu_transquant_bypass_flag) {
                    if (s->ps.sps->scaling_list_enable_flag && !(transform_skip_flag && log2_trafo_size > 2)) {
                        if(y_c || x_c || log2_trafo_size < 4) {
                            switch(log2_trafo_size) {
                                case 3: pos = (y_c << 3) + x_c; break;
                                case 4: pos = ((y_c >> 1) << 3) + (x_c >> 1); break;
                                case 5: pos = ((y_c >> 2) << 3) + (x_c >> 2); break;
                                default: pos = (y_c << 2) + x_c; break;
                            }
                            scale_m = scale_matrix[pos];
                        } else {
                            scale_m = dc_scale;
                        }
                    }
                    trans_coeff_level = (trans_coeff_level * (int64_t)scale * (int64_t)scale_m + add) >> shift;
                    if(trans_coeff_level < 0) {
                        if((~trans_coeff_level) & 0xFffffffffff8000)
                            trans_coeff_level = -32768;
                    } else {
                        if(trans_coeff_level & 0xffffffffffff8000)
                            trans_coeff_level = 32767;
                    }
                }
                coeffs[y_c * trafo_size + x_c] = trans_coeff_level;
            }
        }
    }

    if (lc->cu.cu_transquant_bypass_flag) {
        if (explicit_rdpcm_flag || (s->ps.sps->implicit_rdpcm_enabled_flag &&
                                    (pred_mode_intra == 10 || pred_mode_intra == 26))) {
            int mode = s->ps.sps->implicit_rdpcm_enabled_flag ? (pred_mode_intra == 26) : explicit_rdpcm_dir_flag;

            s->hevcdsp.transform_rdpcm(coeffs, log2_trafo_size, mode);
        }
    } else {
        if (transform_skip_flag) {
            int rot = s->ps.sps->transform_skip_rotation_enabled_flag &&
                      log2_trafo_size == 2 &&
                      lc->cu.pred_mode == MODE_INTRA;
            if (rot) {
                for (i = 0; i < 8; i++)
                    FFSWAP(int16_t, coeffs[i], coeffs[16 - i - 1]);
            }

            s->hevcdsp.dequant(coeffs, log2_trafo_size);

            if (explicit_rdpcm_flag || (s->ps.sps->implicit_rdpcm_enabled_flag &&
                                        lc->cu.pred_mode == MODE_INTRA &&
                                        (pred_mode_intra == 10 || pred_mode_intra == 26))) {
                int mode = explicit_rdpcm_flag ? explicit_rdpcm_dir_flag : (pred_mode_intra == 26);

                s->hevcdsp.transform_rdpcm(coeffs, log2_trafo_size, mode);
            }
        } else if (lc->cu.pred_mode == MODE_INTRA && c_idx == 0 && log2_trafo_size == 2) {
            s->hevcdsp.transform_4x4_luma(coeffs);
        } else {
            int max_xy = FFMAX(last_significant_coeff_x, last_significant_coeff_y);
            if (max_xy == 0)
                s->hevcdsp.idct_dc[log2_trafo_size - 2](coeffs);
            else {
                int col_limit = last_significant_coeff_x + last_significant_coeff_y + 4;
                if (max_xy < 4)
                    col_limit = FFMIN(4, col_limit);
                else if (max_xy < 8)
                    col_limit = FFMIN(8, col_limit);
                else if (max_xy < 12)
                    col_limit = FFMIN(24, col_limit);
                s->hevcdsp.idct[log2_trafo_size - 2](coeffs, col_limit);
            }
        }
    }
    if (lc->tu.cross_pf) {
        int16_t *coeffs_y = (int16_t*)lc->edge_emu_buffer;

        for (i = 0; i < (trafo_size * trafo_size); i++) {
            coeffs[i] = coeffs[i] + ((lc->tu.res_scale_val * coeffs_y[i]) >> 3);
        }
    }
    s->hevcdsp.add_residual[log2_trafo_size-2](dst, coeffs, stride);
}

void ff_hevc_hls_mvd_coding(HEVCContext *s, int x0, int y0, int log2_cb_size)
{
    HEVCLocalContext *lc = s->HEVClc;
    int x = abs_mvd_greater0_flag_decode(s);
    int y = abs_mvd_greater0_flag_decode(s);

    if (x)
        x += abs_mvd_greater1_flag_decode(s);
    if (y)
        y += abs_mvd_greater1_flag_decode(s);

    switch (x) {
    case 2: lc->pu.mvd.x = mvd_decode(s);           break;
    case 1: lc->pu.mvd.x = mvd_sign_flag_decode(s); break;
    case 0: lc->pu.mvd.x = 0;                       break;
    }

    switch (y) {
    case 2: lc->pu.mvd.y = mvd_decode(s);           break;
    case 1: lc->pu.mvd.y = mvd_sign_flag_decode(s); break;
    case 0: lc->pu.mvd.y = 0;                       break;
    }
}

