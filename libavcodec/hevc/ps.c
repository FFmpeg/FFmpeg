/*
 * HEVC Parameter Set decoding
 *
 * Copyright (C) 2012 - 2013 Guillaume Martres
 * Copyright (C) 2012 - 2013 Mickael Raulet
 * Copyright (C) 2012 - 2013 Gildas Cocherel
 * Copyright (C) 2013 Vittorio Giovara
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

#include "libavutil/imgutils.h"
#include "libavutil/mem.h"
#include "golomb.h"
#include "h2645_vui.h"
#include "data.h"
#include "ps.h"
#include "profiles.h"
#include "refstruct.h"

static const uint8_t default_scaling_list_intra[] = {
    16, 16, 16, 16, 17, 18, 21, 24,
    16, 16, 16, 16, 17, 19, 22, 25,
    16, 16, 17, 18, 20, 22, 25, 29,
    16, 16, 18, 21, 24, 27, 31, 36,
    17, 17, 20, 24, 30, 35, 41, 47,
    18, 19, 22, 27, 35, 44, 54, 65,
    21, 22, 25, 31, 41, 54, 70, 88,
    24, 25, 29, 36, 47, 65, 88, 115
};

static const uint8_t default_scaling_list_inter[] = {
    16, 16, 16, 16, 17, 18, 20, 24,
    16, 16, 16, 17, 18, 20, 24, 25,
    16, 16, 17, 18, 20, 24, 25, 28,
    16, 17, 18, 20, 24, 25, 28, 33,
    17, 18, 20, 24, 25, 28, 33, 41,
    18, 20, 24, 25, 28, 33, 41, 54,
    20, 24, 25, 28, 33, 41, 54, 71,
    24, 25, 28, 33, 41, 54, 71, 91
};

static const uint8_t hevc_sub_width_c[] = {
    1, 2, 2, 1
};

static const uint8_t hevc_sub_height_c[] = {
    1, 2, 1, 1
};

static void remove_sps(HEVCParamSets *s, int id)
{
    int i;
    if (s->sps_list[id]) {
        /* drop all PPS that depend on this SPS */
        for (i = 0; i < FF_ARRAY_ELEMS(s->pps_list); i++)
            if (s->pps_list[i] && s->pps_list[i]->sps_id == id)
                ff_refstruct_unref(&s->pps_list[i]);

        ff_refstruct_unref(&s->sps_list[id]);
    }
}

static void remove_vps(HEVCParamSets *s, int id)
{
    int i;
    if (s->vps_list[id]) {
        for (i = 0; i < FF_ARRAY_ELEMS(s->sps_list); i++)
            if (s->sps_list[i] && s->sps_list[i]->vps_id == id)
                remove_sps(s, i);
        ff_refstruct_unref(&s->vps_list[id]);
    }
}

int ff_hevc_decode_short_term_rps(GetBitContext *gb, AVCodecContext *avctx,
                                  ShortTermRPS *rps, const HEVCSPS *sps, int is_slice_header)
{
    int delta_poc;
    int k0 = 0;
    int k  = 0;
    int i;

    rps->used        = 0;
    rps->rps_predict = 0;

    if (rps != sps->st_rps && sps->nb_st_rps)
        rps->rps_predict = get_bits1(gb);

    if (rps->rps_predict) {
        const ShortTermRPS *rps_ridx;
        uint8_t used[32] = { 0 };
        int delta_rps;

        if (is_slice_header) {
            rps->delta_idx = get_ue_golomb_long(gb) + 1;
            if (rps->delta_idx > sps->nb_st_rps) {
                av_log(avctx, AV_LOG_ERROR,
                       "Invalid value of delta_idx in slice header RPS: %d > %d.\n",
                       rps->delta_idx, sps->nb_st_rps);
                return AVERROR_INVALIDDATA;
            }
            rps_ridx = &sps->st_rps[sps->nb_st_rps - rps->delta_idx];
            rps->rps_idx_num_delta_pocs = rps_ridx->num_delta_pocs;
        } else
            rps_ridx = &sps->st_rps[rps - sps->st_rps - 1];

        rps->delta_rps_sign = get_bits1(gb);
        rps->abs_delta_rps  = get_ue_golomb_long(gb) + 1;
        if (rps->abs_delta_rps > 32768) {
            av_log(avctx, AV_LOG_ERROR,
                   "Invalid value of abs_delta_rps: %d\n",
                   rps->abs_delta_rps);
            return AVERROR_INVALIDDATA;
        }
        delta_rps      = (1 - (rps->delta_rps_sign << 1)) * rps->abs_delta_rps;
        for (i = 0; i <= rps_ridx->num_delta_pocs; i++) {
            used[k] = get_bits1(gb);

            rps->use_delta = 0;
            if (!used[k])
                rps->use_delta = get_bits1(gb);

            if (used[k] || rps->use_delta) {
                if (i < rps_ridx->num_delta_pocs)
                    delta_poc = delta_rps + rps_ridx->delta_poc[i];
                else
                    delta_poc = delta_rps;
                rps->delta_poc[k] = delta_poc;
                if (delta_poc < 0)
                    k0++;
                k++;
            }
        }

        if (k >= FF_ARRAY_ELEMS(used)) {
            av_log(avctx, AV_LOG_ERROR,
                   "Invalid num_delta_pocs: %d\n", k);
            return AVERROR_INVALIDDATA;
        }

        rps->num_delta_pocs    = k;
        rps->num_negative_pics = k0;
        // sort in increasing order (smallest first)
        if (rps->num_delta_pocs != 0) {
            int u, tmp;
            for (i = 1; i < rps->num_delta_pocs; i++) {
                delta_poc = rps->delta_poc[i];
                u         = used[i];
                for (k = i - 1; k >= 0; k--) {
                    tmp = rps->delta_poc[k];
                    if (delta_poc < tmp) {
                        rps->delta_poc[k + 1] = tmp;
                        used[k + 1]           = used[k];
                        rps->delta_poc[k]     = delta_poc;
                        used[k]               = u;
                    }
                }
            }
        }
        if ((rps->num_negative_pics >> 1) != 0) {
            int u;
            k = rps->num_negative_pics - 1;
            // flip the negative values to largest first
            for (i = 0; i < rps->num_negative_pics >> 1; i++) {
                delta_poc         = rps->delta_poc[i];
                u                 = used[i];
                rps->delta_poc[i] = rps->delta_poc[k];
                used[i]           = used[k];
                rps->delta_poc[k] = delta_poc;
                used[k]           = u;
                k--;
            }
        }

        for (unsigned i = 0; i < FF_ARRAY_ELEMS(used); i++)
            rps->used |= (uint32_t)used[i] << i;
    } else {
        unsigned int nb_positive_pics;

        rps->num_negative_pics = get_ue_golomb_long(gb);
        nb_positive_pics       = get_ue_golomb_long(gb);

        if (rps->num_negative_pics >= HEVC_MAX_REFS ||
            nb_positive_pics >= HEVC_MAX_REFS) {
            av_log(avctx, AV_LOG_ERROR, "Too many refs in a short term RPS.\n");
            return AVERROR_INVALIDDATA;
        }

        rps->num_delta_pocs = rps->num_negative_pics + nb_positive_pics;
        if (rps->num_delta_pocs) {
            int prev = 0;

            for (i = 0; i < rps->num_negative_pics; i++) {
                delta_poc = get_ue_golomb_long(gb) + 1;
                if (delta_poc < 1 || delta_poc > 32768) {
                    av_log(avctx, AV_LOG_ERROR,
                        "Invalid value of delta_poc: %d\n",
                        delta_poc);
                    return AVERROR_INVALIDDATA;
                }
                prev -= delta_poc;
                rps->delta_poc[i] = prev;
                rps->used        |= get_bits1(gb) * (1 << i);
            }
            prev = 0;
            for (i = 0; i < nb_positive_pics; i++) {
                delta_poc = get_ue_golomb_long(gb) + 1;
                if (delta_poc < 1 || delta_poc > 32768) {
                    av_log(avctx, AV_LOG_ERROR,
                        "Invalid value of delta_poc: %d\n",
                        delta_poc);
                    return AVERROR_INVALIDDATA;
                }
                prev += delta_poc;
                rps->delta_poc[rps->num_negative_pics + i] = prev;
                rps->used                                 |= get_bits1(gb) * (1 << (rps->num_negative_pics + i));
            }
        }
    }
    return 0;
}


static int decode_profile_tier_level(GetBitContext *gb, AVCodecContext *avctx,
                                      PTLCommon *ptl)
{
    const char *profile_name = NULL;
    int i;

    if (get_bits_left(gb) < 2+1+5 + 32 + 4 + 43 + 1)
        return -1;

    ptl->profile_space = get_bits(gb, 2);
    ptl->tier_flag     = get_bits1(gb);
    ptl->profile_idc   = get_bits(gb, 5);

#if !CONFIG_SMALL
    for (int i = 0; ff_hevc_profiles[i].profile != AV_PROFILE_UNKNOWN; i++)
        if (ff_hevc_profiles[i].profile == ptl->profile_idc) {
            profile_name = ff_hevc_profiles[i].name;
            break;
        }
#endif
    av_log(avctx, profile_name ? AV_LOG_DEBUG : AV_LOG_WARNING,
           "%s profile bitstream\n", profile_name ? profile_name : "Unknown");

    for (i = 0; i < 32; i++) {
        ptl->profile_compatibility_flag[i] = get_bits1(gb);

        if (ptl->profile_idc == 0 && i > 0 && ptl->profile_compatibility_flag[i])
            ptl->profile_idc = i;
    }
    ptl->progressive_source_flag    = get_bits1(gb);
    ptl->interlaced_source_flag     = get_bits1(gb);
    ptl->non_packed_constraint_flag = get_bits1(gb);
    ptl->frame_only_constraint_flag = get_bits1(gb);

#define check_profile_idc(idc) \
        ptl->profile_idc == idc || ptl->profile_compatibility_flag[idc]

    if (check_profile_idc(4) || check_profile_idc(5) || check_profile_idc(6) ||
        check_profile_idc(7) || check_profile_idc(8) || check_profile_idc(9) ||
        check_profile_idc(10)) {

        ptl->max_12bit_constraint_flag        = get_bits1(gb);
        ptl->max_10bit_constraint_flag        = get_bits1(gb);
        ptl->max_8bit_constraint_flag         = get_bits1(gb);
        ptl->max_422chroma_constraint_flag    = get_bits1(gb);
        ptl->max_420chroma_constraint_flag    = get_bits1(gb);
        ptl->max_monochrome_constraint_flag   = get_bits1(gb);
        ptl->intra_constraint_flag            = get_bits1(gb);
        ptl->one_picture_only_constraint_flag = get_bits1(gb);
        ptl->lower_bit_rate_constraint_flag   = get_bits1(gb);

        if (check_profile_idc(5) || check_profile_idc(9) || check_profile_idc(10)) {
            ptl->max_14bit_constraint_flag    = get_bits1(gb);
            skip_bits_long(gb, 33); // XXX_reserved_zero_33bits[0..32]
        } else {
            skip_bits_long(gb, 34); // XXX_reserved_zero_34bits[0..33]
        }
    } else if (check_profile_idc(2)) {
        skip_bits(gb, 7);
        ptl->one_picture_only_constraint_flag = get_bits1(gb);
        skip_bits_long(gb, 35); // XXX_reserved_zero_35bits[0..34]
    } else {
        skip_bits_long(gb, 43); // XXX_reserved_zero_43bits[0..42]
    }

    if (check_profile_idc(1) || check_profile_idc(2) || check_profile_idc(3) ||
        check_profile_idc(4) || check_profile_idc(5) || check_profile_idc(9))
        ptl->inbld_flag = get_bits1(gb);
    else
        skip_bits1(gb);
#undef check_profile_idc

    return 0;
}

static int parse_ptl(GetBitContext *gb, AVCodecContext *avctx,
                     int profile_present, PTL *ptl, int max_num_sub_layers)
{
    int i, status = 0;

    if (profile_present) {
        status = decode_profile_tier_level(gb, avctx, &ptl->general_ptl);
    } else {
        memset(&ptl->general_ptl, 0, sizeof(ptl->general_ptl));
    }

    if (status < 0 || get_bits_left(gb) < 8 + (8*2 * (max_num_sub_layers - 1 > 0))) {
        av_log(avctx, AV_LOG_ERROR, "PTL information too short\n");
        return -1;
    }

    ptl->general_ptl.level_idc = get_bits(gb, 8);

    for (i = 0; i < max_num_sub_layers - 1; i++) {
        ptl->sub_layer_profile_present_flag[i] = get_bits1(gb);
        ptl->sub_layer_level_present_flag[i]   = get_bits1(gb);
    }

    if (max_num_sub_layers - 1> 0)
        for (i = max_num_sub_layers - 1; i < 8; i++)
            skip_bits(gb, 2); // reserved_zero_2bits[i]
    for (i = 0; i < max_num_sub_layers - 1; i++) {
        if (ptl->sub_layer_profile_present_flag[i] &&
            decode_profile_tier_level(gb, avctx, &ptl->sub_layer_ptl[i]) < 0) {
            av_log(avctx, AV_LOG_ERROR,
                   "PTL information for sublayer %i too short\n", i);
            return -1;
        }
        if (ptl->sub_layer_level_present_flag[i]) {
            if (get_bits_left(gb) < 8) {
                av_log(avctx, AV_LOG_ERROR,
                       "Not enough data for sublayer %i level_idc\n", i);
                return -1;
            } else
                ptl->sub_layer_ptl[i].level_idc = get_bits(gb, 8);
        }
    }

    return 0;
}

