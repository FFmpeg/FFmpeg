/*
 * DXVA2 HEVC HW acceleration.
 *
 * copyright (c) 2014 - 2015 Hendrik Leppkes
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

#include "libavutil/avassert.h"

#include "dxva2_internal.h"
#include "hevc.h"

#define MAX_SLICES 256

struct hevc_dxva2_picture_context {
    DXVA_PicParams_HEVC   pp;
    DXVA_Qmatrix_HEVC     qm;
    unsigned              slice_count;
    DXVA_Slice_HEVC_Short slice_short[MAX_SLICES];
    const uint8_t         *bitstream;
    unsigned              bitstream_size;
};

static void fill_picture_entry(DXVA_PicEntry_HEVC *pic,
                               unsigned index, unsigned flag)
{
    av_assert0((index & 0x7f) == index && (flag & 0x01) == flag);
    pic->bPicEntry = index | (flag << 7);
}

static int get_refpic_index(const DXVA_PicParams_HEVC *pp, int surface_index)
{
    int i;
    for (i = 0; i < FF_ARRAY_ELEMS(pp->RefPicList); i++) {
        if ((pp->RefPicList[i].bPicEntry & 0x7f) == surface_index)
          return i;
    }
    return 0xff;
}

static void fill_picture_parameters(struct dxva_context *ctx, const HEVCContext *h,
                                    DXVA_PicParams_HEVC *pp)
{
    const HEVCFrame *current_picture = h->ref;
    int i, j, k;

    memset(pp, 0, sizeof(*pp));

    pp->PicWidthInMinCbsY  = h->sps->min_cb_width;
    pp->PicHeightInMinCbsY = h->sps->min_cb_height;

    pp->wFormatAndSequenceInfoFlags = (h->sps->chroma_format_idc          <<  0) |
                                      (h->sps->separate_colour_plane_flag <<  2) |
                                      ((h->sps->bit_depth - 8)            <<  3) |
                                      ((h->sps->bit_depth - 8)            <<  6) |
                                      ((h->sps->log2_max_poc_lsb - 4)     <<  9) |
                                      (0                                  << 13) |
                                      (0                                  << 14) |
                                      (0                                  << 15);

    fill_picture_entry(&pp->CurrPic, ff_dxva2_get_surface_index(ctx, current_picture->frame), 0);

    pp->sps_max_dec_pic_buffering_minus1         = h->sps->temporal_layer[h->sps->max_sub_layers - 1].max_dec_pic_buffering - 1;
    pp->log2_min_luma_coding_block_size_minus3   = h->sps->log2_min_cb_size - 3;
    pp->log2_diff_max_min_luma_coding_block_size = h->sps->log2_diff_max_min_coding_block_size;
    pp->log2_min_transform_block_size_minus2     = h->sps->log2_min_tb_size - 2;
    pp->log2_diff_max_min_transform_block_size   = h->sps->log2_max_trafo_size  - h->sps->log2_min_tb_size;
    pp->max_transform_hierarchy_depth_inter      = h->sps->max_transform_hierarchy_depth_inter;
    pp->max_transform_hierarchy_depth_intra      = h->sps->max_transform_hierarchy_depth_intra;
    pp->num_short_term_ref_pic_sets              = h->sps->nb_st_rps;
    pp->num_long_term_ref_pics_sps               = h->sps->num_long_term_ref_pics_sps;

    pp->num_ref_idx_l0_default_active_minus1     = h->pps->num_ref_idx_l0_default_active - 1;
    pp->num_ref_idx_l1_default_active_minus1     = h->pps->num_ref_idx_l1_default_active - 1;
    pp->init_qp_minus26                          = h->pps->pic_init_qp_minus26;

    if (h->sh.short_term_ref_pic_set_sps_flag == 0 && h->sh.short_term_rps) {
        pp->ucNumDeltaPocsOfRefRpsIdx            = h->sh.short_term_rps->num_delta_pocs;
        pp->wNumBitsForShortTermRPSInSlice       = h->sh.short_term_ref_pic_set_size;
    }

    pp->dwCodingParamToolFlags = (h->sps->scaling_list_enable_flag               <<  0) |
                                 (h->sps->amp_enabled_flag                       <<  1) |
                                 (h->sps->sao_enabled                            <<  2) |
                                 (h->sps->pcm_enabled_flag                       <<  3) |
                                 ((h->sps->pcm_enabled_flag ? (h->sps->pcm.bit_depth - 1) : 0)            <<  4) |
                                 ((h->sps->pcm_enabled_flag ? (h->sps->pcm.bit_depth_chroma - 1) : 0)     <<  8) |
                                 ((h->sps->pcm_enabled_flag ? (h->sps->pcm.log2_min_pcm_cb_size - 3) : 0) << 12) |
                                 ((h->sps->pcm_enabled_flag ? (h->sps->pcm.log2_max_pcm_cb_size - h->sps->pcm.log2_min_pcm_cb_size) : 0) << 14) |
                                 (h->sps->pcm.loop_filter_disable_flag           << 16) |
                                 (h->sps->long_term_ref_pics_present_flag        << 17) |
                                 (h->sps->sps_temporal_mvp_enabled_flag          << 18) |
                                 (h->sps->sps_strong_intra_smoothing_enable_flag << 19) |
                                 (h->pps->dependent_slice_segments_enabled_flag  << 20) |
                                 (h->pps->output_flag_present_flag               << 21) |
                                 (h->pps->num_extra_slice_header_bits            << 22) |
                                 (h->pps->sign_data_hiding_flag                  << 25) |
                                 (h->pps->cabac_init_present_flag                << 26) |
                                 (0                                              << 27);

    pp->dwCodingSettingPicturePropertyFlags = (h->pps->constrained_intra_pred_flag                <<  0) |
                                              (h->pps->transform_skip_enabled_flag                <<  1) |
                                              (h->pps->cu_qp_delta_enabled_flag                   <<  2) |
                                              (h->pps->pic_slice_level_chroma_qp_offsets_present_flag <<  3) |
                                              (h->pps->weighted_pred_flag                         <<  4) |
                                              (h->pps->weighted_bipred_flag                       <<  5) |
                                              (h->pps->transquant_bypass_enable_flag              <<  6) |
                                              (h->pps->tiles_enabled_flag                         <<  7) |
                                              (h->pps->entropy_coding_sync_enabled_flag           <<  8) |
                                              (h->pps->uniform_spacing_flag                       <<  9) |
                                              ((h->pps->tiles_enabled_flag ? h->pps->loop_filter_across_tiles_enabled_flag : 0) << 10) |
                                              (h->pps->seq_loop_filter_across_slices_enabled_flag << 11) |
                                              (h->pps->deblocking_filter_override_enabled_flag    << 12) |
                                              (h->pps->disable_dbf                                << 13) |
                                              (h->pps->lists_modification_present_flag            << 14) |
                                              (h->pps->slice_header_extension_present_flag        << 15) |
                                              (IS_IRAP(h)                                         << 16) |
                                              (IS_IDR(h)                                          << 17) |
                                              /* IntraPicFlag */
                                              (IS_IRAP(h)                                         << 18) |
                                              (0                                                  << 19);
    pp->pps_cb_qp_offset            = h->pps->cb_qp_offset;
    pp->pps_cr_qp_offset            = h->pps->cr_qp_offset;
    if (h->pps->tiles_enabled_flag) {
        pp->num_tile_columns_minus1 = h->pps->num_tile_columns - 1;
        pp->num_tile_rows_minus1    = h->pps->num_tile_rows - 1;

        if (!h->pps->uniform_spacing_flag) {
            for (i = 0; i < h->pps->num_tile_columns; i++)
                pp->column_width_minus1[i] = h->pps->column_width[i] - 1;

            for (i = 0; i < h->pps->num_tile_rows; i++)
                pp->row_height_minus1[i] = h->pps->row_height[i] - 1;
        }
    }

    pp->diff_cu_qp_delta_depth           = h->pps->diff_cu_qp_delta_depth;
    pp->pps_beta_offset_div2             = h->pps->beta_offset / 2;
    pp->pps_tc_offset_div2               = h->pps->tc_offset / 2;
    pp->log2_parallel_merge_level_minus2 = h->pps->log2_parallel_merge_level - 2;
    pp->CurrPicOrderCntVal               = h->poc;

    // empty the lists
    memset(&pp->RefPicList, 0xff, sizeof(pp->RefPicList));
    memset(&pp->RefPicSetStCurrBefore, 0xff, sizeof(pp->RefPicSetStCurrBefore));
    memset(&pp->RefPicSetStCurrAfter, 0xff, sizeof(pp->RefPicSetStCurrAfter));
    memset(&pp->RefPicSetLtCurr, 0xff, sizeof(pp->RefPicSetLtCurr));

    // fill RefPicList from the DPB
    for (i = 0, j = 0; i < FF_ARRAY_ELEMS(h->DPB); i++) {
        const HEVCFrame *frame = &h->DPB[i];
        if (frame != current_picture && (frame->flags & (HEVC_FRAME_FLAG_LONG_REF | HEVC_FRAME_FLAG_SHORT_REF))) {
            fill_picture_entry(&pp->RefPicList[j], ff_dxva2_get_surface_index(ctx, frame->frame), !!(frame->flags & HEVC_FRAME_FLAG_LONG_REF));
            pp->PicOrderCntValList[j] = frame->poc;
            j++;
        }
    }

    #define DO_REF_LIST(ref_idx, ref_list) { \
        const RefPicList *rpl = &h->rps[ref_idx]; \
        av_assert0(rpl->nb_refs <= FF_ARRAY_ELEMS(pp->ref_list)); \
        for (j = 0, k = 0; j < rpl->nb_refs; j++) { \
            if (rpl->ref[j]) { \
                pp->ref_list[k] = get_refpic_index(pp, ff_dxva2_get_surface_index(ctx, rpl->ref[j]->frame)); \
                k++; \
            } \
        } \
    }

    // Fill short term and long term lists
    DO_REF_LIST(ST_CURR_BEF, RefPicSetStCurrBefore);
    DO_REF_LIST(ST_CURR_AFT, RefPicSetStCurrAfter);
    DO_REF_LIST(LT_CURR, RefPicSetLtCurr);

    pp->StatusReportFeedbackNumber = 1 + ctx->report_id++;
}

