/*
 * Intel MediaSDK QSV public API
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

#ifndef AVCODEC_QSV_H
#define AVCODEC_QSV_H

#include <mfx/mfxvideo.h>

typedef struct AVQSVContext {
    mfxSession session;
    int iopattern;

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
