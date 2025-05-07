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
#include "libavutil/avassert.h"

#include "libavutil/error.h"

/* AVIO Writer */

# define WRITER_NAME "aviowriter"

typedef struct IOWriterContext {
    const AVClass *class;
    AVIOContext *avio_context;
    int close_on_uninit;
} IOWriterContext;

static av_cold int iowriter_uninit(AVTextWriterContext *wctx)
{
    IOWriterContext *ctx = wctx->priv;
    int ret = 0;

    if (ctx->close_on_uninit)
        ret = avio_closep(&ctx->avio_context);
    return ret;
}

static void io_w8(AVTextWriterContext *wctx, int b)
{
    IOWriterContext *ctx = wctx->priv;
    avio_w8(ctx->avio_context, b);
}

static void io_put_str(AVTextWriterContext *wctx, const char *str)
{
    IOWriterContext *ctx = wctx->priv;
    avio_write(ctx->avio_context, (const unsigned char *)str, (int)strlen(str));
}

static void io_vprintf(AVTextWriterContext *wctx, const char *fmt, va_list vl)
{
    IOWriterContext *ctx = wctx->priv;

    avio_vprintf(ctx->avio_context, fmt, vl);
}


const AVTextWriter avtextwriter_avio = {
    .name                 = WRITER_NAME,
    .priv_size            = sizeof(IOWriterContext),
    .uninit               = iowriter_uninit,
    .writer_put_str       = io_put_str,
    .writer_vprintf       = io_vprintf,
    .writer_w8            = io_w8
};

int avtextwriter_create_file(AVTextWriterContext **pwctx, const char *output_filename)
{
    IOWriterContext *ctx;
    int ret;

    if (!output_filename || !output_filename[0]) {
        av_log(NULL, AV_LOG_ERROR, "The output_filename cannot be NULL or empty\n");
        return AVERROR(EINVAL);
    }

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

    av_assert0(avio_ctx);

    ret = avtextwriter_context_open(pwctx, &avtextwriter_avio);
    if (ret < 0)
        return ret;

    ctx = (*pwctx)->priv;
    ctx->avio_context = avio_ctx;
    ctx->close_on_uninit = close_on_uninit;

    return ret;
}
