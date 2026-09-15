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

#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <d3d12.h>
#include <d3dcompiler.h>

#include "onnxruntime_c_api.h"
#include "dml_provider_factory.h"

DEFINE_GUID(IID_IDMLDevice, 0x6dbd6437, 0x96fd, 0x423f,
            0xa9, 0x8c, 0xae, 0x5e, 0x7c, 0x2a, 0x57, 0x3f);
typedef HRESULT(WINAPI *PFN_DMLCreateDevice)(ID3D12Device *, UINT, REFIID, void **);

#include "config.h"
#include "libavutil/error.h"
#include "libavutil/fifo.h"
#include "libavutil/hwcontext_d3d12va.h"
#include "libavutil/log.h"
#include "libavutil/macros.h"
#include "libavutil/mem.h"
#include "libavutil/time.h"
#include "dnn_onnx_d3d12.h"

#include <stdint.h>
#include <string.h>

static inline HMODULE onnx_d3d12_dlopen(const char *name)
{
    return LoadLibraryExA(name, NULL,
                           LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_APPLICATION_DIR);
}

/*
 * All three compute shaders share one root signature: 8 32-bit root
 * constants at b0 (each shader only names/uses the subset it needs; the
 * layout must stay identical across all of them so a single
 * ID3D12RootSignature works for all three ID3D12PipelineState objects), plus
 * two UAV root descriptors (u0, u1) bound directly to a buffer's GPU virtual
 * address -- no descriptor heap is needed since every buffer is consumed as
 * a raw RWByteAddressBuffer.
 *
 * The shader bodies below are HLSL source, stringized via the
 * HLSL_SHADER() macro (the same trick used in vsrc_gfxcapture_shader.h) so
 * they can be written as plain, syntax-highlightable HLSL instead of
 * manually escaped C string literals.
 */
#define HLSL_SHADER(shader) #shader

static const char *kShaderCompactY = HLSL_SHADER(
    cbuffer Params : register(b0) {
        uint width; uint height; uint src_pitch; uint dst_pitch;
        uint pad4; uint pad5; uint pad6; uint pad7;
    };
    RWByteAddressBuffer SrcBuf : register(u0); // padded byte plane, 1 Bpp
    RWByteAddressBuffer DstBuf : register(u1); // compact FP32 tensor
    [numthreads(8, 8, 1)]
    void CSCompactY(uint3 id : SV_DispatchThreadID)
    {
        if (id.x >= width || id.y >= height) return;
        uint src_off = id.y * src_pitch + id.x;
        uint aligned = src_off & ~3u;
        uint shift = (src_off & 3u) * 8u;
        uint byte_val = (SrcBuf.Load(aligned) >> shift) & 0xFFu;
        float v = float(byte_val) * (1.0f / 255.0f);
        uint dst_off = (id.y * width + id.x) * 4u;
        DstBuf.Store(dst_off, asuint(v));
    }
);

static const char *kShaderExpandY = HLSL_SHADER(
    cbuffer Params : register(b0) {
        uint width; uint height; uint src_pitch; uint dst_pitch;
        uint pad4; uint pad5; uint pad6; uint pad7;
    };
    RWByteAddressBuffer SrcBuf : register(u0); // compact FP32 tensor
    RWByteAddressBuffer DstBuf : register(u1); // padded byte plane, 1 Bpp
    [numthreads(8, 8, 1)]
    void CSExpandY(uint3 id : SV_DispatchThreadID)
    {
        if (id.x >= width || id.y >= height) return;
        uint src_off = (id.y * width + id.x) * 4u;
        float v = asfloat(SrcBuf.Load(src_off));
        float scaled = clamp(v * 255.0f, 0.0f, 255.0f);
        uint byte_val = (uint)round(scaled);
        uint dst_off = id.y * dst_pitch + id.x;
        uint aligned = dst_off & ~3u;
        uint shift = (dst_off & 3u) * 8u;
        uint mask = 0xFFu << shift;
        uint prev;
        DstBuf.InterlockedAnd(aligned, ~mask, prev);
        DstBuf.InterlockedOr(aligned, (byte_val & 0xFFu) << shift, prev);
    }
);

/* Params here means (dst_width, dst_height, src_width, src_height, src_pitch,
 * dst_pitch, -, -) instead of (width, height, src_pitch, dst_pitch, ...): the
 * root signature layout (8 constants) is shared, the meaning is shader-local. */
static const char *kShaderUpscaleUV = HLSL_SHADER(
    cbuffer Params : register(b0) {
        uint dst_width; uint dst_height; uint src_width; uint src_height;
        uint src_pitch; uint dst_pitch; uint pad6; uint pad7;
    };
    RWByteAddressBuffer SrcBuf : register(u0); // padded interleaved UV plane, 2 Bpp
    RWByteAddressBuffer DstBuf : register(u1); // padded interleaved UV plane, 2 Bpp
    uint LoadUV(uint x, uint y, uint comp)
    {
        x = min(x, src_width - 1); y = min(y, src_height - 1);
        uint off = y * src_pitch + x * 2u + comp;
        uint aligned = off & ~3u; uint shift = (off & 3u) * 8u;
        return (SrcBuf.Load(aligned) >> shift) & 0xFFu;
    }
    void StoreUV(uint x, uint y, uint comp, uint value)
    {
        uint off = y * dst_pitch + x * 2u + comp;
        uint aligned = off & ~3u; uint shift = (off & 3u) * 8u;
        uint mask = 0xFFu << shift; uint prev;
        DstBuf.InterlockedAnd(aligned, ~mask, prev);
        DstBuf.InterlockedOr(aligned, (value & 0xFFu) << shift, prev);
    }
    [numthreads(8, 8, 1)]
    void CSUpscaleUV(uint3 id : SV_DispatchThreadID)
    {
        if (id.x >= dst_width || id.y >= dst_height) return;
        float sx = (float(id.x) + 0.5f) * float(src_width)  / float(dst_width)  - 0.5f;
        float sy = (float(id.y) + 0.5f) * float(src_height) / float(dst_height) - 0.5f;
        int x0 = (int)floor(sx);
        int y0 = (int)floor(sy);
        float fx = sx - float(x0);
        float fy = sy - float(y0);
        uint x0c = (uint)clamp(x0,     0, (int)src_width  - 1);
        uint x1c = (uint)clamp(x0 + 1, 0, (int)src_width  - 1);
        uint y0c = (uint)clamp(y0,     0, (int)src_height - 1);
        uint y1c = (uint)clamp(y0 + 1, 0, (int)src_height - 1);
        for (uint comp = 0; comp < 2; comp++) {
            float v00 = float(LoadUV(x0c, y0c, comp));
            float v10 = float(LoadUV(x1c, y0c, comp));
            float v01 = float(LoadUV(x0c, y1c, comp));
            float v11 = float(LoadUV(x1c, y1c, comp));
            float v = lerp(lerp(v00, v10, fx), lerp(v01, v11, fx), fy);
            uint bv = (uint)round(clamp(v, 0.0f, 255.0f));
            StoreUV(id.x, id.y, comp, bv);
        }
    }
);

#undef HLSL_SHADER

typedef struct DnnOnnxD3D12CmdAlloc {
    ID3D12CommandAllocator *alloc;
    UINT64 fence_value;
} DnnOnnxD3D12CmdAlloc;

struct DnnOnnxD3D12Ctx {
    void *log_ctx;
    DnnOnnxD3D12TensorMem mem;

