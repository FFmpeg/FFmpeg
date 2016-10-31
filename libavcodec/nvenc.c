/*
 * H.264/HEVC hardware encoding using nvidia nvenc
 * Copyright (c) 2016 Timo Rothenpieler <timo@rothenpieler.org>
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

#include "config.h"

#if defined(_WIN32) || defined(__CYGWIN__)
# define CUDA_LIBNAME "nvcuda.dll"
# if ARCH_X86_64
#  define NVENC_LIBNAME "nvEncodeAPI64.dll"
# else
#  define NVENC_LIBNAME "nvEncodeAPI.dll"
# endif
#else
# define CUDA_LIBNAME "libcuda.so.1"
# define NVENC_LIBNAME "libnvidia-encode.so.1"
#endif

#if defined(_WIN32)
#include <windows.h>

#define dlopen(filename, flags) LoadLibrary(TEXT(filename))
#define dlsym(handle, symbol)   GetProcAddress(handle, symbol)
#define dlclose(handle)         FreeLibrary(handle)
#else
#include <dlfcn.h>
#endif

#include "libavutil/hwcontext.h"
#include "libavutil/imgutils.h"
#include "libavutil/avassert.h"
#include "libavutil/mem.h"
#include "internal.h"
#include "nvenc.h"

#define NVENC_CAP 0x30
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
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_NV12,
    AV_PIX_FMT_P010,
    AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_YUV444P16,
    AV_PIX_FMT_0RGB32,
    AV_PIX_FMT_0BGR32,
#if CONFIG_CUDA
    AV_PIX_FMT_CUDA,
#endif
    AV_PIX_FMT_NONE
};

#define IS_10BIT(pix_fmt) (pix_fmt == AV_PIX_FMT_P010 ||    \
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
    { NV_ENC_ERR_LOCK_BUSY,                AVERROR(EAGAIN),  "lock busy"                },
    { NV_ENC_ERR_NOT_ENOUGH_BUFFER,        AVERROR_BUFFER_TOO_SMALL, "not enough buffer"},
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
    NvencContext *ctx = avctx->priv_data;
    NvencDynLoadFunctions *dl_fn = &ctx->nvenc_dload_funcs;
    PNVENCODEAPIGETMAXSUPPORTEDVERSION nvenc_get_max_ver;
    PNVENCODEAPICREATEINSTANCE nvenc_create_instance;
    NVENCSTATUS err;
    uint32_t nvenc_max_ver;

#if CONFIG_CUDA
    dl_fn->cu_init                      = cuInit;
    dl_fn->cu_device_get_count          = cuDeviceGetCount;
    dl_fn->cu_device_get                = cuDeviceGet;
    dl_fn->cu_device_get_name           = cuDeviceGetName;
    dl_fn->cu_device_compute_capability = cuDeviceComputeCapability;
    dl_fn->cu_ctx_create                = cuCtxCreate_v2;
    dl_fn->cu_ctx_pop_current           = cuCtxPopCurrent_v2;
    dl_fn->cu_ctx_destroy               = cuCtxDestroy_v2;
#else
    LOAD_LIBRARY(dl_fn->cuda, CUDA_LIBNAME);

    LOAD_SYMBOL(dl_fn->cu_init, dl_fn->cuda, "cuInit");
    LOAD_SYMBOL(dl_fn->cu_device_get_count, dl_fn->cuda, "cuDeviceGetCount");
    LOAD_SYMBOL(dl_fn->cu_device_get, dl_fn->cuda, "cuDeviceGet");
    LOAD_SYMBOL(dl_fn->cu_device_get_name, dl_fn->cuda, "cuDeviceGetName");
    LOAD_SYMBOL(dl_fn->cu_device_compute_capability, dl_fn->cuda,
                "cuDeviceComputeCapability");
    LOAD_SYMBOL(dl_fn->cu_ctx_create, dl_fn->cuda, "cuCtxCreate_v2");
    LOAD_SYMBOL(dl_fn->cu_ctx_pop_current, dl_fn->cuda, "cuCtxPopCurrent_v2");
    LOAD_SYMBOL(dl_fn->cu_ctx_destroy, dl_fn->cuda, "cuCtxDestroy_v2");
#endif

    LOAD_LIBRARY(dl_fn->nvenc, NVENC_LIBNAME);

    LOAD_SYMBOL(nvenc_get_max_ver, dl_fn->nvenc,
                "NvEncodeAPIGetMaxSupportedVersion");
    LOAD_SYMBOL(nvenc_create_instance, dl_fn->nvenc,
                "NvEncodeAPICreateInstance");

    err = nvenc_get_max_ver(&nvenc_max_ver);
    if (err != NV_ENC_SUCCESS)
        return nvenc_print_error(avctx, err, "Failed to query nvenc max version");

    av_log(avctx, AV_LOG_VERBOSE, "Loaded Nvenc version %d.%d\n", nvenc_max_ver >> 4, nvenc_max_ver & 0xf);

    if ((NVENCAPI_MAJOR_VERSION << 4 | NVENCAPI_MINOR_VERSION) > nvenc_max_ver) {
        av_log(avctx, AV_LOG_ERROR, "Driver does not support the required nvenc API version. "
               "Required: %d.%d Found: %d.%d\n",
               NVENCAPI_MAJOR_VERSION, NVENCAPI_MINOR_VERSION,
               nvenc_max_ver >> 4, nvenc_max_ver & 0xf);
        return AVERROR(ENOSYS);
    }

    dl_fn->nvenc_funcs.version = NV_ENCODE_API_FUNCTION_LIST_VER;

    err = nvenc_create_instance(&dl_fn->nvenc_funcs);
    if (err != NV_ENC_SUCCESS)
        return nvenc_print_error(avctx, err, "Failed to create nvenc instance");

    av_log(avctx, AV_LOG_VERBOSE, "Nvenc initialized successfully\n");

    return 0;
}

static av_cold int nvenc_open_session(AVCodecContext *avctx)
{
    NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS params = { 0 };
    NvencContext *ctx = avctx->priv_data;
    NV_ENCODE_API_FUNCTION_LIST *p_nvenc = &ctx->nvenc_dload_funcs.nvenc_funcs;
    NVENCSTATUS ret;

    params.version    = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
    params.apiVersion = NVENCAPI_VERSION;
    params.device     = ctx->cu_context;
    params.deviceType = NV_ENC_DEVICE_TYPE_CUDA;

    ret = p_nvenc->nvEncOpenEncodeSessionEx(&params, &ctx->nvencoder);
    if (ret != NV_ENC_SUCCESS) {
        ctx->nvencoder = NULL;
        return nvenc_print_error(avctx, ret, "OpenEncodeSessionEx failed");
    }

    return 0;
}

static int nvenc_check_codec_support(AVCodecContext *avctx)
{
    NvencContext *ctx = avctx->priv_data;
    NV_ENCODE_API_FUNCTION_LIST *p_nvenc = &ctx->nvenc_dload_funcs.nvenc_funcs;
    int i, ret, count = 0;
    GUID *guids = NULL;

    ret = p_nvenc->nvEncGetEncodeGUIDCount(ctx->nvencoder, &count);

    if (ret != NV_ENC_SUCCESS || !count)
        return AVERROR(ENOSYS);

    guids = av_malloc(count * sizeof(GUID));
    if (!guids)
        return AVERROR(ENOMEM);

    ret = p_nvenc->nvEncGetEncodeGUIDs(ctx->nvencoder, guids, count, &count);
    if (ret != NV_ENC_SUCCESS) {
        ret = AVERROR(ENOSYS);
        goto fail;
    }

    ret = AVERROR(ENOSYS);
    for (i = 0; i < count; i++) {
        if (!memcmp(&guids[i], &ctx->init_encode_params.encodeGUID, sizeof(*guids))) {
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
    NvencContext *ctx = avctx->priv_data;
    NV_ENCODE_API_FUNCTION_LIST *p_nvenc = &ctx->nvenc_dload_funcs.nvenc_funcs;
    NV_ENC_CAPS_PARAM params        = { 0 };
    int ret, val = 0;

    params.version     = NV_ENC_CAPS_PARAM_VER;
    params.capsToQuery = cap;

    ret = p_nvenc->nvEncGetEncodeCaps(ctx->nvencoder, ctx->init_encode_params.encodeGUID, &params, &val);

    if (ret == NV_ENC_SUCCESS)
        return val;
    return 0;
}

static int nvenc_check_capabilities(AVCodecContext *avctx)
{
    NvencContext *ctx = avctx->priv_data;
    int ret;

    ret = nvenc_check_codec_support(avctx);
    if (ret < 0) {
        av_log(avctx, AV_LOG_VERBOSE, "Codec not supported\n");
        return ret;
    }

    ret = nvenc_check_cap(avctx, NV_ENC_CAPS_SUPPORT_YUV444_ENCODE);
    if (IS_YUV444(ctx->data_pix_fmt) && ret <= 0) {
        av_log(avctx, AV_LOG_VERBOSE, "YUV444P not supported\n");
        return AVERROR(ENOSYS);
    }

    ret = nvenc_check_cap(avctx, NV_ENC_CAPS_SUPPORT_LOSSLESS_ENCODE);
    if (ctx->preset >= PRESET_LOSSLESS_DEFAULT && ret <= 0) {
        av_log(avctx, AV_LOG_VERBOSE, "Lossless encoding not supported\n");
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

    ret = nvenc_check_cap(avctx, NV_ENC_CAPS_SUPPORT_FIELD_ENCODING);
    if (ret < 1 && avctx->flags & AV_CODEC_FLAG_INTERLACED_DCT) {
        av_log(avctx, AV_LOG_VERBOSE,
               "Interlaced encoding is not supported. Supported level: %d\n",
               ret);
        return AVERROR(ENOSYS);
    }

    ret = nvenc_check_cap(avctx, NV_ENC_CAPS_SUPPORT_10BIT_ENCODE);
    if (IS_10BIT(ctx->data_pix_fmt) && ret <= 0) {
        av_log(avctx, AV_LOG_VERBOSE, "10 bit encode not supported\n");
        return AVERROR(ENOSYS);
    }

    ret = nvenc_check_cap(avctx, NV_ENC_CAPS_SUPPORT_LOOKAHEAD);
    if (ctx->rc_lookahead > 0 && ret <= 0) {
        av_log(avctx, AV_LOG_VERBOSE, "RC lookahead not supported\n");
        return AVERROR(ENOSYS);
    }

    ret = nvenc_check_cap(avctx, NV_ENC_CAPS_SUPPORT_TEMPORAL_AQ);
    if (ctx->temporal_aq > 0 && ret <= 0) {
        av_log(avctx, AV_LOG_VERBOSE, "Temporal AQ not supported\n");
        return AVERROR(ENOSYS);
    }

    return 0;
}

static av_cold int nvenc_check_device(AVCodecContext *avctx, int idx)
{
    NvencContext *ctx = avctx->priv_data;
    NvencDynLoadFunctions *dl_fn = &ctx->nvenc_dload_funcs;
    NV_ENCODE_API_FUNCTION_LIST *p_nvenc = &dl_fn->nvenc_funcs;
    char name[128] = { 0};
    int major, minor, ret;
    CUresult cu_res;
    CUdevice cu_device;
    CUcontext dummy;
    int loglevel = AV_LOG_VERBOSE;

    if (ctx->device == LIST_DEVICES)
        loglevel = AV_LOG_INFO;

    cu_res = dl_fn->cu_device_get(&cu_device, idx);
    if (cu_res != CUDA_SUCCESS) {
        av_log(avctx, AV_LOG_ERROR,
               "Cannot access the CUDA device %d\n",
               idx);
        return -1;
    }

    cu_res = dl_fn->cu_device_get_name(name, sizeof(name), cu_device);
    if (cu_res != CUDA_SUCCESS)
        return -1;

    cu_res = dl_fn->cu_device_compute_capability(&major, &minor, cu_device);
    if (cu_res != CUDA_SUCCESS)
        return -1;

    av_log(avctx, loglevel, "[ GPU #%d - < %s > has Compute SM %d.%d ]\n", idx, name, major, minor);
    if (((major << 4) | minor) < NVENC_CAP) {
        av_log(avctx, loglevel, "does not support NVENC\n");
        goto fail;
    }

    cu_res = dl_fn->cu_ctx_create(&ctx->cu_context_internal, 0, cu_device);
    if (cu_res != CUDA_SUCCESS) {
        av_log(avctx, AV_LOG_FATAL, "Failed creating CUDA context for NVENC: 0x%x\n", (int)cu_res);
        goto fail;
    }

    ctx->cu_context = ctx->cu_context_internal;

    cu_res = dl_fn->cu_ctx_pop_current(&dummy);
    if (cu_res != CUDA_SUCCESS) {
        av_log(avctx, AV_LOG_FATAL, "Failed popping CUDA context: 0x%x\n", (int)cu_res);
        goto fail2;
    }

    if ((ret = nvenc_open_session(avctx)) < 0)
        goto fail2;

    if ((ret = nvenc_check_capabilities(avctx)) < 0)
        goto fail3;

    av_log(avctx, loglevel, "supports NVENC\n");

    dl_fn->nvenc_device_count++;

    if (ctx->device == dl_fn->nvenc_device_count - 1 || ctx->device == ANY_DEVICE)
        return 0;

fail3:
    p_nvenc->nvEncDestroyEncoder(ctx->nvencoder);
    ctx->nvencoder = NULL;

fail2:
    dl_fn->cu_ctx_destroy(ctx->cu_context_internal);
    ctx->cu_context_internal = NULL;

fail:
    return AVERROR(ENOSYS);
}

static av_cold int nvenc_setup_device(AVCodecContext *avctx)
{
    NvencContext *ctx = avctx->priv_data;
    NvencDynLoadFunctions *dl_fn = &ctx->nvenc_dload_funcs;

    switch (avctx->codec->id) {
    case AV_CODEC_ID_H264:
        ctx->init_encode_params.encodeGUID = NV_ENC_CODEC_H264_GUID;
        break;
    case AV_CODEC_ID_HEVC:
        ctx->init_encode_params.encodeGUID = NV_ENC_CODEC_HEVC_GUID;
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
        if (ret < 0) {
            av_log(avctx, AV_LOG_FATAL, "Provided device doesn't support required NVENC features\n");
            return ret;
        }
#else
        return AVERROR_BUG;
#endif
    } else {
        int i, nb_devices = 0;

        if ((dl_fn->cu_init(0)) != CUDA_SUCCESS) {
            av_log(avctx, AV_LOG_ERROR,
                   "Cannot init CUDA\n");
            return AVERROR_UNKNOWN;
        }

        if ((dl_fn->cu_device_get_count(&nb_devices)) != CUDA_SUCCESS) {
            av_log(avctx, AV_LOG_ERROR,
                   "Cannot enumerate the CUDA devices\n");
            return AVERROR_UNKNOWN;
        }

        if (!nb_devices) {
            av_log(avctx, AV_LOG_FATAL, "No CUDA capable devices found\n");
                return AVERROR_EXTERNAL;
        }

        av_log(avctx, AV_LOG_VERBOSE, "%d CUDA capable devices found\n", nb_devices);

        dl_fn->nvenc_device_count = 0;
        for (i = 0; i < nb_devices; ++i) {
            if ((nvenc_check_device(avctx, i)) >= 0 && ctx->device != LIST_DEVICES)
                return 0;
        }

        if (ctx->device == LIST_DEVICES)
            return AVERROR_EXIT;

        if (!dl_fn->nvenc_device_count) {
            av_log(avctx, AV_LOG_FATAL, "No NVENC capable devices found\n");
            return AVERROR_EXTERNAL;
        }

        av_log(avctx, AV_LOG_FATAL, "Requested GPU %d, but only %d GPUs are available!\n", ctx->device, dl_fn->nvenc_device_count);
        return AVERROR(EINVAL);
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

static void nvenc_map_preset(NvencContext *ctx)
{
    GUIDTuple presets[] = {
        PRESET(DEFAULT),
        PRESET(HP),
        PRESET(HQ),
        PRESET(BD),
        PRESET_ALIAS(SLOW,   HQ,    NVENC_TWO_PASSES),
        PRESET_ALIAS(MEDIUM, HQ,    NVENC_ONE_PASS),
        PRESET_ALIAS(FAST,   HP,    NVENC_ONE_PASS),
        PRESET(LOW_LATENCY_DEFAULT, NVENC_LOWLATENCY),
        PRESET(LOW_LATENCY_HP,      NVENC_LOWLATENCY),
        PRESET(LOW_LATENCY_HQ,      NVENC_LOWLATENCY),
        PRESET(LOSSLESS_DEFAULT,    NVENC_LOSSLESS),
        PRESET(LOSSLESS_HP,         NVENC_LOSSLESS),
    };

    GUIDTuple *t = &presets[ctx->preset];

    ctx->init_encode_params.presetGUID = t->guid;
    ctx->flags = t->flags;
}

#undef PRESET
#undef PRESET_ALIAS

static av_cold void set_constqp(AVCodecContext *avctx)
{
    NvencContext *ctx = avctx->priv_data;
    NV_ENC_RC_PARAMS *rc = &ctx->encode_config.rcParams;

    rc->rateControlMode = NV_ENC_PARAMS_RC_CONSTQP;
    rc->constQP.qpInterB = avctx->global_quality;
    rc->constQP.qpInterP = avctx->global_quality;
    rc->constQP.qpIntra = avctx->global_quality;

    avctx->qmin = -1;
    avctx->qmax = -1;
}

static av_cold void set_vbr(AVCodecContext *avctx)
{
    NvencContext *ctx = avctx->priv_data;
    NV_ENC_RC_PARAMS *rc = &ctx->encode_config.rcParams;
    int qp_inter_p;

    if (avctx->qmin >= 0 && avctx->qmax >= 0) {
        rc->enableMinQP = 1;
        rc->enableMaxQP = 1;

        rc->minQP.qpInterB = avctx->qmin;
        rc->minQP.qpInterP = avctx->qmin;
        rc->minQP.qpIntra = avctx->qmin;

        rc->maxQP.qpInterB = avctx->qmax;
        rc->maxQP.qpInterP = avctx->qmax;
        rc->maxQP.qpIntra = avctx->qmax;

        qp_inter_p = (avctx->qmax + 3 * avctx->qmin) / 4; // biased towards Qmin
    } else if (avctx->qmin >= 0) {
        rc->enableMinQP = 1;

        rc->minQP.qpInterB = avctx->qmin;
        rc->minQP.qpInterP = avctx->qmin;
        rc->minQP.qpIntra = avctx->qmin;

        qp_inter_p = avctx->qmin;
    } else {
        qp_inter_p = 26; // default to 26
    }

    rc->enableInitialRCQP = 1;
    rc->initialRCQP.qpInterP  = qp_inter_p;

    if (avctx->i_quant_factor != 0.0 && avctx->b_quant_factor != 0.0) {
        rc->initialRCQP.qpIntra = av_clip(
            qp_inter_p * fabs(avctx->i_quant_factor) + avctx->i_quant_offset, 0, 51);
        rc->initialRCQP.qpInterB = av_clip(
            qp_inter_p * fabs(avctx->b_quant_factor) + avctx->b_quant_offset, 0, 51);
    } else {
        rc->initialRCQP.qpIntra = qp_inter_p;
        rc->initialRCQP.qpInterB = qp_inter_p;
    }
}

static av_cold void set_lossless(AVCodecContext *avctx)
{
    NvencContext *ctx = avctx->priv_data;
    NV_ENC_RC_PARAMS *rc = &ctx->encode_config.rcParams;

    rc->rateControlMode = NV_ENC_PARAMS_RC_CONSTQP;
    rc->constQP.qpInterB = 0;
    rc->constQP.qpInterP = 0;
    rc->constQP.qpIntra = 0;

    avctx->qmin = -1;
    avctx->qmax = -1;
}

static void nvenc_override_rate_control(AVCodecContext *avctx)
{
    NvencContext *ctx    = avctx->priv_data;
    NV_ENC_RC_PARAMS *rc = &ctx->encode_config.rcParams;

    switch (ctx->rc) {
    case NV_ENC_PARAMS_RC_CONSTQP:
        if (avctx->global_quality <= 0) {
            av_log(avctx, AV_LOG_WARNING,
                   "The constant quality rate-control requires "
                   "the 'global_quality' option set.\n");
            return;
        }
        set_constqp(avctx);
        return;
    case NV_ENC_PARAMS_RC_2_PASS_VBR:
    case NV_ENC_PARAMS_RC_VBR:
        if (avctx->qmin < 0 && avctx->qmax < 0) {
            av_log(avctx, AV_LOG_WARNING,
                   "The variable bitrate rate-control requires "
                   "the 'qmin' and/or 'qmax' option set.\n");
            set_vbr(avctx);
            return;
        }
    case NV_ENC_PARAMS_RC_VBR_MINQP:
        if (avctx->qmin < 0) {
            av_log(avctx, AV_LOG_WARNING,
                   "The variable bitrate rate-control requires "
                   "the 'qmin' option set.\n");
            set_vbr(avctx);
            return;
        }
        set_vbr(avctx);
        break;
    case NV_ENC_PARAMS_RC_CBR:
    case NV_ENC_PARAMS_RC_2_PASS_QUALITY:
    case NV_ENC_PARAMS_RC_2_PASS_FRAMESIZE_CAP:
        break;
    }

    rc->rateControlMode = ctx->rc;
}

static av_cold void nvenc_setup_rate_control(AVCodecContext *avctx)
{
    NvencContext *ctx = avctx->priv_data;

    if (avctx->bit_rate > 0) {
        ctx->encode_config.rcParams.averageBitRate = avctx->bit_rate;
    } else if (ctx->encode_config.rcParams.averageBitRate > 0) {
        ctx->encode_config.rcParams.maxBitRate = ctx->encode_config.rcParams.averageBitRate;
    }

    if (avctx->rc_max_rate > 0)
        ctx->encode_config.rcParams.maxBitRate = avctx->rc_max_rate;

    if (ctx->rc < 0) {
        if (ctx->flags & NVENC_ONE_PASS)
            ctx->twopass = 0;
        if (ctx->flags & NVENC_TWO_PASSES)
            ctx->twopass = 1;

        if (ctx->twopass < 0)
            ctx->twopass = (ctx->flags & NVENC_LOWLATENCY) != 0;

        if (ctx->cbr) {
            if (ctx->twopass) {
                ctx->rc = NV_ENC_PARAMS_RC_2_PASS_QUALITY;
            } else {
                ctx->rc = NV_ENC_PARAMS_RC_CBR;
            }
        } else if (avctx->global_quality > 0) {
            ctx->rc = NV_ENC_PARAMS_RC_CONSTQP;
        } else if (ctx->twopass) {
            ctx->rc = NV_ENC_PARAMS_RC_2_PASS_VBR;
        } else if (avctx->qmin >= 0 && avctx->qmax >= 0) {
            ctx->rc = NV_ENC_PARAMS_RC_VBR_MINQP;
        }
    }

    if (ctx->flags & NVENC_LOSSLESS) {
        set_lossless(avctx);
    } else if (ctx->rc >= 0) {
        nvenc_override_rate_control(avctx);
    } else {
        ctx->encode_config.rcParams.rateControlMode = NV_ENC_PARAMS_RC_VBR;
        set_vbr(avctx);
    }

    if (avctx->rc_buffer_size > 0) {
        ctx->encode_config.rcParams.vbvBufferSize = avctx->rc_buffer_size;
    } else if (ctx->encode_config.rcParams.averageBitRate > 0) {
        ctx->encode_config.rcParams.vbvBufferSize = 2 * ctx->encode_config.rcParams.averageBitRate;
    }

    if (ctx->aq) {
        ctx->encode_config.rcParams.enableAQ   = 1;
        ctx->encode_config.rcParams.aqStrength = ctx->aq_strength;
        av_log(avctx, AV_LOG_VERBOSE, "AQ enabled.\n");
    }

    if (ctx->temporal_aq) {
        ctx->encode_config.rcParams.enableTemporalAQ = 1;
        av_log(avctx, AV_LOG_VERBOSE, "Temporal AQ enabled.\n");
    }

    if (ctx->rc_lookahead) {
        int lkd_bound = FFMIN(ctx->nb_surfaces, ctx->async_depth) -
                        ctx->encode_config.frameIntervalP - 4;

        if (lkd_bound < 0) {
            av_log(avctx, AV_LOG_WARNING,
                   "Lookahead not enabled. Increase buffer delay (-delay).\n");
        } else {
            ctx->encode_config.rcParams.enableLookahead = 1;
            ctx->encode_config.rcParams.lookaheadDepth  = av_clip(ctx->rc_lookahead, 0, lkd_bound);
            ctx->encode_config.rcParams.disableIadapt   = ctx->no_scenecut;
            ctx->encode_config.rcParams.disableBadapt   = !ctx->b_adapt;
            av_log(avctx, AV_LOG_VERBOSE,
                   "Lookahead enabled: depth %d, scenecut %s, B-adapt %s.\n",
                   ctx->encode_config.rcParams.lookaheadDepth,
                   ctx->encode_config.rcParams.disableIadapt ? "disabled" : "enabled",
                   ctx->encode_config.rcParams.disableBadapt ? "disabled" : "enabled");
        }
    }

    if (ctx->strict_gop) {
        ctx->encode_config.rcParams.strictGOPTarget = 1;
        av_log(avctx, AV_LOG_VERBOSE, "Strict GOP target enabled.\n");
    }

    if (ctx->nonref_p)
        ctx->encode_config.rcParams.enableNonRefP = 1;

    if (ctx->zerolatency)
        ctx->encode_config.rcParams.zeroReorderDelay = 1;

    if (ctx->quality)
        ctx->encode_config.rcParams.targetQuality = ctx->quality;
}

static av_cold int nvenc_setup_h264_config(AVCodecContext *avctx)
{
    NvencContext *ctx                      = avctx->priv_data;
    NV_ENC_CONFIG *cc                      = &ctx->encode_config;
    NV_ENC_CONFIG_H264 *h264               = &cc->encodeCodecConfig.h264Config;
    NV_ENC_CONFIG_H264_VUI_PARAMETERS *vui = &h264->h264VUIParameters;

    vui->colourMatrix = avctx->colorspace;
    vui->colourPrimaries = avctx->color_primaries;
    vui->transferCharacteristics = avctx->color_trc;
    vui->videoFullRangeFlag = (avctx->color_range == AVCOL_RANGE_JPEG
        || ctx->data_pix_fmt == AV_PIX_FMT_YUVJ420P || ctx->data_pix_fmt == AV_PIX_FMT_YUVJ422P || ctx->data_pix_fmt == AV_PIX_FMT_YUVJ444P);

    vui->colourDescriptionPresentFlag =
        (avctx->colorspace != 2 || avctx->color_primaries != 2 || avctx->color_trc != 2);

    vui->videoSignalTypePresentFlag =
        (vui->colourDescriptionPresentFlag
        || vui->videoFormat != 5
        || vui->videoFullRangeFlag != 0);

    h264->sliceMode = 3;
    h264->sliceModeData = 1;

    h264->disableSPSPPS = (avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER) ? 1 : 0;
    h264->repeatSPSPPS  = (avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER) ? 0 : 1;
    h264->outputAUD     = 1;

    if (avctx->refs >= 0) {
        /* 0 means "let the hardware decide" */
        h264->maxNumRefFrames = avctx->refs;
    }
    if (avctx->gop_size >= 0) {
        h264->idrPeriod = cc->gopLength;
    }

    if (IS_CBR(cc->rcParams.rateControlMode)) {
        h264->outputBufferingPeriodSEI = 1;
        h264->outputPictureTimingSEI   = 1;
    }

    if (cc->rcParams.rateControlMode == NV_ENC_PARAMS_RC_2_PASS_QUALITY ||
        cc->rcParams.rateControlMode == NV_ENC_PARAMS_RC_2_PASS_FRAMESIZE_CAP ||
        cc->rcParams.rateControlMode == NV_ENC_PARAMS_RC_2_PASS_VBR) {
        h264->adaptiveTransformMode = NV_ENC_H264_ADAPTIVE_TRANSFORM_ENABLE;
        h264->fmoMode = NV_ENC_H264_FMO_DISABLE;
    }

    if (ctx->flags & NVENC_LOSSLESS) {
        h264->qpPrimeYZeroTransformBypassFlag = 1;
    } else {
        switch(ctx->profile) {
        case NV_ENC_H264_PROFILE_BASELINE:
            cc->profileGUID = NV_ENC_H264_PROFILE_BASELINE_GUID;
            avctx->profile = FF_PROFILE_H264_BASELINE;
            break;
        case NV_ENC_H264_PROFILE_MAIN:
            cc->profileGUID = NV_ENC_H264_PROFILE_MAIN_GUID;
            avctx->profile = FF_PROFILE_H264_MAIN;
            break;
        case NV_ENC_H264_PROFILE_HIGH:
            cc->profileGUID = NV_ENC_H264_PROFILE_HIGH_GUID;
            avctx->profile = FF_PROFILE_H264_HIGH;
            break;
        case NV_ENC_H264_PROFILE_HIGH_444P:
            cc->profileGUID = NV_ENC_H264_PROFILE_HIGH_444_GUID;
            avctx->profile = FF_PROFILE_H264_HIGH_444_PREDICTIVE;
            break;
        }
    }

    // force setting profile as high444p if input is AV_PIX_FMT_YUV444P
    if (ctx->data_pix_fmt == AV_PIX_FMT_YUV444P) {
        cc->profileGUID = NV_ENC_H264_PROFILE_HIGH_444_GUID;
        avctx->profile = FF_PROFILE_H264_HIGH_444_PREDICTIVE;
    }

    h264->chromaFormatIDC = avctx->profile == FF_PROFILE_H264_HIGH_444_PREDICTIVE ? 3 : 1;

    h264->level = ctx->level;

    return 0;
}

