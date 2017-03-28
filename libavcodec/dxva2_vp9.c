/*
 * DXVA2 VP9 HW acceleration.
 *
 * copyright (c) 2015 Hendrik Leppkes
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
#include "libavutil/pixdesc.h"

#include "vp9shared.h"

// The headers above may include w32threads.h, which uses the original
// _WIN32_WINNT define, while dxva2_internal.h redefines it to target a
// potentially newer version.
#include "dxva2_internal.h"

struct vp9_dxva2_picture_context {
    DXVA_PicParams_VP9    pp;
    DXVA_Slice_VPx_Short  slice;
    const uint8_t         *bitstream;
    unsigned              bitstream_size;
};

static void fill_picture_entry(DXVA_PicEntry_VPx *pic,
                               unsigned index, unsigned flag)
{
    av_assert0((index & 0x7f) == index && (flag & 0x01) == flag);
    pic->bPicEntry = index | (flag << 7);
}

static int fill_picture_parameters(const AVCodecContext *avctx, AVDXVAContext *ctx, const VP9SharedContext *h,
                                    DXVA_PicParams_VP9 *pp)
{
    int i;
    const AVPixFmtDescriptor * pixdesc = av_pix_fmt_desc_get(avctx->sw_pix_fmt);

    if (!pixdesc)
        return -1;

    memset(pp, 0, sizeof(*pp));

    fill_picture_entry(&pp->CurrPic, ff_dxva2_get_surface_index(avctx, ctx, h->frames[CUR_FRAME].tf.f), 0);

    pp->profile = h->h.profile;
    pp->wFormatAndPictureInfoFlags = ((h->h.keyframe == 0)   <<  0) |
                                     ((h->h.invisible == 0)  <<  1) |
                                     (h->h.errorres          <<  2) |
                                     (pixdesc->log2_chroma_w <<  3) | /* subsampling_x */
                                     (pixdesc->log2_chroma_h <<  4) | /* subsampling_y */
                                     (0                      <<  5) | /* extra_plane */
                                     (h->h.refreshctx        <<  6) |
                                     (h->h.parallelmode      <<  7) |
                                     (h->h.intraonly         <<  8) |
                                     (h->h.framectxid        <<  9) |
                                     (h->h.resetctx          << 11) |
                                     ((h->h.keyframe ? 0 : h->h.highprecisionmvs) << 13) |
                                     (0                      << 14);  /* ReservedFormatInfo2Bits */

    pp->width  = avctx->width;
    pp->height = avctx->height;
    pp->BitDepthMinus8Luma   = pixdesc->comp[0].depth - 8;
    pp->BitDepthMinus8Chroma = pixdesc->comp[1].depth - 8;
    /* swap 0/1 to match the reference */
    pp->interp_filter = h->h.filtermode ^ (h->h.filtermode <= 1);
    pp->Reserved8Bits = 0;

    for (i = 0; i < 8; i++) {
        if (h->refs[i].f->buf[0]) {
            fill_picture_entry(&pp->ref_frame_map[i], ff_dxva2_get_surface_index(avctx, ctx, h->refs[i].f), 0);
            pp->ref_frame_coded_width[i]  = h->refs[i].f->width;
            pp->ref_frame_coded_height[i] = h->refs[i].f->height;
        } else
            pp->ref_frame_map[i].bPicEntry = 0xFF;
    }

    for (i = 0; i < 3; i++) {
        uint8_t refidx = h->h.refidx[i];
        if (h->refs[refidx].f->buf[0])
            fill_picture_entry(&pp->frame_refs[i], ff_dxva2_get_surface_index(avctx, ctx, h->refs[refidx].f), 0);
        else
            pp->frame_refs[i].bPicEntry = 0xFF;

        pp->ref_frame_sign_bias[i + 1] = h->h.signbias[i];
    }

    pp->filter_level    = h->h.filter.level;
    pp->sharpness_level = h->h.filter.sharpness;

    pp->wControlInfoFlags = (h->h.lf_delta.enabled   << 0) |
                            (h->h.lf_delta.updated   << 1) |
                            (h->h.use_last_frame_mvs << 2) |
                            (0                       << 3);  /* ReservedControlInfo5Bits */

    for (i = 0; i < 4; i++)
        pp->ref_deltas[i]  = h->h.lf_delta.ref[i];

    for (i = 0; i < 2; i++)
        pp->mode_deltas[i]  = h->h.lf_delta.mode[i];

    pp->base_qindex   = h->h.yac_qi;
    pp->y_dc_delta_q  = h->h.ydc_qdelta;
    pp->uv_dc_delta_q = h->h.uvdc_qdelta;
    pp->uv_ac_delta_q = h->h.uvac_qdelta;

    /* segmentation data */
    pp->stVP9Segments.wSegmentInfoFlags = (h->h.segmentation.enabled       << 0) |
                                          (h->h.segmentation.update_map    << 1) |
                                          (h->h.segmentation.temporal      << 2) |
                                          (h->h.segmentation.absolute_vals << 3) |
                                          (0                               << 4);  /* ReservedSegmentFlags4Bits */

    for (i = 0; i < 7; i++)
        pp->stVP9Segments.tree_probs[i] = h->h.segmentation.prob[i];

    if (h->h.segmentation.temporal)
        for (i = 0; i < 3; i++)
            pp->stVP9Segments.pred_probs[i] = h->h.segmentation.pred_prob[i];
    else
        memset(pp->stVP9Segments.pred_probs, 255, sizeof(pp->stVP9Segments.pred_probs));

    for (i = 0; i < 8; i++) {
        pp->stVP9Segments.feature_mask[i] = (h->h.segmentation.feat[i].q_enabled    << 0) |
                                            (h->h.segmentation.feat[i].lf_enabled   << 1) |
                                            (h->h.segmentation.feat[i].ref_enabled  << 2) |
                                            (h->h.segmentation.feat[i].skip_enabled << 3);

        pp->stVP9Segments.feature_data[i][0] = h->h.segmentation.feat[i].q_val;
        pp->stVP9Segments.feature_data[i][1] = h->h.segmentation.feat[i].lf_val;
        pp->stVP9Segments.feature_data[i][2] = h->h.segmentation.feat[i].ref_val;
        pp->stVP9Segments.feature_data[i][3] = 0; /* no data for skip */
    }

    pp->log2_tile_cols = h->h.tiling.log2_tile_cols;
    pp->log2_tile_rows = h->h.tiling.log2_tile_rows;

    pp->uncompressed_header_size_byte_aligned = h->h.uncompressed_header_size;
    pp->first_partition_size = h->h.compressed_header_size;

    pp->StatusReportFeedbackNumber = 1 + DXVA_CONTEXT_REPORT_ID(avctx, ctx)++;
    return 0;
}

