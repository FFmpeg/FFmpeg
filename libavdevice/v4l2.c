/*
 * Copyright (c) 2000,2001 Fabrice Bellard
 * Copyright (c) 2006 Luca Abeni
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
 * Video4Linux2 grab interface
 *
 * Part of this file is based on the V4L2 video capture example
 * (http://linuxtv.org/downloads/v4l-dvb-apis/capture-example.html)
 *
 * Thanks to Michael Niedermayer for providing the mapping between
 * V4L2_PIX_FMT_* and AV_PIX_FMT_*
 */

#include <stdatomic.h>

#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/imgutils.h"
#include "libavutil/parseutils.h"
#include "libavutil/pixdesc.h"
#include "libavutil/time.h"
#include "libavcodec/avcodec.h"
#include "libavcodec/codec_desc.h"
#include "libavformat/demux.h"
#include "libavformat/internal.h"
#include "avdevice.h"
#include "timefilter.h"
#include "v4l2-common.h"
#include <dirent.h>

#if CONFIG_LIBV4L2
#include <libv4l2.h>
#endif

static const int desired_video_buffers = 256;

#define V4L_ALLFORMATS  3
#define V4L_RAWFORMATS  1
#define V4L_COMPFORMATS 2

/**
 * Return timestamps to the user exactly as returned by the kernel
 */
#define V4L_TS_DEFAULT  0
/**
 * Autodetect the kind of timestamps returned by the kernel and convert to
 * absolute (wall clock) timestamps.
 */
#define V4L_TS_ABS      1
/**
 * Assume kernel timestamps are from the monotonic clock and convert to
 * absolute timestamps.
 */
#define V4L_TS_MONO2ABS 2

/**
 * Once the kind of timestamps returned by the kernel have been detected,
 * the value of the timefilter (NULL or not) determines whether a conversion
 * takes place.
 */
#define V4L_TS_CONVERT_READY V4L_TS_DEFAULT

struct video_data {
    AVClass *class;
    int fd;
    int pixelformat; /* V4L2_PIX_FMT_* */
    int width, height;
    int frame_size;
    int interlaced;
    int top_field_first;
    int ts_mode;
    TimeFilter *timefilter;
    int64_t last_time_m;

    int buffers;
    atomic_int buffers_queued;
    void **buf_start;
    unsigned int *buf_len;
    char *standard;
    v4l2_std_id std_id;
    int channel;
    char *pixel_format; /**< Set by a private option. */
    int list_format;    /**< Set by a private option. */
    int list_standard;  /**< Set by a private option. */
    char *framerate;    /**< Set by a private option. */

    int use_libv4l2;
    int (*open_f)(const char *file, int oflag, ...);
    int (*close_f)(int fd);
    int (*dup_f)(int fd);
#ifdef __GLIBC__
    int (*ioctl_f)(int fd, unsigned long int request, ...);
#else
    int (*ioctl_f)(int fd, int request, ...);
#endif
    ssize_t (*read_f)(int fd, void *buffer, size_t n);
    void *(*mmap_f)(void *start, size_t length, int prot, int flags, int fd, int64_t offset);
    int (*munmap_f)(void *_start, size_t length);
};

struct buff_data {
    struct video_data *s;
    int index;
};

static int device_open(AVFormatContext *ctx, const char* device_path)
{
    struct video_data *s = ctx->priv_data;
    struct v4l2_capability cap;
    int fd;
    int err;
    int flags = O_RDWR;

#define SET_WRAPPERS(prefix) do {       \
    s->open_f   = prefix ## open;       \
    s->close_f  = prefix ## close;      \
    s->dup_f    = prefix ## dup;        \
    s->ioctl_f  = prefix ## ioctl;      \
    s->read_f   = prefix ## read;       \
    s->mmap_f   = prefix ## mmap;       \
    s->munmap_f = prefix ## munmap;     \
} while (0)

    if (s->use_libv4l2) {
#if CONFIG_LIBV4L2
        SET_WRAPPERS(v4l2_);
#else
        av_log(ctx, AV_LOG_ERROR, "libavdevice is not built with libv4l2 support.\n");
        return AVERROR(EINVAL);
#endif
    } else {
        SET_WRAPPERS();
    }

#define v4l2_open   s->open_f
#define v4l2_close  s->close_f
#define v4l2_dup    s->dup_f
#define v4l2_ioctl  s->ioctl_f
#define v4l2_read   s->read_f
#define v4l2_mmap   s->mmap_f
#define v4l2_munmap s->munmap_f

    if (ctx->flags & AVFMT_FLAG_NONBLOCK) {
        flags |= O_NONBLOCK;
    }

    fd = v4l2_open(device_path, flags, 0);
    if (fd < 0) {
        err = AVERROR(errno);
        av_log(ctx, AV_LOG_ERROR, "Cannot open video device %s: %s\n",
               device_path, av_err2str(err));
        return err;
    }

    if (v4l2_ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
        err = AVERROR(errno);
        av_log(ctx, AV_LOG_ERROR, "ioctl(VIDIOC_QUERYCAP): %s\n",
               av_err2str(err));
        goto fail;
    }

    av_log(ctx, AV_LOG_VERBOSE, "fd:%d capabilities:%x\n",
           fd, cap.capabilities);

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        av_log(ctx, AV_LOG_ERROR, "Not a video capture device.\n");
        err = AVERROR(ENODEV);
        goto fail;
    }

    if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
        av_log(ctx, AV_LOG_ERROR,
               "The device does not support the streaming I/O method.\n");
        err = AVERROR(ENOSYS);
        goto fail;
    }

    return fd;

