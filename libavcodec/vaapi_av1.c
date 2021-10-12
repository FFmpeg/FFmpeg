/*
 * AV1 HW decode acceleration through VA API
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

#include "libavutil/pixdesc.h"
#include "hwconfig.h"
#include "vaapi_decode.h"
#include "internal.h"
#include "av1dec.h"

typedef struct VAAPIAV1FrameRef {
    ThreadFrame frame;
    int valid;
} VAAPIAV1FrameRef;

typedef struct VAAPIAV1DecContext {
    VAAPIDecodeContext base;

    /**
     * For film grain case, VAAPI generate 2 output for each frame,
     * current_frame will not apply film grain, and will be used for
     * references for next frames. Maintain the reference list without
     * applying film grain here. And current_display_picture will be
     * used to apply film grain and push to downstream.
    */
    VAAPIAV1FrameRef ref_tab[AV1_NUM_REF_FRAMES];
    ThreadFrame tmp_frame;
} VAAPIAV1DecContext;

static VASurfaceID vaapi_av1_surface_id(AV1Frame *vf)
{
    if (vf)
        return ff_vaapi_get_surface_id(vf->tf.f);
    else
        return VA_INVALID_SURFACE;
}

static int8_t vaapi_av1_get_bit_depth_idx(AVCodecContext *avctx)
{
    AV1DecContext *s = avctx->priv_data;
    const AV1RawSequenceHeader *seq = s->raw_seq;
    int8_t bit_depth = 8;

    if (seq->seq_profile == 2 && seq->color_config.high_bitdepth)
        bit_depth = seq->color_config.twelve_bit ? 12 : 10;
    else if (seq->seq_profile <= 2)
        bit_depth = seq->color_config.high_bitdepth ? 10 : 8;
    else {
        av_log(avctx, AV_LOG_ERROR,
               "Couldn't get bit depth from profile:%d.\n", seq->seq_profile);
        return -1;
    }
    return bit_depth == 8 ? 0 : bit_depth == 10 ? 1 : 2;
}

static int vaapi_av1_decode_init(AVCodecContext *avctx)
{
    VAAPIAV1DecContext *ctx = avctx->internal->hwaccel_priv_data;

    ctx->tmp_frame.f = av_frame_alloc();
    if (!ctx->tmp_frame.f) {
        av_log(avctx, AV_LOG_ERROR,
               "Failed to allocate frame.\n");
        return AVERROR(ENOMEM);
    }

    for (int i = 0; i < FF_ARRAY_ELEMS(ctx->ref_tab); i++) {
        ctx->ref_tab[i].frame.f = av_frame_alloc();
        if (!ctx->ref_tab[i].frame.f) {
            av_log(avctx, AV_LOG_ERROR,
                   "Failed to allocate reference table frame %d.\n", i);
            return AVERROR(ENOMEM);
        }
        ctx->ref_tab[i].valid = 0;
    }

    return ff_vaapi_decode_init(avctx);
}

static int vaapi_av1_decode_uninit(AVCodecContext *avctx)
{
    VAAPIAV1DecContext *ctx = avctx->internal->hwaccel_priv_data;

    if (ctx->tmp_frame.f->buf[0])
        ff_thread_release_buffer(avctx, &ctx->tmp_frame);
    av_frame_free(&ctx->tmp_frame.f);

    for (int i = 0; i < FF_ARRAY_ELEMS(ctx->ref_tab); i++) {
        if (ctx->ref_tab[i].frame.f->buf[0])
            ff_thread_release_buffer(avctx, &ctx->ref_tab[i].frame);
        av_frame_free(&ctx->ref_tab[i].frame.f);
    }

    return ff_vaapi_decode_uninit(avctx);
}


