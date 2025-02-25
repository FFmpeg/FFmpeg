/*
 * AV1 HW decode acceleration through NVDEC
 *
 * Copyright (c) 2020 Timo Rothenpieler
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

#include "libavutil/mem.h"
#include "avcodec.h"
#include "nvdec.h"
#include "decode.h"
#include "hwaccel_internal.h"
#include "internal.h"
#include "av1dec.h"


static int get_bit_depth_from_seq(const AV1RawSequenceHeader *seq)
{
    if (seq->seq_profile == 2 && seq->color_config.high_bitdepth)
        return seq->color_config.twelve_bit ? 12 : 10;
    else if (seq->seq_profile <= 2 && seq->color_config.high_bitdepth)
        return 10;
    else
        return 8;
}

static int nvdec_av1_start_frame(AVCodecContext *avctx,
                                 const AVBufferRef *buffer_ref,
                                 const uint8_t *buffer, uint32_t size)
{
    const AV1DecContext *s = avctx->priv_data;
    const AV1RawSequenceHeader *seq = s->raw_seq;
    const AV1RawFrameHeader *frame_header = s->raw_frame_header;
    const AV1RawFilmGrainParams *film_grain = &s->cur_frame.film_grain;

    NVDECContext      *ctx = avctx->internal->hwaccel_priv_data;
    CUVIDPICPARAMS     *pp = &ctx->pic_params;
    CUVIDAV1PICPARAMS *ppc = &pp->CodecSpecific.av1;
    FrameDecodeData *fdd;
    NVDECFrame *cf;
    AVFrame *cur_frame = s->cur_frame.f;

    unsigned char remap_lr_type[4] = { AV1_RESTORE_NONE, AV1_RESTORE_SWITCHABLE, AV1_RESTORE_WIENER, AV1_RESTORE_SGRPROJ };

    int apply_grain = !(avctx->export_side_data & AV_CODEC_EXPORT_DATA_FILM_GRAIN) && film_grain->apply_grain;
    int ret, i, j;

    ret = ff_nvdec_start_frame_sep_ref(avctx, cur_frame, apply_grain);
    if (ret < 0)
        return ret;

    fdd = cur_frame->private_ref;
    cf  = (NVDECFrame*)fdd->hwaccel_priv;

    *pp = (CUVIDPICPARAMS) {
        .PicWidthInMbs     = (cur_frame->width  + 15) / 16,
        .FrameHeightInMbs  = (cur_frame->height + 15) / 16,
        .CurrPicIdx        = cf->idx,
        .ref_pic_flag      = !!frame_header->refresh_frame_flags,
        .intra_pic_flag    = frame_header->frame_type == AV1_FRAME_INTRA_ONLY ||
                             frame_header->frame_type == AV1_FRAME_KEY,

        .CodecSpecific.av1 = {
            .width  = cur_frame->width,
            .height = cur_frame->height,

            .frame_offset = frame_header->order_hint,
            .decodePicIdx = cf->ref_idx,

            /* Sequence Header */
            .profile                    = seq->seq_profile,
            .use_128x128_superblock     = seq->use_128x128_superblock,
            .subsampling_x              = seq->color_config.subsampling_x,
            .subsampling_y              = seq->color_config.subsampling_y,
            .mono_chrome                = seq->color_config.mono_chrome,
            .bit_depth_minus8           = get_bit_depth_from_seq(seq) - 8,
            .enable_filter_intra        = seq->enable_filter_intra,
            .enable_intra_edge_filter   = seq->enable_intra_edge_filter,
            .enable_interintra_compound = seq->enable_interintra_compound,
            .enable_masked_compound     = seq->enable_masked_compound,
            .enable_dual_filter         = seq->enable_dual_filter,
            .enable_order_hint          = seq->enable_order_hint,
            .order_hint_bits_minus1     = seq->order_hint_bits_minus_1,
            .enable_jnt_comp            = seq->enable_jnt_comp,
            .enable_superres            = seq->enable_superres,
            .enable_cdef                = seq->enable_cdef,
            .enable_restoration         = seq->enable_restoration,
            .enable_fgs                 = seq->film_grain_params_present &&
                                          !(avctx->export_side_data & AV_CODEC_EXPORT_DATA_FILM_GRAIN),

            /* Frame Header */
            .frame_type                   = frame_header->frame_type,
            .show_frame                   = frame_header->show_frame,
            .disable_cdf_update           = frame_header->disable_cdf_update,
            .allow_screen_content_tools   = frame_header->allow_screen_content_tools,
            .force_integer_mv             = s->cur_frame.force_integer_mv,
            .coded_denom                  = frame_header->coded_denom,
            .allow_intrabc                = frame_header->allow_intrabc,
            .allow_high_precision_mv      = frame_header->allow_high_precision_mv,
            .interp_filter                = frame_header->interpolation_filter,
            .switchable_motion_mode       = frame_header->is_motion_mode_switchable,
            .use_ref_frame_mvs            = frame_header->use_ref_frame_mvs,
            .disable_frame_end_update_cdf = frame_header->disable_frame_end_update_cdf,
            .delta_q_present              = frame_header->delta_q_present,
            .delta_q_res                  = frame_header->delta_q_res,
            .using_qmatrix                = frame_header->using_qmatrix,
            .coded_lossless               = s->cur_frame.coded_lossless,
            .use_superres                 = frame_header->use_superres,
            .tx_mode                      = frame_header->tx_mode,
            .reference_mode               = frame_header->reference_select,
            .allow_warped_motion          = frame_header->allow_warped_motion,
            .reduced_tx_set               = frame_header->reduced_tx_set,
            .skip_mode                    = frame_header->skip_mode_present,

            /* Tiling Info */
            .num_tile_cols          = frame_header->tile_cols,
            .num_tile_rows          = frame_header->tile_rows,
            .context_update_tile_id = frame_header->context_update_tile_id,

            /* CDEF */
            .cdef_damping_minus_3 = frame_header->cdef_damping_minus_3,
            .cdef_bits            = frame_header->cdef_bits,

            /* SkipModeFrames */
            .SkipModeFrame0 = frame_header->skip_mode_present ?
                              s->cur_frame.skip_mode_frame_idx[0] : 0,
            .SkipModeFrame1 = frame_header->skip_mode_present ?
                              s->cur_frame.skip_mode_frame_idx[1] : 0,

            /* QP Information */
            .base_qindex     = frame_header->base_q_idx,
            .qp_y_dc_delta_q = frame_header->delta_q_y_dc,
            .qp_u_dc_delta_q = frame_header->delta_q_u_dc,
            .qp_v_dc_delta_q = frame_header->delta_q_v_dc,
            .qp_u_ac_delta_q = frame_header->delta_q_u_ac,
            .qp_v_ac_delta_q = frame_header->delta_q_v_ac,
            .qm_y            = frame_header->qm_y,
            .qm_u            = frame_header->qm_u,
            .qm_v            = frame_header->qm_v,

            /* Segmentation */
            .segmentation_enabled         = frame_header->segmentation_enabled,
            .segmentation_update_map      = frame_header->segmentation_update_map,
            .segmentation_update_data     = frame_header->segmentation_update_data,
            .segmentation_temporal_update = frame_header->segmentation_temporal_update,

            /* Loopfilter */
            .loop_filter_level[0]       = frame_header->loop_filter_level[0],
            .loop_filter_level[1]       = frame_header->loop_filter_level[1],
            .loop_filter_level_u        = frame_header->loop_filter_level[2],
            .loop_filter_level_v        = frame_header->loop_filter_level[3],
            .loop_filter_sharpness      = frame_header->loop_filter_sharpness,
            .loop_filter_delta_enabled  = frame_header->loop_filter_delta_enabled,
            .loop_filter_delta_update   = frame_header->loop_filter_delta_update,
            .loop_filter_mode_deltas[0] = frame_header->loop_filter_mode_deltas[0],
            .loop_filter_mode_deltas[1] = frame_header->loop_filter_mode_deltas[1],
            .delta_lf_present           = frame_header->delta_lf_present,
            .delta_lf_res               = frame_header->delta_lf_res,
            .delta_lf_multi             = frame_header->delta_lf_multi,

            /* Restoration */
            .lr_type[0]      = remap_lr_type[frame_header->lr_type[0]],
            .lr_type[1]      = remap_lr_type[frame_header->lr_type[1]],
            .lr_type[2]      = remap_lr_type[frame_header->lr_type[2]],
            .lr_unit_size[0] = 1 + frame_header->lr_unit_shift,
            .lr_unit_size[1] = 1 + frame_header->lr_unit_shift - frame_header->lr_uv_shift,
            .lr_unit_size[2] = 1 + frame_header->lr_unit_shift - frame_header->lr_uv_shift,

            /* Reference Frames */
            .temporal_layer_id = s->cur_frame.temporal_id,
            .spatial_layer_id  = s->cur_frame.spatial_id,

            /* Film Grain Params */
            .apply_grain              = apply_grain,
            .overlap_flag             = film_grain->overlap_flag,
            .scaling_shift_minus8     = film_grain->grain_scaling_minus_8,
            .chroma_scaling_from_luma = film_grain->chroma_scaling_from_luma,
            .ar_coeff_lag             = film_grain->ar_coeff_lag,
            .ar_coeff_shift_minus6    = film_grain->ar_coeff_shift_minus_6,
            .grain_scale_shift        = film_grain->grain_scale_shift,
            .clip_to_restricted_range = film_grain->clip_to_restricted_range,
            .num_y_points             = film_grain->num_y_points,
            .num_cb_points            = film_grain->num_cb_points,
            .num_cr_points            = film_grain->num_cr_points,
            .random_seed              = film_grain->grain_seed,
            .cb_mult                  = film_grain->cb_mult,
            .cb_luma_mult             = film_grain->cb_luma_mult,
            .cb_offset                = film_grain->cb_offset,
            .cr_mult                  = film_grain->cr_mult,
            .cr_luma_mult             = film_grain->cr_luma_mult,
            .cr_offset                = film_grain->cr_offset
        }
    };

    /* Tiling Info */
    for (i = 0; i < frame_header->tile_cols; ++i) {
        ppc->tile_widths[i] = frame_header->width_in_sbs_minus_1[i] + 1;
    }
    for (i = 0; i < frame_header->tile_rows; ++i) {
        ppc->tile_heights[i] = frame_header->height_in_sbs_minus_1[i] + 1;
    }

    /* CDEF */
    for (i = 0; i < (1 << frame_header->cdef_bits); ++i) {
        ppc->cdef_y_strength[i] = (frame_header->cdef_y_pri_strength[i] & 0x0F) | (frame_header->cdef_y_sec_strength[i] << 4);
        ppc->cdef_uv_strength[i] = (frame_header->cdef_uv_pri_strength[i] & 0x0F) | (frame_header->cdef_uv_sec_strength[i] << 4);
    }

    /* Segmentation */
    for (i = 0; i < AV1_MAX_SEGMENTS; ++i) {
        ppc->segmentation_feature_mask[i] = 0;
        for (j = 0; j < AV1_SEG_LVL_MAX; ++j) {
            ppc->segmentation_feature_mask[i] |= frame_header->feature_enabled[i][j] << j;
            ppc->segmentation_feature_data[i][j] = frame_header->feature_value[i][j];
        }
    }

    for (i = 0; i < AV1_NUM_REF_FRAMES; ++i) {
        /* Loopfilter */
        ppc->loop_filter_ref_deltas[i] = frame_header->loop_filter_ref_deltas[i];

        /* Reference Frames */
        ppc->ref_frame_map[i] = ff_nvdec_get_ref_idx(s->ref[i].f);
    }

    if (frame_header->primary_ref_frame == AV1_PRIMARY_REF_NONE) {
        ppc->primary_ref_frame = -1;
    } else {
        int8_t pri_ref_idx = frame_header->ref_frame_idx[frame_header->primary_ref_frame];
        ppc->primary_ref_frame = ppc->ref_frame_map[pri_ref_idx];
    }

    for (i = 0; i < AV1_REFS_PER_FRAME; ++i) {
        /* Ref Frame List */
        int8_t ref_idx = frame_header->ref_frame_idx[i];
        AVFrame *ref_frame = s->ref[ref_idx].f;

        ppc->ref_frame[i].index = ppc->ref_frame_map[ref_idx];
        ppc->ref_frame[i].width  = ref_frame ? ref_frame->width  : 0;
        ppc->ref_frame[i].height = ref_frame ? ref_frame->height : 0;

        /* Global Motion */
        ppc->global_motion[i].invalid = !frame_header->is_global[AV1_REF_FRAME_LAST + i];
        ppc->global_motion[i].wmtype = s->cur_frame.gm_type[AV1_REF_FRAME_LAST + i];
        for (j = 0; j < 6; ++j) {
            ppc->global_motion[i].wmmat[j] = s->cur_frame.gm_params[AV1_REF_FRAME_LAST + i][j];
        }
    }

    /* Film Grain Params */
    if (apply_grain) {
        for (i = 0; i < 14; ++i) {
            ppc->scaling_points_y[i][0] = film_grain->point_y_value[i];
            ppc->scaling_points_y[i][1] = film_grain->point_y_scaling[i];
        }
        for (i = 0; i < 10; ++i) {
            ppc->scaling_points_cb[i][0] = film_grain->point_cb_value[i];
            ppc->scaling_points_cb[i][1] = film_grain->point_cb_scaling[i];
            ppc->scaling_points_cr[i][0] = film_grain->point_cr_value[i];
            ppc->scaling_points_cr[i][1] = film_grain->point_cr_scaling[i];
        }
        for (i = 0; i < 24; ++i) {
            ppc->ar_coeffs_y[i] = (short)film_grain->ar_coeffs_y_plus_128[i] - 128;
        }
        for (i = 0; i < 25; ++i) {
            ppc->ar_coeffs_cb[i] = (short)film_grain->ar_coeffs_cb_plus_128[i] - 128;
            ppc->ar_coeffs_cr[i] = (short)film_grain->ar_coeffs_cr_plus_128[i] - 128;
        }
    }

    return 0;
}