fail:
    v4l2_close(fd);
    return err;
}

static int device_init(AVFormatContext *ctx, int *width, int *height,
                       uint32_t pixelformat)
{
    struct video_data *s = ctx->priv_data;
    struct v4l2_format fmt = { .type = V4L2_BUF_TYPE_VIDEO_CAPTURE };
    int res = 0;

    fmt.fmt.pix.width = *width;
    fmt.fmt.pix.height = *height;
    fmt.fmt.pix.pixelformat = pixelformat;
    fmt.fmt.pix.field = V4L2_FIELD_ANY;

    /* Some drivers will fail and return EINVAL when the pixelformat
       is not supported (even if type field is valid and supported) */
    if (v4l2_ioctl(s->fd, VIDIOC_S_FMT, &fmt) < 0)
        res = AVERROR(errno);

    if ((*width != fmt.fmt.pix.width) || (*height != fmt.fmt.pix.height)) {
        av_log(ctx, AV_LOG_INFO,
               "The V4L2 driver changed the video from %dx%d to %dx%d\n",
               *width, *height, fmt.fmt.pix.width, fmt.fmt.pix.height);
        *width = fmt.fmt.pix.width;
        *height = fmt.fmt.pix.height;
    }

    if (pixelformat != fmt.fmt.pix.pixelformat) {
        av_log(ctx, AV_LOG_DEBUG,
               "The V4L2 driver changed the pixel format "
               "from 0x%08X to 0x%08X\n",
               pixelformat, fmt.fmt.pix.pixelformat);
        res = AVERROR(EINVAL);
    }

    if (fmt.fmt.pix.field == V4L2_FIELD_INTERLACED) {
        av_log(ctx, AV_LOG_DEBUG,
               "The V4L2 driver is using the interlaced mode\n");
        s->interlaced = 1;
    }

    return res;
}

static int first_field(const struct video_data *s)
{
    int res;
    v4l2_std_id std;

    res = v4l2_ioctl(s->fd, VIDIOC_G_STD, &std);
    if (res < 0)
        return 0;
    if (std & V4L2_STD_NTSC)
        return 0;

    return 1;
}

#if HAVE_STRUCT_V4L2_FRMIVALENUM_DISCRETE
static void list_framesizes(AVFormatContext *ctx, uint32_t pixelformat)
{
    const struct video_data *s = ctx->priv_data;
    struct v4l2_frmsizeenum vfse = { .pixel_format = pixelformat };

    while(!v4l2_ioctl(s->fd, VIDIOC_ENUM_FRAMESIZES, &vfse)) {
        switch (vfse.type) {
        case V4L2_FRMSIZE_TYPE_DISCRETE:
            av_log(ctx, AV_LOG_INFO, " %ux%u",
                   vfse.discrete.width, vfse.discrete.height);
        break;
        case V4L2_FRMSIZE_TYPE_CONTINUOUS:
        case V4L2_FRMSIZE_TYPE_STEPWISE:
            av_log(ctx, AV_LOG_INFO, " {%u-%u, %u}x{%u-%u, %u}",
                   vfse.stepwise.min_width,
                   vfse.stepwise.max_width,
                   vfse.stepwise.step_width,
                   vfse.stepwise.min_height,
                   vfse.stepwise.max_height,
                   vfse.stepwise.step_height);
        }
        vfse.index++;
    }
}
#endif

static void list_formats(AVFormatContext *ctx, int type)
{
    const struct video_data *s = ctx->priv_data;
    struct v4l2_fmtdesc vfd = { .type = V4L2_BUF_TYPE_VIDEO_CAPTURE };

    while(!v4l2_ioctl(s->fd, VIDIOC_ENUM_FMT, &vfd)) {
        enum AVCodecID codec_id = ff_fmt_v4l2codec(vfd.pixelformat);
        enum AVPixelFormat pix_fmt = ff_fmt_v4l2ff(vfd.pixelformat, codec_id);

        vfd.index++;

        if (!(vfd.flags & V4L2_FMT_FLAG_COMPRESSED) &&
            type & V4L_RAWFORMATS) {
            const char *fmt_name = av_get_pix_fmt_name(pix_fmt);
            av_log(ctx, AV_LOG_INFO, "Raw       : %11s : %20s :",
                   fmt_name ? fmt_name : "Unsupported",
                   vfd.description);
        } else if (vfd.flags & V4L2_FMT_FLAG_COMPRESSED &&
                   type & V4L_COMPFORMATS) {
            const AVCodecDescriptor *desc = avcodec_descriptor_get(codec_id);
            av_log(ctx, AV_LOG_INFO, "Compressed: %11s : %20s :",
                   desc ? desc->name : "Unsupported",
                   vfd.description);
        } else {
            continue;
        }

#ifdef V4L2_FMT_FLAG_EMULATED
        if (vfd.flags & V4L2_FMT_FLAG_EMULATED)
            av_log(ctx, AV_LOG_INFO, " Emulated :");
#endif
#if HAVE_STRUCT_V4L2_FRMIVALENUM_DISCRETE
        list_framesizes(ctx, vfd.pixelformat);
#endif
        av_log(ctx, AV_LOG_INFO, "\n");
    }
}