static av_cold int nvenc_setup_hevc_config(AVCodecContext *avctx)
{
    NvencContext *ctx                      = avctx->priv_data;
    NV_ENC_CONFIG *cc                      = &ctx->encode_config;
    NV_ENC_CONFIG_HEVC *hevc               = &cc->encodeCodecConfig.hevcConfig;
    NV_ENC_CONFIG_HEVC_VUI_PARAMETERS *vui = &hevc->hevcVUIParameters;

    vui->colourMatrix = avctx->colorspace;
    vui->colourPrimaries = avctx->color_primaries;
    vui->transferCharacteristics = avctx->color_trc;
    vui->videoFullRangeFlag = (avctx->color_range == AVCOL_RANGE_JPEG
        || ctx->data_pix_fmt == AV_PIX_FMT_YUVJ420P || ctx->data_pix_fmt == AV_PIX_FMT_YUVJ422P || ctx->data_pix_fmt == AV_PIX_FMT_YUVJ444P);

    vui->colourDescriptionPresentFlag =
        (avctx->colorspace != 2 || avctx->color_primaries != 2 || avctx->color_trc != 2);

    vui->videoSignalTypePresentFlag =
        (vui->colourDescriptionPresentFlag
        || vui->videoFormat != 5
        || vui->videoFullRangeFlag != 0);

    hevc->sliceMode = 3;
    hevc->sliceModeData = 1;

    hevc->disableSPSPPS = (avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER) ? 1 : 0;
    hevc->repeatSPSPPS  = (avctx->flags & AV_CODEC_FLAG_GLOBAL_HEADER) ? 0 : 1;
    hevc->outputAUD     = 1;

    if (avctx->refs >= 0) {
        /* 0 means "let the hardware decide" */
        hevc->maxNumRefFramesInDPB = avctx->refs;
    }
    if (avctx->gop_size >= 0) {
        hevc->idrPeriod = cc->gopLength;
    }

    if (IS_CBR(cc->rcParams.rateControlMode)) {
        hevc->outputBufferingPeriodSEI = 1;
        hevc->outputPictureTimingSEI   = 1;
    }

    switch(ctx->profile) {
    case NV_ENC_HEVC_PROFILE_MAIN:
        cc->profileGUID = NV_ENC_HEVC_PROFILE_MAIN_GUID;
        avctx->profile = FF_PROFILE_HEVC_MAIN;
        break;
    case NV_ENC_HEVC_PROFILE_MAIN_10:
        cc->profileGUID = NV_ENC_HEVC_PROFILE_MAIN10_GUID;
        avctx->profile = FF_PROFILE_HEVC_MAIN_10;
        break;
    case NV_ENC_HEVC_PROFILE_REXT:
        cc->profileGUID = NV_ENC_HEVC_PROFILE_FREXT_GUID;
        avctx->profile = FF_PROFILE_HEVC_REXT;
        break;
    }

    // force setting profile as main10 if input is 10 bit
    if (IS_10BIT(ctx->data_pix_fmt)) {
        cc->profileGUID = NV_ENC_HEVC_PROFILE_MAIN10_GUID;
        avctx->profile = FF_PROFILE_HEVC_MAIN_10;
    }

    // force setting profile as rext if input is yuv444
    if (IS_YUV444(ctx->data_pix_fmt)) {
        cc->profileGUID = NV_ENC_HEVC_PROFILE_FREXT_GUID;
        avctx->profile = FF_PROFILE_HEVC_REXT;
    }

    hevc->chromaFormatIDC = IS_YUV444(ctx->data_pix_fmt) ? 3 : 1;

    hevc->pixelBitDepthMinus8 = IS_10BIT(ctx->data_pix_fmt) ? 2 : 0;

    hevc->level = ctx->level;

    hevc->tier = ctx->tier;

    return 0;
}

