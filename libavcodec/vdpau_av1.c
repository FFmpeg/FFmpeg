/*
 * AV1 HW decode acceleration through VDPAU
 *
 * Copyright (c) 2022 Manoj Gupta Bonda
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
 * License along with FFmpeg; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <vdpau/vdpau.h>
#include "libavutil/pixdesc.h"
#include "avcodec.h"
#include "internal.h"
#include "av1dec.h"
#include "hwaccel_internal.h"
#include "vdpau.h"
#include "vdpau_internal.h"

static int get_bit_depth_from_seq(const AV1RawSequenceHeader *seq)
{
    if (seq->seq_profile == 2 && seq->color_config.high_bitdepth) {
        return seq->color_config.twelve_bit ? 12 : 10;
    } else if (seq->seq_profile <= 2 && seq->color_config.high_bitdepth) {
        return 10;
    } else {
        return 8;
    }
}

static int vdpau_av1_start_frame(AVCodecContext *avctx,
                                  const uint8_t *buffer, uint32_t size)
{
    AV1DecContext *s = avctx->priv_data;
    const AV1RawSequenceHeader *seq = s->raw_seq;
    const AV1RawFrameHeader *frame_header = s->raw_frame_header;
    const AV1RawFilmGrainParams *film_grain = &s->cur_frame.film_grain;

    struct vdpau_picture_context *pic_ctx = s->cur_frame.hwaccel_picture_private;
    int i,j;

    unsigned char remap_lr_type[4] = { AV1_RESTORE_NONE, AV1_RESTORE_SWITCHABLE, AV1_RESTORE_WIENER, AV1_RESTORE_SGRPROJ };


    VdpPictureInfoAV1 *info = &pic_ctx->info.av1;
    const AVPixFmtDescriptor *pixdesc = av_pix_fmt_desc_get(avctx->sw_pix_fmt);
    if (!pixdesc) {
        return AV_PIX_FMT_NONE;
    }

    info->width = avctx->width;
    info->height = avctx->height;


    info->frame_offset = frame_header->order_hint;

    /* Sequence Header */
    info->profile                    = seq->seq_profile;
    info->use_128x128_superblock     = seq->use_128x128_superblock;
    info->subsampling_x              = seq->color_config.subsampling_x;
    info->subsampling_y              = seq->color_config.subsampling_y;
    info->mono_chrome                = seq->color_config.mono_chrome;
    info->bit_depth_minus8           = get_bit_depth_from_seq(seq) - 8;
    info->enable_filter_intra        = seq->enable_filter_intra;
    info->enable_intra_edge_filter   = seq->enable_intra_edge_filter;
    info->enable_interintra_compound = seq->enable_interintra_compound;
    info->enable_masked_compound     = seq->enable_masked_compound;
    info->enable_dual_filter         = seq->enable_dual_filter;
    info->enable_order_hint          = seq->enable_order_hint;
    info->order_hint_bits_minus1     = seq->order_hint_bits_minus_1;
    info->enable_jnt_comp            = seq->enable_jnt_comp;
    info->enable_superres            = seq->enable_superres;
    info->enable_cdef                = seq->enable_cdef;
    info->enable_restoration         = seq->enable_restoration;
    info->enable_fgs                 = seq->film_grain_params_present;

    /* Frame Header */
    info->frame_type                   = frame_header->frame_type;
    info->show_frame                   = frame_header->show_frame;
    info->disable_cdf_update           = frame_header->disable_cdf_update;
    info->allow_screen_content_tools   = frame_header->allow_screen_content_tools;
    info->force_integer_mv             = frame_header->force_integer_mv ||
                                    frame_header->frame_type == AV1_FRAME_INTRA_ONLY ||
                                    frame_header->frame_type == AV1_FRAME_KEY;
    info->coded_denom                  = frame_header->coded_denom;
    info->allow_intrabc                = frame_header->allow_intrabc;
    info->allow_high_precision_mv      = frame_header->allow_high_precision_mv;
    info->interp_filter                = frame_header->interpolation_filter;
    info->switchable_motion_mode       = frame_header->is_motion_mode_switchable;
    info->use_ref_frame_mvs            = frame_header->use_ref_frame_mvs;
    info->disable_frame_end_update_cdf = frame_header->disable_frame_end_update_cdf;
    info->delta_q_present              = frame_header->delta_q_present;
    info->delta_q_res                  = frame_header->delta_q_res;
    info->using_qmatrix                = frame_header->using_qmatrix;
    info->coded_lossless               = s->cur_frame.coded_lossless;
    info->use_superres                 = frame_header->use_superres;
    info->tx_mode                      = frame_header->tx_mode;
    info->reference_mode               = frame_header->reference_select;
    info->allow_warped_motion          = frame_header->allow_warped_motion;
    info->reduced_tx_set               = frame_header->reduced_tx_set;
    info->skip_mode                    = frame_header->skip_mode_present;

    /* Tiling Info */
    info->num_tile_cols          = frame_header->tile_cols;
    info->num_tile_rows          = frame_header->tile_rows;
    info->context_update_tile_id = frame_header->context_update_tile_id;

    /* CDEF */
    info->cdef_damping_minus_3 = frame_header->cdef_damping_minus_3;
    info->cdef_bits            = frame_header->cdef_bits;

    /* SkipModeFrames */
    info->SkipModeFrame0 = frame_header->skip_mode_present ?
                      s->cur_frame.skip_mode_frame_idx[0] : 0;
    info->SkipModeFrame1 = frame_header->skip_mode_present ?
                      s->cur_frame.skip_mode_frame_idx[1] : 0;

    /* QP Information */
    info->base_qindex     = frame_header->base_q_idx;
    info->qp_y_dc_delta_q = frame_header->delta_q_y_dc;
    info->qp_u_dc_delta_q = frame_header->delta_q_u_dc;
    info->qp_v_dc_delta_q = frame_header->delta_q_v_dc;
    info->qp_u_ac_delta_q = frame_header->delta_q_u_ac;
    info->qp_v_ac_delta_q = frame_header->delta_q_v_ac;
    info->qm_y            = frame_header->qm_y;
    info->qm_u            = frame_header->qm_u;
    info->qm_v            = frame_header->qm_v;

    /* Segmentation */
    info->segmentation_enabled         = frame_header->segmentation_enabled;
    info->segmentation_update_map      = frame_header->segmentation_update_map;
    info->segmentation_update_data     = frame_header->segmentation_update_data;
    info->segmentation_temporal_update = frame_header->segmentation_temporal_update;

    /* Loopfilter */
    info->loop_filter_level[0]       = frame_header->loop_filter_level[0];
    info->loop_filter_level[1]       = frame_header->loop_filter_level[1];
    info->loop_filter_level_u        = frame_header->loop_filter_level[2];
    info->loop_filter_level_v        = frame_header->loop_filter_level[3];
    info->loop_filter_sharpness      = frame_header->loop_filter_sharpness;
    info->loop_filter_delta_enabled  = frame_header->loop_filter_delta_enabled;
    info->loop_filter_delta_update   = frame_header->loop_filter_delta_update;
    info->loop_filter_mode_deltas[0] = frame_header->loop_filter_mode_deltas[0];
    info->loop_filter_mode_deltas[1] = frame_header->loop_filter_mode_deltas[1];
    info->delta_lf_present           = frame_header->delta_lf_present;
    info->delta_lf_res               = frame_header->delta_lf_res;
    info->delta_lf_multi             = frame_header->delta_lf_multi;

    /* Restoration */
    info->lr_type[0]      = remap_lr_type[frame_header->lr_type[0]];
    info->lr_type[1]      = remap_lr_type[frame_header->lr_type[1]];
    info->lr_type[2]      = remap_lr_type[frame_header->lr_type[2]];
    info->lr_unit_size[0] = 1 + frame_header->lr_unit_shift;
    info->lr_unit_size[1] = 1 + frame_header->lr_unit_shift - frame_header->lr_uv_shift;
    info->lr_unit_size[2] = 1 + frame_header->lr_unit_shift - frame_header->lr_uv_shift;

    /* Reference Frames */
    info->temporal_layer_id = s->cur_frame.temporal_id;
    info->spatial_layer_id  = s->cur_frame.spatial_id;

    /* Film Grain Params */
    info->apply_grain              = film_grain->apply_grain;
    info->overlap_flag             = film_grain->overlap_flag;
    info->scaling_shift_minus8     = film_grain->grain_scaling_minus_8;
    info->chroma_scaling_from_luma = film_grain->chroma_scaling_from_luma;
    info->ar_coeff_lag             = film_grain->ar_coeff_lag;
    info->ar_coeff_shift_minus6    = film_grain->ar_coeff_shift_minus_6;
    info->grain_scale_shift        = film_grain->grain_scale_shift;
    info->clip_to_restricted_range = film_grain->clip_to_restricted_range;
    info->num_y_points             = film_grain->num_y_points;
    info->num_cb_points            = film_grain->num_cb_points;
    info->num_cr_points            = film_grain->num_cr_points;
    info->random_seed              = film_grain->grain_seed;
    info->cb_mult                  = film_grain->cb_mult;
    info->cb_luma_mult             = film_grain->cb_luma_mult;
    info->cb_offset                = film_grain->cb_offset;
    info->cr_mult                  = film_grain->cr_mult;
    info->cr_luma_mult             = film_grain->cr_luma_mult;
    info->cr_offset                = film_grain->cr_offset;

    /* Tiling Info */
    for (i = 0; i < frame_header->tile_cols; ++i) {
        info->tile_widths[i] = frame_header->width_in_sbs_minus_1[i] + 1;
    }
    for (i = 0; i < frame_header->tile_rows; ++i) {
        info->tile_heights[i] = frame_header->height_in_sbs_minus_1[i] + 1;
    }

    /* CDEF */
    for (i = 0; i < (1 << frame_header->cdef_bits); ++i) {
        info->cdef_y_strength[i] = (frame_header->cdef_y_pri_strength[i] & 0x0F) | (frame_header->cdef_y_sec_strength[i] << 4);
        info->cdef_uv_strength[i] = (frame_header->cdef_uv_pri_strength[i] & 0x0F) | (frame_header->cdef_uv_sec_strength[i] << 4);
    }


    /* Segmentation */
    for (i = 0; i < AV1_MAX_SEGMENTS; ++i) {
        info->segmentation_feature_mask[i] = 0;
        for (j = 0; j < AV1_SEG_LVL_MAX; ++j) {
            info->segmentation_feature_mask[i] |= frame_header->feature_enabled[i][j] << j;
            info->segmentation_feature_data[i][j] = frame_header->feature_value[i][j];
        }
    }

    for (i = 0; i < AV1_NUM_REF_FRAMES; ++i) {
        /* Loopfilter */
        info->loop_filter_ref_deltas[i] = frame_header->loop_filter_ref_deltas[i];

        /* Reference Frames */
        info->ref_frame_map[i] = ff_vdpau_get_surface_id(s->ref[i].f) ? ff_vdpau_get_surface_id(s->ref[i].f) : VDP_INVALID_HANDLE;
    }

    if (frame_header->primary_ref_frame == AV1_PRIMARY_REF_NONE) {
        info->primary_ref_frame = -1;
    } else {
        int8_t pri_ref_idx = frame_header->ref_frame_idx[frame_header->primary_ref_frame];
        info->primary_ref_frame = info->ref_frame_map[pri_ref_idx];
    }

    for (i = 0; i < AV1_REFS_PER_FRAME; ++i) {
        /* Ref Frame List */
        int8_t ref_idx = frame_header->ref_frame_idx[i];
        AVFrame *ref_frame = s->ref[ref_idx].f;

        info->ref_frame[i].index = info->ref_frame_map[ref_idx];
        info->ref_frame[i].width = ref_frame->width;
        info->ref_frame[i].height = ref_frame->height;

        /* Global Motion */
        info->global_motion[i].invalid = !frame_header->is_global[AV1_REF_FRAME_LAST + i];
        info->global_motion[i].wmtype = s->cur_frame.gm_type[AV1_REF_FRAME_LAST + i];
        for (j = 0; j < 6; ++j) {
            info->global_motion[i].wmmat[j] = s->cur_frame.gm_params[AV1_REF_FRAME_LAST + i][j];
        }
    }

    /* Film Grain Params */
    if (film_grain->apply_grain) {
        for (i = 0; i < 14; ++i) {
            info->scaling_points_y[i][0] = film_grain->point_y_value[i];
            info->scaling_points_y[i][1] = film_grain->point_y_scaling[i];
        }
        for (i = 0; i < 10; ++i) {
            info->scaling_points_cb[i][0] = film_grain->point_cb_value[i];
            info->scaling_points_cb[i][1] = film_grain->point_cb_scaling[i];
            info->scaling_points_cr[i][0] = film_grain->point_cr_value[i];
            info->scaling_points_cr[i][1] = film_grain->point_cr_scaling[i];
        }
        for (i = 0; i < 24; ++i) {
            info->ar_coeffs_y[i] = (short)film_grain->ar_coeffs_y_plus_128[i] - 128;
        }
        for (i = 0; i < 25; ++i) {
            info->ar_coeffs_cb[i] = (short)film_grain->ar_coeffs_cb_plus_128[i] - 128;
            info->ar_coeffs_cr[i] = (short)film_grain->ar_coeffs_cr_plus_128[i] - 128;
        }
    }


    return ff_vdpau_common_start_frame(pic_ctx, buffer, size);

}

