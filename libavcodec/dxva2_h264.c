/*
 * DXVA2 H264 HW acceleration.
 *
 * copyright (c) 2009 Laurent Aimar
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

#include "dxva2_internal.h"
#include "h264.h"
#include "h264data.h"

struct dxva2_picture_context {
    DXVA_PicParams_H264   pp;
    DXVA_Qmatrix_H264     qm;
    unsigned              slice_count;
    DXVA_Slice_H264_Short slice_short[MAX_SLICES];
    DXVA_Slice_H264_Long  slice_long[MAX_SLICES];
    const uint8_t         *bitstream;
    unsigned              bitstream_size;
};

static void fill_picture_entry(DXVA_PicEntry_H264 *pic,
                               unsigned index, unsigned flag)
{
    assert((index&0x7f) == index && (flag&0x01) == flag);
    pic->bPicEntry = index | (flag << 7);
}

static void fill_picture_parameters(struct dxva_context *ctx, const H264Context *h,
                                    DXVA_PicParams_H264 *pp)
{
    const MpegEncContext *s = &h->s;
    const Picture *current_picture = s->current_picture_ptr;
    int i, j;

    memset(pp, 0, sizeof(*pp));
    /* Configure current picture */
    fill_picture_entry(&pp->CurrPic,
                       ff_dxva2_get_surface_index(ctx, current_picture),
                       s->picture_structure == PICT_BOTTOM_FIELD);
    /* Configure the set of references */
    pp->UsedForReferenceFlags  = 0;
    pp->NonExistingFrameFlags  = 0;
    for (i = 0, j = 0; i < FF_ARRAY_ELEMS(pp->RefFrameList); i++) {
        const Picture *r;
        if (j < h->short_ref_count) {
            r = h->short_ref[j++];
        } else {
            r = NULL;
            while (!r && j < h->short_ref_count + 16)
                r = h->long_ref[j++ - h->short_ref_count];
        }
        if (r) {
            fill_picture_entry(&pp->RefFrameList[i],
                               ff_dxva2_get_surface_index(ctx, r),
                               r->long_ref != 0);

            if ((r->reference & PICT_TOP_FIELD) && r->field_poc[0] != INT_MAX)
                pp->FieldOrderCntList[i][0] = r->field_poc[0];
            if ((r->reference & PICT_BOTTOM_FIELD) && r->field_poc[1] != INT_MAX)
                pp->FieldOrderCntList[i][1] = r->field_poc[1];

            pp->FrameNumList[i] = r->long_ref ? r->pic_id : r->frame_num;
            if (r->reference & PICT_TOP_FIELD)
                pp->UsedForReferenceFlags |= 1 << (2*i + 0);
            if (r->reference & PICT_BOTTOM_FIELD)
                pp->UsedForReferenceFlags |= 1 << (2*i + 1);
        } else {
            pp->RefFrameList[i].bPicEntry = 0xff;
            pp->FieldOrderCntList[i][0]   = 0;
            pp->FieldOrderCntList[i][1]   = 0;
            pp->FrameNumList[i]           = 0;
        }
    }

    pp->wFrameWidthInMbsMinus1        = s->mb_width  - 1;
    pp->wFrameHeightInMbsMinus1       = s->mb_height - 1;
    pp->num_ref_frames                = h->sps.ref_frame_count;

    pp->wBitFields                    = ((s->picture_structure != PICT_FRAME) <<  0) |
                                        (h->sps.mb_aff                        <<  1) |
                                        (h->sps.residual_color_transform_flag <<  2) |
                                        /* sp_for_switch_flag (not implemented by FFmpeg) */
                                        (0                                    <<  3) |
                                        (h->sps.chroma_format_idc             <<  4) |
                                        ((h->nal_ref_idc != 0)                <<  6) |
                                        (h->pps.constrained_intra_pred        <<  7) |
                                        (h->pps.weighted_pred                 <<  8) |
                                        (h->pps.weighted_bipred_idc           <<  9) |
                                        /* MbsConsecutiveFlag */
                                        (1                                    << 11) |
                                        (h->sps.frame_mbs_only_flag           << 12) |
                                        (h->pps.transform_8x8_mode            << 13) |
                                        ((h->sps.level_idc >= 31)             << 14) |
                                        /* IntraPicFlag (Modified if we detect a non
                                         * intra slice in decode_slice) */
                                        (1                                    << 15);

    pp->bit_depth_luma_minus8         = h->sps.bit_depth_luma - 8;
    pp->bit_depth_chroma_minus8       = h->sps.bit_depth_chroma - 8;
    pp->Reserved16Bits                = 3; /* FIXME is there a way to detect the right mode ? */
    pp->StatusReportFeedbackNumber    = 1 + ctx->report_id++;
    pp->CurrFieldOrderCnt[0] = 0;
    if ((s->picture_structure & PICT_TOP_FIELD) &&
        current_picture->field_poc[0] != INT_MAX)
        pp->CurrFieldOrderCnt[0] = current_picture->field_poc[0];
    pp->CurrFieldOrderCnt[1] = 0;
    if ((s->picture_structure & PICT_BOTTOM_FIELD) &&
        current_picture->field_poc[1] != INT_MAX)
        pp->CurrFieldOrderCnt[1] = current_picture->field_poc[1];
    pp->pic_init_qs_minus26           = h->pps.init_qs - 26;
    pp->chroma_qp_index_offset        = h->pps.chroma_qp_index_offset[0];
    pp->second_chroma_qp_index_offset = h->pps.chroma_qp_index_offset[1];
    pp->ContinuationFlag              = 1;
    pp->pic_init_qp_minus26           = h->pps.init_qp - 26;
    pp->num_ref_idx_l0_active_minus1  = h->pps.ref_count[0] - 1;
    pp->num_ref_idx_l1_active_minus1  = h->pps.ref_count[1] - 1;
    pp->Reserved8BitsA                = 0;
    pp->frame_num                     = h->frame_num;
    pp->log2_max_frame_num_minus4     = h->sps.log2_max_frame_num - 4;
    pp->pic_order_cnt_type            = h->sps.poc_type;
    if (h->sps.poc_type == 0)
        pp->log2_max_pic_order_cnt_lsb_minus4 = h->sps.log2_max_poc_lsb - 4;
    else if (h->sps.poc_type == 1)
        pp->delta_pic_order_always_zero_flag = h->sps.delta_pic_order_always_zero_flag;
    pp->direct_8x8_inference_flag     = h->sps.direct_8x8_inference_flag;
    pp->entropy_coding_mode_flag      = h->pps.cabac;
    pp->pic_order_present_flag        = h->pps.pic_order_present;
    pp->num_slice_groups_minus1       = h->pps.slice_group_count - 1;
    pp->slice_group_map_type          = h->pps.mb_slice_group_map_type;
    pp->deblocking_filter_control_present_flag = h->pps.deblocking_filter_parameters_present;
    pp->redundant_pic_cnt_present_flag= h->pps.redundant_pic_cnt_present;
    pp->Reserved8BitsB                = 0;
    pp->slice_group_change_rate_minus1= 0;  /* XXX not implemented by FFmpeg */
    //pp->SliceGroupMap[810];               /* XXX not implemented by FFmpeg */
}

