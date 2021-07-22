/*
 * Copyright (c) 2011 Stefano Sabatini
 * Copyright (c) 2009 Giliard B. de Freitas <giliarde@gmail.com>
 * Copyright (C) 2002 Gunnar Monell <gmo@linux.nu>
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
 * Linux framebuffer input device,
 * inspired by code from fbgrab.c by Gunnar Monell.
 * @see http://linux-fbdev.sourceforge.net/
 */

#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <linux/fb.h>

#include "libavutil/internal.h"
#include "libavutil/log.h"
#include "libavutil/opt.h"
#include "libavutil/time.h"
#include "libavutil/parseutils.h"
#include "libavutil/pixdesc.h"
#include "libavformat/internal.h"
#include "avdevice.h"
#include "fbdev_common.h"

typedef struct FBDevContext {
    AVClass *class;          ///< class for private options
    int frame_size;          ///< size in bytes of a grabbed frame
    AVRational framerate_q;  ///< framerate
    int64_t time_frame;      ///< time for the next frame to output (in 1/1000000 units)

    int fd;                  ///< framebuffer device file descriptor
    int width, height;       ///< assumed frame resolution
    int frame_linesize;      ///< linesize of the output frame, it is assumed to be constant
    int bytes_per_pixel;

    struct fb_var_screeninfo varinfo; ///< variable info;
    struct fb_fix_screeninfo fixinfo; ///< fixed    info;

    uint8_t *data;           ///< framebuffer data
} FBDevContext;

static av_cold int fbdev_read_header(AVFormatContext *avctx)
{
    FBDevContext *fbdev = avctx->priv_data;
    AVStream *st = NULL;
    enum AVPixelFormat pix_fmt;
    int ret, flags = O_RDONLY;
    const char* device;

    if (!(st = avformat_new_stream(avctx, NULL)))
        return AVERROR(ENOMEM);
    avpriv_set_pts_info(st, 64, 1, 1000000); /* 64 bits pts in microseconds */

    /* NONBLOCK is ignored by the fbdev driver, only set for consistency */
    if (avctx->flags & AVFMT_FLAG_NONBLOCK)
        flags |= O_NONBLOCK;

    if (avctx->url[0])
        device = avctx->url;
    else
        device = ff_fbdev_default_device();

    if ((fbdev->fd = avpriv_open(device, flags)) == -1) {
        ret = AVERROR(errno);
        av_log(avctx, AV_LOG_ERROR,
               "Could not open framebuffer device '%s': %s\n",
               device, av_err2str(ret));
        return ret;
    }

    if (ioctl(fbdev->fd, FBIOGET_VSCREENINFO, &fbdev->varinfo) < 0) {
        ret = AVERROR(errno);
        av_log(avctx, AV_LOG_ERROR,
               "FBIOGET_VSCREENINFO: %s\n", av_err2str(ret));
        goto fail;
    }

    if (ioctl(fbdev->fd, FBIOGET_FSCREENINFO, &fbdev->fixinfo) < 0) {
        ret = AVERROR(errno);
        av_log(avctx, AV_LOG_ERROR,
               "FBIOGET_FSCREENINFO: %s\n", av_err2str(ret));
        goto fail;
    }

    pix_fmt = ff_get_pixfmt_from_fb_varinfo(&fbdev->varinfo);
    if (pix_fmt == AV_PIX_FMT_NONE) {
        ret = AVERROR(EINVAL);
        av_log(avctx, AV_LOG_ERROR,
               "Framebuffer pixel format not supported.\n");
        goto fail;
    }

    fbdev->width           = fbdev->varinfo.xres;
    fbdev->height          = fbdev->varinfo.yres;
    fbdev->bytes_per_pixel = (fbdev->varinfo.bits_per_pixel + 7) >> 3;
    fbdev->frame_linesize  = fbdev->width * fbdev->bytes_per_pixel;
    fbdev->frame_size      = fbdev->frame_linesize * fbdev->height;
    fbdev->time_frame      = AV_NOPTS_VALUE;
    fbdev->data = mmap(NULL, fbdev->fixinfo.smem_len, PROT_READ, MAP_SHARED, fbdev->fd, 0);
    if (fbdev->data == MAP_FAILED) {
        ret = AVERROR(errno);
        av_log(avctx, AV_LOG_ERROR, "Error in mmap(): %s\n", av_err2str(ret));
        goto fail;
    }

    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_id   = AV_CODEC_ID_RAWVIDEO;
    st->codecpar->width      = fbdev->width;
    st->codecpar->height     = fbdev->height;
    st->codecpar->format     = pix_fmt;
    st->avg_frame_rate       = fbdev->framerate_q;
    st->codecpar->bit_rate   =
        fbdev->width * fbdev->height * fbdev->bytes_per_pixel * av_q2d(fbdev->framerate_q) * 8;

    av_log(avctx, AV_LOG_INFO,
           "w:%d h:%d bpp:%d pixfmt:%s fps:%d/%d bit_rate:%"PRId64"\n",
           fbdev->width, fbdev->height, fbdev->varinfo.bits_per_pixel,
           av_get_pix_fmt_name(pix_fmt),
           fbdev->framerate_q.num, fbdev->framerate_q.den,
           st->codecpar->bit_rate);
    return 0;

fail:
    close(fbdev->fd);
    return ret;
}

