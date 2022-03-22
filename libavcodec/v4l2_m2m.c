/*
 * V4L mem2mem
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
#include <dirent.h>
#include <fcntl.h>
#include "libavcodec/avcodec.h"
#include "libavutil/pixdesc.h"
#include "libavutil/imgutils.h"
#include "libavutil/pixfmt.h"
#include "v4l2_context.h"
#include "v4l2_fmt.h"
#include "v4l2_m2m.h"

static inline int v4l2_splane_video(struct v4l2_capability *cap)
{
    if (cap->capabilities & (V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_VIDEO_OUTPUT) &&
        cap->capabilities & V4L2_CAP_STREAMING)
        return 1;

    if (cap->capabilities & V4L2_CAP_VIDEO_M2M)
        return 1;

    return 0;
}

static inline int v4l2_mplane_video(struct v4l2_capability *cap)
{
    if (cap->capabilities & (V4L2_CAP_VIDEO_CAPTURE_MPLANE | V4L2_CAP_VIDEO_OUTPUT_MPLANE) &&
        cap->capabilities & V4L2_CAP_STREAMING)
        return 1;

    if (cap->capabilities & V4L2_CAP_VIDEO_M2M_MPLANE)
        return 1;

    return 0;
}

static int v4l2_prepare_contexts(V4L2m2mContext *s, int probe)
{
    struct v4l2_capability cap;
    void *log_ctx = s->avctx;
    int ret;

    s->capture.done = s->output.done = 0;
    s->capture.name = "capture";
    s->output.name = "output";
    atomic_init(&s->refcount, 0);
    sem_init(&s->refsync, 0, 0);

    memset(&cap, 0, sizeof(cap));
    ret = ioctl(s->fd, VIDIOC_QUERYCAP, &cap);
    if (ret < 0)
        return ret;

    av_log(log_ctx, probe ? AV_LOG_DEBUG : AV_LOG_INFO,
                     "driver '%s' on card '%s' in %s mode\n", cap.driver, cap.card,
                     v4l2_mplane_video(&cap) ? "mplane" :
                     v4l2_splane_video(&cap) ? "splane" : "unknown");

    if (v4l2_mplane_video(&cap)) {
        s->capture.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        s->output.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
        return 0;
    }

    if (v4l2_splane_video(&cap)) {
        s->capture.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        s->output.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        return 0;
    }

    return AVERROR(EINVAL);
}

static int v4l2_probe_driver(V4L2m2mContext *s)
{
    void *log_ctx = s->avctx;
    int ret;

    s->fd = open(s->devname, O_RDWR | O_NONBLOCK, 0);
    if (s->fd < 0)
        return AVERROR(errno);

    ret = v4l2_prepare_contexts(s, 1);
    if (ret < 0)
        goto done;

    ret = ff_v4l2_context_get_format(&s->output, 1);
    if (ret) {
        av_log(log_ctx, AV_LOG_DEBUG, "v4l2 output format not supported\n");
        goto done;
    }

    ret = ff_v4l2_context_get_format(&s->capture, 1);
    if (ret) {
        av_log(log_ctx, AV_LOG_DEBUG, "v4l2 capture format not supported\n");
        goto done;
    }

done:
    if (close(s->fd) < 0) {
        ret = AVERROR(errno);
        av_log(log_ctx, AV_LOG_ERROR, "failure closing %s (%s)\n", s->devname, av_err2str(AVERROR(errno)));
    }

    s->fd = -1;

    return ret;
}

static int v4l2_configure_contexts(V4L2m2mContext *s)
{
    void *log_ctx = s->avctx;
    int ret;
    struct v4l2_format ofmt, cfmt;

    s->fd = open(s->devname, O_RDWR | O_NONBLOCK, 0);
    if (s->fd < 0)
        return AVERROR(errno);

    ret = v4l2_prepare_contexts(s, 0);
    if (ret < 0)
        goto error;

    ofmt = s->output.format;
    cfmt = s->capture.format;
    av_log(log_ctx, AV_LOG_INFO, "requesting formats: output=%s capture=%s\n",
                                 av_fourcc2str(V4L2_TYPE_IS_MULTIPLANAR(ofmt.type) ?
                                               ofmt.fmt.pix_mp.pixelformat :
                                               ofmt.fmt.pix.pixelformat),
                                 av_fourcc2str(V4L2_TYPE_IS_MULTIPLANAR(cfmt.type) ?
                                               cfmt.fmt.pix_mp.pixelformat :
                                               cfmt.fmt.pix.pixelformat));

    ret = ff_v4l2_context_set_format(&s->output);
    if (ret) {
        av_log(log_ctx, AV_LOG_ERROR, "can't set v4l2 output format\n");
        goto error;
    }

    ret = ff_v4l2_context_set_format(&s->capture);
    if (ret) {
        av_log(log_ctx, AV_LOG_ERROR, "can't to set v4l2 capture format\n");
        goto error;
    }

    ret = ff_v4l2_context_init(&s->output);
    if (ret) {
        av_log(log_ctx, AV_LOG_ERROR, "no v4l2 output context's buffers\n");
        goto error;
    }

    /* decoder's buffers need to be updated at a later stage */
    if (s->avctx && !av_codec_is_decoder(s->avctx->codec)) {
        ret = ff_v4l2_context_init(&s->capture);
        if (ret) {
            av_log(log_ctx, AV_LOG_ERROR, "no v4l2 capture context's buffers\n");
            goto error;
        }
    }

    return 0;

