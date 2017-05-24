/*
 * H.264 HW decode acceleration through VA API
 *
 * Copyright (C) 2008-2009 Splitted-Desktop Systems
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

#include "h264dec.h"
#include "h264_ps.h"
#include "hwaccel.h"
#include "vaapi_decode.h"

/**
 * @file
 * This file implements the glue code between FFmpeg's and VA API's
 * structures for H.264 decoding.
 */

/**
 * Initialize an empty VA API picture.
 *
 * VA API requires a fixed-size reference picture array.
 */
static void init_vaapi_pic(VAPictureH264 *va_pic)
{
    va_pic->picture_id          = VA_INVALID_ID;
    va_pic->flags               = VA_PICTURE_H264_INVALID;
    va_pic->TopFieldOrderCnt    = 0;
    va_pic->BottomFieldOrderCnt = 0;
}

/**
 * Translate an FFmpeg Picture into its VA API form.
 *
 * @param[out] va_pic          A pointer to VA API's own picture struct
 * @param[in]  pic             A pointer to the FFmpeg picture struct to convert
 * @param[in]  pic_structure   The picture field type (as defined in mpegvideo.h),
 *                             supersedes pic's field type if nonzero.
 */
static void fill_vaapi_pic(VAPictureH264 *va_pic,
                           const H264Picture *pic,
                           int            pic_structure)
{
    if (pic_structure == 0)
        pic_structure = pic->reference;
    pic_structure &= PICT_FRAME; /* PICT_TOP_FIELD|PICT_BOTTOM_FIELD */

    va_pic->picture_id = ff_vaapi_get_surface_id(pic->f);
    va_pic->frame_idx  = pic->long_ref ? pic->pic_id : pic->frame_num;

    va_pic->flags      = 0;
    if (pic_structure != PICT_FRAME)
        va_pic->flags |= (pic_structure & PICT_TOP_FIELD) ? VA_PICTURE_H264_TOP_FIELD : VA_PICTURE_H264_BOTTOM_FIELD;
    if (pic->reference)
        va_pic->flags |= pic->long_ref ? VA_PICTURE_H264_LONG_TERM_REFERENCE : VA_PICTURE_H264_SHORT_TERM_REFERENCE;

    va_pic->TopFieldOrderCnt = 0;
    if (pic->field_poc[0] != INT_MAX)
        va_pic->TopFieldOrderCnt = pic->field_poc[0];

    va_pic->BottomFieldOrderCnt = 0;
    if (pic->field_poc[1] != INT_MAX)
        va_pic->BottomFieldOrderCnt = pic->field_poc[1];
}

/** Decoded Picture Buffer (DPB). */
typedef struct DPB {
    int            size;        ///< Current number of reference frames in the DPB
    int            max_size;    ///< Max number of reference frames. This is FF_ARRAY_ELEMS(VAPictureParameterBufferH264.ReferenceFrames)
    VAPictureH264 *va_pics;     ///< Pointer to VAPictureParameterBufferH264.ReferenceFrames array
} DPB;

/**
 * Append picture to the decoded picture buffer, in a VA API form that
 * merges the second field picture attributes with the first, if
 * available.  The decoded picture buffer's size must be large enough
 * to receive the new VA API picture object.
 */
static int dpb_add(DPB *dpb, const H264Picture *pic)
{
    int i;

    if (dpb->size >= dpb->max_size)
        return -1;

    for (i = 0; i < dpb->size; i++) {
        VAPictureH264 * const va_pic = &dpb->va_pics[i];
        if (va_pic->picture_id == ff_vaapi_get_surface_id(pic->f)) {
            VAPictureH264 temp_va_pic;
            fill_vaapi_pic(&temp_va_pic, pic, 0);

            if ((temp_va_pic.flags ^ va_pic->flags) & (VA_PICTURE_H264_TOP_FIELD | VA_PICTURE_H264_BOTTOM_FIELD)) {
                va_pic->flags |= temp_va_pic.flags & (VA_PICTURE_H264_TOP_FIELD | VA_PICTURE_H264_BOTTOM_FIELD);
                /* Merge second field */
                if (temp_va_pic.flags & VA_PICTURE_H264_TOP_FIELD) {
                    va_pic->TopFieldOrderCnt    = temp_va_pic.TopFieldOrderCnt;
                } else {
                    va_pic->BottomFieldOrderCnt = temp_va_pic.BottomFieldOrderCnt;
                }
            }
            return 0;
        }
    }

    fill_vaapi_pic(&dpb->va_pics[dpb->size++], pic, 0);
    return 0;
}

