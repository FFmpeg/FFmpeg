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

#include <stdint.h>
#include <string.h>

#include "avcodec.h"
#include "nvdec.h"
#include "decode.h"
#include "internal.h"
#include "hevcdec.h"
#include "hevc_data.h"

static void dpb_add(CUVIDHEVCPICPARAMS *pp, int idx, const HEVCFrame *src)
{
    FrameDecodeData *fdd = (FrameDecodeData*)src->frame->private_ref->data;
    const NVDECFrame *cf = fdd->hwaccel_priv;

    pp->RefPicIdx[idx]      = cf ? cf->idx : -1;
    pp->PicOrderCntVal[idx] = src->poc;
    pp->IsLongTerm[idx]     = !!(src->flags & HEVC_FRAME_FLAG_LONG_REF);
}

static void fill_scaling_lists(CUVIDHEVCPICPARAMS *ppc, const HEVCContext *s)
{
    const ScalingList *sl = s->ps.pps->scaling_list_data_present_flag ?
                            &s->ps.pps->scaling_list : &s->ps.sps->scaling_list;
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

static int nvdec_hevc_start_frame(AVCodecContext *avctx,
                                  const uint8_t *buffer, uint32_t size)
{
    const HEVCContext *s = avctx->priv_data;
    const HEVCPPS *pps = s->ps.pps;
    const HEVCSPS *sps = s->ps.sps;

    NVDECContext       *ctx = avctx->internal->hwaccel_priv_data;
    CUVIDPICPARAMS      *pp = &ctx->pic_params;
    CUVIDHEVCPICPARAMS *ppc = &pp->CodecSpecific.hevc;
    FrameDecodeData *fdd;
    NVDECFrame *cf;

    int i, j, dpb_size, ret;

    ret = ff_nvdec_start_frame(avctx, s->ref->frame);
    if (ret < 0)
        return ret;

    fdd = (FrameDecodeData*)s->ref->frame->private_ref->data;
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
            .pcm_enabled_flag                             = sps->pcm_enabled_flag,
            .log2_min_pcm_luma_coding_block_size_minus3   = sps->pcm_enabled_flag ? sps->pcm.log2_min_pcm_cb_size - 3 : 0,
            .log2_diff_max_min_pcm_luma_coding_block_size = sps->pcm.log2_max_pcm_cb_size - sps->pcm.log2_min_pcm_cb_size,
            .pcm_sample_bit_depth_luma_minus1             = sps->pcm_enabled_flag ? sps->pcm.bit_depth - 1 : 0,
            .pcm_sample_bit_depth_chroma_minus1           = sps->pcm_enabled_flag ? sps->pcm.bit_depth_chroma - 1 : 0,
#if NVDECAPI_CHECK_VERSION(8, 1)
            .log2_max_transform_skip_block_size_minus2    = pps->log2_max_transform_skip_block_size - 2,
            .log2_sao_offset_scale_luma                   = pps->log2_sao_offset_scale_luma,
            .log2_sao_offset_scale_chroma                 = pps->log2_sao_offset_scale_chroma,
            .high_precision_offsets_enabled_flag          = sps->high_precision_offsets_enabled_flag,
#endif
            .pcm_loop_filter_disabled_flag                = sps->pcm.loop_filter_disable_flag,
            .strong_intra_smoothing_enabled_flag          = sps->sps_strong_intra_smoothing_enable_flag,
            .max_transform_hierarchy_depth_intra          = sps->max_transform_hierarchy_depth_intra,
            .max_transform_hierarchy_depth_inter          = sps->max_transform_hierarchy_depth_inter,
            .amp_enabled_flag                             = sps->amp_enabled_flag,
            .separate_colour_plane_flag                   = sps->separate_colour_plane_flag,
            .log2_max_pic_order_cnt_lsb_minus4            = sps->log2_max_poc_lsb - 4,
            .num_short_term_ref_pic_sets                  = sps->nb_st_rps,
            .long_term_ref_pics_present_flag              = sps->long_term_ref_pics_present_flag,
            .num_long_term_ref_pics_sps                   = sps->num_long_term_ref_pics_sps,
            .sps_temporal_mvp_enabled_flag                = sps->sps_temporal_mvp_enabled_flag,
            .sample_adaptive_offset_enabled_flag          = sps->sao_enabled,
            .scaling_list_enable_flag                     = sps->scaling_list_enable_flag,
            .IrapPicFlag                                  = IS_IRAP(s),
            .IdrPicFlag                                   = IS_IDR(s),
            .bit_depth_luma_minus8                        = sps->bit_depth - 8,
            .bit_depth_chroma_minus8                      = sps->bit_depth - 8,
#if NVDECAPI_CHECK_VERSION(9, 0)
            .sps_range_extension_flag                     = sps->sps_range_extension_flag,
            .transform_skip_rotation_enabled_flag         = sps->transform_skip_rotation_enabled_flag,
            .transform_skip_context_enabled_flag          = sps->transform_skip_context_enabled_flag,
            .implicit_rdpcm_enabled_flag                  = sps->implicit_rdpcm_enabled_flag,
            .explicit_rdpcm_enabled_flag                  = sps->explicit_rdpcm_enabled_flag,
            .extended_precision_processing_flag           = sps->extended_precision_processing_flag,
            .intra_smoothing_disabled_flag                = sps->intra_smoothing_disabled_flag,
            .persistent_rice_adaptation_enabled_flag      = sps->persistent_rice_adaptation_enabled_flag,
            .cabac_bypass_alignment_enabled_flag          = sps->cabac_bypass_alignment_enabled_flag,
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
            .NumPocTotalCurr                              = ff_hevc_frame_nb_refs(s),
            .NumPocStCurrBefore                           = s->rps[ST_CURR_BEF].nb_refs,
            .NumPocStCurrAfter                            = s->rps[ST_CURR_AFT].nb_refs,
            .NumPocLtCurr                                 = s->rps[LT_CURR].nb_refs,
            .CurrPicOrderCntVal                           = s->ref->poc,
        },
    };

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
    if (pps->chroma_qp_offset_list_len_minus1 > FF_ARRAY_ELEMS(ppc->cb_qp_offset_list) ||
        pps->chroma_qp_offset_list_len_minus1 > FF_ARRAY_ELEMS(ppc->cr_qp_offset_list)) {
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
    for (i = 0; i < FF_ARRAY_ELEMS(s->DPB); i++) {
        const HEVCFrame *ref = &s->DPB[i];
        if (!(ref->flags & (HEVC_FRAME_FLAG_SHORT_REF | HEVC_FRAME_FLAG_LONG_REF)))
            continue;
        if (dpb_size >= FF_ARRAY_ELEMS(ppc->RefPicIdx)) {
            av_log(avctx, AV_LOG_ERROR, "Too many reference frames\n");
            return AVERROR_INVALIDDATA;
        }
        dpb_add(ppc, dpb_size++, ref);

    }
    for (i = dpb_size; i < FF_ARRAY_ELEMS(ppc->RefPicIdx); i++)
        ppc->RefPicIdx[i] = -1;

    for (i = 0; i < s->rps[ST_CURR_BEF].nb_refs; i++) {
        for (j = 0; j < dpb_size; j++) {
            if (ppc->PicOrderCntVal[j] == s->rps[ST_CURR_BEF].list[i]) {
                ppc->RefPicSetStCurrBefore[i] = j;
                break;
            }
        }
    }
    for (i = 0; i < s->rps[ST_CURR_AFT].nb_refs; i++) {
        for (j = 0; j < dpb_size; j++) {
            if (ppc->PicOrderCntVal[j] == s->rps[ST_CURR_AFT].list[i]) {
                ppc->RefPicSetStCurrAfter[i] = j;
                break;
            }
        }
    }
    for (i = 0; i < s->rps[LT_CURR].nb_refs; i++) {
        for (j = 0; j < dpb_size; j++) {
            if (ppc->PicOrderCntVal[j] == s->rps[LT_CURR].list[i]) {
                ppc->RefPicSetLtCurr[i] = j;
                break;
            }
        }
    }

    fill_scaling_lists(ppc, s);

    return 0;
}

