/*
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
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef AVFILTER_BUFFERSRC_H
#define AVFILTER_BUFFERSRC_H

/**
 * @file
 * @ingroup lavfi_buffersrc
 * Memory buffer source API.
 */

#include "avfilter.h"

/**
 * @defgroup lavfi_buffersrc Buffer source API
 * @ingroup lavfi
 * @{
 */

#if FF_API_AVFILTERBUFFER
/**
 * Add a buffer to a filtergraph.
 *
 * @param ctx an instance of the buffersrc filter
 * @param buf buffer containing frame data to be passed down the filtergraph.
 * This function will take ownership of buf, the user must not free it.
 * A NULL buf signals EOF -- i.e. no more frames will be sent to this filter.
 *
 * @deprecated use av_buffersrc_write_frame() or av_buffersrc_add_frame()
 */
attribute_deprecated
int av_buffersrc_buffer(AVFilterContext *ctx, AVFilterBufferRef *buf);
#endif

/**
 * Add a frame to the buffer source.
 *
 * @param ctx   an instance of the buffersrc filter
 * @param frame frame to be added. If the frame is reference counted, this
 * function will make a new reference to it. Otherwise the frame data will be
 * copied.
 *
 * @return 0 on success, a negative AVERROR on error
 */
int av_buffersrc_write_frame(AVFilterContext *ctx, const AVFrame *frame);

/**
 * Add a frame to the buffer source.
 *
 * @param ctx   an instance of the buffersrc filter
 * @param frame frame to be added. If the frame is reference counted, this
 * function will take ownership of the reference(s) and reset the frame.
 * Otherwise the frame data will be copied. If this function returns an error,
 * the input frame is not touched.
 *
 * @return 0 on success, a negative AVERROR on error.
 *
 * @note the difference between this function and av_buffersrc_write_frame() is
 * that av_buffersrc_write_frame() creates a new reference to the input frame,
 * while this function takes ownership of the reference passed to it.
 */
int av_buffersrc_add_frame(AVFilterContext *ctx, AVFrame *frame);

/**
 * @}
 */

#endif /* AVFILTER_BUFFERSRC_H */