/** Fill in VA API reference frames array. */
static int fill_vaapi_ReferenceFrames(VAPictureParameterBufferH264 *pic_param,
                                      const H264Context            *h)
{
    DPB dpb;
    int i;

    dpb.size     = 0;
    dpb.max_size = FF_ARRAY_ELEMS(pic_param->ReferenceFrames);
    dpb.va_pics  = pic_param->ReferenceFrames;
    for (i = 0; i < dpb.max_size; i++)
        init_vaapi_pic(&dpb.va_pics[i]);

    for (i = 0; i < h->short_ref_count; i++) {
        const H264Picture *pic = h->short_ref[i];
        if (pic && pic->reference && dpb_add(&dpb, pic) < 0)
            return -1;
    }

    for (i = 0; i < 16; i++) {
        const H264Picture *pic = h->long_ref[i];
        if (pic && pic->reference && dpb_add(&dpb, pic) < 0)
            return -1;
    }
    return 0;
}

/**
 * Fill in VA API reference picture lists from the FFmpeg reference
 * picture list.
 *
 * @param[out] RefPicList  VA API internal reference picture list
 * @param[in]  ref_list    A pointer to the FFmpeg reference list
 * @param[in]  ref_count   The number of reference pictures in ref_list
 */
static void fill_vaapi_RefPicList(VAPictureH264 RefPicList[32],
                                  const H264Ref *ref_list,
                                  unsigned int  ref_count)
{
    unsigned int i, n = 0;
    for (i = 0; i < ref_count; i++)
        if (ref_list[i].reference)
            fill_vaapi_pic(&RefPicList[n++], ref_list[i].parent,
                           ref_list[i].reference);

    for (; n < 32; n++)
        init_vaapi_pic(&RefPicList[n]);
}

/**
 * Fill in prediction weight table.
 *
 * VA API requires a plain prediction weight table as it does not infer
 * any value.
 *
 * @param[in]  h                   A pointer to the current H.264 context
 * @param[in]  list                The reference frame list index to use
 * @param[out] luma_weight_flag    VA API plain luma weight flag
 * @param[out] luma_weight         VA API plain luma weight table
 * @param[out] luma_offset         VA API plain luma offset table
 * @param[out] chroma_weight_flag  VA API plain chroma weight flag
 * @param[out] chroma_weight       VA API plain chroma weight table
 * @param[out] chroma_offset       VA API plain chroma offset table
 */
static void fill_vaapi_plain_pred_weight_table(const H264Context *h,
                                               int            list,
                                               unsigned char *luma_weight_flag,
                                               short          luma_weight[32],
                                               short          luma_offset[32],
                                               unsigned char *chroma_weight_flag,
                                               short          chroma_weight[32][2],
                                               short          chroma_offset[32][2])
{
    const H264SliceContext *sl = &h->slice_ctx[0];
    unsigned int i, j;

    *luma_weight_flag    = sl->pwt.luma_weight_flag[list];
    *chroma_weight_flag  = sl->pwt.chroma_weight_flag[list];

    for (i = 0; i < sl->ref_count[list]; i++) {
        /* VA API also wants the inferred (default) values, not
           only what is available in the bitstream (7.4.3.2). */
        if (sl->pwt.luma_weight_flag[list]) {
            luma_weight[i] = sl->pwt.luma_weight[i][list][0];
            luma_offset[i] = sl->pwt.luma_weight[i][list][1];
        } else {
            luma_weight[i] = 1 << sl->pwt.luma_log2_weight_denom;
            luma_offset[i] = 0;
        }
        for (j = 0; j < 2; j++) {
            if (sl->pwt.chroma_weight_flag[list]) {
                chroma_weight[i][j] = sl->pwt.chroma_weight[i][list][j][0];
                chroma_offset[i][j] = sl->pwt.chroma_weight[i][list][j][1];
            } else {
                chroma_weight[i][j] = 1 << sl->pwt.chroma_log2_weight_denom;
                chroma_offset[i][j] = 0;
            }
        }
    }
}

