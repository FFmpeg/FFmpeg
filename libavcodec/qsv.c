/*
 * Intel MediaSDK QSV encoder/decoder shared code
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

#include <mfxvideo.h>
#include <mfxjpeg.h>

#include <stdio.h>
#include <string.h>

#include "libavutil/avstring.h"
#include "libavutil/common.h"
#include "libavutil/error.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_qsv.h"
#include "libavutil/imgutils.h"
#include "libavutil/avassert.h"

#include "avcodec.h"
#include "qsv_internal.h"

#define MFX_IMPL_VIA_MASK(impl) (0x0f00 & (impl))
#define QSV_HAVE_USER_PLUGIN    !QSV_ONEVPL
#define QSV_HAVE_AUDIO          !QSV_ONEVPL

#include "mfxvp8.h"

#if QSV_HAVE_USER_PLUGIN
#include <mfxplugin.h>
#endif

#if QSV_ONEVPL
#include <mfxdispatcher.h>
#else
#define MFXUnload(a) do { } while(0)
#endif

int ff_qsv_codec_id_to_mfx(enum AVCodecID codec_id)
{
    switch (codec_id) {
    case AV_CODEC_ID_H264:
        return MFX_CODEC_AVC;
    case AV_CODEC_ID_HEVC:
        return MFX_CODEC_HEVC;
    case AV_CODEC_ID_MPEG1VIDEO:
    case AV_CODEC_ID_MPEG2VIDEO:
        return MFX_CODEC_MPEG2;
    case AV_CODEC_ID_VC1:
        return MFX_CODEC_VC1;
    case AV_CODEC_ID_VP8:
        return MFX_CODEC_VP8;
    case AV_CODEC_ID_MJPEG:
        return MFX_CODEC_JPEG;
    case AV_CODEC_ID_VP9:
        return MFX_CODEC_VP9;
#if QSV_VERSION_ATLEAST(1, 34)
    case AV_CODEC_ID_AV1:
        return MFX_CODEC_AV1;
#endif

    default:
        break;
    }

    return AVERROR(ENOSYS);
}

static const struct {
    int mfx_iopattern;
    const char *desc;
} qsv_iopatterns[] = {
    {MFX_IOPATTERN_IN_VIDEO_MEMORY,     "input is video memory surface"         },
    {MFX_IOPATTERN_IN_SYSTEM_MEMORY,    "input is system memory surface"        },
#if QSV_HAVE_OPAQUE
    {MFX_IOPATTERN_IN_OPAQUE_MEMORY,    "input is opaque memory surface"        },
#endif
    {MFX_IOPATTERN_OUT_VIDEO_MEMORY,    "output is video memory surface"        },
    {MFX_IOPATTERN_OUT_SYSTEM_MEMORY,   "output is system memory surface"       },
#if QSV_HAVE_OPAQUE
    {MFX_IOPATTERN_OUT_OPAQUE_MEMORY,   "output is opaque memory surface"       },
#endif
};

int ff_qsv_print_iopattern(void *log_ctx, int mfx_iopattern,
                           const char *extra_string)
{
    const char *desc = NULL;

    for (int i = 0; i < FF_ARRAY_ELEMS(qsv_iopatterns); i++) {
        if (qsv_iopatterns[i].mfx_iopattern == mfx_iopattern) {
            desc = qsv_iopatterns[i].desc;
        }
    }
    if (!desc)
        desc = "unknown iopattern";

    av_log(log_ctx, AV_LOG_VERBOSE, "%s: %s\n", extra_string, desc);
    return 0;
}

static const struct {
    mfxStatus   mfxerr;
    int         averr;
    const char *desc;
} qsv_errors[] = {
    { MFX_ERR_NONE,                     0,               "success"                              },
    { MFX_ERR_UNKNOWN,                  AVERROR_UNKNOWN, "unknown error"                        },
    { MFX_ERR_NULL_PTR,                 AVERROR(EINVAL), "NULL pointer"                         },
    { MFX_ERR_UNSUPPORTED,              AVERROR(ENOSYS), "unsupported"                          },
    { MFX_ERR_MEMORY_ALLOC,             AVERROR(ENOMEM), "failed to allocate memory"            },
    { MFX_ERR_NOT_ENOUGH_BUFFER,        AVERROR(ENOMEM), "insufficient input/output buffer"     },
    { MFX_ERR_INVALID_HANDLE,           AVERROR(EINVAL), "invalid handle"                       },
    { MFX_ERR_LOCK_MEMORY,              AVERROR(EIO),    "failed to lock the memory block"      },
    { MFX_ERR_NOT_INITIALIZED,          AVERROR_BUG,     "not initialized"                      },
    { MFX_ERR_NOT_FOUND,                AVERROR(ENOSYS), "specified object was not found"       },
    /* the following 3 errors should always be handled explicitly, so those "mappings"
     * are for completeness only */
    { MFX_ERR_MORE_DATA,                AVERROR_UNKNOWN, "expect more data at input"            },
    { MFX_ERR_MORE_SURFACE,             AVERROR_UNKNOWN, "expect more surface at output"        },
    { MFX_ERR_MORE_BITSTREAM,           AVERROR_UNKNOWN, "expect more bitstream at output"      },
    { MFX_ERR_ABORTED,                  AVERROR_UNKNOWN, "operation aborted"                    },
    { MFX_ERR_DEVICE_LOST,              AVERROR(EIO),    "device lost"                          },
    { MFX_ERR_INCOMPATIBLE_VIDEO_PARAM, AVERROR(EINVAL), "incompatible video parameters"        },
    { MFX_ERR_INVALID_VIDEO_PARAM,      AVERROR(EINVAL), "invalid video parameters"             },
    { MFX_ERR_UNDEFINED_BEHAVIOR,       AVERROR_BUG,     "undefined behavior"                   },
    { MFX_ERR_DEVICE_FAILED,            AVERROR(EIO),    "device failed"                        },
