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

#ifndef AVCORE_IMGUTILS_H
#define AVCORE_IMGUTILS_H

/**
 * @file
 * misc image utilities
 */

#include "libavutil/pixdesc.h"
#include "avcore.h"

/**
 * Compute the max pixel step for each plane of an image with a
 * format described by pixdesc.
 *
 * The pixel step is the distance in bytes between the first byte of
 * the group of bytes which describe a pixel component and the first
 * byte of the successive group in the same plane for the same
 * component.
 *
 * @param max_pixsteps an array which is filled with the max pixel step
 * for each plane. Since a plane may contain different pixel
 * components, the computed max_pixsteps[plane] is relative to the
 * component in the plane with the max pixel step.
 * @param max_pixstep_comps an array which is filled with the component
 * for each plane which has the max pixel step. May be NULL.
 */
void av_image_fill_max_pixsteps(int max_pixsteps[4], int max_pixstep_comps[4],
                                const AVPixFmtDescriptor *pixdesc);

/**
 * Compute the size of an image line with format pix_fmt and width
 * width for the plane plane.
 *
 * @return the computed size in bytes
 */
int av_image_get_linesize(enum PixelFormat pix_fmt, int width, int plane);

/**
 * Fill plane linesizes for an image with pixel format pix_fmt and
 * width width.
 *
 * @param linesizes array to be filled with the linesize for each plane
 * @return >= 0 in case of success, a negative error code otherwise
 */
int av_image_fill_linesizes(int linesizes[4], enum PixelFormat pix_fmt, int width);

/**
 * Fill plane data pointers for an image with pixel format pix_fmt and
 * height height.
 *
 * @param data pointers array to be filled with the pointer for each image plane
 * @param ptr the pointer to a buffer which will contain the image
 * @param linesizes[4] the array containing the linesize for each
 * plane, should be filled by av_image_fill_linesizes()
 * @return the size in bytes required for the image buffer, a negative
 * error code in case of failure
 */
int av_image_fill_pointers(uint8_t *data[4], enum PixelFormat pix_fmt, int height,
                           uint8_t *ptr, const int linesizes[4]);

/**
 * Copy image plane from src to dst.
 * That is, copy "height" number of lines of "bytewidth" bytes each.
 * The first byte of each successive line is separated by *_linesize
 * bytes.
 *
 * @param dst_linesize linesize for the image plane in dst
 * @param src_linesize linesize for the image plane in src
 */
void av_image_copy_plane(uint8_t       *dst, int dst_linesize,
                         const uint8_t *src, int src_linesize,
                         int bytewidth, int height);

/**
 * Copy image in src_data to dst_data.
 *
 * @param dst_linesize linesizes for the image in dst_data
 * @param src_linesize linesizes for the image in src_data
 */
void av_image_copy(uint8_t *dst_data[4], int dst_linesizes[4],
                   const uint8_t *src_data[4], const int src_linesizes[4],
                   enum PixelFormat pix_fmt, int width, int height);

/**
 * Check if the given dimension of an image is valid, meaning that all
 * bytes of the image can be addressed with a signed int.
 *
 * @param w the width of the picture
 * @param h the height of the picture
 * @param log_offset the offset to sum to the log level for logging with log_ctx
 * @param log_ctx the parent logging context, it may be NULL
 * @return >= 0 if valid, a negative error code otherwise
 */
int av_image_check_size(unsigned int w, unsigned int h, int log_offset, void *log_ctx);

#if FF_API_OLD_IMAGE_NAMES
attribute_deprecated
void av_fill_image_max_pixsteps(int max_pixsteps[4], int max_pixstep_comps[4],
                                const AVPixFmtDescriptor *pixdesc);

attribute_deprecated
int av_get_image_linesize(enum PixelFormat pix_fmt, int width, int plane);

attribute_deprecated
int av_fill_image_linesizes(int linesizes[4], enum PixelFormat pix_fmt, int width);

attribute_deprecated
int av_fill_image_pointers(uint8_t *data[4], enum PixelFormat pix_fmt, int height,
                           uint8_t *ptr, const int linesizes[4]);

attribute_deprecated
int av_check_image_size(unsigned int w, unsigned int h, int log_offset, void *log_ctx);
#endif

#endif /* AVCORE_IMGUTILS_H */