static void fill_scaling_lists(const H264Context *h, DXVA_Qmatrix_H264 *qm)
{
    unsigned i, j;
    memset(qm, 0, sizeof(*qm));
    for (i = 0; i < 6; i++)
        for (j = 0; j < 16; j++)
            qm->bScalingLists4x4[i][j] = h->pps.scaling_matrix4[i][zigzag_scan[j]];

    for (j = 0; j < 64; j++) {
        qm->bScalingLists8x8[0][j] = h->pps.scaling_matrix8[0][ff_zigzag_direct[j]];
        qm->bScalingLists8x8[1][j] = h->pps.scaling_matrix8[3][ff_zigzag_direct[j]];
    }
}

static int is_slice_short(struct dxva_context *ctx)
{
    assert(ctx->cfg->ConfigBitstreamRaw == 1 ||
           ctx->cfg->ConfigBitstreamRaw == 2);
    return ctx->cfg->ConfigBitstreamRaw == 2;
}

static void fill_slice_short(DXVA_Slice_H264_Short *slice,
                             unsigned position, unsigned size)
{
    memset(slice, 0, sizeof(*slice));
    slice->BSNALunitDataLocation = position;
    slice->SliceBytesInBuffer    = size;
    slice->wBadSliceChopping     = 0;
}

