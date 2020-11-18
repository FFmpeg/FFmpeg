/*
 * Generate a header file for hardcoded shared mpegaudiodec tables
 *
 * Copyright (c) 2009 Reimar Döffinger <Reimar.Doeffinger@gmx.de>
 * Copyright (c) 2020 Andreas Rheinhardt <andreas.rheinhardt@gmail.com>
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
#include "libavutil/tablegen.h"
#include "mpegaudiodec_common_tablegen.h"
#include "tableprint.h"

int main(void)
{
    mpegaudiodec_common_tableinit();

    write_fileheader();

    WRITE_ARRAY("const", int8_t, ff_table_4_3_exp);
    WRITE_ARRAY("const", uint32_t, ff_table_4_3_value);

    return 0;
}
