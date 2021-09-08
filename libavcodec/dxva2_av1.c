/*
 * DXVA2 AV1 HW acceleration.
 *
 * copyright (c) 2020 Hendrik Leppkes
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
#include "libavutil/pixdesc.h"

#include "dxva2_internal.h"
#include "av1dec.h"

#define MAX_TILES 256

struct AV1DXVAContext {
    FFDXVASharedContext shared;

    unsigned int bitstream_allocated;
    uint8_t *bitstream_cache;
};

struct av1_dxva2_picture_context {
    DXVA_PicParams_AV1    pp;
    unsigned              tile_count;
    DXVA_Tile_AV1         tiles[MAX_TILES];
    uint8_t              *bitstream;
    unsigned              bitstream_size;
};

static int get_bit_depth_from_seq(const AV1RawSequenceHeader *seq)
{
    if (seq->seq_profile == 2 && seq->color_config.high_bitdepth)
        return seq->color_config.twelve_bit ? 12 : 10;
    else if (seq->seq_profile <= 2 && seq->color_config.high_bitdepth)
        return 10;
    else
        return 8;
}

static int fill_picture_parameters(const AVCodecContext *avctx, AVDXVAContext *ctx, const AV1DecContext *h,
                                    DXVA_PicParams_AV1 *pp)
{
    int i,j, uses_lr;
    const AV1RawSequenceHeader *seq = h->raw_seq;
    const AV1RawFrameHeader *frame_header = h->raw_frame_header;
    const AV1RawFilmGrainParams *film_grain = &h->cur_frame.film_grain;

    unsigned char remap_lr_type[4] = { AV1_RESTORE_NONE, AV1_RESTORE_SWITCHABLE, AV1_RESTORE_WIENER, AV1_RESTORE_SGRPROJ };
    int apply_grain = !(avctx->export_side_data & AV_CODEC_EXPORT_DATA_FILM_GRAIN) && film_grain->apply_grain;

    memset(pp, 0, sizeof(*pp));

    pp->width  = avctx->width;
    pp->height = avctx->height;

    pp->max_width  = seq->max_frame_width_minus_1 + 1;
    pp->max_height = seq->max_frame_height_minus_1 + 1;

    pp->CurrPicTextureIndex = ff_dxva2_get_surface_index(avctx, ctx, h->cur_frame.tf.f);
    pp->superres_denom      = frame_header->use_superres ? frame_header->coded_denom + AV1_SUPERRES_DENOM_MIN : AV1_SUPERRES_NUM;
    pp->bitdepth            = get_bit_depth_from_seq(seq);
    pp->seq_profile         = seq->seq_profile;

    /* Tiling info */
    pp->tiles.cols = frame_header->tile_cols;
    pp->tiles.rows = frame_header->tile_rows;
    pp->tiles.context_update_id = frame_header->context_update_tile_id;

    for (i = 0; i < pp->tiles.cols; i++)
        pp->tiles.widths[i] = frame_header->width_in_sbs_minus_1[i] + 1;

    for (i = 0; i < pp->tiles.rows; i++)
        pp->tiles.heights[i] = frame_header->height_in_sbs_minus_1[i] + 1;

    /* Coding tools */
    pp->coding.use_128x128_superblock       = seq->use_128x128_superblock;
    pp->coding.intra_edge_filter            = seq->enable_intra_edge_filter;
    pp->coding.interintra_compound          = seq->enable_interintra_compound;
    pp->coding.masked_compound              = seq->enable_masked_compound;
    pp->coding.warped_motion                = frame_header->allow_warped_motion;
    pp->coding.dual_filter                  = seq->enable_dual_filter;
    pp->coding.jnt_comp                     = seq->enable_jnt_comp;
    pp->coding.screen_content_tools         = frame_header->allow_screen_content_tools;
    pp->coding.integer_mv                   = frame_header->force_integer_mv || !(frame_header->frame_type & 1);
    pp->coding.cdef                         = seq->enable_cdef;
    pp->coding.restoration                  = seq->enable_restoration;
    pp->coding.film_grain                   = seq->film_grain_params_present && !(avctx->export_side_data & AV_CODEC_EXPORT_DATA_FILM_GRAIN);
    pp->coding.intrabc                      = frame_header->allow_intrabc;
    pp->coding.high_precision_mv            = frame_header->allow_high_precision_mv;
    pp->coding.switchable_motion_mode       = frame_header->is_motion_mode_switchable;
    pp->coding.filter_intra                 = seq->enable_filter_intra;
    pp->coding.disable_frame_end_update_cdf = frame_header->disable_frame_end_update_cdf;
    pp->coding.disable_cdf_update           = frame_header->disable_cdf_update;
    pp->coding.reference_mode               = frame_header->reference_select;
    pp->coding.skip_mode                    = frame_header->skip_mode_present;
    pp->coding.reduced_tx_set               = frame_header->reduced_tx_set;
    pp->coding.superres                     = frame_header->use_superres;
    pp->coding.tx_mode                      = frame_header->tx_mode;
    pp->coding.use_ref_frame_mvs            = frame_header->use_ref_frame_mvs;
    pp->coding.enable_ref_frame_mvs         = seq->enable_ref_frame_mvs;
    pp->coding.reference_frame_update       = 1; // 0 for show_existing_frame with key frames, but those are not passed to the hwaccel

    /* Format & Picture Info flags */
    pp->format.frame_type     = frame_header->frame_type;
    pp->format.show_frame     = frame_header->show_frame;
    pp->format.showable_frame = frame_header->showable_frame;
    pp->format.subsampling_x  = seq->color_config.subsampling_x;
    pp->format.subsampling_y  = seq->color_config.subsampling_y;
    pp->format.mono_chrome    = seq->color_config.mono_chrome;

    /* References */
    pp->primary_ref_frame = frame_header->primary_ref_frame;
    pp->order_hint        = frame_header->order_hint;
    pp->order_hint_bits   = seq->enable_order_hint ? seq->order_hint_bits_minus_1 + 1 : 0;

    memset(pp->RefFrameMapTextureIndex, 0xFF, sizeof(pp->RefFrameMapTextureIndex));
    for (i = 0; i < AV1_REFS_PER_FRAME; i++) {
        int8_t ref_idx = frame_header->ref_frame_idx[i];
        AVFrame *ref_frame = h->ref[ref_idx].tf.f;

        pp->frame_refs[i].width  = ref_frame->width;
        pp->frame_refs[i].height = ref_frame->height;
        pp->frame_refs[i].Index  = ref_frame->buf[0] ? ref_idx : 0xFF;

        /* Global Motion */
        pp->frame_refs[i].wminvalid = (h->cur_frame.gm_type[AV1_REF_FRAME_LAST + i] == AV1_WARP_MODEL_IDENTITY);
        pp->frame_refs[i].wmtype    = h->cur_frame.gm_type[AV1_REF_FRAME_LAST + i];
        for (j = 0; j < 6; ++j) {
             pp->frame_refs[i].wmmat[j] = h->cur_frame.gm_params[AV1_REF_FRAME_LAST + i][j];
        }
    }
    for (i = 0; i < AV1_NUM_REF_FRAMES; i++) {
        AVFrame *ref_frame = h->ref[i].tf.f;
        if (ref_frame->buf[0])
            pp->RefFrameMapTextureIndex[i] = ff_dxva2_get_surface_index(avctx, ctx, ref_frame);
    }

    /* Loop filter parameters */
    pp->loop_filter.filter_level[0]        = frame_header->loop_filter_level[0];
    pp->loop_filter.filter_level[1]        = frame_header->loop_filter_level[1];
    pp->loop_filter.filter_level_u         = frame_header->loop_filter_level[2];
    pp->loop_filter.filter_level_v         = frame_header->loop_filter_level[3];
    pp->loop_filter.sharpness_level        = frame_header->loop_filter_sharpness;
    pp->loop_filter.mode_ref_delta_enabled = frame_header->loop_filter_delta_enabled;
    pp->loop_filter.mode_ref_delta_update  = frame_header->loop_filter_delta_update;
    pp->loop_filter.delta_lf_multi         = frame_header->delta_lf_multi;
    pp->loop_filter.delta_lf_present       = frame_header->delta_lf_present;
    pp->loop_filter.delta_lf_res           = frame_header->delta_lf_res;

    for (i = 0; i < AV1_TOTAL_REFS_PER_FRAME; i++) {
        pp->loop_filter.ref_deltas[i] = frame_header->loop_filter_ref_deltas[i];
    }

    pp->loop_filter.mode_deltas[0]                = frame_header->loop_filter_mode_deltas[0];
    pp->loop_filter.mode_deltas[1]                = frame_header->loop_filter_mode_deltas[1];
    pp->loop_filter.frame_restoration_type[0]     = remap_lr_type[frame_header->lr_type[0]];
    pp->loop_filter.frame_restoration_type[1]     = remap_lr_type[frame_header->lr_type[1]];
    pp->loop_filter.frame_restoration_type[2]     = remap_lr_type[frame_header->lr_type[2]];
    uses_lr = frame_header->lr_type[0] || frame_header->lr_type[1] || frame_header->lr_type[2];
    pp->loop_filter.log2_restoration_unit_size[0] = uses_lr ? (6 + frame_header->lr_unit_shift) : 8;
    pp->loop_filter.log2_restoration_unit_size[1] = uses_lr ? (6 + frame_header->lr_unit_shift - frame_header->lr_uv_shift) : 8;
    pp->loop_filter.log2_restoration_unit_size[2] = uses_lr ? (6 + frame_header->lr_unit_shift - frame_header->lr_uv_shift) : 8;

    /* Quantization */
    pp->quantization.delta_q_present = frame_header->delta_q_present;
    pp->quantization.delta_q_res     = frame_header->delta_q_res;
    pp->quantization.base_qindex     = frame_header->base_q_idx;
    pp->quantization.y_dc_delta_q    = frame_header->delta_q_y_dc;
    pp->quantization.u_dc_delta_q    = frame_header->delta_q_u_dc;
    pp->quantization.v_dc_delta_q    = frame_header->delta_q_v_dc;
    pp->quantization.u_ac_delta_q    = frame_header->delta_q_u_ac;
    pp->quantization.v_ac_delta_q    = frame_header->delta_q_v_ac;
    pp->quantization.qm_y            = frame_header->using_qmatrix ? frame_header->qm_y : 0xFF;
    pp->quantization.qm_u            = frame_header->using_qmatrix ? frame_header->qm_u : 0xFF;
    pp->quantization.qm_v            = frame_header->using_qmatrix ? frame_header->qm_v : 0xFF;

    /* Cdef parameters */
    pp->cdef.damping = frame_header->cdef_damping_minus_3;
    pp->cdef.bits    = frame_header->cdef_bits;
    for (i = 0; i < 8; i++) {
        pp->cdef.y_strengths[i].primary    = frame_header->cdef_y_pri_strength[i];
        pp->cdef.y_strengths[i].secondary  = frame_header->cdef_y_sec_strength[i];
        pp->cdef.uv_strengths[i].primary   = frame_header->cdef_uv_pri_strength[i];
        pp->cdef.uv_strengths[i].secondary = frame_header->cdef_uv_sec_strength[i];
    }

    /* Misc flags */
    pp->interp_filter = frame_header->interpolation_filter;

    /* Segmentation */
    pp->segmentation.enabled         = frame_header->segmentation_enabled;
    pp->segmentation.update_map      = frame_header->segmentation_update_map;
    pp->segmentation.update_data     = frame_header->segmentation_update_data;
    pp->segmentation.temporal_update = frame_header->segmentation_temporal_update;
    for (i = 0; i < AV1_MAX_SEGMENTS; i++) {
        for (j = 0; j < AV1_SEG_LVL_MAX; j++) {
            pp->segmentation.feature_mask[i].mask |= frame_header->feature_enabled[i][j] << j;
            pp->segmentation.feature_data[i][j]    = frame_header->feature_value[i][j];
        }
    }

    /* Film grain */
    if (apply_grain) {
        pp->film_grain.apply_grain              = 1;
        pp->film_grain.scaling_shift_minus8     = film_grain->grain_scaling_minus_8;
        pp->film_grain.chroma_scaling_from_luma = film_grain->chroma_scaling_from_luma;
        pp->film_grain.ar_coeff_lag             = film_grain->ar_coeff_lag;
        pp->film_grain.ar_coeff_shift_minus6    = film_grain->ar_coeff_shift_minus_6;
        pp->film_grain.grain_scale_shift        = film_grain->grain_scale_shift;
        pp->film_grain.overlap_flag             = film_grain->overlap_flag;
        pp->film_grain.clip_to_restricted_range = film_grain->clip_to_restricted_range;
        pp->film_grain.matrix_coeff_is_identity = (seq->color_config.matrix_coefficients == AVCOL_SPC_RGB);

        pp->film_grain.grain_seed               = film_grain->grain_seed;
        pp->film_grain.num_y_points             = film_grain->num_y_points;
        for (i = 0; i < film_grain->num_y_points; i++) {
            pp->film_grain.scaling_points_y[i][0] = film_grain->point_y_value[i];
            pp->film_grain.scaling_points_y[i][1] = film_grain->point_y_scaling[i];
        }
        pp->film_grain.num_cb_points            = film_grain->num_cb_points;
        for (i = 0; i < film_grain->num_cb_points; i++) {
            pp->film_grain.scaling_points_cb[i][0] = film_grain->point_cb_value[i];
            pp->film_grain.scaling_points_cb[i][1] = film_grain->point_cb_scaling[i];
        }
        pp->film_grain.num_cr_points            = film_grain->num_cr_points;
        for (i = 0; i < film_grain->num_cr_points; i++) {
            pp->film_grain.scaling_points_cr[i][0] = film_grain->point_cr_value[i];
            pp->film_grain.scaling_points_cr[i][1] = film_grain->point_cr_scaling[i];
        }
        for (i = 0; i < 24; i++) {
            pp->film_grain.ar_coeffs_y[i] = film_grain->ar_coeffs_y_plus_128[i];
        }
        for (i = 0; i < 25; i++) {
            pp->film_grain.ar_coeffs_cb[i] = film_grain->ar_coeffs_cb_plus_128[i];
            pp->film_grain.ar_coeffs_cr[i] = film_grain->ar_coeffs_cr_plus_128[i];
        }
        pp->film_grain.cb_mult      = film_grain->cb_mult;
        pp->film_grain.cb_luma_mult = film_grain->cb_luma_mult;
        pp->film_grain.cr_mult      = film_grain->cr_mult;
        pp->film_grain.cr_luma_mult = film_grain->cr_luma_mult;
        pp->film_grain.cb_offset    = film_grain->cb_offset;
        pp->film_grain.cr_offset    = film_grain->cr_offset;
        pp->film_grain.cr_offset    = film_grain->cr_offset;
    }

    // XXX: Setting the StatusReportFeedbackNumber breaks decoding on some drivers (tested on NVIDIA 457.09)
    // Status Reporting is not used by FFmpeg, hence not providing a number does not cause any issues
    //pp->StatusReportFeedbackNumber = 1 + DXVA_CONTEXT_REPORT_ID(avctx, ctx)++;
    return 0;
}

