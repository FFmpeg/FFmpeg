/*
 * V4L2 context helper functions.
 *
 * Copyright (C) 2017 Alexis Ballier <aballier@gentoo.org>
 * Copyright (C) 2017 Jorge Ramirez <jorge.ramirez-ortiz@linaro.org>
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

#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include "libavcodec/avcodec.h"
#include "libavcodec/internal.h"
#include "v4l2_buffers.h"
#include "v4l2_fmt.h"
#include "v4l2_m2m.h"

struct v4l2_format_update {
    uint32_t v4l2_fmt;
    int update_v4l2;

    enum AVPixelFormat av_fmt;
    int update_avfmt;
};

static inline V4L2m2mContext *ctx_to_m2mctx(V4L2Context *ctx)
{
    return V4L2_TYPE_IS_OUTPUT(ctx->type) ?
        container_of(ctx, V4L2m2mContext, output) :
        container_of(ctx, V4L2m2mContext, capture);
}

static inline AVCodecContext *logger(V4L2Context *ctx)
{
    return ctx_to_m2mctx(ctx)->avctx;
}

static inline unsigned int v4l2_get_width(struct v4l2_format *fmt)
{
    return V4L2_TYPE_IS_MULTIPLANAR(fmt->type) ? fmt->fmt.pix_mp.width : fmt->fmt.pix.width;
}

static inline unsigned int v4l2_get_height(struct v4l2_format *fmt)
{
    return V4L2_TYPE_IS_MULTIPLANAR(fmt->type) ? fmt->fmt.pix_mp.height : fmt->fmt.pix.height;
}

static AVRational v4l2_get_sar(V4L2Context *ctx)
{
    struct AVRational sar = { 0, 1 };
    struct v4l2_cropcap cropcap;
    int ret;

    memset(&cropcap, 0, sizeof(cropcap));
    cropcap.type = ctx->type;

    ret = ioctl(ctx_to_m2mctx(ctx)->fd, VIDIOC_CROPCAP, &cropcap);
    if (ret)
        return sar;

    sar.num = cropcap.pixelaspect.numerator;
    sar.den = cropcap.pixelaspect.denominator;
    return sar;
}

static inline unsigned int v4l2_resolution_changed(V4L2Context *ctx, struct v4l2_format *fmt2)
{
    struct v4l2_format *fmt1 = &ctx->format;
    int ret =  V4L2_TYPE_IS_MULTIPLANAR(ctx->type) ?
        fmt1->fmt.pix_mp.width != fmt2->fmt.pix_mp.width ||
        fmt1->fmt.pix_mp.height != fmt2->fmt.pix_mp.height
        :
        fmt1->fmt.pix.width != fmt2->fmt.pix.width ||
        fmt1->fmt.pix.height != fmt2->fmt.pix.height;

    if (ret)
        av_log(logger(ctx), AV_LOG_DEBUG, "%s changed (%dx%d) -> (%dx%d)\n",
            ctx->name,
            v4l2_get_width(fmt1), v4l2_get_height(fmt1),
            v4l2_get_width(fmt2), v4l2_get_height(fmt2));

    return ret;
}

static inline int v4l2_type_supported(V4L2Context *ctx)
{
    return ctx->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE ||
        ctx->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE ||
        ctx->type == V4L2_BUF_TYPE_VIDEO_CAPTURE ||
        ctx->type == V4L2_BUF_TYPE_VIDEO_OUTPUT;
}

static inline int v4l2_get_framesize_compressed(V4L2Context* ctx, int width, int height)
{
    V4L2m2mContext *s = ctx_to_m2mctx(ctx);
    const int SZ_4K = 0x1000;
    int size;

    if (s->avctx && av_codec_is_decoder(s->avctx->codec))
        return ((width * height * 3 / 2) / 2) + 128;

    /* encoder */
    size = FFALIGN(height, 32) * FFALIGN(width, 32) * 3 / 2 / 2;
    return FFALIGN(size, SZ_4K);
}

