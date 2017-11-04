/*
 * MPEG-4 Part 2 HW decode acceleration through NVDEC
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
#include "mpeg4video.h"
#include "nvdec.h"
#include "decode.h"

static int nvdec_mpeg4_start_frame(AVCodecContext *avctx, const uint8_t *buffer, uint32_t size)
{
    Mpeg4DecContext *m = avctx->priv_data;
    MpegEncContext *s = &m->m;

    NVDECContext      *ctx = avctx->internal->hwaccel_priv_data;
    CUVIDPICPARAMS     *pp = &ctx->pic_params;
    CUVIDMPEG4PICPARAMS *ppc = &pp->CodecSpecific.mpeg4;
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
                             s->pict_type == AV_PICTURE_TYPE_P ||
                             s->pict_type == AV_PICTURE_TYPE_S,

        .CodecSpecific.mpeg4 = {
            .ForwardRefIdx                = ff_nvdec_get_ref_idx(s->last_picture.f),
            .BackwardRefIdx               = ff_nvdec_get_ref_idx(s->next_picture.f),

            .video_object_layer_width     = s->width,
            .video_object_layer_height    = s->height,
            .vop_time_increment_bitcount  = m->time_increment_bits,
            .top_field_first              = s->top_field_first,
            .resync_marker_disable        = !m->resync_marker,
            .quant_type                   = s->mpeg_quant,
            .quarter_sample               = s->quarter_sample,
            .short_video_header           = avctx->codec->id == AV_CODEC_ID_H263,
            .divx_flags                   = s->divx_packed ? 5 : 0,

            .vop_coding_type              = s->pict_type - AV_PICTURE_TYPE_I,
            .vop_coded                    = 1,
            .vop_rounding_type            = s->no_rounding,
            .alternate_vertical_scan_flag = s->alternate_scan,
            .interlaced                   = !s->progressive_sequence,
            .vop_fcode_forward            = s->f_code,
            .vop_fcode_backward           = s->b_code,
            .trd                          = { s->pp_time, s->pp_field_time >> 1 },
            .trb                          = { s->pb_time, s->pb_field_time >> 1 },

            .gmc_enabled                  = s->pict_type == AV_PICTURE_TYPE_S &&
                                            m->vol_sprite_usage == GMC_SPRITE,
        }
    };

    for (i = 0; i < 64; ++i) {
        ppc->QuantMatrixIntra[i] = s->intra_matrix[i];
        ppc->QuantMatrixInter[i] = s->inter_matrix[i];
    }

    // We need to pass the full frame buffer and not just the slice
    return ff_nvdec_simple_decode_slice(avctx, buffer, size);
}

static int nvdec_mpeg4_decode_slice(AVCodecContext *avctx, const uint8_t *buffer, uint32_t size)
{
    return 0;
}

static int nvdec_mpeg4_frame_params(AVCodecContext *avctx,
                                  AVBufferRef *hw_frames_ctx)
{
    // Each frame can at most have one P and one B reference
    return ff_nvdec_frame_params(avctx, hw_frames_ctx, 2);
}

const AVHWAccel ff_mpeg4_nvdec_hwaccel = {
    .name                 = "mpeg4_nvdec",
    .type                 = AVMEDIA_TYPE_VIDEO,
    .id                   = AV_CODEC_ID_MPEG4,
    .pix_fmt              = AV_PIX_FMT_CUDA,
    .start_frame          = nvdec_mpeg4_start_frame,
    .end_frame            = ff_nvdec_simple_end_frame,
    .decode_slice         = nvdec_mpeg4_decode_slice,
    .frame_params         = nvdec_mpeg4_frame_params,
    .init                 = ff_nvdec_decode_init,
    .uninit               = ff_nvdec_decode_uninit,
    .priv_data_size       = sizeof(NVDECContext),
};
