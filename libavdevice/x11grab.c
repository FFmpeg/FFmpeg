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
 * @file libavdevice/x11grab.c
 * X11 frame device demuxer by Clemens Fruhwirth <clemens@endorphin.org>
 * and Edouard Gomez <ed.gomez@free.fr>.
 */

#include "config.h"
#include "libavformat/avformat.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#define _LINUX_TIME_H 1
#include <time.h>
#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xlibint.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <X11/extensions/XShm.h>

/**
 * X11 Device Demuxer context
 */
struct x11_grab
{
    int frame_size;          /**< Size in bytes of a grabbed frame */
    AVRational time_base;    /**< Time base */
    int64_t time_frame;      /**< Current time */

    int height;              /**< Height of the grab frame */
    int width;               /**< Width of the grab frame */
    int x_off;               /**< Horizontal top-left corner coordinate */
    int y_off;               /**< Vertical top-left corner coordinate */

    Display *dpy;            /**< X11 display from which x11grab grabs frames */
    XImage *image;           /**< X11 image holding the grab */
    int use_shm;             /**< !0 when using XShm extension */
    XShmSegmentInfo shminfo; /**< When using XShm, keeps track of XShm infos */
    int mouse_warning_shown;
};

/**
 * Initializes the x11 grab device demuxer (public device demuxer API).
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
    int input_pixfmt;
    XImage *image;
    int x_off = 0;
    int y_off = 0;
    int use_shm;
    char *param, *offset;

    param = av_strdup(s1->filename);
    offset = strchr(param, '+');
    if (offset) {
        sscanf(offset, "%d,%d", &x_off, &y_off);
        *offset= 0;
    }

    av_log(s1, AV_LOG_INFO, "device: %s -> display: %s x: %d y: %d width: %d height: %d\n", s1->filename, param, x_off, y_off, ap->width, ap->height);

    dpy = XOpenDisplay(param);
    if(!dpy) {
        av_log(s1, AV_LOG_ERROR, "Could not open X display.\n");
        return AVERROR(EIO);
    }

    if (!ap || ap->width <= 0 || ap->height <= 0 || ap->time_base.den <= 0) {
        av_log(s1, AV_LOG_ERROR, "AVParameters don't have video size and/or rate. Use -s and -r.\n");
        return AVERROR(EIO);
    }

    st = av_new_stream(s1, 0);
    if (!st) {
        return AVERROR(ENOMEM);
    }
    av_set_pts_info(st, 64, 1, 1000000); /* 64 bits pts in us */

    use_shm = XShmQueryExtension(dpy);
    av_log(s1, AV_LOG_INFO, "shared memory extension %s found\n", use_shm ? "" : "not");

    if(use_shm) {
        int scr = XDefaultScreen(dpy);
        image = XShmCreateImage(dpy,
                                DefaultVisual(dpy, scr),
                                DefaultDepth(dpy, scr),
                                ZPixmap,
                                NULL,
                                &x11grab->shminfo,
                                ap->width, ap->height);
        x11grab->shminfo.shmid = shmget(IPC_PRIVATE,
                                        image->bytes_per_line * image->height,
                                        IPC_CREAT|0777);
        if (x11grab->shminfo.shmid == -1) {
            av_log(s1, AV_LOG_ERROR, "Fatal: Can't get shared memory!\n");
            return AVERROR(ENOMEM);
        }
        x11grab->shminfo.shmaddr = image->data = shmat(x11grab->shminfo.shmid, 0, 0);
        x11grab->shminfo.readOnly = False;

        if (!XShmAttach(dpy, &x11grab->shminfo)) {
            av_log(s1, AV_LOG_ERROR, "Fatal: Failed to attach shared memory!\n");
            /* needs some better error subroutine :) */
            return AVERROR(EIO);
        }
    } else {
        image = XGetImage(dpy, RootWindow(dpy, DefaultScreen(dpy)),
                          x_off,y_off,
                          ap->width,ap->height,
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
            return AVERROR(EIO);
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
            return AVERROR(EIO);
        }
        break;
    case 32:
#if 0
        GetColorInfo (image, &c_info);
        if ( c_info.alpha_mask == 0xff000000 && image->green_mask == 0x0000ff00) {
            /* byte order is relevant here, not endianness
             * endianness is handled by avcodec, but atm no such thing
             * as having ABGR, instead of ARGB in a word. Since we
             * need this for Solaris/SPARC, but need to do the conversion
             * for every frame we do it outside of this loop, cf. below
             * this matches both ARGB32 and ABGR32 */
            input_pixfmt = PIX_FMT_ARGB32;
        }  else {
            av_log(s1, AV_LOG_ERROR,"image depth %i not supported ... aborting\n", image->bits_per_pixel);
            return AVERROR(EIO);
        }
#endif
        input_pixfmt = PIX_FMT_RGB32;
        break;
    default:
        av_log(s1, AV_LOG_ERROR, "image depth %i not supported ... aborting\n", image->bits_per_pixel);
        return -1;
    }

    x11grab->frame_size = ap->width * ap->height * image->bits_per_pixel/8;
    x11grab->dpy = dpy;
    x11grab->width = ap->width;
    x11grab->height = ap->height;
    x11grab->time_base  = ap->time_base;
    x11grab->time_frame = av_gettime() / av_q2d(ap->time_base);
    x11grab->x_off = x_off;
    x11grab->y_off = y_off;
    x11grab->image = image;
    x11grab->use_shm = use_shm;
    x11grab->mouse_warning_shown = 0;

    st->codec->codec_type = CODEC_TYPE_VIDEO;
    st->codec->codec_id = CODEC_ID_RAWVIDEO;
    st->codec->width = ap->width;
    st->codec->height = ap->height;
    st->codec->pix_fmt = input_pixfmt;
    st->codec->time_base = ap->time_base;
    st->codec->bit_rate = x11grab->frame_size * 1/av_q2d(ap->time_base) * 8;

    return 0;
}

