/*
 * Copyright (c) The FFmpeg developers
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

#include <limits.h>
#include <stdarg.h>

#include "avtextwriters.h"
#include "libavutil/opt.h"
#include "libavutil/bprint.h"

/* Buffer Writer */

# define WRITER_NAME "bufferwriter"

typedef struct BufferWriterContext {
    const AVClass *class;
    AVBPrint *buffer;
} BufferWriterContext;

static const char *bufferwriter_get_name(void *ctx)
{
    return WRITER_NAME;
}

static const AVClass bufferwriter_class = {
    .class_name = WRITER_NAME,
    .item_name = bufferwriter_get_name,
};

static void buffer_w8(AVTextWriterContext *wctx, int b)
{
    BufferWriterContext *ctx = wctx->priv;
    av_bprintf(ctx->buffer, "%c", b);
}

static void buffer_put_str(AVTextWriterContext *wctx, const char *str)
{
    BufferWriterContext *ctx = wctx->priv;
    av_bprintf(ctx->buffer, "%s", str);
}

static void buffer_vprintf(AVTextWriterContext *wctx, const char *fmt, va_list vl)
{
    BufferWriterContext *ctx = wctx->priv;

    av_vbprintf(ctx->buffer, fmt, vl);
}


const AVTextWriter avtextwriter_buffer = {
    .name                 = WRITER_NAME,
    .priv_size            = sizeof(BufferWriterContext),
    .priv_class           = &bufferwriter_class,
    .writer_put_str       = buffer_put_str,
    .writer_vprintf       = buffer_vprintf,
    .writer_w8            = buffer_w8
};

int avtextwriter_create_buffer(AVTextWriterContext **pwctx, AVBPrint *buffer)
{
    BufferWriterContext *ctx;
    int ret;

    ret = avtextwriter_context_open(pwctx, &avtextwriter_buffer);
    if (ret < 0)
        return ret;

    ctx = (*pwctx)->priv;
    ctx->buffer = buffer;

    return ret;
}
