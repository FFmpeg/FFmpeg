/*
 * Generate a header file for hardcoded PCM tables
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
#include "pcm_tablegen.h"
#include "tableprint.h"

int main(void)
{
    pcm_alaw_tableinit();
    pcm_ulaw_tableinit();

    write_fileheader();

    printf("static const uint8_t linear_to_alaw[1 << 14] = {\n");
    write_uint8_array(linear_to_alaw, 1 << 14);
    printf("};\n");

    printf("static const uint8_t linear_to_ulaw[1 << 14] = {\n");
    write_uint8_array(linear_to_ulaw, 1 << 14);
    printf("};\n");

    return 0;
}