static void list_standards(AVFormatContext *ctx)
{
    int ret;
    struct video_data *s = ctx->priv_data;
    struct v4l2_standard standard;

    if (s->std_id == 0)
        return;

    for (standard.index = 0; ; standard.index++) {
        if (v4l2_ioctl(s->fd, VIDIOC_ENUMSTD, &standard) < 0) {
            ret = AVERROR(errno);
            if (ret == AVERROR(EINVAL)) {
                break;
            } else {
                av_log(ctx, AV_LOG_ERROR, "ioctl(VIDIOC_ENUMSTD): %s\n", av_err2str(ret));
                return;
            }
        }
        av_log(ctx, AV_LOG_INFO, "%2d, %16"PRIx64", %s\n",
               standard.index, (uint64_t)standard.id, standard.name);
    }
}

static int mmap_init(AVFormatContext *ctx)
{
    int i, res;
    struct video_data *s = ctx->priv_data;
    struct v4l2_requestbuffers req = {
        .type   = V4L2_BUF_TYPE_VIDEO_CAPTURE,
        .count  = desired_video_buffers,
        .memory = V4L2_MEMORY_MMAP
    };

    if (v4l2_ioctl(s->fd, VIDIOC_REQBUFS, &req) < 0) {
        res = AVERROR(errno);
        av_log(ctx, AV_LOG_ERROR, "ioctl(VIDIOC_REQBUFS): %s\n", av_err2str(res));
        return res;
    }

    if (req.count < 2) {
        av_log(ctx, AV_LOG_ERROR, "Insufficient buffer memory\n");
        return AVERROR(ENOMEM);
    }
    s->buffers = req.count;
    s->buf_start = av_malloc_array(s->buffers, sizeof(void *));
    if (!s->buf_start) {
        av_log(ctx, AV_LOG_ERROR, "Cannot allocate buffer pointers\n");
        return AVERROR(ENOMEM);
    }
    s->buf_len = av_malloc_array(s->buffers, sizeof(unsigned int));
    if (!s->buf_len) {
        av_log(ctx, AV_LOG_ERROR, "Cannot allocate buffer sizes\n");
        av_freep(&s->buf_start);
        return AVERROR(ENOMEM);
    }

    for (i = 0; i < req.count; i++) {
        struct v4l2_buffer buf = {
            .type   = V4L2_BUF_TYPE_VIDEO_CAPTURE,
            .index  = i,
            .memory = V4L2_MEMORY_MMAP
        };
        if (v4l2_ioctl(s->fd, VIDIOC_QUERYBUF, &buf) < 0) {
            res = AVERROR(errno);
            av_log(ctx, AV_LOG_ERROR, "ioctl(VIDIOC_QUERYBUF): %s\n", av_err2str(res));
            return res;
        }

        s->buf_len[i] = buf.length;
        if (s->frame_size > 0 && s->buf_len[i] < s->frame_size) {
            av_log(ctx, AV_LOG_ERROR,
                   "buf_len[%d] = %d < expected frame size %d\n",
                   i, s->buf_len[i], s->frame_size);
            return AVERROR(ENOMEM);
        }
        s->buf_start[i] = v4l2_mmap(NULL, buf.length,
                               PROT_READ | PROT_WRITE, MAP_SHARED,
                               s->fd, buf.m.offset);

        if (s->buf_start[i] == MAP_FAILED) {
            res = AVERROR(errno);
            av_log(ctx, AV_LOG_ERROR, "mmap: %s\n", av_err2str(res));
            return res;
        }
    }

    return 0;
}

static int enqueue_buffer(struct video_data *s, struct v4l2_buffer *buf)
{
    int res = 0;

    if (v4l2_ioctl(s->fd, VIDIOC_QBUF, buf) < 0) {
        res = AVERROR(errno);
        av_log(NULL, AV_LOG_ERROR, "ioctl(VIDIOC_QBUF): %s\n", av_err2str(res));
    } else {
        atomic_fetch_add(&s->buffers_queued, 1);
    }

    return res;
}

static void mmap_release_buffer(void *opaque, uint8_t *data)
{
    struct v4l2_buffer buf = { 0 };
    struct buff_data *buf_descriptor = opaque;
    struct video_data *s = buf_descriptor->s;

    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = buf_descriptor->index;
    av_free(buf_descriptor);

    enqueue_buffer(s, &buf);
}

#if HAVE_CLOCK_GETTIME && defined(CLOCK_MONOTONIC)
static int64_t av_gettime_monotonic(void)
{
    return av_gettime_relative();
}
#endif

static int init_convert_timestamp(AVFormatContext *ctx, int64_t ts)
{
    struct video_data *s = ctx->priv_data;
    int64_t now;

    now = av_gettime();
    if (s->ts_mode == V4L_TS_ABS &&
        ts <= now + 1 * AV_TIME_BASE && ts >= now - 10 * AV_TIME_BASE) {
        av_log(ctx, AV_LOG_INFO, "Detected absolute timestamps\n");
        s->ts_mode = V4L_TS_CONVERT_READY;
        return 0;
    }
#if HAVE_CLOCK_GETTIME && defined(CLOCK_MONOTONIC)
    if (ctx->streams[0]->avg_frame_rate.num) {
        now = av_gettime_monotonic();
        if (s->ts_mode == V4L_TS_MONO2ABS ||
            (ts <= now + 1 * AV_TIME_BASE && ts >= now - 10 * AV_TIME_BASE)) {
            AVRational tb = {AV_TIME_BASE, 1};
            int64_t period = av_rescale_q(1, tb, ctx->streams[0]->avg_frame_rate);
            av_log(ctx, AV_LOG_INFO, "Detected monotonic timestamps, converting\n");
            /* microseconds instead of seconds, MHz instead of Hz */
            s->timefilter = ff_timefilter_new(1, period, 1.0E-6);
            if (!s->timefilter)
                return AVERROR(ENOMEM);
            s->ts_mode = V4L_TS_CONVERT_READY;
            return 0;
        }
    }
#endif
    av_log(ctx, AV_LOG_ERROR, "Unknown timestamps\n");
    return AVERROR(EIO);
}

