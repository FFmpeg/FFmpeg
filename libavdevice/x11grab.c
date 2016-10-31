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
 * X11 frame device demuxer
 * @author Clemens Fruhwirth <clemens@endorphin.org>
 * @author Edouard Gomez <ed.gomez@free.fr>
 */

#include "config.h"

#include <time.h>
#include <sys/shm.h>

#include <X11/cursorfont.h>
#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xlibint.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>

#include <X11/extensions/shape.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/XShm.h>

#include "libavutil/internal.h"
#include "libavutil/log.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "libavutil/time.h"

#include "libavformat/internal.h"

#include "avdevice.h"

/** X11 device demuxer context */
typedef struct X11GrabContext {
    const AVClass *class;    /**< Class for private options. */
    int frame_size;          /**< Size in bytes of a grabbed frame */
    AVRational time_base;    /**< Time base */
    int64_t time_frame;      /**< Current time */

    int width;               /**< Width of the grab frame */
    int height;              /**< Height of the grab frame */
    int x_off;               /**< Horizontal top-left corner coordinate */
    int y_off;               /**< Vertical top-left corner coordinate */

    Display *dpy;            /**< X11 display from which x11grab grabs frames */
    XImage *image;           /**< X11 image holding the grab */
    int use_shm;             /**< !0 when using XShm extension */
    XShmSegmentInfo shminfo; /**< When using XShm, keeps track of XShm info */
    int draw_mouse;          /**< Set by a private option. */
    int follow_mouse;        /**< Set by a private option. */
    int show_region;         /**< set by a private option. */
    AVRational framerate;    /**< Set by a private option. */
    int palette_changed;
    uint32_t palette[256];

    Cursor c;
    Window region_win;       /**< This is used by show_region option. */
} X11GrabContext;

#define REGION_WIN_BORDER 3

/**
 * Draw grabbing region window
 *
 * @param s x11grab context
 */
static void x11grab_draw_region_win(X11GrabContext *s)
{
    Display *dpy = s->dpy;
    Window win   = s->region_win;
    int screen = DefaultScreen(dpy);
    GC gc = XCreateGC(dpy, win, 0, 0);

    XSetForeground(dpy, gc, WhitePixel(dpy, screen));
    XSetBackground(dpy, gc, BlackPixel(dpy, screen));
    XSetLineAttributes(dpy, gc, REGION_WIN_BORDER, LineDoubleDash, 0, 0);
    XDrawRectangle(dpy, win, gc, 1, 1,
                   (s->width  + REGION_WIN_BORDER * 2) - 1 * 2 - 1,
                   (s->height + REGION_WIN_BORDER * 2) - 1 * 2 - 1);
    XFreeGC(dpy, gc);
}

/**
 * Initialize grabbing region window
 *
 * @param s x11grab context
 */
static void x11grab_region_win_init(X11GrabContext *s)
{
    Display *dpy = s->dpy;
    XRectangle rect;
    XSetWindowAttributes attribs = { .override_redirect = True };
    int screen = DefaultScreen(dpy);

    s->region_win = XCreateWindow(dpy, RootWindow(dpy, screen),
                                  s->x_off  - REGION_WIN_BORDER,
                                  s->y_off  - REGION_WIN_BORDER,
                                  s->width  + REGION_WIN_BORDER * 2,
                                  s->height + REGION_WIN_BORDER * 2,
                                  0, CopyFromParent,
                                  InputOutput, CopyFromParent,
                                  CWOverrideRedirect, &attribs);
    rect.x      = 0;
    rect.y      = 0;
    rect.width  = s->width;
    rect.height = s->height;
    XShapeCombineRectangles(dpy, s->region_win,
                            ShapeBounding, REGION_WIN_BORDER, REGION_WIN_BORDER,
                            &rect, 1, ShapeSubtract, 0);
    XMapWindow(dpy, s->region_win);
    XSelectInput(dpy, s->region_win, ExposureMask | StructureNotifyMask);
    x11grab_draw_region_win(s);
}

static int setup_shm(AVFormatContext *s, Display *dpy, XImage **image)
{
    X11GrabContext *g = s->priv_data;
    int scr           = XDefaultScreen(dpy);
    XImage *img       = XShmCreateImage(dpy, DefaultVisual(dpy, scr),
                                        DefaultDepth(dpy, scr), ZPixmap, NULL,
                                        &g->shminfo, g->width, g->height);

    g->shminfo.shmid = shmget(IPC_PRIVATE, img->bytes_per_line * img->height,
                              IPC_CREAT | 0777);

    if (g->shminfo.shmid == -1) {
        av_log(s, AV_LOG_ERROR, "Cannot get shared memory!\n");
        return AVERROR(ENOMEM);
    }

    g->shminfo.shmaddr  = img->data = shmat(g->shminfo.shmid, 0, 0);
    g->shminfo.readOnly = False;

    if (!XShmAttach(dpy, &g->shminfo)) {
        av_log(s, AV_LOG_ERROR, "Failed to attach shared memory!\n");
        /* needs some better error subroutine :) */
        return AVERROR(EIO);
    }

    *image = img;
    return 0;
}

