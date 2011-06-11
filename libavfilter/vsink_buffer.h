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

#ifndef AVFILTER_VSINK_BUFFER_H
#define AVFILTER_VSINK_BUFFER_H

/**
 * @file
 * memory buffer sink API for video
 */

#include "avfilter.h"

/**
 * Tell av_vsink_buffer_get_video_buffer_ref() to read the picref, but not
 * remove it from the buffer. This is useful if you need only to read
 * the picref, without to fetch it.
 */
#define AV_VSINK_BUF_FLAG_PEEK 1

/**
 * Get a video buffer data from buffer_sink and put it in picref.
 *
 * @param buffer_sink pointer to a buffer sink context
 * @param flags a combination of AV_VSINK_BUF_FLAG_* flags
 * @return >= 0 in case of success, a negative AVERROR code in case of
 * failure
 */
int av_vsink_buffer_get_video_buffer_ref(AVFilterContext *buffer_sink,
                                         AVFilterBufferRef **picref, int flags);

#endif /* AVFILTER_VSINK_BUFFER_H */
