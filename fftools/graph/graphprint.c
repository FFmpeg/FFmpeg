/*
 * Copyright (c) 2018-2025 - softworkz
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
 * output writers for filtergraph details
 */

#include <string.h>
#include <stdatomic.h>

#include "graphprint.h"

#include "fftools/ffmpeg.h"
#include "fftools/ffmpeg_mux.h"

#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/pixdesc.h"
#include "libavutil/dict.h"
#include "libavutil/common.h"
#include "libavfilter/avfilter.h"
#include "libavutil/buffer.h"
#include "libavutil/hwcontext.h"
#include "fftools/textformat/avtextformat.h"
#include "fftools/textformat/tf_mermaid.h"
#include "fftools/resources/resman.h"

typedef enum {
    SECTION_ID_ROOT,
    SECTION_ID_FILTERGRAPHS,
    SECTION_ID_FILTERGRAPH,
    SECTION_ID_GRAPH_INPUTS,
    SECTION_ID_GRAPH_INPUT,
    SECTION_ID_GRAPH_OUTPUTS,
    SECTION_ID_GRAPH_OUTPUT,
    SECTION_ID_FILTERS,
    SECTION_ID_FILTER,
    SECTION_ID_FILTER_INPUTS,
    SECTION_ID_FILTER_INPUT,
    SECTION_ID_FILTER_OUTPUTS,
    SECTION_ID_FILTER_OUTPUT,
    SECTION_ID_HWFRAMESCONTEXT,
    SECTION_ID_INPUTFILES,
    SECTION_ID_INPUTFILE,
    SECTION_ID_INPUTSTREAMS,
    SECTION_ID_INPUTSTREAM,
    SECTION_ID_OUTPUTFILES,
    SECTION_ID_OUTPUTFILE,
    SECTION_ID_OUTPUTSTREAMS,
    SECTION_ID_OUTPUTSTREAM,
    SECTION_ID_STREAMLINKS,
    SECTION_ID_STREAMLINK,
    SECTION_ID_DECODERS,
    SECTION_ID_DECODER,
    SECTION_ID_ENCODERS,
    SECTION_ID_ENCODER,
} SectionID;

static struct AVTextFormatSection sections[] = {
    [SECTION_ID_ROOT]            = { SECTION_ID_ROOT, "root", AV_TEXTFORMAT_SECTION_FLAG_IS_WRAPPER, { SECTION_ID_FILTERGRAPHS, SECTION_ID_INPUTFILES, SECTION_ID_OUTPUTFILES, SECTION_ID_DECODERS, SECTION_ID_ENCODERS, SECTION_ID_STREAMLINKS, -1 } },

    [SECTION_ID_FILTERGRAPHS]    = { SECTION_ID_FILTERGRAPHS, "graphs", AV_TEXTFORMAT_SECTION_FLAG_IS_ARRAY, { SECTION_ID_FILTERGRAPH, -1 } },
    [SECTION_ID_FILTERGRAPH]     = { SECTION_ID_FILTERGRAPH, "graph", AV_TEXTFORMAT_SECTION_FLAG_HAS_VARIABLE_FIELDS, { SECTION_ID_GRAPH_INPUTS, SECTION_ID_GRAPH_OUTPUTS, SECTION_ID_FILTERS, -1 }, .element_name = "graph_info" },

    [SECTION_ID_GRAPH_INPUTS]    = { SECTION_ID_GRAPH_INPUTS, "graph_inputs", AV_TEXTFORMAT_SECTION_FLAG_IS_ARRAY, { SECTION_ID_GRAPH_INPUT, -1 }, .id_key = "id" },
    [SECTION_ID_GRAPH_INPUT]     = { SECTION_ID_GRAPH_INPUT, "graph_input", 0, { -1 }, .id_key = "filter_id" },

    [SECTION_ID_GRAPH_OUTPUTS]   = { SECTION_ID_GRAPH_OUTPUTS, "graph_outputs", AV_TEXTFORMAT_SECTION_FLAG_IS_ARRAY, { SECTION_ID_GRAPH_OUTPUT, -1 }, .id_key = "id" },
    [SECTION_ID_GRAPH_OUTPUT]    = { SECTION_ID_GRAPH_OUTPUT, "graph_output", 0, { -1 }, .id_key = "filter_id" },

    [SECTION_ID_FILTERS]         = { SECTION_ID_FILTERS, "filters", AV_TEXTFORMAT_SECTION_FLAG_IS_ARRAY | AV_TEXTFORMAT_SECTION_FLAG_IS_SUBGRAPH, { SECTION_ID_FILTER, -1 }, .id_key = "graph_id" },
    [SECTION_ID_FILTER]          = { SECTION_ID_FILTER, "filter", AV_TEXTFORMAT_SECTION_FLAG_IS_SHAPE | AV_TEXTFORMAT_SECTION_PRINT_TAGS, { SECTION_ID_FILTER_INPUTS, SECTION_ID_FILTER_OUTPUTS, -1 }, .id_key = "filter_id" },

    [SECTION_ID_FILTER_INPUTS]   = { SECTION_ID_FILTER_INPUTS, "filter_inputs", AV_TEXTFORMAT_SECTION_FLAG_IS_ARRAY, { SECTION_ID_FILTER_INPUT, -1 } },
    [SECTION_ID_FILTER_INPUT]    = { SECTION_ID_FILTER_INPUT, "filter_input", AV_TEXTFORMAT_SECTION_FLAG_HAS_LINKS, { SECTION_ID_HWFRAMESCONTEXT, -1 }, .id_key = "filter_id", .src_id_key = "source_filter_id", .dest_id_key = "filter_id" },

    [SECTION_ID_FILTER_OUTPUTS]  = { SECTION_ID_FILTER_OUTPUTS, "filter_outputs", AV_TEXTFORMAT_SECTION_FLAG_IS_ARRAY, { SECTION_ID_FILTER_OUTPUT, -1 } },
    [SECTION_ID_FILTER_OUTPUT]   = { SECTION_ID_FILTER_OUTPUT, "filter_output", AV_TEXTFORMAT_SECTION_FLAG_HAS_LINKS, { SECTION_ID_HWFRAMESCONTEXT, -1 }, .id_key = "filter_id", .src_id_key = "filter_id", .dest_id_key = "dest_filter_id" },

