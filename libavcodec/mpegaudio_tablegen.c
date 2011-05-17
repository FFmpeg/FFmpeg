/*
 * Generate a header file for hardcoded mpegaudiodec tables
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
#include "mpegaudio_tablegen.h"
#include "tableprint.h"

int main(void)
{
    mpegaudio_tableinit();

    write_fileheader();

    WRITE_ARRAY("static const", int8_t, table_4_3_exp);
    WRITE_ARRAY("static const", uint32_t, table_4_3_value);
    WRITE_ARRAY("static const", uint32_t, exp_table_fixed);
    WRITE_ARRAY("static const", float, exp_table_float);
    WRITE_2D_ARRAY("static const", uint32_t, expval_table_fixed);
    WRITE_2D_ARRAY("static const", float, expval_table_float);

    return 0;
}
