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

#ifndef AVFILTER_VIDEO_H
#define AVFILTER_VIDEO_H

#include "avfilter.h"

AVFilterBufferRef *ff_default_get_video_buffer(AVFilterLink *link,
                                               int perms, int w, int h);
AVFilterBufferRef *ff_null_get_video_buffer(AVFilterLink *link, int perms, int w, int h);

void ff_null_start_frame(AVFilterLink *link, AVFilterBufferRef *picref);
void ff_null_draw_slice(AVFilterLink *link, int y, int h, int slice_dir);
void ff_null_end_frame(AVFilterLink *link);

/**
 * Notify the next filter of the start of a frame.
 *
 * @param link   the output link the frame will be sent over
 * @param picref A reference to the frame about to be sent. The data for this
 *               frame need only be valid once draw_slice() is called for that
 *               portion. The receiving filter will free this reference when
 *               it no longer needs it.
 */
void ff_start_frame(AVFilterLink *link, AVFilterBufferRef *picref);

/**
 * Notify the next filter that the current frame has finished.
 *
 * @param link the output link the frame was sent over
 */
void ff_end_frame(AVFilterLink *link);

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
 */
void ff_draw_slice(AVFilterLink *link, int y, int h, int slice_dir);

#endif /* AVFILTER_VIDEO_H */