static void fill_scaling_lists(struct dxva_context *ctx, const HEVCContext *h, DXVA_Qmatrix_HEVC *qm)
{
    unsigned i, j, pos;
    const ScalingList *sl = h->pps->scaling_list_data_present_flag ?
                            &h->pps->scaling_list : &h->sps->scaling_list;

    memset(qm, 0, sizeof(*qm));
    for (i = 0; i < 6; i++) {
        for (j = 0; j < 16; j++) {
            pos = 4 * ff_hevc_diag_scan4x4_y[j] + ff_hevc_diag_scan4x4_x[j];
            qm->ucScalingLists0[i][j] = sl->sl[0][i][pos];
        }

        for (j = 0; j < 64; j++) {
            pos = 8 * ff_hevc_diag_scan8x8_y[j] + ff_hevc_diag_scan8x8_x[j];
            qm->ucScalingLists1[i][j] = sl->sl[1][i][pos];
            qm->ucScalingLists2[i][j] = sl->sl[2][i][pos];

            if (i < 2)
                qm->ucScalingLists3[i][j] = sl->sl[3][i][pos];
        }

        qm->ucScalingListDCCoefSizeID2[i] = sl->sl_dc[0][i];
        if (i < 2)
            qm->ucScalingListDCCoefSizeID3[i] = sl->sl_dc[1][i];
    }
}

