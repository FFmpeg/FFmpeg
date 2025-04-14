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

#define writer_w8(wctx_, b_) (wctx_)->writer->writer->writer_w8((wctx_)->writer, b_)
#define writer_put_str(wctx_, str_) (wctx_)->writer->writer->writer_put_str((wctx_)->writer, str_)
#define writer_printf(wctx_, fmt_, ...) (wctx_)->writer->writer->writer_printf((wctx_)->writer, fmt_, __VA_ARGS__)

#define DEFINE_FORMATTER_CLASS(name)                   \
static const char *name##_get_name(void *ctx)       \
{                                                   \
    return #name ;                                  \
}                                                   \
static const AVClass name##_class = {               \
    .class_name = #name,                            \
    .item_name  = name##_get_name,                  \
    .option     = name##_options                    \
}


/* JSON output */

typedef struct JSONContext {
    const AVClass *class;
    int indent_level;
    int compact;
    const char *item_sep, *item_start_end;
} JSONContext;

#undef OFFSET
#define OFFSET(x) offsetof(JSONContext, x)

static const AVOption json_options[]= {
    { "compact", "enable compact output", OFFSET(compact), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1 },
    { "c",       "enable compact output", OFFSET(compact), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1 },
    { NULL }
};

DEFINE_FORMATTER_CLASS(json);

static av_cold int json_init(AVTextFormatContext *wctx)
{
    JSONContext *json = wctx->priv;

    json->item_sep       = json->compact ? ", " : ",\n";
    json->item_start_end = json->compact ? " "  : "\n";

    return 0;
}

static const char *json_escape_str(AVBPrint *dst, const char *src, void *log_ctx)
{
    static const char json_escape[] = {'"', '\\', '\b', '\f', '\n', '\r', '\t', 0};
    static const char json_subst[]  = {'"', '\\',  'b',  'f',  'n',  'r',  't', 0};
    const char *p;

    for (p = src; *p; p++) {
        char *s = strchr(json_escape, *p);
        if (s) {
            av_bprint_chars(dst, '\\', 1);
            av_bprint_chars(dst, json_subst[s - json_escape], 1);
        } else if ((unsigned char)*p < 32) {
            av_bprintf(dst, "\\u00%02x", *p & 0xff);
        } else {
            av_bprint_chars(dst, *p, 1);
        }
    }
    return dst->str;
}

#define JSON_INDENT() writer_printf(wctx, "%*c", json->indent_level * 4, ' ')

static void json_print_section_header(AVTextFormatContext *wctx, const void *data)
{
    JSONContext *json = wctx->priv;
    AVBPrint buf;
    const struct AVTextFormatSection *section = wctx->section[wctx->level];
    const struct AVTextFormatSection *parent_section = wctx->level ?
        wctx->section[wctx->level-1] : NULL;

    if (wctx->level && wctx->nb_item[wctx->level-1])
        writer_put_str(wctx, ",\n");

    if (section->flags & AV_TEXTFORMAT_SECTION_FLAG_IS_WRAPPER) {
        writer_put_str(wctx, "{\n");
        json->indent_level++;
    } else {
        av_bprint_init(&buf, 1, AV_BPRINT_SIZE_UNLIMITED);
        json_escape_str(&buf, section->name, wctx);
        JSON_INDENT();

        json->indent_level++;
        if (section->flags & AV_TEXTFORMAT_SECTION_FLAG_IS_ARRAY) {
            writer_printf(wctx, "\"%s\": [\n", buf.str);
        } else if (parent_section && !(parent_section->flags & AV_TEXTFORMAT_SECTION_FLAG_IS_ARRAY)) {
            writer_printf(wctx, "\"%s\": {%s", buf.str, json->item_start_end);
        } else {
            writer_printf(wctx, "{%s", json->item_start_end);

            /* this is required so the parser can distinguish between packets and frames */
            if (parent_section && parent_section->flags & AV_TEXTFORMAT_SECTION_FLAG_NUMBERING_BY_TYPE) {
                if (!json->compact)
                    JSON_INDENT();
                writer_printf(wctx, "\"type\": \"%s\"", section->name);
                wctx->nb_item[wctx->level]++;
            }
        }
        av_bprint_finalize(&buf, NULL);
    }
}

static void json_print_section_footer(AVTextFormatContext *wctx)
{
    JSONContext *json = wctx->priv;
    const struct AVTextFormatSection *section = wctx->section[wctx->level];

    if (wctx->level == 0) {
        json->indent_level--;
        writer_put_str(wctx, "\n}\n");
    } else if (section->flags & AV_TEXTFORMAT_SECTION_FLAG_IS_ARRAY) {
        writer_w8(wctx, '\n');
        json->indent_level--;
        JSON_INDENT();
        writer_w8(wctx, ']');
    } else {
        writer_put_str(wctx, json->item_start_end);
        json->indent_level--;
        if (!json->compact)
            JSON_INDENT();
        writer_w8(wctx, '}');
    }
}

static inline void json_print_item_str(AVTextFormatContext *wctx,
                                       const char *key, const char *value)
{
    AVBPrint buf;

    av_bprint_init(&buf, 1, AV_BPRINT_SIZE_UNLIMITED);
    writer_printf(wctx, "\"%s\":", json_escape_str(&buf, key,   wctx));
    av_bprint_clear(&buf);
    writer_printf(wctx, " \"%s\"", json_escape_str(&buf, value, wctx));
    av_bprint_finalize(&buf, NULL);
}

static void json_print_str(AVTextFormatContext *wctx, const char *key, const char *value)
{
    JSONContext *json = wctx->priv;
    const struct AVTextFormatSection *parent_section = wctx->level ?
        wctx->section[wctx->level-1] : NULL;

    if (wctx->nb_item[wctx->level] || (parent_section && parent_section->flags & AV_TEXTFORMAT_SECTION_FLAG_NUMBERING_BY_TYPE))
        writer_put_str(wctx, json->item_sep);
    if (!json->compact)
        JSON_INDENT();
    json_print_item_str(wctx, key, value);
}

static void json_print_int(AVTextFormatContext *wctx, const char *key, int64_t value)
{
    JSONContext *json = wctx->priv;
    const struct AVTextFormatSection *parent_section = wctx->level ?
        wctx->section[wctx->level-1] : NULL;
    AVBPrint buf;

    if (wctx->nb_item[wctx->level] || (parent_section && parent_section->flags & AV_TEXTFORMAT_SECTION_FLAG_NUMBERING_BY_TYPE))
        writer_put_str(wctx, json->item_sep);
    if (!json->compact)
        JSON_INDENT();

    av_bprint_init(&buf, 1, AV_BPRINT_SIZE_UNLIMITED);
    writer_printf(wctx, "\"%s\": %"PRId64, json_escape_str(&buf, key, wctx), value);
    av_bprint_finalize(&buf, NULL);
}

const AVTextFormatter avtextformatter_json = {
    .name                 = "json",
    .priv_size            = sizeof(JSONContext),
    .init                 = json_init,
    .print_section_header = json_print_section_header,
    .print_section_footer = json_print_section_footer,
    .print_integer        = json_print_int,
    .print_string         = json_print_str,
    .flags = AV_TEXTFORMAT_FLAG_SUPPORTS_MIXED_ARRAY_CONTENT,
    .priv_class           = &json_class,
};