static void decode_sublayer_hrd(GetBitContext *gb, unsigned int nb_cpb,
                                HEVCSublayerHdrParams *par, int subpic_params_present)
{
    int i;

    for (i = 0; i < nb_cpb; i++) {
        par->bit_rate_value_minus1[i] = get_ue_golomb_long(gb);
        par->cpb_size_value_minus1[i] = get_ue_golomb_long(gb);

        if (subpic_params_present) {
            par->cpb_size_du_value_minus1[i] = get_ue_golomb_long(gb);
            par->bit_rate_du_value_minus1[i] = get_ue_golomb_long(gb);
        }

        par->cbr_flag |= get_bits1(gb) << i;
    }
}

static int decode_hrd(GetBitContext *gb, int common_inf_present,
                      HEVCHdrParams *hdr, int max_sublayers)
{
    if (common_inf_present) {
        hdr->nal_hrd_parameters_present_flag = get_bits1(gb);
        hdr->vcl_hrd_parameters_present_flag = get_bits1(gb);

        if (hdr->nal_hrd_parameters_present_flag ||
            hdr->vcl_hrd_parameters_present_flag) {
            hdr->sub_pic_hrd_params_present_flag = get_bits1(gb);

            if (hdr->sub_pic_hrd_params_present_flag) {
                hdr->tick_divisor_minus2 = get_bits(gb, 8);
                hdr->du_cpb_removal_delay_increment_length_minus1 = get_bits(gb, 5);
                hdr->sub_pic_cpb_params_in_pic_timing_sei_flag = get_bits1(gb);
                hdr->dpb_output_delay_du_length_minus1 = get_bits(gb, 5);
            }

            hdr->bit_rate_scale = get_bits(gb, 4);
            hdr->cpb_size_scale = get_bits(gb, 4);

            if (hdr->sub_pic_hrd_params_present_flag)
                hdr->cpb_size_du_scale = get_bits(gb, 4);

            hdr->initial_cpb_removal_delay_length_minus1 = get_bits(gb, 5);
            hdr->au_cpb_removal_delay_length_minus1 = get_bits(gb, 5);
            hdr->dpb_output_delay_length_minus1 = get_bits(gb, 5);
        }
    }

    for (int i = 0; i < max_sublayers; i++) {
        unsigned fixed_pic_rate_general_flag = get_bits1(gb);
        unsigned fixed_pic_rate_within_cvs_flag = 0;
        unsigned low_delay_hrd_flag = 0;
        hdr->flags.fixed_pic_rate_general_flag |= fixed_pic_rate_general_flag << i;

        if (!fixed_pic_rate_general_flag)
            fixed_pic_rate_within_cvs_flag = get_bits1(gb);
        hdr->flags.fixed_pic_rate_within_cvs_flag |= fixed_pic_rate_within_cvs_flag << i;

        if (fixed_pic_rate_within_cvs_flag || fixed_pic_rate_general_flag)
            hdr->elemental_duration_in_tc_minus1[i] = get_ue_golomb_long(gb);
        else
            low_delay_hrd_flag = get_bits1(gb);
        hdr->flags.low_delay_hrd_flag |= low_delay_hrd_flag << i;

        if (!low_delay_hrd_flag) {
            unsigned cpb_cnt_minus1 = get_ue_golomb_long(gb);
            if (cpb_cnt_minus1 > 31) {
                av_log(NULL, AV_LOG_ERROR, "nb_cpb %d invalid\n",
                       cpb_cnt_minus1);
                return AVERROR_INVALIDDATA;
            }
            hdr->cpb_cnt_minus1[i] = cpb_cnt_minus1;
        }

        if (hdr->nal_hrd_parameters_present_flag)
            decode_sublayer_hrd(gb, hdr->cpb_cnt_minus1[i]+1, &hdr->nal_params[i],
                                hdr->sub_pic_hrd_params_present_flag);

        if (hdr->vcl_hrd_parameters_present_flag)
            decode_sublayer_hrd(gb, hdr->cpb_cnt_minus1[i]+1, &hdr->vcl_params[i],
                                hdr->sub_pic_hrd_params_present_flag);
    }

    return 0;
}

static void hevc_vps_free(FFRefStructOpaque opaque, void *obj)
{
    HEVCVPS *vps = obj;

    av_freep(&vps->hdr);
    av_freep(&vps->data);
}

enum ScalabilityMask {
    HEVC_SCALABILITY_DEPTH      = 0,
    HEVC_SCALABILITY_MULTIVIEW  = 1,
    HEVC_SCALABILITY_SPATIAL    = 2,
    HEVC_SCALABILITY_AUXILIARY  = 3,
    HEVC_SCALABILITY_MASK_MAX   = 15,
};

enum DependencyType {
    HEVC_DEP_TYPE_SAMPLE = 0,
    HEVC_DEP_TYPE_MV     = 1,
    HEVC_DEP_TYPE_BOTH   = 2,
};

static int decode_vps_ext(GetBitContext *gb, AVCodecContext *avctx, HEVCVPS *vps,
                          uint64_t layer1_id_included)
{
    PTL ptl_dummy;
    uint8_t max_sub_layers[HEVC_MAX_LAYERS];

    int splitting_flag, dimension_id_len, view_id_len, num_add_olss, num_scalability_types,
        default_output_layer_idc, direct_dep_type_len, direct_dep_type,
        sub_layers_max_present, sub_layer_flag_info_present_flag, nb_ptl;
    unsigned non_vui_extension_length;

    if (vps->vps_max_layers == 1 || vps->vps_num_layer_sets == 1) {
        av_log(avctx, AV_LOG_VERBOSE, "Ignoring VPS extensions with a single layer\n");
        return 0;
    }

    if (vps->vps_max_layers > 2) {
        av_log(avctx, AV_LOG_ERROR,
               "VPS has %d layers, only 2 layers are supported\n",
               vps->vps_max_layers);
        return AVERROR_PATCHWELCOME;
    }
    if (vps->vps_num_layer_sets > 2) {
        av_log(avctx, AV_LOG_ERROR,
               "VPS has %d layer sets, only 2 layer sets are supported\n",
               vps->vps_num_layer_sets);
        return AVERROR_PATCHWELCOME;
    }

    align_get_bits(gb);

    /**
     * For stereoscopic MV-HEVC, the following simplifying assumptions are made:
     *
     * - vps_max_layers = 2 (one base layer, one multiview layer)
     * - vps_num_layer_sets = 2 (one output layer set for each view)
     * - NumScalabilityTypes = 1 (only HEVC_SCALABILITY_MULTIVIEW)
     * - direct_dependency_flag[1][0] = 1 (second layer depends on first)
     * - num_add_olss = 0 (no extra output layer sets)
     * - default_output_layer_idc = 0 (1:1 mapping between OLSs and layers)
     * - layer_id_included_flag[1] = {1, 1} (consequence of layer dependencies)
     * - vps_num_rep_formats_minus1 = 0 (all layers have the same size)
     *
     * Which results in the following derived variables:
     * - ViewOrderIdx = {0, 1}
     * - NumViews = 2
     * - DependencyFlag[1][0] = 1
     * - NumDirectRefLayers = {0, 1}
     * - NumRefLayers = {0, 1}
     * - NumPredictedLayers = {1, 0}
     * - NumIndependentLayers = 1
     * - NumLayersInTreePartition = {2}
     * - NumLayerSets = 2
     * - NumOutputLayerSets = 2
     * - OlsIdxToLsIdx = {0, 1}
     * - LayerIdxInVps = {0, 1}
     * - NumLayersInIdList = {1, 2}
     * - NumNecessaryLayers = {1, 2}
     * - NecessaryLayerFlag = {{1, 0}, {1, 1}}
     * - NumOutputLayersInOutputLayerSet = {1, 2}
     * - OutputLayerFlag = {{1, 0}, {1, 1}}
     */
    vps->nb_layers = 2;

    if (parse_ptl(gb, avctx, 0, &ptl_dummy, vps->vps_max_sub_layers) < 0)
        return AVERROR_INVALIDDATA;

    splitting_flag = get_bits1(gb);
    num_scalability_types = 0;
    for (int i = 0; i <= HEVC_SCALABILITY_MASK_MAX; i++) {
        int scalability_mask_flag = get_bits1(gb);
        if (scalability_mask_flag && (i != HEVC_SCALABILITY_MULTIVIEW)) {
            av_log(avctx, AV_LOG_ERROR, "Scalability type %d not supported\n", i);
            return AVERROR_PATCHWELCOME;
        }
        num_scalability_types += scalability_mask_flag;
    }
    if (num_scalability_types != 1)
        return AVERROR_INVALIDDATA;

    if (!splitting_flag)
        dimension_id_len = get_bits(gb, 3) + 1;

    if (get_bits1(gb)) { /* vps_nuh_layer_id_present_flag */
        int layer_id_in_nuh = get_bits(gb, 6);
        if (layer_id_in_nuh >= FF_ARRAY_ELEMS(vps->layer_idx)) {
            av_log(avctx, AV_LOG_ERROR, "Invalid layer_id_in_nuh[1]: %d\n",
                   layer_id_in_nuh);
            return AVERROR_INVALIDDATA;
        }
        vps->layer_idx[layer_id_in_nuh] = 1;
        vps->layer_id_in_nuh[1] = layer_id_in_nuh;
    } else {
        vps->layer_idx[1]       = 1;
        vps->layer_id_in_nuh[1] = 1;
    }

    if (!splitting_flag) {
        int view_idx = get_bits(gb, dimension_id_len);
        if (view_idx != 1) {
            av_log(avctx, AV_LOG_ERROR, "Unexpected ViewOrderIdx: %d\n", view_idx);
            return AVERROR_PATCHWELCOME;
        }
    }

    view_id_len = get_bits(gb, 4);
    if (view_id_len)
        for (int i = 0; i < 2 /* NumViews */; i++)
            vps->view_id[i] = get_bits(gb, view_id_len);

    if (!get_bits1(gb) /* direct_dependency_flag */) {
        av_log(avctx, AV_LOG_WARNING, "Independent output layers not supported\n");
        return AVERROR_PATCHWELCOME;
    }
    vps->num_direct_ref_layers[1] = 1;

    sub_layers_max_present = get_bits1(gb); // vps_sub_layers_max_minus1_present_flag
    for (int i = 0; i < vps->vps_max_layers; i++)
        max_sub_layers[i] = sub_layers_max_present ? get_bits(gb, 3) + 1 :
                                                     vps->vps_max_sub_layers;

    if (get_bits1(gb) /* max_tid_ref_present_flag */)
        skip_bits(gb, 3); // max_tid_il_ref_pics_plus1

    vps->default_ref_layers_active = get_bits1(gb);

    nb_ptl = get_ue_golomb(gb) + 1;
    /* idx [0] is signalled in base VPS, idx [1] is signalled at the
     * start of VPS extension, indices 2+ are signalled here;
     * we ignore all but the first one anyway */
    for (int i = 2; i < nb_ptl; i++) {
        int profile_present = get_bits1(gb);
        if (parse_ptl(gb, avctx, profile_present, &ptl_dummy, vps->vps_max_sub_layers) < 0)
            return AVERROR_INVALIDDATA;
    }

    num_add_olss = get_ue_golomb(gb);
    if (num_add_olss != 0) {
        /* Since we don't implement support for independent output layer sets
         * and auxiliary layers, this should never nonzero */
        av_log(avctx, AV_LOG_ERROR, "Unexpected num_add_olss: %d\n", num_add_olss);
        return AVERROR_PATCHWELCOME;
    }

    default_output_layer_idc = get_bits(gb, 2);
    if (default_output_layer_idc != 0) {
        av_log(avctx, AV_LOG_WARNING, "Unsupported default_output_layer_idc: %d\n",
               default_output_layer_idc);
        return AVERROR_PATCHWELCOME;
    }

    /* Consequence of established layer dependencies */
    if (layer1_id_included != ((1 << vps->layer_id_in_nuh[0]) |
                               (1 << vps->layer_id_in_nuh[1]))) {
        av_log(avctx, AV_LOG_ERROR, "Dependent layer not included in layer ID?\n");
        return AVERROR_PATCHWELCOME;
    }

    vps->num_output_layer_sets = 2;
    vps->ols[1] = 3;

    for (int j = 0; j < av_popcount64(vps->ols[1]); j++) {
        int ptl_idx = get_bits(gb, av_ceil_log2(nb_ptl));
        if (ptl_idx < 1 || ptl_idx >= nb_ptl) {
            av_log(avctx, AV_LOG_ERROR, "Invalid PTL index: %d\n", ptl_idx);
            return AVERROR_INVALIDDATA;
        }
    }

    if (get_ue_golomb_31(gb) != 0 /* vps_num_rep_formats_minus1 */) {
        av_log(avctx, AV_LOG_ERROR, "Unexpected extra rep formats\n");
        return AVERROR_INVALIDDATA;
    }

    vps->rep_format.pic_width_in_luma_samples  = get_bits(gb, 16);
    vps->rep_format.pic_height_in_luma_samples = get_bits(gb, 16);

    if (!get_bits1(gb) /* chroma_and_bit_depth_vps_present_flag */) {
        av_log(avctx, AV_LOG_ERROR,
               "chroma_and_bit_depth_vps_present_flag=0 in first rep_format\n");
        return AVERROR_INVALIDDATA;
    }
    vps->rep_format.chroma_format_idc = get_bits(gb, 2);
    if (vps->rep_format.chroma_format_idc == 3)
        vps->rep_format.separate_colour_plane_flag = get_bits1(gb);
    vps->rep_format.bit_depth_luma   = get_bits(gb, 4) + 8;
    vps->rep_format.bit_depth_chroma = get_bits(gb, 4) + 8;
    if (vps->rep_format.bit_depth_luma > 16 ||
        vps->rep_format.bit_depth_chroma > 16 ||
        vps->rep_format.bit_depth_luma != vps->rep_format.bit_depth_chroma) {
        av_log(avctx, AV_LOG_ERROR, "Unsupported bit depth: %"PRIu8" %"PRIu8"\n",
               vps->rep_format.bit_depth_luma, vps->rep_format.bit_depth_chroma);
        return AVERROR_PATCHWELCOME;
    }

    if (get_bits1(gb) /* conformance_window_vps_flag */) {
        int vert_mult  = hevc_sub_height_c[vps->rep_format.chroma_format_idc];
        int horiz_mult = hevc_sub_width_c[vps->rep_format.chroma_format_idc];
        vps->rep_format.conf_win_left_offset   = get_ue_golomb(gb) * horiz_mult;
        vps->rep_format.conf_win_right_offset  = get_ue_golomb(gb) * horiz_mult;
        vps->rep_format.conf_win_top_offset    = get_ue_golomb(gb) * vert_mult;
        vps->rep_format.conf_win_bottom_offset = get_ue_golomb(gb) * vert_mult;
    }

    vps->max_one_active_ref_layer = get_bits1(gb);
    vps->poc_lsb_aligned          = get_bits1(gb);

    sub_layer_flag_info_present_flag = get_bits1(gb);
    for (int j = 0; j < FFMAX(max_sub_layers[0], max_sub_layers[1]); j++) {
        int sub_layer_dpb_info_present_flag = 1;
        if (j > 0 && sub_layer_flag_info_present_flag)
            sub_layer_dpb_info_present_flag = get_bits1(gb);
        if (sub_layer_dpb_info_present_flag) {
            for (int k = 0; k < av_popcount64(vps->ols[1]); k++)
                vps->dpb_size.max_dec_pic_buffering = get_ue_golomb_long(gb) + 1;
            vps->dpb_size.max_num_reorder_pics = get_ue_golomb_long(gb);
            vps->dpb_size.max_latency_increase = get_ue_golomb_long(gb) - 1;
        }
    }

    direct_dep_type_len = get_ue_golomb_31(gb) + 2;
    if (direct_dep_type_len > 32) {
        av_log(avctx, AV_LOG_ERROR, "Invalid direct_dep_type_len: %d\n",
               direct_dep_type_len);
        return AVERROR_INVALIDDATA;
    }

    skip_bits1(gb); /* direct_depenency_all_layers_flag */
    direct_dep_type = get_bits_long(gb, direct_dep_type_len);
    if (direct_dep_type > HEVC_DEP_TYPE_BOTH) {
        av_log(avctx, AV_LOG_WARNING, "Unsupported direct_dep_type: %d\n",
               direct_dep_type);
        return AVERROR_PATCHWELCOME;
    }

    non_vui_extension_length = get_ue_golomb(gb);
    if (non_vui_extension_length > 4096) {
        av_log(avctx, AV_LOG_ERROR, "vps_non_vui_extension_length too large: %u\n",
               non_vui_extension_length);
        return AVERROR_INVALIDDATA;
    }
    skip_bits_long(gb, non_vui_extension_length * 8);

    if (get_bits1(gb)) // vps_vui_present_flag
        av_log(avctx, AV_LOG_WARNING, "VPS VUI not supported\n");

    return 0;
}

