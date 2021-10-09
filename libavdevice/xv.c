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
    int64_t window_id;
    char *window_title;
    int window_width, window_height;
    int window_x, window_y;
    int dest_x, dest_y;          /**< display area position */
    unsigned int dest_w, dest_h; /**< display area dimensions */

    Display* display;
    char *display_name;

    XvImage* yuv_image;
    enum AVPixelFormat image_format;
    int image_width, image_height;
    XShmSegmentInfo yuv_shminfo;
    int xv_port;
    Atom wm_delete_message;
} XVContext;

typedef struct XVTagFormatMap
{
    int tag;
    enum AVPixelFormat format;
} XVTagFormatMap;

static const XVTagFormatMap tag_codec_map[] = {
    { MKTAG('I','4','2','0'), AV_PIX_FMT_YUV420P },
    { MKTAG('U','Y','V','Y'), AV_PIX_FMT_UYVY422 },
    { MKTAG('Y','U','Y','2'), AV_PIX_FMT_YUYV422 },
    { 0,                      AV_PIX_FMT_NONE }
};

static int xv_get_tag_from_format(enum AVPixelFormat format)
{
    const XVTagFormatMap *m = tag_codec_map;
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
    XColor fgcolor;
    XWindowAttributes window_attrs;
    int num_formats = 0, j, tag, ret;
    AVCodecParameters *par = s->streams[0]->codecpar;

    if (   s->nb_streams > 1
        || par->codec_type != AVMEDIA_TYPE_VIDEO
        || (par->codec_id != AV_CODEC_ID_WRAPPED_AVFRAME && par->codec_id != AV_CODEC_ID_RAWVIDEO)) {
        av_log(s, AV_LOG_ERROR, "Only a single raw or wrapped avframe video stream is supported.\n");
        return AVERROR(EINVAL);
    }

    if (!(tag = xv_get_tag_from_format(par->format))) {
        av_log(s, AV_LOG_ERROR,
               "Unsupported pixel format '%s', only yuv420p, uyvy422, yuyv422 are currently supported\n",
               av_get_pix_fmt_name(par->format));
        return AVERROR_PATCHWELCOME;
    }
    xv->image_format = par->format;

    xv->display = XOpenDisplay(xv->display_name);
    if (!xv->display) {
        av_log(s, AV_LOG_ERROR, "Could not open the X11 display '%s'\n", xv->display_name);
        return AVERROR(EINVAL);
    }

    xv->image_width  = par->width;
    xv->image_height = par->height;
    if (!xv->window_width && !xv->window_height) {
        AVRational sar = par->sample_aspect_ratio;
        xv->window_width  = par->width;
        xv->window_height = par->height;
        if (sar.num) {
            if (sar.num > sar.den)
                xv->window_width = av_rescale(xv->window_width, sar.num, sar.den);
            if (sar.num < sar.den)
                xv->window_height = av_rescale(xv->window_height, sar.den, sar.num);
        }
    }
    if (!xv->window_id) {
        xv->window = XCreateSimpleWindow(xv->display, DefaultRootWindow(xv->display),
                                         xv->window_x, xv->window_y,
                                         xv->window_width, xv->window_height,
                                         0, 0, 0);
        if (!xv->window_title) {
            if (!(xv->window_title = av_strdup(s->url))) {
                ret = AVERROR(ENOMEM);
                goto fail;
            }
        }
        XStoreName(xv->display, xv->window, xv->window_title);
        xv->wm_delete_message = XInternAtom(xv->display, "WM_DELETE_WINDOW", False);
        XSetWMProtocols(xv->display, xv->window, &xv->wm_delete_message, 1);
        XMapWindow(xv->display, xv->window);
    } else
        xv->window = xv->window_id;

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
               av_get_pix_fmt_name(par->format));
        ret = AVERROR(EINVAL);
        goto fail;
    }

    xv->gc = XCreateGC(xv->display, xv->window, 0, 0);
    xv->image_width  = par->width;
    xv->image_height = par->height;
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

    XGetWindowAttributes(xv->display, xv->window, &window_attrs);
    fgcolor.red = fgcolor.green = fgcolor.blue = 0;
    fgcolor.flags = DoRed | DoGreen | DoBlue;
    XAllocColor(xv->display, window_attrs.colormap, &fgcolor);
    XSetForeground(xv->display, xv->gc, fgcolor.pixel);
    //force display area recalculation at first frame
    xv->window_width = xv->window_height = 0;

    return 0;
  fail:
    xv_write_trailer(s);
    return ret;
}

static void compute_display_area(AVFormatContext *s)
{
    XVContext *xv = s->priv_data;
    AVRational sar, dar; /* sample and display aspect ratios */
    AVStream *st = s->streams[0];
    AVCodecParameters *par = st->codecpar;

    /* compute overlay width and height from the codec context information */
    sar = st->sample_aspect_ratio.num ? st->sample_aspect_ratio : (AVRational){ 1, 1 };
    dar = av_mul_q(sar, (AVRational){ par->width, par->height });

    /* we suppose the screen has a 1/1 sample aspect ratio */
    /* fit in the window */
    if (av_cmp_q(dar, (AVRational){ xv->dest_w, xv->dest_h }) > 0) {
        /* fit in width */
        xv->dest_y = xv->dest_h;
        xv->dest_x = 0;
        xv->dest_h = av_rescale(xv->dest_w, dar.den, dar.num);
        xv->dest_y -= xv->dest_h;
        xv->dest_y /= 2;
    } else {
        /* fit in height */
        xv->dest_x = xv->dest_w;
        xv->dest_y = 0;
        xv->dest_w = av_rescale(xv->dest_h, dar.num, dar.den);
        xv->dest_x -= xv->dest_w;
        xv->dest_x /= 2;
    }
}

