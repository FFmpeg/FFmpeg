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

/**
 * @file
 * Internal utilities for text formatters.
 */

#ifndef FFTOOLS_TEXTFORMAT_TF_INTERNAL_H
#define FFTOOLS_TEXTFORMAT_TF_INTERNAL_H

#include "avtextformat.h"

#define DEFINE_FORMATTER_CLASS(name)                \
static const AVClass name##_class = {               \
    .class_name = #name,                            \
    .item_name  = av_default_item_name,             \
    .option     = name##_options                    \
}


/**
 * Safely validate and access a section at a given level
 */
static inline const AVTextFormatSection *tf_get_section(AVTextFormatContext *tfc, int level)
{
    if (!tfc || level < 0 || level >= SECTION_MAX_NB_LEVELS || !tfc->section[level]) {
        if (tfc)
            av_log(tfc, AV_LOG_ERROR, "Invalid section access at level %d\n", level);
        return NULL;
    }
    return tfc->section[level];
}

/**
 * Safely access the parent section
 */
static inline const AVTextFormatSection *tf_get_parent_section(AVTextFormatContext *tfc, int level)
{
    if (level <= 0)
        return NULL;

    return tf_get_section(tfc, level - 1);
}

static inline void writer_w8(AVTextFormatContext *wctx, int b)
{
    wctx->writer->writer->writer_w8(wctx->writer, b);
}

static inline void writer_put_str(AVTextFormatContext *wctx, const char *str)
{
    wctx->writer->writer->writer_put_str(wctx->writer, str);
}

static inline void writer_printf(AVTextFormatContext *wctx, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    wctx->writer->writer->writer_vprintf(wctx->writer, fmt, args);
    va_end(args);
}

#endif /* FFTOOLS_TEXTFORMAT_TF_INTERNAL_H */
