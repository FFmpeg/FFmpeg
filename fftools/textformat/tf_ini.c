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

/* INI format output */

typedef struct INIContext {
    const AVClass *class;
    int hierarchical;
} INIContext;

#undef OFFSET
#define OFFSET(x) offsetof(INIContext, x)

static const AVOption ini_options[] = {
    { "hierarchical", "specify if the section specification should be hierarchical", OFFSET(hierarchical), AV_OPT_TYPE_BOOL, { .i64 = 1 }, 0, 1 },
    { "h",            "specify if the section specification should be hierarchical", OFFSET(hierarchical), AV_OPT_TYPE_BOOL, { .i64 = 1 }, 0, 1 },
    { NULL },
};

DEFINE_FORMATTER_CLASS(ini);

static char *ini_escape_str(AVBPrint *dst, const char *src)
{
    int i = 0;
    char c;

    while ((c = src[i++])) {
        switch (c) {
        case '\b': av_bprintf(dst, "%s", "\\b"); break;
        case '\f': av_bprintf(dst, "%s", "\\f"); break;
        case '\n': av_bprintf(dst, "%s", "\\n"); break;
        case '\r': av_bprintf(dst, "%s", "\\r"); break;
        case '\t': av_bprintf(dst, "%s", "\\t"); break;
        case '\\':
        case '#':
        case '=':
        case ':':
            av_bprint_chars(dst, '\\', 1);
            /* fallthrough */
        default:
            if ((unsigned char)c < 32)
                av_bprintf(dst, "\\x00%02x", (unsigned char)c);
            else
                av_bprint_chars(dst, c, 1);
            break;
        }
    }
    return dst->str;
}

static void ini_print_section_header(AVTextFormatContext *wctx, const void *data)
{
    INIContext *ini = wctx->priv;
    AVBPrint *buf = &wctx->section_pbuf[wctx->level];
    const AVTextFormatSection *section = tf_get_section(wctx, wctx->level);
    const AVTextFormatSection *parent_section = tf_get_parent_section(wctx, wctx->level);

    if (!section)
        return;

    av_bprint_clear(buf);
    if (!parent_section) {
        writer_put_str(wctx, "# ffprobe output\n\n");
        return;
    }

    if (wctx->nb_item[wctx->level - 1])
        writer_w8(wctx, '\n');

    av_bprintf(buf, "%s", wctx->section_pbuf[wctx->level - 1].str);
    if (ini->hierarchical ||
        !(section->flags & (AV_TEXTFORMAT_SECTION_FLAG_IS_ARRAY | AV_TEXTFORMAT_SECTION_FLAG_IS_WRAPPER))) {
        av_bprintf(buf, "%s%s", buf->str[0] ? "." : "", wctx->section[wctx->level]->name);

        if (parent_section->flags & AV_TEXTFORMAT_SECTION_FLAG_IS_ARRAY) {
            unsigned n = parent_section->flags & AV_TEXTFORMAT_SECTION_FLAG_NUMBERING_BY_TYPE
                ? wctx->nb_item_type[wctx->level - 1][section->id]
                : wctx->nb_item[wctx->level - 1];
            av_bprintf(buf, ".%u", n);
        }
    }

    if (!(section->flags & (AV_TEXTFORMAT_SECTION_FLAG_IS_ARRAY | AV_TEXTFORMAT_SECTION_FLAG_IS_WRAPPER)))
        writer_printf(wctx, "[%s]\n", buf->str);
}

static void ini_print_str(AVTextFormatContext *wctx, const char *key, const char *value)
{
    AVBPrint buf;

    av_bprint_init(&buf, 1, AV_BPRINT_SIZE_UNLIMITED);
    writer_printf(wctx, "%s=", ini_escape_str(&buf, key));
    av_bprint_clear(&buf);
    writer_printf(wctx, "%s\n", ini_escape_str(&buf, value));
    av_bprint_finalize(&buf, NULL);
}

static void ini_print_int(AVTextFormatContext *wctx, const char *key, int64_t value)
{
    writer_printf(wctx, "%s=%"PRId64"\n", key, value);
}

const AVTextFormatter avtextformatter_ini = {
    .name                  = "ini",
    .priv_size             = sizeof(INIContext),
    .print_section_header  = ini_print_section_header,
    .print_integer         = ini_print_int,
    .print_string          = ini_print_str,
    .flags = AV_TEXTFORMAT_FLAG_SUPPORTS_OPTIONAL_FIELDS | AV_TEXTFORMAT_FLAG_SUPPORTS_MIXED_ARRAY_CONTENT,
    .priv_class            = &ini_class,
};