static int convert_timestamp(AVFormatContext *ctx, int64_t *ts)
{
    struct video_data *s = ctx->priv_data;

    if (s->ts_mode) {
        int r = init_convert_timestamp(ctx, *ts);
        if (r < 0)
            return r;
    }
#if HAVE_CLOCK_GETTIME && defined(CLOCK_MONOTONIC)
    if (s->timefilter) {
        int64_t nowa = av_gettime();
        int64_t nowm = av_gettime_monotonic();
        ff_timefilter_update(s->timefilter, nowa, nowm - s->last_time_m);
        s->last_time_m = nowm;
        *ts = ff_timefilter_eval(s->timefilter, *ts - nowm);
    }
#endif
    return 0;
}

static int mmap_read_frame(AVFormatContext *ctx, AVPacket *pkt)
{
    struct video_data *s = ctx->priv_data;
    struct v4l2_buffer buf = {
        .type   = V4L2_BUF_TYPE_VIDEO_CAPTURE,
        .memory = V4L2_MEMORY_MMAP
    };
    struct timeval buf_ts;
    int res;

    pkt->size = 0;

    /* FIXME: Some special treatment might be needed in case of loss of signal... */
    while ((res = v4l2_ioctl(s->fd, VIDIOC_DQBUF, &buf)) < 0 && (errno == EINTR));
    if (res < 0) {
        if (errno == EAGAIN)
            return AVERROR(EAGAIN);

        res = AVERROR(errno);
        av_log(ctx, AV_LOG_ERROR, "ioctl(VIDIOC_DQBUF): %s\n",
               av_err2str(res));
        return res;
    }

    buf_ts = buf.timestamp;

    if (buf.index >= s->buffers) {
        av_log(ctx, AV_LOG_ERROR, "Invalid buffer index received.\n");
        return AVERROR(EINVAL);
    }
    atomic_fetch_add(&s->buffers_queued, -1);
    // always keep at least one buffer queued
    av_assert0(atomic_load(&s->buffers_queued) >= 1);

#ifdef V4L2_BUF_FLAG_ERROR
    if (buf.flags & V4L2_BUF_FLAG_ERROR) {
        av_log(ctx, AV_LOG_WARNING,
               "Dequeued v4l2 buffer contains corrupted data (%d bytes).\n",
               buf.bytesused);
        buf.bytesused = 0;
    } else
#endif
    {
        /* CPIA is a compressed format and we don't know the exact number of bytes
         * used by a frame, so set it here as the driver announces it. */
        if (ctx->video_codec_id == AV_CODEC_ID_CPIA)
            s->frame_size = buf.bytesused;

        if (s->frame_size > 0 && buf.bytesused != s->frame_size) {
            av_log(ctx, AV_LOG_WARNING,
                   "Dequeued v4l2 buffer contains %d bytes, but %d were expected. Flags: 0x%08X.\n",
                   buf.bytesused, s->frame_size, buf.flags);
            buf.bytesused = 0;
        }
    }

    /* Image is at s->buff_start[buf.index] */
    if (atomic_load(&s->buffers_queued) == FFMAX(s->buffers / 8, 1)) {
        /* when we start getting low on queued buffers, fall back on copying data */
        res = av_new_packet(pkt, buf.bytesused);
        if (res < 0) {
            av_log(ctx, AV_LOG_ERROR, "Error allocating a packet.\n");
            enqueue_buffer(s, &buf);
            return res;
        }
        memcpy(pkt->data, s->buf_start[buf.index], buf.bytesused);

        res = enqueue_buffer(s, &buf);
        if (res) {
            av_packet_unref(pkt);
            return res;
        }
    } else {
        struct buff_data *buf_descriptor;

        pkt->data     = s->buf_start[buf.index];
        pkt->size     = buf.bytesused;

        buf_descriptor = av_malloc(sizeof(struct buff_data));
        if (!buf_descriptor) {
            /* Something went wrong... Since av_malloc() failed, we cannot even
             * allocate a buffer for memcpying into it
             */
            av_log(ctx, AV_LOG_ERROR, "Failed to allocate a buffer descriptor\n");
            enqueue_buffer(s, &buf);

            return AVERROR(ENOMEM);
        }
        buf_descriptor->index = buf.index;
        buf_descriptor->s     = s;

        pkt->buf = av_buffer_create(pkt->data, pkt->size, mmap_release_buffer,
                                    buf_descriptor, 0);
        if (!pkt->buf) {
            av_log(ctx, AV_LOG_ERROR, "Failed to create a buffer\n");
            enqueue_buffer(s, &buf);
            av_freep(&buf_descriptor);
            return AVERROR(ENOMEM);
        }
    }
    pkt->pts = buf_ts.tv_sec * INT64_C(1000000) + buf_ts.tv_usec;
    convert_timestamp(ctx, &pkt->pts);

    return pkt->size;
}

