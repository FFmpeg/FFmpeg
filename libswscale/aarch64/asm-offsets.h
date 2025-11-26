/*
 * Copyright (c) 2025 Arpad Panyik <Arpad.Panyik@arm.com>
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

#ifndef SWSCALE_AARCH64_ASM_OFFSETS_H
#define SWSCALE_AARCH64_ASM_OFFSETS_H

/* SwsLuts */
#define SL_IN  0x00
#define SL_OUT 0x08

/* SwsColorXform */
#define SCX_GAMMA     0x00
#define SCX_MAT       0x10
#define SCX_GAMMA_IN  (SCX_GAMMA + SL_IN)
#define SCX_GAMMA_OUT (SCX_GAMMA + SL_OUT)
#define SCX_MAT_00    SCX_MAT
#define SCX_MAT_22    (SCX_MAT + 8 * 2)

#endif /* SWSCALE_AARCH64_ASM_OFFSETS_H */
