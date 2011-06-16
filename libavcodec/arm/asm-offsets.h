/*
 * Copyright (c) 2010 Mans Rullgard
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

#ifndef AVCODEC_ARM_ASM_OFFSETS_H
#define AVCODEC_ARM_ASM_OFFSETS_H

#ifndef __ASSEMBLER__
#include <stddef.h>
#define CHK_OFFS(s, m, o) struct check_##o {    \
        int x_##o[offsetof(s, m) == o? 1: -1];  \
    }
#endif

/* MpegEncContext */
#define Y_DC_SCALE               0xb4
#define C_DC_SCALE               0xb8
#define AC_PRED                  0xbc
#define BLOCK_LAST_INDEX         0xc0
#define H263_AIC                 0xf0
#define INTER_SCANTAB_RASTER_END 0x138

#endif /* AVCODEC_ARM_ASM_OFFSETS_H */
