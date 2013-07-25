/*
 * MPEG-4 Part 10 / AVC / H.264 HW decode acceleration through VDPAU
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
#include "h264.h"
#include "vdpau.h"
#include "vdpau_internal.h"

static int32_t h264_foc(int foc)
{
    if (foc == INT_MAX)
        foc = 0;
    return foc;
}

static void vdpau_h264_clear_rf(VdpReferenceFrameH264 *rf)
{
    rf->surface             = VDP_INVALID_HANDLE;
    rf->is_long_term        = VDP_FALSE;
    rf->top_is_reference    = VDP_FALSE;
    rf->bottom_is_reference = VDP_FALSE;
    rf->field_order_cnt[0]  = 0;
    rf->field_order_cnt[1]  = 0;
    rf->frame_idx           = 0;
}

static void vdpau_h264_set_rf(VdpReferenceFrameH264 *rf, Picture *pic,
                              int pic_structure)
{
    VdpVideoSurface surface = ff_vdpau_get_surface_id(pic);

    if (pic_structure == 0)
        pic_structure = pic->reference;

    rf->surface             = surface;
    rf->is_long_term        = pic->reference && pic->long_ref;
    rf->top_is_reference    = (pic_structure & PICT_TOP_FIELD)    != 0;
    rf->bottom_is_reference = (pic_structure & PICT_BOTTOM_FIELD) != 0;
    rf->field_order_cnt[0]  = h264_foc(pic->field_poc[0]);
    rf->field_order_cnt[1]  = h264_foc(pic->field_poc[1]);
    rf->frame_idx           = pic->long_ref ? pic->pic_id : pic->frame_num;
}

static void vdpau_h264_set_reference_frames(AVCodecContext *avctx)
{
    H264Context * const h = avctx->priv_data;
    struct vdpau_picture_context *pic_ctx = h->cur_pic_ptr->hwaccel_picture_private;
    VdpPictureInfoH264 *info = &pic_ctx->info.h264;
    int list;

    VdpReferenceFrameH264 *rf = &info->referenceFrames[0];
#define H264_RF_COUNT FF_ARRAY_ELEMS(info->referenceFrames)

    for (list = 0; list < 2; ++list) {
        Picture **lp = list ? h->long_ref : h->short_ref;
        int i, ls    = list ? 16          : h->short_ref_count;

        for (i = 0; i < ls; ++i) {
            Picture *pic = lp[i];
            VdpReferenceFrameH264 *rf2;
            VdpVideoSurface surface_ref;
            int pic_frame_idx;

            if (!pic || !pic->reference)
                continue;
            pic_frame_idx = pic->long_ref ? pic->pic_id : pic->frame_num;
            surface_ref = ff_vdpau_get_surface_id(pic);

            rf2 = &info->referenceFrames[0];
            while (rf2 != rf) {
                if ((rf2->surface      == surface_ref)   &&
                    (rf2->is_long_term == pic->long_ref) &&
                    (rf2->frame_idx    == pic_frame_idx))
                    break;
                ++rf2;
            }
            if (rf2 != rf) {
                rf2->top_is_reference    |= (pic->reference & PICT_TOP_FIELD)    ? VDP_TRUE : VDP_FALSE;
                rf2->bottom_is_reference |= (pic->reference & PICT_BOTTOM_FIELD) ? VDP_TRUE : VDP_FALSE;
                continue;
            }

            if (rf >= &info->referenceFrames[H264_RF_COUNT])
                continue;

            vdpau_h264_set_rf(rf, pic, pic->reference);
            ++rf;
        }
    }

    for (; rf < &info->referenceFrames[H264_RF_COUNT]; ++rf)
        vdpau_h264_clear_rf(rf);
}

static int vdpau_h264_start_frame(AVCodecContext *avctx,
                                  const uint8_t *buffer, uint32_t size)
{
    H264Context * const h = avctx->priv_data;
    Picture *pic = h->cur_pic_ptr;
    struct vdpau_picture_context *pic_ctx = pic->hwaccel_picture_private;
    VdpPictureInfoH264 *info = &pic_ctx->info.h264;

    /* init VdpPictureInfoH264 */
    info->slice_count                            = 0;
    info->field_order_cnt[0]                     = h264_foc(pic->field_poc[0]);
    info->field_order_cnt[1]                     = h264_foc(pic->field_poc[1]);
    info->is_reference                           = h->nal_ref_idc != 0;
    info->frame_num                              = h->frame_num;
    info->field_pic_flag                         = h->picture_structure != PICT_FRAME;
    info->bottom_field_flag                      = h->picture_structure == PICT_BOTTOM_FIELD;
    info->num_ref_frames                         = h->sps.ref_frame_count;
    info->mb_adaptive_frame_field_flag           = h->sps.mb_aff && !info->field_pic_flag;
    info->constrained_intra_pred_flag            = h->pps.constrained_intra_pred;
    info->weighted_pred_flag                     = h->pps.weighted_pred;
    info->weighted_bipred_idc                    = h->pps.weighted_bipred_idc;
    info->frame_mbs_only_flag                    = h->sps.frame_mbs_only_flag;
    info->transform_8x8_mode_flag                = h->pps.transform_8x8_mode;
    info->chroma_qp_index_offset                 = h->pps.chroma_qp_index_offset[0];
    info->second_chroma_qp_index_offset          = h->pps.chroma_qp_index_offset[1];
    info->pic_init_qp_minus26                    = h->pps.init_qp - 26;
    info->num_ref_idx_l0_active_minus1           = h->pps.ref_count[0] - 1;
    info->num_ref_idx_l1_active_minus1           = h->pps.ref_count[1] - 1;
    info->log2_max_frame_num_minus4              = h->sps.log2_max_frame_num - 4;
    info->pic_order_cnt_type                     = h->sps.poc_type;
    info->log2_max_pic_order_cnt_lsb_minus4      = h->sps.poc_type ? 0 : h->sps.log2_max_poc_lsb - 4;
    info->delta_pic_order_always_zero_flag       = h->sps.delta_pic_order_always_zero_flag;
    info->direct_8x8_inference_flag              = h->sps.direct_8x8_inference_flag;
    info->entropy_coding_mode_flag               = h->pps.cabac;
    info->pic_order_present_flag                 = h->pps.pic_order_present;
    info->deblocking_filter_control_present_flag = h->pps.deblocking_filter_parameters_present;
    info->redundant_pic_cnt_present_flag         = h->pps.redundant_pic_cnt_present;

    memcpy(info->scaling_lists_4x4, h->pps.scaling_matrix4,
           sizeof(info->scaling_lists_4x4));
    memcpy(info->scaling_lists_8x8[0], h->pps.scaling_matrix8[0],
           sizeof(info->scaling_lists_8x8[0]));
    memcpy(info->scaling_lists_8x8[1], h->pps.scaling_matrix8[3],
           sizeof(info->scaling_lists_8x8[1]));

    vdpau_h264_set_reference_frames(avctx);

    return ff_vdpau_common_start_frame(pic, buffer, size);
}

