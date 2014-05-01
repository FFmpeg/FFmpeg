/*
 * HEVC video Decoder
 *
 * Copyright (C) 2012 - 2013 Guillaume Martres
 * Copyright (C) 2012 - 2013 Mickael Raulet
 * Copyright (C) 2012 - 2013 Gildas Cocherel
 * Copyright (C) 2012 - 2013 Wassim Hamidouche
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

#include "libavutil/atomic.h"
#include "libavutil/attributes.h"
#include "libavutil/common.h"
#include "libavutil/internal.h"
#include "libavutil/md5.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/stereo3d.h"

#include "bytestream.h"
#include "cabac_functions.h"
#include "dsputil.h"
#include "golomb.h"
#include "hevc.h"

const uint8_t ff_hevc_qpel_extra_before[4] = { 0, 3, 3, 2 };
const uint8_t ff_hevc_qpel_extra_after[4]  = { 0, 3, 4, 4 };
const uint8_t ff_hevc_qpel_extra[4]        = { 0, 6, 7, 6 };

/**
 * NOTE: Each function hls_foo correspond to the function foo in the
 * specification (HLS stands for High Level Syntax).
 */

/**
 * Section 5.7
 */

/* free everything allocated  by pic_arrays_init() */
static void pic_arrays_free(HEVCContext *s)
{
    av_freep(&s->sao);
    av_freep(&s->deblock);
    av_freep(&s->split_cu_flag);

    av_freep(&s->skip_flag);
    av_freep(&s->tab_ct_depth);

    av_freep(&s->tab_ipm);
    av_freep(&s->cbf_luma);
    av_freep(&s->is_pcm);

    av_freep(&s->qp_y_tab);
    av_freep(&s->tab_slice_address);
    av_freep(&s->filter_slice_edges);

    av_freep(&s->horizontal_bs);
    av_freep(&s->vertical_bs);

    av_freep(&s->sh.entry_point_offset);
    av_freep(&s->sh.size);
    av_freep(&s->sh.offset);

    av_buffer_pool_uninit(&s->tab_mvf_pool);
    av_buffer_pool_uninit(&s->rpl_tab_pool);
}

/* allocate arrays that depend on frame dimensions */
static int pic_arrays_init(HEVCContext *s, const HEVCSPS *sps)
{
    int log2_min_cb_size = sps->log2_min_cb_size;
    int width            = sps->width;
    int height           = sps->height;
    int pic_size         = width * height;
    int pic_size_in_ctb  = ((width  >> log2_min_cb_size) + 1) *
                           ((height >> log2_min_cb_size) + 1);
    int ctb_count        = sps->ctb_width * sps->ctb_height;
    int min_pu_size      = sps->min_pu_width * sps->min_pu_height;

    s->bs_width  = width  >> 3;
    s->bs_height = height >> 3;

    s->sao           = av_mallocz_array(ctb_count, sizeof(*s->sao));
    s->deblock       = av_mallocz_array(ctb_count, sizeof(*s->deblock));
    s->split_cu_flag = av_malloc(pic_size);
    if (!s->sao || !s->deblock || !s->split_cu_flag)
        goto fail;

    s->skip_flag    = av_malloc(pic_size_in_ctb);
    s->tab_ct_depth = av_malloc(sps->min_cb_height * sps->min_cb_width);
    if (!s->skip_flag || !s->tab_ct_depth)
        goto fail;

    s->cbf_luma = av_malloc(sps->min_tb_width * sps->min_tb_height);
    s->tab_ipm  = av_mallocz(min_pu_size);
    s->is_pcm   = av_malloc(min_pu_size);
    if (!s->tab_ipm || !s->cbf_luma || !s->is_pcm)
        goto fail;

    s->filter_slice_edges = av_malloc(ctb_count);
    s->tab_slice_address  = av_malloc(pic_size_in_ctb *
                                      sizeof(*s->tab_slice_address));
    s->qp_y_tab           = av_malloc(pic_size_in_ctb *
                                      sizeof(*s->qp_y_tab));
    if (!s->qp_y_tab || !s->filter_slice_edges || !s->tab_slice_address)
        goto fail;

    s->horizontal_bs = av_mallocz(2 * s->bs_width * (s->bs_height + 1));
    s->vertical_bs   = av_mallocz(2 * s->bs_width * (s->bs_height + 1));
    if (!s->horizontal_bs || !s->vertical_bs)
        goto fail;

    s->tab_mvf_pool = av_buffer_pool_init(min_pu_size * sizeof(MvField),
                                          av_buffer_alloc);
    s->rpl_tab_pool = av_buffer_pool_init(ctb_count * sizeof(RefPicListTab),
                                          av_buffer_allocz);
    if (!s->tab_mvf_pool || !s->rpl_tab_pool)
        goto fail;

    return 0;

fail:
    pic_arrays_free(s);
    return AVERROR(ENOMEM);
}

static void pred_weight_table(HEVCContext *s, GetBitContext *gb)
{
    int i = 0;
    int j = 0;
    uint8_t luma_weight_l0_flag[16];
    uint8_t chroma_weight_l0_flag[16];
    uint8_t luma_weight_l1_flag[16];
    uint8_t chroma_weight_l1_flag[16];

    s->sh.luma_log2_weight_denom = get_ue_golomb_long(gb);
    if (s->sps->chroma_format_idc != 0) {
        int delta = get_se_golomb(gb);
        s->sh.chroma_log2_weight_denom = av_clip_c(s->sh.luma_log2_weight_denom + delta, 0, 7);
    }

    for (i = 0; i < s->sh.nb_refs[L0]; i++) {
        luma_weight_l0_flag[i] = get_bits1(gb);
        if (!luma_weight_l0_flag[i]) {
            s->sh.luma_weight_l0[i] = 1 << s->sh.luma_log2_weight_denom;
            s->sh.luma_offset_l0[i] = 0;
        }
    }
    if (s->sps->chroma_format_idc != 0) { // FIXME: invert "if" and "for"
        for (i = 0; i < s->sh.nb_refs[L0]; i++)
            chroma_weight_l0_flag[i] = get_bits1(gb);
    } else {
        for (i = 0; i < s->sh.nb_refs[L0]; i++)
            chroma_weight_l0_flag[i] = 0;
    }
    for (i = 0; i < s->sh.nb_refs[L0]; i++) {
        if (luma_weight_l0_flag[i]) {
            int delta_luma_weight_l0 = get_se_golomb(gb);
            s->sh.luma_weight_l0[i] = (1 << s->sh.luma_log2_weight_denom) + delta_luma_weight_l0;
            s->sh.luma_offset_l0[i] = get_se_golomb(gb);
        }
        if (chroma_weight_l0_flag[i]) {
            for (j = 0; j < 2; j++) {
                int delta_chroma_weight_l0 = get_se_golomb(gb);
                int delta_chroma_offset_l0 = get_se_golomb(gb);
                s->sh.chroma_weight_l0[i][j] = (1 << s->sh.chroma_log2_weight_denom) + delta_chroma_weight_l0;
                s->sh.chroma_offset_l0[i][j] = av_clip_c((delta_chroma_offset_l0 - ((128 * s->sh.chroma_weight_l0[i][j])
                                                                                    >> s->sh.chroma_log2_weight_denom) + 128), -128, 127);
            }
        } else {
            s->sh.chroma_weight_l0[i][0] = 1 << s->sh.chroma_log2_weight_denom;
            s->sh.chroma_offset_l0[i][0] = 0;
            s->sh.chroma_weight_l0[i][1] = 1 << s->sh.chroma_log2_weight_denom;
            s->sh.chroma_offset_l0[i][1] = 0;
        }
    }
    if (s->sh.slice_type == B_SLICE) {
        for (i = 0; i < s->sh.nb_refs[L1]; i++) {
            luma_weight_l1_flag[i] = get_bits1(gb);
            if (!luma_weight_l1_flag[i]) {
                s->sh.luma_weight_l1[i] = 1 << s->sh.luma_log2_weight_denom;
                s->sh.luma_offset_l1[i] = 0;
            }
        }
        if (s->sps->chroma_format_idc != 0) {
            for (i = 0; i < s->sh.nb_refs[L1]; i++)
                chroma_weight_l1_flag[i] = get_bits1(gb);
        } else {
            for (i = 0; i < s->sh.nb_refs[L1]; i++)
                chroma_weight_l1_flag[i] = 0;
        }
        for (i = 0; i < s->sh.nb_refs[L1]; i++) {
            if (luma_weight_l1_flag[i]) {
                int delta_luma_weight_l1 = get_se_golomb(gb);
                s->sh.luma_weight_l1[i] = (1 << s->sh.luma_log2_weight_denom) + delta_luma_weight_l1;
                s->sh.luma_offset_l1[i] = get_se_golomb(gb);
            }
            if (chroma_weight_l1_flag[i]) {
                for (j = 0; j < 2; j++) {
                    int delta_chroma_weight_l1 = get_se_golomb(gb);
                    int delta_chroma_offset_l1 = get_se_golomb(gb);
                    s->sh.chroma_weight_l1[i][j] = (1 << s->sh.chroma_log2_weight_denom) + delta_chroma_weight_l1;
                    s->sh.chroma_offset_l1[i][j] = av_clip_c((delta_chroma_offset_l1 - ((128 * s->sh.chroma_weight_l1[i][j])
                                                                                        >> s->sh.chroma_log2_weight_denom) + 128), -128, 127);
                }
            } else {
                s->sh.chroma_weight_l1[i][0] = 1 << s->sh.chroma_log2_weight_denom;
                s->sh.chroma_offset_l1[i][0] = 0;
                s->sh.chroma_weight_l1[i][1] = 1 << s->sh.chroma_log2_weight_denom;
                s->sh.chroma_offset_l1[i][1] = 0;
            }
        }
    }
}

static int decode_lt_rps(HEVCContext *s, LongTermRPS *rps, GetBitContext *gb)
{
    const HEVCSPS *sps = s->sps;
    int max_poc_lsb    = 1 << sps->log2_max_poc_lsb;
    int prev_delta_msb = 0;
    unsigned int nb_sps = 0, nb_sh;
    int i;

    rps->nb_refs = 0;
    if (!sps->long_term_ref_pics_present_flag)
        return 0;

    if (sps->num_long_term_ref_pics_sps > 0)
        nb_sps = get_ue_golomb_long(gb);
    nb_sh = get_ue_golomb_long(gb);

    if (nb_sh + nb_sps > FF_ARRAY_ELEMS(rps->poc))
        return AVERROR_INVALIDDATA;

    rps->nb_refs = nb_sh + nb_sps;

    for (i = 0; i < rps->nb_refs; i++) {
        uint8_t delta_poc_msb_present;

        if (i < nb_sps) {
            uint8_t lt_idx_sps = 0;

            if (sps->num_long_term_ref_pics_sps > 1)
                lt_idx_sps = get_bits(gb, av_ceil_log2(sps->num_long_term_ref_pics_sps));

            rps->poc[i]  = sps->lt_ref_pic_poc_lsb_sps[lt_idx_sps];
            rps->used[i] = sps->used_by_curr_pic_lt_sps_flag[lt_idx_sps];
        } else {
            rps->poc[i]  = get_bits(gb, sps->log2_max_poc_lsb);
            rps->used[i] = get_bits1(gb);
        }

        delta_poc_msb_present = get_bits1(gb);
        if (delta_poc_msb_present) {
            int delta = get_ue_golomb_long(gb);

            if (i && i != nb_sps)
                delta += prev_delta_msb;

            rps->poc[i] += s->poc - delta * max_poc_lsb - s->sh.pic_order_cnt_lsb;
            prev_delta_msb = delta;
        }
    }

    return 0;
}

static int set_sps(HEVCContext *s, const HEVCSPS *sps)
{
    int ret;
    unsigned int num = 0, den = 0;

    pic_arrays_free(s);
    ret = pic_arrays_init(s, sps);
    if (ret < 0)
        goto fail;

    s->avctx->coded_width         = sps->width;
    s->avctx->coded_height        = sps->height;
    s->avctx->width               = sps->output_width;
    s->avctx->height              = sps->output_height;
    s->avctx->pix_fmt             = sps->pix_fmt;
    s->avctx->sample_aspect_ratio = sps->vui.sar;
    s->avctx->has_b_frames        = sps->temporal_layer[sps->max_sub_layers - 1].num_reorder_pics;

    if (sps->vui.video_signal_type_present_flag)
        s->avctx->color_range = sps->vui.video_full_range_flag ? AVCOL_RANGE_JPEG
                                                               : AVCOL_RANGE_MPEG;
    else
        s->avctx->color_range = AVCOL_RANGE_MPEG;

    if (sps->vui.colour_description_present_flag) {
        s->avctx->color_primaries = sps->vui.colour_primaries;
        s->avctx->color_trc       = sps->vui.transfer_characteristic;
        s->avctx->colorspace      = sps->vui.matrix_coeffs;
    } else {
        s->avctx->color_primaries = AVCOL_PRI_UNSPECIFIED;
        s->avctx->color_trc       = AVCOL_TRC_UNSPECIFIED;
        s->avctx->colorspace      = AVCOL_SPC_UNSPECIFIED;
    }

    ff_hevc_pred_init(&s->hpc,     sps->bit_depth);
    ff_hevc_dsp_init (&s->hevcdsp, sps->bit_depth);
    ff_videodsp_init (&s->vdsp,    sps->bit_depth);

    if (sps->sao_enabled) {
        av_frame_unref(s->tmp_frame);
        ret = ff_get_buffer(s->avctx, s->tmp_frame, AV_GET_BUFFER_FLAG_REF);
        if (ret < 0)
            goto fail;
        s->frame = s->tmp_frame;
    }

    s->sps = sps;
    s->vps = (HEVCVPS*) s->vps_list[s->sps->vps_id]->data;

    if (s->vps->vps_timing_info_present_flag) {
        num = s->vps->vps_num_units_in_tick;
        den = s->vps->vps_time_scale;
    } else if (sps->vui.vui_timing_info_present_flag) {
        num = sps->vui.vui_num_units_in_tick;
        den = sps->vui.vui_time_scale;
    }

    if (num != 0 && den != 0)
        av_reduce(&s->avctx->time_base.num, &s->avctx->time_base.den,
                  num, den, 1 << 30);

    return 0;

fail:
    pic_arrays_free(s);
    s->sps = NULL;
    return ret;
}

static int is_sps_exist(HEVCContext *s, const HEVCSPS* last_sps)
{
    int i;

    for( i = 0; i < MAX_SPS_COUNT; i++)
        if(s->sps_list[i])
            if (last_sps == (HEVCSPS*)s->sps_list[i]->data)
                return 1;
    return 0;
}

