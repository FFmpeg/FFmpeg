/*
 * HEVC HW decode acceleration through NVDEC
 *
 * Copyright (c) 2017 Anton Khirnov
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

#include "config_components.h"

#include <stdint.h>
#include <string.h>

#include "libavutil/mem.h"
#include "avcodec.h"
#include "nvdec.h"
#include "decode.h"
#include "internal.h"
#include "hevc/hevcdec.h"
#include "hevc/data.h"
#include "hwaccel_internal.h"

static void dpb_add(CUVIDHEVCPICPARAMS *pp, int idx, const HEVCFrame *src)
{
    FrameDecodeData *fdd = src->f->private_ref;
    const NVDECFrame *cf = fdd->hwaccel_priv;

    pp->RefPicIdx[idx]      = cf ? cf->idx : -1;
    pp->PicOrderCntVal[idx] = src->poc;
    pp->IsLongTerm[idx]     = !!(src->flags & HEVC_FRAME_FLAG_LONG_REF);
}

static int dpb_find(const HEVCFrame *const *dpb, int dpb_size,
                    const HEVCFrame *ref)
{
    for (int i = 0; i < dpb_size; i++) {
        if (dpb[i] == ref)
            return i;
    }

    return -1;
}

static void fill_scaling_lists(CUVIDHEVCPICPARAMS *ppc, const HEVCContext *s)
{
    const ScalingList *sl = s->pps->scaling_list_data_present_flag ?
                            &s->pps->scaling_list : &s->pps->sps->scaling_list;
    int i, j, pos;

    for (i = 0; i < 6; i++) {
        for (j = 0; j < 16; j++) {
            pos = 4 * ff_hevc_diag_scan4x4_y[j] + ff_hevc_diag_scan4x4_x[j];
            ppc->ScalingList4x4[i][j] = sl->sl[0][i][pos];
        }

        for (j = 0; j < 64; j++) {
            pos = 8 * ff_hevc_diag_scan8x8_y[j] + ff_hevc_diag_scan8x8_x[j];
            ppc->ScalingList8x8[i][j]   = sl->sl[1][i][pos];
            ppc->ScalingList16x16[i][j] = sl->sl[2][i][pos];

            if (i < 2)
                ppc->ScalingList32x32[i][j] = sl->sl[3][i * 3][pos];
        }

        ppc->ScalingListDCCoeff16x16[i] = sl->sl_dc[0][i];
        if (i < 2)
            ppc->ScalingListDCCoeff32x32[i] = sl->sl_dc[1][i * 3];
    }
}

#ifdef NVDEC_HAVE_MVHEVC_DECODE
/* Auxiliary alpha video also codes more than one layer, but the native decoder
 * keeps only the base layer active for pixel formats without an alpha plane,
 * so it must not be routed through the MV-HEVC path. */
static int nvdec_hevc_is_multiview(const HEVCContext *s)
{
    return s->vps && s->vps->nb_layers > 1 && !ff_hevc_is_alpha_video(s);
}

/* Number of layers that will consume a decode surface per access unit.  The
 * caller may have requested fewer views than the VPS codes, and setup_multilayer()
 * has not run yet when the hardware pool is sized. */
static int nvdec_hevc_nb_decode_layers(const HEVCContext *s)
{
    unsigned active_output;
    int nb_layers;

    if (!nvdec_hevc_is_multiview(s))
        return 1;

    nb_layers = ff_hevc_requested_layers(s, s->vps, &active_output);
    /* An invalid selection is reported by the decoder itself; size for
     * everything the VPS codes until then. */
    return nb_layers < 0 ? s->vps->nb_layers : nb_layers;
}

/* DPB size to request for the selected layers, as passed to
 * ff_nvdec_frame_params().  Returns the number of decoded layers in nb_layers. */
