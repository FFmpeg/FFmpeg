/*
 * X11 video grab interface
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg integration:
 * Copyright (C) 2006 Clemens Fruhwirth <clemens@endorphin.org>
 *                    Edouard Gomez <ed.gomez@free.fr>
 *
 * This file contains code from grab.c:
 * Copyright (c) 2000-2001 Fabrice Bellard
 *
 * This file contains code from the xvidcap project:
 * Copyright (C) 1997-1998 Rasca, Berlin
 *               2003-2004 Karl H. Beckers, Frankfurt
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * X11 frame device demuxer by Clemens Fruhwirth <clemens@endorphin.org>
 * and Edouard Gomez <ed.gomez@free.fr>.
 */

#include "config.h"
#include "libavutil/log.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include <time.h>
#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xlibint.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>
#include <sys/shm.h>
#include <X11/extensions/shape.h>
#include <X11/extensions/XShm.h>
#include <X11/extensions/Xfixes.h>
#include "avdevice.h"

/**
 * X11 Device Demuxer context
 */
struct x11_grab
{
    const AVClass *class;    /**< Class for private options. */
    int frame_size;          /**< Size in bytes of a grabbed frame */
    AVRational time_base;    /**< Time base */
    int64_t time_frame;      /**< Current time */

    char *video_size;        /**< String describing video size, set by a private option. */
    int height;              /**< Height of the grab frame */
    int width;               /**< Width of the grab frame */
    int x_off;               /**< Horizontal top-left corner coordinate */
    int y_off;               /**< Vertical top-left corner coordinate */

    Display *dpy;            /**< X11 display from which x11grab grabs frames */
    XImage *image;           /**< X11 image holding the grab */
    int use_shm;             /**< !0 when using XShm extension */
    XShmSegmentInfo shminfo; /**< When using XShm, keeps track of XShm infos */
    int  draw_mouse;         /**< Set by a private option. */
    int  follow_mouse;       /**< Set by a private option. */
    int  show_region;        /**< set by a private option. */
    char *framerate;         /**< Set by a private option. */

    Window region_win;       /**< This is used by show_region option. */
};

#define REGION_WIN_BORDER 3
/**
 * Draw grabbing region window
 *
 * @param s x11_grab context
 */
static void
x11grab_draw_region_win(struct x11_grab *s)
{
    Display *dpy = s->dpy;
    int screen;
    Window win = s->region_win;
    GC gc;

    screen = DefaultScreen(dpy);
    gc = XCreateGC(dpy, win, 0, 0);
    XSetForeground(dpy, gc, WhitePixel(dpy, screen));
    XSetBackground(dpy, gc, BlackPixel(dpy, screen));
    XSetLineAttributes(dpy, gc, REGION_WIN_BORDER, LineDoubleDash, 0, 0);
    XDrawRectangle(dpy, win, gc,
                   1, 1,
                   (s->width  + REGION_WIN_BORDER * 2) - 1 * 2 - 1,
                   (s->height + REGION_WIN_BORDER * 2) - 1 * 2 - 1);
    XFreeGC(dpy, gc);
}

/**
 * Initialize grabbing region window
 *
 * @param s x11_grab context
 */
static void
x11grab_region_win_init(struct x11_grab *s)
{
    Display *dpy = s->dpy;
    int screen;
    XSetWindowAttributes attribs;
    XRectangle rect;

    screen = DefaultScreen(dpy);
    attribs.override_redirect = True;
    s->region_win = XCreateWindow(dpy, RootWindow(dpy, screen),
                                  s->x_off  - REGION_WIN_BORDER,
                                  s->y_off  - REGION_WIN_BORDER,
                                  s->width  + REGION_WIN_BORDER * 2,
                                  s->height + REGION_WIN_BORDER * 2,
                                  0, CopyFromParent,
                                  InputOutput, CopyFromParent,
                                  CWOverrideRedirect, &attribs);
    rect.x = 0;
    rect.y = 0;
    rect.width  = s->width;
    rect.height = s->height;
    XShapeCombineRectangles(dpy, s->region_win,
                            ShapeBounding, REGION_WIN_BORDER, REGION_WIN_BORDER,
                            &rect, 1, ShapeSubtract, 0);
    XMapWindow(dpy, s->region_win);
    XSelectInput(dpy, s->region_win, ExposureMask | StructureNotifyMask);
    x11grab_draw_region_win(s);
}

/**
 * Initialize the x11 grab device demuxer (public device demuxer API).
 *
 * @param s1 Context from avformat core
 * @param ap Parameters from avformat core
 * @return <ul>
 *          <li>AVERROR(ENOMEM) no memory left</li>
 *          <li>AVERROR(EIO) other failure case</li>
 *          <li>0 success</li>
 *         </ul>
 */
