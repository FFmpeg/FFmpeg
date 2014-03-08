/*
 * Copyright (c) 2013 Jeff Moguillansky
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

/**
 * @file
 * XVideo output device
 *
 * TODO:
 * - add support to more formats
 * - add support to window id specification
 */

#include <X11/Xlib.h>
#include <X11/extensions/Xv.h>
#include <X11/extensions/XShm.h>
#include <X11/extensions/Xvlib.h>
#include <sys/shm.h>

#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/imgutils.h"
#include "libavformat/internal.h"
#include "avdevice.h"

typedef struct {
    AVClass *class;
    GC gc;

    Window window;
    char *window_title;
    int window_width, window_height;
    int window_x, window_y;

    Display* display;
    char *display_name;

    XvImage* yuv_image;
    enum AVPixelFormat image_format;
    int image_width, image_height;
    XShmSegmentInfo yuv_shminfo;
    int xv_port;
} XVContext;

typedef struct XVTagFormatMap
{
    int tag;
    enum AVPixelFormat format;
} XVTagFormatMap;

static XVTagFormatMap tag_codec_map[] = {
    { MKTAG('I','4','2','0'), AV_PIX_FMT_YUV420P },
    { MKTAG('U','Y','V','Y'), AV_PIX_FMT_UYVY422 },
    { MKTAG('Y','U','Y','2'), AV_PIX_FMT_YUYV422 },
    { 0,                      AV_PIX_FMT_NONE }
};

static int xv_get_tag_from_format(enum AVPixelFormat format)
{
    XVTagFormatMap *m = tag_codec_map;
    int i;
    for (i = 0; m->tag; m = &tag_codec_map[++i]) {
        if (m->format == format)
            return m->tag;
    }
    return 0;
}

static int xv_write_trailer(AVFormatContext *s)
{
    XVContext *xv = s->priv_data;
    if (xv->display) {
        XShmDetach(xv->display, &xv->yuv_shminfo);
        if (xv->yuv_image)
            shmdt(xv->yuv_image->data);
        XFree(xv->yuv_image);
        if (xv->gc)
            XFreeGC(xv->display, xv->gc);
        XCloseDisplay(xv->display);
    }
    return 0;
}

static int xv_write_header(AVFormatContext *s)
{
    XVContext *xv = s->priv_data;
    unsigned int num_adaptors;
    XvAdaptorInfo *ai;
    XvImageFormatValues *fv;
    int num_formats = 0, j, tag, ret;
    AVCodecContext *encctx = s->streams[0]->codec;

    if (   s->nb_streams > 1
        || encctx->codec_type != AVMEDIA_TYPE_VIDEO
        || encctx->codec_id   != AV_CODEC_ID_RAWVIDEO) {
        av_log(s, AV_LOG_ERROR, "Only supports one rawvideo stream\n");
        return AVERROR(EINVAL);
    }

    if (!(tag = xv_get_tag_from_format(encctx->pix_fmt))) {
        av_log(s, AV_LOG_ERROR,
               "Unsupported pixel format '%s', only yuv420p, uyvy422, yuyv422 are currently supported\n",
               av_get_pix_fmt_name(encctx->pix_fmt));
        return AVERROR_PATCHWELCOME;
    }
    xv->image_format = encctx->pix_fmt;

    xv->display = XOpenDisplay(xv->display_name);
    if (!xv->display) {
        av_log(s, AV_LOG_ERROR, "Could not open the X11 display '%s'\n", xv->display_name);
        return AVERROR(EINVAL);
    }

    xv->image_width  = encctx->width;
    xv->image_height = encctx->height;
    if (!xv->window_width && !xv->window_height) {
        AVRational sar = encctx->sample_aspect_ratio;
        xv->window_width  = encctx->width;
        xv->window_height = encctx->height;
        if (sar.num) {
            if (sar.num > sar.den)
                xv->window_width = av_rescale(xv->window_width, sar.num, sar.den);
            if (sar.num < sar.den)
                xv->window_height = av_rescale(xv->window_height, sar.den, sar.num);
        }
    }
    xv->window = XCreateSimpleWindow(xv->display, DefaultRootWindow(xv->display),
                                     xv->window_x, xv->window_y,
                                     xv->window_width, xv->window_height,
                                     0, 0, 0);
    if (!xv->window_title) {
        if (!(xv->window_title = av_strdup(s->filename))) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
    }
    XStoreName(xv->display, xv->window, xv->window_title);
    XMapWindow(xv->display, xv->window);

    if (XvQueryAdaptors(xv->display, DefaultRootWindow(xv->display), &num_adaptors, &ai) != Success) {
        ret = AVERROR_EXTERNAL;
        goto fail;
    }
    if (!num_adaptors) {
        av_log(s, AV_LOG_ERROR, "No X-Video adaptors present\n");
        return AVERROR(ENODEV);
    }
    xv->xv_port = ai[0].base_id;
    XvFreeAdaptorInfo(ai);

    fv = XvListImageFormats(xv->display, xv->xv_port, &num_formats);
    if (!fv) {
        ret = AVERROR_EXTERNAL;
        goto fail;
    }
    for (j = 0; j < num_formats; j++) {
        if (fv[j].id == tag) {
            break;
        }
    }
    XFree(fv);

    if (j >= num_formats) {
        av_log(s, AV_LOG_ERROR,
               "Device does not support pixel format %s, aborting\n",
               av_get_pix_fmt_name(encctx->pix_fmt));
        ret = AVERROR(EINVAL);
        goto fail;
    }

    xv->gc = XCreateGC(xv->display, xv->window, 0, 0);
    xv->image_width  = encctx->width;
    xv->image_height = encctx->height;
    xv->yuv_image = XvShmCreateImage(xv->display, xv->xv_port, tag, 0,
                                     xv->image_width, xv->image_height, &xv->yuv_shminfo);
    xv->yuv_shminfo.shmid = shmget(IPC_PRIVATE, xv->yuv_image->data_size,
                                   IPC_CREAT | 0777);
    xv->yuv_shminfo.shmaddr = (char *)shmat(xv->yuv_shminfo.shmid, 0, 0);
    xv->yuv_image->data = xv->yuv_shminfo.shmaddr;
    xv->yuv_shminfo.readOnly = False;

    XShmAttach(xv->display, &xv->yuv_shminfo);
    XSync(xv->display, False);
    shmctl(xv->yuv_shminfo.shmid, IPC_RMID, 0);

    return 0;
  fail:
    xv_write_trailer(s);
    return ret;
}