    [SECTION_ID_HWFRAMESCONTEXT] = { SECTION_ID_HWFRAMESCONTEXT, "hw_frames_context",  0, { -1 }, },

    [SECTION_ID_INPUTFILES]      = { SECTION_ID_INPUTFILES, "inputfiles", AV_TEXTFORMAT_SECTION_FLAG_IS_ARRAY | AV_TEXTFORMAT_SECTION_FLAG_IS_SUBGRAPH, { SECTION_ID_INPUTFILE, -1 }, .id_key = "id" },
    [SECTION_ID_INPUTFILE]       = { SECTION_ID_INPUTFILE, "inputfile", AV_TEXTFORMAT_SECTION_FLAG_IS_SUBGRAPH, { SECTION_ID_INPUTSTREAMS, -1 }, .id_key = "id" },

    [SECTION_ID_INPUTSTREAMS]    = { SECTION_ID_INPUTSTREAMS, "inputstreams", AV_TEXTFORMAT_SECTION_FLAG_IS_ARRAY | AV_TEXTFORMAT_SECTION_FLAG_IS_SUBGRAPH, { SECTION_ID_INPUTSTREAM, -1 }, .id_key = "id" },
    [SECTION_ID_INPUTSTREAM]     = { SECTION_ID_INPUTSTREAM, "inputstream", AV_TEXTFORMAT_SECTION_FLAG_IS_SHAPE | AV_TEXTFORMAT_SECTION_PRINT_TAGS, { -1 }, .id_key = "id" },

    [SECTION_ID_OUTPUTFILES]     = { SECTION_ID_OUTPUTFILES, "outputfiles", AV_TEXTFORMAT_SECTION_FLAG_IS_ARRAY | AV_TEXTFORMAT_SECTION_FLAG_IS_SUBGRAPH, { SECTION_ID_OUTPUTFILE, -1 }, .id_key = "id" },
    [SECTION_ID_OUTPUTFILE]      = { SECTION_ID_OUTPUTFILE, "outputfile", AV_TEXTFORMAT_SECTION_FLAG_IS_SUBGRAPH, { SECTION_ID_OUTPUTSTREAMS, -1 }, .id_key = "id" },

    [SECTION_ID_OUTPUTSTREAMS]   = { SECTION_ID_OUTPUTSTREAMS, "outputstreams", AV_TEXTFORMAT_SECTION_FLAG_IS_ARRAY | AV_TEXTFORMAT_SECTION_FLAG_IS_SUBGRAPH, { SECTION_ID_OUTPUTSTREAM, -1 }, .id_key = "id" },
    [SECTION_ID_OUTPUTSTREAM]    = { SECTION_ID_OUTPUTSTREAM, "outputstream", AV_TEXTFORMAT_SECTION_FLAG_IS_SHAPE | AV_TEXTFORMAT_SECTION_PRINT_TAGS, { -1 }, .id_key = "id", },

    [SECTION_ID_STREAMLINKS]     = { SECTION_ID_STREAMLINKS, "streamlinks", AV_TEXTFORMAT_SECTION_FLAG_IS_ARRAY, { SECTION_ID_STREAMLINK, -1 } },
    [SECTION_ID_STREAMLINK]      = { SECTION_ID_STREAMLINK, "streamlink", AV_TEXTFORMAT_SECTION_FLAG_HAS_LINKS, { -1 }, .src_id_key = "source_stream_id", .dest_id_key = "dest_stream_id" },

    [SECTION_ID_DECODERS]        = { SECTION_ID_DECODERS, "decoders", AV_TEXTFORMAT_SECTION_FLAG_IS_ARRAY | AV_TEXTFORMAT_SECTION_FLAG_IS_SUBGRAPH, { SECTION_ID_DECODER, -1 } },
    [SECTION_ID_DECODER]         = { SECTION_ID_DECODER, "decoder", AV_TEXTFORMAT_SECTION_FLAG_IS_SHAPE | AV_TEXTFORMAT_SECTION_PRINT_TAGS | AV_TEXTFORMAT_SECTION_FLAG_HAS_LINKS, { -1 }, .id_key = "id", .src_id_key = "source_id", .dest_id_key = "id" },

    [SECTION_ID_ENCODERS]        = { SECTION_ID_ENCODERS, "encoders", AV_TEXTFORMAT_SECTION_FLAG_IS_ARRAY | AV_TEXTFORMAT_SECTION_FLAG_IS_SUBGRAPH, { SECTION_ID_ENCODER, -1 } },
    [SECTION_ID_ENCODER]         = { SECTION_ID_ENCODER, "encoder", AV_TEXTFORMAT_SECTION_FLAG_IS_SHAPE | AV_TEXTFORMAT_SECTION_PRINT_TAGS | AV_TEXTFORMAT_SECTION_FLAG_HAS_LINKS, { -1 }, .id_key = "id", .src_id_key = "id", .dest_id_key = "dest_id" },
};

typedef struct GraphPrintContext {
    AVTextFormatContext *tfc;
    AVTextWriterContext *wctx;
    AVDiagramConfig diagram_config;

    int id_prefix_num;
    int is_diagram;
    int opt_flags;
    int skip_buffer_filters;
    AVBPrint pbuf;

} GraphPrintContext;

/* Text Format API Shortcuts */
#define print_id(k, v)          print_sanizied_id(gpc, k, v, 0)
#define print_id_noprefix(k, v) print_sanizied_id(gpc, k, v, 1)
#define print_int(k, v)         avtext_print_integer(tfc, k, v, 0)
#define print_int_opt(k, v)     avtext_print_integer(tfc, k, v, gpc->opt_flags)
#define print_q(k, v, s)        avtext_print_rational(tfc, k, v, s)
#define print_str(k, v)         avtext_print_string(tfc, k, v, 0)
#define print_str_opt(k, v)     avtext_print_string(tfc, k, v, gpc->opt_flags)
#define print_val(k, v, u)      avtext_print_unit_int(tfc, k, v, u)

