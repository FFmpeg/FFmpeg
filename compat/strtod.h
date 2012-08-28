/*
 * C99-compatible strtod() implementation
 * Copyright (c) 2012 Ronald S. Bultje <rsbultje@gmail.com>
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

#ifndef AVCOMPAT_STRTOD_H
#define AVCOMPAT_STRTOD_H

#include <stdlib.h>

/*
 * strtod() on MSVCRT doesn't handle strings like 'inf' or 'nan'. Also,
 * it doesn't handle "0x" prefixes for hexadecimal input.
 *
 * Thus, provide our own fallback wrapper with correct behaviour.
 */
#undef strtod
#define strtod avpriv_strtod
double strtod(char *restrict nptr, char **restrict endptr);

#endif /* AVCOMPAT_STRTOD_H */