static av_cold int nvenc_setup_codec_config(AVCodecContext *avctx)
{
    switch (avctx->codec->id) {
    case AV_CODEC_ID_H264:
        return nvenc_setup_h264_config(avctx);
    case AV_CODEC_ID_HEVC:
        return nvenc_setup_hevc_config(avctx);
    /* Earlier switch/case will return if unknown codec is passed. */
    }

    return 0;
}

static av_cold int nvenc_setup_encoder(AVCodecContext *avctx)
{
    NvencContext *ctx = avctx->priv_data;
    NvencDynLoadFunctions *dl_fn = &ctx->nvenc_dload_funcs;
    NV_ENCODE_API_FUNCTION_LIST *p_nvenc = &dl_fn->nvenc_funcs;

    NV_ENC_PRESET_CONFIG preset_config = { 0 };
    NVENCSTATUS nv_status = NV_ENC_SUCCESS;
    AVCPBProperties *cpb_props;
    int res = 0;
    int dw, dh;

    ctx->encode_config.version = NV_ENC_CONFIG_VER;
    ctx->init_encode_params.version = NV_ENC_INITIALIZE_PARAMS_VER;

    ctx->init_encode_params.encodeHeight = avctx->height;
    ctx->init_encode_params.encodeWidth = avctx->width;

    ctx->init_encode_params.encodeConfig = &ctx->encode_config;

    nvenc_map_preset(ctx);

    preset_config.version = NV_ENC_PRESET_CONFIG_VER;
    preset_config.presetCfg.version = NV_ENC_CONFIG_VER;

    nv_status = p_nvenc->nvEncGetEncodePresetConfig(ctx->nvencoder,
                                                    ctx->init_encode_params.encodeGUID,
                                                    ctx->init_encode_params.presetGUID,
                                                    &preset_config);
    if (nv_status != NV_ENC_SUCCESS)
        return nvenc_print_error(avctx, nv_status, "Cannot get the preset configuration");

    memcpy(&ctx->encode_config, &preset_config.presetCfg, sizeof(ctx->encode_config));

    ctx->encode_config.version = NV_ENC_CONFIG_VER;

    if (avctx->sample_aspect_ratio.num && avctx->sample_aspect_ratio.den &&
        (avctx->sample_aspect_ratio.num != 1 || avctx->sample_aspect_ratio.num != 1)) {
        av_reduce(&dw, &dh,
                  avctx->width * avctx->sample_aspect_ratio.num,
                  avctx->height * avctx->sample_aspect_ratio.den,
                  1024 * 1024);
        ctx->init_encode_params.darHeight = dh;
        ctx->init_encode_params.darWidth = dw;
    } else {
        ctx->init_encode_params.darHeight = avctx->height;
        ctx->init_encode_params.darWidth = avctx->width;
    }

    // De-compensate for hardware, dubiously, trying to compensate for
    // playback at 704 pixel width.
    if (avctx->width == 720 &&
        (avctx->height == 480 || avctx->height == 576)) {
        av_reduce(&dw, &dh,
                  ctx->init_encode_params.darWidth * 44,
                  ctx->init_encode_params.darHeight * 45,
                  1024 * 1024);
        ctx->init_encode_params.darHeight = dh;
        ctx->init_encode_params.darWidth = dw;
    }

    ctx->init_encode_params.frameRateNum = avctx->time_base.den;
    ctx->init_encode_params.frameRateDen = avctx->time_base.num * avctx->ticks_per_frame;

    ctx->init_encode_params.enableEncodeAsync = 0;
    ctx->init_encode_params.enablePTD = 1;

    if (avctx->gop_size > 0) {
        if (avctx->max_b_frames >= 0) {
            /* 0 is intra-only, 1 is I/P only, 2 is one B-Frame, 3 two B-frames, and so on. */
            ctx->encode_config.frameIntervalP = avctx->max_b_frames + 1;
        }

        ctx->encode_config.gopLength = avctx->gop_size;
    } else if (avctx->gop_size == 0) {
        ctx->encode_config.frameIntervalP = 0;
        ctx->encode_config.gopLength = 1;
    }

    ctx->initial_pts[0] = AV_NOPTS_VALUE;
    ctx->initial_pts[1] = AV_NOPTS_VALUE;

    nvenc_setup_rate_control(avctx);

    if (avctx->flags & AV_CODEC_FLAG_INTERLACED_DCT) {
        ctx->encode_config.frameFieldMode = NV_ENC_PARAMS_FRAME_FIELD_MODE_FIELD;
    } else {
        ctx->encode_config.frameFieldMode = NV_ENC_PARAMS_FRAME_FIELD_MODE_FRAME;
    }

    res = nvenc_setup_codec_config(avctx);
    if (res)
        return res;

    nv_status = p_nvenc->nvEncInitializeEncoder(ctx->nvencoder, &ctx->init_encode_params);
    if (nv_status != NV_ENC_SUCCESS) {
        return nvenc_print_error(avctx, nv_status, "InitializeEncoder failed");
    }

    if (ctx->encode_config.frameIntervalP > 1)
        avctx->has_b_frames = 2;

    if (ctx->encode_config.rcParams.averageBitRate > 0)
        avctx->bit_rate = ctx->encode_config.rcParams.averageBitRate;

    cpb_props = ff_add_cpb_side_data(avctx);
    if (!cpb_props)
        return AVERROR(ENOMEM);
    cpb_props->max_bitrate = ctx->encode_config.rcParams.maxBitRate;
    cpb_props->avg_bitrate = avctx->bit_rate;
    cpb_props->buffer_size = ctx->encode_config.rcParams.vbvBufferSize;

    return 0;
}