error:
    if (close(s->fd) < 0) {
        ret = AVERROR(errno);
        av_log(log_ctx, AV_LOG_ERROR, "error closing %s (%s)\n",
            s->devname, av_err2str(AVERROR(errno)));
    }
    s->fd = -1;

    return ret;
}

/******************************************************************************
 *
 *                  V4L2 M2M Interface
 *
 ******************************************************************************/
int ff_v4l2_m2m_codec_reinit(V4L2m2mContext *s)
{
    void *log_ctx = s->avctx;
    int ret;

    av_log(log_ctx, AV_LOG_DEBUG, "reinit context\n");

    /* 1. streamoff */
    ret = ff_v4l2_context_set_status(&s->capture, VIDIOC_STREAMOFF);
    if (ret)
        av_log(log_ctx, AV_LOG_ERROR, "capture VIDIOC_STREAMOFF\n");

    /* 2. unmap the capture buffers (v4l2 and ffmpeg):
     *    we must wait for all references to be released before being allowed
     *    to queue new buffers.
     */
    av_log(log_ctx, AV_LOG_DEBUG, "waiting for user to release AVBufferRefs\n");
    if (atomic_load(&s->refcount))
        while(sem_wait(&s->refsync) == -1 && errno == EINTR);

    ff_v4l2_context_release(&s->capture);

    /* 3. get the new capture format */
    ret = ff_v4l2_context_get_format(&s->capture, 0);
    if (ret) {
        av_log(log_ctx, AV_LOG_ERROR, "query the new capture format\n");
        return ret;
    }

    /* 4. set the capture format */
    ret = ff_v4l2_context_set_format(&s->capture);
    if (ret) {
        av_log(log_ctx, AV_LOG_ERROR, "setting capture format\n");
        return ret;
    }

    /* 5. complete reinit */
    s->draining = 0;
    s->reinit = 0;

    return 0;
}

int ff_v4l2_m2m_codec_full_reinit(V4L2m2mContext *s)
{
    void *log_ctx = s->avctx;
    int ret;

    av_log(log_ctx, AV_LOG_DEBUG, "%s full reinit\n", s->devname);

    /* wait for pending buffer references */
    if (atomic_load(&s->refcount))
        while(sem_wait(&s->refsync) == -1 && errno == EINTR);

    ret = ff_v4l2_context_set_status(&s->output, VIDIOC_STREAMOFF);
    if (ret) {
        av_log(log_ctx, AV_LOG_ERROR, "output VIDIOC_STREAMOFF\n");
        goto error;
    }

    ret = ff_v4l2_context_set_status(&s->capture, VIDIOC_STREAMOFF);
    if (ret) {
        av_log(log_ctx, AV_LOG_ERROR, "capture VIDIOC_STREAMOFF\n");
        goto error;
    }

    /* release and unmmap the buffers */
    ff_v4l2_context_release(&s->output);
    ff_v4l2_context_release(&s->capture);

    /* start again now that we know the stream dimensions */
    s->draining = 0;
    s->reinit = 0;

    ret = ff_v4l2_context_get_format(&s->output, 0);
    if (ret) {
        av_log(log_ctx, AV_LOG_DEBUG, "v4l2 output format not supported\n");
        goto error;
    }

    ret = ff_v4l2_context_get_format(&s->capture, 0);
    if (ret) {
        av_log(log_ctx, AV_LOG_DEBUG, "v4l2 capture format not supported\n");
        goto error;
    }

    ret = ff_v4l2_context_set_format(&s->output);
    if (ret) {
        av_log(log_ctx, AV_LOG_ERROR, "can't set v4l2 output format\n");
        goto error;
    }

    ret = ff_v4l2_context_set_format(&s->capture);
    if (ret) {
        av_log(log_ctx, AV_LOG_ERROR, "can't to set v4l2 capture format\n");
        goto error;
    }

    ret = ff_v4l2_context_init(&s->output);
    if (ret) {
        av_log(log_ctx, AV_LOG_ERROR, "no v4l2 output context's buffers\n");
        goto error;
    }

    /* decoder's buffers need to be updated at a later stage */
    if (s->avctx && !av_codec_is_decoder(s->avctx->codec)) {
        ret = ff_v4l2_context_init(&s->capture);
        if (ret) {
            av_log(log_ctx, AV_LOG_ERROR, "no v4l2 capture context's buffers\n");
            goto error;
        }
    }

    return 0;

error:
    return ret;
}