int ff_hevc_decode_nal_vps(GetBitContext *gb, AVCodecContext *avctx,
                           HEVCParamSets *ps)
{
    int i;
    int vps_id = get_bits(gb, 4);
    ptrdiff_t nal_size = gb->buffer_end - gb->buffer;
    int ret = AVERROR_INVALIDDATA;
    uint64_t layer1_id_included = 0;
    HEVCVPS *vps;

    if (ps->vps_list[vps_id]) {
        const HEVCVPS *vps1 = ps->vps_list[vps_id];
        if (vps1->data_size == nal_size &&
            !memcmp(vps1->data, gb->buffer, vps1->data_size))
            return 0;
    }

    vps = ff_refstruct_alloc_ext(sizeof(*vps), 0, NULL, hevc_vps_free);
    if (!vps)
        return AVERROR(ENOMEM);

    av_log(avctx, AV_LOG_DEBUG, "Decoding VPS\n");

    vps->data_size = nal_size;
    vps->data = av_memdup(gb->buffer, nal_size);
    if (!vps->data) {
        ret = AVERROR(ENOMEM);
        goto err;
    }
    vps->vps_id = vps_id;

    if (get_bits(gb, 2) != 3) { // vps_reserved_three_2bits
        av_log(avctx, AV_LOG_ERROR, "vps_reserved_three_2bits is not three\n");
        goto err;
    }

    vps->vps_max_layers               = get_bits(gb, 6) + 1;
    vps->vps_max_sub_layers           = get_bits(gb, 3) + 1;
    vps->vps_temporal_id_nesting_flag = get_bits1(gb);

    if (get_bits(gb, 16) != 0xffff) { // vps_reserved_ffff_16bits
        av_log(avctx, AV_LOG_ERROR, "vps_reserved_ffff_16bits is not 0xffff\n");
        goto err;
    }

    if (vps->vps_max_sub_layers > HEVC_MAX_SUB_LAYERS) {
        av_log(avctx, AV_LOG_ERROR, "vps_max_sub_layers out of range: %d\n",
               vps->vps_max_sub_layers);
        goto err;
    }

    if (parse_ptl(gb, avctx, 1, &vps->ptl, vps->vps_max_sub_layers) < 0)
        goto err;

    vps->vps_sub_layer_ordering_info_present_flag = get_bits1(gb);

    i = vps->vps_sub_layer_ordering_info_present_flag ? 0 : vps->vps_max_sub_layers - 1;
    for (; i < vps->vps_max_sub_layers; i++) {
        vps->vps_max_dec_pic_buffering[i] = get_ue_golomb_long(gb) + 1;
        vps->vps_num_reorder_pics[i]      = get_ue_golomb_long(gb);
        vps->vps_max_latency_increase[i]  = get_ue_golomb_long(gb) - 1;

        if (vps->vps_max_dec_pic_buffering[i] > HEVC_MAX_DPB_SIZE || !vps->vps_max_dec_pic_buffering[i]) {
            av_log(avctx, AV_LOG_ERROR, "vps_max_dec_pic_buffering_minus1 out of range: %d\n",
                   vps->vps_max_dec_pic_buffering[i] - 1);
            goto err;
        }
        if (vps->vps_num_reorder_pics[i] > vps->vps_max_dec_pic_buffering[i] - 1) {
            av_log(avctx, AV_LOG_WARNING, "vps_max_num_reorder_pics out of range: %d\n",
                   vps->vps_num_reorder_pics[i]);
            if (avctx->err_recognition & AV_EF_EXPLODE)
                goto err;
        }
    }

    vps->vps_max_layer_id   = get_bits(gb, 6);
    vps->vps_num_layer_sets = get_ue_golomb_long(gb) + 1;
    if (vps->vps_num_layer_sets < 1 || vps->vps_num_layer_sets > 1024 ||
        (vps->vps_num_layer_sets - 1LL) * (vps->vps_max_layer_id + 1LL) > get_bits_left(gb)) {
        av_log(avctx, AV_LOG_ERROR, "too many layer_id_included_flags\n");
        goto err;
    }

    vps->num_output_layer_sets = 1;
    vps->ols[0] = 1;

    // we support at most 2 layers, so ignore the others
    if (vps->vps_num_layer_sets > 1)
        layer1_id_included = get_bits64(gb, vps->vps_max_layer_id + 1); // layer_id_included_flag
    if (vps->vps_num_layer_sets > 2)
        skip_bits_long(gb, (vps->vps_num_layer_sets - 2) * (vps->vps_max_layer_id + 1));

    vps->vps_timing_info_present_flag = get_bits1(gb);
    if (vps->vps_timing_info_present_flag) {
        vps->vps_num_units_in_tick               = get_bits_long(gb, 32);
        vps->vps_time_scale                      = get_bits_long(gb, 32);
        vps->vps_poc_proportional_to_timing_flag = get_bits1(gb);
        if (vps->vps_poc_proportional_to_timing_flag)
            vps->vps_num_ticks_poc_diff_one = get_ue_golomb_long(gb) + 1;
        vps->vps_num_hrd_parameters = get_ue_golomb_long(gb);
        if (vps->vps_num_hrd_parameters > (unsigned)vps->vps_num_layer_sets) {
            av_log(avctx, AV_LOG_ERROR,
                   "vps_num_hrd_parameters %d is invalid\n", vps->vps_num_hrd_parameters);
            goto err;
        }

        if (vps->vps_num_hrd_parameters) {
            vps->hdr = av_calloc(vps->vps_num_hrd_parameters, sizeof(*vps->hdr));
            if (!vps->hdr)
                goto err;
        }

        for (i = 0; i < vps->vps_num_hrd_parameters; i++) {
            int common_inf_present = 1;

            get_ue_golomb_long(gb); // hrd_layer_set_idx
            if (i)
                common_inf_present = get_bits1(gb);
            decode_hrd(gb, common_inf_present, &vps->hdr[i],
                       vps->vps_max_sub_layers);
        }
    }

    vps->nb_layers    = 1;
    vps->layer_idx[0] = 0;
    for (int i = 1; i < FF_ARRAY_ELEMS(vps->layer_idx); i++)
        vps->layer_idx[i] = -1;

    if (vps->vps_max_layers > 1 && get_bits1(gb)) { /* vps_extension_flag */
        int ret = decode_vps_ext(gb, avctx, vps, layer1_id_included);
        if (ret == AVERROR_PATCHWELCOME) {
            vps->nb_layers = 1;
            av_log(avctx, AV_LOG_WARNING, "Ignoring unsupported VPS extension\n");
            ret = 0;
        } else if (ret < 0)
            goto err;
    }

    if (get_bits_left(gb) < 0) {
        av_log(avctx, AV_LOG_ERROR,
               "Overread VPS by %d bits\n", -get_bits_left(gb));
        if (ps->vps_list[vps_id])
            goto err;
    }

    remove_vps(ps, vps_id);
    ps->vps_list[vps_id] = vps;

    return 0;

err:
    ff_refstruct_unref(&vps);
    return ret;
}

static void decode_vui(GetBitContext *gb, AVCodecContext *avctx,
                       int apply_defdispwin, HEVCSPS *sps)
{
    VUI backup_vui, *vui = &sps->vui;
    GetBitContext backup;
    int alt = 0;

    ff_h2645_decode_common_vui_params(gb, &sps->vui.common, avctx);

    if (vui->common.video_signal_type_present_flag) {
        if (vui->common.video_full_range_flag && sps->pix_fmt == AV_PIX_FMT_YUV420P)
            sps->pix_fmt = AV_PIX_FMT_YUVJ420P;
        if (vui->common.colour_description_present_flag) {
            if (vui->common.matrix_coeffs == AVCOL_SPC_RGB) {
                switch (sps->pix_fmt) {
                case AV_PIX_FMT_YUV444P:
                    sps->pix_fmt = AV_PIX_FMT_GBRP;
                    break;
                case AV_PIX_FMT_YUV444P10:
                    sps->pix_fmt = AV_PIX_FMT_GBRP10;
                    break;
                case AV_PIX_FMT_YUV444P12:
                    sps->pix_fmt = AV_PIX_FMT_GBRP12;
                    break;
                }
            }
        }
    }

    vui->neutra_chroma_indication_flag = get_bits1(gb);
    vui->field_seq_flag                = get_bits1(gb);
    vui->frame_field_info_present_flag = get_bits1(gb);

    // Backup context in case an alternate header is detected
    memcpy(&backup, gb, sizeof(backup));
    memcpy(&backup_vui, vui, sizeof(backup_vui));
    if (get_bits_left(gb) >= 68 && show_bits(gb, 21) == 0x100000) {
        vui->default_display_window_flag = 0;
        av_log(avctx, AV_LOG_WARNING, "Invalid default display window\n");
    } else
        vui->default_display_window_flag = get_bits1(gb);

    if (vui->default_display_window_flag) {
        int vert_mult  = hevc_sub_height_c[sps->chroma_format_idc];
        int horiz_mult = hevc_sub_width_c[sps->chroma_format_idc];
        vui->def_disp_win.left_offset   = get_ue_golomb_long(gb) * horiz_mult;
        vui->def_disp_win.right_offset  = get_ue_golomb_long(gb) * horiz_mult;
        vui->def_disp_win.top_offset    = get_ue_golomb_long(gb) *  vert_mult;
        vui->def_disp_win.bottom_offset = get_ue_golomb_long(gb) *  vert_mult;

        if (apply_defdispwin &&
            avctx->flags2 & AV_CODEC_FLAG2_IGNORE_CROP) {
            av_log(avctx, AV_LOG_DEBUG,
                   "discarding vui default display window, "
                   "original values are l:%u r:%u t:%u b:%u\n",
                   vui->def_disp_win.left_offset,
                   vui->def_disp_win.right_offset,
                   vui->def_disp_win.top_offset,
                   vui->def_disp_win.bottom_offset);

            vui->def_disp_win.left_offset   =
            vui->def_disp_win.right_offset  =
            vui->def_disp_win.top_offset    =
            vui->def_disp_win.bottom_offset = 0;
        }
    }

timing_info:
    vui->vui_timing_info_present_flag = get_bits1(gb);

    if (vui->vui_timing_info_present_flag) {
        if( get_bits_left(gb) < 66 && !alt) {
            // The alternate syntax seem to have timing info located
            // at where def_disp_win is normally located
            av_log(avctx, AV_LOG_WARNING,
                   "Strange VUI timing information, retrying...\n");
            memcpy(vui, &backup_vui, sizeof(backup_vui));
            memcpy(gb, &backup, sizeof(backup));
            alt = 1;
            goto timing_info;
        }
        vui->vui_num_units_in_tick               = get_bits_long(gb, 32);
        vui->vui_time_scale                      = get_bits_long(gb, 32);
        if (alt) {
            av_log(avctx, AV_LOG_INFO, "Retry got %"PRIu32"/%"PRIu32" fps\n",
                   vui->vui_time_scale, vui->vui_num_units_in_tick);
        }
        vui->vui_poc_proportional_to_timing_flag = get_bits1(gb);
        if (vui->vui_poc_proportional_to_timing_flag)
            vui->vui_num_ticks_poc_diff_one_minus1 = get_ue_golomb_long(gb);
        vui->vui_hrd_parameters_present_flag = get_bits1(gb);
        if (vui->vui_hrd_parameters_present_flag)
            decode_hrd(gb, 1, &sps->hdr, sps->max_sub_layers);
    }

    vui->bitstream_restriction_flag = get_bits1(gb);
    if (vui->bitstream_restriction_flag) {
        if (get_bits_left(gb) < 8 && !alt) {
            av_log(avctx, AV_LOG_WARNING,
                   "Strange VUI bitstream restriction information, retrying"
                   " from timing information...\n");
            memcpy(vui, &backup_vui, sizeof(backup_vui));
            memcpy(gb, &backup, sizeof(backup));
            alt = 1;
            goto timing_info;
        }
        vui->tiles_fixed_structure_flag              = get_bits1(gb);
        vui->motion_vectors_over_pic_boundaries_flag = get_bits1(gb);
        vui->restricted_ref_pic_lists_flag           = get_bits1(gb);
        vui->min_spatial_segmentation_idc            = get_ue_golomb_long(gb);
        vui->max_bytes_per_pic_denom                 = get_ue_golomb_long(gb);
        vui->max_bits_per_min_cu_denom               = get_ue_golomb_long(gb);
        vui->log2_max_mv_length_horizontal           = get_ue_golomb_long(gb);
        vui->log2_max_mv_length_vertical             = get_ue_golomb_long(gb);
    }

    if (get_bits_left(gb) < 1 && !alt) {
        // XXX: Alternate syntax when sps_range_extension_flag != 0?
        av_log(avctx, AV_LOG_WARNING,
               "Overread in VUI, retrying from timing information...\n");
        memcpy(vui, &backup_vui, sizeof(backup_vui));
        memcpy(gb, &backup, sizeof(backup));
        alt = 1;
        goto timing_info;
    }
}