static int hls_slice_header(HEVCContext *s)
{
    GetBitContext *gb = &s->HEVClc->gb;
    SliceHeader *sh   = &s->sh;
    int i, j, ret;

    // Coded parameters
    sh->first_slice_in_pic_flag = get_bits1(gb);
    if ((IS_IDR(s) || IS_BLA(s)) && sh->first_slice_in_pic_flag) {
        s->seq_decode = (s->seq_decode + 1) & 0xff;
        s->max_ra     = INT_MAX;
        if (IS_IDR(s))
            ff_hevc_clear_refs(s);
    }
    sh->no_output_of_prior_pics_flag = 0;
    if (s->nal_unit_type >= 16 && s->nal_unit_type <= 23)
        sh->no_output_of_prior_pics_flag = get_bits1(gb);
    if (s->nal_unit_type == NAL_CRA_NUT && s->last_eos == 1)
        sh->no_output_of_prior_pics_flag = 1;

    sh->pps_id = get_ue_golomb_long(gb);
    if (sh->pps_id >= MAX_PPS_COUNT || !s->pps_list[sh->pps_id]) {
        av_log(s->avctx, AV_LOG_ERROR, "PPS id out of range: %d\n", sh->pps_id);
        return AVERROR_INVALIDDATA;
    }
    if (!sh->first_slice_in_pic_flag &&
        s->pps != (HEVCPPS*)s->pps_list[sh->pps_id]->data) {
        av_log(s->avctx, AV_LOG_ERROR, "PPS changed between slices.\n");
        return AVERROR_INVALIDDATA;
    }
    s->pps = (HEVCPPS*)s->pps_list[sh->pps_id]->data;

    if (s->sps != (HEVCSPS*)s->sps_list[s->pps->sps_id]->data) {
        const HEVCSPS* last_sps = s->sps;
        s->sps = (HEVCSPS*)s->sps_list[s->pps->sps_id]->data;
        if (last_sps) {
            if (is_sps_exist(s, last_sps)) {
                if (s->sps->width !=  last_sps->width || s->sps->height != last_sps->height ||
                        s->sps->temporal_layer[s->sps->max_sub_layers - 1].max_dec_pic_buffering != last_sps->temporal_layer[last_sps->max_sub_layers - 1].max_dec_pic_buffering)
                    sh->no_output_of_prior_pics_flag = 0;
            } else
                sh->no_output_of_prior_pics_flag = 0;
        }
        ff_hevc_clear_refs(s);
        ret = set_sps(s, s->sps);
        if (ret < 0)
            return ret;

        s->seq_decode = (s->seq_decode + 1) & 0xff;
        s->max_ra     = INT_MAX;
    }

    s->avctx->profile = s->sps->ptl.general_ptl.profile_idc;
    s->avctx->level   = s->sps->ptl.general_ptl.level_idc;

    sh->dependent_slice_segment_flag = 0;
    if (!sh->first_slice_in_pic_flag) {
        int slice_address_length;

        if (s->pps->dependent_slice_segments_enabled_flag)
            sh->dependent_slice_segment_flag = get_bits1(gb);

        slice_address_length = av_ceil_log2(s->sps->ctb_width *
                                            s->sps->ctb_height);
        sh->slice_segment_addr = get_bits(gb, slice_address_length);
        if (sh->slice_segment_addr >= s->sps->ctb_width * s->sps->ctb_height) {
            av_log(s->avctx, AV_LOG_ERROR,
                   "Invalid slice segment address: %u.\n",
                   sh->slice_segment_addr);
            return AVERROR_INVALIDDATA;
        }

        if (!sh->dependent_slice_segment_flag) {
            sh->slice_addr = sh->slice_segment_addr;
            s->slice_idx++;
        }
    } else {
        sh->slice_segment_addr = sh->slice_addr = 0;
        s->slice_idx           = 0;
        s->slice_initialized   = 0;
    }

    if (!sh->dependent_slice_segment_flag) {
        s->slice_initialized = 0;

        for (i = 0; i < s->pps->num_extra_slice_header_bits; i++)
            skip_bits(gb, 1);  // slice_reserved_undetermined_flag[]

        sh->slice_type = get_ue_golomb_long(gb);
        if (!(sh->slice_type == I_SLICE ||
              sh->slice_type == P_SLICE ||
              sh->slice_type == B_SLICE)) {
            av_log(s->avctx, AV_LOG_ERROR, "Unknown slice type: %d.\n",
                   sh->slice_type);
            return AVERROR_INVALIDDATA;
        }
        if (IS_IRAP(s) && sh->slice_type != I_SLICE) {
            av_log(s->avctx, AV_LOG_ERROR, "Inter slices in an IRAP frame.\n");
            return AVERROR_INVALIDDATA;
        }

        sh->pic_output_flag = 1;
        if (s->pps->output_flag_present_flag)
            sh->pic_output_flag = get_bits1(gb);

        if (s->sps->separate_colour_plane_flag)
            sh->colour_plane_id = get_bits(gb, 2);

        if (!IS_IDR(s)) {
            int short_term_ref_pic_set_sps_flag, poc;

            sh->pic_order_cnt_lsb = get_bits(gb, s->sps->log2_max_poc_lsb);
            poc = ff_hevc_compute_poc(s, sh->pic_order_cnt_lsb);
            if (!sh->first_slice_in_pic_flag && poc != s->poc) {
                av_log(s->avctx, AV_LOG_WARNING,
                       "Ignoring POC change between slices: %d -> %d\n", s->poc, poc);
                if (s->avctx->err_recognition & AV_EF_EXPLODE)
                    return AVERROR_INVALIDDATA;
                poc = s->poc;
            }
            s->poc = poc;

            short_term_ref_pic_set_sps_flag = get_bits1(gb);
            if (!short_term_ref_pic_set_sps_flag) {
                ret = ff_hevc_decode_short_term_rps(s, &sh->slice_rps, s->sps, 1);
                if (ret < 0)
                    return ret;

                sh->short_term_rps = &sh->slice_rps;
            } else {
                int numbits, rps_idx;

                if (!s->sps->nb_st_rps) {
                    av_log(s->avctx, AV_LOG_ERROR, "No ref lists in the SPS.\n");
                    return AVERROR_INVALIDDATA;
                }

                numbits = av_ceil_log2(s->sps->nb_st_rps);
                rps_idx = numbits > 0 ? get_bits(gb, numbits) : 0;
                sh->short_term_rps = &s->sps->st_rps[rps_idx];
            }

            ret = decode_lt_rps(s, &sh->long_term_rps, gb);
            if (ret < 0) {
                av_log(s->avctx, AV_LOG_WARNING, "Invalid long term RPS.\n");
                if (s->avctx->err_recognition & AV_EF_EXPLODE)
                    return AVERROR_INVALIDDATA;
            }

            if (s->sps->sps_temporal_mvp_enabled_flag)
                sh->slice_temporal_mvp_enabled_flag = get_bits1(gb);
            else
                sh->slice_temporal_mvp_enabled_flag = 0;
        } else {
            s->sh.short_term_rps = NULL;
            s->poc               = 0;
        }

        /* 8.3.1 */
        if (s->temporal_id == 0 &&
            s->nal_unit_type != NAL_TRAIL_N &&
            s->nal_unit_type != NAL_TSA_N   &&
            s->nal_unit_type != NAL_STSA_N  &&
            s->nal_unit_type != NAL_RADL_N  &&
            s->nal_unit_type != NAL_RADL_R  &&
            s->nal_unit_type != NAL_RASL_N  &&
            s->nal_unit_type != NAL_RASL_R)
            s->pocTid0 = s->poc;

        if (s->sps->sao_enabled) {
            sh->slice_sample_adaptive_offset_flag[0] = get_bits1(gb);
            sh->slice_sample_adaptive_offset_flag[1] =
            sh->slice_sample_adaptive_offset_flag[2] = get_bits1(gb);
        } else {
            sh->slice_sample_adaptive_offset_flag[0] = 0;
            sh->slice_sample_adaptive_offset_flag[1] = 0;
            sh->slice_sample_adaptive_offset_flag[2] = 0;
        }

        sh->nb_refs[L0] = sh->nb_refs[L1] = 0;
        if (sh->slice_type == P_SLICE || sh->slice_type == B_SLICE) {
            int nb_refs;

            sh->nb_refs[L0] = s->pps->num_ref_idx_l0_default_active;
            if (sh->slice_type == B_SLICE)
                sh->nb_refs[L1] = s->pps->num_ref_idx_l1_default_active;

            if (get_bits1(gb)) { // num_ref_idx_active_override_flag
                sh->nb_refs[L0] = get_ue_golomb_long(gb) + 1;
                if (sh->slice_type == B_SLICE)
                    sh->nb_refs[L1] = get_ue_golomb_long(gb) + 1;
            }
            if (sh->nb_refs[L0] > MAX_REFS || sh->nb_refs[L1] > MAX_REFS) {
                av_log(s->avctx, AV_LOG_ERROR, "Too many refs: %d/%d.\n",
                       sh->nb_refs[L0], sh->nb_refs[L1]);
                return AVERROR_INVALIDDATA;
            }

            sh->rpl_modification_flag[0] = 0;
            sh->rpl_modification_flag[1] = 0;
            nb_refs = ff_hevc_frame_nb_refs(s);
            if (!nb_refs) {
                av_log(s->avctx, AV_LOG_ERROR, "Zero refs for a frame with P or B slices.\n");
                return AVERROR_INVALIDDATA;
            }

            if (s->pps->lists_modification_present_flag && nb_refs > 1) {
                sh->rpl_modification_flag[0] = get_bits1(gb);
                if (sh->rpl_modification_flag[0]) {
                    for (i = 0; i < sh->nb_refs[L0]; i++)
                        sh->list_entry_lx[0][i] = get_bits(gb, av_ceil_log2(nb_refs));
                }

                if (sh->slice_type == B_SLICE) {
                    sh->rpl_modification_flag[1] = get_bits1(gb);
                    if (sh->rpl_modification_flag[1] == 1)
                        for (i = 0; i < sh->nb_refs[L1]; i++)
                            sh->list_entry_lx[1][i] = get_bits(gb, av_ceil_log2(nb_refs));
                }
            }

            if (sh->slice_type == B_SLICE)
                sh->mvd_l1_zero_flag = get_bits1(gb);

            if (s->pps->cabac_init_present_flag)
                sh->cabac_init_flag = get_bits1(gb);
            else
                sh->cabac_init_flag = 0;

            sh->collocated_ref_idx = 0;
            if (sh->slice_temporal_mvp_enabled_flag) {
                sh->collocated_list = L0;
                if (sh->slice_type == B_SLICE)
                    sh->collocated_list = !get_bits1(gb);

                if (sh->nb_refs[sh->collocated_list] > 1) {
                    sh->collocated_ref_idx = get_ue_golomb_long(gb);
                    if (sh->collocated_ref_idx >= sh->nb_refs[sh->collocated_list]) {
                        av_log(s->avctx, AV_LOG_ERROR,
                               "Invalid collocated_ref_idx: %d.\n",
                               sh->collocated_ref_idx);
                        return AVERROR_INVALIDDATA;
                    }
                }
            }

            if ((s->pps->weighted_pred_flag   && sh->slice_type == P_SLICE) ||
                (s->pps->weighted_bipred_flag && sh->slice_type == B_SLICE)) {
                pred_weight_table(s, gb);
            }

            sh->max_num_merge_cand = 5 - get_ue_golomb_long(gb);
            if (sh->max_num_merge_cand < 1 || sh->max_num_merge_cand > 5) {
                av_log(s->avctx, AV_LOG_ERROR,
                       "Invalid number of merging MVP candidates: %d.\n",
                       sh->max_num_merge_cand);
                return AVERROR_INVALIDDATA;
            }
        }

        sh->slice_qp_delta = get_se_golomb(gb);

        if (s->pps->pic_slice_level_chroma_qp_offsets_present_flag) {
            sh->slice_cb_qp_offset = get_se_golomb(gb);
            sh->slice_cr_qp_offset = get_se_golomb(gb);
        } else {
            sh->slice_cb_qp_offset = 0;
            sh->slice_cr_qp_offset = 0;
        }

        if (s->pps->deblocking_filter_control_present_flag) {
            int deblocking_filter_override_flag = 0;

            if (s->pps->deblocking_filter_override_enabled_flag)
                deblocking_filter_override_flag = get_bits1(gb);

            if (deblocking_filter_override_flag) {
                sh->disable_deblocking_filter_flag = get_bits1(gb);
                if (!sh->disable_deblocking_filter_flag) {
                    sh->beta_offset = get_se_golomb(gb) * 2;
                    sh->tc_offset   = get_se_golomb(gb) * 2;
                }
            } else {
                sh->disable_deblocking_filter_flag = s->pps->disable_dbf;
                sh->beta_offset                    = s->pps->beta_offset;
                sh->tc_offset                      = s->pps->tc_offset;
            }
        } else {
            sh->disable_deblocking_filter_flag = 0;
            sh->beta_offset                    = 0;
            sh->tc_offset                      = 0;
        }

        if (s->pps->seq_loop_filter_across_slices_enabled_flag &&
            (sh->slice_sample_adaptive_offset_flag[0] ||
             sh->slice_sample_adaptive_offset_flag[1] ||
             !sh->disable_deblocking_filter_flag)) {
            sh->slice_loop_filter_across_slices_enabled_flag = get_bits1(gb);
        } else {
            sh->slice_loop_filter_across_slices_enabled_flag = s->pps->seq_loop_filter_across_slices_enabled_flag;
        }
    } else if (!s->slice_initialized) {
        av_log(s->avctx, AV_LOG_ERROR, "Independent slice segment missing.\n");
        return AVERROR_INVALIDDATA;
    }

    sh->num_entry_point_offsets = 0;
    if (s->pps->tiles_enabled_flag || s->pps->entropy_coding_sync_enabled_flag) {
        sh->num_entry_point_offsets = get_ue_golomb_long(gb);
        if (sh->num_entry_point_offsets > 0) {
            int offset_len = get_ue_golomb_long(gb) + 1;
            int segments = offset_len >> 4;
            int rest = (offset_len & 15);
            av_freep(&sh->entry_point_offset);
            av_freep(&sh->offset);
            av_freep(&sh->size);
            sh->entry_point_offset = av_malloc(sh->num_entry_point_offsets * sizeof(int));
            sh->offset = av_malloc(sh->num_entry_point_offsets * sizeof(int));
            sh->size = av_malloc(sh->num_entry_point_offsets * sizeof(int));
            if (!sh->entry_point_offset || !sh->offset || !sh->size) {
                sh->num_entry_point_offsets = 0;
                av_log(s->avctx, AV_LOG_ERROR, "Failed to allocate memory\n");
                return AVERROR(ENOMEM);
            }
            for (i = 0; i < sh->num_entry_point_offsets; i++) {
                int val = 0;
                for (j = 0; j < segments; j++) {
                    val <<= 16;
                    val += get_bits(gb, 16);
                }
                if (rest) {
                    val <<= rest;
                    val += get_bits(gb, rest);
                }
                sh->entry_point_offset[i] = val + 1; // +1; // +1 to get the size
            }
            if (s->threads_number > 1 && (s->pps->num_tile_rows > 1 || s->pps->num_tile_columns > 1)) {
                s->enable_parallel_tiles = 0; // TODO: you can enable tiles in parallel here
                s->threads_number = 1;
            } else
                s->enable_parallel_tiles = 0;
        } else
            s->enable_parallel_tiles = 0;
    }

    if (s->pps->slice_header_extension_present_flag) {
        unsigned int length = get_ue_golomb_long(gb);
        for (i = 0; i < length; i++)
            skip_bits(gb, 8);  // slice_header_extension_data_byte
    }

    // Inferred parameters
    sh->slice_qp = 26U + s->pps->pic_init_qp_minus26 + sh->slice_qp_delta;
    if (sh->slice_qp > 51 ||
        sh->slice_qp < -s->sps->qp_bd_offset) {
        av_log(s->avctx, AV_LOG_ERROR,
               "The slice_qp %d is outside the valid range "
               "[%d, 51].\n",
               sh->slice_qp,
               -s->sps->qp_bd_offset);
        return AVERROR_INVALIDDATA;
    }

    sh->slice_ctb_addr_rs = sh->slice_segment_addr;

    if (!s->sh.slice_ctb_addr_rs && s->sh.dependent_slice_segment_flag) {
        av_log(s->avctx, AV_LOG_ERROR, "Impossible slice segment.\n");
        return AVERROR_INVALIDDATA;
    }

    s->HEVClc->first_qp_group = !s->sh.dependent_slice_segment_flag;

    if (!s->pps->cu_qp_delta_enabled_flag)
        s->HEVClc->qp_y = s->sh.slice_qp;

    s->slice_initialized = 1;

    return 0;
}

#define CTB(tab, x, y) ((tab)[(y) * s->sps->ctb_width + (x)])

#define SET_SAO(elem, value)                            \
do {                                                    \
    if (!sao_merge_up_flag && !sao_merge_left_flag)     \
        sao->elem = value;                              \
    else if (sao_merge_left_flag)                       \
        sao->elem = CTB(s->sao, rx-1, ry).elem;         \
    else if (sao_merge_up_flag)                         \
        sao->elem = CTB(s->sao, rx, ry-1).elem;         \
    else                                                \
        sao->elem = 0;                                  \
} while (0)

static void hls_sao_param(HEVCContext *s, int rx, int ry)
{
    HEVCLocalContext *lc    = s->HEVClc;
    int sao_merge_left_flag = 0;
    int sao_merge_up_flag   = 0;
    int shift               = s->sps->bit_depth - FFMIN(s->sps->bit_depth, 10);
    SAOParams *sao          = &CTB(s->sao, rx, ry);
    int c_idx, i;

    if (s->sh.slice_sample_adaptive_offset_flag[0] ||
        s->sh.slice_sample_adaptive_offset_flag[1]) {
        if (rx > 0) {
            if (lc->ctb_left_flag)
                sao_merge_left_flag = ff_hevc_sao_merge_flag_decode(s);
        }
        if (ry > 0 && !sao_merge_left_flag) {
            if (lc->ctb_up_flag)
                sao_merge_up_flag = ff_hevc_sao_merge_flag_decode(s);
        }
    }

    for (c_idx = 0; c_idx < 3; c_idx++) {
        if (!s->sh.slice_sample_adaptive_offset_flag[c_idx]) {
            sao->type_idx[c_idx] = SAO_NOT_APPLIED;
            continue;
        }

        if (c_idx == 2) {
            sao->type_idx[2] = sao->type_idx[1];
            sao->eo_class[2] = sao->eo_class[1];
        } else {
            SET_SAO(type_idx[c_idx], ff_hevc_sao_type_idx_decode(s));
        }

        if (sao->type_idx[c_idx] == SAO_NOT_APPLIED)
            continue;

        for (i = 0; i < 4; i++)
            SET_SAO(offset_abs[c_idx][i], ff_hevc_sao_offset_abs_decode(s));

        if (sao->type_idx[c_idx] == SAO_BAND) {
            for (i = 0; i < 4; i++) {
                if (sao->offset_abs[c_idx][i]) {
                    SET_SAO(offset_sign[c_idx][i],
                            ff_hevc_sao_offset_sign_decode(s));
                } else {
                    sao->offset_sign[c_idx][i] = 0;
                }
            }
            SET_SAO(band_position[c_idx], ff_hevc_sao_band_position_decode(s));
        } else if (c_idx != 2) {
            SET_SAO(eo_class[c_idx], ff_hevc_sao_eo_class_decode(s));
        }

        // Inferred parameters
        sao->offset_val[c_idx][0] = 0;
        for (i = 0; i < 4; i++) {
            sao->offset_val[c_idx][i + 1] = sao->offset_abs[c_idx][i] << shift;
            if (sao->type_idx[c_idx] == SAO_EDGE) {
                if (i > 1)
                    sao->offset_val[c_idx][i + 1] = -sao->offset_val[c_idx][i + 1];
            } else if (sao->offset_sign[c_idx][i]) {
                sao->offset_val[c_idx][i + 1] = -sao->offset_val[c_idx][i + 1];
            }
        }
    }
}