static int nvdec_hevc_dpb_size(const AVCodecContext *avctx, int *nb_layers)
{
    const HEVCContext *s = avctx->priv_data;
    const HEVCSPS *sps = s->pps->sps;
    int dpb_size = sps->temporal_layer[sps->max_sub_layers - 1].max_dec_pic_buffering + 1;

    *nb_layers = nvdec_hevc_nb_decode_layers(s);
    if (*nb_layers > 1) {
        const HEVCVPS *vps = s->vps;
        /* Unlike its SPS and VPS base-layer counterparts, the VPS extension's
         * max_dec_pic_buffering is not range-checked when parsed, so bound it
         * before it reaches the surface arithmetic below. */
        unsigned vps_ext_dpb = FFMIN(vps->dpb_size.max_dec_pic_buffering,
                                     HEVC_MAX_DPB_SIZE);
        int vps_dpb = FFMAX(vps->vps_max_dec_pic_buffering[vps->vps_max_sub_layers - 1],
                            vps_ext_dpb) + 1;
        dpb_size = FFMAX(dpb_size, vps_dpb);
        dpb_size *= *nb_layers;

        /* Generic hwaccel setup adds one surface per frame thread. */
        if (avctx->active_thread_type & FF_THREAD_FRAME)
            dpb_size += avctx->thread_count * (*nb_layers - 1);
    }

    return dpb_size;
}
#endif