/** Initialize and start decoding a frame with VA API. */
static int vaapi_h264_start_frame(AVCodecContext          *avctx,
                                  av_unused const uint8_t *buffer,
                                  av_unused uint32_t       size)
{
    const H264Context *h = avctx->priv_data;
    VAAPIDecodePicture *pic = h->cur_pic_ptr->hwaccel_picture_private;
    const PPS *pps = h->ps.pps;
    const SPS *sps = h->ps.sps;
    VAPictureParameterBufferH264 pic_param;
    VAIQMatrixBufferH264 iq_matrix;
    int err;

    pic->output_surface = ff_vaapi_get_surface_id(h->cur_pic_ptr->f);

    pic_param = (VAPictureParameterBufferH264) {
        .picture_width_in_mbs_minus1                = h->mb_width - 1,
        .picture_height_in_mbs_minus1               = h->mb_height - 1,
        .bit_depth_luma_minus8                      = sps->bit_depth_luma - 8,
        .bit_depth_chroma_minus8                    = sps->bit_depth_chroma - 8,
        .num_ref_frames                             = sps->ref_frame_count,
        .seq_fields.bits = {
            .chroma_format_idc                      = sps->chroma_format_idc,
            .residual_colour_transform_flag         = sps->residual_color_transform_flag,
            .gaps_in_frame_num_value_allowed_flag   = sps->gaps_in_frame_num_allowed_flag,
            .frame_mbs_only_flag                    = sps->frame_mbs_only_flag,
            .mb_adaptive_frame_field_flag           = sps->mb_aff,
            .direct_8x8_inference_flag              = sps->direct_8x8_inference_flag,
            .MinLumaBiPredSize8x8                   = sps->level_idc >= 31, /* A.3.3.2 */
            .log2_max_frame_num_minus4              = sps->log2_max_frame_num - 4,
            .pic_order_cnt_type                     = sps->poc_type,
            .log2_max_pic_order_cnt_lsb_minus4      = sps->log2_max_poc_lsb - 4,
            .delta_pic_order_always_zero_flag       = sps->delta_pic_order_always_zero_flag,
        },
        .num_slice_groups_minus1                    = pps->slice_group_count - 1,
        .slice_group_map_type                       = pps->mb_slice_group_map_type,
        .slice_group_change_rate_minus1             = 0, /* FMO is not implemented */
        .pic_init_qp_minus26                        = pps->init_qp - 26,
        .pic_init_qs_minus26                        = pps->init_qs - 26,
        .chroma_qp_index_offset                     = pps->chroma_qp_index_offset[0],
        .second_chroma_qp_index_offset              = pps->chroma_qp_index_offset[1],
        .pic_fields.bits = {
            .entropy_coding_mode_flag               = pps->cabac,
            .weighted_pred_flag                     = pps->weighted_pred,
            .weighted_bipred_idc                    = pps->weighted_bipred_idc,
            .transform_8x8_mode_flag                = pps->transform_8x8_mode,
            .field_pic_flag                         = h->picture_structure != PICT_FRAME,
            .constrained_intra_pred_flag            = pps->constrained_intra_pred,
            .pic_order_present_flag                 = pps->pic_order_present,
            .deblocking_filter_control_present_flag = pps->deblocking_filter_parameters_present,
            .redundant_pic_cnt_present_flag         = pps->redundant_pic_cnt_present,
            .reference_pic_flag                     = h->nal_ref_idc != 0,
        },
        .frame_num                                  = h->poc.frame_num,
    };

    fill_vaapi_pic(&pic_param.CurrPic, h->cur_pic_ptr, h->picture_structure);
    err = fill_vaapi_ReferenceFrames(&pic_param, h);
    if (err < 0)
        goto fail;

    err = ff_vaapi_decode_make_param_buffer(avctx, pic,
                                            VAPictureParameterBufferType,
                                            &pic_param, sizeof(pic_param));
    if (err < 0)
        goto fail;

    memcpy(iq_matrix.ScalingList4x4,
           pps->scaling_matrix4, sizeof(iq_matrix.ScalingList4x4));
    memcpy(iq_matrix.ScalingList8x8[0],
           pps->scaling_matrix8[0], sizeof(iq_matrix.ScalingList8x8[0]));
    memcpy(iq_matrix.ScalingList8x8[1],
           pps->scaling_matrix8[3], sizeof(iq_matrix.ScalingList8x8[0]));

    err = ff_vaapi_decode_make_param_buffer(avctx, pic,
                                            VAIQMatrixBufferType,
                                            &iq_matrix, sizeof(iq_matrix));
    if (err < 0)
        goto fail;

    return 0;

fail:
    ff_vaapi_decode_cancel(avctx, pic);
    return err;
}

