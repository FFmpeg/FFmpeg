/*
 * Copyright (c) 2013 Clément Bœsch
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

#include "libavutil/imgutils.h"
#include "libavutil/pixdesc.h"
#include "libavformat/avformat.h"
#include "libavformat/mux.h"
#include "v4l2-common.h"

typedef struct {
    AVClass *class;
    int fd;
} V4L2Context;

static av_cold int write_header(AVFormatContext *s1)
{
    int res = 0, flags = O_RDWR;
    struct v4l2_format fmt = {
        .type = V4L2_BUF_TYPE_VIDEO_OUTPUT
    };
    V4L2Context *s = s1->priv_data;
    AVCodecParameters *par;
    uint32_t v4l2_pixfmt;

    if (s1->flags & AVFMT_FLAG_NONBLOCK)
        flags |= O_NONBLOCK;

    s->fd = open(s1->url, flags);
    if (s->fd < 0) {
        res = AVERROR(errno);
        av_log(s1, AV_LOG_ERROR, "Unable to open V4L2 device '%s'\n", s1->url);
        return res;
    }

    if (s1->nb_streams != 1 ||
        s1->streams[0]->codecpar->codec_type != AVMEDIA_TYPE_VIDEO) {
        av_log(s1, AV_LOG_ERROR,
               "V4L2 output device supports only a single raw video stream\n");
        return AVERROR(EINVAL);
    }

    par = s1->streams[0]->codecpar;

    if(par->codec_id == AV_CODEC_ID_RAWVIDEO) {
        v4l2_pixfmt = ff_fmt_ff2v4l(par->format, AV_CODEC_ID_RAWVIDEO);
    } else {
        v4l2_pixfmt = ff_fmt_ff2v4l(AV_PIX_FMT_NONE, par->codec_id);
    }

    if (!v4l2_pixfmt) { // XXX: try to force them one by one?
        av_log(s1, AV_LOG_ERROR, "Unknown V4L2 pixel format equivalent for %s\n",
               av_get_pix_fmt_name(par->format));
        return AVERROR(EINVAL);
    }

    if (ioctl(s->fd, VIDIOC_G_FMT, &fmt) < 0) {
        res = AVERROR(errno);
        av_log(s1, AV_LOG_ERROR, "ioctl(VIDIOC_G_FMT): %s\n", av_err2str(res));
        return res;
    }

    fmt.fmt.pix.width       = par->width;
    fmt.fmt.pix.height      = par->height;
    fmt.fmt.pix.pixelformat = v4l2_pixfmt;
    fmt.fmt.pix.sizeimage   = av_image_get_buffer_size(par->format, par->width, par->height, 1);

    if (ioctl(s->fd, VIDIOC_S_FMT, &fmt) < 0) {
        res = AVERROR(errno);
        av_log(s1, AV_LOG_ERROR, "ioctl(VIDIOC_S_FMT): %s\n", av_err2str(res));
        return res;
    }

    return res;
}

static int write_packet(AVFormatContext *s1, AVPacket *pkt)
{
    const V4L2Context *s = s1->priv_data;
    if (write(s->fd, pkt->data, pkt->size) == -1)
        return AVERROR(errno);
    return 0;
}

static int write_trailer(AVFormatContext *s1)
{
    const V4L2Context *s = s1->priv_data;
    close(s->fd);
    return 0;
}

static const AVClass v4l2_class = {
    .class_name = "V4L2 outdev",
    .item_name  = av_default_item_name,
    .version    = LIBAVUTIL_VERSION_INT,
    .category   = AV_CLASS_CATEGORY_DEVICE_VIDEO_OUTPUT,
};

const FFOutputFormat ff_v4l2_muxer = {
    .p.name         = "video4linux2,v4l2",
    .p.long_name    = NULL_IF_CONFIG_SMALL("Video4Linux2 output device"),
    .priv_data_size = sizeof(V4L2Context),
    .p.audio_codec  = AV_CODEC_ID_NONE,
    .p.video_codec  = AV_CODEC_ID_RAWVIDEO,
    .write_header   = write_header,
    .write_packet   = write_packet,
    .write_trailer  = write_trailer,
    .p.flags        = AVFMT_NOFILE,
    .p.priv_class   = &v4l2_class,
};
