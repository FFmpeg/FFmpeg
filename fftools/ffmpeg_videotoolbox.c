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

#include "config.h"

#if HAVE_UTGETOSTYPEFROMSTRING
#include <CoreServices/CoreServices.h>
#endif

#include "libavcodec/avcodec.h"
#include "libavcodec/videotoolbox.h"
#include "libavutil/imgutils.h"
#include "ffmpeg.h"

typedef struct VTContext {
    AVFrame *tmp_frame;
} VTContext;

char *videotoolbox_pixfmt;

static int videotoolbox_retrieve_data(AVCodecContext *s, AVFrame *frame)
{
    InputStream *ist = s->opaque;
    VTContext  *vt = ist->hwaccel_ctx;
    CVPixelBufferRef pixbuf = (CVPixelBufferRef)frame->data[3];
    OSType pixel_format = CVPixelBufferGetPixelFormatType(pixbuf);
    CVReturn err;
    uint8_t *data[4] = { 0 };
    int linesize[4] = { 0 };
    int planes, ret, i;

    av_frame_unref(vt->tmp_frame);

    switch (pixel_format) {
    case kCVPixelFormatType_420YpCbCr8Planar: vt->tmp_frame->format = AV_PIX_FMT_YUV420P; break;
    case kCVPixelFormatType_422YpCbCr8:       vt->tmp_frame->format = AV_PIX_FMT_UYVY422; break;
    case kCVPixelFormatType_32BGRA:           vt->tmp_frame->format = AV_PIX_FMT_BGRA; break;
#ifdef kCFCoreFoundationVersionNumber10_7
    case kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange:
    case kCVPixelFormatType_420YpCbCr8BiPlanarFullRange: vt->tmp_frame->format = AV_PIX_FMT_NV12; break;
#endif
#if HAVE_KCVPIXELFORMATTYPE_420YPCBCR10BIPLANARVIDEORANGE
    case kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange:
    case kCVPixelFormatType_420YpCbCr10BiPlanarFullRange: vt->tmp_frame->format = AV_PIX_FMT_P010; break;
#endif
    default:
        av_log(NULL, AV_LOG_ERROR,
               "%s: Unsupported pixel format: %s\n",
               av_fourcc2str(s->codec_tag), videotoolbox_pixfmt);
        return AVERROR(ENOSYS);
    }

    vt->tmp_frame->width  = frame->width;
    vt->tmp_frame->height = frame->height;
    ret = av_frame_get_buffer(vt->tmp_frame, 32);
    if (ret < 0)
        return ret;

    err = CVPixelBufferLockBaseAddress(pixbuf, kCVPixelBufferLock_ReadOnly);
    if (err != kCVReturnSuccess) {
        av_log(NULL, AV_LOG_ERROR, "Error locking the pixel buffer.\n");
        return AVERROR_UNKNOWN;
    }

    if (CVPixelBufferIsPlanar(pixbuf)) {

        planes = CVPixelBufferGetPlaneCount(pixbuf);
        for (i = 0; i < planes; i++) {
            data[i]     = CVPixelBufferGetBaseAddressOfPlane(pixbuf, i);
            linesize[i] = CVPixelBufferGetBytesPerRowOfPlane(pixbuf, i);
        }
    } else {
        data[0] = CVPixelBufferGetBaseAddress(pixbuf);
        linesize[0] = CVPixelBufferGetBytesPerRow(pixbuf);
    }

    av_image_copy(vt->tmp_frame->data, vt->tmp_frame->linesize,
                  (const uint8_t **)data, linesize, vt->tmp_frame->format,
                  frame->width, frame->height);

    ret = av_frame_copy_props(vt->tmp_frame, frame);
    CVPixelBufferUnlockBaseAddress(pixbuf, kCVPixelBufferLock_ReadOnly);
    if (ret < 0)
        return ret;

    av_frame_unref(frame);
    av_frame_move_ref(frame, vt->tmp_frame);

    return 0;
}

static void videotoolbox_uninit(AVCodecContext *s)
{
    InputStream *ist = s->opaque;
    VTContext  *vt = ist->hwaccel_ctx;

    ist->hwaccel_uninit        = NULL;
    ist->hwaccel_retrieve_data = NULL;

    av_frame_free(&vt->tmp_frame);

    av_videotoolbox_default_free(s);
    av_freep(&ist->hwaccel_ctx);
}

int videotoolbox_init(AVCodecContext *s)
{
    InputStream *ist = s->opaque;
    int loglevel = (ist->hwaccel_id == HWACCEL_AUTO) ? AV_LOG_VERBOSE : AV_LOG_ERROR;
    int ret = 0;
    VTContext *vt;

    vt = av_mallocz(sizeof(*vt));
    if (!vt)
        return AVERROR(ENOMEM);

    ist->hwaccel_ctx           = vt;
    ist->hwaccel_uninit        = videotoolbox_uninit;
    ist->hwaccel_retrieve_data = videotoolbox_retrieve_data;

    vt->tmp_frame = av_frame_alloc();
    if (!vt->tmp_frame) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    // TODO: reindent
        if (!videotoolbox_pixfmt) {
            ret = av_videotoolbox_default_init(s);
        } else {
            AVVideotoolboxContext *vtctx = av_videotoolbox_alloc_context();
            CFStringRef pixfmt_str = CFStringCreateWithCString(kCFAllocatorDefault,
                                                               videotoolbox_pixfmt,
                                                               kCFStringEncodingUTF8);
#if HAVE_UTGETOSTYPEFROMSTRING
            vtctx->cv_pix_fmt_type = UTGetOSTypeFromString(pixfmt_str);
#else
            av_log(s, loglevel, "UTGetOSTypeFromString() is not available "
                   "on this platform, %s pixel format can not be honored from "
                   "the command line\n", videotoolbox_pixfmt);
#endif
            ret = av_videotoolbox_default_init2(s, vtctx);
            CFRelease(pixfmt_str);
        }
    if (ret < 0) {
        av_log(NULL, loglevel, "Error creating Videotoolbox decoder.\n");
        goto fail;
    }

    return 0;
fail:
    videotoolbox_uninit(s);
    return ret;
}
