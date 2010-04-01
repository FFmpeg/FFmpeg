/*
 * Generate a header file for hardcoded QDM2 tables
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
#include "qdm2_tablegen.h"
#include "tableprint.h"

int main(void)
{
    softclip_table_init();
    rnd_table_init();
    init_noise_samples();

    write_fileheader();

    printf("static const uint16_t softclip_table[HARDCLIP_THRESHOLD - SOFTCLIP_THRESHOLD + 1] = {\n");
    write_uint16_array(softclip_table, HARDCLIP_THRESHOLD - SOFTCLIP_THRESHOLD + 1);
    printf("};\n");

    printf("static const float noise_table[4096] = {\n");
    write_float_array(noise_table, 4096);
    printf("};\n");

    printf("static const uint8_t random_dequant_index[256][5] = {\n");
    write_uint8_2d_array(random_dequant_index, 256, 5);
    printf("};\n");

    printf("static const uint8_t random_dequant_type24[128][3] = {\n");
    write_uint8_2d_array(random_dequant_type24, 128, 3);
    printf("};\n");

    printf("static const float noise_samples[128] = {\n");
    write_float_array(noise_samples, 128);
    printf("};\n");

    return 0;
}