static int nvdec_hevc_start_frame(AVCodecContext *avctx,
                                  const AVBufferRef *buffer_ref,
                                  const uint8_t *buffer, uint32_t size)
{
    const HEVCContext *s = avctx->priv_data;
    const HEVCPPS *pps = s->pps;
    const HEVCSPS *sps = pps->sps;

    NVDECContext       *ctx = avctx->internal->hwaccel_priv_data;
    CUVIDPICPARAMS      *pp = &ctx->pic_params;
    CUVIDHEVCPICPARAMS *ppc = &pp->CodecSpecific.hevc;
    FrameDecodeData *fdd;
    NVDECFrame *cf;
    const HEVCFrame *dpb[FF_ARRAY_ELEMS(ppc->RefPicIdx)];

    int i, dpb_size, ret;
    /* Outside MV-HEVC only the layer being decoded contributes references,
     * and it is not necessarily the base layer (e.g. auxiliary alpha). */
    unsigned first_dpb_layer = s->cur_layer, nb_dpb_layers = 1;

    /* The HEVC decoder starts every layer of an access unit before ending any
     * of them, while NVDECContext has storage for a single pending picture, so
     * submit the previous layer before its parameters and slices are
     * overwritten.  Only a non-base layer can have a sibling still pending: a
     * base-layer picture always starts a new access unit, and state left behind
     * by a picture whose decoding was aborted must be discarded rather than
     * submitted, which ff_nvdec_start_frame() below does.  Requiring the base
     * layer to still be open rejects the leftovers of an aborted access unit
     * whose base-layer picture was skipped entirely. */
    if (ctx->nb_slices && s->cur_layer > 0 && s->layers[0].cur_frame) {
        ret = ff_nvdec_end_frame(avctx);
        ctx->bitstream_len = 0;
        ctx->nb_slices     = 0;
        if (ret < 0)
            return ret;
    }

    ret = ff_nvdec_start_frame(avctx, s->cur_frame->f);
    if (ret < 0)
        return ret;

    fdd = s->cur_frame->f->private_ref;
    cf  = (NVDECFrame*)fdd->hwaccel_priv;

    *pp = (CUVIDPICPARAMS) {
        .PicWidthInMbs     = sps->width  / 16,
        .FrameHeightInMbs  = sps->height / 16,
        .CurrPicIdx        = cf->idx,
        .ref_pic_flag      = 1,
        .intra_pic_flag    = IS_IRAP(s),

        .CodecSpecific.hevc = {
            .pic_width_in_luma_samples                    = sps->width,
            .pic_height_in_luma_samples                   = sps->height,
            .log2_min_luma_coding_block_size_minus3       = sps->log2_min_cb_size - 3,
            .log2_diff_max_min_luma_coding_block_size     = sps->log2_diff_max_min_coding_block_size,
            .log2_min_transform_block_size_minus2         = sps->log2_min_tb_size - 2,
            .log2_diff_max_min_transform_block_size       = sps->log2_max_trafo_size - sps->log2_min_tb_size,
            .pcm_enabled_flag                             = sps->pcm_enabled,
            .log2_min_pcm_luma_coding_block_size_minus3   = sps->pcm_enabled ? sps->pcm.log2_min_pcm_cb_size - 3 : 0,
            .log2_diff_max_min_pcm_luma_coding_block_size = sps->pcm.log2_max_pcm_cb_size - sps->pcm.log2_min_pcm_cb_size,
            .pcm_sample_bit_depth_luma_minus1             = sps->pcm_enabled ? sps->pcm.bit_depth - 1 : 0,
            .pcm_sample_bit_depth_chroma_minus1           = sps->pcm_enabled ? sps->pcm.bit_depth_chroma - 1 : 0,
#if NVDECAPI_CHECK_VERSION(8, 1)
            .log2_max_transform_skip_block_size_minus2    = pps->log2_max_transform_skip_block_size - 2,
            .log2_sao_offset_scale_luma                   = pps->log2_sao_offset_scale_luma,
            .log2_sao_offset_scale_chroma                 = pps->log2_sao_offset_scale_chroma,
            .high_precision_offsets_enabled_flag          = sps->high_precision_offsets_enabled,
#endif
            .pcm_loop_filter_disabled_flag                = sps->pcm_loop_filter_disabled,
            .strong_intra_smoothing_enabled_flag          = sps->strong_intra_smoothing_enabled,
            .max_transform_hierarchy_depth_intra          = sps->max_transform_hierarchy_depth_intra,
            .max_transform_hierarchy_depth_inter          = sps->max_transform_hierarchy_depth_inter,
            .amp_enabled_flag                             = sps->amp_enabled,
            .separate_colour_plane_flag                   = sps->separate_colour_plane,
            .log2_max_pic_order_cnt_lsb_minus4            = sps->log2_max_poc_lsb - 4,
            .num_short_term_ref_pic_sets                  = sps->nb_st_rps,
            .long_term_ref_pics_present_flag              = sps->long_term_ref_pics_present,
            .num_long_term_ref_pics_sps                   = sps->num_long_term_ref_pics_sps,
            .sps_temporal_mvp_enabled_flag                = sps->temporal_mvp_enabled,
            .sample_adaptive_offset_enabled_flag          = sps->sao_enabled,
            .scaling_list_enable_flag                     = sps->scaling_list_enabled,
            .IrapPicFlag                                  = IS_IRAP(s),
            .IdrPicFlag                                   = IS_IDR(s),
            .bit_depth_luma_minus8                        = sps->bit_depth - 8,
            .bit_depth_chroma_minus8                      = sps->bit_depth - 8,
#if NVDECAPI_CHECK_VERSION(9, 0)
            .sps_range_extension_flag                     = sps->range_extension,
            .transform_skip_rotation_enabled_flag         = sps->transform_skip_rotation_enabled,
            .transform_skip_context_enabled_flag          = sps->transform_skip_context_enabled,
            .implicit_rdpcm_enabled_flag                  = sps->implicit_rdpcm_enabled,
            .explicit_rdpcm_enabled_flag                  = sps->explicit_rdpcm_enabled,
            .extended_precision_processing_flag           = sps->extended_precision_processing,
            .intra_smoothing_disabled_flag                = sps->intra_smoothing_disabled,
            .persistent_rice_adaptation_enabled_flag      = sps->persistent_rice_adaptation_enabled,
            .cabac_bypass_alignment_enabled_flag          = sps->cabac_bypass_alignment_enabled,
#endif

            .dependent_slice_segments_enabled_flag        = pps->dependent_slice_segments_enabled_flag,
            .slice_segment_header_extension_present_flag  = pps->slice_header_extension_present_flag,
            .sign_data_hiding_enabled_flag                = pps->sign_data_hiding_flag,
            .cu_qp_delta_enabled_flag                     = pps->cu_qp_delta_enabled_flag,
            .diff_cu_qp_delta_depth                       = pps->diff_cu_qp_delta_depth,
            .init_qp_minus26                              = pps->pic_init_qp_minus26,
            .pps_cb_qp_offset                             = pps->cb_qp_offset,
            .pps_cr_qp_offset                             = pps->cr_qp_offset,
            .constrained_intra_pred_flag                  = pps->constrained_intra_pred_flag,
            .weighted_pred_flag                           = pps->weighted_pred_flag,
            .weighted_bipred_flag                         = pps->weighted_bipred_flag,
            .transform_skip_enabled_flag                  = pps->transform_skip_enabled_flag,
            .transquant_bypass_enabled_flag               = pps->transquant_bypass_enable_flag,
            .entropy_coding_sync_enabled_flag             = pps->entropy_coding_sync_enabled_flag,
            .log2_parallel_merge_level_minus2             = pps->log2_parallel_merge_level - 2,
            .num_extra_slice_header_bits                  = pps->num_extra_slice_header_bits,
            .loop_filter_across_tiles_enabled_flag        = pps->loop_filter_across_tiles_enabled_flag,
            .loop_filter_across_slices_enabled_flag       = pps->seq_loop_filter_across_slices_enabled_flag,
            .output_flag_present_flag                     = pps->output_flag_present_flag,
            .num_ref_idx_l0_default_active_minus1         = pps->num_ref_idx_l0_default_active - 1,
            .num_ref_idx_l1_default_active_minus1         = pps->num_ref_idx_l1_default_active - 1,
            .lists_modification_present_flag              = pps->lists_modification_present_flag,
            .cabac_init_present_flag                      = pps->cabac_init_present_flag,
            .pps_slice_chroma_qp_offsets_present_flag     = pps->pic_slice_level_chroma_qp_offsets_present_flag,
            .deblocking_filter_override_enabled_flag      = pps->deblocking_filter_override_enabled_flag,
            .pps_deblocking_filter_disabled_flag          = pps->disable_dbf,
            .pps_beta_offset_div2                         = pps->beta_offset / 2,
            .pps_tc_offset_div2                           = pps->tc_offset / 2,
            .tiles_enabled_flag                           = pps->tiles_enabled_flag,
            .uniform_spacing_flag                         = pps->uniform_spacing_flag,
            .num_tile_columns_minus1                      = pps->num_tile_columns - 1,
            .num_tile_rows_minus1                         = pps->num_tile_rows - 1,
#if NVDECAPI_CHECK_VERSION(9, 0)
            .pps_range_extension_flag                     = pps->pps_range_extensions_flag,
            .cross_component_prediction_enabled_flag      = pps->cross_component_prediction_enabled_flag,
            .chroma_qp_offset_list_enabled_flag           = pps->chroma_qp_offset_list_enabled_flag,
            .diff_cu_chroma_qp_offset_depth               = pps->diff_cu_chroma_qp_offset_depth,
            .chroma_qp_offset_list_len_minus1             = pps->chroma_qp_offset_list_len_minus1,
#endif

            .NumBitsForShortTermRPSInSlice                = s->sh.short_term_rps ? s->sh.short_term_ref_pic_set_size : 0,
            .NumDeltaPocsOfRefRpsIdx                      = s->sh.short_term_rps ? s->sh.short_term_rps->rps_idx_num_delta_pocs : 0,
            .NumPocTotalCurr                              = ff_hevc_frame_nb_refs(&s->sh, pps, s->cur_layer),
            .NumPocStCurrBefore                           = s->rps[ST_CURR_BEF].nb_refs,
            .NumPocStCurrAfter                            = s->rps[ST_CURR_AFT].nb_refs,
            .NumPocLtCurr                                 = s->rps[LT_CURR].nb_refs,
            .CurrPicOrderCntVal                           = s->cur_frame->poc,
        },
    };

#ifdef NVDEC_HAVE_MVHEVC_DECODE
    if (nvdec_hevc_is_multiview(s)) {
        const HEVCVPS *vps = s->vps;
        int layer_idx = vps->layer_idx[s->nuh_layer_id];
        int nb_inter_layer_refs = s->rps[INTER_LAYER0].nb_refs +
                                  s->rps[INTER_LAYER1].nb_refs;

        if (layer_idx < 0 || layer_idx >= vps->nb_layers) {
            av_log(avctx, AV_LOG_ERROR,
                   "Invalid MV-HEVC layer ID: %d\n", s->nuh_layer_id);
            return AVERROR_INVALIDDATA;
        }
        if (s->rps[INTER_LAYER0].nb_refs > FF_ARRAY_ELEMS(ppc->RefPicSetInterLayer0) ||
            s->rps[INTER_LAYER1].nb_refs > FF_ARRAY_ELEMS(ppc->RefPicSetInterLayer1)) {
            av_log(avctx, AV_LOG_ERROR, "Too many inter-layer reference frames\n");
            return AVERROR_INVALIDDATA;
        }
        if (ppc->NumPocTotalCurr < nb_inter_layer_refs) {
            av_log(avctx, AV_LOG_ERROR, "Invalid inter-layer reference count\n");
            return AVERROR_INVALIDDATA;
        }

        ppc->mv_hevc_enable                 = 1;
        ppc->nuh_layer_id                   = s->nuh_layer_id;
        ppc->default_ref_layers_active_flag = vps->default_ref_layers_active;
        ppc->NumDirectRefLayers             = vps->num_direct_ref_layers[layer_idx];
        ppc->max_one_active_ref_layer_flag  = vps->max_one_active_ref_layer;
        ppc->poc_lsb_not_present_flag       = (vps->poc_lsb_not_present >> layer_idx) & 1;
        ppc->NumActiveRefLayerPics0         = s->rps[INTER_LAYER0].nb_refs;
        ppc->NumActiveRefLayerPics1         = s->rps[INTER_LAYER1].nb_refs;
        /* NVDEC counts active inter-layer pictures separately. */
        ppc->NumPocTotalCurr               -= nb_inter_layer_refs;
        first_dpb_layer = 0;
        nb_dpb_layers   = vps->nb_layers;
    }
#endif

    if (pps->num_tile_columns > FF_ARRAY_ELEMS(ppc->column_width_minus1) ||
        pps->num_tile_rows    > FF_ARRAY_ELEMS(ppc->row_height_minus1)) {
        av_log(avctx, AV_LOG_ERROR, "Too many tiles\n");
        return AVERROR(ENOSYS);
    }
    for (i = 0; i < pps->num_tile_columns; i++)
        ppc->column_width_minus1[i] = pps->column_width[i] - 1;
    for (i = 0; i < pps->num_tile_rows; i++)
        ppc->row_height_minus1[i] = pps->row_height[i] - 1;

#if NVDECAPI_CHECK_VERSION(9, 0)
    if (pps->chroma_qp_offset_list_len_minus1 >= FF_ARRAY_ELEMS(ppc->cb_qp_offset_list) ||
        pps->chroma_qp_offset_list_len_minus1 >= FF_ARRAY_ELEMS(ppc->cr_qp_offset_list)) {
        av_log(avctx, AV_LOG_ERROR, "Too many chroma_qp_offsets\n");
        return AVERROR(ENOSYS);
    }
    for (i = 0; i <= pps->chroma_qp_offset_list_len_minus1; i++) {
        ppc->cb_qp_offset_list[i] = pps->cb_qp_offset_list[i];
        ppc->cr_qp_offset_list[i] = pps->cr_qp_offset_list[i];
    }
#endif

    if (s->rps[LT_CURR].nb_refs     > FF_ARRAY_ELEMS(ppc->RefPicSetLtCurr)       ||
        s->rps[ST_CURR_BEF].nb_refs > FF_ARRAY_ELEMS(ppc->RefPicSetStCurrBefore) ||
        s->rps[ST_CURR_AFT].nb_refs > FF_ARRAY_ELEMS(ppc->RefPicSetStCurrAfter)) {
        av_log(avctx, AV_LOG_ERROR, "Too many reference frames\n");
        return AVERROR(ENOSYS);
    }

    dpb_size = 0;
    for (unsigned layer = first_dpb_layer; layer < first_dpb_layer + nb_dpb_layers; layer++) {
        const HEVCLayerContext *layer_ctx = &s->layers[layer];

        for (i = 0; i < FF_ARRAY_ELEMS(layer_ctx->DPB); i++) {
            const HEVCFrame *ref = &layer_ctx->DPB[i];

            if (!(ref->flags & (HEVC_FRAME_FLAG_SHORT_REF |
                                HEVC_FRAME_FLAG_LONG_REF)))
                continue;
            if (dpb_size >= FF_ARRAY_ELEMS(ppc->RefPicIdx)) {
                av_log(avctx, AV_LOG_ERROR, "Too many reference frames\n");
                return AVERROR_INVALIDDATA;
            }
            dpb[dpb_size] = ref;
            dpb_add(ppc, dpb_size++, ref);
        }
    }

#ifdef NVDEC_HAVE_MVHEVC_DECODE
    /* Inter-layer references are long-term references for MV-HEVC. */
    for (i = 0; i < s->rps[INTER_LAYER0].nb_refs; i++) {
        int idx = dpb_find(dpb, dpb_size, s->rps[INTER_LAYER0].ref[i]);

        if (idx < 0) {
            av_log(avctx, AV_LOG_ERROR,
                   "Inter-layer reference frame missing from the DPB\n");
            return AVERROR_INVALIDDATA;
        }
        ppc->IsLongTerm[idx] = 1;
        ppc->RefPicSetInterLayer0[i] = idx;
    }
    for (i = 0; i < s->rps[INTER_LAYER1].nb_refs; i++) {
        int idx = dpb_find(dpb, dpb_size, s->rps[INTER_LAYER1].ref[i]);

        if (idx < 0) {
            av_log(avctx, AV_LOG_ERROR,
                   "Inter-layer reference frame missing from the DPB\n");
            return AVERROR_INVALIDDATA;
        }
        ppc->IsLongTerm[idx] = 1;
        ppc->RefPicSetInterLayer1[i] = idx;
    }
#endif

    for (i = dpb_size; i < FF_ARRAY_ELEMS(ppc->RefPicIdx); i++)
        ppc->RefPicIdx[i] = -1;

    for (i = 0; i < s->rps[ST_CURR_BEF].nb_refs; i++) {
        int idx = dpb_find(dpb, dpb_size, s->rps[ST_CURR_BEF].ref[i]);

        if (idx < 0)
            return AVERROR_BUG;
        ppc->RefPicSetStCurrBefore[i] = idx;
    }
    for (i = 0; i < s->rps[ST_CURR_AFT].nb_refs; i++) {
        int idx = dpb_find(dpb, dpb_size, s->rps[ST_CURR_AFT].ref[i]);

        if (idx < 0)
            return AVERROR_BUG;
        ppc->RefPicSetStCurrAfter[i] = idx;
    }
    for (i = 0; i < s->rps[LT_CURR].nb_refs; i++) {
        int idx = dpb_find(dpb, dpb_size, s->rps[LT_CURR].ref[i]);

        if (idx < 0)
            return AVERROR_BUG;
        ppc->RefPicSetLtCurr[i] = idx;
    }

    fill_scaling_lists(ppc, s);

    return 0;
}