#define print_fmt(k, f, ...) do {              \
    av_bprint_clear(&gpc->pbuf);                    \
    av_bprintf(&gpc->pbuf, f, __VA_ARGS__);         \
    avtext_print_string(tfc, k, gpc->pbuf.str, 0);    \
} while (0)

#define print_fmt_opt(k, f, ...) do {              \
    av_bprint_clear(&gpc->pbuf);                    \
    av_bprintf(&gpc->pbuf, f, __VA_ARGS__);         \
    avtext_print_string(tfc, k, gpc->pbuf.str, gpc->opt_flags);    \
} while (0)


static atomic_int prefix_num = 0;

static inline char *upcase_string(char *dst, size_t dst_size, const char *src)
{
    unsigned i;
    for (i = 0; src[i] && i < dst_size - 1; i++)
        dst[i]      = (char)av_toupper(src[i]);
    dst[i] = 0;
    return dst;
}

static char *get_extension(const char *url)
{
    const char *dot = NULL;
    const char *sep = NULL;
    const char *end;

    if (!url)
        return NULL;

    /* Stop at the first query ('?') or fragment ('#') delimiter so they
     * are not considered part of the path. */
    end = strpbrk(url, "?#");
    if (!end)
        end = url + strlen(url);

    /* Scan the path component only. */
    for (const char *p = url; p < end; p++) {
        if (*p == '.')
            dot = p;
        else if (*p == '/' || *p == '\\')
            sep = p;
    }

    /* Validate that we have a proper extension. */
    if (dot && dot != url && (!sep || dot > sep + 1) && (dot + 1) < end) {
        /* Use FFmpeg helper to duplicate the substring. */
        return av_strndup(dot + 1, end - (dot + 1));
    }

    return NULL;
}

static void print_hwdevicecontext(const GraphPrintContext *gpc, const AVHWDeviceContext *hw_device_context)
{
    AVTextFormatContext *tfc = gpc->tfc;

    if (!hw_device_context)
        return;

    print_int_opt("has_hw_device_context", 1);
    print_str_opt("hw_device_type", av_hwdevice_get_type_name(hw_device_context->type));
}

static void print_hwframescontext(const GraphPrintContext *gpc, const AVHWFramesContext *hw_frames_context)
{
    AVTextFormatContext *tfc = gpc->tfc;
    const AVPixFmtDescriptor *pix_desc_hw;
    const AVPixFmtDescriptor *pix_desc_sw;

    if (!hw_frames_context || !hw_frames_context->device_ctx)
        return;

    avtext_print_section_header(tfc, NULL, SECTION_ID_HWFRAMESCONTEXT);

    print_int_opt("has_hw_frames_context", 1);
    print_str("hw_device_type", av_hwdevice_get_type_name(hw_frames_context->device_ctx->type));

    pix_desc_hw = av_pix_fmt_desc_get(hw_frames_context->format);
    if (pix_desc_hw) {
        print_str("hw_pixel_format", pix_desc_hw->name);
        if (pix_desc_hw->alias)
            print_str_opt("hw_pixel_format_alias", pix_desc_hw->alias);
    }

    pix_desc_sw = av_pix_fmt_desc_get(hw_frames_context->sw_format);
    if (pix_desc_sw) {
        print_str("sw_pixel_format", pix_desc_sw->name);
        if (pix_desc_sw->alias)
            print_str_opt("sw_pixel_format_alias", pix_desc_sw->alias);
    }

    print_int_opt("width", hw_frames_context->width);
    print_int_opt("height", hw_frames_context->height);
    print_int_opt("initial_pool_size", hw_frames_context->initial_pool_size);

    avtext_print_section_footer(tfc); // SECTION_ID_HWFRAMESCONTEXT
}

static void print_link(GraphPrintContext *gpc, AVFilterLink *link)
{
    AVTextFormatContext *tfc = gpc->tfc;
    AVBufferRef *hw_frames_ctx;
    char layout_string[64];

    if (!link)
        return;

    hw_frames_ctx = avfilter_link_get_hw_frames_ctx(link);

    print_str_opt("media_type", av_get_media_type_string(link->type));

    switch (link->type) {
    case AVMEDIA_TYPE_VIDEO:

        if (hw_frames_ctx && hw_frames_ctx->data) {
            AVHWFramesContext *      hwfctx      = (AVHWFramesContext *)hw_frames_ctx->data;
            const AVPixFmtDescriptor *pix_desc_hw = av_pix_fmt_desc_get(hwfctx->format);
            const AVPixFmtDescriptor *pix_desc_sw = av_pix_fmt_desc_get(hwfctx->sw_format);
            if (pix_desc_hw && pix_desc_sw)
                print_fmt("format", "%s | %s", pix_desc_hw->name, pix_desc_sw->name);
        } else {
            print_str("format", av_x_if_null(av_get_pix_fmt_name(link->format), "?"));
        }

        if (link->w && link->h) {
            if (tfc->show_value_unit) {
                print_fmt("size", "%dx%d", link->w, link->h);
            } else {
                print_int("width", link->w);
                print_int("height", link->h);
            }
        }

        print_q("sar", link->sample_aspect_ratio, ':');

        if (link->color_range != AVCOL_RANGE_UNSPECIFIED)
            print_str_opt("color_range", av_color_range_name(link->color_range));

        if (link->colorspace != AVCOL_SPC_UNSPECIFIED)
            print_str("color_space", av_color_space_name(link->colorspace));
        break;

    case AVMEDIA_TYPE_SUBTITLE:
        ////print_str("format", av_x_if_null(av_get_subtitle_fmt_name(link->format), "?"));

        if (link->w && link->h) {
            if (tfc->show_value_unit) {
                print_fmt("size", "%dx%d", link->w, link->h);
            } else {
                print_int("width", link->w);
                print_int("height", link->h);
            }
        }

        break;

    case AVMEDIA_TYPE_AUDIO:
        av_channel_layout_describe(&link->ch_layout, layout_string, sizeof(layout_string));
        print_str("channel_layout", layout_string);
        print_val("channels", link->ch_layout.nb_channels, "ch");
        if (tfc->show_value_unit)
            print_fmt("sample_rate", "%d.1 kHz", link->sample_rate / 1000);
        else
            print_val("sample_rate", link->sample_rate, "Hz");

        break;
    }

    print_fmt_opt("sample_rate", "%d/%d", link->time_base.num, link->time_base.den);

    if (hw_frames_ctx && hw_frames_ctx->data)
        print_hwframescontext(gpc, (AVHWFramesContext *)hw_frames_ctx->data);
    av_buffer_unref(&hw_frames_ctx);
}

