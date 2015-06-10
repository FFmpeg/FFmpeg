/*
 * Intel MediaSDK QSV encoder/decoder shared code
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

#ifndef AVCODEC_QSV_INTERNAL_H
#define AVCODEC_QSV_INTERNAL_H

#include <mfx/mfxvideo.h>

#include "libavutil/frame.h"

#define QSV_VERSION_MAJOR 1
#define QSV_VERSION_MINOR 1

#define ASYNC_DEPTH_DEFAULT 4       // internal parallelism

typedef struct QSVFrame {
    AVFrame *frame;
    mfxFrameSurface1 *surface;

    mfxFrameSurface1 surface_internal;

    struct QSVFrame *next;
} QSVFrame;

/**
 * Convert a libmfx error code into a ffmpeg error code.
 */
int ff_qsv_error(int mfx_err);

int ff_qsv_codec_id_to_mfx(enum AVCodecID codec_id);

int ff_qsv_init_internal_session(AVCodecContext *avctx, mfxSession *session);

#endif /* AVCODEC_QSV_INTERNAL_H */