static int nvdec_hevc_decode_slice(AVCodecContext *avctx, const uint8_t *buffer,
                                   uint32_t size)
{
    NVDECContext *ctx = avctx->internal->hwaccel_priv_data;
    void *tmp;

    tmp = av_fast_realloc(ctx->bitstream_internal, &ctx->bitstream_allocated,
                          ctx->bitstream_len + size + 3);
    if (!tmp)
        return AVERROR(ENOMEM);
    ctx->bitstream = ctx->bitstream_internal = tmp;

    tmp = av_fast_realloc(ctx->slice_offsets, &ctx->slice_offsets_allocated,
                          (ctx->nb_slices + 1) * sizeof(*ctx->slice_offsets));
    if (!tmp)
        return AVERROR(ENOMEM);
    ctx->slice_offsets = tmp;

    AV_WB24(ctx->bitstream_internal + ctx->bitstream_len, 1);
    memcpy(ctx->bitstream_internal + ctx->bitstream_len + 3, buffer, size);
    ctx->slice_offsets[ctx->nb_slices] = ctx->bitstream_len ;
    ctx->bitstream_len += size + 3;
    ctx->nb_slices++;

    return 0;
}

static int nvdec_hevc_end_frame(AVCodecContext *avctx)
{
    NVDECContext *ctx = avctx->internal->hwaccel_priv_data;
    int ret;

    if (!ctx->nb_slices)
        return 0;

    ret = ff_nvdec_end_frame(avctx);
    ctx->bitstream_len = 0;
    ctx->nb_slices     = 0;

    return ret;
}

