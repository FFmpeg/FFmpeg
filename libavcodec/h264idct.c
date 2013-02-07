/*
 * H.264 IDCT
 * Copyright (c) 2004 Michael Niedermayer <michaelni@gmx.at>
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

/**
 * @file
 * H.264 IDCT.
 * @author Michael Niedermayer <michaelni@gmx.at>
 */

#include "h264idct.h"

#define BIT_DEPTH 8
#include "h264idct_template.c"
#undef BIT_DEPTH

#define BIT_DEPTH 9
#include "h264idct_template.c"
#undef BIT_DEPTH

#define BIT_DEPTH 10
#include "h264idct_template.c"
#undef BIT_DEPTH
