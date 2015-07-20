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
#include "libavutil/internal.h"
#include "libavutil/common.h"
#undef CONFIG_HARDCODED_TABLES
#define CONFIG_HARDCODED_TABLES 0
#define USE_FIXED 1
#include "aacsbr_fixed_tablegen.h"
#include "tableprint.h"

int main(void)
{
    aacsbr_tableinit();

    write_fileheader();

    WRITE_ARRAY_ALIGNED("static const", 32, int32_t, sbr_qmf_window_ds);
    WRITE_ARRAY_ALIGNED("static const", 32, int32_t, sbr_qmf_window_us);

    return 0;
}
