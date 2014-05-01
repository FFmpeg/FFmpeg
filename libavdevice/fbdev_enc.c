/*
 * Copyright (c) 2013 Lukasz Marek
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

#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include "libavutil/pixdesc.h"
#include "libavutil/log.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavformat/avformat.h"
#include "fbdev_common.h"
#include "avdevice.h"

typedef struct {
    AVClass *class;                   ///< class for private options
    int xoffset;                      ///< x coordinate of top left corner
    int yoffset;                      ///< y coordinate of top left corner
    struct fb_var_screeninfo varinfo; ///< framebuffer variable info
    struct fb_fix_screeninfo fixinfo; ///< framebuffer fixed info
    int fd;                           ///< framebuffer device file descriptor
    uint8_t *data;                    ///< framebuffer data
} FBDevContext;

static av_cold int fbdev_write_header(AVFormatContext *h)
{
    FBDevContext *fbdev = h->priv_data;
    enum AVPixelFormat pix_fmt;
    int ret, flags = O_RDWR;
    const char* device;

    if (h->nb_streams != 1 || h->streams[0]->codec->codec_type != AVMEDIA_TYPE_VIDEO) {
        av_log(fbdev, AV_LOG_ERROR, "Only a single video stream is supported.\n");
        return AVERROR(EINVAL);
    }

    if (h->filename[0])
        device = h->filename;
    else
        device = ff_fbdev_default_device();

    if ((fbdev->fd = avpriv_open(device, flags)) == -1) {
        ret = AVERROR(errno);
        av_log(h, AV_LOG_ERROR,
               "Could not open framebuffer device '%s': %s\n",
               device, av_err2str(ret));
        return ret;
    }

    if (ioctl(fbdev->fd, FBIOGET_VSCREENINFO, &fbdev->varinfo) < 0) {
        ret = AVERROR(errno);
        av_log(h, AV_LOG_ERROR, "FBIOGET_VSCREENINFO: %s\n", av_err2str(ret));
        goto fail;
    }

    if (ioctl(fbdev->fd, FBIOGET_FSCREENINFO, &fbdev->fixinfo) < 0) {
        ret = AVERROR(errno);
        av_log(h, AV_LOG_ERROR, "FBIOGET_FSCREENINFO: %s\n", av_err2str(ret));
        goto fail;
    }

    pix_fmt = ff_get_pixfmt_from_fb_varinfo(&fbdev->varinfo);
    if (pix_fmt == AV_PIX_FMT_NONE) {
        ret = AVERROR(EINVAL);
        av_log(h, AV_LOG_ERROR, "Framebuffer pixel format not supported.\n");
        goto fail;
    }

    fbdev->data = mmap(NULL, fbdev->fixinfo.smem_len, PROT_WRITE, MAP_SHARED, fbdev->fd, 0);
    if (fbdev->data == MAP_FAILED) {
        ret = AVERROR(errno);
        av_log(h, AV_LOG_ERROR, "Error in mmap(): %s\n", av_err2str(ret));
        goto fail;
    }

    return 0;
  fail:
    close(fbdev->fd);
    return ret;
}

static int fbdev_write_packet(AVFormatContext *h, AVPacket *pkt)
{
    FBDevContext *fbdev = h->priv_data;
    uint8_t *pin, *pout;
    enum AVPixelFormat fb_pix_fmt;
    int disp_height;
    int bytes_to_copy;
    AVCodecContext *codec_ctx = h->streams[0]->codec;
    enum AVPixelFormat video_pix_fmt = codec_ctx->pix_fmt;
    int video_width = codec_ctx->width;
    int video_height = codec_ctx->height;
    int bytes_per_pixel = ((codec_ctx->bits_per_coded_sample + 7) >> 3);
    int src_line_size = video_width * bytes_per_pixel;
    int i;

    if (ioctl(fbdev->fd, FBIOGET_VSCREENINFO, &fbdev->varinfo) < 0)
        av_log(h, AV_LOG_WARNING,
               "Error refreshing variable info: %s\n", av_err2str(AVERROR(errno)));

    fb_pix_fmt = ff_get_pixfmt_from_fb_varinfo(&fbdev->varinfo);

    if (fb_pix_fmt != video_pix_fmt) {
        av_log(h, AV_LOG_ERROR, "Pixel format %s is not supported, use %s\n",
               av_get_pix_fmt_name(video_pix_fmt), av_get_pix_fmt_name(fb_pix_fmt));
        return AVERROR(EINVAL);
    }

    disp_height = FFMIN(fbdev->varinfo.yres, video_height);
    bytes_to_copy = FFMIN(fbdev->varinfo.xres, video_width) * bytes_per_pixel;

    pin  = pkt->data;
    pout = fbdev->data +
           bytes_per_pixel * fbdev->varinfo.xoffset +
           fbdev->varinfo.yoffset * fbdev->fixinfo.line_length;

    if (fbdev->xoffset) {
        if (fbdev->xoffset < 0) {
            if (-fbdev->xoffset >= video_width) //nothing to display
                return 0;
            bytes_to_copy += fbdev->xoffset * bytes_per_pixel;
            pin -= fbdev->xoffset * bytes_per_pixel;
        } else {
            int diff = (video_width + fbdev->xoffset) - fbdev->varinfo.xres;
            if (diff > 0) {
                if (diff >= video_width) //nothing to display
                    return 0;
                bytes_to_copy -= diff * bytes_per_pixel;
            }
            pout += bytes_per_pixel * fbdev->xoffset;
        }
    }

    if (fbdev->yoffset) {
        if (fbdev->yoffset < 0) {
            if (-fbdev->yoffset >= video_height) //nothing to display
                return 0;
            disp_height += fbdev->yoffset;
            pin -= fbdev->yoffset * src_line_size;
        } else {
            int diff = (video_height + fbdev->yoffset) - fbdev->varinfo.yres;
            if (diff > 0) {
                if (diff >= video_height) //nothing to display
                    return 0;
                disp_height -= diff;
            }
            pout += fbdev->yoffset * fbdev->fixinfo.line_length;
        }
    }

    for (i = 0; i < disp_height; i++) {
        memcpy(pout, pin, bytes_to_copy);
        pout += fbdev->fixinfo.line_length;
        pin  += src_line_size;
    }

    return 0;
}

static av_cold int fbdev_write_trailer(AVFormatContext *h)
{
    FBDevContext *fbdev = h->priv_data;
    munmap(fbdev->data, fbdev->fixinfo.smem_len);
    close(fbdev->fd);
    return 0;
}

static int fbdev_get_device_list(AVFormatContext *s, AVDeviceInfoList *device_list)
{
    return ff_fbdev_get_device_list(device_list);
}

#define OFFSET(x) offsetof(FBDevContext, x)
#define ENC AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "xoffset", "set x coordinate of top left corner", OFFSET(xoffset), AV_OPT_TYPE_INT, {.i64 = 0}, INT_MIN, INT_MAX, ENC },
    { "yoffset", "set y coordinate of top left corner", OFFSET(yoffset), AV_OPT_TYPE_INT, {.i64 = 0}, INT_MIN, INT_MAX, ENC },
    { NULL }
};

static const AVClass fbdev_class = {
    .class_name = "fbdev outdev",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
    .category   = AV_CLASS_CATEGORY_DEVICE_VIDEO_OUTPUT,
};

AVOutputFormat ff_fbdev_muxer = {
    .name           = "fbdev",
    .long_name      = NULL_IF_CONFIG_SMALL("Linux framebuffer"),
    .priv_data_size = sizeof(FBDevContext),
    .audio_codec    = AV_CODEC_ID_NONE,
    .video_codec    = AV_CODEC_ID_RAWVIDEO,
    .write_header   = fbdev_write_header,
    .write_packet   = fbdev_write_packet,
    .write_trailer  = fbdev_write_trailer,
    .get_device_list = fbdev_get_device_list,
    .flags          = AVFMT_NOFILE | AVFMT_VARIABLE_FPS | AVFMT_NOTIMESTAMPS,
    .priv_class     = &fbdev_class,
};
