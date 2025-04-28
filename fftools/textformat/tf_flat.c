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
#include "libavutil/error.h"
#include "libavutil/opt.h"
#include "tf_internal.h"

/* Flat output */

typedef struct FlatContext {
    const AVClass *class;
    const char *sep_str;
    char sep;
    int hierarchical;
} FlatContext;

#undef OFFSET
#define OFFSET(x) offsetof(FlatContext, x)

static const AVOption flat_options[] = {
    { "sep_char",     "set separator",                                               OFFSET(sep_str),      AV_OPT_TYPE_STRING, { .str = "." }, 0, 0 },
    { "s",            "set separator",                                               OFFSET(sep_str),      AV_OPT_TYPE_STRING, { .str = "." }, 0, 0 },
    { "hierarchical", "specify if the section specification should be hierarchical", OFFSET(hierarchical), AV_OPT_TYPE_BOOL,   { .i64 = 1   }, 0, 1 },
    { "h",            "specify if the section specification should be hierarchical", OFFSET(hierarchical), AV_OPT_TYPE_BOOL,   { .i64 = 1   }, 0, 1 },
    { NULL },
};

DEFINE_FORMATTER_CLASS(flat);

static av_cold int flat_init(AVTextFormatContext *wctx)
{
    FlatContext *flat = wctx->priv;

    if (strlen(flat->sep_str) != 1) {
        av_log(wctx, AV_LOG_ERROR, "Item separator '%s' specified, but must contain a single character\n",
               flat->sep_str);
        return AVERROR(EINVAL);
    }
    flat->sep = flat->sep_str[0];

    return 0;
}

static const char *flat_escape_key_str(AVBPrint *dst, const char *src, const char sep)
{
    const char *p;

    for (p = src; *p; p++) {
        if (!((*p >= '0' && *p <= '9') ||
              (*p >= 'a' && *p <= 'z') ||
              (*p >= 'A' && *p <= 'Z')))
            av_bprint_chars(dst, '_', 1);
        else
            av_bprint_chars(dst, *p, 1);
    }
    return dst->str;
}

static const char *flat_escape_value_str(AVBPrint *dst, const char *src)
{
    const char *p;

    for (p = src; *p; p++) {
        switch (*p) {
        case '\n': av_bprintf(dst, "%s", "\\n");  break;
        case '\r': av_bprintf(dst, "%s", "\\r");  break;
        case '\\': av_bprintf(dst, "%s", "\\\\"); break;
        case '"':  av_bprintf(dst, "%s", "\\\""); break;
        case '`':  av_bprintf(dst, "%s", "\\`");  break;
        case '$':  av_bprintf(dst, "%s", "\\$");  break;
        default:   av_bprint_chars(dst, *p, 1);   break;
        }
    }
    return dst->str;
}

static void flat_print_section_header(AVTextFormatContext *wctx, const void *data)
{
    FlatContext *flat = wctx->priv;
    AVBPrint *buf = &wctx->section_pbuf[wctx->level];
    const AVTextFormatSection *section = tf_get_section(wctx, wctx->level);
    const AVTextFormatSection *parent_section = tf_get_parent_section(wctx, wctx->level);

    if (!section)
        return;

    /* build section header */
    av_bprint_clear(buf);
    if (!parent_section)
        return;

    av_bprintf(buf, "%s", wctx->section_pbuf[wctx->level - 1].str);

    if (flat->hierarchical ||
        !(section->flags & (AV_TEXTFORMAT_SECTION_FLAG_IS_ARRAY | AV_TEXTFORMAT_SECTION_FLAG_IS_WRAPPER))) {
        av_bprintf(buf, "%s%s", wctx->section[wctx->level]->name, flat->sep_str);

        if (parent_section->flags & AV_TEXTFORMAT_SECTION_FLAG_IS_ARRAY) {
            int n = parent_section->flags & AV_TEXTFORMAT_SECTION_FLAG_NUMBERING_BY_TYPE
                ? wctx->nb_item_type[wctx->level - 1][section->id]
                : wctx->nb_item[wctx->level - 1];

            av_bprintf(buf, "%d%s", n, flat->sep_str);
        }
    }
}

static void flat_print_int(AVTextFormatContext *wctx, const char *key, int64_t value)
{
    writer_printf(wctx, "%s%s=%"PRId64"\n", wctx->section_pbuf[wctx->level].str, key, value);
}

static void flat_print_str(AVTextFormatContext *wctx, const char *key, const char *value)
{
    FlatContext *flat = wctx->priv;
    AVBPrint buf;

    writer_put_str(wctx, wctx->section_pbuf[wctx->level].str);
    av_bprint_init(&buf, 1, AV_BPRINT_SIZE_UNLIMITED);
    writer_printf(wctx, "%s=", flat_escape_key_str(&buf, key, flat->sep));
    av_bprint_clear(&buf);
    writer_printf(wctx, "\"%s\"\n", flat_escape_value_str(&buf, value));
    av_bprint_finalize(&buf, NULL);
}

const AVTextFormatter avtextformatter_flat = {
    .name                  = "flat",
    .priv_size             = sizeof(FlatContext),
    .init                  = flat_init,
    .print_section_header  = flat_print_section_header,
    .print_integer         = flat_print_int,
    .print_string          = flat_print_str,
    .flags = AV_TEXTFORMAT_FLAG_SUPPORTS_OPTIONAL_FIELDS | AV_TEXTFORMAT_FLAG_SUPPORTS_MIXED_ARRAY_CONTENT,
    .priv_class            = &flat_class,
};