static int
x11grab_read_header(AVFormatContext *s1, AVFormatParameters *ap)
{
    struct x11_grab *x11grab = s1->priv_data;
    Display *dpy;
    AVStream *st = NULL;
    enum PixelFormat input_pixfmt;
    XImage *image;
    int x_off = 0;
    int y_off = 0;
    int screen;
    int use_shm;
    char *dpyname, *offset;
    int ret = 0;
    AVRational framerate;

    dpyname = av_strdup(s1->filename);
    offset = strchr(dpyname, '+');
    if (offset) {
        sscanf(offset, "%d,%d", &x_off, &y_off);
        x11grab->draw_mouse = !strstr(offset, "nomouse");
        *offset= 0;
    }

    if ((ret = av_parse_video_size(&x11grab->width, &x11grab->height, x11grab->video_size)) < 0) {
        av_log(s1, AV_LOG_ERROR, "Couldn't parse video size.\n");
        goto out;
    }
    if ((ret = av_parse_video_rate(&framerate, x11grab->framerate)) < 0) {
        av_log(s1, AV_LOG_ERROR, "Could not parse framerate: %s.\n", x11grab->framerate);
        goto out;
    }
    av_log(s1, AV_LOG_INFO, "device: %s -> display: %s x: %d y: %d width: %d height: %d\n",
           s1->filename, dpyname, x_off, y_off, x11grab->width, x11grab->height);

    dpy = XOpenDisplay(dpyname);
    av_freep(&dpyname);
    if(!dpy) {
        av_log(s1, AV_LOG_ERROR, "Could not open X display.\n");
        ret = AVERROR(EIO);
        goto out;
    }

    st = avformat_new_stream(s1, NULL);
    if (!st) {
        ret = AVERROR(ENOMEM);
        goto out;
    }
    av_set_pts_info(st, 64, 1, 1000000); /* 64 bits pts in us */

    screen = DefaultScreen(dpy);

    if (x11grab->follow_mouse) {
        int screen_w, screen_h;
        Window w;

        screen_w = DisplayWidth(dpy, screen);
        screen_h = DisplayHeight(dpy, screen);
        XQueryPointer(dpy, RootWindow(dpy, screen), &w, &w, &x_off, &y_off, &ret, &ret, &ret);
        x_off -= x11grab->width / 2;
        y_off -= x11grab->height / 2;
        x_off = FFMIN(FFMAX(x_off, 0), screen_w - x11grab->width);
        y_off = FFMIN(FFMAX(y_off, 0), screen_h - x11grab->height);
        av_log(s1, AV_LOG_INFO, "followmouse is enabled, resetting grabbing region to x: %d y: %d\n", x_off, y_off);
    }

    use_shm = XShmQueryExtension(dpy);
    av_log(s1, AV_LOG_INFO, "shared memory extension%s found\n", use_shm ? "" : " not");

    if(use_shm) {
        int scr = XDefaultScreen(dpy);
        image = XShmCreateImage(dpy,
                                DefaultVisual(dpy, scr),
                                DefaultDepth(dpy, scr),
                                ZPixmap,
                                NULL,
                                &x11grab->shminfo,
                                x11grab->width, x11grab->height);
        x11grab->shminfo.shmid = shmget(IPC_PRIVATE,
                                        image->bytes_per_line * image->height,
                                        IPC_CREAT|0777);
        if (x11grab->shminfo.shmid == -1) {
            av_log(s1, AV_LOG_ERROR, "Fatal: Can't get shared memory!\n");
            ret = AVERROR(ENOMEM);
            goto out;
        }
        x11grab->shminfo.shmaddr = image->data = shmat(x11grab->shminfo.shmid, 0, 0);
        x11grab->shminfo.readOnly = False;

        if (!XShmAttach(dpy, &x11grab->shminfo)) {
            av_log(s1, AV_LOG_ERROR, "Fatal: Failed to attach shared memory!\n");
            /* needs some better error subroutine :) */
            ret = AVERROR(EIO);
            goto out;
        }
    } else {
        image = XGetImage(dpy, RootWindow(dpy, screen),
                          x_off,y_off,
                          x11grab->width, x11grab->height,
                          AllPlanes, ZPixmap);
    }

    switch (image->bits_per_pixel) {
    case 8:
        av_log (s1, AV_LOG_DEBUG, "8 bit palette\n");
        input_pixfmt = PIX_FMT_PAL8;
        break;
    case 16:
        if (       image->red_mask   == 0xf800 &&
                   image->green_mask == 0x07e0 &&
                   image->blue_mask  == 0x001f ) {
            av_log (s1, AV_LOG_DEBUG, "16 bit RGB565\n");
            input_pixfmt = PIX_FMT_RGB565;
        } else if (image->red_mask   == 0x7c00 &&
                   image->green_mask == 0x03e0 &&
                   image->blue_mask  == 0x001f ) {
            av_log(s1, AV_LOG_DEBUG, "16 bit RGB555\n");
            input_pixfmt = PIX_FMT_RGB555;
        } else {
            av_log(s1, AV_LOG_ERROR, "RGB ordering at image depth %i not supported ... aborting\n", image->bits_per_pixel);
            av_log(s1, AV_LOG_ERROR, "color masks: r 0x%.6lx g 0x%.6lx b 0x%.6lx\n", image->red_mask, image->green_mask, image->blue_mask);
            ret = AVERROR(EIO);
            goto out;
        }
        break;
    case 24:
        if (        image->red_mask   == 0xff0000 &&
                    image->green_mask == 0x00ff00 &&
                    image->blue_mask  == 0x0000ff ) {
            input_pixfmt = PIX_FMT_BGR24;
        } else if ( image->red_mask   == 0x0000ff &&
                    image->green_mask == 0x00ff00 &&
                    image->blue_mask  == 0xff0000 ) {
            input_pixfmt = PIX_FMT_RGB24;
        } else {
            av_log(s1, AV_LOG_ERROR,"rgb ordering at image depth %i not supported ... aborting\n", image->bits_per_pixel);
            av_log(s1, AV_LOG_ERROR, "color masks: r 0x%.6lx g 0x%.6lx b 0x%.6lx\n", image->red_mask, image->green_mask, image->blue_mask);
            ret = AVERROR(EIO);
            goto out;
        }
        break;
    case 32:
        input_pixfmt = PIX_FMT_RGB32;
        break;
    default:
        av_log(s1, AV_LOG_ERROR, "image depth %i not supported ... aborting\n", image->bits_per_pixel);
        ret = AVERROR(EINVAL);
        goto out;
    }

    x11grab->frame_size = x11grab->width * x11grab->height * image->bits_per_pixel/8;
    x11grab->dpy = dpy;
    x11grab->time_base  = (AVRational){framerate.den, framerate.num};
    x11grab->time_frame = av_gettime() / av_q2d(x11grab->time_base);
    x11grab->x_off = x_off;
    x11grab->y_off = y_off;
    x11grab->image = image;
    x11grab->use_shm = use_shm;

    st->codec->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codec->codec_id = CODEC_ID_RAWVIDEO;
    st->codec->width  = x11grab->width;
    st->codec->height = x11grab->height;
    st->codec->pix_fmt = input_pixfmt;
    st->codec->time_base = x11grab->time_base;
    st->codec->bit_rate = x11grab->frame_size * 1/av_q2d(x11grab->time_base) * 8;

out:
    return ret;
}