static void fill_slice_long(AVCodecContext *avctx, DXVA_Slice_H264_Long *slice,
                            unsigned position, unsigned size)
{
    const H264Context *h = avctx->priv_data;
    struct dxva_context *ctx = avctx->hwaccel_context;
    const MpegEncContext *s = &h->s;
    unsigned list;

    memset(slice, 0, sizeof(*slice));
    slice->BSNALunitDataLocation = position;
    slice->SliceBytesInBuffer    = size;
    slice->wBadSliceChopping     = 0;

    slice->first_mb_in_slice     = (s->mb_y >> FIELD_OR_MBAFF_PICTURE) * s->mb_width + s->mb_x;
    slice->NumMbsForSlice        = 0; /* XXX it is set once we have all slices */
    slice->BitOffsetToSliceData  = get_bits_count(&s->gb);
    slice->slice_type            = ff_h264_get_slice_type(h);
    if (h->slice_type_fixed)
        slice->slice_type += 5;
    slice->luma_log2_weight_denom       = h->luma_log2_weight_denom;
    slice->chroma_log2_weight_denom     = h->chroma_log2_weight_denom;
    if (h->list_count > 0)
        slice->num_ref_idx_l0_active_minus1 = h->ref_count[0] - 1;
    if (h->list_count > 1)
        slice->num_ref_idx_l1_active_minus1 = h->ref_count[1] - 1;
    slice->slice_alpha_c0_offset_div2   = h->slice_alpha_c0_offset / 2 - 26;
    slice->slice_beta_offset_div2       = h->slice_beta_offset     / 2 - 26;
    slice->Reserved8Bits                = 0;

    for (list = 0; list < 2; list++) {
        unsigned i;
        for (i = 0; i < FF_ARRAY_ELEMS(slice->RefPicList[list]); i++) {
            if (list < h->list_count && i < h->ref_count[list]) {
                const Picture *r = &h->ref_list[list][i];
                unsigned plane;
                fill_picture_entry(&slice->RefPicList[list][i],
                                   ff_dxva2_get_surface_index(ctx, r),
                                   r->reference == PICT_BOTTOM_FIELD);
                for (plane = 0; plane < 3; plane++) {
                    int w, o;
                    if (plane == 0 && h->luma_weight_flag[list]) {
                        w = h->luma_weight[i][list][0];
                        o = h->luma_weight[i][list][1];
                    } else if (plane >= 1 && h->chroma_weight_flag[list]) {
                        w = h->chroma_weight[i][list][plane-1][0];
                        o = h->chroma_weight[i][list][plane-1][1];
                    } else {
                        w = 1 << (plane == 0 ? h->luma_log2_weight_denom :
                                               h->chroma_log2_weight_denom);
                        o = 0;
                    }
                    slice->Weights[list][i][plane][0] = w;
                    slice->Weights[list][i][plane][1] = o;
                }
            } else {
                unsigned plane;
                slice->RefPicList[list][i].bPicEntry = 0xff;
                for (plane = 0; plane < 3; plane++) {
                    slice->Weights[list][i][plane][0] = 0;
                    slice->Weights[list][i][plane][1] = 0;
                }
            }
        }
    }
    slice->slice_qs_delta    = 0; /* XXX not implemented by FFmpeg */
    slice->slice_qp_delta    = s->qscale - h->pps.init_qp;
    slice->redundant_pic_cnt = h->redundant_pic_count;
    if (h->slice_type == AV_PICTURE_TYPE_B)
        slice->direct_spatial_mv_pred_flag = h->direct_spatial_mv_pred;
    slice->cabac_init_idc = h->pps.cabac ? h->cabac_init_idc : 0;
    if (h->deblocking_filter < 2)
        slice->disable_deblocking_filter_idc = 1 - h->deblocking_filter;
    else
        slice->disable_deblocking_filter_idc = h->deblocking_filter;
    slice->slice_id = h->current_slice - 1;
}