#if QSV_HAVE_AUDIO
    { MFX_ERR_INCOMPATIBLE_AUDIO_PARAM, AVERROR(EINVAL), "incompatible audio parameters"        },
    { MFX_ERR_INVALID_AUDIO_PARAM,      AVERROR(EINVAL), "invalid audio parameters"             },
#endif
    { MFX_ERR_GPU_HANG,                 AVERROR(EIO),    "GPU Hang"                             },
    { MFX_ERR_REALLOC_SURFACE,          AVERROR_UNKNOWN, "need bigger surface for output"       },

    { MFX_WRN_IN_EXECUTION,             0,               "operation in execution"               },
    { MFX_WRN_DEVICE_BUSY,              0,               "device busy"                          },
    { MFX_WRN_VIDEO_PARAM_CHANGED,      0,               "video parameters changed"             },
    { MFX_WRN_PARTIAL_ACCELERATION,     0,               "partial acceleration"                 },
    { MFX_WRN_INCOMPATIBLE_VIDEO_PARAM, 0,               "incompatible video parameters"        },
    { MFX_WRN_VALUE_NOT_CHANGED,        0,               "value is saturated"                   },
    { MFX_WRN_OUT_OF_RANGE,             0,               "value out of range"                   },
    { MFX_WRN_FILTER_SKIPPED,           0,               "filter skipped"                       },
#if QSV_HAVE_AUDIO
    { MFX_WRN_INCOMPATIBLE_AUDIO_PARAM, 0,               "incompatible audio parameters"        },
#endif

#if QSV_VERSION_ATLEAST(1, 31)
    { MFX_ERR_NONE_PARTIAL_OUTPUT,      0,               "partial output"                       },
#endif
};

/**
 * Convert a libmfx error code into an FFmpeg error code.
 */
static int qsv_map_error(mfxStatus mfx_err, const char **desc)
{
    int i;
    for (i = 0; i < FF_ARRAY_ELEMS(qsv_errors); i++) {
        if (qsv_errors[i].mfxerr == mfx_err) {
            if (desc)
                *desc = qsv_errors[i].desc;
            return qsv_errors[i].averr;
        }
    }
    if (desc)
        *desc = "unknown error";
    return AVERROR_UNKNOWN;
}

int ff_qsv_print_error(void *log_ctx, mfxStatus err,
                       const char *error_string)
{
    const char *desc;
    int ret = qsv_map_error(err, &desc);
    av_log(log_ctx, AV_LOG_ERROR, "%s: %s (%d)\n", error_string, desc, err);
    return ret;
}

int ff_qsv_print_warning(void *log_ctx, mfxStatus err,
                         const char *warning_string)
{
    const char *desc;
    int ret = qsv_map_error(err, &desc);
    av_log(log_ctx, AV_LOG_WARNING, "%s: %s (%d)\n", warning_string, desc, err);
    return ret;
}

enum AVPixelFormat ff_qsv_map_fourcc(uint32_t fourcc)
{
    switch (fourcc) {
    case MFX_FOURCC_NV12: return AV_PIX_FMT_NV12;
    case MFX_FOURCC_P010: return AV_PIX_FMT_P010;
    case MFX_FOURCC_P8:   return AV_PIX_FMT_PAL8;
    case MFX_FOURCC_A2RGB10: return AV_PIX_FMT_X2RGB10;
    case MFX_FOURCC_RGB4: return AV_PIX_FMT_BGRA;
#if CONFIG_VAAPI
    case MFX_FOURCC_YUY2: return AV_PIX_FMT_YUYV422;
    case MFX_FOURCC_Y210: return AV_PIX_FMT_Y210;
    case MFX_FOURCC_AYUV: return AV_PIX_FMT_VUYX;
    case MFX_FOURCC_Y410: return AV_PIX_FMT_XV30;
#if QSV_VERSION_ATLEAST(1, 31)
    case MFX_FOURCC_P016: return AV_PIX_FMT_P012;
    case MFX_FOURCC_Y216: return AV_PIX_FMT_Y212;
    case MFX_FOURCC_Y416: return AV_PIX_FMT_XV36;
#endif
#endif
    }
    return AV_PIX_FMT_NONE;
}