static void fill_slice_short(DXVA_Slice_HEVC_Short *slice,
                             unsigned position, unsigned size)
{
    memset(slice, 0, sizeof(*slice));
    slice->BSNALunitDataLocation = position;
    slice->SliceBytesInBuffer    = size;
    slice->wBadSliceChopping     = 0;
}

static int commit_bitstream_and_slice_buffer(AVCodecContext *avctx,
                                             DXVA2_DecodeBufferDesc *bs,
                                             DXVA2_DecodeBufferDesc *sc)
{
    const HEVCContext *h = avctx->priv_data;
    struct dxva_context *ctx = avctx->hwaccel_context;
    const HEVCFrame *current_picture = h->ref;
    struct hevc_dxva2_picture_context *ctx_pic = current_picture->hwaccel_picture_private;
    DXVA_Slice_HEVC_Short *slice = NULL;
    void     *dxva_data_ptr;
    uint8_t  *dxva_data, *current, *end;
    unsigned dxva_size;
    void     *slice_data;
    unsigned slice_size;
    unsigned padding;
    unsigned i;

    /* Create an annex B bitstream buffer with only slice NAL and finalize slice */
    if (FAILED(IDirectXVideoDecoder_GetBuffer(ctx->decoder,
                                              DXVA2_BitStreamDateBufferType,
                                              &dxva_data_ptr, &dxva_size)))
        return -1;

    dxva_data = dxva_data_ptr;
    current = dxva_data;
    end = dxva_data + dxva_size;

    for (i = 0; i < ctx_pic->slice_count; i++) {
        static const uint8_t start_code[] = { 0, 0, 1 };
        static const unsigned start_code_size = sizeof(start_code);
        unsigned position, size;

        slice = &ctx_pic->slice_short[i];

        position = slice->BSNALunitDataLocation;
        size     = slice->SliceBytesInBuffer;
        if (start_code_size + size > end - current) {
            av_log(avctx, AV_LOG_ERROR, "Failed to build bitstream");
            break;
        }

        slice->BSNALunitDataLocation = current - dxva_data;
        slice->SliceBytesInBuffer    = start_code_size + size;

        memcpy(current, start_code, start_code_size);
        current += start_code_size;

        memcpy(current, &ctx_pic->bitstream[position], size);
        current += size;
    }
    padding = FFMIN(128 - ((current - dxva_data) & 127), end - current);
    if (slice && padding > 0) {
        memset(current, 0, padding);
        current += padding;

        slice->SliceBytesInBuffer += padding;
    }
    if (FAILED(IDirectXVideoDecoder_ReleaseBuffer(ctx->decoder,
                                                  DXVA2_BitStreamDateBufferType)))
        return -1;
    if (i < ctx_pic->slice_count)
        return -1;

    memset(bs, 0, sizeof(*bs));
    bs->CompressedBufferType = DXVA2_BitStreamDateBufferType;
    bs->DataSize             = current - dxva_data;
    bs->NumMBsInBuffer       = 0;

    slice_data = ctx_pic->slice_short;
    slice_size = ctx_pic->slice_count * sizeof(*ctx_pic->slice_short);

    av_assert0((bs->DataSize & 127) == 0);
    return ff_dxva2_commit_buffer(avctx, ctx, sc,
                                  DXVA2_SliceControlBufferType,
                                  slice_data, slice_size, 0);
}