static char sanitize_char(const char c)
{
    if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))
        return c;
    return '_';
}

static void print_sanizied_id(const GraphPrintContext *gpc, const char *key, const char *id_str, int skip_prefix)
{
    AVTextFormatContext *tfc = gpc->tfc;
    AVBPrint buf;

    if (!key || !id_str)
        return;

    av_bprint_init(&buf, 0, AV_BPRINT_SIZE_UNLIMITED);

    if (!skip_prefix)
        av_bprintf(&buf, "G%d_", gpc->id_prefix_num);

    // sanizize section id
    for (const char *p = id_str; *p; p++)
        av_bprint_chars(&buf, sanitize_char(*p), 1);

    print_str(key, buf.str);

    av_bprint_finalize(&buf, NULL);
}

static void print_section_header_id(const GraphPrintContext *gpc, int section_id, const char *id_str, int skip_prefix)
{
    AVTextFormatContext *tfc = gpc->tfc;
    AVTextFormatSectionContext sec_ctx = { 0 };
    AVBPrint buf;

    if (!id_str)
        return;

    av_bprint_init(&buf, 0, AV_BPRINT_SIZE_UNLIMITED);

    if (!skip_prefix)
        av_bprintf(&buf, "G%d_", gpc->id_prefix_num);

    // sanizize section id
    for (const char *p = id_str; *p; p++)
        av_bprint_chars(&buf, sanitize_char(*p), 1);

    sec_ctx.context_id = buf.str;

    avtext_print_section_header(tfc, &sec_ctx, section_id);

    av_bprint_finalize(&buf, NULL);
}

static const char *get_filterpad_name(const AVFilterPad *pad)
{
    return pad ? avfilter_pad_get_name(pad, 0) : "pad";
}

static void print_filter(GraphPrintContext *gpc, const AVFilterContext *filter, AVDictionary *input_map, AVDictionary *output_map)
{
    AVTextFormatContext *tfc = gpc->tfc;
    AVTextFormatSectionContext sec_ctx = { 0 };

    print_section_header_id(gpc, SECTION_ID_FILTER, filter->name, 0);

    ////print_id("filter_id", filter->name);

    if (filter->filter) {
        print_str("filter_name", filter->filter->name);
        print_str_opt("description", filter->filter->description);
        print_int_opt("nb_inputs", filter->nb_inputs);
        print_int_opt("nb_outputs", filter->nb_outputs);
    }

    if (filter->hw_device_ctx) {
        AVHWDeviceContext *device_context = (AVHWDeviceContext *)filter->hw_device_ctx->data;
        print_hwdevicecontext(gpc, device_context);
        if (filter->extra_hw_frames > 0)
            print_int("extra_hw_frames", filter->extra_hw_frames);
    }

    avtext_print_section_header(tfc, NULL, SECTION_ID_FILTER_INPUTS);

    for (unsigned i = 0; i < filter->nb_inputs; i++) {
        AVDictionaryEntry *dic_entry;
        AVFilterLink *link = filter->inputs[i];

        sec_ctx.context_type = av_get_media_type_string(link->type);
        avtext_print_section_header(tfc, &sec_ctx, SECTION_ID_FILTER_INPUT);
        sec_ctx.context_type = NULL;

        print_int_opt("input_index", i);
        print_str_opt("pad_name", get_filterpad_name(link->dstpad));;

        dic_entry = av_dict_get(input_map, link->src->name, NULL, 0);
        if (dic_entry) {
            char buf[256];
            (void)snprintf(buf, sizeof(buf), "in_%s", dic_entry->value);
            print_id_noprefix("source_filter_id", buf);
        } else {
            print_id("source_filter_id", link->src->name);
        }

        print_str_opt("source_pad_name", get_filterpad_name(link->srcpad));
        print_id("filter_id", filter->name);

        print_link(gpc, link);

        avtext_print_section_footer(tfc); // SECTION_ID_FILTER_INPUT
    }

    avtext_print_section_footer(tfc); // SECTION_ID_FILTER_INPUTS

    avtext_print_section_header(tfc, NULL, SECTION_ID_FILTER_OUTPUTS);

    for (unsigned i = 0; i < filter->nb_outputs; i++) {
        AVDictionaryEntry *dic_entry;
        AVFilterLink *link = filter->outputs[i];
        char buf[256];

        sec_ctx.context_type = av_get_media_type_string(link->type);
        avtext_print_section_header(tfc, &sec_ctx, SECTION_ID_FILTER_OUTPUT);
        sec_ctx.context_type = NULL;

        dic_entry = av_dict_get(output_map, link->dst->name, NULL, 0);
        if (dic_entry) {
            (void)snprintf(buf, sizeof(buf), "out_%s", dic_entry->value);
            print_id_noprefix("dest_filter_id", buf);
        } else {
            print_id("dest_filter_id", link->dst->name);
        }

        print_int_opt("output_index", i);
        print_str_opt("pad_name", get_filterpad_name(link->srcpad));
        ////print_id("dest_filter_id", link->dst->name);
        print_str_opt("dest_pad_name", get_filterpad_name(link->dstpad));
        print_id("filter_id", filter->name);

        print_link(gpc, link);

        avtext_print_section_footer(tfc); // SECTION_ID_FILTER_OUTPUT
    }

    avtext_print_section_footer(tfc); // SECTION_ID_FILTER_OUTPUTS

    avtext_print_section_footer(tfc); // SECTION_ID_FILTER
}

