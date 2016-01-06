/*
 * NVIDIA NVENC Support
 * Copyright (C) 2015 Luca Barbato
 *
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

#include "config.h"

#include <cuda.h>
#include <nvEncodeAPI.h>
#include <string.h>

#define CUDA_LIBNAME "libcuda.so"

#if HAVE_DLFCN_H
#include <dlfcn.h>

#define NVENC_LIBNAME "libnvidia-encode.so"

#elif HAVE_WINDOWS_H
#include <windows.h>

#if ARCH_X86_64
#define NVENC_LIBNAME "nvEncodeAPI64.dll"
#else
#define NVENC_LIBNAME "nvEncodeAPI.dll"
#endif

#define dlopen(filename, flags) LoadLibrary((filename))
#define dlsym(handle, symbol)   GetProcAddress(handle, symbol)
#define dlclose(handle)         FreeLibrary(handle)
#endif

#include "libavutil/common.h"
#include "libavutil/imgutils.h"
#include "libavutil/mem.h"
#include "avcodec.h"
#include "internal.h"
#include "nvenc.h"

#define NVENC_CAP 0x30
#define BITSTREAM_BUFFER_SIZE 1024 * 1024

#define LOAD_LIBRARY(l, path)                   \
    do {                                        \
        if (!((l) = dlopen(path, RTLD_LAZY))) { \
            av_log(avctx, AV_LOG_ERROR,         \
                   "Cannot load %s\n",          \
                   path);                       \
            return AVERROR_UNKNOWN;             \
        }                                       \
    } while (0)

#define LOAD_SYMBOL(fun, lib, symbol)        \
    do {                                     \
        if (!((fun) = dlsym(lib, symbol))) { \
            av_log(avctx, AV_LOG_ERROR,      \
                   "Cannot load %s\n",       \
                   symbol);                  \
            return AVERROR_UNKNOWN;          \
        }                                    \
    } while (0)

static const struct {
    NVENCSTATUS nverr;
    int         averr;
    const char *desc;
} nvenc_errors[] = {
    { NV_ENC_SUCCESS,                      0,                "success"                  },
    { NV_ENC_ERR_NO_ENCODE_DEVICE,         AVERROR(ENOENT),  "no encode device"         },
    { NV_ENC_ERR_UNSUPPORTED_DEVICE,       AVERROR(ENOSYS),  "unsupported device"       },
    { NV_ENC_ERR_INVALID_ENCODERDEVICE,    AVERROR(EINVAL),  "invalid encoder device"   },
    { NV_ENC_ERR_INVALID_DEVICE,           AVERROR(EINVAL),  "invalid device"           },
    { NV_ENC_ERR_DEVICE_NOT_EXIST,         AVERROR(EIO),     "device does not exist"    },
    { NV_ENC_ERR_INVALID_PTR,              AVERROR(EFAULT),  "invalid ptr"              },
    { NV_ENC_ERR_INVALID_EVENT,            AVERROR(EINVAL),  "invalid event"            },
    { NV_ENC_ERR_INVALID_PARAM,            AVERROR(EINVAL),  "invalid param"            },
    { NV_ENC_ERR_INVALID_CALL,             AVERROR(EINVAL),  "invalid call"             },
    { NV_ENC_ERR_OUT_OF_MEMORY,            AVERROR(ENOMEM),  "out of memory"            },
    { NV_ENC_ERR_ENCODER_NOT_INITIALIZED,  AVERROR(EINVAL),  "encoder not initialized"  },
    { NV_ENC_ERR_UNSUPPORTED_PARAM,        AVERROR(ENOSYS),  "unsupported param"        },
    { NV_ENC_ERR_LOCK_BUSY,                AVERROR(EAGAIN),  "lock busy"                },
    { NV_ENC_ERR_NOT_ENOUGH_BUFFER,        AVERROR(ENOBUFS), "not enough buffer"        },
    { NV_ENC_ERR_INVALID_VERSION,          AVERROR(EINVAL),  "invalid version"          },
    { NV_ENC_ERR_MAP_FAILED,               AVERROR(EIO),     "map failed"               },
    { NV_ENC_ERR_NEED_MORE_INPUT,          AVERROR(EAGAIN),  "need more input"          },
    { NV_ENC_ERR_ENCODER_BUSY,             AVERROR(EAGAIN),  "encoder busy"             },
    { NV_ENC_ERR_EVENT_NOT_REGISTERD,      AVERROR(EBADF),   "event not registered"     },
    { NV_ENC_ERR_GENERIC,                  AVERROR_UNKNOWN,  "generic error"            },
    { NV_ENC_ERR_INCOMPATIBLE_CLIENT_KEY,  AVERROR(EINVAL),  "incompatible client key"  },
    { NV_ENC_ERR_UNIMPLEMENTED,            AVERROR(ENOSYS),  "unimplemented"            },
    { NV_ENC_ERR_RESOURCE_REGISTER_FAILED, AVERROR(EIO),     "resource register failed" },
    { NV_ENC_ERR_RESOURCE_NOT_REGISTERED,  AVERROR(EBADF),   "resource not registered"  },
    { NV_ENC_ERR_RESOURCE_NOT_MAPPED,      AVERROR(EBADF),   "resource not mapped"      },
};

static int nvenc_map_error(NVENCSTATUS err, const char **desc)
{
    int i;
    for (i = 0; i < FF_ARRAY_ELEMS(nvenc_errors); i++) {
        if (nvenc_errors[i].nverr == err) {
            if (desc)
                *desc = nvenc_errors[i].desc;
            return nvenc_errors[i].averr;
        }
    }
    if (desc)
        *desc = "unknown error";
    return AVERROR_UNKNOWN;
}

static int nvenc_print_error(void *log_ctx, NVENCSTATUS err,
                             const char *error_string)
{
    const char *desc;
    int ret;
    ret = nvenc_map_error(err, &desc);
    av_log(log_ctx, AV_LOG_ERROR, "%s: %s (%d)\n", error_string, desc, err);
    return ret;
}

static av_cold int nvenc_load_libraries(AVCodecContext *avctx)
{
    NVENCContext *ctx         = avctx->priv_data;
    NVENCLibraryContext *nvel = &ctx->nvel;
    PNVENCODEAPICREATEINSTANCE nvenc_create_instance;
    NVENCSTATUS err;

    LOAD_LIBRARY(nvel->cuda, CUDA_LIBNAME);

    LOAD_SYMBOL(nvel->cu_init, nvel->cuda, "cuInit");
    LOAD_SYMBOL(nvel->cu_device_get_count, nvel->cuda, "cuDeviceGetCount");
    LOAD_SYMBOL(nvel->cu_device_get, nvel->cuda, "cuDeviceGet");
    LOAD_SYMBOL(nvel->cu_device_get_name, nvel->cuda, "cuDeviceGetName");
    LOAD_SYMBOL(nvel->cu_device_compute_capability, nvel->cuda,
                "cuDeviceComputeCapability");
    LOAD_SYMBOL(nvel->cu_ctx_create, nvel->cuda, "cuCtxCreate_v2");
    LOAD_SYMBOL(nvel->cu_ctx_pop_current, nvel->cuda, "cuCtxPopCurrent_v2");
    LOAD_SYMBOL(nvel->cu_ctx_destroy, nvel->cuda, "cuCtxDestroy_v2");

    LOAD_LIBRARY(nvel->nvenc, NVENC_LIBNAME);

    LOAD_SYMBOL(nvenc_create_instance, nvel->nvenc,
                "NvEncodeAPICreateInstance");

    nvel->nvenc_funcs.version = NV_ENCODE_API_FUNCTION_LIST_VER;

    err = nvenc_create_instance(&nvel->nvenc_funcs);
    if (err != NV_ENC_SUCCESS)
        return nvenc_print_error(avctx, err, "Cannot create the NVENC instance");

    return 0;
}

static int nvenc_open_session(AVCodecContext *avctx)
{
    NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS params = { 0 };
    NVENCContext *ctx                           = avctx->priv_data;
    NV_ENCODE_API_FUNCTION_LIST *nv             = &ctx->nvel.nvenc_funcs;
    int ret;

    params.version    = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
    params.apiVersion = NVENCAPI_VERSION;
    params.device     = ctx->cu_context;
    params.deviceType = NV_ENC_DEVICE_TYPE_CUDA;

    ret = nv->nvEncOpenEncodeSessionEx(&params, &ctx->nvenc_ctx);
    if (ret != NV_ENC_SUCCESS) {
        ctx->nvenc_ctx = NULL;
        return nvenc_print_error(avctx, ret, "Cannot open the NVENC Session");
    }

    return 0;
}

static int nvenc_check_codec_support(AVCodecContext *avctx)
{
    NVENCContext *ctx               = avctx->priv_data;
    NV_ENCODE_API_FUNCTION_LIST *nv = &ctx->nvel.nvenc_funcs;
    int i, ret, count = 0;
    GUID *guids = NULL;

    ret = nv->nvEncGetEncodeGUIDCount(ctx->nvenc_ctx, &count);

    if (ret != NV_ENC_SUCCESS || !count)
        return AVERROR(ENOSYS);

    guids = av_malloc(count * sizeof(GUID));
    if (!guids)
        return AVERROR(ENOMEM);

    ret = nv->nvEncGetEncodeGUIDs(ctx->nvenc_ctx, guids, count, &count);
    if (ret != NV_ENC_SUCCESS) {
        ret = AVERROR(ENOSYS);
        goto fail;
    }

    ret = AVERROR(ENOSYS);
    for (i = 0; i < count; i++) {
        if (!memcmp(&guids[i], &ctx->params.encodeGUID, sizeof(*guids))) {
            ret = 0;
            break;
        }
    }

fail:
    av_free(guids);

    return ret;
}

static int nvenc_check_cap(AVCodecContext *avctx, NV_ENC_CAPS cap)
{
    NVENCContext *ctx               = avctx->priv_data;
    NV_ENCODE_API_FUNCTION_LIST *nv = &ctx->nvel.nvenc_funcs;
    NV_ENC_CAPS_PARAM params        = { 0 };
    int ret, val = 0;

    params.version     = NV_ENC_CAPS_PARAM_VER;
    params.capsToQuery = cap;

    ret = nv->nvEncGetEncodeCaps(ctx->nvenc_ctx, ctx->params.encodeGUID, &params, &val);

    if (ret == NV_ENC_SUCCESS)
        return val;
    return 0;
}

static int nvenc_check_capabilities(AVCodecContext *avctx)
{
    int ret;

    ret = nvenc_check_codec_support(avctx);
    if (ret < 0) {
        av_log(avctx, AV_LOG_VERBOSE, "Codec not supported\n");
        return ret;
    }

    ret = nvenc_check_cap(avctx, NV_ENC_CAPS_SUPPORT_YUV444_ENCODE);
    if (avctx->pix_fmt == AV_PIX_FMT_YUV444P && ret <= 0) {
        av_log(avctx, AV_LOG_VERBOSE, "YUV444P not supported\n");
        return AVERROR(ENOSYS);
    }

    ret = nvenc_check_cap(avctx, NV_ENC_CAPS_WIDTH_MAX);
    if (ret < avctx->width) {
        av_log(avctx, AV_LOG_VERBOSE, "Width %d exceeds %d\n",
               avctx->width, ret);
        return AVERROR(ENOSYS);
    }

    ret = nvenc_check_cap(avctx, NV_ENC_CAPS_HEIGHT_MAX);
    if (ret < avctx->height) {
        av_log(avctx, AV_LOG_VERBOSE, "Height %d exceeds %d\n",
               avctx->height, ret);
        return AVERROR(ENOSYS);
    }

    ret = nvenc_check_cap(avctx, NV_ENC_CAPS_NUM_MAX_BFRAMES);
    if (ret < avctx->max_b_frames) {
        av_log(avctx, AV_LOG_VERBOSE, "Max b-frames %d exceed %d\n",
               avctx->max_b_frames, ret);

        return AVERROR(ENOSYS);
    }

    return 0;
}

static int nvenc_check_device(AVCodecContext *avctx, int idx)
{
    NVENCContext *ctx               = avctx->priv_data;
    NVENCLibraryContext *nvel       = &ctx->nvel;
    char name[128]                  = { 0 };
    int major, minor, ret;
    CUdevice cu_device;
    CUcontext dummy;
    int loglevel = AV_LOG_VERBOSE;

    if (ctx->device == LIST_DEVICES)
        loglevel = AV_LOG_INFO;

    ret = nvel->cu_device_get(&cu_device, idx);
    if (ret != CUDA_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR,
               "Cannot access the CUDA device %d\n",
               idx);
        return -1;
    }

    ret = nvel->cu_device_get_name(name, sizeof(name), cu_device);
    if (ret != CUDA_SUCCESS)
        return -1;

    ret = nvel->cu_device_compute_capability(&major, &minor, cu_device);
    if (ret != CUDA_SUCCESS)
        return -1;

    av_log(avctx, loglevel, "Device %d [%s] ", cu_device, name);

    if (((major << 4) | minor) < NVENC_CAP)
        goto fail;

    ret = nvel->cu_ctx_create(&ctx->cu_context, 0, cu_device);
    if (ret != CUDA_SUCCESS)
        goto fail;

    ret = nvel->cu_ctx_pop_current(&dummy);
    if (ret != CUDA_SUCCESS)
        goto fail2;

    if ((ret = nvenc_open_session(avctx)) < 0)
        goto fail2;

    if ((ret = nvenc_check_capabilities(avctx)) < 0)
        goto fail3;

    av_log(avctx, loglevel, "supports NVENC\n");

    if (ctx->device == cu_device || ctx->device == ANY_DEVICE)
        return 0;

fail3:
    nvel->nvenc_funcs.nvEncDestroyEncoder(ctx->nvenc_ctx);
    ctx->nvenc_ctx = NULL;

fail2:
    nvel->cu_ctx_destroy(ctx->cu_context);
    ctx->cu_context = NULL;

fail:
    if (ret != 0)
        av_log(avctx, loglevel, "does not support NVENC (major %d minor %d)\n",
               major, minor);

    return AVERROR(ENOSYS);
}

static int nvenc_setup_device(AVCodecContext *avctx)
{
    NVENCContext *ctx         = avctx->priv_data;
    NVENCLibraryContext *nvel = &ctx->nvel;
    int i, nb_devices = 0;

    if ((nvel->cu_init(0)) != CUDA_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR,
               "Cannot init CUDA\n");
        return AVERROR_UNKNOWN;
    }

    if ((nvel->cu_device_get_count(&nb_devices)) != CUDA_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR,
               "Cannot enumerate the CUDA devices\n");
        return AVERROR_UNKNOWN;
    }

    switch (avctx->codec->id) {
    case AV_CODEC_ID_H264:
        ctx->params.encodeGUID = NV_ENC_CODEC_H264_GUID;
        break;
    case AV_CODEC_ID_HEVC:
        ctx->params.encodeGUID = NV_ENC_CODEC_HEVC_GUID;
        break;
    default:
        return AVERROR_BUG;
    }

    for (i = 0; i < nb_devices; ++i) {
        if ((nvenc_check_device(avctx, i)) >= 0 && ctx->device != LIST_DEVICES)
            return 0;
    }

    if (ctx->device == LIST_DEVICES)
        return AVERROR_EXIT;

    return AVERROR(ENOSYS);
}

typedef struct GUIDTuple {
    const GUID guid;
    int flags;
} GUIDTuple;

static int nvec_map_preset(NVENCContext *ctx)
{
    GUIDTuple presets[] = {
        { NV_ENC_PRESET_DEFAULT_GUID },
        { NV_ENC_PRESET_HP_GUID },
        { NV_ENC_PRESET_HQ_GUID },
        { NV_ENC_PRESET_BD_GUID },
        { NV_ENC_PRESET_LOW_LATENCY_DEFAULT_GUID, NVENC_LOWLATENCY },
        { NV_ENC_PRESET_LOW_LATENCY_HP_GUID,      NVENC_LOWLATENCY },
        { NV_ENC_PRESET_LOW_LATENCY_HQ_GUID,      NVENC_LOWLATENCY },
        { NV_ENC_PRESET_LOSSLESS_DEFAULT_GUID,    NVENC_LOSSLESS },
        { NV_ENC_PRESET_LOSSLESS_HP_GUID,         NVENC_LOSSLESS },
        { { 0 } }
    };

    GUIDTuple *t = &presets[ctx->preset];

    ctx->params.presetGUID = t->guid;
    ctx->flags             = t->flags;

    return AVERROR(EINVAL);
}

static void set_constqp(AVCodecContext *avctx, NV_ENC_RC_PARAMS *rc)
{
    rc->rateControlMode = NV_ENC_PARAMS_RC_CONSTQP;
    rc->constQP.qpInterB = avctx->global_quality;
    rc->constQP.qpInterP = avctx->global_quality;
    rc->constQP.qpIntra  = avctx->global_quality;
}

static void set_vbr(AVCodecContext *avctx, NV_ENC_RC_PARAMS *rc)
{
    if (avctx->qmin >= 0) {
        rc->enableMinQP    = 1;
        rc->minQP.qpInterB = avctx->qmin;
        rc->minQP.qpInterP = avctx->qmin;
        rc->minQP.qpIntra  = avctx->qmin;
    }

    if (avctx->qmax >= 0) {
        rc->enableMaxQP = 1;
        rc->maxQP.qpInterB = avctx->qmax;
        rc->maxQP.qpInterP = avctx->qmax;
        rc->maxQP.qpIntra  = avctx->qmax;
    }
}

static void nvenc_override_rate_control(AVCodecContext *avctx,
                                        NV_ENC_RC_PARAMS *rc)
{
    NVENCContext *ctx    = avctx->priv_data;

    switch (ctx->rc) {
    case NV_ENC_PARAMS_RC_CONSTQP:
        if (avctx->global_quality < 0) {
            av_log(avctx, AV_LOG_WARNING,
                   "The constant quality rate-control requires "
                   "the 'global_quality' option set.\n");
            return;
        }
        set_constqp(avctx, rc);
        return;
    case NV_ENC_PARAMS_RC_2_PASS_VBR:
    case NV_ENC_PARAMS_RC_VBR:
        if (avctx->qmin < 0 && avctx->qmax < 0) {
            av_log(avctx, AV_LOG_WARNING,
                   "The variable bitrate rate-control requires "
                   "the 'qmin' and/or 'qmax' option set.\n");
            return;
        }
    case NV_ENC_PARAMS_RC_VBR_MINQP:
        if (avctx->qmin < 0) {
            av_log(avctx, AV_LOG_WARNING,
                   "The variable bitrate rate-control requires "
                   "the 'qmin' option set.\n");
            return;
        }
        set_vbr(avctx, rc);
        break;
    case NV_ENC_PARAMS_RC_CBR:
        break;
    case NV_ENC_PARAMS_RC_2_PASS_QUALITY:
    case NV_ENC_PARAMS_RC_2_PASS_FRAMESIZE_CAP:
        if (!(ctx->flags & NVENC_LOWLATENCY)) {
            av_log(avctx, AV_LOG_WARNING,
                   "The multipass rate-control requires "
                   "a low-latency preset.\n");
            return;
        }
    }

    rc->rateControlMode = ctx->rc;
}

static void nvenc_setup_rate_control(AVCodecContext *avctx)
{
    NVENCContext *ctx    = avctx->priv_data;
    NV_ENC_RC_PARAMS *rc = &ctx->config.rcParams;

    if (avctx->bit_rate > 0)
        rc->averageBitRate = avctx->bit_rate;

    if (avctx->rc_max_rate > 0)
        rc->maxBitRate = avctx->rc_max_rate;

    if (ctx->rc > 0) {
        nvenc_override_rate_control(avctx, rc);
    } else if (avctx->global_quality > 0) {
        set_constqp(avctx, rc);
    } else if (avctx->qmin >= 0 && avctx->qmax >= 0) {
        rc->rateControlMode = NV_ENC_PARAMS_RC_VBR;
        set_vbr(avctx, rc);
    }

    if (avctx->rc_buffer_size > 0)
        rc->vbvBufferSize = avctx->rc_buffer_size;

    if (rc->averageBitRate > 0)
        avctx->bit_rate = rc->averageBitRate;
}

static int nvenc_setup_h264_config(AVCodecContext *avctx)
{
    NVENCContext *ctx                      = avctx->priv_data;
    NV_ENC_CONFIG *cc                      = &ctx->config;
    NV_ENC_CONFIG_H264 *h264               = &cc->encodeCodecConfig.h264Config;
    NV_ENC_CONFIG_H264_VUI_PARAMETERS *vui = &h264->h264VUIParameters;

    vui->colourDescriptionPresentFlag = 1;
    vui->videoSignalTypePresentFlag   = 1;

    vui->colourMatrix            = avctx->colorspace;
    vui->colourPrimaries         = avctx->color_primaries;
    vui->transferCharacteristics = avctx->color_trc;

    vui->videoFullRangeFlag = avctx->color_range == AVCOL_RANGE_JPEG;

    h264->disableSPSPPS = (avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER) ? 1 : 0;
    h264->repeatSPSPPS  = (avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER) ? 0 : 1;

    h264->maxNumRefFrames = avctx->refs;
    h264->idrPeriod       = cc->gopLength;

    if (ctx->profile)
        avctx->profile = ctx->profile;

    if (avctx->pix_fmt == AV_PIX_FMT_YUV444P)
        h264->chromaFormatIDC = 3;
    else
        h264->chromaFormatIDC = 1;

    switch (ctx->profile) {
    case NV_ENC_H264_PROFILE_BASELINE:
        cc->profileGUID = NV_ENC_H264_PROFILE_BASELINE_GUID;
        break;
    case NV_ENC_H264_PROFILE_MAIN:
        cc->profileGUID = NV_ENC_H264_PROFILE_MAIN_GUID;
        break;
    case NV_ENC_H264_PROFILE_HIGH:
        cc->profileGUID = NV_ENC_H264_PROFILE_HIGH_GUID;
        break;
    case NV_ENC_H264_PROFILE_HIGH_444:
        cc->profileGUID = NV_ENC_H264_PROFILE_HIGH_444_GUID;
        break;
    case NV_ENC_H264_PROFILE_CONSTRAINED_HIGH:
        cc->profileGUID = NV_ENC_H264_PROFILE_CONSTRAINED_HIGH_GUID;
        break;
    }

    h264->level = ctx->level;

    return 0;
}

static int nvenc_setup_hevc_config(AVCodecContext *avctx)
{
    NVENCContext *ctx                      = avctx->priv_data;
    NV_ENC_CONFIG *cc                      = &ctx->config;
    NV_ENC_CONFIG_HEVC *hevc               = &cc->encodeCodecConfig.hevcConfig;

    hevc->disableSPSPPS = (avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER) ? 1 : 0;
    hevc->repeatSPSPPS  = (avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER) ? 0 : 1;

    hevc->maxNumRefFramesInDPB = avctx->refs;
    hevc->idrPeriod            = cc->gopLength;

    /* No other profile is supported in the current SDK version 5 */
    cc->profileGUID = NV_ENC_HEVC_PROFILE_MAIN_GUID;
    avctx->profile  = FF_PROFILE_HEVC_MAIN;

    if (ctx->level) {
        hevc->level = ctx->level;
    } else {
        hevc->level = NV_ENC_LEVEL_AUTOSELECT;
    }

    if (ctx->tier) {
        hevc->tier = ctx->tier;
    }

    return 0;
}
static int nvenc_setup_codec_config(AVCodecContext *avctx)
{
    switch (avctx->codec->id) {
    case AV_CODEC_ID_H264:
        return nvenc_setup_h264_config(avctx);
    case AV_CODEC_ID_HEVC:
        return nvenc_setup_hevc_config(avctx);
    }
    return 0;
}

