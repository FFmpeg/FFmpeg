/*
 * Generate a header file for hardcoded mpegaudiodec tables
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

#include <stdlib.h>
#define CONFIG_HARDCODED_TABLES 0
#include "mpegaudio_tablegen.h"
#include "tableprint.h"

int main(void)
{
    mpegaudio_tableinit();

    write_fileheader();

    printf("static const int8_t table_4_3_exp[TABLE_4_3_SIZE] = {\n");
    write_int8_array(table_4_3_exp, TABLE_4_3_SIZE);
    printf("};\n");

    printf("static const uint32_t table_4_3_value[TABLE_4_3_SIZE] = {\n");
    write_uint32_array(table_4_3_value, TABLE_4_3_SIZE);
    printf("};\n");

    printf("static const uint32_t exp_table[512] = {\n");
    write_uint32_array(exp_table, 512);
    printf("};\n");

    printf("static const uint32_t expval_table[512][16] = {\n");
    write_uint32_2d_array(expval_table, 512, 16);
    printf("};\n");

    return 0;
}