static inline void v4l2_save_to_context(V4L2Context* ctx, struct v4l2_format_update *fmt)
{
    ctx->format.type = ctx->type;

    if (fmt->update_avfmt)
        ctx->av_pix_fmt = fmt->av_fmt;

    if (V4L2_TYPE_IS_MULTIPLANAR(ctx->type)) {
        /* update the sizes to handle the reconfiguration of the capture stream at runtime */
        ctx->format.fmt.pix_mp.height = ctx->height;
        ctx->format.fmt.pix_mp.width = ctx->width;
        if (fmt->update_v4l2) {
            ctx->format.fmt.pix_mp.pixelformat = fmt->v4l2_fmt;

            /* s5p-mfc requires the user to specify a buffer size */
            ctx->format.fmt.pix_mp.plane_fmt[0].sizeimage =
                v4l2_get_framesize_compressed(ctx, ctx->width, ctx->height);
        }
    } else {
        ctx->format.fmt.pix.height = ctx->height;
        ctx->format.fmt.pix.width = ctx->width;
        if (fmt->update_v4l2) {
            ctx->format.fmt.pix.pixelformat = fmt->v4l2_fmt;

            /* s5p-mfc requires the user to specify a buffer size */
            ctx->format.fmt.pix.sizeimage =
                v4l2_get_framesize_compressed(ctx, ctx->width, ctx->height);
        }
    }
}

static int v4l2_start_decode(V4L2Context *ctx)
{
    struct v4l2_decoder_cmd cmd = {
        .cmd = V4L2_DEC_CMD_START,
        .flags = 0,
    };
    int ret;

    ret = ioctl(ctx_to_m2mctx(ctx)->fd, VIDIOC_DECODER_CMD, &cmd);
    if (ret)
        return AVERROR(errno);

    return 0;
}

/**
 * handle resolution change event and end of stream event
 * returns 1 if reinit was successful, negative if it failed
 * returns 0 if reinit was not executed
 */
static int v4l2_handle_event(V4L2Context *ctx)
{
    V4L2m2mContext *s = ctx_to_m2mctx(ctx);
    struct v4l2_format cap_fmt = s->capture.format;
    struct v4l2_event evt = { 0 };
    int ret;

    ret = ioctl(s->fd, VIDIOC_DQEVENT, &evt);
    if (ret < 0) {
        av_log(logger(ctx), AV_LOG_ERROR, "%s VIDIOC_DQEVENT\n", ctx->name);
        return 0;
    }

    if (evt.type == V4L2_EVENT_EOS) {
        ctx->done = 1;
        return 0;
    }

    if (evt.type != V4L2_EVENT_SOURCE_CHANGE)
        return 0;

    ret = ioctl(s->fd, VIDIOC_G_FMT, &cap_fmt);
    if (ret) {
        av_log(logger(ctx), AV_LOG_ERROR, "%s VIDIOC_G_FMT\n", s->capture.name);
        return 0;
    }

    if (v4l2_resolution_changed(&s->capture, &cap_fmt)) {
        s->capture.height = v4l2_get_height(&cap_fmt);
        s->capture.width = v4l2_get_width(&cap_fmt);
        s->capture.sample_aspect_ratio = v4l2_get_sar(&s->capture);
    } else {
        v4l2_start_decode(ctx);
        return 0;
    }

    s->reinit = 1;

    if (s->avctx)
        ret = ff_set_dimensions(s->avctx, s->capture.width, s->capture.height);
    if (ret < 0)
        av_log(logger(ctx), AV_LOG_WARNING, "update avcodec height and width\n");

    ret = ff_v4l2_m2m_codec_reinit(s);
    if (ret) {
        av_log(logger(ctx), AV_LOG_ERROR, "v4l2_m2m_codec_reinit\n");
        return AVERROR(EINVAL);
    }

    /* reinit executed */
    return 1;
}

static int v4l2_stop_decode(V4L2Context *ctx)
{
    struct v4l2_decoder_cmd cmd = {
        .cmd = V4L2_DEC_CMD_STOP,
        .flags = 0,
    };
    int ret;

    ret = ioctl(ctx_to_m2mctx(ctx)->fd, VIDIOC_DECODER_CMD, &cmd);
    if (ret) {
        /* DECODER_CMD is optional */
        if (errno == ENOTTY)
            return ff_v4l2_context_set_status(ctx, VIDIOC_STREAMOFF);
        else
            return AVERROR(errno);
    }

    return 0;
}

static int v4l2_stop_encode(V4L2Context *ctx)
{
    struct v4l2_encoder_cmd cmd = {
        .cmd = V4L2_ENC_CMD_STOP,
        .flags = 0,
    };
    int ret;

    ret = ioctl(ctx_to_m2mctx(ctx)->fd, VIDIOC_ENCODER_CMD, &cmd);
    if (ret) {
        /* ENCODER_CMD is optional */
        if (errno == ENOTTY)
            return ff_v4l2_context_set_status(ctx, VIDIOC_STREAMOFF);
        else
            return AVERROR(errno);
    }

    return 0;
}

