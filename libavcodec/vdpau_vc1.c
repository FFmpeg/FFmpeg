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

#include <vdpau/vdpau.h>

#include "avcodec.h"
#include "vc1.h"
#include "vdpau.h"
#include "vdpau_internal.h"

static int vdpau_vc1_start_frame(AVCodecContext *avctx,
                                 const uint8_t *buffer, uint32_t size)
{
    VC1Context * const v  = avctx->priv_data;
    AVVDPAUContext *hwctx = avctx->hwaccel_context;
    MpegEncContext * const s = &v->s;
    VdpPictureInfoVC1 *info = &hwctx->info.vc1;
    VdpVideoSurface ref;

    /*  fill LvPictureInfoVC1 struct */
    info->forward_reference  = VDP_INVALID_HANDLE;
    info->backward_reference = VDP_INVALID_HANDLE;

    switch (s->pict_type) {
    case AV_PICTURE_TYPE_B:
        ref = ff_vdpau_get_surface_id(&s->next_picture);
        assert(ref != VDP_INVALID_HANDLE);
        info->backward_reference = ref;
        /* fall-through */
    case AV_PICTURE_TYPE_P:
        ref = ff_vdpau_get_surface_id(&s->last_picture);
        assert(ref != VDP_INVALID_HANDLE);
        info->forward_reference  = ref;
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
    info->syncmarker        = v->s.resync_marker;
    info->rangered          = v->rangered | (v->rangeredfrm << 1);
    info->maxbframes        = v->s.max_b_frames;
    info->deblockEnable     = v->postprocflag & 1;
    info->pquant            = v->pq;

    return ff_vdpau_common_start_frame(avctx, buffer, size);
}

static int vdpau_vc1_decode_slice(AVCodecContext *avctx,
                                  const uint8_t *buffer, uint32_t size)
{
    AVVDPAUContext *hwctx = avctx->hwaccel_context;
    int val;

    val = ff_vdpau_add_buffer(avctx, buffer, size);
    if (val < 0)
        return val;

    hwctx->info.vc1.slice_count++;
    return 0;
}

#if CONFIG_WMV3_VDPAU_HWACCEL
AVHWAccel ff_wmv3_vdpau_hwaccel = {
    .name           = "wm3_vdpau",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_WMV3,
    .pix_fmt        = AV_PIX_FMT_VDPAU,
    .start_frame    = vdpau_vc1_start_frame,
    .end_frame      = ff_vdpau_mpeg_end_frame,
    .decode_slice   = vdpau_vc1_decode_slice,
};
#endif

AVHWAccel ff_vc1_vdpau_hwaccel = {
    .name           = "vc1_vdpau",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_VC1,
    .pix_fmt        = AV_PIX_FMT_VDPAU,
    .start_frame    = vdpau_vc1_start_frame,
    .end_frame      = ff_vdpau_mpeg_end_frame,
    .decode_slice   = vdpau_vc1_decode_slice,
};
