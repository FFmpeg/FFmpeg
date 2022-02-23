/*
 * MJPEG HW decode acceleration through NVDEC
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

#include "config_components.h"

#include "avcodec.h"
#include "internal.h"
#include "mjpegdec.h"
#include "nvdec.h"
#include "decode.h"

static int nvdec_mjpeg_start_frame(AVCodecContext *avctx, const uint8_t *buffer, uint32_t size)
{
    MJpegDecodeContext *s = avctx->priv_data;

    NVDECContext      *ctx = avctx->internal->hwaccel_priv_data;
    CUVIDPICPARAMS     *pp = &ctx->pic_params;
    FrameDecodeData *fdd;
    NVDECFrame *cf;
    AVFrame *cur_frame = s->picture;

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

        .intra_pic_flag    = 1,
        .ref_pic_flag      = 0,
    };

    return ff_nvdec_simple_decode_slice(avctx, buffer, size);
}

static int nvdec_mjpeg_decode_slice(AVCodecContext *avctx, const uint8_t *buffer, uint32_t size)
{
    return 0;
}

static int nvdec_mjpeg_frame_params(AVCodecContext *avctx,
                                  AVBufferRef *hw_frames_ctx)
{
    // Only need storage for the current frame
    return ff_nvdec_frame_params(avctx, hw_frames_ctx, 1, 0);
}

#if CONFIG_MJPEG_NVDEC_HWACCEL
AVHWAccel ff_mjpeg_nvdec_hwaccel = {
    .name                 = "mjpeg_nvdec",
    .type                 = AVMEDIA_TYPE_VIDEO,
    .id                   = AV_CODEC_ID_MJPEG,
    .pix_fmt              = AV_PIX_FMT_CUDA,
    .start_frame          = nvdec_mjpeg_start_frame,
    .end_frame            = ff_nvdec_simple_end_frame,
    .decode_slice         = nvdec_mjpeg_decode_slice,
    .frame_params         = nvdec_mjpeg_frame_params,
    .init                 = ff_nvdec_decode_init,
    .uninit               = ff_nvdec_decode_uninit,
    .priv_data_size       = sizeof(NVDECContext),
};
#endif
