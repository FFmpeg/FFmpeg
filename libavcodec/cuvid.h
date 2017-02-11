/*
 * HW decode acceleration through CUVID
 *
 * Copyright (c) 2016 Anton Khirnov
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
 * License along with Libav; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef AVCODEC_CUVID_H
#define AVCODEC_CUVID_H

#include <cuviddec.h>
#include <stdint.h>

#include "libavutil/buffer.h"
#include "libavutil/frame.h"

#include "avcodec.h"

typedef struct CUVIDFrame {
    unsigned int idx;
    AVBufferRef *idx_ref;
    AVBufferRef *decoder_ref;
} CUVIDFrame;

typedef struct CUVIDContext {
    CUVIDPICPARAMS pic_params;

    AVBufferPool *decoder_pool;

    AVBufferRef  *decoder_ref;

    uint8_t      *bitstream;
    int           bitstream_len;
    unsigned int  bitstream_allocated;

    unsigned     *slice_offsets;
    int           nb_slices;
    unsigned int  slice_offsets_allocated;
} CUVIDContext;

int ff_cuvid_decode_init(AVCodecContext *avctx, unsigned int dpb_size);
int ff_cuvid_decode_uninit(AVCodecContext *avctx);
int ff_cuvid_start_frame(AVCodecContext *avctx, AVFrame *frame);
int ff_cuvid_end_frame(AVCodecContext *avctx);

#endif /* AVCODEC_CUVID_H */
