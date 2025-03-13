/*
 * Direct3D 12 AV1 HW acceleration
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
#include "libavutil/mem.h"
#include "av1dec.h"
#include "dxva2_internal.h"
#include "d3d12va_decode.h"

#define MAX_TILES 256

typedef struct D3D12AV1DecodeContext {
    D3D12VADecodeContext ctx;
    uint8_t *bitstream_buffer;
} D3D12AV1DecodeContext;

#define D3D12_AV1_DECODE_CONTEXT(avctx) ((D3D12AV1DecodeContext *)D3D12VA_DECODE_CONTEXT(avctx))

typedef struct AV1DecodePictureContext {
    DXVA_PicParams_AV1  pp;
    unsigned            tile_count;
    DXVA_Tile_AV1       tiles[MAX_TILES];
    uint8_t            *bitstream;
    unsigned            bitstream_size;
} AV1DecodePictureContext;

static int d3d12va_av1_start_frame(AVCodecContext *avctx,
                                   av_unused const AVBufferRef *buffer_ref,
                                   av_unused const uint8_t *buffer,
                                   av_unused uint32_t size)
{
    const AV1DecContext     *h       = avctx->priv_data;
    AV1DecodePictureContext *ctx_pic = h->cur_frame.hwaccel_picture_private;
    D3D12VADecodeContext    *ctx     = D3D12VA_DECODE_CONTEXT(avctx);
    if (!ctx)
        return -1;

    av_assert0(ctx_pic);

    ctx->used_mask = 0;

    if (ff_dxva2_av1_fill_picture_parameters(avctx, (AVDXVAContext *)ctx, &ctx_pic->pp) < 0)
        return -1;

    ctx_pic->bitstream      = NULL;
    ctx_pic->bitstream_size = 0;
    ctx_pic->tile_count     = 0;

    return 0;
}

static int d3d12va_av1_decode_slice(AVCodecContext *avctx,
                                   const uint8_t *buffer,
                                   uint32_t size)
{
    const AV1DecContext     *h            = avctx->priv_data;
    const AV1RawFrameHeader *frame_header = h->raw_frame_header;
    AV1DecodePictureContext *ctx_pic      = h->cur_frame.hwaccel_picture_private;
    int offset = 0;
    uint32_t tg_start, tg_end;

    ctx_pic->tile_count = frame_header->tile_cols * frame_header->tile_rows;

    if (ctx_pic->tile_count > MAX_TILES)
        return AVERROR(ENOSYS);

    if (ctx_pic->tile_count == h->tg_end - h->tg_start + 1) {
        tg_start = 0;
        tg_end   = ctx_pic->tile_count - 1;
        ctx_pic->bitstream      = (uint8_t *)buffer;
        ctx_pic->bitstream_size = size;
    } else {
        ctx_pic->bitstream = D3D12_AV1_DECODE_CONTEXT(avctx)->bitstream_buffer;
        memcpy(ctx_pic->bitstream + ctx_pic->bitstream_size, buffer, size);
        tg_start = h->tg_start;
        tg_end   = h->tg_end;
        offset   = ctx_pic->bitstream_size;
        ctx_pic->bitstream_size += size;
    }

    for (uint32_t tile_num = tg_start; tile_num <= tg_end; tile_num++) {
        ctx_pic->tiles[tile_num].DataOffset   = offset + h->tile_group_info[tile_num].tile_offset;
        ctx_pic->tiles[tile_num].DataSize     = h->tile_group_info[tile_num].tile_size;
        ctx_pic->tiles[tile_num].row          = h->tile_group_info[tile_num].tile_row;
        ctx_pic->tiles[tile_num].column       = h->tile_group_info[tile_num].tile_column;
        ctx_pic->tiles[tile_num].anchor_frame = 0xFF;
    }

    return 0;
}

static int update_input_arguments(AVCodecContext *avctx, D3D12_VIDEO_DECODE_INPUT_STREAM_ARGUMENTS *input_args, ID3D12Resource *buffer)
{
    const AV1DecContext     *h            = avctx->priv_data;
    AV1DecodePictureContext *ctx_pic      = h->cur_frame.hwaccel_picture_private;
    void *mapped_data;

    D3D12_VIDEO_DECODE_FRAME_ARGUMENT *args = &input_args->FrameArguments[input_args->NumFrameArguments++];
    args->Type  = D3D12_VIDEO_DECODE_ARGUMENT_TYPE_SLICE_CONTROL;
    args->Size  = sizeof(DXVA_Tile_AV1) * ctx_pic->tile_count;
    args->pData = ctx_pic->tiles;

    input_args->CompressedBitstream = (D3D12_VIDEO_DECODE_COMPRESSED_BITSTREAM){
        .pBuffer = buffer,
        .Offset  = 0,
        .Size    = ctx_pic->bitstream_size,
    };

    if (FAILED(ID3D12Resource_Map(buffer, 0, NULL, &mapped_data))) {
        av_log(avctx, AV_LOG_ERROR, "Failed to map D3D12 Buffer resource!\n");
        return AVERROR(EINVAL);
    }

    memcpy(mapped_data, ctx_pic->bitstream, ctx_pic->bitstream_size);

    ID3D12Resource_Unmap(buffer, 0, NULL);

    return 0;
}

static int d3d12va_av1_end_frame(AVCodecContext *avctx)
{
    int ret;
    const AV1DecContext     *h       = avctx->priv_data;
    AV1DecodePictureContext *ctx_pic = h->cur_frame.hwaccel_picture_private;

    if (ctx_pic->tiles <= 0 || ctx_pic->bitstream_size <= 0)
        return -1;

    ret = ff_d3d12va_common_end_frame(avctx, h->cur_frame.f, &ctx_pic->pp, sizeof(ctx_pic->pp),
                                      NULL, 0, update_input_arguments);

    return ret;
}

static av_cold int d3d12va_av1_decode_init(AVCodecContext *avctx)
{
    D3D12VADecodeContext    *ctx     = D3D12VA_DECODE_CONTEXT(avctx);
    D3D12AV1DecodeContext   *av1_ctx = D3D12_AV1_DECODE_CONTEXT(avctx);
    DXVA_PicParams_AV1 pp;

    int ret;

    if (avctx->profile != AV_PROFILE_AV1_MAIN)
        return AVERROR(EINVAL);

    ctx->cfg.DecodeProfile = D3D12_VIDEO_DECODE_PROFILE_AV1_PROFILE0;

    ctx->max_num_ref = FF_ARRAY_ELEMS(pp.RefFrameMapTextureIndex) + 1;

    ret = ff_d3d12va_decode_init(avctx);
    if (ret < 0)
        return ret;

    if (!av1_ctx->bitstream_buffer) {
        av1_ctx->bitstream_buffer = av_malloc(ff_d3d12va_get_suitable_max_bitstream_size(avctx));
        if (!av1_ctx->bitstream_buffer)
            return AVERROR(ENOMEM);
    }

    return 0;
}

static av_cold int d3d12va_av1_decode_uninit(AVCodecContext *avctx)
{
    D3D12AV1DecodeContext   *ctx     = D3D12_AV1_DECODE_CONTEXT(avctx);

    if (ctx->bitstream_buffer)
        av_freep(&ctx->bitstream_buffer);

    return ff_d3d12va_decode_uninit(avctx);
}

#if CONFIG_AV1_D3D12VA_HWACCEL
const FFHWAccel ff_av1_d3d12va_hwaccel = {
    .p.name               = "av1_d3d12va",
    .p.type               = AVMEDIA_TYPE_VIDEO,
    .p.id                 = AV_CODEC_ID_AV1,
    .p.pix_fmt            = AV_PIX_FMT_D3D12,
    .init                 = d3d12va_av1_decode_init,
    .uninit               = d3d12va_av1_decode_uninit,
    .start_frame          = d3d12va_av1_start_frame,
    .decode_slice         = d3d12va_av1_decode_slice,
    .end_frame            = d3d12va_av1_end_frame,
    .frame_params         = ff_d3d12va_common_frame_params,
    .frame_priv_data_size = sizeof(AV1DecodePictureContext),
    .priv_data_size       = sizeof(D3D12AV1DecodeContext),
};
#endif
