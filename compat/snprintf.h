/*
 * C99-compatible snprintf() and vsnprintf() implementations
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

#ifndef AVCOMPAT_SNPRINTF_H
#define AVCOMPAT_SNPRINTF_H

#include <stdarg.h>
#include <stdio.h>

/*
 * snprintf() on MSVCRT returns -1 (instead of required buffer length)
 * if the input buffer isn't big enough. Also, if the required buffer
 * length is exactly identical to the input buffer size, or if the
 * return value is -1 because the input buffer isn't big enough, MSVCRT
 * will fail to NULL-terminate the output buffer. vsnprintf() has the
 * same issue.
 *
 * Thus, provide our own fallback wrappers with correct behaviour.
 */
#undef snprintf
#define snprintf avpriv_snprintf
int snprintf(char *restrict s, size_t n, const char *restrict format, ...);

#undef vsnprintf
#define vsnprintf avpriv_vsnprintf
int vsnprintf(char *restrict s, size_t n, const char *restrict format, va_list ap);

#endif /* AVCOMPAT_SNPRINTF_H */