static void init_sections(void)
{
    for (unsigned i = 0; i < FF_ARRAY_ELEMS(sections); i++)
        sections[i].show_all_entries = 1;
}

static void print_filtergraph_single(GraphPrintContext *gpc, FilterGraph *fg, AVFilterGraph *graph)
{
    AVTextFormatContext *tfc = gpc->tfc;
    AVDictionary *input_map = NULL;
    AVDictionary *output_map = NULL;

    print_int("graph_index", fg->index);
    print_fmt("name", "Graph %d.%d", gpc->id_prefix_num, fg->index);
    print_fmt("id", "Graph_%d_%d", gpc->id_prefix_num, fg->index);
    print_str("description", fg->graph_desc);

    print_section_header_id(gpc, SECTION_ID_GRAPH_INPUTS, "Input_File", 0);

    for (int i = 0; i < fg->nb_inputs; i++) {
        InputFilter *ifilter = fg->inputs[i];
        enum AVMediaType media_type = ifilter->type;

        avtext_print_section_header(tfc, NULL, SECTION_ID_GRAPH_INPUT);

        print_int("input_index", ifilter->index);

        if (ifilter->linklabel)
            print_str("link_label", (const char*)ifilter->linklabel);

        if (ifilter->filter) {
            print_id("filter_id", ifilter->filter->name);
            print_str("filter_name", ifilter->filter->filter->name);
        }

        if (ifilter->linklabel && ifilter->filter)
            av_dict_set(&input_map, ifilter->filter->name, (const char *)ifilter->linklabel, 0);
        else if (ifilter->input_name && ifilter->filter)
            av_dict_set(&input_map, ifilter->filter->name, (const char *)ifilter->input_name, 0);

        print_str("media_type", av_get_media_type_string(media_type));

        avtext_print_section_footer(tfc); // SECTION_ID_GRAPH_INPUT
    }

    avtext_print_section_footer(tfc); // SECTION_ID_GRAPH_INPUTS

    print_section_header_id(gpc, SECTION_ID_GRAPH_OUTPUTS, "Output_File", 0);

    for (int i = 0; i < fg->nb_outputs; i++) {
        OutputFilter *ofilter = fg->outputs[i];

        avtext_print_section_header(tfc, NULL, SECTION_ID_GRAPH_OUTPUT);

        print_int("output_index", ofilter->index);

        print_str("name", ofilter->output_name);

        if (fg->outputs[i]->linklabel)
            print_str("link_label", (const char*)fg->outputs[i]->linklabel);

        if (ofilter->filter) {
            print_id("filter_id", ofilter->filter->name);
            print_str("filter_name", ofilter->filter->filter->name);
        }

        if (ofilter->output_name && ofilter->filter)
            av_dict_set(&output_map, ofilter->filter->name, ofilter->output_name, 0);


        print_str("media_type", av_get_media_type_string(ofilter->type));

        avtext_print_section_footer(tfc); // SECTION_ID_GRAPH_OUTPUT
    }

    avtext_print_section_footer(tfc); // SECTION_ID_GRAPH_OUTPUTS

    if (graph) {
        AVTextFormatSectionContext sec_ctx = { 0 };

        sec_ctx.context_id = av_asprintf("Graph_%d_%d", gpc->id_prefix_num, fg->index);

        avtext_print_section_header(tfc, &sec_ctx, SECTION_ID_FILTERS);

        if (gpc->is_diagram) {
            print_fmt("name", "Graph %d.%d", gpc->id_prefix_num, fg->index);
            print_str("description", fg->graph_desc);
            print_str("id", sec_ctx.context_id);
        }

        av_freep(&sec_ctx.context_id);

        for (unsigned i = 0; i < graph->nb_filters; i++) {
            AVFilterContext *filter = graph->filters[i];

            if (gpc->skip_buffer_filters) {
                if (av_dict_get(input_map, filter->name, NULL, 0))
                    continue;
                if (av_dict_get(output_map, filter->name, NULL, 0))
                    continue;
            }

            sec_ctx.context_id = filter->name;

            print_filter(gpc, filter, input_map, output_map);
        }

        avtext_print_section_footer(tfc); // SECTION_ID_FILTERS
    }

    // Clean up dictionaries
    av_dict_free(&input_map);
    av_dict_free(&output_map);
}