/**
 * Paint a mouse pointer in an X11 image.
 *
 * @param image image to paint the mouse pointer to
 * @param s context used to retrieve original grabbing rectangle
 *          coordinates
 */
static void
paint_mouse_pointer(XImage *image, struct x11_grab *s)
{
    int x_off = s->x_off;
    int y_off = s->y_off;
    int width = s->width;
    int height = s->height;
    Display *dpy = s->dpy;
    XFixesCursorImage *xcim;
    int x, y;
    int line, column;
    int to_line, to_column;
    int pixstride = image->bits_per_pixel >> 3;
    /* Warning: in its insanity, xlib provides unsigned image data through a
     * char* pointer, so we have to make it uint8_t to make things not break.
     * Anyone who performs further investigation of the xlib API likely risks
     * permanent brain damage. */
    uint8_t *pix = image->data;

    /* Code doesn't currently support 16-bit or PAL8 */
    if (image->bits_per_pixel != 24 && image->bits_per_pixel != 32)
        return;

    xcim = XFixesGetCursorImage(dpy);

    x = xcim->x - xcim->xhot;
    y = xcim->y - xcim->yhot;

    to_line = FFMIN((y + xcim->height), (height + y_off));
    to_column = FFMIN((x + xcim->width), (width + x_off));

    for (line = FFMAX(y, y_off); line < to_line; line++) {
        for (column = FFMAX(x, x_off); column < to_column; column++) {
            int  xcim_addr = (line - y) * xcim->width + column - x;
            int image_addr = ((line - y_off) * width + column - x_off) * pixstride;
            int r = (uint8_t)(xcim->pixels[xcim_addr] >>  0);
            int g = (uint8_t)(xcim->pixels[xcim_addr] >>  8);
            int b = (uint8_t)(xcim->pixels[xcim_addr] >> 16);
            int a = (uint8_t)(xcim->pixels[xcim_addr] >> 24);

            if (a == 255) {
                pix[image_addr+0] = r;
                pix[image_addr+1] = g;
                pix[image_addr+2] = b;
            } else if (a) {
                /* pixel values from XFixesGetCursorImage come premultiplied by alpha */
                pix[image_addr+0] = r + (pix[image_addr+0]*(255-a) + 255/2) / 255;
                pix[image_addr+1] = g + (pix[image_addr+1]*(255-a) + 255/2) / 255;
                pix[image_addr+2] = b + (pix[image_addr+2]*(255-a) + 255/2) / 255;
            }
        }
    }

    XFree(xcim);
    xcim = NULL;
}