static int vaapi_av1_start_frame(AVCodecContext *avctx,
                                 av_unused const uint8_t *buffer,
                                 av_unused uint32_t size)
{
    AV1DecContext *s = avctx->priv_data;
    const AV1RawSequenceHeader *seq = s->raw_seq;
    const AV1RawFrameHeader *frame_header = s->raw_frame_header;
    const AV1RawFilmGrainParams *film_grain = &s->cur_frame.film_grain;
    VAAPIDecodePicture *pic = s->cur_frame.hwaccel_picture_private;
    VAAPIAV1DecContext *ctx = avctx->internal->hwaccel_priv_data;
    VADecPictureParameterBufferAV1 pic_param;
    int8_t bit_depth_idx;
    int err = 0;
    int apply_grain = !(avctx->export_side_data & AV_CODEC_EXPORT_DATA_FILM_GRAIN) && film_grain->apply_grain;
    uint8_t remap_lr_type[4] = {AV1_RESTORE_NONE, AV1_RESTORE_SWITCHABLE, AV1_RESTORE_WIENER, AV1_RESTORE_SGRPROJ};
    uint8_t segmentation_feature_signed[AV1_SEG_LVL_MAX] = {1, 1, 1, 1, 1, 0, 0, 0};
    uint8_t segmentation_feature_max[AV1_SEG_LVL_MAX] = {255, AV1_MAX_LOOP_FILTER,
        AV1_MAX_LOOP_FILTER, AV1_MAX_LOOP_FILTER, AV1_MAX_LOOP_FILTER, 7 , 0 , 0 };

    bit_depth_idx = vaapi_av1_get_bit_depth_idx(avctx);
    if (bit_depth_idx < 0)
        goto fail;

    if (apply_grain) {
        if (ctx->tmp_frame.f->buf[0])
            ff_thread_release_buffer(avctx, &ctx->tmp_frame);
        err = ff_thread_get_buffer(avctx, &ctx->tmp_frame, AV_GET_BUFFER_FLAG_REF);
        if (err < 0)
            goto fail;
        pic->output_surface = ff_vaapi_get_surface_id(ctx->tmp_frame.f);
    } else {
        pic->output_surface = vaapi_av1_surface_id(&s->cur_frame);
    }

    memset(&pic_param, 0, sizeof(VADecPictureParameterBufferAV1));
    pic_param = (VADecPictureParameterBufferAV1) {
        .profile                    = seq->seq_profile,
        .order_hint_bits_minus_1    = seq->order_hint_bits_minus_1,
        .bit_depth_idx              = bit_depth_idx,
        .matrix_coefficients        = seq->color_config.matrix_coefficients,
        .current_frame              = pic->output_surface,
        .current_display_picture    = vaapi_av1_surface_id(&s->cur_frame),
        .frame_width_minus1         = frame_header->frame_width_minus_1,
        .frame_height_minus1        = frame_header->frame_height_minus_1,
        .primary_ref_frame          = frame_header->primary_ref_frame,
        .order_hint                 = frame_header->order_hint,
        .tile_cols                  = frame_header->tile_cols,
        .tile_rows                  = frame_header->tile_rows,
        .context_update_tile_id     = frame_header->context_update_tile_id,
        .superres_scale_denominator = frame_header->use_superres ?
                                        frame_header->coded_denom + AV1_SUPERRES_DENOM_MIN :
                                        AV1_SUPERRES_NUM,
        .interp_filter              = frame_header->interpolation_filter,
        .filter_level[0]            = frame_header->loop_filter_level[0],
        .filter_level[1]            = frame_header->loop_filter_level[1],
        .filter_level_u             = frame_header->loop_filter_level[2],
        .filter_level_v             = frame_header->loop_filter_level[3],
        .base_qindex                = frame_header->base_q_idx,
        .y_dc_delta_q               = frame_header->delta_q_y_dc,
        .u_dc_delta_q               = frame_header->delta_q_u_dc,
        .u_ac_delta_q               = frame_header->delta_q_u_ac,
        .v_dc_delta_q               = frame_header->delta_q_v_dc,
        .v_ac_delta_q               = frame_header->delta_q_v_ac,
        .cdef_damping_minus_3       = frame_header->cdef_damping_minus_3,
        .cdef_bits                  = frame_header->cdef_bits,
        .seq_info_fields.fields = {
            .still_picture              = seq->still_picture,
            .use_128x128_superblock     = seq->use_128x128_superblock,
            .enable_filter_intra        = seq->enable_filter_intra,
            .enable_intra_edge_filter   = seq->enable_intra_edge_filter,
            .enable_interintra_compound = seq->enable_interintra_compound,
            .enable_masked_compound     = seq->enable_masked_compound,
            .enable_dual_filter         = seq->enable_dual_filter,
            .enable_order_hint          = seq->enable_order_hint,
            .enable_jnt_comp            = seq->enable_jnt_comp,
            .enable_cdef                = seq->enable_cdef,
            .mono_chrome                = seq->color_config.mono_chrome,
            .color_range                = seq->color_config.color_range,
            .subsampling_x              = seq->color_config.subsampling_x,
            .subsampling_y              = seq->color_config.subsampling_y,
            .chroma_sample_position     = seq->color_config.chroma_sample_position,
            .film_grain_params_present  = seq->film_grain_params_present &&
                                          !(avctx->export_side_data & AV_CODEC_EXPORT_DATA_FILM_GRAIN),
        },
        .seg_info.segment_info_fields.bits = {
            .enabled         = frame_header->segmentation_enabled,
            .update_map      = frame_header->segmentation_update_map,
            .temporal_update = frame_header->segmentation_temporal_update,
            .update_data     = frame_header->segmentation_update_data,
        },
        .film_grain_info = {
            .film_grain_info_fields.bits = {
                .apply_grain              = apply_grain,
                .chroma_scaling_from_luma = film_grain->chroma_scaling_from_luma,
                .grain_scaling_minus_8    = film_grain->grain_scaling_minus_8,
                .ar_coeff_lag             = film_grain->ar_coeff_lag,
                .ar_coeff_shift_minus_6   = film_grain->ar_coeff_shift_minus_6,
                .grain_scale_shift        = film_grain->grain_scale_shift,
                .overlap_flag             = film_grain->overlap_flag,
                .clip_to_restricted_range = film_grain->clip_to_restricted_range,
            },
            .grain_seed    = film_grain->grain_seed,
            .num_y_points  = film_grain->num_y_points,
            .num_cb_points = film_grain->num_cb_points,
            .num_cr_points = film_grain->num_cr_points,
            .cb_mult       = film_grain->cb_mult,
            .cb_luma_mult  = film_grain->cb_luma_mult,
            .cb_offset     = film_grain->cb_offset,
            .cr_mult       = film_grain->cr_mult,
            .cr_luma_mult  = film_grain->cr_luma_mult,
            .cr_offset     = film_grain->cr_offset,
        },
        .pic_info_fields.bits = {
            .frame_type                   = frame_header->frame_type,
            .show_frame                   = frame_header->show_frame,
            .showable_frame               = frame_header->showable_frame,
            .error_resilient_mode         = frame_header->error_resilient_mode,
            .disable_cdf_update           = frame_header->disable_cdf_update,
            .allow_screen_content_tools   = frame_header->allow_screen_content_tools,
            .force_integer_mv             = frame_header->force_integer_mv,
            .allow_intrabc                = frame_header->allow_intrabc,
            .use_superres                 = frame_header->use_superres,
            .allow_high_precision_mv      = frame_header->allow_high_precision_mv,
            .is_motion_mode_switchable    = frame_header->is_motion_mode_switchable,
            .use_ref_frame_mvs            = frame_header->use_ref_frame_mvs,
            .disable_frame_end_update_cdf = frame_header->disable_frame_end_update_cdf,
            .uniform_tile_spacing_flag    = frame_header->uniform_tile_spacing_flag,
            .allow_warped_motion          = frame_header->allow_warped_motion,
        },
        .loop_filter_info_fields.bits = {
            .sharpness_level        = frame_header->loop_filter_sharpness,
            .mode_ref_delta_enabled = frame_header->loop_filter_delta_enabled,
            .mode_ref_delta_update  = frame_header->loop_filter_delta_update,
        },
        .mode_control_fields.bits = {
            .delta_q_present_flag  = frame_header->delta_q_present,
            .log2_delta_q_res      = frame_header->delta_q_res,
            .delta_lf_present_flag = frame_header->delta_lf_present,
            .log2_delta_lf_res     = frame_header->delta_lf_res,
            .delta_lf_multi        = frame_header->delta_lf_multi,
            .tx_mode               = frame_header->tx_mode,
            .reference_select      = frame_header->reference_select,
            .reduced_tx_set_used   = frame_header->reduced_tx_set,
            .skip_mode_present     = frame_header->skip_mode_present,
        },
        .loop_restoration_fields.bits = {
            .yframe_restoration_type  = remap_lr_type[frame_header->lr_type[0]],
            .cbframe_restoration_type = remap_lr_type[frame_header->lr_type[1]],
            .crframe_restoration_type = remap_lr_type[frame_header->lr_type[2]],
            .lr_unit_shift            = frame_header->lr_unit_shift,
            .lr_uv_shift              = frame_header->lr_uv_shift,
        },
        .qmatrix_fields.bits = {
            .using_qmatrix = frame_header->using_qmatrix,
            .qm_y          = frame_header->qm_y,
            .qm_u          = frame_header->qm_u,
            .qm_v          = frame_header->qm_v,
        }
    };

    for (int i = 0; i < AV1_NUM_REF_FRAMES; i++) {
        if (pic_param.pic_info_fields.bits.frame_type == AV1_FRAME_KEY)
            pic_param.ref_frame_map[i] = VA_INVALID_ID;
        else
            pic_param.ref_frame_map[i] = ctx->ref_tab[i].valid ?
                                         ff_vaapi_get_surface_id(ctx->ref_tab[i].frame.f) :
                                         vaapi_av1_surface_id(&s->ref[i]);
    }
    for (int i = 0; i < AV1_REFS_PER_FRAME; i++) {
        pic_param.ref_frame_idx[i] = frame_header->ref_frame_idx[i];
    }
    for (int i = 0; i < AV1_TOTAL_REFS_PER_FRAME; i++) {
        pic_param.ref_deltas[i] = frame_header->loop_filter_ref_deltas[i];
    }
    for (int i = 0; i < 2; i++) {
        pic_param.mode_deltas[i] = frame_header->loop_filter_mode_deltas[i];
    }
    for (int i = 0; i < (1 << frame_header->cdef_bits); i++) {
        pic_param.cdef_y_strengths[i] =
            (frame_header->cdef_y_pri_strength[i] << 2) +
                frame_header->cdef_y_sec_strength[i];
        pic_param.cdef_uv_strengths[i] =
            (frame_header->cdef_uv_pri_strength[i] << 2) +
                frame_header->cdef_uv_sec_strength[i];
    }
    for (int i = 0; i < frame_header->tile_cols; i++) {
        pic_param.width_in_sbs_minus_1[i] =
            frame_header->width_in_sbs_minus_1[i];
    }
    for (int i = 0; i < frame_header->tile_rows; i++) {
        pic_param.height_in_sbs_minus_1[i] =
            frame_header->height_in_sbs_minus_1[i];
    }
    for (int i = AV1_REF_FRAME_LAST; i <= AV1_REF_FRAME_ALTREF; i++) {
        pic_param.wm[i - 1].invalid = s->cur_frame.gm_invalid[i];
        pic_param.wm[i - 1].wmtype  = s->cur_frame.gm_type[i];
        for (int j = 0; j < 6; j++)
            pic_param.wm[i - 1].wmmat[j] = s->cur_frame.gm_params[i][j];
    }
    for (int i = 0; i < AV1_MAX_SEGMENTS; i++) {
        for (int j = 0; j < AV1_SEG_LVL_MAX; j++) {
            pic_param.seg_info.feature_mask[i] |= (frame_header->feature_enabled[i][j] << j);
            if (segmentation_feature_signed[j])
                pic_param.seg_info.feature_data[i][j] = av_clip(frame_header->feature_value[i][j],
                    -segmentation_feature_max[j], segmentation_feature_max[j]);
            else
                pic_param.seg_info.feature_data[i][j] = av_clip(frame_header->feature_value[i][j],
                    0, segmentation_feature_max[j]);
        }
    }
    if (apply_grain) {
        for (int i = 0; i < film_grain->num_y_points; i++) {
            pic_param.film_grain_info.point_y_value[i] =
                film_grain->point_y_value[i];
            pic_param.film_grain_info.point_y_scaling[i] =
                film_grain->point_y_scaling[i];
        }
        for (int i = 0; i < film_grain->num_cb_points; i++) {
            pic_param.film_grain_info.point_cb_value[i] =
                film_grain->point_cb_value[i];
            pic_param.film_grain_info.point_cb_scaling[i] =
                film_grain->point_cb_scaling[i];
        }
        for (int i = 0; i < film_grain->num_cr_points; i++) {
            pic_param.film_grain_info.point_cr_value[i] =
                film_grain->point_cr_value[i];
            pic_param.film_grain_info.point_cr_scaling[i] =
                film_grain->point_cr_scaling[i];
        }
        for (int i = 0; i < 24; i++) {
            pic_param.film_grain_info.ar_coeffs_y[i] =
                film_grain->ar_coeffs_y_plus_128[i] - 128;
        }
        for (int i = 0; i < 25; i++) {
            pic_param.film_grain_info.ar_coeffs_cb[i] =
                film_grain->ar_coeffs_cb_plus_128[i] - 128;
            pic_param.film_grain_info.ar_coeffs_cr[i] =
                film_grain->ar_coeffs_cr_plus_128[i] - 128;
        }
    }
    err = ff_vaapi_decode_make_param_buffer(avctx, pic,
                                            VAPictureParameterBufferType,
                                            &pic_param, sizeof(pic_param));
    if (err < 0)
        goto fail;

    return 0;

fail:
    ff_vaapi_decode_cancel(avctx, pic);
    return err;
}

