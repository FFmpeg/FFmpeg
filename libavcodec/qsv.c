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
#include "libavutil/error.h"

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

int ff_qsv_error(int mfx_err)
{
    switch (mfx_err) {
    case MFX_ERR_NONE:
        return 0;
    case MFX_ERR_MEMORY_ALLOC:
    case MFX_ERR_NOT_ENOUGH_BUFFER:
        return AVERROR(ENOMEM);
    case MFX_ERR_INVALID_HANDLE:
        return AVERROR(EINVAL);
    case MFX_ERR_DEVICE_FAILED:
    case MFX_ERR_DEVICE_LOST:
    case MFX_ERR_LOCK_MEMORY:
        return AVERROR(EIO);
    case MFX_ERR_NULL_PTR:
    case MFX_ERR_UNDEFINED_BEHAVIOR:
    case MFX_ERR_NOT_INITIALIZED:
        return AVERROR_BUG;
    case MFX_ERR_UNSUPPORTED:
    case MFX_ERR_NOT_FOUND:
        return AVERROR(ENOSYS);
    case MFX_ERR_MORE_DATA:
    case MFX_ERR_MORE_SURFACE:
    case MFX_ERR_MORE_BITSTREAM:
        return AVERROR(EAGAIN);
    case MFX_ERR_INCOMPATIBLE_VIDEO_PARAM:
    case MFX_ERR_INVALID_VIDEO_PARAM:
        return AVERROR(EINVAL);
    case MFX_ERR_ABORTED:
    case MFX_ERR_UNKNOWN:
    default:
        return AVERROR_UNKNOWN;
    }
}
static int ff_qsv_set_display_handle(AVCodecContext *avctx, QSVSession *qs)
{
    // this code is only required for Linux.  It searches for a valid
    // display handle.  First in /dev/dri/renderD then in /dev/dri/card
#ifdef AVCODEC_QSV_LINUX_SESSION_HANDLE
    // VAAPI display handle
    int ret = 0;
    VADisplay va_dpy = NULL;
    VAStatus va_res = VA_STATUS_SUCCESS;
    int major_version = 0, minor_version = 0;
    int fd = -1;
    char adapterpath[256];
    int adapter_num;

    qs->fd_display = -1;
    qs->va_display = NULL;

    //search for valid graphics device
    for (adapter_num = 0;adapter_num < 6;adapter_num++) {

        if (adapter_num<3) {
            snprintf(adapterpath,sizeof(adapterpath),
                "/dev/dri/renderD%d", adapter_num+128);
        } else {
            snprintf(adapterpath,sizeof(adapterpath),
                "/dev/dri/card%d", adapter_num-3);
        }

        fd = open(adapterpath, O_RDWR);
        if (fd < 0) {
            av_log(avctx, AV_LOG_ERROR,
                "mfx init: %s fd open failed\n", adapterpath);
            continue;
        }

        va_dpy = vaGetDisplayDRM(fd);
        if (!va_dpy) {
            av_log(avctx, AV_LOG_ERROR,
                "mfx init: %s vaGetDisplayDRM failed\n", adapterpath);
            close(fd);
            continue;
        }

        va_res = vaInitialize(va_dpy, &major_version, &minor_version);
        if (VA_STATUS_SUCCESS != va_res) {
            av_log(avctx, AV_LOG_ERROR,
                "mfx init: %s vaInitialize failed\n", adapterpath);
            close(fd);
            fd = -1;
            continue;
        } else {
            av_log(avctx, AV_LOG_VERBOSE,
            "mfx initialization: %s vaInitialize successful\n",adapterpath);
            qs->fd_display = fd;
            qs->va_display = va_dpy;
            ret = MFXVideoCORE_SetHandle(qs->session,
                  (mfxHandleType)MFX_HANDLE_VA_DISPLAY, (mfxHDL)va_dpy);
            if (ret < 0) {
                av_log(avctx, AV_LOG_ERROR,
                "Error %d during set display handle\n", ret);
                return ff_qsv_error(ret);
            }
            break;
        }
    }
#endif //AVCODEC_QSV_LINUX_SESSION_HANDLE
    return 0;
}
/**
 * @brief Initialize a MSDK session
 *
 * Media SDK is based on sessions, so this is the prerequisite
 * initialization for HW acceleration.  For Windows the session is
 * complete and ready to use, for Linux a display handle is
 * required.  For releases of Media Server Studio >= 2015 R4 the
 * render nodes interface is preferred (/dev/dri/renderD).
 * Using Media Server Studio 2015 R4 or newer is recommended
 * but the older /dev/dri/card interface is also searched
 * for broader compatibility.
 *
 * @param avctx    ffmpeg metadata for this codec context
 * @param session  the MSDK session used
 */
int ff_qsv_init_internal_session(AVCodecContext *avctx, QSVSession *qs,
                                 const char *load_plugins)
{
    mfxIMPL impl   = MFX_IMPL_AUTO_ANY;
    mfxVersion ver = { { QSV_VERSION_MINOR, QSV_VERSION_MAJOR } };

    const char *desc;
    int ret;

    ret = MFXInit(impl, &ver, &qs->session);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Error initializing an internal MFX session\n");
        return ff_qsv_error(ret);
    }

    ret = ff_qsv_set_display_handle(avctx, qs);
    if (ret < 0)
        return ret;

    MFXQueryIMPL(qs->session, &impl);

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

    if (load_plugins && *load_plugins) {
        while (*load_plugins) {
            mfxPluginUID uid;
            int i, err = 0;

            char *plugin = av_get_token(&load_plugins, ":");
            if (!plugin)
                return AVERROR(ENOMEM);
            if (strlen(plugin) != 2 * sizeof(uid.Data)) {
                av_log(avctx, AV_LOG_ERROR, "Invalid plugin UID length\n");
                err = AVERROR(EINVAL);
                goto load_plugin_fail;
            }

            for (i = 0; i < sizeof(uid.Data); i++) {
                err = sscanf(plugin + 2 * i, "%2hhx", uid.Data + i);
                if (err != 1) {
                    av_log(avctx, AV_LOG_ERROR, "Invalid plugin UID\n");
                    err = AVERROR(EINVAL);
                    goto load_plugin_fail;
                }

            }

            ret = MFXVideoUSER_Load(qs->session, &uid, 1);
            if (ret < 0) {
                av_log(avctx, AV_LOG_ERROR, "Could not load the requested plugin: %s\n",
                       plugin);
                err = ff_qsv_error(ret);
                goto load_plugin_fail;
            }

load_plugin_fail:
            av_freep(&plugin);
            if (err < 0)
                return err;
        }
    }

    av_log(avctx, AV_LOG_VERBOSE,
           "Initialized an internal MFX session using %s implementation\n",
           desc);

    return 0;
}

int ff_qsv_close_internal_session(QSVSession *qs)
{
    if (qs->session) {
        MFXClose(qs->session);
        qs->session = NULL;
    }
#ifdef AVCODEC_QSV_LINUX_SESSION_HANDLE
    if (qs->va_display) {
        vaTerminate(qs->va_display);
        qs->va_display = NULL;
    }
    if (qs->fd_display > 0) {
        close(qs->fd_display);
        qs->fd_display = -1;
    }
#endif
    return 0;
}