static int setup_mouse(Display *dpy, int screen)
{
    int ev_ret, ev_err;

    if (XFixesQueryExtension(dpy, &ev_ret, &ev_err)) {
        Window root = RootWindow(dpy, screen);
        XFixesSelectCursorInput(dpy, root, XFixesDisplayCursorNotifyMask);
        return 0;
    }

    return AVERROR(ENOSYS);
}

static int pixfmt_from_image(AVFormatContext *s, XImage *image, int *pix_fmt)
{
    av_log(s, AV_LOG_DEBUG,
           "Image r 0x%.6lx g 0x%.6lx b 0x%.6lx and depth %i\n",
           image->red_mask,
           image->green_mask,
           image->blue_mask,
           image->bits_per_pixel);

    *pix_fmt = AV_PIX_FMT_NONE;

    switch (image->bits_per_pixel) {
    case 8:
        *pix_fmt =  AV_PIX_FMT_PAL8;
        break;
    case 16:
        if (image->red_mask   == 0xf800 &&
            image->green_mask == 0x07e0 &&
            image->blue_mask  == 0x001f) {
            *pix_fmt = AV_PIX_FMT_RGB565;
        } else if (image->red_mask   == 0x7c00 &&
                   image->green_mask == 0x03e0 &&
                   image->blue_mask  == 0x001f) {
            *pix_fmt = AV_PIX_FMT_RGB555;
        }
        break;
    case 24:
        if (image->red_mask   == 0xff0000 &&
            image->green_mask == 0x00ff00 &&
            image->blue_mask  == 0x0000ff) {
            *pix_fmt = AV_PIX_FMT_BGR24;
        } else if (image->red_mask   == 0x0000ff &&
                   image->green_mask == 0x00ff00 &&
                   image->blue_mask  == 0xff0000) {
            *pix_fmt = AV_PIX_FMT_RGB24;
        }
        break;
    case 32:
        if (image->red_mask   == 0xff0000 &&
            image->green_mask == 0x00ff00 &&
            image->blue_mask  == 0x0000ff ) {
            *pix_fmt = AV_PIX_FMT_0RGB32;
        }
        break;
    }
    if (*pix_fmt == AV_PIX_FMT_NONE) {
        av_log(s, AV_LOG_ERROR,
               "XImages with RGB mask 0x%.6lx 0x%.6lx 0x%.6lx and depth %i "
               "are currently not supported.\n",
               image->red_mask,
               image->green_mask,
               image->blue_mask,
               image->bits_per_pixel);

        return AVERROR_PATCHWELCOME;
    }

    return 0;
}

/**
 * Initialize the x11 grab device demuxer (public device demuxer API).
 *
 * @param s1 Context from avformat core
 * @return <ul>
 *          <li>AVERROR(ENOMEM) no memory left</li>
 *          <li>AVERROR(EIO) other failure case</li>
 *          <li>0 success</li>
 *         </ul>
 */
