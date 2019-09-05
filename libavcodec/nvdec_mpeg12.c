/*
 * MPEG-1/2 HW decode acceleration through NVDEC
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
#include "mpegvideo.h"
#include "nvdec.h"
#include "decode.h"

static int nvdec_mpeg12_start_frame(AVCodecContext *avctx, const uint8_t *buffer, uint32_t size)
{
    MpegEncContext *s = avctx->priv_data;

    NVDECContext      *ctx = avctx->internal->hwaccel_priv_data;
    CUVIDPICPARAMS     *pp = &ctx->pic_params;
    CUVIDMPEG2PICPARAMS *ppc = &pp->CodecSpecific.mpeg2;
    FrameDecodeData *fdd;
    NVDECFrame *cf;
    AVFrame *cur_frame = s->current_picture.f;

    int ret, i;

    ret = ff_nvdec_start_frame(avctx, cur_frame);
    if (ret < 0)
        return ret;

    fdd = (FrameDecodeData*)cur_frame->private_ref->data;
    cf  = (NVDECFrame*)fdd->hwaccel_priv;

    *pp = (CUVIDPICPARAMS) {
        .PicWidthInMbs     = (cur_frame->width  + 15) / 16,
        .FrameHeightInMbs  = (cur_frame->height + 15) / 16,
        .CurrPicIdx        = cf->idx,

        .intra_pic_flag    = s->pict_type == AV_PICTURE_TYPE_I,
        .ref_pic_flag      = s->pict_type == AV_PICTURE_TYPE_I ||
                             s->pict_type == AV_PICTURE_TYPE_P,

        .CodecSpecific.mpeg2 = {
            .ForwardRefIdx     = ff_nvdec_get_ref_idx(s->last_picture.f),
            .BackwardRefIdx    = ff_nvdec_get_ref_idx(s->next_picture.f),

            .picture_coding_type        = s->pict_type,
            .full_pel_forward_vector    = s->full_pel[0],
            .full_pel_backward_vector   = s->full_pel[1],
            .f_code                     = { { s->mpeg_f_code[0][0],
                                              s->mpeg_f_code[0][1] },
                                            { s->mpeg_f_code[1][0],
                                              s->mpeg_f_code[1][1] } },
            .intra_dc_precision         = s->intra_dc_precision,
            .frame_pred_frame_dct       = s->frame_pred_frame_dct,
            .concealment_motion_vectors = s->concealment_motion_vectors,
            .q_scale_type               = s->q_scale_type,
            .intra_vlc_format           = s->intra_vlc_format,
            .alternate_scan             = s->alternate_scan,
            .top_field_first            = s->top_field_first,
        }
    };

    for (i = 0; i < 64; ++i) {
        ppc->QuantMatrixIntra[i] = s->intra_matrix[i];
        ppc->QuantMatrixInter[i] = s->inter_matrix[i];
    }

    return 0;
}

static int nvdec_mpeg12_frame_params(AVCodecContext *avctx,
                                  AVBufferRef *hw_frames_ctx)
{
    // Each frame can at most have one P and one B reference
    return ff_nvdec_frame_params(avctx, hw_frames_ctx, 2, 0);
}

#if CONFIG_MPEG2_NVDEC_HWACCEL
const AVHWAccel ff_mpeg2_nvdec_hwaccel = {
    .name                 = "mpeg2_nvdec",
    .type                 = AVMEDIA_TYPE_VIDEO,
    .id                   = AV_CODEC_ID_MPEG2VIDEO,
    .pix_fmt              = AV_PIX_FMT_CUDA,
    .start_frame          = nvdec_mpeg12_start_frame,
    .end_frame            = ff_nvdec_simple_end_frame,
    .decode_slice         = ff_nvdec_simple_decode_slice,
    .frame_params         = nvdec_mpeg12_frame_params,
    .init                 = ff_nvdec_decode_init,
    .uninit               = ff_nvdec_decode_uninit,
    .priv_data_size       = sizeof(NVDECContext),
};
#endif

#if CONFIG_MPEG1_NVDEC_HWACCEL
const AVHWAccel ff_mpeg1_nvdec_hwaccel = {
    .name                 = "mpeg1_nvdec",
    .type                 = AVMEDIA_TYPE_VIDEO,
    .id                   = AV_CODEC_ID_MPEG1VIDEO,
    .pix_fmt              = AV_PIX_FMT_CUDA,
    .start_frame          = nvdec_mpeg12_start_frame,
    .end_frame            = ff_nvdec_simple_end_frame,
    .decode_slice         = ff_nvdec_simple_decode_slice,
    .frame_params         = nvdec_mpeg12_frame_params,
    .init                 = ff_nvdec_decode_init,
    .uninit               = ff_nvdec_decode_uninit,
    .priv_data_size       = sizeof(NVDECContext),
};
#endif
