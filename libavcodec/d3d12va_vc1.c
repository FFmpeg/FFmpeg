/*
 * Direct3D12 WMV3/VC-1 HW acceleration
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
#include "vc1.h"
#include "vc1data.h"
#include "d3d12va_decode.h"
#include "dxva2_internal.h"

#define MAX_SLICES  1024
#define INVALID_REF 0xffff

typedef struct D3D12DecodePictureContext {
    DXVA_PictureParameters pp;
    unsigned               slice_count;
    DXVA_SliceInfo         slices[MAX_SLICES];
    const uint8_t         *bitstream;
    unsigned               bitstream_size;
} D3D12DecodePictureContext;

static int d3d12va_vc1_start_frame(AVCodecContext *avctx, av_unused const uint8_t *buffer,  av_unused uint32_t size)
{
    const VC1Context          *v       = avctx->priv_data;
    D3D12VADecodeContext      *ctx     = D3D12VA_DECODE_CONTEXT(avctx);
    D3D12DecodePictureContext *ctx_pic = v->s.cur_pic.ptr->hwaccel_picture_private;

    if (!ctx)
        return -1;

    av_assert0(ctx_pic);

    ctx->used_mask = 0;

    ff_dxva2_vc1_fill_picture_parameters(avctx, (AVDXVAContext *)ctx, &ctx_pic->pp);
    ctx_pic->pp.wDeblockedPictureIndex = INVALID_REF;

    ctx_pic->bitstream      = NULL;
    ctx_pic->bitstream_size = 0;
    ctx_pic->slice_count    = 0;

    return 0;
}

static int d3d12va_vc1_decode_slice(AVCodecContext *avctx, const uint8_t *buffer, uint32_t size)
{
    const VC1Context          *v       = avctx->priv_data;
    D3D12DecodePictureContext *ctx_pic = v->s.cur_pic.ptr->hwaccel_picture_private;

    if (ctx_pic->slice_count >= MAX_SLICES) {
        return AVERROR(ERANGE);
    }

    if (avctx->codec_id == AV_CODEC_ID_VC1 &&
        size >= 4 && IS_MARKER(AV_RB32(buffer))) {
        buffer += 4;
        size -= 4;
    }

    if (!ctx_pic->bitstream)
        ctx_pic->bitstream = buffer;
    ctx_pic->bitstream_size += size;

    ff_dxva2_vc1_fill_slice(avctx, &ctx_pic->slices[ctx_pic->slice_count++],
                            buffer - ctx_pic->bitstream, size);

    return 0;
}

static int update_input_arguments(AVCodecContext *avctx, D3D12_VIDEO_DECODE_INPUT_STREAM_ARGUMENTS *input_args, ID3D12Resource *buffer)
{
    const VC1Context *v                     = avctx->priv_data;
    const MpegEncContext      *s            = &v->s;
    D3D12DecodePictureContext *ctx_pic      = s->cur_pic.ptr->hwaccel_picture_private;
    D3D12_VIDEO_DECODE_FRAME_ARGUMENT *args = &input_args->FrameArguments[input_args->NumFrameArguments++];

    const unsigned mb_count = s->mb_width * (s->mb_height >> v->field_mode);
    uint8_t *mapped_data, *mapped_ptr;

    static const uint8_t start_code[] = { 0, 0, 1, 0x0d };

    if (FAILED(ID3D12Resource_Map(buffer, 0, NULL, (void **)&mapped_data))) {
        av_log(avctx, AV_LOG_ERROR, "Failed to map D3D12 Buffer resource!\n");
        return AVERROR(EINVAL);
    }

    mapped_ptr = mapped_data;
    for (int i = 0; i < ctx_pic->slice_count; i++) {
        DXVA_SliceInfo *slice = &ctx_pic->slices[i];
        unsigned position     = slice->dwSliceDataLocation;
        unsigned size         = slice->dwSliceBitsInBuffer / 8;

        slice->dwSliceDataLocation = mapped_ptr - mapped_data;
        if (i < ctx_pic->slice_count - 1)
            slice->wNumberMBsInSlice = slice[1].wNumberMBsInSlice - slice[0].wNumberMBsInSlice;
        else
            slice->wNumberMBsInSlice = mb_count - slice[0].wNumberMBsInSlice;

        if (avctx->codec_id == AV_CODEC_ID_VC1) {
            memcpy(mapped_ptr, start_code, sizeof(start_code));
            if (i == 0 && v->second_field)
                mapped_ptr[3] = 0x0c;
            else if (i > 0)
                mapped_ptr[3] = 0x0b;

            mapped_ptr += sizeof(start_code);
            slice->dwSliceBitsInBuffer += sizeof(start_code) * 8;
        }

        memcpy(mapped_ptr, &ctx_pic->bitstream[position], size);
        mapped_ptr += size;
    }

    ID3D12Resource_Unmap(buffer, 0, NULL);

    args->Type  = D3D12_VIDEO_DECODE_ARGUMENT_TYPE_SLICE_CONTROL;
    args->Size  = sizeof(DXVA_SliceInfo) * ctx_pic->slice_count;
    args->pData = ctx_pic->slices;

    input_args->CompressedBitstream = (D3D12_VIDEO_DECODE_COMPRESSED_BITSTREAM){
        .pBuffer = buffer,
        .Offset  = 0,
        .Size    = mapped_ptr - mapped_data,
    };

    return 0;
}

static int d3d12va_vc1_end_frame(AVCodecContext *avctx)
{
    const VC1Context          *v       = avctx->priv_data;
    D3D12DecodePictureContext *ctx_pic = v->s.cur_pic.ptr->hwaccel_picture_private;

    if (ctx_pic->slice_count <= 0 || ctx_pic->bitstream_size <= 0)
        return -1;

    return ff_d3d12va_common_end_frame(avctx, v->s.cur_pic.ptr->f,
                                       &ctx_pic->pp, sizeof(ctx_pic->pp),
                                       NULL, 0,
                                       update_input_arguments);
}

static int d3d12va_vc1_decode_init(AVCodecContext *avctx)
{
    int ret;
    D3D12VADecodeContext *ctx = D3D12VA_DECODE_CONTEXT(avctx);
    ctx->cfg.DecodeProfile = D3D12_VIDEO_DECODE_PROFILE_VC1_D2010;

    ctx->max_num_ref = 3;

    ret = ff_d3d12va_decode_init(avctx);
    if (ret < 0) {
        ctx->cfg.DecodeProfile = D3D12_VIDEO_DECODE_PROFILE_VC1;
        ret = ff_d3d12va_decode_init(avctx);
    }

    return ret;
}

#if CONFIG_WMV3_D3D12VA_HWACCEL
const FFHWAccel ff_wmv3_d3d12va_hwaccel = {
    .p.name               = "wmv3_d3d12va",
    .p.type               = AVMEDIA_TYPE_VIDEO,
    .p.id                 = AV_CODEC_ID_WMV3,
    .p.pix_fmt            = AV_PIX_FMT_D3D12,
    .init                 = d3d12va_vc1_decode_init,
    .uninit               = ff_d3d12va_decode_uninit,
    .start_frame          = d3d12va_vc1_start_frame,
    .decode_slice         = d3d12va_vc1_decode_slice,
    .end_frame            = d3d12va_vc1_end_frame,
    .frame_params         = ff_d3d12va_common_frame_params,
    .frame_priv_data_size = sizeof(D3D12DecodePictureContext),
    .priv_data_size       = sizeof(D3D12VADecodeContext),
};
#endif

#if CONFIG_VC1_D3D12VA_HWACCEL
const FFHWAccel ff_vc1_d3d12va_hwaccel = {
    .p.name               = "vc1_d3d12va",
    .p.type               = AVMEDIA_TYPE_VIDEO,
    .p.id                 = AV_CODEC_ID_VC1,
    .p.pix_fmt            = AV_PIX_FMT_D3D12,
    .init                 = d3d12va_vc1_decode_init,
    .uninit               = ff_d3d12va_decode_uninit,
    .start_frame          = d3d12va_vc1_start_frame,
    .decode_slice         = d3d12va_vc1_decode_slice,
    .end_frame            = d3d12va_vc1_end_frame,
    .frame_params         = ff_d3d12va_common_frame_params,
    .frame_priv_data_size = sizeof(D3D12DecodePictureContext),
    .priv_data_size       = sizeof(D3D12VADecodeContext),
};
#endif
