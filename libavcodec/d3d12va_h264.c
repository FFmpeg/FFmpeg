/*
 * Direct3D 12 h264 HW acceleration
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
#include "h264dec.h"
#include "h264data.h"
#include "h264_ps.h"
#include "mpegutils.h"
#include "dxva2_internal.h"
#include "d3d12va_decode.h"
#include "libavutil/hwcontext_d3d12va_internal.h"
#include <dxva.h>

typedef struct H264DecodePictureContext {
    DXVA_PicParams_H264   pp;
    DXVA_Qmatrix_H264     qm;
    unsigned              slice_count;
    DXVA_Slice_H264_Short slice_short[MAX_SLICES];
    const uint8_t         *bitstream;
    unsigned              bitstream_size;
} H264DecodePictureContext;

static void fill_slice_short(DXVA_Slice_H264_Short *slice,
                             unsigned position, unsigned size)
{
    memset(slice, 0, sizeof(*slice));
    slice->BSNALunitDataLocation = position;
    slice->SliceBytesInBuffer    = size;
    slice->wBadSliceChopping     = 0;
}

static int d3d12va_h264_start_frame(AVCodecContext *avctx,
                                    av_unused const AVBufferRef *buffer_ref,
                                    av_unused const uint8_t *buffer,
                                    av_unused uint32_t size)
{
    const H264Context        *h       = avctx->priv_data;
    H264DecodePictureContext *ctx_pic = h->cur_pic_ptr->hwaccel_picture_private;
    D3D12VADecodeContext     *ctx     = D3D12VA_DECODE_CONTEXT(avctx);

    if (!ctx)
        return -1;

    av_assert0(ctx_pic);

    ctx->used_mask = 0;

    ff_dxva2_h264_fill_picture_parameters(avctx, (AVDXVAContext *)ctx, &ctx_pic->pp);

    ff_dxva2_h264_fill_scaling_lists(avctx, (AVDXVAContext *)ctx, &ctx_pic->qm);

    ctx_pic->slice_count    = 0;
    ctx_pic->bitstream_size = 0;
    ctx_pic->bitstream      = NULL;

    return 0;
}

static int d3d12va_h264_decode_slice(AVCodecContext *avctx, const uint8_t *buffer, uint32_t size)
{
    unsigned position;
    const H264Context        *h               = avctx->priv_data;
    const H264SliceContext   *sl              = &h->slice_ctx[0];
    const H264Picture        *current_picture = h->cur_pic_ptr;
    H264DecodePictureContext *ctx_pic         = current_picture->hwaccel_picture_private;

    if (ctx_pic->slice_count >= MAX_SLICES)
        return AVERROR(ERANGE);

    if (!ctx_pic->bitstream)
        ctx_pic->bitstream = buffer;
    ctx_pic->bitstream_size += size;

    position = buffer - ctx_pic->bitstream;
    fill_slice_short(&ctx_pic->slice_short[ctx_pic->slice_count], position, size);
    ctx_pic->slice_count++;

    if (sl->slice_type != AV_PICTURE_TYPE_I && sl->slice_type != AV_PICTURE_TYPE_SI)
        ctx_pic->pp.wBitFields &= ~(1 << 15); /* Set IntraPicFlag to 0 */

    return 0;
}

#define START_CODE 65536
#define START_CODE_SIZE 3
static int update_input_arguments(AVCodecContext *avctx, D3D12_VIDEO_DECODE_INPUT_STREAM_ARGUMENTS *input_args, ID3D12Resource *buffer)
{
    const H264Context        *h               = avctx->priv_data;
    const H264Picture        *current_picture = h->cur_pic_ptr;
    H264DecodePictureContext *ctx_pic         = current_picture->hwaccel_picture_private;

    int i;
    uint8_t *mapped_data, *mapped_ptr;
    DXVA_Slice_H264_Short *slice;
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
        size     = slice->SliceBytesInBuffer;

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
    args->Type  = D3D12_VIDEO_DECODE_ARGUMENT_TYPE_SLICE_CONTROL;
    args->Size  = sizeof(DXVA_Slice_H264_Short) * ctx_pic->slice_count;
    args->pData = ctx_pic->slice_short;

    return 0;
}

static int d3d12va_h264_end_frame(AVCodecContext *avctx)
{
    H264Context               *h       = avctx->priv_data;
    H264DecodePictureContext  *ctx_pic = h->cur_pic_ptr->hwaccel_picture_private;
    H264SliceContext          *sl      = &h->slice_ctx[0];

    int ret;

    if (ctx_pic->slice_count <= 0 || ctx_pic->bitstream_size <= 0)
        return -1;

    ret = ff_d3d12va_common_end_frame(avctx, h->cur_pic_ptr->f,
                                      &ctx_pic->pp, sizeof(ctx_pic->pp),
                                      &ctx_pic->qm, sizeof(ctx_pic->qm),
                                      update_input_arguments);
    if (!ret)
        ff_h264_draw_horiz_band(h, sl, 0, h->avctx->height);

    return ret;
}

static av_cold int d3d12va_h264_decode_init(AVCodecContext *avctx)
{
    D3D12VADecodeContext *ctx = D3D12VA_DECODE_CONTEXT(avctx);
    DXVA_PicParams_H264 pp;

    ctx->cfg.DecodeProfile = D3D12_VIDEO_DECODE_PROFILE_H264;

    ctx->max_num_ref = FF_ARRAY_ELEMS(pp.RefFrameList) + 1;

    return ff_d3d12va_decode_init(avctx);
}

#if CONFIG_H264_D3D12VA_HWACCEL
const FFHWAccel ff_h264_d3d12va_hwaccel = {
    .p.name               = "h264_d3d12va",
    .p.type               = AVMEDIA_TYPE_VIDEO,
    .p.id                 = AV_CODEC_ID_H264,
    .p.pix_fmt            = AV_PIX_FMT_D3D12,
    .init                 = d3d12va_h264_decode_init,
    .uninit               = ff_d3d12va_decode_uninit,
    .start_frame          = d3d12va_h264_start_frame,
    .decode_slice         = d3d12va_h264_decode_slice,
    .end_frame            = d3d12va_h264_end_frame,
    .frame_params         = ff_d3d12va_common_frame_params,
    .frame_priv_data_size = sizeof(H264DecodePictureContext),
    .priv_data_size       = sizeof(D3D12VADecodeContext),
};
#endif