static int nvdec_hevc_decode_slice(AVCodecContext *avctx, const uint8_t *buffer,
                                   uint32_t size)
{
    NVDECContext *ctx = avctx->internal->hwaccel_priv_data;
    void *tmp;

    tmp = av_fast_realloc(ctx->bitstream, &ctx->bitstream_allocated,
                          ctx->bitstream_len + size + 3);
    if (!tmp)
        return AVERROR(ENOMEM);
    ctx->bitstream = tmp;

    tmp = av_fast_realloc(ctx->slice_offsets, &ctx->slice_offsets_allocated,
                          (ctx->nb_slices + 1) * sizeof(*ctx->slice_offsets));
    if (!tmp)
        return AVERROR(ENOMEM);
    ctx->slice_offsets = tmp;

    AV_WB24(ctx->bitstream + ctx->bitstream_len, 1);
    memcpy(ctx->bitstream + ctx->bitstream_len + 3, buffer, size);
    ctx->slice_offsets[ctx->nb_slices] = ctx->bitstream_len ;
    ctx->bitstream_len += size + 3;
    ctx->nb_slices++;

    return 0;
}

static int nvdec_hevc_frame_params(AVCodecContext *avctx,
                                   AVBufferRef *hw_frames_ctx)
{
    const HEVCContext *s = avctx->priv_data;
    const HEVCSPS *sps = s->ps.sps;
    return ff_nvdec_frame_params(avctx, hw_frames_ctx, sps->temporal_layer[sps->max_sub_layers - 1].max_dec_pic_buffering + 1, 1);
}

static int nvdec_hevc_decode_init(AVCodecContext *avctx) {
    NVDECContext *ctx = avctx->internal->hwaccel_priv_data;
    ctx->supports_444 = 1;
    return ff_nvdec_decode_init(avctx);
}

const AVHWAccel ff_hevc_nvdec_hwaccel = {
    .name                 = "hevc_nvdec",
    .type                 = AVMEDIA_TYPE_VIDEO,
    .id                   = AV_CODEC_ID_HEVC,
    .pix_fmt              = AV_PIX_FMT_CUDA,
    .start_frame          = nvdec_hevc_start_frame,
    .end_frame            = ff_nvdec_end_frame,
    .decode_slice         = nvdec_hevc_decode_slice,
    .frame_params         = nvdec_hevc_frame_params,
    .init                 = nvdec_hevc_decode_init,
    .uninit               = ff_nvdec_decode_uninit,
    .priv_data_size       = sizeof(NVDECContext),
};
