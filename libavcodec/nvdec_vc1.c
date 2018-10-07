/*
 * VC1 HW decode acceleration through NVDEC
 *
 * Copyright (c) 2017 Philip Langdale
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

#include "avcodec.h"
#include "nvdec.h"
#include "decode.h"
#include "vc1.h"

static int nvdec_vc1_start_frame(AVCodecContext *avctx, const uint8_t *buffer, uint32_t size)
{
    VC1Context *v = avctx->priv_data;
    MpegEncContext *s = &v->s;

    NVDECContext      *ctx = avctx->internal->hwaccel_priv_data;
    CUVIDPICPARAMS     *pp = &ctx->pic_params;
    FrameDecodeData *fdd;
    NVDECFrame *cf;
    AVFrame *cur_frame = s->current_picture.f;

    int ret;

    ret = ff_nvdec_start_frame(avctx, cur_frame);
    if (ret < 0)
        return ret;

    fdd = (FrameDecodeData*)cur_frame->private_ref->data;
    cf  = (NVDECFrame*)fdd->hwaccel_priv;

    *pp = (CUVIDPICPARAMS) {
        .PicWidthInMbs     = (cur_frame->width  + 15) / 16,
        .FrameHeightInMbs  = (cur_frame->height + 15) / 16,
        .CurrPicIdx        = cf->idx,
        .field_pic_flag    = v->field_mode,
        .bottom_field_flag = v->cur_field_type,
        .second_field      = v->second_field,

        .intra_pic_flag    = s->pict_type == AV_PICTURE_TYPE_I ||
                             s->pict_type == AV_PICTURE_TYPE_BI,
        .ref_pic_flag      = s->pict_type == AV_PICTURE_TYPE_I ||
                             s->pict_type == AV_PICTURE_TYPE_P,

        .CodecSpecific.vc1 = {
            .ForwardRefIdx     = ff_nvdec_get_ref_idx(s->last_picture.f),
            .BackwardRefIdx    = ff_nvdec_get_ref_idx(s->next_picture.f),
            .FrameWidth        = cur_frame->width,
            .FrameHeight       = cur_frame->height,

            .intra_pic_flag    = s->pict_type == AV_PICTURE_TYPE_I ||
                                 s->pict_type == AV_PICTURE_TYPE_BI,
            .ref_pic_flag      = s->pict_type == AV_PICTURE_TYPE_I ||
                                 s->pict_type == AV_PICTURE_TYPE_P,
            .progressive_fcm   = v->fcm == 0,

            .profile           = v->profile,
            .postprocflag      = v->postprocflag,
            .pulldown          = v->broadcast,
            .interlace         = v->interlace,
            .tfcntrflag        = v->tfcntrflag,
            .finterpflag       = v->finterpflag,
            .psf               = v->psf,
            .multires          = v->multires,
            .syncmarker        = v->resync_marker,
            .rangered          = v->rangered,
            .maxbframes        = s->max_b_frames,

            .panscan_flag      = v->panscanflag,
            .refdist_flag      = v->refdist_flag,
            .extended_mv       = v->extended_mv,
            .dquant            = v->dquant,
            .vstransform       = v->vstransform,
            .loopfilter        = v->s.loop_filter,
            .fastuvmc          = v->fastuvmc,
            .overlap           = v->overlap,
            .quantizer         = v->quantizer_mode,
            .extended_dmv      = v->extended_dmv,
            .range_mapy_flag   = v->range_mapy_flag,
            .range_mapy        = v->range_mapy,
            .range_mapuv_flag  = v->range_mapuv_flag,
            .range_mapuv       = v->range_mapuv,
            .rangeredfrm       = v->rangeredfrm,
        }
    };

    return 0;
}

static int nvdec_vc1_frame_params(AVCodecContext *avctx,
                                  AVBufferRef *hw_frames_ctx)
{
    // Each frame can at most have one P and one B reference
    return ff_nvdec_frame_params(avctx, hw_frames_ctx, 2, 0);
}

const AVHWAccel ff_vc1_nvdec_hwaccel = {
    .name                 = "vc1_nvdec",
    .type                 = AVMEDIA_TYPE_VIDEO,
    .id                   = AV_CODEC_ID_VC1,
    .pix_fmt              = AV_PIX_FMT_CUDA,
    .start_frame          = nvdec_vc1_start_frame,
    .end_frame            = ff_nvdec_simple_end_frame,
    .decode_slice         = ff_nvdec_simple_decode_slice,
    .frame_params         = nvdec_vc1_frame_params,
    .init                 = ff_nvdec_decode_init,
    .uninit               = ff_nvdec_decode_uninit,
    .priv_data_size       = sizeof(NVDECContext),
};

#if CONFIG_WMV3_NVDEC_HWACCEL
const AVHWAccel ff_wmv3_nvdec_hwaccel = {
    .name                 = "wmv3_nvdec",
    .type                 = AVMEDIA_TYPE_VIDEO,
    .id                   = AV_CODEC_ID_WMV3,
    .pix_fmt              = AV_PIX_FMT_CUDA,
    .start_frame          = nvdec_vc1_start_frame,
    .end_frame            = ff_nvdec_simple_end_frame,
    .decode_slice         = ff_nvdec_simple_decode_slice,
    .frame_params         = nvdec_vc1_frame_params,
    .init                 = ff_nvdec_decode_init,
    .uninit               = ff_nvdec_decode_uninit,
    .priv_data_size       = sizeof(NVDECContext),
};
#endif