    ID3D12Device *device;
    IDMLDevice *dml_device;
    /* DIRECT queue, shared with the DirectML EP */
    ID3D12CommandQueue *queue;
    const OrtApi *ort;
    const OrtDmlApi *dml_api;

    HMODULE directml_dll;
    HMODULE d3dcompiler_dll;
    HMODULE d3d12_dll;
    pD3DCompile compile_fn;

    PFN_D3D12_SERIALIZE_ROOT_SIGNATURE serialize_root_sig_fn;

    ID3D12RootSignature *root_sig;
    ID3D12PipelineState *pso_compact_y;
    ID3D12PipelineState *pso_expand_y;
    ID3D12PipelineState *pso_upscale_uv;

    AVFifo *alloc_queue;
    ID3D12GraphicsCommandList *cmd_list;

    ID3D12Fence *fence;
    UINT64 fence_value;
    HANDLE fence_event;

    /* the two compact tensors DML actually touches */
    ID3D12Resource *in_buf;
    ID3D12Resource *out_buf;

    /* padded transit buffers whose row pitch matches CopyTextureRegion's
     * 256-byte-aligned requirement.
     * pad_y_src/pad_y_dst additionally bridge to the tight (unpadded)
     * in_buf/out_buf tensors above via CSCompactY/CSExpandY; pad_uv_src and
     * pad_uv_dst hold the interleaved-UV (R8G8) bytes directly and are
     * read/written between each other by CSUpscaleUV */
    ID3D12Resource *pad_y_src;
    ID3D12Resource *pad_y_dst;
    ID3D12Resource *pad_uv_src;
    ID3D12Resource *pad_uv_dst;

    void *dml_in_alloc;
    void *dml_out_alloc;
    OrtMemoryInfo *dml_mem_info;
    OrtValue *in_tensor;
    OrtValue *out_tensor;

    int configured;
    int in_w, in_h, out_w, out_h, channels;
    DNNDataType dt;
    DNNLayout layout;
    int uv_passthrough;

    UINT y_src_pitch, y_dst_pitch, uv_src_pitch, uv_dst_pitch;
    UINT y_src_rows, y_dst_rows, uv_src_rows, uv_dst_rows;
};

static void transition(ID3D12GraphicsCommandList *cl, ID3D12Resource *res, UINT subresource,
                        D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
    D3D12_RESOURCE_BARRIER barrier = { 0 };
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource   = res;
    barrier.Transition.Subresource = subresource;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter  = after;
    ID3D12GraphicsCommandList_ResourceBarrier(cl, 1, &barrier);
}

static void uav_barrier(ID3D12GraphicsCommandList *cl, ID3D12Resource *res)
{
    D3D12_RESOURCE_BARRIER barrier = { 0 };
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = res;
    ID3D12GraphicsCommandList_ResourceBarrier(cl, 1, &barrier);
}

static int wait_for_fence(DnnOnnxD3D12Ctx *s)
{
    HRESULT hr;
    int poll = 0;

    if (ID3D12Fence_GetCompletedValue(s->fence) >= s->fence_value)
        return 0;

    hr = ID3D12Fence_SetEventOnCompletion(s->fence, s->fence_value, s->fence_event);
    if (FAILED(hr)) {
        av_log(s->log_ctx, AV_LOG_WARNING,
               "dnn_onnx_d3d12: SetEventOnCompletion failed: 0x%lX, polling the GPU fence\n", (long)hr);
        poll = 1;
    }

    while (ID3D12Fence_GetCompletedValue(s->fence) < s->fence_value) {
        if (poll) {
            av_usleep(1000);
        } else {
            DWORD wait_result = WaitForSingleObject(s->fence_event, 5000);
            if (wait_result == WAIT_OBJECT_0)
                continue;
            if (wait_result == WAIT_TIMEOUT) {
                av_log(s->log_ctx, AV_LOG_WARNING,
                       "dnn_onnx_d3d12: still waiting on GPU fence after 5s (want=%llu, completed=%llu)\n",
                       (unsigned long long)s->fence_value,
                       (unsigned long long)ID3D12Fence_GetCompletedValue(s->fence));
            } else {
                av_log(s->log_ctx, AV_LOG_WARNING,
                       "dnn_onnx_d3d12: WaitForSingleObject failed: %lu, polling the GPU fence\n",
                       (unsigned long)GetLastError());
                poll = 1;
            }
        }
        if (FAILED(ID3D12Device_GetDeviceRemovedReason(s->device)))
            break;
    }

    hr = ID3D12Device_GetDeviceRemovedReason(s->device);
    if (FAILED(hr)) {
        av_log(s->log_ctx, AV_LOG_ERROR,
               "dnn_onnx_d3d12: D3D12 device removed while waiting on GPU fence: 0x%08x\n", (unsigned int)hr);
        return AVERROR_EXTERNAL;
    }
    return 0;
}

static int wait_queue_idle(DnnOnnxD3D12Ctx *s)
{
    HRESULT hr = ID3D12CommandQueue_Signal(s->queue, s->fence, s->fence_value + 1);
    if (FAILED(hr)) {
        av_log(s->log_ctx, AV_LOG_ERROR, "dnn_onnx_d3d12: Signal failed: 0x%lX\n", (long)hr);
        return AVERROR_EXTERNAL;
    }
    s->fence_value++;
    return wait_for_fence(s);
}

static int create_uav_buffer(ID3D12Device *device, UINT64 size, ID3D12Resource **out)
{
    D3D12_HEAP_PROPERTIES heap_props = { 0 };
    D3D12_RESOURCE_DESC desc = { 0 };
    HRESULT hr;

    heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;

    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width     = size;
    desc.Height    = 1;
    desc.MipLevels = 1;
    desc.Format    = DXGI_FORMAT_UNKNOWN;
    desc.DepthOrArraySize = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags  = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    hr = ID3D12Device_CreateCommittedResource(device, &heap_props, D3D12_HEAP_FLAG_NONE, &desc,
                                               D3D12_RESOURCE_STATE_COMMON, NULL,
                                               &IID_ID3D12Resource, (void **)out);
    return SUCCEEDED(hr) ? 0 : AVERROR(ENOMEM);
}

static void release_buffers(DnnOnnxD3D12Ctx *s)
{
    OrtStatus *status;

    if (s->in_tensor) {
        s->ort->ReleaseValue(s->in_tensor);
        s->in_tensor = NULL;
    }
    if (s->out_tensor) {
        s->ort->ReleaseValue(s->out_tensor);
        s->out_tensor = NULL;
    }

    /* DML allocations must be freed before the D3D12 resources they wrap. */
    if (s->dml_in_alloc) {
        status = s->dml_api->FreeGPUAllocation(s->dml_in_alloc);
        if (status)
            s->ort->ReleaseStatus(status);
        s->dml_in_alloc = NULL;
    }
    if (s->dml_out_alloc) {
        status = s->dml_api->FreeGPUAllocation(s->dml_out_alloc);
        if (status)
            s->ort->ReleaseStatus(status);
        s->dml_out_alloc = NULL;
    }
    if (s->dml_mem_info) {
        s->ort->ReleaseMemoryInfo(s->dml_mem_info);
        s->dml_mem_info = NULL;
    }

    if (s->in_buf) {
        ID3D12Resource_Release(s->in_buf);
        s->in_buf = NULL;
    }
    if (s->out_buf) {
        ID3D12Resource_Release(s->out_buf);
        s->out_buf = NULL;
    }
    if (s->pad_y_src) {
        ID3D12Resource_Release(s->pad_y_src);
        s->pad_y_src = NULL;
    }
    if (s->pad_y_dst) {
        ID3D12Resource_Release(s->pad_y_dst);
        s->pad_y_dst = NULL;
    }
    if (s->pad_uv_src) {
        ID3D12Resource_Release(s->pad_uv_src);
        s->pad_uv_src = NULL;
    }
    if (s->pad_uv_dst) {
        ID3D12Resource_Release(s->pad_uv_dst);
        s->pad_uv_dst = NULL;
    }

    s->configured = 0;
}