static V4L2Buffer* v4l2_dequeue_v4l2buf(V4L2Context *ctx, int timeout)
{
    struct v4l2_plane planes[VIDEO_MAX_PLANES];
    struct v4l2_buffer buf = { 0 };
    V4L2Buffer *avbuf;
    struct pollfd pfd = {
        .events =  POLLIN | POLLRDNORM | POLLPRI | POLLOUT | POLLWRNORM, /* default blocking capture */
        .fd = ctx_to_m2mctx(ctx)->fd,
    };
    int i, ret;

    if (!V4L2_TYPE_IS_OUTPUT(ctx->type) && ctx->buffers) {
        for (i = 0; i < ctx->num_buffers; i++) {
            if (ctx->buffers[i].status == V4L2BUF_IN_DRIVER)
                break;
        }
        if (i == ctx->num_buffers)
            av_log(logger(ctx), AV_LOG_WARNING, "All capture buffers returned to "
                                                "userspace. Increase num_capture_buffers "
                                                "to prevent device deadlock or dropped "
                                                "packets/frames.\n");
    }

    /* if we are draining and there are no more capture buffers queued in the driver we are done */
    if (!V4L2_TYPE_IS_OUTPUT(ctx->type) && ctx_to_m2mctx(ctx)->draining) {
        for (i = 0; i < ctx->num_buffers; i++) {
            /* capture buffer initialization happens during decode hence
             * detection happens at runtime
             */
            if (!ctx->buffers)
                break;

            if (ctx->buffers[i].status == V4L2BUF_IN_DRIVER)
                goto start;
        }
        ctx->done = 1;
        return NULL;
    }

start:
    if (V4L2_TYPE_IS_OUTPUT(ctx->type))
        pfd.events =  POLLOUT | POLLWRNORM;
    else {
        /* no need to listen to requests for more input while draining */
        if (ctx_to_m2mctx(ctx)->draining)
            pfd.events =  POLLIN | POLLRDNORM | POLLPRI;
    }

    for (;;) {
        ret = poll(&pfd, 1, timeout);
        if (ret > 0)
            break;
        if (errno == EINTR)
            continue;
        return NULL;
    }

    /* 0. handle errors */
    if (pfd.revents & POLLERR) {
        /* if we are trying to get free buffers but none have been queued yet
           no need to raise a warning */
        if (timeout == 0) {
            for (i = 0; i < ctx->num_buffers; i++) {
                if (ctx->buffers[i].status != V4L2BUF_AVAILABLE)
                    av_log(logger(ctx), AV_LOG_WARNING, "%s POLLERR\n", ctx->name);
            }
        }
        else
            av_log(logger(ctx), AV_LOG_WARNING, "%s POLLERR\n", ctx->name);

        return NULL;
    }

    /* 1. handle resolution changes */
    if (pfd.revents & POLLPRI) {
        ret = v4l2_handle_event(ctx);
        if (ret < 0) {
            /* if re-init failed, abort */
            ctx->done = 1;
            return NULL;
        }
        if (ret) {
            /* if re-init was successful drop the buffer (if there was one)
             * since we had to reconfigure capture (unmap all buffers)
             */
            return NULL;
        }
    }

    /* 2. dequeue the buffer */
    if (pfd.revents & (POLLIN | POLLRDNORM | POLLOUT | POLLWRNORM)) {

        if (!V4L2_TYPE_IS_OUTPUT(ctx->type)) {
            /* there is a capture buffer ready */
            if (pfd.revents & (POLLIN | POLLRDNORM))
                goto dequeue;

            /* the driver is ready to accept more input; instead of waiting for the capture
             * buffer to complete we return NULL so input can proceed (we are single threaded)
             */
            if (pfd.revents & (POLLOUT | POLLWRNORM))
                return NULL;
        }

dequeue:
        memset(&buf, 0, sizeof(buf));
        buf.memory = V4L2_MEMORY_MMAP;
        buf.type = ctx->type;
        if (V4L2_TYPE_IS_MULTIPLANAR(ctx->type)) {
            memset(planes, 0, sizeof(planes));
            buf.length = VIDEO_MAX_PLANES;
            buf.m.planes = planes;
        }

        ret = ioctl(ctx_to_m2mctx(ctx)->fd, VIDIOC_DQBUF, &buf);
        if (ret) {
            if (errno != EAGAIN) {
                ctx->done = 1;
                if (errno != EPIPE)
                    av_log(logger(ctx), AV_LOG_DEBUG, "%s VIDIOC_DQBUF, errno (%s)\n",
                        ctx->name, av_err2str(AVERROR(errno)));
            }
            return NULL;
        }

        if (ctx_to_m2mctx(ctx)->draining && !V4L2_TYPE_IS_OUTPUT(ctx->type)) {
            int bytesused = V4L2_TYPE_IS_MULTIPLANAR(buf.type) ?
                            buf.m.planes[0].bytesused : buf.bytesused;
            if (bytesused == 0) {
                ctx->done = 1;
                return NULL;
            }
#ifdef V4L2_BUF_FLAG_LAST
            if (buf.flags & V4L2_BUF_FLAG_LAST)
                ctx->done = 1;
#endif
        }

        avbuf = &ctx->buffers[buf.index];
        avbuf->status = V4L2BUF_AVAILABLE;
        avbuf->buf = buf;
        if (V4L2_TYPE_IS_MULTIPLANAR(ctx->type)) {
            memcpy(avbuf->planes, planes, sizeof(planes));
            avbuf->buf.m.planes = avbuf->planes;
        }
        return avbuf;
    }

    return NULL;
}

