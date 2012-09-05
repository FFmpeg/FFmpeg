/*
 * Linux video grab interface
 * Copyright (c) 2000,2001 Fabrice Bellard
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

#include "avdevice.h"

#undef __STRICT_ANSI__ //workaround due to broken kernel headers
#include "config.h"
#include "libavutil/rational.h"
#include "libavutil/imgutils.h"
#include "libavutil/log.h"
#include "libavutil/opt.h"
#include "libavformat/internal.h"
#include "libavcodec/dsputil.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>
#define _LINUX_TIME_H 1
#include <linux/videodev.h>
#include <time.h>
#include "avdevice.h"

typedef struct {
    AVClass *class;
    int fd;
    int frame_format; /* see VIDEO_PALETTE_xxx */
    int use_mmap;
    AVRational time_base;
    int64_t time_frame;
    int frame_size;
    struct video_capability video_cap;
    struct video_audio audio_saved;
    struct video_window video_win;
    uint8_t *video_buf;
    struct video_mbuf gb_buffers;
    struct video_mmap gb_buf;
    int gb_frame;
    int standard;
} VideoData;

static const struct {
    int palette;
    int depth;
    enum PixelFormat pix_fmt;
} video_formats [] = {
    {.palette = VIDEO_PALETTE_YUV420P, .depth = 12, .pix_fmt = PIX_FMT_YUV420P },
    {.palette = VIDEO_PALETTE_YUV422,  .depth = 16, .pix_fmt = PIX_FMT_YUYV422 },
    {.palette = VIDEO_PALETTE_UYVY,    .depth = 16, .pix_fmt = PIX_FMT_UYVY422 },
    {.palette = VIDEO_PALETTE_YUYV,    .depth = 16, .pix_fmt = PIX_FMT_YUYV422 },
    /* NOTE: v4l uses BGR24, not RGB24 */
    {.palette = VIDEO_PALETTE_RGB24,   .depth = 24, .pix_fmt = PIX_FMT_BGR24   },
    {.palette = VIDEO_PALETTE_RGB565,  .depth = 16, .pix_fmt = PIX_FMT_BGR565  },
    {.palette = VIDEO_PALETTE_GREY,    .depth = 8,  .pix_fmt = PIX_FMT_GRAY8   },
};