static int create_root_signature(DnnOnnxD3D12Ctx *s)
{
    D3D12_ROOT_PARAMETER params[3] = { 0 };
    D3D12_ROOT_SIGNATURE_DESC desc = { 0 };
    ID3DBlob *sig_blob = NULL, *err_blob = NULL;
    HRESULT hr;

    /*The 3 compute shaders HLSL use the exact same root signature:
     * the 3 registers are:
     * 0: Constants (8 32-bit values)
     * 1: UAV (Unordered Access View)
     * 2: UAV (Unordered Access View)
     */
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants.ShaderRegister = 0;
    params[0].Constants.Num32BitValues = 8;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    params[1].Descriptor.ShaderRegister = 0;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    params[2].Descriptor.ShaderRegister = 1;
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    desc.NumParameters = 3;
    desc.pParameters   = params;
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    hr = s->serialize_root_sig_fn(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &sig_blob, &err_blob);
    if (FAILED(hr)) {
        av_log(s->log_ctx, AV_LOG_ERROR, "dnn_onnx_d3d12: root signature serialize failed: %s\n",
               err_blob ? (const char *)ID3D10Blob_GetBufferPointer(err_blob) : "unknown error");
        if (err_blob)
            ID3D10Blob_Release(err_blob);
        return AVERROR_EXTERNAL;
    }
    if (err_blob)
        ID3D10Blob_Release(err_blob);

    hr = ID3D12Device_CreateRootSignature(s->device, 0, ID3D10Blob_GetBufferPointer(sig_blob),
                                           ID3D10Blob_GetBufferSize(sig_blob),
                                           &IID_ID3D12RootSignature, (void **)&s->root_sig);
    ID3D10Blob_Release(sig_blob);
    if (FAILED(hr)) {
        av_log(s->log_ctx, AV_LOG_ERROR, "dnn_onnx_d3d12: CreateRootSignature failed: 0x%lX\n", (long)hr);
        return AVERROR_EXTERNAL;
    }
    return 0;
}

static int compile_and_create_pso(DnnOnnxD3D12Ctx *s, const char *src, const char *entry,
                                   ID3D12PipelineState **out)
{
    ID3DBlob *code = NULL, *err = NULL;
    D3D12_COMPUTE_PIPELINE_STATE_DESC desc = { 0 };
    HRESULT hr;

    hr = s->compile_fn(src, strlen(src), NULL, NULL, NULL, entry, "cs_5_0", 0, 0, &code, &err);
    if (FAILED(hr)) {
        av_log(s->log_ctx, AV_LOG_ERROR, "dnn_onnx_d3d12: failed to compile %s: %s\n", entry,
               err ? (const char *)ID3D10Blob_GetBufferPointer(err) : "unknown error");
        if (err) ID3D10Blob_Release(err);
            return AVERROR_EXTERNAL;
    }
    if (err)
        ID3D10Blob_Release(err);

    desc.pRootSignature     = s->root_sig;
    desc.CS.pShaderBytecode = ID3D10Blob_GetBufferPointer(code);
    desc.CS.BytecodeLength  = ID3D10Blob_GetBufferSize(code);

    hr = ID3D12Device_CreateComputePipelineState(s->device, &desc, &IID_ID3D12PipelineState, (void **)out);
    ID3D10Blob_Release(code);
    if (FAILED(hr)) {
        av_log(s->log_ctx, AV_LOG_ERROR, "dnn_onnx_d3d12: CreateComputePipelineState(%s) failed: 0x%lX\n",
               entry, (long)hr);
        return AVERROR_EXTERNAL;
    }
    return 0;
}

int ff_dnn_onnx_d3d12_probe(void *d3d12_device_void, DnnOnnxD3D12TensorMem mem, int *is_uma, void *log_ctx)
{
    ID3D12Device *device = (ID3D12Device *)d3d12_device_void;
    HMODULE dml_dll, compiler_dll;

    if (mem != DNN_ONNX_D3D12_TENSOR_GPU) {
        av_log(log_ctx, AV_LOG_ERROR, "dnn_onnx_d3d12: unsupported tensor memory mode\n");
        return AVERROR(ENOSYS);
    }

    if (is_uma)
        *is_uma = 0;

    dml_dll = onnx_d3d12_dlopen("DirectML.dll");
    if (!dml_dll) {
        av_log(log_ctx, AV_LOG_VERBOSE, "dnn_onnx_d3d12: DirectML.dll not found, D3D12 hardware-frame path unavailable\n");
        return AVERROR(ENOSYS);
    }
    compiler_dll = onnx_d3d12_dlopen("d3dcompiler_47.dll");
    if (!compiler_dll) {
        FreeLibrary(dml_dll);
        av_log(log_ctx, AV_LOG_VERBOSE, "dnn_onnx_d3d12: d3dcompiler_47.dll not found, D3D12 hardware-frame path unavailable\n");
        return AVERROR(ENOSYS);
    }
    FreeLibrary(compiler_dll);
    FreeLibrary(dml_dll);

    if (is_uma && device) {
        D3D12_FEATURE_DATA_ARCHITECTURE1 arch = { 0 };
        if (SUCCEEDED(ID3D12Device_CheckFeatureSupport(device, D3D12_FEATURE_ARCHITECTURE1, &arch, sizeof(arch))))
            *is_uma = arch.UMA || arch.CacheCoherentUMA;
    }
    return 0;
}

static int get_valid_command_allocator(DnnOnnxD3D12Ctx *s, ID3D12CommandAllocator **out)
{
    DnnOnnxD3D12CmdAlloc entry;
    HRESULT hr;

    if (av_fifo_peek(s->alloc_queue, &entry, 1, 0) >= 0) {
        UINT64 completed = ID3D12Fence_GetCompletedValue(s->fence);
        if (completed >= entry.fence_value) {
            *out = entry.alloc;
            av_fifo_read(s->alloc_queue, &entry, 1);
            return 0;
        }
    }

    hr = ID3D12Device_CreateCommandAllocator(s->device, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                              &IID_ID3D12CommandAllocator, (void **)out);
    if (FAILED(hr)) {
        av_log(s->log_ctx, AV_LOG_ERROR, "dnn_onnx_d3d12: CreateCommandAllocator failed: 0x%lX\n", (long)hr);
        return AVERROR_EXTERNAL;
    }
    return 0;
}

static int discard_command_allocator(DnnOnnxD3D12Ctx *s, ID3D12CommandAllocator *alloc, UINT64 fence_value)
{
    DnnOnnxD3D12CmdAlloc entry = { .alloc = alloc, .fence_value = fence_value };

    if (av_fifo_write(s->alloc_queue, &entry, 1) < 0) {
        if (fence_value > 0)
            wait_queue_idle(s);
        ID3D12CommandAllocator_Release(alloc);
        return AVERROR(ENOMEM);
    }
    return 0;
}

