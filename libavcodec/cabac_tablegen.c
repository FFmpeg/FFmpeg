/*
 * Header file for hardcoded AAC SBR windows
 *
 * Copyright (c) 2014 Reimar Döffinger <Reimar.Doeffinger@gmx.de>
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
#include "libavutil/common.h"
#include "cabac_functions.h"
#undef CONFIG_HARDCODED_TABLES
#define CONFIG_HARDCODED_TABLES 0
av_const int av_log2(unsigned v) { int r = 0; while (v >>= 1) r++; return r; }
#include "cabac_tablegen.h"
#include "tableprint.h"

int main(void)
{
    cabac_tableinit();

    write_fileheader();

    WRITE_ARRAY("const", uint8_t, ff_h264_cabac_tables);

    return 0;
}