int ff_qsv_map_pixfmt(enum AVPixelFormat format, uint32_t *fourcc, uint16_t *shift)
{
    switch (format) {
    case AV_PIX_FMT_YUV420P:
    case AV_PIX_FMT_YUVJ420P:
    case AV_PIX_FMT_NV12:
        *fourcc = MFX_FOURCC_NV12;
        *shift = 0;
        return AV_PIX_FMT_NV12;
    case AV_PIX_FMT_YUV420P10:
    case AV_PIX_FMT_P010:
        *fourcc = MFX_FOURCC_P010;
        *shift = 1;
        return AV_PIX_FMT_P010;
    case AV_PIX_FMT_X2RGB10:
        *fourcc = MFX_FOURCC_A2RGB10;
        *shift = 1;
        return AV_PIX_FMT_X2RGB10;
    case AV_PIX_FMT_BGRA:
        *fourcc = MFX_FOURCC_RGB4;
        *shift = 0;
        return AV_PIX_FMT_BGRA;
#if CONFIG_VAAPI
    case AV_PIX_FMT_YUV422P:
    case AV_PIX_FMT_YUYV422:
        *fourcc = MFX_FOURCC_YUY2;
        *shift = 0;
        return AV_PIX_FMT_YUYV422;
    case AV_PIX_FMT_YUV422P10:
    case AV_PIX_FMT_Y210:
        *fourcc = MFX_FOURCC_Y210;
        *shift = 1;
        return AV_PIX_FMT_Y210;
    case AV_PIX_FMT_VUYX:
        *fourcc = MFX_FOURCC_AYUV;
        *shift = 0;
        return AV_PIX_FMT_VUYX;
    case AV_PIX_FMT_XV30:
        *fourcc = MFX_FOURCC_Y410;
        *shift = 0;
        return AV_PIX_FMT_XV30;
#if QSV_VERSION_ATLEAST(1, 31)
    case AV_PIX_FMT_P012:
        *fourcc = MFX_FOURCC_P016;
        *shift = 1;
        return AV_PIX_FMT_P012;
    case AV_PIX_FMT_Y212:
        *fourcc = MFX_FOURCC_Y216;
        *shift = 1;
        return AV_PIX_FMT_Y212;
    case AV_PIX_FMT_XV36:
        *fourcc = MFX_FOURCC_Y416;
        *shift = 1;
        return AV_PIX_FMT_XV36;
#endif
#endif
    default:
        return AVERROR(ENOSYS);
    }
}

int ff_qsv_map_frame_to_surface(const AVFrame *frame, mfxFrameSurface1 *surface)
{
    switch (frame->format) {
    case AV_PIX_FMT_NV12:
    case AV_PIX_FMT_P010:
    case AV_PIX_FMT_P012:
        surface->Data.Y  = frame->data[0];
        surface->Data.UV = frame->data[1];
        /* The SDK checks Data.V when using system memory for VP9 encoding */
        surface->Data.V  = surface->Data.UV + 1;
        break;
    case AV_PIX_FMT_X2RGB10LE:
    case AV_PIX_FMT_BGRA:
        surface->Data.B = frame->data[0];
        surface->Data.G = frame->data[0] + 1;
        surface->Data.R = frame->data[0] + 2;
        surface->Data.A = frame->data[0] + 3;
        break;
    case AV_PIX_FMT_YUYV422:
        surface->Data.Y = frame->data[0];
        surface->Data.U = frame->data[0] + 1;
        surface->Data.V = frame->data[0] + 3;
        break;

    case AV_PIX_FMT_Y210:
    case AV_PIX_FMT_Y212:
        surface->Data.Y16 = (mfxU16 *)frame->data[0];
        surface->Data.U16 = (mfxU16 *)frame->data[0] + 1;
        surface->Data.V16 = (mfxU16 *)frame->data[0] + 3;
        break;

    case AV_PIX_FMT_VUYX:
        surface->Data.V = frame->data[0];
        surface->Data.U = frame->data[0] + 1;
        surface->Data.Y = frame->data[0] + 2;
        // Only set Data.A to a valid address, the SDK doesn't
        // use the value from the frame.
        surface->Data.A = frame->data[0] + 3;
        break;

    case AV_PIX_FMT_XV30:
        surface->Data.U = frame->data[0];
        break;

    case AV_PIX_FMT_XV36:
        surface->Data.U = frame->data[0];
        surface->Data.Y = frame->data[0] + 2;
        surface->Data.V = frame->data[0] + 4;
        // Only set Data.A to a valid address, the SDK doesn't
        // use the value from the frame.
        surface->Data.A = frame->data[0] + 6;
        break;

    default:
        return AVERROR(ENOSYS);
    }
    surface->Data.PitchLow  = frame->linesize[0];

    return 0;
}

int ff_qsv_find_surface_idx(QSVFramesContext *ctx, QSVFrame *frame)
{
    int i;
    for (i = 0; i < ctx->nb_mids; i++) {
        QSVMid *mid = &ctx->mids[i];
        mfxHDLPair *pair = (mfxHDLPair*)frame->surface.Data.MemId;
        if ((mid->handle_pair->first == pair->first) &&
            (mid->handle_pair->second == pair->second))
            return i;
    }
    return AVERROR_BUG;
}

enum AVFieldOrder ff_qsv_map_picstruct(int mfx_pic_struct)
{
    enum AVFieldOrder field = AV_FIELD_UNKNOWN;
    switch (mfx_pic_struct & 0xF) {
    case MFX_PICSTRUCT_PROGRESSIVE:
        field = AV_FIELD_PROGRESSIVE;
        break;
    case MFX_PICSTRUCT_FIELD_TFF:
        field = AV_FIELD_TT;
        break;
    case MFX_PICSTRUCT_FIELD_BFF:
        field = AV_FIELD_BB;
        break;
    }

    return field;
}

enum AVPictureType ff_qsv_map_pictype(int mfx_pic_type)
{
    enum AVPictureType type;
    switch (mfx_pic_type & 0x7) {
    case MFX_FRAMETYPE_I:
        if (mfx_pic_type & MFX_FRAMETYPE_S)
            type = AV_PICTURE_TYPE_SI;
        else
            type = AV_PICTURE_TYPE_I;
        break;
    case MFX_FRAMETYPE_B:
        type = AV_PICTURE_TYPE_B;
        break;
    case MFX_FRAMETYPE_P:
        if (mfx_pic_type & MFX_FRAMETYPE_S)
            type = AV_PICTURE_TYPE_SP;
        else
            type = AV_PICTURE_TYPE_P;
        break;
    case MFX_FRAMETYPE_UNKNOWN:
        type = AV_PICTURE_TYPE_NONE;
        break;
    default:
        av_assert0(0);
    }