static void fill_slice_short(DXVA_Slice_VPx_Short *slice,
                             unsigned position, unsigned size)
{
    memset(slice, 0, sizeof(*slice));
    slice->BSNALunitDataLocation = position;
    slice->SliceBytesInBuffer    = size;
    slice->wBadSliceChopping     = 0;
}

static int commit_bitstream_and_slice_buffer(AVCodecContext *avctx,
                                             DECODER_BUFFER_DESC *bs,
                                             DECODER_BUFFER_DESC *sc)
{
    const VP9SharedContext *h = avctx->priv_data;
    AVDXVAContext *ctx = avctx->hwaccel_context;
    struct vp9_dxva2_picture_context *ctx_pic = h->frames[CUR_FRAME].hwaccel_picture_private;
    void     *dxva_data_ptr;
    uint8_t  *dxva_data;
    unsigned dxva_size;
    unsigned padding;
    unsigned type;

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

    if (ctx_pic->slice.SliceBytesInBuffer > dxva_size) {
        av_log(avctx, AV_LOG_ERROR, "Failed to build bitstream");
        return -1;
    }

    memcpy(dxva_data, ctx_pic->bitstream, ctx_pic->slice.SliceBytesInBuffer);

    padding = FFMIN(128 - ((ctx_pic->slice.SliceBytesInBuffer) & 127), dxva_size - ctx_pic->slice.SliceBytesInBuffer);
    if (padding > 0) {
        memset(dxva_data + ctx_pic->slice.SliceBytesInBuffer, 0, padding);
        ctx_pic->slice.SliceBytesInBuffer += padding;
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

#if CONFIG_D3D11VA
    if (avctx->pix_fmt == AV_PIX_FMT_D3D11VA_VLD) {
        D3D11_VIDEO_DECODER_BUFFER_DESC *dsc11 = bs;
        memset(dsc11, 0, sizeof(*dsc11));
        dsc11->BufferType           = type;
        dsc11->DataSize             = ctx_pic->slice.SliceBytesInBuffer;
        dsc11->NumMBsInBuffer       = 0;

        type = D3D11_VIDEO_DECODER_BUFFER_SLICE_CONTROL;
    }
#endif
#if CONFIG_DXVA2
    if (avctx->pix_fmt == AV_PIX_FMT_DXVA2_VLD) {
        DXVA2_DecodeBufferDesc *dsc2 = bs;
        memset(dsc2, 0, sizeof(*dsc2));
        dsc2->CompressedBufferType = type;
        dsc2->DataSize             = ctx_pic->slice.SliceBytesInBuffer;
        dsc2->NumMBsInBuffer       = 0;

        type = DXVA2_SliceControlBufferType;
    }
#endif

    return ff_dxva2_commit_buffer(avctx, ctx, sc,
                                  type,
                                  &ctx_pic->slice, sizeof(ctx_pic->slice), 0);
}


static int dxva2_vp9_start_frame(AVCodecContext *avctx,
                                 av_unused const uint8_t *buffer,
                                 av_unused uint32_t size)
{
    const VP9SharedContext *h = avctx->priv_data;
    AVDXVAContext *ctx = avctx->hwaccel_context;
    struct vp9_dxva2_picture_context *ctx_pic = h->frames[CUR_FRAME].hwaccel_picture_private;

    if (!DXVA_CONTEXT_VALID(avctx, ctx))
        return -1;
    av_assert0(ctx_pic);

    /* Fill up DXVA_PicParams_VP9 */
    if (fill_picture_parameters(avctx, ctx, h, &ctx_pic->pp) < 0)
        return -1;

    ctx_pic->bitstream_size = 0;
    ctx_pic->bitstream      = NULL;
    return 0;
}

static int dxva2_vp9_decode_slice(AVCodecContext *avctx,
                                  const uint8_t *buffer,
                                  uint32_t size)
{
    const VP9SharedContext *h = avctx->priv_data;
    struct vp9_dxva2_picture_context *ctx_pic = h->frames[CUR_FRAME].hwaccel_picture_private;
    unsigned position;

    if (!ctx_pic->bitstream)
        ctx_pic->bitstream = buffer;
    ctx_pic->bitstream_size += size;

    position = buffer - ctx_pic->bitstream;
    fill_slice_short(&ctx_pic->slice, position, size);

    return 0;
}

static int dxva2_vp9_end_frame(AVCodecContext *avctx)
{
    VP9SharedContext *h = avctx->priv_data;
    struct vp9_dxva2_picture_context *ctx_pic = h->frames[CUR_FRAME].hwaccel_picture_private;
    int ret;

    if (ctx_pic->bitstream_size <= 0)
        return -1;

    ret = ff_dxva2_common_end_frame(avctx, h->frames[CUR_FRAME].tf.f,
                                    &ctx_pic->pp, sizeof(ctx_pic->pp),
                                    NULL, 0,
                                    commit_bitstream_and_slice_buffer);
    return ret;
}

#if CONFIG_VP9_DXVA2_HWACCEL
AVHWAccel ff_vp9_dxva2_hwaccel = {
    .name           = "vp9_dxva2",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_VP9,
    .pix_fmt        = AV_PIX_FMT_DXVA2_VLD,
    .start_frame    = dxva2_vp9_start_frame,
    .decode_slice   = dxva2_vp9_decode_slice,
    .end_frame      = dxva2_vp9_end_frame,
    .frame_priv_data_size = sizeof(struct vp9_dxva2_picture_context),
};
#endif

#if CONFIG_VP9_D3D11VA_HWACCEL
AVHWAccel ff_vp9_d3d11va_hwaccel = {
    .name           = "vp9_d3d11va",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_VP9,
    .pix_fmt        = AV_PIX_FMT_D3D11VA_VLD,
    .start_frame    = dxva2_vp9_start_frame,
    .decode_slice   = dxva2_vp9_decode_slice,
    .end_frame      = dxva2_vp9_end_frame,
    .frame_priv_data_size = sizeof(struct vp9_dxva2_picture_context),
};
#endif
