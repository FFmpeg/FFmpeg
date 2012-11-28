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

AVFrame *ff_default_get_video_buffer(AVFilterLink *link, int w, int h);
AVFrame *ff_null_get_video_buffer(AVFilterLink *link, int w, int h);

/**
 * Request a picture buffer with a specific set of permissions.
 *
 * @param link  the output link to the filter from which the buffer will
 *              be requested
 * @param w     the minimum width of the buffer to allocate
 * @param h     the minimum height of the buffer to allocate
 * @return      A reference to the buffer. This must be unreferenced with
 *              avfilter_unref_buffer when you are finished with it.
 */
AVFrame *ff_get_video_buffer(AVFilterLink *link, int w, int h);

#endif /* AVFILTER_VIDEO_H */