    return type;
}

static int qsv_load_plugins(mfxSession session, const char *load_plugins,
                            void *logctx)
{
#if QSV_HAVE_USER_PLUGIN
    if (!load_plugins || !*load_plugins)
        return 0;

    while (*load_plugins) {
        mfxPluginUID uid;
        mfxStatus ret;
        int i, err = 0;

        char *plugin = av_get_token(&load_plugins, ":");
        if (!plugin)
            return AVERROR(ENOMEM);
        if (strlen(plugin) != 2 * sizeof(uid.Data)) {
            av_log(logctx, AV_LOG_ERROR, "Invalid plugin UID length\n");
            err = AVERROR(EINVAL);
            goto load_plugin_fail;
        }

        for (i = 0; i < sizeof(uid.Data); i++) {
            err = sscanf(plugin + 2 * i, "%2hhx", uid.Data + i);
            if (err != 1) {
                av_log(logctx, AV_LOG_ERROR, "Invalid plugin UID\n");
                err = AVERROR(EINVAL);
                goto load_plugin_fail;
            }

        }

        ret = MFXVideoUSER_Load(session, &uid, 1);
        if (ret < 0) {
            char errorbuf[128];
            snprintf(errorbuf, sizeof(errorbuf),
                     "Could not load the requested plugin '%s'", plugin);
            err = ff_qsv_print_error(logctx, ret, errorbuf);
            goto load_plugin_fail;
        }

        if (*load_plugins)
            load_plugins++;
load_plugin_fail:
        av_freep(&plugin);
        if (err < 0)
            return err;
    }
#endif

    return 0;

}

//This code is only required for Linux since a display handle is required.
//For Windows the session is complete and ready to use.

#ifdef AVCODEC_QSV_LINUX_SESSION_HANDLE
static int ff_qsv_set_display_handle(AVCodecContext *avctx, QSVSession *qs)
{
    AVDictionary *child_device_opts = NULL;
    AVVAAPIDeviceContext *hwctx;
    int ret;

    av_dict_set(&child_device_opts, "kernel_driver", "i915", 0);
    av_dict_set(&child_device_opts, "driver",        "iHD",  0);

    ret = av_hwdevice_ctx_create(&qs->va_device_ref, AV_HWDEVICE_TYPE_VAAPI, NULL, child_device_opts, 0);
    av_dict_free(&child_device_opts);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to create a VAAPI device.\n");
        return ret;
    } else {
        qs->va_device_ctx = (AVHWDeviceContext*)qs->va_device_ref->data;
        hwctx = qs->va_device_ctx->hwctx;

        ret = MFXVideoCORE_SetHandle(qs->session,
                (mfxHandleType)MFX_HANDLE_VA_DISPLAY, (mfxHDL)hwctx->display);
        if (ret < 0) {
            return ff_qsv_print_error(avctx, ret, "Error during set display handle\n");
        }
    }

    return 0;
}
#endif //AVCODEC_QSV_LINUX_SESSION_HANDLE

#if QSV_ONEVPL
static int qsv_new_mfx_loader(AVCodecContext *avctx,
                              mfxIMPL implementation,
                              mfxVersion *pver,
                              void **ploader)
{
    mfxStatus sts;
    mfxLoader loader = NULL;
    mfxConfig cfg;
    mfxVariant impl_value;

    loader = MFXLoad();
    if (!loader) {
        av_log(avctx, AV_LOG_ERROR, "Error creating a MFX loader\n");
        goto fail;
    }

    /* Create configurations for implementation */
    cfg = MFXCreateConfig(loader);
    if (!cfg) {
        av_log(avctx, AV_LOG_ERROR, "Error creating a MFX configurations\n");
        goto fail;
    }

    impl_value.Type = MFX_VARIANT_TYPE_U32;
    impl_value.Data.U32 = (implementation == MFX_IMPL_SOFTWARE) ?
        MFX_IMPL_TYPE_SOFTWARE : MFX_IMPL_TYPE_HARDWARE;
    sts = MFXSetConfigFilterProperty(cfg,
                                     (const mfxU8 *)"mfxImplDescription.Impl", impl_value);
    if (sts != MFX_ERR_NONE) {
        av_log(avctx, AV_LOG_ERROR, "Error adding a MFX configuration "
               "property: %d\n", sts);
        goto fail;
    }

    impl_value.Type = MFX_VARIANT_TYPE_U32;
    impl_value.Data.U32 = pver->Version;
    sts = MFXSetConfigFilterProperty(cfg,
                                     (const mfxU8 *)"mfxImplDescription.ApiVersion.Version",
                                     impl_value);
    if (sts != MFX_ERR_NONE) {
        av_log(avctx, AV_LOG_ERROR, "Error adding a MFX configuration "
               "property: %d\n", sts);
        goto fail;
    }

    *ploader = loader;

    return 0;

fail:
    if (loader)
        MFXUnload(loader);

    *ploader = NULL;
    return AVERROR_UNKNOWN;
}