static void set_default_scaling_list_data(ScalingList *sl)
{
    int matrixId;

    for (matrixId = 0; matrixId < 6; matrixId++) {
        // 4x4 default is 16
        memset(sl->sl[0][matrixId], 16, 16);
        sl->sl_dc[0][matrixId] = 16; // default for 16x16
        sl->sl_dc[1][matrixId] = 16; // default for 32x32
    }
    memcpy(sl->sl[1][0], default_scaling_list_intra, 64);
    memcpy(sl->sl[1][1], default_scaling_list_intra, 64);
    memcpy(sl->sl[1][2], default_scaling_list_intra, 64);
    memcpy(sl->sl[1][3], default_scaling_list_inter, 64);
    memcpy(sl->sl[1][4], default_scaling_list_inter, 64);
    memcpy(sl->sl[1][5], default_scaling_list_inter, 64);
    memcpy(sl->sl[2][0], default_scaling_list_intra, 64);
    memcpy(sl->sl[2][1], default_scaling_list_intra, 64);
    memcpy(sl->sl[2][2], default_scaling_list_intra, 64);
    memcpy(sl->sl[2][3], default_scaling_list_inter, 64);
    memcpy(sl->sl[2][4], default_scaling_list_inter, 64);
    memcpy(sl->sl[2][5], default_scaling_list_inter, 64);
    memcpy(sl->sl[3][0], default_scaling_list_intra, 64);
    memcpy(sl->sl[3][1], default_scaling_list_intra, 64);
    memcpy(sl->sl[3][2], default_scaling_list_intra, 64);
    memcpy(sl->sl[3][3], default_scaling_list_inter, 64);
    memcpy(sl->sl[3][4], default_scaling_list_inter, 64);
    memcpy(sl->sl[3][5], default_scaling_list_inter, 64);
}

static int scaling_list_data(GetBitContext *gb, AVCodecContext *avctx,
                             ScalingList *sl, const HEVCSPS *sps)
{
    uint8_t scaling_list_pred_mode_flag;
    uint8_t scaling_list_dc_coef[2][6];
    int size_id, matrix_id, pos;
    int i;

    for (size_id = 0; size_id < 4; size_id++)
        for (matrix_id = 0; matrix_id < 6; matrix_id += ((size_id == 3) ? 3 : 1)) {
            scaling_list_pred_mode_flag = get_bits1(gb);
            if (!scaling_list_pred_mode_flag) {
                unsigned int delta = get_ue_golomb_long(gb);
                /* Only need to handle non-zero delta. Zero means default,
                 * which should already be in the arrays. */
                if (delta) {
                    // Copy from previous array.
                    delta *= (size_id == 3) ? 3 : 1;
                    if (matrix_id < delta) {
                        av_log(avctx, AV_LOG_ERROR,
                               "Invalid delta in scaling list data: %d.\n", delta);
                        return AVERROR_INVALIDDATA;
                    }

                    memcpy(sl->sl[size_id][matrix_id],
                           sl->sl[size_id][matrix_id - delta],
                           size_id > 0 ? 64 : 16);
                    if (size_id > 1)
                        sl->sl_dc[size_id - 2][matrix_id] = sl->sl_dc[size_id - 2][matrix_id - delta];
                }
            } else {
                int next_coef, coef_num;
                int32_t scaling_list_delta_coef;

                next_coef = 8;
                coef_num  = FFMIN(64, 1 << (4 + (size_id << 1)));
                if (size_id > 1) {
                    int scaling_list_coeff_minus8 = get_se_golomb(gb);
                    if (scaling_list_coeff_minus8 < -7 ||
                        scaling_list_coeff_minus8 > 247)
                        return AVERROR_INVALIDDATA;
                    scaling_list_dc_coef[size_id - 2][matrix_id] = scaling_list_coeff_minus8 + 8;
                    next_coef = scaling_list_dc_coef[size_id - 2][matrix_id];
                    sl->sl_dc[size_id - 2][matrix_id] = next_coef;
                }
                for (i = 0; i < coef_num; i++) {
                    if (size_id == 0)
                        pos = 4 * ff_hevc_diag_scan4x4_y[i] +
                                  ff_hevc_diag_scan4x4_x[i];
                    else
                        pos = 8 * ff_hevc_diag_scan8x8_y[i] +
                                  ff_hevc_diag_scan8x8_x[i];

                    scaling_list_delta_coef = get_se_golomb(gb);
                    next_coef = (next_coef + 256U + scaling_list_delta_coef) % 256;
                    sl->sl[size_id][matrix_id][pos] = next_coef;
                }
            }
        }

    if (sps->chroma_format_idc == 3) {
        for (i = 0; i < 64; i++) {
            sl->sl[3][1][i] = sl->sl[2][1][i];
            sl->sl[3][2][i] = sl->sl[2][2][i];
            sl->sl[3][4][i] = sl->sl[2][4][i];
            sl->sl[3][5][i] = sl->sl[2][5][i];
        }
        sl->sl_dc[1][1] = sl->sl_dc[0][1];
        sl->sl_dc[1][2] = sl->sl_dc[0][2];
        sl->sl_dc[1][4] = sl->sl_dc[0][4];
        sl->sl_dc[1][5] = sl->sl_dc[0][5];
    }


    return 0;
}