static int nvdec_hevc_frame_params(AVCodecContext *avctx,
                                   AVBufferRef *hw_frames_ctx,
                                   enum AVPixelFormat hw_format)
{
#ifdef NVDEC_HAVE_MVHEVC_DECODE
    int nb_layers;
    int dpb_size = nvdec_hevc_dpb_size(avctx, &nb_layers);
#else
    const HEVCContext *s = avctx->priv_data;
    const HEVCSPS *sps = s->pps->sps;
    int dpb_size = sps->temporal_layer[sps->max_sub_layers - 1].max_dec_pic_buffering + 1;
#endif

    return ff_nvdec_frame_params(avctx, hw_frames_ctx, hw_format, dpb_size, 1);
}

static int nvdec_hevc_decode_init(AVCodecContext *avctx) {
    NVDECContext *ctx = avctx->internal->hwaccel_priv_data;
    ctx->supports_444 = 1;

#ifdef NVDEC_HAVE_MVHEVC_DECODE
    {
        int nb_layers;
        int dpb_size = nvdec_hevc_dpb_size(avctx, &nb_layers);

        /* Every decoded layer of an access unit occupies a decode surface, so a
         * capped pool would truncate the output instead of failing at init.
         * Record the requirement here as well: frame_params() is not called
         * when the caller supplies its own frames context. */
        if (nb_layers > 1) {
            ctx->strict_pool_layers = nb_layers;
            /* Mirror what avcodec_get_hw_frames_parameters() and
             * nvdec_init_hwframes() add on top of the requested DPB size. */
            ctx->strict_pool_min = dpb_size + 2 + 3;
            if (avctx->active_thread_type & FF_THREAD_FRAME)
                ctx->strict_pool_min += avctx->thread_count;
        }
    }
#endif

    if (avctx->profile != AV_PROFILE_HEVC_MAIN &&
        avctx->profile != AV_PROFILE_HEVC_MAIN_10 &&
        avctx->profile != AV_PROFILE_HEVC_MAIN_STILL_PICTURE &&
#ifdef NVDEC_HAVE_MVHEVC_DECODE
        avctx->profile != AV_PROFILE_HEVC_MULTIVIEW_MAIN &&
#endif
        avctx->profile != AV_PROFILE_HEVC_REXT) {
        av_log(avctx, AV_LOG_ERROR, "Unsupported HEVC profile: %d\n", avctx->profile);
        return AVERROR(ENOTSUP);
    }

    return ff_nvdec_decode_init(avctx);
}

