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

#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <string.h>

#include "avtextformat.h"
#include "libavutil/avutil.h"
#include "libavutil/bprint.h"
#include "libavutil/error.h"
#include "libavutil/opt.h"
#include "tf_internal.h"


/* Compact output */

/**
 * Apply C-language-like string escaping.
 */
static const char *c_escape_str(AVBPrint *dst, const char *src, const char sep, void *log_ctx)
{
    const char *p;

    for (p = src; *p; p++) {
        switch (*p) {
        case '\b': av_bprintf(dst, "%s", "\\b"); break;
        case '\f': av_bprintf(dst, "%s", "\\f"); break;
        case '\n': av_bprintf(dst, "%s", "\\n"); break;
        case '\r': av_bprintf(dst, "%s", "\\r"); break;
        case '\\': av_bprintf(dst, "%s", "\\\\"); break;
        default:
            if (*p == sep)
                av_bprint_chars(dst, '\\', 1);
            av_bprint_chars(dst, *p, 1);
        }
    }
    return dst->str;
}

/**
 * Quote fields containing special characters, check RFC4180.
 */
static const char *csv_escape_str(AVBPrint *dst, const char *src, const char sep, void *log_ctx)
{
    char meta_chars[] = { sep, '"', '\n', '\r', '\0' };

    int needs_quoting = !!src[strcspn(src, meta_chars)];

    if (needs_quoting)
        av_bprint_chars(dst, '"', 1);

    for (; *src; src++) {
        if (*src == '"')
            av_bprint_chars(dst, '"', 1);
        av_bprint_chars(dst, *src, 1);
    }
    if (needs_quoting)
        av_bprint_chars(dst, '"', 1);
    return dst->str;
}

static const char *none_escape_str(AVBPrint *dst, const char *src, const char sep, void *log_ctx)
{
    return src;
}

typedef struct CompactContext {
    const AVClass *class;
    char *item_sep_str;
    char item_sep;
    int nokey;
    int print_section;
    char *escape_mode_str;
    const char * (*escape_str)(AVBPrint *dst, const char *src, const char sep, void *log_ctx);
    /** level of the row whose line the fields at this level belong to, -1 for none */
    int row_level[SECTION_MAX_NB_LEVELS];
    /** number of fields written to the row at this level */
    int nb_fields[SECTION_MAX_NB_LEVELS];
    /** the line of the row at this level */
    AVBPrint line[SECTION_MAX_NB_LEVELS];
    /** the lines of the rows nested in the row at this level */
    AVBPrint rows[SECTION_MAX_NB_LEVELS];
} CompactContext;

#undef OFFSET
#define OFFSET(x) offsetof(CompactContext, x)

static const AVOption compact_options[] = {
    { "item_sep", "set item separator",      OFFSET(item_sep_str),    AV_OPT_TYPE_STRING, { .str = "|" },  0, 0 },
    { "s",        "set item separator",      OFFSET(item_sep_str),    AV_OPT_TYPE_STRING, { .str = "|" },  0, 0 },
    { "nokey",    "force no key printing",   OFFSET(nokey),           AV_OPT_TYPE_BOOL,   { .i64 = 0   },  0, 1 },
    { "nk",       "force no key printing",   OFFSET(nokey),           AV_OPT_TYPE_BOOL,   { .i64 = 0   },  0, 1 },
    { "escape",   "set escape mode",         OFFSET(escape_mode_str), AV_OPT_TYPE_STRING, { .str = "c" },  0, 0 },
    { "e",        "set escape mode",         OFFSET(escape_mode_str), AV_OPT_TYPE_STRING, { .str = "c" },  0, 0 },
    { "print_section", "print section name", OFFSET(print_section),   AV_OPT_TYPE_BOOL,   { .i64 = 1   },  0, 1 },
    { "p",             "print section name", OFFSET(print_section),   AV_OPT_TYPE_BOOL,   { .i64 = 1   },  0, 1 },
    { NULL },
};

DEFINE_FORMATTER_CLASS(compact);

