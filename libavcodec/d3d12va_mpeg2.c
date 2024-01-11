/*
 * Direct3D12 MPEG-2 HW acceleration
 *
 * copyright (c) 2022-2023 Wu Jianhua <toqsxw@outlook.com>
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

#include "config_components.h"
#include "libavutil/avassert.h"
#include "libavutil/hwcontext_d3d12va_internal.h"
#include "mpegutils.h"
#include "mpegvideodec.h"
#include "d3d12va_decode.h"
#include "dxva2_internal.h"

#define MAX_SLICES  1024
#define INVALID_REF 0xffff

typedef struct D3D12DecodePictureContext {
    DXVA_PictureParameters  pp;
    DXVA_QmatrixData        qm;
    unsigned                slice_count;
    DXVA_SliceInfo          slices[MAX_SLICES];
    const uint8_t          *bitstream;
    unsigned                bitstream_size;
} D3D12DecodePictureContext;

static int d3d12va_mpeg2_start_frame(AVCodecContext *avctx, av_unused const uint8_t *buffer,  av_unused uint32_t size)
{
    const MpegEncContext      *s       = avctx->priv_data;
    D3D12VADecodeContext      *ctx     = D3D12VA_DECODE_CONTEXT(avctx);
    D3D12DecodePictureContext *ctx_pic = s->current_picture_ptr->hwaccel_picture_private;

    if (!ctx)
        return -1;

    av_assert0(ctx_pic);

    ctx->used_mask = 0;

    ff_dxva2_mpeg2_fill_picture_parameters(avctx, (AVDXVAContext *)ctx, &ctx_pic->pp);
    ff_dxva2_mpeg2_fill_quantization_matrices(avctx, (AVDXVAContext *)ctx, &ctx_pic->qm);

    // Post processing operations are not supported in D3D12 Video
    ctx_pic->pp.wDeblockedPictureIndex = INVALID_REF;

    ctx_pic->bitstream      = NULL;
    ctx_pic->bitstream_size = 0;
    ctx_pic->slice_count    = 0;

    return 0;
}

static int d3d12va_mpeg2_decode_slice(AVCodecContext *avctx, const uint8_t *buffer, uint32_t size)
{
    const MpegEncContext      *s       = avctx->priv_data;
    D3D12DecodePictureContext *ctx_pic = s->current_picture_ptr->hwaccel_picture_private;

    if (ctx_pic->slice_count >= MAX_SLICES) {
        return AVERROR(ERANGE);
    }

    if (!ctx_pic->bitstream)
        ctx_pic->bitstream = buffer;
    ctx_pic->bitstream_size += size;

    ff_dxva2_mpeg2_fill_slice(avctx, &ctx_pic->slices[ctx_pic->slice_count++],
                              buffer - ctx_pic->bitstream, buffer, size);

    return 0;
}

static int update_input_arguments(AVCodecContext *avctx, D3D12_VIDEO_DECODE_INPUT_STREAM_ARGUMENTS *input_args, ID3D12Resource *buffer)
{
    const MpegEncContext      *s            = avctx->priv_data;
    D3D12DecodePictureContext *ctx_pic      = s->current_picture_ptr->hwaccel_picture_private;

    const int is_field = s->picture_structure != PICT_FRAME;
    const unsigned mb_count = s->mb_width * (s->mb_height >> is_field);

    int i;
    void *mapped_data = NULL;
    D3D12_VIDEO_DECODE_FRAME_ARGUMENT *args = &input_args->FrameArguments[input_args->NumFrameArguments++];

    D3D12_RANGE range = {
        .Begin = 0,
        .End = ctx_pic->bitstream_size,
    };

    if (FAILED(ID3D12Resource_Map(buffer, 0, &range, &mapped_data))) {
        av_log(avctx, AV_LOG_ERROR, "Failed to map D3D12 Buffer resource!\n");
        return AVERROR(EINVAL);
    }

    for (i = 0; i < ctx_pic->slice_count; i++) {
        DXVA_SliceInfo *slice = &ctx_pic->slices[i];

        if (i < ctx_pic->slice_count - 1)
            slice->wNumberMBsInSlice = slice[1].wNumberMBsInSlice - slice[0].wNumberMBsInSlice;
        else
            slice->wNumberMBsInSlice = mb_count - slice[0].wNumberMBsInSlice;
    }

    memcpy(mapped_data, ctx_pic->bitstream, ctx_pic->bitstream_size);

    ID3D12Resource_Unmap(buffer, 0, &range);

    args->Type  = D3D12_VIDEO_DECODE_ARGUMENT_TYPE_SLICE_CONTROL;
    args->Size  = sizeof(DXVA_SliceInfo) * ctx_pic->slice_count;
    args->pData = ctx_pic->slices;

    input_args->CompressedBitstream = (D3D12_VIDEO_DECODE_COMPRESSED_BITSTREAM){
        .pBuffer = buffer,
        .Offset  = 0,
        .Size    = ctx_pic->bitstream_size,
    };

    return 0;
}

static int d3d12va_mpeg2_end_frame(AVCodecContext *avctx)
{
    int ret;
    MpegEncContext            *s       = avctx->priv_data;
    D3D12DecodePictureContext *ctx_pic = s->current_picture_ptr->hwaccel_picture_private;

    if (ctx_pic->slice_count <= 0 || ctx_pic->bitstream_size <= 0)
        return -1;

    ret = ff_d3d12va_common_end_frame(avctx, s->current_picture_ptr->f, &ctx_pic->pp, sizeof(ctx_pic->pp),
                                      &ctx_pic->qm, sizeof(ctx_pic->qm), update_input_arguments);
    if (!ret)
        ff_mpeg_draw_horiz_band(s, 0, avctx->height);

    return ret;
}

static int d3d12va_mpeg2_decode_init(AVCodecContext *avctx)
{
    D3D12VADecodeContext      *ctx     = D3D12VA_DECODE_CONTEXT(avctx);

    ctx->cfg.DecodeProfile = D3D12_VIDEO_DECODE_PROFILE_MPEG2;

    ctx->max_num_ref = 3;

    return ff_d3d12va_decode_init(avctx);
}

#if CONFIG_MPEG2_D3D12VA_HWACCEL
const FFHWAccel ff_mpeg2_d3d12va_hwaccel = {
    .p.name               = "mpeg2_d3d12va",
    .p.type               = AVMEDIA_TYPE_VIDEO,
    .p.id                 = AV_CODEC_ID_MPEG2VIDEO,
    .p.pix_fmt            = AV_PIX_FMT_D3D12,
    .init                 = d3d12va_mpeg2_decode_init,
    .uninit               = ff_d3d12va_decode_uninit,
    .start_frame          = d3d12va_mpeg2_start_frame,
    .decode_slice         = d3d12va_mpeg2_decode_slice,
    .end_frame            = d3d12va_mpeg2_end_frame,
    .frame_params         = ff_d3d12va_common_frame_params,
    .frame_priv_data_size = sizeof(D3D12DecodePictureContext),
    .priv_data_size       = sizeof(D3D12VADecodeContext),
};
#endif
