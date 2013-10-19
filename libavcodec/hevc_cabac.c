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
#include "hevc.h"

#define CABAC_MAX_BIN 100

/**
 * number of bin by SyntaxElement.
 */
static const int8_t num_bins_in_se[] = {
     1,  // sao_merge_flag
     1,  // sao_type_idx
     0,  // sao_eo_class
     0,  // sao_band_position
     0,  // sao_offset_abs
     0,  // sao_offset_sign
     0,  // end_of_slice_flag
     3,  // split_coding_unit_flag
     1,  // cu_transquant_bypass_flag
     3,  // skip_flag
     3,  // cu_qp_delta
     1,  // pred_mode
     4,  // part_mode
     0,  // pcm_flag
     1,  // prev_intra_luma_pred_mode
     0,  // mpm_idx
     0,  // rem_intra_luma_pred_mode
     2,  // intra_chroma_pred_mode
     1,  // merge_flag
     1,  // merge_idx
     5,  // inter_pred_idc
     2,  // ref_idx_l0
     2,  // ref_idx_l1
     2,  // abs_mvd_greater0_flag
     2,  // abs_mvd_greater1_flag
     0,  // abs_mvd_minus2
     0,  // mvd_sign_flag
     1,  // mvp_lx_flag
     1,  // no_residual_data_flag
     3,  // split_transform_flag
     2,  // cbf_luma
     4,  // cbf_cb, cbf_cr
     2,  // transform_skip_flag[][]
    18,  // last_significant_coeff_x_prefix
    18,  // last_significant_coeff_y_prefix
     0,  // last_significant_coeff_x_suffix
     0,  // last_significant_coeff_y_suffix
     4,  // significant_coeff_group_flag
    42,  // significant_coeff_flag
    24,  // coeff_abs_level_greater1_flag
     6,  // coeff_abs_level_greater2_flag
     0,  // coeff_abs_level_remaining
     0,  // coeff_sign_flag
};

/**
 * Offset to ctxIdx 0 in init_values and states, indexed by SyntaxElement.
 */
static const int elem_offset[sizeof(num_bins_in_se)] = {
    0,
    1,
    2,
    2,
    2,
    2,
    2,
    2,
    5,
    6,
    9,
    12,
    13,
    17,
    17,
    18,
    18,
    18,
    20,
    21,
    22,
    27,
    29,
    31,
    33,
    35,
    35,
    35,
    36,
    37,
    40,
    42,
    46,
    48,
    66,
    84,
    84,
    84,
    88,
    130,
    154,
    160,
    160,
};

#define CNU 154
/**
 * Indexed by init_type
 */
