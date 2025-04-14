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
#include <string.h>

#include "avtextwriters.h"

#include "libavutil/error.h"

/* AVIO Writer */

# define WRITER_NAME "aviowriter"

typedef struct IOWriterContext {
    const AVClass *class;
    AVIOContext *avio_context;
    int close_on_uninit;
} IOWriterContext;

static av_cold void iowriter_uninit(AVTextWriterContext *wctx)
{
    IOWriterContext *ctx = wctx->priv;

    if (ctx->close_on_uninit)
        avio_closep(&ctx->avio_context);
}

static void io_w8(AVTextWriterContext *wctx, int b)
{
    IOWriterContext *ctx = wctx->priv;
    avio_w8(ctx->avio_context, b);
}

static void io_put_str(AVTextWriterContext *wctx, const char *str)
{
    IOWriterContext *ctx = wctx->priv;
    avio_write(ctx->avio_context, str, strlen(str));
}

static void io_printf(AVTextWriterContext *wctx, const char *fmt, ...)
{
    IOWriterContext *ctx = wctx->priv;
    va_list ap;

    va_start(ap, fmt);
    avio_vprintf(ctx->avio_context, fmt, ap);
    va_end(ap);
}


const AVTextWriter avtextwriter_avio = {
    .name                 = WRITER_NAME,
    .priv_size            = sizeof(IOWriterContext),
    .uninit               = iowriter_uninit,
    .writer_put_str       = io_put_str,
    .writer_printf        = io_printf,
    .writer_w8            = io_w8
};

int avtextwriter_create_file(AVTextWriterContext **pwctx, const char *output_filename)
{
    IOWriterContext *ctx;
    int ret;


    ret = avtextwriter_context_open(pwctx, &avtextwriter_avio);
    if (ret < 0)
        return ret;

    ctx = (*pwctx)->priv;

    if ((ret = avio_open(&ctx->avio_context, output_filename, AVIO_FLAG_WRITE)) < 0) {
        av_log(ctx, AV_LOG_ERROR,
               "Failed to open output '%s' with error: %s\n", output_filename, av_err2str(ret));
        avtextwriter_context_close(pwctx);
        return ret;
    }

    ctx->close_on_uninit = 1;

    return ret;
}


int avtextwriter_create_avio(AVTextWriterContext **pwctx, AVIOContext *avio_ctx, int close_on_uninit)
{
    IOWriterContext *ctx;
    int ret;

    ret = avtextwriter_context_open(pwctx, &avtextwriter_avio);
    if (ret < 0)
        return ret;

    ctx = (*pwctx)->priv;
    ctx->avio_context = avio_ctx;
    ctx->close_on_uninit = close_on_uninit;

    return ret;
}
