/*
 * MPEG-1/2 HW decode acceleration through VDPAU
 *
 * Copyright (c) 2008 NVIDIA
 * Copyright (c) 2013 Rémi Denis-Courmont
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

#include "avcodec.h"
#include "hwaccel.h"
#include "mpegvideo.h"
#include "vdpau.h"
#include "vdpau_internal.h"

static int vdpau_mpeg_start_frame(AVCodecContext *avctx,
                                  const uint8_t *buffer, uint32_t size)
{
    MpegEncContext * const s = avctx->priv_data;
    Picture *pic             = s->current_picture_ptr;
    struct vdpau_picture_context *pic_ctx = pic->hwaccel_picture_private;
    VdpPictureInfoMPEG1Or2 *info = &pic_ctx->info.mpeg;
    VdpVideoSurface ref;
    int i;

    /* fill VdpPictureInfoMPEG1Or2 struct */
    info->forward_reference  = VDP_INVALID_HANDLE;
    info->backward_reference = VDP_INVALID_HANDLE;

    switch (s->pict_type) {
    case AV_PICTURE_TYPE_B:
        ref = ff_vdpau_get_surface_id(s->next_picture.f);
        assert(ref != VDP_INVALID_HANDLE);
        info->backward_reference = ref;
        /* fall through to forward prediction */
    case AV_PICTURE_TYPE_P:
        ref = ff_vdpau_get_surface_id(s->last_picture.f);
        info->forward_reference  = ref;
    }

    info->slice_count                = 0;
    info->picture_structure          = s->picture_structure;
    info->picture_coding_type        = s->pict_type;
    info->intra_dc_precision         = s->intra_dc_precision;
    info->frame_pred_frame_dct       = s->frame_pred_frame_dct;
    info->concealment_motion_vectors = s->concealment_motion_vectors;
    info->intra_vlc_format           = s->intra_vlc_format;
    info->alternate_scan             = s->alternate_scan;
    info->q_scale_type               = s->q_scale_type;
    info->top_field_first            = s->top_field_first;
    // Both for MPEG-1 only, zero for MPEG-2:
    info->full_pel_forward_vector    = s->full_pel[0];
    info->full_pel_backward_vector   = s->full_pel[1];
    // For MPEG-1 fill both horizontal & vertical:
    info->f_code[0][0]               = s->mpeg_f_code[0][0];
    info->f_code[0][1]               = s->mpeg_f_code[0][1];
    info->f_code[1][0]               = s->mpeg_f_code[1][0];
    info->f_code[1][1]               = s->mpeg_f_code[1][1];
    for (i = 0; i < 64; ++i) {
        info->intra_quantizer_matrix[i]     = s->intra_matrix[i];
        info->non_intra_quantizer_matrix[i] = s->inter_matrix[i];
    }

    return ff_vdpau_common_start_frame(pic_ctx, buffer, size);
}

static int vdpau_mpeg_decode_slice(AVCodecContext *avctx,
                                   const uint8_t *buffer, uint32_t size)
{
    MpegEncContext * const s = avctx->priv_data;
    Picture *pic             = s->current_picture_ptr;
    struct vdpau_picture_context *pic_ctx = pic->hwaccel_picture_private;
    int val;

    val = ff_vdpau_add_buffer(pic_ctx, buffer, size);
    if (val < 0)
        return val;

    pic_ctx->info.mpeg.slice_count++;
    return 0;
}

#if CONFIG_MPEG1_VDPAU_HWACCEL
static int vdpau_mpeg1_init(AVCodecContext *avctx)
{
    return ff_vdpau_common_init(avctx, VDP_DECODER_PROFILE_MPEG1,
                                VDP_DECODER_LEVEL_MPEG1_NA);
}

const AVHWAccel ff_mpeg1_vdpau_hwaccel = {
    .name           = "mpeg1_vdpau",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_MPEG1VIDEO,
    .pix_fmt        = AV_PIX_FMT_VDPAU,
    .start_frame    = vdpau_mpeg_start_frame,
    .end_frame      = ff_vdpau_mpeg_end_frame,
    .decode_slice   = vdpau_mpeg_decode_slice,
    .frame_priv_data_size = sizeof(struct vdpau_picture_context),
    .init           = vdpau_mpeg1_init,
    .uninit         = ff_vdpau_common_uninit,
    .priv_data_size = sizeof(VDPAUContext),
    .caps_internal  = HWACCEL_CAP_ASYNC_SAFE,
};
#endif

#if CONFIG_MPEG2_VDPAU_HWACCEL
static int vdpau_mpeg2_init(AVCodecContext *avctx)
{
    VdpDecoderProfile profile;

    switch (avctx->profile) {
    case FF_PROFILE_MPEG2_MAIN:
        profile = VDP_DECODER_PROFILE_MPEG2_MAIN;
        break;
    case FF_PROFILE_MPEG2_SIMPLE:
        profile = VDP_DECODER_PROFILE_MPEG2_SIMPLE;
        break;
    default:
        return AVERROR(EINVAL);
    }

    return ff_vdpau_common_init(avctx, profile, VDP_DECODER_LEVEL_MPEG2_HL);
}

const AVHWAccel ff_mpeg2_vdpau_hwaccel = {
    .name           = "mpeg2_vdpau",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_MPEG2VIDEO,
    .pix_fmt        = AV_PIX_FMT_VDPAU,
    .start_frame    = vdpau_mpeg_start_frame,
    .end_frame      = ff_vdpau_mpeg_end_frame,
    .decode_slice   = vdpau_mpeg_decode_slice,
    .frame_priv_data_size = sizeof(struct vdpau_picture_context),
    .init           = vdpau_mpeg2_init,
    .uninit         = ff_vdpau_common_uninit,
    .frame_params   = ff_vdpau_common_frame_params,
    .priv_data_size = sizeof(VDPAUContext),
    .caps_internal  = HWACCEL_CAP_ASYNC_SAFE,
};
#endif