static V4L2Buffer* v4l2_getfree_v4l2buf(V4L2Context *ctx)
{
    int timeout = 0; /* return when no more buffers to dequeue */
    int i;

    /* get back as many output buffers as possible */
    if (V4L2_TYPE_IS_OUTPUT(ctx->type)) {
          do {
          } while (v4l2_dequeue_v4l2buf(ctx, timeout));
    }

    for (i = 0; i < ctx->num_buffers; i++) {
        if (ctx->buffers[i].status == V4L2BUF_AVAILABLE)
            return &ctx->buffers[i];
    }

    return NULL;
}

static int v4l2_release_buffers(V4L2Context* ctx)
{
    struct v4l2_requestbuffers req = {
        .memory = V4L2_MEMORY_MMAP,
        .type = ctx->type,
        .count = 0, /* 0 -> unmaps buffers from the driver */
    };
    int i, j;

    for (i = 0; i < ctx->num_buffers; i++) {
        V4L2Buffer *buffer = &ctx->buffers[i];

        for (j = 0; j < buffer->num_planes; j++) {
            struct V4L2Plane_info *p = &buffer->plane_info[j];
            if (p->mm_addr && p->length)
                if (munmap(p->mm_addr, p->length) < 0)
                    av_log(logger(ctx), AV_LOG_ERROR, "%s unmap plane (%s))\n", ctx->name, av_err2str(AVERROR(errno)));
        }
    }

    return ioctl(ctx_to_m2mctx(ctx)->fd, VIDIOC_REQBUFS, &req);
}

static inline int v4l2_try_raw_format(V4L2Context* ctx, enum AVPixelFormat pixfmt)
{
    struct v4l2_format *fmt = &ctx->format;
    uint32_t v4l2_fmt;
    int ret;

    v4l2_fmt = ff_v4l2_format_avfmt_to_v4l2(pixfmt);
    if (!v4l2_fmt)
        return AVERROR(EINVAL);

    if (V4L2_TYPE_IS_MULTIPLANAR(ctx->type))
        fmt->fmt.pix_mp.pixelformat = v4l2_fmt;
    else
        fmt->fmt.pix.pixelformat = v4l2_fmt;

    fmt->type = ctx->type;

    ret = ioctl(ctx_to_m2mctx(ctx)->fd, VIDIOC_TRY_FMT, fmt);
    if (ret)
        return AVERROR(EINVAL);

    return 0;
}

static int v4l2_get_raw_format(V4L2Context* ctx, enum AVPixelFormat *p)
{
    enum AVPixelFormat pixfmt = ctx->av_pix_fmt;
    struct v4l2_fmtdesc fdesc;
    int ret;

    memset(&fdesc, 0, sizeof(fdesc));
    fdesc.type = ctx->type;

    if (pixfmt != AV_PIX_FMT_NONE) {
        ret = v4l2_try_raw_format(ctx, pixfmt);
        if (!ret)
            return 0;
    }

    for (;;) {
        ret = ioctl(ctx_to_m2mctx(ctx)->fd, VIDIOC_ENUM_FMT, &fdesc);
        if (ret)
            return AVERROR(EINVAL);

        pixfmt = ff_v4l2_format_v4l2_to_avfmt(fdesc.pixelformat, AV_CODEC_ID_RAWVIDEO);
        ret = v4l2_try_raw_format(ctx, pixfmt);
        if (ret){
            fdesc.index++;
            continue;
        }

        *p = pixfmt;

        return 0;
    }

    return AVERROR(EINVAL);
}

