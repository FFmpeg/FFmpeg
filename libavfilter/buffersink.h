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

#ifndef AVFILTER_BUFFERSINK_H
#define AVFILTER_BUFFERSINK_H

/**
 * @file
 * memory buffer sink API for audio and video
 */

#include "avfilter.h"

/**
 * Struct to use for initializing a buffersink context.
 */
typedef struct {
    const enum PixelFormat *pixel_fmts; ///< list of allowed pixel formats, terminated by PIX_FMT_NONE
} AVBufferSinkParams;

/**
 * Create an AVBufferSinkParams structure.
 *
 * Must be freed with av_free().
 */
AVBufferSinkParams *av_buffersink_params_alloc(void);

/**
 * Struct to use for initializing an abuffersink context.
 */
typedef struct {
    const enum AVSampleFormat *sample_fmts; ///< list of allowed sample formats, terminated by AV_SAMPLE_FMT_NONE
    const int64_t *channel_layouts;         ///< list of allowed channel layouts, terminated by -1
    const int *packing_fmts;                ///< list of allowed packing formats
} AVABufferSinkParams;

/**
 * Create an AVABufferSinkParams structure.
 *
 * Must be freed with av_free().
 */
AVABufferSinkParams *av_abuffersink_params_alloc(void);

/**
 * Tell av_buffersink_get_buffer_ref() to read video/samples buffer
 * reference, but not remove it from the buffer. This is useful if you
 * need only to read a video/samples buffer, without to fetch it.
 */
#define AV_BUFFERSINK_FLAG_PEEK 1

/**
 * Tell av_buffersink_get_buffer_ref() not to request a frame fom its input.
 * If a frame is already buffered, it is read (and removed from the buffer),
 * but if no frame is present, return AVERROR(EAGAIN).
 */
#define AV_BUFFERSINK_FLAG_NO_REQUEST 2

/**
 * Get an audio/video buffer data from buffer_sink and put it in bufref.
 *
 * This function works with both audio and video buffer sinks.
 *
 * @param buffer_sink pointer to a buffersink or abuffersink context
 * @param flags a combination of AV_BUFFERSINK_FLAG_* flags
 * @return >= 0 in case of success, a negative AVERROR code in case of
 * failure
 */
int av_buffersink_get_buffer_ref(AVFilterContext *buffer_sink,
                                 AVFilterBufferRef **bufref, int flags);


/**
 * Get the number of immediately available frames.
 */
int av_buffersink_poll_frame(AVFilterContext *ctx);

#if FF_API_OLD_VSINK_API
/**
 * @deprecated Use av_buffersink_get_buffer_ref() instead.
 */
attribute_deprecated
int av_vsink_buffer_get_video_buffer_ref(AVFilterContext *buffer_sink,
                                         AVFilterBufferRef **picref, int flags);
#endif

/**
 * Get a buffer with filtered data from sink and put it in buf.
 *
 * @param sink pointer to a context of a buffersink AVFilter.
 * @param buf pointer to the buffer will be written here if buf is non-NULL. buf
 *            must be freed by the caller using avfilter_unref_buffer().
 *            Buf may also be NULL to query whether a buffer is ready to be
 *            output.
 *
 * @return >= 0 in case of success, a negative AVERROR code in case of
 *         failure.
 */
int av_buffersink_read(AVFilterContext *sink, AVFilterBufferRef **buf);

#endif /* AVFILTER_BUFFERSINK_H */