static int qsv_create_mfx_session_from_loader(void *ctx, mfxLoader loader, mfxSession *psession)
{
    mfxStatus sts;
    mfxSession session = NULL;
    uint32_t impl_idx = 0;

    while (1) {
        /* Enumerate all implementations */
        mfxImplDescription *impl_desc;

        sts = MFXEnumImplementations(loader, impl_idx,
                                     MFX_IMPLCAPS_IMPLDESCSTRUCTURE,
                                     (mfxHDL *)&impl_desc);
        /* Failed to find an available implementation */
        if (sts == MFX_ERR_NOT_FOUND)
            break;
        else if (sts != MFX_ERR_NONE) {
            impl_idx++;
            continue;
        }

        sts = MFXCreateSession(loader, impl_idx, &session);
        MFXDispReleaseImplDescription(loader, impl_desc);
        if (sts == MFX_ERR_NONE)
            break;

        impl_idx++;
    }

    if (sts != MFX_ERR_NONE) {
        av_log(ctx, AV_LOG_ERROR, "Error creating a MFX session: %d.\n", sts);
        goto fail;
    }

    *psession = session;

    return 0;

fail:
    if (session)
        MFXClose(session);

    *psession = NULL;
    return AVERROR_UNKNOWN;
}

static int qsv_create_mfx_session(AVCodecContext *avctx,
                                  mfxIMPL implementation,
                                  mfxVersion *pver,
                                  int gpu_copy,
                                  mfxSession *psession,
                                  void **ploader)
{
    mfxLoader loader = NULL;

    /* Don't create a new MFX loader if the input loader is valid */
    if (*ploader == NULL) {
        av_log(avctx, AV_LOG_VERBOSE,
               "Use Intel(R) oneVPL to create MFX session, the required "
               "implementation version is %d.%d\n",
               pver->Major, pver->Minor);

        if (qsv_new_mfx_loader(avctx, implementation, pver, (void **)&loader))
            goto fail;

        av_assert0(loader);
    } else {
        av_log(avctx, AV_LOG_VERBOSE,
               "Use Intel(R) oneVPL to create MFX session with the specified MFX loader\n");

        loader = *ploader;
    }

    if (qsv_create_mfx_session_from_loader(avctx, loader, psession))
        goto fail;

    if (!*ploader)
        *ploader = loader;

    return 0;

fail:
    if (!*ploader && loader)
        MFXUnload(loader);

    return AVERROR_UNKNOWN;
}

#else

static int qsv_create_mfx_session(AVCodecContext *avctx,
                                  mfxIMPL implementation,
                                  mfxVersion *pver,
                                  int gpu_copy,
                                  mfxSession *psession,
                                  void **ploader)
{
    mfxInitParam init_par = { MFX_IMPL_AUTO_ANY };
    mfxSession session = NULL;
    mfxStatus sts;

    av_log(avctx, AV_LOG_VERBOSE,
           "Use Intel(R) Media SDK to create MFX session, the required "
           "implementation version is %d.%d\n",
           pver->Major, pver->Minor);

    *psession = NULL;
    *ploader = NULL;

    init_par.GPUCopy = gpu_copy;
    init_par.Implementation = implementation;
    init_par.Version = *pver;
    sts = MFXInitEx(init_par, &session);
    if (sts < 0)
        return ff_qsv_print_error(avctx, sts,
                                  "Error initializing a MFX session");
    else if (sts > 0) {
        ff_qsv_print_warning(avctx, sts,
                             "Warning in MFX initialization");
        return AVERROR_UNKNOWN;
    }

    *psession = session;

    return 0;
}

#endif

int ff_qsv_init_internal_session(AVCodecContext *avctx, QSVSession *qs,
                                 const char *load_plugins, int gpu_copy)
{
#if CONFIG_D3D11VA
    mfxIMPL          impl = MFX_IMPL_AUTO_ANY | MFX_IMPL_VIA_D3D11;
#else
    mfxIMPL          impl = MFX_IMPL_AUTO_ANY;
#endif
    mfxVersion        ver = { { QSV_VERSION_MINOR, QSV_VERSION_MAJOR } };

    const char *desc;
    int ret = qsv_create_mfx_session(avctx, impl, &ver, gpu_copy, &qs->session,
                                     &qs->loader);
    if (ret)
        return ret;

#ifdef AVCODEC_QSV_LINUX_SESSION_HANDLE
    ret = ff_qsv_set_display_handle(avctx, qs);
    if (ret < 0)
        return ret;
#endif

    ret = qsv_load_plugins(qs->session, load_plugins, avctx);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Error loading plugins\n");
        return ret;
    }

    ret = MFXQueryIMPL(qs->session, &impl);
    if (ret != MFX_ERR_NONE)
        return ff_qsv_print_error(avctx, ret,
                                  "Error querying the session attributes");

    switch (MFX_IMPL_BASETYPE(impl)) {
    case MFX_IMPL_SOFTWARE:
        desc = "software";
        break;
    case MFX_IMPL_HARDWARE:
    case MFX_IMPL_HARDWARE2:
    case MFX_IMPL_HARDWARE3:
    case MFX_IMPL_HARDWARE4:
        desc = "hardware accelerated";
        break;
    default:
        desc = "unknown";
    }

    av_log(avctx, AV_LOG_VERBOSE,
           "Initialized an internal MFX session using %s implementation\n",
           desc);

    return 0;
}

static void mids_buf_free(void *opaque, uint8_t *data)
{
    AVBufferRef *hw_frames_ref = opaque;
    av_buffer_unref(&hw_frames_ref);
    av_freep(&data);
}