static const uint8_t start_code_prefix[3] = { 0x00, 0x00, 0x01 };

static int vdpau_h264_decode_slice(AVCodecContext *avctx,
                                   const uint8_t *buffer, uint32_t size)
{
    H264Context *h = avctx->priv_data;
    Picture *pic   = h->cur_pic_ptr;
    struct vdpau_picture_context *pic_ctx = pic->hwaccel_picture_private;
    int val;

    val = ff_vdpau_add_buffer(pic, start_code_prefix, 3);
    if (val)
        return val;

    val = ff_vdpau_add_buffer(pic, buffer, size);
    if (val)
        return val;

    pic_ctx->info.h264.slice_count++;
    return 0;
}

static int vdpau_h264_end_frame(AVCodecContext *avctx)
{
    AVVDPAUContext *hwctx = avctx->hwaccel_context;
    H264Context *h = avctx->priv_data;
    Picture *pic   = h->cur_pic_ptr;
    struct vdpau_picture_context *pic_ctx = pic->hwaccel_picture_private;
    VdpVideoSurface surf = ff_vdpau_get_surface_id(pic);

    hwctx->render(hwctx->decoder, surf, (void *)&pic_ctx->info,
                  pic_ctx->bitstream_buffers_used, pic_ctx->bitstream_buffers);

    ff_h264_draw_horiz_band(h, 0, h->avctx->height);
    av_freep(&pic_ctx->bitstream_buffers);

    return 0;
}

AVHWAccel ff_h264_vdpau_hwaccel = {
    .name           = "h264_vdpau",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_H264,
    .pix_fmt        = AV_PIX_FMT_VDPAU,
    .start_frame    = vdpau_h264_start_frame,
    .end_frame      = vdpau_h264_end_frame,
    .decode_slice   = vdpau_h264_decode_slice,
    .priv_data_size = sizeof(struct vdpau_picture_context),
};
