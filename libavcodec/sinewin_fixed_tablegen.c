/*
 * Generate a header file for hardcoded sine windows
 *
 * Copyright (c) 2009 Reimar Döffinger <Reimar.Doeffinger@gmx.de>
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

#include "tableprint.h"

#define BUILD_TABLES
#define CONFIG_HARDCODED_TABLES 0
#include "sinewin_fixed_tablegen.h"

int main(void)
{
    write_fileheader();

    init_sine_windows_fixed();
#define PRINT_TABLE(size)                               \
    printf("SINETABLE("#size") = {\n");                 \
    write_int32_t_array(sine_ ## size ## _fixed, size); \
    printf("};\n")
    PRINT_TABLE(96);
    PRINT_TABLE(120);
    PRINT_TABLE(128);
    PRINT_TABLE(480);
    PRINT_TABLE(512);
    PRINT_TABLE(768);
    PRINT_TABLE(960);
    PRINT_TABLE(1024);
    return 0;
}
