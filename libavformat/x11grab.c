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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "avformat.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
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

typedef struct
{
    Display *dpy;
    int frame_format;
    int frame_size;
    int frame_rate;
    int frame_rate_base;
    int64_t time_frame;

    int height;
    int width;
    int x_off;
    int y_off;
    XImage *image;
    int use_shm;
    XShmSegmentInfo shminfo;
    int mouse_wanted;
} X11Grab;

static int
x11grab_read_header(AVFormatContext *s1, AVFormatParameters *ap)
{
    X11Grab *x11grab = s1->priv_data;
    Display *dpy;
    AVStream *st = NULL;
    int width, height;
    int frame_rate, frame_rate_base, frame_size;
    int input_pixfmt;
    XImage *image;
    int x_off=0; int y_off = 0;
    int use_shm;

    dpy = XOpenDisplay(NULL);
    if(!dpy) {
        goto fail;
    }

    sscanf(ap->device, "x11:%d,%d", &x_off, &y_off);
    av_log(s1, AV_LOG_INFO, "device: %s -> x: %d y: %d width: %d height: %d\n", ap->device, x_off, y_off, ap->width, ap->height);

    if (!ap || ap->width <= 0 || ap->height <= 0 || ap->time_base.den <= 0) {
        av_log(s1, AV_LOG_ERROR, "AVParameters don't have any video size. Use -s.\n");
        return AVERROR_IO;
    }

    width = ap->width;
    height = ap->height;
    frame_rate = ap->time_base.den;
    frame_rate_base = ap->time_base.num;

    st = av_new_stream(s1, 0);
    if (!st) {
        return -ENOMEM;
    }
    av_set_pts_info(st, 64, 1, 1000000); /* 64 bits pts in us */

    use_shm = XShmQueryExtension(dpy);
    av_log(s1, AV_LOG_INFO, "shared memory extension %s\n", use_shm ? "found" : "not found");

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
            return -ENOMEM;
        }
        x11grab->shminfo.shmaddr = image->data = shmat(x11grab->shminfo.shmid, 0, 0);
        x11grab->shminfo.readOnly = False;

        if (!XShmAttach(dpy, &x11grab->shminfo)) {
            av_log(s1, AV_LOG_ERROR, "Fatal: Failed to attach shared memory!\n");
            /* needs some better error subroutine :) */
            return AVERROR_IO;
        }
    } else {
        image = XGetImage(dpy, RootWindow(dpy, DefaultScreen(dpy)),
                          x_off,y_off,
                          ap->width,ap->height,
                          AllPlanes, ZPixmap);
    }

    switch (image->bits_per_pixel) {
    case 8:
        av_log (s1, AV_LOG_DEBUG, "8 bit pallete\n");
        input_pixfmt = PIX_FMT_PAL8;
        break;
    case 16:
        if ( image->red_mask == 0xF800 && image->green_mask == 0x07E0
             && image->blue_mask == 0x1F ) {
            av_log (s1, AV_LOG_DEBUG, "16 bit RGB565\n");
            input_pixfmt = PIX_FMT_RGB565;
        } else if ( image->red_mask == 0x7C00 &&
                    image->green_mask == 0x03E0 &&
                    image->blue_mask == 0x1F ) {
            av_log(s1, AV_LOG_DEBUG, "16 bit RGB555\n");
            input_pixfmt = PIX_FMT_RGB555;
        } else {
            av_log(s1, AV_LOG_ERROR, "RGB ordering at image depth %i not supported ... aborting\n", image->bits_per_pixel);
            av_log(s1, AV_LOG_ERROR, "color masks: r 0x%.6lx g 0x%.6lx b 0x%.6lx\n", image->red_mask, image->green_mask, image->blue_mask);
            return AVERROR_IO;
        }
        break;
    case 24:
        if ( image->red_mask == 0xFF0000 &&
             image->green_mask == 0xFF00
             && image->blue_mask == 0xFF ) {
            input_pixfmt = PIX_FMT_BGR24;
        } else if ( image->red_mask == 0xFF && image->green_mask == 0xFF00
                    && image->blue_mask == 0xFF0000 ) {
            input_pixfmt = PIX_FMT_RGB24;
        } else {
            av_log(s1, AV_LOG_ERROR,"rgb ordering at image depth %i not supported ... aborting\n", image->bits_per_pixel);
            av_log(s1, AV_LOG_ERROR, "color masks: r 0x%.6lx g 0x%.6lx b 0x%.6lx\n", image->red_mask, image->green_mask, image->blue_mask);
            return AVERROR_IO;
        }
        break;
    case 32:
#if 0
        GetColorInfo (image, &c_info);
        if ( c_info.alpha_mask == 0xFF000000 && image->green_mask == 0xFF00 ) {
            /* byte order is relevant here, not endianness
             * endianness is handled by avcodec, but atm no such thing
             * as having ABGR, instead of ARGB in a word. Since we
             * need this for Solaris/SPARC, but need to do the conversion
             * for every frame we do it outside of this loop, cf. below
             * this matches both ARGB32 and ABGR32 */
            input_pixfmt = PIX_FMT_ARGB32;
        }  else {
            av_log(s1, AV_LOG_ERROR,"image depth %i not supported ... aborting\n", image->bits_per_pixel);
            return AVERROR_IO;
        }
#endif
        input_pixfmt = PIX_FMT_RGBA32;
        break;
    default:
        av_log(s1, AV_LOG_ERROR, "image depth %i not supported ... aborting\n", image->bits_per_pixel);
        return -1;
    }

    frame_size = width * height * image->bits_per_pixel/8;
    x11grab->frame_size = frame_size;
    x11grab->dpy = dpy;
    x11grab->width = ap->width;
    x11grab->height = ap->height;
    x11grab->frame_rate      = frame_rate;
    x11grab->frame_rate_base = frame_rate_base;
    x11grab->time_frame = av_gettime() * frame_rate / frame_rate_base;
    x11grab->x_off = x_off;
    x11grab->y_off = y_off;
    x11grab->image = image;
    x11grab->use_shm = use_shm;
    x11grab->mouse_wanted = 1;

    st->codec->codec_type = CODEC_TYPE_VIDEO;
    st->codec->codec_id = CODEC_ID_RAWVIDEO;
    st->codec->width = width;
    st->codec->height = height;
    st->codec->pix_fmt = input_pixfmt;
    st->codec->time_base.den = frame_rate;
    st->codec->time_base.num = frame_rate_base;
    st->codec->bit_rate = frame_size * 1/av_q2d(st->codec->time_base) * 8;

    return 0;
