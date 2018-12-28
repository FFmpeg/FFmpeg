/*
 * V4L2 mem2mem helper functions
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

#ifndef AVCODEC_V4L2_M2M_H
#define AVCODEC_V4L2_M2M_H

#include <semaphore.h>
#include <unistd.h>
#include <dirent.h>
#include <linux/videodev2.h>

#include "libavcodec/avcodec.h"
#include "v4l2_context.h"

#define container_of(ptr, type, member) ({ \
        const __typeof__(((type *)0)->member ) *__mptr = (ptr); \
        (type *)((char *)__mptr - offsetof(type,member) );})

#define V4L_M2M_DEFAULT_OPTS \
    { "num_output_buffers", "Number of buffers in the output context",\
        OFFSET(output.num_buffers), AV_OPT_TYPE_INT, { .i64 = 16 }, 6, INT_MAX, FLAGS }

typedef struct V4L2m2mContext
{
    AVClass *class;
    char devname[PATH_MAX];
    int fd;

    /* the codec context queues */
    V4L2Context capture;
    V4L2Context output;

    /* refcount of buffers held by the user */
    atomic_uint refcount;

    /* dynamic stream reconfig */
    AVCodecContext *avctx;
    sem_t refsync;
    int reinit;

    /* null frame/packet received */
    int draining;
} V4L2m2mContext;

/**
 * Probes the video nodes looking for the required codec capabilities.
 *
 * @param[in] ctx The AVCodecContext instantiated by the encoder/decoder.
 *
 * @returns 0 if a driver is found, a negative number otherwise.
 */
int ff_v4l2_m2m_codec_init(AVCodecContext *avctx);

/**
 * Releases all the codec resources if all AVBufferRefs have been returned to the
 * ctx. Otherwise keep the driver open.
 *
 * @param[in] The AVCodecContext instantiated by the encoder/decoder.
 *
 * @returns 0
 *
 */
int ff_v4l2_m2m_codec_end(AVCodecContext *avctx);

/**
 * Reinitializes the V4L2m2mContext when the driver cannot continue processing
 * with the capture parameters.
 *
 * @param[in] ctx The V4L2m2mContext instantiated by the encoder/decoder.
 *
 * @returns 0 in case of success, negative number otherwise
 */
int ff_v4l2_m2m_codec_reinit(V4L2m2mContext *ctx);

/**
 * Reinitializes the V4L2m2mContext when the driver cannot continue processing
 * with the  any of the current V4L2Contexts (ie, changes in output and capture).
 *
 * @param[in] ctx The V4L2m2mContext instantiated by the encoder/decoder.
 *
 * @returns 0 in case of success, negative number otherwise
 */
int ff_v4l2_m2m_codec_full_reinit(V4L2m2mContext *ctx);

#endif /* AVCODEC_V4L2_M2M_H */
