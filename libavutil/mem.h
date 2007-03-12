/*
 * copyright (c) 2006 Michael Niedermayer <michaelni@gmx.at>
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

/**
 * @file mem.h
 * Memory handling functions.
 */

#ifndef AV_MEM_H
#define AV_MEM_H

#ifdef __GNUC__
  #define DECLARE_ALIGNED(n,t,v)       t v __attribute__ ((aligned (n)))
#else
  #define DECLARE_ALIGNED(n,t,v)      __declspec(align(n)) t v
#endif

/**
 * Memory allocation of size byte with alignment suitable for all
 * memory accesses (including vectors if available on the
 * CPU). av_malloc(0) must return a non NULL pointer.
 */
void *av_malloc(unsigned int size);

/**
 * av_realloc semantics (same as glibc): if ptr is NULL and size > 0,
 * identical to malloc(size). If size is zero, it is identical to
 * free(ptr) and NULL is returned.
 */
void *av_realloc(void *ptr, unsigned int size);

/**
 * Free memory which has been allocated with av_malloc(z)() or av_realloc().
 * NOTE: ptr = NULL is explicetly allowed
 * Note2: it is recommended that you use av_freep() instead
 */
void av_free(void *ptr);

void *av_mallocz(unsigned int size);
char *av_strdup(const char *s);

/**
 * Frees memory and sets the pointer to NULL.
 * @param ptr pointer to the pointer which should be freed
 */
void av_freep(void *ptr);

#endif /* AV_MEM_H */
