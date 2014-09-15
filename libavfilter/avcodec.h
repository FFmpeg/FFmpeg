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

#ifndef AVFILTER_AVCODEC_H
#define AVFILTER_AVCODEC_H

/**
 * @file
 * libavcodec/libavfilter gluing utilities
 *
 * This should be included in an application ONLY if the installed
 * libavfilter has been compiled with libavcodec support, otherwise
 * symbols defined below will not be available.
 */

#include "avfilter.h"

#if FF_API_AVFILTERBUFFER
/**
 * Create and return a picref reference from the data and properties
 * contained in frame.
 *
 * @param perms permissions to assign to the new buffer reference
 * @deprecated avfilter APIs work natively with AVFrame instead.
 */
attribute_deprecated
AVFilterBufferRef *avfilter_get_video_buffer_ref_from_frame(const AVFrame *frame, int perms);


/**
 * Create and return a picref reference from the data and properties
 * contained in frame.
 *
 * @param perms permissions to assign to the new buffer reference
 * @deprecated avfilter APIs work natively with AVFrame instead.
 */
attribute_deprecated
AVFilterBufferRef *avfilter_get_audio_buffer_ref_from_frame(const AVFrame *frame,
                                                            int perms);

/**
 * Create and return a buffer reference from the data and properties
 * contained in frame.
 *
 * @param perms permissions to assign to the new buffer reference
 * @deprecated avfilter APIs work natively with AVFrame instead.
 */
attribute_deprecated
AVFilterBufferRef *avfilter_get_buffer_ref_from_frame(enum AVMediaType type,
                                                      const AVFrame *frame,
                                                      int perms);
#endif

#endif /* AVFILTER_AVCODEC_H */
