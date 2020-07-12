/*
 * MPEG-4 Part 10 / AVC / H.264 HW decode acceleration through VDPAU
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
#include "internal.h"
#include "h264dec.h"
#include "h264_ps.h"
#include "hwconfig.h"
#include "mpegutils.h"
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

static void vdpau_h264_set_rf(VdpReferenceFrameH264 *rf, H264Picture *pic,
                              int pic_structure)
{
    VdpVideoSurface surface = ff_vdpau_get_surface_id(pic->f);

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
        H264Picture **lp = list ? h->long_ref : h->short_ref;
        int i, ls    = list ? 16          : h->short_ref_count;

        for (i = 0; i < ls; ++i) {
            H264Picture *pic = lp[i];
            VdpReferenceFrameH264 *rf2;
            VdpVideoSurface surface_ref;
            int pic_frame_idx;

            if (!pic || !pic->reference)
                continue;
            pic_frame_idx = pic->long_ref ? pic->pic_id : pic->frame_num;
            surface_ref = ff_vdpau_get_surface_id(pic->f);

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
    const PPS *pps = h->ps.pps;
    const SPS *sps = h->ps.sps;
    H264Picture *pic = h->cur_pic_ptr;
    struct vdpau_picture_context *pic_ctx = pic->hwaccel_picture_private;
    VdpPictureInfoH264 *info = &pic_ctx->info.h264;
#ifdef VDP_DECODER_PROFILE_H264_HIGH_444_PREDICTIVE
    VdpPictureInfoH264Predictive *info2 = &pic_ctx->info.h264_predictive;
#endif

    /* init VdpPictureInfoH264 */
    info->slice_count                            = 0;
    info->field_order_cnt[0]                     = h264_foc(pic->field_poc[0]);
    info->field_order_cnt[1]                     = h264_foc(pic->field_poc[1]);
    info->is_reference                           = h->nal_ref_idc != 0;
    info->frame_num                              = h->poc.frame_num;
    info->field_pic_flag                         = h->picture_structure != PICT_FRAME;
    info->bottom_field_flag                      = h->picture_structure == PICT_BOTTOM_FIELD;
    info->num_ref_frames                         = sps->ref_frame_count;
    info->mb_adaptive_frame_field_flag           = sps->mb_aff && !info->field_pic_flag;
    info->constrained_intra_pred_flag            = pps->constrained_intra_pred;
    info->weighted_pred_flag                     = pps->weighted_pred;
    info->weighted_bipred_idc                    = pps->weighted_bipred_idc;
    info->frame_mbs_only_flag                    = sps->frame_mbs_only_flag;
    info->transform_8x8_mode_flag                = pps->transform_8x8_mode;
    info->chroma_qp_index_offset                 = pps->chroma_qp_index_offset[0];
    info->second_chroma_qp_index_offset          = pps->chroma_qp_index_offset[1];
    info->pic_init_qp_minus26                    = pps->init_qp - 26;
    info->num_ref_idx_l0_active_minus1           = pps->ref_count[0] - 1;
    info->num_ref_idx_l1_active_minus1           = pps->ref_count[1] - 1;
    info->log2_max_frame_num_minus4              = sps->log2_max_frame_num - 4;
    info->pic_order_cnt_type                     = sps->poc_type;
    info->log2_max_pic_order_cnt_lsb_minus4      = sps->poc_type ? 0 : sps->log2_max_poc_lsb - 4;
    info->delta_pic_order_always_zero_flag       = sps->delta_pic_order_always_zero_flag;
    info->direct_8x8_inference_flag              = sps->direct_8x8_inference_flag;
#ifdef VDP_DECODER_PROFILE_H264_HIGH_444_PREDICTIVE
    info2->qpprime_y_zero_transform_bypass_flag  = sps->transform_bypass;
    info2->separate_colour_plane_flag            = sps->residual_color_transform_flag;
#endif
    info->entropy_coding_mode_flag               = pps->cabac;
    info->pic_order_present_flag                 = pps->pic_order_present;
    info->deblocking_filter_control_present_flag = pps->deblocking_filter_parameters_present;
    info->redundant_pic_cnt_present_flag         = pps->redundant_pic_cnt_present;

    memcpy(info->scaling_lists_4x4, pps->scaling_matrix4,
           sizeof(info->scaling_lists_4x4));
    memcpy(info->scaling_lists_8x8[0], pps->scaling_matrix8[0],
           sizeof(info->scaling_lists_8x8[0]));
    memcpy(info->scaling_lists_8x8[1], pps->scaling_matrix8[3],
           sizeof(info->scaling_lists_8x8[1]));

    vdpau_h264_set_reference_frames(avctx);

    return ff_vdpau_common_start_frame(pic_ctx, buffer, size);
}

