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

#include <stdint.h>
#include <string.h>

#include "avtextformat.h"
#include "libavutil/bprint.h"
#include "libavutil/error.h"
#include "libavutil/opt.h"
#include "tf_internal.h"

/* XML output */

typedef struct XMLContext {
    const AVClass *class;
    int within_tag;
    int indent_level;
    int fully_qualified;
    int xsd_strict;
} XMLContext;

#undef OFFSET
#define OFFSET(x) offsetof(XMLContext, x)

static const AVOption xml_options[] = {
    { "fully_qualified", "specify if the output should be fully qualified", OFFSET(fully_qualified), AV_OPT_TYPE_BOOL, { .i64 = 0 },  0, 1 },
    { "q",               "specify if the output should be fully qualified", OFFSET(fully_qualified), AV_OPT_TYPE_BOOL, { .i64 = 0 },  0, 1 },
    { "xsd_strict",      "ensure that the output is XSD compliant",         OFFSET(xsd_strict),      AV_OPT_TYPE_BOOL, { .i64 = 0 },  0, 1 },
    { "x",               "ensure that the output is XSD compliant",         OFFSET(xsd_strict),      AV_OPT_TYPE_BOOL, { .i64 = 0 },  0, 1 },
    { NULL },
};

DEFINE_FORMATTER_CLASS(xml);

static av_cold int xml_init(AVTextFormatContext *wctx)
{
    XMLContext *xml = wctx->priv;

    if (xml->xsd_strict) {
        xml->fully_qualified = 1;
#define CHECK_COMPLIANCE(opt, opt_name)                                 \
        if (opt) {                                                      \
            av_log(wctx, AV_LOG_ERROR,                                  \
                   "XSD-compliant output selected but option '%s' was selected, XML output may be non-compliant.\n" \
                   "You need to disable such option with '-no%s'\n", opt_name, opt_name); \
            return AVERROR(EINVAL);                                     \
        }
        ////CHECK_COMPLIANCE(show_private_data, "private");
        CHECK_COMPLIANCE(wctx->show_value_unit,   "unit");
        CHECK_COMPLIANCE(wctx->use_value_prefix,  "prefix");
    }

    return 0;
}

#define XML_INDENT() writer_printf(wctx, "%*c", xml->indent_level * 4, ' ')