static int nvenc_setup_encoder(AVCodecContext *avctx)
{
    NVENCContext *ctx               = avctx->priv_data;
    NV_ENCODE_API_FUNCTION_LIST *nv = &ctx->nvel.nvenc_funcs;
    NV_ENC_PRESET_CONFIG preset_cfg = { 0 };
    AVCPBProperties *cpb_props;
    int ret;

    ctx->params.version = NV_ENC_INITIALIZE_PARAMS_VER;

    ctx->params.encodeHeight = avctx->height;
    ctx->params.encodeWidth  = avctx->width;

    if (avctx->sample_aspect_ratio.num &&
        avctx->sample_aspect_ratio.den &&
        (avctx->sample_aspect_ratio.num != 1 ||
         avctx->sample_aspect_ratio.den != 1)) {
        av_reduce(&ctx->params.darWidth,
                  &ctx->params.darHeight,
                  avctx->width * avctx->sample_aspect_ratio.num,
                  avctx->height * avctx->sample_aspect_ratio.den,
                  INT_MAX / 8);
    } else {
        ctx->params.darHeight = avctx->height;
        ctx->params.darWidth  = avctx->width;
    }

    ctx->params.frameRateNum = avctx->time_base.den;
    ctx->params.frameRateDen = avctx->time_base.num * avctx->ticks_per_frame;

    ctx->params.enableEncodeAsync = 0;
    ctx->params.enablePTD         = 1;

    ctx->params.encodeConfig = &ctx->config;

    nvec_map_preset(ctx);

    preset_cfg.version           = NV_ENC_PRESET_CONFIG_VER;
    preset_cfg.presetCfg.version = NV_ENC_CONFIG_VER;

    ret = nv->nvEncGetEncodePresetConfig(ctx->nvenc_ctx,
                                         ctx->params.encodeGUID,
                                         ctx->params.presetGUID,
                                         &preset_cfg);
    if (ret != NV_ENC_SUCCESS)
        return nvenc_print_error(avctx, ret, "Cannot get the preset configuration");

    memcpy(&ctx->config, &preset_cfg.presetCfg, sizeof(ctx->config));

    ctx->config.version = NV_ENC_CONFIG_VER;

    if (avctx->gop_size > 0) {
        if (avctx->max_b_frames > 0) {
            ctx->last_dts = -2;
            /* 0 is intra-only,
             * 1 is I/P only,
             * 2 is one B Frame,
             * 3 two B frames, and so on. */
            ctx->config.frameIntervalP = avctx->max_b_frames + 1;
        } else if (avctx->max_b_frames == 0) {
            ctx->config.frameIntervalP = 1;
        }
        ctx->config.gopLength = avctx->gop_size;
    } else if (avctx->gop_size == 0) {
        ctx->config.frameIntervalP = 0;
        ctx->config.gopLength      = 1;
    }

    if (ctx->config.frameIntervalP > 1)
        avctx->max_b_frames = ctx->config.frameIntervalP - 1;

    nvenc_setup_rate_control(avctx);

    if (avctx->flags & AV_CODEC_FLAG_INTERLACED_DCT) {
        ctx->config.frameFieldMode = NV_ENC_PARAMS_FRAME_FIELD_MODE_FIELD;
    } else {
        ctx->config.frameFieldMode = NV_ENC_PARAMS_FRAME_FIELD_MODE_FRAME;
    }

    if ((ret = nvenc_setup_codec_config(avctx)) < 0)
        return ret;

    ret = nv->nvEncInitializeEncoder(ctx->nvenc_ctx, &ctx->params);
    if (ret != NV_ENC_SUCCESS)
        return nvenc_print_error(avctx, ret, "Cannot initialize the decoder");

    cpb_props = ff_add_cpb_side_data(avctx);
    if (!cpb_props)
        return AVERROR(ENOMEM);
    cpb_props->max_bitrate = avctx->rc_max_rate;
    cpb_props->min_bitrate = avctx->rc_min_rate;
    cpb_props->avg_bitrate = avctx->bit_rate;
    cpb_props->buffer_size = avctx->rc_buffer_size;

    return 0;
}