static int dxva2_av1_start_frame(AVCodecContext *avctx,
                                 av_unused const uint8_t *buffer,
                                 av_unused uint32_t size)
{
    const AV1DecContext *h = avctx->priv_data;
    AVDXVAContext *ctx = DXVA_CONTEXT(avctx);
    struct av1_dxva2_picture_context *ctx_pic = h->cur_frame.hwaccel_picture_private;

    if (!DXVA_CONTEXT_VALID(avctx, ctx))
        return -1;
    av_assert0(ctx_pic);

    /* Fill up DXVA_PicParams_AV1 */
    if (fill_picture_parameters(avctx, ctx, h, &ctx_pic->pp) < 0)
        return -1;

    ctx_pic->bitstream_size = 0;
    ctx_pic->bitstream      = NULL;
    return 0;
}

static int dxva2_av1_decode_slice(AVCodecContext *avctx,
                                  const uint8_t *buffer,
                                  uint32_t size)
{
    const AV1DecContext *h = avctx->priv_data;
    const AV1RawFrameHeader *frame_header = h->raw_frame_header;
    struct av1_dxva2_picture_context *ctx_pic = h->cur_frame.hwaccel_picture_private;
    struct AV1DXVAContext *ctx = avctx->internal->hwaccel_priv_data;
    void *tmp;

    ctx_pic->tile_count = frame_header->tile_cols * frame_header->tile_rows;

    /* too many tiles, exceeding all defined levels in the AV1 spec */
    if (ctx_pic->tile_count > MAX_TILES)
        return AVERROR(ENOSYS);

    /* Shortcut if all tiles are in the same buffer */
    if (ctx_pic->tile_count == h->tg_end - h->tg_start + 1) {
        ctx_pic->bitstream = (uint8_t *)buffer;
        ctx_pic->bitstream_size = size;

        for (uint32_t tile_num = 0; tile_num < ctx_pic->tile_count; tile_num++) {
            ctx_pic->tiles[tile_num].DataOffset   = h->tile_group_info[tile_num].tile_offset;
            ctx_pic->tiles[tile_num].DataSize     = h->tile_group_info[tile_num].tile_size;
            ctx_pic->tiles[tile_num].row          = h->tile_group_info[tile_num].tile_row;
            ctx_pic->tiles[tile_num].column       = h->tile_group_info[tile_num].tile_column;
            ctx_pic->tiles[tile_num].anchor_frame = 0xFF;
        }

        return 0;
    }

    /* allocate an internal buffer */
    tmp = av_fast_realloc(ctx->bitstream_cache, &ctx->bitstream_allocated,
                          ctx_pic->bitstream_size + size);
    if (!tmp) {
        return AVERROR(ENOMEM);
    }
    ctx_pic->bitstream = ctx->bitstream_cache = tmp;

    memcpy(ctx_pic->bitstream + ctx_pic->bitstream_size, buffer, size);

    for (uint32_t tile_num = h->tg_start; tile_num <= h->tg_end; tile_num++) {
        ctx_pic->tiles[tile_num].DataOffset   = ctx_pic->bitstream_size + h->tile_group_info[tile_num].tile_offset;
        ctx_pic->tiles[tile_num].DataSize     = h->tile_group_info[tile_num].tile_size;
        ctx_pic->tiles[tile_num].row          = h->tile_group_info[tile_num].tile_row;
        ctx_pic->tiles[tile_num].column       = h->tile_group_info[tile_num].tile_column;
        ctx_pic->tiles[tile_num].anchor_frame = 0xFF;
    }

    ctx_pic->bitstream_size += size;

    return 0;
}