static int x11grab_read_header(AVFormatContext *s1)
{
    X11GrabContext *x11grab = s1->priv_data;
    Display *dpy;
    AVStream *st = NULL;
    XImage *image;
    int x_off = 0, y_off = 0, ret = 0, screen, use_shm = 0;
    char *dpyname, *offset;
    Colormap color_map;
    XColor color[256];
    int i;

    dpyname = av_strdup(s1->filename);
    if (!dpyname)
        goto out;

    offset = strchr(dpyname, '+');
    if (offset) {
        sscanf(offset, "%d,%d", &x_off, &y_off);
        if (strstr(offset, "nomouse")) {
            av_log(s1, AV_LOG_WARNING,
                   "'nomouse' specification in argument is deprecated: "
                   "use 'draw_mouse' option with value 0 instead\n");
            x11grab->draw_mouse = 0;
        }
        *offset = 0;
    }

    av_log(s1, AV_LOG_INFO,
           "device: %s -> display: %s x: %d y: %d width: %d height: %d\n",
           s1->filename, dpyname, x_off, y_off, x11grab->width, x11grab->height);

    dpy = XOpenDisplay(dpyname);
    av_freep(&dpyname);
    if (!dpy) {
        av_log(s1, AV_LOG_ERROR, "Could not open X display.\n");
        ret = AVERROR(EIO);
        goto out;
    }

    st = avformat_new_stream(s1, NULL);
    if (!st) {
        ret = AVERROR(ENOMEM);
        goto out;
    }
    avpriv_set_pts_info(st, 64, 1, 1000000); /* 64 bits pts in us */

    screen = DefaultScreen(dpy);

    if (x11grab->follow_mouse) {
        int screen_w, screen_h;
        Window w;

        screen_w = DisplayWidth(dpy, screen);
        screen_h = DisplayHeight(dpy, screen);
        XQueryPointer(dpy, RootWindow(dpy, screen), &w, &w, &x_off, &y_off,
                      &ret, &ret, &ret);
        x_off -= x11grab->width / 2;
        y_off -= x11grab->height / 2;
        x_off = av_clip(x_off, 0, screen_w - x11grab->width);
        y_off = av_clip(y_off, 0, screen_h - x11grab->height);
        av_log(s1, AV_LOG_INFO,
               "followmouse is enabled, resetting grabbing region to x: %d y: %d\n",
               x_off, y_off);
    }

    if (x11grab->use_shm) {
        use_shm = XShmQueryExtension(dpy);
        av_log(s1, AV_LOG_INFO,
               "shared memory extension %sfound\n", use_shm ? "" : "not ");
    }

    if (use_shm && setup_shm(s1, dpy, &image) < 0) {
        av_log(s1, AV_LOG_WARNING, "Falling back to XGetImage\n");
        use_shm = 0;
    }

    if (!use_shm) {
        image = XGetImage(dpy, RootWindow(dpy, screen),
                          x_off, y_off,
                          x11grab->width, x11grab->height,
                          AllPlanes, ZPixmap);
    }

    if (x11grab->draw_mouse && setup_mouse(dpy, screen) < 0) {
        av_log(s1, AV_LOG_WARNING,
               "XFixes not available, cannot draw the mouse cursor\n");
        x11grab->draw_mouse = 0;
    }

    x11grab->frame_size = x11grab->width * x11grab->height * image->bits_per_pixel / 8;
    x11grab->dpy        = dpy;
    x11grab->time_base  = av_inv_q(x11grab->framerate);
    x11grab->time_frame = av_gettime() / av_q2d(x11grab->time_base);
    x11grab->x_off      = x_off;
    x11grab->y_off      = y_off;
    x11grab->image      = image;
    x11grab->use_shm    = use_shm;

    ret = pixfmt_from_image(s1, image, &st->codecpar->format);
    if (ret < 0)
        goto out;

    if (st->codecpar->format == AV_PIX_FMT_PAL8) {
        color_map = DefaultColormap(dpy, screen);
        for (i = 0; i < 256; ++i)
            color[i].pixel = i;
        XQueryColors(dpy, color_map, color, 256);
        for (i = 0; i < 256; ++i)
            x11grab->palette[i] = (color[i].red   & 0xFF00) << 8 |
                                  (color[i].green & 0xFF00)      |
                                  (color[i].blue  & 0xFF00) >> 8;
        x11grab->palette_changed = 1;
    }


    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_id   = AV_CODEC_ID_RAWVIDEO;
    st->codecpar->width      = x11grab->width;
    st->codecpar->height     = x11grab->height;
    st->codecpar->bit_rate   = x11grab->frame_size * 1 / av_q2d(x11grab->time_base) * 8;

    st->avg_frame_rate       = av_inv_q(x11grab->time_base);

out:
    av_free(dpyname);
    return ret;
}

/**
 * Paint a mouse pointer in an X11 image.
 *
 * @param image image to paint the mouse pointer to
 * @param s context used to retrieve original grabbing rectangle
 *          coordinates
 */
