/*
 * Intel MediaSDK QSV public API
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

#ifndef AVCODEC_QSV_H
#define AVCODEC_QSV_H

#include <mfx/mfxvideo.h>

/**
 * This struct is used for communicating QSV parameters between libavcodec and
 * the caller. It is managed by the caller and must be assigned to
 * AVCodecContext.hwaccel_context.
 * - decoding: hwaccel_context must be set on return from the get_format()
 *             callback
 * - encoding: hwaccel_context must be set before avcodec_open2()
 */
typedef struct AVQSVContext {
    /**
     * If non-NULL, the session to use for encoding or decoding.
     * Otherwise, libavcodec will try to create an internal session.
     */
    mfxSession session;

    /**
     * The IO pattern to use.
     */
    int iopattern;

    /**
     * Extra buffers to pass to encoder or decoder initialization.
     */
    mfxExtBuffer **ext_buffers;
    int         nb_ext_buffers;
} AVQSVContext;

/**
 * Allocate a new context.
 *
 * It must be freed by the caller with av_free().
 */
AVQSVContext *av_qsv_alloc_context(void);

#endif /* AVCODEC_QSV_H */
