/*
 * Copyright (c) 2026 Sam Richards
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

#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void *OCIOHandle;

// Create an OCIO processor for Display/View transform.
// Returns NULL on failure.
OCIOHandle ocio_create_display_view_processor(AVFilterContext *ctx,
                                              const char *config_path,
                                              const char *input_color_space,
                                              const char *display,
                                              const char *view, int inverse,
                                              AVDictionary *params);

// Create an OCIO processor for output colorspace transform.
// Returns NULL on failure.
OCIOHandle
ocio_create_output_colorspace_processor(AVFilterContext *ctx,
                                        const char *config_path,
                                        const char *input_color_space,
                                        const char *output_color_space,
                                        AVDictionary *params);

// Create an OCIO processor for file transform.
// Returns NULL on failure.
OCIOHandle ocio_create_file_transform_processor(AVFilterContext *ctx,
                                                const char *file_transform,
                                                int inverse);

// Finalize OCIO processor for given bit depth.
// is_half_float: true for half-float, false for float
int ocio_finalize_processor(AVFilterContext *ctx, OCIOHandle handle, int input_format,
                            int output_format);

// Apply processor to planar float RGB(A).
// pixels: pointer to float samples
// w,h: image dimensions
// channels: 3 or 4
// stride_bytes: bytes between row starts (use 0 for tightly packed)
int ocio_apply(AVFilterContext *ctx, OCIOHandle handle, AVFrame *input_frame, AVFrame *output_frame,
               int y_start, int height);

// Destroy OCIO processor.
void ocio_destroy_processor(AVFilterContext *ctx, OCIOHandle handle);

#ifdef __cplusplus
}
#endif