#if CONFIG_HEVC_NVDEC_HWACCEL
static int nvdec_hevc_cuda_frame_params(AVCodecContext *avctx,
                                        AVBufferRef *hw_frames_ctx)
{
    return nvdec_hevc_frame_params(avctx, hw_frames_ctx, AV_PIX_FMT_CUDA);
}

const FFHWAccel ff_hevc_nvdec_hwaccel = {
    .p.name               = "hevc_nvdec",
    .p.type               = AVMEDIA_TYPE_VIDEO,
    .p.id                 = AV_CODEC_ID_HEVC,
    .p.pix_fmt            = AV_PIX_FMT_CUDA,
    .start_frame          = nvdec_hevc_start_frame,
    .end_frame            = nvdec_hevc_end_frame,
    .decode_slice         = nvdec_hevc_decode_slice,
    .frame_params         = nvdec_hevc_cuda_frame_params,
    .init                 = nvdec_hevc_decode_init,
    .uninit               = ff_nvdec_decode_uninit,
    .priv_data_size       = sizeof(NVDECContext),
};
#endif

#if CONFIG_HEVC_NVDEC_CUARRAY_HWACCEL
static int nvdec_hevc_cuarray_frame_params(AVCodecContext *avctx,
                                           AVBufferRef *hw_frames_ctx)
{
    return nvdec_hevc_frame_params(avctx, hw_frames_ctx, AV_PIX_FMT_CUARRAY);
}

const FFHWAccel ff_hevc_nvdec_cuarray_hwaccel = {
    .p.name               = "hevc_nvdec_cuarray",
    .p.type               = AVMEDIA_TYPE_VIDEO,
    .p.id                 = AV_CODEC_ID_HEVC,
    .p.pix_fmt            = AV_PIX_FMT_CUARRAY,
    .start_frame          = nvdec_hevc_start_frame,
    .end_frame            = nvdec_hevc_end_frame,
    .decode_slice         = nvdec_hevc_decode_slice,
    .frame_params         = nvdec_hevc_cuarray_frame_params,
    .init                 = nvdec_hevc_decode_init,
    .uninit               = ff_nvdec_decode_uninit,
    .priv_data_size       = sizeof(NVDECContext),
};
#endif