static int vaapi_av1_end_frame(AVCodecContext *avctx)
{
    const AV1DecContext *s = avctx->priv_data;
    const AV1RawFrameHeader *header = s->raw_frame_header;
    const AV1RawFilmGrainParams *film_grain = &s->cur_frame.film_grain;
    VAAPIDecodePicture *pic = s->cur_frame.hwaccel_picture_private;
    VAAPIAV1DecContext *ctx = avctx->internal->hwaccel_priv_data;

    int apply_grain = !(avctx->export_side_data & AV_CODEC_EXPORT_DATA_FILM_GRAIN) && film_grain->apply_grain;
    int ret;
    ret = ff_vaapi_decode_issue(avctx, pic);
    if (ret < 0)
        return ret;

    for (int i = 0; i < AV1_NUM_REF_FRAMES; i++) {
        if (header->refresh_frame_flags & (1 << i)) {
            if (ctx->ref_tab[i].frame.f->buf[0])
                ff_thread_release_buffer(avctx, &ctx->ref_tab[i].frame);

            if (apply_grain) {
                ret = ff_thread_ref_frame(&ctx->ref_tab[i].frame, &ctx->tmp_frame);
                if (ret < 0)
                    return ret;
                ctx->ref_tab[i].valid = 1;
            } else {
                ctx->ref_tab[i].valid = 0;
            }
        }
    }

    return 0;
}