static int commit_bitstream_and_slice_buffer(AVCodecContext *avctx,
                                             DECODER_BUFFER_DESC *bs,
                                             DECODER_BUFFER_DESC *sc)
{
    const AV1DecContext *h = avctx->priv_data;
    AVDXVAContext *ctx = DXVA_CONTEXT(avctx);
    struct av1_dxva2_picture_context *ctx_pic = h->cur_frame.hwaccel_picture_private;
    void     *dxva_data_ptr;
    uint8_t  *dxva_data;
    unsigned dxva_size;
    unsigned padding;
    unsigned type;

#if CONFIG_D3D11VA
    if (ff_dxva2_is_d3d11(avctx)) {
        type = D3D11_VIDEO_DECODER_BUFFER_BITSTREAM;
        if (FAILED(ID3D11VideoContext_GetDecoderBuffer(D3D11VA_CONTEXT(ctx)->video_context,
                                                       D3D11VA_CONTEXT(ctx)->decoder,
                                                       type,
                                                       &dxva_size, &dxva_data_ptr)))
            return -1;
    }
#endif
#if CONFIG_DXVA2
    if (avctx->pix_fmt == AV_PIX_FMT_DXVA2_VLD) {
        type = DXVA2_BitStreamDateBufferType;
        if (FAILED(IDirectXVideoDecoder_GetBuffer(DXVA2_CONTEXT(ctx)->decoder,
                                                  type,
                                                  &dxva_data_ptr, &dxva_size)))
            return -1;
    }
#endif

    dxva_data = dxva_data_ptr;

    if (ctx_pic->bitstream_size > dxva_size) {
        av_log(avctx, AV_LOG_ERROR, "Bitstream size exceeds hardware buffer");
        return -1;
    }

    memcpy(dxva_data, ctx_pic->bitstream, ctx_pic->bitstream_size);

    padding = FFMIN(128 - ((ctx_pic->bitstream_size) & 127), dxva_size - ctx_pic->bitstream_size);
    if (padding > 0) {
        memset(dxva_data + ctx_pic->bitstream_size, 0, padding);
        ctx_pic->bitstream_size += padding;
    }

#if CONFIG_D3D11VA
    if (ff_dxva2_is_d3d11(avctx))
        if (FAILED(ID3D11VideoContext_ReleaseDecoderBuffer(D3D11VA_CONTEXT(ctx)->video_context, D3D11VA_CONTEXT(ctx)->decoder, type)))
            return -1;
#endif
#if CONFIG_DXVA2
    if (avctx->pix_fmt == AV_PIX_FMT_DXVA2_VLD)
        if (FAILED(IDirectXVideoDecoder_ReleaseBuffer(DXVA2_CONTEXT(ctx)->decoder, type)))
            return -1;
#endif

#if CONFIG_D3D11VA
    if (ff_dxva2_is_d3d11(avctx)) {
        D3D11_VIDEO_DECODER_BUFFER_DESC *dsc11 = bs;
        memset(dsc11, 0, sizeof(*dsc11));
        dsc11->BufferType           = type;
        dsc11->DataSize             = ctx_pic->bitstream_size;
        dsc11->NumMBsInBuffer       = 0;

        type = D3D11_VIDEO_DECODER_BUFFER_SLICE_CONTROL;
    }
#endif
#if CONFIG_DXVA2
    if (avctx->pix_fmt == AV_PIX_FMT_DXVA2_VLD) {
        DXVA2_DecodeBufferDesc *dsc2 = bs;
        memset(dsc2, 0, sizeof(*dsc2));
        dsc2->CompressedBufferType = type;
        dsc2->DataSize             = ctx_pic->bitstream_size;
        dsc2->NumMBsInBuffer       = 0;

        type = DXVA2_SliceControlBufferType;
    }
#endif

    return ff_dxva2_commit_buffer(avctx, ctx, sc, type,
                                  ctx_pic->tiles, sizeof(*ctx_pic->tiles) * ctx_pic->tile_count, 0);
}

