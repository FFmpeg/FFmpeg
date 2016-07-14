/*
 * Intel MediaSDK QSV encoder/decoder shared code
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

#include <mfx/mfxvideo.h>
#include <mfx/mfxplugin.h>

#include <stdio.h>
#include <string.h>

#include "libavutil/avstring.h"
#include "libavutil/common.h"
#include "libavutil/error.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_qsv.h"

#include "avcodec.h"
#include "qsv_internal.h"

int ff_qsv_codec_id_to_mfx(enum AVCodecID codec_id)
{
    switch (codec_id) {
    case AV_CODEC_ID_H264:
        return MFX_CODEC_AVC;
#if QSV_VERSION_ATLEAST(1, 8)
    case AV_CODEC_ID_HEVC:
        return MFX_CODEC_HEVC;
#endif
    case AV_CODEC_ID_MPEG1VIDEO:
    case AV_CODEC_ID_MPEG2VIDEO:
        return MFX_CODEC_MPEG2;
    case AV_CODEC_ID_VC1:
        return MFX_CODEC_VC1;
    default:
        break;
    }

    return AVERROR(ENOSYS);
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
    { MFX_ERR_MORE_DATA,                AVERROR(EAGAIN), "expect more data at input"            },
    { MFX_ERR_MORE_SURFACE,             AVERROR(EAGAIN), "expect more surface at output"        },
    { MFX_ERR_ABORTED,                  AVERROR_UNKNOWN, "operation aborted"                    },
    { MFX_ERR_DEVICE_LOST,              AVERROR(EIO),    "device lost"                          },
    { MFX_ERR_INCOMPATIBLE_VIDEO_PARAM, AVERROR(EINVAL), "incompatible video parameters"        },
    { MFX_ERR_INVALID_VIDEO_PARAM,      AVERROR(EINVAL), "invalid video parameters"             },
    { MFX_ERR_UNDEFINED_BEHAVIOR,       AVERROR_BUG,     "undefined behavior"                   },
    { MFX_ERR_DEVICE_FAILED,            AVERROR(EIO),    "device failed"                        },
    { MFX_ERR_MORE_BITSTREAM,           AVERROR(EAGAIN), "expect more bitstream at output"      },
    { MFX_ERR_INCOMPATIBLE_AUDIO_PARAM, AVERROR(EINVAL), "incompatible audio parameters"        },
    { MFX_ERR_INVALID_AUDIO_PARAM,      AVERROR(EINVAL), "invalid audio parameters"             },

    { MFX_WRN_IN_EXECUTION,             0,               "operation in execution"               },
    { MFX_WRN_DEVICE_BUSY,              0,               "device busy"                          },
    { MFX_WRN_VIDEO_PARAM_CHANGED,      0,               "video parameters changed"             },
    { MFX_WRN_PARTIAL_ACCELERATION,     0,               "partial acceleration"                 },
    { MFX_WRN_INCOMPATIBLE_VIDEO_PARAM, 0,               "incompatible video parameters"        },
    { MFX_WRN_VALUE_NOT_CHANGED,        0,               "value is saturated"                   },
    { MFX_WRN_OUT_OF_RANGE,             0,               "value out of range"                   },
    { MFX_WRN_FILTER_SKIPPED,           0,               "filter skipped"                       },
    { MFX_WRN_INCOMPATIBLE_AUDIO_PARAM, 0,               "incompatible audio parameters"        },
};

int ff_qsv_map_error(mfxStatus mfx_err, const char **desc)
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
    int ret;
    ret = ff_qsv_map_error(err, &desc);
    av_log(log_ctx, AV_LOG_ERROR, "%s: %s (%d)\n", error_string, desc, err);
    return ret;
}

int ff_qsv_print_warning(void *log_ctx, mfxStatus err,
                         const char *warning_string)
{
    const char *desc;
    int ret;
    ret = ff_qsv_map_error(err, &desc);
    av_log(log_ctx, AV_LOG_WARNING, "%s: %s (%d)\n", warning_string, desc, err);
    return ret;
}

int ff_qsv_map_pixfmt(enum AVPixelFormat format, uint32_t *fourcc)
{
    switch (format) {
    case AV_PIX_FMT_YUV420P:
    case AV_PIX_FMT_YUVJ420P:
    case AV_PIX_FMT_NV12:
        *fourcc = MFX_FOURCC_NV12;
        return AV_PIX_FMT_NV12;
    case AV_PIX_FMT_YUV420P10:
    case AV_PIX_FMT_P010:
        *fourcc = MFX_FOURCC_P010;
        return AV_PIX_FMT_P010;
    default:
        return AVERROR(ENOSYS);
    }
}

static int qsv_load_plugins(mfxSession session, const char *load_plugins,
                            void *logctx)
{
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

    return 0;

}

int ff_qsv_init_internal_session(AVCodecContext *avctx, mfxSession *session,
                                 const char *load_plugins)
{
    mfxIMPL impl   = MFX_IMPL_AUTO_ANY;
    mfxVersion ver = { { QSV_VERSION_MINOR, QSV_VERSION_MAJOR } };

    const char *desc;
    int ret;

    ret = MFXInit(impl, &ver, session);
    if (ret < 0)
        return ff_qsv_print_error(avctx, ret,
                                  "Error initializing an internal MFX session");

    ret = qsv_load_plugins(*session, load_plugins, avctx);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Error loading plugins\n");
        return ret;
    }

    MFXQueryIMPL(*session, &impl);

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

static mfxStatus qsv_frame_alloc(mfxHDL pthis, mfxFrameAllocRequest *req,
                                 mfxFrameAllocResponse *resp)
{
    QSVFramesContext *ctx = pthis;
    mfxFrameInfo      *i  = &req->Info;
    mfxFrameInfo      *i1 = &ctx->info;

    if (!(req->Type & MFX_MEMTYPE_VIDEO_MEMORY_DECODER_TARGET) ||
        !(req->Type & (MFX_MEMTYPE_FROM_DECODE | MFX_MEMTYPE_FROM_ENCODE)) ||
        !(req->Type & MFX_MEMTYPE_EXTERNAL_FRAME))
        return MFX_ERR_UNSUPPORTED;
    if (i->Width  != i1->Width || i->Height != i1->Height ||
        i->FourCC != i1->FourCC || i->ChromaFormat != i1->ChromaFormat) {
        av_log(ctx, AV_LOG_ERROR, "Mismatching surface properties in an "
               "allocation request: %dx%d %d %d vs %dx%d %d %d\n",
               i->Width,  i->Height,  i->FourCC,  i->ChromaFormat,
               i1->Width, i1->Height, i1->FourCC, i1->ChromaFormat);
        return MFX_ERR_UNSUPPORTED;
    }

    resp->mids           = ctx->mids;
    resp->NumFrameActual = ctx->nb_mids;

    return MFX_ERR_NONE;
}

static mfxStatus qsv_frame_free(mfxHDL pthis, mfxFrameAllocResponse *resp)
{
    return MFX_ERR_NONE;
}

static mfxStatus qsv_frame_lock(mfxHDL pthis, mfxMemId mid, mfxFrameData *ptr)
{
    return MFX_ERR_UNSUPPORTED;
}

static mfxStatus qsv_frame_unlock(mfxHDL pthis, mfxMemId mid, mfxFrameData *ptr)
{
    return MFX_ERR_UNSUPPORTED;
}

static mfxStatus qsv_frame_get_hdl(mfxHDL pthis, mfxMemId mid, mfxHDL *hdl)
{
    *hdl = mid;
    return MFX_ERR_NONE;
}

int ff_qsv_init_session_hwcontext(AVCodecContext *avctx, mfxSession *psession,
                                  QSVFramesContext *qsv_frames_ctx,
                                  const char *load_plugins, int opaque)
{
    static const mfxHandleType handle_types[] = {
        MFX_HANDLE_VA_DISPLAY,
        MFX_HANDLE_D3D9_DEVICE_MANAGER,
        MFX_HANDLE_D3D11_DEVICE,
    };
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
    AVQSVDeviceContext *device_hwctx = frames_ctx->device_ctx->hwctx;
    mfxSession        parent_session = device_hwctx->session;

    mfxSession    session;
    mfxVersion    ver;
    mfxIMPL       impl;
    mfxHDL        handle = NULL;
    mfxHandleType handle_type;
    mfxStatus err;

    int i, ret;

    err = MFXQueryIMPL(parent_session, &impl);
    if (err == MFX_ERR_NONE)
        err = MFXQueryVersion(parent_session, &ver);
    if (err != MFX_ERR_NONE)
        return ff_qsv_print_error(avctx, err,
                                  "Error querying the session attributes");

    for (i = 0; i < FF_ARRAY_ELEMS(handle_types); i++) {
        err = MFXVideoCORE_GetHandle(parent_session, handle_types[i], &handle);
        if (err == MFX_ERR_NONE) {
            handle_type = handle_types[i];
            break;
        }
        handle = NULL;
    }
    if (!handle) {
        av_log(avctx, AV_LOG_VERBOSE, "No supported hw handle could be retrieved "
               "from the session\n");
    }

    err = MFXInit(impl, &ver, &session);
    if (err != MFX_ERR_NONE)
        return ff_qsv_print_error(avctx, err,
                                  "Error initializing a child MFX session");

    if (handle) {
        err = MFXVideoCORE_SetHandle(session, handle_type, handle);
        if (err != MFX_ERR_NONE)
            return ff_qsv_print_error(avctx, err,
                                      "Error setting a HW handle");
    }

    ret = qsv_load_plugins(session, load_plugins, avctx);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Error loading plugins\n");
        return ret;
    }

    if (!opaque) {
        av_freep(&qsv_frames_ctx->mids);
        qsv_frames_ctx->mids = av_mallocz_array(frames_hwctx->nb_surfaces,
                                                sizeof(*qsv_frames_ctx->mids));
        if (!qsv_frames_ctx->mids)
            return AVERROR(ENOMEM);

        qsv_frames_ctx->info    = frames_hwctx->surfaces[0].Info;
        qsv_frames_ctx->nb_mids = frames_hwctx->nb_surfaces;
        for (i = 0; i < frames_hwctx->nb_surfaces; i++)
            qsv_frames_ctx->mids[i] = frames_hwctx->surfaces[i].Data.MemId;

        err = MFXVideoCORE_SetFrameAllocator(session, &frame_allocator);
        if (err != MFX_ERR_NONE)
            return ff_qsv_print_error(avctx, err,
                                      "Error setting a frame allocator");
    }

    *psession = session;
    return 0;
}
