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
#include "tf_internal.h"
#include "tf_mermaid.h"
#include <libavutil/mem.h>
#include <libavutil/avassert.h>
#include <libavutil/bprint.h>
#include <libavutil/opt.h>


static const char *init_directive = ""
    "%%{init: {"
        "\"theme\": \"base\","
        "\"curve\": \"monotoneX\","
        "\"rankSpacing\": 10,"
        "\"nodeSpacing\": 10,"
        "\"themeCSS\": \"__###__\","
        "\"fontFamily\": \"Roboto,Segoe UI,sans-serif\","
        "\"themeVariables\": { "
            "\"clusterBkg\": \"white\", "
            "\"primaryBorderColor\": \"gray\", "
            "\"lineColor\": \"gray\", "
            "\"secondaryTextColor\": \"gray\", "
            "\"tertiaryBorderColor\": \"gray\", "
            "\"primaryTextColor\": \"#666\", "
            "\"secondaryTextColor\": \"red\" "
        "},"
        "\"flowchart\": { "
            "\"subGraphTitleMargin\": { \"top\": -15, \"bottom\": 20 }, "
            "\"diagramPadding\": 20, "
            "\"curve\": \"monotoneX\" "
        "}"
    " }}%%\n\n";

static const char* init_directive_er = ""
    "%%{init: {"
        "\"theme\": \"base\","
        "\"layout\": \"elk\","
        "\"curve\": \"monotoneX\","
        "\"rankSpacing\": 65,"
        "\"nodeSpacing\": 60,"
        "\"themeCSS\": \"__###__\","
        "\"fontFamily\": \"Roboto,Segoe UI,sans-serif\","
        "\"themeVariables\": { "
            "\"clusterBkg\": \"white\", "
            "\"primaryBorderColor\": \"gray\", "
            "\"lineColor\": \"gray\", "
            "\"secondaryTextColor\": \"gray\", "
            "\"tertiaryBorderColor\": \"gray\", "
            "\"primaryTextColor\": \"#666\", "
            "\"secondaryTextColor\": \"red\" "
        "},"
        "\"er\": { "
            "\"diagramPadding\": 12, "
            "\"entityPadding\": 4, "
            "\"minEntityWidth\": 150, "
            "\"minEntityHeight\": 20, "
            "\"curve\": \"monotoneX\" "
        "}"
    " }}%%\n\n";

static const char *theme_css_er = ""

    // Variables
            ".root { "
                "--ff-colvideo: #6eaa7b; "
                "--ff-colaudio: #477fb3; "
                "--ff-colsubtitle: #ad76ab; "
                "--ff-coltext: #666; "
            "} "
            " g.nodes g.node.default rect.basic.label-container, "
            " g.nodes g.node.default path { "
            "     rx: 1; "
            "     ry: 1; "
            "     stroke-width: 1px !important; "
            "     stroke: #e9e9e9 !important; "
            "     fill: url(#ff-filtergradient) !important; "
            "     filter: drop-shadow(0px 0px 5.5px rgba(0, 0, 0, 0.05)); "
            "     fill: white !important; "
            " } "
            "  "
            " .relationshipLine { "
            "     stroke: gray; "
            "     stroke-width: 1; "
            "     fill: none; "
            "     filter: drop-shadow(0px 0px 3px rgba(0, 0, 0, 0.2)); "
            " } "
            "  "
            " g.node.default g.label.name  foreignObject > div > span > p, "
            " g.nodes g.node.default g.label:not(.attribute-name, .attribute-keys, .attribute-type, .attribute-comment) foreignObject > div > span > p { "
            "     font-size: 0.95rem; "
            "     font-weight: 500; "
            "     text-transform: uppercase; "
            "     min-width: 5.5rem; "
            "     margin-bottom: 0.5rem; "
            "      "
            " } "
            "  "
            " .edgePaths path { "
            "     marker-end: none; "
            "     marker-start: none; "
            "  "
            "} ";


/* Mermaid Graph output */

typedef struct MermaidContext {
    const AVClass *class;
    AVDiagramConfig *diagram_config;
    int subgraph_count;
    int within_tag;
    int indent_level;
    int create_html;

    // Options
    int enable_link_colors; // Requires Mermaid 11.5

    struct section_data {
        const char *section_id;
        const char *section_type;
        const char *src_id;
        const char *dest_id;
        AVTextFormatLinkType link_type;
        int current_is_textblock;
        int current_is_stadium;
        int subgraph_start_incomplete;
    }  section_data[SECTION_MAX_NB_LEVELS];

    unsigned nb_link_captions[SECTION_MAX_NB_LEVELS]; ///< generic print buffer dedicated to each section,
    AVBPrint link_buf; ///< print buffer for writing diagram links
    AVDictionary *link_dict;
} MermaidContext;

