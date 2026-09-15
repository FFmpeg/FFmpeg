/*
 * Copyright (c) 2026 Advanced Micro Devices, Inc.
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

/**
 * @file
 * Pure C interface to the D3D12/DirectML GPU pipeline helper used by the
 * ONNX Runtime backend and the dnn_processing filter.
 *
 * This header intentionally never mentions DirectML, OrtDmlApi, or any
 * other execution-provider-specific type: DnnOnnxD3D12Ctx is an opaque
 * handle, and every D3D12/DML/HLSL detail is confined to
 * dnn_onnx_d3d12.c, the only translation unit in the tree that includes
 * the D3D12 and DirectML headers.
 */

#ifndef AVFILTER_DNN_DNN_ONNX_D3D12_H
#define AVFILTER_DNN_DNN_ONNX_D3D12_H

#include "libavutil/frame.h"
#include "../dnn_interface.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DnnOnnxD3D12Ctx DnnOnnxD3D12Ctx;

/**
 * Describes where the tensors ff_dnn_onnx_d3d12_fill_input()/run()/
 * writeback() hand to the execution provider physically live.
 */
typedef enum DnnOnnxD3D12TensorMem {
    /* GPU-resident (VRAM) tensors, bound to the DirectML execution
     * provider via OrtDmlApi without ever touching host memory. */
    DNN_ONNX_D3D12_TENSOR_GPU,
} DnnOnnxD3D12TensorMem;

/**
 * Check whether the D3D12/DirectML path is usable on d3d12_device, without
 * creating any lasting state. Call from a filter's config_output() on
 * every negotiation.
 *
 * @param d3d12_device an ID3D12Device*
 * @param mem the tensor memory mode to check feasibility for.
 * @param is_uma if non-NULL, set to 1 if the device reports a UMA
 *               (integrated/APU-style) memory architecture via
 *               D3D12_FEATURE_ARCHITECTURE1, 0 otherwise.
 * @param log_ctx an AVClass-compatible pointer to use for av_log(), or NULL
 * @return 0 if the path is usable, otherwise a negative AVERROR (e.g.
 *         AVERROR(ENOSYS)).
 */
int ff_dnn_onnx_d3d12_probe(void *d3d12_device, DnnOnnxD3D12TensorMem mem, int *is_uma, void *log_ctx);

/**
 * Create a helper context bound to d3d12_device: a DirectML device and a
 * single dedicated DIRECT command queue;
 * Both our own compute shader dispatches and the DirectML execution
 * provider submit their work to this one queue, so the GPU simply runs
 * everything in submission order -- no extra cross-queue fences are needed
 * to keep the two in sync.
 *
 * @param d3d12_device an ID3D12Device*; the caller retains ownership and
 *                      must keep it alive for the lifetime of the returned
 *                      context.
 * @param mem the tensor memory mode this context will allocate its
 *            per-frame buffers in; fixed for the context's lifetime.
 * @param log_ctx an AVClass-compatible pointer to use for av_log(), or NULL
 * @return a new context, or NULL on failure (logged via log_ctx).
 */
DnnOnnxD3D12Ctx *ff_dnn_onnx_d3d12_create(void *d3d12_device, DnnOnnxD3D12TensorMem mem, void *log_ctx);

/**
 * Release a context created by ff_dnn_onnx_d3d12_create(), including any
 * buffers configured via ff_dnn_onnx_d3d12_config(). Waits for the shared
 * command queue to drain first. *ctx is set to NULL. No-op if *ctx is NULL.
 */
void ff_dnn_onnx_d3d12_free(DnnOnnxD3D12Ctx **ctx);

/**
 * Append the DirectML execution provider to session_options, built on top
 * of ctx's DirectML device and shared command queue instead of a default
 * device ORT would otherwise create internally. Must be called before the
 * OrtSession is created from session_options.
 *
 * @param session_options an OrtSessionOptions*
 * @return 0 on success, negative AVERROR on failure.
 */
int ff_dnn_onnx_d3d12_append_ep(DnnOnnxD3D12Ctx *ctx, void *session_options);

/**
 * Configure ctx's buffers for the given model input/output tensor.
 *
 * Current gate: only channels == 1, dt == DNN_FLOAT and
 * layout == DL_NCHW are accepted; anything else fails with a clear
 * AVERROR(ENOSYS) log line naming the unsupported combination rather than
 * a bare AVERROR(EINVAL).
 *
 * @return 0 on success, negative AVERROR on failure.
 */
int ff_dnn_onnx_d3d12_config(DnnOnnxD3D12Ctx *ctx, int in_w, int in_h,
                              int out_w, int out_h,
                              DNNDataType dt, DNNLayout layout, int channels);

/**
 * Step 1/3 of the per-frame pipeline: extract the luma plane of the D3D12
 * NV12 input frame into ctx's compact GPU tensor buffer (normalizing to
 * [0,1] as it goes), waiting on the frame's decode fence first.
 *
 * @param in the input AVFrame; in->data[0] must be an AVD3D12VAFrame* and
 *           in->hw_frames_ctx's device must be the same one ctx was created
 *           with.
 * @param in_tensor  if non-NULL, set to the opaque OrtValue* wrapping the
 *                   input tensor for this frame.
 * @param out_tensor if non-NULL, set to the opaque OrtValue* wrapping the
 *                   output tensor for this frame.
 * @return 0 on success, negative AVERROR on failure.
 */
int ff_dnn_onnx_d3d12_fill_input(DnnOnnxD3D12Ctx *ctx, const AVFrame *in,
                                  void **in_tensor, void **out_tensor);

/**
 * Step 2/3 of the per-frame pipeline: run the model against the tensors
 * produced by ff_dnn_onnx_d3d12_fill_input(), using IoBinding so that the
 * DirectML EP consumes/produces the GPU tensors directly instead of
 * copying the data out to host (CPU) memory and back.
 *
 * @param session an OrtSession* whose session_options had
 *                 ff_dnn_onnx_d3d12_append_ep() applied.
 * @param in_tensor/out_tensor the opaque OrtValue* pointers returned by
 *                 ff_dnn_onnx_d3d12_fill_input().
 * @return 0 on success, negative AVERROR on failure.
 */
int ff_dnn_onnx_d3d12_run(DnnOnnxD3D12Ctx *ctx, void *session,
                           const char *in_name, const char *out_name,
                           void *in_tensor, void *out_tensor);

/**
 * Step 3/3 of the per-frame pipeline: write ctx's compact GPU output tensor
 * back into the luma plane of the D3D12 NV12 output frame (denormalizing
 * from [0,1] with round-to-nearest, matching swscale's GRAYF32->GRAY8), and
 * copy or upscale the chroma planes.
 * Signals out's sync fence with the work submitted by this call and by the
 * preceding fill_input()/run().
 *
 * @param out the output AVFrame; out->data[0] must be an AVD3D12VAFrame*.
 * @return 0 on success, negative AVERROR on failure.
 */
int ff_dnn_onnx_d3d12_writeback(DnnOnnxD3D12Ctx *ctx, AVFrame *out);

/**
 * Block until all work submitted to ctx's shared command queue, by the
 * helper and by the DirectML execution provider, has completed or the
 * device has been removed.
 *
 * @return 0 on success, negative AVERROR on failure.
 */
int ff_dnn_onnx_d3d12_sync(DnnOnnxD3D12Ctx *ctx);

#ifdef __cplusplus
}
#endif

#endif /* AVFILTER_DNN_DNN_ONNX_D3D12_H */
