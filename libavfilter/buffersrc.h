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
 * Memory buffer source API.
 */

#include "libavcodec/avcodec.h"
#include "avfilter.h"

enum {

    /**
     * Do not check for format changes.
     */
    AV_BUFFERSRC_FLAG_NO_CHECK_FORMAT = 1,

#if FF_API_AVFILTERBUFFER
    /**
     * Ignored
     */
    AV_BUFFERSRC_FLAG_NO_COPY = 2,
#endif

    /**
     * Immediately push the frame to the output.
     */
    AV_BUFFERSRC_FLAG_PUSH = 4,

    /**
     * Keep a reference to the frame.
     * If the frame if reference-counted, create a new reference; otherwise
     * copy the frame data.
     */
    AV_BUFFERSRC_FLAG_KEEP_REF = 8,

};

/**
 * Add buffer data in picref to buffer_src.
 *
 * @param buffer_src  pointer to a buffer source context
 * @param picref      a buffer reference, or NULL to mark EOF
 * @param flags       a combination of AV_BUFFERSRC_FLAG_*
 * @return            >= 0 in case of success, a negative AVERROR code
 *                    in case of failure
 */
int av_buffersrc_add_ref(AVFilterContext *buffer_src,
                         AVFilterBufferRef *picref, int flags);

/**
 * Get the number of failed requests.
 *
 * A failed request is when the request_frame method is called while no
 * frame is present in the buffer.
 * The number is reset when a frame is added.
 */
unsigned av_buffersrc_get_nb_failed_requests(AVFilterContext *buffer_src);

#if FF_API_AVFILTERBUFFER
/**
 * Add a buffer to the filtergraph s.
 *
 * @param buf buffer containing frame data to be passed down the filtergraph.
 * This function will take ownership of buf, the user must not free it.
 * A NULL buf signals EOF -- i.e. no more frames will be sent to this filter.
 *
 * @deprecated use av_buffersrc_write_frame() or av_buffersrc_add_frame()
 */
attribute_deprecated
int av_buffersrc_buffer(AVFilterContext *s, AVFilterBufferRef *buf);
#endif

/**
 * Add a frame to the buffer source.
 *
 * @param s an instance of the buffersrc filter.
 * @param frame frame to be added. If the frame is reference counted, this
 * function will make a new reference to it. Otherwise the frame data will be
 * copied.
 *
 * @return 0 on success, a negative AVERROR on error
 *
 * This function is equivalent to av_buffersrc_add_frame_flags() with the
 * AV_BUFFERSRC_FLAG_KEEP_REF flag.
 */
int av_buffersrc_write_frame(AVFilterContext *s, const AVFrame *frame);

/**
 * Add a frame to the buffer source.
 *
 * @param s an instance of the buffersrc filter.
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
 *
 * This function is equivalent to av_buffersrc_add_frame_flags() without the
 * AV_BUFFERSRC_FLAG_KEEP_REF flag.
 */
int av_buffersrc_add_frame(AVFilterContext *ctx, AVFrame *frame);

/**
 * Add a frame to the buffer source.
 *
 * By default, if the frame is reference-counted, this function will take
 * ownership of the reference(s) and reset the frame. This can be controled
 * using the flags.
 *
 * If this function returns an error, the input frame is not touched.
 *
 * @param buffer_src  pointer to a buffer source context
 * @param frame       a frame, or NULL to mark EOF
 * @param flags       a combination of AV_BUFFERSRC_FLAG_*
 * @return            >= 0 in case of success, a negative AVERROR code
 *                    in case of failure
 */
int av_buffersrc_add_frame_flags(AVFilterContext *buffer_src,
                                 AVFrame *frame, int flags);


#endif /* AVFILTER_BUFFERSRC_H */
