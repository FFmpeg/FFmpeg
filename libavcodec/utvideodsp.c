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

#include <stdint.h>

#include "config.h"
#include "libavutil/attributes.h"
#include "utvideodsp.h"

static void restore_rgb_planes_c(uint8_t *src_r,
                                 uint8_t *src_g,
                                 uint8_t *src_b,
                                 ptrdiff_t linesize_r,
                                 ptrdiff_t linesize_g,
                                 ptrdiff_t linesize_b,
                                 int width, int height)
{
    uint8_t r, g, b;
    int i, j;

    for (j = 0; j < height; j++) {
        for (i = 0; i < width; i++) {
            r = src_r[i];
            g = src_g[i];
            b = src_b[i];
            src_r[i] = r + g - 0x80;
            src_b[i] = b + g - 0x80;
        }
        src_r += linesize_r;
        src_g += linesize_g;
        src_b += linesize_b;
    }
}

static void restore_rgb_planes10_c(uint16_t *src_r,
                                   uint16_t *src_g,
                                   uint16_t *src_b,
                                   ptrdiff_t linesize_r,
                                   ptrdiff_t linesize_g,
                                   ptrdiff_t linesize_b,
                                   int width, int height)
{
    int r, g, b;
    int i, j;

    for (j = 0; j < height; j++) {
        for (i = 0; i < width; i++) {
            r = src_r[i];
            g = src_g[i];
            b = src_b[i];
            src_r[i] = (r + g - 0x200) & 0x3FF;
            src_b[i] = (b + g - 0x200) & 0x3FF;
        }
        src_r += linesize_r;
        src_g += linesize_g;
        src_b += linesize_b;
    }
}

av_cold void ff_utvideodsp_init(UTVideoDSPContext *c)
{
    c->restore_rgb_planes   = restore_rgb_planes_c;
    c->restore_rgb_planes10 = restore_rgb_planes10_c;

#if ARCH_X86
    ff_utvideodsp_init_x86(c);
#endif
}