static int nvdec_av1_decode_slice(AVCodecContext *avctx, const uint8_t *buffer, uint32_t size)
{
    const AV1DecContext *s = avctx->priv_data;
    const AV1RawFrameHeader *frame_header = s->raw_frame_header;
    NVDECContext *ctx = avctx->internal->hwaccel_priv_data;
    void *tmp;

    ctx->nb_slices = frame_header->tile_cols * frame_header->tile_rows;

    tmp = av_fast_realloc(ctx->slice_offsets, &ctx->slice_offsets_allocated,
                          ctx->nb_slices * 2 * sizeof(*ctx->slice_offsets));
    if (!tmp) {
        return AVERROR(ENOMEM);
    }
    ctx->slice_offsets = tmp;

    /* Shortcut if all tiles are in the same buffer */
    if (ctx->nb_slices == s->tg_end - s->tg_start + 1) {
        ctx->bitstream = buffer;
        ctx->bitstream_len = size;

        for (int i = 0; i < ctx->nb_slices; ++i) {
            ctx->slice_offsets[i*2    ] = s->tile_group_info[i].tile_offset;
            ctx->slice_offsets[i*2 + 1] = ctx->slice_offsets[i*2] + s->tile_group_info[i].tile_size;
        }

        return 0;
    }

    tmp = av_fast_realloc(ctx->bitstream_internal, &ctx->bitstream_allocated,
                          ctx->bitstream_len + size);
    if (!tmp) {
        return AVERROR(ENOMEM);
    }
    ctx->bitstream = ctx->bitstream_internal = tmp;

    memcpy(ctx->bitstream_internal + ctx->bitstream_len, buffer, size);

    for (uint32_t tile_num = s->tg_start; tile_num <= s->tg_end; ++tile_num) {
        ctx->slice_offsets[tile_num*2    ] = ctx->bitstream_len + s->tile_group_info[tile_num].tile_offset;
        ctx->slice_offsets[tile_num*2 + 1] = ctx->slice_offsets[tile_num*2] + s->tile_group_info[tile_num].tile_size;
    }
    ctx->bitstream_len += size;

    return 0;
}

static int nvdec_av1_frame_params(AVCodecContext *avctx, AVBufferRef *hw_frames_ctx)
{
    /* Maximum of 8 reference frames, but potentially stored twice due to film grain */
    return ff_nvdec_frame_params(avctx, hw_frames_ctx, 8 * 2, 0);
}

const FFHWAccel ff_av1_nvdec_hwaccel = {
    .p.name               = "av1_nvdec",
    .p.type               = AVMEDIA_TYPE_VIDEO,
    .p.id                 = AV_CODEC_ID_AV1,
    .p.pix_fmt            = AV_PIX_FMT_CUDA,
    .start_frame          = nvdec_av1_start_frame,
    .end_frame            = ff_nvdec_simple_end_frame,
    .decode_slice         = nvdec_av1_decode_slice,
    .frame_params         = nvdec_av1_frame_params,
    .init                 = ff_nvdec_decode_init,
    .uninit               = ff_nvdec_decode_uninit,
    .priv_data_size       = sizeof(NVDECContext),
};