static void paint_mouse_pointer(XImage *image, AVFormatContext *s1)
{
    X11GrabContext *s = s1->priv_data;
    int x_off    = s->x_off;
    int y_off    = s->y_off;
    int width    = s->width;
    int height   = s->height;
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
    Window root;
    XSetWindowAttributes attr;

    /* Code doesn't currently support 16-bit or PAL8 */
    if (image->bits_per_pixel != 24 && image->bits_per_pixel != 32)
        return;

    if (!s->c)
        s->c = XCreateFontCursor(dpy, XC_left_ptr);
    root = DefaultRootWindow(dpy);
    attr.cursor = s->c;
    XChangeWindowAttributes(dpy, root, CWCursor, &attr);

    xcim = XFixesGetCursorImage(dpy);
    if (!xcim) {
        av_log(s1, AV_LOG_WARNING,
               "XFixesGetCursorImage failed\n");
        return;
    }

    x = xcim->x - xcim->xhot;
    y = xcim->y - xcim->yhot;

    to_line   = FFMIN((y + xcim->height), (height + y_off));
    to_column = FFMIN((x + xcim->width),  (width  + x_off));

    for (line = FFMAX(y, y_off); line < to_line; line++) {
        for (column = FFMAX(x, x_off); column < to_column; column++) {
            int xcim_addr  = (line  - y)     * xcim->width + column - x;
            int image_addr = ((line - y_off) * width       + column - x_off) * pixstride;
            int r          = (uint8_t)(xcim->pixels[xcim_addr] >>  0);
            int g          = (uint8_t)(xcim->pixels[xcim_addr] >>  8);
            int b          = (uint8_t)(xcim->pixels[xcim_addr] >> 16);
            int a          = (uint8_t)(xcim->pixels[xcim_addr] >> 24);

            if (a == 255) {
                pix[image_addr + 0] = r;
                pix[image_addr + 1] = g;
                pix[image_addr + 2] = b;
            } else if (a) {
                /* pixel values from XFixesGetCursorImage come premultiplied by alpha */
                pix[image_addr + 0] = r + (pix[image_addr + 0] * (255 - a) + 255 / 2) / 255;
                pix[image_addr + 1] = g + (pix[image_addr + 1] * (255 - a) + 255 / 2) / 255;
                pix[image_addr + 2] = b + (pix[image_addr + 2] * (255 - a) + 255 / 2) / 255;
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
static int xget_zpixmap(Display *dpy, Drawable d, XImage *image, int x, int y)
{
    xGetImageReply rep;
    xGetImageReq *req;
    long nbytes;

    if (!image)
        return 0;

    LockDisplay(dpy);
    GetReq(GetImage, req);

    /* First set up the standard stuff in the request */
    req->drawable  = d;
    req->x         = x;
    req->y         = y;
    req->width     = image->width;
    req->height    = image->height;
    req->planeMask = (unsigned int)AllPlanes;
    req->format    = ZPixmap;

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
static int x11grab_read_packet(AVFormatContext *s1, AVPacket *pkt)
{
    X11GrabContext *s = s1->priv_data;
    Display *dpy      = s->dpy;
    XImage *image     = s->image;
    int x_off         = s->x_off;
    int y_off         = s->y_off;
    int follow_mouse  = s->follow_mouse;
    int screen, pointer_x, pointer_y, _, same_screen = 1;
    Window w, root;
    int64_t curtime, delay;
    struct timespec ts;

    /* wait based on the frame rate */
    for (;;) {
        curtime = av_gettime();
        delay   = s->time_frame * av_q2d(s->time_base) - curtime;
        if (delay <= 0) {
            break;
        }
        ts.tv_sec  = delay / 1000000;
        ts.tv_nsec = (delay % 1000000) * 1000;
        nanosleep(&ts, NULL);
    }

    /* Calculate the time of the next frame */
    do {
      s->time_frame += INT64_C(1000000);
    } while ((s->time_frame * av_q2d(s->time_base) - curtime) <= 0);

    av_init_packet(pkt);
    pkt->data = image->data;
    pkt->size = s->frame_size;
    pkt->pts  = curtime;
    if (s->palette_changed) {
        uint8_t *pal = av_packet_new_side_data(pkt, AV_PKT_DATA_PALETTE,
                                               AVPALETTE_SIZE);
        if (!pal) {
            av_log(s, AV_LOG_ERROR, "Cannot append palette to packet\n");
        } else {
            memcpy(pal, s->palette, AVPALETTE_SIZE);
            s->palette_changed = 0;
        }
    }

    screen = DefaultScreen(dpy);
    root   = RootWindow(dpy, screen);

    if (follow_mouse || s->draw_mouse)
        same_screen = XQueryPointer(dpy, root, &w, &w,
                                    &pointer_x, &pointer_y, &_, &_, &_);

    if (follow_mouse && same_screen) {
        int screen_w, screen_h;

        screen_w = DisplayWidth(dpy, screen);
        screen_h = DisplayHeight(dpy, screen);
        if (follow_mouse == -1) {
            // follow the mouse, put it at center of grabbing region
            x_off += pointer_x - s->width / 2 - x_off;
            y_off += pointer_y - s->height / 2 - y_off;
        } else {
            // follow the mouse, but only move the grabbing region when mouse
            // reaches within certain pixels to the edge.
            if (pointer_x > x_off + s->width - follow_mouse)
                x_off += pointer_x - (x_off + s->width - follow_mouse);
            else if (pointer_x < x_off + follow_mouse)
                x_off -= (x_off + follow_mouse) - pointer_x;
            if (pointer_y > y_off + s->height - follow_mouse)
                y_off += pointer_y - (y_off + s->height - follow_mouse);
            else if (pointer_y < y_off + follow_mouse)
                y_off -= (y_off + follow_mouse) - pointer_y;
        }
        // adjust grabbing region position if it goes out of screen.
        s->x_off = x_off = av_clip(x_off, 0, screen_w - s->width);
        s->y_off = y_off = av_clip(y_off, 0, screen_h - s->height);

        if (s->show_region && s->region_win)
            XMoveWindow(dpy, s->region_win,
                        s->x_off - REGION_WIN_BORDER,
                        s->y_off - REGION_WIN_BORDER);
    }

    if (s->show_region && same_screen) {
        if (s->region_win) {
            XEvent evt = { .type = NoEventMask };
            // Clean up the events, and do the initial draw or redraw.
            while (XCheckMaskEvent(dpy, ExposureMask | StructureNotifyMask,
                                   &evt))
                ;
            if (evt.type)
                x11grab_draw_region_win(s);
        } else {
            x11grab_region_win_init(s);
        }
    }

    if (s->use_shm) {
        if (!XShmGetImage(dpy, root, image, x_off, y_off, AllPlanes))
            av_log(s1, AV_LOG_INFO, "XShmGetImage() failed\n");
    } else {
        if (!xget_zpixmap(dpy, root, image, x_off, y_off))
            av_log(s1, AV_LOG_INFO, "XGetZPixmap() failed\n");
    }

    if (s->draw_mouse && same_screen)
        paint_mouse_pointer(image, s1);

    return s->frame_size;
}

/**
 * Close x11 frame grabber (public device demuxer API).
 *
 * @param s1 Context from avformat core
 * @return 0 success, !0 failure
 */
static int x11grab_read_close(AVFormatContext *s1)
{
    X11GrabContext *x11grab = s1->priv_data;

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

    if (x11grab->region_win)
        XDestroyWindow(x11grab->dpy, x11grab->region_win);

    /* Free X11 display */
    XCloseDisplay(x11grab->dpy);
    return 0;
}

#define OFFSET(x) offsetof(X11GrabContext, x)
#define DEC AV_OPT_FLAG_DECODING_PARAM
static const AVOption options[] = {
    { "grab_x", "Initial x coordinate.", OFFSET(x_off), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX, DEC },
    { "grab_y", "Initial y coordinate.", OFFSET(y_off), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX, DEC },
    { "draw_mouse", "draw the mouse pointer", OFFSET(draw_mouse), AV_OPT_TYPE_INT, {.i64 = 1}, 0, 1, DEC },

    { "follow_mouse", "move the grabbing region when the mouse pointer reaches within specified amount of pixels to the edge of region",
      OFFSET(follow_mouse), AV_OPT_TYPE_INT, {.i64 = 0}, -1, INT_MAX, DEC, "follow_mouse" },
    { "centered",     "keep the mouse pointer at the center of grabbing region when following",
      0, AV_OPT_TYPE_CONST, {.i64 = -1}, INT_MIN, INT_MAX, DEC, "follow_mouse" },

    { "framerate",  "set video frame rate",      OFFSET(framerate),   AV_OPT_TYPE_VIDEO_RATE, {.str = "ntsc"}, 0, INT_MAX, DEC },
    { "show_region", "show the grabbing region", OFFSET(show_region), AV_OPT_TYPE_INT,        {.i64 = 0}, 0, 1, DEC },
    { "video_size",  "set video frame size",     OFFSET(width),       AV_OPT_TYPE_IMAGE_SIZE, {.str = "vga"}, 0, 0, DEC },
    { "use_shm",     "use MIT-SHM extension",    OFFSET(use_shm),     AV_OPT_TYPE_INT,        {.i64 = 1}, 0, 1, DEC },
    { NULL },
};

static const AVClass x11_class = {
    .class_name = "X11grab indev",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
    .category   = AV_CLASS_CATEGORY_DEVICE_VIDEO_INPUT,
};

/** x11 grabber device demuxer declaration */
AVInputFormat ff_x11grab_demuxer = {
    .name           = "x11grab",
    .long_name      = NULL_IF_CONFIG_SMALL("X11grab"),
    .priv_data_size = sizeof(X11GrabContext),
    .read_header    = x11grab_read_header,
    .read_packet    = x11grab_read_packet,
    .read_close     = x11grab_read_close,
    .flags          = AVFMT_NOFILE,
    .priv_class     = &x11_class,
};
