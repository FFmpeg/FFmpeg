/*
 * Copyright (C) 2005 Aurelien Jacobs <aurel@gnuage.org>
 * Copyright (C) 2009, 2026 Ramiro Polla <ramiro.polla@gmail.com>
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

#if HAVE_MMAP && HAVE_MPROTECT
#   define _DEFAULT_SOURCE
#   define _SVID_SOURCE // needed for MAP_ANONYMOUS
#   define _DARWIN_C_SOURCE // needed for MAP_ANON
#   include <sys/mman.h>
#   if defined(MAP_ANON) && !defined(MAP_ANONYMOUS)
#       define MAP_ANONYMOUS MAP_ANON
#   endif
#endif

#include "libavutil/error.h"

#include "jit.h"

#if HAVE_MMAP && HAVE_MPROTECT && defined(MAP_ANONYMOUS)

void *ff_sws_jit_alloc(size_t size)
{
    void *ptr = mmap(NULL, size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED)
        return NULL;
    return ptr;
}

int ff_sws_jit_protect(void *ptr, size_t size)
{
    if (mprotect(ptr, size, PROT_READ | PROT_EXEC) == -1)
        return AVERROR(errno);
    return 0;
}

void ff_sws_jit_free(void *ptr, size_t size)
{
    if (ptr)
        munmap(ptr, size);
}

#elif HAVE_VIRTUALALLOC

#include <windows.h>

void *ff_sws_jit_alloc(size_t size)
{
    return VirtualAlloc(NULL, size, MEM_COMMIT, PAGE_READWRITE);
}

int ff_sws_jit_protect(void *ptr, size_t size)
{
    DWORD old_protect;
    if (!VirtualProtect(ptr, size, PAGE_EXECUTE_READ, &old_protect))
        return AVERROR(EINVAL);
    return 0;
}

void ff_sws_jit_free(void *ptr, size_t size)
{
    if (ptr)
        VirtualFree(ptr, 0, MEM_RELEASE);
}

#else

#include "libavutil/mem.h"

void *ff_sws_jit_alloc(size_t size)
{
    return av_malloc(size);
}

int ff_sws_jit_protect(void *ptr, size_t size)
{
    return 0;
}

void ff_sws_jit_free(void *ptr, size_t size)
{
    av_free(ptr);
}

#endif
