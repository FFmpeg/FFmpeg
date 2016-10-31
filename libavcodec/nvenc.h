/*
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

#ifndef AVCODEC_NVENC_H
#define AVCODEC_NVENC_H

#include "compat/nvenc/nvEncodeAPI.h"

#include "config.h"

#include "libavutil/fifo.h"
#include "libavutil/opt.h"

#include "avcodec.h"

#if CONFIG_CUDA
#include "libavutil/hwcontext_cuda.h"
#else

#if defined(_WIN32)
#define CUDAAPI __stdcall
#else
#define CUDAAPI
#endif

typedef enum cudaError_enum {
    CUDA_SUCCESS = 0
} CUresult;
typedef int CUdevice;
typedef void* CUcontext;
typedef void* CUdeviceptr;
#endif

#define MAX_REGISTERED_FRAMES 64

typedef struct NvencSurface
{
    NV_ENC_INPUT_PTR input_surface;
    AVFrame *in_ref;
    NV_ENC_MAP_INPUT_RESOURCE in_map;
    int reg_idx;
    int width;
    int height;
    int pitch;

    NV_ENC_OUTPUT_PTR output_surface;
    NV_ENC_BUFFER_FORMAT format;
    int size;
    int lockCount;
} NvencSurface;

typedef CUresult(CUDAAPI *PCUINIT)(unsigned int Flags);
typedef CUresult(CUDAAPI *PCUDEVICEGETCOUNT)(int *count);
typedef CUresult(CUDAAPI *PCUDEVICEGET)(CUdevice *device, int ordinal);
typedef CUresult(CUDAAPI *PCUDEVICEGETNAME)(char *name, int len, CUdevice dev);
typedef CUresult(CUDAAPI *PCUDEVICECOMPUTECAPABILITY)(int *major, int *minor, CUdevice dev);
typedef CUresult(CUDAAPI *PCUCTXCREATE)(CUcontext *pctx, unsigned int flags, CUdevice dev);
typedef CUresult(CUDAAPI *PCUCTXPOPCURRENT)(CUcontext *pctx);
typedef CUresult(CUDAAPI *PCUCTXDESTROY)(CUcontext ctx);

typedef NVENCSTATUS (NVENCAPI *PNVENCODEAPIGETMAXSUPPORTEDVERSION)(uint32_t* version);
typedef NVENCSTATUS (NVENCAPI *PNVENCODEAPICREATEINSTANCE)(NV_ENCODE_API_FUNCTION_LIST *functionList);

typedef struct NvencDynLoadFunctions
{
#if !CONFIG_CUDA
    void *cuda;
#endif
    void *nvenc;

    PCUINIT cu_init;
    PCUDEVICEGETCOUNT cu_device_get_count;
    PCUDEVICEGET cu_device_get;
    PCUDEVICEGETNAME cu_device_get_name;
    PCUDEVICECOMPUTECAPABILITY cu_device_compute_capability;
    PCUCTXCREATE cu_ctx_create;
    PCUCTXPOPCURRENT cu_ctx_pop_current;
    PCUCTXDESTROY cu_ctx_destroy;

    NV_ENCODE_API_FUNCTION_LIST nvenc_funcs;
    int nvenc_device_count;
} NvencDynLoadFunctions;

enum {
    PRESET_DEFAULT = 0,
    PRESET_SLOW,
    PRESET_MEDIUM,
    PRESET_FAST,
    PRESET_HP,
    PRESET_HQ,
    PRESET_BD ,
    PRESET_LOW_LATENCY_DEFAULT ,
    PRESET_LOW_LATENCY_HQ ,
    PRESET_LOW_LATENCY_HP,
    PRESET_LOSSLESS_DEFAULT, // lossless presets must be the last ones
    PRESET_LOSSLESS_HP,
};

enum {
    NV_ENC_H264_PROFILE_BASELINE,
    NV_ENC_H264_PROFILE_MAIN,
    NV_ENC_H264_PROFILE_HIGH,
    NV_ENC_H264_PROFILE_HIGH_444P,
};

enum {
    NV_ENC_HEVC_PROFILE_MAIN,
    NV_ENC_HEVC_PROFILE_MAIN_10,
    NV_ENC_HEVC_PROFILE_REXT,
};

enum {
    NVENC_LOWLATENCY = 1,
    NVENC_LOSSLESS   = 2,
    NVENC_ONE_PASS   = 4,
    NVENC_TWO_PASSES = 8,
};

enum {
    LIST_DEVICES = -2,
    ANY_DEVICE,
};

typedef struct NvencContext
{
    AVClass *avclass;

    NvencDynLoadFunctions nvenc_dload_funcs;

    NV_ENC_INITIALIZE_PARAMS init_encode_params;
    NV_ENC_CONFIG encode_config;
    CUcontext cu_context;
    CUcontext cu_context_internal;

    int nb_surfaces;
    NvencSurface *surfaces;

    AVFifoBuffer *output_surface_queue;
    AVFifoBuffer *output_surface_ready_queue;
    AVFifoBuffer *timestamp_list;

    struct {
        CUdeviceptr ptr;
        NV_ENC_REGISTERED_PTR regptr;
        int mapped;
    } registered_frames[MAX_REGISTERED_FRAMES];
    int nb_registered_frames;

    /* the actual data pixel format, different from
     * AVCodecContext.pix_fmt when using hwaccel frames on input */
    enum AVPixelFormat data_pix_fmt;

    /* timestamps of the first two frames, for computing the first dts
     * when B-frames are present */
    int64_t initial_pts[2];
    int first_packet_output;

    void *nvencoder;

    int preset;
    int profile;
    int level;
    int tier;
    int rc;
    int cbr;
    int twopass;
    int device;
    int flags;
    int async_depth;
    int rc_lookahead;
    int aq;
    int no_scenecut;
    int forced_idr;
    int b_adapt;
    int temporal_aq;
    int zerolatency;
    int nonref_p;
    int strict_gop;
    int aq_strength;
    int quality;
} NvencContext;

int ff_nvenc_encode_init(AVCodecContext *avctx);

int ff_nvenc_encode_close(AVCodecContext *avctx);

int ff_nvenc_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                          const AVFrame *frame, int *got_packet);

extern const enum AVPixelFormat ff_nvenc_pix_fmts[];

#endif /* AVCODEC_NVENC_H */