static void v4l2_m2m_destroy_context(void *opaque, uint8_t *context)
{
    V4L2m2mContext *s = (V4L2m2mContext*)context;

    ff_v4l2_context_release(&s->capture);
    sem_destroy(&s->refsync);

    close(s->fd);
    av_frame_unref(s->frame);
    av_frame_free(&s->frame);
    av_packet_unref(&s->buf_pkt);

    av_free(s);
}

int ff_v4l2_m2m_codec_end(V4L2m2mPriv *priv)
{
    V4L2m2mContext *s = priv->context;
    int ret;

    if (!s)
        return 0;

    if (s->fd >= 0) {
        ret = ff_v4l2_context_set_status(&s->output, VIDIOC_STREAMOFF);
        if (ret)
            av_log(s->avctx, AV_LOG_ERROR, "VIDIOC_STREAMOFF %s\n", s->output.name);

        ret = ff_v4l2_context_set_status(&s->capture, VIDIOC_STREAMOFF);
        if (ret)
            av_log(s->avctx, AV_LOG_ERROR, "VIDIOC_STREAMOFF %s\n", s->capture.name);
    }

    ff_v4l2_context_release(&s->output);

    s->self_ref = NULL;
    av_buffer_unref(&priv->context_ref);

    return 0;
}

int ff_v4l2_m2m_codec_init(V4L2m2mPriv *priv)
{
    int ret = AVERROR(EINVAL);
    struct dirent *entry;
    DIR *dirp;

    V4L2m2mContext *s = priv->context;

    dirp = opendir("/dev");
    if (!dirp)
        return AVERROR(errno);

    for (entry = readdir(dirp); entry; entry = readdir(dirp)) {

        if (strncmp(entry->d_name, "video", 5))
            continue;

        snprintf(s->devname, sizeof(s->devname), "/dev/%s", entry->d_name);
        av_log(s->avctx, AV_LOG_DEBUG, "probing device %s\n", s->devname);
        ret = v4l2_probe_driver(s);
        if (!ret)
            break;
    }

    closedir(dirp);

    if (ret) {
        av_log(s->avctx, AV_LOG_ERROR, "Could not find a valid device\n");
        memset(s->devname, 0, sizeof(s->devname));

        return ret;
    }

    av_log(s->avctx, AV_LOG_INFO, "Using device %s\n", s->devname);

    return v4l2_configure_contexts(s);
}

int ff_v4l2_m2m_create_context(V4L2m2mPriv *priv, V4L2m2mContext **s)
{
    *s = av_mallocz(sizeof(V4L2m2mContext));
    if (!*s)
        return AVERROR(ENOMEM);

    priv->context_ref = av_buffer_create((uint8_t *) *s, sizeof(V4L2m2mContext),
                                         &v4l2_m2m_destroy_context, NULL, 0);
    if (!priv->context_ref) {
        av_freep(s);
        return AVERROR(ENOMEM);
    }

    /* assign the context */
    priv->context = *s;
    (*s)->priv = priv;

    /* populate it */
    priv->context->capture.num_buffers = priv->num_capture_buffers;
    priv->context->output.num_buffers  = priv->num_output_buffers;
    priv->context->self_ref = priv->context_ref;
    priv->context->fd = -1;

    priv->context->frame = av_frame_alloc();
    if (!priv->context->frame) {
        av_buffer_unref(&priv->context_ref);
        *s = NULL; /* freed when unreferencing context_ref */
        return AVERROR(ENOMEM);
    }

    return 0;
}