static void xml_print_section_header(AVTextFormatContext *wctx, const void *data)
{
    XMLContext *xml = wctx->priv;
    const AVTextFormatSection *section = tf_get_section(wctx, wctx->level);
    const AVTextFormatSection *parent_section = tf_get_parent_section(wctx, wctx->level);

    if (!section)
        return;

    if (wctx->level == 0) {
        const char *qual = " xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
            "xmlns:ffprobe=\"http://www.ffmpeg.org/schema/ffprobe\" "
            "xsi:schemaLocation=\"http://www.ffmpeg.org/schema/ffprobe ffprobe.xsd\"";

        writer_put_str(wctx, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
        writer_printf(wctx, "<%sffprobe%s>\n",
                      xml->fully_qualified ? "ffprobe:" : "",
                      xml->fully_qualified ? qual : "");
        return;
    }

    if (xml->within_tag) {
        xml->within_tag = 0;
        writer_put_str(wctx, ">\n");
    }

    if (parent_section && (parent_section->flags & AV_TEXTFORMAT_SECTION_FLAG_IS_WRAPPER) &&
        wctx->level && wctx->nb_item[wctx->level - 1])
        writer_w8(wctx, '\n');
    xml->indent_level++;

    if (section->flags & (AV_TEXTFORMAT_SECTION_FLAG_IS_ARRAY | AV_TEXTFORMAT_SECTION_FLAG_HAS_VARIABLE_FIELDS)) {
        XML_INDENT();
        writer_printf(wctx, "<%s", section->name);

        if (section->flags & AV_TEXTFORMAT_SECTION_FLAG_HAS_TYPE) {
            AVBPrint buf;
            av_bprint_init(&buf, 1, AV_BPRINT_SIZE_UNLIMITED);
            av_bprint_escape(&buf, section->get_type(data), NULL,
                             AV_ESCAPE_MODE_XML, AV_ESCAPE_FLAG_XML_DOUBLE_QUOTES);
            writer_printf(wctx, " type=\"%s\"", buf.str);
        }
        writer_printf(wctx, ">\n", section->name);
    } else {
        XML_INDENT();
        writer_printf(wctx, "<%s ", section->name);
        xml->within_tag = 1;
    }
}

static void xml_print_section_footer(AVTextFormatContext *wctx)
{
    XMLContext *xml = wctx->priv;
    const AVTextFormatSection *section = tf_get_section(wctx, wctx->level);

    if (!section)
        return;

    if (wctx->level == 0) {
        writer_printf(wctx, "</%sffprobe>\n", xml->fully_qualified ? "ffprobe:" : "");
    } else if (xml->within_tag) {
        xml->within_tag = 0;
        writer_put_str(wctx, "/>\n");
        xml->indent_level--;
    } else {
        XML_INDENT();
        writer_printf(wctx, "</%s>\n", section->name);
        xml->indent_level--;
    }
}

static void xml_print_value(AVTextFormatContext *wctx, const char *key,
                            const char *str, int64_t num, const int is_int)
{
    AVBPrint buf;
    XMLContext *xml = wctx->priv;
    const AVTextFormatSection *section = tf_get_section(wctx, wctx->level);

    if (!section)
        return;

    av_bprint_init(&buf, 1, AV_BPRINT_SIZE_UNLIMITED);

    if (section->flags & AV_TEXTFORMAT_SECTION_FLAG_HAS_VARIABLE_FIELDS) {
        xml->indent_level++;
        XML_INDENT();
        av_bprint_escape(&buf, key, NULL,
                         AV_ESCAPE_MODE_XML, AV_ESCAPE_FLAG_XML_DOUBLE_QUOTES);
        writer_printf(wctx, "<%s key=\"%s\"",
                      section->element_name, buf.str);
        av_bprint_clear(&buf);

        if (is_int) {
            writer_printf(wctx, " value=\"%"PRId64"\"/>\n", num);
        } else {
            av_bprint_escape(&buf, str, NULL,
                             AV_ESCAPE_MODE_XML, AV_ESCAPE_FLAG_XML_DOUBLE_QUOTES);
            writer_printf(wctx, " value=\"%s\"/>\n", buf.str);
        }
        xml->indent_level--;
    } else {
        if (wctx->nb_item[wctx->level])
            writer_w8(wctx, ' ');

        if (is_int) {
            writer_printf(wctx, "%s=\"%"PRId64"\"", key, num);
        } else {
            av_bprint_escape(&buf, str, NULL,
                             AV_ESCAPE_MODE_XML, AV_ESCAPE_FLAG_XML_DOUBLE_QUOTES);
            writer_printf(wctx, "%s=\"%s\"", key, buf.str);
        }
    }

    av_bprint_finalize(&buf, NULL);
}

static inline void xml_print_str(AVTextFormatContext *wctx, const char *key, const char *value)
{
    xml_print_value(wctx, key, value, 0, 0);
}

static void xml_print_int(AVTextFormatContext *wctx, const char *key, int64_t value)
{
    xml_print_value(wctx, key, NULL, value, 1);
}

const AVTextFormatter avtextformatter_xml = {
    .name                 = "xml",
    .priv_size            = sizeof(XMLContext),
    .init                 = xml_init,
    .print_section_header = xml_print_section_header,
    .print_section_footer = xml_print_section_footer,
    .print_integer        = xml_print_int,
    .print_string         = xml_print_str,
    .flags = AV_TEXTFORMAT_FLAG_SUPPORTS_MIXED_ARRAY_CONTENT,
    .priv_class           = &xml_class,
};