#undef OFFSET
#define OFFSET(x) offsetof(MermaidContext, x)

static const AVOption mermaid_options[] = {
    { "link_coloring",    "enable colored links (requires Mermaid >= 11.5)",  OFFSET(enable_link_colors), AV_OPT_TYPE_BOOL,   { .i64 = 1 },  0, 1 },
    ////{"diagram_css",      "CSS for the diagram",                              OFFSET(diagram_css),        AV_OPT_TYPE_STRING, {.i64=0},  0, 1 },
    ////{"html_template",    "Template HTML",                                    OFFSET(html_template),      AV_OPT_TYPE_STRING, {.i64=0},  0, 1 },
    { NULL },
};

DEFINE_FORMATTER_CLASS(mermaid);

void av_diagram_init(AVTextFormatContext *tfc, AVDiagramConfig *diagram_config)
{
    MermaidContext *mmc = tfc->priv;
    mmc->diagram_config = diagram_config;
}

static av_cold int has_link_pair(const AVTextFormatContext *tfc, const char *src, const char *dest)
{
    MermaidContext *mmc = tfc->priv;
    AVBPrint buf;

    av_bprint_init(&buf, 0, AV_BPRINT_SIZE_UNLIMITED);
    av_bprintf(&buf, "%s--%s", src, dest);

    if (mmc->link_dict && av_dict_get(mmc->link_dict, buf.str, NULL, 0))
        return 1;

    av_dict_set(&mmc->link_dict, buf.str, buf.str, 0);

    return 0;
}

static av_cold int mermaid_init(AVTextFormatContext *tfc)
{
    MermaidContext *mmc = tfc->priv;

    av_bprint_init(&mmc->link_buf, 0, AV_BPRINT_SIZE_UNLIMITED);

    ////mmc->enable_link_colors = 1; // Requires Mermaid 11.5
    return 0;
}

static av_cold int mermaid_init_html(AVTextFormatContext *tfc)
{
    MermaidContext *mmc = tfc->priv;

    int ret = mermaid_init(tfc);

    if (ret < 0)
        return ret;

    mmc->create_html = 1;

    return 0;
}

static av_cold int mermaid_uninit(AVTextFormatContext *tfc)
{
    MermaidContext *mmc = tfc->priv;

    av_bprint_finalize(&mmc->link_buf, NULL);
    av_dict_free(&mmc->link_dict);

    for (unsigned i = 0; i < SECTION_MAX_NB_LEVELS; i++) {
        av_freep(&mmc->section_data[i].dest_id);
        av_freep(&mmc->section_data[i].section_id);
        av_freep(&mmc->section_data[i].src_id);
        av_freep(&mmc->section_data[i].section_type);
    }

    return 0;
}

static void set_str(const char **dst, const char *src)
{
    if (*dst)
        av_freep(dst);

    if (src)
        *dst = av_strdup(src);
}

#define MM_INDENT() writer_printf(tfc, "%*c", mmc->indent_level * 2, ' ')