static int commit_bitstream_and_slice_buffer(AVCodecContext *avctx,
                                             DXVA2_DecodeBufferDesc *bs,
                                             DXVA2_DecodeBufferDesc *sc)
{
    const H264Context *h = avctx->priv_data;
    const MpegEncContext *s = &h->s;
    const unsigned mb_count = s->mb_width * s->mb_height;
    struct dxva_context *ctx = avctx->hwaccel_context;
    const Picture *current_picture = h->s.current_picture_ptr;
    struct dxva2_picture_context *ctx_pic = current_picture->hwaccel_picture_private;
    DXVA_Slice_H264_Short *slice = NULL;
    uint8_t  *dxva_data, *current, *end;
    unsigned dxva_size;
    void     *slice_data;
    unsigned slice_size;
    unsigned padding;
    unsigned i;

    /* Create an annex B bitstream buffer with only slice NAL and finalize slice */
    if (FAILED(IDirectXVideoDecoder_GetBuffer(ctx->decoder,
                                               DXVA2_BitStreamDateBufferType,
                                               &dxva_data, &dxva_size)))
        return -1;
    current = dxva_data;
    end = dxva_data + dxva_size;

    for (i = 0; i < ctx_pic->slice_count; i++) {
        static const uint8_t start_code[] = { 0, 0, 1 };
        static const unsigned start_code_size = sizeof(start_code);
        unsigned position, size;

        assert(offsetof(DXVA_Slice_H264_Short, BSNALunitDataLocation) ==
               offsetof(DXVA_Slice_H264_Long,  BSNALunitDataLocation));
        assert(offsetof(DXVA_Slice_H264_Short, SliceBytesInBuffer) ==
               offsetof(DXVA_Slice_H264_Long,  SliceBytesInBuffer));

        if (is_slice_short(ctx))
            slice = &ctx_pic->slice_short[i];
        else
            slice = (DXVA_Slice_H264_Short*)&ctx_pic->slice_long[i];

        position = slice->BSNALunitDataLocation;
        size     = slice->SliceBytesInBuffer;
        if (start_code_size + size > end - current) {
            av_log(avctx, AV_LOG_ERROR, "Failed to build bitstream");
            break;
        }

        slice->BSNALunitDataLocation = current - dxva_data;
        slice->SliceBytesInBuffer    = start_code_size + size;

        if (!is_slice_short(ctx)) {
            DXVA_Slice_H264_Long *slice_long = (DXVA_Slice_H264_Long*)slice;
            if (i < ctx_pic->slice_count - 1)
                slice_long->NumMbsForSlice =
                    slice_long[1].first_mb_in_slice - slice_long[0].first_mb_in_slice;
            else
                slice_long->NumMbsForSlice = mb_count - slice_long->first_mb_in_slice;
        }

        memcpy(current, start_code, start_code_size);
        current += start_code_size;

        memcpy(current, &ctx_pic->bitstream[position], size);
        current += size;
    }
    padding = FFMIN(128 - ((current - dxva_data) & 127), end - current);
    if (slice && padding > 0) {
        memset(current, 0, padding);
        current += padding;

        slice->SliceBytesInBuffer += padding;
    }
    if (FAILED(IDirectXVideoDecoder_ReleaseBuffer(ctx->decoder,
                                                  DXVA2_BitStreamDateBufferType)))
        return -1;
    if (i < ctx_pic->slice_count)
        return -1;

    memset(bs, 0, sizeof(*bs));
    bs->CompressedBufferType = DXVA2_BitStreamDateBufferType;
    bs->DataSize             = current - dxva_data;
    bs->NumMBsInBuffer       = mb_count;

    if (is_slice_short(ctx)) {
        slice_data = ctx_pic->slice_short;
        slice_size = ctx_pic->slice_count * sizeof(*ctx_pic->slice_short);
    } else {
        slice_data = ctx_pic->slice_long;
        slice_size = ctx_pic->slice_count * sizeof(*ctx_pic->slice_long);
    }
    assert((bs->DataSize & 127) == 0);
    return ff_dxva2_commit_buffer(avctx, ctx, sc,
                                  DXVA2_SliceControlBufferType,
                                  slice_data, slice_size, mb_count);
}