static int mmap_start(AVFormatContext *ctx)
{
    struct video_data *s = ctx->priv_data;
    enum v4l2_buf_type type;
    int i, res;

    for (i = 0; i < s->buffers; i++) {
        struct v4l2_buffer buf = {
            .type   = V4L2_BUF_TYPE_VIDEO_CAPTURE,
            .index  = i,
            .memory = V4L2_MEMORY_MMAP
        };

        if (v4l2_ioctl(s->fd, VIDIOC_QBUF, &buf) < 0) {
            res = AVERROR(errno);
            av_log(ctx, AV_LOG_ERROR, "ioctl(VIDIOC_QBUF): %s\n",
                   av_err2str(res));
            return res;
        }
    }
    atomic_store(&s->buffers_queued, s->buffers);

    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (v4l2_ioctl(s->fd, VIDIOC_STREAMON, &type) < 0) {
        res = AVERROR(errno);
        av_log(ctx, AV_LOG_ERROR, "ioctl(VIDIOC_STREAMON): %s\n",
               av_err2str(res));
        return res;
    }

    return 0;
}

static void mmap_close(struct video_data *s)
{
    enum v4l2_buf_type type;
    int i;

    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    /* We do not check for the result, because we could
     * not do anything about it anyway...
     */
    v4l2_ioctl(s->fd, VIDIOC_STREAMOFF, &type);
    for (i = 0; i < s->buffers; i++) {
        v4l2_munmap(s->buf_start[i], s->buf_len[i]);
    }
    av_freep(&s->buf_start);
    av_freep(&s->buf_len);
}

static int v4l2_set_parameters(AVFormatContext *ctx)
{
    struct video_data *s = ctx->priv_data;
    struct v4l2_standard standard = { 0 };
    struct v4l2_streamparm streamparm = { 0 };
    struct v4l2_fract *tpf;
    AVRational framerate_q = { 0 };
    int i, ret;

    if (s->framerate &&
        (ret = av_parse_video_rate(&framerate_q, s->framerate)) < 0) {
        av_log(ctx, AV_LOG_ERROR, "Could not parse framerate '%s'.\n",
               s->framerate);
        return ret;
    }

    if (s->standard) {
        if (s->std_id) {
            ret = 0;
            av_log(ctx, AV_LOG_DEBUG, "Setting standard: %s\n", s->standard);
            /* set tv standard */
            for (i = 0; ; i++) {
                standard.index = i;
                if (v4l2_ioctl(s->fd, VIDIOC_ENUMSTD, &standard) < 0) {
                    ret = AVERROR(errno);
                    break;
                }
                if (!av_strcasecmp(standard.name, s->standard))
                    break;
            }
            if (ret < 0) {
                av_log(ctx, AV_LOG_ERROR, "Unknown or unsupported standard '%s'\n", s->standard);
                return ret;
            }

            if (v4l2_ioctl(s->fd, VIDIOC_S_STD, &standard.id) < 0) {
                ret = AVERROR(errno);
                av_log(ctx, AV_LOG_ERROR, "ioctl(VIDIOC_S_STD): %s\n", av_err2str(ret));
                return ret;
            }
        } else {
            av_log(ctx, AV_LOG_WARNING,
                   "This device does not support any standard\n");
        }
    }

    /* get standard */
    if (v4l2_ioctl(s->fd, VIDIOC_G_STD, &s->std_id) == 0) {
        tpf = &standard.frameperiod;
        for (i = 0; ; i++) {
            standard.index = i;
            if (v4l2_ioctl(s->fd, VIDIOC_ENUMSTD, &standard) < 0) {
                ret = AVERROR(errno);
                if (ret == AVERROR(EINVAL)
#ifdef ENODATA
                    || ret == AVERROR(ENODATA)
#endif
                ) {
                    tpf = &streamparm.parm.capture.timeperframe;
                    break;
                }
                av_log(ctx, AV_LOG_ERROR, "ioctl(VIDIOC_ENUMSTD): %s\n", av_err2str(ret));
                return ret;
            }
            if (standard.id == s->std_id) {
                av_log(ctx, AV_LOG_DEBUG,
                       "Current standard: %s, id: %"PRIx64", frameperiod: %d/%d\n",
                       standard.name, (uint64_t)standard.id, tpf->numerator, tpf->denominator);
                break;
            }
        }
    } else {
        tpf = &streamparm.parm.capture.timeperframe;
    }

    streamparm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (v4l2_ioctl(s->fd, VIDIOC_G_PARM, &streamparm) < 0) {
        ret = AVERROR(errno);
        av_log(ctx, AV_LOG_WARNING, "ioctl(VIDIOC_G_PARM): %s\n", av_err2str(ret));
    } else if (framerate_q.num && framerate_q.den) {
        if (streamparm.parm.capture.capability & V4L2_CAP_TIMEPERFRAME) {
            tpf = &streamparm.parm.capture.timeperframe;

            av_log(ctx, AV_LOG_DEBUG, "Setting time per frame to %d/%d\n",
                   framerate_q.den, framerate_q.num);
            tpf->numerator   = framerate_q.den;
            tpf->denominator = framerate_q.num;

            if (v4l2_ioctl(s->fd, VIDIOC_S_PARM, &streamparm) < 0) {
                ret = AVERROR(errno);
                av_log(ctx, AV_LOG_ERROR, "ioctl(VIDIOC_S_PARM): %s\n",
                       av_err2str(ret));
                return ret;
            }

            if (framerate_q.num != tpf->denominator ||
                framerate_q.den != tpf->numerator) {
                av_log(ctx, AV_LOG_INFO,
                       "The driver changed the time per frame from "
                       "%d/%d to %d/%d\n",
                       framerate_q.den, framerate_q.num,
                       tpf->numerator, tpf->denominator);
            }
        } else {
            av_log(ctx, AV_LOG_WARNING,
                   "The driver does not permit changing the time per frame\n");
        }
    }
    if (tpf->denominator > 0 && tpf->numerator > 0) {
        ctx->streams[0]->avg_frame_rate.num = tpf->denominator;
        ctx->streams[0]->avg_frame_rate.den = tpf->numerator;
        ctx->streams[0]->r_frame_rate = ctx->streams[0]->avg_frame_rate;
    } else
        av_log(ctx, AV_LOG_WARNING, "Time per frame unknown\n");

    return 0;
}