static int fbdev_read_packet(AVFormatContext *avctx, AVPacket *pkt)
{
    FBDevContext *fbdev = avctx->priv_data;
    int64_t curtime, delay;
    struct timespec ts;
    int i, ret;
    uint8_t *pin, *pout;

    if (fbdev->time_frame == AV_NOPTS_VALUE)
        fbdev->time_frame = av_gettime_relative();

    /* wait based on the frame rate */
    while (1) {
        curtime = av_gettime_relative();
        delay = fbdev->time_frame - curtime;
        av_log(avctx, AV_LOG_TRACE,
                "time_frame:%"PRId64" curtime:%"PRId64" delay:%"PRId64"\n",
                fbdev->time_frame, curtime, delay);
        if (delay <= 0) {
            fbdev->time_frame += INT64_C(1000000) / av_q2d(fbdev->framerate_q);
            break;
        }
        if (avctx->flags & AVFMT_FLAG_NONBLOCK)
            return AVERROR(EAGAIN);
        ts.tv_sec  =  delay / 1000000;
        ts.tv_nsec = (delay % 1000000) * 1000;
        while (nanosleep(&ts, &ts) < 0 && errno == EINTR);
    }

    if ((ret = av_new_packet(pkt, fbdev->frame_size)) < 0)
        return ret;

    /* refresh fbdev->varinfo, visible data position may change at each call */
    if (ioctl(fbdev->fd, FBIOGET_VSCREENINFO, &fbdev->varinfo) < 0) {
        av_log(avctx, AV_LOG_WARNING,
               "Error refreshing variable info: %s\n", av_err2str(AVERROR(errno)));
    }

    pkt->pts = av_gettime();

    /* compute visible data offset */
    pin = fbdev->data + fbdev->bytes_per_pixel * fbdev->varinfo.xoffset +
                        fbdev->varinfo.yoffset * fbdev->fixinfo.line_length;
    pout = pkt->data;

    for (i = 0; i < fbdev->height; i++) {
        memcpy(pout, pin, fbdev->frame_linesize);
        pin  += fbdev->fixinfo.line_length;
        pout += fbdev->frame_linesize;
    }

    return fbdev->frame_size;
}

static av_cold int fbdev_read_close(AVFormatContext *avctx)
{
    FBDevContext *fbdev = avctx->priv_data;

    munmap(fbdev->data, fbdev->fixinfo.smem_len);
    close(fbdev->fd);

    return 0;
}

static int fbdev_get_device_list(AVFormatContext *s, AVDeviceInfoList *device_list)
{
    return ff_fbdev_get_device_list(device_list);
}

#define OFFSET(x) offsetof(FBDevContext, x)
#define DEC AV_OPT_FLAG_DECODING_PARAM
static const AVOption options[] = {
    { "framerate","", OFFSET(framerate_q), AV_OPT_TYPE_VIDEO_RATE, {.str = "25"}, 0, INT_MAX, DEC },
    { NULL },
};

static const AVClass fbdev_class = {
    .class_name = "fbdev indev",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
    .category   = AV_CLASS_CATEGORY_DEVICE_VIDEO_INPUT,
};

const AVInputFormat ff_fbdev_demuxer = {
    .name           = "fbdev",
    .long_name      = NULL_IF_CONFIG_SMALL("Linux framebuffer"),
    .priv_data_size = sizeof(FBDevContext),
    .read_header    = fbdev_read_header,
    .read_packet    = fbdev_read_packet,
    .read_close     = fbdev_read_close,
    .get_device_list = fbdev_get_device_list,
    .flags          = AVFMT_NOFILE,
    .priv_class     = &fbdev_class,
};
