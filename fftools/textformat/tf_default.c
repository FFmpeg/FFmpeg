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
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "avtextformat.h"
#include "libavutil/bprint.h"
#include "libavutil/opt.h"
#include "tf_internal.h"

/* Default output */

typedef struct DefaultContext {
    const AVClass *class;
    int nokey;
    int noprint_wrappers;
    int nested_section[SECTION_MAX_NB_LEVELS];
} DefaultContext;

#undef OFFSET
#define OFFSET(x) offsetof(DefaultContext, x)

static const AVOption default_options[] = {
    { "noprint_wrappers", "do not print headers and footers", OFFSET(noprint_wrappers), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1 },
    { "nw",               "do not print headers and footers", OFFSET(noprint_wrappers), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1 },
    { "nokey",            "force no key printing",            OFFSET(nokey),            AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1 },
    { "nk",               "force no key printing",            OFFSET(nokey),            AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1 },
    { NULL },
};

DEFINE_FORMATTER_CLASS(default);

/* lame uppercasing routine, assumes the string is lower case ASCII */
static inline char *upcase_string(char *dst, size_t dst_size, const char *src)
{
    unsigned i;

    for (i = 0; src[i] && i < dst_size - 1; i++)
        dst[i] = (char)av_toupper(src[i]);
    dst[i] = 0;
    return dst;
}

static void default_print_section_header(AVTextFormatContext *wctx, const void *data)
{
    DefaultContext *def = wctx->priv;
    char buf[32];
    const AVTextFormatSection *section = tf_get_section(wctx, wctx->level);
    const AVTextFormatSection *parent_section = tf_get_parent_section(wctx, wctx->level);

    if (!section)
        return;

    av_bprint_clear(&wctx->section_pbuf[wctx->level]);
    if (parent_section &&
        !(parent_section->flags & (AV_TEXTFORMAT_SECTION_FLAG_IS_WRAPPER | AV_TEXTFORMAT_SECTION_FLAG_IS_ARRAY))) {
        def->nested_section[wctx->level] = 1;
        av_bprintf(&wctx->section_pbuf[wctx->level], "%s%s:",
                   wctx->section_pbuf[wctx->level - 1].str,
                   upcase_string(buf, sizeof(buf),
                                 av_x_if_null(section->element_name, section->name)));
    }

    if (def->noprint_wrappers || def->nested_section[wctx->level])
        return;

    if (!(section->flags & (AV_TEXTFORMAT_SECTION_FLAG_IS_WRAPPER | AV_TEXTFORMAT_SECTION_FLAG_IS_ARRAY)))
        writer_printf(wctx, "[%s]\n", upcase_string(buf, sizeof(buf), section->name));
}

static void default_print_section_footer(AVTextFormatContext *wctx)
{
    DefaultContext *def = wctx->priv;
    const AVTextFormatSection *section = tf_get_section(wctx, wctx->level);

    char buf[32];

    if (!section)
        return;

    if (def->noprint_wrappers || def->nested_section[wctx->level])
        return;

    if (!(section->flags & (AV_TEXTFORMAT_SECTION_FLAG_IS_WRAPPER | AV_TEXTFORMAT_SECTION_FLAG_IS_ARRAY)))
        writer_printf(wctx, "[/%s]\n", upcase_string(buf, sizeof(buf), section->name));
}

static void default_print_str(AVTextFormatContext *wctx, const char *key, const char *value)
{
    DefaultContext *def = wctx->priv;

    if (!def->nokey)
        writer_printf(wctx, "%s%s=", wctx->section_pbuf[wctx->level].str, key);
    writer_printf(wctx, "%s\n", value);
}

static void default_print_int(AVTextFormatContext *wctx, const char *key, int64_t value)
{
    DefaultContext *def = wctx->priv;

    if (!def->nokey)
        writer_printf(wctx, "%s%s=", wctx->section_pbuf[wctx->level].str, key);
    writer_printf(wctx, "%"PRId64"\n", value);
}

const AVTextFormatter avtextformatter_default = {
    .name                  = "default",
    .priv_size             = sizeof(DefaultContext),
    .print_section_header  = default_print_section_header,
    .print_section_footer  = default_print_section_footer,
    .print_integer         = default_print_int,
    .print_string          = default_print_str,
    .flags = AV_TEXTFORMAT_FLAG_SUPPORTS_OPTIONAL_FIELDS,
    .priv_class            = &default_class,
};