static av_cold int compact_init(AVTextFormatContext *wctx)
{
    CompactContext *compact = wctx->priv;

    for (int i = 0; i < SECTION_MAX_NB_LEVELS; i++) {
        av_bprint_init(&compact->line[i], 1, AV_BPRINT_SIZE_UNLIMITED);
        av_bprint_init(&compact->rows[i], 1, AV_BPRINT_SIZE_UNLIMITED);
    }

    if (strlen(compact->item_sep_str) != 1) {
        av_log(wctx, AV_LOG_ERROR, "Item separator '%s' specified, but must contain a single character\n",
               compact->item_sep_str);
        return AVERROR(EINVAL);
    }
    compact->item_sep = compact->item_sep_str[0];

    if        (!strcmp(compact->escape_mode_str, "none")) {
        compact->escape_str = none_escape_str;
    } else if (!strcmp(compact->escape_mode_str, "c"   )) {
        compact->escape_str = c_escape_str;
    } else if (!strcmp(compact->escape_mode_str, "csv" )) {
        compact->escape_str = csv_escape_str;
    } else {
        av_log(wctx, AV_LOG_ERROR, "Unknown escape mode '%s'\n", compact->escape_mode_str);
        return AVERROR(EINVAL);
    }

    return 0;
}

static av_cold int compact_uninit(AVTextFormatContext *wctx)
{
    CompactContext *compact = wctx->priv;

    for (int i = 0; i < SECTION_MAX_NB_LEVELS; i++) {
        av_bprint_finalize(&compact->line[i], NULL);
        av_bprint_finalize(&compact->rows[i], NULL);
    }

    return 0;
}

static void bprint_append_bprint(AVBPrint *dst, const AVBPrint *src)
{
    av_bprint_append_data(dst, src->str, FFMIN(src->len, src->size - 1));
}

static void compact_print_section_header(AVTextFormatContext *wctx, const void *data)
{
    CompactContext *compact = wctx->priv;
    const AVTextFormatSection *section = tf_get_section(wctx, wctx->level);
    const AVTextFormatSection *parent_section = tf_get_parent_section(wctx, wctx->level);
    int level = wctx->level;

    if (!section)
        return;

    av_bprint_clear(&wctx->section_pbuf[level]);
    compact->row_level[level] = level ? compact->row_level[level - 1] : -1;

    if (parent_section &&
        (section->flags & AV_TEXTFORMAT_SECTION_FLAG_HAS_TYPE ||
            (!(section->flags & AV_TEXTFORMAT_SECTION_FLAG_IS_ARRAY) &&
                !(parent_section->flags & (AV_TEXTFORMAT_SECTION_FLAG_IS_WRAPPER | AV_TEXTFORMAT_SECTION_FLAG_IS_ARRAY))))) {

        /* define a prefix for elements not contained in an array or
           in a wrapper, or for array elements with a type, their
           fields continue the line of the enclosing row */
        const char *element_name = (char *)av_x_if_null(section->element_name, section->name);
        AVBPrint *section_pbuf = &wctx->section_pbuf[level];

        av_bprintf(section_pbuf, "%s%s",
                   wctx->section_pbuf[level - 1].str, element_name);

        if (section->flags & AV_TEXTFORMAT_SECTION_FLAG_HAS_TYPE) {
            // add /TYPE to prefix
            av_bprint_chars(section_pbuf, '/', 1);

            // normalize section type, replace special characters and lower case
            for (const char *p = section->get_type(data); *p; p++) {
                char c =
                    (*p >= '0' && *p <= '9') ||
                    (*p >= 'a' && *p <= 'z') ||
                    (*p >= 'A' && *p <= 'Z') ? av_tolower(*p) : '_';
                av_bprint_chars(section_pbuf, c, 1);
            }
        }
        av_bprint_chars(section_pbuf, ':', 1);
        return;
    }

    if (section->flags & (AV_TEXTFORMAT_SECTION_FLAG_IS_WRAPPER | AV_TEXTFORMAT_SECTION_FLAG_IS_ARRAY))
        return;

    /* a row, printed as one line once the section ends */
    compact->row_level[level] = level;
    compact->nb_fields[level] = 0;
    av_bprint_clear(&compact->line[level]);
    av_bprint_clear(&compact->rows[level]);
    if (compact->print_section)
        av_bprintf(&compact->line[level], "%s", section->name);
}

static void compact_print_section_footer(AVTextFormatContext *wctx)
{
    CompactContext *compact = wctx->priv;
    const AVTextFormatSection *section = tf_get_section(wctx, wctx->level);
    int level = wctx->level;
    int parent_row;

    if (!section || compact->row_level[level] != level)
        return;

    /* the row's line, followed by the lines of the rows nested in it */
    av_bprint_chars(&compact->line[level], '\n', 1);
    bprint_append_bprint(&compact->line[level], &compact->rows[level]);

    parent_row = level ? compact->row_level[level - 1] : -1;
    if (parent_row >= 0)
        bprint_append_bprint(&compact->rows[parent_row], &compact->line[level]);
    else
        writer_put_str(wctx, compact->line[level].str);
}