static av_cold int nvenc_alloc_surface(AVCodecContext *avctx, int idx)
{
    NvencContext *ctx = avctx->priv_data;
    NvencDynLoadFunctions *dl_fn = &ctx->nvenc_dload_funcs;
    NV_ENCODE_API_FUNCTION_LIST *p_nvenc = &dl_fn->nvenc_funcs;

    NVENCSTATUS nv_status;
    NV_ENC_CREATE_BITSTREAM_BUFFER allocOut = { 0 };
    allocOut.version = NV_ENC_CREATE_BITSTREAM_BUFFER_VER;

    switch (ctx->data_pix_fmt) {
    case AV_PIX_FMT_YUV420P:
        ctx->surfaces[idx].format = NV_ENC_BUFFER_FORMAT_YV12_PL;
        break;

    case AV_PIX_FMT_NV12:
        ctx->surfaces[idx].format = NV_ENC_BUFFER_FORMAT_NV12_PL;
        break;

    case AV_PIX_FMT_P010:
        ctx->surfaces[idx].format = NV_ENC_BUFFER_FORMAT_YUV420_10BIT;
        break;

    case AV_PIX_FMT_YUV444P:
        ctx->surfaces[idx].format = NV_ENC_BUFFER_FORMAT_YUV444_PL;
        break;

    case AV_PIX_FMT_YUV444P16:
        ctx->surfaces[idx].format = NV_ENC_BUFFER_FORMAT_YUV444_10BIT;
        break;

    case AV_PIX_FMT_0RGB32:
        ctx->surfaces[idx].format = NV_ENC_BUFFER_FORMAT_ARGB;
        break;

    case AV_PIX_FMT_0BGR32:
        ctx->surfaces[idx].format = NV_ENC_BUFFER_FORMAT_ABGR;
        break;

    default:
        av_log(avctx, AV_LOG_FATAL, "Invalid input pixel format\n");
        return AVERROR(EINVAL);
    }

    if (avctx->pix_fmt == AV_PIX_FMT_CUDA) {
        ctx->surfaces[idx].in_ref = av_frame_alloc();
        if (!ctx->surfaces[idx].in_ref)
            return AVERROR(ENOMEM);
    } else {
        NV_ENC_CREATE_INPUT_BUFFER allocSurf = { 0 };
        allocSurf.version = NV_ENC_CREATE_INPUT_BUFFER_VER;
        allocSurf.width = (avctx->width + 31) & ~31;
        allocSurf.height = (avctx->height + 31) & ~31;
        allocSurf.memoryHeap = NV_ENC_MEMORY_HEAP_SYSMEM_CACHED;
        allocSurf.bufferFmt = ctx->surfaces[idx].format;

        nv_status = p_nvenc->nvEncCreateInputBuffer(ctx->nvencoder, &allocSurf);
        if (nv_status != NV_ENC_SUCCESS) {
            return nvenc_print_error(avctx, nv_status, "CreateInputBuffer failed");
        }

        ctx->surfaces[idx].input_surface = allocSurf.inputBuffer;
        ctx->surfaces[idx].width = allocSurf.width;
        ctx->surfaces[idx].height = allocSurf.height;
    }

    ctx->surfaces[idx].lockCount = 0;

    /* 1MB is large enough to hold most output frames.
     * NVENC increases this automaticaly if it is not enough. */
    allocOut.size = 1024 * 1024;

    allocOut.memoryHeap = NV_ENC_MEMORY_HEAP_SYSMEM_CACHED;

    nv_status = p_nvenc->nvEncCreateBitstreamBuffer(ctx->nvencoder, &allocOut);
    if (nv_status != NV_ENC_SUCCESS) {
        int err = nvenc_print_error(avctx, nv_status, "CreateBitstreamBuffer failed");
        if (avctx->pix_fmt != AV_PIX_FMT_CUDA)
            p_nvenc->nvEncDestroyInputBuffer(ctx->nvencoder, ctx->surfaces[idx].input_surface);
        av_frame_free(&ctx->surfaces[idx].in_ref);
        return err;
    }

    ctx->surfaces[idx].output_surface = allocOut.bitstreamBuffer;
    ctx->surfaces[idx].size = allocOut.size;

    return 0;
}

