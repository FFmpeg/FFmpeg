/*
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

#ifndef AVCODEC_PTHREAD_INTERNAL_H
#define AVCODEC_PTHREAD_INTERNAL_H

#include "avcodec.h"

/* H264 slice threading seems to be buggy with more than 16 threads,
 * limit the number of threads to 16 for automatic detection */
#define MAX_AUTO_THREADS 16

int ff_slice_thread_init(AVCodecContext *avctx);
void ff_slice_thread_free(AVCodecContext *avctx);

int ff_frame_thread_init(AVCodecContext *avctx);
void ff_frame_thread_free(AVCodecContext *avctx, int thread_count);

#endif // AVCODEC_PTHREAD_INTERNAL_H