static void mermaid_print_section_header(AVTextFormatContext *tfc, const void *data)
{
    const AVTextFormatSection *section = tf_get_section(tfc, tfc->level);
    const AVTextFormatSection *parent_section = tf_get_parent_section(tfc, tfc->level);

    if (!section)
        return;
    AVBPrint *buf = &tfc->section_pbuf[tfc->level];
    MermaidContext *mmc = tfc->priv;
    const AVTextFormatSectionContext *sec_ctx = data;

    if (tfc->level == 0) {
        char *directive;
        AVBPrint css_buf;
        const char *diag_directive = mmc->diagram_config->diagram_type == AV_DIAGRAMTYPE_ENTITYRELATIONSHIP ? init_directive_er : init_directive;
        char *single_line_css = av_strireplace(mmc->diagram_config->diagram_css, "\n", " ");
        (void)theme_css_er;
        ////char *single_line_css = av_strireplace(theme_css_er, "\n", " ");
        av_bprint_init(&css_buf, 0, AV_BPRINT_SIZE_UNLIMITED);
        av_bprint_escape(&css_buf, single_line_css, "'\\", AV_ESCAPE_MODE_BACKSLASH, AV_ESCAPE_FLAG_STRICT);
        av_freep(&single_line_css);

        directive = av_strireplace(diag_directive, "__###__", css_buf.str);
        if (mmc->create_html) {
            uint64_t length;
            char *token_pos = av_stristr(mmc->diagram_config->html_template, "__###__");
            if (!token_pos) {
                av_log(tfc, AV_LOG_ERROR, "Unable to locate the required token (__###__) in the html template.");
                return;
            }

            length = token_pos - mmc->diagram_config->html_template;
            for (uint64_t i = 0; i < length; i++)
                writer_w8(tfc, mmc->diagram_config->html_template[i]);
        }

        writer_put_str(tfc, directive);
        switch (mmc->diagram_config->diagram_type) {
        case AV_DIAGRAMTYPE_GRAPH:
            writer_put_str(tfc, "flowchart LR\n");
        ////writer_put_str(tfc, "  gradient_def@{ shape: text, label: \"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"1\" height=\"1\"><defs><linearGradient id=\"ff-filtergradient\" x1=\"0%\" y1=\"0%\" x2=\"0%\" y2=\"100%\"><stop offset=\"0%\" style=\"stop-color:hsla(0, 0%, 30%, 0.02);\"/><stop offset=\"50%\" style=\"stop-color:hsla(0, 0%, 30%, 0);\"/><stop offset=\"100%\" style=\"stop-color:hsla(0, 0%, 30%, 0.05);\"/></linearGradient></defs></svg>\" }\n");
            writer_put_str(tfc, "  gradient_def@{ shape: text, label: \"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"1\" height=\"1\"><defs><linearGradient id=\"ff-filtergradient\" x1=\"0%\" y1=\"0%\" x2=\"0%\" y2=\"100%\"><stop offset=\"0%\" style=\"stop-color:hsl(0, 0%, 98.6%);     \"/><stop offset=\"50%\" style=\"stop-color:hsl(0, 0%, 100%);   \"/><stop offset=\"100%\" style=\"stop-color:hsl(0, 0%, 96.5%);     \"/></linearGradient><radialGradient id=\"ff-radgradient\" cx=\"50%\" cy=\"50%\" r=\"100%\" fx=\"45%\" fy=\"40%\"><stop offset=\"25%\" stop-color=\"hsl(0, 0%, 100%)\" /><stop offset=\"100%\" stop-color=\"hsl(0, 0%, 96%)\" /></radialGradient></defs></svg>\" }\n");
            break;
        case AV_DIAGRAMTYPE_ENTITYRELATIONSHIP:
            writer_put_str(tfc, "erDiagram\n");
            break;
        }

        av_bprint_finalize(&css_buf, NULL);
        av_freep(&directive);
        return;
    }

    if (parent_section && parent_section->flags & AV_TEXTFORMAT_SECTION_FLAG_IS_SUBGRAPH) {

        struct section_data parent_sec_data = mmc->section_data[tfc->level - 1];
        AVBPrint *parent_buf = &tfc->section_pbuf[tfc->level - 1];

        if (parent_sec_data.subgraph_start_incomplete) {

            if (parent_buf->len > 0)
                writer_printf(tfc, "%s", parent_buf->str);

            writer_put_str(tfc, "</div>\"]\n");

            mmc->section_data[tfc->level - 1].subgraph_start_incomplete = 0;
        }
    }

    av_freep(&mmc->section_data[tfc->level].section_id);
    av_freep(&mmc->section_data[tfc->level].section_type);
    av_freep(&mmc->section_data[tfc->level].src_id);
    av_freep(&mmc->section_data[tfc->level].dest_id);
    mmc->section_data[tfc->level].current_is_textblock = 0;
    mmc->section_data[tfc->level].current_is_stadium = 0;
    mmc->section_data[tfc->level].subgraph_start_incomplete = 0;
    mmc->section_data[tfc->level].link_type = AV_TEXTFORMAT_LINKTYPE_SRCDEST;

    // NOTE: av_strdup() allocations aren't checked
    if (section->flags & AV_TEXTFORMAT_SECTION_FLAG_IS_SUBGRAPH) {

        av_bprint_clear(buf);
        writer_put_str(tfc, "\n");

        mmc->indent_level++;

        if (sec_ctx->context_id) {
            MM_INDENT();
            writer_printf(tfc, "subgraph %s[\"<div class=\"ff-%s\">", sec_ctx->context_id, section->name);
        } else {
            av_log(tfc, AV_LOG_ERROR, "Unable to write subgraph start. Missing id field. Section: %s", section->name);
        }

        mmc->section_data[tfc->level].subgraph_start_incomplete = 1;
        set_str(&mmc->section_data[tfc->level].section_id, sec_ctx->context_id);
    }

    if (section->flags & AV_TEXTFORMAT_SECTION_FLAG_IS_SHAPE) {

        av_bprint_clear(buf);
        writer_put_str(tfc, "\n");

        mmc->indent_level++;

        if (sec_ctx->context_id) {

            set_str(&mmc->section_data[tfc->level].section_id, sec_ctx->context_id);

            switch (mmc->diagram_config->diagram_type) {
            case AV_DIAGRAMTYPE_GRAPH:
                if (sec_ctx->context_flags & 1) {

                    MM_INDENT();
                    writer_printf(tfc, "%s@{ shape: text, label: \"", sec_ctx->context_id);
                    mmc->section_data[tfc->level].current_is_textblock = 1;
                } else if (sec_ctx->context_flags & 2) {

                    MM_INDENT();
                    writer_printf(tfc, "%s([\"", sec_ctx->context_id);
                    mmc->section_data[tfc->level].current_is_stadium = 1;
                } else {
                    MM_INDENT();
                    writer_printf(tfc, "%s(\"", sec_ctx->context_id);
                }

                break;
            case AV_DIAGRAMTYPE_ENTITYRELATIONSHIP:
                MM_INDENT();
                writer_printf(tfc, "%s {\n", sec_ctx->context_id);
                break;
            }

        } else {
            av_log(tfc, AV_LOG_ERROR, "Unable to write shape start. Missing id field. Section: %s", section->name);
        }

        set_str(&mmc->section_data[tfc->level].section_id, sec_ctx->context_id);
    }


    if (section->flags & AV_TEXTFORMAT_SECTION_PRINT_TAGS) {

        if (sec_ctx && sec_ctx->context_type)
            writer_printf(tfc, "<div class=\"ff-%s %s\">", section->name, sec_ctx->context_type);
        else
            writer_printf(tfc, "<div class=\"ff-%s\">", section->name);
    }


    if (section->flags & AV_TEXTFORMAT_SECTION_FLAG_HAS_LINKS) {

        av_bprint_clear(buf);
        mmc->nb_link_captions[tfc->level] = 0;

        if (sec_ctx && sec_ctx->context_type)
            set_str(&mmc->section_data[tfc->level].section_type, sec_ctx->context_type);

        ////if (section->flags & AV_TEXTFORMAT_SECTION_FLAG_HAS_TYPE) {
        ////    AVBPrint buf;
        ////    av_bprint_init(&buf, 1, AV_BPRINT_SIZE_UNLIMITED);
        ////    av_bprint_escape(&buf, section->get_type(data), NULL,
        ////                     AV_ESCAPE_MODE_XML, AV_ESCAPE_FLAG_XML_DOUBLE_QUOTES);
        ////    writer_printf(tfc, " type=\"%s\"", buf.str);
    }
}