static int nvenc_alloc_surface(AVCodecContext *avctx, int idx)
{
    NVENCContext *ctx               = avctx->priv_data;
    NV_ENCODE_API_FUNCTION_LIST *nv = &ctx->nvel.nvenc_funcs;
    int ret;
    NV_ENC_CREATE_INPUT_BUFFER in_buffer      = { 0 };
    NV_ENC_CREATE_BITSTREAM_BUFFER out_buffer = { 0 };

    in_buffer.version  = NV_ENC_CREATE_INPUT_BUFFER_VER;
    out_buffer.version = NV_ENC_CREATE_BITSTREAM_BUFFER_VER;

    in_buffer.width  = avctx->width;
    in_buffer.height = avctx->height;

    in_buffer.memoryHeap = NV_ENC_MEMORY_HEAP_SYSMEM_UNCACHED;

    switch (avctx->pix_fmt) {
    case AV_PIX_FMT_YUV420P:
        in_buffer.bufferFmt = NV_ENC_BUFFER_FORMAT_YV12_PL;
        break;
    case AV_PIX_FMT_NV12:
        in_buffer.bufferFmt = NV_ENC_BUFFER_FORMAT_NV12_PL;
        break;
    case AV_PIX_FMT_YUV444P:
        in_buffer.bufferFmt = NV_ENC_BUFFER_FORMAT_YUV444_PL;
        break;
    default:
        return AVERROR_BUG;
    }

    ret = nv->nvEncCreateInputBuffer(ctx->nvenc_ctx, &in_buffer);
    if (ret != NV_ENC_SUCCESS)
        return nvenc_print_error(avctx, ret, "CreateInputBuffer failed");

    ctx->in[idx].in        = in_buffer.inputBuffer;
    ctx->in[idx].format    = in_buffer.bufferFmt;

    /* 1MB is large enough to hold most output frames.
     * NVENC increases this automaticaly if it's not enough. */
    out_buffer.size = BITSTREAM_BUFFER_SIZE;

    out_buffer.memoryHeap = NV_ENC_MEMORY_HEAP_SYSMEM_UNCACHED;

    ret = nv->nvEncCreateBitstreamBuffer(ctx->nvenc_ctx, &out_buffer);
    if (ret != NV_ENC_SUCCESS)
        return nvenc_print_error(avctx, ret, "CreateBitstreamBuffer failed");

    ctx->out[idx].out  = out_buffer.bitstreamBuffer;
    ctx->out[idx].busy = 0;

    return 0;
}

