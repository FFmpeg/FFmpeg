/*
 * MPEG-4 / H.263 HW decode acceleration through VA API
 *
 * Copyright (C) 2008-2009 Splitted-Desktop Systems
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

#include "h263.h"
#include "hwaccel.h"
#include "internal.h"
#include "mpeg4video.h"
#include "mpegvideo.h"
#include "vaapi_decode.h"

/** Reconstruct bitstream intra_dc_vlc_thr */
static int mpeg4_get_intra_dc_vlc_thr(Mpeg4DecContext *s)
{
    switch (s->intra_dc_threshold) {
    case 99: return 0;
    case 13: return 1;
    case 15: return 2;
    case 17: return 3;
    case 19: return 4;
    case 21: return 5;
    case 23: return 6;
    case 0:  return 7;
    }
    return 0;
}

static int vaapi_mpeg4_start_frame(AVCodecContext *avctx, av_unused const uint8_t *buffer, av_unused uint32_t size)
{
    Mpeg4DecContext *ctx = avctx->priv_data;
    MpegEncContext *s = &ctx->m;
    VAAPIDecodePicture *pic = s->current_picture_ptr->hwaccel_picture_private;
    VAPictureParameterBufferMPEG4 pic_param;
    int i, err;

    pic->output_surface = ff_vaapi_get_surface_id(s->current_picture_ptr->f);

    pic_param = (VAPictureParameterBufferMPEG4) {
        .vop_width                        = s->width,
        .vop_height                       = s->height,
        .forward_reference_picture        = VA_INVALID_ID,
        .backward_reference_picture       = VA_INVALID_ID,
        .vol_fields.bits = {
            .short_video_header           = avctx->codec->id == AV_CODEC_ID_H263,
            .chroma_format                = CHROMA_420,
            .interlaced                   = !s->progressive_sequence,
            .obmc_disable                 = 1,
            .sprite_enable                = ctx->vol_sprite_usage,
            .sprite_warping_accuracy      = s->sprite_warping_accuracy,
            .quant_type                   = s->mpeg_quant,
            .quarter_sample               = s->quarter_sample,
            .data_partitioned             = s->data_partitioning,
            .reversible_vlc               = ctx->rvlc,
            .resync_marker_disable        = !ctx->resync_marker,
        },
        .no_of_sprite_warping_points      = ctx->num_sprite_warping_points,
        .quant_precision                  = s->quant_precision,
        .vop_fields.bits = {
            .vop_coding_type              = s->pict_type - AV_PICTURE_TYPE_I,
            .backward_reference_vop_coding_type =
                s->pict_type == AV_PICTURE_TYPE_B ? s->next_picture.f->pict_type - AV_PICTURE_TYPE_I : 0,
            .vop_rounding_type            = s->no_rounding,
            .intra_dc_vlc_thr             = mpeg4_get_intra_dc_vlc_thr(ctx),
            .top_field_first              = s->top_field_first,
            .alternate_vertical_scan_flag = s->alternate_scan,
        },
        .vop_fcode_forward                = s->f_code,
        .vop_fcode_backward               = s->b_code,
        .vop_time_increment_resolution    = avctx->framerate.num,
        .num_macroblocks_in_gob           = s->mb_width * H263_GOB_HEIGHT(s->height),
        .num_gobs_in_vop                  =
            (s->mb_width * s->mb_height) / (s->mb_width * H263_GOB_HEIGHT(s->height)),
        .TRB                              = s->pb_time,
        .TRD                              = s->pp_time,
    };

    for (i = 0; i < ctx->num_sprite_warping_points && i < 3; i++) {
        pic_param.sprite_trajectory_du[i]              = ctx->sprite_traj[i][0];
        pic_param.sprite_trajectory_dv[i]              = ctx->sprite_traj[i][1];
    }

    if (s->pict_type == AV_PICTURE_TYPE_B)
        pic_param.backward_reference_picture = ff_vaapi_get_surface_id(s->next_picture.f);
    if (s->pict_type != AV_PICTURE_TYPE_I)
        pic_param.forward_reference_picture  = ff_vaapi_get_surface_id(s->last_picture.f);

    err = ff_vaapi_decode_make_param_buffer(avctx, pic,
                                            VAPictureParameterBufferType,
                                            &pic_param, sizeof(pic_param));
    if (err < 0)
        goto fail;

    /* Only the first inverse quantisation method uses the weighting matrices */
    if (pic_param.vol_fields.bits.quant_type) {
        VAIQMatrixBufferMPEG4 iq_matrix;

        iq_matrix.load_intra_quant_mat     = 1;
        iq_matrix.load_non_intra_quant_mat = 1;

        for (i = 0; i < 64; i++) {
            int n = s->idsp.idct_permutation[ff_zigzag_direct[i]];
            iq_matrix.intra_quant_mat[i]     = s->intra_matrix[n];
            iq_matrix.non_intra_quant_mat[i] = s->inter_matrix[n];
        }

        err = ff_vaapi_decode_make_param_buffer(avctx, pic,
                                                VAIQMatrixBufferType,
                                                &iq_matrix, sizeof(iq_matrix));
        if (err < 0)
            goto fail;
    }
    return 0;

fail:
    ff_vaapi_decode_cancel(avctx, pic);
    return err;
}

