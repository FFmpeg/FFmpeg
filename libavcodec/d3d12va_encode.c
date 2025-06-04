/*
 * Direct3D 12 HW acceleration video encoder
 *
 * Copyright (c) 2024 Intel Corporation
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
#include "libavutil/common.h"
#include "libavutil/internal.h"
#include "libavutil/log.h"
#include "libavutil/mem.h"
#include "libavutil/pixdesc.h"
#include "libavutil/hwcontext_d3d12va_internal.h"
#include "libavutil/hwcontext_d3d12va.h"

#include "avcodec.h"
#include "d3d12va_encode.h"
#include "encode.h"

const AVCodecHWConfigInternal *const ff_d3d12va_encode_hw_configs[] = {
    HW_CONFIG_ENCODER_FRAMES(D3D12, D3D12VA),
    NULL,
};

static int d3d12va_fence_completion(AVD3D12VASyncContext *psync_ctx)
{
    uint64_t completion = ID3D12Fence_GetCompletedValue(psync_ctx->fence);
    if (completion < psync_ctx->fence_value) {
        if (FAILED(ID3D12Fence_SetEventOnCompletion(psync_ctx->fence, psync_ctx->fence_value, psync_ctx->event)))
            return AVERROR(EINVAL);

        WaitForSingleObjectEx(psync_ctx->event, INFINITE, FALSE);
    }

    return 0;
}

static int d3d12va_sync_with_gpu(AVCodecContext *avctx)
{
    D3D12VAEncodeContext *ctx = avctx->priv_data;

    DX_CHECK(ID3D12CommandQueue_Signal(ctx->command_queue, ctx->sync_ctx.fence, ++ctx->sync_ctx.fence_value));
    return d3d12va_fence_completion(&ctx->sync_ctx);

fail:
    return AVERROR(EINVAL);
}

typedef struct CommandAllocator {
    ID3D12CommandAllocator *command_allocator;
    uint64_t fence_value;
} CommandAllocator;

static int d3d12va_get_valid_command_allocator(AVCodecContext *avctx, ID3D12CommandAllocator **ppAllocator)
{
    HRESULT hr;
    D3D12VAEncodeContext *ctx = avctx->priv_data;
    CommandAllocator allocator;

    if (av_fifo_peek(ctx->allocator_queue, &allocator, 1, 0) >= 0) {
        uint64_t completion = ID3D12Fence_GetCompletedValue(ctx->sync_ctx.fence);
        if (completion >= allocator.fence_value) {
            *ppAllocator = allocator.command_allocator;
            av_fifo_read(ctx->allocator_queue, &allocator, 1);
            return 0;
        }
    }

    hr = ID3D12Device_CreateCommandAllocator(ctx->hwctx->device, D3D12_COMMAND_LIST_TYPE_VIDEO_ENCODE,
                                             &IID_ID3D12CommandAllocator, (void **)ppAllocator);
    if (FAILED(hr)) {
        av_log(avctx, AV_LOG_ERROR, "Failed to create a new command allocator!\n");
        return AVERROR(EINVAL);
    }

    return 0;
}

static int d3d12va_discard_command_allocator(AVCodecContext *avctx, ID3D12CommandAllocator *pAllocator, uint64_t fence_value)
{
    D3D12VAEncodeContext *ctx = avctx->priv_data;

    CommandAllocator allocator = {
        .command_allocator = pAllocator,
        .fence_value = fence_value,
    };

    av_fifo_write(ctx->allocator_queue, &allocator, 1);

    return 0;
}

static int d3d12va_encode_wait(AVCodecContext *avctx,
                               FFHWBaseEncodePicture *base_pic)
{
    D3D12VAEncodeContext *ctx = avctx->priv_data;
    D3D12VAEncodePicture *pic = base_pic->priv;
    uint64_t completion;

    av_assert0(base_pic->encode_issued);

    if (base_pic->encode_complete) {
        // Already waited for this picture.
        return 0;
    }

    completion = ID3D12Fence_GetCompletedValue(ctx->sync_ctx.fence);
    if (completion < pic->fence_value) {
        if (FAILED(ID3D12Fence_SetEventOnCompletion(ctx->sync_ctx.fence, pic->fence_value,
                                                    ctx->sync_ctx.event)))
            return AVERROR(EINVAL);

        WaitForSingleObjectEx(ctx->sync_ctx.event, INFINITE, FALSE);
    }

    av_log(avctx, AV_LOG_DEBUG, "Sync to pic %"PRId64"/%"PRId64" "
           "(input surface %p).\n", base_pic->display_order,
           base_pic->encode_order, pic->input_surface->texture);

    av_frame_free(&base_pic->input_image);

    base_pic->encode_complete = 1;
    return 0;
}

static int d3d12va_encode_create_metadata_buffers(AVCodecContext *avctx,
                                                  D3D12VAEncodePicture *pic)
{
    D3D12VAEncodeContext *ctx = avctx->priv_data;
    int width = sizeof(D3D12_VIDEO_ENCODER_OUTPUT_METADATA) + sizeof(D3D12_VIDEO_ENCODER_FRAME_SUBREGION_METADATA);
    D3D12_HEAP_PROPERTIES encoded_meta_props = { .Type = D3D12_HEAP_TYPE_DEFAULT }, resolved_meta_props;
    D3D12_HEAP_TYPE resolved_heap_type = D3D12_HEAP_TYPE_READBACK;
    HRESULT hr;

    D3D12_RESOURCE_DESC meta_desc = {
        .Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER,
        .Alignment        = 0,
        .Width            = ctx->req.MaxEncoderOutputMetadataBufferSize,
        .Height           = 1,
        .DepthOrArraySize = 1,
        .MipLevels        = 1,
        .Format           = DXGI_FORMAT_UNKNOWN,
        .SampleDesc       = { .Count = 1, .Quality = 0 },
        .Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
        .Flags            = D3D12_RESOURCE_FLAG_NONE,
    };

    hr = ID3D12Device_CreateCommittedResource(ctx->hwctx->device, &encoded_meta_props, D3D12_HEAP_FLAG_NONE,
                                              &meta_desc, D3D12_RESOURCE_STATE_COMMON, NULL,
                                              &IID_ID3D12Resource, (void **)&pic->encoded_metadata);
    if (FAILED(hr)) {
        av_log(avctx, AV_LOG_ERROR, "Failed to create metadata buffer.\n");
        return AVERROR_UNKNOWN;
    }

    ctx->hwctx->device->lpVtbl->GetCustomHeapProperties(ctx->hwctx->device, &resolved_meta_props, 0, resolved_heap_type);

    meta_desc.Width = width;

    hr = ID3D12Device_CreateCommittedResource(ctx->hwctx->device, &resolved_meta_props, D3D12_HEAP_FLAG_NONE,
                                              &meta_desc, D3D12_RESOURCE_STATE_COMMON, NULL,
                                              &IID_ID3D12Resource, (void **)&pic->resolved_metadata);

    if (FAILED(hr)) {
        av_log(avctx, AV_LOG_ERROR, "Failed to create output metadata buffer.\n");
        return AVERROR_UNKNOWN;
    }

    return 0;
}

static int d3d12va_encode_issue(AVCodecContext *avctx,
                                FFHWBaseEncodePicture *base_pic)
{
    FFHWBaseEncodeContext *base_ctx = avctx->priv_data;
    D3D12VAEncodeContext       *ctx = avctx->priv_data;
    D3D12VAEncodePicture       *pic = base_pic->priv;
    AVD3D12VAFramesContext *frames_hwctx = base_ctx->input_frames->hwctx;
    int err, i, j;
    HRESULT hr;
    char data[MAX_PARAM_BUFFER_SIZE];
    void *ptr;
    size_t bit_len;
    ID3D12CommandAllocator *command_allocator = NULL;
    ID3D12VideoEncodeCommandList2 *cmd_list = ctx->command_list;
    D3D12_RESOURCE_BARRIER barriers[32] = { 0 };
    D3D12_VIDEO_ENCODE_REFERENCE_FRAMES d3d12_refs = { 0 };

    D3D12_VIDEO_ENCODER_ENCODEFRAME_INPUT_ARGUMENTS input_args = {
        .SequenceControlDesc = {
            .Flags = D3D12_VIDEO_ENCODER_SEQUENCE_CONTROL_FLAG_NONE,
            .IntraRefreshConfig = { 0 },
            .RateControl = ctx->rc,
            .PictureTargetResolution = ctx->resolution,
            .SelectedLayoutMode = D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_FULL_FRAME,
            .FrameSubregionsLayoutData = { 0 },
            .CodecGopSequence = ctx->gop,
        },
        .pInputFrame = pic->input_surface->texture,
        .InputFrameSubresource = 0,
    };

    D3D12_VIDEO_ENCODER_ENCODEFRAME_OUTPUT_ARGUMENTS output_args = { 0 };

    D3D12_VIDEO_ENCODER_RESOLVE_METADATA_INPUT_ARGUMENTS input_metadata = {
        .EncoderCodec = ctx->codec->d3d12_codec,
        .EncoderProfile = ctx->profile->d3d12_profile,
        .EncoderInputFormat = frames_hwctx->format,
        .EncodedPictureEffectiveResolution = ctx->resolution,
    };

    D3D12_VIDEO_ENCODER_RESOLVE_METADATA_OUTPUT_ARGUMENTS output_metadata = { 0 };

    memset(data, 0, sizeof(data));

    av_log(avctx, AV_LOG_DEBUG, "Issuing encode for pic %"PRId64"/%"PRId64" "
           "as type %s.\n", base_pic->display_order, base_pic->encode_order,
           ff_hw_base_encode_get_pictype_name(base_pic->type));
    if (base_pic->nb_refs[0] == 0 && base_pic->nb_refs[1] == 0) {
        av_log(avctx, AV_LOG_DEBUG, "No reference pictures.\n");
    } else {
        av_log(avctx, AV_LOG_DEBUG, "L0 refers to");
        for (i = 0; i < base_pic->nb_refs[0]; i++) {
            av_log(avctx, AV_LOG_DEBUG, " %"PRId64"/%"PRId64,
                   base_pic->refs[0][i]->display_order, base_pic->refs[0][i]->encode_order);
        }
        av_log(avctx, AV_LOG_DEBUG, ".\n");

        if (base_pic->nb_refs[1]) {
            av_log(avctx, AV_LOG_DEBUG, "L1 refers to");
            for (i = 0; i < base_pic->nb_refs[1]; i++) {
                av_log(avctx, AV_LOG_DEBUG, " %"PRId64"/%"PRId64,
                       base_pic->refs[1][i]->display_order, base_pic->refs[1][i]->encode_order);
            }
            av_log(avctx, AV_LOG_DEBUG, ".\n");
        }
    }

    av_assert0(!base_pic->encode_issued);
    for (i = 0; i < base_pic->nb_refs[0]; i++) {
        av_assert0(base_pic->refs[0][i]);
        av_assert0(base_pic->refs[0][i]->encode_issued);
    }
    for (i = 0; i < base_pic->nb_refs[1]; i++) {
        av_assert0(base_pic->refs[1][i]);
        av_assert0(base_pic->refs[1][i]->encode_issued);
    }

    av_log(avctx, AV_LOG_DEBUG, "Input surface is %p.\n", pic->input_surface->texture);

    pic->recon_surface = (AVD3D12VAFrame *)base_pic->recon_image->data[0];
    av_log(avctx, AV_LOG_DEBUG, "Recon surface is %p.\n",
           pic->recon_surface->texture);

    pic->output_buffer_ref = av_buffer_pool_get(ctx->output_buffer_pool);
    if (!pic->output_buffer_ref) {
        err = AVERROR(ENOMEM);
        goto fail;
    }
    pic->output_buffer = (ID3D12Resource *)pic->output_buffer_ref->data;
    av_log(avctx, AV_LOG_DEBUG, "Output buffer is %p.\n",
           pic->output_buffer);

    err = d3d12va_encode_create_metadata_buffers(avctx, pic);
    if (err < 0)
        goto fail;

    if (ctx->codec->init_picture_params) {
        err = ctx->codec->init_picture_params(avctx, base_pic);
        if (err < 0) {
            av_log(avctx, AV_LOG_ERROR, "Failed to initialise picture "
                   "parameters: %d.\n", err);
            goto fail;
        }
    }

    if (base_pic->type == FF_HW_PICTURE_TYPE_IDR) {
        if (ctx->codec->write_sequence_header) {
            bit_len = 8 * sizeof(data);
            err = ctx->codec->write_sequence_header(avctx, data, &bit_len);
            if (err < 0) {
                av_log(avctx, AV_LOG_ERROR, "Failed to write per-sequence "
                       "header: %d.\n", err);
                goto fail;
            }
        }

        pic->header_size = (int)bit_len / 8;
        pic->aligned_header_size = pic->header_size % ctx->req.CompressedBitstreamBufferAccessAlignment ?
                                   FFALIGN(pic->header_size, ctx->req.CompressedBitstreamBufferAccessAlignment) :
                                   pic->header_size;

        hr = ID3D12Resource_Map(pic->output_buffer, 0, NULL, (void **)&ptr);
        if (FAILED(hr)) {
            err = AVERROR_UNKNOWN;
            goto fail;
        }

        memcpy(ptr, data, pic->aligned_header_size);
        ID3D12Resource_Unmap(pic->output_buffer, 0, NULL);
    }

    d3d12_refs.NumTexture2Ds = base_pic->nb_refs[0] + base_pic->nb_refs[1];
    if (d3d12_refs.NumTexture2Ds) {
        d3d12_refs.ppTexture2Ds = av_calloc(d3d12_refs.NumTexture2Ds,
                                            sizeof(*d3d12_refs.ppTexture2Ds));
        if (!d3d12_refs.ppTexture2Ds) {
            err = AVERROR(ENOMEM);
            goto fail;
        }

        i = 0;
        for (j = 0; j < base_pic->nb_refs[0]; j++)
            d3d12_refs.ppTexture2Ds[i++] = ((D3D12VAEncodePicture *)base_pic->refs[0][j]->priv)->recon_surface->texture;
        for (j = 0; j < base_pic->nb_refs[1]; j++)
            d3d12_refs.ppTexture2Ds[i++] = ((D3D12VAEncodePicture *)base_pic->refs[1][j]->priv)->recon_surface->texture;
    }

    input_args.PictureControlDesc.IntraRefreshFrameIndex  = 0;
    if (base_pic->is_reference)
        input_args.PictureControlDesc.Flags |= D3D12_VIDEO_ENCODER_PICTURE_CONTROL_FLAG_USED_AS_REFERENCE_PICTURE;

    input_args.PictureControlDesc.PictureControlCodecData = pic->pic_ctl;
    input_args.PictureControlDesc.ReferenceFrames         = d3d12_refs;
    input_args.CurrentFrameBitstreamMetadataSize          = pic->aligned_header_size;

    output_args.Bitstream.pBuffer                                    = pic->output_buffer;
    output_args.Bitstream.FrameStartOffset                           = pic->aligned_header_size;
    output_args.ReconstructedPicture.pReconstructedPicture           = pic->recon_surface->texture;
    output_args.ReconstructedPicture.ReconstructedPictureSubresource = 0;
    output_args.EncoderOutputMetadata.pBuffer                        = pic->encoded_metadata;
    output_args.EncoderOutputMetadata.Offset                         = 0;

    input_metadata.HWLayoutMetadata.pBuffer = pic->encoded_metadata;
    input_metadata.HWLayoutMetadata.Offset  = 0;

    output_metadata.ResolvedLayoutMetadata.pBuffer = pic->resolved_metadata;
    output_metadata.ResolvedLayoutMetadata.Offset  = 0;

    err = d3d12va_get_valid_command_allocator(avctx, &command_allocator);
    if (err < 0)
        goto fail;

    hr = ID3D12CommandAllocator_Reset(command_allocator);
    if (FAILED(hr)) {
        err = AVERROR_UNKNOWN;
        goto fail;
    }

    hr = ID3D12VideoEncodeCommandList2_Reset(cmd_list, command_allocator);
    if (FAILED(hr)) {
        err = AVERROR_UNKNOWN;
        goto fail;
    }

#define TRANSITION_BARRIER(res, before, after)                      \
    (D3D12_RESOURCE_BARRIER) {                                      \
        .Type  = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,            \
        .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,                  \
        .Transition = {                                             \
            .pResource   = res,                                     \
            .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, \
            .StateBefore = before,                                  \
            .StateAfter  = after,                                   \
        },                                                          \
    }

    barriers[0] = TRANSITION_BARRIER(pic->input_surface->texture,
                                     D3D12_RESOURCE_STATE_COMMON,
                                     D3D12_RESOURCE_STATE_VIDEO_ENCODE_READ);
    barriers[1] = TRANSITION_BARRIER(pic->output_buffer,
                                     D3D12_RESOURCE_STATE_COMMON,
                                     D3D12_RESOURCE_STATE_VIDEO_ENCODE_WRITE);
    barriers[2] = TRANSITION_BARRIER(pic->recon_surface->texture,
                                     D3D12_RESOURCE_STATE_COMMON,
                                     D3D12_RESOURCE_STATE_VIDEO_ENCODE_WRITE);
    barriers[3] = TRANSITION_BARRIER(pic->encoded_metadata,
                                     D3D12_RESOURCE_STATE_COMMON,
                                     D3D12_RESOURCE_STATE_VIDEO_ENCODE_WRITE);
    barriers[4] = TRANSITION_BARRIER(pic->resolved_metadata,
                                     D3D12_RESOURCE_STATE_COMMON,
                                     D3D12_RESOURCE_STATE_VIDEO_ENCODE_WRITE);

    ID3D12VideoEncodeCommandList2_ResourceBarrier(cmd_list, 5, barriers);

    if (d3d12_refs.NumTexture2Ds) {
        D3D12_RESOURCE_BARRIER refs_barriers[3];

        for (i = 0; i < d3d12_refs.NumTexture2Ds; i++)
            refs_barriers[i] = TRANSITION_BARRIER(d3d12_refs.ppTexture2Ds[i],
                                                  D3D12_RESOURCE_STATE_COMMON,
                                                  D3D12_RESOURCE_STATE_VIDEO_ENCODE_READ);

        ID3D12VideoEncodeCommandList2_ResourceBarrier(cmd_list, d3d12_refs.NumTexture2Ds,
                                                      refs_barriers);
    }

    ID3D12VideoEncodeCommandList2_EncodeFrame(cmd_list, ctx->encoder, ctx->encoder_heap,
                                              &input_args, &output_args);

    barriers[3] = TRANSITION_BARRIER(pic->encoded_metadata,
                                     D3D12_RESOURCE_STATE_VIDEO_ENCODE_WRITE,
                                     D3D12_RESOURCE_STATE_VIDEO_ENCODE_READ);

    ID3D12VideoEncodeCommandList2_ResourceBarrier(cmd_list, 1, &barriers[3]);

    ID3D12VideoEncodeCommandList2_ResolveEncoderOutputMetadata(cmd_list, &input_metadata, &output_metadata);

    if (d3d12_refs.NumTexture2Ds) {
        D3D12_RESOURCE_BARRIER refs_barriers[3];

        for (i = 0; i < d3d12_refs.NumTexture2Ds; i++)
            refs_barriers[i] = TRANSITION_BARRIER(d3d12_refs.ppTexture2Ds[i],
                                                  D3D12_RESOURCE_STATE_VIDEO_ENCODE_READ,
                                                  D3D12_RESOURCE_STATE_COMMON);

        ID3D12VideoEncodeCommandList2_ResourceBarrier(cmd_list, d3d12_refs.NumTexture2Ds,
                                                      refs_barriers);
    }

    barriers[0] = TRANSITION_BARRIER(pic->input_surface->texture,
                                     D3D12_RESOURCE_STATE_VIDEO_ENCODE_READ,
                                     D3D12_RESOURCE_STATE_COMMON);
    barriers[1] = TRANSITION_BARRIER(pic->output_buffer,
                                     D3D12_RESOURCE_STATE_VIDEO_ENCODE_WRITE,
                                     D3D12_RESOURCE_STATE_COMMON);
    barriers[2] = TRANSITION_BARRIER(pic->recon_surface->texture,
                                     D3D12_RESOURCE_STATE_VIDEO_ENCODE_WRITE,
                                     D3D12_RESOURCE_STATE_COMMON);
    barriers[3] = TRANSITION_BARRIER(pic->encoded_metadata,
                                     D3D12_RESOURCE_STATE_VIDEO_ENCODE_READ,
                                     D3D12_RESOURCE_STATE_COMMON);
    barriers[4] = TRANSITION_BARRIER(pic->resolved_metadata,
                                     D3D12_RESOURCE_STATE_VIDEO_ENCODE_WRITE,
                                     D3D12_RESOURCE_STATE_COMMON);

    ID3D12VideoEncodeCommandList2_ResourceBarrier(cmd_list, 5, barriers);

    hr = ID3D12VideoEncodeCommandList2_Close(cmd_list);
    if (FAILED(hr)) {
        err = AVERROR_UNKNOWN;
        goto fail;
    }

    hr = ID3D12CommandQueue_Wait(ctx->command_queue, pic->input_surface->sync_ctx.fence,
                                 pic->input_surface->sync_ctx.fence_value);
    if (FAILED(hr)) {
        err = AVERROR_UNKNOWN;
        goto fail;
    }

    ID3D12CommandQueue_ExecuteCommandLists(ctx->command_queue, 1, (ID3D12CommandList **)&ctx->command_list);

    hr = ID3D12CommandQueue_Signal(ctx->command_queue, pic->input_surface->sync_ctx.fence,
                                   ++pic->input_surface->sync_ctx.fence_value);
    if (FAILED(hr)) {
        err = AVERROR_UNKNOWN;
        goto fail;
    }

    hr = ID3D12CommandQueue_Signal(ctx->command_queue, ctx->sync_ctx.fence, ++ctx->sync_ctx.fence_value);
    if (FAILED(hr)) {
        err = AVERROR_UNKNOWN;
        goto fail;
    }

    err = d3d12va_discard_command_allocator(avctx, command_allocator, ctx->sync_ctx.fence_value);
    if (err < 0)
        goto fail;

    pic->fence_value = ctx->sync_ctx.fence_value;

    if (d3d12_refs.ppTexture2Ds)
        av_freep(&d3d12_refs.ppTexture2Ds);

    return 0;

fail:
    if (command_allocator)
        d3d12va_discard_command_allocator(avctx, command_allocator, ctx->sync_ctx.fence_value);

    if (d3d12_refs.ppTexture2Ds)
        av_freep(&d3d12_refs.ppTexture2Ds);

    if (ctx->codec->free_picture_params)
        ctx->codec->free_picture_params(pic);

    av_buffer_unref(&pic->output_buffer_ref);
    pic->output_buffer = NULL;
    D3D12_OBJECT_RELEASE(pic->encoded_metadata);
    D3D12_OBJECT_RELEASE(pic->resolved_metadata);
    return err;
}

static int d3d12va_encode_discard(AVCodecContext *avctx,
                                  FFHWBaseEncodePicture *base_pic)
{
    D3D12VAEncodePicture *pic = base_pic->priv;

    d3d12va_encode_wait(avctx, base_pic);

    if (pic->output_buffer_ref) {
        av_log(avctx, AV_LOG_DEBUG, "Discard output for pic "
               "%"PRId64"/%"PRId64".\n",
               base_pic->display_order, base_pic->encode_order);

        av_buffer_unref(&pic->output_buffer_ref);
        pic->output_buffer = NULL;
    }

    D3D12_OBJECT_RELEASE(pic->encoded_metadata);
    D3D12_OBJECT_RELEASE(pic->resolved_metadata);

    return 0;
}

static int d3d12va_encode_free_rc_params(AVCodecContext *avctx)
{
    D3D12VAEncodeContext *ctx = avctx->priv_data;

    switch (ctx->rc.Mode)
    {
    case D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_CQP:
        av_freep(&ctx->rc.ConfigParams.pConfiguration_CQP);
        break;
    case D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_CBR:
        av_freep(&ctx->rc.ConfigParams.pConfiguration_CBR);
        break;
    case D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_VBR:
        av_freep(&ctx->rc.ConfigParams.pConfiguration_VBR);
        break;
    case D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_QVBR:
        av_freep(&ctx->rc.ConfigParams.pConfiguration_QVBR);
        break;
    default:
        break;
    }

    return 0;
}

static int d3d12va_encode_init(AVCodecContext *avctx, FFHWBaseEncodePicture *pic)
{
    D3D12VAEncodeContext *ctx = avctx->priv_data;
    D3D12VAEncodePicture *priv = pic->priv;
    AVFrame *frame = pic->input_image;

    if (ctx->codec->picture_priv_data_size > 0) {
        pic->codec_priv = av_mallocz(ctx->codec->picture_priv_data_size);
        if (!pic->codec_priv)
            return AVERROR(ENOMEM);
    }

    priv->input_surface = (AVD3D12VAFrame *)frame->data[0];

    return 0;
}

static int d3d12va_encode_free(AVCodecContext *avctx, FFHWBaseEncodePicture *pic)
{
    D3D12VAEncodeContext *ctx = avctx->priv_data;
    D3D12VAEncodePicture *priv = pic->priv;

    if (pic->encode_issued)
        d3d12va_encode_discard(avctx, pic);

    if (ctx->codec->free_picture_params)
        ctx->codec->free_picture_params(priv);

    return 0;
}

static int d3d12va_encode_get_buffer_size(AVCodecContext *avctx,
                                          D3D12VAEncodePicture *pic, size_t *size)
{
    D3D12_VIDEO_ENCODER_OUTPUT_METADATA *meta = NULL;
    uint8_t *data;
    HRESULT hr;
    int err;

    hr = ID3D12Resource_Map(pic->resolved_metadata, 0, NULL, (void **)&data);
    if (FAILED(hr)) {
        err = AVERROR_UNKNOWN;
        return err;
    }

    meta = (D3D12_VIDEO_ENCODER_OUTPUT_METADATA *)data;

    if (meta->EncodeErrorFlags != D3D12_VIDEO_ENCODER_ENCODE_ERROR_FLAG_NO_ERROR) {
        av_log(avctx, AV_LOG_ERROR, "Encode failed %"PRIu64"\n", meta->EncodeErrorFlags);
        err = AVERROR(EINVAL);
        return err;
    }

    if (meta->EncodedBitstreamWrittenBytesCount == 0) {
        av_log(avctx, AV_LOG_ERROR, "No bytes were written to encoded bitstream\n");
        err = AVERROR(EINVAL);
        return err;
    }

    *size = meta->EncodedBitstreamWrittenBytesCount;

    ID3D12Resource_Unmap(pic->resolved_metadata, 0, NULL);

    return 0;
}

static int d3d12va_encode_get_coded_data(AVCodecContext *avctx,
                                         D3D12VAEncodePicture *pic, AVPacket *pkt)
{
    int err;
    uint8_t *ptr, *mapped_data;
    size_t total_size = 0;
    HRESULT hr;

    err = d3d12va_encode_get_buffer_size(avctx, pic, &total_size);
    if (err < 0)
        goto end;

    total_size += pic->header_size;
    av_log(avctx, AV_LOG_DEBUG, "Output buffer size %"SIZE_SPECIFIER"\n", total_size);

    hr = ID3D12Resource_Map(pic->output_buffer, 0, NULL, (void **)&mapped_data);
    if (FAILED(hr)) {
        err = AVERROR_UNKNOWN;
        goto end;
    }

    err = ff_get_encode_buffer(avctx, pkt, total_size, 0);
    if (err < 0)
        goto end;
    ptr = pkt->data;

    memcpy(ptr, mapped_data, pic->header_size);

    ptr += pic->header_size;
    mapped_data += pic->aligned_header_size;
    total_size -= pic->header_size;

    memcpy(ptr, mapped_data, total_size);

    ID3D12Resource_Unmap(pic->output_buffer, 0, NULL);

end:
    av_buffer_unref(&pic->output_buffer_ref);
    pic->output_buffer = NULL;
    return err;
}

static int d3d12va_encode_output(AVCodecContext *avctx,
                                 FFHWBaseEncodePicture *base_pic, AVPacket *pkt)
{
    FFHWBaseEncodeContext *base_ctx = avctx->priv_data;
    D3D12VAEncodePicture *pic = base_pic->priv;
    AVPacket *pkt_ptr = pkt;
    int err;

    err = d3d12va_encode_wait(avctx, base_pic);
    if (err < 0)
        return err;

    err = d3d12va_encode_get_coded_data(avctx, pic, pkt);
    if (err < 0)
        return err;

    av_log(avctx, AV_LOG_DEBUG, "Output read for pic %"PRId64"/%"PRId64".\n",
           base_pic->display_order, base_pic->encode_order);

    ff_hw_base_encode_set_output_property(base_ctx, avctx, (FFHWBaseEncodePicture *)base_pic,
                                          pkt_ptr, 0);

    return 0;
}

static int d3d12va_encode_set_profile(AVCodecContext *avctx)
{
    FFHWBaseEncodeContext *base_ctx = avctx->priv_data;
    D3D12VAEncodeContext       *ctx = avctx->priv_data;
    const D3D12VAEncodeProfile *profile;
    const AVPixFmtDescriptor *desc;
    int i, depth;

    desc = av_pix_fmt_desc_get(base_ctx->input_frames->sw_format);
    if (!desc) {
        av_log(avctx, AV_LOG_ERROR, "Invalid input pixfmt (%d).\n",
               base_ctx->input_frames->sw_format);
        return AVERROR(EINVAL);
    }

    depth = desc->comp[0].depth;
    for (i = 1; i < desc->nb_components; i++) {
        if (desc->comp[i].depth != depth) {
            av_log(avctx, AV_LOG_ERROR, "Invalid input pixfmt (%s).\n",
                   desc->name);
            return AVERROR(EINVAL);
        }
    }
    av_log(avctx, AV_LOG_VERBOSE, "Input surface format is %s.\n",
           desc->name);

    av_assert0(ctx->codec->profiles);
    for (i = 0; (ctx->codec->profiles[i].av_profile !=
                 AV_PROFILE_UNKNOWN); i++) {
        profile = &ctx->codec->profiles[i];
        if (depth               != profile->depth ||
            desc->nb_components != profile->nb_components)
            continue;
        if (desc->nb_components > 1 &&
            (desc->log2_chroma_w != profile->log2_chroma_w ||
             desc->log2_chroma_h != profile->log2_chroma_h))
            continue;
        if (avctx->profile != profile->av_profile &&
            avctx->profile != AV_PROFILE_UNKNOWN)
            continue;

        ctx->profile = profile;
        break;
    }
    if (!ctx->profile) {
        av_log(avctx, AV_LOG_ERROR, "No usable encoding profile found.\n");
        return AVERROR(ENOSYS);
    }

    avctx->profile = profile->av_profile;
    return 0;
}

static const D3D12VAEncodeRCMode d3d12va_encode_rc_modes[] = {
    //                   Bitrate   Quality
    //                      | Maxrate | HRD/VBV
    { 0 }, //               |    |    |    |
    { RC_MODE_CQP,  "CQP",  0,   0,   1,   0, D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_CQP },
    { RC_MODE_CBR,  "CBR",  1,   0,   0,   1, D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_CBR },
    { RC_MODE_VBR,  "VBR",  1,   1,   0,   1, D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_VBR },
    { RC_MODE_QVBR, "QVBR", 1,   1,   1,   1, D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_QVBR },
};

static int check_rate_control_support(AVCodecContext *avctx, const D3D12VAEncodeRCMode *rc_mode)
{
    HRESULT hr;
    D3D12VAEncodeContext *ctx = avctx->priv_data;
    D3D12_FEATURE_DATA_VIDEO_ENCODER_RATE_CONTROL_MODE d3d12_rc_mode = {
        .Codec = ctx->codec->d3d12_codec,
    };

    if (!rc_mode->d3d12_mode)
        return 0;

    d3d12_rc_mode.IsSupported = 0;
    d3d12_rc_mode.RateControlMode = rc_mode->d3d12_mode;

    hr = ID3D12VideoDevice3_CheckFeatureSupport(ctx->video_device3,
                                                D3D12_FEATURE_VIDEO_ENCODER_RATE_CONTROL_MODE,
                                                &d3d12_rc_mode, sizeof(d3d12_rc_mode));
    if (FAILED(hr)) {
        av_log(avctx, AV_LOG_ERROR, "Failed to check rate control support.\n");
        return 0;
    }

    return d3d12_rc_mode.IsSupported;
}

static int d3d12va_encode_init_rate_control(AVCodecContext *avctx)
{
    D3D12VAEncodeContext *ctx = avctx->priv_data;
    int64_t rc_target_bitrate;
    int64_t rc_peak_bitrate;
    int     rc_quality;
    int64_t hrd_buffer_size;
    int64_t hrd_initial_buffer_fullness;
    int fr_num, fr_den;
    const D3D12VAEncodeRCMode *rc_mode;

    // Rate control mode selection:
    // * If the user has set a mode explicitly with the rc_mode option,
    //   use it and fail if it is not available.
    // * If an explicit QP option has been set, use CQP.
    // * If the codec is CQ-only, use CQP.
    // * If the QSCALE avcodec option is set, use CQP.
    // * If bitrate and quality are both set, try QVBR.
    // * If quality is set, try CQP.
    // * If bitrate and maxrate are set and have the same value, try CBR.
    // * If a bitrate is set, try VBR, then CBR.
    // * If no bitrate is set, try CQP.

#define TRY_RC_MODE(mode, fail) do { \
        rc_mode = &d3d12va_encode_rc_modes[mode]; \
        if (!(rc_mode->d3d12_mode && check_rate_control_support(avctx, rc_mode))) { \
            if (fail) { \
                av_log(avctx, AV_LOG_ERROR, "Driver does not support %s " \
                       "RC mode.\n", rc_mode->name); \
                return AVERROR(EINVAL); \
            } \
            av_log(avctx, AV_LOG_DEBUG, "Driver does not support %s " \
                   "RC mode.\n", rc_mode->name); \
            rc_mode = NULL; \
        } else { \
            goto rc_mode_found; \
        } \
    } while (0)

    if (ctx->explicit_rc_mode)
        TRY_RC_MODE(ctx->explicit_rc_mode, 1);

    if (ctx->explicit_qp)
        TRY_RC_MODE(RC_MODE_CQP, 1);

    if (ctx->codec->flags & FF_HW_FLAG_CONSTANT_QUALITY_ONLY)
        TRY_RC_MODE(RC_MODE_CQP, 1);

    if (avctx->flags & AV_CODEC_FLAG_QSCALE)
        TRY_RC_MODE(RC_MODE_CQP, 1);

    if (avctx->bit_rate > 0 && avctx->global_quality > 0)
        TRY_RC_MODE(RC_MODE_QVBR, 0);

    if (avctx->global_quality > 0) {
        TRY_RC_MODE(RC_MODE_CQP, 0);
    }

    if (avctx->bit_rate > 0 && avctx->rc_max_rate == avctx->bit_rate)
        TRY_RC_MODE(RC_MODE_CBR, 0);

    if (avctx->bit_rate > 0) {
        TRY_RC_MODE(RC_MODE_VBR, 0);
        TRY_RC_MODE(RC_MODE_CBR, 0);
    } else {
        TRY_RC_MODE(RC_MODE_CQP, 0);
    }

    av_log(avctx, AV_LOG_ERROR, "Driver does not support any "
           "RC mode compatible with selected options.\n");
    return AVERROR(EINVAL);

rc_mode_found:
    if (rc_mode->bitrate) {
        if (avctx->bit_rate <= 0) {
            av_log(avctx, AV_LOG_ERROR, "Bitrate must be set for %s "
                   "RC mode.\n", rc_mode->name);
            return AVERROR(EINVAL);
        }

        if (rc_mode->maxrate) {
            if (avctx->rc_max_rate > 0) {
                if (avctx->rc_max_rate < avctx->bit_rate) {
                    av_log(avctx, AV_LOG_ERROR, "Invalid bitrate settings: "
                           "bitrate (%"PRId64") must not be greater than "
                           "maxrate (%"PRId64").\n", avctx->bit_rate,
                           avctx->rc_max_rate);
                    return AVERROR(EINVAL);
                }
                rc_target_bitrate = avctx->bit_rate;
                rc_peak_bitrate   = avctx->rc_max_rate;
            } else {
                // We only have a target bitrate, but this mode requires
                // that a maximum rate be supplied as well.  Since the
                // user does not want this to be a constraint, arbitrarily
                // pick a maximum rate of double the target rate.
                rc_target_bitrate = avctx->bit_rate;
                rc_peak_bitrate   = 2 * avctx->bit_rate;
            }
        } else {
            if (avctx->rc_max_rate > avctx->bit_rate) {
                av_log(avctx, AV_LOG_WARNING, "Max bitrate is ignored "
                       "in %s RC mode.\n", rc_mode->name);
            }
            rc_target_bitrate = avctx->bit_rate;
            rc_peak_bitrate   = 0;
        }
    } else {
        rc_target_bitrate = 0;
        rc_peak_bitrate   = 0;
    }

    if (rc_mode->quality) {
        if (ctx->explicit_qp) {
            rc_quality = ctx->explicit_qp;
        } else if (avctx->global_quality > 0) {
            if (avctx->flags & AV_CODEC_FLAG_QSCALE)
                rc_quality = avctx->global_quality / FF_QP2LAMBDA;
            else
                rc_quality = avctx->global_quality;
        } else {
            rc_quality = ctx->codec->default_quality;
            av_log(avctx, AV_LOG_WARNING, "No quality level set; "
                   "using default (%d).\n", rc_quality);
        }
    } else {
        rc_quality = 0;
    }

    if (rc_mode->hrd) {
        if (avctx->rc_buffer_size)
            hrd_buffer_size = avctx->rc_buffer_size;
        else if (avctx->rc_max_rate > 0)
            hrd_buffer_size = avctx->rc_max_rate;
        else
            hrd_buffer_size = avctx->bit_rate;
        if (avctx->rc_initial_buffer_occupancy) {
            if (avctx->rc_initial_buffer_occupancy > hrd_buffer_size) {
                av_log(avctx, AV_LOG_ERROR, "Invalid RC buffer settings: "
                       "must have initial buffer size (%d) <= "
                       "buffer size (%"PRId64").\n",
                       avctx->rc_initial_buffer_occupancy, hrd_buffer_size);
                return AVERROR(EINVAL);
            }
            hrd_initial_buffer_fullness = avctx->rc_initial_buffer_occupancy;
        } else {
            hrd_initial_buffer_fullness = hrd_buffer_size * 3 / 4;
        }
    } else {
        if (avctx->rc_buffer_size || avctx->rc_initial_buffer_occupancy) {
            av_log(avctx, AV_LOG_WARNING, "Buffering settings are ignored "
                   "in %s RC mode.\n", rc_mode->name);
        }

        hrd_buffer_size             = 0;
        hrd_initial_buffer_fullness = 0;
    }

    if (rc_target_bitrate          > UINT32_MAX ||
        hrd_buffer_size             > UINT32_MAX ||
        hrd_initial_buffer_fullness > UINT32_MAX) {
        av_log(avctx, AV_LOG_ERROR, "RC parameters of 2^32 or "
               "greater are not supported by D3D12.\n");
        return AVERROR(EINVAL);
    }

    ctx->rc_quality = rc_quality;

    av_log(avctx, AV_LOG_VERBOSE, "RC mode: %s.\n", rc_mode->name);

    if (rc_mode->quality)
        av_log(avctx, AV_LOG_VERBOSE, "RC quality: %d.\n", rc_quality);

    if (rc_mode->hrd) {
        av_log(avctx, AV_LOG_VERBOSE, "RC buffer: %"PRId64" bits, "
               "initial fullness %"PRId64" bits.\n",
               hrd_buffer_size, hrd_initial_buffer_fullness);
    }

    if (avctx->framerate.num > 0 && avctx->framerate.den > 0)
        av_reduce(&fr_num, &fr_den,
                  avctx->framerate.num, avctx->framerate.den, 65535);
    else
        av_reduce(&fr_num, &fr_den,
                  avctx->time_base.den, avctx->time_base.num, 65535);

    av_log(avctx, AV_LOG_VERBOSE, "RC framerate: %d/%d (%.2f fps).\n",
           fr_num, fr_den, (double)fr_num / fr_den);

    ctx->rc.Flags                       = D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_NONE;
    ctx->rc.TargetFrameRate.Numerator   = fr_num;
    ctx->rc.TargetFrameRate.Denominator = fr_den;
    ctx->rc.Mode                        = rc_mode->d3d12_mode;

    switch (rc_mode->mode) {
        case RC_MODE_CQP:
            // cqp ConfigParams will be updated in ctx->codec->configure.
            break;

        case RC_MODE_CBR:
            D3D12_VIDEO_ENCODER_RATE_CONTROL_CBR *cbr_ctl;

            ctx->rc.ConfigParams.DataSize = sizeof(D3D12_VIDEO_ENCODER_RATE_CONTROL_CBR);
            cbr_ctl = av_mallocz(ctx->rc.ConfigParams.DataSize);
            if (!cbr_ctl)
                return AVERROR(ENOMEM);

            cbr_ctl->TargetBitRate      = rc_target_bitrate;
            cbr_ctl->VBVCapacity        = hrd_buffer_size;
            cbr_ctl->InitialVBVFullness = hrd_initial_buffer_fullness;
            ctx->rc.Flags |= D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_VBV_SIZES;

            if (avctx->qmin > 0 || avctx->qmax > 0) {
                cbr_ctl->MinQP = avctx->qmin;
                cbr_ctl->MaxQP = avctx->qmax;
                ctx->rc.Flags |= D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_QP_RANGE;
            }

            ctx->rc.ConfigParams.pConfiguration_CBR = cbr_ctl;
            break;

        case RC_MODE_VBR:
            D3D12_VIDEO_ENCODER_RATE_CONTROL_VBR *vbr_ctl;

            ctx->rc.ConfigParams.DataSize = sizeof(D3D12_VIDEO_ENCODER_RATE_CONTROL_VBR);
            vbr_ctl = av_mallocz(ctx->rc.ConfigParams.DataSize);
            if (!vbr_ctl)
                return AVERROR(ENOMEM);

            vbr_ctl->TargetAvgBitRate   = rc_target_bitrate;
            vbr_ctl->PeakBitRate        = rc_peak_bitrate;
            vbr_ctl->VBVCapacity        = hrd_buffer_size;
            vbr_ctl->InitialVBVFullness = hrd_initial_buffer_fullness;
            ctx->rc.Flags |= D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_VBV_SIZES;

            if (avctx->qmin > 0 || avctx->qmax > 0) {
                vbr_ctl->MinQP = avctx->qmin;
                vbr_ctl->MaxQP = avctx->qmax;
                ctx->rc.Flags |= D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_QP_RANGE;
            }

            ctx->rc.ConfigParams.pConfiguration_VBR = vbr_ctl;
            break;

        case RC_MODE_QVBR:
            D3D12_VIDEO_ENCODER_RATE_CONTROL_QVBR *qvbr_ctl;

            ctx->rc.ConfigParams.DataSize = sizeof(D3D12_VIDEO_ENCODER_RATE_CONTROL_QVBR);
            qvbr_ctl = av_mallocz(ctx->rc.ConfigParams.DataSize);
            if (!qvbr_ctl)
                return AVERROR(ENOMEM);

            qvbr_ctl->TargetAvgBitRate      = rc_target_bitrate;
            qvbr_ctl->PeakBitRate           = rc_peak_bitrate;
            qvbr_ctl->ConstantQualityTarget = rc_quality;

            if (avctx->qmin > 0 || avctx->qmax > 0) {
                qvbr_ctl->MinQP = avctx->qmin;
                qvbr_ctl->MaxQP = avctx->qmax;
                ctx->rc.Flags |= D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_QP_RANGE;
            }

            ctx->rc.ConfigParams.pConfiguration_QVBR = qvbr_ctl;
            break;

        default:
            break;
    }
    return 0;
}

static int d3d12va_encode_init_gop_structure(AVCodecContext *avctx)
{
    FFHWBaseEncodeContext *base_ctx = avctx->priv_data;
    D3D12VAEncodeContext       *ctx = avctx->priv_data;
    uint32_t ref_l0, ref_l1;
    int err;
    HRESULT hr;
    D3D12_FEATURE_DATA_VIDEO_ENCODER_CODEC_PICTURE_CONTROL_SUPPORT support;
    union {
        D3D12_VIDEO_ENCODER_CODEC_PICTURE_CONTROL_SUPPORT_H264 h264;
        D3D12_VIDEO_ENCODER_CODEC_PICTURE_CONTROL_SUPPORT_HEVC hevc;
    } codec_support;

    support.NodeIndex = 0;
    support.Codec     = ctx->codec->d3d12_codec;
    support.Profile   = ctx->profile->d3d12_profile;

    switch (ctx->codec->d3d12_codec) {
        case D3D12_VIDEO_ENCODER_CODEC_H264:
            support.PictureSupport.DataSize = sizeof(codec_support.h264);
            support.PictureSupport.pH264Support = &codec_support.h264;
            break;

        case D3D12_VIDEO_ENCODER_CODEC_HEVC:
            support.PictureSupport.DataSize = sizeof(codec_support.hevc);
            support.PictureSupport.pHEVCSupport = &codec_support.hevc;
            break;

        default:
            av_assert0(0);
    }

    hr = ID3D12VideoDevice3_CheckFeatureSupport(ctx->video_device3, D3D12_FEATURE_VIDEO_ENCODER_CODEC_PICTURE_CONTROL_SUPPORT,
                                                &support, sizeof(support));
    if (FAILED(hr))
        return AVERROR(EINVAL);

    if (support.IsSupported) {
        switch (ctx->codec->d3d12_codec) {
            case D3D12_VIDEO_ENCODER_CODEC_H264:
                ref_l0 = FFMIN(support.PictureSupport.pH264Support->MaxL0ReferencesForP,
                               support.PictureSupport.pH264Support->MaxL1ReferencesForB ?
                               support.PictureSupport.pH264Support->MaxL1ReferencesForB : UINT_MAX);
                ref_l1 = support.PictureSupport.pH264Support->MaxL1ReferencesForB;
                break;

            case D3D12_VIDEO_ENCODER_CODEC_HEVC:
                ref_l0 = FFMIN(support.PictureSupport.pHEVCSupport->MaxL0ReferencesForP,
                               support.PictureSupport.pHEVCSupport->MaxL1ReferencesForB ?
                               support.PictureSupport.pHEVCSupport->MaxL1ReferencesForB : UINT_MAX);
                ref_l1 = support.PictureSupport.pHEVCSupport->MaxL1ReferencesForB;
                break;

            default:
                av_assert0(0);
        }
    } else {
        ref_l0 = ref_l1 = 0;
    }

    if (ref_l0 > 0 && ref_l1 > 0 && ctx->bi_not_empty) {
        base_ctx->p_to_gpb = 1;
        av_log(avctx, AV_LOG_VERBOSE, "Driver does not support P-frames, "
               "replacing them with B-frames.\n");
    }

    err = ff_hw_base_init_gop_structure(base_ctx, avctx, ref_l0, ref_l1, ctx->codec->flags, 0);
    if (err < 0)
        return err;

    return 0;
}

static int d3d12va_create_encoder(AVCodecContext *avctx)
{
    FFHWBaseEncodeContext  *base_ctx     = avctx->priv_data;
    D3D12VAEncodeContext   *ctx          = avctx->priv_data;
    AVD3D12VAFramesContext *frames_hwctx = base_ctx->input_frames->hwctx;
    HRESULT hr;

    D3D12_VIDEO_ENCODER_DESC desc = {
        .NodeMask                     = 0,
        .Flags                        = D3D12_VIDEO_ENCODER_FLAG_NONE,
        .EncodeCodec                  = ctx->codec->d3d12_codec,
        .EncodeProfile                = ctx->profile->d3d12_profile,
        .InputFormat                  = frames_hwctx->format,
        .CodecConfiguration           = ctx->codec_conf,
        .MaxMotionEstimationPrecision = D3D12_VIDEO_ENCODER_MOTION_ESTIMATION_PRECISION_MODE_MAXIMUM,
    };

    hr = ID3D12VideoDevice3_CreateVideoEncoder(ctx->video_device3, &desc, &IID_ID3D12VideoEncoder,
                                               (void **)&ctx->encoder);
    if (FAILED(hr)) {
        av_log(avctx, AV_LOG_ERROR, "Failed to create encoder.\n");
        return AVERROR(EINVAL);
    }

    return 0;
}

static int d3d12va_create_encoder_heap(AVCodecContext *avctx)
{
    D3D12VAEncodeContext *ctx = avctx->priv_data;
    HRESULT hr;

    D3D12_VIDEO_ENCODER_HEAP_DESC desc = {
        .NodeMask             = 0,
        .Flags                = D3D12_VIDEO_ENCODER_FLAG_NONE,
        .EncodeCodec          = ctx->codec->d3d12_codec,
        .EncodeProfile        = ctx->profile->d3d12_profile,
        .EncodeLevel          = ctx->level,
        .ResolutionsListCount = 1,
        .pResolutionList      = &ctx->resolution,
    };

    hr = ID3D12VideoDevice3_CreateVideoEncoderHeap(ctx->video_device3, &desc,
                                                   &IID_ID3D12VideoEncoderHeap, (void **)&ctx->encoder_heap);
    if (FAILED(hr)) {
        av_log(avctx, AV_LOG_ERROR, "Failed to create encoder heap.\n");
        return AVERROR(EINVAL);
    }

    return 0;
}

static void d3d12va_encode_free_buffer(void *opaque, uint8_t *data)
{
    ID3D12Resource *pResource;

    pResource = (ID3D12Resource *)data;
    D3D12_OBJECT_RELEASE(pResource);
}

static AVBufferRef *d3d12va_encode_alloc_output_buffer(void *opaque, size_t size)
{
    AVCodecContext           *avctx = opaque;
    FFHWBaseEncodeContext *base_ctx = avctx->priv_data;
    D3D12VAEncodeContext       *ctx = avctx->priv_data;
    ID3D12Resource *pResource = NULL;
    HRESULT hr;
    AVBufferRef *ref;
    D3D12_HEAP_PROPERTIES heap_props;
    D3D12_HEAP_TYPE heap_type = D3D12_HEAP_TYPE_READBACK;

    D3D12_RESOURCE_DESC desc = {
        .Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER,
        .Alignment        = 0,
        .Width            = FFALIGN(3 * base_ctx->surface_width * base_ctx->surface_height + (1 << 16),
                                    D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT),
        .Height           = 1,
        .DepthOrArraySize = 1,
        .MipLevels        = 1,
        .Format           = DXGI_FORMAT_UNKNOWN,
        .SampleDesc       = { .Count = 1, .Quality = 0 },
        .Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
        .Flags            = D3D12_RESOURCE_FLAG_NONE,
    };

    ctx->hwctx->device->lpVtbl->GetCustomHeapProperties(ctx->hwctx->device, &heap_props, 0, heap_type);

    hr = ID3D12Device_CreateCommittedResource(ctx->hwctx->device, &heap_props, D3D12_HEAP_FLAG_NONE,
                                              &desc, D3D12_RESOURCE_STATE_COMMON, NULL, &IID_ID3D12Resource,
                                              (void **)&pResource);

    if (FAILED(hr)) {
        av_log(avctx, AV_LOG_ERROR, "Failed to create d3d12 buffer.\n");
        return NULL;
    }

    ref = av_buffer_create((uint8_t *)(uintptr_t)pResource,
                           sizeof(pResource),
                           &d3d12va_encode_free_buffer,
                           avctx, AV_BUFFER_FLAG_READONLY);
    if (!ref) {
        D3D12_OBJECT_RELEASE(pResource);
        return NULL;
    }

    return ref;
}

static int d3d12va_encode_prepare_output_buffers(AVCodecContext *avctx)
{
    FFHWBaseEncodeContext    *base_ctx = avctx->priv_data;
    D3D12VAEncodeContext          *ctx = avctx->priv_data;
    AVD3D12VAFramesContext *frames_ctx = base_ctx->input_frames->hwctx;
    HRESULT hr;

    ctx->req.NodeIndex               = 0;
    ctx->req.Codec                   = ctx->codec->d3d12_codec;
    ctx->req.Profile                 = ctx->profile->d3d12_profile;
    ctx->req.InputFormat             = frames_ctx->format;
    ctx->req.PictureTargetResolution = ctx->resolution;

    hr = ID3D12VideoDevice3_CheckFeatureSupport(ctx->video_device3,
                                                D3D12_FEATURE_VIDEO_ENCODER_RESOURCE_REQUIREMENTS,
                                                &ctx->req, sizeof(ctx->req));
    if (FAILED(hr)) {
        av_log(avctx, AV_LOG_ERROR, "Failed to check encoder resource requirements support.\n");
        return AVERROR(EINVAL);
    }

    if (!ctx->req.IsSupported) {
        av_log(avctx, AV_LOG_ERROR, "Encoder resource requirements unsupported.\n");
        return AVERROR(EINVAL);
    }

    ctx->output_buffer_pool = av_buffer_pool_init2(sizeof(ID3D12Resource *), avctx,
                                                   &d3d12va_encode_alloc_output_buffer, NULL);
    if (!ctx->output_buffer_pool)
        return AVERROR(ENOMEM);

    return 0;
}

static int d3d12va_encode_create_command_objects(AVCodecContext *avctx)
{
    D3D12VAEncodeContext *ctx = avctx->priv_data;
    ID3D12CommandAllocator *command_allocator = NULL;
    int err;
    HRESULT hr;

    D3D12_COMMAND_QUEUE_DESC queue_desc = {
        .Type     = D3D12_COMMAND_LIST_TYPE_VIDEO_ENCODE,
        .Priority = 0,
        .Flags    = D3D12_COMMAND_QUEUE_FLAG_NONE,
        .NodeMask = 0,
    };

    ctx->allocator_queue = av_fifo_alloc2(D3D12VA_VIDEO_ENC_ASYNC_DEPTH,
                                          sizeof(CommandAllocator), AV_FIFO_FLAG_AUTO_GROW);
    if (!ctx->allocator_queue)
        return AVERROR(ENOMEM);

    hr = ID3D12Device_CreateFence(ctx->hwctx->device, 0, D3D12_FENCE_FLAG_NONE,
                                  &IID_ID3D12Fence, (void **)&ctx->sync_ctx.fence);
    if (FAILED(hr)) {
        av_log(avctx, AV_LOG_ERROR, "Failed to create fence(%lx)\n", (long)hr);
        err = AVERROR_UNKNOWN;
        goto fail;
    }

    ctx->sync_ctx.event = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (!ctx->sync_ctx.event)
        goto fail;

    err = d3d12va_get_valid_command_allocator(avctx, &command_allocator);
    if (err < 0)
        goto fail;

    hr = ID3D12Device_CreateCommandQueue(ctx->hwctx->device, &queue_desc,
                                         &IID_ID3D12CommandQueue, (void **)&ctx->command_queue);
    if (FAILED(hr)) {
        av_log(avctx, AV_LOG_ERROR, "Failed to create command queue(%lx)\n", (long)hr);
        err = AVERROR_UNKNOWN;
        goto fail;
    }

    hr = ID3D12Device_CreateCommandList(ctx->hwctx->device, 0, queue_desc.Type,
                                        command_allocator, NULL, &IID_ID3D12CommandList,
                                        (void **)&ctx->command_list);
    if (FAILED(hr)) {
        av_log(avctx, AV_LOG_ERROR, "Failed to create command list(%lx)\n", (long)hr);
        err = AVERROR_UNKNOWN;
        goto fail;
    }

    hr = ID3D12VideoEncodeCommandList2_Close(ctx->command_list);
    if (FAILED(hr)) {
        av_log(avctx, AV_LOG_ERROR, "Failed to close the command list(%lx)\n", (long)hr);
        err = AVERROR_UNKNOWN;
        goto fail;
    }

    ID3D12CommandQueue_ExecuteCommandLists(ctx->command_queue, 1, (ID3D12CommandList **)&ctx->command_list);

    err = d3d12va_sync_with_gpu(avctx);
    if (err < 0)
        goto fail;

    err = d3d12va_discard_command_allocator(avctx, command_allocator, ctx->sync_ctx.fence_value);
    if (err < 0)
        goto fail;

    return 0;

fail:
    D3D12_OBJECT_RELEASE(command_allocator);
    return err;
}

static int d3d12va_encode_create_recon_frames(AVCodecContext *avctx)
{
    FFHWBaseEncodeContext *base_ctx = avctx->priv_data;
    AVD3D12VAFramesContext *hwctx;
    enum AVPixelFormat recon_format;
    int err;

    err = ff_hw_base_get_recon_format(base_ctx, NULL, &recon_format);
    if (err < 0)
        return err;

    base_ctx->recon_frames_ref = av_hwframe_ctx_alloc(base_ctx->device_ref);
    if (!base_ctx->recon_frames_ref)
        return AVERROR(ENOMEM);

    base_ctx->recon_frames = (AVHWFramesContext *)base_ctx->recon_frames_ref->data;
    hwctx = (AVD3D12VAFramesContext *)base_ctx->recon_frames->hwctx;

    base_ctx->recon_frames->format    = AV_PIX_FMT_D3D12;
    base_ctx->recon_frames->sw_format = recon_format;
    base_ctx->recon_frames->width     = base_ctx->surface_width;
    base_ctx->recon_frames->height    = base_ctx->surface_height;

    hwctx->flags = D3D12_RESOURCE_FLAG_VIDEO_ENCODE_REFERENCE_ONLY |
                   D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;

    err = av_hwframe_ctx_init(base_ctx->recon_frames_ref);
    if (err < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to initialise reconstructed "
               "frame context: %d.\n", err);
        return err;
    }

    return 0;
}

static const FFHWEncodePictureOperation d3d12va_type = {
    .priv_size = sizeof(D3D12VAEncodePicture),

    .init   = &d3d12va_encode_init,

    .issue  = &d3d12va_encode_issue,

    .output = &d3d12va_encode_output,

    .free   = &d3d12va_encode_free,
};

int ff_d3d12va_encode_receive_packet(AVCodecContext *avctx, AVPacket *pkt)
{
    return ff_hw_base_encode_receive_packet(avctx->priv_data, avctx, pkt);
}

int ff_d3d12va_encode_init(AVCodecContext *avctx)
{
    FFHWBaseEncodeContext *base_ctx = avctx->priv_data;
    D3D12VAEncodeContext       *ctx = avctx->priv_data;
    D3D12_FEATURE_DATA_VIDEO_FEATURE_AREA_SUPPORT support = { 0 };
    int err;
    HRESULT hr;

    err = ff_hw_base_encode_init(avctx, base_ctx);
    if (err < 0)
        goto fail;

    base_ctx->op = &d3d12va_type;

    ctx->hwctx = base_ctx->device->hwctx;

    ctx->resolution.Width  = base_ctx->input_frames->width;
    ctx->resolution.Height = base_ctx->input_frames->height;

    hr = ID3D12Device_QueryInterface(ctx->hwctx->device, &IID_ID3D12Device3, (void **)&ctx->device3);
    if (FAILED(hr)) {
        av_log(avctx, AV_LOG_ERROR, "ID3D12Device3 interface is not supported.\n");
        err = AVERROR_UNKNOWN;
        goto fail;
    }

    hr = ID3D12Device3_QueryInterface(ctx->device3, &IID_ID3D12VideoDevice3, (void **)&ctx->video_device3);
    if (FAILED(hr)) {
        av_log(avctx, AV_LOG_ERROR, "ID3D12VideoDevice3 interface is not supported.\n");
        err = AVERROR_UNKNOWN;
        goto fail;
    }

    if (FAILED(ID3D12VideoDevice3_CheckFeatureSupport(ctx->video_device3, D3D12_FEATURE_VIDEO_FEATURE_AREA_SUPPORT,
                                                      &support, sizeof(support))) && !support.VideoEncodeSupport) {
        av_log(avctx, AV_LOG_ERROR, "D3D12 video device has no video encoder support.\n");
        err = AVERROR(EINVAL);
        goto fail;
    }

    err = d3d12va_encode_set_profile(avctx);
    if (err < 0)
        goto fail;

    err = d3d12va_encode_init_rate_control(avctx);
    if (err < 0)
        goto fail;

    if (ctx->codec->get_encoder_caps) {
        err = ctx->codec->get_encoder_caps(avctx);
        if (err < 0)
            goto fail;
    }

    err = d3d12va_encode_init_gop_structure(avctx);
    if (err < 0)
        goto fail;

    if (!(ctx->codec->flags & FF_HW_FLAG_SLICE_CONTROL) && avctx->slices > 0) {
        av_log(avctx, AV_LOG_WARNING, "Multiple slices were requested "
               "but this codec does not support controlling slices.\n");
    }

    err = d3d12va_encode_create_command_objects(avctx);
    if (err < 0)
        goto fail;

    err = d3d12va_encode_create_recon_frames(avctx);
    if (err < 0)
        goto fail;

    err = d3d12va_encode_prepare_output_buffers(avctx);
    if (err < 0)
        goto fail;

    if (ctx->codec->configure) {
        err = ctx->codec->configure(avctx);
        if (err < 0)
            goto fail;
    }

    if (ctx->codec->init_sequence_params) {
        err = ctx->codec->init_sequence_params(avctx);
        if (err < 0) {
            av_log(avctx, AV_LOG_ERROR, "Codec sequence initialisation "
                   "failed: %d.\n", err);
            goto fail;
        }
    }

    if (ctx->codec->set_level) {
        err = ctx->codec->set_level(avctx);
        if (err < 0)
            goto fail;
    }

    base_ctx->output_delay = base_ctx->b_per_p;
    base_ctx->decode_delay = base_ctx->max_b_depth;

    err = d3d12va_create_encoder(avctx);
    if (err < 0)
        goto fail;

    err = d3d12va_create_encoder_heap(avctx);
    if (err < 0)
        goto fail;

    base_ctx->async_encode = 1;
    base_ctx->encode_fifo = av_fifo_alloc2(base_ctx->async_depth,
                                           sizeof(D3D12VAEncodePicture *), 0);
    if (!base_ctx->encode_fifo)
        return AVERROR(ENOMEM);

    return 0;

fail:
    return err;
}

int ff_d3d12va_encode_close(AVCodecContext *avctx)
{
    int num_allocator = 0;
    FFHWBaseEncodeContext *base_ctx = avctx->priv_data;
    D3D12VAEncodeContext       *ctx = avctx->priv_data;
    FFHWBaseEncodePicture *pic, *next;
    CommandAllocator allocator;

    if (!base_ctx->frame)
        return 0;

    for (pic = base_ctx->pic_start; pic; pic = next) {
        next = pic->next;
        d3d12va_encode_free(avctx, pic);
    }

    d3d12va_encode_free_rc_params(avctx);

    av_buffer_pool_uninit(&ctx->output_buffer_pool);

    D3D12_OBJECT_RELEASE(ctx->command_list);
    D3D12_OBJECT_RELEASE(ctx->command_queue);

    if (ctx->allocator_queue) {
        while (av_fifo_read(ctx->allocator_queue, &allocator, 1) >= 0) {
            num_allocator++;
            D3D12_OBJECT_RELEASE(allocator.command_allocator);
        }

        av_log(avctx, AV_LOG_VERBOSE, "Total number of command allocators reused: %d\n", num_allocator);
    }

    av_fifo_freep2(&ctx->allocator_queue);

    D3D12_OBJECT_RELEASE(ctx->sync_ctx.fence);
    if (ctx->sync_ctx.event)
        CloseHandle(ctx->sync_ctx.event);

    D3D12_OBJECT_RELEASE(ctx->encoder_heap);
    D3D12_OBJECT_RELEASE(ctx->encoder);
    D3D12_OBJECT_RELEASE(ctx->video_device3);
    D3D12_OBJECT_RELEASE(ctx->device3);

    ff_hw_base_encode_close(base_ctx);

    return 0;
}
