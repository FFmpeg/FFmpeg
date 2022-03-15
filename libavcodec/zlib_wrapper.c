/*
 * Wrappers for zlib
 * Copyright (C) 2022 Andreas Rheinhardt
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

#include <zlib.h>

#include "config.h"
#include "libavutil/error.h"
#include "libavutil/log.h"
#include "libavutil/mem.h"
#include "zlib_wrapper.h"

static void *alloc_wrapper(void *opaque, uInt items, uInt size)
{
    return av_malloc_array(items, size);
}

static void free_wrapper(void *opaque, void *ptr)
{
    av_free(ptr);
}

#if CONFIG_INFLATE_WRAPPER
int ff_inflate_init(FFZStream *z, void *logctx)
{
    z_stream *const zstream = &z->zstream;
    int zret;

    z->inited = 0;
    zstream->next_in  = Z_NULL;
    zstream->avail_in = 0;
    zstream->zalloc   = alloc_wrapper;
    zstream->zfree    = free_wrapper;
    zstream->opaque   = Z_NULL;

    zret = inflateInit(zstream);
    if (zret == Z_OK) {
        z->inited = 1;
    } else {
        av_log(logctx, AV_LOG_ERROR, "inflateInit error %d, message: %s\n",
               zret, zstream->msg ? zstream->msg : "");
        return AVERROR_EXTERNAL;
    }
    return 0;
}

void ff_inflate_end(FFZStream *z)
{
    if (z->inited) {
        z->inited = 0;
        inflateEnd(&z->zstream);
    }
}
#endif

#if CONFIG_DEFLATE_WRAPPER
int ff_deflate_init(FFZStream *z, int level, void *logctx)
{
    z_stream *const zstream = &z->zstream;
    int zret;

    z->inited = 0;
    zstream->zalloc = alloc_wrapper;
    zstream->zfree  = free_wrapper;
    zstream->opaque = Z_NULL;

    zret = deflateInit(zstream, level);
    if (zret == Z_OK) {
        z->inited = 1;
    } else {
        av_log(logctx, AV_LOG_ERROR, "deflateInit error %d, message: %s\n",
               zret, zstream->msg ? zstream->msg : "");
        return AVERROR_EXTERNAL;
    }
    return 0;
}

void ff_deflate_end(FFZStream *z)
{
    if (z->inited) {
        z->inited = 0;
        deflateEnd(&z->zstream);
    }
}
#endif
