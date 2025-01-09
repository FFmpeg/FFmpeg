/*
 * Copyright (c) 2009 Stefano Sabatini
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

#include <stdio.h>

#include "libavutil/channel_layout.h"
#include "libavutil/mem.h"
#include "libavutil/pixdesc.h"
#include "libavutil/samplefmt.h"

#include "libavfilter/avfilter.h"
#include "libavfilter/avfilter_internal.h"
#include "libavfilter/formats.h"
#include "libavfilter/framequeue.h"

static void print_formats_internal(AVFilterLink **links, const AVFilterPad *pads,
                                   unsigned nb, size_t fmts_cfg_offset,
                                   const char *inout_string)
{
    for (unsigned i = 0; i < nb; i++) {
        const AVFilterLink *const link = links[i];
        const AVFilterFormatsConfig *const cfg = (AVFilterFormatsConfig*)((const char*)link + fmts_cfg_offset);
        const char *pad_name = avfilter_pad_get_name(pads, i);

        if (link->type == AVMEDIA_TYPE_VIDEO) {
            const AVFilterFormats *const fmts = cfg->formats;
            for (unsigned j = 0; fmts && j < fmts->nb_formats; j++) {
                printf("%s[%u] %s: fmt:%s\n",
                        inout_string, i, pad_name,
                        av_get_pix_fmt_name(fmts->formats[j]));
            }
        } else if (link->type == AVMEDIA_TYPE_AUDIO) {
            const AVFilterFormats *const fmts = cfg->formats;
            const AVFilterChannelLayouts *const layouts = cfg->channel_layouts;

            for (unsigned j = 0; fmts && j < fmts->nb_formats; j++)
                printf("%s[%u] %s: fmt:%s\n",
                       inout_string, i, pad_name,
                       av_get_sample_fmt_name(fmts->formats[j]));

            for (unsigned j = 0; layouts && j < layouts->nb_channel_layouts; j++) {
                char buf[256];
                av_channel_layout_describe(&layouts->channel_layouts[j], buf, sizeof(buf));
                printf("%s[%u] %s: chlayout:%s\n",
                       inout_string, i, pad_name, buf);
            }
        }
    }
}

static void print_formats(AVFilterContext *filter_ctx)
{
    print_formats_internal(filter_ctx->inputs, filter_ctx->input_pads,
                           filter_ctx->nb_inputs,
                           offsetof(AVFilterLink, outcfg), "INPUT");
    print_formats_internal(filter_ctx->outputs, filter_ctx->output_pads,
                           filter_ctx->nb_outputs,
                           offsetof(AVFilterLink, incfg), "OUTPUT");
}

int main(int argc, char **argv)
{
    const AVFilter *filter;
    const FFFilter *fi;
    AVFilterContext *filter_ctx;
    AVFilterGraph *graph_ctx;
    const char *filter_name;
    const char *filter_args = NULL;
    int i;
    int ret = 0;

    av_log_set_level(AV_LOG_DEBUG);

    if (argc < 2) {
        fprintf(stderr, "Missing filter name as argument\n");
        return 1;
    }

    filter_name = argv[1];
    if (argc > 2)
        filter_args = argv[2];

    /* allocate graph */
    graph_ctx = avfilter_graph_alloc();
    if (!graph_ctx)
        return 1;

    /* get a corresponding filter and open it */
    if (!(filter = avfilter_get_by_name(filter_name))) {
        fprintf(stderr, "Unrecognized filter with name '%s'\n", filter_name);
        return 1;
    }
    fi = fffilter(filter);

    /* open filter and add it to the graph */
    if (!(filter_ctx = avfilter_graph_alloc_filter(graph_ctx, filter, filter_name))) {
        fprintf(stderr, "Impossible to open filter with name '%s'\n",
                filter_name);
        return 1;
    }
    if (avfilter_init_str(filter_ctx, filter_args) < 0) {
        fprintf(stderr, "Impossible to init filter '%s' with arguments '%s'\n",
                filter_name, filter_args);
        return 1;
    }

    /* create a link for each of the input pads */
    for (i = 0; i < filter_ctx->nb_inputs; i++) {
        AVFilterLink *link = av_mallocz(sizeof(FilterLinkInternal));
        if (!link) {
            fprintf(stderr, "Unable to allocate memory for filter input link\n");
            ret = 1;
            goto fail;
        }
        link->type = avfilter_pad_get_type(filter_ctx->input_pads, i);
        filter_ctx->inputs[i] = link;
    }
    for (i = 0; i < filter_ctx->nb_outputs; i++) {
        AVFilterLink *link = av_mallocz(sizeof(FilterLinkInternal));
        if (!link) {
            fprintf(stderr, "Unable to allocate memory for filter output link\n");
            ret = 1;
            goto fail;
        }
        link->type = avfilter_pad_get_type(filter_ctx->output_pads, i);
        filter_ctx->outputs[i] = link;
    }

    if (fi->formats_state == FF_FILTER_FORMATS_QUERY_FUNC)
        ret = fi->formats.query_func(filter_ctx);
    else if (fi->formats_state == FF_FILTER_FORMATS_QUERY_FUNC2) {
        AVFilterFormatsConfig **cfg_in = NULL, **cfg_out = NULL;

        if (filter_ctx->nb_inputs) {
            cfg_in = av_malloc_array(filter_ctx->nb_inputs, sizeof(*cfg_in));
            for (unsigned i = 0; i < filter_ctx->nb_inputs; i++)
                cfg_in[i] = &filter_ctx->inputs[i]->outcfg;
        }
        if (filter_ctx->nb_outputs) {
            cfg_out = av_malloc_array(filter_ctx->nb_outputs, sizeof(*cfg_out));
            for (unsigned i = 0; i < filter_ctx->nb_outputs; i++)
                cfg_out[i] = &filter_ctx->outputs[i]->incfg;
        }

        ret = fi->formats.query_func2(filter_ctx, cfg_in, cfg_out);
        av_freep(&cfg_in);
        av_freep(&cfg_out);
    } else
        ret = ff_default_query_formats(filter_ctx);

    print_formats(filter_ctx);

fail:
    avfilter_free(filter_ctx);
    avfilter_graph_free(&graph_ctx);
    fflush(stdout);
    return ret;
}