static int device_try_init(AVFormatContext *ctx,
                           enum AVPixelFormat pix_fmt,
                           int *width,
                           int *height,
                           uint32_t *desired_format,
                           enum AVCodecID *codec_id)
{
    int ret, i;

    *desired_format = ff_fmt_ff2v4l(pix_fmt, ctx->video_codec_id);

    if (*desired_format) {
        ret = device_init(ctx, width, height, *desired_format);
        if (ret < 0) {
            *desired_format = 0;
            if (ret != AVERROR(EINVAL))
                return ret;
        }
    }

    if (!*desired_format) {
        for (i = 0; ff_fmt_conversion_table[i].codec_id != AV_CODEC_ID_NONE; i++) {
            if (ctx->video_codec_id == AV_CODEC_ID_NONE ||
                ff_fmt_conversion_table[i].codec_id == ctx->video_codec_id) {
                av_log(ctx, AV_LOG_DEBUG, "Trying to set codec:%s pix_fmt:%s\n",
                       avcodec_get_name(ff_fmt_conversion_table[i].codec_id),
                       (char *)av_x_if_null(av_get_pix_fmt_name(ff_fmt_conversion_table[i].ff_fmt), "none"));

                *desired_format = ff_fmt_conversion_table[i].v4l2_fmt;
                ret = device_init(ctx, width, height, *desired_format);
                if (ret >= 0)
                    break;
                else if (ret != AVERROR(EINVAL))
                    return ret;
                *desired_format = 0;
            }
        }

        if (*desired_format == 0) {
            av_log(ctx, AV_LOG_ERROR, "Cannot find a proper format for "
                   "codec '%s' (id %d), pixel format '%s' (id %d)\n",
                   avcodec_get_name(ctx->video_codec_id), ctx->video_codec_id,
                   (char *)av_x_if_null(av_get_pix_fmt_name(pix_fmt), "none"), pix_fmt);
            ret = AVERROR(EINVAL);
        }
    }

    *codec_id = ff_fmt_v4l2codec(*desired_format);
    if (*codec_id == AV_CODEC_ID_NONE)
        av_assert0(ret == AVERROR(EINVAL));
    return ret;
}

static int v4l2_read_probe(const AVProbeData *p)
{
    if (av_strstart(p->filename, "/dev/video", NULL))
        return AVPROBE_SCORE_MAX - 1;
    return 0;
}