static const uint8_t init_values[3][HEVC_CONTEXTS] = {
    {
        // sao_merge_flag
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
        94, 138, 182, 154,
        // transform_skip_flag
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
        // coeff_abs_level_greater1_flag
        140,  92, 137, 138, 140, 152, 138, 139, 153,  74, 149,  92, 139, 107,
        122, 152, 140, 179, 166, 182, 140, 227, 122, 197,
        // coeff_abs_level_greater2_flag
        138, 153, 136, 167, 152, 152,
    },
    {
        // sao_merge_flag
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
        149, 107, 167, 154,
        // transform_skip_flag
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
        // coeff_abs_level_greater1_flag
        154, 196, 196, 167, 154, 152, 167, 182, 182, 134, 149, 136, 153, 121,
        136, 137, 169, 194, 166, 167, 154, 167, 137, 182,
        // coeff_abs_level_greater2_flag
        107, 167, 91, 122, 107, 167,
    },
    {
        // sao_merge_flag
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
        149, 92, 167, 154,
        // transform_skip_flag
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
        // coeff_abs_level_greater1_flag
        154, 196, 167, 167, 154, 152, 167, 182, 182, 134, 149, 136, 153, 121,
        136, 122, 169, 208, 166, 167, 154, 152, 167, 182,
        // coeff_abs_level_greater2_flag
        107, 167, 91, 107, 107, 167,
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

static const uint8_t diag_scan4x1_x[4] = {
    0, 1, 2, 3,
};

static const uint8_t diag_scan1x4_y[4] = {
    0, 1, 2, 3,
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

static const uint8_t diag_scan8x2_x[16] = {
    0, 0, 1, 1,
    2, 2, 3, 3,
    4, 4, 5, 5,
    6, 6, 7, 7,
};

static const uint8_t diag_scan8x2_y[16] = {
    0, 1, 0, 1,
    0, 1, 0, 1,
    0, 1, 0, 1,
    0, 1, 0, 1,
};

static const uint8_t diag_scan8x2_inv[2][8] = {
    { 0, 2, 4, 6, 8, 10, 12, 14, },
    { 1, 3, 5, 7, 9, 11, 13, 15, },
};

static const uint8_t diag_scan2x8_x[16] = {
    0, 0, 1, 0,
    1, 0, 1, 0,
    1, 0, 1, 0,
    1, 0, 1, 1,
};

static const uint8_t diag_scan2x8_y[16] = {
    0, 1, 0, 2,
    1, 3, 2, 4,
    3, 5, 4, 6,
    5, 7, 6, 7,
};

static const uint8_t diag_scan2x8_inv[8][2] = {
    {  0,  2, },
    {  1,  4, },
    {  3,  6, },
    {  5,  8, },
    {  7, 10, },
    {  9, 12, },
    { 11, 14, },
    { 13, 15, },
};

const uint8_t ff_hevc_diag_scan4x4_x[16] = {
    0, 0, 1, 0,
    1, 2, 0, 1,
    2, 3, 1, 2,
    3, 2, 3, 3,
};

const uint8_t ff_hevc_diag_scan4x4_y[16] = {
    0, 1, 0, 2,
    1, 0, 3, 2,
    1, 0, 3, 2,
    1, 3, 2, 3,
};

static const uint8_t diag_scan4x4_inv[4][4] = {
    { 0,  2,  5,  9, },
    { 1,  4,  8, 12, },
    { 3,  7, 11, 14, },
    { 6, 10, 13, 15, },
};

const uint8_t ff_hevc_diag_scan8x8_x[64] = {
    0, 0, 1, 0,
    1, 2, 0, 1,
    2, 3, 0, 1,
    2, 3, 4, 0,
    1, 2, 3, 4,
    5, 0, 1, 2,
    3, 4, 5, 6,
    0, 1, 2, 3,
    4, 5, 6, 7,
    1, 2, 3, 4,
    5, 6, 7, 2,
    3, 4, 5, 6,
    7, 3, 4, 5,
    6, 7, 4, 5,
    6, 7, 5, 6,
    7, 6, 7, 7,
};

const uint8_t ff_hevc_diag_scan8x8_y[64] = {
    0, 1, 0, 2,
    1, 0, 3, 2,
    1, 0, 4, 3,
    2, 1, 0, 5,
    4, 3, 2, 1,
    0, 6, 5, 4,
    3, 2, 1, 0,
    7, 6, 5, 4,
    3, 2, 1, 0,
    7, 6, 5, 4,
    3, 2, 1, 7,
    6, 5, 4, 3,
    2, 7, 6, 5,
    4, 3, 7, 6,
    5, 4, 7, 6,
    5, 7, 6, 7,
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
    if (s->pps->entropy_coding_sync_enabled_flag &&
        ((ctb_addr_ts % s->sps->ctb_width) == 2 ||
         (s->sps->ctb_width == 2 &&
          (ctb_addr_ts % s->sps->ctb_width) == 0))) {
        memcpy(s->cabac_state, s->HEVClc.cabac_state, HEVC_CONTEXTS);
    }
}

static void load_states(HEVCContext *s)
{
    memcpy(s->HEVClc.cabac_state, s->cabac_state, HEVC_CONTEXTS);
}

static void cabac_reinit(HEVCLocalContext *lc)
{
    skip_bytes(&lc->cc, 0);
}

static void cabac_init_decoder(HEVCContext *s)
{
    GetBitContext *gb = &s->HEVClc.gb;
    skip_bits(gb, 1);
    align_get_bits(gb);
    ff_init_cabac_decoder(&s->HEVClc.cc,
                          gb->buffer + get_bits_count(gb) / 8,
                          (get_bits_left(gb) + 7) / 8);
}

static void cabac_init_state(HEVCContext *s)
{
    int init_type = 2 - s->sh.slice_type;
    int i;

    if (s->sh.cabac_init_flag && s->sh.slice_type != I_SLICE)
        init_type ^= 3;

    for (i = 0; i < HEVC_CONTEXTS; i++) {
        int init_value = init_values[init_type][i];
        int m = (init_value >> 4) * 5 - 45;
        int n = ((init_value & 15) << 3) - 16;
        int pre = 2 * (((m * av_clip_c(s->sh.slice_qp, 0, 51)) >> 4) + n) - 127;

        pre ^= pre >> 31;
        if (pre > 124)
            pre = 124 + (pre & 1);
        s->HEVClc.cabac_state[i] =  pre;
    }
}

void ff_hevc_cabac_init(HEVCContext *s, int ctb_addr_ts)
{
    if (ctb_addr_ts == s->pps->ctb_addr_rs_to_ts[s->sh.slice_ctb_addr_rs]) {
        cabac_init_decoder(s);
        if ((s->sh.dependent_slice_segment_flag == 0) ||
            (s->pps->tiles_enabled_flag &&
             (s->pps->tile_id[ctb_addr_ts] != s->pps->tile_id[ctb_addr_ts - 1])))
            cabac_init_state(s);

        if (!s->sh.first_slice_in_pic_flag && s->pps->entropy_coding_sync_enabled_flag) {
            if ((ctb_addr_ts % s->sps->ctb_width) == 0) {
                if (s->sps->ctb_width == 1)
                    cabac_init_state(s);
                else if (s->sh.dependent_slice_segment_flag == 1)
                    load_states(s);
            }
        }
    } else {
        if (s->pps->tiles_enabled_flag &&
            (s->pps->tile_id[ctb_addr_ts] != s->pps->tile_id[ctb_addr_ts - 1])) {
            cabac_reinit(&s->HEVClc);
            cabac_init_state(s);
        }
        if (s->pps->entropy_coding_sync_enabled_flag) {
            if ((ctb_addr_ts % s->sps->ctb_width) == 0) {
                get_cabac_terminate(&s->HEVClc.cc);
                cabac_reinit(&s->HEVClc);

                if (s->sps->ctb_width == 1)
                    cabac_init_state(s);
                else
                    load_states(s);
            }
        }
    }
}

#define GET_CABAC(ctx) get_cabac(&s->HEVClc.cc, &s->HEVClc.cabac_state[ctx])

int ff_hevc_sao_merge_flag_decode(HEVCContext *s)
{
    return GET_CABAC(elem_offset[SAO_MERGE_FLAG]);
}

int ff_hevc_sao_type_idx_decode(HEVCContext *s)
{
    if (!GET_CABAC(elem_offset[SAO_TYPE_IDX]))
        return 0;

    if (!get_cabac_bypass(&s->HEVClc.cc))
        return SAO_BAND;
    return SAO_EDGE;
}

int ff_hevc_sao_band_position_decode(HEVCContext *s)
{
    int i;
    int value = get_cabac_bypass(&s->HEVClc.cc);

    for (i = 0; i < 4; i++)
        value = (value << 1) | get_cabac_bypass(&s->HEVClc.cc);
    return value;
}

int ff_hevc_sao_offset_abs_decode(HEVCContext *s)
{
    int i = 0;
    int length = (1 << (FFMIN(s->sps->bit_depth, 10) - 5)) - 1;

    while (i < length && get_cabac_bypass(&s->HEVClc.cc))
        i++;
    return i;
}

int ff_hevc_sao_offset_sign_decode(HEVCContext *s)
{
    return get_cabac_bypass(&s->HEVClc.cc);
}

int ff_hevc_sao_eo_class_decode(HEVCContext *s)
{
    int ret = (get_cabac_bypass(&s->HEVClc.cc) << 1);
    ret    |=  get_cabac_bypass(&s->HEVClc.cc);
    return ret;
}

int ff_hevc_end_of_slice_flag_decode(HEVCContext *s)
{
    return get_cabac_terminate(&s->HEVClc.cc);
}

int ff_hevc_cu_transquant_bypass_flag_decode(HEVCContext *s)
{
    return GET_CABAC(elem_offset[CU_TRANSQUANT_BYPASS_FLAG]);
}

int ff_hevc_skip_flag_decode(HEVCContext *s, int x0, int y0, int x_cb, int y_cb)
{
    int pic_width_in_ctb = s->sps->width >> s->sps->log2_min_coding_block_size;
    int inc = 0;
    int x0b = x0 & ((1 << s->sps->log2_ctb_size) - 1);
    int y0b = y0 & ((1 << s->sps->log2_ctb_size) - 1);

    if (s->HEVClc.ctb_left_flag || x0b)
        inc = SAMPLE_CTB(s->skip_flag, x_cb-1, y_cb);
    if (s->HEVClc.ctb_up_flag || y0b)
        inc += SAMPLE_CTB(s->skip_flag, x_cb, y_cb-1);

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
        while (k < CABAC_MAX_BIN && get_cabac_bypass(&s->HEVClc.cc)) {
            suffix_val += 1 << k;
            k++;
        }
        if (k == CABAC_MAX_BIN)
            av_log(s->avctx, AV_LOG_ERROR, "CABAC_MAX_BIN : %d\n", k);

        while (k--)
            suffix_val += get_cabac_bypass(&s->HEVClc.cc) << k;
    }
    return prefix_val + suffix_val;
}

int ff_hevc_cu_qp_delta_sign_flag(HEVCContext *s)
{
    return get_cabac_bypass(&s->HEVClc.cc);
}

int ff_hevc_pred_mode_decode(HEVCContext *s)
{
    return GET_CABAC(elem_offset[PRED_MODE_FLAG]);
}

int ff_hevc_split_coding_unit_flag_decode(HEVCContext *s, int ct_depth, int x0, int y0)
{
    int inc = 0, depth_left = 0, depth_top = 0;
    int x0b = x0 & ((1 << s->sps->log2_ctb_size) - 1);
    int y0b = y0 & ((1 << s->sps->log2_ctb_size) - 1);
    int x_cb = x0 >> s->sps->log2_min_coding_block_size;
    int y_cb = y0 >> s->sps->log2_min_coding_block_size;

    if (s->HEVClc.ctb_left_flag || x0b)
        depth_left = s->tab_ct_depth[(y_cb)*s->sps->min_cb_width + x_cb-1];
    if (s->HEVClc.ctb_up_flag || y0b)
        depth_top = s->tab_ct_depth[(y_cb-1)*s->sps->min_cb_width + x_cb];

    inc += (depth_left > ct_depth);
    inc += (depth_top > ct_depth);
    return GET_CABAC(elem_offset[SPLIT_CODING_UNIT_FLAG] + inc);
}

int ff_hevc_part_mode_decode(HEVCContext *s, int log2_cb_size)
{
    if (GET_CABAC(elem_offset[PART_MODE])) // 1
        return PART_2Nx2N;
    if (log2_cb_size == s->sps->log2_min_coding_block_size) {
        if (s->HEVClc.cu.pred_mode == MODE_INTRA) // 0
            return PART_NxN;
        if (GET_CABAC(elem_offset[PART_MODE] + 1)) // 01
            return PART_2NxN;
        if (log2_cb_size == 3) // 00
            return PART_Nx2N;
        if (GET_CABAC(elem_offset[PART_MODE] + 2)) // 001
            return PART_Nx2N;
        return PART_NxN; // 000
    }

    if (!s->sps->amp_enabled_flag) {
        if (GET_CABAC(elem_offset[PART_MODE] + 1)) // 01
            return PART_2NxN;
        return PART_Nx2N;
    }

    if (GET_CABAC(elem_offset[PART_MODE] + 1)) { // 01X, 01XX
        if (GET_CABAC(elem_offset[PART_MODE] + 3)) // 011
            return PART_2NxN;
        if (get_cabac_bypass(&s->HEVClc.cc)) // 0101
            return PART_2NxnD;
        return PART_2NxnU; // 0100
    }

    if (GET_CABAC(elem_offset[PART_MODE] + 3)) // 001
        return PART_Nx2N;
    if (get_cabac_bypass(&s->HEVClc.cc)) // 0001
        return PART_nRx2N;
    return  PART_nLx2N; // 0000
}

int ff_hevc_pcm_flag_decode(HEVCContext *s)
{
    return get_cabac_terminate(&s->HEVClc.cc);
}

int ff_hevc_prev_intra_luma_pred_flag_decode(HEVCContext *s)
{
    return GET_CABAC(elem_offset[PREV_INTRA_LUMA_PRED_FLAG]);
}

int ff_hevc_mpm_idx_decode(HEVCContext *s)
{
    int i = 0;
    while (i < 2 && get_cabac_bypass(&s->HEVClc.cc))
        i++;
    return i;
}

int ff_hevc_rem_intra_luma_pred_mode_decode(HEVCContext *s)
{
    int i;
    int value = get_cabac_bypass(&s->HEVClc.cc);

    for (i = 0; i < 4; i++)
        value = (value << 1) | get_cabac_bypass(&s->HEVClc.cc);
    return value;
}

int ff_hevc_intra_chroma_pred_mode_decode(HEVCContext *s)
{
    int ret;
    if (!GET_CABAC(elem_offset[INTRA_CHROMA_PRED_MODE]))
        return 4;

    ret  = (get_cabac_bypass(&s->HEVClc.cc) << 1);
    ret |=  get_cabac_bypass(&s->HEVClc.cc);
    return ret;
}

int ff_hevc_merge_idx_decode(HEVCContext *s)
{
    int i = GET_CABAC(elem_offset[MERGE_IDX]);

    if (i != 0) {
        while (i < s->sh.max_num_merge_cand-1 && get_cabac_bypass(&s->HEVClc.cc))
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
    if (GET_CABAC(elem_offset[INTER_PRED_IDC] + s->HEVClc.ct.depth))
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
        while (i < max && get_cabac_bypass(&s->HEVClc.cc))
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

    while (k < CABAC_MAX_BIN && get_cabac_bypass(&s->HEVClc.cc)) {
        ret += 1 << k;
        k++;
    }
    if (k == CABAC_MAX_BIN)
        av_log(s->avctx, AV_LOG_ERROR, "CABAC_MAX_BIN : %d\n", k);
    while (k--)
        ret += get_cabac_bypass(&s->HEVClc.cc) << k;
    return get_cabac_bypass_sign(&s->HEVClc.cc, -ret);
}

static av_always_inline int mvd_sign_flag_decode(HEVCContext *s)
{
    return get_cabac_bypass_sign(&s->HEVClc.cc, -1);
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

int ff_hevc_transform_skip_flag_decode(HEVCContext *s, int c_idx)
{
    return GET_CABAC(elem_offset[TRANSFORM_SKIP_FLAG] + !!c_idx);
}

#define LAST_SIG_COEFF(elem)                                                    \
    int i = 0;                                                                  \
    int max = (log2_size << 1) - 1;                                             \
    int ctx_offset, ctx_shift;                                                  \
                                                                                \
    if (c_idx == 0) {                                                           \
        ctx_offset = 3 * (log2_size - 2)  + ((log2_size - 1) >> 2);             \
        ctx_shift = (log2_size + 1) >> 2;                                       \
    } else {                                                                    \
        ctx_offset = 15;                                                        \
        ctx_shift = log2_size - 2;                                              \
    }                                                                           \
    while (i < max &&                                                           \
           GET_CABAC(elem_offset[elem] + (i >> ctx_shift) + ctx_offset))        \
        i++;                                                                    \
    return i;

static av_always_inline int last_significant_coeff_x_prefix_decode(HEVCContext *s, int c_idx,
                                                   int log2_size)
{
    LAST_SIG_COEFF(LAST_SIGNIFICANT_COEFF_X_PREFIX)
}

static av_always_inline int last_significant_coeff_y_prefix_decode(HEVCContext *s, int c_idx,
                                                   int log2_size)
{
    LAST_SIG_COEFF(LAST_SIGNIFICANT_COEFF_Y_PREFIX)
}

static av_always_inline int last_significant_coeff_suffix_decode(HEVCContext *s,
                                                 int last_significant_coeff_prefix)
{
    int i;
    int length = (last_significant_coeff_prefix >> 1) - 1;
    int value = get_cabac_bypass(&s->HEVClc.cc);

    for (i = 1; i < length; i++)
        value = (value << 1) | get_cabac_bypass(&s->HEVClc.cc);
    return value;
}

static av_always_inline int significant_coeff_group_flag_decode(HEVCContext *s, int c_idx, int ctx_cg)
{
    int inc;

    inc = FFMIN(ctx_cg, 1) + (c_idx>0 ? 2 : 0);

    return GET_CABAC(elem_offset[SIGNIFICANT_COEFF_GROUP_FLAG] + inc);
}

static av_always_inline int significant_coeff_flag_decode(HEVCContext *s, int c_idx, int x_c, int y_c,
                                          int log2_trafo_size, int scan_idx, int prev_sig)
{
    static const uint8_t ctx_idx_map[] = {
        0, 1, 4, 5, 2, 3, 4, 5, 6, 6, 8, 8, 7, 7, 8, 8
    };
    int x_cg = x_c >> 2;
    int y_cg = y_c >> 2;
    int sig_ctx;
    int inc;

    if (x_c + y_c == 0) {
        sig_ctx = 0;
    } else if (log2_trafo_size == 2) {
        sig_ctx = ctx_idx_map[(y_c << 2) + x_c];
    } else {
        switch (prev_sig) {
        case 0: {
                int x_off = x_c & 3;
                int y_off = y_c & 3;
                sig_ctx   = ((x_off + y_off) == 0) ? 2 : ((x_off + y_off) <= 2) ? 1 : 0;
            }
            break;
        case 1:
            sig_ctx = 2 - FFMIN(y_c & 3, 2);
            break;
        case 2:
            sig_ctx = 2 - FFMIN(x_c & 3, 2);
            break;
        default:
            sig_ctx = 2;
        }

        if (c_idx == 0 && (x_cg > 0 || y_cg > 0))
            sig_ctx += 3;

        if (log2_trafo_size == 3) {
            sig_ctx += (scan_idx == SCAN_DIAG) ? 9 : 15;
        } else {
            sig_ctx += c_idx ? 12 : 21;
        }
    }

    if (c_idx == 0) {
        inc = sig_ctx;
    } else {
        inc = sig_ctx + 27;
    }

    return GET_CABAC(elem_offset[SIGNIFICANT_COEFF_FLAG] + inc);
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

static av_always_inline int coeff_abs_level_remaining_decode(HEVCContext *s, int base_level, int rc_rice_param)
{
    int prefix = 0;
    int suffix = 0;
    int last_coeff_abs_level_remaining;
    int i;

    while (prefix < CABAC_MAX_BIN && get_cabac_bypass(&s->HEVClc.cc))
        prefix++;
    if (prefix == CABAC_MAX_BIN)
        av_log(s->avctx, AV_LOG_ERROR, "CABAC_MAX_BIN : %d\n", prefix);
    if (prefix < 3) {
        for (i = 0; i < rc_rice_param; i++)
            suffix = (suffix << 1) | get_cabac_bypass(&s->HEVClc.cc);
        last_coeff_abs_level_remaining = (prefix << rc_rice_param) + suffix;
    } else {
        int prefix_minus3 = prefix - 3;
        for (i = 0; i < prefix_minus3 + rc_rice_param; i++)
            suffix = (suffix << 1) | get_cabac_bypass(&s->HEVClc.cc);
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
        ret = (ret << 1) | get_cabac_bypass(&s->HEVClc.cc);
    return ret;
}

void ff_hevc_hls_residual_coding(HEVCContext *s, int x0, int y0,
                                int log2_trafo_size, enum ScanType scan_idx,
                                int c_idx)
{
#define GET_COORD(offset, n)                                    \
    do {                                                        \
        x_c = (scan_x_cg[offset >> 4] << 2) + scan_x_off[n];    \
        y_c = (scan_y_cg[offset >> 4] << 2) + scan_y_off[n];    \
    } while (0)
    HEVCLocalContext *lc = &s->HEVClc;
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
    int hshift = s->sps->hshift[c_idx];
    int vshift = s->sps->vshift[c_idx];
    uint8_t *dst = &s->frame->data[c_idx][(y0 >> vshift) * stride +
                                          ((x0 >> hshift) << s->sps->pixel_shift)];
    DECLARE_ALIGNED(16, int16_t, coeffs[MAX_TB_SIZE * MAX_TB_SIZE]) = {0};
    DECLARE_ALIGNED(8, uint8_t, significant_coeff_group_flag[8][8]) = {{0}};

    int trafo_size = 1 << log2_trafo_size;
    int i;
    int qp,shift,add,scale,scale_m;
    const uint8_t level_scale[] = { 40, 45, 51, 57, 64, 72 };
    const uint8_t *scale_matrix;
    uint8_t dc_scale;

    // Derive QP for dequant
    if (!lc->cu.cu_transquant_bypass_flag) {
        static const int qp_c[] = { 29, 30, 31, 32, 33, 33, 34, 34, 35, 35, 36, 36, 37, 37 };
        static const uint8_t rem6[51 + 2 * 6 + 1] = {
            0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, 0, 1, 2,
            3, 4, 5, 0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5,
            0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, 0, 1, 2, 3,
        };

        static const uint8_t div6[51 + 2 * 6 + 1] = {
            0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3,  3,  3,
            3, 3, 3, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 6, 6, 6, 6,  6,  6,
            7, 7, 7, 7, 7, 7, 8, 8, 8, 8, 8, 8, 9, 9, 9, 9, 9, 9, 10, 10, 10, 10,
        };
        int qp_y = lc->qp_y;

        if (c_idx == 0) {
            qp = qp_y + s->sps->qp_bd_offset;
        } else {
            int qp_i, offset;

            if (c_idx == 1)
                offset = s->pps->cb_qp_offset + s->sh.slice_cb_qp_offset;
            else
                offset = s->pps->cr_qp_offset + s->sh.slice_cr_qp_offset;

            qp_i = av_clip_c(qp_y + offset, - s->sps->qp_bd_offset, 57);
            if (qp_i < 30)
                qp = qp_i;
            else if (qp_i > 43)
                qp = qp_i - 6;
            else
                qp = qp_c[qp_i - 30];

            qp += s->sps->qp_bd_offset;
        }

        shift    = s->sps->bit_depth + log2_trafo_size - 5;
        add      = 1 << (shift-1);
        scale    = level_scale[rem6[qp]] << (div6[qp]);
        scale_m  = 16; // default when no custom scaling lists.
        dc_scale = 16;

        if (s->sps->scaling_list_enable_flag) {
            const ScalingList *sl = s->pps->pps_scaling_list_data_present_flag ?
            &s->pps->scaling_list : &s->sps->scaling_list;
            int matrix_id = lc->cu.pred_mode != MODE_INTRA;

            if (log2_trafo_size != 5)
                matrix_id = 3 * matrix_id + c_idx;

            scale_matrix = sl->sl[log2_trafo_size - 2][matrix_id];
            if (log2_trafo_size >= 4)
                dc_scale = sl->sl_dc[log2_trafo_size - 4][matrix_id];
        }
    }

    if (s->pps->transform_skip_enabled_flag && !lc->cu.cu_transquant_bypass_flag &&
        log2_trafo_size == 2) {
        transform_skip_flag = ff_hevc_transform_skip_flag_decode(s, c_idx);
    }

    last_significant_coeff_x =
        last_significant_coeff_x_prefix_decode(s, c_idx, log2_trafo_size);
    last_significant_coeff_y =
        last_significant_coeff_y_prefix_decode(s, c_idx, log2_trafo_size);

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
            prev_sig = significant_coeff_group_flag[x_cg + 1][y_cg];
        if (y_cg < ((1 << log2_trafo_size) - 1) >> 2)
            prev_sig += (significant_coeff_group_flag[x_cg][y_cg + 1] << 1);

        for (n = n_end; n >= 0; n--) {
            GET_COORD(offset, n);

            if (significant_coeff_group_flag[x_cg][y_cg] &&
                (n > 0 || implicit_non_zero_coeff == 0)) {
                if (significant_coeff_flag_decode(s, c_idx, x_c, y_c, log2_trafo_size, scan_idx, prev_sig) == 1) {
                    significant_coeff_flag_idx[nb_significant_coeff_flag] = n;
                    nb_significant_coeff_flag++;
                    implicit_non_zero_coeff = 0;
                }
            } else {
                int last_cg = (x_c == (x_cg << 2) && y_c == (y_cg << 2));
                if (last_cg && implicit_non_zero_coeff && significant_coeff_group_flag[x_cg][y_cg]) {
                    significant_coeff_flag_idx[nb_significant_coeff_flag] = n;
                    nb_significant_coeff_flag++;
                }
            }
        }

        n_end = nb_significant_coeff_flag;


        if (n_end) {
            int first_nz_pos_in_cg = 16;
            int last_nz_pos_in_cg = -1;
            int c_rice_param = 0;
            int first_greater1_coeff_idx = -1;
            uint8_t coeff_abs_level_greater1_flag[16] = {0};
            uint16_t coeff_sign_flag;
            int sum_abs = 0;
            int sign_hidden = 0;

            // initialize first elem of coeff_bas_level_greater1_flag
            int ctx_set = (i > 0 && c_idx == 0) ? 2 : 0;

            if (!(i == num_last_subset) && greater1_ctx == 0)
                ctx_set++;
            greater1_ctx = 1;
            last_nz_pos_in_cg = significant_coeff_flag_idx[0];

            for (m = 0; m < (n_end > 8 ? 8 : n_end); m++) {
                int n_idx = significant_coeff_flag_idx[m];
                int inc = (ctx_set << 2) + greater1_ctx;
                coeff_abs_level_greater1_flag[n_idx] =
                    coeff_abs_level_greater1_flag_decode(s, c_idx, inc);
                if (coeff_abs_level_greater1_flag[n_idx]) {
                    greater1_ctx = 0;
                } else if (greater1_ctx > 0 && greater1_ctx < 3) {
                    greater1_ctx++;
                }

                if (coeff_abs_level_greater1_flag[n_idx] &&
                    first_greater1_coeff_idx == -1)
                    first_greater1_coeff_idx = n_idx;
            }
            first_nz_pos_in_cg = significant_coeff_flag_idx[n_end - 1];
            sign_hidden = (last_nz_pos_in_cg - first_nz_pos_in_cg >= 4 &&
                           !lc->cu.cu_transquant_bypass_flag);

            if (first_greater1_coeff_idx != -1) {
                coeff_abs_level_greater1_flag[first_greater1_coeff_idx] += coeff_abs_level_greater2_flag_decode(s, c_idx, ctx_set);
            }
            if (!s->pps->sign_data_hiding_flag || !sign_hidden ) {
                coeff_sign_flag = coeff_sign_flag_decode(s, nb_significant_coeff_flag) << (16 - nb_significant_coeff_flag);
            } else {
                coeff_sign_flag = coeff_sign_flag_decode(s, nb_significant_coeff_flag - 1) << (16 - (nb_significant_coeff_flag - 1));
            }

            for (m = 0; m < n_end; m++) {
                n = significant_coeff_flag_idx[m];
                GET_COORD(offset, n);
                trans_coeff_level = 1 + coeff_abs_level_greater1_flag[n];
                if (trans_coeff_level == ((m < 8) ?
                                          ((n == first_greater1_coeff_idx) ? 3 : 2) : 1)) {
                    int last_coeff_abs_level_remaining = coeff_abs_level_remaining_decode(s, trans_coeff_level, c_rice_param);

                    trans_coeff_level += last_coeff_abs_level_remaining;
                    if (trans_coeff_level > (3 << c_rice_param))
                        c_rice_param = FFMIN(c_rice_param + 1, 4);

                }
                if (s->pps->sign_data_hiding_flag && sign_hidden) {
                    sum_abs += trans_coeff_level;
                    if (n == first_nz_pos_in_cg && (sum_abs&1))
                        trans_coeff_level = -trans_coeff_level;
                }
                if (coeff_sign_flag >> 15)
                    trans_coeff_level = -trans_coeff_level;
                coeff_sign_flag <<= 1;
                if(!lc->cu.cu_transquant_bypass_flag) {
                    if(s->sps->scaling_list_enable_flag) {
                        if(y_c || x_c || log2_trafo_size < 4) {
                            switch(log2_trafo_size) {
                                case 3: pos = (y_c << 3) + x_c; break;
                                case 4: pos = ((y_c >> 1) << 3) + (x_c >> 1); break;
                                case 5: pos = ((y_c >> 2) << 3) + (x_c >> 2); break;
                                default: pos = (y_c << 2) + x_c;
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
        s->hevcdsp.transquant_bypass[log2_trafo_size-2](dst, coeffs, stride);
    } else {
        if (transform_skip_flag)
            s->hevcdsp.transform_skip(dst, coeffs, stride);
        else if (lc->cu.pred_mode == MODE_INTRA && c_idx == 0 && log2_trafo_size == 2)
            s->hevcdsp.transform_4x4_luma_add(dst, coeffs, stride);
        else
            s->hevcdsp.transform_add[log2_trafo_size-2](dst, coeffs, stride);
    }
}

void ff_hevc_hls_mvd_coding(HEVCContext *s, int x0, int y0, int log2_cb_size)
{
    HEVCLocalContext *lc = &s->HEVClc;
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

