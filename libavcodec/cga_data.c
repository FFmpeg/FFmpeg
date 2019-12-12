/*
 * CGA/EGA/VGA ROM data
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

/**
 * @file
 * CGA/EGA/VGA ROM data
 * @note fonts are in libavutil/xga_font_data.[ch]
 */

#include <stdint.h>
#include "cga_data.h"

const uint32_t ff_cga_palette[16] = {
    0xFF000000, 0xFF0000AA, 0xFF00AA00, 0xFF00AAAA, 0xFFAA0000, 0xFFAA00AA, 0xFFAA5500, 0xFFAAAAAA,
    0xFF555555, 0xFF5555FF, 0xFF55FF55, 0xFF55FFFF, 0xFFFF5555, 0xFFFF55FF, 0xFFFFFF55, 0xFFFFFFFF,
};

const uint32_t ff_ega_palette[64] = {
    0xFF000000, 0xFF0000AA, 0xFF00AA00, 0xFF00AAAA, 0xFFAA0000, 0xFFAA00AA, 0xFFAAAA00, 0xFFAAAAAA,
    0xFF000055, 0xFF0000FF, 0xFF00AA55, 0xFF00AAFF, 0xFFAA0055, 0xFFAA00FF, 0xFFAAAA55, 0xFFAAAAFF,
    0xFF005500, 0xFF0055AA, 0xFF00FF00, 0xFF00FFAA, 0xFFAA5500, 0xFFAA55AA, 0xFFAAFF00, 0xFFAAFFAA,
    0xFF005555, 0xFF0055FF, 0xFF00FF55, 0xFF00FFFF, 0xFFAA5555, 0xFFAA55FF, 0xFFAAFF55, 0xFFAAFFFF,
    0xFF550000, 0xFF5500AA, 0xFF55AA00, 0xFF55AAAA, 0xFFFF0000, 0xFFFF00AA, 0xFFFFAA00, 0xFFFFAAAA,
    0xFF550055, 0xFF5500FF, 0xFF55AA55, 0xFF55AAFF, 0xFFFF0055, 0xFFFF00FF, 0xFFFFAA55, 0xFFFFAAFF,
    0xFF555500, 0xFF5555AA, 0xFF55FF00, 0xFF55FFAA, 0xFFFF5500, 0xFFFF55AA, 0xFFFFFF00, 0xFFFFFFAA,
    0xFF555555, 0xFF5555FF, 0xFF55FF55, 0xFF55FFFF, 0xFFFF5555, 0xFFFF55FF, 0xFFFFFF55, 0xFFFFFFFF
};

void ff_draw_pc_font(uint8_t *dst, int linesize, const uint8_t *font, int font_height, int ch, int fg, int bg)
{
    int char_y, mask;
    for (char_y = 0; char_y < font_height; char_y++) {
        for (mask = 0x80; mask; mask >>= 1) {
            *dst++ = font[ch * font_height + char_y] & mask ? fg : bg;
        }
        dst += linesize - 8;
    }
}