static int print_streams(GraphPrintContext *gpc, InputFile **ifiles, int nb_ifiles, OutputFile **ofiles, int nb_ofiles)
{
    AVTextFormatContext       *tfc = gpc->tfc;
    AVBPrint                   buf;
    AVTextFormatSectionContext sec_ctx = { 0 };

    av_bprint_init(&buf, 0, AV_BPRINT_SIZE_UNLIMITED);

    print_section_header_id(gpc, SECTION_ID_INPUTFILES, "Inputs", 0);

    for (int n = nb_ifiles - 1; n >= 0; n--) {
        InputFile *ifi = ifiles[n];
        AVFormatContext *fc = ifi->ctx;

        sec_ctx.context_id = av_asprintf("Input_%d", n);
        avtext_print_section_header(tfc, &sec_ctx, SECTION_ID_INPUTFILE);
        av_freep(&sec_ctx.context_id);

        print_fmt("index", "%d", ifi->index);

        if (fc) {
            print_str("demuxer_name", fc->iformat->name);
            if (fc->url) {
                char *extension = get_extension(fc->url);
                if (extension) {
                    print_str("file_extension", extension);
                    av_freep(&extension);
                }
                print_str("url", fc->url);
            }
        }

        sec_ctx.context_id = av_asprintf("InputStreams_%d", n);

        avtext_print_section_header(tfc, &sec_ctx, SECTION_ID_INPUTSTREAMS);

        av_freep(&sec_ctx.context_id);

        for (int i = 0; i < ifi->nb_streams; i++) {
            InputStream *ist = ifi->streams[i];
            const AVCodecDescriptor *codec_desc;

            if (!ist || !ist->par)
                continue;

            codec_desc = avcodec_descriptor_get(ist->par->codec_id);

            sec_ctx.context_id = av_asprintf("r_in_%d_%d", n, i);

            sec_ctx.context_type = av_get_media_type_string(ist->par->codec_type);

            avtext_print_section_header(tfc, &sec_ctx, SECTION_ID_INPUTSTREAM);
            av_freep(&sec_ctx.context_id);
            sec_ctx.context_type = NULL;

            av_bprint_clear(&buf);

            print_fmt("id", "r_in_%d_%d", n, i);

            if (codec_desc && codec_desc->name) {
                ////av_bprintf(&buf, "%s", upcase_string(char_buf, sizeof(char_buf), codec_desc->long_name));
                av_bprintf(&buf, "%s", codec_desc->long_name);
            } else if (ist->dec) {
                char char_buf[256];
                av_bprintf(&buf, "%s", upcase_string(char_buf, sizeof(char_buf), ist->dec->name));
            } else if (ist->par->codec_type == AVMEDIA_TYPE_ATTACHMENT) {
                av_bprintf(&buf, "%s", "Attachment");
            } else if (ist->par->codec_type == AVMEDIA_TYPE_DATA) {
                av_bprintf(&buf, "%s", "Data");
            }

            print_fmt("name", "%s", buf.str);
            print_fmt("index", "%d", ist->index);

            if (ist->dec)
                print_str_opt("media_type", av_get_media_type_string(ist->par->codec_type));

            avtext_print_section_footer(tfc); // SECTION_ID_INPUTSTREAM
        }

        avtext_print_section_footer(tfc); // SECTION_ID_INPUTSTREAMS
        avtext_print_section_footer(tfc); // SECTION_ID_INPUTFILE
    }

    avtext_print_section_footer(tfc); // SECTION_ID_INPUTFILES


    print_section_header_id(gpc, SECTION_ID_DECODERS, "Decoders", 0);

    for (int n = 0; n < nb_ifiles; n++) {
        InputFile *ifi = ifiles[n];

        for (int i = 0; i < ifi->nb_streams; i++) {
            InputStream *ist = ifi->streams[i];

            if (!ist->decoder)
                continue;

            sec_ctx.context_id = av_asprintf("in_%d_%d", n, i);
            sec_ctx.context_type = av_get_media_type_string(ist->par->codec_type);
            sec_ctx.context_flags = 2;

            avtext_print_section_header(tfc, &sec_ctx, SECTION_ID_DECODER);
            av_freep(&sec_ctx.context_id);
            sec_ctx.context_type = NULL;
            sec_ctx.context_flags = 0;

            av_bprint_clear(&buf);

            print_fmt("source_id", "r_in_%d_%d", n, i);
            print_fmt("id", "in_%d_%d", n, i);

            ////av_bprintf(&buf, "%s", upcase_string(char_buf, sizeof(char_buf), ist->dec->name));
            print_fmt("name", "%s", ist->dec->name);

            print_str_opt("media_type", av_get_media_type_string(ist->par->codec_type));

            avtext_print_section_footer(tfc); // SECTION_ID_DECODER
        }
    }

    avtext_print_section_footer(tfc); // SECTION_ID_DECODERS


    print_section_header_id(gpc, SECTION_ID_ENCODERS, "Encoders", 0);

    for (int n = 0; n < nb_ofiles; n++) {
        OutputFile *of = ofiles[n];

        for (int i = 0; i < of->nb_streams; i++) {
            OutputStream *ost = of->streams[i];
            ////const AVCodecDescriptor *codec_desc;

            if (!ost || !ost->st || !ost->st->codecpar || !ost->enc)
                continue;

            ////codec_desc = avcodec_descriptor_get(ost->st->codecpar->codec_id);

            sec_ctx.context_id = av_asprintf("out__%d_%d", n, i);
            sec_ctx.context_type = av_get_media_type_string(ost->type);
            sec_ctx.context_flags = 2;

            avtext_print_section_header(tfc, &sec_ctx, SECTION_ID_ENCODER);
            av_freep(&sec_ctx.context_id);
            sec_ctx.context_type = NULL;
            sec_ctx.context_flags = 0;

            av_bprint_clear(&buf);

            print_fmt("id", "out__%d_%d", n, i);
            print_fmt("dest_id", "r_out__%d_%d", n, i);

            print_fmt("name", "%s", ost->enc->enc_ctx->av_class->item_name(ost->enc->enc_ctx));

            print_str_opt("media_type", av_get_media_type_string(ost->type));

            avtext_print_section_footer(tfc); // SECTION_ID_ENCODER
        }
    }

    avtext_print_section_footer(tfc); // SECTION_ID_ENCODERS


    print_section_header_id(gpc, SECTION_ID_OUTPUTFILES, "Outputs", 0);

    for (int n = nb_ofiles - 1; n >= 0; n--) {
        OutputFile *of = ofiles[n];
        Muxer *muxer = (Muxer *)of;

        if (!muxer->fc)
            continue;

        sec_ctx.context_id = av_asprintf("Output_%d", n);

        avtext_print_section_header(tfc, &sec_ctx, SECTION_ID_OUTPUTFILE);

        av_freep(&sec_ctx.context_id);

        ////print_str_opt("index", av_get_media_type_string(of->index));
        print_fmt("index", "%d", of->index);
        ////print_str("url", of->url);
        print_str("muxer_name", muxer->fc->oformat->name);
        if (of->url) {
            char *extension = get_extension(of->url);
            if (extension) {
                print_str("file_extension", extension);
                av_freep(&extension);
            }
            print_str("url", of->url);
        }

        sec_ctx.context_id = av_asprintf("OutputStreams_%d", n);

        avtext_print_section_header(tfc, &sec_ctx, SECTION_ID_OUTPUTSTREAMS);

        av_freep(&sec_ctx.context_id);

        for (int i = 0; i < of->nb_streams; i++) {
            OutputStream *ost = of->streams[i];
            const AVCodecDescriptor *codec_desc = avcodec_descriptor_get(ost->st->codecpar->codec_id);

            sec_ctx.context_id = av_asprintf("r_out__%d_%d", n, i);
            sec_ctx.context_type = av_get_media_type_string(ost->type);
            avtext_print_section_header(tfc, &sec_ctx, SECTION_ID_OUTPUTSTREAM);
            av_freep(&sec_ctx.context_id);
            sec_ctx.context_type = NULL;

            av_bprint_clear(&buf);

            print_fmt("id", "r_out__%d_%d", n, i);

            if (codec_desc && codec_desc->name) {
                av_bprintf(&buf, "%s", codec_desc->long_name);
            } else {
                av_bprintf(&buf, "%s", "unknown");
            }

            print_fmt("name", "%s", buf.str);
            print_fmt("index", "%d", ost->index);

            print_str_opt("media_type", av_get_media_type_string(ost->type));

            avtext_print_section_footer(tfc); // SECTION_ID_OUTPUTSTREAM
        }

        avtext_print_section_footer(tfc); // SECTION_ID_OUTPUTSTREAMS
        avtext_print_section_footer(tfc); // SECTION_ID_OUTPUTFILE
    }

    avtext_print_section_footer(tfc); // SECTION_ID_OUTPUTFILES


    avtext_print_section_header(tfc, NULL, SECTION_ID_STREAMLINKS);

    for (int n = 0; n < nb_ofiles; n++) {
        OutputFile *of = ofiles[n];

        for (int i = 0; i < of->nb_streams; i++) {
            OutputStream *ost = of->streams[i];

            if (ost->ist && !ost->filter) {
                sec_ctx.context_type = av_get_media_type_string(ost->type);
                avtext_print_section_header(tfc, &sec_ctx, SECTION_ID_STREAMLINK);
                sec_ctx.context_type = NULL;

                if (ost->enc) {
                    print_fmt("dest_stream_id", "out__%d_%d", n, i);
                    print_fmt("source_stream_id", "in_%d_%d", ost->ist->file->index, ost->ist->index);
                    print_str("operation", "Transcode");
                } else {
                    print_fmt("dest_stream_id", "r_out__%d_%d", n, i);
                    print_fmt("source_stream_id", "r_in_%d_%d", ost->ist->file->index, ost->ist->index);
                    print_str("operation", "Stream Copy");
                }

                print_str_opt("media_type", av_get_media_type_string(ost->type));

                avtext_print_section_footer(tfc); // SECTION_ID_STREAMLINK
            }
        }
    }

    avtext_print_section_footer(tfc); // SECTION_ID_STREAMLINKS

    av_bprint_finalize(&buf, NULL);
    return 0;
}