static int nvenc_setup_surfaces(AVCodecContext *avctx)
{
    NVENCContext *ctx = avctx->priv_data;
    int i, ret;

    ctx->nb_surfaces = FFMAX(4 + avctx->max_b_frames,
                             ctx->nb_surfaces);

    ctx->in = av_mallocz(ctx->nb_surfaces * sizeof(*ctx->in));
    if (!ctx->in)
        return AVERROR(ENOMEM);

    ctx->out = av_mallocz(ctx->nb_surfaces * sizeof(*ctx->out));
    if (!ctx->out)
        return AVERROR(ENOMEM);

    ctx->timestamps = av_fifo_alloc(ctx->nb_surfaces * sizeof(int64_t));
    if (!ctx->timestamps)
        return AVERROR(ENOMEM);
    ctx->pending = av_fifo_alloc(ctx->nb_surfaces * sizeof(ctx->out));
    if (!ctx->pending)
        return AVERROR(ENOMEM);
    ctx->ready = av_fifo_alloc(ctx->nb_surfaces * sizeof(ctx->out));
    if (!ctx->ready)
        return AVERROR(ENOMEM);

    for (i = 0; i < ctx->nb_surfaces; i++) {
        if ((ret = nvenc_alloc_surface(avctx, i)) < 0)
            return ret;
    }

    return 0;
}