static int dxva2_av1_end_frame(AVCodecContext *avctx)
{
    const AV1DecContext *h = avctx->priv_data;
    struct av1_dxva2_picture_context *ctx_pic = h->cur_frame.hwaccel_picture_private;
    int ret;

    if (ctx_pic->bitstream_size <= 0)
        return -1;

    ret = ff_dxva2_common_end_frame(avctx, h->cur_frame.tf.f,
                                    &ctx_pic->pp, sizeof(ctx_pic->pp),
                                    NULL, 0,
                                    commit_bitstream_and_slice_buffer);

    return ret;
}

static int dxva2_av1_uninit(AVCodecContext *avctx)
{
    struct AV1DXVAContext *ctx = avctx->internal->hwaccel_priv_data;

    av_freep(&ctx->bitstream_cache);
    ctx->bitstream_allocated = 0;

    return ff_dxva2_decode_uninit(avctx);
}

#if CONFIG_AV1_DXVA2_HWACCEL
const AVHWAccel ff_av1_dxva2_hwaccel = {
    .name           = "av1_dxva2",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_AV1,
    .pix_fmt        = AV_PIX_FMT_DXVA2_VLD,
    .init           = ff_dxva2_decode_init,
    .uninit         = dxva2_av1_uninit,
    .start_frame    = dxva2_av1_start_frame,
    .decode_slice   = dxva2_av1_decode_slice,
    .end_frame      = dxva2_av1_end_frame,
    .frame_params   = ff_dxva2_common_frame_params,
    .frame_priv_data_size = sizeof(struct av1_dxva2_picture_context),
    .priv_data_size = sizeof(struct AV1DXVAContext),
};
#endif