/** End a hardware decoding based frame. */
static int vaapi_h264_end_frame(AVCodecContext *avctx)
{
    const H264Context *h = avctx->priv_data;
    VAAPIDecodePicture *pic = h->cur_pic_ptr->hwaccel_picture_private;
    H264SliceContext *sl = &h->slice_ctx[0];
    int ret;

    ret = ff_vaapi_decode_issue(avctx, pic);
    if (ret < 0)
        goto finish;

    ff_h264_draw_horiz_band(h, sl, 0, h->avctx->height);

finish:
    return ret;
}

/** Decode the given H.264 slice with VA API. */
static int vaapi_h264_decode_slice(AVCodecContext *avctx,
                                   const uint8_t  *buffer,
                                   uint32_t        size)
{
    const H264Context *h = avctx->priv_data;
    VAAPIDecodePicture *pic = h->cur_pic_ptr->hwaccel_picture_private;
    const H264SliceContext *sl  = &h->slice_ctx[0];
    VASliceParameterBufferH264 slice_param;
    int err;

    slice_param = (VASliceParameterBufferH264) {
        .slice_data_size               = size,
        .slice_data_offset             = 0,
        .slice_data_flag               = VA_SLICE_DATA_FLAG_ALL,
        .slice_data_bit_offset         = get_bits_count(&sl->gb),
        .first_mb_in_slice             = (sl->mb_y >> FIELD_OR_MBAFF_PICTURE(h)) * h->mb_width + sl->mb_x,
        .slice_type                    = ff_h264_get_slice_type(sl),
        .direct_spatial_mv_pred_flag   = sl->slice_type == AV_PICTURE_TYPE_B ? sl->direct_spatial_mv_pred : 0,
        .num_ref_idx_l0_active_minus1  = sl->list_count > 0 ? sl->ref_count[0] - 1 : 0,
        .num_ref_idx_l1_active_minus1  = sl->list_count > 1 ? sl->ref_count[1] - 1 : 0,
        .cabac_init_idc                = sl->cabac_init_idc,
        .slice_qp_delta                = sl->qscale - h->ps.pps->init_qp,
        .disable_deblocking_filter_idc = sl->deblocking_filter < 2 ? !sl->deblocking_filter : sl->deblocking_filter,
        .slice_alpha_c0_offset_div2    = sl->slice_alpha_c0_offset / 2,
        .slice_beta_offset_div2        = sl->slice_beta_offset     / 2,
        .luma_log2_weight_denom        = sl->pwt.luma_log2_weight_denom,
        .chroma_log2_weight_denom      = sl->pwt.chroma_log2_weight_denom,
    };

    fill_vaapi_RefPicList(slice_param.RefPicList0, sl->ref_list[0],
                          sl->list_count > 0 ? sl->ref_count[0] : 0);
    fill_vaapi_RefPicList(slice_param.RefPicList1, sl->ref_list[1],
                          sl->list_count > 1 ? sl->ref_count[1] : 0);

    fill_vaapi_plain_pred_weight_table(h, 0,
                                       &slice_param.luma_weight_l0_flag,
                                       slice_param.luma_weight_l0,
                                       slice_param.luma_offset_l0,
                                       &slice_param.chroma_weight_l0_flag,
                                       slice_param.chroma_weight_l0,
                                       slice_param.chroma_offset_l0);
    fill_vaapi_plain_pred_weight_table(h, 1,
                                       &slice_param.luma_weight_l1_flag,
                                       slice_param.luma_weight_l1,
                                       slice_param.luma_offset_l1,
                                       &slice_param.chroma_weight_l1_flag,
                                       slice_param.chroma_weight_l1,
                                       slice_param.chroma_offset_l1);

    err = ff_vaapi_decode_make_slice_buffer(avctx, pic,
                                            &slice_param, sizeof(slice_param),
                                            buffer, size);
    if (err) {
        ff_vaapi_decode_cancel(avctx, pic);
        return err;
    }

    return 0;
}

AVHWAccel ff_h264_vaapi_hwaccel = {
    .name                 = "h264_vaapi",
    .type                 = AVMEDIA_TYPE_VIDEO,
    .id                   = AV_CODEC_ID_H264,
    .pix_fmt              = AV_PIX_FMT_VAAPI,
    .start_frame          = &vaapi_h264_start_frame,
    .end_frame            = &vaapi_h264_end_frame,
    .decode_slice         = &vaapi_h264_decode_slice,
    .frame_priv_data_size = sizeof(VAAPIDecodePicture),
    .init                 = &ff_vaapi_decode_init,
    .uninit               = &ff_vaapi_decode_uninit,
    .priv_data_size       = sizeof(VAAPIDecodeContext),
    .caps_internal        = HWACCEL_CAP_ASYNC_SAFE,
};