DnnOnnxD3D12Ctx *ff_dnn_onnx_d3d12_create(void *d3d12_device_void, DnnOnnxD3D12TensorMem mem, void *log_ctx)
{
    ID3D12Device *device = (ID3D12Device *)d3d12_device_void;
    DnnOnnxD3D12Ctx   *s;
    D3D12_COMMAND_QUEUE_DESC qdesc = { 0 };
    ID3D12CommandAllocator  *alloc = NULL;
    PFN_DMLCreateDevice create_dml_device;
    HRESULT hr;

    if (mem != DNN_ONNX_D3D12_TENSOR_GPU) {
        av_log(log_ctx, AV_LOG_ERROR, "dnn_onnx_d3d12: unsupported tensor memory mode\n");
        return NULL;
    }

    s = (DnnOnnxD3D12Ctx *)av_mallocz(sizeof(*s));
    if (!s)
        return NULL;
    s->log_ctx = log_ctx;
    s->mem = mem;
    s->device = device;

    s->ort = OrtGetApiBase()->GetApi(ORT_API_VERSION);
    if (!s->ort) {
        av_log(log_ctx, AV_LOG_ERROR, "dnn_onnx_d3d12: failed to get OrtApi\n");
        goto fail;
    }

    s->directml_dll = onnx_d3d12_dlopen("DirectML.dll");
    if (!s->directml_dll) {
        av_log(log_ctx, AV_LOG_ERROR, "dnn_onnx_d3d12: failed to load DirectML.dll\n");
        goto fail;
    }
    create_dml_device = (PFN_DMLCreateDevice)(void *)GetProcAddress(s->directml_dll, "DMLCreateDevice");
    if (!create_dml_device) {
        av_log(log_ctx, AV_LOG_ERROR, "dnn_onnx_d3d12: DMLCreateDevice not found in DirectML.dll\n");
        goto fail;
    }
    hr = create_dml_device(device, 0 /* DML_CREATE_DEVICE_FLAG_NONE */,
                            &IID_IDMLDevice, (void **)&s->dml_device);
    if (FAILED(hr)) {
        av_log(log_ctx, AV_LOG_ERROR, "dnn_onnx_d3d12: DMLCreateDevice failed: 0x%lX\n", (long)hr);
        goto fail;
    }

    s->d3dcompiler_dll = onnx_d3d12_dlopen("d3dcompiler_47.dll");
    if (!s->d3dcompiler_dll) {
        av_log(log_ctx, AV_LOG_ERROR, "dnn_onnx_d3d12: failed to load d3dcompiler_47.dll\n");
        goto fail;
    }
    s->compile_fn = (pD3DCompile)(void *)GetProcAddress(s->d3dcompiler_dll, "D3DCompile");
    if (!s->compile_fn) {
        av_log(log_ctx, AV_LOG_ERROR, "dnn_onnx_d3d12: D3DCompile not found in d3dcompiler_47.dll\n");
        goto fail;
    }

    s->d3d12_dll = onnx_d3d12_dlopen("d3d12.dll");
    if (!s->d3d12_dll) {
        av_log(log_ctx, AV_LOG_ERROR, "dnn_onnx_d3d12: failed to load d3d12.dll\n");
        goto fail;
    }
    s->serialize_root_sig_fn = (PFN_D3D12_SERIALIZE_ROOT_SIGNATURE)(void *)
        GetProcAddress(s->d3d12_dll, "D3D12SerializeRootSignature");
    if (!s->serialize_root_sig_fn) {
        av_log(log_ctx, AV_LOG_ERROR, "dnn_onnx_d3d12: D3D12SerializeRootSignature not found in d3d12.dll\n");
        goto fail;
    }

    qdesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    qdesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    hr = ID3D12Device_CreateCommandQueue(device, &qdesc, &IID_ID3D12CommandQueue, (void **)&s->queue);
    if (FAILED(hr)) {
        av_log(log_ctx, AV_LOG_ERROR, "dnn_onnx_d3d12: CreateCommandQueue failed: 0x%lX\n", (long)hr);
        goto fail;
    }

    hr = ID3D12Device_CreateFence(device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&s->fence);
    if (FAILED(hr))
        goto fail;
    s->fence_event = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (!s->fence_event)
        goto fail;

    s->alloc_queue = av_fifo_alloc2(2, sizeof(DnnOnnxD3D12CmdAlloc), AV_FIFO_FLAG_AUTO_GROW);
    if (!s->alloc_queue)
        goto fail;

    if (get_valid_command_allocator(s, &alloc) < 0)
        goto fail;
    hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc, NULL,
                                            &IID_ID3D12GraphicsCommandList, (void **)&s->cmd_list);
    if (FAILED(hr)) {
        ID3D12CommandAllocator_Release(alloc);
        goto fail;
    }
    ID3D12GraphicsCommandList_Close(s->cmd_list);
    if (discard_command_allocator(s, alloc, 0) < 0)
        goto fail;

    if (create_root_signature(s) < 0)
        goto fail;
    if (compile_and_create_pso(s, kShaderCompactY, "CSCompactY", &s->pso_compact_y) < 0)
        goto fail;
    if (compile_and_create_pso(s, kShaderExpandY, "CSExpandY", &s->pso_expand_y) < 0)
        goto fail;
    if (compile_and_create_pso(s, kShaderUpscaleUV, "CSUpscaleUV", &s->pso_upscale_uv) < 0)
        goto fail;

    return s;

fail:
    ff_dnn_onnx_d3d12_free(&s);
    return NULL;
}

void ff_dnn_onnx_d3d12_free(DnnOnnxD3D12Ctx **ctx)
{
    DnnOnnxD3D12Ctx *s = *ctx;

    if (!s)
        return;

    if (s->queue && s->fence && s->fence_event)
        wait_queue_idle(s);

    if (s->ort)
        release_buffers(s);

    if (s->pso_compact_y) {
        ID3D12PipelineState_Release(s->pso_compact_y);
        s->pso_compact_y = NULL;
    }
    if (s->pso_expand_y) {
        ID3D12PipelineState_Release(s->pso_expand_y);
        s->pso_expand_y = NULL;
    }
    if (s->pso_upscale_uv) {
        ID3D12PipelineState_Release(s->pso_upscale_uv);
        s->pso_upscale_uv = NULL;
    }
    if (s->root_sig) {
        ID3D12RootSignature_Release(s->root_sig);
        s->root_sig = NULL;
    }
    if (s->cmd_list) {
        ID3D12GraphicsCommandList_Release(s->cmd_list);
        s->cmd_list = NULL;
    }
    if (s->alloc_queue) {
        DnnOnnxD3D12CmdAlloc entry;
        while (av_fifo_read(s->alloc_queue, &entry, 1) >= 0)
            ID3D12CommandAllocator_Release(entry.alloc);
        av_fifo_freep2(&s->alloc_queue);
    }
    if (s->fence_event) {
        CloseHandle(s->fence_event);
        s->fence_event = NULL;
    }
    if (s->fence) {
        ID3D12Fence_Release(s->fence);
        s->fence = NULL;
    }
    if (s->queue) {
        ID3D12CommandQueue_Release(s->queue);
        s->queue = NULL;
    }
    if (s->dml_device) {
        IUnknown_Release((IUnknown *)s->dml_device);
        s->dml_device = NULL;
    }

    if (s->d3dcompiler_dll) {
        FreeLibrary(s->d3dcompiler_dll);
        s->d3dcompiler_dll = NULL;
    }
    if (s->d3d12_dll) {
        FreeLibrary(s->d3d12_dll);
        s->d3d12_dll = NULL;
    }
    if (s->directml_dll) {
        FreeLibrary(s->directml_dll);
        s->directml_dll = NULL;
    }

    av_free(s);
    *ctx = NULL;
}