static void compact_print_field(AVTextFormatContext *wctx, const char *key, const char *value)
{
    CompactContext *compact = wctx->priv;
    int row = compact->row_level[wctx->level];
    AVBPrint tmp, *line = &tmp;

    if (row >= 0) {
        line = &compact->line[row];
        if (compact->nb_fields[row]++ || compact->print_section)
            av_bprint_chars(line, compact->item_sep, 1);
    } else {
        /* a field outside of any row */
        av_bprint_init(&tmp, 1, AV_BPRINT_SIZE_UNLIMITED);
    }

    if (!compact->nokey)
        av_bprintf(line, "%s%s=", wctx->section_pbuf[wctx->level].str, key);
    av_bprintf(line, "%s", value);

    if (line == &tmp) {
        writer_put_str(wctx, tmp.str);
        av_bprint_finalize(&tmp, NULL);
    }
}

static void compact_print_str(AVTextFormatContext *wctx, const char *key, const char *value)
{
    CompactContext *compact = wctx->priv;
    AVBPrint buf;

    av_bprint_init(&buf, 1, AV_BPRINT_SIZE_UNLIMITED);
    compact_print_field(wctx, key, compact->escape_str(&buf, value, compact->item_sep, wctx));
    av_bprint_finalize(&buf, NULL);
}

static void compact_print_int(AVTextFormatContext *wctx, const char *key, int64_t value)
{
    char buf[32];

    snprintf(buf, sizeof(buf), "%"PRId64, value);
    compact_print_field(wctx, key, buf);
}

const AVTextFormatter avtextformatter_compact = {
    .name                 = "compact",
    .priv_size            = sizeof(CompactContext),
    .init                 = compact_init,
    .uninit               = compact_uninit,
    .print_section_header = compact_print_section_header,
    .print_section_footer = compact_print_section_footer,
    .print_integer        = compact_print_int,
    .print_string         = compact_print_str,
    .flags = AV_TEXTFORMAT_FLAG_SUPPORTS_OPTIONAL_FIELDS,
    .priv_class           = &compact_class,
};

/* CSV output */

#undef OFFSET
#define OFFSET(x) offsetof(CompactContext, x)

static const AVOption csv_options[] = {
    { "item_sep", "set item separator",      OFFSET(item_sep_str),    AV_OPT_TYPE_STRING, { .str = ","   }, 0, 0 },
    { "s",        "set item separator",      OFFSET(item_sep_str),    AV_OPT_TYPE_STRING, { .str = ","   }, 0, 0 },
    { "nokey",    "force no key printing",   OFFSET(nokey),           AV_OPT_TYPE_BOOL,   { .i64 = 1     }, 0, 1 },
    { "nk",       "force no key printing",   OFFSET(nokey),           AV_OPT_TYPE_BOOL,   { .i64 = 1     }, 0, 1 },
    { "escape",   "set escape mode",         OFFSET(escape_mode_str), AV_OPT_TYPE_STRING, { .str = "csv" }, 0, 0 },
    { "e",        "set escape mode",         OFFSET(escape_mode_str), AV_OPT_TYPE_STRING, { .str = "csv" }, 0, 0 },
    { "print_section", "print section name", OFFSET(print_section),   AV_OPT_TYPE_BOOL,   { .i64 = 1     }, 0, 1 },
    { "p",             "print section name", OFFSET(print_section),   AV_OPT_TYPE_BOOL,   { .i64 = 1     }, 0, 1 },
    { NULL },
};

DEFINE_FORMATTER_CLASS(csv);

const AVTextFormatter avtextformatter_csv = {
    .name                 = "csv",
    .priv_size            = sizeof(CompactContext),
    .init                 = compact_init,
    .uninit               = compact_uninit,
    .print_section_header = compact_print_section_header,
    .print_section_footer = compact_print_section_footer,
    .print_integer        = compact_print_int,
    .print_string         = compact_print_str,
    .flags = AV_TEXTFORMAT_FLAG_SUPPORTS_OPTIONAL_FIELDS,
    .priv_class           = &csv_class,
};
