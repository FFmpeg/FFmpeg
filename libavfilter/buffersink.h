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
    const enum AVPixelFormat *pixel_fmts; ///< list of allowed pixel formats, terminated by AV_PIX_FMT_NONE
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
} AVABufferSinkParams;

/**
 * Create an AVABufferSinkParams structure.
 *
 * Must be freed with av_free().
 */
AVABufferSinkParams *av_abuffersink_params_alloc(void);

/**
 * Set the frame size for an audio buffer sink.
 *
 * All calls to av_buffersink_get_buffer_ref will return a buffer with
 * exactly the specified number of samples, or AVERROR(EAGAIN) if there is
 * not enough. The last buffer at EOF will be padded with 0.
 */
void av_buffersink_set_frame_size(AVFilterContext *ctx, unsigned frame_size);

/**
 * Tell av_buffersink_get_buffer_ref() to read video/samples buffer
 * reference, but not remove it from the buffer. This is useful if you
 * need only to read a video/samples buffer, without to fetch it.
 */
#define AV_BUFFERSINK_FLAG_PEEK 1

/**
 * Tell av_buffersink_get_buffer_ref() not to request a frame from its input.
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

/**
 * Get the frame rate of the input.
 */
AVRational av_buffersink_get_frame_rate(AVFilterContext *ctx);

/**
 * @defgroup libav_api Libav API
 * @{
 */

/**
 * Get a buffer with filtered data from sink and put it in buf.
 *
 * @param ctx pointer to a context of a buffersink or abuffersink AVFilter.
 * @param buf pointer to the buffer will be written here if buf is non-NULL. buf
 *            must be freed by the caller using avfilter_unref_buffer().
 *            Buf may also be NULL to query whether a buffer is ready to be
 *            output.
 *
 * @return >= 0 in case of success, a negative AVERROR code in case of
 *         failure.
 */
int av_buffersink_read(AVFilterContext *ctx, AVFilterBufferRef **buf);

/**
 * Same as av_buffersink_read, but with the ability to specify the number of
 * samples read. This function is less efficient than av_buffersink_read(),
 * because it copies the data around.
 *
 * @param ctx pointer to a context of the abuffersink AVFilter.
 * @param buf pointer to the buffer will be written here if buf is non-NULL. buf
 *            must be freed by the caller using avfilter_unref_buffer(). buf
 *            will contain exactly nb_samples audio samples, except at the end
 *            of stream, when it can contain less than nb_samples.
 *            Buf may also be NULL to query whether a buffer is ready to be
 *            output.
 *
 * @warning do not mix this function with av_buffersink_read(). Use only one or
 * the other with a single sink, not both.
 */
int av_buffersink_read_samples(AVFilterContext *ctx, AVFilterBufferRef **buf,
                               int nb_samples);

/**
 * @}
 */

#endif /* AVFILTER_BUFFERSINK_H */