int ff_dnn_onnx_d3d12_append_ep(DnnOnnxD3D12Ctx *ctx, void *session_options_void)
{
    OrtSessionOptions *session_options = (OrtSessionOptions *)session_options_void;
    OrtStatus *status;

    if (!ctx->dml_api) {
        status = ctx->ort->GetExecutionProviderApi("DML", ORT_API_VERSION, (const void **)&ctx->dml_api);
        if (status != NULL) {
            av_log(ctx->log_ctx, AV_LOG_ERROR, "dnn_onnx_d3d12: failed to get OrtDmlApi: %s\n",
                   ctx->ort->GetErrorMessage(status));
            ctx->ort->ReleaseStatus(status);
            ctx->dml_api = NULL;
            return AVERROR(ENOSYS);
        }
    }

    /* Same DirectML constraints as the string-based EP path in dnn_backend_onnx.c:
     * sequential execution only, and the memory-pattern optimizer must be off. */
    status = ctx->ort->SetSessionExecutionMode(session_options, ORT_SEQUENTIAL);
    if (status)
        ctx->ort->ReleaseStatus(status);
    status = ctx->ort->DisableMemPattern(session_options);
    if (status)
        ctx->ort->ReleaseStatus(status);

    status = ctx->dml_api->SessionOptionsAppendExecutionProvider_DML1(session_options, ctx->dml_device, ctx->queue);
    if (status != NULL) {
        av_log(ctx->log_ctx, AV_LOG_ERROR,
               "dnn_onnx_d3d12: failed to append DirectML EP on shared device/queue: %s\n",
               ctx->ort->GetErrorMessage(status));
        ctx->ort->ReleaseStatus(status);
        return AVERROR_EXTERNAL;
    }
    return 0;
}

static int check_mult(uint64_t a, uint64_t b, uint64_t *out)
{
    if (a != 0 && b > UINT64_MAX / a)
        return AVERROR(ERANGE);
    *out = a * b;
    return 0;
}

int ff_dnn_onnx_d3d12_config(DnnOnnxD3D12Ctx *ctx, int in_w, int in_h, int out_w, int out_h,
                              DNNDataType dt, DNNLayout layout, int channels)
{
    D3D12_RESOURCE_DESC nv12_desc = { 0 };
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprints[2];
    UINT num_rows[2];
    UINT64 row_sizes[2], total_bytes;
    uint64_t in_pixels, out_pixels, tmp;
    OrtStatus *status;
    int64_t in_shape[4], out_shape[4];
    int ret;

    if (channels != 1) {
        av_log(ctx->log_ctx, AV_LOG_ERROR,
               "dnn_onnx_d3d12: only single-channel models are supported in this phase "
               "(got channels=%d); multi-channel support is second-phase work\n", channels);
        return AVERROR(ENOSYS);
    }
    if (dt != DNN_FLOAT) {
        av_log(ctx->log_ctx, AV_LOG_ERROR,
               "dnn_onnx_d3d12: only FLOAT tensors are supported in this phase; "
               "uint8 tensor support is second-phase work\n");
        return AVERROR(ENOSYS);
    }
    if (layout != DL_NCHW) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "dnn_onnx_d3d12: only NCHW layout is supported\n");
        return AVERROR(ENOSYS);
    }
    if (in_w <= 0 || in_h <= 0 || out_w <= 0 || out_h <= 0) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "dnn_onnx_d3d12: invalid tensor dimensions %dx%d -> %dx%d\n",
               in_w, in_h, out_w, out_h);
        return AVERROR(EINVAL);
    }
    /* NV12 chroma is subsampled 2x2, so all dimensions must be even. */
    if ((in_w | in_h | out_w | out_h) & 1) {
        av_log(ctx->log_ctx, AV_LOG_ERROR,
               "dnn_onnx_d3d12: NV12 frames require even dimensions, got %dx%d -> %dx%d\n",
               in_w, in_h, out_w, out_h);
        return AVERROR(EINVAL);
    }

    if (ctx->configured && ctx->in_w == in_w && ctx->in_h == in_h && ctx->out_w == out_w &&
        ctx->out_h == out_h && ctx->channels == channels && ctx->dt == dt && ctx->layout == layout)
        return 0;

    ret = wait_queue_idle(ctx);
    if (ret < 0)
        return ret;
    release_buffers(ctx);

    if (check_mult((uint64_t)in_w, (uint64_t)in_h, &in_pixels) < 0 ||
        check_mult(in_pixels, sizeof(float), &tmp) < 0 ||
        check_mult((uint64_t)out_w, (uint64_t)out_h, &out_pixels) < 0 ||
        check_mult(out_pixels, sizeof(float), &tmp) < 0) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "dnn_onnx_d3d12: tensor size overflow\n");
        return AVERROR(ERANGE);
    }

     /* CopyTextureRegion() needs to know the exact row pitch it expects for
     * a texture of these dimensions, so we ask the driver once here via
     * GetCopyableFootprints(), instead of re-deriving the alignment rule
     * (and re-querying it) on every frame. */
    nv12_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    nv12_desc.Width     = in_w;
    nv12_desc.Height    = in_h;
    nv12_desc.MipLevels = 1;
    nv12_desc.Format    = DXGI_FORMAT_NV12;
    nv12_desc.SampleDesc.Count = 1;
    nv12_desc.DepthOrArraySize = 1;
    nv12_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    ID3D12Device_GetCopyableFootprints(ctx->device, &nv12_desc, 0, 2, 0, footprints, num_rows, row_sizes, &total_bytes);
    if (total_bytes == UINT64_MAX) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "dnn_onnx_d3d12: GetCopyableFootprints failed for %dx%d NV12\n",
               in_w, in_h);
        return AVERROR_EXTERNAL;
    }
    ctx->y_src_pitch  = footprints[0].Footprint.RowPitch;
    ctx->y_src_rows   = (UINT)in_h;
    ctx->uv_src_pitch = footprints[1].Footprint.RowPitch;
    ctx->uv_src_rows  = (UINT)((in_h + 1) / 2);

    nv12_desc.Width  = out_w;
    nv12_desc.Height = out_h;
    ID3D12Device_GetCopyableFootprints(ctx->device, &nv12_desc, 0, 2, 0, footprints, num_rows, row_sizes, &total_bytes);
    if (total_bytes == UINT64_MAX) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "dnn_onnx_d3d12: GetCopyableFootprints failed for %dx%d NV12\n",
               out_w, out_h);
        return AVERROR_EXTERNAL;
    }
    ctx->y_dst_pitch  = footprints[0].Footprint.RowPitch;
    ctx->y_dst_rows   = (UINT)out_h;
    ctx->uv_dst_pitch = footprints[1].Footprint.RowPitch;
    ctx->uv_dst_rows  = (UINT)((out_h + 1) / 2);

    if (create_uav_buffer(ctx->device, (UINT64)in_w * in_h * sizeof(float), &ctx->in_buf) < 0 ||
        create_uav_buffer(ctx->device, (UINT64)out_w * out_h * sizeof(float), &ctx->out_buf) < 0 ||
        create_uav_buffer(ctx->device, (UINT64)ctx->y_src_pitch * ctx->y_src_rows, &ctx->pad_y_src) < 0 ||
        create_uav_buffer(ctx->device, (UINT64)ctx->y_dst_pitch * ctx->y_dst_rows, &ctx->pad_y_dst) < 0 ||
        create_uav_buffer(ctx->device, (UINT64)ctx->uv_src_pitch * ctx->uv_src_rows, &ctx->pad_uv_src) < 0 ||
        create_uav_buffer(ctx->device, (UINT64)ctx->uv_dst_pitch * ctx->uv_dst_rows, &ctx->pad_uv_dst) < 0) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "dnn_onnx_d3d12: failed to allocate GPU buffers\n");
        release_buffers(ctx);
        return AVERROR(ENOMEM);
    }

    if (!ctx->dml_api) {
        status = ctx->ort->GetExecutionProviderApi("DML", ORT_API_VERSION, (const void **)&ctx->dml_api);
        if (status) {
            ctx->ort->ReleaseStatus(status);
            ctx->dml_api = NULL;
        }
    }
    if (!ctx->dml_api) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "dnn_onnx_d3d12: OrtDmlApi unavailable\n");
        release_buffers(ctx);
        return AVERROR(ENOSYS);
    }

    status = ctx->ort->CreateMemoryInfo("DML", OrtDeviceAllocator, 0, OrtMemTypeDefault, &ctx->dml_mem_info);
    if (status) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "dnn_onnx_d3d12: CreateMemoryInfo(DML) failed: %s\n",
               ctx->ort->GetErrorMessage(status));
        ctx->ort->ReleaseStatus(status);
        release_buffers(ctx);
        return AVERROR_EXTERNAL;
    }

    status = ctx->dml_api->CreateGPUAllocationFromD3DResource(ctx->in_buf, &ctx->dml_in_alloc);
    if (status) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "dnn_onnx_d3d12: CreateGPUAllocationFromD3DResource(in) failed: %s\n",
               ctx->ort->GetErrorMessage(status));
        ctx->ort->ReleaseStatus(status);
        release_buffers(ctx);
        return AVERROR_EXTERNAL;
    }
    status = ctx->dml_api->CreateGPUAllocationFromD3DResource(ctx->out_buf, &ctx->dml_out_alloc);
    if (status) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "dnn_onnx_d3d12: CreateGPUAllocationFromD3DResource(out) failed: %s\n",
               ctx->ort->GetErrorMessage(status));
        ctx->ort->ReleaseStatus(status);
        release_buffers(ctx);
        return AVERROR_EXTERNAL;
    }

    /* NCHW layout */
    in_shape[0]  = 1; in_shape[1]  = 1; in_shape[2]  = in_h;  in_shape[3]  = in_w;
    out_shape[0] = 1; out_shape[1] = 1; out_shape[2] = out_h; out_shape[3] = out_w;

    status = ctx->ort->CreateTensorWithDataAsOrtValue(ctx->dml_mem_info, ctx->dml_in_alloc,
                                                     (size_t)in_w * in_h * sizeof(float),
                                                     in_shape, 4, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
                                                     &ctx->in_tensor);
    if (status) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "dnn_onnx_d3d12: CreateTensorWithDataAsOrtValue(in) failed: %s\n",
               ctx->ort->GetErrorMessage(status));
        ctx->ort->ReleaseStatus(status);
        release_buffers(ctx);
        return AVERROR_EXTERNAL;
    }
    status = ctx->ort->CreateTensorWithDataAsOrtValue(ctx->dml_mem_info, ctx->dml_out_alloc,
                                                     (size_t)out_w * out_h * sizeof(float),
                                                     out_shape, 4, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
                                                     &ctx->out_tensor);
    if (status) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "dnn_onnx_d3d12: CreateTensorWithDataAsOrtValue(out) failed: %s\n",
               ctx->ort->GetErrorMessage(status));
        ctx->ort->ReleaseStatus(status);
        release_buffers(ctx);
        return AVERROR_EXTERNAL;
    }

    ctx->in_w = in_w; ctx->in_h = in_h; ctx->out_w = out_w; ctx->out_h = out_h;
    ctx->channels = channels; ctx->dt = dt; ctx->layout = layout;
    ctx->uv_passthrough = (in_w == out_w && in_h == out_h);
    ctx->configured = 1;
    return 0;
}