static int vaapi_mpeg4_end_frame(AVCodecContext *avctx)
{
    MpegEncContext *s = avctx->priv_data;
    VAAPIDecodePicture *pic = s->current_picture_ptr->hwaccel_picture_private;
    int ret;

    ret = ff_vaapi_decode_issue(avctx, pic);
    if (ret < 0)
        goto fail;

    ff_mpeg_draw_horiz_band(s, 0, s->avctx->height);

fail:
    return ret;
}

static int vaapi_mpeg4_decode_slice(AVCodecContext *avctx, const uint8_t *buffer, uint32_t size)
{
    MpegEncContext *s = avctx->priv_data;
    VAAPIDecodePicture *pic = s->current_picture_ptr->hwaccel_picture_private;
    VASliceParameterBufferMPEG4 slice_param;
    int err;

    slice_param = (VASliceParameterBufferMPEG4) {
        .slice_data_size   = size,
        .slice_data_offset = 0,
        .slice_data_flag   = VA_SLICE_DATA_FLAG_ALL,
        .macroblock_offset = get_bits_count(&s->gb) % 8,
        .macroblock_number = 0,
        .quant_scale       = s->qscale,
    };

    err = ff_vaapi_decode_make_slice_buffer(avctx, pic,
                                            &slice_param, sizeof(slice_param),
                                            buffer, size);
    if (err < 0) {
        ff_vaapi_decode_cancel(avctx, pic);
        return err;
    }

    return 0;
}

#if CONFIG_MPEG4_VAAPI_HWACCEL
AVHWAccel ff_mpeg4_vaapi_hwaccel = {
    .name                 = "mpeg4_vaapi",
    .type                 = AVMEDIA_TYPE_VIDEO,
    .id                   = AV_CODEC_ID_MPEG4,
    .pix_fmt              = AV_PIX_FMT_VAAPI,
    .start_frame          = &vaapi_mpeg4_start_frame,
    .end_frame            = &vaapi_mpeg4_end_frame,
    .decode_slice         = &vaapi_mpeg4_decode_slice,
    .frame_priv_data_size = sizeof(VAAPIDecodePicture),
    .init                 = &ff_vaapi_decode_init,
    .uninit               = &ff_vaapi_decode_uninit,
    .priv_data_size       = sizeof(VAAPIDecodeContext),
    .caps_internal        = HWACCEL_CAP_ASYNC_SAFE,
};
#endif

#if CONFIG_H263_VAAPI_HWACCEL
AVHWAccel ff_h263_vaapi_hwaccel = {
    .name                 = "h263_vaapi",
    .type                 = AVMEDIA_TYPE_VIDEO,
    .id                   = AV_CODEC_ID_H263,
    .pix_fmt              = AV_PIX_FMT_VAAPI,
    .start_frame          = &vaapi_mpeg4_start_frame,
    .end_frame            = &vaapi_mpeg4_end_frame,
    .decode_slice         = &vaapi_mpeg4_decode_slice,
    .frame_priv_data_size = sizeof(VAAPIDecodePicture),
    .init                 = &ff_vaapi_decode_init,
    .uninit               = &ff_vaapi_decode_uninit,
    .priv_data_size       = sizeof(VAAPIDecodeContext),
    .caps_internal        = HWACCEL_CAP_ASYNC_SAFE,
};
#endif
