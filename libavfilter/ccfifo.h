/*
 * CEA-708 Closed Captioning FIFO
 * Copyright (c) 2023 LTN Global Communications
 *
 * Author: Devin Heitmueller <dheitmueller@ltnglobal.com>
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
 * CC FIFO Buffer
 */

#ifndef AVFILTER_CCFIFO_H
#define AVFILTER_CCFIFO_H

#include "libavutil/avutil.h"
#include "libavutil/frame.h"
#include "libavutil/fifo.h"

typedef struct AVCCFifo AVCCFifo;

/**
 * Allocate an AVCCFifo.
 *
 * @param framerate   output framerate
 * @param log_ctx     used for any av_log() calls
 * @return            newly allocated AVCCFifo, or NULL on error
 */
AVCCFifo *ff_ccfifo_alloc(AVRational framerate, void *log_ctx);

/**
 * Free an AVCCFifo
 *
 * @param ccf Pointer to the pointer to the AVCCFifo which should be freed
 * @note `*ptr = NULL` is safe and leads to no action.
 */
void ff_ccfifo_freep(AVCCFifo **ccf);


/**
 * Extract CC data from an AVFrame
 *
 * Extract CC bytes from the AVFrame, insert them into our queue, and
 * remove the side data from the AVFrame.  The side data is removed
 * as it will be re-inserted at the appropriate rate later in the
 * filter.
 *
 * @param af          AVCCFifo to write to
 * @param frame       AVFrame with the video frame to operate on
 * @return            Zero on success, or negative AVERROR
 *                    code on failure.
 */
int ff_ccfifo_extract(AVCCFifo *ccf, AVFrame *frame);

/**
 *Just like ff_ccfifo_extract(), but takes the raw bytes instead of an AVFrame
 */
int ff_ccfifo_extractbytes(AVCCFifo *ccf, uint8_t *data, size_t len);

/**
 * Provide the size in bytes of an output buffer to allocate
 *
 * Ask for how many bytes the output will contain, so the caller can allocate
 * an appropriately sized buffer and pass it to ff_ccfifo_injectbytes()
 *
 */
int ff_ccfifo_getoutputsize(AVCCFifo *ccf);

/**
 * Insert CC data from the FIFO into an AVFrame (as side data)
 *
 * Dequeue the appropriate number of CC tuples based on the
 * frame rate, and insert them into the AVFrame
 *
 * @param af          AVCCFifo to read from
 * @param frame       AVFrame with the video frame to operate on
 * @return            Zero on success, or negative AVERROR
 *                    code on failure.
 */
int ff_ccfifo_inject(AVCCFifo *ccf, AVFrame *frame);

/**
 * Just like ff_ccfifo_inject(), but takes the raw bytes to insert the CC data
 * int rather than an AVFrame
 */
int ff_ccfifo_injectbytes(AVCCFifo *ccf, uint8_t *data, size_t len);

/**
 * Returns 1 if captions have been found as a prior call
 * to ff_ccfifo_extract() or ff_ccfifo_extractbytes()
 */
int ff_ccfifo_ccdetected(AVCCFifo *ccf);

#endif /* AVFILTER_CCFIFO_H */