static void uninit_graphprint(GraphPrintContext *gpc)
{
    if (gpc->tfc)
        avtext_context_close(&gpc->tfc);

    if (gpc->wctx)
        avtextwriter_context_close(&gpc->wctx);

    // Finalize the print buffer if it was initialized
    av_bprint_finalize(&gpc->pbuf, NULL);

    av_freep(&gpc);
}

static int init_graphprint(GraphPrintContext **pgpc, AVBPrint *target_buf)
{
    const AVTextFormatter *text_formatter;
    AVTextFormatContext *tfc = NULL;
    AVTextWriterContext *wctx = NULL;
    GraphPrintContext *gpc = NULL;
    int ret;

    init_sections();
    *pgpc = NULL;

    av_bprint_init(target_buf, 0, AV_BPRINT_SIZE_UNLIMITED);

    const char *w_name = print_graphs_format ? print_graphs_format : "json";

    text_formatter = avtext_get_formatter_by_name(w_name);
    if (!text_formatter) {
        av_log(NULL, AV_LOG_ERROR, "Unknown filter graph output format with name '%s'\n", w_name);
        ret = AVERROR(EINVAL);
        goto fail;
    }

    ret = avtextwriter_create_buffer(&wctx, target_buf);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "avtextwriter_create_buffer failed. Error code %d\n", ret);
        ret = AVERROR(EINVAL);
        goto fail;
    }

    AVTextFormatOptions tf_options = { .show_optional_fields = -1 };
    const char *w_args = print_graphs_format ? strchr(print_graphs_format, '=') : NULL;
    if (w_args)
        ++w_args; // consume '='
    ret = avtext_context_open(&tfc, text_formatter, wctx, w_args, sections, FF_ARRAY_ELEMS(sections), tf_options, NULL);
    if (ret < 0) {
        goto fail;
    }

    gpc = av_mallocz(sizeof(GraphPrintContext));
    if (!gpc) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    gpc->wctx = wctx;
    gpc->tfc = tfc;
    av_bprint_init(&gpc->pbuf, 0, AV_BPRINT_SIZE_UNLIMITED);

    gpc->id_prefix_num = atomic_fetch_add(&prefix_num, 1);
    gpc->is_diagram = !!(tfc->formatter->flags & AV_TEXTFORMAT_FLAG_IS_DIAGRAM_FORMATTER);
    if (gpc->is_diagram) {
        tfc->show_value_unit = 1;
        tfc->show_optional_fields = -1;
        gpc->opt_flags = AV_TEXTFORMAT_PRINT_STRING_OPTIONAL;
        gpc->skip_buffer_filters = 1;
        ////} else {
        ////    gpc->opt_flags = AV_TEXTFORMAT_PRINT_STRING_OPTIONAL;
    }

    if (!strcmp(text_formatter->name, "mermaid") || !strcmp(text_formatter->name, "mermaidhtml")) {
        gpc->diagram_config.diagram_css = ff_resman_get_string(FF_RESOURCE_GRAPH_CSS);

        if (!strcmp(text_formatter->name, "mermaidhtml"))
            gpc->diagram_config.html_template = ff_resman_get_string(FF_RESOURCE_GRAPH_HTML);

        av_diagram_init(tfc, &gpc->diagram_config);
    }

    *pgpc = gpc;

    return 0;