static AVBufferRef *qsv_create_mids(AVBufferRef *hw_frames_ref)
{
    AVHWFramesContext    *frames_ctx = (AVHWFramesContext*)hw_frames_ref->data;
    AVQSVFramesContext *frames_hwctx = frames_ctx->hwctx;
    int                  nb_surfaces = frames_hwctx->nb_surfaces;

    AVBufferRef *mids_buf, *hw_frames_ref1;
    QSVMid *mids;
    int i;

    hw_frames_ref1 = av_buffer_ref(hw_frames_ref);
    if (!hw_frames_ref1)
        return NULL;

    mids = av_calloc(nb_surfaces, sizeof(*mids));
    if (!mids) {
        av_buffer_unref(&hw_frames_ref1);
        return NULL;
    }

    mids_buf = av_buffer_create((uint8_t*)mids, nb_surfaces * sizeof(*mids),
                                mids_buf_free, hw_frames_ref1, 0);
    if (!mids_buf) {
        av_buffer_unref(&hw_frames_ref1);
        av_freep(&mids);
        return NULL;
    }

    for (i = 0; i < nb_surfaces; i++) {
        QSVMid *mid = &mids[i];
        mid->handle_pair   = (mfxHDLPair*)frames_hwctx->surfaces[i].Data.MemId;
        mid->hw_frames_ref = hw_frames_ref1;
    }

    return mids_buf;
}

static int qsv_setup_mids(mfxFrameAllocResponse *resp, AVBufferRef *hw_frames_ref,
                          AVBufferRef *mids_buf)
{
    AVHWFramesContext    *frames_ctx = (AVHWFramesContext*)hw_frames_ref->data;
    AVQSVFramesContext *frames_hwctx = frames_ctx->hwctx;
    QSVMid                     *mids = (QSVMid*)mids_buf->data;
    int                  nb_surfaces = frames_hwctx->nb_surfaces;
    int i;

    // the allocated size of the array is two larger than the number of
    // surfaces, we store the references to the frames context and the
    // QSVMid array there
    resp->mids = av_calloc(nb_surfaces + 2, sizeof(*resp->mids));
    if (!resp->mids)
        return AVERROR(ENOMEM);

    for (i = 0; i < nb_surfaces; i++)
        resp->mids[i] = &mids[i];
    resp->NumFrameActual = nb_surfaces;

    resp->mids[resp->NumFrameActual] = (mfxMemId)av_buffer_ref(hw_frames_ref);
    if (!resp->mids[resp->NumFrameActual]) {
        av_freep(&resp->mids);
        return AVERROR(ENOMEM);
    }

    resp->mids[resp->NumFrameActual + 1] = av_buffer_ref(mids_buf);
    if (!resp->mids[resp->NumFrameActual + 1]) {
        av_buffer_unref((AVBufferRef**)&resp->mids[resp->NumFrameActual]);
        av_freep(&resp->mids);
        return AVERROR(ENOMEM);
    }

    return 0;
}

static mfxStatus qsv_frame_alloc(mfxHDL pthis, mfxFrameAllocRequest *req,
                                 mfxFrameAllocResponse *resp)
{
    QSVFramesContext *ctx = pthis;
    int ret;

    /* this should only be called from an encoder or decoder and
     * only allocates video memory frames */
    if (!(req->Type & (MFX_MEMTYPE_VIDEO_MEMORY_DECODER_TARGET |
                       MFX_MEMTYPE_VIDEO_MEMORY_PROCESSOR_TARGET))         ||
        !(req->Type & (MFX_MEMTYPE_FROM_DECODE | MFX_MEMTYPE_FROM_ENCODE)))
        return MFX_ERR_UNSUPPORTED;

    if (req->Type & MFX_MEMTYPE_EXTERNAL_FRAME) {
        /* external frames -- fill from the caller-supplied frames context */
        AVHWFramesContext *frames_ctx = (AVHWFramesContext*)ctx->hw_frames_ctx->data;
        AVQSVFramesContext *frames_hwctx = frames_ctx->hwctx;
        mfxFrameInfo      *i  = &req->Info;
        mfxFrameInfo      *i1 = &frames_hwctx->surfaces[0].Info;

        if (i->Width  > i1->Width  || i->Height > i1->Height ||
            i->FourCC != i1->FourCC || i->ChromaFormat != i1->ChromaFormat) {
            av_log(ctx->logctx, AV_LOG_ERROR, "Mismatching surface properties in an "
                   "allocation request: %dx%d %d %d vs %dx%d %d %d\n",
                   i->Width,  i->Height,  i->FourCC,  i->ChromaFormat,
                   i1->Width, i1->Height, i1->FourCC, i1->ChromaFormat);
            return MFX_ERR_UNSUPPORTED;
        }

        ret = qsv_setup_mids(resp, ctx->hw_frames_ctx, ctx->mids_buf);
        if (ret < 0) {
            av_log(ctx->logctx, AV_LOG_ERROR,
                   "Error filling an external frame allocation request\n");
            return MFX_ERR_MEMORY_ALLOC;
        }
    } else if (req->Type & MFX_MEMTYPE_INTERNAL_FRAME) {
        /* internal frames -- allocate a new hw frames context */
        AVHWFramesContext *ext_frames_ctx = (AVHWFramesContext*)ctx->hw_frames_ctx->data;
        mfxFrameInfo      *i  = &req->Info;

        AVBufferRef *frames_ref, *mids_buf;
        AVHWFramesContext *frames_ctx;
        AVQSVFramesContext *frames_hwctx;

        frames_ref = av_hwframe_ctx_alloc(ext_frames_ctx->device_ref);
        if (!frames_ref)
            return MFX_ERR_MEMORY_ALLOC;

        frames_ctx   = (AVHWFramesContext*)frames_ref->data;
        frames_hwctx = frames_ctx->hwctx;

        frames_ctx->format            = AV_PIX_FMT_QSV;
        frames_ctx->sw_format         = ff_qsv_map_fourcc(i->FourCC);
        frames_ctx->width             = i->Width;
        frames_ctx->height            = i->Height;
        frames_ctx->initial_pool_size = req->NumFrameSuggested;

        frames_hwctx->frame_type      = req->Type;

        ret = av_hwframe_ctx_init(frames_ref);
        if (ret < 0) {
            av_log(ctx->logctx, AV_LOG_ERROR,
                   "Error initializing a frames context for an internal frame "
                   "allocation request\n");
            av_buffer_unref(&frames_ref);
            return MFX_ERR_MEMORY_ALLOC;
        }

        mids_buf = qsv_create_mids(frames_ref);
        if (!mids_buf) {
            av_buffer_unref(&frames_ref);
            return MFX_ERR_MEMORY_ALLOC;
        }

        ret = qsv_setup_mids(resp, frames_ref, mids_buf);
        av_buffer_unref(&mids_buf);
        av_buffer_unref(&frames_ref);
        if (ret < 0) {
            av_log(ctx->logctx, AV_LOG_ERROR,
                   "Error filling an internal frame allocation request\n");
            return MFX_ERR_MEMORY_ALLOC;
        }
    } else {
        return MFX_ERR_UNSUPPORTED;
    }

    return MFX_ERR_NONE;
}