/**
 * Read new data in the image structure.
 *
 * @param dpy X11 display to grab from
 * @param d
 * @param image Image where the grab will be put
 * @param x Top-Left grabbing rectangle horizontal coordinate
 * @param y Top-Left grabbing rectangle vertical coordinate
 * @return 0 if error, !0 if successful
 */
static int
xget_zpixmap(Display *dpy, Drawable d, XImage *image, int x, int y)
{
    xGetImageReply rep;
    xGetImageReq *req;
    long nbytes;

    if (!image) {
        return 0;
    }

    LockDisplay(dpy);
    GetReq(GetImage, req);

    /* First set up the standard stuff in the request */
    req->drawable = d;
    req->x = x;
    req->y = y;
    req->width = image->width;
    req->height = image->height;
    req->planeMask = (unsigned int)AllPlanes;
    req->format = ZPixmap;

    if (!_XReply(dpy, (xReply *)&rep, 0, xFalse) || !rep.length) {
        UnlockDisplay(dpy);
        SyncHandle();
        return 0;
    }

    nbytes = (long)rep.length << 2;
    _XReadPad(dpy, image->data, nbytes);

    UnlockDisplay(dpy);
    SyncHandle();
    return 1;
}

/**
 * Grab a frame from x11 (public device demuxer API).
 *
 * @param s1 Context from avformat core
 * @param pkt Packet holding the brabbed frame
 * @return frame size in bytes
 */
static int
x11grab_read_packet(AVFormatContext *s1, AVPacket *pkt)
{
    struct x11_grab *s = s1->priv_data;
    Display *dpy = s->dpy;
    XImage *image = s->image;
    int x_off = s->x_off;
    int y_off = s->y_off;

    int screen;
    Window root;
    int follow_mouse = s->follow_mouse;

    int64_t curtime, delay;
    struct timespec ts;

    /* Calculate the time of the next frame */
    s->time_frame += INT64_C(1000000);

    /* wait based on the frame rate */
    for(;;) {
        curtime = av_gettime();
        delay = s->time_frame * av_q2d(s->time_base) - curtime;
        if (delay <= 0) {
            if (delay < INT64_C(-1000000) * av_q2d(s->time_base)) {
                s->time_frame += INT64_C(1000000);
            }
            break;
        }
        ts.tv_sec = delay / 1000000;
        ts.tv_nsec = (delay % 1000000) * 1000;
        nanosleep(&ts, NULL);
    }

    av_init_packet(pkt);
    pkt->data = image->data;
    pkt->size = s->frame_size;
    pkt->pts = curtime;

    screen = DefaultScreen(dpy);
    root = RootWindow(dpy, screen);
    if (follow_mouse) {
        int screen_w, screen_h;
        int pointer_x, pointer_y, _;
        Window w;

        screen_w = DisplayWidth(dpy, screen);
        screen_h = DisplayHeight(dpy, screen);
        XQueryPointer(dpy, root, &w, &w, &pointer_x, &pointer_y, &_, &_, &_);
        if (follow_mouse == -1) {
            // follow the mouse, put it at center of grabbing region
            x_off += pointer_x - s->width  / 2 - x_off;
            y_off += pointer_y - s->height / 2 - y_off;
        } else {
            // follow the mouse, but only move the grabbing region when mouse
            // reaches within certain pixels to the edge.
            if (pointer_x > x_off + s->width - follow_mouse) {
                x_off += pointer_x - (x_off + s->width - follow_mouse);
            } else if (pointer_x < x_off + follow_mouse)
                x_off -= (x_off + follow_mouse) - pointer_x;
            if (pointer_y > y_off + s->height - follow_mouse) {
                y_off += pointer_y - (y_off + s->height - follow_mouse);
            } else if (pointer_y < y_off + follow_mouse)
                y_off -= (y_off + follow_mouse) - pointer_y;
        }
        // adjust grabbing region position if it goes out of screen.
        s->x_off = x_off = FFMIN(FFMAX(x_off, 0), screen_w - s->width);
        s->y_off = y_off = FFMIN(FFMAX(y_off, 0), screen_h - s->height);

        if (s->show_region && s->region_win)
            XMoveWindow(dpy, s->region_win,
                        s->x_off - REGION_WIN_BORDER,
                        s->y_off - REGION_WIN_BORDER);
    }

    if (s->show_region) {
        if (s->region_win) {
            XEvent evt;
            // clean up the events, and do the initinal draw or redraw.
            for (evt.type = NoEventMask; XCheckMaskEvent(dpy, ExposureMask | StructureNotifyMask, &evt); );
            if (evt.type)
                x11grab_draw_region_win(s);
        } else {
            x11grab_region_win_init(s);
        }
    }

    if(s->use_shm) {
        if (!XShmGetImage(dpy, root, image, x_off, y_off, AllPlanes)) {
            av_log (s1, AV_LOG_INFO, "XShmGetImage() failed\n");
        }
    } else {
        if (!xget_zpixmap(dpy, root, image, x_off, y_off)) {
            av_log (s1, AV_LOG_INFO, "XGetZPixmap() failed\n");
        }
    }
    if (image->bits_per_pixel == 32)
        XAddPixel(image, 0xFF000000);

    if (s->draw_mouse) {
        paint_mouse_pointer(image, s);
    }

    return s->frame_size;
}

