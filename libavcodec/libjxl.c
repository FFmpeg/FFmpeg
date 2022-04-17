/*
 * JPEG XL de/encoding via libjxl, common support implementation
 * Copyright (c) 2021 Leo Izen <leo.izen@gmail.com>
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
 * @file
 * JPEG XL via libjxl common support implementation
 */

#include "libavutil/cpu.h"
#include "libavutil/mem.h"

#include <jxl/memory_manager.h>
#include "libjxl.h"

size_t ff_libjxl_get_threadcount(int threads)
{
    if (threads <= 0)
        return av_cpu_count();
    if (threads == 1)
        return 0;
    return threads;
}

/**
 * Wrapper around av_malloc used as a jpegxl_alloc_func.
 *
 * @param  opaque opaque pointer for jpegxl_alloc_func, always ignored
 * @param  size Size in bytes for the memory block to be allocated
 * @return Pointer to the allocated block, or `NULL` if it cannot be allocated
 */
static void *libjxl_av_malloc(void *opaque, size_t size)
{
    return av_malloc(size);
}

/**
 * Wrapper around av_free used as a jpegxl_free_func.
 *
 * @param opaque  opaque pointer for jpegxl_free_func, always ignored
 * @param address Pointer to the allocated block, to free. `NULL` permitted as a no-op.
 */
static void libjxl_av_free(void *opaque, void *address)
{
    av_free(address);
}

void ff_libjxl_init_memory_manager(JxlMemoryManager *manager)
{
    manager->opaque = NULL;
    manager->alloc = &libjxl_av_malloc;
    manager->free = &libjxl_av_free;
}