static int vdpau_av1_decode_slice(AVCodecContext *avctx,
                                   const uint8_t *buffer, uint32_t size)
{
    const AV1DecContext *s = avctx->priv_data;
    const AV1RawFrameHeader *frame_header = s->raw_frame_header;
    struct vdpau_picture_context *pic_ctx = s->cur_frame.hwaccel_picture_private;
    VdpPictureInfoAV1 *info = &pic_ctx->info.av1;
    int val;
    int nb_slices;
    VdpBitstreamBuffer *buffers = pic_ctx->bitstream_buffers;
    int bitstream_len = 0;

    nb_slices = frame_header->tile_cols * frame_header->tile_rows;
    /* Shortcut if all tiles are in the same buffer*/
    if (nb_slices == s->tg_end - s->tg_start + 1) {
        for (int i = 0; i < nb_slices; ++i) {
            info->tile_info[i*2    ] = s->tile_group_info[i].tile_offset;
            info->tile_info[i*2 + 1] = info->tile_info[i*2] + s->tile_group_info[i].tile_size;
        }
        val = ff_vdpau_add_buffer(pic_ctx, buffer, size);
        if (val) {
            return val;
        }

        return 0;
    }

    for(int i = 0; i < pic_ctx->bitstream_buffers_used; i++) {
        bitstream_len += buffers->bitstream_bytes;
        buffers++;
    }

    for (uint32_t tile_num = s->tg_start; tile_num <= s->tg_end; ++tile_num) {
        info->tile_info[tile_num*2    ] = bitstream_len + s->tile_group_info[tile_num].tile_offset;
        info->tile_info[tile_num*2 + 1] = info->tile_info[tile_num*2] + s->tile_group_info[tile_num].tile_size;
    }

    val = ff_vdpau_add_buffer(pic_ctx, buffer, size);
    if (val) {
        return val;
    }

    return 0;
}