static int grab_read_header(AVFormatContext *s1, AVFormatParameters *ap)
{
    VideoData *s = s1->priv_data;
    AVStream *st;
    int video_fd;
    int desired_palette, desired_depth;
    struct video_tuner tuner;
    struct video_audio audio;
    struct video_picture pict;
    int j;
    int vformat_num = FF_ARRAY_ELEMS(video_formats);

    av_log(s1, AV_LOG_WARNING, "V4L input device is deprecated and will be removed in the next release.");

    if (ap->time_base.den <= 0) {
        av_log(s1, AV_LOG_ERROR, "Wrong time base (%d)\n", ap->time_base.den);
        return -1;
    }
    s->time_base = ap->time_base;

    s->video_win.width = ap->width;
    s->video_win.height = ap->height;

    st = avformat_new_stream(s1, NULL);
    if (!st)
        return AVERROR(ENOMEM);
    avpriv_set_pts_info(st, 64, 1, 1000000); /* 64 bits pts in us */

    video_fd = open(s1->filename, O_RDWR);
    if (video_fd < 0) {
        av_log(s1, AV_LOG_ERROR, "%s: %s\n", s1->filename, strerror(errno));
        goto fail;
    }

    if (ioctl(video_fd, VIDIOCGCAP, &s->video_cap) < 0) {
        av_log(s1, AV_LOG_ERROR, "VIDIOCGCAP: %s\n", strerror(errno));
        goto fail;
    }

    if (!(s->video_cap.type & VID_TYPE_CAPTURE)) {
        av_log(s1, AV_LOG_ERROR, "Fatal: grab device does not handle capture\n");
        goto fail;
    }

    /* no values set, autodetect them */
    if (s->video_win.width <= 0 || s->video_win.height <= 0) {
        if (ioctl(video_fd, VIDIOCGWIN, &s->video_win, sizeof(s->video_win)) < 0) {
            av_log(s1, AV_LOG_ERROR, "VIDIOCGWIN: %s\n", strerror(errno));
            goto fail;
        }
    }

    if(av_image_check_size(s->video_win.width, s->video_win.height, 0, s1) < 0)
        return -1;

    desired_palette = -1;
    desired_depth = -1;
    for (j = 0; j < vformat_num; j++) {
        if (ap->pix_fmt == video_formats[j].pix_fmt) {
            desired_palette = video_formats[j].palette;
            desired_depth = video_formats[j].depth;
            break;
        }
    }

    /* set tv standard */
    if (!ioctl(video_fd, VIDIOCGTUNER, &tuner)) {
        tuner.mode = s->standard;
        ioctl(video_fd, VIDIOCSTUNER, &tuner);
    }

    /* unmute audio */
    audio.audio = 0;
    ioctl(video_fd, VIDIOCGAUDIO, &audio);
    memcpy(&s->audio_saved, &audio, sizeof(audio));
    audio.flags &= ~VIDEO_AUDIO_MUTE;
    ioctl(video_fd, VIDIOCSAUDIO, &audio);

    ioctl(video_fd, VIDIOCGPICT, &pict);
    av_dlog(s1, "v4l: colour=%d hue=%d brightness=%d constrast=%d whiteness=%d\n",
            pict.colour, pict.hue, pict.brightness, pict.contrast, pict.whiteness);
    /* try to choose a suitable video format */
    pict.palette = desired_palette;
    pict.depth= desired_depth;
    if (desired_palette == -1 || ioctl(video_fd, VIDIOCSPICT, &pict) < 0) {
        for (j = 0; j < vformat_num; j++) {
            pict.palette = video_formats[j].palette;
            pict.depth = video_formats[j].depth;
            if (-1 != ioctl(video_fd, VIDIOCSPICT, &pict))
                break;
        }
        if (j >= vformat_num)
            goto fail1;
    }

    if (ioctl(video_fd, VIDIOCGMBUF, &s->gb_buffers) < 0) {
        /* try to use read based access */
        int val;

        s->video_win.x = 0;
        s->video_win.y = 0;
        s->video_win.chromakey = -1;
        s->video_win.flags = 0;

        if (ioctl(video_fd, VIDIOCSWIN, s->video_win) < 0) {
            av_log(s1, AV_LOG_ERROR, "VIDIOCSWIN: %s\n", strerror(errno));
            goto fail;
        }

        s->frame_format = pict.palette;

        val = 1;
        if (ioctl(video_fd, VIDIOCCAPTURE, &val) < 0) {
            av_log(s1, AV_LOG_ERROR, "VIDIOCCAPTURE: %s\n", strerror(errno));
            goto fail;
        }

        s->time_frame = av_gettime() * s->time_base.den / s->time_base.num;
        s->use_mmap = 0;
    } else {
        s->video_buf = mmap(0, s->gb_buffers.size, PROT_READ|PROT_WRITE, MAP_SHARED, video_fd, 0);
        if ((unsigned char*)-1 == s->video_buf) {
            s->video_buf = mmap(0, s->gb_buffers.size, PROT_READ|PROT_WRITE, MAP_PRIVATE, video_fd, 0);
            if ((unsigned char*)-1 == s->video_buf) {
                av_log(s1, AV_LOG_ERROR, "mmap: %s\n", strerror(errno));
                goto fail;
            }
        }
        s->gb_frame = 0;
        s->time_frame = av_gettime() * s->time_base.den / s->time_base.num;

        /* start to grab the first frame */
        s->gb_buf.frame = s->gb_frame % s->gb_buffers.frames;
        s->gb_buf.height = s->video_win.height;
        s->gb_buf.width = s->video_win.width;
        s->gb_buf.format = pict.palette;

        if (ioctl(video_fd, VIDIOCMCAPTURE, &s->gb_buf) < 0) {
            if (errno != EAGAIN) {
            fail1:
                av_log(s1, AV_LOG_ERROR, "VIDIOCMCAPTURE: %s\n", strerror(errno));
            } else {
                av_log(s1, AV_LOG_ERROR, "Fatal: grab device does not receive any video signal\n");
            }
            goto fail;
        }
        for (j = 1; j < s->gb_buffers.frames; j++) {
          s->gb_buf.frame = j;
          ioctl(video_fd, VIDIOCMCAPTURE, &s->gb_buf);
        }
        s->frame_format = s->gb_buf.format;
        s->use_mmap = 1;
    }

    for (j = 0; j < vformat_num; j++) {
        if (s->frame_format == video_formats[j].palette) {
            s->frame_size = s->video_win.width * s->video_win.height * video_formats[j].depth / 8;
            st->codec->pix_fmt = video_formats[j].pix_fmt;
            break;
        }
    }

    if (j >= vformat_num)
        goto fail;

    s->fd = video_fd;

    st->codec->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codec->codec_id = AV_CODEC_ID_RAWVIDEO;
    st->codec->width = s->video_win.width;
    st->codec->height = s->video_win.height;
    st->codec->time_base = s->time_base;
    st->codec->bit_rate = s->frame_size * 1/av_q2d(st->codec->time_base) * 8;

    return 0;
 fail:
    if (video_fd >= 0)
        close(video_fd);
    return AVERROR(EIO);
}

