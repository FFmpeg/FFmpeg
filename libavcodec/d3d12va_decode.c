/*
 * Direct3D 12 HW acceleration video decoder
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

#include <string.h>
#include <initguid.h>

#include "libavutil/common.h"
#include "libavutil/log.h"
#include "libavutil/mem.h"
#include "libavutil/time.h"
#include "libavutil/imgutils.h"
#include "libavutil/hwcontext_d3d12va_internal.h"
#include "libavutil/hwcontext_d3d12va.h"
#include "avcodec.h"
#include "decode.h"
#include "d3d12va_decode.h"
#include "dxva2_internal.h"

typedef struct HelperObjects {
    ID3D12CommandAllocator *command_allocator;
    ID3D12Resource *buffer;
    uint64_t fence_value;
} HelperObjects;

int ff_d3d12va_get_suitable_max_bitstream_size(AVCodecContext *avctx)
{
    AVHWFramesContext *frames_ctx = D3D12VA_FRAMES_CONTEXT(avctx);
    return av_image_get_buffer_size(frames_ctx->sw_format, avctx->coded_width, avctx->coded_height, 1);
}

unsigned ff_d3d12va_get_surface_index(const AVCodecContext *avctx,
                                      D3D12VADecodeContext *ctx, const AVFrame *frame,
                                      int curr)
{
    AVD3D12VAFrame *f;
    ID3D12Resource *res;
    unsigned i;

    f = (AVD3D12VAFrame *)frame->data[0];
    if (!f)
        goto fail;

    res = f->texture;
    if (!res)
        goto fail;

    for (i = 0; i < ctx->max_num_ref; i++) {
        if (ctx->ref_resources[i] && res == ctx->ref_resources[i]) {
            ctx->used_mask |= 1 << i;
            return i;
        }
    }

    if (curr) {
        for (i = 0; i < ctx->max_num_ref; i++) {
            if (!((ctx->used_mask >> i) & 0x1)) {
                ctx->ref_resources[i] = res;
                return i;
            }
        }
    }

fail:
    av_log((AVCodecContext *)avctx, AV_LOG_WARNING, "Could not get surface index. Using 0 instead.\n");
    return 0;
}

static int d3d12va_get_valid_helper_objects(AVCodecContext *avctx, ID3D12CommandAllocator **ppAllocator,
                                            ID3D12Resource **ppBuffer)
{
    HRESULT hr;
    D3D12VADecodeContext *ctx = D3D12VA_DECODE_CONTEXT(avctx);
    HelperObjects obj = { 0 };
    D3D12_HEAP_PROPERTIES heap_props = { .Type = D3D12_HEAP_TYPE_UPLOAD };

    D3D12_RESOURCE_DESC desc = {
        .Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER,
        .Alignment        = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT,
        .Width            = ctx->bitstream_size,
        .Height           = 1,
        .DepthOrArraySize = 1,
        .MipLevels        = 1,
        .Format           = DXGI_FORMAT_UNKNOWN,
        .SampleDesc       = { .Count = 1, .Quality = 0 },
        .Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
        .Flags            = D3D12_RESOURCE_FLAG_NONE,
    };

    if (av_fifo_peek(ctx->objects_queue, &obj, 1, 0) >= 0) {
        uint64_t completion = ID3D12Fence_GetCompletedValue(ctx->sync_ctx.fence);
        if (completion >= obj.fence_value) {
            *ppAllocator = obj.command_allocator;
            *ppBuffer    = obj.buffer;
            av_fifo_read(ctx->objects_queue, &obj, 1);
            return 0;
        }
    }

    hr = ID3D12Device_CreateCommandAllocator(ctx->device_ctx->device, D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE,
                                             &IID_ID3D12CommandAllocator, (void **)ppAllocator);
    if (FAILED(hr)) {
        av_log(avctx, AV_LOG_ERROR, "Failed to create a new command allocator!\n");
        return AVERROR(EINVAL);
    }

    hr = ID3D12Device_CreateCommittedResource(ctx->device_ctx->device, &heap_props, D3D12_HEAP_FLAG_NONE,
                                              &desc, D3D12_RESOURCE_STATE_GENERIC_READ, NULL,
                                              &IID_ID3D12Resource, (void **)ppBuffer);

    if (FAILED(hr)) {
        av_log(avctx, AV_LOG_ERROR, "Failed to create a new d3d12 buffer!\n");
        return AVERROR(EINVAL);
    }

    return 0;
}

static int d3d12va_discard_helper_objects(AVCodecContext *avctx, ID3D12CommandAllocator *pAllocator,
                                          ID3D12Resource *pBuffer, uint64_t fence_value)
{
    D3D12VADecodeContext *ctx = D3D12VA_DECODE_CONTEXT(avctx);

    HelperObjects obj = {
        .command_allocator = pAllocator,
        .buffer            = pBuffer,
        .fence_value       = fence_value,
    };

    if (av_fifo_write(ctx->objects_queue, &obj, 1) < 0) {
        D3D12_OBJECT_RELEASE(pAllocator);
        D3D12_OBJECT_RELEASE(pBuffer);
        return AVERROR(ENOMEM);
    }

    return 0;
}

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

static void bufref_free_interface(void *opaque, uint8_t *data)
{
    D3D12_OBJECT_RELEASE(opaque);
}

static AVBufferRef *bufref_wrap_interface(IUnknown *iface)
{
    return av_buffer_create((uint8_t*)iface, 1, bufref_free_interface, iface, 0);
}

static int d3d12va_sync_with_gpu(AVCodecContext *avctx)
{
    D3D12VADecodeContext *ctx = D3D12VA_DECODE_CONTEXT(avctx);

    DX_CHECK(ID3D12CommandQueue_Signal(ctx->command_queue, ctx->sync_ctx.fence, ++ctx->sync_ctx.fence_value));
    return d3d12va_fence_completion(&ctx->sync_ctx);

fail:
    return AVERROR(EINVAL);
}

static int d3d12va_create_decoder_heap(AVCodecContext *avctx)
{
    D3D12VADecodeContext   *ctx          = D3D12VA_DECODE_CONTEXT(avctx);
    AVHWFramesContext      *frames_ctx   = D3D12VA_FRAMES_CONTEXT(avctx);
    AVD3D12VADeviceContext *device_hwctx = ctx->device_ctx;
    AVD3D12VAFramesContext *frames_hwctx = frames_ctx->hwctx;

    D3D12_VIDEO_DECODER_HEAP_DESC desc = {
        .NodeMask      = 0,
        .Configuration = ctx->cfg,
        .DecodeWidth   = frames_ctx->width,
        .DecodeHeight  = frames_ctx->height,
        .Format        = frames_hwctx->format,
        .FrameRate     = { avctx->framerate.num, avctx->framerate.den },
        .BitRate       = avctx->bit_rate,
        .MaxDecodePictureBufferCount = ctx->max_num_ref,
    };

    DX_CHECK(ID3D12VideoDevice_CreateVideoDecoderHeap(device_hwctx->video_device, &desc,
             &IID_ID3D12VideoDecoderHeap, (void **)&ctx->decoder_heap));

    return 0;

fail:
    if (ctx->decoder) {
        av_log(avctx, AV_LOG_ERROR, "D3D12 doesn't support decoding frames with an extent "
            "[width(%d), height(%d)], on your device!\n", frames_ctx->width, frames_ctx->height);
    }

    return AVERROR(EINVAL);
}

static int d3d12va_create_decoder(AVCodecContext *avctx)
{
    D3D12_VIDEO_DECODER_DESC desc;
    D3D12VADecodeContext   *ctx          = D3D12VA_DECODE_CONTEXT(avctx);
    AVHWFramesContext      *frames_ctx   = D3D12VA_FRAMES_CONTEXT(avctx);
    AVD3D12VADeviceContext *device_hwctx = ctx->device_ctx;
    AVD3D12VAFramesContext *frames_hwctx = frames_ctx->hwctx;

    D3D12_FEATURE_DATA_VIDEO_DECODE_SUPPORT feature = {
        .NodeIndex     = 0,
        .Configuration = ctx->cfg,
        .Width         = frames_ctx->width,
        .Height        = frames_ctx->height,
        .DecodeFormat  = frames_hwctx->format,
        .FrameRate     = { avctx->framerate.num, avctx->framerate.den },
        .BitRate       = avctx->bit_rate,
    };

    DX_CHECK(ID3D12VideoDevice_CheckFeatureSupport(device_hwctx->video_device, D3D12_FEATURE_VIDEO_DECODE_SUPPORT,
                                                   &feature, sizeof(feature)));
    if (!(feature.SupportFlags & D3D12_VIDEO_DECODE_SUPPORT_FLAG_SUPPORTED)) {
        av_log(avctx, AV_LOG_ERROR, "D3D12 video decode is not supported on this device.\n");
        return AVERROR(ENOSYS);
    }
    if (!(feature.DecodeTier >= D3D12_VIDEO_DECODE_TIER_2)) {
        av_log(avctx, AV_LOG_ERROR, "D3D12 video decode on this device requires tier %d support, "
               "but it is not implemented.\n", feature.DecodeTier);
        return AVERROR_PATCHWELCOME;
    }

    desc = (D3D12_VIDEO_DECODER_DESC) {
        .NodeMask = 0,
        .Configuration = ctx->cfg,
    };

    DX_CHECK(ID3D12VideoDevice_CreateVideoDecoder(device_hwctx->video_device, &desc, &IID_ID3D12VideoDecoder,
                                                  (void **)&ctx->decoder));

    ctx->decoder_ref = bufref_wrap_interface((IUnknown *)ctx->decoder);
    if (!ctx->decoder_ref)
        return AVERROR(ENOMEM);

    return 0;

fail:
    return AVERROR(EINVAL);
}

int ff_d3d12va_common_frame_params(AVCodecContext *avctx, AVBufferRef *hw_frames_ctx)
{
    AVHWFramesContext *frames_ctx = (AVHWFramesContext *)hw_frames_ctx->data;

    frames_ctx->format    = AV_PIX_FMT_D3D12;
    frames_ctx->sw_format = avctx->sw_pix_fmt == AV_PIX_FMT_YUV420P10 ? AV_PIX_FMT_P010 : AV_PIX_FMT_NV12;
    frames_ctx->width     = avctx->width;
    frames_ctx->height    = avctx->height;

    return 0;
}

int ff_d3d12va_decode_init(AVCodecContext *avctx)
{
    int ret;
    AVHWFramesContext *frames_ctx;
    D3D12VADecodeContext *ctx = D3D12VA_DECODE_CONTEXT(avctx);
    ID3D12Resource *buffer = NULL;
    ID3D12CommandAllocator *command_allocator = NULL;
    D3D12_COMMAND_QUEUE_DESC queue_desc = {
        .Type     = D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE,
        .Priority = 0,
        .Flags    = D3D12_COMMAND_QUEUE_FLAG_NONE,
        .NodeMask = 0,
    };

    ctx->pix_fmt = avctx->hwaccel->pix_fmt;

    ret = ff_decode_get_hw_frames_ctx(avctx, AV_HWDEVICE_TYPE_D3D12VA);
    if (ret < 0)
        return ret;

    frames_ctx = D3D12VA_FRAMES_CONTEXT(avctx);
    ctx->device_ctx = (AVD3D12VADeviceContext *)frames_ctx->device_ctx->hwctx;

    if (frames_ctx->format != ctx->pix_fmt) {
        av_log(avctx, AV_LOG_ERROR, "Invalid pixfmt for hwaccel!\n");
        goto fail;
    }

    ret = d3d12va_create_decoder(avctx);
    if (ret < 0)
        goto fail;

    ret = d3d12va_create_decoder_heap(avctx);
    if (ret < 0)
        goto fail;

    ctx->bitstream_size = ff_d3d12va_get_suitable_max_bitstream_size(avctx);

    ctx->ref_resources = av_calloc(ctx->max_num_ref, sizeof(*ctx->ref_resources));
    if (!ctx->ref_resources)
        return AVERROR(ENOMEM);

    ctx->ref_subresources = av_calloc(ctx->max_num_ref, sizeof(*ctx->ref_subresources));
    if (!ctx->ref_subresources)
        return AVERROR(ENOMEM);

    ctx->objects_queue = av_fifo_alloc2(D3D12VA_VIDEO_DEC_ASYNC_DEPTH,
                                        sizeof(HelperObjects), AV_FIFO_FLAG_AUTO_GROW);
    if (!ctx->objects_queue)
        return AVERROR(ENOMEM);

    DX_CHECK(ID3D12Device_CreateFence(ctx->device_ctx->device, 0, D3D12_FENCE_FLAG_NONE,
                                      &IID_ID3D12Fence, (void **)&ctx->sync_ctx.fence));

    ctx->sync_ctx.event = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (!ctx->sync_ctx.event)
        goto fail;

    ret = d3d12va_get_valid_helper_objects(avctx, &command_allocator, &buffer);
    if (ret < 0)
        goto fail;

    DX_CHECK(ID3D12Device_CreateCommandQueue(ctx->device_ctx->device, &queue_desc,
             &IID_ID3D12CommandQueue, (void **)&ctx->command_queue));

    DX_CHECK(ID3D12Device_CreateCommandList(ctx->device_ctx->device, 0, queue_desc.Type,
             command_allocator, NULL, &IID_ID3D12CommandList, (void **)&ctx->command_list));

    DX_CHECK(ID3D12VideoDecodeCommandList_Close(ctx->command_list));

    ID3D12CommandQueue_ExecuteCommandLists(ctx->command_queue, 1, (ID3D12CommandList **)&ctx->command_list);

    ret = d3d12va_sync_with_gpu(avctx);
    if (ret < 0)
        goto fail;

    d3d12va_discard_helper_objects(avctx, command_allocator, buffer, ctx->sync_ctx.fence_value);
    if (ret < 0)
        goto fail;

    return 0;

fail:
    D3D12_OBJECT_RELEASE(command_allocator);
    D3D12_OBJECT_RELEASE(buffer);
    ff_d3d12va_decode_uninit(avctx);

    return AVERROR(EINVAL);
}

int ff_d3d12va_decode_uninit(AVCodecContext *avctx)
{
    int num_allocator = 0;
    D3D12VADecodeContext *ctx = D3D12VA_DECODE_CONTEXT(avctx);
    HelperObjects obj;

    if (ctx->sync_ctx.fence)
        d3d12va_sync_with_gpu(avctx);

    av_freep(&ctx->ref_resources);
    av_freep(&ctx->ref_subresources);

    D3D12_OBJECT_RELEASE(ctx->command_list);
    D3D12_OBJECT_RELEASE(ctx->command_queue);

    if (ctx->objects_queue) {
        while (av_fifo_read(ctx->objects_queue, &obj, 1) >= 0) {
            num_allocator++;
            D3D12_OBJECT_RELEASE(obj.buffer);
            D3D12_OBJECT_RELEASE(obj.command_allocator);
        }

        av_log(avctx, AV_LOG_VERBOSE, "Total number of command allocators reused: %d\n", num_allocator);
    }

    av_fifo_freep2(&ctx->objects_queue);

    D3D12_OBJECT_RELEASE(ctx->sync_ctx.fence);
    if (ctx->sync_ctx.event)
        CloseHandle(ctx->sync_ctx.event);

    D3D12_OBJECT_RELEASE(ctx->decoder_heap);

    av_buffer_unref(&ctx->decoder_ref);

    return 0;
}

static inline int d3d12va_update_reference_frames_state(AVCodecContext *avctx, D3D12_RESOURCE_BARRIER *barriers,
                                                        ID3D12Resource *current_resource, int state_before, int state_end)
{
    D3D12VADecodeContext *ctx = D3D12VA_DECODE_CONTEXT(avctx);

    int num_barrier = 0;
    for (int i = 0; i < ctx->max_num_ref; i++) {
        if (((ctx->used_mask >> i) & 0x1) && ctx->ref_resources[i] && ctx->ref_resources[i] != current_resource) {
            barriers[num_barrier].Type  = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barriers[num_barrier].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            barriers[num_barrier].Transition = (D3D12_RESOURCE_TRANSITION_BARRIER){
                .pResource   = ctx->ref_resources[i],
                .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                .StateBefore = state_before,
                .StateAfter  = state_end,
            };
            num_barrier++;
        }
    }

    return num_barrier;
}

int ff_d3d12va_common_end_frame(AVCodecContext *avctx, AVFrame *frame,
                              const void *pp, unsigned pp_size,
                              const void *qm, unsigned qm_size,
                              int(*update_input_arguments)(AVCodecContext *, D3D12_VIDEO_DECODE_INPUT_STREAM_ARGUMENTS *, ID3D12Resource *))
{
    int ret;
    D3D12VADecodeContext   *ctx               = D3D12VA_DECODE_CONTEXT(avctx);
    ID3D12Resource         *buffer            = NULL;
    ID3D12CommandAllocator *command_allocator = NULL;
    AVD3D12VAFrame         *f                 = (AVD3D12VAFrame *)frame->data[0];
    ID3D12Resource         *resource          = (ID3D12Resource *)f->texture;

    ID3D12VideoDecodeCommandList *cmd_list = ctx->command_list;
    D3D12_RESOURCE_BARRIER barriers[32] = { 0 };

    D3D12_VIDEO_DECODE_INPUT_STREAM_ARGUMENTS input_args = {
        .NumFrameArguments = 2,
        .FrameArguments = {
            [0] = {
                .Type  = D3D12_VIDEO_DECODE_ARGUMENT_TYPE_PICTURE_PARAMETERS,
                .Size  = pp_size,
                .pData = (void *)pp,
            },
            [1] = {
                .Type  = D3D12_VIDEO_DECODE_ARGUMENT_TYPE_INVERSE_QUANTIZATION_MATRIX,
                .Size  = qm_size,
                .pData = (void *)qm,
            },
        },
        .pHeap = ctx->decoder_heap,
    };

    D3D12_VIDEO_DECODE_OUTPUT_STREAM_ARGUMENTS output_args = {
        .ConversionArguments = { 0 },
        .OutputSubresource   = 0,
        .pOutputTexture2D    = resource,
    };

    UINT num_barrier = 1;
    barriers[0] = (D3D12_RESOURCE_BARRIER) {
        .Type  = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
        .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
        .Transition = {
            .pResource   = resource,
            .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
            .StateBefore = D3D12_RESOURCE_STATE_COMMON,
            .StateAfter  = D3D12_RESOURCE_STATE_VIDEO_DECODE_WRITE,
        },
    };

    memset(ctx->ref_subresources, 0, sizeof(UINT) * ctx->max_num_ref);
    input_args.ReferenceFrames.NumTexture2Ds = ctx->max_num_ref;
    input_args.ReferenceFrames.ppTexture2Ds  = ctx->ref_resources;
    input_args.ReferenceFrames.pSubresources = ctx->ref_subresources;

    ret = d3d12va_fence_completion(&f->sync_ctx);
    if (ret < 0)
        goto fail;

    if (!qm)
        input_args.NumFrameArguments = 1;

    ret = d3d12va_get_valid_helper_objects(avctx, &command_allocator, &buffer);
    if (ret < 0)
        goto fail;

    ret = update_input_arguments(avctx, &input_args, buffer);
    if (ret < 0)
        goto fail;

    DX_CHECK(ID3D12CommandAllocator_Reset(command_allocator));

    DX_CHECK(ID3D12VideoDecodeCommandList_Reset(cmd_list, command_allocator));

    num_barrier += d3d12va_update_reference_frames_state(avctx, &barriers[1], resource, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_VIDEO_DECODE_READ);

    ID3D12VideoDecodeCommandList_ResourceBarrier(cmd_list, num_barrier, barriers);

    ID3D12VideoDecodeCommandList_DecodeFrame(cmd_list, ctx->decoder, &output_args, &input_args);

    for (int i = 0; i < num_barrier; i++)
        FFSWAP(D3D12_RESOURCE_STATES, barriers[i].Transition.StateBefore, barriers[i].Transition.StateAfter);

    ID3D12VideoDecodeCommandList_ResourceBarrier(cmd_list, num_barrier, barriers);

    DX_CHECK(ID3D12VideoDecodeCommandList_Close(cmd_list));

    ID3D12CommandQueue_ExecuteCommandLists(ctx->command_queue, 1, (ID3D12CommandList **)&ctx->command_list);

    DX_CHECK(ID3D12CommandQueue_Signal(ctx->command_queue, f->sync_ctx.fence, ++f->sync_ctx.fence_value));

    DX_CHECK(ID3D12CommandQueue_Signal(ctx->command_queue, ctx->sync_ctx.fence, ++ctx->sync_ctx.fence_value));

    ret = d3d12va_discard_helper_objects(avctx, command_allocator, buffer, ctx->sync_ctx.fence_value);
    if (ret < 0)
        return ret;

    return 0;

fail:
    if (command_allocator)
        d3d12va_discard_helper_objects(avctx, command_allocator, buffer, ctx->sync_ctx.fence_value);
    return AVERROR(EINVAL);
}