static void mermaid_print_section_footer(AVTextFormatContext *tfc)
{
    MermaidContext *mmc = tfc->priv;
    const AVTextFormatSection *section = tf_get_section(tfc, tfc->level);

    if (!section)
        return;
    AVBPrint *buf = &tfc->section_pbuf[tfc->level];
    struct section_data sec_data = mmc->section_data[tfc->level];

    if (section->flags & AV_TEXTFORMAT_SECTION_PRINT_TAGS)
        writer_put_str(tfc, "</div>");

    if (section->flags & AV_TEXTFORMAT_SECTION_FLAG_IS_SHAPE) {

        switch (mmc->diagram_config->diagram_type) {
        case AV_DIAGRAMTYPE_GRAPH:

            if (sec_data.current_is_textblock) {
                writer_printf(tfc, "\"}\n", section->name);

                if (sec_data.section_id) {
                    MM_INDENT();
                    writer_put_str(tfc, "class ");
                    writer_put_str(tfc, sec_data.section_id);
                    writer_put_str(tfc, " ff-");
                    writer_put_str(tfc, section->name);
                    writer_put_str(tfc, "\n");
                }
            } else if (sec_data.current_is_stadium) {
                writer_printf(tfc, "\"]):::ff-%s\n", section->name);
            } else {
                writer_printf(tfc, "\"):::ff-%s\n", section->name);
            }

            break;
        case AV_DIAGRAMTYPE_ENTITYRELATIONSHIP:
            MM_INDENT();
            writer_put_str(tfc, "}\n\n");
            break;
        }

        mmc->indent_level--;

    } else if ((section->flags & AV_TEXTFORMAT_SECTION_FLAG_IS_SUBGRAPH)) {

        MM_INDENT();
        writer_put_str(tfc, "end\n");

        if (sec_data.section_id) {
            MM_INDENT();
            writer_put_str(tfc, "class ");
            writer_put_str(tfc, sec_data.section_id);
            writer_put_str(tfc, " ff-");
            writer_put_str(tfc, section->name);
            writer_put_str(tfc, "\n");
        }

        mmc->indent_level--;
    }

    if ((section->flags & AV_TEXTFORMAT_SECTION_FLAG_HAS_LINKS))
        if (sec_data.src_id && sec_data.dest_id
            && !has_link_pair(tfc, sec_data.src_id, sec_data.dest_id))
            switch (mmc->diagram_config->diagram_type) {
            case AV_DIAGRAMTYPE_GRAPH:

                if (sec_data.section_type && mmc->enable_link_colors)
                    av_bprintf(&mmc->link_buf, "\n  %s %s-%s-%s@==", sec_data.src_id, sec_data.section_type, sec_data.src_id, sec_data.dest_id);
                else
                    av_bprintf(&mmc->link_buf, "\n  %s ==", sec_data.src_id);

                if (buf->len > 0) {
                    av_bprintf(&mmc->link_buf, " \"%s", buf->str);

                    for (unsigned i = 0; i < mmc->nb_link_captions[tfc->level]; i++)
                        av_bprintf(&mmc->link_buf, "<br>&nbsp;");

                    av_bprintf(&mmc->link_buf, "\" ==");
                }

                av_bprintf(&mmc->link_buf, "> %s", sec_data.dest_id);

                break;
            case AV_DIAGRAMTYPE_ENTITYRELATIONSHIP:


                av_bprintf(&mmc->link_buf, "\n  %s", sec_data.src_id);

                switch (sec_data.link_type) {
                case AV_TEXTFORMAT_LINKTYPE_ONETOMANY:
                    av_bprintf(&mmc->link_buf, "%s", " ||--o{ ");
                    break;
                case AV_TEXTFORMAT_LINKTYPE_MANYTOONE:
                    av_bprintf(&mmc->link_buf, "%s", " }o--|| ");
                    break;
                case AV_TEXTFORMAT_LINKTYPE_ONETOONE:
                    av_bprintf(&mmc->link_buf, "%s", " ||--|| ");
                    break;
                case AV_TEXTFORMAT_LINKTYPE_MANYTOMANY:
                    av_bprintf(&mmc->link_buf, "%s", " }o--o{ ");
                    break;
                default:
                    av_bprintf(&mmc->link_buf, "%s", " ||--|| ");
                    break;
                }

                av_bprintf(&mmc->link_buf, "%s : \"\"", sec_data.dest_id);

                break;
            }

    if (tfc->level == 0) {

        writer_put_str(tfc, "\n");
        if (mmc->create_html) {
            char *token_pos = av_stristr(mmc->diagram_config->html_template, "__###__");
            if (!token_pos) {
                av_log(tfc, AV_LOG_ERROR, "Unable to locate the required token (__###__) in the html template.");
                return;
            }
            token_pos += strlen("__###__");
            writer_put_str(tfc, token_pos);
        }
    }

    if (tfc->level == 1) {

        if (mmc->link_buf.len > 0) {
            writer_put_str(tfc, mmc->link_buf.str);
            av_bprint_clear(&mmc->link_buf);
        }

        writer_put_str(tfc, "\n");
    }
}

