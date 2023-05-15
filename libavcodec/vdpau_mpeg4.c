/*
 * MPEG-4 Part 2 / H.263 decode acceleration through VDPAU
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
#include "hwconfig.h"
#include "mpeg4video.h"
#include "vdpau.h"
#include "vdpau_internal.h"

static int vdpau_mpeg4_start_frame(AVCodecContext *avctx,
                                   const uint8_t *buffer, uint32_t size)
{
    Mpeg4DecContext *ctx = avctx->priv_data;
    MpegEncContext * const s = &ctx->m;
    Picture *pic             = s->current_picture_ptr;
    struct vdpau_picture_context *pic_ctx = pic->hwaccel_picture_private;
    VdpPictureInfoMPEG4Part2 *info = &pic_ctx->info.mpeg4;
    VdpVideoSurface ref;
    int i;

    /* fill VdpPictureInfoMPEG4Part2 struct */
    info->forward_reference  = VDP_INVALID_HANDLE;
    info->backward_reference = VDP_INVALID_HANDLE;
    info->vop_coding_type    = 0;

    switch (s->pict_type) {
    case AV_PICTURE_TYPE_B:
        ref = ff_vdpau_get_surface_id(s->next_picture.f);
        assert(ref != VDP_INVALID_HANDLE);
        info->backward_reference = ref;
        info->vop_coding_type    = 2;
        /* fall-through */
    case AV_PICTURE_TYPE_P:
        ref = ff_vdpau_get_surface_id(s->last_picture.f);
        assert(ref != VDP_INVALID_HANDLE);
        info->forward_reference  = ref;
    }

    info->trd[0]                            = s->pp_time;
    info->trb[0]                            = s->pb_time;
    info->trd[1]                            = s->pp_field_time >> 1;
    info->trb[1]                            = s->pb_field_time >> 1;
    info->vop_time_increment_resolution     = s->avctx->framerate.num;
    info->vop_fcode_forward                 = s->f_code;
    info->vop_fcode_backward                = s->b_code;
    info->resync_marker_disable             = !ctx->resync_marker;
    info->interlaced                        = !s->progressive_sequence;
    info->quant_type                        = s->mpeg_quant;
    info->quarter_sample                    = s->quarter_sample;
    info->short_video_header                = avctx->codec->id == AV_CODEC_ID_H263;
    info->rounding_control                  = s->no_rounding;
    info->alternate_vertical_scan_flag      = s->alternate_scan;
    info->top_field_first                   = s->top_field_first;
    for (i = 0; i < 64; ++i) {
        int n = s->idsp.idct_permutation[i];
        info->intra_quantizer_matrix[i]     = s->intra_matrix[n];
        info->non_intra_quantizer_matrix[i] = s->inter_matrix[n];
    }

    ff_vdpau_common_start_frame(pic_ctx, buffer, size);
    return ff_vdpau_add_buffer(pic_ctx, buffer, size);
}

static int vdpau_mpeg4_decode_slice(av_unused AVCodecContext *avctx,
                                    av_unused const uint8_t *buffer,
                                    av_unused uint32_t size)
{
     return 0;
}

static int vdpau_mpeg4_init(AVCodecContext *avctx)
{
    VdpDecoderProfile profile;

    switch (avctx->profile) {
    case FF_PROFILE_MPEG4_SIMPLE:
        profile = VDP_DECODER_PROFILE_MPEG4_PART2_SP;
        break;
    // As any ASP decoder must be able to decode SP, this
    // should be a safe fallback if profile is unknown/unspecified.
    case FF_PROFILE_UNKNOWN:
    case FF_PROFILE_MPEG4_ADVANCED_SIMPLE:
        profile = VDP_DECODER_PROFILE_MPEG4_PART2_ASP;
        break;
    default:
        return AVERROR(ENOTSUP);
    }

    return ff_vdpau_common_init(avctx, profile, avctx->level);
}

const AVHWAccel ff_mpeg4_vdpau_hwaccel = {
    .name           = "mpeg4_vdpau",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_MPEG4,
    .pix_fmt        = AV_PIX_FMT_VDPAU,
    .start_frame    = vdpau_mpeg4_start_frame,
    .end_frame      = ff_vdpau_mpeg_end_frame,
    .decode_slice   = vdpau_mpeg4_decode_slice,
    .frame_priv_data_size = sizeof(struct vdpau_picture_context),
    .init           = vdpau_mpeg4_init,
    .uninit         = ff_vdpau_common_uninit,
    .frame_params   = ff_vdpau_common_frame_params,
    .priv_data_size = sizeof(VDPAUContext),
    .caps_internal  = HWACCEL_CAP_ASYNC_SAFE,
};