static av_cold int nvenc_setup_surfaces(AVCodecContext *avctx)
{
    NvencContext *ctx = avctx->priv_data;
    int i, res;
    int num_mbs = ((avctx->width + 15) >> 4) * ((avctx->height + 15) >> 4);
    ctx->nb_surfaces = FFMAX((num_mbs >= 8160) ? 32 : 48,
                             ctx->nb_surfaces);
    ctx->async_depth = FFMIN(ctx->async_depth, ctx->nb_surfaces - 1);


    ctx->surfaces = av_mallocz_array(ctx->nb_surfaces, sizeof(*ctx->surfaces));
    if (!ctx->surfaces)
        return AVERROR(ENOMEM);

    ctx->timestamp_list = av_fifo_alloc(ctx->nb_surfaces * sizeof(int64_t));
    if (!ctx->timestamp_list)
        return AVERROR(ENOMEM);
    ctx->output_surface_queue = av_fifo_alloc(ctx->nb_surfaces * sizeof(NvencSurface*));
    if (!ctx->output_surface_queue)
        return AVERROR(ENOMEM);
    ctx->output_surface_ready_queue = av_fifo_alloc(ctx->nb_surfaces * sizeof(NvencSurface*));
    if (!ctx->output_surface_ready_queue)
        return AVERROR(ENOMEM);

    for (i = 0; i < ctx->nb_surfaces; i++) {
        if ((res = nvenc_alloc_surface(avctx, i)) < 0)
            return res;
    }

    return 0;
}