#undef SET_SAO
#undef CTB

static int hls_transform_unit(HEVCContext *s, int x0, int y0,
                              int xBase, int yBase, int cb_xBase, int cb_yBase,
                              int log2_cb_size, int log2_trafo_size,
                              int trafo_depth, int blk_idx)
{
    HEVCLocalContext *lc = s->HEVClc;

    if (lc->cu.pred_mode == MODE_INTRA) {
        int trafo_size = 1 << log2_trafo_size;
        ff_hevc_set_neighbour_available(s, x0, y0, trafo_size, trafo_size);

        s->hpc.intra_pred(s, x0, y0, log2_trafo_size, 0);
        if (log2_trafo_size > 2) {
            trafo_size = trafo_size << (s->sps->hshift[1] - 1);
            ff_hevc_set_neighbour_available(s, x0, y0, trafo_size, trafo_size);
            s->hpc.intra_pred(s, x0, y0, log2_trafo_size - 1, 1);
            s->hpc.intra_pred(s, x0, y0, log2_trafo_size - 1, 2);
        } else if (blk_idx == 3) {
            trafo_size = trafo_size << s->sps->hshift[1];
            ff_hevc_set_neighbour_available(s, xBase, yBase,
                                            trafo_size, trafo_size);
            s->hpc.intra_pred(s, xBase, yBase, log2_trafo_size, 1);
            s->hpc.intra_pred(s, xBase, yBase, log2_trafo_size, 2);
        }
    }

    if (lc->tt.cbf_luma ||
        SAMPLE_CBF(lc->tt.cbf_cb[trafo_depth], x0, y0) ||
        SAMPLE_CBF(lc->tt.cbf_cr[trafo_depth], x0, y0)) {
        int scan_idx   = SCAN_DIAG;
        int scan_idx_c = SCAN_DIAG;

        if (s->pps->cu_qp_delta_enabled_flag && !lc->tu.is_cu_qp_delta_coded) {
            lc->tu.cu_qp_delta = ff_hevc_cu_qp_delta_abs(s);
            if (lc->tu.cu_qp_delta != 0)
                if (ff_hevc_cu_qp_delta_sign_flag(s) == 1)
                    lc->tu.cu_qp_delta = -lc->tu.cu_qp_delta;
            lc->tu.is_cu_qp_delta_coded = 1;

            if (lc->tu.cu_qp_delta < -(26 + s->sps->qp_bd_offset / 2) ||
                lc->tu.cu_qp_delta >  (25 + s->sps->qp_bd_offset / 2)) {
                av_log(s->avctx, AV_LOG_ERROR,
                       "The cu_qp_delta %d is outside the valid range "
                       "[%d, %d].\n",
                       lc->tu.cu_qp_delta,
                       -(26 + s->sps->qp_bd_offset / 2),
                        (25 + s->sps->qp_bd_offset / 2));
                return AVERROR_INVALIDDATA;
            }

            ff_hevc_set_qPy(s, x0, y0, cb_xBase, cb_yBase, log2_cb_size);
        }

        if (lc->cu.pred_mode == MODE_INTRA && log2_trafo_size < 4) {
            if (lc->tu.cur_intra_pred_mode >= 6 &&
                lc->tu.cur_intra_pred_mode <= 14) {
                scan_idx = SCAN_VERT;
            } else if (lc->tu.cur_intra_pred_mode >= 22 &&
                       lc->tu.cur_intra_pred_mode <= 30) {
                scan_idx = SCAN_HORIZ;
            }

            if (lc->pu.intra_pred_mode_c >=  6 &&
                lc->pu.intra_pred_mode_c <= 14) {
                scan_idx_c = SCAN_VERT;
            } else if (lc->pu.intra_pred_mode_c >= 22 &&
                       lc->pu.intra_pred_mode_c <= 30) {
                scan_idx_c = SCAN_HORIZ;
            }
        }

        if (lc->tt.cbf_luma)
            ff_hevc_hls_residual_coding(s, x0, y0, log2_trafo_size, scan_idx, 0);
        if (log2_trafo_size > 2) {
            if (SAMPLE_CBF(lc->tt.cbf_cb[trafo_depth], x0, y0))
                ff_hevc_hls_residual_coding(s, x0, y0, log2_trafo_size - 1, scan_idx_c, 1);
            if (SAMPLE_CBF(lc->tt.cbf_cr[trafo_depth], x0, y0))
                ff_hevc_hls_residual_coding(s, x0, y0, log2_trafo_size - 1, scan_idx_c, 2);
        } else if (blk_idx == 3) {
            if (SAMPLE_CBF(lc->tt.cbf_cb[trafo_depth], xBase, yBase))
                ff_hevc_hls_residual_coding(s, xBase, yBase, log2_trafo_size, scan_idx_c, 1);
            if (SAMPLE_CBF(lc->tt.cbf_cr[trafo_depth], xBase, yBase))
                ff_hevc_hls_residual_coding(s, xBase, yBase, log2_trafo_size, scan_idx_c, 2);
        }
    }
    return 0;
}

static void set_deblocking_bypass(HEVCContext *s, int x0, int y0, int log2_cb_size)
{
    int cb_size          = 1 << log2_cb_size;
    int log2_min_pu_size = s->sps->log2_min_pu_size;

    int min_pu_width     = s->sps->min_pu_width;
    int x_end = FFMIN(x0 + cb_size, s->sps->width);
    int y_end = FFMIN(y0 + cb_size, s->sps->height);
    int i, j;

    for (j = (y0 >> log2_min_pu_size); j < (y_end >> log2_min_pu_size); j++)
        for (i = (x0 >> log2_min_pu_size); i < (x_end >> log2_min_pu_size); i++)
            s->is_pcm[i + j * min_pu_width] = 2;
}

static int hls_transform_tree(HEVCContext *s, int x0, int y0,
                              int xBase, int yBase, int cb_xBase, int cb_yBase,
                              int log2_cb_size, int log2_trafo_size,
                              int trafo_depth, int blk_idx)
{
    HEVCLocalContext *lc = s->HEVClc;
    uint8_t split_transform_flag;
    int ret;

    if (trafo_depth > 0 && log2_trafo_size == 2) {
        SAMPLE_CBF(lc->tt.cbf_cb[trafo_depth], x0, y0) =
            SAMPLE_CBF(lc->tt.cbf_cb[trafo_depth - 1], xBase, yBase);
        SAMPLE_CBF(lc->tt.cbf_cr[trafo_depth], x0, y0) =
            SAMPLE_CBF(lc->tt.cbf_cr[trafo_depth - 1], xBase, yBase);
    } else {
        SAMPLE_CBF(lc->tt.cbf_cb[trafo_depth], x0, y0) =
        SAMPLE_CBF(lc->tt.cbf_cr[trafo_depth], x0, y0) = 0;
    }

    if (lc->cu.intra_split_flag) {
        if (trafo_depth == 1)
            lc->tu.cur_intra_pred_mode = lc->pu.intra_pred_mode[blk_idx];
    } else {
        lc->tu.cur_intra_pred_mode = lc->pu.intra_pred_mode[0];
    }

    lc->tt.cbf_luma = 1;

    lc->tt.inter_split_flag = s->sps->max_transform_hierarchy_depth_inter == 0 &&
                              lc->cu.pred_mode == MODE_INTER &&
                              lc->cu.part_mode != PART_2Nx2N &&
                              trafo_depth == 0;

    if (log2_trafo_size <= s->sps->log2_max_trafo_size &&
        log2_trafo_size >  s->sps->log2_min_tb_size    &&
        trafo_depth     < lc->cu.max_trafo_depth       &&
        !(lc->cu.intra_split_flag && trafo_depth == 0)) {
        split_transform_flag = ff_hevc_split_transform_flag_decode(s, log2_trafo_size);
    } else {
        split_transform_flag = log2_trafo_size > s->sps->log2_max_trafo_size ||
                               (lc->cu.intra_split_flag && trafo_depth == 0) ||
                               lc->tt.inter_split_flag;
    }

    if (log2_trafo_size > 2) {
        if (trafo_depth == 0 ||
            SAMPLE_CBF(lc->tt.cbf_cb[trafo_depth - 1], xBase, yBase)) {
            SAMPLE_CBF(lc->tt.cbf_cb[trafo_depth], x0, y0) =
                ff_hevc_cbf_cb_cr_decode(s, trafo_depth);
        }

        if (trafo_depth == 0 ||
            SAMPLE_CBF(lc->tt.cbf_cr[trafo_depth - 1], xBase, yBase)) {
            SAMPLE_CBF(lc->tt.cbf_cr[trafo_depth], x0, y0) =
                ff_hevc_cbf_cb_cr_decode(s, trafo_depth);
        }
    }

    if (split_transform_flag) {
        int x1 = x0 + ((1 << log2_trafo_size) >> 1);
        int y1 = y0 + ((1 << log2_trafo_size) >> 1);

        ret = hls_transform_tree(s, x0, y0, x0, y0, cb_xBase, cb_yBase,
                                 log2_cb_size, log2_trafo_size - 1,
                                 trafo_depth + 1, 0);
        if (ret < 0)
            return ret;
        ret = hls_transform_tree(s, x1, y0, x0, y0, cb_xBase, cb_yBase,
                                 log2_cb_size, log2_trafo_size - 1,
                                 trafo_depth + 1, 1);
        if (ret < 0)
            return ret;
        ret = hls_transform_tree(s, x0, y1, x0, y0, cb_xBase, cb_yBase,
                                 log2_cb_size, log2_trafo_size - 1,
                                 trafo_depth + 1, 2);
        if (ret < 0)
            return ret;
        ret = hls_transform_tree(s, x1, y1, x0, y0, cb_xBase, cb_yBase,
                                 log2_cb_size, log2_trafo_size - 1,
                                 trafo_depth + 1, 3);
        if (ret < 0)
            return ret;
    } else {
        int min_tu_size      = 1 << s->sps->log2_min_tb_size;
        int log2_min_tu_size = s->sps->log2_min_tb_size;
        int min_tu_width     = s->sps->min_tb_width;

        if (lc->cu.pred_mode == MODE_INTRA || trafo_depth != 0 ||
            SAMPLE_CBF(lc->tt.cbf_cb[trafo_depth], x0, y0) ||
            SAMPLE_CBF(lc->tt.cbf_cr[trafo_depth], x0, y0)) {
            lc->tt.cbf_luma = ff_hevc_cbf_luma_decode(s, trafo_depth);
        }

        ret = hls_transform_unit(s, x0, y0, xBase, yBase, cb_xBase, cb_yBase,
                                 log2_cb_size, log2_trafo_size, trafo_depth,
                                 blk_idx);
        if (ret < 0)
            return ret;
        // TODO: store cbf_luma somewhere else
        if (lc->tt.cbf_luma) {
            int i, j;
            for (i = 0; i < (1 << log2_trafo_size); i += min_tu_size)
                for (j = 0; j < (1 << log2_trafo_size); j += min_tu_size) {
                    int x_tu = (x0 + j) >> log2_min_tu_size;
                    int y_tu = (y0 + i) >> log2_min_tu_size;
                    s->cbf_luma[y_tu * min_tu_width + x_tu] = 1;
                }
        }
        if (!s->sh.disable_deblocking_filter_flag) {
            ff_hevc_deblocking_boundary_strengths(s, x0, y0, log2_trafo_size,
                                                  lc->slice_or_tiles_up_boundary,
                                                  lc->slice_or_tiles_left_boundary);
            if (s->pps->transquant_bypass_enable_flag &&
                lc->cu.cu_transquant_bypass_flag)
                set_deblocking_bypass(s, x0, y0, log2_trafo_size);
        }
    }
    return 0;
}

static int hls_pcm_sample(HEVCContext *s, int x0, int y0, int log2_cb_size)
{
    //TODO: non-4:2:0 support
    HEVCLocalContext *lc = s->HEVClc;
    GetBitContext gb;
    int cb_size   = 1 << log2_cb_size;
    int stride0   = s->frame->linesize[0];
    uint8_t *dst0 = &s->frame->data[0][y0 * stride0 + (x0 << s->sps->pixel_shift)];
    int   stride1 = s->frame->linesize[1];
    uint8_t *dst1 = &s->frame->data[1][(y0 >> s->sps->vshift[1]) * stride1 + ((x0 >> s->sps->hshift[1]) << s->sps->pixel_shift)];
    int   stride2 = s->frame->linesize[2];
    uint8_t *dst2 = &s->frame->data[2][(y0 >> s->sps->vshift[2]) * stride2 + ((x0 >> s->sps->hshift[2]) << s->sps->pixel_shift)];

    int length         = cb_size * cb_size * s->sps->pcm.bit_depth + ((cb_size * cb_size) >> 1) * s->sps->pcm.bit_depth_chroma;
    const uint8_t *pcm = skip_bytes(&s->HEVClc->cc, (length + 7) >> 3);
    int ret;

    ff_hevc_deblocking_boundary_strengths(s, x0, y0, log2_cb_size,
                                          lc->slice_or_tiles_up_boundary,
                                          lc->slice_or_tiles_left_boundary);

    ret = init_get_bits(&gb, pcm, length);
    if (ret < 0)
        return ret;

    s->hevcdsp.put_pcm(dst0, stride0, cb_size,     &gb, s->sps->pcm.bit_depth);
    s->hevcdsp.put_pcm(dst1, stride1, cb_size / 2, &gb, s->sps->pcm.bit_depth_chroma);
    s->hevcdsp.put_pcm(dst2, stride2, cb_size / 2, &gb, s->sps->pcm.bit_depth_chroma);
    return 0;
}

/**
 * 8.5.3.2.2.1 Luma sample interpolation process
 *
 * @param s HEVC decoding context
 * @param dst target buffer for block data at block position
 * @param dststride stride of the dst buffer
 * @param ref reference picture buffer at origin (0, 0)
 * @param mv motion vector (relative to block position) to get pixel data from
 * @param x_off horizontal position of block from origin (0, 0)
 * @param y_off vertical position of block from origin (0, 0)
 * @param block_w width of block
 * @param block_h height of block
 */
static void luma_mc(HEVCContext *s, int16_t *dst, ptrdiff_t dststride,
                    AVFrame *ref, const Mv *mv, int x_off, int y_off,
                    int block_w, int block_h)
{
    HEVCLocalContext *lc = s->HEVClc;
    uint8_t *src         = ref->data[0];
    ptrdiff_t srcstride  = ref->linesize[0];
    int pic_width        = s->sps->width;
    int pic_height       = s->sps->height;

    int mx         = mv->x & 3;
    int my         = mv->y & 3;
    int extra_left = ff_hevc_qpel_extra_before[mx];
    int extra_top  = ff_hevc_qpel_extra_before[my];

    x_off += mv->x >> 2;
    y_off += mv->y >> 2;
    src   += y_off * srcstride + (x_off << s->sps->pixel_shift);

    if (x_off < extra_left || y_off < extra_top ||
        x_off >= pic_width - block_w - ff_hevc_qpel_extra_after[mx] ||
        y_off >= pic_height - block_h - ff_hevc_qpel_extra_after[my]) {
        const int edge_emu_stride = EDGE_EMU_BUFFER_STRIDE << s->sps->pixel_shift;
        int offset = extra_top * srcstride + (extra_left << s->sps->pixel_shift);
        int buf_offset = extra_top *
                         edge_emu_stride + (extra_left << s->sps->pixel_shift);

        s->vdsp.emulated_edge_mc(lc->edge_emu_buffer, src - offset,
                                 edge_emu_stride, srcstride,
                                 block_w + ff_hevc_qpel_extra[mx],
                                 block_h + ff_hevc_qpel_extra[my],
                                 x_off - extra_left, y_off - extra_top,
                                 pic_width, pic_height);
        src = lc->edge_emu_buffer + buf_offset;
        srcstride = edge_emu_stride;
    }
    s->hevcdsp.put_hevc_qpel[my][mx](dst, dststride, src, srcstride, block_w,
                                     block_h, lc->mc_buffer);
}

/**
 * 8.5.3.2.2.2 Chroma sample interpolation process
 *
 * @param s HEVC decoding context
 * @param dst1 target buffer for block data at block position (U plane)
 * @param dst2 target buffer for block data at block position (V plane)
 * @param dststride stride of the dst1 and dst2 buffers
 * @param ref reference picture buffer at origin (0, 0)
 * @param mv motion vector (relative to block position) to get pixel data from
 * @param x_off horizontal position of block from origin (0, 0)
 * @param y_off vertical position of block from origin (0, 0)
 * @param block_w width of block
 * @param block_h height of block
 */