static int v4l2_read_header(AVFormatContext *ctx)
{
    struct video_data *s = ctx->priv_data;
    AVStream *st;
    int res = 0;
    uint32_t desired_format;
    enum AVCodecID codec_id = AV_CODEC_ID_NONE;
    enum AVPixelFormat pix_fmt = AV_PIX_FMT_NONE;
    struct v4l2_input input = { 0 };

    st = avformat_new_stream(ctx, NULL);
    if (!st)
        return AVERROR(ENOMEM);

#if CONFIG_LIBV4L2
    /* silence libv4l2 logging. if fopen() fails v4l2_log_file will be NULL
       and errors will get sent to stderr */
    if (s->use_libv4l2)
        v4l2_log_file = fopen("/dev/null", "w");
#endif

    s->fd = device_open(ctx, ctx->url);
    if (s->fd < 0)
        return s->fd;

    if (s->channel != -1) {
        /* set video input */
        av_log(ctx, AV_LOG_DEBUG, "Selecting input_channel: %d\n", s->channel);
        if (v4l2_ioctl(s->fd, VIDIOC_S_INPUT, &s->channel) < 0) {
            res = AVERROR(errno);
            av_log(ctx, AV_LOG_ERROR, "ioctl(VIDIOC_S_INPUT): %s\n", av_err2str(res));
            goto fail;
        }
    } else {
        /* get current video input */
        if (v4l2_ioctl(s->fd, VIDIOC_G_INPUT, &s->channel) < 0) {
            res = AVERROR(errno);
            av_log(ctx, AV_LOG_ERROR, "ioctl(VIDIOC_G_INPUT): %s\n", av_err2str(res));
            goto fail;
        }
    }

    /* enum input */
    input.index = s->channel;
    if (v4l2_ioctl(s->fd, VIDIOC_ENUMINPUT, &input) < 0) {
        res = AVERROR(errno);
        av_log(ctx, AV_LOG_ERROR, "ioctl(VIDIOC_ENUMINPUT): %s\n", av_err2str(res));
        goto fail;
    }
    s->std_id = input.std;
    av_log(ctx, AV_LOG_DEBUG, "Current input_channel: %d, input_name: %s, input_std: %"PRIx64"\n",
           s->channel, input.name, (uint64_t)input.std);

    if (s->list_format) {
        list_formats(ctx, s->list_format);
        res = AVERROR_EXIT;
        goto fail;
    }

    if (s->list_standard) {
        list_standards(ctx);
        res = AVERROR_EXIT;
        goto fail;
    }

    avpriv_set_pts_info(st, 64, 1, 1000000); /* 64 bits pts in us */

    if (s->pixel_format) {
        const AVCodecDescriptor *desc = avcodec_descriptor_get_by_name(s->pixel_format);

        if (desc)
            ctx->video_codec_id = desc->id;

        pix_fmt = av_get_pix_fmt(s->pixel_format);

        if (pix_fmt == AV_PIX_FMT_NONE && !desc) {
            av_log(ctx, AV_LOG_ERROR, "No such input format: %s.\n",
                   s->pixel_format);

            res = AVERROR(EINVAL);
            goto fail;
        }
    }

    if (!s->width && !s->height) {
        struct v4l2_format fmt = { .type = V4L2_BUF_TYPE_VIDEO_CAPTURE };

        av_log(ctx, AV_LOG_VERBOSE,
               "Querying the device for the current frame size\n");
        if (v4l2_ioctl(s->fd, VIDIOC_G_FMT, &fmt) < 0) {
            res = AVERROR(errno);
            av_log(ctx, AV_LOG_ERROR, "ioctl(VIDIOC_G_FMT): %s\n",
                   av_err2str(res));
            goto fail;
        }

        s->width  = fmt.fmt.pix.width;
        s->height = fmt.fmt.pix.height;
        av_log(ctx, AV_LOG_VERBOSE,
               "Setting frame size to %dx%d\n", s->width, s->height);
    }

    res = device_try_init(ctx, pix_fmt, &s->width, &s->height, &desired_format, &codec_id);
    if (res < 0)
        goto fail;

    /* If no pixel_format was specified, the codec_id was not known up
     * until now. Set video_codec_id in the context, as codec_id will
     * not be available outside this function
     */
    if (codec_id != AV_CODEC_ID_NONE && ctx->video_codec_id == AV_CODEC_ID_NONE)
        ctx->video_codec_id = codec_id;

    if ((res = av_image_check_size(s->width, s->height, 0, ctx)) < 0)
        goto fail;

    s->pixelformat = desired_format;

    if ((res = v4l2_set_parameters(ctx)) < 0)
        goto fail;

    st->codecpar->format = ff_fmt_v4l2ff(desired_format, codec_id);
    if (st->codecpar->format != AV_PIX_FMT_NONE)
        s->frame_size = av_image_get_buffer_size(st->codecpar->format,
                                                 s->width, s->height, 1);

    if ((res = mmap_init(ctx)) ||
        (res = mmap_start(ctx)) < 0)
            goto fail;

    s->top_field_first = first_field(s);

    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_id = codec_id;
    if (codec_id == AV_CODEC_ID_RAWVIDEO)
        st->codecpar->codec_tag =
            avcodec_pix_fmt_to_codec_tag(st->codecpar->format);
    else if (codec_id == AV_CODEC_ID_H264) {
        avpriv_stream_set_need_parsing(st, AVSTREAM_PARSE_FULL_ONCE);
    }
    if (desired_format == V4L2_PIX_FMT_YVU420)
        st->codecpar->codec_tag = MKTAG('Y', 'V', '1', '2');
    else if (desired_format == V4L2_PIX_FMT_YVU410)
        st->codecpar->codec_tag = MKTAG('Y', 'V', 'U', '9');
    st->codecpar->width = s->width;
    st->codecpar->height = s->height;
    if (st->avg_frame_rate.den)
        st->codecpar->bit_rate = s->frame_size * av_q2d(st->avg_frame_rate) * 8;

    return 0;

fail:
    v4l2_close(s->fd);
    return res;
}

static int v4l2_read_packet(AVFormatContext *ctx, AVPacket *pkt)
{
    int res;

    if ((res = mmap_read_frame(ctx, pkt)) < 0) {
        return res;
    }

    return pkt->size;
}

static int v4l2_read_close(AVFormatContext *ctx)
{
    struct video_data *s = ctx->priv_data;

    if (atomic_load(&s->buffers_queued) != s->buffers)
        av_log(ctx, AV_LOG_WARNING, "Some buffers are still owned by the caller on "
               "close.\n");

    mmap_close(s);

    ff_timefilter_destroy(s->timefilter);
    v4l2_close(s->fd);
    return 0;
}

static int v4l2_is_v4l_dev(const char *name)
{
    return !strncmp(name, "video", 5) ||
           !strncmp(name, "radio", 5) ||
           !strncmp(name, "vbi", 3) ||
           !strncmp(name, "v4l-subdev", 10);
}

static int v4l2_get_device_list(AVFormatContext *ctx, AVDeviceInfoList *device_list)
{
    struct video_data *s = ctx->priv_data;
    DIR *dir;
    struct dirent *entry;
    int ret = 0;

    if (!device_list)
        return AVERROR(EINVAL);

    dir = opendir("/dev");
    if (!dir) {
        ret = AVERROR(errno);
        av_log(ctx, AV_LOG_ERROR, "Couldn't open the directory: %s\n", av_err2str(ret));
        return ret;
    }
    while ((entry = readdir(dir))) {
        AVDeviceInfo *device = NULL;
        struct v4l2_capability cap;
        int fd = -1, size;
        char device_name[256];

        if (!v4l2_is_v4l_dev(entry->d_name))
            continue;

        size = snprintf(device_name, sizeof(device_name), "/dev/%s", entry->d_name);
        if (size >= sizeof(device_name)) {
            av_log(ctx, AV_LOG_ERROR, "Device name too long.\n");
            ret = AVERROR(ENOSYS);
            break;
        }

        if ((fd = device_open(ctx, device_name)) < 0)
            continue;

        if (v4l2_ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
            ret = AVERROR(errno);
            av_log(ctx, AV_LOG_ERROR, "ioctl(VIDIOC_QUERYCAP): %s\n", av_err2str(ret));
            goto fail;
        }

        device = av_mallocz(sizeof(AVDeviceInfo));
        if (!device) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        device->device_name = av_strdup(device_name);
        device->device_description = av_strdup(cap.card);
        if (!device->device_name || !device->device_description) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        if ((ret = av_dynarray_add_nofree(&device_list->devices,
                                          &device_list->nb_devices, device)) < 0)
            goto fail;

        v4l2_close(fd);
        continue;

      fail:
        if (device) {
            av_freep(&device->device_name);
            av_freep(&device->device_description);
            av_freep(&device);
        }
        v4l2_close(fd);
        break;
    }
    closedir(dir);
    return ret;
}