static av_cold int nvenc_setup_extradata(AVCodecContext *avctx)
{
    NvencContext *ctx = avctx->priv_data;
    NvencDynLoadFunctions *dl_fn = &ctx->nvenc_dload_funcs;
    NV_ENCODE_API_FUNCTION_LIST *p_nvenc = &dl_fn->nvenc_funcs;

    NVENCSTATUS nv_status;
    uint32_t outSize = 0;
    char tmpHeader[256];
    NV_ENC_SEQUENCE_PARAM_PAYLOAD payload = { 0 };
    payload.version = NV_ENC_SEQUENCE_PARAM_PAYLOAD_VER;

    payload.spsppsBuffer = tmpHeader;
    payload.inBufferSize = sizeof(tmpHeader);
    payload.outSPSPPSPayloadSize = &outSize;

    nv_status = p_nvenc->nvEncGetSequenceParams(ctx->nvencoder, &payload);
    if (nv_status != NV_ENC_SUCCESS) {
        return nvenc_print_error(avctx, nv_status, "GetSequenceParams failed");
    }

    avctx->extradata_size = outSize;
    avctx->extradata = av_mallocz(outSize + AV_INPUT_BUFFER_PADDING_SIZE);

    if (!avctx->extradata) {
        return AVERROR(ENOMEM);
    }

    memcpy(avctx->extradata, tmpHeader, outSize);

    return 0;
}

av_cold int ff_nvenc_encode_close(AVCodecContext *avctx)
{
    NvencContext *ctx               = avctx->priv_data;
    NvencDynLoadFunctions *dl_fn = &ctx->nvenc_dload_funcs;
    NV_ENCODE_API_FUNCTION_LIST *p_nvenc = &dl_fn->nvenc_funcs;
    int i;

    /* the encoder has to be flushed before it can be closed */
    if (ctx->nvencoder) {
        NV_ENC_PIC_PARAMS params        = { .version        = NV_ENC_PIC_PARAMS_VER,
                                            .encodePicFlags = NV_ENC_PIC_FLAG_EOS };

        p_nvenc->nvEncEncodePicture(ctx->nvencoder, &params);
    }

    av_fifo_freep(&ctx->timestamp_list);
    av_fifo_freep(&ctx->output_surface_ready_queue);
    av_fifo_freep(&ctx->output_surface_queue);

    if (ctx->surfaces && avctx->pix_fmt == AV_PIX_FMT_CUDA) {
        for (i = 0; i < ctx->nb_surfaces; ++i) {
            if (ctx->surfaces[i].input_surface) {
                 p_nvenc->nvEncUnmapInputResource(ctx->nvencoder, ctx->surfaces[i].in_map.mappedResource);
            }
        }
        for (i = 0; i < ctx->nb_registered_frames; i++) {
            if (ctx->registered_frames[i].regptr)
                p_nvenc->nvEncUnregisterResource(ctx->nvencoder, ctx->registered_frames[i].regptr);
        }
        ctx->nb_registered_frames = 0;
    }

    if (ctx->surfaces) {
        for (i = 0; i < ctx->nb_surfaces; ++i) {
            if (avctx->pix_fmt != AV_PIX_FMT_CUDA)
                p_nvenc->nvEncDestroyInputBuffer(ctx->nvencoder, ctx->surfaces[i].input_surface);
            av_frame_free(&ctx->surfaces[i].in_ref);
            p_nvenc->nvEncDestroyBitstreamBuffer(ctx->nvencoder, ctx->surfaces[i].output_surface);
        }
    }
    av_freep(&ctx->surfaces);
    ctx->nb_surfaces = 0;

    if (ctx->nvencoder)
        p_nvenc->nvEncDestroyEncoder(ctx->nvencoder);
    ctx->nvencoder = NULL;

    if (ctx->cu_context_internal)
        dl_fn->cu_ctx_destroy(ctx->cu_context_internal);
    ctx->cu_context = ctx->cu_context_internal = NULL;

    if (dl_fn->nvenc)
        dlclose(dl_fn->nvenc);
    dl_fn->nvenc = NULL;

    dl_fn->nvenc_device_count = 0;

#if !CONFIG_CUDA
    if (dl_fn->cuda)
        dlclose(dl_fn->cuda);
    dl_fn->cuda = NULL;
#endif

    dl_fn->cu_init = NULL;
    dl_fn->cu_device_get_count = NULL;
    dl_fn->cu_device_get = NULL;
    dl_fn->cu_device_get_name = NULL;
    dl_fn->cu_device_compute_capability = NULL;
    dl_fn->cu_ctx_create = NULL;
    dl_fn->cu_ctx_pop_current = NULL;
    dl_fn->cu_ctx_destroy = NULL;

    av_log(avctx, AV_LOG_VERBOSE, "Nvenc unloaded\n");

    return 0;
}

