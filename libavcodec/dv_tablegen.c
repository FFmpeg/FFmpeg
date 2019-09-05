/*
 * Generate a header file for hardcoded DV tables
 *
 * Copyright (c) 2010 Reimar Döffinger <Reimar.Doeffinger@gmx.de>
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

#include <stdlib.h>
#define CONFIG_HARDCODED_TABLES 0
#include "dv_tablegen.h"
#include "tableprint.h"
#include <inttypes.h>

WRITE_1D_FUNC_ARGV(dv_vlc_pair, 7,
                   "{0x%"PRIx32", %"PRIu32"}", data[i].vlc, data[i].size)
WRITE_2D_FUNC(dv_vlc_pair)

int main(void)
{
    dv_vlc_map_tableinit();

    write_fileheader();

    printf("static const struct dv_vlc_pair dv_vlc_map[DV_VLC_MAP_RUN_SIZE][DV_VLC_MAP_LEV_SIZE] = {\n");
    write_dv_vlc_pair_2d_array(dv_vlc_map, DV_VLC_MAP_RUN_SIZE, DV_VLC_MAP_LEV_SIZE);
    printf("};\n");

    return 0;
}