static void chroma_mc(HEVCContext *s, int16_t *dst1, int16_t *dst2,
                      ptrdiff_t dststride, AVFrame *ref, const Mv *mv,
                      int x_off, int y_off, int block_w, int block_h)
{
    HEVCLocalContext *lc = s->HEVClc;
    uint8_t *src1        = ref->data[1];
    uint8_t *src2        = ref->data[2];
    ptrdiff_t src1stride = ref->linesize[1];
    ptrdiff_t src2stride = ref->linesize[2];
    int pic_width        = s->sps->width >> 1;
    int pic_height       = s->sps->height >> 1;

    int mx = mv->x & 7;
    int my = mv->y & 7;

    x_off += mv->x >> 3;
    y_off += mv->y >> 3;
    src1  += y_off * src1stride + (x_off << s->sps->pixel_shift);
    src2  += y_off * src2stride + (x_off << s->sps->pixel_shift);

    if (x_off < EPEL_EXTRA_BEFORE || y_off < EPEL_EXTRA_AFTER ||
        x_off >= pic_width - block_w - EPEL_EXTRA_AFTER ||
        y_off >= pic_height - block_h - EPEL_EXTRA_AFTER) {
        const int edge_emu_stride = EDGE_EMU_BUFFER_STRIDE << s->sps->pixel_shift;
        int offset1 = EPEL_EXTRA_BEFORE * (src1stride + (1 << s->sps->pixel_shift));
        int buf_offset1 = EPEL_EXTRA_BEFORE *
                          (edge_emu_stride + (1 << s->sps->pixel_shift));
        int offset2 = EPEL_EXTRA_BEFORE * (src2stride + (1 << s->sps->pixel_shift));
        int buf_offset2 = EPEL_EXTRA_BEFORE *
                          (edge_emu_stride + (1 << s->sps->pixel_shift));

        s->vdsp.emulated_edge_mc(lc->edge_emu_buffer, src1 - offset1,
                                 edge_emu_stride, src1stride,
                                 block_w + EPEL_EXTRA, block_h + EPEL_EXTRA,
                                 x_off - EPEL_EXTRA_BEFORE,
                                 y_off - EPEL_EXTRA_BEFORE,
                                 pic_width, pic_height);

        src1 = lc->edge_emu_buffer + buf_offset1;
        src1stride = edge_emu_stride;
        s->hevcdsp.put_hevc_epel[!!my][!!mx](dst1, dststride, src1, src1stride,
                                             block_w, block_h, mx, my, lc->mc_buffer);

        s->vdsp.emulated_edge_mc(lc->edge_emu_buffer, src2 - offset2,
                                 edge_emu_stride, src2stride,
                                 block_w + EPEL_EXTRA, block_h + EPEL_EXTRA,
                                 x_off - EPEL_EXTRA_BEFORE,
                                 y_off - EPEL_EXTRA_BEFORE,
                                 pic_width, pic_height);
        src2 = lc->edge_emu_buffer + buf_offset2;
        src2stride = edge_emu_stride;

        s->hevcdsp.put_hevc_epel[!!my][!!mx](dst2, dststride, src2, src2stride,
                                             block_w, block_h, mx, my,
                                             lc->mc_buffer);
    } else {
        s->hevcdsp.put_hevc_epel[!!my][!!mx](dst1, dststride, src1, src1stride,
                                             block_w, block_h, mx, my,
                                             lc->mc_buffer);
        s->hevcdsp.put_hevc_epel[!!my][!!mx](dst2, dststride, src2, src2stride,
                                             block_w, block_h, mx, my,
                                             lc->mc_buffer);
    }
}

static void hevc_await_progress(HEVCContext *s, HEVCFrame *ref,
                                const Mv *mv, int y0, int height)
{
    int y = (mv->y >> 2) + y0 + height + 9;

    if (s->threads_type == FF_THREAD_FRAME )
        ff_thread_await_progress(&ref->tf, y, 0);
}

static void hls_prediction_unit(HEVCContext *s, int x0, int y0,
                                int nPbW, int nPbH,
                                int log2_cb_size, int partIdx)
{
#define POS(c_idx, x, y)                                                              \
    &s->frame->data[c_idx][((y) >> s->sps->vshift[c_idx]) * s->frame->linesize[c_idx] + \
                           (((x) >> s->sps->hshift[c_idx]) << s->sps->pixel_shift)]
    HEVCLocalContext *lc = s->HEVClc;
    int merge_idx = 0;
    struct MvField current_mv = {{{ 0 }}};

    int min_pu_width = s->sps->min_pu_width;

    MvField *tab_mvf = s->ref->tab_mvf;
    RefPicList  *refPicList = s->ref->refPicList;
    HEVCFrame *ref0, *ref1;

    int tmpstride = MAX_PB_SIZE;

    uint8_t *dst0 = POS(0, x0, y0);
    uint8_t *dst1 = POS(1, x0, y0);
    uint8_t *dst2 = POS(2, x0, y0);
    int log2_min_cb_size = s->sps->log2_min_cb_size;
    int min_cb_width     = s->sps->min_cb_width;
    int x_cb             = x0 >> log2_min_cb_size;
    int y_cb             = y0 >> log2_min_cb_size;
    int ref_idx[2];
    int mvp_flag[2];
    int x_pu, y_pu;
    int i, j;

    if (SAMPLE_CTB(s->skip_flag, x_cb, y_cb)) {
        if (s->sh.max_num_merge_cand > 1)
            merge_idx = ff_hevc_merge_idx_decode(s);
        else
            merge_idx = 0;

        ff_hevc_luma_mv_merge_mode(s, x0, y0,
                                   1 << log2_cb_size,
                                   1 << log2_cb_size,
                                   log2_cb_size, partIdx,
                                   merge_idx, &current_mv);
        x_pu = x0 >> s->sps->log2_min_pu_size;
        y_pu = y0 >> s->sps->log2_min_pu_size;

        for (i = 0; i < nPbW >> s->sps->log2_min_pu_size; i++)
            for (j = 0; j < nPbH >> s->sps->log2_min_pu_size; j++)
                tab_mvf[(y_pu + j) * min_pu_width + x_pu + i] = current_mv;
    } else { /* MODE_INTER */
        lc->pu.merge_flag = ff_hevc_merge_flag_decode(s);
        if (lc->pu.merge_flag) {
            if (s->sh.max_num_merge_cand > 1)
                merge_idx = ff_hevc_merge_idx_decode(s);
            else
                merge_idx = 0;

            ff_hevc_luma_mv_merge_mode(s, x0, y0, nPbW, nPbH, log2_cb_size,
                                       partIdx, merge_idx, &current_mv);
            x_pu = x0 >> s->sps->log2_min_pu_size;
            y_pu = y0 >> s->sps->log2_min_pu_size;

            for (i = 0; i < nPbW >> s->sps->log2_min_pu_size; i++)
                for (j = 0; j < nPbH >> s->sps->log2_min_pu_size; j++)
                    tab_mvf[(y_pu + j) * min_pu_width + x_pu + i] = current_mv;
        } else {
            enum InterPredIdc inter_pred_idc = PRED_L0;
            ff_hevc_set_neighbour_available(s, x0, y0, nPbW, nPbH);
            if (s->sh.slice_type == B_SLICE)
                inter_pred_idc = ff_hevc_inter_pred_idc_decode(s, nPbW, nPbH);

            if (inter_pred_idc != PRED_L1) {
                if (s->sh.nb_refs[L0]) {
                    ref_idx[0] = ff_hevc_ref_idx_lx_decode(s, s->sh.nb_refs[L0]);
                    current_mv.ref_idx[0] = ref_idx[0];
                }
                current_mv.pred_flag[0] = 1;
                ff_hevc_hls_mvd_coding(s, x0, y0, 0);
                mvp_flag[0] = ff_hevc_mvp_lx_flag_decode(s);
                ff_hevc_luma_mv_mvp_mode(s, x0, y0, nPbW, nPbH, log2_cb_size,
                                         partIdx, merge_idx, &current_mv,
                                         mvp_flag[0], 0);
                current_mv.mv[0].x += lc->pu.mvd.x;
                current_mv.mv[0].y += lc->pu.mvd.y;
            }

            if (inter_pred_idc != PRED_L0) {
                if (s->sh.nb_refs[L1]) {
                    ref_idx[1] = ff_hevc_ref_idx_lx_decode(s, s->sh.nb_refs[L1]);
                    current_mv.ref_idx[1] = ref_idx[1];
                }

                if (s->sh.mvd_l1_zero_flag == 1 && inter_pred_idc == PRED_BI) {
                    lc->pu.mvd.x = 0;
                    lc->pu.mvd.y = 0;
                } else {
                    ff_hevc_hls_mvd_coding(s, x0, y0, 1);
                }

                current_mv.pred_flag[1] = 1;
                mvp_flag[1] = ff_hevc_mvp_lx_flag_decode(s);
                ff_hevc_luma_mv_mvp_mode(s, x0, y0, nPbW, nPbH, log2_cb_size,
                                         partIdx, merge_idx, &current_mv,
                                         mvp_flag[1], 1);
                current_mv.mv[1].x += lc->pu.mvd.x;
                current_mv.mv[1].y += lc->pu.mvd.y;
            }

            x_pu = x0 >> s->sps->log2_min_pu_size;
            y_pu = y0 >> s->sps->log2_min_pu_size;

            for (i = 0; i < nPbW >> s->sps->log2_min_pu_size; i++)
                for(j = 0; j < nPbH >> s->sps->log2_min_pu_size; j++)
                    tab_mvf[(y_pu + j) * min_pu_width + x_pu + i] = current_mv;
        }
    }

    if (current_mv.pred_flag[0]) {
        ref0 = refPicList[0].ref[current_mv.ref_idx[0]];
        if (!ref0)
            return;
        hevc_await_progress(s, ref0, &current_mv.mv[0], y0, nPbH);
    }
    if (current_mv.pred_flag[1]) {
        ref1 = refPicList[1].ref[current_mv.ref_idx[1]];
        if (!ref1)
            return;
        hevc_await_progress(s, ref1, &current_mv.mv[1], y0, nPbH);
    }

    if (current_mv.pred_flag[0] && !current_mv.pred_flag[1]) {
        DECLARE_ALIGNED(16, int16_t,  tmp[MAX_PB_SIZE * MAX_PB_SIZE]);
        DECLARE_ALIGNED(16, int16_t, tmp2[MAX_PB_SIZE * MAX_PB_SIZE]);

        luma_mc(s, tmp, tmpstride, ref0->frame,
                &current_mv.mv[0], x0, y0, nPbW, nPbH);

        if ((s->sh.slice_type == P_SLICE && s->pps->weighted_pred_flag) ||
            (s->sh.slice_type == B_SLICE && s->pps->weighted_bipred_flag)) {
            s->hevcdsp.weighted_pred(s->sh.luma_log2_weight_denom,
                                     s->sh.luma_weight_l0[current_mv.ref_idx[0]],
                                     s->sh.luma_offset_l0[current_mv.ref_idx[0]],
                                     dst0, s->frame->linesize[0], tmp,
                                     tmpstride, nPbW, nPbH);
        } else {
            s->hevcdsp.put_unweighted_pred(dst0, s->frame->linesize[0], tmp, tmpstride, nPbW, nPbH);
        }
        chroma_mc(s, tmp, tmp2, tmpstride, ref0->frame,
                  &current_mv.mv[0], x0 / 2, y0 / 2, nPbW / 2, nPbH / 2);

        if ((s->sh.slice_type == P_SLICE && s->pps->weighted_pred_flag) ||
            (s->sh.slice_type == B_SLICE && s->pps->weighted_bipred_flag)) {
            s->hevcdsp.weighted_pred(s->sh.chroma_log2_weight_denom,
                                     s->sh.chroma_weight_l0[current_mv.ref_idx[0]][0],
                                     s->sh.chroma_offset_l0[current_mv.ref_idx[0]][0],
                                     dst1, s->frame->linesize[1], tmp, tmpstride,
                                     nPbW / 2, nPbH / 2);
            s->hevcdsp.weighted_pred(s->sh.chroma_log2_weight_denom,
                                     s->sh.chroma_weight_l0[current_mv.ref_idx[0]][1],
                                     s->sh.chroma_offset_l0[current_mv.ref_idx[0]][1],
                                     dst2, s->frame->linesize[2], tmp2, tmpstride,
                                     nPbW / 2, nPbH / 2);
        } else {
            s->hevcdsp.put_unweighted_pred(dst1, s->frame->linesize[1], tmp, tmpstride, nPbW/2, nPbH/2);
            s->hevcdsp.put_unweighted_pred(dst2, s->frame->linesize[2], tmp2, tmpstride, nPbW/2, nPbH/2);
        }
    } else if (!current_mv.pred_flag[0] && current_mv.pred_flag[1]) {
        DECLARE_ALIGNED(16, int16_t, tmp [MAX_PB_SIZE * MAX_PB_SIZE]);
        DECLARE_ALIGNED(16, int16_t, tmp2[MAX_PB_SIZE * MAX_PB_SIZE]);

        if (!ref1)
            return;

        luma_mc(s, tmp, tmpstride, ref1->frame,
                &current_mv.mv[1], x0, y0, nPbW, nPbH);

        if ((s->sh.slice_type == P_SLICE && s->pps->weighted_pred_flag) ||
            (s->sh.slice_type == B_SLICE && s->pps->weighted_bipred_flag)) {
            s->hevcdsp.weighted_pred(s->sh.luma_log2_weight_denom,
                                      s->sh.luma_weight_l1[current_mv.ref_idx[1]],
                                      s->sh.luma_offset_l1[current_mv.ref_idx[1]],
                                      dst0, s->frame->linesize[0], tmp, tmpstride,
                                      nPbW, nPbH);
        } else {
            s->hevcdsp.put_unweighted_pred(dst0, s->frame->linesize[0], tmp, tmpstride, nPbW, nPbH);
        }

        chroma_mc(s, tmp, tmp2, tmpstride, ref1->frame,
                  &current_mv.mv[1], x0/2, y0/2, nPbW/2, nPbH/2);

        if ((s->sh.slice_type == P_SLICE && s->pps->weighted_pred_flag) ||
            (s->sh.slice_type == B_SLICE && s->pps->weighted_bipred_flag)) {
            s->hevcdsp.weighted_pred(s->sh.chroma_log2_weight_denom,
                                     s->sh.chroma_weight_l1[current_mv.ref_idx[1]][0],
                                     s->sh.chroma_offset_l1[current_mv.ref_idx[1]][0],
                                     dst1, s->frame->linesize[1], tmp, tmpstride, nPbW/2, nPbH/2);
            s->hevcdsp.weighted_pred(s->sh.chroma_log2_weight_denom,
                                     s->sh.chroma_weight_l1[current_mv.ref_idx[1]][1],
                                     s->sh.chroma_offset_l1[current_mv.ref_idx[1]][1],
                                     dst2, s->frame->linesize[2], tmp2, tmpstride, nPbW/2, nPbH/2);
        } else {
            s->hevcdsp.put_unweighted_pred(dst1, s->frame->linesize[1], tmp, tmpstride, nPbW/2, nPbH/2);
            s->hevcdsp.put_unweighted_pred(dst2, s->frame->linesize[2], tmp2, tmpstride, nPbW/2, nPbH/2);
        }
    } else if (current_mv.pred_flag[0] && current_mv.pred_flag[1]) {
        DECLARE_ALIGNED(16, int16_t, tmp [MAX_PB_SIZE * MAX_PB_SIZE]);
        DECLARE_ALIGNED(16, int16_t, tmp2[MAX_PB_SIZE * MAX_PB_SIZE]);
        DECLARE_ALIGNED(16, int16_t, tmp3[MAX_PB_SIZE * MAX_PB_SIZE]);
        DECLARE_ALIGNED(16, int16_t, tmp4[MAX_PB_SIZE * MAX_PB_SIZE]);
        HEVCFrame *ref0 = refPicList[0].ref[current_mv.ref_idx[0]];
        HEVCFrame *ref1 = refPicList[1].ref[current_mv.ref_idx[1]];

        if (!ref0 || !ref1)
            return;

        luma_mc(s, tmp, tmpstride, ref0->frame,
                &current_mv.mv[0], x0, y0, nPbW, nPbH);
        luma_mc(s, tmp2, tmpstride, ref1->frame,
                &current_mv.mv[1], x0, y0, nPbW, nPbH);

        if ((s->sh.slice_type == P_SLICE && s->pps->weighted_pred_flag) ||
            (s->sh.slice_type == B_SLICE && s->pps->weighted_bipred_flag)) {
            s->hevcdsp.weighted_pred_avg(s->sh.luma_log2_weight_denom,
                                         s->sh.luma_weight_l0[current_mv.ref_idx[0]],
                                         s->sh.luma_weight_l1[current_mv.ref_idx[1]],
                                         s->sh.luma_offset_l0[current_mv.ref_idx[0]],
                                         s->sh.luma_offset_l1[current_mv.ref_idx[1]],
                                         dst0, s->frame->linesize[0],
                                         tmp, tmp2, tmpstride, nPbW, nPbH);
        } else {
            s->hevcdsp.put_weighted_pred_avg(dst0, s->frame->linesize[0],
                                             tmp, tmp2, tmpstride, nPbW, nPbH);
        }

        chroma_mc(s, tmp, tmp2, tmpstride, ref0->frame,
                  &current_mv.mv[0], x0 / 2, y0 / 2, nPbW / 2, nPbH / 2);
        chroma_mc(s, tmp3, tmp4, tmpstride, ref1->frame,
                  &current_mv.mv[1], x0 / 2, y0 / 2, nPbW / 2, nPbH / 2);

        if ((s->sh.slice_type == P_SLICE && s->pps->weighted_pred_flag) ||
            (s->sh.slice_type == B_SLICE && s->pps->weighted_bipred_flag)) {
            s->hevcdsp.weighted_pred_avg(s->sh.chroma_log2_weight_denom,
                                         s->sh.chroma_weight_l0[current_mv.ref_idx[0]][0],
                                         s->sh.chroma_weight_l1[current_mv.ref_idx[1]][0],
                                         s->sh.chroma_offset_l0[current_mv.ref_idx[0]][0],
                                         s->sh.chroma_offset_l1[current_mv.ref_idx[1]][0],
                                         dst1, s->frame->linesize[1], tmp, tmp3,
                                         tmpstride, nPbW / 2, nPbH / 2);
            s->hevcdsp.weighted_pred_avg(s->sh.chroma_log2_weight_denom,
                                         s->sh.chroma_weight_l0[current_mv.ref_idx[0]][1],
                                         s->sh.chroma_weight_l1[current_mv.ref_idx[1]][1],
                                         s->sh.chroma_offset_l0[current_mv.ref_idx[0]][1],
                                         s->sh.chroma_offset_l1[current_mv.ref_idx[1]][1],
                                         dst2, s->frame->linesize[2], tmp2, tmp4,
                                         tmpstride, nPbW / 2, nPbH / 2);
        } else {
            s->hevcdsp.put_weighted_pred_avg(dst1, s->frame->linesize[1], tmp, tmp3, tmpstride, nPbW/2, nPbH/2);
            s->hevcdsp.put_weighted_pred_avg(dst2, s->frame->linesize[2], tmp2, tmp4, tmpstride, nPbW/2, nPbH/2);
        }
    }
}

