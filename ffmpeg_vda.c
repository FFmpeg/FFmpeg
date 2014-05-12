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

#include "libavcodec/avcodec.h"
#include "libavcodec/vda.h"
#include "libavutil/imgutils.h"

#include "ffmpeg.h"

typedef struct VDAContext {
    AVFrame *tmp_frame;
} VDAContext;

static int vda_retrieve_data(AVCodecContext *s, AVFrame *frame)
{
    InputStream *ist = s->opaque;
    VDAContext  *vda = ist->hwaccel_ctx;
    CVPixelBufferRef pixbuf = (CVPixelBufferRef)frame->data[3];
    OSType pixel_format = CVPixelBufferGetPixelFormatType(pixbuf);
    CVReturn err;
    uint8_t *data[4] = { 0 };
    int linesize[4] = { 0 };
    int planes, ret, i;

    av_frame_unref(vda->tmp_frame);

    switch (pixel_format) {
    case kCVPixelFormatType_420YpCbCr8Planar: vda->tmp_frame->format = AV_PIX_FMT_YUV420P; break;
    case kCVPixelFormatType_422YpCbCr8:       vda->tmp_frame->format = AV_PIX_FMT_UYVY422; break;
    default:
        av_log(NULL, AV_LOG_ERROR,
               "Unsupported pixel format: %u\n", pixel_format);
        return AVERROR(ENOSYS);
    }

    vda->tmp_frame->width  = frame->width;
    vda->tmp_frame->height = frame->height;
    ret = av_frame_get_buffer(vda->tmp_frame, 32);
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

    av_image_copy(vda->tmp_frame->data, vda->tmp_frame->linesize,
                  data, linesize, vda->tmp_frame->format,
                  frame->width, frame->height);

    ret = av_frame_copy_props(vda->tmp_frame, frame);
    if (ret < 0)
        return ret;

    av_frame_unref(frame);
    av_frame_move_ref(frame, vda->tmp_frame);

    return 0;
}

static void vda_uninit(AVCodecContext *s)
{
    InputStream *ist = s->opaque;
    VDAContext  *vda = ist->hwaccel_ctx;

    ist->hwaccel_uninit        = NULL;
    ist->hwaccel_retrieve_data = NULL;

    av_frame_free(&vda->tmp_frame);

    av_vda_default_free(s);
    av_freep(&ist->hwaccel_ctx);
}

int vda_init(AVCodecContext *s)
{
    InputStream *ist = s->opaque;
    int loglevel = (ist->hwaccel_id == HWACCEL_AUTO) ? AV_LOG_VERBOSE : AV_LOG_ERROR;
    VDAContext *vda;
    int ret;

    vda = av_mallocz(sizeof(*vda));
    if (!vda)
        return AVERROR(ENOMEM);

    ist->hwaccel_ctx           = vda;
    ist->hwaccel_uninit        = vda_uninit;
    ist->hwaccel_retrieve_data = vda_retrieve_data;

    vda->tmp_frame = av_frame_alloc();
    if (!vda->tmp_frame) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    ret = av_vda_default_init(s);
    if (ret < 0) {
        av_log(NULL, loglevel, "Error creating VDA decoder.\n");
        goto fail;
    }

    return 0;
fail:
    vda_uninit(s);
    return ret;
}