fail:
    if (tfc)
        avtext_context_close(&tfc);
    if (wctx && !tfc) // Only free wctx if tfc didn't take ownership of it
        avtextwriter_context_close(&wctx);
    av_freep(&gpc);

    return ret;
}


int print_filtergraph(FilterGraph *fg, AVFilterGraph *graph)
{
    GraphPrintContext *gpc = NULL;
    AVTextFormatContext *tfc;
    AVBPrint *target_buf = &fg->graph_print_buf;
    int ret;

    if (!fg) {
        av_log(NULL, AV_LOG_ERROR, "Invalid filter graph provided\n");
        return AVERROR(EINVAL);
    }

    if (target_buf->len)
        av_bprint_finalize(target_buf, NULL);

    ret = init_graphprint(&gpc, target_buf);
    if (ret)
        return ret;

    if (!gpc) {
        av_log(NULL, AV_LOG_ERROR, "Failed to initialize graph print context\n");
        return AVERROR(ENOMEM);
    }

    tfc = gpc->tfc;

    // Due to the threading model each graph needs to print itself into a buffer
    // from its own thread. The actual printing happens short before cleanup in ffmpeg.c
    // where all graphs are assembled together. To make this work, we need to put the
    // formatting context into the same state like it would be when printing all at once,
    // so here we print the section headers and clear the buffer to get into the right state.
    avtext_print_section_header(tfc, NULL, SECTION_ID_ROOT);
    avtext_print_section_header(tfc, NULL, SECTION_ID_FILTERGRAPHS);
    avtext_print_section_header(tfc, NULL, SECTION_ID_FILTERGRAPH);

    av_bprint_clear(target_buf);

    print_filtergraph_single(gpc, fg, graph);

    if (gpc->is_diagram) {
        avtext_print_section_footer(tfc); // SECTION_ID_FILTERGRAPH
        avtext_print_section_footer(tfc); // SECTION_ID_FILTERGRAPHS
    }

    uninit_graphprint(gpc);

    return 0;
}

static int print_filtergraphs_priv(FilterGraph **graphs, int nb_graphs, InputFile **ifiles, int nb_ifiles, OutputFile **ofiles, int nb_ofiles)
{
    GraphPrintContext *gpc = NULL;
    AVTextFormatContext *tfc;
    AVBPrint target_buf;
    int ret;

    ret = init_graphprint(&gpc, &target_buf);
    if (ret)
        goto cleanup;

    if (!gpc) {
        ret = AVERROR(ENOMEM);
        goto cleanup;
    }

    tfc = gpc->tfc;

    avtext_print_section_header(tfc, NULL, SECTION_ID_ROOT);
    avtext_print_section_header(tfc, NULL, SECTION_ID_FILTERGRAPHS);

    for (int i = 0; i < nb_graphs; i++) {
        AVBPrint *graph_buf = &graphs[i]->graph_print_buf;

        if (graph_buf->len > 0) {
            avtext_print_section_header(tfc, NULL, SECTION_ID_FILTERGRAPH);
            av_bprint_append_data(&target_buf, graph_buf->str, graph_buf->len);
            av_bprint_finalize(graph_buf, NULL);
            avtext_print_section_footer(tfc); // SECTION_ID_FILTERGRAPH
        }
    }

    for (int n = 0; n < nb_ofiles; n++) {
        OutputFile *of = ofiles[n];

        for (int i = 0; i < of->nb_streams; i++) {
            OutputStream *ost = of->streams[i];

            if (ost->fg_simple) {
                AVBPrint *graph_buf = &ost->fg_simple->graph_print_buf;

                if (graph_buf->len > 0) {
                    avtext_print_section_header(tfc, NULL, SECTION_ID_FILTERGRAPH);
                    av_bprint_append_data(&target_buf, graph_buf->str, graph_buf->len);
                    av_bprint_finalize(graph_buf, NULL);
                    avtext_print_section_footer(tfc); // SECTION_ID_FILTERGRAPH
                }
            }
        }
    }

    avtext_print_section_footer(tfc); // SECTION_ID_FILTERGRAPHS

    print_streams(gpc, ifiles, nb_ifiles, ofiles, nb_ofiles);

    avtext_print_section_footer(tfc); // SECTION_ID_ROOT

    if (print_graphs_file) {
        AVIOContext *avio = NULL;

        if (!strcmp(print_graphs_file, "-")) {
            printf("%s", target_buf.str);
        } else {
            ret = avio_open2(&avio, print_graphs_file, AVIO_FLAG_WRITE, NULL, NULL);
            if (ret < 0) {
                av_log(NULL, AV_LOG_ERROR, "Failed to open graph output file, \"%s\": %s\n", print_graphs_file, av_err2str(ret));
                goto cleanup;
            }

            avio_write(avio, (const unsigned char *)target_buf.str, FFMIN(target_buf.len, target_buf.size - 1));

            if ((ret = avio_closep(&avio)) < 0)
                av_log(NULL, AV_LOG_ERROR, "Error closing graph output file, loss of information possible: %s\n", av_err2str(ret));
        }
    }

    if (print_graphs)
        av_log(NULL, AV_LOG_INFO, "%s    %c", target_buf.str, '\n');

cleanup:
    // Properly clean up resources
    if (gpc)
        uninit_graphprint(gpc);

    // Ensure the target buffer is properly finalized
    av_bprint_finalize(&target_buf, NULL);

    return ret;
}

int print_filtergraphs(FilterGraph **graphs, int nb_graphs, InputFile **ifiles, int nb_ifiles, OutputFile **ofiles, int nb_ofiles)
{
    int ret = print_filtergraphs_priv(graphs, nb_graphs, ifiles, nb_ifiles, ofiles, nb_ofiles);
    ff_resman_uninit();
    return ret;
}
