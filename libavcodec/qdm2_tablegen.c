/*
 * Generate a header file for hardcoded QDM2 tables
 *
 * Copyright (c) 2010 Reimar Döffinger <Reimar.Doeffinger@gmx.de>
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
#include "qdm2_tablegen.h"
#include "tableprint.h"

int main(void)
{
    softclip_table_init();
    rnd_table_init();
    init_noise_samples();

    write_fileheader();

    WRITE_ARRAY("static const", uint16_t, softclip_table);
    WRITE_ARRAY("static const", float, noise_table);
    WRITE_ARRAY("static const", float, noise_samples);

    WRITE_2D_ARRAY("static const", uint8_t, random_dequant_index);
    WRITE_2D_ARRAY("static const", uint8_t, random_dequant_type24);

    return 0;
}