#define EXTRADATA_SIZE 512

static int nvenc_setup_extradata(AVCodecContext *avctx)
{
    NVENCContext *ctx                     = avctx->priv_data;
    NV_ENCODE_API_FUNCTION_LIST *nv       = &ctx->nvel.nvenc_funcs;
    NV_ENC_SEQUENCE_PARAM_PAYLOAD payload = { 0 };
    int ret;

    avctx->extradata = av_mallocz(EXTRADATA_SIZE + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!avctx->extradata)
        return AVERROR(ENOMEM);

    payload.version              = NV_ENC_SEQUENCE_PARAM_PAYLOAD_VER;
    payload.spsppsBuffer         = avctx->extradata;
    payload.inBufferSize         = EXTRADATA_SIZE;
    payload.outSPSPPSPayloadSize = &avctx->extradata_size;

    ret = nv->nvEncGetSequenceParams(ctx->nvenc_ctx, &payload);
    if (ret != NV_ENC_SUCCESS)
        return nvenc_print_error(avctx, ret, "Cannot get the extradata");

    return 0;
}

av_cold int ff_nvenc_encode_close(AVCodecContext *avctx)
{
    NVENCContext *ctx               = avctx->priv_data;
    NV_ENCODE_API_FUNCTION_LIST *nv = &ctx->nvel.nvenc_funcs;
    int i;

    av_fifo_free(ctx->timestamps);
    av_fifo_free(ctx->pending);
    av_fifo_free(ctx->ready);

    if (ctx->in) {
        for (i = 0; i < ctx->nb_surfaces; ++i) {
            nv->nvEncDestroyInputBuffer(ctx->nvenc_ctx, ctx->in[i].in);
            nv->nvEncDestroyBitstreamBuffer(ctx->nvenc_ctx, ctx->out[i].out);
        }
    }

    av_freep(&ctx->in);
    av_freep(&ctx->out);

    if (ctx->nvenc_ctx)
        nv->nvEncDestroyEncoder(ctx->nvenc_ctx);

    if (ctx->cu_context)
        ctx->nvel.cu_ctx_destroy(ctx->cu_context);

    if (ctx->nvel.nvenc)
        dlclose(ctx->nvel.nvenc);

    if (ctx->nvel.cuda)
        dlclose(ctx->nvel.cuda);

    return 0;
}

