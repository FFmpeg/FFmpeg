/*
 * Copyright (c) 2005 Robert Edele <yartrebo@earthlink.net>
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

#include "bbox.h"

int ff_calculate_bounding_box(FFBoundingBox *bbox,
                              const uint8_t *data, int linesize, int w, int h,
                              int min_val)
{
    int x, y;
    int start_x;
    int start_y;
    int end_x;
    int end_y;
    const uint8_t *line;

    /* left bound */
    for (start_x = 0; start_x < w; start_x++)
        for (y = 0; y < h; y++)
            if ((data[y * linesize + start_x] > min_val))
                goto outl;
outl:
    if (start_x == w) /* no points found */
        return 0;

    /* right bound */
    for (end_x = w - 1; end_x >= start_x; end_x--)
        for (y = 0; y < h; y++)
            if ((data[y * linesize + end_x] > min_val))
                goto outr;
outr:

    /* top bound */
    line = data;
    for (start_y = 0; start_y < h; start_y++) {
        for (x = 0; x < w; x++)
            if (line[x] > min_val)
                goto outt;
        line += linesize;
    }
outt:

    /* bottom bound */
    line = data + (h-1)*linesize;
    for (end_y = h - 1; end_y >= start_y; end_y--) {
        for (x = 0; x < w; x++)
            if (line[x] > min_val)
                goto outb;
        line -= linesize;
    }
outb:

    bbox->x1 = start_x;
    bbox->y1 = start_y;
    bbox->x2 = end_x;
    bbox->y2 = end_y;
    return 1;
}
