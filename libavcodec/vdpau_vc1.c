/*
 * VC-1 decode acceleration through VDPAU
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

#include "config_components.h"

#include <vdpau/vdpau.h>

#include "avcodec.h"
#include "hwaccel_internal.h"
#include "vc1.h"
#include "vdpau.h"
#include "vdpau_internal.h"

static int vdpau_vc1_start_frame(AVCodecContext *avctx,
                                 const AVBufferRef *buffer_ref,
                                 const uint8_t *buffer, uint32_t size)
{
    VC1Context * const v  = avctx->priv_data;
    MpegEncContext * const s = &v->s;
    MPVPicture *pic          = s->cur_pic.ptr;
    struct vdpau_picture_context *pic_ctx = pic->hwaccel_picture_private;
    VdpPictureInfoVC1 *info = &pic_ctx->info.vc1;
    VdpVideoSurface ref;

    /*  fill LvPictureInfoVC1 struct */
    info->forward_reference  = VDP_INVALID_HANDLE;
    info->backward_reference = VDP_INVALID_HANDLE;

    switch (s->pict_type) {
    case AV_PICTURE_TYPE_B:
        if (s->next_pic.ptr) {
            ref = ff_vdpau_get_surface_id(s->next_pic.ptr->f);
            assert(ref != VDP_INVALID_HANDLE);
            info->backward_reference = ref;
        }
        /* fall-through */
    case AV_PICTURE_TYPE_P:
        if (s->last_pic.ptr) {
            ref = ff_vdpau_get_surface_id(s->last_pic.ptr->f);
            assert(ref != VDP_INVALID_HANDLE);
            info->forward_reference  = ref;
        }
    }

    info->slice_count       = 0;
    if (v->bi_type)
        info->picture_type  = 4;
    else
        info->picture_type  = s->pict_type - 1 + s->pict_type / 3;

    info->frame_coding_mode = v->fcm ? (v->fcm + 1) : 0;
    info->postprocflag      = v->postprocflag;
    info->pulldown          = v->broadcast;
    info->interlace         = v->interlace;
    info->tfcntrflag        = v->tfcntrflag;
    info->finterpflag       = v->finterpflag;
    info->psf               = v->psf;
    info->dquant            = v->dquant;
    info->panscan_flag      = v->panscanflag;
    info->refdist_flag      = v->refdist_flag;
    info->quantizer         = v->quantizer_mode;
    info->extended_mv       = v->extended_mv;
    info->extended_dmv      = v->extended_dmv;
    info->overlap           = v->overlap;
    info->vstransform       = v->vstransform;
    info->loopfilter        = v->s.loop_filter;
    info->fastuvmc          = v->fastuvmc;
    info->range_mapy_flag   = v->range_mapy_flag;
    info->range_mapy        = v->range_mapy;
    info->range_mapuv_flag  = v->range_mapuv_flag;
    info->range_mapuv       = v->range_mapuv;
    /* Specific to simple/main profile only */
    info->multires          = v->multires;
    info->syncmarker        = v->resync_marker;
    info->rangered          = v->rangered | (v->rangeredfrm << 1);
    info->maxbframes        = v->max_b_frames;
    info->deblockEnable     = v->postprocflag & 1;
    info->pquant            = v->pq;

    return ff_vdpau_common_start_frame(pic_ctx, buffer, size);
}

static int vdpau_vc1_decode_slice(AVCodecContext *avctx,
                                  const uint8_t *buffer, uint32_t size)
{
    VC1Context * const v  = avctx->priv_data;
    MpegEncContext * const s = &v->s;
    MPVPicture *pic          = s->cur_pic.ptr;
    struct vdpau_picture_context *pic_ctx = pic->hwaccel_picture_private;
    int val;

    val = ff_vdpau_add_buffer(pic_ctx, buffer, size);
    if (val < 0)
        return val;

    pic_ctx->info.vc1.slice_count++;
    return 0;
}

static av_cold int vdpau_vc1_init(AVCodecContext *avctx)
{
    VdpDecoderProfile profile;

    switch (avctx->profile) {
    case AV_PROFILE_VC1_SIMPLE:
        profile = VDP_DECODER_PROFILE_VC1_SIMPLE;
        break;
    case AV_PROFILE_VC1_MAIN:
        profile = VDP_DECODER_PROFILE_VC1_MAIN;
        break;
    case AV_PROFILE_VC1_ADVANCED:
        profile = VDP_DECODER_PROFILE_VC1_ADVANCED;
        break;
    default:
        return AVERROR(ENOTSUP);
    }

    return ff_vdpau_common_init(avctx, profile, avctx->level);
}

#if CONFIG_WMV3_VDPAU_HWACCEL
const FFHWAccel ff_wmv3_vdpau_hwaccel = {
    .p.name         = "wm3_vdpau",
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_WMV3,
    .p.pix_fmt      = AV_PIX_FMT_VDPAU,
    .start_frame    = vdpau_vc1_start_frame,
    .end_frame      = ff_vdpau_mpeg_end_frame,
    .decode_slice   = vdpau_vc1_decode_slice,
    .frame_priv_data_size = sizeof(struct vdpau_picture_context),
    .init           = vdpau_vc1_init,
    .uninit         = ff_vdpau_common_uninit,
    .frame_params   = ff_vdpau_common_frame_params,
    .priv_data_size = sizeof(VDPAUContext),
    .caps_internal  = HWACCEL_CAP_ASYNC_SAFE,
};
#endif

const FFHWAccel ff_vc1_vdpau_hwaccel = {
    .p.name         = "vc1_vdpau",
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_VC1,
    .p.pix_fmt      = AV_PIX_FMT_VDPAU,
    .start_frame    = vdpau_vc1_start_frame,
    .end_frame      = ff_vdpau_mpeg_end_frame,
    .decode_slice   = vdpau_vc1_decode_slice,
    .frame_priv_data_size = sizeof(struct vdpau_picture_context),
    .init           = vdpau_vc1_init,
    .uninit         = ff_vdpau_common_uninit,
    .frame_params   = ff_vdpau_common_frame_params,
    .priv_data_size = sizeof(VDPAUContext),
    .caps_internal  = HWACCEL_CAP_ASYNC_SAFE,
};