fail:
    av_free(st);
    return AVERROR_IO;
}

static void
getCurrentPointer(AVFormatContext *s1, X11Grab *s, int *x, int *y)
{
    Window mrootwindow, childwindow;
    int dummy;
    Display *dpy = s->dpy;

    mrootwindow = DefaultRootWindow(dpy);

    if (XQueryPointer(dpy, mrootwindow, &mrootwindow, &childwindow,
                      x, y, &dummy, &dummy, (unsigned int*)&dummy)) {
    } else {
        av_log(s1, AV_LOG_INFO, "couldn't find mouse pointer\n");
        *x = -1;
        *y = -1;
    }
}

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
        *dst = (or) ? 1 : 0;
        break;
    }
}

static void
paintMousePointer(AVFormatContext *s1, X11Grab *s, int *x, int *y, XImage *image)
{
    static const uint16_t const mousePointerBlack[] =
        {
            0, 49152, 40960, 36864, 34816,
            33792, 33280, 33024, 32896, 32832,
            33728, 37376, 43264, 51456, 1152,
            1152, 576, 576, 448, 0
        };

    static const uint16_t const mousePointerWhite[] =
        {
            0, 0, 16384, 24576, 28672,
            30720, 31744, 32256, 32512, 32640,
            31744, 27648, 17920, 1536, 768,
            768, 384, 384, 0, 0
        };

    int x_off = s->x_off;
    int y_off = s->y_off;
    int width = s->width;
    int height = s->height;

    if (   (*x - x_off) >= 0 && *x < (width + x_off)
           && (*y - y_off) >= 0 && *y < (height + y_off) ) {
        int line;
        uint8_t *im_data = (uint8_t*)image->data;
        const uint16_t *black;
        const uint16_t *white;
        int masks;
        int onepixel;

        /* Select correct pointer pixels */
        if (s->mouse_wanted == 1) {
            /* Normal pointer */
            black = mousePointerBlack;
            white = mousePointerWhite;
        } else {
            /* Inverted pointer */
            black = mousePointerWhite;
            white = mousePointerBlack;
        }

        /* Select correct masks and pixel size */
        switch (image->bits_per_pixel) {
        case 32:
            masks = (image->red_mask|image->green_mask|image->blue_mask);
            onepixel = 4;
            break;
        case 24:
            /* XXX: Though the code seems to support 24bit images, the
             * apply_masks lacks support for 24bit */
            masks = (image->red_mask|image->green_mask|image->blue_mask);
            onepixel = 3;
            break;
        case 16:
            masks = (image->red_mask|image->green_mask|image->blue_mask);
            onepixel = 2;
            break;
        case 8:
            masks = 1;
            onepixel = 1;
            break;
        default:
            /* Shut up gcc */
            masks = 0;
            onepixel = 0;
        }

        /* Shift to right line */
        im_data += (image->bytes_per_line * (*y - y_off));
        /* Shift to right pixel */
        im_data += (image->bits_per_pixel / 8 * (*x - x_off));

        /* Draw the cursor - proper loop */
        for (line = 0; line < min(20, (y_off + height) - *y); line++) {
            uint8_t *cursor = im_data;
            int width_cursor;
            uint16_t bm_b;
            uint16_t bm_w;

            bm_b = black[line];
            bm_w = white[line];

            for (width_cursor=0;
                 width_cursor < 16 && (width_cursor + *x) < (width + x_off);
                 width_cursor++) {
                apply_masks(cursor,
                            ~(masks*(bm_b&1)), masks*(bm_w&1),
                            image->bits_per_pixel);
                cursor += onepixel;
                bm_b >>= 1;
                bm_w >>= 1;
            }
            im_data += image->bytes_per_line;
        }
    }
}