av_cold int ff_nvenc_encode_init(AVCodecContext *avctx)
{
    int ret;

    if ((ret = nvenc_load_libraries(avctx)) < 0)
        return ret;

    if ((ret = nvenc_setup_device(avctx)) < 0)
        return ret;

    if ((ret = nvenc_setup_encoder(avctx)) < 0)
        return ret;

    if ((ret = nvenc_setup_surfaces(avctx)) < 0)
        return ret;

    if (avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER) {
        if ((ret = nvenc_setup_extradata(avctx)) < 0)
            return ret;
    }

    return 0;
}

static NVENCInputSurface *get_input_surface(NVENCContext *ctx)
{
    int i;

    for (i = 0; i < ctx->nb_surfaces; i++) {
        if (!ctx->in[i].locked) {
            ctx->in[i].locked = 1;
            return &ctx->in[i];
        }
    }

    return NULL;
}

static NVENCOutputSurface *get_output_surface(NVENCContext *ctx)
{
    int i;

    for (i = 0; i < ctx->nb_surfaces; i++) {
        if (!ctx->out[i].busy) {
            return &ctx->out[i];
        }
    }

    return NULL;
}

static int nvenc_copy_frame(NV_ENC_LOCK_INPUT_BUFFER *in, const AVFrame *frame)
{
    uint8_t *buf = in->bufferDataPtr;
    int off      = frame->height * in->pitch;

    switch (frame->format) {
    case AV_PIX_FMT_YUV420P:
        av_image_copy_plane(buf, in->pitch,
                            frame->data[0], frame->linesize[0],
                            frame->width, frame->height);
        buf += off;

        av_image_copy_plane(buf, in->pitch >> 1,
                            frame->data[2], frame->linesize[2],
                            frame->width >> 1, frame->height >> 1);

        buf += off >> 2;

        av_image_copy_plane(buf, in->pitch >> 1,
                            frame->data[1], frame->linesize[1],
                            frame->width >> 1, frame->height >> 1);
        break;
    case AV_PIX_FMT_NV12:
        av_image_copy_plane(buf, in->pitch,
                            frame->data[0], frame->linesize[0],
                            frame->width, frame->height);
        buf += off;

        av_image_copy_plane(buf, in->pitch,
                            frame->data[1], frame->linesize[1],
                            frame->width, frame->height >> 1);
        break;
    case AV_PIX_FMT_YUV444P:
        av_image_copy_plane(buf, in->pitch,
                            frame->data[0], frame->linesize[0],
                            frame->width, frame->height);
        buf += off;

        av_image_copy_plane(buf, in->pitch,
                            frame->data[1], frame->linesize[1],
                            frame->width, frame->height);
        buf += off;

        av_image_copy_plane(buf, in->pitch,
                            frame->data[2], frame->linesize[2],
                            frame->width, frame->height);
        break;
    default:
        return AVERROR_BUG;
    }

    return 0;
}