/**
 * 8.4.1
 */
static int luma_intra_pred_mode(HEVCContext *s, int x0, int y0, int pu_size,
                                int prev_intra_luma_pred_flag)
{
    HEVCLocalContext *lc = s->HEVClc;
    int x_pu             = x0 >> s->sps->log2_min_pu_size;
    int y_pu             = y0 >> s->sps->log2_min_pu_size;
    int min_pu_width     = s->sps->min_pu_width;
    int size_in_pus      = pu_size >> s->sps->log2_min_pu_size;
    int x0b              = x0 & ((1 << s->sps->log2_ctb_size) - 1);
    int y0b              = y0 & ((1 << s->sps->log2_ctb_size) - 1);

    int cand_up   = (lc->ctb_up_flag || y0b) ?
                    s->tab_ipm[(y_pu - 1) * min_pu_width + x_pu] : INTRA_DC;
    int cand_left = (lc->ctb_left_flag || x0b) ?
                    s->tab_ipm[y_pu * min_pu_width + x_pu - 1]   : INTRA_DC;

    int y_ctb = (y0 >> (s->sps->log2_ctb_size)) << (s->sps->log2_ctb_size);

    MvField *tab_mvf = s->ref->tab_mvf;
    int intra_pred_mode;
    int candidate[3];
    int i, j;

    // intra_pred_mode prediction does not cross vertical CTB boundaries
    if ((y0 - 1) < y_ctb)
        cand_up = INTRA_DC;

    if (cand_left == cand_up) {
        if (cand_left < 2) {
            candidate[0] = INTRA_PLANAR;
            candidate[1] = INTRA_DC;
            candidate[2] = INTRA_ANGULAR_26;
        } else {
            candidate[0] = cand_left;
            candidate[1] = 2 + ((cand_left - 2 - 1 + 32) & 31);
            candidate[2] = 2 + ((cand_left - 2 + 1) & 31);
        }
    } else {
        candidate[0] = cand_left;
        candidate[1] = cand_up;
        if (candidate[0] != INTRA_PLANAR && candidate[1] != INTRA_PLANAR) {
            candidate[2] = INTRA_PLANAR;
        } else if (candidate[0] != INTRA_DC && candidate[1] != INTRA_DC) {
            candidate[2] = INTRA_DC;
        } else {
            candidate[2] = INTRA_ANGULAR_26;
        }
    }

    if (prev_intra_luma_pred_flag) {
        intra_pred_mode = candidate[lc->pu.mpm_idx];
    } else {
        if (candidate[0] > candidate[1])
            FFSWAP(uint8_t, candidate[0], candidate[1]);
        if (candidate[0] > candidate[2])
            FFSWAP(uint8_t, candidate[0], candidate[2]);
        if (candidate[1] > candidate[2])
            FFSWAP(uint8_t, candidate[1], candidate[2]);

        intra_pred_mode = lc->pu.rem_intra_luma_pred_mode;
        for (i = 0; i < 3; i++)
            if (intra_pred_mode >= candidate[i])
                intra_pred_mode++;
    }

    /* write the intra prediction units into the mv array */
    if (!size_in_pus)
        size_in_pus = 1;
    for (i = 0; i < size_in_pus; i++) {
        memset(&s->tab_ipm[(y_pu + i) * min_pu_width + x_pu],
               intra_pred_mode, size_in_pus);

        for (j = 0; j < size_in_pus; j++) {
            tab_mvf[(y_pu + j) * min_pu_width + x_pu + i].is_intra     = 1;
            tab_mvf[(y_pu + j) * min_pu_width + x_pu + i].pred_flag[0] = 0;
            tab_mvf[(y_pu + j) * min_pu_width + x_pu + i].pred_flag[1] = 0;
            tab_mvf[(y_pu + j) * min_pu_width + x_pu + i].ref_idx[0]   = 0;
            tab_mvf[(y_pu + j) * min_pu_width + x_pu + i].ref_idx[1]   = 0;
            tab_mvf[(y_pu + j) * min_pu_width + x_pu + i].mv[0].x      = 0;
            tab_mvf[(y_pu + j) * min_pu_width + x_pu + i].mv[0].y      = 0;
            tab_mvf[(y_pu + j) * min_pu_width + x_pu + i].mv[1].x      = 0;
            tab_mvf[(y_pu + j) * min_pu_width + x_pu + i].mv[1].y      = 0;
        }
    }

    return intra_pred_mode;
}

static av_always_inline void set_ct_depth(HEVCContext *s, int x0, int y0,
                                          int log2_cb_size, int ct_depth)
{
    int length = (1 << log2_cb_size) >> s->sps->log2_min_cb_size;
    int x_cb   = x0 >> s->sps->log2_min_cb_size;
    int y_cb   = y0 >> s->sps->log2_min_cb_size;
    int y;

    for (y = 0; y < length; y++)
        memset(&s->tab_ct_depth[(y_cb + y) * s->sps->min_cb_width + x_cb],
               ct_depth, length);
}

static void intra_prediction_unit(HEVCContext *s, int x0, int y0,
                                  int log2_cb_size)
{
    HEVCLocalContext *lc = s->HEVClc;
    static const uint8_t intra_chroma_table[4] = { 0, 26, 10, 1 };
    uint8_t prev_intra_luma_pred_flag[4];
    int split   = lc->cu.part_mode == PART_NxN;
    int pb_size = (1 << log2_cb_size) >> split;
    int side    = split + 1;
    int chroma_mode;
    int i, j;

    for (i = 0; i < side; i++)
        for (j = 0; j < side; j++)
            prev_intra_luma_pred_flag[2 * i + j] = ff_hevc_prev_intra_luma_pred_flag_decode(s);

    for (i = 0; i < side; i++) {
        for (j = 0; j < side; j++) {
            if (prev_intra_luma_pred_flag[2 * i + j])
                lc->pu.mpm_idx = ff_hevc_mpm_idx_decode(s);
            else
                lc->pu.rem_intra_luma_pred_mode = ff_hevc_rem_intra_luma_pred_mode_decode(s);

            lc->pu.intra_pred_mode[2 * i + j] =
                luma_intra_pred_mode(s, x0 + pb_size * j, y0 + pb_size * i, pb_size,
                                     prev_intra_luma_pred_flag[2 * i + j]);
        }
    }

    chroma_mode = ff_hevc_intra_chroma_pred_mode_decode(s);
    if (chroma_mode != 4) {
        if (lc->pu.intra_pred_mode[0] == intra_chroma_table[chroma_mode])
            lc->pu.intra_pred_mode_c = 34;
        else
            lc->pu.intra_pred_mode_c = intra_chroma_table[chroma_mode];
    } else {
        lc->pu.intra_pred_mode_c = lc->pu.intra_pred_mode[0];
    }
}

static void intra_prediction_unit_default_value(HEVCContext *s,
                                                int x0, int y0,
                                                int log2_cb_size)
{
    HEVCLocalContext *lc = s->HEVClc;
    int pb_size          = 1 << log2_cb_size;
    int size_in_pus      = pb_size >> s->sps->log2_min_pu_size;
    int min_pu_width     = s->sps->min_pu_width;
    MvField *tab_mvf     = s->ref->tab_mvf;
    int x_pu             = x0 >> s->sps->log2_min_pu_size;
    int y_pu             = y0 >> s->sps->log2_min_pu_size;
    int j, k;

    if (size_in_pus == 0)
        size_in_pus = 1;
    for (j = 0; j < size_in_pus; j++) {
        memset(&s->tab_ipm[(y_pu + j) * min_pu_width + x_pu], INTRA_DC, size_in_pus);
        for (k = 0; k < size_in_pus; k++)
            tab_mvf[(y_pu + j) * min_pu_width + x_pu + k].is_intra = lc->cu.pred_mode == MODE_INTRA;
    }
}

static int hls_coding_unit(HEVCContext *s, int x0, int y0, int log2_cb_size)
{
    int cb_size          = 1 << log2_cb_size;
    HEVCLocalContext *lc = s->HEVClc;
    int log2_min_cb_size = s->sps->log2_min_cb_size;
    int length           = cb_size >> log2_min_cb_size;
    int min_cb_width     = s->sps->min_cb_width;
    int x_cb             = x0 >> log2_min_cb_size;
    int y_cb             = y0 >> log2_min_cb_size;
    int x, y, ret;
    int qp_block_mask = (1<<(s->sps->log2_ctb_size - s->pps->diff_cu_qp_delta_depth)) - 1;

    lc->cu.x                = x0;
    lc->cu.y                = y0;
    lc->cu.rqt_root_cbf     = 1;
    lc->cu.pred_mode        = MODE_INTRA;
    lc->cu.part_mode        = PART_2Nx2N;
    lc->cu.intra_split_flag = 0;
    lc->cu.pcm_flag         = 0;

    SAMPLE_CTB(s->skip_flag, x_cb, y_cb) = 0;
    for (x = 0; x < 4; x++)
        lc->pu.intra_pred_mode[x] = 1;
    if (s->pps->transquant_bypass_enable_flag) {
        lc->cu.cu_transquant_bypass_flag = ff_hevc_cu_transquant_bypass_flag_decode(s);
        if (lc->cu.cu_transquant_bypass_flag)
            set_deblocking_bypass(s, x0, y0, log2_cb_size);
    } else
        lc->cu.cu_transquant_bypass_flag = 0;

    if (s->sh.slice_type != I_SLICE) {
        uint8_t skip_flag = ff_hevc_skip_flag_decode(s, x0, y0, x_cb, y_cb);

        lc->cu.pred_mode = MODE_SKIP;
        x = y_cb * min_cb_width + x_cb;
        for (y = 0; y < length; y++) {
            memset(&s->skip_flag[x], skip_flag, length);
            x += min_cb_width;
        }
        lc->cu.pred_mode = skip_flag ? MODE_SKIP : MODE_INTER;
    }

    if (SAMPLE_CTB(s->skip_flag, x_cb, y_cb)) {
        hls_prediction_unit(s, x0, y0, cb_size, cb_size, log2_cb_size, 0);
        intra_prediction_unit_default_value(s, x0, y0, log2_cb_size);

        if (!s->sh.disable_deblocking_filter_flag)
            ff_hevc_deblocking_boundary_strengths(s, x0, y0, log2_cb_size,
                                                  lc->slice_or_tiles_up_boundary,
                                                  lc->slice_or_tiles_left_boundary);
    } else {
        if (s->sh.slice_type != I_SLICE)
            lc->cu.pred_mode = ff_hevc_pred_mode_decode(s);
        if (lc->cu.pred_mode != MODE_INTRA ||
            log2_cb_size == s->sps->log2_min_cb_size) {
            lc->cu.part_mode        = ff_hevc_part_mode_decode(s, log2_cb_size);
            lc->cu.intra_split_flag = lc->cu.part_mode == PART_NxN &&
                                      lc->cu.pred_mode == MODE_INTRA;
        }

        if (lc->cu.pred_mode == MODE_INTRA) {
            if (lc->cu.part_mode == PART_2Nx2N && s->sps->pcm_enabled_flag &&
                log2_cb_size >= s->sps->pcm.log2_min_pcm_cb_size &&
                log2_cb_size <= s->sps->pcm.log2_max_pcm_cb_size) {
                lc->cu.pcm_flag = ff_hevc_pcm_flag_decode(s);
            }
            if (lc->cu.pcm_flag) {
                intra_prediction_unit_default_value(s, x0, y0, log2_cb_size);
                ret = hls_pcm_sample(s, x0, y0, log2_cb_size);
                if (s->sps->pcm.loop_filter_disable_flag)
                    set_deblocking_bypass(s, x0, y0, log2_cb_size);

                if (ret < 0)
                    return ret;
            } else {
                intra_prediction_unit(s, x0, y0, log2_cb_size);
            }
        } else {
            intra_prediction_unit_default_value(s, x0, y0, log2_cb_size);
            switch (lc->cu.part_mode) {
            case PART_2Nx2N:
                hls_prediction_unit(s, x0, y0, cb_size, cb_size, log2_cb_size, 0);
                break;
            case PART_2NxN:
                hls_prediction_unit(s, x0, y0,               cb_size, cb_size / 2, log2_cb_size, 0);
                hls_prediction_unit(s, x0, y0 + cb_size / 2, cb_size, cb_size / 2, log2_cb_size, 1);
                break;
            case PART_Nx2N:
                hls_prediction_unit(s, x0,               y0, cb_size / 2, cb_size, log2_cb_size, 0);
                hls_prediction_unit(s, x0 + cb_size / 2, y0, cb_size / 2, cb_size, log2_cb_size, 1);
                break;
            case PART_2NxnU:
                hls_prediction_unit(s, x0, y0,               cb_size, cb_size     / 4, log2_cb_size, 0);
                hls_prediction_unit(s, x0, y0 + cb_size / 4, cb_size, cb_size * 3 / 4, log2_cb_size, 1);
                break;
            case PART_2NxnD:
                hls_prediction_unit(s, x0, y0,                   cb_size, cb_size * 3 / 4, log2_cb_size, 0);
                hls_prediction_unit(s, x0, y0 + cb_size * 3 / 4, cb_size, cb_size     / 4, log2_cb_size, 1);
                break;
            case PART_nLx2N:
                hls_prediction_unit(s, x0,               y0, cb_size     / 4, cb_size, log2_cb_size, 0);
                hls_prediction_unit(s, x0 + cb_size / 4, y0, cb_size * 3 / 4, cb_size, log2_cb_size, 1);
                break;
            case PART_nRx2N:
                hls_prediction_unit(s, x0,                   y0, cb_size * 3 / 4, cb_size, log2_cb_size, 0);
                hls_prediction_unit(s, x0 + cb_size * 3 / 4, y0, cb_size     / 4, cb_size, log2_cb_size, 1);
                break;
            case PART_NxN:
                hls_prediction_unit(s, x0,               y0,               cb_size / 2, cb_size / 2, log2_cb_size, 0);
                hls_prediction_unit(s, x0 + cb_size / 2, y0,               cb_size / 2, cb_size / 2, log2_cb_size, 1);
                hls_prediction_unit(s, x0,               y0 + cb_size / 2, cb_size / 2, cb_size / 2, log2_cb_size, 2);
                hls_prediction_unit(s, x0 + cb_size / 2, y0 + cb_size / 2, cb_size / 2, cb_size / 2, log2_cb_size, 3);
                break;
            }
        }

        if (!lc->cu.pcm_flag) {
            if (lc->cu.pred_mode != MODE_INTRA &&
                !(lc->cu.part_mode == PART_2Nx2N && lc->pu.merge_flag)) {
                lc->cu.rqt_root_cbf = ff_hevc_no_residual_syntax_flag_decode(s);
            }
            if (lc->cu.rqt_root_cbf) {
                lc->cu.max_trafo_depth = lc->cu.pred_mode == MODE_INTRA ?
                                         s->sps->max_transform_hierarchy_depth_intra + lc->cu.intra_split_flag :
                                         s->sps->max_transform_hierarchy_depth_inter;
                ret = hls_transform_tree(s, x0, y0, x0, y0, x0, y0,
                                         log2_cb_size,
                                         log2_cb_size, 0, 0);
                if (ret < 0)
                    return ret;
            } else {
                if (!s->sh.disable_deblocking_filter_flag)
                    ff_hevc_deblocking_boundary_strengths(s, x0, y0, log2_cb_size,
                                                          lc->slice_or_tiles_up_boundary,
                                                          lc->slice_or_tiles_left_boundary);
            }
        }
    }

    if (s->pps->cu_qp_delta_enabled_flag && lc->tu.is_cu_qp_delta_coded == 0)
        ff_hevc_set_qPy(s, x0, y0, x0, y0, log2_cb_size);

    x = y_cb * min_cb_width + x_cb;
    for (y = 0; y < length; y++) {
        memset(&s->qp_y_tab[x], lc->qp_y, length);
        x += min_cb_width;
    }

    if(((x0 + (1<<log2_cb_size)) & qp_block_mask) == 0 &&
       ((y0 + (1<<log2_cb_size)) & qp_block_mask) == 0) {
        lc->qPy_pred = lc->qp_y;
    }

    set_ct_depth(s, x0, y0, log2_cb_size, lc->ct.depth);

    return 0;
}