static int v4l2_get_coded_format(V4L2Context* ctx, uint32_t *p)
{
    struct v4l2_fmtdesc fdesc;
    uint32_t v4l2_fmt;
    int ret;

    /* translate to a valid v4l2 format */
    v4l2_fmt = ff_v4l2_format_avcodec_to_v4l2(ctx->av_codec_id);
    if (!v4l2_fmt)
        return AVERROR(EINVAL);

    /* check if the driver supports this format */
    memset(&fdesc, 0, sizeof(fdesc));
    fdesc.type = ctx->type;

    for (;;) {
        ret = ioctl(ctx_to_m2mctx(ctx)->fd, VIDIOC_ENUM_FMT, &fdesc);
        if (ret)
            return AVERROR(EINVAL);

        if (fdesc.pixelformat == v4l2_fmt)
            break;

        fdesc.index++;
    }

    *p = v4l2_fmt;

    return 0;
}

 /*****************************************************************************
  *
  *             V4L2 Context Interface
  *
  *****************************************************************************/

int ff_v4l2_context_set_status(V4L2Context* ctx, uint32_t cmd)
{
    int type = ctx->type;
    int ret;

    ret = ioctl(ctx_to_m2mctx(ctx)->fd, cmd, &type);
    if (ret < 0)
        return AVERROR(errno);

    ctx->streamon = (cmd == VIDIOC_STREAMON);

    return 0;
}

int ff_v4l2_context_enqueue_frame(V4L2Context* ctx, const AVFrame* frame)
{
    V4L2m2mContext *s = ctx_to_m2mctx(ctx);
    V4L2Buffer* avbuf;
    int ret;

    if (!frame) {
        ret = v4l2_stop_encode(ctx);
        if (ret)
            av_log(logger(ctx), AV_LOG_ERROR, "%s stop_encode\n", ctx->name);
        s->draining= 1;
        return 0;
    }

    avbuf = v4l2_getfree_v4l2buf(ctx);
    if (!avbuf)
        return AVERROR(EAGAIN);

    ret = ff_v4l2_buffer_avframe_to_buf(frame, avbuf);
    if (ret)
        return ret;

    return ff_v4l2_buffer_enqueue(avbuf);
}

int ff_v4l2_context_enqueue_packet(V4L2Context* ctx, const AVPacket* pkt)
{
    V4L2m2mContext *s = ctx_to_m2mctx(ctx);
    V4L2Buffer* avbuf;
    int ret;

    if (!pkt->size) {
        ret = v4l2_stop_decode(ctx);
        if (ret)
            av_log(logger(ctx), AV_LOG_ERROR, "%s stop_decode\n", ctx->name);
        s->draining = 1;
        return 0;
    }

    avbuf = v4l2_getfree_v4l2buf(ctx);
    if (!avbuf)
        return AVERROR(EAGAIN);

    ret = ff_v4l2_buffer_avpkt_to_buf(pkt, avbuf);
    if (ret)
        return ret;

    return ff_v4l2_buffer_enqueue(avbuf);
}

int ff_v4l2_context_dequeue_frame(V4L2Context* ctx, AVFrame* frame, int timeout)
{
    V4L2Buffer *avbuf;

    /*
     * timeout=-1 blocks until:
     *  1. decoded frame available
     *  2. an input buffer is ready to be dequeued
     */
    avbuf = v4l2_dequeue_v4l2buf(ctx, timeout);
    if (!avbuf) {
        if (ctx->done)
            return AVERROR_EOF;

        return AVERROR(EAGAIN);
    }

    return ff_v4l2_buffer_buf_to_avframe(frame, avbuf);
}

int ff_v4l2_context_dequeue_packet(V4L2Context* ctx, AVPacket* pkt)
{
    V4L2Buffer *avbuf;

    /*
     * blocks until:
     *  1. encoded packet available
     *  2. an input buffer ready to be dequeued
     */
    avbuf = v4l2_dequeue_v4l2buf(ctx, -1);
    if (!avbuf) {
        if (ctx->done)
            return AVERROR_EOF;

        return AVERROR(EAGAIN);
    }

    return ff_v4l2_buffer_buf_to_avpkt(pkt, avbuf);
}