static int nvenc_enqueue_frame(AVCodecContext *avctx, const AVFrame *frame,
                               NVENCInputSurface **in_surf)
{
    NVENCContext *ctx               = avctx->priv_data;
    NV_ENCODE_API_FUNCTION_LIST *nv = &ctx->nvel.nvenc_funcs;
    NV_ENC_LOCK_INPUT_BUFFER params = { 0 };
    NVENCInputSurface *in           = get_input_surface(ctx);
    int ret;

    if (!in)
        return AVERROR_BUG;

    params.version     = NV_ENC_LOCK_INPUT_BUFFER_VER;
    params.inputBuffer = in->in;


    ret = nv->nvEncLockInputBuffer(ctx->nvenc_ctx, &params);
    if (ret != NV_ENC_SUCCESS)
        return nvenc_print_error(avctx, ret, "Cannot lock the buffer");

    ret = nvenc_copy_frame(&params, frame);
    if (ret < 0)
        goto fail;

    ret = nv->nvEncUnlockInputBuffer(ctx->nvenc_ctx, in->in);
    if (ret != NV_ENC_SUCCESS)
        return nvenc_print_error(avctx, ret, "Cannot unlock the buffer");

    *in_surf = in;

    return 0;

fail:
    nv->nvEncUnlockInputBuffer(ctx->nvenc_ctx, in->in);

    return ret;
}

static void nvenc_codec_specific_pic_params(AVCodecContext *avctx,
                                            NV_ENC_PIC_PARAMS *params)
{
    NVENCContext *ctx = avctx->priv_data;

    switch (avctx->codec->id) {
    case AV_CODEC_ID_H264:
        params->codecPicParams.h264PicParams.sliceMode =
            ctx->config.encodeCodecConfig.h264Config.sliceMode;
        params->codecPicParams.h264PicParams.sliceModeData =
            ctx->config.encodeCodecConfig.h264Config.sliceModeData;
        break;
    case AV_CODEC_ID_HEVC:
        params->codecPicParams.hevcPicParams.sliceMode =
            ctx->config.encodeCodecConfig.hevcConfig.sliceMode;
        params->codecPicParams.hevcPicParams.sliceModeData =
            ctx->config.encodeCodecConfig.hevcConfig.sliceModeData;
        break;
    }
}

static inline int nvenc_enqueue_timestamp(AVFifoBuffer *f, int64_t pts)
{
    return av_fifo_generic_write(f, &pts, sizeof(pts), NULL);
}

