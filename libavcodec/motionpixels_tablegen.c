/*
 * Generate a header file for hardcoded motion pixels RGB to YUV table
 *
 * Copyright (c) 2009 Reimar Döffinger <Reimar.Doeffinger@gmx.de>
 *
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

#include <stdlib.h>
#define CONFIG_HARDCODED_TABLES 0
#define MAX_NEG_CROP 0
#define ff_crop_tab ((uint8_t *)NULL)
#include "motionpixels_tablegen.h"
#include "tableprint.h"

int main(void)
{
    motionpixels_tableinit();

    write_fileheader();

    printf("static const YuvPixel mp_rgb_yuv_table[1 << 15] = {\n");
    write_int8_t_2d_array(mp_rgb_yuv_table, 1 << 15, 3);
    printf("};\n");

    return 0;
}