static int vaapi_av1_decode_slice(AVCodecContext *avctx,
                                  const uint8_t *buffer,
                                  uint32_t size)
{
    const AV1DecContext *s = avctx->priv_data;
    VAAPIDecodePicture *pic = s->cur_frame.hwaccel_picture_private;
    VASliceParameterBufferAV1 slice_param;
    int err = 0;

    for (int i = s->tg_start; i <= s->tg_end; i++) {
        memset(&slice_param, 0, sizeof(VASliceParameterBufferAV1));

        slice_param = (VASliceParameterBufferAV1) {
            .slice_data_size   = s->tile_group_info[i].tile_size,
            .slice_data_offset = s->tile_group_info[i].tile_offset,
            .slice_data_flag   = VA_SLICE_DATA_FLAG_ALL,
            .tile_row          = s->tile_group_info[i].tile_row,
            .tile_column       = s->tile_group_info[i].tile_column,
            .tg_start          = s->tg_start,
            .tg_end            = s->tg_end,
        };

        err = ff_vaapi_decode_make_slice_buffer(avctx, pic, &slice_param,
                                                sizeof(VASliceParameterBufferAV1),
                                                buffer,
                                                size);
        if (err) {
            ff_vaapi_decode_cancel(avctx, pic);
            return err;
        }
    }

    return 0;
}

const AVHWAccel ff_av1_vaapi_hwaccel = {
    .name                 = "av1_vaapi",
    .type                 = AVMEDIA_TYPE_VIDEO,
    .id                   = AV_CODEC_ID_AV1,
    .pix_fmt              = AV_PIX_FMT_VAAPI,
    .start_frame          = vaapi_av1_start_frame,
    .end_frame            = vaapi_av1_end_frame,
    .decode_slice         = vaapi_av1_decode_slice,
    .frame_priv_data_size = sizeof(VAAPIDecodePicture),
    .init                 = vaapi_av1_decode_init,
    .uninit               = vaapi_av1_decode_uninit,
    .frame_params         = ff_vaapi_common_frame_params,
    .priv_data_size       = sizeof(VAAPIAV1DecContext),
    .caps_internal        = HWACCEL_CAP_ASYNC_SAFE,
};
