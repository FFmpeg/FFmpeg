/*
 * NVIDIA NVENC Support
 * Copyright (C) 2015 Luca Barbato
 * Copyright (C) 2015 Philip Langdale <philipl@overt.org>
 * Copyright (C) 2014 Timo Rothenpieler <timo@rothenpieler.org>
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

#include <nvEncodeAPI.h>
#include <string.h>

#define CUDA_LIBNAME "libcuda.so"

#if HAVE_WINDOWS_H
#include <windows.h>

#if ARCH_X86_64
#define NVENC_LIBNAME "nvEncodeAPI64.dll"
#else
#define NVENC_LIBNAME "nvEncodeAPI.dll"
#endif

#define dlopen(filename, flags) LoadLibrary((filename))
#define dlsym(handle, symbol)   GetProcAddress(handle, symbol)
#define dlclose(handle)         FreeLibrary(handle)
#else
#include <dlfcn.h>
#define NVENC_LIBNAME "libnvidia-encode.so"
#endif

#include "libavutil/common.h"
#include "libavutil/hwcontext.h"
#include "libavutil/imgutils.h"
#include "libavutil/mem.h"
#include "avcodec.h"
#include "internal.h"
#include "nvenc.h"

#if CONFIG_CUDA
#include "libavutil/hwcontext_cuda.h"
#endif

#define NVENC_CAP 0x30
#define BITSTREAM_BUFFER_SIZE 1024 * 1024
#define IS_CBR(rc) (rc == NV_ENC_PARAMS_RC_CBR ||               \
                    rc == NV_ENC_PARAMS_RC_2_PASS_QUALITY ||    \
                    rc == NV_ENC_PARAMS_RC_2_PASS_FRAMESIZE_CAP)

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

const enum AVPixelFormat ff_nvenc_pix_fmts[] = {
    AV_PIX_FMT_NV12,
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUV444P,
#if NVENCAPI_MAJOR_VERSION >= 7
    AV_PIX_FMT_P010,
    AV_PIX_FMT_YUV444P16,
#endif
#if CONFIG_CUDA
    AV_PIX_FMT_CUDA,
#endif
    AV_PIX_FMT_NONE
};

#define IS_10BIT(pix_fmt)  (pix_fmt == AV_PIX_FMT_P010    || \
                            pix_fmt == AV_PIX_FMT_YUV444P16)

#define IS_YUV444(pix_fmt) (pix_fmt == AV_PIX_FMT_YUV444P || \
                            pix_fmt == AV_PIX_FMT_YUV444P16)

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
    { NV_ENC_ERR_LOCK_BUSY,                AVERROR(EBUSY),   "lock busy"                },
    { NV_ENC_ERR_NOT_ENOUGH_BUFFER,        AVERROR(ENOBUFS), "not enough buffer"        },
    { NV_ENC_ERR_INVALID_VERSION,          AVERROR(EINVAL),  "invalid version"          },
    { NV_ENC_ERR_MAP_FAILED,               AVERROR(EIO),     "map failed"               },
    /* this is error should always be treated specially, so this "mapping"
     * is for completeness only */
    { NV_ENC_ERR_NEED_MORE_INPUT,          AVERROR_UNKNOWN,  "need more input"          },
    { NV_ENC_ERR_ENCODER_BUSY,             AVERROR(EBUSY),   "encoder busy"             },
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

#if CONFIG_CUDA
    nvel->cu_init                      = cuInit;
    nvel->cu_device_get_count          = cuDeviceGetCount;
    nvel->cu_device_get                = cuDeviceGet;
    nvel->cu_device_get_name           = cuDeviceGetName;
    nvel->cu_device_compute_capability = cuDeviceComputeCapability;
    nvel->cu_ctx_create                = cuCtxCreate_v2;
    nvel->cu_ctx_pop_current           = cuCtxPopCurrent_v2;
    nvel->cu_ctx_push_current          = cuCtxPushCurrent_v2;
    nvel->cu_ctx_destroy               = cuCtxDestroy_v2;
#else
    LOAD_LIBRARY(nvel->cuda, CUDA_LIBNAME);

    LOAD_SYMBOL(nvel->cu_init, nvel->cuda, "cuInit");
    LOAD_SYMBOL(nvel->cu_device_get_count, nvel->cuda, "cuDeviceGetCount");
    LOAD_SYMBOL(nvel->cu_device_get, nvel->cuda, "cuDeviceGet");
    LOAD_SYMBOL(nvel->cu_device_get_name, nvel->cuda, "cuDeviceGetName");
    LOAD_SYMBOL(nvel->cu_device_compute_capability, nvel->cuda,
                "cuDeviceComputeCapability");
    LOAD_SYMBOL(nvel->cu_ctx_create, nvel->cuda, "cuCtxCreate_v2");
    LOAD_SYMBOL(nvel->cu_ctx_pop_current, nvel->cuda, "cuCtxPopCurrent_v2");
    LOAD_SYMBOL(nvel->cu_ctx_push_current, nvel->cuda, "cuCtxPushCurrent_v2");
    LOAD_SYMBOL(nvel->cu_ctx_destroy, nvel->cuda, "cuCtxDestroy_v2");