static void mermaid_print_value(AVTextFormatContext *tfc, const char *key,
                                const char *str, int64_t num, const int is_int)
{
    MermaidContext *mmc = tfc->priv;
    const AVTextFormatSection *section = tf_get_section(tfc, tfc->level);

    if (!section)
        return;

    AVBPrint *buf = &tfc->section_pbuf[tfc->level];
    struct section_data sec_data = mmc->section_data[tfc->level];
    int exit = 0;

    if (section->id_key && !strcmp(section->id_key, key)) {
        set_str(&mmc->section_data[tfc->level].section_id, str);
        exit = 1;
    }

    if (section->dest_id_key && !strcmp(section->dest_id_key, key)) {
        set_str(&mmc->section_data[tfc->level].dest_id, str);
        exit = 1;
    }

    if (section->src_id_key && !strcmp(section->src_id_key, key)) {
        set_str(&mmc->section_data[tfc->level].src_id, str);
        exit = 1;
    }

    if (section->linktype_key && !strcmp(section->linktype_key, key)) {
        mmc->section_data[tfc->level].link_type = (AVTextFormatLinkType)num;;
        exit = 1;
    }

    //if (exit)
    //    return;

    if ((section->flags & (AV_TEXTFORMAT_SECTION_FLAG_IS_SHAPE | AV_TEXTFORMAT_SECTION_PRINT_TAGS))
        || (section->flags & AV_TEXTFORMAT_SECTION_FLAG_IS_SUBGRAPH && sec_data.subgraph_start_incomplete)) {

        if (exit)
            return;

        switch (mmc->diagram_config->diagram_type) {
        case AV_DIAGRAMTYPE_GRAPH:

            if (is_int) {
                writer_printf(tfc, "<span class=\"%s\">%s: %"PRId64"</span>", key, key, num);
            } else {
                ////AVBPrint b;
                ////av_bprint_init(&b, 0, AV_BPRINT_SIZE_UNLIMITED);
                const char *tmp = av_strireplace(str, "\"", "'");
                ////av_bprint_escape(&b, str, NULL, AV_ESCAPE_MODE_AUTO, AV_ESCAPE_FLAG_STRICT);
                writer_printf(tfc, "<span class=\"%s\">%s</span>", key, tmp);
                av_freep(&tmp);
            }

            break;
        case AV_DIAGRAMTYPE_ENTITYRELATIONSHIP:

            if (!is_int && str)
            {
                const char *col_type;

                if (key[0] == '_')
                    return;

                if (sec_data.section_id && !strcmp(str, sec_data.section_id))
                    col_type = "PK";
                else if (sec_data.dest_id && !strcmp(str, sec_data.dest_id))
                    col_type = "FK";
                else if (sec_data.src_id && !strcmp(str, sec_data.src_id))
                    col_type = "FK";
                else
                    col_type = "";

                MM_INDENT();

                if (is_int)
                    writer_printf(tfc, "    %s %"PRId64" %s\n", key, num, col_type);
                else
                    writer_printf(tfc, "    %s %s %s\n", key, str, col_type);
            }
            break;
        }

    } else if (section->flags & AV_TEXTFORMAT_SECTION_FLAG_HAS_LINKS) {

        if (exit)
            return;

        if (buf->len > 0)
            av_bprintf(buf, "%s", "<br>");

        av_bprintf(buf, "");
        if (is_int)
            av_bprintf(buf, "<span>%s: %"PRId64"</span>", key, num);
        else
            av_bprintf(buf, "<span>%s</span>", str);

        mmc->nb_link_captions[tfc->level]++;
    }
}