static int map_pixel_format(AVCodecContext *avctx, HEVCSPS *sps)
{
    const AVPixFmtDescriptor *desc;
    switch (sps->bit_depth) {
    case 8:
        if (sps->chroma_format_idc == 0) sps->pix_fmt = AV_PIX_FMT_GRAY8;
        if (sps->chroma_format_idc == 1) sps->pix_fmt = AV_PIX_FMT_YUV420P;
        if (sps->chroma_format_idc == 2) sps->pix_fmt = AV_PIX_FMT_YUV422P;
        if (sps->chroma_format_idc == 3) sps->pix_fmt = AV_PIX_FMT_YUV444P;
       break;
    case 9:
        if (sps->chroma_format_idc == 0) sps->pix_fmt = AV_PIX_FMT_GRAY9;
        if (sps->chroma_format_idc == 1) sps->pix_fmt = AV_PIX_FMT_YUV420P9;
        if (sps->chroma_format_idc == 2) sps->pix_fmt = AV_PIX_FMT_YUV422P9;
        if (sps->chroma_format_idc == 3) sps->pix_fmt = AV_PIX_FMT_YUV444P9;
        break;
    case 10:
        if (sps->chroma_format_idc == 0) sps->pix_fmt = AV_PIX_FMT_GRAY10;
        if (sps->chroma_format_idc == 1) sps->pix_fmt = AV_PIX_FMT_YUV420P10;
        if (sps->chroma_format_idc == 2) sps->pix_fmt = AV_PIX_FMT_YUV422P10;
        if (sps->chroma_format_idc == 3) sps->pix_fmt = AV_PIX_FMT_YUV444P10;
        break;
    case 12:
        if (sps->chroma_format_idc == 0) sps->pix_fmt = AV_PIX_FMT_GRAY12;
        if (sps->chroma_format_idc == 1) sps->pix_fmt = AV_PIX_FMT_YUV420P12;
        if (sps->chroma_format_idc == 2) sps->pix_fmt = AV_PIX_FMT_YUV422P12;
        if (sps->chroma_format_idc == 3) sps->pix_fmt = AV_PIX_FMT_YUV444P12;
        break;
    default:
        av_log(avctx, AV_LOG_ERROR,
               "The following bit-depths are currently specified: 8, 9, 10 and 12 bits, "
               "chroma_format_idc is %d, depth is %d\n",
               sps->chroma_format_idc, sps->bit_depth);
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

int ff_hevc_parse_sps(HEVCSPS *sps, GetBitContext *gb, unsigned int *sps_id,
                      unsigned nuh_layer_id, int apply_defdispwin,
                      const HEVCVPS * const *vps_list, AVCodecContext *avctx)
{
    HEVCWindow *ow;
    int ret = 0;
    int bit_depth_chroma, num_comps, multi_layer_ext;
    int vps_max_sub_layers;
    int i;

    // Coded parameters

    sps->vps_id = get_bits(gb, 4);

    if (vps_list) {
        if (!vps_list[sps->vps_id]) {
            av_log(avctx, AV_LOG_ERROR, "VPS %d does not exist\n",
                   sps->vps_id);
            return AVERROR_INVALIDDATA;
        }
        sps->vps = ff_refstruct_ref_c(vps_list[sps->vps_id]);
    }

    sps->max_sub_layers = get_bits(gb, 3) + 1;
    multi_layer_ext = nuh_layer_id > 0 &&
                      sps->max_sub_layers == HEVC_MAX_SUB_LAYERS + 1;
    if (multi_layer_ext) {
        if (!sps->vps)
            return AVERROR(EINVAL);

        sps->max_sub_layers = sps->vps->vps_max_sub_layers;
    }
    vps_max_sub_layers = sps->vps ? sps->vps->vps_max_sub_layers
                                  : FFMIN(sps->max_sub_layers, HEVC_MAX_SUB_LAYERS);

    if (sps->max_sub_layers > vps_max_sub_layers) {
        av_log(avctx, AV_LOG_ERROR, "sps_max_sub_layers out of range: %d\n",
               sps->max_sub_layers);
        return AVERROR_INVALIDDATA;
    }

    if (!multi_layer_ext) {
        sps->temporal_id_nesting = get_bits(gb, 1);

        if ((ret = parse_ptl(gb, avctx, 1, &sps->ptl, sps->max_sub_layers)) < 0)
            return ret;
    } else {
        sps->temporal_id_nesting = sps->max_sub_layers > 1 ?
                                   sps->vps->vps_max_sub_layers : 1;
    }

    *sps_id = get_ue_golomb_long(gb);
    if (*sps_id >= HEVC_MAX_SPS_COUNT) {
        av_log(avctx, AV_LOG_ERROR, "SPS id out of range: %d\n", *sps_id);
        return AVERROR_INVALIDDATA;
    }

    if (multi_layer_ext) {
        const RepFormat *rf = &sps->vps->rep_format;

        if (sps->vps->nb_layers == 1) {
            av_log(avctx, AV_LOG_WARNING, "SPS %d references an unsupported VPS extension. Ignoring\n",
                   *sps_id);
            return AVERROR(ENOSYS);
        }

        if (get_bits1(gb) &&    // update_rep_format_flag
            get_bits(gb, 8)) {  // sps_rep_format_idx
            av_log(avctx, AV_LOG_ERROR, "sps_rep_format_idx!=0\n");
            return AVERROR_PATCHWELCOME;
        }

        sps->separate_colour_plane = rf->separate_colour_plane_flag;
        sps->chroma_format_idc     = sps->separate_colour_plane ? 0 :
                                     rf->chroma_format_idc;
        sps->bit_depth             = rf->bit_depth_luma;
        sps->width                 = rf->pic_width_in_luma_samples;
        sps->height                = rf->pic_height_in_luma_samples;

        sps->pic_conf_win.left_offset   = rf->conf_win_left_offset;
        sps->pic_conf_win.right_offset  = rf->conf_win_right_offset;
        sps->pic_conf_win.top_offset    = rf->conf_win_top_offset;
        sps->pic_conf_win.bottom_offset = rf->conf_win_bottom_offset;

    } else {
        sps->chroma_format_idc = get_ue_golomb_long(gb);
        if (sps->chroma_format_idc > 3U) {
            av_log(avctx, AV_LOG_ERROR, "chroma_format_idc %d is invalid\n", sps->chroma_format_idc);
            return AVERROR_INVALIDDATA;
        }

        if (sps->chroma_format_idc == 3)
            sps->separate_colour_plane = get_bits1(gb);

        if (sps->separate_colour_plane)
            sps->chroma_format_idc = 0;

        sps->width  = get_ue_golomb_long(gb);
        sps->height = get_ue_golomb_long(gb);
        if ((ret = av_image_check_size(sps->width,
                                       sps->height, 0, avctx)) < 0)
            return ret;

        sps->conformance_window = get_bits1(gb);
        if (sps->conformance_window) {
            int vert_mult  = hevc_sub_height_c[sps->chroma_format_idc];
            int horiz_mult = hevc_sub_width_c[sps->chroma_format_idc];
            sps->pic_conf_win.left_offset   = get_ue_golomb_long(gb) * horiz_mult;
            sps->pic_conf_win.right_offset  = get_ue_golomb_long(gb) * horiz_mult;
            sps->pic_conf_win.top_offset    = get_ue_golomb_long(gb) *  vert_mult;
            sps->pic_conf_win.bottom_offset = get_ue_golomb_long(gb) *  vert_mult;

            if (avctx->flags2 & AV_CODEC_FLAG2_IGNORE_CROP) {
                av_log(avctx, AV_LOG_DEBUG,
                       "discarding sps conformance window, "
                       "original values are l:%u r:%u t:%u b:%u\n",
                       sps->pic_conf_win.left_offset,
                       sps->pic_conf_win.right_offset,
                       sps->pic_conf_win.top_offset,
                       sps->pic_conf_win.bottom_offset);

                sps->pic_conf_win.left_offset   =
                sps->pic_conf_win.right_offset  =
                sps->pic_conf_win.top_offset    =
                sps->pic_conf_win.bottom_offset = 0;
            }
        }

        sps->bit_depth = get_ue_golomb_31(gb) + 8;
        if (sps->bit_depth > 16) {
            av_log(avctx, AV_LOG_ERROR, "Luma bit depth (%d) is out of range\n",
                   sps->bit_depth);
            return AVERROR_INVALIDDATA;
        }
        bit_depth_chroma = get_ue_golomb_31(gb) + 8;
        if (bit_depth_chroma > 16) {
            av_log(avctx, AV_LOG_ERROR, "Chroma bit depth (%d) is out of range\n",
                   bit_depth_chroma);
            return AVERROR_INVALIDDATA;
        }
        if (sps->chroma_format_idc && bit_depth_chroma != sps->bit_depth) {
            av_log(avctx, AV_LOG_ERROR,
                   "Luma bit depth (%d) is different from chroma bit depth (%d), "
                   "this is unsupported.\n",
                   sps->bit_depth, bit_depth_chroma);
            return AVERROR_INVALIDDATA;
        }
        sps->bit_depth_chroma = bit_depth_chroma;
    }

    sps->output_window = sps->pic_conf_win;

    ret = map_pixel_format(avctx, sps);
    if (ret < 0)
        return ret;

    sps->log2_max_poc_lsb = get_ue_golomb_long(gb) + 4;
    if (sps->log2_max_poc_lsb > 16) {
        av_log(avctx, AV_LOG_ERROR, "log2_max_pic_order_cnt_lsb_minus4 out range: %d\n",
               sps->log2_max_poc_lsb - 4);
        return AVERROR_INVALIDDATA;
    }

    if (!multi_layer_ext) {
        int start;

        sps->sublayer_ordering_info = get_bits1(gb);
        start = sps->sublayer_ordering_info ? 0 : sps->max_sub_layers - 1;
        for (i = start; i < sps->max_sub_layers; i++) {
            sps->temporal_layer[i].max_dec_pic_buffering = get_ue_golomb_long(gb) + 1;
            sps->temporal_layer[i].num_reorder_pics      = get_ue_golomb_long(gb);
            sps->temporal_layer[i].max_latency_increase  = get_ue_golomb_long(gb) - 1;
            if (sps->temporal_layer[i].max_dec_pic_buffering > (unsigned)HEVC_MAX_DPB_SIZE) {
                av_log(avctx, AV_LOG_ERROR, "sps_max_dec_pic_buffering_minus1 out of range: %d\n",
                       sps->temporal_layer[i].max_dec_pic_buffering - 1U);
                return AVERROR_INVALIDDATA;
            }
            if (sps->temporal_layer[i].num_reorder_pics > sps->temporal_layer[i].max_dec_pic_buffering - 1) {
                av_log(avctx, AV_LOG_WARNING, "sps_max_num_reorder_pics out of range: %d\n",
                       sps->temporal_layer[i].num_reorder_pics);
                if (avctx->err_recognition & AV_EF_EXPLODE ||
                    sps->temporal_layer[i].num_reorder_pics > HEVC_MAX_DPB_SIZE - 1) {
                    return AVERROR_INVALIDDATA;
                }
                sps->temporal_layer[i].max_dec_pic_buffering = sps->temporal_layer[i].num_reorder_pics + 1;
            }
        }

        if (!sps->sublayer_ordering_info) {
            for (i = 0; i < start; i++) {
                sps->temporal_layer[i].max_dec_pic_buffering = sps->temporal_layer[start].max_dec_pic_buffering;
                sps->temporal_layer[i].num_reorder_pics      = sps->temporal_layer[start].num_reorder_pics;
                sps->temporal_layer[i].max_latency_increase  = sps->temporal_layer[start].max_latency_increase;
            }
        }
    } else {
        for (int i = 0; i < sps->max_sub_layers; i++) {
            sps->temporal_layer[i].max_dec_pic_buffering = sps->vps->dpb_size.max_dec_pic_buffering;
            sps->temporal_layer[i].num_reorder_pics      = sps->vps->dpb_size.max_num_reorder_pics;
            sps->temporal_layer[i].max_latency_increase  = sps->vps->dpb_size.max_latency_increase;
        }
    }

    sps->log2_min_cb_size                       = get_ue_golomb_long(gb) + 3;
    sps->log2_diff_max_min_coding_block_size    = get_ue_golomb_long(gb);
    sps->log2_min_tb_size                       = get_ue_golomb_long(gb) + 2;
    sps->log2_diff_max_min_transform_block_size = get_ue_golomb_long(gb);
    sps->log2_max_trafo_size                    = sps->log2_diff_max_min_transform_block_size +
                                                  sps->log2_min_tb_size;

    if (sps->log2_min_cb_size < 3 || sps->log2_min_cb_size > 30) {
        av_log(avctx, AV_LOG_ERROR, "Invalid value %d for log2_min_cb_size", sps->log2_min_cb_size);
        return AVERROR_INVALIDDATA;
    }

    if (sps->log2_diff_max_min_coding_block_size > 30) {
        av_log(avctx, AV_LOG_ERROR, "Invalid value %d for log2_diff_max_min_coding_block_size", sps->log2_diff_max_min_coding_block_size);
        return AVERROR_INVALIDDATA;
    }

    if (sps->log2_min_tb_size >= sps->log2_min_cb_size || sps->log2_min_tb_size < 2) {
        av_log(avctx, AV_LOG_ERROR, "Invalid value for log2_min_tb_size");
        return AVERROR_INVALIDDATA;
    }

    if (sps->log2_diff_max_min_transform_block_size > 30) {
        av_log(avctx, AV_LOG_ERROR, "Invalid value %d for log2_diff_max_min_transform_block_size",
               sps->log2_diff_max_min_transform_block_size);
        return AVERROR_INVALIDDATA;
    }

    sps->max_transform_hierarchy_depth_inter = get_ue_golomb_long(gb);
    sps->max_transform_hierarchy_depth_intra = get_ue_golomb_long(gb);

    sps->scaling_list_enabled = get_bits1(gb);
    if (sps->scaling_list_enabled) {
        set_default_scaling_list_data(&sps->scaling_list);

        if (multi_layer_ext && get_bits1(gb)) { // sps_infer_scaling_list_flag
            av_log(avctx, AV_LOG_ERROR, "sps_infer_scaling_list_flag=1 not supported\n");
            return AVERROR_PATCHWELCOME;
        }

        if (get_bits1(gb)) {
            ret = scaling_list_data(gb, avctx, &sps->scaling_list, sps);
            if (ret < 0)
                return ret;
        }
    }

    sps->amp_enabled = get_bits1(gb);
    sps->sao_enabled = get_bits1(gb);

    sps->pcm_enabled = get_bits1(gb);
    if (sps->pcm_enabled) {
        sps->pcm.bit_depth   = get_bits(gb, 4) + 1;
        sps->pcm.bit_depth_chroma = get_bits(gb, 4) + 1;
        sps->pcm.log2_min_pcm_cb_size = get_ue_golomb_long(gb) + 3;
        sps->pcm.log2_max_pcm_cb_size = sps->pcm.log2_min_pcm_cb_size +
                                        get_ue_golomb_long(gb);
        if (FFMAX(sps->pcm.bit_depth, sps->pcm.bit_depth_chroma) > sps->bit_depth) {
            av_log(avctx, AV_LOG_ERROR,
                   "PCM bit depth (%d, %d) is greater than normal bit depth (%d)\n",
                   sps->pcm.bit_depth, sps->pcm.bit_depth_chroma, sps->bit_depth);
            return AVERROR_INVALIDDATA;
        }

        sps->pcm_loop_filter_disabled = get_bits1(gb);
    }

    sps->nb_st_rps = get_ue_golomb_long(gb);
    if (sps->nb_st_rps > HEVC_MAX_SHORT_TERM_REF_PIC_SETS) {
        av_log(avctx, AV_LOG_ERROR, "Too many short term RPS: %d.\n",
               sps->nb_st_rps);
        return AVERROR_INVALIDDATA;
    }
    for (i = 0; i < sps->nb_st_rps; i++) {
        if ((ret = ff_hevc_decode_short_term_rps(gb, avctx, &sps->st_rps[i],
                                                 sps, 0)) < 0)
            return ret;
    }

    sps->long_term_ref_pics_present = get_bits1(gb);
    if (sps->long_term_ref_pics_present) {
        sps->num_long_term_ref_pics_sps = get_ue_golomb_long(gb);
        if (sps->num_long_term_ref_pics_sps > HEVC_MAX_LONG_TERM_REF_PICS) {
            av_log(avctx, AV_LOG_ERROR, "Too many long term ref pics: %d.\n",
                   sps->num_long_term_ref_pics_sps);
            return AVERROR_INVALIDDATA;
        }

        sps->used_by_curr_pic_lt = 0;
        for (i = 0; i < sps->num_long_term_ref_pics_sps; i++) {
            sps->lt_ref_pic_poc_lsb_sps[i]       = get_bits(gb, sps->log2_max_poc_lsb);
            sps->used_by_curr_pic_lt            |= get_bits1(gb) << i;
        }
    }

    sps->temporal_mvp_enabled           = get_bits1(gb);
    sps->strong_intra_smoothing_enabled = get_bits1(gb);
    sps->vui.common.sar = (AVRational){0, 1};
    sps->vui_present = get_bits1(gb);
    if (sps->vui_present)
        decode_vui(gb, avctx, apply_defdispwin, sps);

    sps->extension_present = get_bits1(gb);
    if (sps->extension_present) {
        sps->range_extension               = get_bits1(gb);
        sps->multilayer_extension          = get_bits1(gb);
        sps->sps_3d_extension              = get_bits1(gb);
        sps->scc_extension                 = get_bits1(gb);
        skip_bits(gb, 4); // sps_extension_4bits

        if (sps->range_extension) {
            sps->transform_skip_rotation_enabled = get_bits1(gb);
            sps->transform_skip_context_enabled  = get_bits1(gb);
            sps->implicit_rdpcm_enabled          = get_bits1(gb);
            sps->explicit_rdpcm_enabled          = get_bits1(gb);

            sps->extended_precision_processing   = get_bits1(gb);
            if (sps->extended_precision_processing)
                av_log(avctx, AV_LOG_WARNING,
                   "extended_precision_processing_flag not yet implemented\n");

            sps->intra_smoothing_disabled        = get_bits1(gb);
            sps->high_precision_offsets_enabled  = get_bits1(gb);
            if (sps->high_precision_offsets_enabled)
                av_log(avctx, AV_LOG_WARNING,
                   "high_precision_offsets_enabled_flag not yet implemented\n");

            sps->persistent_rice_adaptation_enabled = get_bits1(gb);

            sps->cabac_bypass_alignment_enabled     = get_bits1(gb);
            if (sps->cabac_bypass_alignment_enabled)
                av_log(avctx, AV_LOG_WARNING,
                   "cabac_bypass_alignment_enabled_flag not yet implemented\n");
        }

        if (sps->multilayer_extension) {
            skip_bits1(gb); // inter_view_mv_vert_constraint_flag
        }

        if (sps->sps_3d_extension) {
            for (i = 0; i <= 1; i++) {
                skip_bits1(gb); // iv_di_mc_enabled_flag
                skip_bits1(gb); // iv_mv_scal_enabled_flag
                if (i == 0) {
                    get_ue_golomb_long(gb); // log2_ivmc_sub_pb_size_minus3
                    skip_bits1(gb); // iv_res_pred_enabled_flag
                    skip_bits1(gb); // depth_ref_enabled_flag
                    skip_bits1(gb); // vsp_mc_enabled_flag
                    skip_bits1(gb); // dbbp_enabled_flag
                } else {
                    skip_bits1(gb); // tex_mc_enabled_flag
                    get_ue_golomb_long(gb); // log2_ivmc_sub_pb_size_minus3
                    skip_bits1(gb); // intra_contour_enabled_flag
                    skip_bits1(gb); // intra_dc_only_wedge_enabled_flag
                    skip_bits1(gb); // cqt_cu_part_pred_enabled_flag
                    skip_bits1(gb); // inter_dc_only_enabled_flag
                    skip_bits1(gb); // skip_intra_enabled_flag
                }
            }
            av_log(avctx, AV_LOG_WARNING,
                   "sps_3d_extension_flag not yet implemented\n");
        }

        if (sps->scc_extension) {
            sps->curr_pic_ref_enabled = get_bits1(gb);
            sps->palette_mode_enabled = get_bits1(gb);
            if (sps->palette_mode_enabled) {
                sps->palette_max_size = get_ue_golomb(gb);
                sps->delta_palette_max_predictor_size = get_ue_golomb(gb);
                sps->palette_predictor_initializers_present = get_bits1(gb);

                if (sps->palette_predictor_initializers_present) {
                    sps->sps_num_palette_predictor_initializers = get_ue_golomb(gb) + 1;
                    if (sps->sps_num_palette_predictor_initializers > HEVC_MAX_PALETTE_PREDICTOR_SIZE) {
                        av_log(avctx, AV_LOG_ERROR,
                               "sps_num_palette_predictor_initializers out of range: %u\n",
                               sps->sps_num_palette_predictor_initializers);
                        return AVERROR_INVALIDDATA;
                    }
                    num_comps = !sps->chroma_format_idc ? 1 : 3;
                    for (int comp = 0; comp < num_comps; comp++) {
                        int bit_depth = !comp ? sps->bit_depth : sps->bit_depth_chroma;
                        for (i = 0; i < sps->sps_num_palette_predictor_initializers; i++)
                            sps->sps_palette_predictor_initializer[comp][i] = get_bits(gb, bit_depth);
                    }
                }
            }
            sps->motion_vector_resolution_control_idc   = get_bits(gb, 2);
            sps->intra_boundary_filtering_disabled      = get_bits1(gb);
        }
    }
    if (apply_defdispwin) {
        sps->output_window.left_offset   += sps->vui.def_disp_win.left_offset;
        sps->output_window.right_offset  += sps->vui.def_disp_win.right_offset;
        sps->output_window.top_offset    += sps->vui.def_disp_win.top_offset;
        sps->output_window.bottom_offset += sps->vui.def_disp_win.bottom_offset;
    }

    ow = &sps->output_window;
    if (ow->left_offset >= INT_MAX - ow->right_offset     ||
        ow->top_offset  >= INT_MAX - ow->bottom_offset    ||
        ow->left_offset + ow->right_offset  >= sps->width ||
        ow->top_offset  + ow->bottom_offset >= sps->height) {
        av_log(avctx, AV_LOG_WARNING, "Invalid cropping offsets: %u/%u/%u/%u\n",
               ow->left_offset, ow->right_offset, ow->top_offset, ow->bottom_offset);
        if (avctx->err_recognition & AV_EF_EXPLODE) {
            return AVERROR_INVALIDDATA;
        }
        av_log(avctx, AV_LOG_WARNING,
               "Displaying the whole video surface.\n");
        memset(ow, 0, sizeof(*ow));
        memset(&sps->pic_conf_win, 0, sizeof(sps->pic_conf_win));
    }

    // Inferred parameters
    sps->log2_ctb_size = sps->log2_min_cb_size +
                         sps->log2_diff_max_min_coding_block_size;
    sps->log2_min_pu_size = sps->log2_min_cb_size - 1;

    if (sps->log2_ctb_size > HEVC_MAX_LOG2_CTB_SIZE) {
        av_log(avctx, AV_LOG_ERROR, "CTB size out of range: 2^%d\n", sps->log2_ctb_size);
        return AVERROR_INVALIDDATA;
    }
    if (sps->log2_ctb_size < 4) {
        av_log(avctx,
               AV_LOG_ERROR,
               "log2_ctb_size %d differs from the bounds of any known profile\n",
               sps->log2_ctb_size);
        avpriv_request_sample(avctx, "log2_ctb_size %d", sps->log2_ctb_size);
        return AVERROR_INVALIDDATA;
    }

    sps->ctb_width  = (sps->width  + (1 << sps->log2_ctb_size) - 1) >> sps->log2_ctb_size;
    sps->ctb_height = (sps->height + (1 << sps->log2_ctb_size) - 1) >> sps->log2_ctb_size;
    sps->ctb_size   = sps->ctb_width * sps->ctb_height;

    sps->min_cb_width  = sps->width  >> sps->log2_min_cb_size;
    sps->min_cb_height = sps->height >> sps->log2_min_cb_size;
    sps->min_tb_width  = sps->width  >> sps->log2_min_tb_size;
    sps->min_tb_height = sps->height >> sps->log2_min_tb_size;
    sps->min_pu_width  = sps->width  >> sps->log2_min_pu_size;
    sps->min_pu_height = sps->height >> sps->log2_min_pu_size;
    sps->tb_mask       = (1 << (sps->log2_ctb_size - sps->log2_min_tb_size)) - 1;

    sps->qp_bd_offset = 6 * (sps->bit_depth - 8);

    if (av_zero_extend(sps->width, sps->log2_min_cb_size) ||
        av_zero_extend(sps->height, sps->log2_min_cb_size)) {
        av_log(avctx, AV_LOG_ERROR, "Invalid coded frame dimensions.\n");
        return AVERROR_INVALIDDATA;
    }

    if (sps->max_transform_hierarchy_depth_inter > sps->log2_ctb_size - sps->log2_min_tb_size) {
        av_log(avctx, AV_LOG_ERROR, "max_transform_hierarchy_depth_inter out of range: %d\n",
               sps->max_transform_hierarchy_depth_inter);
        return AVERROR_INVALIDDATA;
    }
    if (sps->max_transform_hierarchy_depth_intra > sps->log2_ctb_size - sps->log2_min_tb_size) {
        av_log(avctx, AV_LOG_ERROR, "max_transform_hierarchy_depth_intra out of range: %d\n",
               sps->max_transform_hierarchy_depth_intra);
        return AVERROR_INVALIDDATA;
    }
    if (sps->log2_max_trafo_size > FFMIN(sps->log2_ctb_size, 5)) {
        av_log(avctx, AV_LOG_ERROR,
               "max transform block size out of range: %d\n",
               sps->log2_max_trafo_size);
        return AVERROR_INVALIDDATA;
    }

    if (get_bits_left(gb) < 0) {
        av_log(avctx, AV_LOG_ERROR,
               "Overread SPS by %d bits\n", -get_bits_left(gb));
        return AVERROR_INVALIDDATA;
    }

    return 0;
}

static void hevc_sps_free(FFRefStructOpaque opaque, void *obj)
{
    HEVCSPS *sps = obj;

    ff_refstruct_unref(&sps->vps);

    av_freep(&sps->data);
}

static int compare_sps(const HEVCSPS *sps1, const HEVCSPS *sps2)
{
    return sps1->data_size == sps2->data_size &&
           !memcmp(sps1->data, sps2->data, sps1->data_size);
}

int ff_hevc_decode_nal_sps(GetBitContext *gb, AVCodecContext *avctx,
                           HEVCParamSets *ps, unsigned nuh_layer_id,
                           int apply_defdispwin)
{
    HEVCSPS *sps = ff_refstruct_alloc_ext(sizeof(*sps), 0, NULL, hevc_sps_free);
    unsigned int sps_id;
    int ret;

    if (!sps)
        return AVERROR(ENOMEM);

    av_log(avctx, AV_LOG_DEBUG, "Decoding SPS\n");

    sps->data_size = gb->buffer_end - gb->buffer;
    sps->data = av_memdup(gb->buffer, sps->data_size);
    if (!sps->data) {
        ret = AVERROR(ENOMEM);
        goto err;
    }

    ret = ff_hevc_parse_sps(sps, gb, &sps_id,
                            nuh_layer_id, apply_defdispwin,
                            ps->vps_list, avctx);
    if (ret < 0)
        goto err;

    if (avctx->debug & FF_DEBUG_BITSTREAM) {
        av_log(avctx, AV_LOG_DEBUG,
               "Parsed SPS: id %d; coded wxh: %dx%d; "
               "cropped wxh: %dx%d; pix_fmt: %s.\n",
               sps_id, sps->width, sps->height,
               sps->width - (sps->output_window.left_offset + sps->output_window.right_offset),
               sps->height - (sps->output_window.top_offset + sps->output_window.bottom_offset),
               av_get_pix_fmt_name(sps->pix_fmt));
    }

    /* check if this is a repeat of an already parsed SPS, then keep the
     * original one.
     * otherwise drop all PPSes that depend on it */
    if (ps->sps_list[sps_id] &&
        compare_sps(ps->sps_list[sps_id], sps)) {
        ff_refstruct_unref(&sps);
    } else {
        remove_sps(ps, sps_id);
        ps->sps_list[sps_id] = sps;
    }

    return 0;
err:
    ff_refstruct_unref(&sps);
    return ret;
}

static void hevc_pps_free(FFRefStructOpaque unused, void *obj)
{
    HEVCPPS *pps = obj;

    ff_refstruct_unref(&pps->sps);

    av_freep(&pps->column_width);
    av_freep(&pps->row_height);
    av_freep(&pps->col_bd);
    av_freep(&pps->row_bd);
    av_freep(&pps->col_idxX);
    av_freep(&pps->ctb_addr_rs_to_ts);
    av_freep(&pps->ctb_addr_ts_to_rs);
    av_freep(&pps->tile_pos_rs);
    av_freep(&pps->tile_id);
    av_freep(&pps->min_tb_addr_zs_tab);
    av_freep(&pps->data);
}

static void colour_mapping_octants(GetBitContext *gb, HEVCPPS *pps, int inp_depth,
                                   int idx_y, int idx_cb, int idx_cr, int inp_length)
{
    unsigned int split_octant_flag, part_num_y, coded_res_flag, res_coeff_q, res_coeff_r;
    int cm_res_bits;

    part_num_y = 1 << pps->cm_y_part_num_log2;

    split_octant_flag = inp_depth < pps->cm_octant_depth ? get_bits1(gb) : 0;

    if (split_octant_flag)
        for (int k = 0; k < 2; k++)
            for (int m = 0; m < 2; m++)
                for (int n = 0; n < 2; n++)
                    colour_mapping_octants(gb, pps, inp_depth + 1,
                                           idx_y + part_num_y * k * inp_length / 2,
                                           idx_cb + m * inp_length / 2,
                                           idx_cr + n * inp_length / 2,
                                           inp_length / 2);
    else
        for (int i = 0; i < part_num_y; i++) {
            for (int j = 0; j < 4; j++) {
                coded_res_flag = get_bits1(gb);
                if (coded_res_flag)
                    for (int c = 0; c < 3; c++) {
                        res_coeff_q = get_ue_golomb_long(gb);
                        cm_res_bits = FFMAX(0, 10 + pps->luma_bit_depth_cm_input -
                                            pps->luma_bit_depth_cm_output -
                                            pps->cm_res_quant_bits - pps->cm_delta_flc_bits);
                        res_coeff_r = cm_res_bits ? get_bits(gb, cm_res_bits) : 0;
                        if (res_coeff_q || res_coeff_r)
                            skip_bits1(gb);
                    }
            }
        }
}

static int colour_mapping_table(GetBitContext *gb, AVCodecContext *avctx, HEVCPPS *pps)
{
    pps->num_cm_ref_layers = get_ue_golomb(gb) + 1;
    if (pps->num_cm_ref_layers > 62) {
        av_log(avctx, AV_LOG_ERROR,
               "num_cm_ref_layers_minus1 shall be in the range [0, 61].\n");
        return AVERROR_INVALIDDATA;
    }
    for (int i = 0; i < pps->num_cm_ref_layers; i++)
        pps->cm_ref_layer_id[i] = get_bits(gb, 6);

    pps->cm_octant_depth = get_bits(gb, 2);
    pps->cm_y_part_num_log2 = get_bits(gb, 2);

    pps->luma_bit_depth_cm_input    = get_ue_golomb(gb) + 8;
    pps->chroma_bit_depth_cm_input  = get_ue_golomb(gb) + 8;
    pps->luma_bit_depth_cm_output   = get_ue_golomb(gb) + 8;
    pps->chroma_bit_depth_cm_output = get_ue_golomb(gb) + 8;

    pps->cm_res_quant_bits = get_bits(gb, 2);
    pps->cm_delta_flc_bits = get_bits(gb, 2) + 1;

    if (pps->cm_octant_depth == 1) {
        pps->cm_adapt_threshold_u_delta = get_se_golomb_long(gb);
        pps->cm_adapt_threshold_v_delta = get_se_golomb_long(gb);
    }

    colour_mapping_octants(gb, pps, 0, 0, 0, 0, 1 << pps->cm_octant_depth);

    return 0;
}

static int pps_multilayer_extension(GetBitContext *gb, AVCodecContext *avctx,
                                    HEVCPPS *pps, const HEVCSPS *sps, const HEVCVPS *vps)
{
    pps->poc_reset_info_present_flag = get_bits1(gb);
    pps->pps_infer_scaling_list_flag = get_bits1(gb);
    if (pps->pps_infer_scaling_list_flag)
        pps->pps_scaling_list_ref_layer_id = get_bits(gb, 6);

    pps->num_ref_loc_offsets = get_ue_golomb(gb);
    if (pps->num_ref_loc_offsets > vps->vps_max_layers - 1)
        return AVERROR_INVALIDDATA;

    for (int i = 0; i < pps->num_ref_loc_offsets; i++) {
        pps->ref_loc_offset_layer_id[i] = get_bits(gb, 6);
        pps->scaled_ref_layer_offset_present_flag[i] = get_bits1(gb);
        if (pps->scaled_ref_layer_offset_present_flag[i]) {
            pps->scaled_ref_layer_left_offset[pps->ref_loc_offset_layer_id[i]]   = get_se_golomb_long(gb);
            pps->scaled_ref_layer_top_offset[pps->ref_loc_offset_layer_id[i]]    = get_se_golomb_long(gb);
            pps->scaled_ref_layer_right_offset[pps->ref_loc_offset_layer_id[i]]  = get_se_golomb_long(gb);
            pps->scaled_ref_layer_bottom_offset[pps->ref_loc_offset_layer_id[i]] = get_se_golomb_long(gb);
        }

        pps->ref_region_offset_present_flag[i] = get_bits1(gb);
        if (pps->ref_region_offset_present_flag[i]) {
            pps->ref_region_left_offset[pps->ref_loc_offset_layer_id[i]]   = get_se_golomb_long(gb);
            pps->ref_region_top_offset[pps->ref_loc_offset_layer_id[i]]    = get_se_golomb_long(gb);
            pps->ref_region_right_offset[pps->ref_loc_offset_layer_id[i]]  = get_se_golomb_long(gb);
            pps->ref_region_bottom_offset[pps->ref_loc_offset_layer_id[i]] = get_se_golomb_long(gb);
        }

        pps->resample_phase_set_present_flag[i] = get_bits1(gb);
        if (pps->resample_phase_set_present_flag[i]) {
            pps->phase_hor_luma[pps->ref_loc_offset_layer_id[i]]   = get_ue_golomb_31(gb);
            pps->phase_ver_luma[pps->ref_loc_offset_layer_id[i]]   = get_ue_golomb_31(gb);
            pps->phase_hor_chroma[pps->ref_loc_offset_layer_id[i]] = get_ue_golomb(gb) - 8;
            pps->phase_ver_chroma[pps->ref_loc_offset_layer_id[i]] = get_ue_golomb(gb) - 8;
        }
    }

    pps->colour_mapping_enabled_flag = get_bits1(gb);
    if (pps->colour_mapping_enabled_flag) {
        int ret = colour_mapping_table(gb, avctx, pps);
        if (ret < 0)
            return ret;
    }

    return 0;
}

static void delta_dlt(GetBitContext *gb, HEVCPPS *pps)
{
    unsigned int num_val_delta_dlt, max_diff = 0;
    int min_diff_minus1 = -1;
    unsigned int len;

    num_val_delta_dlt = get_bits(gb, pps->pps_bit_depth_for_depth_layers_minus8 + 8);
    if (num_val_delta_dlt) {
        if (num_val_delta_dlt > 1)
            max_diff = get_bits(gb, pps->pps_bit_depth_for_depth_layers_minus8 + 8);
        if (num_val_delta_dlt > 2 && max_diff) {
            len = av_log2(max_diff) + 1;
            min_diff_minus1 = get_bits(gb, len);
        }
        if (max_diff > (min_diff_minus1 + 1))
            for (int k = 1; k < num_val_delta_dlt; k++) {
                len = av_log2(max_diff - (min_diff_minus1 + 1)) + 1;
                skip_bits(gb, len); // delta_val_diff_minus_min
            }
    }
}

static int pps_3d_extension(GetBitContext *gb, AVCodecContext *avctx,
                            HEVCPPS *pps, const HEVCSPS *sps)
{
    unsigned int pps_depth_layers_minus1;

    if (get_bits1(gb)) { // dlts_present_flag
        pps_depth_layers_minus1 = get_bits(gb, 6);
        pps->pps_bit_depth_for_depth_layers_minus8 = get_bits(gb, 4);
        for (int i = 0; i <= pps_depth_layers_minus1; i++) {
            if (get_bits1(gb)) { // dlt_flag[i]
                if (!get_bits1(gb)) { // dlt_pred_flag[i]
                    if (get_bits1(gb)) { // dlt_val_flags_present_flag[i]
                        for (int j = 0; j <= ((1 << (pps->pps_bit_depth_for_depth_layers_minus8 + 8)) - 1); j++)
                            skip_bits1(gb); // dlt_value_flag[i][j]
                    } else
                        delta_dlt(gb, pps);
                }
            }
        }
    }

    return 0;
}

static int pps_range_extensions(GetBitContext *gb, AVCodecContext *avctx,
                                HEVCPPS *pps, const HEVCSPS *sps)
{
    if (pps->transform_skip_enabled_flag) {
        pps->log2_max_transform_skip_block_size = get_ue_golomb_31(gb) + 2;
    }
    pps->cross_component_prediction_enabled_flag = get_bits1(gb);
    pps->chroma_qp_offset_list_enabled_flag = get_bits1(gb);
    if (pps->chroma_qp_offset_list_enabled_flag) {
        pps->diff_cu_chroma_qp_offset_depth = get_ue_golomb_31(gb);
        pps->chroma_qp_offset_list_len_minus1 = get_ue_golomb_31(gb);
        if (pps->chroma_qp_offset_list_len_minus1 > 5) {
            av_log(avctx, AV_LOG_ERROR,
                   "chroma_qp_offset_list_len_minus1 shall be in the range [0, 5].\n");
            return AVERROR_INVALIDDATA;
        }
        for (int i = 0; i <= pps->chroma_qp_offset_list_len_minus1; i++) {
            pps->cb_qp_offset_list[i] = get_se_golomb(gb);
            if (pps->cb_qp_offset_list[i]) {
                av_log(avctx, AV_LOG_WARNING,
                       "cb_qp_offset_list not tested yet.\n");
            }
            pps->cr_qp_offset_list[i] = get_se_golomb(gb);
            if (pps->cr_qp_offset_list[i]) {
                av_log(avctx, AV_LOG_WARNING,
                       "cb_qp_offset_list not tested yet.\n");
            }
        }
    }
    pps->log2_sao_offset_scale_luma = get_ue_golomb_31(gb);
    pps->log2_sao_offset_scale_chroma = get_ue_golomb_31(gb);

    if (   pps->log2_sao_offset_scale_luma   > FFMAX(sps->bit_depth        - 10, 0)
        || pps->log2_sao_offset_scale_chroma > FFMAX(sps->bit_depth_chroma - 10, 0)
    )
        return AVERROR_INVALIDDATA;

    return(0);
}

static int pps_scc_extension(GetBitContext *gb, AVCodecContext *avctx,
                             HEVCPPS *pps, const HEVCSPS *sps)
{
    int num_comps, ret;

    pps->pps_curr_pic_ref_enabled_flag = get_bits1(gb);
    if (pps->residual_adaptive_colour_transform_enabled_flag = get_bits1(gb)) {
        pps->pps_slice_act_qp_offsets_present_flag = get_bits1(gb);
        pps->pps_act_y_qp_offset  = get_se_golomb(gb) - 5;
        pps->pps_act_cb_qp_offset = get_se_golomb(gb) - 5;
        pps->pps_act_cr_qp_offset = get_se_golomb(gb) - 3;

#define CHECK_QP_OFFSET(name) (pps->pps_act_ ## name ## _qp_offset <= -12 || \
                               pps->pps_act_ ## name ## _qp_offset >= 12)
        ret = CHECK_QP_OFFSET(y) || CHECK_QP_OFFSET(cb) || CHECK_QP_OFFSET(cr);
#undef CHECK_QP_OFFSET
        if (ret) {
            av_log(avctx, AV_LOG_ERROR,
                   "PpsActQpOffsetY/Cb/Cr shall be in the range of [-12, 12].\n");
            return AVERROR_INVALIDDATA;
        }
    }

    if (pps->pps_palette_predictor_initializers_present_flag = get_bits1(gb)) {
        pps->pps_num_palette_predictor_initializers = get_ue_golomb(gb);
        if (pps->pps_num_palette_predictor_initializers > 0) {
            if (pps->pps_num_palette_predictor_initializers > HEVC_MAX_PALETTE_PREDICTOR_SIZE) {
                av_log(avctx, AV_LOG_ERROR,
                       "pps_num_palette_predictor_initializers out of range: %u\n",
                       pps->pps_num_palette_predictor_initializers);
                return AVERROR_INVALIDDATA;
            }
            pps->monochrome_palette_flag = get_bits1(gb);
            pps->luma_bit_depth_entry = get_ue_golomb_31(gb) + 8;
            if (pps->luma_bit_depth_entry != sps->bit_depth)
                return AVERROR_INVALIDDATA;
            if (!pps->monochrome_palette_flag) {
                pps->chroma_bit_depth_entry = get_ue_golomb_31(gb) + 8;
                if (pps->chroma_bit_depth_entry != sps->bit_depth_chroma)
                    return AVERROR_INVALIDDATA;
            }

            num_comps = pps->monochrome_palette_flag ? 1 : 3;
            for (int comp = 0; comp < num_comps; comp++) {
                int bit_depth = !comp ? pps->luma_bit_depth_entry : pps->chroma_bit_depth_entry;
                for (int i = 0; i < pps->pps_num_palette_predictor_initializers; i++)
                    pps->pps_palette_predictor_initializer[comp][i] = get_bits(gb, bit_depth);
            }
        }
    }

    return 0;
}

static inline int setup_pps(AVCodecContext *avctx, GetBitContext *gb,
                            HEVCPPS *pps, const HEVCSPS *sps)
{
    int log2_diff;
    int pic_area_in_ctbs;
    int i, j, x, y, ctb_addr_rs, tile_id;

    // Inferred parameters
    pps->col_bd   = av_malloc_array(pps->num_tile_columns + 1, sizeof(*pps->col_bd));
    pps->row_bd   = av_malloc_array(pps->num_tile_rows + 1,    sizeof(*pps->row_bd));
    pps->col_idxX = av_malloc_array(sps->ctb_width,    sizeof(*pps->col_idxX));
    if (!pps->col_bd || !pps->row_bd || !pps->col_idxX)
        return AVERROR(ENOMEM);

    if (pps->uniform_spacing_flag) {
        if (!pps->column_width) {
            pps->column_width = av_malloc_array(pps->num_tile_columns, sizeof(*pps->column_width));
            pps->row_height   = av_malloc_array(pps->num_tile_rows,    sizeof(*pps->row_height));
        }
        if (!pps->column_width || !pps->row_height)
            return AVERROR(ENOMEM);

        for (i = 0; i < pps->num_tile_columns; i++) {
            pps->column_width[i] = ((i + 1) * sps->ctb_width) / pps->num_tile_columns -
                                   (i * sps->ctb_width) / pps->num_tile_columns;
        }

        for (i = 0; i < pps->num_tile_rows; i++) {
            pps->row_height[i] = ((i + 1) * sps->ctb_height) / pps->num_tile_rows -
                                 (i * sps->ctb_height) / pps->num_tile_rows;
        }
    }

    pps->col_bd[0] = 0;
    for (i = 0; i < pps->num_tile_columns; i++)
        pps->col_bd[i + 1] = pps->col_bd[i] + pps->column_width[i];

    pps->row_bd[0] = 0;
    for (i = 0; i < pps->num_tile_rows; i++)
        pps->row_bd[i + 1] = pps->row_bd[i] + pps->row_height[i];

    for (i = 0, j = 0; i < sps->ctb_width; i++) {
        if (i > pps->col_bd[j])
            j++;
        pps->col_idxX[i] = j;
    }

    /**
     * 6.5
     */
    pic_area_in_ctbs     = sps->ctb_width    * sps->ctb_height;

    pps->ctb_addr_rs_to_ts = av_malloc_array(pic_area_in_ctbs,    sizeof(*pps->ctb_addr_rs_to_ts));
    pps->ctb_addr_ts_to_rs = av_malloc_array(pic_area_in_ctbs,    sizeof(*pps->ctb_addr_ts_to_rs));
    pps->tile_id           = av_malloc_array(pic_area_in_ctbs,    sizeof(*pps->tile_id));
    pps->min_tb_addr_zs_tab = av_malloc_array((sps->tb_mask+2) * (sps->tb_mask+2), sizeof(*pps->min_tb_addr_zs_tab));
    if (!pps->ctb_addr_rs_to_ts || !pps->ctb_addr_ts_to_rs ||
        !pps->tile_id || !pps->min_tb_addr_zs_tab) {
        return AVERROR(ENOMEM);
    }

    for (ctb_addr_rs = 0; ctb_addr_rs < pic_area_in_ctbs; ctb_addr_rs++) {
        int tb_x   = ctb_addr_rs % sps->ctb_width;
        int tb_y   = ctb_addr_rs / sps->ctb_width;
        int tile_x = 0;
        int tile_y = 0;
        int val    = 0;

        for (i = 0; i < pps->num_tile_columns; i++) {
            if (tb_x < pps->col_bd[i + 1]) {
                tile_x = i;
                break;
            }
        }

        for (i = 0; i < pps->num_tile_rows; i++) {
            if (tb_y < pps->row_bd[i + 1]) {
                tile_y = i;
                break;
            }
        }

        for (i = 0; i < tile_x; i++)
            val += pps->row_height[tile_y] * pps->column_width[i];
        for (i = 0; i < tile_y; i++)
            val += sps->ctb_width * pps->row_height[i];

        val += (tb_y - pps->row_bd[tile_y]) * pps->column_width[tile_x] +
               tb_x - pps->col_bd[tile_x];

        pps->ctb_addr_rs_to_ts[ctb_addr_rs] = val;
        pps->ctb_addr_ts_to_rs[val]         = ctb_addr_rs;
    }

    for (j = 0, tile_id = 0; j < pps->num_tile_rows; j++)
        for (i = 0; i < pps->num_tile_columns; i++, tile_id++)
            for (y = pps->row_bd[j]; y < pps->row_bd[j + 1]; y++)
                for (x = pps->col_bd[i]; x < pps->col_bd[i + 1]; x++)
                    pps->tile_id[pps->ctb_addr_rs_to_ts[y * sps->ctb_width + x]] = tile_id;

    pps->tile_pos_rs = av_malloc_array(tile_id, sizeof(*pps->tile_pos_rs));
    if (!pps->tile_pos_rs)
        return AVERROR(ENOMEM);

    for (j = 0; j < pps->num_tile_rows; j++)
        for (i = 0; i < pps->num_tile_columns; i++)
            pps->tile_pos_rs[j * pps->num_tile_columns + i] =
                pps->row_bd[j] * sps->ctb_width + pps->col_bd[i];

    log2_diff = sps->log2_ctb_size - sps->log2_min_tb_size;
    pps->min_tb_addr_zs = &pps->min_tb_addr_zs_tab[1*(sps->tb_mask+2)+1];
    for (y = 0; y < sps->tb_mask+2; y++) {
        pps->min_tb_addr_zs_tab[y*(sps->tb_mask+2)] = -1;
        pps->min_tb_addr_zs_tab[y]    = -1;
    }
    for (y = 0; y < sps->tb_mask+1; y++) {
        for (x = 0; x < sps->tb_mask+1; x++) {
            int tb_x = x >> log2_diff;
            int tb_y = y >> log2_diff;
            int rs   = sps->ctb_width * tb_y + tb_x;
            int val  = pps->ctb_addr_rs_to_ts[rs] << (log2_diff * 2);
            for (i = 0; i < log2_diff; i++) {
                int m = 1 << i;
                val += (m & x ? m * m : 0) + (m & y ? 2 * m * m : 0);
            }
            pps->min_tb_addr_zs[y * (sps->tb_mask+2) + x] = val;
        }
    }

    return 0;
}

int ff_hevc_decode_nal_pps(GetBitContext *gb, AVCodecContext *avctx,
                           HEVCParamSets *ps)
{
    const HEVCSPS *sps = NULL;
    const HEVCVPS *vps = NULL;
    int i, ret = 0;
    ptrdiff_t nal_size = gb->buffer_end - gb->buffer;
    unsigned int pps_id = get_ue_golomb_long(gb);
    unsigned log2_parallel_merge_level_minus2;
    HEVCPPS *pps;

    av_log(avctx, AV_LOG_DEBUG, "Decoding PPS\n");

    if (pps_id >= HEVC_MAX_PPS_COUNT) {
        av_log(avctx, AV_LOG_ERROR, "PPS id out of range: %d\n", pps_id);
        return AVERROR_INVALIDDATA;
    }

    if (ps->pps_list[pps_id]) {
        const HEVCPPS *pps1 = ps->pps_list[pps_id];
        if (pps1->data_size == nal_size &&
            !memcmp(pps1->data, gb->buffer, pps1->data_size))
            return 0;
    }

    pps = ff_refstruct_alloc_ext(sizeof(*pps), 0, NULL, hevc_pps_free);
    if (!pps)
        return AVERROR(ENOMEM);

    pps->data_size = nal_size;
    pps->data = av_memdup(gb->buffer, nal_size);
    if (!pps->data) {
        ret = AVERROR_INVALIDDATA;
        goto err;
    }

    // Default values
    pps->loop_filter_across_tiles_enabled_flag = 1;
    pps->num_tile_columns                      = 1;
    pps->num_tile_rows                         = 1;
    pps->uniform_spacing_flag                  = 1;
    pps->disable_dbf                           = 0;
    pps->beta_offset                           = 0;
    pps->tc_offset                             = 0;
    pps->log2_max_transform_skip_block_size    = 2;

    // Coded parameters
    pps->pps_id = pps_id;
    pps->sps_id = get_ue_golomb_long(gb);
    if (pps->sps_id >= HEVC_MAX_SPS_COUNT) {
        av_log(avctx, AV_LOG_ERROR, "SPS id out of range: %d\n", pps->sps_id);
        ret = AVERROR_INVALIDDATA;
        goto err;
    }
    if (!ps->sps_list[pps->sps_id]) {
        av_log(avctx, AV_LOG_ERROR, "SPS %u does not exist.\n", pps->sps_id);
        ret = AVERROR_INVALIDDATA;
        goto err;
    }
    sps = ps->sps_list[pps->sps_id];
    vps = ps->vps_list[sps->vps_id];

    pps->sps = ff_refstruct_ref_c(sps);

    pps->dependent_slice_segments_enabled_flag = get_bits1(gb);
    pps->output_flag_present_flag              = get_bits1(gb);
    pps->num_extra_slice_header_bits           = get_bits(gb, 3);

    pps->sign_data_hiding_flag = get_bits1(gb);

    pps->cabac_init_present_flag = get_bits1(gb);

    pps->num_ref_idx_l0_default_active = get_ue_golomb_31(gb) + 1;
    pps->num_ref_idx_l1_default_active = get_ue_golomb_31(gb) + 1;
    if (pps->num_ref_idx_l0_default_active >= HEVC_MAX_REFS ||
        pps->num_ref_idx_l1_default_active >= HEVC_MAX_REFS) {
        av_log(avctx, AV_LOG_ERROR, "Too many default refs in PPS: %d/%d.\n",
               pps->num_ref_idx_l0_default_active, pps->num_ref_idx_l1_default_active);
        goto err;
    }

    pps->pic_init_qp_minus26 = get_se_golomb(gb);

    pps->constrained_intra_pred_flag = get_bits1(gb);
    pps->transform_skip_enabled_flag = get_bits1(gb);

    pps->cu_qp_delta_enabled_flag = get_bits1(gb);
    pps->diff_cu_qp_delta_depth   = 0;
    if (pps->cu_qp_delta_enabled_flag)
        pps->diff_cu_qp_delta_depth = get_ue_golomb_long(gb);

    if (pps->diff_cu_qp_delta_depth < 0 ||
        pps->diff_cu_qp_delta_depth > sps->log2_diff_max_min_coding_block_size) {
        av_log(avctx, AV_LOG_ERROR, "diff_cu_qp_delta_depth %d is invalid\n",
               pps->diff_cu_qp_delta_depth);
        ret = AVERROR_INVALIDDATA;
        goto err;
    }

    pps->cb_qp_offset = get_se_golomb(gb);
    if (pps->cb_qp_offset < -12 || pps->cb_qp_offset > 12) {
        av_log(avctx, AV_LOG_ERROR, "pps_cb_qp_offset out of range: %d\n",
               pps->cb_qp_offset);
        ret = AVERROR_INVALIDDATA;
        goto err;
    }
    pps->cr_qp_offset = get_se_golomb(gb);
    if (pps->cr_qp_offset < -12 || pps->cr_qp_offset > 12) {
        av_log(avctx, AV_LOG_ERROR, "pps_cr_qp_offset out of range: %d\n",
               pps->cr_qp_offset);
        ret = AVERROR_INVALIDDATA;
        goto err;
    }
    pps->pic_slice_level_chroma_qp_offsets_present_flag = get_bits1(gb);

    pps->weighted_pred_flag   = get_bits1(gb);
    pps->weighted_bipred_flag = get_bits1(gb);

    pps->transquant_bypass_enable_flag    = get_bits1(gb);
    pps->tiles_enabled_flag               = get_bits1(gb);
    pps->entropy_coding_sync_enabled_flag = get_bits1(gb);

    if (pps->tiles_enabled_flag) {
        int num_tile_columns_minus1 = get_ue_golomb(gb);
        int num_tile_rows_minus1    = get_ue_golomb(gb);

        if (num_tile_columns_minus1 < 0 ||
            num_tile_columns_minus1 >= sps->ctb_width) {
            av_log(avctx, AV_LOG_ERROR, "num_tile_columns_minus1 out of range: %d\n",
                   num_tile_columns_minus1);
            ret = num_tile_columns_minus1 < 0 ? num_tile_columns_minus1 : AVERROR_INVALIDDATA;
            goto err;
        }
        if (num_tile_rows_minus1 < 0 ||
            num_tile_rows_minus1 >= sps->ctb_height) {
            av_log(avctx, AV_LOG_ERROR, "num_tile_rows_minus1 out of range: %d\n",
                   num_tile_rows_minus1);
            ret = num_tile_rows_minus1 < 0 ? num_tile_rows_minus1 : AVERROR_INVALIDDATA;
            goto err;
        }
        pps->num_tile_columns = num_tile_columns_minus1 + 1;
        pps->num_tile_rows    = num_tile_rows_minus1    + 1;

        pps->column_width = av_malloc_array(pps->num_tile_columns, sizeof(*pps->column_width));
        pps->row_height   = av_malloc_array(pps->num_tile_rows,    sizeof(*pps->row_height));
        if (!pps->column_width || !pps->row_height) {
            ret = AVERROR(ENOMEM);
            goto err;
        }

        pps->uniform_spacing_flag = get_bits1(gb);
        if (!pps->uniform_spacing_flag) {
            uint64_t sum = 0;
            for (i = 0; i < pps->num_tile_columns - 1; i++) {
                pps->column_width[i] = get_ue_golomb_long(gb) + 1;
                sum                 += pps->column_width[i];
            }
            if (sum >= sps->ctb_width) {
                av_log(avctx, AV_LOG_ERROR, "Invalid tile widths.\n");
                ret = AVERROR_INVALIDDATA;
                goto err;
            }
            pps->column_width[pps->num_tile_columns - 1] = sps->ctb_width - sum;

            sum = 0;
            for (i = 0; i < pps->num_tile_rows - 1; i++) {
                pps->row_height[i] = get_ue_golomb_long(gb) + 1;
                sum               += pps->row_height[i];
            }
            if (sum >= sps->ctb_height) {
                av_log(avctx, AV_LOG_ERROR, "Invalid tile heights.\n");
                ret = AVERROR_INVALIDDATA;
                goto err;
            }
            pps->row_height[pps->num_tile_rows - 1] = sps->ctb_height - sum;
        }
        pps->loop_filter_across_tiles_enabled_flag = get_bits1(gb);
    }

    pps->seq_loop_filter_across_slices_enabled_flag = get_bits1(gb);

    pps->deblocking_filter_control_present_flag = get_bits1(gb);
    if (pps->deblocking_filter_control_present_flag) {
        pps->deblocking_filter_override_enabled_flag = get_bits1(gb);
        pps->disable_dbf                             = get_bits1(gb);
        if (!pps->disable_dbf) {
            int beta_offset_div2 = get_se_golomb(gb);
            int tc_offset_div2   = get_se_golomb(gb) ;
            if (beta_offset_div2 < -6 || beta_offset_div2 > 6) {
                av_log(avctx, AV_LOG_ERROR, "pps_beta_offset_div2 out of range: %d\n",
                       beta_offset_div2);
                ret = AVERROR_INVALIDDATA;
                goto err;
            }
            if (tc_offset_div2 < -6 || tc_offset_div2 > 6) {
                av_log(avctx, AV_LOG_ERROR, "pps_tc_offset_div2 out of range: %d\n",
                       tc_offset_div2);
                ret = AVERROR_INVALIDDATA;
                goto err;
            }
            pps->beta_offset = 2 * beta_offset_div2;
            pps->tc_offset   = 2 *   tc_offset_div2;
        }
    }

    pps->scaling_list_data_present_flag = get_bits1(gb);
    if (pps->scaling_list_data_present_flag) {
        set_default_scaling_list_data(&pps->scaling_list);
        ret = scaling_list_data(gb, avctx, &pps->scaling_list, sps);
        if (ret < 0)
            goto err;
    }
    pps->lists_modification_present_flag = get_bits1(gb);
    log2_parallel_merge_level_minus2     = get_ue_golomb_long(gb);
    if (log2_parallel_merge_level_minus2 > sps->log2_ctb_size) {
        av_log(avctx, AV_LOG_ERROR, "log2_parallel_merge_level_minus2 out of range: %d\n",
               log2_parallel_merge_level_minus2);
        ret = AVERROR_INVALIDDATA;
        goto err;
    }
    pps->log2_parallel_merge_level       = log2_parallel_merge_level_minus2 + 2;

    pps->slice_header_extension_present_flag = get_bits1(gb);

    pps->pps_extension_present_flag = get_bits1(gb);
    if (pps->pps_extension_present_flag) {
        pps->pps_range_extensions_flag     = get_bits1(gb);
        pps->pps_multilayer_extension_flag = get_bits1(gb);
        pps->pps_3d_extension_flag         = get_bits1(gb);
        pps->pps_scc_extension_flag        = get_bits1(gb);
        skip_bits(gb, 4); // pps_extension_4bits

        if (sps->ptl.general_ptl.profile_idc >= AV_PROFILE_HEVC_REXT && pps->pps_range_extensions_flag) {
            if ((ret = pps_range_extensions(gb, avctx, pps, sps)) < 0)
                goto err;
        }

        if (pps->pps_multilayer_extension_flag) {
            if ((ret = pps_multilayer_extension(gb, avctx, pps, sps, vps)) < 0)
                goto err;
        }

        if (pps->pps_3d_extension_flag) {
            if ((ret = pps_3d_extension(gb, avctx, pps, sps)) < 0)
                goto err;
        }

        if (pps->pps_scc_extension_flag) {
            if ((ret = pps_scc_extension(gb, avctx, pps, sps)) < 0)
                goto err;
        }
    }

    ret = setup_pps(avctx, gb, pps, sps);
    if (ret < 0)
        goto err;

    if (get_bits_left(gb) < 0) {
        av_log(avctx, AV_LOG_WARNING,
               "Overread PPS by %d bits\n", -get_bits_left(gb));
    }

    ff_refstruct_unref(&ps->pps_list[pps_id]);
    ps->pps_list[pps_id] = pps;

    return 0;

err:
    ff_refstruct_unref(&pps);
    return ret;
}

void ff_hevc_ps_uninit(HEVCParamSets *ps)
{
    int i;

    for (i = 0; i < FF_ARRAY_ELEMS(ps->vps_list); i++)
        ff_refstruct_unref(&ps->vps_list[i]);
    for (i = 0; i < FF_ARRAY_ELEMS(ps->sps_list); i++)
        ff_refstruct_unref(&ps->sps_list[i]);
    for (i = 0; i < FF_ARRAY_ELEMS(ps->pps_list); i++)
        ff_refstruct_unref(&ps->pps_list[i]);
}

int ff_hevc_compute_poc(const HEVCSPS *sps, int pocTid0, int poc_lsb, int nal_unit_type)
{
    int max_poc_lsb  = 1 << sps->log2_max_poc_lsb;
    int prev_poc_lsb = pocTid0 % max_poc_lsb;
    int prev_poc_msb = pocTid0 - prev_poc_lsb;
    int poc_msb;

    if (poc_lsb < prev_poc_lsb && prev_poc_lsb - poc_lsb >= max_poc_lsb / 2)
        poc_msb = prev_poc_msb + max_poc_lsb;
    else if (poc_lsb > prev_poc_lsb && poc_lsb - prev_poc_lsb > max_poc_lsb / 2)
        poc_msb = prev_poc_msb - max_poc_lsb;
    else
        poc_msb = prev_poc_msb;

    // For BLA picture types, POCmsb is set to 0.
    if (nal_unit_type == HEVC_NAL_BLA_W_LP   ||
        nal_unit_type == HEVC_NAL_BLA_W_RADL ||
        nal_unit_type == HEVC_NAL_BLA_N_LP)
        poc_msb = 0;

    return poc_msb + poc_lsb;
}