static int dxva2_hevc_start_frame(AVCodecContext *avctx,
                                  av_unused const uint8_t *buffer,
                                  av_unused uint32_t size)
{
    const HEVCContext *h = avctx->priv_data;
    struct dxva_context *ctx = avctx->hwaccel_context;
    struct hevc_dxva2_picture_context *ctx_pic = h->ref->hwaccel_picture_private;

    if (!ctx->decoder || !ctx->cfg || ctx->surface_count <= 0)
        return -1;
    av_assert0(ctx_pic);

    /* Fill up DXVA_PicParams_HEVC */
    fill_picture_parameters(ctx, h, &ctx_pic->pp);

    /* Fill up DXVA_Qmatrix_HEVC */
    fill_scaling_lists(ctx, h, &ctx_pic->qm);

    ctx_pic->slice_count    = 0;
    ctx_pic->bitstream_size = 0;
    ctx_pic->bitstream      = NULL;
    return 0;
}

static int dxva2_hevc_decode_slice(AVCodecContext *avctx,
                                   const uint8_t *buffer,
                                   uint32_t size)
{
    const HEVCContext *h = avctx->priv_data;
    const HEVCFrame *current_picture = h->ref;
    struct hevc_dxva2_picture_context *ctx_pic = current_picture->hwaccel_picture_private;
    unsigned position;

    if (ctx_pic->slice_count >= MAX_SLICES)
        return -1;

    if (!ctx_pic->bitstream)
        ctx_pic->bitstream = buffer;
    ctx_pic->bitstream_size += size;

    position = buffer - ctx_pic->bitstream;
    fill_slice_short(&ctx_pic->slice_short[ctx_pic->slice_count], position, size);
    ctx_pic->slice_count++;

    return 0;
}

static int dxva2_hevc_end_frame(AVCodecContext *avctx)
{
    HEVCContext *h = avctx->priv_data;
    struct hevc_dxva2_picture_context *ctx_pic = h->ref->hwaccel_picture_private;
    int scale = ctx_pic->pp.dwCodingParamToolFlags & 1;
    int ret;

    if (ctx_pic->slice_count <= 0 || ctx_pic->bitstream_size <= 0)
        return -1;

    ret = ff_dxva2_common_end_frame(avctx, h->ref->frame,
                                    &ctx_pic->pp, sizeof(ctx_pic->pp),
                                    scale ? &ctx_pic->qm : NULL, scale ? sizeof(ctx_pic->qm) : 0,
                                    commit_bitstream_and_slice_buffer);
    return ret;
}

AVHWAccel ff_hevc_dxva2_hwaccel = {
    .name           = "hevc_dxva2",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_HEVC,
    .pix_fmt        = AV_PIX_FMT_DXVA2_VLD,
    .start_frame    = dxva2_hevc_start_frame,
    .decode_slice   = dxva2_hevc_decode_slice,
    .end_frame      = dxva2_hevc_end_frame,
    .frame_priv_data_size = sizeof(struct hevc_dxva2_picture_context),
};