static int start_frame(AVCodecContext *avctx,
                       av_unused const uint8_t *buffer,
                       av_unused uint32_t size)
{
    const H264Context *h = avctx->priv_data;
    struct dxva_context *ctx = avctx->hwaccel_context;
    struct dxva2_picture_context *ctx_pic = h->s.current_picture_ptr->hwaccel_picture_private;

    if (!ctx->decoder || !ctx->cfg || ctx->surface_count <= 0)
        return -1;
    assert(ctx_pic);

    /* Fill up DXVA_PicParams_H264 */
    fill_picture_parameters(ctx, h, &ctx_pic->pp);

    /* Fill up DXVA_Qmatrix_H264 */
    fill_scaling_lists(h, &ctx_pic->qm);

    ctx_pic->slice_count    = 0;
    ctx_pic->bitstream_size = 0;
    ctx_pic->bitstream      = NULL;
    return 0;
}

static int decode_slice(AVCodecContext *avctx,
                        const uint8_t *buffer, uint32_t size)
{
    const H264Context *h = avctx->priv_data;
    struct dxva_context *ctx = avctx->hwaccel_context;
    const Picture *current_picture = h->s.current_picture_ptr;
    struct dxva2_picture_context *ctx_pic = current_picture->hwaccel_picture_private;
    unsigned position;

    if (ctx_pic->slice_count >= MAX_SLICES)
        return -1;

    if (!ctx_pic->bitstream)
        ctx_pic->bitstream = buffer;
    ctx_pic->bitstream_size += size;

    position = buffer - ctx_pic->bitstream;
    if (is_slice_short(ctx))
        fill_slice_short(&ctx_pic->slice_short[ctx_pic->slice_count],
                         position, size);
    else
        fill_slice_long(avctx, &ctx_pic->slice_long[ctx_pic->slice_count],
                        position, size);
    ctx_pic->slice_count++;

    if (h->slice_type != AV_PICTURE_TYPE_I && h->slice_type != AV_PICTURE_TYPE_SI)
        ctx_pic->pp.wBitFields &= ~(1 << 15); /* Set IntraPicFlag to 0 */
    return 0;
}

static int end_frame(AVCodecContext *avctx)
{
    H264Context *h = avctx->priv_data;
    MpegEncContext *s = &h->s;
    struct dxva2_picture_context *ctx_pic =
        h->s.current_picture_ptr->hwaccel_picture_private;

    if (ctx_pic->slice_count <= 0 || ctx_pic->bitstream_size <= 0)
        return -1;
    return ff_dxva2_common_end_frame(avctx, s,
                                     &ctx_pic->pp, sizeof(ctx_pic->pp),
                                     &ctx_pic->qm, sizeof(ctx_pic->qm),
                                     commit_bitstream_and_slice_buffer);
}

AVHWAccel ff_h264_dxva2_hwaccel = {
    .name           = "h264_dxva2",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_H264,
    .pix_fmt        = PIX_FMT_DXVA2_VLD,
    .capabilities   = 0,
    .start_frame    = start_frame,
    .decode_slice   = decode_slice,
    .end_frame      = end_frame,
    .priv_data_size = sizeof(struct dxva2_picture_context),
};