static int hls_coding_quadtree(HEVCContext *s, int x0, int y0,
                               int log2_cb_size, int cb_depth)
{
    HEVCLocalContext *lc = s->HEVClc;
    const int cb_size    = 1 << log2_cb_size;
    int ret;
    int qp_block_mask = (1<<(s->sps->log2_ctb_size - s->pps->diff_cu_qp_delta_depth)) - 1;

    lc->ct.depth = cb_depth;
    if (x0 + cb_size <= s->sps->width  &&
        y0 + cb_size <= s->sps->height &&
        log2_cb_size > s->sps->log2_min_cb_size) {
        SAMPLE(s->split_cu_flag, x0, y0) =
            ff_hevc_split_coding_unit_flag_decode(s, cb_depth, x0, y0);
    } else {
        SAMPLE(s->split_cu_flag, x0, y0) =
            (log2_cb_size > s->sps->log2_min_cb_size);
    }
    if (s->pps->cu_qp_delta_enabled_flag &&
        log2_cb_size >= s->sps->log2_ctb_size - s->pps->diff_cu_qp_delta_depth) {
        lc->tu.is_cu_qp_delta_coded = 0;
        lc->tu.cu_qp_delta          = 0;
    }

    if (SAMPLE(s->split_cu_flag, x0, y0)) {
        const int cb_size_split = cb_size >> 1;
        const int x1 = x0 + cb_size_split;
        const int y1 = y0 + cb_size_split;

        int more_data = 0;

        more_data = hls_coding_quadtree(s, x0, y0, log2_cb_size - 1, cb_depth + 1);
        if (more_data < 0)
            return more_data;

        if (more_data && x1 < s->sps->width) {
            more_data = hls_coding_quadtree(s, x1, y0, log2_cb_size - 1, cb_depth + 1);
            if (more_data < 0)
                return more_data;
        }
        if (more_data && y1 < s->sps->height) {
            more_data = hls_coding_quadtree(s, x0, y1, log2_cb_size - 1, cb_depth + 1);
            if (more_data < 0)
                return more_data;
        }
        if (more_data && x1 < s->sps->width &&
            y1 < s->sps->height) {
            more_data = hls_coding_quadtree(s, x1, y1, log2_cb_size - 1, cb_depth + 1);
            if (more_data < 0)
                return more_data;
        }

        if(((x0 + (1<<log2_cb_size)) & qp_block_mask) == 0 &&
            ((y0 + (1<<log2_cb_size)) & qp_block_mask) == 0)
            lc->qPy_pred = lc->qp_y;

        if (more_data)
            return ((x1 + cb_size_split) < s->sps->width ||
                    (y1 + cb_size_split) < s->sps->height);
        else
            return 0;
    } else {
        ret = hls_coding_unit(s, x0, y0, log2_cb_size);
        if (ret < 0)
            return ret;
        if ((!((x0 + cb_size) %
               (1 << (s->sps->log2_ctb_size))) ||
             (x0 + cb_size >= s->sps->width)) &&
            (!((y0 + cb_size) %
               (1 << (s->sps->log2_ctb_size))) ||
             (y0 + cb_size >= s->sps->height))) {
            int end_of_slice_flag = ff_hevc_end_of_slice_flag_decode(s);
            return !end_of_slice_flag;
        } else {
            return 1;
        }
    }

    return 0;
}

static void hls_decode_neighbour(HEVCContext *s, int x_ctb, int y_ctb,
                                 int ctb_addr_ts)
{
    HEVCLocalContext *lc  = s->HEVClc;
    int ctb_size          = 1 << s->sps->log2_ctb_size;
    int ctb_addr_rs       = s->pps->ctb_addr_ts_to_rs[ctb_addr_ts];
    int ctb_addr_in_slice = ctb_addr_rs - s->sh.slice_addr;

    int tile_left_boundary, tile_up_boundary;
    int slice_left_boundary, slice_up_boundary;

    s->tab_slice_address[ctb_addr_rs] = s->sh.slice_addr;

    if (s->pps->entropy_coding_sync_enabled_flag) {
        if (x_ctb == 0 && (y_ctb & (ctb_size - 1)) == 0)
            lc->first_qp_group = 1;
        lc->end_of_tiles_x = s->sps->width;
    } else if (s->pps->tiles_enabled_flag) {
        if (ctb_addr_ts && s->pps->tile_id[ctb_addr_ts] != s->pps->tile_id[ctb_addr_ts - 1]) {
            int idxX = s->pps->col_idxX[x_ctb >> s->sps->log2_ctb_size];
            lc->end_of_tiles_x   = x_ctb + (s->pps->column_width[idxX] << s->sps->log2_ctb_size);
            lc->first_qp_group   = 1;
        }
    } else {
        lc->end_of_tiles_x = s->sps->width;
    }

    lc->end_of_tiles_y = FFMIN(y_ctb + ctb_size, s->sps->height);

    if (s->pps->tiles_enabled_flag) {
        tile_left_boundary = x_ctb > 0 &&
                             s->pps->tile_id[ctb_addr_ts] != s->pps->tile_id[s->pps->ctb_addr_rs_to_ts[ctb_addr_rs-1]];
        slice_left_boundary = x_ctb > 0 &&
                              s->tab_slice_address[ctb_addr_rs] != s->tab_slice_address[ctb_addr_rs - 1];
        tile_up_boundary  = y_ctb > 0 &&
                            s->pps->tile_id[ctb_addr_ts] != s->pps->tile_id[s->pps->ctb_addr_rs_to_ts[ctb_addr_rs - s->sps->ctb_width]];
        slice_up_boundary = y_ctb > 0 &&
                            s->tab_slice_address[ctb_addr_rs] != s->tab_slice_address[ctb_addr_rs - s->sps->ctb_width];
    } else {
        tile_left_boundary =
        tile_up_boundary   = 0;
        slice_left_boundary = ctb_addr_in_slice <= 0;
        slice_up_boundary   = ctb_addr_in_slice < s->sps->ctb_width;
    }
    lc->slice_or_tiles_left_boundary = slice_left_boundary + (tile_left_boundary << 1);
    lc->slice_or_tiles_up_boundary   = slice_up_boundary   + (tile_up_boundary   << 1);
    lc->ctb_left_flag = ((x_ctb > 0) && (ctb_addr_in_slice > 0)                  && !tile_left_boundary);
    lc->ctb_up_flag   = ((y_ctb > 0) && (ctb_addr_in_slice >= s->sps->ctb_width) && !tile_up_boundary);
    lc->ctb_up_right_flag = ((y_ctb > 0)                 && (ctb_addr_in_slice+1 >= s->sps->ctb_width) && (s->pps->tile_id[ctb_addr_ts] == s->pps->tile_id[s->pps->ctb_addr_rs_to_ts[ctb_addr_rs+1 - s->sps->ctb_width]]));
    lc->ctb_up_left_flag  = ((x_ctb > 0) && (y_ctb > 0)  && (ctb_addr_in_slice-1 >= s->sps->ctb_width) && (s->pps->tile_id[ctb_addr_ts] == s->pps->tile_id[s->pps->ctb_addr_rs_to_ts[ctb_addr_rs-1 - s->sps->ctb_width]]));
}

static int hls_decode_entry(AVCodecContext *avctxt, void *isFilterThread)
{
    HEVCContext *s  = avctxt->priv_data;
    int ctb_size    = 1 << s->sps->log2_ctb_size;
    int more_data   = 1;
    int x_ctb       = 0;
    int y_ctb       = 0;
    int ctb_addr_ts = s->pps->ctb_addr_rs_to_ts[s->sh.slice_ctb_addr_rs];

    if (!ctb_addr_ts && s->sh.dependent_slice_segment_flag) {
        av_log(s->avctx, AV_LOG_ERROR, "Impossible initial tile.\n");
        return AVERROR_INVALIDDATA;
    }

    if (s->sh.dependent_slice_segment_flag) {
        int prev_rs = s->pps->ctb_addr_ts_to_rs[ctb_addr_ts - 1];
        if (s->tab_slice_address[prev_rs] != s->sh.slice_addr) {
            av_log(s->avctx, AV_LOG_ERROR, "Previous slice segment missing\n");
            return AVERROR_INVALIDDATA;
        }
    }

    while (more_data && ctb_addr_ts < s->sps->ctb_size) {
        int ctb_addr_rs = s->pps->ctb_addr_ts_to_rs[ctb_addr_ts];

        x_ctb = (ctb_addr_rs % ((s->sps->width + ctb_size - 1) >> s->sps->log2_ctb_size)) << s->sps->log2_ctb_size;
        y_ctb = (ctb_addr_rs / ((s->sps->width + ctb_size - 1) >> s->sps->log2_ctb_size)) << s->sps->log2_ctb_size;
        hls_decode_neighbour(s, x_ctb, y_ctb, ctb_addr_ts);

        ff_hevc_cabac_init(s, ctb_addr_ts);

        hls_sao_param(s, x_ctb >> s->sps->log2_ctb_size, y_ctb >> s->sps->log2_ctb_size);

        s->deblock[ctb_addr_rs].beta_offset = s->sh.beta_offset;
        s->deblock[ctb_addr_rs].tc_offset   = s->sh.tc_offset;
        s->filter_slice_edges[ctb_addr_rs]  = s->sh.slice_loop_filter_across_slices_enabled_flag;

        more_data = hls_coding_quadtree(s, x_ctb, y_ctb, s->sps->log2_ctb_size, 0);
        if (more_data < 0) {
            s->tab_slice_address[ctb_addr_rs] = -1;
            return more_data;
        }


        ctb_addr_ts++;
        ff_hevc_save_states(s, ctb_addr_ts);
        ff_hevc_hls_filters(s, x_ctb, y_ctb, ctb_size);
    }

    if (x_ctb + ctb_size >= s->sps->width &&
        y_ctb + ctb_size >= s->sps->height)
        ff_hevc_hls_filter(s, x_ctb, y_ctb);

    return ctb_addr_ts;
}

static int hls_slice_data(HEVCContext *s)
{
    int arg[2];
    int ret[2];

    arg[0] = 0;
    arg[1] = 1;

    s->avctx->execute(s->avctx, hls_decode_entry, arg, ret , 1, sizeof(int));
    return ret[0];
}
static int hls_decode_entry_wpp(AVCodecContext *avctxt, void *input_ctb_row, int job, int self_id)
{
    HEVCContext *s1  = avctxt->priv_data, *s;
    HEVCLocalContext *lc;
    int ctb_size    = 1<< s1->sps->log2_ctb_size;
    int more_data   = 1;
    int *ctb_row_p    = input_ctb_row;
    int ctb_row = ctb_row_p[job];
    int ctb_addr_rs = s1->sh.slice_ctb_addr_rs + ctb_row * ((s1->sps->width + ctb_size - 1) >> s1->sps->log2_ctb_size);
    int ctb_addr_ts = s1->pps->ctb_addr_rs_to_ts[ctb_addr_rs];
    int thread = ctb_row % s1->threads_number;
    int ret;

    s = s1->sList[self_id];
    lc = s->HEVClc;

    if(ctb_row) {
        ret = init_get_bits8(&lc->gb, s->data + s->sh.offset[ctb_row - 1], s->sh.size[ctb_row - 1]);

        if (ret < 0)
            return ret;
        ff_init_cabac_decoder(&lc->cc, s->data + s->sh.offset[(ctb_row)-1], s->sh.size[ctb_row - 1]);
    }

    while(more_data && ctb_addr_ts < s->sps->ctb_size) {
        int x_ctb = (ctb_addr_rs % s->sps->ctb_width) << s->sps->log2_ctb_size;
        int y_ctb = (ctb_addr_rs / s->sps->ctb_width) << s->sps->log2_ctb_size;

        hls_decode_neighbour(s, x_ctb, y_ctb, ctb_addr_ts);

        ff_thread_await_progress2(s->avctx, ctb_row, thread, SHIFT_CTB_WPP);

        if (avpriv_atomic_int_get(&s1->wpp_err)){
            ff_thread_report_progress2(s->avctx, ctb_row , thread, SHIFT_CTB_WPP);
            return 0;
        }

        ff_hevc_cabac_init(s, ctb_addr_ts);
        hls_sao_param(s, x_ctb >> s->sps->log2_ctb_size, y_ctb >> s->sps->log2_ctb_size);
        more_data = hls_coding_quadtree(s, x_ctb, y_ctb, s->sps->log2_ctb_size, 0);

        if (more_data < 0) {
            s->tab_slice_address[ctb_addr_rs] = -1;
            return more_data;
        }

        ctb_addr_ts++;

        ff_hevc_save_states(s, ctb_addr_ts);
        ff_thread_report_progress2(s->avctx, ctb_row, thread, 1);
        ff_hevc_hls_filters(s, x_ctb, y_ctb, ctb_size);

        if (!more_data && (x_ctb+ctb_size) < s->sps->width && ctb_row != s->sh.num_entry_point_offsets) {
            avpriv_atomic_int_set(&s1->wpp_err,  1);
            ff_thread_report_progress2(s->avctx, ctb_row ,thread, SHIFT_CTB_WPP);
            return 0;
        }

        if ((x_ctb+ctb_size) >= s->sps->width && (y_ctb+ctb_size) >= s->sps->height ) {
            ff_hevc_hls_filter(s, x_ctb, y_ctb);
            ff_thread_report_progress2(s->avctx, ctb_row , thread, SHIFT_CTB_WPP);
            return ctb_addr_ts;
        }
        ctb_addr_rs       = s->pps->ctb_addr_ts_to_rs[ctb_addr_ts];
        x_ctb+=ctb_size;

        if(x_ctb >= s->sps->width) {
            break;
        }
    }
    ff_thread_report_progress2(s->avctx, ctb_row ,thread, SHIFT_CTB_WPP);

    return 0;
}

static int hls_slice_data_wpp(HEVCContext *s, const uint8_t *nal, int length)
{
    HEVCLocalContext *lc = s->HEVClc;
    int *ret = av_malloc((s->sh.num_entry_point_offsets + 1) * sizeof(int));
    int *arg = av_malloc((s->sh.num_entry_point_offsets + 1) * sizeof(int));
    int offset;
    int startheader, cmpt = 0;
    int i, j, res = 0;


    if (!s->sList[1]) {
        ff_alloc_entries(s->avctx, s->sh.num_entry_point_offsets + 1);


        for (i = 1; i < s->threads_number; i++) {
            s->sList[i] = av_malloc(sizeof(HEVCContext));
            memcpy(s->sList[i], s, sizeof(HEVCContext));
            s->HEVClcList[i] = av_malloc(sizeof(HEVCLocalContext));
            s->sList[i]->HEVClc = s->HEVClcList[i];
        }
    }

    offset = (lc->gb.index >> 3);

    for (j = 0, cmpt = 0, startheader = offset + s->sh.entry_point_offset[0]; j < s->skipped_bytes; j++) {
        if (s->skipped_bytes_pos[j] >= offset && s->skipped_bytes_pos[j] < startheader) {
            startheader--;
            cmpt++;
        }
    }

    for (i = 1; i < s->sh.num_entry_point_offsets; i++) {
        offset += (s->sh.entry_point_offset[i - 1] - cmpt);
        for (j = 0, cmpt = 0, startheader = offset
             + s->sh.entry_point_offset[i]; j < s->skipped_bytes; j++) {
            if (s->skipped_bytes_pos[j] >= offset && s->skipped_bytes_pos[j] < startheader) {
                startheader--;
                cmpt++;
            }
        }
        s->sh.size[i - 1] = s->sh.entry_point_offset[i] - cmpt;
        s->sh.offset[i - 1] = offset;

    }
    if (s->sh.num_entry_point_offsets != 0) {
        offset += s->sh.entry_point_offset[s->sh.num_entry_point_offsets - 1] - cmpt;
        s->sh.size[s->sh.num_entry_point_offsets - 1] = length - offset;
        s->sh.offset[s->sh.num_entry_point_offsets - 1] = offset;

    }
    s->data = nal;

    for (i = 1; i < s->threads_number; i++) {
        s->sList[i]->HEVClc->first_qp_group = 1;
        s->sList[i]->HEVClc->qp_y = s->sList[0]->HEVClc->qp_y;
        memcpy(s->sList[i], s, sizeof(HEVCContext));
        s->sList[i]->HEVClc = s->HEVClcList[i];
    }

    avpriv_atomic_int_set(&s->wpp_err, 0);
    ff_reset_entries(s->avctx);

    for (i = 0; i <= s->sh.num_entry_point_offsets; i++) {
        arg[i] = i;
        ret[i] = 0;
    }

    if (s->pps->entropy_coding_sync_enabled_flag)
        s->avctx->execute2(s->avctx, (void *) hls_decode_entry_wpp, arg, ret, s->sh.num_entry_point_offsets + 1);

    for (i = 0; i <= s->sh.num_entry_point_offsets; i++)
        res += ret[i];
    av_free(ret);
    av_free(arg);
    return res;
}

