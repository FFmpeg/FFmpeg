/*
 * Copyright (c) 2008 Alexander Strange <astrange@ithinksw.com>
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
 * Multithreading support functions
 * @author Alexander Strange <astrange@ithinksw.com>
 */

#ifndef AVCODEC_THREAD_H
#define AVCODEC_THREAD_H

#include "libavutil/buffer.h"

#include "config.h"
#include "avcodec.h"

typedef struct ThreadFrame {
    AVFrame *f;
    AVCodecContext *owner[2];
    // progress->data is an array of 2 ints holding progress for top/bottom
    // fields
    AVBufferRef *progress;
} ThreadFrame;

/**
 * Wait for decoding threads to finish and reset internal state.
 * Called by avcodec_flush_buffers().
 *
 * @param avctx The context.
 */
void ff_thread_flush(AVCodecContext *avctx);

/**
 * Submit a new frame to a decoding thread.
 * Returns the next available frame in picture. *got_picture_ptr
 * will be 0 if none is available.
 * The return value on success is the size of the consumed packet for
 * compatibility with avcodec_decode_video2(). This means the decoder
 * has to consume the full packet.
 *
 * Parameters are the same as avcodec_decode_video2().
 */
int ff_thread_decode_frame(AVCodecContext *avctx, AVFrame *picture,
                           int *got_picture_ptr, AVPacket *avpkt);

/**
 * If the codec defines update_thread_context(), call this
 * when they are ready for the next thread to start decoding
 * the next frame. After calling it, do not change any variables
 * read by the update_thread_context() method, or call ff_thread_get_buffer().
 *
 * @param avctx The context.
 */
void ff_thread_finish_setup(AVCodecContext *avctx);

/**
 * Notify later decoding threads when part of their reference picture is ready.
 * Call this when some part of the picture is finished decoding.
 * Later calls with lower values of progress have no effect.
 *
 * @param f The picture being decoded.
 * @param progress Value, in arbitrary units, of how much of the picture has decoded.
 * @param field The field being decoded, for field-picture codecs.
 * 0 for top field or frame pictures, 1 for bottom field.
 */
void ff_thread_report_progress(ThreadFrame *f, int progress, int field);

/**
 * Wait for earlier decoding threads to finish reference pictures.
 * Call this before accessing some part of a picture, with a given
 * value for progress, and it will return after the responsible decoding
 * thread calls ff_thread_report_progress() with the same or
 * higher value for progress.
 *
 * @param f The picture being referenced.
 * @param progress Value, in arbitrary units, to wait for.
 * @param field The field being referenced, for field-picture codecs.
 * 0 for top field or frame pictures, 1 for bottom field.
 */
void ff_thread_await_progress(ThreadFrame *f, int progress, int field);

/**
 * Wrapper around get_format() for frame-multithreaded codecs.
 * Call this function instead of avctx->get_format().
 * Cannot be called after the codec has called ff_thread_finish_setup().
 *
 * @param avctx The current context.
 * @param fmt The list of available formats.
 */
enum AVPixelFormat ff_thread_get_format(AVCodecContext *avctx, const enum AVPixelFormat *fmt);

/**
 * Wrapper around get_buffer() for frame-multithreaded codecs.
 * Call this function instead of ff_get_buffer(f).
 * Cannot be called after the codec has called ff_thread_finish_setup().
 *
 * @param avctx The current context.
 * @param f The frame to write into.
 */
int ff_thread_get_buffer(AVCodecContext *avctx, ThreadFrame *f, int flags);

/**
 * Wrapper around release_buffer() frame-for multithreaded codecs.
 * Call this function instead of avctx->release_buffer(f).
 * The AVFrame will be copied and the actual release_buffer() call
 * will be performed later. The contents of data pointed to by the
 * AVFrame should not be changed until ff_thread_get_buffer() is called
 * on it.
 *
 * @param avctx The current context.
 * @param f The picture being released.
 */
void ff_thread_release_buffer(AVCodecContext *avctx, ThreadFrame *f);

int ff_thread_ref_frame(ThreadFrame *dst, ThreadFrame *src);

int ff_thread_init(AVCodecContext *s);
void ff_thread_free(AVCodecContext *s);

int ff_alloc_entries(AVCodecContext *avctx, int count);
void ff_reset_entries(AVCodecContext *avctx);
void ff_thread_report_progress2(AVCodecContext *avctx, int field, int thread, int n);
void ff_thread_await_progress2(AVCodecContext *avctx,  int field, int thread, int shift);

#endif /* AVCODEC_THREAD_H */