static mfxStatus qsv_frame_free(mfxHDL pthis, mfxFrameAllocResponse *resp)
{
    av_buffer_unref((AVBufferRef**)&resp->mids[resp->NumFrameActual]);
    av_buffer_unref((AVBufferRef**)&resp->mids[resp->NumFrameActual + 1]);
    av_freep(&resp->mids);
    return MFX_ERR_NONE;
}

static mfxStatus qsv_frame_lock(mfxHDL pthis, mfxMemId mid, mfxFrameData *ptr)
{
    QSVMid *qsv_mid = mid;
    AVHWFramesContext *hw_frames_ctx = (AVHWFramesContext*)qsv_mid->hw_frames_ref->data;
    AVQSVFramesContext *hw_frames_hwctx = hw_frames_ctx->hwctx;
    int ret;

    if (qsv_mid->locked_frame)
        return MFX_ERR_UNDEFINED_BEHAVIOR;

    /* Allocate a system memory frame that will hold the mapped data. */
    qsv_mid->locked_frame = av_frame_alloc();
    if (!qsv_mid->locked_frame)
        return MFX_ERR_MEMORY_ALLOC;
    qsv_mid->locked_frame->format  = hw_frames_ctx->sw_format;

    /* wrap the provided handle in a hwaccel AVFrame */
    qsv_mid->hw_frame = av_frame_alloc();
    if (!qsv_mid->hw_frame)
        goto fail;

    qsv_mid->hw_frame->data[3] = (uint8_t*)&qsv_mid->surf;
    qsv_mid->hw_frame->format  = AV_PIX_FMT_QSV;

    // doesn't really matter what buffer is used here
    qsv_mid->hw_frame->buf[0]  = av_buffer_alloc(1);
    if (!qsv_mid->hw_frame->buf[0])
        goto fail;

    qsv_mid->hw_frame->width   = hw_frames_ctx->width;
    qsv_mid->hw_frame->height  = hw_frames_ctx->height;

    qsv_mid->hw_frame->hw_frames_ctx = av_buffer_ref(qsv_mid->hw_frames_ref);
    if (!qsv_mid->hw_frame->hw_frames_ctx)
        goto fail;

    qsv_mid->surf.Info = hw_frames_hwctx->surfaces[0].Info;
    qsv_mid->surf.Data.MemId = qsv_mid->handle_pair;

    /* map the data to the system memory */
    ret = av_hwframe_map(qsv_mid->locked_frame, qsv_mid->hw_frame,
                         AV_HWFRAME_MAP_DIRECT);
    if (ret < 0)
        goto fail;

    ptr->Pitch = qsv_mid->locked_frame->linesize[0];
    ptr->Y     = qsv_mid->locked_frame->data[0];
    ptr->U     = qsv_mid->locked_frame->data[1];
    ptr->V     = qsv_mid->locked_frame->data[1] + 1;

    return MFX_ERR_NONE;
fail:
    av_frame_free(&qsv_mid->hw_frame);
    av_frame_free(&qsv_mid->locked_frame);
    return MFX_ERR_MEMORY_ALLOC;
}

static mfxStatus qsv_frame_unlock(mfxHDL pthis, mfxMemId mid, mfxFrameData *ptr)
{
    QSVMid *qsv_mid = mid;

    av_frame_free(&qsv_mid->locked_frame);
    av_frame_free(&qsv_mid->hw_frame);

    return MFX_ERR_NONE;
}

static mfxStatus qsv_frame_get_hdl(mfxHDL pthis, mfxMemId mid, mfxHDL *hdl)
{
    QSVMid *qsv_mid = (QSVMid*)mid;
    mfxHDLPair *pair_dst = (mfxHDLPair*)hdl;
    mfxHDLPair *pair_src = (mfxHDLPair*)qsv_mid->handle_pair;

    pair_dst->first = pair_src->first;

    if (pair_src->second != (mfxMemId)MFX_INFINITE)
        pair_dst->second = pair_src->second;
    return MFX_ERR_NONE;
}