static int v4l_mm_read_picture(VideoData *s, uint8_t *buf)
{
    uint8_t *ptr;

    while (ioctl(s->fd, VIDIOCSYNC, &s->gb_frame) < 0 &&
           (errno == EAGAIN || errno == EINTR));

    ptr = s->video_buf + s->gb_buffers.offsets[s->gb_frame];
    memcpy(buf, ptr, s->frame_size);

    /* Setup to capture the next frame */
    s->gb_buf.frame = s->gb_frame;
    if (ioctl(s->fd, VIDIOCMCAPTURE, &s->gb_buf) < 0) {
        if (errno == EAGAIN)
            av_log(NULL, AV_LOG_ERROR, "Cannot Sync\n");
        else
            av_log(NULL, AV_LOG_ERROR, "VIDIOCMCAPTURE: %s\n", strerror(errno));
        return AVERROR(EIO);
    }

    /* This is now the grabbing frame */
    s->gb_frame = (s->gb_frame + 1) % s->gb_buffers.frames;

    return s->frame_size;
}

static int grab_read_packet(AVFormatContext *s1, AVPacket *pkt)
{
    VideoData *s = s1->priv_data;
    int64_t curtime, delay;
    struct timespec ts;

    /* Calculate the time of the next frame */
    s->time_frame += INT64_C(1000000);

    /* wait based on the frame rate */
    for(;;) {
        curtime = av_gettime();
        delay = s->time_frame * s->time_base.num / s->time_base.den - curtime;
        if (delay <= 0) {
            if (delay < INT64_C(-1000000) * s->time_base.num / s->time_base.den) {
                /* printf("grabbing is %d frames late (dropping)\n", (int) -(delay / 16666)); */
                s->time_frame += INT64_C(1000000);
            }
            break;
        }
        ts.tv_sec = delay / 1000000;
        ts.tv_nsec = (delay % 1000000) * 1000;
        nanosleep(&ts, NULL);
    }

    if (av_new_packet(pkt, s->frame_size) < 0)
        return AVERROR(EIO);

    pkt->pts = curtime;

    /* read one frame */
    if (s->use_mmap) {
        return v4l_mm_read_picture(s, pkt->data);
    } else {
        if (read(s->fd, pkt->data, pkt->size) != pkt->size)
            return AVERROR(EIO);
        return s->frame_size;
    }
}

static int grab_read_close(AVFormatContext *s1)
{
    VideoData *s = s1->priv_data;

    if (s->use_mmap)
        munmap(s->video_buf, s->gb_buffers.size);

    /* mute audio. we must force it because the BTTV driver does not
       return its state correctly */
    s->audio_saved.flags |= VIDEO_AUDIO_MUTE;
    ioctl(s->fd, VIDIOCSAUDIO, &s->audio_saved);

    close(s->fd);
    return 0;
}

static const AVOption options[] = {
    { "standard", "", offsetof(VideoData, standard), AV_OPT_TYPE_INT, {.i64 = VIDEO_MODE_NTSC}, VIDEO_MODE_PAL, VIDEO_MODE_NTSC, AV_OPT_FLAG_DECODING_PARAM, "standard" },
    { "PAL",   "", 0, AV_OPT_TYPE_CONST, {.i64 = VIDEO_MODE_PAL},   0, 0, AV_OPT_FLAG_DECODING_PARAM, "standard" },
    { "SECAM", "", 0, AV_OPT_TYPE_CONST, {.i64 = VIDEO_MODE_SECAM}, 0, 0, AV_OPT_FLAG_DECODING_PARAM, "standard" },
    { "NTSC",  "", 0, AV_OPT_TYPE_CONST, {.i64 = VIDEO_MODE_NTSC},  0, 0, AV_OPT_FLAG_DECODING_PARAM, "standard" },
    { NULL },
};

static const AVClass v4l_class = {
    .class_name = "V4L indev",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVInputFormat ff_v4l_demuxer = {
    .name           = "video4linux,v4l",
    .long_name      = NULL_IF_CONFIG_SMALL("Video4Linux device grab"),
    .priv_data_size = sizeof(VideoData),
    .read_header    = grab_read_header,
    .read_packet    = grab_read_packet,
    .read_close     = grab_read_close,
    .flags          = AVFMT_NOFILE,
    .priv_class     = &v4l_class,
};
