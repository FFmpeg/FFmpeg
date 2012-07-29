/*
 * Copyright (c) 2007 Bobby Bingham
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


#ifndef AVFILTER_VIDEO_H
#define AVFILTER_VIDEO_H

#include "avfilter.h"

AVFilterBufferRef *ff_default_get_video_buffer(AVFilterLink *link,
                                               int perms, int w, int h);
AVFilterBufferRef *ff_null_get_video_buffer(AVFilterLink *link, int perms, int w, int h);

/**
 * Request a picture buffer with a specific set of permissions.
 *
 * @param link  the output link to the filter from which the buffer will
 *              be requested
 * @param perms the required access permissions
 * @param w     the minimum width of the buffer to allocate
 * @param h     the minimum height of the buffer to allocate
 * @return      A reference to the buffer. This must be unreferenced with
 *              avfilter_unref_buffer when you are finished with it.
 */
AVFilterBufferRef *ff_get_video_buffer(AVFilterLink *link, int perms,
                                       int w, int h);

int ff_inplace_start_frame(AVFilterLink *link, AVFilterBufferRef *picref);
int ff_null_start_frame(AVFilterLink *link, AVFilterBufferRef *picref);
int ff_null_draw_slice(AVFilterLink *link, int y, int h, int slice_dir);
int ff_null_end_frame(AVFilterLink *link);

/**
 * Notify the next filter of the start of a frame.
 *
 * @param link   the output link the frame will be sent over
 * @param picref A reference to the frame about to be sent. The data for this
 *               frame need only be valid once draw_slice() is called for that
 *               portion. The receiving filter will free this reference when
 *               it no longer needs it.
 *
 * @return >= 0 on success, a negative AVERROR on error. This function will
 * unreference picref in case of error.
 */
int ff_start_frame(AVFilterLink *link, AVFilterBufferRef *picref);

/**
 * Pass video frame along and keep an internal reference for later use.
 */
int ff_null_start_frame_keep_ref(AVFilterLink *inlink, AVFilterBufferRef *picref);

/**
 * Notify the next filter that the current frame has finished.
 *
 * @param link the output link the frame was sent over
 *
 * @return >= 0 on success, a negative AVERROR on error
 */
int ff_end_frame(AVFilterLink *link);

/**
 * Send a slice to the next filter.
 *
 * Slices have to be provided in sequential order, either in
 * top-bottom or bottom-top order. If slices are provided in
 * non-sequential order the behavior of the function is undefined.
 *
 * @param link the output link over which the frame is being sent
 * @param y    offset in pixels from the top of the image for this slice
 * @param h    height of this slice in pixels
 * @param slice_dir the assumed direction for sending slices,
 *             from the top slice to the bottom slice if the value is 1,
 *             from the bottom slice to the top slice if the value is -1,
 *             for other values the behavior of the function is undefined.
 *
 * @return >= 0 on success, a negative AVERROR on error.
 */
int ff_draw_slice(AVFilterLink *link, int y, int h, int slice_dir);

#endif /* AVFILTER_VIDEO_H */
