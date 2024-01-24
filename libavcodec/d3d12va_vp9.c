/*
 * Direct3D 12 VP9 HW acceleration
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
#include "libavutil/pixdesc.h"
#include "libavutil/hwcontext_d3d12va_internal.h"

#include "vp9shared.h"
#include "dxva2_internal.h"
#include "d3d12va_decode.h"

typedef struct VP9DecodePictureContext {
    DXVA_PicParams_VP9    pp;
    DXVA_Slice_VPx_Short  slice;
    const uint8_t        *bitstream;
    unsigned              bitstream_size;
} VP9DecodePictureContext;

static void fill_slice_short(DXVA_Slice_VPx_Short *slice, unsigned position, unsigned size)
{
    memset(slice, 0, sizeof(*slice));
    slice->BSNALunitDataLocation = position;
    slice->SliceBytesInBuffer    = size;
    slice->wBadSliceChopping     = 0;
}

static int d3d12va_vp9_start_frame(AVCodecContext *avctx, av_unused const uint8_t *buffer, av_unused uint32_t size)
{
    const VP9SharedContext  *h       = avctx->priv_data;
    D3D12VADecodeContext     *ctx     = D3D12VA_DECODE_CONTEXT(avctx);
    VP9DecodePictureContext *ctx_pic = h->frames[CUR_FRAME].hwaccel_picture_private;

    if (!ctx)
        return -1;

    av_assert0(ctx_pic);

    ctx->used_mask = 0;

    if (ff_dxva2_vp9_fill_picture_parameters(avctx, (AVDXVAContext *)ctx, &ctx_pic->pp) < 0)
        return -1;

    ctx_pic->bitstream_size = 0;
    ctx_pic->bitstream = NULL;

    return 0;
}

static int d3d12va_vp9_decode_slice(AVCodecContext *avctx, const uint8_t *buffer, uint32_t size)
{
    const VP9SharedContext  *h       = avctx->priv_data;
    VP9DecodePictureContext *ctx_pic = h->frames[CUR_FRAME].hwaccel_picture_private;
    unsigned position;

    if (!ctx_pic->bitstream)
        ctx_pic->bitstream = buffer;
    ctx_pic->bitstream_size += size;

    position = buffer - ctx_pic->bitstream;
    fill_slice_short(&ctx_pic->slice, position, size);

    return 0;
}

static int update_input_arguments(AVCodecContext *avctx, D3D12_VIDEO_DECODE_INPUT_STREAM_ARGUMENTS *input_args, ID3D12Resource *buffer)
{
    const VP9SharedContext  *h       = avctx->priv_data;
    VP9DecodePictureContext *ctx_pic = h->frames[CUR_FRAME].hwaccel_picture_private;

    void *mapped_data;
    D3D12_VIDEO_DECODE_FRAME_ARGUMENT *args;

    if (FAILED(ID3D12Resource_Map(buffer, 0, NULL, &mapped_data))) {
        av_log(avctx, AV_LOG_ERROR, "Failed to map D3D12 Buffer resource!\n");
        return AVERROR(EINVAL);
    }

    args = &input_args->FrameArguments[input_args->NumFrameArguments++];
    args->Type  = D3D12_VIDEO_DECODE_ARGUMENT_TYPE_SLICE_CONTROL;
    args->Size  = sizeof(ctx_pic->slice);
    args->pData = &ctx_pic->slice;

    memcpy(mapped_data, ctx_pic->bitstream, ctx_pic->slice.SliceBytesInBuffer);

    ID3D12Resource_Unmap(buffer, 0, NULL);

    input_args->CompressedBitstream = (D3D12_VIDEO_DECODE_COMPRESSED_BITSTREAM){
        .pBuffer = buffer,
        .Offset  = 0,
        .Size    = ctx_pic->slice.SliceBytesInBuffer,
    };

    return 0;
}

static int d3d12va_vp9_end_frame(AVCodecContext *avctx)
{
    VP9SharedContext        *h       = avctx->priv_data;
    VP9DecodePictureContext *ctx_pic = h->frames[CUR_FRAME].hwaccel_picture_private;

    if (ctx_pic->bitstream_size <= 0)
        return -1;

    return ff_d3d12va_common_end_frame(avctx, h->frames[CUR_FRAME].tf.f,
               &ctx_pic->pp, sizeof(ctx_pic->pp), NULL, 0, update_input_arguments);
}

static int d3d12va_vp9_decode_init(AVCodecContext *avctx)
{
    D3D12VADecodeContext *ctx = D3D12VA_DECODE_CONTEXT(avctx);
    DXVA_PicParams_VP9 pp;

    switch (avctx->profile) {
    case AV_PROFILE_VP9_2:
    case AV_PROFILE_VP9_3:
        ctx->cfg.DecodeProfile = D3D12_VIDEO_DECODE_PROFILE_VP9_10BIT_PROFILE2;
        break;

    case AV_PROFILE_VP9_0:
    case AV_PROFILE_VP9_1:
    default:
        ctx->cfg.DecodeProfile = D3D12_VIDEO_DECODE_PROFILE_VP9;
        break;
    };

    ctx->max_num_ref = FF_ARRAY_ELEMS(pp.ref_frame_map) + 1;

    return ff_d3d12va_decode_init(avctx);
}

#if CONFIG_VP9_D3D12VA_HWACCEL
const FFHWAccel ff_vp9_d3d12va_hwaccel = {
    .p.name               = "vp9_d3d12va",
    .p.type               = AVMEDIA_TYPE_VIDEO,
    .p.id                 = AV_CODEC_ID_VP9,
    .p.pix_fmt            = AV_PIX_FMT_D3D12,
    .init                 = d3d12va_vp9_decode_init,
    .uninit               = ff_d3d12va_decode_uninit,
    .start_frame          = d3d12va_vp9_start_frame,
    .decode_slice         = d3d12va_vp9_decode_slice,
    .end_frame            = d3d12va_vp9_end_frame,
    .frame_params         = ff_d3d12va_common_frame_params,
    .frame_priv_data_size = sizeof(VP9DecodePictureContext),
    .priv_data_size       = sizeof(D3D12VADecodeContext),
};
#endif
