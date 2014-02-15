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
 * @ingroup lavfi_buffersink
 * memory buffer sink API for audio and video
 */

#include "avfilter.h"

/**
 * @defgroup lavfi_buffersink Buffer sink API
 * @ingroup lavfi
 * @{
 */

#if FF_API_AVFILTERBUFFER
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
attribute_deprecated
int av_buffersink_get_buffer_ref(AVFilterContext *buffer_sink,
                                 AVFilterBufferRef **bufref, int flags);

/**
 * Get the number of immediately available frames.
 */
attribute_deprecated
int av_buffersink_poll_frame(AVFilterContext *ctx);

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
attribute_deprecated
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
attribute_deprecated
int av_buffersink_read_samples(AVFilterContext *ctx, AVFilterBufferRef **buf,
                               int nb_samples);
#endif

/**
 * Get a frame with filtered data from sink and put it in frame.
 *
 * @param ctx    pointer to a buffersink or abuffersink filter context.
 * @param frame  pointer to an allocated frame that will be filled with data.
 *               The data must be freed using av_frame_unref() / av_frame_free()
 * @param flags  a combination of AV_BUFFERSINK_FLAG_* flags
 *
 * @return  >= 0 in for success, a negative AVERROR code for failure.
 */
int av_buffersink_get_frame_flags(AVFilterContext *ctx, AVFrame *frame, int flags);

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
    const int *channel_counts;              ///< list of allowed channel counts, terminated by -1
    int all_channel_counts;                 ///< if not 0, accept any channel count or layout
    int *sample_rates;                      ///< list of allowed sample rates, terminated by -1
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
 * Get the frame rate of the input.
 */
AVRational av_buffersink_get_frame_rate(AVFilterContext *ctx);

/**
 * Get a frame with filtered data from sink and put it in frame.
 *
 * @param ctx pointer to a context of a buffersink or abuffersink AVFilter.
 * @param frame pointer to an allocated frame that will be filled with data.
 *              The data must be freed using av_frame_unref() / av_frame_free()
 *
 * @return
 *         - >= 0 if a frame was successfully returned.
 *         - AVERROR(EAGAIN) if no frames are available at this point; more
 *           input frames must be added to the filtergraph to get more output.
 *         - AVERROR_EOF if there will be no more output frames on this sink.
 *         - A different negative AVERROR code in other failure cases.
 */
int av_buffersink_get_frame(AVFilterContext *ctx, AVFrame *frame);

/**
 * Same as av_buffersink_get_frame(), but with the ability to specify the number
 * of samples read. This function is less efficient than
 * av_buffersink_get_frame(), because it copies the data around.
 *
 * @param ctx pointer to a context of the abuffersink AVFilter.
 * @param frame pointer to an allocated frame that will be filled with data.
 *              The data must be freed using av_frame_unref() / av_frame_free()
 *              frame will contain exactly nb_samples audio samples, except at
 *              the end of stream, when it can contain less than nb_samples.
 *
 * @return The return codes have the same meaning as for
 *         av_buffersink_get_samples().
 *
 * @warning do not mix this function with av_buffersink_get_frame(). Use only one or
 * the other with a single sink, not both.
 */
int av_buffersink_get_samples(AVFilterContext *ctx, AVFrame *frame, int nb_samples);

/**
 * @}
 */

#endif /* AVFILTER_BUFFERSINK_H */
