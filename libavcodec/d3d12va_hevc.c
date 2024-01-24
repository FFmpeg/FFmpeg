/*
 * Direct3D 12 HEVC HW acceleration
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
#include "hevc_data.h"
#include "hevcdec.h"
#include "dxva2_internal.h"
#include "d3d12va_decode.h"
#include <dxva.h>

#define MAX_SLICES 256

typedef struct HEVCDecodePictureContext {
    DXVA_PicParams_HEVC    pp;
    DXVA_Qmatrix_HEVC      qm;
    unsigned               slice_count;
    DXVA_Slice_HEVC_Short  slice_short[MAX_SLICES];
    const uint8_t         *bitstream;
    unsigned               bitstream_size;
} HEVCDecodePictureContext;

static void fill_slice_short(DXVA_Slice_HEVC_Short *slice, unsigned position, unsigned size)
{
    memset(slice, 0, sizeof(*slice));
    slice->BSNALunitDataLocation = position;
    slice->SliceBytesInBuffer    = size;
    slice->wBadSliceChopping     = 0;
}

static int d3d12va_hevc_start_frame(AVCodecContext *avctx, av_unused const uint8_t *buffer, av_unused uint32_t size)
{
    const HEVCContext        *h       = avctx->priv_data;
    D3D12VADecodeContext     *ctx     = D3D12VA_DECODE_CONTEXT(avctx);
    HEVCDecodePictureContext *ctx_pic = h->ref->hwaccel_picture_private;

    if (!ctx)
        return -1;

    av_assert0(ctx_pic);

    ctx->used_mask = 0;

    ff_dxva2_hevc_fill_picture_parameters(avctx, (AVDXVAContext *)ctx, &ctx_pic->pp);

    ff_dxva2_hevc_fill_scaling_lists(avctx, (AVDXVAContext *)ctx, &ctx_pic->qm);

    ctx_pic->slice_count    = 0;
    ctx_pic->bitstream_size = 0;
    ctx_pic->bitstream      = NULL;

    return 0;
}

static int d3d12va_hevc_decode_slice(AVCodecContext *avctx, const uint8_t *buffer, uint32_t size)
{
    const HEVCContext        *h               = avctx->priv_data;
    const HEVCFrame          *current_picture = h->ref;
    HEVCDecodePictureContext *ctx_pic         = current_picture->hwaccel_picture_private;
    unsigned position;

    if (ctx_pic->slice_count >= MAX_SLICES)
        return AVERROR(ERANGE);

    if (!ctx_pic->bitstream)
        ctx_pic->bitstream = buffer;
    ctx_pic->bitstream_size += size;

    position = buffer - ctx_pic->bitstream;
    fill_slice_short(&ctx_pic->slice_short[ctx_pic->slice_count], position, size);
    ctx_pic->slice_count++;

    return 0;
}

#define START_CODE 65536
#define START_CODE_SIZE 3
static int update_input_arguments(AVCodecContext *avctx, D3D12_VIDEO_DECODE_INPUT_STREAM_ARGUMENTS *input_args, ID3D12Resource *buffer)
{
    const HEVCContext        *h               = avctx->priv_data;
    const HEVCFrame          *current_picture = h->ref;
    HEVCDecodePictureContext *ctx_pic         = current_picture->hwaccel_picture_private;

    int i;
    uint8_t *mapped_data, *mapped_ptr;
    DXVA_Slice_HEVC_Short *slice;
    D3D12_VIDEO_DECODE_FRAME_ARGUMENT *args;

    if (FAILED(ID3D12Resource_Map(buffer, 0, NULL, (void **)&mapped_data))) {
        av_log(avctx, AV_LOG_ERROR, "Failed to map D3D12 Buffer resource!\n");
        return AVERROR(EINVAL);
    }

    mapped_ptr = mapped_data;
    for (i = 0; i < ctx_pic->slice_count; i++) {
        UINT position, size;
        slice = &ctx_pic->slice_short[i];

        position = slice->BSNALunitDataLocation;
        size = slice->SliceBytesInBuffer;

        slice->SliceBytesInBuffer += START_CODE_SIZE;
        slice->BSNALunitDataLocation = mapped_ptr - mapped_data;

        *(uint32_t *)mapped_ptr = START_CODE;
        mapped_ptr += START_CODE_SIZE;

        memcpy(mapped_ptr, &ctx_pic->bitstream[position], size);
        mapped_ptr += size;
    }

    ID3D12Resource_Unmap(buffer, 0, NULL);

    input_args->CompressedBitstream = (D3D12_VIDEO_DECODE_COMPRESSED_BITSTREAM){
        .pBuffer = buffer,
        .Offset  = 0,
        .Size    = mapped_ptr - mapped_data,
    };

    args = &input_args->FrameArguments[input_args->NumFrameArguments++];
    args->Type = D3D12_VIDEO_DECODE_ARGUMENT_TYPE_SLICE_CONTROL;
    args->Size = sizeof(DXVA_Slice_HEVC_Short) * ctx_pic->slice_count;
    args->pData = ctx_pic->slice_short;

    return 0;
}

static int d3d12va_hevc_end_frame(AVCodecContext *avctx)
{
    HEVCContext              *h       = avctx->priv_data;
    HEVCDecodePictureContext *ctx_pic = h->ref->hwaccel_picture_private;

    int scale = ctx_pic->pp.dwCodingParamToolFlags & 1;

    if (ctx_pic->slice_count <= 0 || ctx_pic->bitstream_size <= 0)
        return -1;

    return ff_d3d12va_common_end_frame(avctx, h->ref->frame, &ctx_pic->pp, sizeof(ctx_pic->pp),
               scale ? &ctx_pic->qm : NULL, scale ? sizeof(ctx_pic->qm) : 0, update_input_arguments);
}

static int d3d12va_hevc_decode_init(AVCodecContext *avctx)
{
    D3D12VADecodeContext *ctx = D3D12VA_DECODE_CONTEXT(avctx);
    DXVA_PicParams_HEVC pp;

    switch (avctx->profile) {
    case AV_PROFILE_HEVC_MAIN_10:
        ctx->cfg.DecodeProfile = D3D12_VIDEO_DECODE_PROFILE_HEVC_MAIN10;
        break;

    case AV_PROFILE_HEVC_MAIN_STILL_PICTURE:
        if (avctx->hwaccel_flags & AV_HWACCEL_FLAG_ALLOW_PROFILE_MISMATCH) {
            ctx->cfg.DecodeProfile = D3D12_VIDEO_DECODE_PROFILE_HEVC_MAIN;
            break;
        } else {
            av_log(avctx, AV_LOG_ERROR, "D3D12 doesn't support PROFILE_HEVC_MAIN_STILL_PICTURE!\n");
            return AVERROR(EINVAL);
        }

    case AV_PROFILE_HEVC_MAIN:
    default:
        ctx->cfg.DecodeProfile = D3D12_VIDEO_DECODE_PROFILE_HEVC_MAIN;
        break;
    };

    ctx->max_num_ref = FF_ARRAY_ELEMS(pp.RefPicList) + 1;

    return ff_d3d12va_decode_init(avctx);
}

#if CONFIG_HEVC_D3D12VA_HWACCEL
const FFHWAccel ff_hevc_d3d12va_hwaccel = {
    .p.name               = "hevc_d3d12va",
    .p.type               = AVMEDIA_TYPE_VIDEO,
    .p.id                 = AV_CODEC_ID_HEVC,
    .p.pix_fmt            = AV_PIX_FMT_D3D12,
    .init                 = d3d12va_hevc_decode_init,
    .uninit               = ff_d3d12va_decode_uninit,
    .start_frame          = d3d12va_hevc_start_frame,
    .decode_slice         = d3d12va_hevc_decode_slice,
    .end_frame            = d3d12va_hevc_end_frame,
    .frame_params         = ff_d3d12va_common_frame_params,
    .frame_priv_data_size = sizeof(HEVCDecodePictureContext),
    .priv_data_size       = sizeof(D3D12VADecodeContext),
};
#endif