/*
 * just read new data in the image structure, the image
 * structure inclusive the data area must be allocated before
 */
static int
XGetZPixmap(Display *dpy, Drawable d, XImage *image, int x, int y)
{
    xGetImageReply rep;
    xGetImageReq *req;
    long nbytes;

    if (!image) {
        return False;
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

    if (!_XReply(dpy, (xReply *) &rep, 0, xFalse) || !rep.length) {
        UnlockDisplay(dpy);
        SyncHandle();
        return False;
    }

    nbytes = (long)rep.length << 2;
    _XReadPad(dpy, image->data, nbytes);

    UnlockDisplay(dpy);
    SyncHandle();
    return True;
}

static int
x11grab_read_packet(AVFormatContext *s1, AVPacket *pkt)
{
    X11Grab *s = s1->priv_data;
    Display *dpy = s->dpy;
    XImage *image = s->image;
    int x_off = s->x_off;
    int y_off = s->y_off;

    int64_t curtime, delay;
    struct timespec ts;

    /* Calculate the time of the next frame */
    s->time_frame += int64_t_C(1000000);

    /* wait based on the frame rate */
    for(;;) {
        curtime = av_gettime();
        delay = s->time_frame  * s->frame_rate_base / s->frame_rate - curtime;
        if (delay <= 0) {
            if (delay < int64_t_C(-1000000) * s->frame_rate_base / s->frame_rate) {
                s->time_frame += int64_t_C(1000000);
            }
            break;
        }
        ts.tv_sec = delay / 1000000;
        ts.tv_nsec = (delay % 1000000) * 1000;
        nanosleep(&ts, NULL);
    }

    if (av_new_packet(pkt, s->frame_size) < 0) {
        return AVERROR_IO;
    }

    pkt->pts = curtime & ((1LL << 48) - 1);

    if(s->use_shm) {
        if (!XShmGetImage(dpy, RootWindow(dpy, DefaultScreen(dpy)), image, x_off, y_off, AllPlanes)) {
            av_log (s1, AV_LOG_INFO, "XShmGetImage() failed\n");
        }
    } else {
        if (!XGetZPixmap(dpy, RootWindow(dpy, DefaultScreen(dpy)), image, x_off, y_off)) {
            av_log (s1, AV_LOG_INFO, "XGetZPixmap() failed\n");
        }
    }

    {
        int pointer_x, pointer_y;
        getCurrentPointer(s1, s, &pointer_x, &pointer_y);
        paintMousePointer(s1, s, &pointer_x, &pointer_y, image);
    }


    /* XXX: avoid memcpy */
    memcpy(pkt->data, image->data, s->frame_size);
    return s->frame_size;
}

static int
x11grab_read_close(AVFormatContext *s1)
{
    X11Grab *x11grab = s1->priv_data;

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

AVInputFormat x11_grab_device_demuxer =
{
    "x11grab",
    "X11grab",
    sizeof(X11Grab),
    NULL,
    x11grab_read_header,
    x11grab_read_packet,
    x11grab_read_close,
    .flags = AVFMT_NOFILE,
};