static const uint8_t start_code_prefix[3] = { 0x00, 0x00, 0x01 };

static int vdpau_h264_decode_slice(AVCodecContext *avctx,
                                   const uint8_t *buffer, uint32_t size)
{
    H264Context *h = avctx->priv_data;
    H264Picture *pic = h->cur_pic_ptr;
    struct vdpau_picture_context *pic_ctx = pic->hwaccel_picture_private;
    int val;

    val = ff_vdpau_add_buffer(pic_ctx, start_code_prefix, 3);
    if (val)
        return val;

    val = ff_vdpau_add_buffer(pic_ctx, buffer, size);
    if (val)
        return val;

    pic_ctx->info.h264.slice_count++;
    return 0;
}

static int vdpau_h264_end_frame(AVCodecContext *avctx)
{
    H264Context *h = avctx->priv_data;
    H264SliceContext *sl = &h->slice_ctx[0];
    H264Picture *pic = h->cur_pic_ptr;
    struct vdpau_picture_context *pic_ctx = pic->hwaccel_picture_private;
    int val;

    val = ff_vdpau_common_end_frame(avctx, pic->f, pic_ctx);
    if (val < 0)
        return val;

    ff_h264_draw_horiz_band(h, sl, 0, h->avctx->height);
    return 0;
}

static int vdpau_h264_init(AVCodecContext *avctx)
{
    VdpDecoderProfile profile;
    uint32_t level = avctx->level;

    switch (avctx->profile & ~FF_PROFILE_H264_INTRA) {
    case FF_PROFILE_H264_BASELINE:
        profile = VDP_DECODER_PROFILE_H264_BASELINE;
        break;
    case FF_PROFILE_H264_CONSTRAINED_BASELINE:
#ifdef VDP_DECODER_PROFILE_H264_CONSTRAINED_BASELINE
        profile = VDP_DECODER_PROFILE_H264_CONSTRAINED_BASELINE;
        break;
#endif
    case FF_PROFILE_H264_MAIN:
        profile = VDP_DECODER_PROFILE_H264_MAIN;
        break;
    case FF_PROFILE_H264_HIGH:
        profile = VDP_DECODER_PROFILE_H264_HIGH;
        break;
#ifdef VDP_DECODER_PROFILE_H264_EXTENDED
    case FF_PROFILE_H264_EXTENDED:
        profile = VDP_DECODER_PROFILE_H264_EXTENDED;
        break;
#endif
    case FF_PROFILE_H264_HIGH_10:
        /* XXX: High 10 can be treated as High so long as only 8 bits per
         * format are supported. */
        profile = VDP_DECODER_PROFILE_H264_HIGH;
        break;
#ifdef VDP_DECODER_PROFILE_H264_HIGH_444_PREDICTIVE
    case FF_PROFILE_H264_HIGH_422:
    case FF_PROFILE_H264_HIGH_444_PREDICTIVE:
    case FF_PROFILE_H264_CAVLC_444:
        profile = VDP_DECODER_PROFILE_H264_HIGH_444_PREDICTIVE;
        break;
#endif
    default:
        return AVERROR(ENOTSUP);
    }

    if ((avctx->profile & FF_PROFILE_H264_INTRA) && avctx->level == 11)
        level = VDP_DECODER_LEVEL_H264_1b;

    return ff_vdpau_common_init(avctx, profile, level);
}

const AVHWAccel ff_h264_vdpau_hwaccel = {
    .name           = "h264_vdpau",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_H264,
    .pix_fmt        = AV_PIX_FMT_VDPAU,
    .start_frame    = vdpau_h264_start_frame,
    .end_frame      = vdpau_h264_end_frame,
    .decode_slice   = vdpau_h264_decode_slice,
    .frame_priv_data_size = sizeof(struct vdpau_picture_context),
    .init           = vdpau_h264_init,
    .uninit         = ff_vdpau_common_uninit,
    .frame_params   = ff_vdpau_common_frame_params,
    .priv_data_size = sizeof(VDPAUContext),
    .caps_internal  = HWACCEL_CAP_ASYNC_SAFE,
};