#define OFFSET(x) offsetof(struct video_data, x)
#define DEC AV_OPT_FLAG_DECODING_PARAM

static const AVOption options[] = {
    { "standard",     "set TV standard, used only by analog frame grabber",       OFFSET(standard),     AV_OPT_TYPE_STRING, {.str = NULL }, 0, 0,       DEC },
    { "channel",      "set TV channel, used only by frame grabber",               OFFSET(channel),      AV_OPT_TYPE_INT,    {.i64 = -1 },  -1, INT_MAX, DEC },
    { "video_size",   "set frame size",                                           OFFSET(width),        AV_OPT_TYPE_IMAGE_SIZE, {.str = NULL},  0, 0,   DEC },
    { "pixel_format", "set preferred pixel format",                               OFFSET(pixel_format), AV_OPT_TYPE_STRING, {.str = NULL},  0, 0,       DEC },
    { "input_format", "set preferred pixel format (for raw video) or codec name", OFFSET(pixel_format), AV_OPT_TYPE_STRING, {.str = NULL},  0, 0,       DEC },
    { "framerate",    "set frame rate",                                           OFFSET(framerate),    AV_OPT_TYPE_STRING, {.str = NULL},  0, 0,       DEC },

    { "list_formats", "list available formats and exit",                          OFFSET(list_format),  AV_OPT_TYPE_INT,    {.i64 = 0 },  0, INT_MAX, DEC, .unit = "list_formats" },
    { "all",          "show all available formats",                               OFFSET(list_format),  AV_OPT_TYPE_CONST,  {.i64 = V4L_ALLFORMATS  },    0, INT_MAX, DEC, .unit = "list_formats" },
    { "raw",          "show only non-compressed formats",                         OFFSET(list_format),  AV_OPT_TYPE_CONST,  {.i64 = V4L_RAWFORMATS  },    0, INT_MAX, DEC, .unit = "list_formats" },
    { "compressed",   "show only compressed formats",                             OFFSET(list_format),  AV_OPT_TYPE_CONST,  {.i64 = V4L_COMPFORMATS },    0, INT_MAX, DEC, .unit = "list_formats" },

    { "list_standards", "list supported standards and exit",                      OFFSET(list_standard), AV_OPT_TYPE_INT,   {.i64 = 0 },  0, 1, DEC, .unit = "list_standards" },
    { "all",            "show all supported standards",                           OFFSET(list_standard), AV_OPT_TYPE_CONST, {.i64 = 1 },  0, 0, DEC, .unit = "list_standards" },

    { "timestamps",   "set type of timestamps for grabbed frames",                OFFSET(ts_mode),      AV_OPT_TYPE_INT,    {.i64 = 0 }, 0, 2, DEC, .unit = "timestamps" },
    { "ts",           "set type of timestamps for grabbed frames",                OFFSET(ts_mode),      AV_OPT_TYPE_INT,    {.i64 = 0 }, 0, 2, DEC, .unit = "timestamps" },
    { "default",      "use timestamps from the kernel",                           OFFSET(ts_mode),      AV_OPT_TYPE_CONST,  {.i64 = V4L_TS_DEFAULT  }, 0, 2, DEC, .unit = "timestamps" },
    { "abs",          "use absolute timestamps (wall clock)",                     OFFSET(ts_mode),      AV_OPT_TYPE_CONST,  {.i64 = V4L_TS_ABS      }, 0, 2, DEC, .unit = "timestamps" },
    { "mono2abs",     "force conversion from monotonic to absolute timestamps",   OFFSET(ts_mode),      AV_OPT_TYPE_CONST,  {.i64 = V4L_TS_MONO2ABS }, 0, 2, DEC, .unit = "timestamps" },
    { "use_libv4l2",  "use libv4l2 (v4l-utils) conversion functions",             OFFSET(use_libv4l2),  AV_OPT_TYPE_BOOL,   {.i64 = 0}, 0, 1, DEC },
    { NULL },
};

static const AVClass v4l2_class = {
    .class_name = "V4L2 indev",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
    .category   = AV_CLASS_CATEGORY_DEVICE_VIDEO_INPUT,
};

const FFInputFormat ff_v4l2_demuxer = {
    .p.name          = "video4linux2,v4l2",
    .p.long_name     = NULL_IF_CONFIG_SMALL("Video4Linux2 device grab"),
    .p.flags         = AVFMT_NOFILE,
    .p.priv_class    = &v4l2_class,
    .priv_data_size = sizeof(struct video_data),
    .read_probe     = v4l2_read_probe,
    .read_header    = v4l2_read_header,
    .read_packet    = v4l2_read_packet,
    .read_close     = v4l2_read_close,
    .get_device_list = v4l2_get_device_list,
};
