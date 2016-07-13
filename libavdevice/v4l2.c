/*
 * Video4Linux2 grab interface
 * Copyright (c) 2000,2001 Fabrice Bellard
 * Copyright (c) 2006 Luca Abeni
 *
 * Part of this file is based on the V4L2 video capture example
 * (http://v4l2spec.bytesex.org/v4l2spec/capture.c)
 *
 * Thanks to Michael Niedermayer for providing the mapping between
 * V4L2_PIX_FMT_* and AV_PIX_FMT_*
 *
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#undef __STRICT_ANSI__ //workaround due to broken kernel headers
#include "config.h"
#include "libavformat/avformat.h"
#include "libavformat/internal.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <poll.h>
#if HAVE_SYS_VIDEOIO_H
#include <sys/videoio.h>
#else
#include <linux/videodev2.h>
#endif
#include "libavutil/atomic.h"
#include "libavutil/avassert.h"
#include "libavutil/imgutils.h"
#include "libavutil/internal.h"
#include "libavutil/log.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "libavutil/pixdesc.h"
#include "libavutil/avstring.h"
#include "libavutil/mathematics.h"

static const int desired_video_buffers = 256;

#define V4L_ALLFORMATS  3
#define V4L_RAWFORMATS  1
#define V4L_COMPFORMATS 2

struct video_data {
    AVClass *class;
    int fd;
    int frame_format; /* V4L2_PIX_FMT_* */
    int width, height;
    int frame_size;
    int timeout;
    int interlaced;
    int top_field_first;

    int buffers;
    volatile int buffers_queued;
    void **buf_start;
    unsigned int *buf_len;
    char *standard;
    int channel;
    char *video_size;   /**< String describing video size,
                             set by a private option. */
    char *pixel_format; /**< Set by a private option. */
    int list_format;    /**< Set by a private option. */
    char *framerate;    /**< Set by a private option. */
};

struct buff_data {
    struct video_data *s;
    int index;
    int fd;
};

struct fmt_map {
    enum AVPixelFormat ff_fmt;
    enum AVCodecID codec_id;
    uint32_t v4l2_fmt;
};

static struct fmt_map fmt_conversion_table[] = {
    //ff_fmt           codec_id           v4l2_fmt
    { AV_PIX_FMT_YUV420P, AV_CODEC_ID_RAWVIDEO, V4L2_PIX_FMT_YUV420  },
    { AV_PIX_FMT_YUV422P, AV_CODEC_ID_RAWVIDEO, V4L2_PIX_FMT_YUV422P },
    { AV_PIX_FMT_YUYV422, AV_CODEC_ID_RAWVIDEO, V4L2_PIX_FMT_YUYV    },
    { AV_PIX_FMT_UYVY422, AV_CODEC_ID_RAWVIDEO, V4L2_PIX_FMT_UYVY    },
    { AV_PIX_FMT_YUV411P, AV_CODEC_ID_RAWVIDEO, V4L2_PIX_FMT_YUV411P },
    { AV_PIX_FMT_YUV410P, AV_CODEC_ID_RAWVIDEO, V4L2_PIX_FMT_YUV410  },
    { AV_PIX_FMT_RGB555,  AV_CODEC_ID_RAWVIDEO, V4L2_PIX_FMT_RGB555  },
    { AV_PIX_FMT_RGB565,  AV_CODEC_ID_RAWVIDEO, V4L2_PIX_FMT_RGB565  },
    { AV_PIX_FMT_BGR24,   AV_CODEC_ID_RAWVIDEO, V4L2_PIX_FMT_BGR24   },
    { AV_PIX_FMT_RGB24,   AV_CODEC_ID_RAWVIDEO, V4L2_PIX_FMT_RGB24   },
    { AV_PIX_FMT_BGRA,    AV_CODEC_ID_RAWVIDEO, V4L2_PIX_FMT_BGR32   },
    { AV_PIX_FMT_GRAY8,   AV_CODEC_ID_RAWVIDEO, V4L2_PIX_FMT_GREY    },
    { AV_PIX_FMT_NV12,    AV_CODEC_ID_RAWVIDEO, V4L2_PIX_FMT_NV12    },
    { AV_PIX_FMT_NONE,    AV_CODEC_ID_MJPEG,    V4L2_PIX_FMT_MJPEG   },
    { AV_PIX_FMT_NONE,    AV_CODEC_ID_MJPEG,    V4L2_PIX_FMT_JPEG    },
#ifdef V4L2_PIX_FMT_H264
    { AV_PIX_FMT_NONE,    AV_CODEC_ID_H264,     V4L2_PIX_FMT_H264    },
#endif
};

