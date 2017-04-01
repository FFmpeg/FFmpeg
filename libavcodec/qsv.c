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

#include <mfx/mfxvideo.h>
#include <mfx/mfxplugin.h>

#include <stdio.h>
#include <string.h>

#include "libavutil/avstring.h"
#include "libavutil/common.h"
#include "libavutil/error.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_qsv.h"
#include "libavutil/imgutils.h"

#include "avcodec.h"
#include "qsv_internal.h"

#if QSV_VERSION_ATLEAST(1, 12)
#include "mfx/mfxvp8.h"
#endif

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
#if QSV_VERSION_ATLEAST(1, 12)
    case AV_CODEC_ID_VP8:
        return MFX_CODEC_VP8;
#endif
    default:
        break;
    }

    return AVERROR(ENOSYS);
}

int ff_qsv_profile_to_mfx(enum AVCodecID codec_id, int profile)
{
    if (profile == FF_PROFILE_UNKNOWN)
        return MFX_PROFILE_UNKNOWN;
    switch (codec_id) {
    case AV_CODEC_ID_H264:
    case AV_CODEC_ID_HEVC:
        return profile;
    case AV_CODEC_ID_VC1:
        return 4 * profile + 1;
    case AV_CODEC_ID_MPEG2VIDEO:
        return 0x10 * profile;
    }
    return MFX_PROFILE_UNKNOWN;
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

static enum AVPixelFormat qsv_map_fourcc(uint32_t fourcc)
{
    switch (fourcc) {
    case MFX_FOURCC_NV12: return AV_PIX_FMT_NV12;
    case MFX_FOURCC_P010: return AV_PIX_FMT_P010;
    case MFX_FOURCC_P8:   return AV_PIX_FMT_PAL8;
    }
    return AV_PIX_FMT_NONE;
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

int ff_qsv_find_surface_idx(QSVFramesContext *ctx, QSVFrame *frame)
{
    int i;
    for (i = 0; i < ctx->nb_mids; i++) {
        QSVMid *mid = &ctx->mids[i];
        if (mid->handle == frame->surface.Data.MemId)
            return i;
    }
    return AVERROR_BUG;
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

    mids = av_mallocz_array(nb_surfaces, sizeof(*mids));
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
        mid->handle        = frames_hwctx->surfaces[i].Data.MemId;
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
    resp->mids = av_mallocz_array(nb_surfaces + 2, sizeof(*resp->mids));
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

        if (i->Width  != i1->Width  || i->Height != i1->Height ||
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
        frames_ctx->sw_format         = qsv_map_fourcc(i->FourCC);
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
    qsv_mid->surf.Data.MemId = qsv_mid->handle;

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
    *hdl = qsv_mid->handle;
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
