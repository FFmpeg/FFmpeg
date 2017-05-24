/*
 * DXVA2 H.264 HW acceleration.
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

#include "libavutil/avassert.h"

#include "h264dec.h"
#include "h264data.h"
#include "h264_ps.h"
#include "mpegutils.h"

// The headers above may include w32threads.h, which uses the original
// _WIN32_WINNT define, while dxva2_internal.h redefines it to target a
// potentially newer version.
#include "dxva2_internal.h"

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

static void fill_picture_parameters(const AVCodecContext *avctx, AVDXVAContext *ctx, const H264Context *h,
                                    DXVA_PicParams_H264 *pp)
{
    const H264Picture *current_picture = h->cur_pic_ptr;
    const SPS *sps = h->ps.sps;
    const PPS *pps = h->ps.pps;
    int i, j;

    memset(pp, 0, sizeof(*pp));
    /* Configure current picture */
    fill_picture_entry(&pp->CurrPic,
                       ff_dxva2_get_surface_index(avctx, ctx, current_picture->f),
                       h->picture_structure == PICT_BOTTOM_FIELD);
    /* Configure the set of references */
    pp->UsedForReferenceFlags  = 0;
    pp->NonExistingFrameFlags  = 0;
    for (i = 0, j = 0; i < FF_ARRAY_ELEMS(pp->RefFrameList); i++) {
        const H264Picture *r;
        if (j < h->short_ref_count) {
            r = h->short_ref[j++];
        } else {
            r = NULL;
            while (!r && j < h->short_ref_count + 16)
                r = h->long_ref[j++ - h->short_ref_count];
        }
        if (r) {
            fill_picture_entry(&pp->RefFrameList[i],
                               ff_dxva2_get_surface_index(avctx, ctx, r->f),
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

    pp->wFrameWidthInMbsMinus1        = h->mb_width  - 1;
    pp->wFrameHeightInMbsMinus1       = h->mb_height - 1;
    pp->num_ref_frames                = sps->ref_frame_count;

    pp->wBitFields                    = ((h->picture_structure != PICT_FRAME) <<  0) |
                                        ((sps->mb_aff &&
                                        (h->picture_structure == PICT_FRAME)) <<  1) |
                                        (sps->residual_color_transform_flag   <<  2) |
                                        /* sp_for_switch_flag (not implemented by FFmpeg) */
                                        (0                                    <<  3) |
                                        (sps->chroma_format_idc               <<  4) |
                                        ((h->nal_ref_idc != 0)                <<  6) |
                                        (pps->constrained_intra_pred          <<  7) |
                                        (pps->weighted_pred                   <<  8) |
                                        (pps->weighted_bipred_idc             <<  9) |
                                        /* MbsConsecutiveFlag */
                                        (1                                    << 11) |
                                        (sps->frame_mbs_only_flag             << 12) |
                                        (pps->transform_8x8_mode              << 13) |
                                        ((sps->level_idc >= 31)               << 14) |
                                        /* IntraPicFlag (Modified if we detect a non
                                         * intra slice in dxva2_h264_decode_slice) */
                                        (1                                    << 15);

    pp->bit_depth_luma_minus8         = sps->bit_depth_luma - 8;
    pp->bit_depth_chroma_minus8       = sps->bit_depth_chroma - 8;
    if (DXVA_CONTEXT_WORKAROUND(avctx, ctx) & FF_DXVA2_WORKAROUND_SCALING_LIST_ZIGZAG)
        pp->Reserved16Bits            = 0;
    else if (DXVA_CONTEXT_WORKAROUND(avctx, ctx) & FF_DXVA2_WORKAROUND_INTEL_CLEARVIDEO)
        pp->Reserved16Bits            = 0x34c;
    else
        pp->Reserved16Bits            = 3; /* FIXME is there a way to detect the right mode ? */
    pp->StatusReportFeedbackNumber    = 1 + DXVA_CONTEXT_REPORT_ID(avctx, ctx)++;
    pp->CurrFieldOrderCnt[0] = 0;
    if ((h->picture_structure & PICT_TOP_FIELD) &&
        current_picture->field_poc[0] != INT_MAX)
        pp->CurrFieldOrderCnt[0] = current_picture->field_poc[0];
    pp->CurrFieldOrderCnt[1] = 0;
    if ((h->picture_structure & PICT_BOTTOM_FIELD) &&
        current_picture->field_poc[1] != INT_MAX)
        pp->CurrFieldOrderCnt[1] = current_picture->field_poc[1];
    pp->pic_init_qs_minus26           = pps->init_qs - 26;
    pp->chroma_qp_index_offset        = pps->chroma_qp_index_offset[0];
    pp->second_chroma_qp_index_offset = pps->chroma_qp_index_offset[1];
    pp->ContinuationFlag              = 1;
    pp->pic_init_qp_minus26           = pps->init_qp - 26;
    pp->num_ref_idx_l0_active_minus1  = pps->ref_count[0] - 1;
    pp->num_ref_idx_l1_active_minus1  = pps->ref_count[1] - 1;
    pp->Reserved8BitsA                = 0;
    pp->frame_num                     = h->poc.frame_num;
    pp->log2_max_frame_num_minus4     = sps->log2_max_frame_num - 4;
    pp->pic_order_cnt_type            = sps->poc_type;
    if (sps->poc_type == 0)
        pp->log2_max_pic_order_cnt_lsb_minus4 = sps->log2_max_poc_lsb - 4;
    else if (sps->poc_type == 1)
        pp->delta_pic_order_always_zero_flag = sps->delta_pic_order_always_zero_flag;
    pp->direct_8x8_inference_flag     = sps->direct_8x8_inference_flag;
    pp->entropy_coding_mode_flag      = pps->cabac;
    pp->pic_order_present_flag        = pps->pic_order_present;
    pp->num_slice_groups_minus1       = pps->slice_group_count - 1;
    pp->slice_group_map_type          = pps->mb_slice_group_map_type;
    pp->deblocking_filter_control_present_flag = pps->deblocking_filter_parameters_present;
    pp->redundant_pic_cnt_present_flag= pps->redundant_pic_cnt_present;
    pp->Reserved8BitsB                = 0;
    pp->slice_group_change_rate_minus1= 0;  /* XXX not implemented by FFmpeg */
    //pp->SliceGroupMap[810];               /* XXX not implemented by FFmpeg */
}

static void fill_scaling_lists(const AVCodecContext *avctx, AVDXVAContext *ctx, const H264Context *h, DXVA_Qmatrix_H264 *qm)
{
    const PPS *pps = h->ps.pps;
    unsigned i, j;
    memset(qm, 0, sizeof(*qm));
    if (DXVA_CONTEXT_WORKAROUND(avctx, ctx) & FF_DXVA2_WORKAROUND_SCALING_LIST_ZIGZAG) {
        for (i = 0; i < 6; i++)
            for (j = 0; j < 16; j++)
                qm->bScalingLists4x4[i][j] = pps->scaling_matrix4[i][j];

        for (i = 0; i < 64; i++) {
            qm->bScalingLists8x8[0][i] = pps->scaling_matrix8[0][i];
            qm->bScalingLists8x8[1][i] = pps->scaling_matrix8[3][i];
        }
    } else {
        for (i = 0; i < 6; i++)
            for (j = 0; j < 16; j++)
                qm->bScalingLists4x4[i][j] = pps->scaling_matrix4[i][ff_zigzag_scan[j]];

        for (i = 0; i < 64; i++) {
            qm->bScalingLists8x8[0][i] = pps->scaling_matrix8[0][ff_zigzag_direct[i]];
            qm->bScalingLists8x8[1][i] = pps->scaling_matrix8[3][ff_zigzag_direct[i]];
        }
    }
}

static int is_slice_short(const AVCodecContext *avctx, AVDXVAContext *ctx)
{
    assert(DXVA_CONTEXT_CFG_BITSTREAM(avctx, ctx) == 1 ||
           DXVA_CONTEXT_CFG_BITSTREAM(avctx, ctx) == 2);
    return DXVA_CONTEXT_CFG_BITSTREAM(avctx, ctx) == 2;
}

static void fill_slice_short(DXVA_Slice_H264_Short *slice,
                             unsigned position, unsigned size)
{
    memset(slice, 0, sizeof(*slice));
    slice->BSNALunitDataLocation = position;
    slice->SliceBytesInBuffer    = size;
    slice->wBadSliceChopping     = 0;
}

static int get_refpic_index(const DXVA_PicParams_H264 *pp, int surface_index)
{
    int i;
    for (i = 0; i < FF_ARRAY_ELEMS(pp->RefFrameList); i++) {
        if ((pp->RefFrameList[i].bPicEntry & 0x7f) == surface_index)
          return i;
    }
    return 0x7f;
}

static void fill_slice_long(AVCodecContext *avctx, DXVA_Slice_H264_Long *slice,
                            const DXVA_PicParams_H264 *pp, unsigned position, unsigned size)
{
    const H264Context *h = avctx->priv_data;
    H264SliceContext *sl = &h->slice_ctx[0];
    AVDXVAContext *ctx = avctx->hwaccel_context;
    unsigned list;

    memset(slice, 0, sizeof(*slice));
    slice->BSNALunitDataLocation = position;
    slice->SliceBytesInBuffer    = size;
    slice->wBadSliceChopping     = 0;

    slice->first_mb_in_slice     = (sl->mb_y >> FIELD_OR_MBAFF_PICTURE(h)) * h->mb_width + sl->mb_x;
    slice->NumMbsForSlice        = 0; /* XXX it is set once we have all slices */
    slice->BitOffsetToSliceData  = get_bits_count(&sl->gb) - 8;
    slice->slice_type            = ff_h264_get_slice_type(sl);
    if (sl->slice_type_fixed)
        slice->slice_type += 5;
    slice->luma_log2_weight_denom       = sl->pwt.luma_log2_weight_denom;
    slice->chroma_log2_weight_denom     = sl->pwt.chroma_log2_weight_denom;
    if (sl->list_count > 0)
        slice->num_ref_idx_l0_active_minus1 = sl->ref_count[0] - 1;
    if (sl->list_count > 1)
        slice->num_ref_idx_l1_active_minus1 = sl->ref_count[1] - 1;
    slice->slice_alpha_c0_offset_div2   = sl->slice_alpha_c0_offset / 2;
    slice->slice_beta_offset_div2       = sl->slice_beta_offset     / 2;
    slice->Reserved8Bits                = 0;

    for (list = 0; list < 2; list++) {
        unsigned i;
        for (i = 0; i < FF_ARRAY_ELEMS(slice->RefPicList[list]); i++) {
            if (list < sl->list_count && i < sl->ref_count[list]) {
                const H264Picture *r = sl->ref_list[list][i].parent;
                unsigned plane;
                unsigned index;
                if (DXVA_CONTEXT_WORKAROUND(avctx, ctx) & FF_DXVA2_WORKAROUND_INTEL_CLEARVIDEO)
                    index = ff_dxva2_get_surface_index(avctx, ctx, r->f);
                else
                    index = get_refpic_index(pp, ff_dxva2_get_surface_index(avctx, ctx, r->f));
                fill_picture_entry(&slice->RefPicList[list][i], index,
                                   sl->ref_list[list][i].reference == PICT_BOTTOM_FIELD);
                for (plane = 0; plane < 3; plane++) {
                    int w, o;
                    if (plane == 0 && sl->pwt.luma_weight_flag[list]) {
                        w = sl->pwt.luma_weight[i][list][0];
                        o = sl->pwt.luma_weight[i][list][1];
                    } else if (plane >= 1 && sl->pwt.chroma_weight_flag[list]) {
                        w = sl->pwt.chroma_weight[i][list][plane-1][0];
                        o = sl->pwt.chroma_weight[i][list][plane-1][1];
                    } else {
                        w = 1 << (plane == 0 ? sl->pwt.luma_log2_weight_denom :
                                               sl->pwt.chroma_log2_weight_denom);
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
    slice->slice_qp_delta    = sl->qscale - h->ps.pps->init_qp;
    slice->redundant_pic_cnt = sl->redundant_pic_count;
    if (sl->slice_type == AV_PICTURE_TYPE_B)
        slice->direct_spatial_mv_pred_flag = sl->direct_spatial_mv_pred;
    slice->cabac_init_idc = h->ps.pps->cabac ? sl->cabac_init_idc : 0;
    if (sl->deblocking_filter < 2)
        slice->disable_deblocking_filter_idc = 1 - sl->deblocking_filter;
    else
        slice->disable_deblocking_filter_idc = sl->deblocking_filter;
    slice->slice_id = h->current_slice - 1;
}

static int commit_bitstream_and_slice_buffer(AVCodecContext *avctx,
                                             DECODER_BUFFER_DESC *bs,
                                             DECODER_BUFFER_DESC *sc)
{
    const H264Context *h = avctx->priv_data;
    const unsigned mb_count = h->mb_width * h->mb_height;
    AVDXVAContext *ctx = avctx->hwaccel_context;
    const H264Picture *current_picture = h->cur_pic_ptr;
    struct dxva2_picture_context *ctx_pic = current_picture->hwaccel_picture_private;
    DXVA_Slice_H264_Short *slice = NULL;
    void     *dxva_data_ptr = NULL;
    uint8_t  *dxva_data, *current, *end;
    unsigned dxva_size = 0;
    void     *slice_data;
    unsigned slice_size;
    unsigned padding;
    unsigned i;
    unsigned type;

    /* Create an annex B bitstream buffer with only slice NAL and finalize slice */
#if CONFIG_D3D11VA
    if (avctx->pix_fmt == AV_PIX_FMT_D3D11VA_VLD) {
        type = D3D11_VIDEO_DECODER_BUFFER_BITSTREAM;
        if (FAILED(ID3D11VideoContext_GetDecoderBuffer(D3D11VA_CONTEXT(ctx)->video_context,
                                                       D3D11VA_CONTEXT(ctx)->decoder,
                                                       type,
                                                       &dxva_size, &dxva_data_ptr)))
            return -1;
    }
#endif
#if CONFIG_DXVA2
    if (avctx->pix_fmt == AV_PIX_FMT_DXVA2_VLD) {
        type = DXVA2_BitStreamDateBufferType;
        if (FAILED(IDirectXVideoDecoder_GetBuffer(DXVA2_CONTEXT(ctx)->decoder,
                                                  type,
                                                  &dxva_data_ptr, &dxva_size)))
            return -1;
    }
#endif

    dxva_data = dxva_data_ptr;
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

        if (is_slice_short(avctx, ctx))
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

        if (!is_slice_short(avctx, ctx)) {
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
#if CONFIG_D3D11VA
    if (avctx->pix_fmt == AV_PIX_FMT_D3D11VA_VLD)
        if (FAILED(ID3D11VideoContext_ReleaseDecoderBuffer(D3D11VA_CONTEXT(ctx)->video_context, D3D11VA_CONTEXT(ctx)->decoder, type)))
            return -1;
#endif
#if CONFIG_DXVA2
    if (avctx->pix_fmt == AV_PIX_FMT_DXVA2_VLD)
        if (FAILED(IDirectXVideoDecoder_ReleaseBuffer(DXVA2_CONTEXT(ctx)->decoder, type)))
            return -1;
#endif
    if (i < ctx_pic->slice_count)
        return -1;

#if CONFIG_D3D11VA
    if (avctx->pix_fmt == AV_PIX_FMT_D3D11VA_VLD) {
        D3D11_VIDEO_DECODER_BUFFER_DESC *dsc11 = bs;
        memset(dsc11, 0, sizeof(*dsc11));
        dsc11->BufferType           = type;
        dsc11->DataSize             = current - dxva_data;
        dsc11->NumMBsInBuffer       = mb_count;

        type = D3D11_VIDEO_DECODER_BUFFER_SLICE_CONTROL;

        av_assert0((dsc11->DataSize & 127) == 0);
    }
#endif
#if CONFIG_DXVA2
    if (avctx->pix_fmt == AV_PIX_FMT_DXVA2_VLD) {
        DXVA2_DecodeBufferDesc *dsc2 = bs;
        memset(dsc2, 0, sizeof(*dsc2));
        dsc2->CompressedBufferType = type;
        dsc2->DataSize             = current - dxva_data;
        dsc2->NumMBsInBuffer       = mb_count;

        type = DXVA2_SliceControlBufferType;

        av_assert0((dsc2->DataSize & 127) == 0);
    }
#endif

    if (is_slice_short(avctx, ctx)) {
        slice_data = ctx_pic->slice_short;
        slice_size = ctx_pic->slice_count * sizeof(*ctx_pic->slice_short);
    } else {
        slice_data = ctx_pic->slice_long;
        slice_size = ctx_pic->slice_count * sizeof(*ctx_pic->slice_long);
    }
    return ff_dxva2_commit_buffer(avctx, ctx, sc,
                                  type,
                                  slice_data, slice_size, mb_count);
}


static int dxva2_h264_start_frame(AVCodecContext *avctx,
                                  av_unused const uint8_t *buffer,
                                  av_unused uint32_t size)
{
    const H264Context *h = avctx->priv_data;
    AVDXVAContext *ctx = avctx->hwaccel_context;
    struct dxva2_picture_context *ctx_pic = h->cur_pic_ptr->hwaccel_picture_private;

    if (!DXVA_CONTEXT_VALID(avctx, ctx))
        return -1;
    assert(ctx_pic);

    /* Fill up DXVA_PicParams_H264 */
    fill_picture_parameters(avctx, ctx, h, &ctx_pic->pp);

    /* Fill up DXVA_Qmatrix_H264 */
    fill_scaling_lists(avctx, ctx, h, &ctx_pic->qm);

    ctx_pic->slice_count    = 0;
    ctx_pic->bitstream_size = 0;
    ctx_pic->bitstream      = NULL;
    return 0;
}

static int dxva2_h264_decode_slice(AVCodecContext *avctx,
                                   const uint8_t *buffer,
                                   uint32_t size)
{
    const H264Context *h = avctx->priv_data;
    const H264SliceContext *sl = &h->slice_ctx[0];
    AVDXVAContext *ctx = avctx->hwaccel_context;
    const H264Picture *current_picture = h->cur_pic_ptr;
    struct dxva2_picture_context *ctx_pic = current_picture->hwaccel_picture_private;
    unsigned position;

    if (ctx_pic->slice_count >= MAX_SLICES)
        return -1;

    if (!ctx_pic->bitstream)
        ctx_pic->bitstream = buffer;
    ctx_pic->bitstream_size += size;

    position = buffer - ctx_pic->bitstream;
    if (is_slice_short(avctx, ctx))
        fill_slice_short(&ctx_pic->slice_short[ctx_pic->slice_count],
                         position, size);
    else
        fill_slice_long(avctx, &ctx_pic->slice_long[ctx_pic->slice_count],
                        &ctx_pic->pp, position, size);
    ctx_pic->slice_count++;

    if (sl->slice_type != AV_PICTURE_TYPE_I && sl->slice_type != AV_PICTURE_TYPE_SI)
        ctx_pic->pp.wBitFields &= ~(1 << 15); /* Set IntraPicFlag to 0 */
    return 0;
}

static int dxva2_h264_end_frame(AVCodecContext *avctx)
{
    H264Context *h = avctx->priv_data;
    H264SliceContext *sl = &h->slice_ctx[0];
    struct dxva2_picture_context *ctx_pic =
        h->cur_pic_ptr->hwaccel_picture_private;
    int ret;

    if (ctx_pic->slice_count <= 0 || ctx_pic->bitstream_size <= 0)
        return -1;
    ret = ff_dxva2_common_end_frame(avctx, h->cur_pic_ptr->f,
                                    &ctx_pic->pp, sizeof(ctx_pic->pp),
                                    &ctx_pic->qm, sizeof(ctx_pic->qm),
                                    commit_bitstream_and_slice_buffer);
    if (!ret)
        ff_h264_draw_horiz_band(h, sl, 0, h->avctx->height);
    return ret;
}

#if CONFIG_H264_DXVA2_HWACCEL
AVHWAccel ff_h264_dxva2_hwaccel = {
    .name           = "h264_dxva2",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_H264,
    .pix_fmt        = AV_PIX_FMT_DXVA2_VLD,
    .start_frame    = dxva2_h264_start_frame,
    .decode_slice   = dxva2_h264_decode_slice,
    .end_frame      = dxva2_h264_end_frame,
    .frame_priv_data_size = sizeof(struct dxva2_picture_context),
};
#endif

#if CONFIG_H264_D3D11VA_HWACCEL
AVHWAccel ff_h264_d3d11va_hwaccel = {
    .name           = "h264_d3d11va",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_H264,
    .pix_fmt        = AV_PIX_FMT_D3D11VA_VLD,
    .start_frame    = dxva2_h264_start_frame,
    .decode_slice   = dxva2_h264_decode_slice,
    .end_frame      = dxva2_h264_end_frame,
    .frame_priv_data_size = sizeof(struct dxva2_picture_context),
};
#endif
