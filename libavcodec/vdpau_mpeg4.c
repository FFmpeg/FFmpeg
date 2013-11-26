/*
 * MPEG-4 Part 2 / H.263 decode acceleration through VDPAU
 *
 * Copyright (c) 2008 NVIDIA
 * Copyright (c) 2013 Rémi Denis-Courmont
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <vdpau/vdpau.h>

#include "avcodec.h"
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
        ref = ff_vdpau_get_surface_id(&s->next_picture);
        assert(ref != VDP_INVALID_HANDLE);
        info->backward_reference = ref;
        info->vop_coding_type    = 2;
        /* fall-through */
    case AV_PICTURE_TYPE_P:
        ref = ff_vdpau_get_surface_id(&s->last_picture);
        assert(ref != VDP_INVALID_HANDLE);
        info->forward_reference  = ref;
    }

    info->trd[0]                            = s->pp_time;
    info->trb[0]                            = s->pb_time;
    info->trd[1]                            = s->pp_field_time >> 1;
    info->trb[1]                            = s->pb_field_time >> 1;
    info->vop_time_increment_resolution     = s->avctx->time_base.den;
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
        info->intra_quantizer_matrix[i]     = s->intra_matrix[i];
        info->non_intra_quantizer_matrix[i] = s->inter_matrix[i];
    }

    ff_vdpau_common_start_frame(pic, buffer, size);
    return ff_vdpau_add_buffer(pic, buffer, size);
}

static int vdpau_mpeg4_decode_slice(av_unused AVCodecContext *avctx,
                                    av_unused const uint8_t *buffer,
                                    av_unused uint32_t size)
{
     return 0;
}

#if CONFIG_H263_VDPAU_HWACCEL
AVHWAccel ff_h263_vdpau_hwaccel = {
    .name           = "h263_vdpau",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_H263,
    .pix_fmt        = AV_PIX_FMT_VDPAU,
    .start_frame    = vdpau_mpeg4_start_frame,
    .end_frame      = ff_vdpau_mpeg_end_frame,
    .decode_slice   = vdpau_mpeg4_decode_slice,
    .priv_data_size = sizeof(struct vdpau_picture_context),
};
#endif

#if CONFIG_MPEG4_VDPAU_HWACCEL
AVHWAccel ff_mpeg4_vdpau_hwaccel = {
    .name           = "mpeg4_vdpau",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_MPEG4,
    .pix_fmt        = AV_PIX_FMT_VDPAU,
    .start_frame    = vdpau_mpeg4_start_frame,
    .end_frame      = ff_vdpau_mpeg_end_frame,
    .decode_slice   = vdpau_mpeg4_decode_slice,
    .priv_data_size = sizeof(struct vdpau_picture_context),
};
#endif
