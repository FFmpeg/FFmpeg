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
#define av_cold
#define CONFIG_HARDCODED_TABLES 0
#include "qdm2_tablegen.h"
#include "tableprint.h"

void tableinit(void)
{
    softclip_table_init();
    rnd_table_init();
    init_noise_samples();
}

const struct tabledef tables[] = {
    {
        "static const uint16_t softclip_table[HARDCLIP_THRESHOLD - SOFTCLIP_THRESHOLD + 1]",
        write_uint16_array,
        softclip_table,
        HARDCLIP_THRESHOLD - SOFTCLIP_THRESHOLD + 1,
        0
    },
    {
        "static const float noise_table[4096]",
        write_float_array,
        noise_table,
        4096,
        0
    },
    {
        "static const uint8_t random_dequant_index[256][5]",
        write_uint8_2d_array,
        random_dequant_index,
        256,
        5
    },
    {
        "static const uint8_t random_dequant_type24[128][3]",
        write_uint8_2d_array,
        random_dequant_type24,
        128,
        3
    },
    {
        "static const float noise_samples[128]",
        write_float_array,
        noise_samples,
        128,
        0
    },
    { NULL }
};