static inline void mermaid_print_str(AVTextFormatContext *tfc, const char *key, const char *value)
{
    mermaid_print_value(tfc, key, value, 0, 0);
}

static void mermaid_print_int(AVTextFormatContext *tfc, const char *key, int64_t value)
{
    mermaid_print_value(tfc, key, NULL, value, 1);
}

const AVTextFormatter avtextformatter_mermaid = {
    .name                 = "mermaid",
    .priv_size            = sizeof(MermaidContext),
    .init                 = mermaid_init,
    .uninit               = mermaid_uninit,
    .print_section_header = mermaid_print_section_header,
    .print_section_footer = mermaid_print_section_footer,
    .print_integer        = mermaid_print_int,
    .print_string         = mermaid_print_str,
    .flags = AV_TEXTFORMAT_FLAG_IS_DIAGRAM_FORMATTER,
    .priv_class           = &mermaid_class,
};


const AVTextFormatter avtextformatter_mermaidhtml = {
    .name                 = "mermaidhtml",
    .priv_size            = sizeof(MermaidContext),
    .init                 = mermaid_init_html,
    .uninit               = mermaid_uninit,
    .print_section_header = mermaid_print_section_header,
    .print_section_footer = mermaid_print_section_footer,
    .print_integer        = mermaid_print_int,
    .print_string         = mermaid_print_str,
    .flags = AV_TEXTFORMAT_FLAG_IS_DIAGRAM_FORMATTER,
    .priv_class           = &mermaid_class,
};
