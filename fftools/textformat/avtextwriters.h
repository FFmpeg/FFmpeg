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

#ifndef FFTOOLS_TEXTFORMAT_AVTEXTWRITERS_H
#define FFTOOLS_TEXTFORMAT_AVTEXTWRITERS_H

#include <stdint.h>
#include "libavformat/avio.h"
#include "libavutil/bprint.h"

typedef struct AVTextWriterContext AVTextWriterContext;

typedef struct AVTextWriter {
    const AVClass *priv_class;      ///< private class of the writer, if any
    int priv_size;                  ///< private size for the writer private class
    const char *name;

    int (*init)(AVTextWriterContext *wctx);
    int (*uninit)(AVTextWriterContext *wctx);
    void (*writer_w8)(AVTextWriterContext *wctx, int b);
    void (*writer_put_str)(AVTextWriterContext *wctx, const char *str);
    void (*writer_vprintf)(AVTextWriterContext *wctx, const char *fmt, va_list vl);
} AVTextWriter;

typedef struct AVTextWriterContext {
    const AVClass *class;            ///< class of the writer
    const AVTextWriter *writer;
    const char *name;
    void *priv;                     ///< private data for use by the writer
} AVTextWriterContext;


int avtextwriter_context_open(AVTextWriterContext **pwctx, const AVTextWriter *writer);

int avtextwriter_context_close(AVTextWriterContext **pwctx);

int avtextwriter_create_stdout(AVTextWriterContext **pwctx);

int avtextwriter_create_avio(AVTextWriterContext **pwctx, AVIOContext *avio_ctx, int close_on_uninit);

int avtextwriter_create_file(AVTextWriterContext **pwctx, const char *output_filename);

int avtextwriter_create_buffer(AVTextWriterContext **pwctx, AVBPrint *buffer);

#endif /* FFTOOLS_TEXTFORMAT_AVTEXTWRITERS_H */