static inline int nvenc_dequeue_timestamp(AVFifoBuffer *f, int64_t *pts)
{
    return av_fifo_generic_read(f, pts, sizeof(*pts), NULL);
}

static inline int nvenc_enqueue_surface(AVFifoBuffer *f,
                                        NVENCOutputSurface *surf)
{
    surf->busy = 1;
    return av_fifo_generic_write(f, &surf, sizeof(surf), NULL);
}

static inline int nvenc_dequeue_surface(AVFifoBuffer *f,
                                        NVENCOutputSurface **surf)
{
    return av_fifo_generic_read(f, surf, sizeof(*surf), NULL);
}

static int nvenc_set_timestamp(NVENCContext *ctx,
                               NV_ENC_LOCK_BITSTREAM *params,
                               AVPacket *pkt)
{
    pkt->pts      = params->outputTimeStamp;
    pkt->duration = params->outputDuration;

    return nvenc_dequeue_timestamp(ctx->timestamps, &pkt->dts);
}

static int nvenc_get_frame(AVCodecContext *avctx, AVPacket *pkt)
{
    NVENCContext *ctx               = avctx->priv_data;
    NV_ENCODE_API_FUNCTION_LIST *nv = &ctx->nvel.nvenc_funcs;
    NV_ENC_LOCK_BITSTREAM params    = { 0 };
    NVENCOutputSurface *out         = NULL;
    int ret;

    ret = nvenc_dequeue_surface(ctx->pending, &out);
    if (ret)
        return ret;

    params.version         = NV_ENC_LOCK_BITSTREAM_VER;
    params.outputBitstream = out->out;

    ret = nv->nvEncLockBitstream(ctx->nvenc_ctx, &params);
    if (ret < 0)
        return nvenc_print_error(avctx, ret, "Cannot lock the bitstream");

    ret = ff_alloc_packet(pkt, params.bitstreamSizeInBytes);
    if (ret < 0)
        return ret;

    memcpy(pkt->data, params.bitstreamBufferPtr, pkt->size);

    ret = nv->nvEncUnlockBitstream(ctx->nvenc_ctx, out->out);
    if (ret < 0)
        return nvenc_print_error(avctx, ret, "Cannot unlock the bitstream");

    out->busy = out->in->locked = 0;

    ret = nvenc_set_timestamp(ctx, &params, pkt);
    if (ret < 0)
        return ret;

    switch (params.pictureType) {
    case NV_ENC_PIC_TYPE_IDR:
        pkt->flags |= AV_PKT_FLAG_KEY;
#if FF_API_CODED_FRAME
FF_DISABLE_DEPRECATION_WARNINGS
    case NV_ENC_PIC_TYPE_INTRA_REFRESH:
    case NV_ENC_PIC_TYPE_I:
        avctx->coded_frame->pict_type = AV_PICTURE_TYPE_I;
        break;
    case NV_ENC_PIC_TYPE_P:
        avctx->coded_frame->pict_type = AV_PICTURE_TYPE_P;
        break;
    case NV_ENC_PIC_TYPE_B:
        avctx->coded_frame->pict_type = AV_PICTURE_TYPE_B;
        break;
    case NV_ENC_PIC_TYPE_BI:
        avctx->coded_frame->pict_type = AV_PICTURE_TYPE_BI;
        break;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
    }

    return 0;
}

int ff_nvenc_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                          const AVFrame *frame, int *got_packet)
{
    NVENCContext *ctx               = avctx->priv_data;
    NV_ENCODE_API_FUNCTION_LIST *nv = &ctx->nvel.nvenc_funcs;
    NV_ENC_PIC_PARAMS params        = { 0 };
    NVENCInputSurface *in           = NULL;
    NVENCOutputSurface *out         = NULL;
    int ret;

    params.version = NV_ENC_PIC_PARAMS_VER;

    if (frame) {
        ret = nvenc_enqueue_frame(avctx, frame, &in);
        if (ret < 0)
            return ret;
        out = get_output_surface(ctx);
        if (!out)
            return AVERROR_BUG;

        out->in = in;

        params.inputBuffer     = in->in;
        params.bufferFmt       = in->format;
        params.inputWidth      = frame->width;
        params.inputHeight     = frame->height;
        params.outputBitstream = out->out;
        params.inputTimeStamp  = frame->pts;

        if (avctx->flags & AV_CODEC_FLAG_INTERLACED_DCT) {
            if (frame->top_field_first)
                params.pictureStruct = NV_ENC_PIC_STRUCT_FIELD_TOP_BOTTOM;
            else
                params.pictureStruct = NV_ENC_PIC_STRUCT_FIELD_BOTTOM_TOP;
        } else {
            params.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;
        }

        nvenc_codec_specific_pic_params(avctx, &params);

        ret = nvenc_enqueue_timestamp(ctx->timestamps, frame->pts);
        if (ret < 0)
            return ret;
    } else {
        params.encodePicFlags = NV_ENC_PIC_FLAG_EOS;
    }

    ret = nv->nvEncEncodePicture(ctx->nvenc_ctx, &params);
    if (ret != NV_ENC_SUCCESS &&
        ret != NV_ENC_ERR_NEED_MORE_INPUT)
        return nvenc_print_error(avctx, ret, "Error encoding the frame");

    if (out) {
        ret = nvenc_enqueue_surface(ctx->pending, out);
        if (ret < 0)
            return ret;
    }

    if (ret != NV_ENC_ERR_NEED_MORE_INPUT &&
        av_fifo_size(ctx->pending)) {
        ret = nvenc_get_frame(avctx, pkt);
        if (ret < 0)
            return ret;
        *got_packet = 1;
    } else {
        *got_packet = 0;
    }

    return 0;
}