#if CONFIG_AV1_D3D11VA_HWACCEL
const AVHWAccel ff_av1_d3d11va_hwaccel = {
    .name           = "av1_d3d11va",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_AV1,
    .pix_fmt        = AV_PIX_FMT_D3D11VA_VLD,
    .init           = ff_dxva2_decode_init,
    .uninit         = dxva2_av1_uninit,
    .start_frame    = dxva2_av1_start_frame,
    .decode_slice   = dxva2_av1_decode_slice,
    .end_frame      = dxva2_av1_end_frame,
    .frame_params   = ff_dxva2_common_frame_params,
    .frame_priv_data_size = sizeof(struct av1_dxva2_picture_context),
    .priv_data_size = sizeof(struct AV1DXVAContext),
};
#endif

#if CONFIG_AV1_D3D11VA2_HWACCEL
const AVHWAccel ff_av1_d3d11va2_hwaccel = {
    .name           = "av1_d3d11va2",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_AV1,
    .pix_fmt        = AV_PIX_FMT_D3D11,
    .init           = ff_dxva2_decode_init,
    .uninit         = dxva2_av1_uninit,
    .start_frame    = dxva2_av1_start_frame,
    .decode_slice   = dxva2_av1_decode_slice,
    .end_frame      = dxva2_av1_end_frame,
    .frame_params   = ff_dxva2_common_frame_params,
    .frame_priv_data_size = sizeof(struct av1_dxva2_picture_context),
    .priv_data_size = sizeof(struct AV1DXVAContext),
};
#endif