#endif

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
    NVENCContext *ctx = avctx->priv_data;
    int ret;

    ret = nvenc_check_codec_support(avctx);
    if (ret < 0) {
        av_log(avctx, AV_LOG_VERBOSE, "Codec not supported\n");
        return ret;
    }

    ret = nvenc_check_cap(avctx, NV_ENC_CAPS_SUPPORT_YUV444_ENCODE);
    if (ctx->data_pix_fmt == AV_PIX_FMT_YUV444P && ret <= 0) {
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
        av_log(avctx, AV_LOG_VERBOSE, "Max B-frames %d exceed %d\n",
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

    if (ctx->device != idx && ctx->device != ANY_DEVICE)
        return -1;

    ret = nvel->cu_ctx_create(&ctx->cu_context_internal, 0, cu_device);
    if (ret != CUDA_SUCCESS)
        goto fail;

    ctx->cu_context = ctx->cu_context_internal;

    ret = nvel->cu_ctx_pop_current(&dummy);
    if (ret != CUDA_SUCCESS)
        goto fail2;

    if ((ret = nvenc_open_session(avctx)) < 0)
        goto fail2;

    if ((ret = nvenc_check_capabilities(avctx)) < 0)
        goto fail3;

    av_log(avctx, loglevel, "supports NVENC\n");

    if (ctx->device == idx || ctx->device == ANY_DEVICE)
        return 0;

fail3:
    nvel->nvenc_funcs.nvEncDestroyEncoder(ctx->nvenc_ctx);
    ctx->nvenc_ctx = NULL;

fail2:
    nvel->cu_ctx_destroy(ctx->cu_context_internal);
    ctx->cu_context_internal = NULL;

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

    if (avctx->pix_fmt == AV_PIX_FMT_CUDA) {
#if CONFIG_CUDA
        AVHWFramesContext   *frames_ctx;
        AVCUDADeviceContext *device_hwctx;
        int ret;

        if (!avctx->hw_frames_ctx)
            return AVERROR(EINVAL);

        frames_ctx   = (AVHWFramesContext*)avctx->hw_frames_ctx->data;
        device_hwctx = frames_ctx->device_ctx->hwctx;

        ctx->cu_context = device_hwctx->cuda_ctx;

        ret = nvenc_open_session(avctx);
        if (ret < 0)
            return ret;

        ret = nvenc_check_capabilities(avctx);
        if (ret < 0)
            return ret;
#else
        return AVERROR_BUG;
#endif
    } else {
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


        for (i = 0; i < nb_devices; ++i) {
            if ((nvenc_check_device(avctx, i)) >= 0 && ctx->device != LIST_DEVICES)
                return 0;
        }

        if (ctx->device == LIST_DEVICES)
            return AVERROR_EXIT;

        return AVERROR(ENOSYS);
    }

    return 0;
}

typedef struct GUIDTuple {
    const GUID guid;
    int flags;
} GUIDTuple;

#define PRESET_ALIAS(alias, name, ...) \
    [PRESET_ ## alias] = { NV_ENC_PRESET_ ## name ## _GUID, __VA_ARGS__ }

#define PRESET(name, ...) PRESET_ALIAS(name, name, __VA_ARGS__)

static int nvenc_map_preset(NVENCContext *ctx)
{
    GUIDTuple presets[] = {
        PRESET(DEFAULT),
        PRESET(HP),
        PRESET(HQ),
        PRESET(BD),
        PRESET(LOW_LATENCY_DEFAULT, NVENC_LOWLATENCY),
        PRESET(LOW_LATENCY_HP,      NVENC_LOWLATENCY),
        PRESET(LOW_LATENCY_HQ,      NVENC_LOWLATENCY),
        PRESET(LOSSLESS_DEFAULT,    NVENC_LOSSLESS),
        PRESET(LOSSLESS_HP,         NVENC_LOSSLESS),
        PRESET_ALIAS(SLOW, HQ,      NVENC_TWO_PASSES),
        PRESET_ALIAS(MEDIUM, HQ,    NVENC_ONE_PASS),
        PRESET_ALIAS(FAST, HP,      NVENC_ONE_PASS)
    };

    GUIDTuple *t = &presets[ctx->preset];

    ctx->params.presetGUID = t->guid;
    ctx->flags             = t->flags;

    return AVERROR(EINVAL);
}

#undef PRESET
#undef PRESET_ALIAS

static void set_constqp(AVCodecContext *avctx, NV_ENC_RC_PARAMS *rc)
{
    NVENCContext *ctx = avctx->priv_data;
    rc->rateControlMode = NV_ENC_PARAMS_RC_CONSTQP;

    if (ctx->init_qp_p >= 0) {
        rc->constQP.qpInterP = ctx->init_qp_p;
        if (ctx->init_qp_i >= 0 && ctx->init_qp_b >= 0) {
            rc->constQP.qpIntra  = ctx->init_qp_i;
            rc->constQP.qpInterB = ctx->init_qp_b;
        } else if (avctx->i_quant_factor != 0.0 && avctx->b_quant_factor != 0.0) {
            rc->constQP.qpIntra  = av_clip(rc->constQP.qpInterP * fabs(avctx->i_quant_factor) + avctx->i_quant_offset + 0.5, 0, 51);
            rc->constQP.qpInterB = av_clip(rc->constQP.qpInterP * fabs(avctx->b_quant_factor) + avctx->b_quant_offset + 0.5, 0, 51);
        } else {
            rc->constQP.qpIntra  = rc->constQP.qpInterP;
            rc->constQP.qpInterB = rc->constQP.qpInterP;
        }
    } else if (avctx->global_quality >= 0) {
        rc->constQP.qpInterP = avctx->global_quality;
        rc->constQP.qpInterB = avctx->global_quality;
        rc->constQP.qpIntra  = avctx->global_quality;
    }
}

static void set_vbr(AVCodecContext *avctx, NV_ENC_RC_PARAMS *rc)
{
    NVENCContext *ctx    = avctx->priv_data;

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

    if (ctx->init_qp_p >= 0) {
        rc->enableInitialRCQP = 1;
        rc->initialRCQP.qpInterP = ctx->init_qp_p;
        if (ctx->init_qp_i < 0) {
            if (avctx->i_quant_factor != 0.0 && avctx->b_quant_factor != 0.0) {
                rc->initialRCQP.qpIntra = av_clip(rc->initialRCQP.qpInterP * fabs(avctx->i_quant_factor) + avctx->i_quant_offset + 0.5, 0, 51);
            } else {
                rc->initialRCQP.qpIntra = rc->initialRCQP.qpInterP;
            }
        } else {
            rc->initialRCQP.qpIntra = ctx->init_qp_i;
        }

        if (ctx->init_qp_b < 0) {
            if (avctx->i_quant_factor != 0.0 && avctx->b_quant_factor != 0.0) {
                rc->initialRCQP.qpInterB = av_clip(rc->initialRCQP.qpInterP * fabs(avctx->b_quant_factor) + avctx->b_quant_offset + 0.5, 0, 51);
            } else {
                rc->initialRCQP.qpInterB = rc->initialRCQP.qpInterP;
            }
        } else {
            rc->initialRCQP.qpInterB = ctx->init_qp_b;
        }
    }
}

static void set_lossless(AVCodecContext *avctx, NV_ENC_RC_PARAMS *rc)
{
    rc->rateControlMode  = NV_ENC_PARAMS_RC_CONSTQP;
    rc->constQP.qpInterB = 0;
    rc->constQP.qpInterP = 0;
    rc->constQP.qpIntra  = 0;
}

static void nvenc_override_rate_control(AVCodecContext *avctx,
                                        NV_ENC_RC_PARAMS *rc)
{
    NVENCContext *ctx    = avctx->priv_data;

    switch (ctx->rc) {
    case NV_ENC_PARAMS_RC_CONSTQP:
        set_constqp(avctx, rc);
        return;
    case NV_ENC_PARAMS_RC_2_PASS_VBR:
    case NV_ENC_PARAMS_RC_VBR:
        set_vbr(avctx, rc);
        break;
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
    } else if (ctx->flags & NVENC_LOSSLESS) {
        set_lossless(avctx, rc);
    } else if (avctx->global_quality > 0) {
        set_constqp(avctx, rc);
    } else {
        if (ctx->flags & NVENC_TWO_PASSES)
            rc->rateControlMode = NV_ENC_PARAMS_RC_2_PASS_VBR;
        else
            rc->rateControlMode = NV_ENC_PARAMS_RC_VBR;
        set_vbr(avctx, rc);
    }

    if (avctx->rc_buffer_size > 0)
        rc->vbvBufferSize = avctx->rc_buffer_size;

    if (rc->averageBitRate > 0)
        avctx->bit_rate = rc->averageBitRate;

#if NVENCAPI_MAJOR_VERSION >= 7
    if (ctx->aq) {
        ctx->config.rcParams.enableAQ   = 1;
        ctx->config.rcParams.aqStrength = ctx->aq_strength;
        av_log(avctx, AV_LOG_VERBOSE, "AQ enabled.\n");
    }

    if (ctx->temporal_aq) {
        ctx->config.rcParams.enableTemporalAQ = 1;
        av_log(avctx, AV_LOG_VERBOSE, "Temporal AQ enabled.\n");
    }

    if (ctx->rc_lookahead > 0) {
        int lkd_bound = FFMIN(ctx->nb_surfaces, ctx->async_depth) -
                        ctx->config.frameIntervalP - 4;

        if (lkd_bound < 0) {
            av_log(avctx, AV_LOG_WARNING,
                   "Lookahead not enabled. Increase buffer delay (-delay).\n");
        } else {
            ctx->config.rcParams.enableLookahead = 1;
            ctx->config.rcParams.lookaheadDepth  = av_clip(ctx->rc_lookahead, 0, lkd_bound);
            ctx->config.rcParams.disableIadapt   = ctx->no_scenecut;
            ctx->config.rcParams.disableBadapt   = !ctx->b_adapt;
            av_log(avctx, AV_LOG_VERBOSE,
                   "Lookahead enabled: depth %d, scenecut %s, B-adapt %s.\n",
                   ctx->config.rcParams.lookaheadDepth,
                   ctx->config.rcParams.disableIadapt ? "disabled" : "enabled",
                   ctx->config.rcParams.disableBadapt ? "disabled" : "enabled");
        }
    }

    if (ctx->strict_gop) {
        ctx->config.rcParams.strictGOPTarget = 1;
        av_log(avctx, AV_LOG_VERBOSE, "Strict GOP target enabled.\n");
    }

    if (ctx->nonref_p)
        ctx->config.rcParams.enableNonRefP = 1;

    if (ctx->zerolatency)
        ctx->config.rcParams.zeroReorderDelay = 1;

    if (ctx->quality)
        ctx->config.rcParams.targetQuality = ctx->quality;
#endif /* NVENCAPI_MAJOR_VERSION >= 7 */
}

static int nvenc_setup_h264_config(AVCodecContext *avctx)
{
    NVENCContext *ctx                      = avctx->priv_data;
    NV_ENC_CONFIG *cc                      = &ctx->config;
    NV_ENC_CONFIG_H264 *h264               = &cc->encodeCodecConfig.h264Config;
    NV_ENC_CONFIG_H264_VUI_PARAMETERS *vui = &h264->h264VUIParameters;

    vui->colourDescriptionPresentFlag = avctx->colorspace      != AVCOL_SPC_UNSPECIFIED ||
                                        avctx->color_primaries != AVCOL_PRI_UNSPECIFIED ||
                                        avctx->color_trc       != AVCOL_TRC_UNSPECIFIED;

    vui->colourMatrix            = avctx->colorspace;
    vui->colourPrimaries         = avctx->color_primaries;
    vui->transferCharacteristics = avctx->color_trc;

    vui->videoFullRangeFlag = avctx->color_range == AVCOL_RANGE_JPEG;

    vui->videoSignalTypePresentFlag = vui->colourDescriptionPresentFlag ||
                                      vui->videoFullRangeFlag;

    h264->disableSPSPPS = (avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER) ? 1 : 0;
    h264->repeatSPSPPS  = (avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER) ? 0 : 1;
    h264->outputAUD     = 1;

    h264->maxNumRefFrames = avctx->refs;
    h264->idrPeriod       = cc->gopLength;

    h264->sliceMode     = 3;
    h264->sliceModeData = FFMAX(avctx->slices, 1);

    if (ctx->flags & NVENC_LOSSLESS)
        h264->qpPrimeYZeroTransformBypassFlag = 1;

    if (IS_CBR(cc->rcParams.rateControlMode)) {
        h264->outputBufferingPeriodSEI = 1;
        h264->outputPictureTimingSEI   = 1;
    }

    if (ctx->profile)
        avctx->profile = ctx->profile;

    if (ctx->data_pix_fmt == AV_PIX_FMT_YUV444P)
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

    if (ctx->data_pix_fmt == AV_PIX_FMT_YUV444P) {
        cc->profileGUID = NV_ENC_H264_PROFILE_HIGH_444_GUID;
        avctx->profile = FF_PROFILE_H264_HIGH_444_PREDICTIVE;
    }

    h264->level = ctx->level;

    return 0;
}

static int nvenc_setup_hevc_config(AVCodecContext *avctx)
{
    NVENCContext *ctx                      = avctx->priv_data;
    NV_ENC_CONFIG *cc                      = &ctx->config;
    NV_ENC_CONFIG_HEVC *hevc               = &cc->encodeCodecConfig.hevcConfig;
    NV_ENC_CONFIG_HEVC_VUI_PARAMETERS *vui = &hevc->hevcVUIParameters;

    vui->colourDescriptionPresentFlag = avctx->colorspace      != AVCOL_SPC_UNSPECIFIED ||
                                        avctx->color_primaries != AVCOL_PRI_UNSPECIFIED ||
                                        avctx->color_trc       != AVCOL_TRC_UNSPECIFIED;

    vui->colourMatrix            = avctx->colorspace;
    vui->colourPrimaries         = avctx->color_primaries;
    vui->transferCharacteristics = avctx->color_trc;

    vui->videoFullRangeFlag = avctx->color_range == AVCOL_RANGE_JPEG;

    vui->videoSignalTypePresentFlag = vui->colourDescriptionPresentFlag ||
                                      vui->videoFullRangeFlag;

    hevc->disableSPSPPS = (avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER) ? 1 : 0;
    hevc->repeatSPSPPS  = (avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER) ? 0 : 1;
    hevc->outputAUD     = 1;

    hevc->maxNumRefFramesInDPB = avctx->refs;
    hevc->idrPeriod            = cc->gopLength;

    if (IS_CBR(cc->rcParams.rateControlMode)) {
        hevc->outputBufferingPeriodSEI = 1;
        hevc->outputPictureTimingSEI   = 1;
    }

    switch (ctx->profile) {
    case NV_ENC_HEVC_PROFILE_MAIN:
        cc->profileGUID = NV_ENC_HEVC_PROFILE_MAIN_GUID;
        avctx->profile  = FF_PROFILE_HEVC_MAIN;
        break;
#if NVENCAPI_MAJOR_VERSION >= 7
    case NV_ENC_HEVC_PROFILE_MAIN_10:
        cc->profileGUID = NV_ENC_HEVC_PROFILE_MAIN10_GUID;
        avctx->profile  = FF_PROFILE_HEVC_MAIN_10;
        break;
    case NV_ENC_HEVC_PROFILE_REXT:
        cc->profileGUID = NV_ENC_HEVC_PROFILE_FREXT_GUID;
        avctx->profile  = FF_PROFILE_HEVC_REXT;
        break;
#endif /* NVENCAPI_MAJOR_VERSION >= 7 */
    }

    // force setting profile for various input formats
    switch (ctx->data_pix_fmt) {
    case AV_PIX_FMT_YUV420P:
    case AV_PIX_FMT_NV12:
        cc->profileGUID = NV_ENC_HEVC_PROFILE_MAIN_GUID;
        avctx->profile  = FF_PROFILE_HEVC_MAIN;
        break;
#if NVENCAPI_MAJOR_VERSION >= 7
    case AV_PIX_FMT_P010:
        cc->profileGUID = NV_ENC_HEVC_PROFILE_MAIN10_GUID;
        avctx->profile  = FF_PROFILE_HEVC_MAIN_10;
        break;
    case AV_PIX_FMT_YUV444P:
    case AV_PIX_FMT_YUV444P16:
        cc->profileGUID = NV_ENC_HEVC_PROFILE_FREXT_GUID;
        avctx->profile  = FF_PROFILE_HEVC_REXT;
        break;
#endif /* NVENCAPI_MAJOR_VERSION >= 7 */
    }

#if NVENCAPI_MAJOR_VERSION >= 7
    hevc->chromaFormatIDC     = IS_YUV444(ctx->data_pix_fmt) ? 3 : 1;
    hevc->pixelBitDepthMinus8 = IS_10BIT(ctx->data_pix_fmt)  ? 2 : 0;
#endif /* NVENCAPI_MAJOR_VERSION >= 7 */

    hevc->sliceMode     = 3;
    hevc->sliceModeData = FFMAX(avctx->slices, 1);

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

static int nvenc_recalc_surfaces(AVCodecContext *avctx)
{
    NVENCContext *ctx = avctx->priv_data;
    // default minimum of 4 surfaces
    // multiply by 2 for number of NVENCs on gpu (hardcode to 2)
    // another multiply by 2 to avoid blocking next PBB group
    int nb_surfaces = FFMAX(4, ctx->config.frameIntervalP * 2 * 2);

    // lookahead enabled
    if (ctx->rc_lookahead > 0) {
        // +1 is to account for lkd_bound calculation later
        // +4 is to allow sufficient pipelining with lookahead
        nb_surfaces = FFMAX(1, FFMAX(nb_surfaces, ctx->rc_lookahead + ctx->config.frameIntervalP + 1 + 4));
        if (nb_surfaces > ctx->nb_surfaces && ctx->nb_surfaces > 0) {
            av_log(avctx, AV_LOG_WARNING,
                "Defined rc_lookahead requires more surfaces, "
                "increasing used surfaces %d -> %d\n",
                ctx->nb_surfaces, nb_surfaces);
        }
        ctx->nb_surfaces = FFMAX(nb_surfaces, ctx->nb_surfaces);
    } else {
        if (ctx->config.frameIntervalP > 1 &&
            ctx->nb_surfaces < nb_surfaces && ctx->nb_surfaces > 0) {
            av_log(avctx, AV_LOG_WARNING,
                "Defined b-frame requires more surfaces, "
                "increasing used surfaces %d -> %d\n",
                ctx->nb_surfaces, nb_surfaces);
            ctx->nb_surfaces = FFMAX(ctx->nb_surfaces, nb_surfaces);
        } else if (ctx->nb_surfaces <= 0)
            ctx->nb_surfaces = nb_surfaces;
        // otherwise use user specified value
    }

    ctx->nb_surfaces = FFMAX(1, FFMIN(MAX_REGISTERED_FRAMES, ctx->nb_surfaces));
    ctx->async_depth = FFMIN(ctx->async_depth, ctx->nb_surfaces - 1);
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

    // De-compensate for hardware, dubiously, trying to compensate for
    // playback at 704 pixel width.
    if (avctx->width == 720 && (avctx->height == 480 || avctx->height == 576)) {
        av_reduce(&ctx->params.darWidth, &ctx->params.darHeight,
                  ctx->params.darWidth * 44,
                  ctx->params.darHeight * 45,
                  1024 * 1024);
    }

    ctx->params.frameRateNum = avctx->time_base.den;
    ctx->params.frameRateDen = avctx->time_base.num * avctx->ticks_per_frame;

    ctx->params.enableEncodeAsync = 0;
    ctx->params.enablePTD         = 1;

    ctx->params.encodeConfig = &ctx->config;

    nvenc_map_preset(ctx);

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
            /* 0 is intra-only,
             * 1 is I/P only,
             * 2 is one B-Frame,
             * 3 two B-frames, and so on. */
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

    ctx->initial_pts[0] = AV_NOPTS_VALUE;
    ctx->initial_pts[1] = AV_NOPTS_VALUE;

    nvenc_recalc_surfaces(avctx);

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
        return nvenc_print_error(avctx, ret, "InitializeEncoder failed");

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
    NVENCFrame *tmp_surface         = &ctx->frames[idx];
    int ret;
    NV_ENC_CREATE_BITSTREAM_BUFFER out_buffer = { 0 };

    switch (ctx->data_pix_fmt) {
    case AV_PIX_FMT_YUV420P:
        ctx->frames[idx].format = NV_ENC_BUFFER_FORMAT_YV12_PL;
        break;
    case AV_PIX_FMT_NV12:
        ctx->frames[idx].format = NV_ENC_BUFFER_FORMAT_NV12_PL;
        break;
    case AV_PIX_FMT_YUV444P:
        ctx->frames[idx].format = NV_ENC_BUFFER_FORMAT_YUV444_PL;
        break;
#if NVENCAPI_MAJOR_VERSION >= 7
    case AV_PIX_FMT_P010:
        ctx->frames[idx].format = NV_ENC_BUFFER_FORMAT_YUV420_10BIT;
        break;
    case AV_PIX_FMT_YUV444P16:
        ctx->frames[idx].format = NV_ENC_BUFFER_FORMAT_YUV444_10BIT;
        break;
#endif /* NVENCAPI_MAJOR_VERSION >= 7 */
    default:
        return AVERROR_BUG;
    }

    if (avctx->pix_fmt == AV_PIX_FMT_CUDA) {
        ctx->frames[idx].in_ref = av_frame_alloc();
        if (!ctx->frames[idx].in_ref)
            return AVERROR(ENOMEM);
    } else {
        NV_ENC_CREATE_INPUT_BUFFER in_buffer      = { 0 };

        in_buffer.version  = NV_ENC_CREATE_INPUT_BUFFER_VER;

        in_buffer.width  = avctx->width;
        in_buffer.height = avctx->height;

        in_buffer.bufferFmt  = ctx->frames[idx].format;
        in_buffer.memoryHeap = NV_ENC_MEMORY_HEAP_SYSMEM_UNCACHED;

        ret = nv->nvEncCreateInputBuffer(ctx->nvenc_ctx, &in_buffer);
        if (ret != NV_ENC_SUCCESS)
            return nvenc_print_error(avctx, ret, "CreateInputBuffer failed");

        ctx->frames[idx].in     = in_buffer.inputBuffer;
    }

    out_buffer.version = NV_ENC_CREATE_BITSTREAM_BUFFER_VER;
    /* 1MB is large enough to hold most output frames.
     * NVENC increases this automatically if it is not enough. */
    out_buffer.size = BITSTREAM_BUFFER_SIZE;

    out_buffer.memoryHeap = NV_ENC_MEMORY_HEAP_SYSMEM_UNCACHED;

    ret = nv->nvEncCreateBitstreamBuffer(ctx->nvenc_ctx, &out_buffer);
    if (ret != NV_ENC_SUCCESS)
        return nvenc_print_error(avctx, ret, "CreateBitstreamBuffer failed");

    ctx->frames[idx].out  = out_buffer.bitstreamBuffer;

    av_fifo_generic_write(ctx->unused_surface_queue, &tmp_surface, sizeof(tmp_surface), NULL);

    return 0;
}

static int nvenc_setup_surfaces(AVCodecContext *avctx)
{
    NVENCContext *ctx = avctx->priv_data;
    int i, ret;

    ctx->frames = av_mallocz_array(ctx->nb_surfaces, sizeof(*ctx->frames));
    if (!ctx->frames)
        return AVERROR(ENOMEM);

    ctx->timestamps = av_fifo_alloc(ctx->nb_surfaces * sizeof(int64_t));
    if (!ctx->timestamps)
        return AVERROR(ENOMEM);
    ctx->unused_surface_queue = av_fifo_alloc(ctx->nb_surfaces * sizeof(NVENCFrame*));
    if (!ctx->unused_surface_queue)
        return AVERROR(ENOMEM);
    ctx->pending = av_fifo_alloc(ctx->nb_surfaces * sizeof(*ctx->frames));
    if (!ctx->pending)
        return AVERROR(ENOMEM);
    ctx->ready = av_fifo_alloc(ctx->nb_surfaces * sizeof(*ctx->frames));
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

    /* the encoder has to be flushed before it can be closed */
    if (ctx->nvenc_ctx) {
        NV_ENC_PIC_PARAMS params        = { .version        = NV_ENC_PIC_PARAMS_VER,
                                            .encodePicFlags = NV_ENC_PIC_FLAG_EOS };

        nv->nvEncEncodePicture(ctx->nvenc_ctx, &params);
    }

    av_fifo_free(ctx->timestamps);
    av_fifo_free(ctx->pending);
    av_fifo_free(ctx->ready);
    av_fifo_free(ctx->unused_surface_queue);

    if (ctx->frames) {
        for (i = 0; i < ctx->nb_surfaces; ++i) {
            if (avctx->pix_fmt != AV_PIX_FMT_CUDA) {
                nv->nvEncDestroyInputBuffer(ctx->nvenc_ctx, ctx->frames[i].in);
            } else if (ctx->frames[i].in) {
                nv->nvEncUnmapInputResource(ctx->nvenc_ctx, ctx->frames[i].in_map.mappedResource);
            }

            av_frame_free(&ctx->frames[i].in_ref);
            nv->nvEncDestroyBitstreamBuffer(ctx->nvenc_ctx, ctx->frames[i].out);
        }
    }
    for (i = 0; i < ctx->nb_registered_frames; i++) {
        if (ctx->registered_frames[i].regptr)
            nv->nvEncUnregisterResource(ctx->nvenc_ctx, ctx->registered_frames[i].regptr);
    }
    ctx->nb_registered_frames = 0;

    av_freep(&ctx->frames);

    if (ctx->nvenc_ctx)
        nv->nvEncDestroyEncoder(ctx->nvenc_ctx);

    if (ctx->cu_context_internal)
        ctx->nvel.cu_ctx_destroy(ctx->cu_context_internal);

    if (ctx->nvel.nvenc)
        dlclose(ctx->nvel.nvenc);

#if !CONFIG_CUDA
    if (ctx->nvel.cuda)
        dlclose(ctx->nvel.cuda);
#endif

    return 0;
}

av_cold int ff_nvenc_encode_init(AVCodecContext *avctx)
{
    NVENCContext *ctx = avctx->priv_data;
    int ret;

    if (avctx->pix_fmt == AV_PIX_FMT_CUDA) {
        AVHWFramesContext *frames_ctx;
        if (!avctx->hw_frames_ctx) {
            av_log(avctx, AV_LOG_ERROR,
                   "hw_frames_ctx must be set when using GPU frames as input\n");
            return AVERROR(EINVAL);
        }
        frames_ctx = (AVHWFramesContext*)avctx->hw_frames_ctx->data;
        ctx->data_pix_fmt = frames_ctx->sw_format;
    } else {
        ctx->data_pix_fmt = avctx->pix_fmt;
    }

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

static NVENCFrame *get_free_frame(NVENCContext *ctx)
{
    NVENCFrame *tmp_surf;

    if (!(av_fifo_size(ctx->unused_surface_queue) > 0))
        // queue empty
        return NULL;

    av_fifo_generic_read(ctx->unused_surface_queue, &tmp_surf, sizeof(tmp_surf), NULL);
    return tmp_surf;
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
    case AV_PIX_FMT_P010:
        av_image_copy_plane(buf, in->pitch,
                            frame->data[0], frame->linesize[0],
                            frame->width << 1, frame->height);
        buf += off;

        av_image_copy_plane(buf, in->pitch,
                            frame->data[1], frame->linesize[1],
                            frame->width << 1, frame->height >> 1);
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
    case AV_PIX_FMT_YUV444P16:
        av_image_copy_plane(buf, in->pitch,
                            frame->data[0], frame->linesize[0],
                            frame->width << 1, frame->height);
        buf += off;

        av_image_copy_plane(buf, in->pitch,
                            frame->data[1], frame->linesize[1],
                            frame->width << 1, frame->height);
        buf += off;

        av_image_copy_plane(buf, in->pitch,
                            frame->data[2], frame->linesize[2],
                            frame->width << 1, frame->height);
        break;
    default:
        return AVERROR_BUG;
    }

    return 0;
}

static int nvenc_find_free_reg_resource(AVCodecContext *avctx)
{
    NVENCContext               *ctx = avctx->priv_data;
    NV_ENCODE_API_FUNCTION_LIST *nv = &ctx->nvel.nvenc_funcs;
    int i;

    if (ctx->nb_registered_frames == FF_ARRAY_ELEMS(ctx->registered_frames)) {
        for (i = 0; i < ctx->nb_registered_frames; i++) {
            if (!ctx->registered_frames[i].mapped) {
                if (ctx->registered_frames[i].regptr) {
                    nv->nvEncUnregisterResource(ctx->nvenc_ctx,
                                                ctx->registered_frames[i].regptr);
                    ctx->registered_frames[i].regptr = NULL;
                }
                return i;
            }
        }
    } else {
        return ctx->nb_registered_frames++;
    }

    av_log(avctx, AV_LOG_ERROR, "Too many registered CUDA frames\n");
    return AVERROR(ENOMEM);
}

static int nvenc_register_frame(AVCodecContext *avctx, const AVFrame *frame)
{
    NVENCContext               *ctx = avctx->priv_data;
    NV_ENCODE_API_FUNCTION_LIST *nv = &ctx->nvel.nvenc_funcs;
    AVHWFramesContext   *frames_ctx = (AVHWFramesContext*)avctx->hw_frames_ctx->data;
    NV_ENC_REGISTER_RESOURCE reg;
    int i, idx, ret;

    for (i = 0; i < ctx->nb_registered_frames; i++) {
        if (ctx->registered_frames[i].ptr == (CUdeviceptr)frame->data[0])
            return i;
    }

    idx = nvenc_find_free_reg_resource(avctx);
    if (idx < 0)
        return idx;

    reg.version            = NV_ENC_REGISTER_RESOURCE_VER;
    reg.resourceType       = NV_ENC_INPUT_RESOURCE_TYPE_CUDADEVICEPTR;
    reg.width              = frames_ctx->width;
    reg.height             = frames_ctx->height;
    reg.bufferFormat       = ctx->frames[0].format;
    reg.pitch              = frame->linesize[0];
    reg.resourceToRegister = frame->data[0];

    ret = nv->nvEncRegisterResource(ctx->nvenc_ctx, &reg);
    if (ret != NV_ENC_SUCCESS) {
        nvenc_print_error(avctx, ret, "Error registering an input resource");
        return AVERROR_UNKNOWN;
    }

    ctx->registered_frames[idx].ptr    = (CUdeviceptr)frame->data[0];
    ctx->registered_frames[idx].regptr = reg.registeredResource;
    return idx;
}

static int nvenc_upload_frame(AVCodecContext *avctx, const AVFrame *frame,
                              NVENCFrame *nvenc_frame)
{
    NVENCContext *ctx               = avctx->priv_data;
    NV_ENCODE_API_FUNCTION_LIST *nv = &ctx->nvel.nvenc_funcs;
    int ret;

    if (avctx->pix_fmt == AV_PIX_FMT_CUDA) {
        int reg_idx;

        ret = nvenc_register_frame(avctx, frame);
        if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR, "Could not register an input CUDA frame\n");
            return ret;
        }
        reg_idx = ret;

        ret = av_frame_ref(nvenc_frame->in_ref, frame);
        if (ret < 0)
            return ret;

        nvenc_frame->in_map.version            = NV_ENC_MAP_INPUT_RESOURCE_VER;
        nvenc_frame->in_map.registeredResource = ctx->registered_frames[reg_idx].regptr;

        ret = nv->nvEncMapInputResource(ctx->nvenc_ctx, &nvenc_frame->in_map);
        if (ret != NV_ENC_SUCCESS) {
            av_frame_unref(nvenc_frame->in_ref);
            return nvenc_print_error(avctx, ret, "Error mapping an input resource");
        }

        ctx->registered_frames[reg_idx].mapped = 1;
        nvenc_frame->reg_idx                   = reg_idx;
        nvenc_frame->in                        = nvenc_frame->in_map.mappedResource;
    } else {
        NV_ENC_LOCK_INPUT_BUFFER params = { 0 };

        params.version     = NV_ENC_LOCK_INPUT_BUFFER_VER;
        params.inputBuffer = nvenc_frame->in;

        ret = nv->nvEncLockInputBuffer(ctx->nvenc_ctx, &params);
        if (ret != NV_ENC_SUCCESS)
            return nvenc_print_error(avctx, ret, "Cannot lock the buffer");

        ret = nvenc_copy_frame(&params, frame);
        if (ret < 0) {
            nv->nvEncUnlockInputBuffer(ctx->nvenc_ctx, nvenc_frame->in);
            return ret;
        }

        ret = nv->nvEncUnlockInputBuffer(ctx->nvenc_ctx, nvenc_frame->in);
        if (ret != NV_ENC_SUCCESS)
            return nvenc_print_error(avctx, ret, "Cannot unlock the buffer");
    }

    return 0;
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

static int nvenc_set_timestamp(AVCodecContext *avctx,
                               NV_ENC_LOCK_BITSTREAM *params,
                               AVPacket *pkt)
{
    NVENCContext *ctx = avctx->priv_data;

    pkt->pts      = params->outputTimeStamp;
    pkt->duration = params->outputDuration;

    /* generate the first dts by linearly extrapolating the
     * first two pts values to the past */
    if (avctx->max_b_frames > 0 && !ctx->first_packet_output &&
        ctx->initial_pts[1] != AV_NOPTS_VALUE) {
        int64_t ts0 = ctx->initial_pts[0], ts1 = ctx->initial_pts[1];
        int64_t delta;

        if ((ts0 < 0 && ts1 > INT64_MAX + ts0) ||
            (ts0 > 0 && ts1 < INT64_MIN + ts0))
            return AVERROR(ERANGE);
        delta = ts1 - ts0;

        if ((delta < 0 && ts0 > INT64_MAX + delta) ||
            (delta > 0 && ts0 < INT64_MIN + delta))
            return AVERROR(ERANGE);
        pkt->dts = ts0 - delta;

        ctx->first_packet_output = 1;
        return 0;
    }
    return nvenc_dequeue_timestamp(ctx->timestamps, &pkt->dts);
}

static int nvenc_get_output(AVCodecContext *avctx, AVPacket *pkt)
{
    NVENCContext *ctx               = avctx->priv_data;
    NV_ENCODE_API_FUNCTION_LIST *nv = &ctx->nvel.nvenc_funcs;
    NV_ENC_LOCK_BITSTREAM params    = { 0 };
    NVENCFrame *frame;
    int ret;

    ret = av_fifo_generic_read(ctx->ready, &frame, sizeof(frame), NULL);
    if (ret)
        return ret;

    params.version         = NV_ENC_LOCK_BITSTREAM_VER;
    params.outputBitstream = frame->out;

    ret = nv->nvEncLockBitstream(ctx->nvenc_ctx, &params);
    if (ret < 0)
        return nvenc_print_error(avctx, ret, "Cannot lock the bitstream");

    ret = ff_alloc_packet(pkt, params.bitstreamSizeInBytes);
    if (ret < 0)
        return ret;

    memcpy(pkt->data, params.bitstreamBufferPtr, pkt->size);

    ret = nv->nvEncUnlockBitstream(ctx->nvenc_ctx, frame->out);
    if (ret < 0)
        return nvenc_print_error(avctx, ret, "Cannot unlock the bitstream");

    if (avctx->pix_fmt == AV_PIX_FMT_CUDA) {
        nv->nvEncUnmapInputResource(ctx->nvenc_ctx, frame->in_map.mappedResource);
        av_frame_unref(frame->in_ref);
        ctx->registered_frames[frame->reg_idx].mapped = 0;

        frame->in = NULL;
    }

    av_fifo_generic_write(ctx->unused_surface_queue, &frame, sizeof(frame), NULL);

    ret = nvenc_set_timestamp(avctx, &params, pkt);
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

static int output_ready(AVCodecContext *avctx, int flush)
{
    NVENCContext *ctx = avctx->priv_data;
    int nb_ready, nb_pending;

    /* when B-frames are enabled, we wait for two initial timestamps to
     * calculate the first dts */
    if (!flush && avctx->max_b_frames > 0 &&
        (ctx->initial_pts[0] == AV_NOPTS_VALUE || ctx->initial_pts[1] == AV_NOPTS_VALUE))
        return 0;

    nb_ready   = av_fifo_size(ctx->ready)   / sizeof(NVENCFrame*);
    nb_pending = av_fifo_size(ctx->pending) / sizeof(NVENCFrame*);
    if (flush)
        return nb_ready > 0;
    return (nb_ready > 0) && (nb_ready + nb_pending >= ctx->async_depth);
}

int ff_nvenc_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                          const AVFrame *frame, int *got_packet)
{
    NVENCContext *ctx               = avctx->priv_data;
    NVENCLibraryContext *nvel       = &ctx->nvel;
    NV_ENCODE_API_FUNCTION_LIST *nv = &ctx->nvel.nvenc_funcs;
    NV_ENC_PIC_PARAMS params        = { 0 };
    NVENCFrame         *nvenc_frame = NULL;
    CUcontext dummy;
    int enc_ret, ret;

    params.version = NV_ENC_PIC_PARAMS_VER;

    if (frame) {
        nvenc_frame = get_free_frame(ctx);
        if (!nvenc_frame) {
            av_log(avctx, AV_LOG_ERROR, "No free surfaces\n");
            return AVERROR_BUG;
        }

        ret = nvenc_upload_frame(avctx, frame, nvenc_frame);
        if (ret < 0)
            return ret;

        params.inputBuffer     = nvenc_frame->in;
        params.bufferFmt       = nvenc_frame->format;
        params.inputWidth      = frame->width;
        params.inputHeight     = frame->height;
        params.outputBitstream = nvenc_frame->out;
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

        if (ctx->initial_pts[0] == AV_NOPTS_VALUE)
            ctx->initial_pts[0] = frame->pts;
        else if (ctx->initial_pts[1] == AV_NOPTS_VALUE)
            ctx->initial_pts[1] = frame->pts;
    } else {
        params.encodePicFlags = NV_ENC_PIC_FLAG_EOS;
    }

    nvel->cu_ctx_push_current(ctx->cu_context);
    enc_ret = nv->nvEncEncodePicture(ctx->nvenc_ctx, &params);
    nvel->cu_ctx_pop_current(&dummy);

    if (enc_ret != NV_ENC_SUCCESS &&
        enc_ret != NV_ENC_ERR_NEED_MORE_INPUT)
        return nvenc_print_error(avctx, enc_ret, "Error encoding the frame");

    if (nvenc_frame) {
        ret = av_fifo_generic_write(ctx->pending, &nvenc_frame, sizeof(nvenc_frame), NULL);
        if (ret < 0)
            return ret;
    }

    /* all the pending buffers are now ready for output */
    if (enc_ret == NV_ENC_SUCCESS) {
        while (av_fifo_size(ctx->pending) > 0) {
            av_fifo_generic_read(ctx->pending, &nvenc_frame, sizeof(nvenc_frame), NULL);
            av_fifo_generic_write(ctx->ready,  &nvenc_frame, sizeof(nvenc_frame), NULL);
        }
    }

    if (output_ready(avctx, !frame)) {
        ret = nvenc_get_output(avctx, pkt);
        if (ret < 0)
            return ret;
        *got_packet = 1;
    } else {
        *got_packet = 0;
    }

    return 0;
}
