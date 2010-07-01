/*
 * Copyright (c) 2009 Loren Merritt
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

#include "config.h"

#if HAVE_IBM_ASM

.macro DEFINE_REG n
    .equiv r\n, \n
    .equiv f\n, \n
    .equiv v\n, \n
.endm

DEFINE_REG 0
DEFINE_REG 1
DEFINE_REG 2
DEFINE_REG 3
DEFINE_REG 4
DEFINE_REG 5
DEFINE_REG 6
DEFINE_REG 7
DEFINE_REG 8
DEFINE_REG 9
DEFINE_REG 10
DEFINE_REG 11
DEFINE_REG 12
DEFINE_REG 13
DEFINE_REG 14
DEFINE_REG 15
DEFINE_REG 16
DEFINE_REG 17
DEFINE_REG 18
DEFINE_REG 19
DEFINE_REG 20
DEFINE_REG 21
DEFINE_REG 22
DEFINE_REG 23
DEFINE_REG 24
DEFINE_REG 25
DEFINE_REG 26
DEFINE_REG 27
DEFINE_REG 28
DEFINE_REG 29
DEFINE_REG 30
DEFINE_REG 31

#endif /* HAVE_IBM_ASM */
