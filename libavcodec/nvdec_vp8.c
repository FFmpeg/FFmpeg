/*
 * VP8 HW decode acceleration through NVDEC
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
#include "hwaccel_internal.h"
#include "internal.h"
#include "vp8.h"

static unsigned char safe_get_ref_idx(VP8Frame *frame)
{
    return frame ? ff_nvdec_get_ref_idx(frame->tf.f) : 255;
}

static int nvdec_vp8_start_frame(AVCodecContext *avctx, const uint8_t *buffer, uint32_t size)
{
    VP8Context *h = avctx->priv_data;

    NVDECContext      *ctx = avctx->internal->hwaccel_priv_data;
    CUVIDPICPARAMS     *pp = &ctx->pic_params;
    FrameDecodeData *fdd;
    NVDECFrame *cf;
    AVFrame *cur_frame = h->framep[VP8_FRAME_CURRENT]->tf.f;

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

        .CodecSpecific.vp8 = {
            .width                       = cur_frame->width,
            .height                      = cur_frame->height,

            .first_partition_size        = h->header_partition_size,

            .LastRefIdx                  = safe_get_ref_idx(h->framep[VP8_FRAME_PREVIOUS]),
            .GoldenRefIdx                = safe_get_ref_idx(h->framep[VP8_FRAME_GOLDEN]),
            .AltRefIdx                   = safe_get_ref_idx(h->framep[VP8_FRAME_ALTREF]),
            /*
             * Explicit braces for anonymous inners and unnamed fields
             * to work around limitations in ancient versions of gcc.
             */
            { // union
                { // struct
                    !h->keyframe,             // frame_type
                    h->profile,               // version
                    !h->invisible,            // show_frame
                    h->segmentation.enabled ? // update_mb_segmentation_data
                        h->segmentation.update_feature_data : 0,
                }
            }
        }
    };

    return 0;
}

static int nvdec_vp8_frame_params(AVCodecContext *avctx,
                                  AVBufferRef *hw_frames_ctx)
{
    // VP8 uses a fixed size pool of 3 possible reference frames
    return ff_nvdec_frame_params(avctx, hw_frames_ctx, 3, 0);
}

const FFHWAccel ff_vp8_nvdec_hwaccel = {
    .p.name               = "vp8_nvdec",
    .p.type               = AVMEDIA_TYPE_VIDEO,
    .p.id                 = AV_CODEC_ID_VP8,
    .p.pix_fmt            = AV_PIX_FMT_CUDA,
    .start_frame          = nvdec_vp8_start_frame,
    .end_frame            = ff_nvdec_simple_end_frame,
    .decode_slice         = ff_nvdec_simple_decode_slice,
    .frame_params         = nvdec_vp8_frame_params,
    .init                 = ff_nvdec_decode_init,
    .uninit               = ff_nvdec_decode_uninit,
    .priv_data_size       = sizeof(NVDECContext),
};