int ff_qsv_init_session_device(AVCodecContext *avctx, mfxSession *psession,
                               AVBufferRef *device_ref, const char *load_plugins,
                               int gpu_copy)
{
    AVHWDeviceContext    *device_ctx = (AVHWDeviceContext*)device_ref->data;
    AVQSVDeviceContext *device_hwctx = device_ctx->hwctx;
    mfxSession        parent_session = device_hwctx->session;
    void                     *loader = device_hwctx->loader;
    mfxHDL                    handle = NULL;
    int          hw_handle_supported = 0;

    mfxSession    session;
    mfxVersion    ver;
    mfxIMPL       impl;
    mfxHandleType handle_type;
    mfxStatus err;
    int ret;

    err = MFXQueryIMPL(parent_session, &impl);
    if (err == MFX_ERR_NONE)
        err = MFXQueryVersion(parent_session, &ver);
    if (err != MFX_ERR_NONE)
        return ff_qsv_print_error(avctx, err,
                                  "Error querying the session attributes");

    if (MFX_IMPL_VIA_VAAPI == MFX_IMPL_VIA_MASK(impl)) {
        handle_type = MFX_HANDLE_VA_DISPLAY;
        hw_handle_supported = 1;
    } else if (MFX_IMPL_VIA_D3D11 == MFX_IMPL_VIA_MASK(impl)) {
        handle_type = MFX_HANDLE_D3D11_DEVICE;
        hw_handle_supported = 1;
    } else if (MFX_IMPL_VIA_D3D9 == MFX_IMPL_VIA_MASK(impl)) {
        handle_type = MFX_HANDLE_D3D9_DEVICE_MANAGER;
        hw_handle_supported = 1;
    }

    if (hw_handle_supported) {
        err = MFXVideoCORE_GetHandle(parent_session, handle_type, &handle);
        if (err != MFX_ERR_NONE) {
            return ff_qsv_print_error(avctx, err,
                                  "Error getting handle session");
        }
    }
    if (!handle) {
        av_log(avctx, AV_LOG_VERBOSE, "No supported hw handle could be retrieved "
               "from the session\n");
    }

    ret = qsv_create_mfx_session(avctx, impl, &ver, gpu_copy, &session,
                                 &loader);
    if (ret)
        return ret;

    if (handle) {
        err = MFXVideoCORE_SetHandle(session, handle_type, handle);
        if (err != MFX_ERR_NONE)
            return ff_qsv_print_error(avctx, err,
                                      "Error setting a HW handle");
    }

    if (QSV_RUNTIME_VERSION_ATLEAST(ver, 1, 25)) {
        err = MFXJoinSession(parent_session, session);
        if (err != MFX_ERR_NONE)
            return ff_qsv_print_error(avctx, err,
                                      "Error joining session");
    }

    ret = qsv_load_plugins(session, load_plugins, avctx);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Error loading plugins\n");
        return ret;
    }

    *psession = session;
    return 0;
}

int ff_qsv_init_session_frames(AVCodecContext *avctx, mfxSession *psession,
                               QSVFramesContext *qsv_frames_ctx,
                               const char *load_plugins, int opaque, int gpu_copy)
{
    mfxFrameAllocator frame_allocator = {
        .pthis  = qsv_frames_ctx,
        .Alloc  = qsv_frame_alloc,
        .Lock   = qsv_frame_lock,
        .Unlock = qsv_frame_unlock,
        .GetHDL = qsv_frame_get_hdl,
        .Free   = qsv_frame_free,
    };

    AVHWFramesContext    *frames_ctx = (AVHWFramesContext*)qsv_frames_ctx->hw_frames_ctx->data;
    AVQSVFramesContext *frames_hwctx = frames_ctx->hwctx;

    mfxSession    session;
    mfxStatus err;

    int ret;

    ret = ff_qsv_init_session_device(avctx, &session,
                                     frames_ctx->device_ref, load_plugins, gpu_copy);
    if (ret < 0)
        return ret;

    if (!opaque) {
        qsv_frames_ctx->logctx = avctx;

        /* allocate the memory ids for the external frames */
        av_buffer_unref(&qsv_frames_ctx->mids_buf);
        qsv_frames_ctx->mids_buf = qsv_create_mids(qsv_frames_ctx->hw_frames_ctx);
        if (!qsv_frames_ctx->mids_buf)
            return AVERROR(ENOMEM);
        qsv_frames_ctx->mids    = (QSVMid*)qsv_frames_ctx->mids_buf->data;
        qsv_frames_ctx->nb_mids = frames_hwctx->nb_surfaces;

        err = MFXVideoCORE_SetFrameAllocator(session, &frame_allocator);
        if (err != MFX_ERR_NONE)
            return ff_qsv_print_error(avctx, err,
                                      "Error setting a frame allocator");
    }

    *psession = session;
    return 0;
}

int ff_qsv_close_internal_session(QSVSession *qs)
{
    if (qs->session) {
        MFXClose(qs->session);
        qs->session = NULL;
    }

    if (qs->loader) {
        MFXUnload(qs->loader);
        qs->loader = NULL;
    }

#ifdef AVCODEC_QSV_LINUX_SESSION_HANDLE
    av_buffer_unref(&qs->va_device_ref);
#endif
    return 0;
}

void ff_qsv_frame_add_ext_param (AVCodecContext *avctx, QSVFrame *frame,
                                 mfxExtBuffer * param)
{
    int i;

    for (i = 0; i < frame->num_ext_params; i++) {
        mfxExtBuffer *ext_buffer = frame->ext_param[i];

        if (ext_buffer->BufferId == param->BufferId) {
            av_log(avctx, AV_LOG_WARNING, "A buffer with the same type has been "
                   "added\n");
            return;
        }
    }

    if (frame->num_ext_params < QSV_MAX_FRAME_EXT_PARAMS) {
        frame->ext_param[frame->num_ext_params] = param;
        frame->num_ext_params++;
        frame->surface.Data.NumExtParam = frame->num_ext_params;
    } else {
        av_log(avctx, AV_LOG_WARNING, "Ignore this extra buffer because do not "
               "have enough space\n");
    }


}
