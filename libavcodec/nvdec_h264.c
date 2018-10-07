/*
 * MPEG-4 Part 10 / AVC / H.264 HW decode acceleration through NVDEC
 *
 * Copyright (c) 2016 Anton Khirnov
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

#include <stdint.h>
#include <string.h>

#include "avcodec.h"
#include "nvdec.h"
#include "decode.h"
#include "internal.h"
#include "h264dec.h"

static void dpb_add(const H264Context *h, CUVIDH264DPBENTRY *dst, const H264Picture *src,
                    int frame_idx)
{
    FrameDecodeData *fdd = (FrameDecodeData*)src->f->private_ref->data;
    const NVDECFrame *cf = fdd->hwaccel_priv;

    dst->PicIdx             = cf ? cf->idx : -1;
    dst->FrameIdx           = frame_idx;
    dst->is_long_term       = src->long_ref;
    dst->not_existing       = 0;
    dst->used_for_reference = src->reference & 3;
    dst->FieldOrderCnt[0]   = src->field_poc[0];
    dst->FieldOrderCnt[1]   = src->field_poc[1];
}

static int nvdec_h264_start_frame(AVCodecContext *avctx,
                                  const uint8_t *buffer, uint32_t size)
{
    const H264Context *h = avctx->priv_data;
    const PPS *pps = h->ps.pps;
    const SPS *sps = h->ps.sps;

    NVDECContext       *ctx = avctx->internal->hwaccel_priv_data;
    CUVIDPICPARAMS      *pp = &ctx->pic_params;
    CUVIDH264PICPARAMS *ppc = &pp->CodecSpecific.h264;
    FrameDecodeData *fdd;
    NVDECFrame *cf;

    int i, dpb_size, ret;

    ret = ff_nvdec_start_frame(avctx, h->cur_pic_ptr->f);
    if (ret < 0)
        return ret;

    fdd = (FrameDecodeData*)h->cur_pic_ptr->f->private_ref->data;
    cf  = (NVDECFrame*)fdd->hwaccel_priv;

    *pp = (CUVIDPICPARAMS) {
        .PicWidthInMbs     = h->mb_width,
        .FrameHeightInMbs  = h->mb_height,
        .CurrPicIdx        = cf->idx,
        .field_pic_flag    = FIELD_PICTURE(h),
        .bottom_field_flag = h->picture_structure == PICT_BOTTOM_FIELD,
        .second_field      = FIELD_PICTURE(h) && !h->first_field,
        .ref_pic_flag      = h->nal_ref_idc != 0,
        .intra_pic_flag    = 1,

        .CodecSpecific.h264 = {
            .log2_max_frame_num_minus4            = sps->log2_max_frame_num - 4,
            .pic_order_cnt_type                   = sps->poc_type,
            .log2_max_pic_order_cnt_lsb_minus4    = FFMAX(sps->log2_max_poc_lsb - 4, 0),
            .delta_pic_order_always_zero_flag     = sps->delta_pic_order_always_zero_flag,
            .frame_mbs_only_flag                  = sps->frame_mbs_only_flag,
            .direct_8x8_inference_flag            = sps->direct_8x8_inference_flag,
            .num_ref_frames                       = sps->ref_frame_count,
            .residual_colour_transform_flag       = sps->residual_color_transform_flag,
            .bit_depth_luma_minus8                = sps->bit_depth_luma - 8,
            .bit_depth_chroma_minus8              = sps->bit_depth_chroma - 8,
            .qpprime_y_zero_transform_bypass_flag = sps->transform_bypass,

            .entropy_coding_mode_flag               = pps->cabac,
            .pic_order_present_flag                 = pps->pic_order_present,
            .num_ref_idx_l0_active_minus1           = pps->ref_count[0] - 1,
            .num_ref_idx_l1_active_minus1           = pps->ref_count[1] - 1,
            .weighted_pred_flag                     = pps->weighted_pred,
            .weighted_bipred_idc                    = pps->weighted_bipred_idc,
            .pic_init_qp_minus26                    = pps->init_qp - 26,
            .deblocking_filter_control_present_flag = pps->deblocking_filter_parameters_present,
            .redundant_pic_cnt_present_flag         = pps->redundant_pic_cnt_present,
            .transform_8x8_mode_flag                = pps->transform_8x8_mode,
            .MbaffFrameFlag                         = sps->mb_aff && !FIELD_PICTURE(h),
            .constrained_intra_pred_flag            = pps->constrained_intra_pred,
            .chroma_qp_index_offset                 = pps->chroma_qp_index_offset[0],
            .second_chroma_qp_index_offset          = pps->chroma_qp_index_offset[1],
            .ref_pic_flag                           = h->nal_ref_idc != 0,
            .frame_num                              = h->poc.frame_num,
            .CurrFieldOrderCnt[0]                   = h->cur_pic_ptr->field_poc[0],
            .CurrFieldOrderCnt[1]                   = h->cur_pic_ptr->field_poc[1],
        },
    };

    memcpy(ppc->WeightScale4x4,    pps->scaling_matrix4,    sizeof(ppc->WeightScale4x4));
    memcpy(ppc->WeightScale8x8[0], pps->scaling_matrix8[0], sizeof(ppc->WeightScale8x8[0]));
    memcpy(ppc->WeightScale8x8[1], pps->scaling_matrix8[3], sizeof(ppc->WeightScale8x8[0]));

    dpb_size = 0;
    for (i = 0; i < h->short_ref_count; i++)
        dpb_add(h, &ppc->dpb[dpb_size++], h->short_ref[i], h->short_ref[i]->frame_num);
    for (i = 0; i < 16; i++) {
        if (h->long_ref[i])
            dpb_add(h, &ppc->dpb[dpb_size++], h->long_ref[i], i);
    }

    for (i = dpb_size; i < FF_ARRAY_ELEMS(ppc->dpb); i++)
        ppc->dpb[i].PicIdx = -1;

    return 0;
}

static int nvdec_h264_decode_slice(AVCodecContext *avctx, const uint8_t *buffer,
                                   uint32_t size)
{
    NVDECContext *ctx = avctx->internal->hwaccel_priv_data;
    CUVIDPICPARAMS *pp = &ctx->pic_params;
    const H264Context *h = avctx->priv_data;
    const H264SliceContext *sl = &h->slice_ctx[0];
    void *tmp;

    tmp = av_fast_realloc(ctx->bitstream, &ctx->bitstream_allocated,
                          ctx->bitstream_len + size + 3);
    if (!tmp)
        return AVERROR(ENOMEM);
    ctx->bitstream = tmp;

    tmp = av_fast_realloc(ctx->slice_offsets, &ctx->slice_offsets_allocated,
                          (ctx->nb_slices + 1) * sizeof(*ctx->slice_offsets));
    if (!tmp)
        return AVERROR(ENOMEM);
    ctx->slice_offsets = tmp;

    AV_WB24(ctx->bitstream + ctx->bitstream_len, 1);
    memcpy(ctx->bitstream + ctx->bitstream_len + 3, buffer, size);
    ctx->slice_offsets[ctx->nb_slices] = ctx->bitstream_len ;
    ctx->bitstream_len += size + 3;
    ctx->nb_slices++;

    if (sl->slice_type != AV_PICTURE_TYPE_I && sl->slice_type != AV_PICTURE_TYPE_SI)
        pp->intra_pic_flag = 0;

    return 0;
}

static int nvdec_h264_frame_params(AVCodecContext *avctx,
                                   AVBufferRef *hw_frames_ctx)
{
    const H264Context *h = avctx->priv_data;
    const SPS       *sps = h->ps.sps;
    return ff_nvdec_frame_params(avctx, hw_frames_ctx, sps->ref_frame_count + sps->num_reorder_frames, 0);
}

const AVHWAccel ff_h264_nvdec_hwaccel = {
    .name                 = "h264_nvdec",
    .type                 = AVMEDIA_TYPE_VIDEO,
    .id                   = AV_CODEC_ID_H264,
    .pix_fmt              = AV_PIX_FMT_CUDA,
    .start_frame          = nvdec_h264_start_frame,
    .end_frame            = ff_nvdec_end_frame,
    .decode_slice         = nvdec_h264_decode_slice,
    .frame_params         = nvdec_h264_frame_params,
    .init                 = ff_nvdec_decode_init,
    .uninit               = ff_nvdec_decode_uninit,
    .priv_data_size       = sizeof(NVDECContext),
};