/**
 * Close x11 frame grabber (public device demuxer API).
 *
 * @param s1 Context from avformat core
 * @return 0 success, !0 failure
 */
static int
x11grab_read_close(AVFormatContext *s1)
{
    struct x11_grab *x11grab = s1->priv_data;

    /* Detach cleanly from shared mem */
    if (x11grab->use_shm) {
        XShmDetach(x11grab->dpy, &x11grab->shminfo);
        shmdt(x11grab->shminfo.shmaddr);
        shmctl(x11grab->shminfo.shmid, IPC_RMID, NULL);
    }

    /* Destroy X11 image */
    if (x11grab->image) {
        XDestroyImage(x11grab->image);
        x11grab->image = NULL;
    }

    if (x11grab->region_win) {
        XDestroyWindow(x11grab->dpy, x11grab->region_win);
    }

    /* Free X11 display */
    XCloseDisplay(x11grab->dpy);
    return 0;
}

#define OFFSET(x) offsetof(struct x11_grab, x)
#define DEC AV_OPT_FLAG_DECODING_PARAM
static const AVOption options[] = {
    { "video_size", "A string describing frame size, such as 640x480 or hd720.", OFFSET(video_size), AV_OPT_TYPE_STRING, {.str = "vga"}, 0, 0, DEC },
    { "framerate", "", OFFSET(framerate), AV_OPT_TYPE_STRING, {.str = "ntsc"}, 0, 0, DEC },
    { "draw_mouse", "Draw the mouse pointer.", OFFSET(draw_mouse), AV_OPT_TYPE_INT, { 1 }, 0, 1, DEC },
    { "follow_mouse", "Move the grabbing region when the mouse pointer reaches within specified amount of pixels to the edge of region.",
      OFFSET(follow_mouse), AV_OPT_TYPE_INT, { 0 }, -1, INT_MAX, DEC, "follow_mouse" },
    { "centered", "Keep the mouse pointer at the center of grabbing region when following.", 0, AV_OPT_TYPE_CONST, { -1 }, INT_MIN, INT_MAX, DEC, "follow_mouse" },
    { "show_region", "Show the grabbing region.", OFFSET(show_region), AV_OPT_TYPE_INT, { 0 }, 0, 1, DEC },
    { NULL },
};

static const AVClass x11_class = {
    .class_name = "X11grab indev",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

/** x11 grabber device demuxer declaration */
AVInputFormat ff_x11_grab_device_demuxer = {
    .name           = "x11grab",
    .long_name      = NULL_IF_CONFIG_SMALL("X11grab"),
    .priv_data_size = sizeof(struct x11_grab),
    .read_header    = x11grab_read_header,
    .read_packet    = x11grab_read_packet,
    .read_close     = x11grab_read_close,
    .flags          = AVFMT_NOFILE,
    .priv_class     = &x11_class,
};