/**
 * Get pointer coordinates from X11.
 *
 * @param x Integer where horizontal coordinate will be returned
 * @param y Integer where vertical coordinate will be returned
 * @param dpy X11 display from where pointer coordinates are retrieved
 * @param s1 Context used for logging errors if necessary
 */
static void
get_pointer_coordinates(int *x, int *y, Display *dpy, AVFormatContext *s1)
{
    Window mrootwindow, childwindow;
    int dummy;

    mrootwindow = DefaultRootWindow(dpy);

    if (XQueryPointer(dpy, mrootwindow, &mrootwindow, &childwindow,
                      x, y, &dummy, &dummy, (unsigned int*)&dummy)) {
    } else {
        struct x11_grab *s = s1->priv_data;
        if (!s->mouse_warning_shown) {
            av_log(s1, AV_LOG_INFO, "couldn't find mouse pointer\n");
            s->mouse_warning_shown = 1;
        }
        *x = -1;
        *y = -1;
    }
}

/**
 * Mouse painting helper function that applies an 'and' and 'or' mask pair to
 * '*dst' pixel. It actually draws a mouse pointer pixel to grabbed frame.
 *
 * @param dst Destination pixel
 * @param and Part of the mask that must be applied using a bitwise 'and'
 *            operator
 * @param or  Part of the mask that must be applied using a bitwise 'or'
 *            operator
 * @param bits_per_pixel Bits per pixel used in the grabbed image
 */
static void inline
apply_masks(uint8_t *dst, int and, int or, int bits_per_pixel)
{
    switch (bits_per_pixel) {
    case 32:
        *(uint32_t*)dst = (*(uint32_t*)dst & and) | or;
        break;
    case 16:
        *(uint16_t*)dst = (*(uint16_t*)dst & and) | or;
        break;
    case 8:
        *dst = !!or;
        break;
    }
}

/**
 * Paints a mouse pointer in an X11 image.
 *
 * @param image image to paint the mouse pointer to
 * @param s context used to retrieve original grabbing rectangle
 *          coordinates
 * @param x Mouse pointer coordinate
 * @param y Mouse pointer coordinate
 */