av_cold int ff_nvenc_encode_init(AVCodecContext *avctx)
{
    NvencContext *ctx = avctx->priv_data;
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

static NvencSurface *get_free_frame(NvencContext *ctx)
{
    int i;

    for (i = 0; i < ctx->nb_surfaces; ++i) {
        if (!ctx->surfaces[i].lockCount) {
            ctx->surfaces[i].lockCount = 1;
            return &ctx->surfaces[i];
        }
    }

    return NULL;
}

static int nvenc_copy_frame(AVCodecContext *avctx, NvencSurface *nv_surface,
            NV_ENC_LOCK_INPUT_BUFFER *lock_buffer_params, const AVFrame *frame)
{
    int dst_linesize[4] = {
        lock_buffer_params->pitch,
        lock_buffer_params->pitch,
        lock_buffer_params->pitch,
        lock_buffer_params->pitch
    };
    uint8_t *dst_data[4];
    int ret;

    if (frame->format == AV_PIX_FMT_YUV420P)
        dst_linesize[1] = dst_linesize[2] >>= 1;

    ret = av_image_fill_pointers(dst_data, frame->format, nv_surface->height,
                                 lock_buffer_params->bufferDataPtr, dst_linesize);
    if (ret < 0)
        return ret;

    if (frame->format == AV_PIX_FMT_YUV420P)
        FFSWAP(uint8_t*, dst_data[1], dst_data[2]);

    av_image_copy(dst_data, dst_linesize,
                  (const uint8_t**)frame->data, frame->linesize, frame->format,
                  avctx->width, avctx->height);

    return 0;
}

static int nvenc_find_free_reg_resource(AVCodecContext *avctx)
{
    NvencContext *ctx = avctx->priv_data;
    NvencDynLoadFunctions *dl_fn = &ctx->nvenc_dload_funcs;
    NV_ENCODE_API_FUNCTION_LIST *p_nvenc = &dl_fn->nvenc_funcs;

    int i;

    if (ctx->nb_registered_frames == FF_ARRAY_ELEMS(ctx->registered_frames)) {
        for (i = 0; i < ctx->nb_registered_frames; i++) {
            if (!ctx->registered_frames[i].mapped) {
                if (ctx->registered_frames[i].regptr) {
                    p_nvenc->nvEncUnregisterResource(ctx->nvencoder,
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
    NvencContext *ctx = avctx->priv_data;
    NvencDynLoadFunctions *dl_fn = &ctx->nvenc_dload_funcs;
    NV_ENCODE_API_FUNCTION_LIST *p_nvenc = &dl_fn->nvenc_funcs;

    AVHWFramesContext *frames_ctx = (AVHWFramesContext*)avctx->hw_frames_ctx->data;
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
    reg.bufferFormat       = ctx->surfaces[0].format;
    reg.pitch              = frame->linesize[0];
    reg.resourceToRegister = frame->data[0];

    ret = p_nvenc->nvEncRegisterResource(ctx->nvencoder, &reg);
    if (ret != NV_ENC_SUCCESS) {
        nvenc_print_error(avctx, ret, "Error registering an input resource");
        return AVERROR_UNKNOWN;
    }

    ctx->registered_frames[idx].ptr    = (CUdeviceptr)frame->data[0];
    ctx->registered_frames[idx].regptr = reg.registeredResource;
    return idx;
}

static int nvenc_upload_frame(AVCodecContext *avctx, const AVFrame *frame,
                                      NvencSurface *nvenc_frame)
{
    NvencContext *ctx = avctx->priv_data;
    NvencDynLoadFunctions *dl_fn = &ctx->nvenc_dload_funcs;
    NV_ENCODE_API_FUNCTION_LIST *p_nvenc = &dl_fn->nvenc_funcs;

    int res;
    NVENCSTATUS nv_status;

    if (avctx->pix_fmt == AV_PIX_FMT_CUDA) {
        int reg_idx = nvenc_register_frame(avctx, frame);
        if (reg_idx < 0) {
            av_log(avctx, AV_LOG_ERROR, "Could not register an input CUDA frame\n");
            return reg_idx;
        }

        res = av_frame_ref(nvenc_frame->in_ref, frame);
        if (res < 0)
            return res;

        nvenc_frame->in_map.version = NV_ENC_MAP_INPUT_RESOURCE_VER;
        nvenc_frame->in_map.registeredResource = ctx->registered_frames[reg_idx].regptr;
        nv_status = p_nvenc->nvEncMapInputResource(ctx->nvencoder, &nvenc_frame->in_map);
        if (nv_status != NV_ENC_SUCCESS) {
            av_frame_unref(nvenc_frame->in_ref);
            return nvenc_print_error(avctx, nv_status, "Error mapping an input resource");
        }

        ctx->registered_frames[reg_idx].mapped = 1;
        nvenc_frame->reg_idx                   = reg_idx;
        nvenc_frame->input_surface             = nvenc_frame->in_map.mappedResource;
        nvenc_frame->pitch                     = frame->linesize[0];
        return 0;
    } else {
        NV_ENC_LOCK_INPUT_BUFFER lockBufferParams = { 0 };

        lockBufferParams.version = NV_ENC_LOCK_INPUT_BUFFER_VER;
        lockBufferParams.inputBuffer = nvenc_frame->input_surface;

        nv_status = p_nvenc->nvEncLockInputBuffer(ctx->nvencoder, &lockBufferParams);
        if (nv_status != NV_ENC_SUCCESS) {
            return nvenc_print_error(avctx, nv_status, "Failed locking nvenc input buffer");
        }

        nvenc_frame->pitch = lockBufferParams.pitch;
        res = nvenc_copy_frame(avctx, nvenc_frame, &lockBufferParams, frame);

        nv_status = p_nvenc->nvEncUnlockInputBuffer(ctx->nvencoder, nvenc_frame->input_surface);
        if (nv_status != NV_ENC_SUCCESS) {
            return nvenc_print_error(avctx, nv_status, "Failed unlocking input buffer!");
        }

        return res;
    }
}

static void nvenc_codec_specific_pic_params(AVCodecContext *avctx,
                                            NV_ENC_PIC_PARAMS *params)
{
    NvencContext *ctx = avctx->priv_data;

    switch (avctx->codec->id) {
    case AV_CODEC_ID_H264:
        params->codecPicParams.h264PicParams.sliceMode =
            ctx->encode_config.encodeCodecConfig.h264Config.sliceMode;
        params->codecPicParams.h264PicParams.sliceModeData =
            ctx->encode_config.encodeCodecConfig.h264Config.sliceModeData;
      break;
    case AV_CODEC_ID_HEVC:
        params->codecPicParams.hevcPicParams.sliceMode =
            ctx->encode_config.encodeCodecConfig.hevcConfig.sliceMode;
        params->codecPicParams.hevcPicParams.sliceModeData =
            ctx->encode_config.encodeCodecConfig.hevcConfig.sliceModeData;
        break;
    }
}

static inline void timestamp_queue_enqueue(AVFifoBuffer* queue, int64_t timestamp)
{
    av_fifo_generic_write(queue, &timestamp, sizeof(timestamp), NULL);
}

static inline int64_t timestamp_queue_dequeue(AVFifoBuffer* queue)
{
    int64_t timestamp = AV_NOPTS_VALUE;
    if (av_fifo_size(queue) > 0)
        av_fifo_generic_read(queue, &timestamp, sizeof(timestamp), NULL);

    return timestamp;
}

static int nvenc_set_timestamp(AVCodecContext *avctx,
                               NV_ENC_LOCK_BITSTREAM *params,
                               AVPacket *pkt)
{
    NvencContext *ctx = avctx->priv_data;

    pkt->pts = params->outputTimeStamp;

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

    pkt->dts = timestamp_queue_dequeue(ctx->timestamp_list);

    return 0;
}

static int process_output_surface(AVCodecContext *avctx, AVPacket *pkt, NvencSurface *tmpoutsurf)
{
    NvencContext *ctx = avctx->priv_data;
    NvencDynLoadFunctions *dl_fn = &ctx->nvenc_dload_funcs;
    NV_ENCODE_API_FUNCTION_LIST *p_nvenc = &dl_fn->nvenc_funcs;

    uint32_t slice_mode_data;
    uint32_t *slice_offsets = NULL;
    NV_ENC_LOCK_BITSTREAM lock_params = { 0 };
    NVENCSTATUS nv_status;
    int res = 0;

    enum AVPictureType pict_type;

    switch (avctx->codec->id) {
    case AV_CODEC_ID_H264:
      slice_mode_data = ctx->encode_config.encodeCodecConfig.h264Config.sliceModeData;
      break;
    case AV_CODEC_ID_H265:
      slice_mode_data = ctx->encode_config.encodeCodecConfig.hevcConfig.sliceModeData;
      break;
    default:
      av_log(avctx, AV_LOG_ERROR, "Unknown codec name\n");
      res = AVERROR(EINVAL);
      goto error;
    }
    slice_offsets = av_mallocz(slice_mode_data * sizeof(*slice_offsets));

    if (!slice_offsets)
        goto error;

    lock_params.version = NV_ENC_LOCK_BITSTREAM_VER;

    lock_params.doNotWait = 0;
    lock_params.outputBitstream = tmpoutsurf->output_surface;
    lock_params.sliceOffsets = slice_offsets;

    nv_status = p_nvenc->nvEncLockBitstream(ctx->nvencoder, &lock_params);
    if (nv_status != NV_ENC_SUCCESS) {
        res = nvenc_print_error(avctx, nv_status, "Failed locking bitstream buffer");
        goto error;
    }

    if (res = ff_alloc_packet2(avctx, pkt, lock_params.bitstreamSizeInBytes,0)) {
        p_nvenc->nvEncUnlockBitstream(ctx->nvencoder, tmpoutsurf->output_surface);
        goto error;
    }

    memcpy(pkt->data, lock_params.bitstreamBufferPtr, lock_params.bitstreamSizeInBytes);

    nv_status = p_nvenc->nvEncUnlockBitstream(ctx->nvencoder, tmpoutsurf->output_surface);
    if (nv_status != NV_ENC_SUCCESS)
        nvenc_print_error(avctx, nv_status, "Failed unlocking bitstream buffer, expect the gates of mordor to open");


    if (avctx->pix_fmt == AV_PIX_FMT_CUDA) {
        p_nvenc->nvEncUnmapInputResource(ctx->nvencoder, tmpoutsurf->in_map.mappedResource);
        av_frame_unref(tmpoutsurf->in_ref);
        ctx->registered_frames[tmpoutsurf->reg_idx].mapped = 0;

        tmpoutsurf->input_surface = NULL;
    }

    switch (lock_params.pictureType) {
    case NV_ENC_PIC_TYPE_IDR:
        pkt->flags |= AV_PKT_FLAG_KEY;
    case NV_ENC_PIC_TYPE_I:
        pict_type = AV_PICTURE_TYPE_I;
        break;
    case NV_ENC_PIC_TYPE_P:
        pict_type = AV_PICTURE_TYPE_P;
        break;
    case NV_ENC_PIC_TYPE_B:
        pict_type = AV_PICTURE_TYPE_B;
        break;
    case NV_ENC_PIC_TYPE_BI:
        pict_type = AV_PICTURE_TYPE_BI;
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Unknown picture type encountered, expect the output to be broken.\n");
        av_log(avctx, AV_LOG_ERROR, "Please report this error and include as much information on how to reproduce it as possible.\n");
        res = AVERROR_EXTERNAL;
        goto error;
    }

#if FF_API_CODED_FRAME
FF_DISABLE_DEPRECATION_WARNINGS
    avctx->coded_frame->pict_type = pict_type;
FF_ENABLE_DEPRECATION_WARNINGS
#endif

    ff_side_data_set_encoder_stats(pkt,
        (lock_params.frameAvgQP - 1) * FF_QP2LAMBDA, NULL, 0, pict_type);

    res = nvenc_set_timestamp(avctx, &lock_params, pkt);
    if (res < 0)
        goto error2;

    av_free(slice_offsets);

    return 0;

error:
    timestamp_queue_dequeue(ctx->timestamp_list);

error2:
    av_free(slice_offsets);

    return res;
}

static int output_ready(AVCodecContext *avctx, int flush)
{
    NvencContext *ctx = avctx->priv_data;
    int nb_ready, nb_pending;

    /* when B-frames are enabled, we wait for two initial timestamps to
     * calculate the first dts */
    if (!flush && avctx->max_b_frames > 0 &&
        (ctx->initial_pts[0] == AV_NOPTS_VALUE || ctx->initial_pts[1] == AV_NOPTS_VALUE))
        return 0;

    nb_ready   = av_fifo_size(ctx->output_surface_ready_queue)   / sizeof(NvencSurface*);
    nb_pending = av_fifo_size(ctx->output_surface_queue)         / sizeof(NvencSurface*);
    if (flush)
        return nb_ready > 0;
    return (nb_ready > 0) && (nb_ready + nb_pending >= ctx->async_depth);
}

int ff_nvenc_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                          const AVFrame *frame, int *got_packet)
{
    NVENCSTATUS nv_status;
    NvencSurface *tmpoutsurf, *inSurf;
    int res;

    NvencContext *ctx = avctx->priv_data;
    NvencDynLoadFunctions *dl_fn = &ctx->nvenc_dload_funcs;
    NV_ENCODE_API_FUNCTION_LIST *p_nvenc = &dl_fn->nvenc_funcs;

    NV_ENC_PIC_PARAMS pic_params = { 0 };
    pic_params.version = NV_ENC_PIC_PARAMS_VER;

    if (frame) {
        inSurf = get_free_frame(ctx);
        if (!inSurf) {
            av_log(avctx, AV_LOG_ERROR, "No free surfaces\n");
            return AVERROR_BUG;
        }

        res = nvenc_upload_frame(avctx, frame, inSurf);
        if (res) {
            inSurf->lockCount = 0;
            return res;
        }

        pic_params.inputBuffer = inSurf->input_surface;
        pic_params.bufferFmt = inSurf->format;
        pic_params.inputWidth = avctx->width;
        pic_params.inputHeight = avctx->height;
        pic_params.inputPitch = inSurf->pitch;
        pic_params.outputBitstream = inSurf->output_surface;

        if (avctx->flags & AV_CODEC_FLAG_INTERLACED_DCT) {
            if (frame->top_field_first)
                pic_params.pictureStruct = NV_ENC_PIC_STRUCT_FIELD_TOP_BOTTOM;
            else
                pic_params.pictureStruct = NV_ENC_PIC_STRUCT_FIELD_BOTTOM_TOP;
        } else {
            pic_params.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;
        }

        if (ctx->forced_idr >= 0 && frame->pict_type == AV_PICTURE_TYPE_I) {
            pic_params.encodePicFlags =
                ctx->forced_idr ? NV_ENC_PIC_FLAG_FORCEIDR : NV_ENC_PIC_FLAG_FORCEINTRA;
        } else {
            pic_params.encodePicFlags = 0;
        }

        pic_params.inputTimeStamp = frame->pts;

        nvenc_codec_specific_pic_params(avctx, &pic_params);
    } else {
        pic_params.encodePicFlags = NV_ENC_PIC_FLAG_EOS;
    }

    nv_status = p_nvenc->nvEncEncodePicture(ctx->nvencoder, &pic_params);
    if (nv_status != NV_ENC_SUCCESS &&
        nv_status != NV_ENC_ERR_NEED_MORE_INPUT)
        return nvenc_print_error(avctx, nv_status, "EncodePicture failed!");

    if (frame) {
        av_fifo_generic_write(ctx->output_surface_queue, &inSurf, sizeof(inSurf), NULL);
        timestamp_queue_enqueue(ctx->timestamp_list, frame->pts);

        if (ctx->initial_pts[0] == AV_NOPTS_VALUE)
            ctx->initial_pts[0] = frame->pts;
        else if (ctx->initial_pts[1] == AV_NOPTS_VALUE)
            ctx->initial_pts[1] = frame->pts;
    }

    /* all the pending buffers are now ready for output */
    if (nv_status == NV_ENC_SUCCESS) {
        while (av_fifo_size(ctx->output_surface_queue) > 0) {
            av_fifo_generic_read(ctx->output_surface_queue, &tmpoutsurf, sizeof(tmpoutsurf), NULL);
            av_fifo_generic_write(ctx->output_surface_ready_queue, &tmpoutsurf, sizeof(tmpoutsurf), NULL);
        }
    }

    if (output_ready(avctx, !frame)) {
        av_fifo_generic_read(ctx->output_surface_ready_queue, &tmpoutsurf, sizeof(tmpoutsurf), NULL);

        res = process_output_surface(avctx, pkt, tmpoutsurf);

        if (res)
            return res;

        av_assert0(tmpoutsurf->lockCount);
        tmpoutsurf->lockCount--;

        *got_packet = 1;
    } else {
        *got_packet = 0;
    }

    return 0;
}