static int xv_repaint(AVFormatContext *s)
{
    XVContext *xv = s->priv_data;
    XWindowAttributes window_attrs;

    XGetWindowAttributes(xv->display, xv->window, &window_attrs);
    if (window_attrs.width != xv->window_width || window_attrs.height != xv->window_height) {
        XRectangle rect[2];
        xv->dest_w = window_attrs.width;
        xv->dest_h = window_attrs.height;
        compute_display_area(s);
        if (xv->dest_x) {
            rect[0].width  = rect[1].width  = xv->dest_x;
            rect[0].height = rect[1].height = window_attrs.height;
            rect[0].y      = rect[1].y      = 0;
            rect[0].x = 0;
            rect[1].x = xv->dest_w + xv->dest_x;
            XFillRectangles(xv->display, xv->window, xv->gc, rect, 2);
        }
        if (xv->dest_y) {
            rect[0].width  = rect[1].width  = window_attrs.width;
            rect[0].height = rect[1].height = xv->dest_y;
            rect[0].x      = rect[1].x      = 0;
            rect[0].y = 0;
            rect[1].y = xv->dest_h + xv->dest_y;
            XFillRectangles(xv->display, xv->window, xv->gc, rect, 2);
        }
    }

    if (XvShmPutImage(xv->display, xv->xv_port, xv->window, xv->gc,
                      xv->yuv_image, 0, 0, xv->image_width, xv->image_height,
                      xv->dest_x, xv->dest_y, xv->dest_w, xv->dest_h, True) != Success) {
        av_log(s, AV_LOG_ERROR, "Could not copy image to XV shared memory buffer\n");
        return AVERROR_EXTERNAL;
    }
    return 0;
}

static int write_picture(AVFormatContext *s, uint8_t *input_data[4],
                         int linesize[4])
{
    XVContext *xv = s->priv_data;
    XvImage *img = xv->yuv_image;
    uint8_t *data[4] = {
        img->data + img->offsets[0],
        img->data + img->offsets[1],
        img->data + img->offsets[2]
    };

    /* Check messages. Window might get closed. */
    if (!xv->window_id) {
        XEvent event;
        while (XPending(xv->display)) {
            XNextEvent(xv->display, &event);
            if (event.type == ClientMessage && event.xclient.data.l[0] == xv->wm_delete_message) {
                av_log(xv, AV_LOG_DEBUG, "Window close event.\n");
                return AVERROR(EPIPE);
            }
        }
    }

    av_image_copy(data, img->pitches, (const uint8_t **)input_data, linesize,
                  xv->image_format, img->width, img->height);
    return xv_repaint(s);
}

static int xv_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVCodecParameters *par = s->streams[0]->codecpar;

    if (par->codec_id == AV_CODEC_ID_WRAPPED_AVFRAME) {
        AVFrame *frame = (AVFrame *)pkt->data;
        return write_picture(s, frame->data, frame->linesize);
    } else {
        uint8_t *data[4];
        int linesize[4];

        av_image_fill_arrays(data, linesize, pkt->data, par->format,
                             par->width, par->height, 1);
        return write_picture(s, data, linesize);
    }
}

static int xv_write_frame(AVFormatContext *s, int stream_index, AVFrame **frame,
                          unsigned flags)
{
    /* xv_write_header() should have accepted only supported formats */
    if ((flags & AV_WRITE_UNCODED_FRAME_QUERY))
        return 0;
    return write_picture(s, (*frame)->data, (*frame)->linesize);
}

static int xv_control_message(AVFormatContext *s, int type, void *data, size_t data_size)
{
    switch(type) {
    case AV_APP_TO_DEV_WINDOW_REPAINT:
        return xv_repaint(s);
    default:
        break;
    }
    return AVERROR(ENOSYS);
}

#define OFFSET(x) offsetof(XVContext, x)
static const AVOption options[] = {
    { "display_name", "set display name",       OFFSET(display_name), AV_OPT_TYPE_STRING, {.str = NULL }, 0, 0, AV_OPT_FLAG_ENCODING_PARAM },
    { "window_id",    "set existing window id", OFFSET(window_id),    AV_OPT_TYPE_INT64,  {.i64 = 0 }, 0, INT64_MAX, AV_OPT_FLAG_ENCODING_PARAM },
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

const AVOutputFormat ff_xv_muxer = {
    .name           = "xv",
    .long_name      = NULL_IF_CONFIG_SMALL("XV (XVideo) output device"),
    .priv_data_size = sizeof(XVContext),
    .audio_codec    = AV_CODEC_ID_NONE,
    .video_codec    = AV_CODEC_ID_WRAPPED_AVFRAME,
    .write_header   = xv_write_header,
    .write_packet   = xv_write_packet,
    .write_uncoded_frame = xv_write_frame,
    .write_trailer  = xv_write_trailer,
    .control_message = xv_control_message,
    .flags          = AVFMT_NOFILE | AVFMT_VARIABLE_FPS | AVFMT_NOTIMESTAMPS,
    .priv_class     = &xv_class,
};