int ff_dnn_onnx_d3d12_fill_input(DnnOnnxD3D12Ctx *ctx, const AVFrame *in, void **in_tensor, void **out_tensor)
{
    AVD3D12VAFrame *iframe;
    D3D12_TEXTURE_COPY_LOCATION dst_loc = { 0 }, src_loc = { 0 };
    D3D12_BOX src_box = { 0 };
    ID3D12CommandList *lists[1];
    ID3D12CommandAllocator *alloc = NULL;
    UINT consts[8] = { 0 };
    HRESULT hr;
    int ret;

    if (!ctx->configured) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "dnn_onnx_d3d12: fill_input() called before config()\n");
        return AVERROR(EINVAL);
    }
    if (!in->hw_frames_ctx || !in->data[0]) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "dnn_onnx_d3d12: input frame has no D3D12 hw data\n");
        return AVERROR(EINVAL);
    }
    iframe = (AVD3D12VAFrame *)in->data[0];
    if ((iframe->flags & AV_D3D12VA_FRAME_FLAG_TEXTURE_ARRAY) ||
        iframe->subresource_index != 0) {
        av_log(ctx->log_ctx, AV_LOG_ERROR,
               "dnn_onnx_d3d12: texture-array frames are not supported yet (subresource_index=%d)\n",
               iframe->subresource_index);
        return AVERROR(ENOSYS);
    }

    /* An AVD3D12VAFrame's texture is what the D3D12VA decoder wrote
     * directly: an opaque, tiled/swizzled NV12 surface, not something a
     * compute shader can index into. Turning it into DML's compact FP32
     * tensor takes two real per-frame byte copies in fill_input():
     * 1. CopyTextureRegion() flattens the opaque texture into a linear,
     *    correctly-pitched staging buffer.
     * 2. CSCompactY normalizes those linear bytes into the tight in_buf tensor.
    */
    ret = get_valid_command_allocator(ctx, &alloc);
    if (ret < 0)
        return ret;
    hr = ID3D12CommandAllocator_Reset(alloc);
    if (FAILED(hr)) {
        discard_command_allocator(ctx, alloc, 0);
        return AVERROR_EXTERNAL;
    }
    hr = ID3D12GraphicsCommandList_Reset(ctx->cmd_list, alloc, NULL);
    if (FAILED(hr)) {
        discard_command_allocator(ctx, alloc, 0);
        return AVERROR_EXTERNAL;
    }

    /* Flatten the decoder's opaque tiled/swizzled NV12 texture into linear,
     * correctly-pitched staging buffers via CopyTextureRegion() */
    transition(ctx->cmd_list, iframe->texture, 0, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
    transition(ctx->cmd_list, iframe->texture, 1, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
    transition(ctx->cmd_list, ctx->pad_y_src,  0, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
    transition(ctx->cmd_list, ctx->pad_uv_src, 0, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);

    dst_loc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst_loc.PlacedFootprint.Footprint.Depth = 1;
    src_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src_loc.pResource = iframe->texture;

    /* handle the mismatch between coded width/height and display width/height. */
    src_box.left = src_box.top = src_box.front = 0;
    src_box.back = 1;

    dst_loc.pResource = ctx->pad_y_src;
    dst_loc.PlacedFootprint.Footprint.Format   = DXGI_FORMAT_R8_UNORM;
    dst_loc.PlacedFootprint.Footprint.Width    = (UINT)ctx->in_w;
    dst_loc.PlacedFootprint.Footprint.Height   = (UINT)ctx->in_h;
    dst_loc.PlacedFootprint.Footprint.RowPitch = ctx->y_src_pitch;
    src_loc.SubresourceIndex = 0;
    src_box.right  = (UINT)ctx->in_w;
    src_box.bottom = (UINT)ctx->in_h;
    ID3D12GraphicsCommandList_CopyTextureRegion(ctx->cmd_list, &dst_loc, 0, 0, 0, &src_loc, &src_box);

    dst_loc.pResource = ctx->pad_uv_src;
    dst_loc.PlacedFootprint.Footprint.Format   = DXGI_FORMAT_R8G8_UNORM;
    dst_loc.PlacedFootprint.Footprint.Width    = (UINT)((ctx->in_w + 1) / 2);
    dst_loc.PlacedFootprint.Footprint.Height   = (UINT)((ctx->in_h + 1) / 2);
    dst_loc.PlacedFootprint.Footprint.RowPitch = ctx->uv_src_pitch;
    src_loc.SubresourceIndex = 1;
    src_box.right  = (UINT)((ctx->in_w + 1) / 2);
    src_box.bottom = (UINT)((ctx->in_h + 1) / 2);
    ID3D12GraphicsCommandList_CopyTextureRegion(ctx->cmd_list, &dst_loc, 0, 0, 0, &src_loc, &src_box);

    transition(ctx->cmd_list, iframe->texture, 0, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
    transition(ctx->cmd_list, iframe->texture, 1, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
    transition(ctx->cmd_list, ctx->pad_y_src,  0, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    transition(ctx->cmd_list, ctx->pad_uv_src, 0, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    /* Normalize the linear Y bytes just staged above into the tight in_buf
     * tensor DML expects, by dispatching CSCompactY. (The UV planes take a
     * different path, only the Y plane goes through a compute shader here.) */
    consts[0] = (UINT)ctx->in_w;
    consts[1] = (UINT)ctx->in_h;
    consts[2] = ctx->y_src_pitch;
    ID3D12GraphicsCommandList_SetComputeRootSignature(ctx->cmd_list, ctx->root_sig);
    ID3D12GraphicsCommandList_SetPipelineState(ctx->cmd_list, ctx->pso_compact_y);
    ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(ctx->cmd_list, 0, 8, consts, 0);
    ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(
        ctx->cmd_list, 1, ID3D12Resource_GetGPUVirtualAddress(ctx->pad_y_src));
    ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(
        ctx->cmd_list, 2, ID3D12Resource_GetGPUVirtualAddress(ctx->in_buf));
    ID3D12GraphicsCommandList_Dispatch(ctx->cmd_list, ((UINT)ctx->in_w + 7) / 8, ((UINT)ctx->in_h + 7) / 8, 1);
    uav_barrier(ctx->cmd_list, ctx->in_buf);

    hr = ID3D12GraphicsCommandList_Close(ctx->cmd_list);
    if (FAILED(hr)) {
        discard_command_allocator(ctx, alloc, 0);
        return AVERROR_EXTERNAL;
    }

    if (iframe->sync_ctx.fence && iframe->sync_ctx.fence_value > 0 &&
        ID3D12Fence_GetCompletedValue(iframe->sync_ctx.fence) < iframe->sync_ctx.fence_value) {
        hr = ID3D12CommandQueue_Wait(ctx->queue, iframe->sync_ctx.fence, iframe->sync_ctx.fence_value);
        if (FAILED(hr)) {
            av_log(ctx->log_ctx, AV_LOG_ERROR, "dnn_onnx_d3d12: queue Wait on input fence failed: 0x%lX\n", (long)hr);
            discard_command_allocator(ctx, alloc, 0);
            return AVERROR_EXTERNAL;
        }
    }

    lists[0] = (ID3D12CommandList *)ctx->cmd_list;
    ID3D12CommandQueue_ExecuteCommandLists(ctx->queue, 1, lists);
    hr = ID3D12CommandQueue_Signal(ctx->queue, ctx->fence, ctx->fence_value + 1);
    if (FAILED(hr)) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "dnn_onnx_d3d12: Signal failed: 0x%lX\n", (long)hr);
        discard_command_allocator(ctx, alloc, ctx->fence_value + 1);
        return AVERROR_EXTERNAL;
    }
    ctx->fence_value++;

    if (discard_command_allocator(ctx, alloc, ctx->fence_value) < 0)
        return AVERROR(ENOMEM);

    if (in_tensor)
        *in_tensor = ctx->in_tensor;
    if (out_tensor)
        *out_tensor = ctx->out_tensor;

    return 0;
}

int ff_dnn_onnx_d3d12_run(DnnOnnxD3D12Ctx *ctx, void *session_void, const char *in_name, const char *out_name,
                          void *in_tensor_void, void *out_tensor_void)
{
    OrtSession   *session = (OrtSession *)session_void;
    OrtValue   *in_tensor = (OrtValue *)in_tensor_void;
    OrtValue  *out_tensor = (OrtValue *)out_tensor_void;
    OrtIoBinding *binding = NULL;
    OrtStatus *status;
    int ret = 0;

    status = ctx->ort->CreateIoBinding(session, &binding);
    if (status) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "dnn_onnx_d3d12: CreateIoBinding failed: %s\n",
               ctx->ort->GetErrorMessage(status));
        ctx->ort->ReleaseStatus(status);
        return DNN_GENERIC_ERROR;
    }

    status = ctx->ort->BindInput(binding, in_name, in_tensor);
    if (!status)
        status = ctx->ort->BindOutput(binding, out_name, out_tensor);
    if (!status)
        status = ctx->ort->RunWithBinding(session, NULL, binding);

    if (status) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "dnn_onnx_d3d12: DirectML inference failed: %s\n",
               ctx->ort->GetErrorMessage(status));
        ctx->ort->ReleaseStatus(status);
        ret = DNN_GENERIC_ERROR;
    }

    ctx->ort->ReleaseIoBinding(binding);
    return ret;
}