static int write_picture(AVFormatContext *s, AVPicture *pict)
{
    XVContext *xv = s->priv_data;
    XvImage *img = xv->yuv_image;
    XWindowAttributes window_attrs;
    uint8_t *data[3] = {
        img->data + img->offsets[0],
        img->data + img->offsets[1],
        img->data + img->offsets[2]
    };

    av_image_copy(data, img->pitches, (const uint8_t **)pict->data, pict->linesize,
                  xv->image_format, img->width, img->height);
    XGetWindowAttributes(xv->display, xv->window, &window_attrs);
    if (XvShmPutImage(xv->display, xv->xv_port, xv->window, xv->gc,
                      xv->yuv_image, 0, 0, xv->image_width, xv->image_height, 0, 0,
                      window_attrs.width, window_attrs.height, True) != Success) {
        av_log(s, AV_LOG_ERROR, "Could not copy image to XV shared memory buffer\n");
        return AVERROR_EXTERNAL;
    }
    return 0;
}

static int xv_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVPicture pict;
    AVCodecContext *ctx = s->streams[0]->codec;

    avpicture_fill(&pict, pkt->data, ctx->pix_fmt, ctx->width, ctx->height);
    return write_picture(s, &pict);
}

static int xv_write_frame(AVFormatContext *s, int stream_index, AVFrame **frame,
                          unsigned flags)
{
    /* xv_write_header() should have accepted only supported formats */
    if ((flags & AV_WRITE_UNCODED_FRAME_QUERY))
        return 0;
    return write_picture(s, (AVPicture *)*frame);
}

#define OFFSET(x) offsetof(XVContext, x)
static const AVOption options[] = {
    { "display_name", "set display name",       OFFSET(display_name), AV_OPT_TYPE_STRING, {.str = NULL }, 0, 0, AV_OPT_FLAG_ENCODING_PARAM },
    { "window_size",  "set window forced size", OFFSET(window_width), AV_OPT_TYPE_IMAGE_SIZE, {.str = NULL}, 0, 0, AV_OPT_FLAG_ENCODING_PARAM },
    { "window_title", "set window title",       OFFSET(window_title), AV_OPT_TYPE_STRING, {.str = NULL }, 0, 0, AV_OPT_FLAG_ENCODING_PARAM },
    { "window_x",     "set window x offset",    OFFSET(window_x),     AV_OPT_TYPE_INT,    {.i64 = 0 }, -INT_MAX, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM },
    { "window_y",     "set window y offset",    OFFSET(window_y),     AV_OPT_TYPE_INT,    {.i64 = 0 }, -INT_MAX, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM },
    { NULL }

};

static const AVClass xv_class = {
    .class_name = "xvideo outdev",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
    .category   = AV_CLASS_CATEGORY_DEVICE_VIDEO_OUTPUT,
};

AVOutputFormat ff_xv_muxer = {
    .name           = "xv",
    .long_name      = NULL_IF_CONFIG_SMALL("XV (XVideo) output device"),
    .priv_data_size = sizeof(XVContext),
    .audio_codec    = AV_CODEC_ID_NONE,
    .video_codec    = AV_CODEC_ID_RAWVIDEO,
    .write_header   = xv_write_header,
    .write_packet   = xv_write_packet,
    .write_uncoded_frame = xv_write_frame,
    .write_trailer  = xv_write_trailer,
    .flags          = AVFMT_NOFILE | AVFMT_VARIABLE_FPS | AVFMT_NOTIMESTAMPS,
    .priv_class     = &xv_class,
};
