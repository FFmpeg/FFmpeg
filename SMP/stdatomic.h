/*
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

#ifndef AVUTIL_STDATOMIC_H
#define AVUTIL_STDATOMIC_H

#include "config.h"

#ifdef __INTEL_COMPILER
#   include_next <stdatomic.h>
#elif defined(_MSC_VER)
#   include "../compat/atomics/win32/stdatomic.h"
#else
#   include_next <stdatomic.h>
#endif

#endif /* AVUTIL_STDATOMIC_H */