int ff_v4l2_context_get_format(V4L2Context* ctx, int probe)
{
    struct v4l2_format_update fmt = { 0 };
    int ret;

    if  (ctx->av_codec_id == AV_CODEC_ID_RAWVIDEO) {
        ret = v4l2_get_raw_format(ctx, &fmt.av_fmt);
        if (ret)
            return ret;

        fmt.update_avfmt = !probe;
        v4l2_save_to_context(ctx, &fmt);

        /* format has been tried already */
        return ret;
    }

    ret = v4l2_get_coded_format(ctx, &fmt.v4l2_fmt);
    if (ret)
        return ret;

    fmt.update_v4l2 = 1;
    v4l2_save_to_context(ctx, &fmt);

    return ioctl(ctx_to_m2mctx(ctx)->fd, VIDIOC_TRY_FMT, &ctx->format);
}

int ff_v4l2_context_set_format(V4L2Context* ctx)
{
    return ioctl(ctx_to_m2mctx(ctx)->fd, VIDIOC_S_FMT, &ctx->format);
}

void ff_v4l2_context_release(V4L2Context* ctx)
{
    int ret;

    if (!ctx->buffers)
        return;

    ret = v4l2_release_buffers(ctx);
    if (ret)
        av_log(logger(ctx), AV_LOG_WARNING, "V4L2 failed to unmap the %s buffers\n", ctx->name);

    av_freep(&ctx->buffers);
}

int ff_v4l2_context_init(V4L2Context* ctx)
{
    V4L2m2mContext *s = ctx_to_m2mctx(ctx);
    struct v4l2_requestbuffers req;
    int ret, i;

    if (!v4l2_type_supported(ctx)) {
        av_log(logger(ctx), AV_LOG_ERROR, "type %i not supported\n", ctx->type);
        return AVERROR_PATCHWELCOME;
    }

    ret = ioctl(s->fd, VIDIOC_G_FMT, &ctx->format);
    if (ret)
        av_log(logger(ctx), AV_LOG_ERROR, "%s VIDIOC_G_FMT failed\n", ctx->name);

    memset(&req, 0, sizeof(req));
    req.count = ctx->num_buffers;
    req.memory = V4L2_MEMORY_MMAP;
    req.type = ctx->type;
    ret = ioctl(s->fd, VIDIOC_REQBUFS, &req);
    if (ret < 0) {
        av_log(logger(ctx), AV_LOG_ERROR, "%s VIDIOC_REQBUFS failed: %s\n", ctx->name, strerror(errno));
        return AVERROR(errno);
    }

    ctx->num_buffers = req.count;
    ctx->buffers = av_mallocz(ctx->num_buffers * sizeof(V4L2Buffer));
    if (!ctx->buffers) {
        av_log(logger(ctx), AV_LOG_ERROR, "%s malloc enomem\n", ctx->name);
        return AVERROR(ENOMEM);
    }

    for (i = 0; i < req.count; i++) {
        ctx->buffers[i].context = ctx;
        ret = ff_v4l2_buffer_initialize(&ctx->buffers[i], i);
        if (ret < 0) {
            av_log(logger(ctx), AV_LOG_ERROR, "%s buffer[%d] initialization (%s)\n", ctx->name, i, av_err2str(ret));
            goto error;
        }
    }

    av_log(logger(ctx), AV_LOG_DEBUG, "%s: %s %02d buffers initialized: %04ux%04u, sizeimage %08u, bytesperline %08u\n", ctx->name,
        V4L2_TYPE_IS_MULTIPLANAR(ctx->type) ? av_fourcc2str(ctx->format.fmt.pix_mp.pixelformat) : av_fourcc2str(ctx->format.fmt.pix.pixelformat),
        req.count,
        v4l2_get_width(&ctx->format),
        v4l2_get_height(&ctx->format),
        V4L2_TYPE_IS_MULTIPLANAR(ctx->type) ? ctx->format.fmt.pix_mp.plane_fmt[0].sizeimage : ctx->format.fmt.pix.sizeimage,
        V4L2_TYPE_IS_MULTIPLANAR(ctx->type) ? ctx->format.fmt.pix_mp.plane_fmt[0].bytesperline : ctx->format.fmt.pix.bytesperline);

    return 0;

error:
    v4l2_release_buffers(ctx);

    av_freep(&ctx->buffers);

    return ret;
}
