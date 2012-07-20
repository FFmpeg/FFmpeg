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

#include "libavcodec/avcodec.h" // AVFrame
#include "avfilter.h"

/**
 * Copy the frame properties of src to dst, without copying the actual
 * image data.
 *
 * @return 0 on success, a negative number on error.
 */
int avfilter_copy_frame_props(AVFilterBufferRef *dst, const AVFrame *src);

/**
 * Copy the frame properties and data pointers of src to dst, without copying
 * the actual data.
 *
 * @return 0 on success, a negative number on error.
 */
int avfilter_copy_buf_props(AVFrame *dst, const AVFilterBufferRef *src);

/**
 * Create and return a picref reference from the data and properties
 * contained in frame.
 *
 * @param perms permissions to assign to the new buffer reference
 */
AVFilterBufferRef *avfilter_get_video_buffer_ref_from_frame(const AVFrame *frame, int perms);


/**
 * Create and return a picref reference from the data and properties
 * contained in frame.
 *
 * @param perms permissions to assign to the new buffer reference
 */
AVFilterBufferRef *avfilter_get_audio_buffer_ref_from_frame(const AVFrame *frame,
                                                            int perms);

/**
 * Create and return a buffer reference from the data and properties
 * contained in frame.
 *
 * @param perms permissions to assign to the new buffer reference
 */
AVFilterBufferRef *avfilter_get_buffer_ref_from_frame(enum AVMediaType type,
                                                      const AVFrame *frame,
                                                      int perms);

#ifdef FF_API_FILL_FRAME
/**
 * Fill an AVFrame with the information stored in samplesref.
 *
 * @param frame an already allocated AVFrame
 * @param samplesref an audio buffer reference
 * @return 0 in case of success, a negative AVERROR code in case of
 * failure
 * @deprecated Use avfilter_copy_buf_props() instead.
 */
attribute_deprecated
int avfilter_fill_frame_from_audio_buffer_ref(AVFrame *frame,
                                              const AVFilterBufferRef *samplesref);

/**
 * Fill an AVFrame with the information stored in picref.
 *
 * @param frame an already allocated AVFrame
 * @param picref a video buffer reference
 * @return 0 in case of success, a negative AVERROR code in case of
 * failure
 * @deprecated Use avfilter_copy_buf_props() instead.
 */
attribute_deprecated
int avfilter_fill_frame_from_video_buffer_ref(AVFrame *frame,
                                              const AVFilterBufferRef *picref);

/**
 * Fill an AVFrame with information stored in ref.
 *
 * @param frame an already allocated AVFrame
 * @param ref a video or audio buffer reference
 * @return 0 in case of success, a negative AVERROR code in case of
 * failure
 * @deprecated Use avfilter_copy_buf_props() instead.
 */
attribute_deprecated
int avfilter_fill_frame_from_buffer_ref(AVFrame *frame,
                                        const AVFilterBufferRef *ref);
#endif

/**
 * Add frame data to buffer_src.
 *
 * @param buffer_src  pointer to a buffer source context
 * @param frame       a frame, or NULL to mark EOF
 * @param flags       a combination of AV_BUFFERSRC_FLAG_*
 * @return            >= 0 in case of success, a negative AVERROR code
 *                    in case of failure
 */
int av_buffersrc_add_frame(AVFilterContext *buffer_src,
                           const AVFrame *frame, int flags);

#endif /* AVFILTER_AVCODEC_H */
