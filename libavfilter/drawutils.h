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

#ifndef AVFILTER_DRAWUTILS_H
#define AVFILTER_DRAWUTILS_H

/**
 * @file
 * misc drawing utilities
 */

#include <stdint.h>
#include "avfilter.h"
#include "libavutil/pixfmt.h"

int ff_fill_rgba_map(uint8_t *rgba_map, enum AVPixelFormat pix_fmt);

int ff_fill_line_with_color(uint8_t *line[4], int pixel_step[4], int w,
                            uint8_t dst_color[4],
                            enum AVPixelFormat pix_fmt, uint8_t rgba_color[4],
                            int *is_packed_rgba, uint8_t rgba_map[4]);

void ff_draw_rectangle(uint8_t *dst[4], int dst_linesize[4],
                       uint8_t *src[4], int pixelstep[4],
                       int hsub, int vsub, int x, int y, int w, int h);

void ff_copy_rectangle(uint8_t *dst[4], int dst_linesize[4],
                       uint8_t *src[4], int src_linesize[4], int pixelstep[4],
                       int hsub, int vsub, int x, int y, int y2, int w, int h);

#define MAX_PLANES 4

typedef struct FFDrawContext {
    const struct AVPixFmtDescriptor *desc;
    enum AVPixelFormat format;
    unsigned nb_planes;
    int pixelstep[MAX_PLANES]; /*< offset between pixels */
    uint8_t comp_mask[MAX_PLANES]; /*< bitmask of used non-alpha components */
    uint8_t hsub[MAX_PLANES];  /*< horizontal subsampling */
    uint8_t vsub[MAX_PLANES];  /*< vertical subsampling */
    uint8_t hsub_max;
    uint8_t vsub_max;
} FFDrawContext;

typedef struct FFDrawColor {
    uint8_t rgba[4];
    union {
        uint32_t u32[4];
        uint16_t u16[8];
        uint8_t  u8[16];
    } comp[MAX_PLANES];
} FFDrawColor;

/**
 * Init a draw context.
 *
 * Only a limited number of pixel formats are supported, if format is not
 * supported the function will return an error.
 * No flags currently defined.
 * @return  0 for success, < 0 for error
 */
int ff_draw_init(FFDrawContext *draw, enum AVPixelFormat format, unsigned flags);

/**
 * Prepare a color.
 */
void ff_draw_color(FFDrawContext *draw, FFDrawColor *color, const uint8_t rgba[4]);

/**
 * Copy a rectangle from an image to another.
 *
 * The coordinates must be as even as the subsampling requires.
 */
void ff_copy_rectangle2(FFDrawContext *draw,
                        uint8_t *dst[], int dst_linesize[],
                        uint8_t *src[], int src_linesize[],
                        int dst_x, int dst_y, int src_x, int src_y,
                        int w, int h);

/**
 * Fill a rectangle with an uniform color.
 *
 * The coordinates must be as even as the subsampling requires.
 * The color needs to be inited with ff_draw_color.
 */
void ff_fill_rectangle(FFDrawContext *draw, FFDrawColor *color,
                       uint8_t *dst[], int dst_linesize[],
                       int dst_x, int dst_y, int w, int h);

/**
 * Blend a rectangle with an uniform color.
 */
void ff_blend_rectangle(FFDrawContext *draw, FFDrawColor *color,
                        uint8_t *dst[], int dst_linesize[],
                        int dst_w, int dst_h,
                        int x0, int y0, int w, int h);

/**
 * Blend an alpha mask with an uniform color.
 *
 * @param draw           draw context
 * @param color          color for the overlay;
 * @param dst            destination image
 * @param dst_linesize   line stride of the destination
 * @param dst_w          width of the destination image
 * @param dst_h          height of the destination image
 * @param mask           mask
 * @param mask_linesize  line stride of the mask
 * @param mask_w         width of the mask
 * @param mask_h         height of the mask
 * @param l2depth        log2 of depth of the mask (0 for 1bpp, 3 for 8bpp)
 * @param endianness     bit order of the mask (0: MSB to the left)
 * @param x0             horizontal position of the overlay
 * @param y0             vertical position of the overlay
 */
void ff_blend_mask(FFDrawContext *draw, FFDrawColor *color,
                   uint8_t *dst[], int dst_linesize[], int dst_w, int dst_h,
                   const uint8_t *mask, int mask_linesize, int mask_w, int mask_h,
                   int l2depth, unsigned endianness, int x0, int y0);

/**
 * Round a dimension according to subsampling.
 *
 * @param draw       draw context
 * @param sub_dir    0 for horizontal, 1 for vertical
 * @param round_dir  0 nearest, -1 round down, +1 round up
 * @param value      value to round
 * @return  the rounded value
 */
int ff_draw_round_to_sub(FFDrawContext *draw, int sub_dir, int round_dir,
                         int value);

/**
 * Return the list of pixel formats supported by the draw functions.
 *
 * The flags are the same as ff_draw_init, i.e., none currently.
 */
AVFilterFormats *ff_draw_supported_pixel_formats(unsigned flags);

#endif /* AVFILTER_DRAWUTILS_H */