static void
paint_mouse_pointer(XImage *image, struct x11_grab *s, int x, int y)
{
    /* 16x20x1bpp bitmap for the black channel of the mouse pointer */
    static const uint16_t const mousePointerBlack[] =
        {
            0x0000, 0x0003, 0x0005, 0x0009, 0x0011,
            0x0021, 0x0041, 0x0081, 0x0101, 0x0201,
            0x03c1, 0x0049, 0x0095, 0x0093, 0x0120,
            0x0120, 0x0240, 0x0240, 0x0380, 0x0000
        };

    /* 16x20x1bpp bitmap for the white channel of the mouse pointer */
    static const uint16_t const mousePointerWhite[] =
        {
            0x0000, 0x0000, 0x0002, 0x0006, 0x000e,
            0x001e, 0x003e, 0x007e, 0x00fe, 0x01fe,
            0x003e, 0x0036, 0x0062, 0x0060, 0x00c0,
            0x00c0, 0x0180, 0x0180, 0x0000, 0x0000
        };

    int x_off = s->x_off;
    int y_off = s->y_off;
    int width = s->width;
    int height = s->height;

    if (   x - x_off >= 0 && x < width + x_off
        && y - y_off >= 0 && y < height + y_off) {
        uint8_t *im_data = (uint8_t*)image->data;
        int bytes_per_pixel;
        int line;
        int masks;

        /* Select correct masks and pixel size */
        if (image->bits_per_pixel == 8) {
            masks = 1;
        } else {
            masks = (image->red_mask|image->green_mask|image->blue_mask);
        }
        bytes_per_pixel = image->bits_per_pixel>>3;

        /* Shift to right line */
        im_data += image->bytes_per_line * (y - y_off);
        /* Shift to right pixel in the line */
        im_data += bytes_per_pixel * (x - x_off);

        /* Draw the cursor - proper loop */
        for (line = 0; line < FFMIN(20, (y_off + height) - y); line++) {
            uint8_t *cursor = im_data;
            int column;
            uint16_t bm_b;
            uint16_t bm_w;

            bm_b = mousePointerBlack[line];
            bm_w = mousePointerWhite[line];

            for (column = 0; column < FFMIN(16, (x_off + width) - x); column++) {
                apply_masks(cursor, ~(masks*(bm_b&1)), masks*(bm_w&1),
                            image->bits_per_pixel);
                cursor += bytes_per_pixel;
                bm_b >>= 1;
                bm_w >>= 1;
            }
            im_data += image->bytes_per_line;
        }
    }
}


/**
 * Reads new data in the image structure.
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
 * Grabs a frame from x11 (public device demuxer API).
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

    if (av_new_packet(pkt, s->frame_size) < 0) {
        return AVERROR(EIO);
    }

    pkt->pts = curtime;

    if(s->use_shm) {
        if (!XShmGetImage(dpy, RootWindow(dpy, DefaultScreen(dpy)), image, x_off, y_off, AllPlanes)) {
            av_log (s1, AV_LOG_INFO, "XShmGetImage() failed\n");
        }
    } else {
        if (!xget_zpixmap(dpy, RootWindow(dpy, DefaultScreen(dpy)), image, x_off, y_off)) {
            av_log (s1, AV_LOG_INFO, "XGetZPixmap() failed\n");
        }
    }

    {
        int pointer_x, pointer_y;
        get_pointer_coordinates(&pointer_x, &pointer_y, dpy, s1);
        paint_mouse_pointer(image, s, pointer_x, pointer_y);
    }


    /* XXX: avoid memcpy */
    memcpy(pkt->data, image->data, s->frame_size);
    return s->frame_size;
}

/**
 * Closes x11 frame grabber (public device demuxer API).
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

    /* Free X11 display */
    XCloseDisplay(x11grab->dpy);
    return 0;
}

/** x11 grabber device demuxer declaration */
AVInputFormat x11_grab_device_demuxer =
{
    "x11grab",
    NULL_IF_CONFIG_SMALL("X11grab"),
    sizeof(struct x11_grab),
    NULL,
    x11grab_read_header,
    x11grab_read_packet,
    x11grab_read_close,
    .flags = AVFMT_NOFILE,
};