static int vdpau_av1_end_frame(AVCodecContext *avctx)
{
    const AV1DecContext *s = avctx->priv_data;
    struct vdpau_picture_context *pic_ctx = s->cur_frame.hwaccel_picture_private;

    int val;

    val = ff_vdpau_common_end_frame(avctx, s->cur_frame.f, pic_ctx);
    if (val < 0)
        return val;

    return 0;
}

static int vdpau_av1_init(AVCodecContext *avctx)
{
    VdpDecoderProfile profile;
    uint32_t level = avctx->level;

    switch (avctx->profile) {
    case AV_PROFILE_AV1_MAIN:
        profile = VDP_DECODER_PROFILE_AV1_MAIN;
        break;
    case AV_PROFILE_AV1_HIGH:
        profile = VDP_DECODER_PROFILE_AV1_HIGH;
        break;
    case AV_PROFILE_AV1_PROFESSIONAL:
        profile = VDP_DECODER_PROFILE_AV1_PROFESSIONAL;
        break;
    default:
        return AVERROR(ENOTSUP);
    }

    return ff_vdpau_common_init(avctx, profile, level);
}

const FFHWAccel ff_av1_vdpau_hwaccel = {
    .p.name         = "av1_vdpau",
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_AV1,
    .p.pix_fmt      = AV_PIX_FMT_VDPAU,
    .start_frame    = vdpau_av1_start_frame,
    .end_frame      = vdpau_av1_end_frame,
    .decode_slice   = vdpau_av1_decode_slice,
    .frame_priv_data_size = sizeof(struct vdpau_picture_context),
    .init           = vdpau_av1_init,
    .uninit         = ff_vdpau_common_uninit,
    .frame_params   = ff_vdpau_common_frame_params,
    .priv_data_size = sizeof(VDPAUContext),
    .caps_internal  = HWACCEL_CAP_ASYNC_SAFE,
};