int ff_dnn_onnx_d3d12_writeback(DnnOnnxD3D12Ctx *ctx, AVFrame *out)
{
    AVD3D12VAFrame *oframe;
    D3D12_TEXTURE_COPY_LOCATION dst_loc = { 0 }, src_loc = { 0 };
    ID3D12Resource *uv_src_buf;
    ID3D12CommandList *lists[1];
    ID3D12CommandAllocator *alloc = NULL;
    UINT consts[8] = { 0 };
    UINT  uv_dst_w = (UINT)((ctx->out_w + 1) / 2);
    UINT  uv_dst_h = (UINT)((ctx->out_h + 1) / 2);
    HRESULT hr;
    int ret;

    if (!ctx->configured)
        return AVERROR(EINVAL);
    if (!out->data[0]) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "dnn_onnx_d3d12: output frame has no D3D12 hw data\n");
        return AVERROR(EINVAL);
    }

    oframe = (AVD3D12VAFrame *)out->data[0];
    if ((oframe->flags & AV_D3D12VA_FRAME_FLAG_TEXTURE_ARRAY) ||
        oframe->subresource_index != 0) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "dnn_onnx_d3d12: texture-array frames are not supported yet\n");
        return AVERROR(ENOSYS);
    }

    ret = get_valid_command_allocator(ctx, &alloc);
    if (ret < 0)
        return ret;
    hr = ID3D12CommandAllocator_Reset(alloc);
    if (FAILED(hr)) {
        discard_command_allocator(ctx, alloc, 0);
        return AVERROR_EXTERNAL;
    }
    hr = ID3D12GraphicsCommandList_Reset(ctx->cmd_list, alloc, NULL);
    if (FAILED(hr)) {
        discard_command_allocator(ctx, alloc, 0);
        return AVERROR_EXTERNAL;
    }

    uav_barrier(ctx->cmd_list, ctx->out_buf);

    /* Denormalize the compact FP32 out_buf tensor DML produced back into
     * linear luma bytes, into the padded pad_y_dst staging buffer, by
     * dispatching CSExpandY (the reverse of fill_input()'s CSCompactY). */
    consts[0] = (UINT)ctx->out_w;
    consts[1] = (UINT)ctx->out_h;
    consts[3] = ctx->y_dst_pitch;
    ID3D12GraphicsCommandList_SetComputeRootSignature(ctx->cmd_list, ctx->root_sig);
    ID3D12GraphicsCommandList_SetPipelineState(ctx->cmd_list, ctx->pso_expand_y);
    ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(ctx->cmd_list, 0, 8, consts, 0);
    ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(
        ctx->cmd_list, 1, ID3D12Resource_GetGPUVirtualAddress(ctx->out_buf));
    ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(
        ctx->cmd_list, 2, ID3D12Resource_GetGPUVirtualAddress(ctx->pad_y_dst));
    ID3D12GraphicsCommandList_Dispatch(ctx->cmd_list, ((UINT)ctx->out_w + 7) / 8, ((UINT)ctx->out_h + 7) / 8, 1);
    uav_barrier(ctx->cmd_list, ctx->pad_y_dst);

    /* Chroma: never goes through a DML tensor. Either reuse what
     * fill_input() already staged in pad_uv_src (1:1, no resize needed),
     * or resize it into pad_uv_dst via the CSUpscaleUV compute shader. */
    if (ctx->uv_passthrough) {
        /* chroma bypasses the model entirely: reuse what fill_input() already staged */
        uv_src_buf = ctx->pad_uv_src;
    } else {
        UINT uv_src_w = (UINT)((ctx->in_w + 1) / 2);
        UINT uv_src_h = (UINT)((ctx->in_h + 1) / 2);
        UINT uv_consts[8] = { uv_dst_w, uv_dst_h, uv_src_w, uv_src_h, ctx->uv_src_pitch, ctx->uv_dst_pitch };
        ID3D12GraphicsCommandList_SetPipelineState(ctx->cmd_list, ctx->pso_upscale_uv);
        ID3D12GraphicsCommandList_SetComputeRoot32BitConstants(ctx->cmd_list, 0, 8, uv_consts, 0);
        ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(
            ctx->cmd_list, 1, ID3D12Resource_GetGPUVirtualAddress(ctx->pad_uv_src));
        ID3D12GraphicsCommandList_SetComputeRootUnorderedAccessView(
            ctx->cmd_list, 2, ID3D12Resource_GetGPUVirtualAddress(ctx->pad_uv_dst));
        ID3D12GraphicsCommandList_Dispatch(ctx->cmd_list, (uv_dst_w + 7) / 8, (uv_dst_h + 7) / 8, 1);
        uav_barrier(ctx->cmd_list, ctx->pad_uv_dst);
        uv_src_buf = ctx->pad_uv_dst;
    }

    /* Flatten the padded linear Y/UV buffers just produced above back into
     * the decoder-format opaque tiled/swizzled NV12 output texture, via
     * CopyTextureRegion() -- the reverse of fill_input()'s flatten step. */
    transition(ctx->cmd_list, ctx->pad_y_dst,  0, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    transition(ctx->cmd_list, uv_src_buf,      0, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    transition(ctx->cmd_list, oframe->texture, 0, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
    transition(ctx->cmd_list, oframe->texture, 1, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);

    src_loc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src_loc.PlacedFootprint.Footprint.Depth = 1;
    dst_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst_loc.pResource = oframe->texture;

    src_loc.pResource = ctx->pad_y_dst;
    src_loc.PlacedFootprint.Footprint.Format   = DXGI_FORMAT_R8_UNORM;
    src_loc.PlacedFootprint.Footprint.Width    = (UINT)ctx->out_w;
    src_loc.PlacedFootprint.Footprint.Height   = (UINT)ctx->out_h;
    src_loc.PlacedFootprint.Footprint.RowPitch = ctx->y_dst_pitch;
    dst_loc.SubresourceIndex = 0;
    ID3D12GraphicsCommandList_CopyTextureRegion(ctx->cmd_list, &dst_loc, 0, 0, 0, &src_loc, NULL);

    src_loc.pResource = uv_src_buf;
    src_loc.PlacedFootprint.Footprint.Format   = DXGI_FORMAT_R8G8_UNORM;
    src_loc.PlacedFootprint.Footprint.Width    = uv_dst_w;
    src_loc.PlacedFootprint.Footprint.Height   = uv_dst_h;
    src_loc.PlacedFootprint.Footprint.RowPitch = ctx->uv_dst_pitch;
    dst_loc.SubresourceIndex = 1;
    ID3D12GraphicsCommandList_CopyTextureRegion(ctx->cmd_list, &dst_loc, 0, 0, 0, &src_loc, NULL);

    transition(ctx->cmd_list, ctx->pad_y_dst,  0, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    transition(ctx->cmd_list, uv_src_buf,      0, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    transition(ctx->cmd_list, oframe->texture, 0, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
    transition(ctx->cmd_list, oframe->texture, 1, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);

    hr = ID3D12GraphicsCommandList_Close(ctx->cmd_list);
    if (FAILED(hr)) {
        discard_command_allocator(ctx, alloc, 0);
        return AVERROR_EXTERNAL;
    }

    if (oframe->sync_ctx.fence && oframe->sync_ctx.fence_value > 0 &&
        ID3D12Fence_GetCompletedValue(oframe->sync_ctx.fence) < oframe->sync_ctx.fence_value) {
        hr = ID3D12CommandQueue_Wait(ctx->queue, oframe->sync_ctx.fence, oframe->sync_ctx.fence_value);
        if (FAILED(hr)) {
            av_log(ctx->log_ctx, AV_LOG_ERROR, "dnn_onnx_d3d12: queue Wait on output fence failed: 0x%lX\n", (long)hr);
            discard_command_allocator(ctx, alloc, 0);
            return AVERROR_EXTERNAL;
        }
    }

    lists[0] = (ID3D12CommandList *)ctx->cmd_list;
    ID3D12CommandQueue_ExecuteCommandLists(ctx->queue, 1, lists);
    hr = ID3D12CommandQueue_Signal(ctx->queue, ctx->fence, ctx->fence_value + 1);
    if (FAILED(hr)) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "dnn_onnx_d3d12: Signal failed: 0x%lX\n", (long)hr);
        discard_command_allocator(ctx, alloc, ctx->fence_value + 1);
        return AVERROR_EXTERNAL;
    }
    ctx->fence_value++;

    if (discard_command_allocator(ctx, alloc, ctx->fence_value) < 0)
        return AVERROR(ENOMEM);

    hr = ID3D12CommandQueue_Signal(ctx->queue, oframe->sync_ctx.fence, oframe->sync_ctx.fence_value + 1);
    if (FAILED(hr)) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "dnn_onnx_d3d12: Signal on output fence failed: 0x%lX\n", (long)hr);
        return AVERROR_EXTERNAL;
    }
    oframe->sync_ctx.fence_value++;

    return wait_for_fence(ctx);
}

int ff_dnn_onnx_d3d12_sync(DnnOnnxD3D12Ctx *ctx)
{
    if (!ctx)
        return 0;
    return wait_queue_idle(ctx);
}
