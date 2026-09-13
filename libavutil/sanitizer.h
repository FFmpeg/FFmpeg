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

#ifndef AVUTIL_SANITIZER_H
#define AVUTIL_SANITIZER_H

#include "config.h"

#if defined(__has_feature)
#if __has_feature(memory_sanitizer)
#define HAVE_MSAN 1
#endif
#endif
#ifndef HAVE_MSAN
#define HAVE_MSAN 0
#endif

/* Mark memory as uninitialized. */
#if HAVE_MSAN
#include <sanitizer/msan_interface.h>
#define FF_MEM_UNDEFINED(ptr, size) __msan_allocated_memory(ptr, size)
#elif CONFIG_MEMORY_POISONING && HAVE_VALGRIND_MEMCHECK_H
#include <valgrind/memcheck.h>
#define FF_MEM_UNDEFINED(ptr, size) VALGRIND_MAKE_MEM_UNDEFINED(ptr, size)
#else
#define FF_MEM_UNDEFINED(ptr, size) ((void)(ptr), (void)(size))
#endif

#endif /* AVUTIL_SANITIZER_H */
