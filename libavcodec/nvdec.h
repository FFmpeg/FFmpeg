/*
 * HW decode acceleration through NVDEC
 *
 * Copyright (c) 2016 Anton Khirnov
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

#ifndef AVCODEC_NVDEC_H
#define AVCODEC_NVDEC_H

#include "compat/cuda/dynlink_loader.h"

#include <stdint.h>

#include "libavutil/buffer.h"
#include "libavutil/frame.h"

#include "avcodec.h"

typedef struct NVDECFrame {
    unsigned int idx;
    AVBufferRef *idx_ref;
    AVBufferRef *decoder_ref;
} NVDECFrame;

typedef struct NVDECContext {
    CUVIDPICPARAMS pic_params;

    AVBufferPool *decoder_pool;

    AVBufferRef  *decoder_ref;

    uint8_t      *bitstream;
    int           bitstream_len;
    unsigned int  bitstream_allocated;

    unsigned     *slice_offsets;
    int           nb_slices;
    unsigned int  slice_offsets_allocated;
} NVDECContext;

int ff_nvdec_decode_init(AVCodecContext *avctx);
int ff_nvdec_decode_uninit(AVCodecContext *avctx);
int ff_nvdec_start_frame(AVCodecContext *avctx, AVFrame *frame);
int ff_nvdec_end_frame(AVCodecContext *avctx);
int ff_nvdec_simple_end_frame(AVCodecContext *avctx);
int ff_nvdec_simple_decode_slice(AVCodecContext *avctx, const uint8_t *buffer,
                                 uint32_t size);
int ff_nvdec_frame_params(AVCodecContext *avctx,
                          AVBufferRef *hw_frames_ctx,
                          int dpb_size);
int ff_nvdec_get_ref_idx(AVFrame *frame);

#endif /* AVCODEC_NVDEC_H */
