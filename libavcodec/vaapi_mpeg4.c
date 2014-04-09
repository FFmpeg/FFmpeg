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

#include "vaapi_internal.h"
#include "h263.h"
#include "mpeg4video.h"

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
    MpegEncContext * const s = &ctx->m;
    struct vaapi_context * const vactx = avctx->hwaccel_context;
    VAPictureParameterBufferMPEG4 *pic_param;
    VAIQMatrixBufferMPEG4 *iq_matrix;
    int i;

    av_dlog(avctx, "vaapi_mpeg4_start_frame()\n");

    vactx->slice_param_size = sizeof(VASliceParameterBufferMPEG4);

    /* Fill in VAPictureParameterBufferMPEG4 */
    pic_param = ff_vaapi_alloc_pic_param(vactx, sizeof(VAPictureParameterBufferMPEG4));
    if (!pic_param)
        return -1;
    pic_param->vop_width                                = s->width;
    pic_param->vop_height                               = s->height;
    pic_param->forward_reference_picture                = VA_INVALID_ID;
    pic_param->backward_reference_picture               = VA_INVALID_ID;
    pic_param->vol_fields.value                         = 0; /* reset all bits */
    pic_param->vol_fields.bits.short_video_header       = avctx->codec->id == AV_CODEC_ID_H263;
    pic_param->vol_fields.bits.chroma_format            = CHROMA_420;
    pic_param->vol_fields.bits.interlaced               = !s->progressive_sequence;
    pic_param->vol_fields.bits.obmc_disable             = 1;
    pic_param->vol_fields.bits.sprite_enable            = ctx->vol_sprite_usage;
    pic_param->vol_fields.bits.sprite_warping_accuracy  = s->sprite_warping_accuracy;
    pic_param->vol_fields.bits.quant_type               = s->mpeg_quant;
    pic_param->vol_fields.bits.quarter_sample           = s->quarter_sample;
    pic_param->vol_fields.bits.data_partitioned         = s->data_partitioning;
    pic_param->vol_fields.bits.reversible_vlc           = ctx->rvlc;
    pic_param->vol_fields.bits.resync_marker_disable    = !ctx->resync_marker;
    pic_param->no_of_sprite_warping_points              = ctx->num_sprite_warping_points;
    for (i = 0; i < ctx->num_sprite_warping_points && i < 3; i++) {
        pic_param->sprite_trajectory_du[i]              = ctx->sprite_traj[i][0];
        pic_param->sprite_trajectory_dv[i]              = ctx->sprite_traj[i][1];
    }
    pic_param->quant_precision                          = s->quant_precision;
    pic_param->vop_fields.value                         = 0; /* reset all bits */
    pic_param->vop_fields.bits.vop_coding_type          = s->pict_type - AV_PICTURE_TYPE_I;
    pic_param->vop_fields.bits.backward_reference_vop_coding_type = s->pict_type == AV_PICTURE_TYPE_B ? s->next_picture.f->pict_type - AV_PICTURE_TYPE_I : 0;
    pic_param->vop_fields.bits.vop_rounding_type        = s->no_rounding;
    pic_param->vop_fields.bits.intra_dc_vlc_thr         = mpeg4_get_intra_dc_vlc_thr(ctx);
    pic_param->vop_fields.bits.top_field_first          = s->top_field_first;
    pic_param->vop_fields.bits.alternate_vertical_scan_flag = s->alternate_scan;
    pic_param->vop_fcode_forward                        = s->f_code;
    pic_param->vop_fcode_backward                       = s->b_code;
    pic_param->vop_time_increment_resolution            = avctx->time_base.den;
    pic_param->num_macroblocks_in_gob                   = s->mb_width * ff_h263_get_gob_height(s);
    pic_param->num_gobs_in_vop                          = (s->mb_width * s->mb_height) / pic_param->num_macroblocks_in_gob;
    pic_param->TRB                                      = s->pb_time;
    pic_param->TRD                                      = s->pp_time;

    if (s->pict_type == AV_PICTURE_TYPE_B)
        pic_param->backward_reference_picture = ff_vaapi_get_surface_id(s->next_picture.f);
    if (s->pict_type != AV_PICTURE_TYPE_I)
        pic_param->forward_reference_picture  = ff_vaapi_get_surface_id(s->last_picture.f);

    /* Fill in VAIQMatrixBufferMPEG4 */
    /* Only the first inverse quantisation method uses the weighting matrices */
    if (pic_param->vol_fields.bits.quant_type) {
        iq_matrix = ff_vaapi_alloc_iq_matrix(vactx, sizeof(VAIQMatrixBufferMPEG4));
        if (!iq_matrix)
            return -1;
        iq_matrix->load_intra_quant_mat         = 1;
        iq_matrix->load_non_intra_quant_mat     = 1;

        for (i = 0; i < 64; i++) {
            int n = s->dsp.idct_permutation[ff_zigzag_direct[i]];
            iq_matrix->intra_quant_mat[i]       = s->intra_matrix[n];
            iq_matrix->non_intra_quant_mat[i]   = s->inter_matrix[n];
        }
    }
    return 0;
}

static int vaapi_mpeg4_decode_slice(AVCodecContext *avctx, const uint8_t *buffer, uint32_t size)
{
    MpegEncContext * const s = avctx->priv_data;
    VASliceParameterBufferMPEG4 *slice_param;

    av_dlog(avctx, "vaapi_mpeg4_decode_slice(): buffer %p, size %d\n", buffer, size);

    /* Fill in VASliceParameterBufferMPEG4 */
    slice_param = (VASliceParameterBufferMPEG4 *)ff_vaapi_alloc_slice(avctx->hwaccel_context, buffer, size);
    if (!slice_param)
        return -1;
    slice_param->macroblock_offset      = get_bits_count(&s->gb) % 8;
    slice_param->macroblock_number      = 0;
    slice_param->quant_scale            = s->qscale;

    return 0;
}

#if CONFIG_MPEG4_VAAPI_HWACCEL
AVHWAccel ff_mpeg4_vaapi_hwaccel = {
    .name           = "mpeg4_vaapi",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_MPEG4,
    .pix_fmt        = AV_PIX_FMT_VAAPI_VLD,
    .start_frame    = vaapi_mpeg4_start_frame,
    .end_frame      = ff_vaapi_mpeg_end_frame,
    .decode_slice   = vaapi_mpeg4_decode_slice,
};
#endif

#if CONFIG_H263_VAAPI_HWACCEL
AVHWAccel ff_h263_vaapi_hwaccel = {
    .name           = "h263_vaapi",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_H263,
    .pix_fmt        = AV_PIX_FMT_VAAPI_VLD,
    .start_frame    = vaapi_mpeg4_start_frame,
    .end_frame      = ff_vaapi_mpeg_end_frame,
    .decode_slice   = vaapi_mpeg4_decode_slice,
};
#endif
