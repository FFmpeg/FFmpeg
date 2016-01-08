/*
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef AVCODEC_NVENC_H
#define AVCODEC_NVENC_H

#include <cuda.h>
#include <nvEncodeAPI.h>

#include "libavutil/fifo.h"
#include "libavutil/opt.h"

#include "avcodec.h"

typedef struct NVENCInputSurface {
    NV_ENC_INPUT_PTR in;
    NV_ENC_BUFFER_FORMAT format;
    int locked;
} NVENCInputSurface;

typedef struct NVENCOutputSurface {
    NV_ENC_OUTPUT_PTR out;
    NVENCInputSurface *in;
    int busy;
} NVENCOutputSurface;

typedef CUresult(CUDAAPI *PCUINIT)(unsigned int Flags);
typedef CUresult(CUDAAPI *PCUDEVICEGETCOUNT)(int *count);
typedef CUresult(CUDAAPI *PCUDEVICEGET)(CUdevice *device, int ordinal);
typedef CUresult(CUDAAPI *PCUDEVICEGETNAME)(char *name, int len, CUdevice dev);
typedef CUresult(CUDAAPI *PCUDEVICECOMPUTECAPABILITY)(int *major, int *minor, CUdevice dev);
typedef CUresult(CUDAAPI *PCUCTXCREATE)(CUcontext *pctx, unsigned int flags, CUdevice dev);
typedef CUresult(CUDAAPI *PCUCTXPOPCURRENT)(CUcontext *pctx);
typedef CUresult(CUDAAPI *PCUCTXDESTROY)(CUcontext ctx);

typedef NVENCSTATUS (NVENCAPI *PNVENCODEAPICREATEINSTANCE)(NV_ENCODE_API_FUNCTION_LIST *functionList);

typedef struct NVENCLibraryContext
{
    void *cuda;
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
} NVENCLibraryContext;

enum {
    PRESET_DEFAULT,
    PRESET_HP,
    PRESET_HQ,
    PRESET_BD ,
    PRESET_LOW_LATENCY_DEFAULT ,
    PRESET_LOW_LATENCY_HQ ,
    PRESET_LOW_LATENCY_HP,
    PRESET_LOSSLESS_DEFAULT,
    PRESET_LOSSLESS_HP,
};

enum {
    NV_ENC_H264_PROFILE_BASELINE,
    NV_ENC_H264_PROFILE_MAIN,
    NV_ENC_H264_PROFILE_HIGH,
    NV_ENC_H264_PROFILE_HIGH_444,
    NV_ENC_H264_PROFILE_CONSTRAINED_HIGH,
};

enum {
    NVENC_LOWLATENCY = 1,
    NVENC_LOSSLESS,
};

enum {
    LIST_DEVICES = -2,
    ANY_DEVICE,
};

typedef struct NVENCContext {
    AVClass *class;
    NVENCLibraryContext nvel;

    NV_ENC_INITIALIZE_PARAMS params;
    NV_ENC_CONFIG config;

    CUcontext cu_context;

    int nb_surfaces;
    NVENCInputSurface *in;
    NVENCOutputSurface *out;
    AVFifoBuffer *timestamps;
    AVFifoBuffer *pending, *ready;

    /* timestamps of the first two frames, for computing the first dts
     * when b-frames are present */
    int64_t initial_pts[2];
    int first_packet_output;

    void *nvenc_ctx;

    int preset;
    int profile;
    int level;
    int tier;
    int rc;
    int device;
    int flags;
} NVENCContext;

int ff_nvenc_encode_init(AVCodecContext *avctx);

int ff_nvenc_encode_close(AVCodecContext *avctx);

int ff_nvenc_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                          const AVFrame *frame, int *got_packet);

#endif /* AVCODEC_NVENC_H */
