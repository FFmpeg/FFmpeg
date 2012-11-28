/*
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

#ifndef AVFILTER_BUFFERSINK_H
#define AVFILTER_BUFFERSINK_H

/**
 * @file
 * memory buffer sink API
 */

#include "avfilter.h"

#if FF_API_AVFILTERBUFFER
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
 * @param ctx pointer to a context of a buffersink or abuffersink AVFilter.
 * @param frame pointer to an allocated frame that will be filled with data.
 *              The data must be freed using av_frame_unref() / av_frame_free()
 *
 * @return >= 0 in case of success, a negative AVERROR code in case of
 *         failure.
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
 * @warning do not mix this function with av_buffersink_get_frame(). Use only one or
 * the other with a single sink, not both.
 */
int av_buffersink_get_samples(AVFilterContext *ctx, AVFrame *frame, int nb_samples);

#endif /* AVFILTER_BUFFERSINK_H */