/**
 * @return AVERROR_INVALIDDATA if the packet is not a valid NAL unit,
 * 0 if the unit should be skipped, 1 otherwise
 */
static int hls_nal_unit(HEVCContext *s)
{
    GetBitContext *gb = &s->HEVClc->gb;
    int nuh_layer_id;

    if (get_bits1(gb) != 0)
        return AVERROR_INVALIDDATA;

    s->nal_unit_type = get_bits(gb, 6);

    nuh_layer_id   = get_bits(gb, 6);
    s->temporal_id = get_bits(gb, 3) - 1;
    if (s->temporal_id < 0)
        return AVERROR_INVALIDDATA;

    av_log(s->avctx, AV_LOG_DEBUG,
           "nal_unit_type: %d, nuh_layer_id: %dtemporal_id: %d\n",
           s->nal_unit_type, nuh_layer_id, s->temporal_id);

    return nuh_layer_id == 0;
}

static void restore_tqb_pixels(HEVCContext *s)
{
    int min_pu_size = 1 << s->sps->log2_min_pu_size;
    int x, y, c_idx;

    for (c_idx = 0; c_idx < 3; c_idx++) {
        ptrdiff_t stride = s->frame->linesize[c_idx];
        int hshift       = s->sps->hshift[c_idx];
        int vshift       = s->sps->vshift[c_idx];
        for (y = 0; y < s->sps->min_pu_height; y++) {
            for (x = 0; x < s->sps->min_pu_width; x++) {
                if (s->is_pcm[y * s->sps->min_pu_width + x]) {
                    int n;
                    int len      = min_pu_size >> hshift;
                    uint8_t *src = &s->frame->data[c_idx][((y << s->sps->log2_min_pu_size) >> vshift) * stride + (((x << s->sps->log2_min_pu_size) >> hshift) << s->sps->pixel_shift)];
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

static int set_side_data(HEVCContext *s)
{
    AVFrame *out = s->ref->frame;

    if (s->sei_frame_packing_present &&
        s->frame_packing_arrangement_type >= 3 &&
        s->frame_packing_arrangement_type <= 5 &&
        s->content_interpretation_type > 0 &&
        s->content_interpretation_type < 3) {
        AVStereo3D *stereo = av_stereo3d_create_side_data(out);
        if (!stereo)
            return AVERROR(ENOMEM);

        switch (s->frame_packing_arrangement_type) {
        case 3:
            if (s->quincunx_subsampling)
                stereo->type = AV_STEREO3D_SIDEBYSIDE_QUINCUNX;
            else
                stereo->type = AV_STEREO3D_SIDEBYSIDE;
            break;
        case 4:
            stereo->type = AV_STEREO3D_TOPBOTTOM;
            break;
        case 5:
            stereo->type = AV_STEREO3D_FRAMESEQUENCE;
            break;
        }

        if (s->content_interpretation_type == 2)
            stereo->flags = AV_STEREO3D_FLAG_INVERT;
    }

    return 0;
}

static int hevc_frame_start(HEVCContext *s)
{
    HEVCLocalContext *lc = s->HEVClc;
    int pic_size_in_ctb  = ((s->sps->width  >> s->sps->log2_min_cb_size) + 1) *
                           ((s->sps->height >> s->sps->log2_min_cb_size) + 1);
    int ret;
    AVFrame *cur_frame;

    memset(s->horizontal_bs, 0, 2 * s->bs_width * (s->bs_height + 1));
    memset(s->vertical_bs,   0, 2 * s->bs_width * (s->bs_height + 1));
    memset(s->cbf_luma,      0, s->sps->min_tb_width * s->sps->min_tb_height);
    memset(s->is_pcm,        0, s->sps->min_pu_width * s->sps->min_pu_height);
    memset(s->tab_slice_address, -1, pic_size_in_ctb * sizeof(*s->tab_slice_address));

    s->is_decoded        = 0;
    s->first_nal_type    = s->nal_unit_type;

    if (s->pps->tiles_enabled_flag)
        lc->end_of_tiles_x = s->pps->column_width[0] << s->sps->log2_ctb_size;

    ret = ff_hevc_set_new_ref(s, s->sps->sao_enabled ? &s->sao_frame : &s->frame,
                              s->poc);
    if (ret < 0)
        goto fail;

    ret = ff_hevc_frame_rps(s);
    if (ret < 0) {
        av_log(s->avctx, AV_LOG_ERROR, "Error constructing the frame RPS.\n");
        goto fail;
    }

    ret = set_side_data(s);
    if (ret < 0)
        goto fail;

    cur_frame = s->sps->sao_enabled ? s->sao_frame : s->frame;
    cur_frame->pict_type = 3 - s->sh.slice_type;

    av_frame_unref(s->output_frame);
    ret = ff_hevc_output_frame(s, s->output_frame, 0);
    if (ret < 0)
        goto fail;

    ff_thread_finish_setup(s->avctx);

    return 0;

fail:
    if (s->ref && s->threads_type == FF_THREAD_FRAME)
        ff_thread_report_progress(&s->ref->tf, INT_MAX, 0);
    s->ref = NULL;
    return ret;
}

static int decode_nal_unit(HEVCContext *s, const uint8_t *nal, int length)
{
    HEVCLocalContext *lc = s->HEVClc;
    GetBitContext *gb    = &lc->gb;
    int ctb_addr_ts, ret;

    ret = init_get_bits8(gb, nal, length);
    if (ret < 0)
        return ret;

    ret = hls_nal_unit(s);
    if (ret < 0) {
        av_log(s->avctx, AV_LOG_ERROR, "Invalid NAL unit %d, skipping.\n",
               s->nal_unit_type);
        goto fail;
    } else if (!ret)
        return 0;

    switch (s->nal_unit_type) {
    case NAL_VPS:
        ret = ff_hevc_decode_nal_vps(s);
        if (ret < 0)
            goto fail;
        break;
    case NAL_SPS:
        ret = ff_hevc_decode_nal_sps(s);
        if (ret < 0)
            goto fail;
        break;
    case NAL_PPS:
        ret = ff_hevc_decode_nal_pps(s);
        if (ret < 0)
            goto fail;
        break;
    case NAL_SEI_PREFIX:
    case NAL_SEI_SUFFIX:
        ret = ff_hevc_decode_nal_sei(s);
        if (ret < 0)
            goto fail;
        break;
    case NAL_TRAIL_R:
    case NAL_TRAIL_N:
    case NAL_TSA_N:
    case NAL_TSA_R:
    case NAL_STSA_N:
    case NAL_STSA_R:
    case NAL_BLA_W_LP:
    case NAL_BLA_W_RADL:
    case NAL_BLA_N_LP:
    case NAL_IDR_W_RADL:
    case NAL_IDR_N_LP:
    case NAL_CRA_NUT:
    case NAL_RADL_N:
    case NAL_RADL_R:
    case NAL_RASL_N:
    case NAL_RASL_R:
        ret = hls_slice_header(s);
        if (ret < 0)
            return ret;

        if (s->max_ra == INT_MAX) {
            if (s->nal_unit_type == NAL_CRA_NUT || IS_BLA(s)) {
                s->max_ra = s->poc;
            } else {
                if (IS_IDR(s))
                    s->max_ra = INT_MIN;
            }
        }

        if ((s->nal_unit_type == NAL_RASL_R || s->nal_unit_type == NAL_RASL_N) &&
            s->poc <= s->max_ra) {
            s->is_decoded = 0;
            break;
        } else {
            if (s->nal_unit_type == NAL_RASL_R && s->poc > s->max_ra)
                s->max_ra = INT_MIN;
        }

        if (s->sh.first_slice_in_pic_flag) {
            ret = hevc_frame_start(s);
            if (ret < 0)
                return ret;
        } else if (!s->ref) {
            av_log(s->avctx, AV_LOG_ERROR, "First slice in a frame missing.\n");
            goto fail;
        }

        if (s->nal_unit_type != s->first_nal_type) {
            av_log(s->avctx, AV_LOG_ERROR,
                   "Non-matching NAL types of the VCL NALUs: %d %d\n",
                   s->first_nal_type, s->nal_unit_type);
            return AVERROR_INVALIDDATA;
        }

        if (!s->sh.dependent_slice_segment_flag &&
            s->sh.slice_type != I_SLICE) {
            ret = ff_hevc_slice_rpl(s);
            if (ret < 0) {
                av_log(s->avctx, AV_LOG_WARNING,
                       "Error constructing the reference lists for the current slice.\n");
                goto fail;
            }
        }

        if (s->threads_number > 1 && s->sh.num_entry_point_offsets > 0)
            ctb_addr_ts = hls_slice_data_wpp(s, nal, length);
        else
            ctb_addr_ts = hls_slice_data(s);
        if (ctb_addr_ts >= (s->sps->ctb_width * s->sps->ctb_height)) {
            s->is_decoded = 1;
            if ((s->pps->transquant_bypass_enable_flag ||
                 (s->sps->pcm.loop_filter_disable_flag && s->sps->pcm_enabled_flag)) &&
                s->sps->sao_enabled)
                restore_tqb_pixels(s);
        }

        if (ctb_addr_ts < 0) {
            ret = ctb_addr_ts;
            goto fail;
        }
        break;
    case NAL_EOS_NUT:
    case NAL_EOB_NUT:
        s->seq_decode = (s->seq_decode + 1) & 0xff;
        s->max_ra     = INT_MAX;
        break;
    case NAL_AUD:
    case NAL_FD_NUT:
        break;
    default:
        av_log(s->avctx, AV_LOG_INFO,
               "Skipping NAL unit %d\n", s->nal_unit_type);
    }

    return 0;
fail:
    if (s->avctx->err_recognition & AV_EF_EXPLODE)
        return ret;
    return 0;
}

/* FIXME: This is adapted from ff_h264_decode_nal, avoiding duplication
 * between these functions would be nice. */
int ff_hevc_extract_rbsp(HEVCContext *s, const uint8_t *src, int length,
                         HEVCNAL *nal)
{
    int i, si, di;
    uint8_t *dst;

    s->skipped_bytes = 0;
#define STARTCODE_TEST                                                  \
        if (i + 2 < length && src[i + 1] == 0 && src[i + 2] <= 3) {     \
            if (src[i + 2] != 3) {                                      \
                /* startcode, so we must be past the end */             \
                length = i;                                             \
            }                                                           \
            break;                                                      \
        }
#if HAVE_FAST_UNALIGNED
#define FIND_FIRST_ZERO                                                 \
        if (i > 0 && !src[i])                                           \
            i--;                                                        \
        while (src[i])                                                  \
            i++
#if HAVE_FAST_64BIT
    for (i = 0; i + 1 < length; i += 9) {
        if (!((~AV_RN64A(src + i) &
               (AV_RN64A(src + i) - 0x0100010001000101ULL)) &
              0x8000800080008080ULL))
            continue;
        FIND_FIRST_ZERO;
        STARTCODE_TEST;
        i -= 7;
    }
#else
    for (i = 0; i + 1 < length; i += 5) {
        if (!((~AV_RN32A(src + i) &
               (AV_RN32A(src + i) - 0x01000101U)) &
              0x80008080U))
            continue;
        FIND_FIRST_ZERO;
        STARTCODE_TEST;
        i -= 3;
    }
#endif /* HAVE_FAST_64BIT */
#else
    for (i = 0; i + 1 < length; i += 2) {
        if (src[i])
            continue;
        if (i > 0 && src[i - 1] == 0)
            i--;
        STARTCODE_TEST;
    }
#endif /* HAVE_FAST_UNALIGNED */

    if (i >= length - 1) { // no escaped 0
        nal->data = src;
        nal->size = length;
        return length;
    }

    av_fast_malloc(&nal->rbsp_buffer, &nal->rbsp_buffer_size,
                   length + FF_INPUT_BUFFER_PADDING_SIZE);
    if (!nal->rbsp_buffer)
        return AVERROR(ENOMEM);

    dst = nal->rbsp_buffer;

    memcpy(dst, src, i);
    si = di = i;
    while (si + 2 < length) {
        // remove escapes (very rare 1:2^22)
        if (src[si + 2] > 3) {
            dst[di++] = src[si++];
            dst[di++] = src[si++];
        } else if (src[si] == 0 && src[si + 1] == 0) {
            if (src[si + 2] == 3) { // escape
                dst[di++] = 0;
                dst[di++] = 0;
                si       += 3;

                s->skipped_bytes++;
                if (s->skipped_bytes_pos_size < s->skipped_bytes) {
                    s->skipped_bytes_pos_size *= 2;
                    av_reallocp_array(&s->skipped_bytes_pos,
                            s->skipped_bytes_pos_size,
                            sizeof(*s->skipped_bytes_pos));
                    if (!s->skipped_bytes_pos)
                        return AVERROR(ENOMEM);
                }
                if (s->skipped_bytes_pos)
                    s->skipped_bytes_pos[s->skipped_bytes-1] = di - 1;
                continue;
            } else // next start code
                goto nsc;
        }

        dst[di++] = src[si++];
    }
    while (si < length)
        dst[di++] = src[si++];

nsc:
    memset(dst + di, 0, FF_INPUT_BUFFER_PADDING_SIZE);

    nal->data = dst;
    nal->size = di;
    return si;
}

static int decode_nal_units(HEVCContext *s, const uint8_t *buf, int length)
{
    int i, consumed, ret = 0;

    s->ref = NULL;
    s->last_eos = s->eos;
    s->eos = 0;

    /* split the input packet into NAL units, so we know the upper bound on the
     * number of slices in the frame */
    s->nb_nals = 0;
    while (length >= 4) {
        HEVCNAL *nal;
        int extract_length = 0;

        if (s->is_nalff) {
            int i;
            for (i = 0; i < s->nal_length_size; i++)
                extract_length = (extract_length << 8) | buf[i];
            buf    += s->nal_length_size;
            length -= s->nal_length_size;

            if (extract_length > length) {
                av_log(s->avctx, AV_LOG_ERROR, "Invalid NAL unit size.\n");
                ret = AVERROR_INVALIDDATA;
                goto fail;
            }
        } else {
            /* search start code */
            while (buf[0] != 0 || buf[1] != 0 || buf[2] != 1) {
                ++buf;
                --length;
                if (length < 4) {
                    av_log(s->avctx, AV_LOG_ERROR, "No start code is found.\n");
                    ret = AVERROR_INVALIDDATA;
                    goto fail;
                }
            }

            buf           += 3;
            length        -= 3;
        }

        if (!s->is_nalff)
            extract_length = length;

        if (s->nals_allocated < s->nb_nals + 1) {
            int new_size = s->nals_allocated + 1;
            HEVCNAL *tmp = av_realloc_array(s->nals, new_size, sizeof(*tmp));
            if (!tmp) {
                ret = AVERROR(ENOMEM);
                goto fail;
            }
            s->nals = tmp;
            memset(s->nals + s->nals_allocated, 0,
                   (new_size - s->nals_allocated) * sizeof(*tmp));
            av_reallocp_array(&s->skipped_bytes_nal, new_size, sizeof(*s->skipped_bytes_nal));
            av_reallocp_array(&s->skipped_bytes_pos_size_nal, new_size, sizeof(*s->skipped_bytes_pos_size_nal));
            av_reallocp_array(&s->skipped_bytes_pos_nal, new_size, sizeof(*s->skipped_bytes_pos_nal));
            s->skipped_bytes_pos_size_nal[s->nals_allocated] = 1024; // initial buffer size
            s->skipped_bytes_pos_nal[s->nals_allocated] = av_malloc_array(s->skipped_bytes_pos_size_nal[s->nals_allocated], sizeof(*s->skipped_bytes_pos));
            s->nals_allocated = new_size;
        }
        s->skipped_bytes_pos_size = s->skipped_bytes_pos_size_nal[s->nb_nals];
        s->skipped_bytes_pos = s->skipped_bytes_pos_nal[s->nb_nals];
        nal = &s->nals[s->nb_nals];

        consumed = ff_hevc_extract_rbsp(s, buf, extract_length, nal);

        s->skipped_bytes_nal[s->nb_nals] = s->skipped_bytes;
        s->skipped_bytes_pos_size_nal[s->nb_nals] = s->skipped_bytes_pos_size;
        s->skipped_bytes_pos_nal[s->nb_nals++] = s->skipped_bytes_pos;


        if (consumed < 0) {
            ret = consumed;
            goto fail;
        }

        ret = init_get_bits8(&s->HEVClc->gb, nal->data, nal->size);
        if (ret < 0)
            goto fail;
        hls_nal_unit(s);

        if (s->nal_unit_type == NAL_EOB_NUT ||
            s->nal_unit_type == NAL_EOS_NUT)
            s->eos = 1;

        buf    += consumed;
        length -= consumed;
    }

    /* parse the NAL units */
    for (i = 0; i < s->nb_nals; i++) {
        int ret;
        s->skipped_bytes = s->skipped_bytes_nal[i];
        s->skipped_bytes_pos = s->skipped_bytes_pos_nal[i];

        ret = decode_nal_unit(s, s->nals[i].data, s->nals[i].size);
        if (ret < 0) {
            av_log(s->avctx, AV_LOG_WARNING,
                   "Error parsing NAL unit #%d.\n", i);
            goto fail;
        }
    }

fail:
    if (s->ref && s->threads_type == FF_THREAD_FRAME)
        ff_thread_report_progress(&s->ref->tf, INT_MAX, 0);

    return ret;
}

static void print_md5(void *log_ctx, int level, uint8_t md5[16])
{
    int i;
    for (i = 0; i < 16; i++)
        av_log(log_ctx, level, "%02"PRIx8, md5[i]);
}

static int verify_md5(HEVCContext *s, AVFrame *frame)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(frame->format);
    int pixel_shift;
    int i, j;

    if (!desc)
        return AVERROR(EINVAL);

    pixel_shift = desc->comp[0].depth_minus1 > 7;

    av_log(s->avctx, AV_LOG_DEBUG, "Verifying checksum for frame with POC %d: ",
           s->poc);

    /* the checksums are LE, so we have to byteswap for >8bpp formats
     * on BE arches */
#if HAVE_BIGENDIAN
    if (pixel_shift && !s->checksum_buf) {
        av_fast_malloc(&s->checksum_buf, &s->checksum_buf_size,
                       FFMAX3(frame->linesize[0], frame->linesize[1],
                              frame->linesize[2]));
        if (!s->checksum_buf)
            return AVERROR(ENOMEM);
    }
#endif

    for (i = 0; frame->data[i]; i++) {
        int width  = s->avctx->coded_width;
        int height = s->avctx->coded_height;
        int w = (i == 1 || i == 2) ? (width  >> desc->log2_chroma_w) : width;
        int h = (i == 1 || i == 2) ? (height >> desc->log2_chroma_h) : height;
        uint8_t md5[16];

        av_md5_init(s->md5_ctx);
        for (j = 0; j < h; j++) {
            const uint8_t *src = frame->data[i] + j * frame->linesize[i];
#if HAVE_BIGENDIAN
            if (pixel_shift) {
                s->dsp.bswap16_buf((uint16_t*)s->checksum_buf,
                                   (const uint16_t*)src, w);
                src = s->checksum_buf;
            }
#endif
            av_md5_update(s->md5_ctx, src, w << pixel_shift);
        }
        av_md5_final(s->md5_ctx, md5);

        if (!memcmp(md5, s->md5[i], 16)) {
            av_log   (s->avctx, AV_LOG_DEBUG, "plane %d - correct ", i);
            print_md5(s->avctx, AV_LOG_DEBUG, md5);
            av_log   (s->avctx, AV_LOG_DEBUG, "; ");
        } else {
            av_log   (s->avctx, AV_LOG_ERROR, "mismatching checksum of plane %d - ", i);
            print_md5(s->avctx, AV_LOG_ERROR, md5);
            av_log   (s->avctx, AV_LOG_ERROR, " != ");
            print_md5(s->avctx, AV_LOG_ERROR, s->md5[i]);
            av_log   (s->avctx, AV_LOG_ERROR, "\n");
            return AVERROR_INVALIDDATA;
        }
    }

    av_log(s->avctx, AV_LOG_DEBUG, "\n");

    return 0;
}

static int hevc_decode_frame(AVCodecContext *avctx, void *data, int *got_output,
                             AVPacket *avpkt)
{
    int ret;
    HEVCContext *s = avctx->priv_data;

    if (!avpkt->size) {
        ret = ff_hevc_output_frame(s, data, 1);
        if (ret < 0)
            return ret;

        *got_output = ret;
        return 0;
    }

    s->ref = NULL;
    ret    = decode_nal_units(s, avpkt->data, avpkt->size);
    if (ret < 0)
        return ret;

    /* verify the SEI checksum */
    if (avctx->err_recognition & AV_EF_CRCCHECK && s->is_decoded &&
        s->is_md5) {
        ret = verify_md5(s, s->ref->frame);
        if (ret < 0 && avctx->err_recognition & AV_EF_EXPLODE) {
            ff_hevc_unref_frame(s, s->ref, ~0);
            return ret;
        }
    }
    s->is_md5 = 0;

    if (s->is_decoded) {
        av_log(avctx, AV_LOG_DEBUG, "Decoded frame with POC %d.\n", s->poc);
        s->is_decoded = 0;
    }

    if (s->output_frame->buf[0]) {
        av_frame_move_ref(data, s->output_frame);
        *got_output = 1;
    }

    return avpkt->size;
}

static int hevc_ref_frame(HEVCContext *s, HEVCFrame *dst, HEVCFrame *src)
{
    int ret;

    ret = ff_thread_ref_frame(&dst->tf, &src->tf);
    if (ret < 0)
        return ret;

    dst->tab_mvf_buf = av_buffer_ref(src->tab_mvf_buf);
    if (!dst->tab_mvf_buf)
        goto fail;
    dst->tab_mvf = src->tab_mvf;

    dst->rpl_tab_buf = av_buffer_ref(src->rpl_tab_buf);
    if (!dst->rpl_tab_buf)
        goto fail;
    dst->rpl_tab = src->rpl_tab;

    dst->rpl_buf = av_buffer_ref(src->rpl_buf);
    if (!dst->rpl_buf)
        goto fail;

    dst->poc        = src->poc;
    dst->ctb_count  = src->ctb_count;
    dst->window     = src->window;
    dst->flags      = src->flags;
    dst->sequence   = src->sequence;

    return 0;
fail:
    ff_hevc_unref_frame(s, dst, ~0);
    return AVERROR(ENOMEM);
}

static av_cold int hevc_decode_free(AVCodecContext *avctx)
{
    HEVCContext       *s = avctx->priv_data;
    HEVCLocalContext *lc = s->HEVClc;
    int i;

    pic_arrays_free(s);

    av_freep(&s->md5_ctx);

    for(i=0; i < s->nals_allocated; i++) {
        av_freep(&s->skipped_bytes_pos_nal[i]);
    }
    av_freep(&s->skipped_bytes_pos_size_nal);
    av_freep(&s->skipped_bytes_nal);
    av_freep(&s->skipped_bytes_pos_nal);

    av_freep(&s->cabac_state);

    av_frame_free(&s->tmp_frame);
    av_frame_free(&s->output_frame);

    for (i = 0; i < FF_ARRAY_ELEMS(s->DPB); i++) {
        ff_hevc_unref_frame(s, &s->DPB[i], ~0);
        av_frame_free(&s->DPB[i].frame);
    }

    for (i = 0; i < FF_ARRAY_ELEMS(s->vps_list); i++)
        av_buffer_unref(&s->vps_list[i]);
    for (i = 0; i < FF_ARRAY_ELEMS(s->sps_list); i++)
        av_buffer_unref(&s->sps_list[i]);
    for (i = 0; i < FF_ARRAY_ELEMS(s->pps_list); i++)
        av_buffer_unref(&s->pps_list[i]);

    av_freep(&s->sh.entry_point_offset);
    av_freep(&s->sh.offset);
    av_freep(&s->sh.size);

    for (i = 1; i < s->threads_number; i++) {
        lc = s->HEVClcList[i];
        if (lc) {
            av_freep(&s->HEVClcList[i]);
            av_freep(&s->sList[i]);
        }
    }
    if (s->HEVClc == s->HEVClcList[0])
        s->HEVClc = NULL;
    av_freep(&s->HEVClcList[0]);

    for (i = 0; i < s->nals_allocated; i++)
        av_freep(&s->nals[i].rbsp_buffer);
    av_freep(&s->nals);
    s->nals_allocated = 0;

    return 0;
}

static av_cold int hevc_init_context(AVCodecContext *avctx)
{
    HEVCContext *s = avctx->priv_data;
    int i;

    s->avctx = avctx;

    s->HEVClc = av_mallocz(sizeof(HEVCLocalContext));
    if (!s->HEVClc)
        goto fail;
    s->HEVClcList[0] = s->HEVClc;
    s->sList[0] = s;

    s->cabac_state = av_malloc(HEVC_CONTEXTS);
    if (!s->cabac_state)
        goto fail;

    s->tmp_frame = av_frame_alloc();
    if (!s->tmp_frame)
        goto fail;

    s->output_frame = av_frame_alloc();
    if (!s->output_frame)
        goto fail;

    for (i = 0; i < FF_ARRAY_ELEMS(s->DPB); i++) {
        s->DPB[i].frame = av_frame_alloc();
        if (!s->DPB[i].frame)
            goto fail;
        s->DPB[i].tf.f = s->DPB[i].frame;
    }

    s->max_ra = INT_MAX;

    s->md5_ctx = av_md5_alloc();
    if (!s->md5_ctx)
        goto fail;

    ff_dsputil_init(&s->dsp, avctx);

    s->context_initialized = 1;
    s->eos = 0;

    return 0;

fail:
    hevc_decode_free(avctx);
    return AVERROR(ENOMEM);
}

static int hevc_update_thread_context(AVCodecContext *dst,
                                      const AVCodecContext *src)
{
    HEVCContext *s  = dst->priv_data;
    HEVCContext *s0 = src->priv_data;
    int i, ret;

    if (!s->context_initialized) {
        ret = hevc_init_context(dst);
        if (ret < 0)
            return ret;
    }

    for (i = 0; i < FF_ARRAY_ELEMS(s->DPB); i++) {
        ff_hevc_unref_frame(s, &s->DPB[i], ~0);
        if (s0->DPB[i].frame->buf[0]) {
            ret = hevc_ref_frame(s, &s->DPB[i], &s0->DPB[i]);
            if (ret < 0)
                return ret;
        }
    }

    for (i = 0; i < FF_ARRAY_ELEMS(s->vps_list); i++) {
        av_buffer_unref(&s->vps_list[i]);
        if (s0->vps_list[i]) {
            s->vps_list[i] = av_buffer_ref(s0->vps_list[i]);
            if (!s->vps_list[i])
                return AVERROR(ENOMEM);
        }
    }

    for (i = 0; i < FF_ARRAY_ELEMS(s->sps_list); i++) {
        av_buffer_unref(&s->sps_list[i]);
        if (s0->sps_list[i]) {
            s->sps_list[i] = av_buffer_ref(s0->sps_list[i]);
            if (!s->sps_list[i])
                return AVERROR(ENOMEM);
        }
    }

    for (i = 0; i < FF_ARRAY_ELEMS(s->pps_list); i++) {
        av_buffer_unref(&s->pps_list[i]);
        if (s0->pps_list[i]) {
            s->pps_list[i] = av_buffer_ref(s0->pps_list[i]);
            if (!s->pps_list[i])
                return AVERROR(ENOMEM);
        }
    }

    if (s->sps != s0->sps)
        ret = set_sps(s, s0->sps);

    s->seq_decode = s0->seq_decode;
    s->seq_output = s0->seq_output;
    s->pocTid0    = s0->pocTid0;
    s->max_ra     = s0->max_ra;
    s->eos        = s0->eos;

    s->is_nalff        = s0->is_nalff;
    s->nal_length_size = s0->nal_length_size;

    s->threads_number      = s0->threads_number;
    s->threads_type        = s0->threads_type;

    if (s0->eos) {
        s->seq_decode = (s->seq_decode + 1) & 0xff;
        s->max_ra = INT_MAX;
    }

    return 0;
}

static int hevc_decode_extradata(HEVCContext *s)
{
    AVCodecContext *avctx = s->avctx;
    GetByteContext gb;
    int ret;

    bytestream2_init(&gb, avctx->extradata, avctx->extradata_size);

    if (avctx->extradata_size > 3 &&
        (avctx->extradata[0] || avctx->extradata[1] ||
         avctx->extradata[2] > 1)) {
        /* It seems the extradata is encoded as hvcC format.
         * Temporarily, we support configurationVersion==0 until 14496-15 3rd
         * is finalized. When finalized, configurationVersion will be 1 and we
         * can recognize hvcC by checking if avctx->extradata[0]==1 or not. */
        int i, j, num_arrays, nal_len_size;

        s->is_nalff = 1;

        bytestream2_skip(&gb, 21);
        nal_len_size = (bytestream2_get_byte(&gb) & 3) + 1;
        num_arrays   = bytestream2_get_byte(&gb);

        /* nal units in the hvcC always have length coded with 2 bytes,
         * so put a fake nal_length_size = 2 while parsing them */
        s->nal_length_size = 2;

        /* Decode nal units from hvcC. */
        for (i = 0; i < num_arrays; i++) {
            int type = bytestream2_get_byte(&gb) & 0x3f;
            int cnt  = bytestream2_get_be16(&gb);

            for (j = 0; j < cnt; j++) {
                // +2 for the nal size field
                int nalsize = bytestream2_peek_be16(&gb) + 2;
                if (bytestream2_get_bytes_left(&gb) < nalsize) {
                    av_log(s->avctx, AV_LOG_ERROR,
                           "Invalid NAL unit size in extradata.\n");
                    return AVERROR_INVALIDDATA;
                }

                ret = decode_nal_units(s, gb.buffer, nalsize);
                if (ret < 0) {
                    av_log(avctx, AV_LOG_ERROR,
                           "Decoding nal unit %d %d from hvcC failed\n",
                           type, i);
                    return ret;
                }
                bytestream2_skip(&gb, nalsize);
            }
        }

        /* Now store right nal length size, that will be used to parse
         * all other nals */
        s->nal_length_size = nal_len_size;
    } else {
        s->is_nalff = 0;
        ret = decode_nal_units(s, avctx->extradata, avctx->extradata_size);
        if (ret < 0)
            return ret;
    }
    return 0;
}

static av_cold int hevc_decode_init(AVCodecContext *avctx)
{
    HEVCContext *s = avctx->priv_data;
    int ret;

    ff_init_cabac_states();

    avctx->internal->allocate_progress = 1;

    ret = hevc_init_context(avctx);
    if (ret < 0)
        return ret;

    s->enable_parallel_tiles = 0;
    s->picture_struct = 0;

    if(avctx->active_thread_type & FF_THREAD_SLICE)
        s->threads_number = avctx->thread_count;
    else
        s->threads_number = 1;

    if (avctx->extradata_size > 0 && avctx->extradata) {
        ret = hevc_decode_extradata(s);
        if (ret < 0) {
            hevc_decode_free(avctx);
            return ret;
        }
    }

    if((avctx->active_thread_type & FF_THREAD_FRAME) && avctx->thread_count > 1)
            s->threads_type = FF_THREAD_FRAME;
        else
            s->threads_type = FF_THREAD_SLICE;

    return 0;
}

static av_cold int hevc_init_thread_copy(AVCodecContext *avctx)
{
    HEVCContext *s = avctx->priv_data;
    int ret;

    memset(s, 0, sizeof(*s));

    ret = hevc_init_context(avctx);
    if (ret < 0)
        return ret;

    return 0;
}

static void hevc_decode_flush(AVCodecContext *avctx)
{
    HEVCContext *s = avctx->priv_data;
    ff_hevc_flush_dpb(s);
    s->max_ra = INT_MAX;
}

#define OFFSET(x) offsetof(HEVCContext, x)
#define PAR (AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM)

static const AVProfile profiles[] = {
    { FF_PROFILE_HEVC_MAIN,                 "Main"                },
    { FF_PROFILE_HEVC_MAIN_10,              "Main 10"             },
    { FF_PROFILE_HEVC_MAIN_STILL_PICTURE,   "Main Still Picture"  },
    { FF_PROFILE_UNKNOWN },
};

static const AVOption options[] = {
    { "apply_defdispwin", "Apply default display window from VUI", OFFSET(apply_defdispwin),
        AV_OPT_TYPE_INT, {.i64 = 0}, 0, 1, PAR },
    { "strict-displaywin", "stricly apply default display window size", OFFSET(apply_defdispwin),
        AV_OPT_TYPE_INT, {.i64 = 0}, 0, 1, PAR },
    { NULL },
};

static const AVClass hevc_decoder_class = {
    .class_name = "HEVC decoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_hevc_decoder = {
    .name                  = "hevc",
    .long_name             = NULL_IF_CONFIG_SMALL("HEVC (High Efficiency Video Coding)"),
    .type                  = AVMEDIA_TYPE_VIDEO,
    .id                    = AV_CODEC_ID_HEVC,
    .priv_data_size        = sizeof(HEVCContext),
    .priv_class            = &hevc_decoder_class,
    .init                  = hevc_decode_init,
    .close                 = hevc_decode_free,
    .decode                = hevc_decode_frame,
    .flush                 = hevc_decode_flush,
    .update_thread_context = hevc_update_thread_context,
    .init_thread_copy      = hevc_init_thread_copy,
    .capabilities          = CODEC_CAP_DR1 | CODEC_CAP_DELAY |
                             CODEC_CAP_SLICE_THREADS | CODEC_CAP_FRAME_THREADS,
    .profiles              = NULL_IF_CONFIG_SMALL(profiles),
};