static int device_open(AVFormatContext *ctx)
{
    struct v4l2_capability cap;
    int fd;
    int res, err;
    int flags = O_RDWR;
    char errbuf[128];

    if (ctx->flags & AVFMT_FLAG_NONBLOCK) {
        flags |= O_NONBLOCK;
    }

    fd = avpriv_open(ctx->filename, flags);
    if (fd < 0) {
        err = AVERROR(errno);
        av_strerror(err, errbuf, sizeof(errbuf));

        av_log(ctx, AV_LOG_ERROR, "Cannot open video device %s : %s\n",
               ctx->filename, errbuf);

        return err;
    }

    res = ioctl(fd, VIDIOC_QUERYCAP, &cap);
    if (res < 0) {
        err = AVERROR(errno);
        av_strerror(err, errbuf, sizeof(errbuf));
        av_log(ctx, AV_LOG_ERROR, "ioctl(VIDIOC_QUERYCAP): %s\n",
               errbuf);

        goto fail;
    }

    av_log(ctx, AV_LOG_VERBOSE, "[%d]Capabilities: %x\n",
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
    close(fd);
    return err;
}

static int device_init(AVFormatContext *ctx, int *width, int *height,
                       uint32_t pix_fmt)
{
    struct video_data *s = ctx->priv_data;
    int fd = s->fd;
    struct v4l2_format fmt = { .type = V4L2_BUF_TYPE_VIDEO_CAPTURE };
    struct v4l2_pix_format *pix = &fmt.fmt.pix;

    int res;

    pix->width = *width;
    pix->height = *height;
    pix->pixelformat = pix_fmt;
    pix->field = V4L2_FIELD_ANY;

    res = ioctl(fd, VIDIOC_S_FMT, &fmt);

    if ((*width != fmt.fmt.pix.width) || (*height != fmt.fmt.pix.height)) {
        av_log(ctx, AV_LOG_INFO,
               "The V4L2 driver changed the video from %dx%d to %dx%d\n",
               *width, *height, fmt.fmt.pix.width, fmt.fmt.pix.height);
        *width = fmt.fmt.pix.width;
        *height = fmt.fmt.pix.height;
    }

    if (pix_fmt != fmt.fmt.pix.pixelformat) {
        av_log(ctx, AV_LOG_DEBUG,
               "The V4L2 driver changed the pixel format "
               "from 0x%08X to 0x%08X\n",
               pix_fmt, fmt.fmt.pix.pixelformat);
        res = -1;
    }

    if (fmt.fmt.pix.field == V4L2_FIELD_INTERLACED) {
        av_log(ctx, AV_LOG_DEBUG, "The V4L2 driver using the interlaced mode");
        s->interlaced = 1;
    }

    return res;
}

static int first_field(int fd)
{
    int res;
    v4l2_std_id std;

    res = ioctl(fd, VIDIOC_G_STD, &std);
    if (res < 0) {
        return 0;
    }
    if (std & V4L2_STD_NTSC) {
        return 0;
    }

    return 1;
}

static uint32_t fmt_ff2v4l(enum AVPixelFormat pix_fmt, enum AVCodecID codec_id)
{
    int i;

    for (i = 0; i < FF_ARRAY_ELEMS(fmt_conversion_table); i++) {
        if ((codec_id == AV_CODEC_ID_NONE ||
             fmt_conversion_table[i].codec_id == codec_id) &&
            (pix_fmt == AV_PIX_FMT_NONE ||
             fmt_conversion_table[i].ff_fmt == pix_fmt)) {
            return fmt_conversion_table[i].v4l2_fmt;
        }
    }

    return 0;
}

static enum AVPixelFormat fmt_v4l2ff(uint32_t v4l2_fmt, enum AVCodecID codec_id)
{
    int i;

    for (i = 0; i < FF_ARRAY_ELEMS(fmt_conversion_table); i++) {
        if (fmt_conversion_table[i].v4l2_fmt == v4l2_fmt &&
            fmt_conversion_table[i].codec_id == codec_id) {
            return fmt_conversion_table[i].ff_fmt;
        }
    }

    return AV_PIX_FMT_NONE;
}

static enum AVCodecID fmt_v4l2codec(uint32_t v4l2_fmt)
{
    int i;

    for (i = 0; i < FF_ARRAY_ELEMS(fmt_conversion_table); i++) {
        if (fmt_conversion_table[i].v4l2_fmt == v4l2_fmt) {
            return fmt_conversion_table[i].codec_id;
        }
    }

    return AV_CODEC_ID_NONE;
}

#if HAVE_STRUCT_V4L2_FRMIVALENUM_DISCRETE
static void list_framesizes(AVFormatContext *ctx, int fd, uint32_t pixelformat)
{
    struct v4l2_frmsizeenum vfse = { .pixel_format = pixelformat };

    while(!ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &vfse)) {
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

static void list_formats(AVFormatContext *ctx, int fd, int type)
{
    struct v4l2_fmtdesc vfd = { .type = V4L2_BUF_TYPE_VIDEO_CAPTURE };

    while(!ioctl(fd, VIDIOC_ENUM_FMT, &vfd)) {
        enum AVCodecID codec_id = fmt_v4l2codec(vfd.pixelformat);
        enum AVPixelFormat pix_fmt = fmt_v4l2ff(vfd.pixelformat, codec_id);

        vfd.index++;

        if (!(vfd.flags & V4L2_FMT_FLAG_COMPRESSED) &&
            type & V4L_RAWFORMATS) {
            const char *fmt_name = av_get_pix_fmt_name(pix_fmt);
            av_log(ctx, AV_LOG_INFO, "R : %9s : %20s :",
                   fmt_name ? fmt_name : "Unsupported",
                   vfd.description);
        } else if (vfd.flags & V4L2_FMT_FLAG_COMPRESSED &&
                   type & V4L_COMPFORMATS) {
            const AVCodecDescriptor *desc = avcodec_descriptor_get(codec_id);
            av_log(ctx, AV_LOG_INFO, "C : %9s : %20s :",
                   desc ? desc->name : "Unsupported",
                   vfd.description);
        } else {
            continue;
        }

#ifdef V4L2_FMT_FLAG_EMULATED
        if (vfd.flags & V4L2_FMT_FLAG_EMULATED) {
            av_log(ctx, AV_LOG_WARNING, "%s", "Emulated");
            continue;
        }
#endif
#if HAVE_STRUCT_V4L2_FRMIVALENUM_DISCRETE
        list_framesizes(ctx, fd, vfd.pixelformat);
#endif
        av_log(ctx, AV_LOG_INFO, "\n");
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

    res = ioctl(s->fd, VIDIOC_REQBUFS, &req);
    if (res < 0) {
        res = AVERROR(errno);
        if (res == AVERROR(EINVAL)) {
            av_log(ctx, AV_LOG_ERROR, "Device does not support mmap\n");
        } else {
            av_log(ctx, AV_LOG_ERROR, "ioctl(VIDIOC_REQBUFS)\n");
        }

        return res;
    }

    if (req.count < 2) {
        av_log(ctx, AV_LOG_ERROR, "Insufficient buffer memory\n");

        return AVERROR(ENOMEM);
    }
    s->buffers = req.count;
    s->buf_start = av_malloc(sizeof(void *) * s->buffers);
    if (!s->buf_start) {
        av_log(ctx, AV_LOG_ERROR, "Cannot allocate buffer pointers\n");

        return AVERROR(ENOMEM);
    }
    s->buf_len = av_malloc(sizeof(unsigned int) * s->buffers);
    if (!s->buf_len) {
        av_log(ctx, AV_LOG_ERROR, "Cannot allocate buffer sizes\n");
        av_free(s->buf_start);

        return AVERROR(ENOMEM);
    }

    for (i = 0; i < req.count; i++) {
        struct v4l2_buffer buf = {
            .type   = V4L2_BUF_TYPE_VIDEO_CAPTURE,
            .index  = i,
            .memory = V4L2_MEMORY_MMAP
        };

        res = ioctl(s->fd, VIDIOC_QUERYBUF, &buf);
        if (res < 0) {
            res = AVERROR(errno);
            av_log(ctx, AV_LOG_ERROR, "ioctl(VIDIOC_QUERYBUF)\n");

            return res;
        }

        s->buf_len[i] = buf.length;
        if (s->frame_size > 0 && s->buf_len[i] < s->frame_size) {
            av_log(ctx, AV_LOG_ERROR,
                   "Buffer len [%d] = %d != %d\n",
                   i, s->buf_len[i], s->frame_size);

            return -1;
        }
        s->buf_start[i] = mmap(NULL, buf.length,
                               PROT_READ | PROT_WRITE, MAP_SHARED,
                               s->fd, buf.m.offset);

        if (s->buf_start[i] == MAP_FAILED) {
            char errbuf[128];
            res = AVERROR(errno);
            av_strerror(res, errbuf, sizeof(errbuf));
            av_log(ctx, AV_LOG_ERROR, "mmap: %s\n", errbuf);

            return res;
        }
    }

    return 0;
}

static void mmap_release_buffer(void *opaque, uint8_t *data)
{
    struct v4l2_buffer buf = { 0 };
    int res, fd;
    struct buff_data *buf_descriptor = opaque;
    struct video_data *s = buf_descriptor->s;
    char errbuf[128];

    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = buf_descriptor->index;
    fd = buf_descriptor->fd;
    av_free(buf_descriptor);

    res = ioctl(fd, VIDIOC_QBUF, &buf);
    if (res < 0) {
        av_strerror(AVERROR(errno), errbuf, sizeof(errbuf));
        av_log(NULL, AV_LOG_ERROR, "ioctl(VIDIOC_QBUF): %s\n",
               errbuf);
    }
    avpriv_atomic_int_add_and_fetch(&s->buffers_queued, 1);
}

static int mmap_read_frame(AVFormatContext *ctx, AVPacket *pkt)
{
    struct video_data *s = ctx->priv_data;
    struct v4l2_buffer buf = {
        .type   = V4L2_BUF_TYPE_VIDEO_CAPTURE,
        .memory = V4L2_MEMORY_MMAP
    };
    struct pollfd p = { .fd = s->fd, .events = POLLIN };
    int res;

    res = poll(&p, 1, s->timeout);
    if (res < 0)
        return AVERROR(errno);

    if (!(p.revents & (POLLIN | POLLERR | POLLHUP)))
        return AVERROR(EAGAIN);

    /* FIXME: Some special treatment might be needed in case of loss of signal... */
    while ((res = ioctl(s->fd, VIDIOC_DQBUF, &buf)) < 0 && (errno == EINTR));
    if (res < 0) {
        char errbuf[128];
        if (errno == EAGAIN) {
            pkt->size = 0;

            return AVERROR(EAGAIN);
        }
        res = AVERROR(errno);
        av_strerror(res, errbuf, sizeof(errbuf));
        av_log(ctx, AV_LOG_ERROR, "ioctl(VIDIOC_DQBUF): %s\n",
               errbuf);

        return res;
    }

    if (buf.index >= s->buffers) {
        av_log(ctx, AV_LOG_ERROR, "Invalid buffer index received.\n");
        return AVERROR(EINVAL);
    }
    avpriv_atomic_int_add_and_fetch(&s->buffers_queued, -1);
    // always keep at least one buffer queued
    av_assert0(avpriv_atomic_int_get(&s->buffers_queued) >= 1);

    if (s->frame_size > 0 && buf.bytesused != s->frame_size) {
        av_log(ctx, AV_LOG_ERROR,
               "The v4l2 frame is %d bytes, but %d bytes are expected\n",
               buf.bytesused, s->frame_size);

        return AVERROR_INVALIDDATA;
    }

    /* Image is at s->buff_start[buf.index] */
    if (avpriv_atomic_int_get(&s->buffers_queued) == FFMAX(s->buffers / 8, 1)) {
        /* when we start getting low on queued buffers, fall back on copying data */
        res = av_new_packet(pkt, buf.bytesused);
        if (res < 0) {
            av_log(ctx, AV_LOG_ERROR, "Error allocating a packet.\n");
            return res;
        }
        memcpy(pkt->data, s->buf_start[buf.index], buf.bytesused);

        res = ioctl(s->fd, VIDIOC_QBUF, &buf);
        if (res < 0) {
            res = AVERROR(errno);
            av_log(ctx, AV_LOG_ERROR, "ioctl(VIDIOC_QBUF)\n");
            av_packet_unref(pkt);
            return res;
        }
        avpriv_atomic_int_add_and_fetch(&s->buffers_queued, 1);
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
            res = ioctl(s->fd, VIDIOC_QBUF, &buf);

            return AVERROR(ENOMEM);
        }
        buf_descriptor->fd    = s->fd;
        buf_descriptor->index = buf.index;
        buf_descriptor->s     = s;

        pkt->buf = av_buffer_create(pkt->data, pkt->size, mmap_release_buffer,
                                    buf_descriptor, 0);
        if (!pkt->buf) {
            av_freep(&buf_descriptor);
            return AVERROR(ENOMEM);
        }
    }
    pkt->pts = buf.timestamp.tv_sec * INT64_C(1000000) + buf.timestamp.tv_usec;

    return s->buf_len[buf.index];
}

static int mmap_start(AVFormatContext *ctx)
{
    struct video_data *s = ctx->priv_data;
    enum v4l2_buf_type type;
    int i, res, err;
    char errbuf[128];

    for (i = 0; i < s->buffers; i++) {
        struct v4l2_buffer buf = {
            .type   = V4L2_BUF_TYPE_VIDEO_CAPTURE,
            .index  = i,
            .memory = V4L2_MEMORY_MMAP
        };

        res = ioctl(s->fd, VIDIOC_QBUF, &buf);
        if (res < 0) {
            err = AVERROR(errno);
            av_strerror(err, errbuf, sizeof(errbuf));
            av_log(ctx, AV_LOG_ERROR, "ioctl(VIDIOC_QBUF): %s\n",
                   errbuf);

            return err;
        }
    }
    s->buffers_queued = s->buffers;

    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    res = ioctl(s->fd, VIDIOC_STREAMON, &type);
    if (res < 0) {
        err = AVERROR(errno);
        av_strerror(err, errbuf, sizeof(errbuf));
        av_log(ctx, AV_LOG_ERROR, "ioctl(VIDIOC_STREAMON): %s\n",
               errbuf);

        return err;
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
    ioctl(s->fd, VIDIOC_STREAMOFF, &type);
    for (i = 0; i < s->buffers; i++) {
        munmap(s->buf_start[i], s->buf_len[i]);
    }
    av_free(s->buf_start);
    av_free(s->buf_len);
}

static int v4l2_set_parameters(AVFormatContext *s1)
{
    struct video_data *s = s1->priv_data;
    struct v4l2_input input = { 0 };
    struct v4l2_standard standard = { 0 };
    struct v4l2_streamparm streamparm = { 0 };
    struct v4l2_fract *tpf = &streamparm.parm.capture.timeperframe;
    AVRational framerate_q = { 0 };
    int i, ret;

    streamparm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (s->framerate &&
        (ret = av_parse_video_rate(&framerate_q, s->framerate)) < 0) {
        av_log(s1, AV_LOG_ERROR, "Could not parse framerate '%s'.\n",
               s->framerate);
        return ret;
    }

    /* set tv video input */
    input.index = s->channel;
    if (ioctl(s->fd, VIDIOC_ENUMINPUT, &input) < 0) {
        av_log(s1, AV_LOG_ERROR, "The V4L2 driver ioctl enum input failed:\n");
        return AVERROR(EIO);
    }

    av_log(s1, AV_LOG_DEBUG, "The V4L2 driver set input_id: %d, input: %s\n",
            s->channel, input.name);
    if (ioctl(s->fd, VIDIOC_S_INPUT, &input.index) < 0) {
        av_log(s1, AV_LOG_ERROR,
               "The V4L2 driver ioctl set input(%d) failed\n",
                s->channel);
        return AVERROR(EIO);
    }

    if (s->standard) {
        av_log(s1, AV_LOG_DEBUG, "The V4L2 driver set standard: %s\n",
               s->standard);
        /* set tv standard */
        for(i=0;;i++) {
            standard.index = i;
            if (ioctl(s->fd, VIDIOC_ENUMSTD, &standard) < 0) {
                av_log(s1, AV_LOG_ERROR,
                       "The V4L2 driver ioctl set standard(%s) failed\n",
                       s->standard);
                return AVERROR(EIO);
            }

            if (!av_strcasecmp(standard.name, s->standard)) {
                break;
            }
        }

        av_log(s1, AV_LOG_DEBUG,
               "The V4L2 driver set standard: %s, id: %"PRIu64"\n",
               s->standard, (uint64_t)standard.id);
        if (ioctl(s->fd, VIDIOC_S_STD, &standard.id) < 0) {
            av_log(s1, AV_LOG_ERROR,
                   "The V4L2 driver ioctl set standard(%s) failed\n",
                   s->standard);
            return AVERROR(EIO);
        }
    }

    if (framerate_q.num && framerate_q.den) {
        av_log(s1, AV_LOG_DEBUG, "Setting time per frame to %d/%d\n",
               framerate_q.den, framerate_q.num);
        tpf->numerator   = framerate_q.den;
        tpf->denominator = framerate_q.num;

        if (ioctl(s->fd, VIDIOC_S_PARM, &streamparm) != 0) {
            av_log(s1, AV_LOG_ERROR,
                   "ioctl set time per frame(%d/%d) failed\n",
                   framerate_q.den, framerate_q.num);
            return AVERROR(EIO);
        }

        if (framerate_q.num != tpf->denominator ||
            framerate_q.den != tpf->numerator) {
            av_log(s1, AV_LOG_INFO,
                   "The driver changed the time per frame from "
                   "%d/%d to %d/%d\n",
                   framerate_q.den, framerate_q.num,
                   tpf->numerator, tpf->denominator);
        }
    } else {
        if (ioctl(s->fd, VIDIOC_G_PARM, &streamparm) != 0) {
            char errbuf[128];
            ret = AVERROR(errno);
            av_strerror(ret, errbuf, sizeof(errbuf));
            av_log(s1, AV_LOG_ERROR, "ioctl(VIDIOC_G_PARM): %s\n",
                   errbuf);
            return ret;
        }
    }
    s1->streams[0]->avg_frame_rate.num = tpf->denominator;
    s1->streams[0]->avg_frame_rate.den = tpf->numerator;

    s->timeout = 100 +
        av_rescale_q(1, s1->streams[0]->avg_frame_rate,
                        (AVRational){1, 1000});

    return 0;
}

static uint32_t device_try_init(AVFormatContext *s1,
                                enum AVPixelFormat pix_fmt,
                                int *width,
                                int *height,
                                enum AVCodecID *codec_id)
{
    uint32_t desired_format = fmt_ff2v4l(pix_fmt, s1->video_codec_id);

    if (desired_format == 0 ||
        device_init(s1, width, height, desired_format) < 0) {
        int i;

        desired_format = 0;
        for (i = 0; i<FF_ARRAY_ELEMS(fmt_conversion_table); i++) {
            if (s1->video_codec_id == AV_CODEC_ID_NONE ||
                fmt_conversion_table[i].codec_id == s1->video_codec_id) {
                desired_format = fmt_conversion_table[i].v4l2_fmt;
                if (device_init(s1, width, height, desired_format) >= 0) {
                    break;
                }
                desired_format = 0;
            }
        }
    }

    if (desired_format != 0) {
        *codec_id = fmt_v4l2codec(desired_format);
        assert(*codec_id != AV_CODEC_ID_NONE);
    }

    return desired_format;
}

static int v4l2_read_header(AVFormatContext *s1)
{
    struct video_data *s = s1->priv_data;
    AVStream *st;
    int res = 0;
    uint32_t desired_format;
    enum AVCodecID codec_id;
    enum AVPixelFormat pix_fmt = AV_PIX_FMT_NONE;

    st = avformat_new_stream(s1, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    s->fd = device_open(s1);
    if (s->fd < 0)
        return s->fd;

    if (s->list_format) {
        list_formats(s1, s->fd, s->list_format);
        return AVERROR_EXIT;
    }

    avpriv_set_pts_info(st, 64, 1, 1000000); /* 64 bits pts in us */

    if (s->video_size &&
        (res = av_parse_video_size(&s->width, &s->height, s->video_size)) < 0) {
        av_log(s1, AV_LOG_ERROR, "Could not parse video size '%s'.\n",
               s->video_size);
        return res;
    }

    if (s->pixel_format) {
        AVCodec *codec = avcodec_find_decoder_by_name(s->pixel_format);

        if (codec) {
            s1->video_codec_id = codec->id;
            st->need_parsing   = AVSTREAM_PARSE_HEADERS;
        }

        pix_fmt = av_get_pix_fmt(s->pixel_format);

        if (pix_fmt == AV_PIX_FMT_NONE && !codec) {
            av_log(s1, AV_LOG_ERROR, "No such input format: %s.\n",
                   s->pixel_format);

            return AVERROR(EINVAL);
        }
    }

    if (!s->width && !s->height) {
        struct v4l2_format fmt;

        av_log(s1, AV_LOG_VERBOSE,
               "Querying the device for the current frame size\n");
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ioctl(s->fd, VIDIOC_G_FMT, &fmt) < 0) {
            char errbuf[128];
            res = AVERROR(errno);
            av_strerror(res, errbuf, sizeof(errbuf));
            av_log(s1, AV_LOG_ERROR, "ioctl(VIDIOC_G_FMT): %s\n",
                   errbuf);
            return res;
        }

        s->width  = fmt.fmt.pix.width;
        s->height = fmt.fmt.pix.height;
        av_log(s1, AV_LOG_VERBOSE,
               "Setting frame size to %dx%d\n", s->width, s->height);
    }

    desired_format = device_try_init(s1, pix_fmt, &s->width, &s->height,
                                     &codec_id);
    if (desired_format == 0) {
        av_log(s1, AV_LOG_ERROR, "Cannot find a proper format for "
               "codec_id %d, pix_fmt %d.\n", s1->video_codec_id, pix_fmt);
        close(s->fd);

        return AVERROR(EIO);
    }

    if ((res = av_image_check_size(s->width, s->height, 0, s1) < 0))
        return res;

    s->frame_format = desired_format;

    if ((res = v4l2_set_parameters(s1) < 0))
        return res;

    st->codecpar->format = fmt_v4l2ff(desired_format, codec_id);
    s->frame_size = av_image_get_buffer_size(st->codecpar->format,
                                             s->width, s->height, 1);

    if ((res = mmap_init(s1)) ||
        (res = mmap_start(s1)) < 0) {
        close(s->fd);
        return res;
    }

    s->top_field_first = first_field(s->fd);

    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_id = codec_id;
    if (codec_id == AV_CODEC_ID_RAWVIDEO)
        st->codecpar->codec_tag =
            avcodec_pix_fmt_to_codec_tag(st->codecpar->format);
    st->codecpar->width = s->width;
    st->codecpar->height = s->height;
    st->codecpar->bit_rate = s->frame_size * av_q2d(st->avg_frame_rate) * 8;

    return 0;
}

static int v4l2_read_packet(AVFormatContext *s1, AVPacket *pkt)
{
#if FF_API_CODED_FRAME && FF_API_LAVF_AVCTX
FF_DISABLE_DEPRECATION_WARNINGS
    struct video_data *s = s1->priv_data;
    AVFrame *frame = s1->streams[0]->codec->coded_frame;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
    int res;

    if ((res = mmap_read_frame(s1, pkt)) < 0) {
        return res;
    }

#if FF_API_CODED_FRAME && FF_API_LAVF_AVCTX
FF_DISABLE_DEPRECATION_WARNINGS
    if (frame && s->interlaced) {
        frame->interlaced_frame = 1;
        frame->top_field_first = s->top_field_first;
    }
FF_ENABLE_DEPRECATION_WARNINGS
#endif

    return pkt->size;
}

static int v4l2_read_close(AVFormatContext *s1)
{
    struct video_data *s = s1->priv_data;

    if (avpriv_atomic_int_get(&s->buffers_queued) != s->buffers)
        av_log(s1, AV_LOG_WARNING, "Some buffers are still owned by the caller on "
               "close.\n");

    mmap_close(s);

    close(s->fd);
    return 0;
}

#define OFFSET(x) offsetof(struct video_data, x)
#define DEC AV_OPT_FLAG_DECODING_PARAM
static const AVOption options[] = {
    { "standard",     "TV standard, used only by analog frame grabber",            OFFSET(standard),     AV_OPT_TYPE_STRING, {.str = NULL }, 0, 0,       DEC },
    { "channel",      "TV channel, used only by frame grabber",                    OFFSET(channel),      AV_OPT_TYPE_INT,    {.i64 = 0 },    0, INT_MAX, DEC },
    { "video_size",   "A string describing frame size, such as 640x480 or hd720.", OFFSET(video_size),   AV_OPT_TYPE_STRING, {.str = NULL},  0, 0,       DEC },
    { "pixel_format", "Preferred pixel format",                                    OFFSET(pixel_format), AV_OPT_TYPE_STRING, {.str = NULL},  0, 0,       DEC },
    { "input_format", "Preferred pixel format (for raw video) or codec name",      OFFSET(pixel_format), AV_OPT_TYPE_STRING, {.str = NULL},  0, 0,       DEC },
    { "framerate",    "",                                                          OFFSET(framerate),    AV_OPT_TYPE_STRING, {.str = NULL},  0, 0,       DEC },
    { "list_formats", "List available formats and exit",                           OFFSET(list_format),  AV_OPT_TYPE_INT,    {.i64 = 0 },  0, INT_MAX, DEC, "list_formats" },
    { "all",          "Show all available formats",                                OFFSET(list_format),  AV_OPT_TYPE_CONST,  {.i64 = V4L_ALLFORMATS  },    0, INT_MAX, DEC, "list_formats" },
    { "raw",          "Show only non-compressed formats",                          OFFSET(list_format),  AV_OPT_TYPE_CONST,  {.i64 = V4L_RAWFORMATS  },    0, INT_MAX, DEC, "list_formats" },
    { "compressed",   "Show only compressed formats",                              OFFSET(list_format),  AV_OPT_TYPE_CONST,  {.i64 = V4L_COMPFORMATS },    0, INT_MAX, DEC, "list_formats" },
    { NULL },
};

static const AVClass v4l2_class = {
    .class_name = "V4L2 indev",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVInputFormat ff_v4l2_demuxer = {
    .name           = "video4linux2",
    .long_name      = NULL_IF_CONFIG_SMALL("Video4Linux2 device grab"),
    .priv_data_size = sizeof(struct video_data),
    .read_header    = v4l2_read_header,
    .read_packet    = v4l2_read_packet,
    .read_close     = v4l2_read_close,
    .flags          = AVFMT_NOFILE,
    .priv_class     = &v4l2_class,
};
