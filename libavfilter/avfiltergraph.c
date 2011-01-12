/*
 * filter graphs
 * Copyright (c) 2008 Vitor Sessak
 * Copyright (c) 2007 Bobby Bingham
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

#include <ctype.h>
#include <string.h>

#include "avfilter.h"
#include "avfiltergraph.h"
#include "internal.h"

AVFilterGraph *avfilter_graph_alloc(void)
{
    return av_mallocz(sizeof(AVFilterGraph));
}

void avfilter_graph_free(AVFilterGraph *graph)
{
    if (!graph)
        return;
    for (; graph->filter_count > 0; graph->filter_count --)
        avfilter_free(graph->filters[graph->filter_count - 1]);
    av_freep(&graph->scale_sws_opts);
    av_freep(&graph->filters);
}

int avfilter_graph_add_filter(AVFilterGraph *graph, AVFilterContext *filter)
{
    AVFilterContext **filters = av_realloc(graph->filters,
                                           sizeof(AVFilterContext*) * (graph->filter_count+1));
    if (!filters)
        return AVERROR(ENOMEM);

    graph->filters = filters;
    graph->filters[graph->filter_count++] = filter;

    return 0;
}

int avfilter_graph_create_filter(AVFilterContext **filt_ctx, AVFilter *filt,
                                 const char *name, const char *args, void *opaque,
                                 AVFilterGraph *graph_ctx)
{
    int ret;

    if ((ret = avfilter_open(filt_ctx, filt, name)) < 0)
        goto fail;
    if ((ret = avfilter_init_filter(*filt_ctx, args, opaque)) < 0)
        goto fail;
    if ((ret = avfilter_graph_add_filter(graph_ctx, *filt_ctx)) < 0)
        goto fail;
    return 0;

fail:
    if (*filt_ctx)
        avfilter_free(*filt_ctx);
    *filt_ctx = NULL;
    return ret;
}

int ff_avfilter_graph_check_validity(AVFilterGraph *graph, AVClass *log_ctx)
{
    AVFilterContext *filt;
    int i, j;

    for (i = 0; i < graph->filter_count; i++) {
        filt = graph->filters[i];

        for (j = 0; j < filt->input_count; j++) {
            if (!filt->inputs[j] || !filt->inputs[j]->src) {
                av_log(log_ctx, AV_LOG_ERROR,
                       "Input pad \"%s\" for the filter \"%s\" of type \"%s\" not connected to any source\n",
                       filt->input_pads[j].name, filt->name, filt->filter->name);
                return -1;
            }
        }

        for (j = 0; j < filt->output_count; j++) {
            if (!filt->outputs[j] || !filt->outputs[j]->dst) {
                av_log(log_ctx, AV_LOG_ERROR,
                       "Output pad \"%s\" for the filter \"%s\" of type \"%s\" not connected to any destination\n",
                       filt->output_pads[j].name, filt->name, filt->filter->name);
                return -1;
            }
        }
    }

    return 0;
}

int ff_avfilter_graph_config_links(AVFilterGraph *graph, AVClass *log_ctx)
{
    AVFilterContext *filt;
    int i, ret;

    for (i=0; i < graph->filter_count; i++) {
        filt = graph->filters[i];

        if (!filt->output_count) {
            if ((ret = avfilter_config_links(filt)))
                return ret;
        }
    }

    return 0;
}

AVFilterContext *avfilter_graph_get_filter(AVFilterGraph *graph, char *name)
{
    int i;

    for (i = 0; i < graph->filter_count; i++)
        if (graph->filters[i]->name && !strcmp(name, graph->filters[i]->name))
            return graph->filters[i];

    return NULL;
}

static int query_formats(AVFilterGraph *graph, AVClass *log_ctx)
{
    int i, j, ret;
    int scaler_count = 0;
    char inst_name[30];

    /* ask all the sub-filters for their supported media formats */
    for (i = 0; i < graph->filter_count; i++) {
        if (graph->filters[i]->filter->query_formats)
            graph->filters[i]->filter->query_formats(graph->filters[i]);
        else
            avfilter_default_query_formats(graph->filters[i]);
    }

    /* go through and merge as many format lists as possible */
    for (i = 0; i < graph->filter_count; i++) {
        AVFilterContext *filter = graph->filters[i];

        for (j = 0; j < filter->input_count; j++) {
            AVFilterLink *link = filter->inputs[j];
            if (link && link->in_formats != link->out_formats) {
                if (!avfilter_merge_formats(link->in_formats,
                                            link->out_formats)) {
                    AVFilterContext *scale;
                    char scale_args[256];
                    /* couldn't merge format lists. auto-insert scale filter */
                    snprintf(inst_name, sizeof(inst_name), "auto-inserted scaler %d",
                             scaler_count++);
                    snprintf(scale_args, sizeof(scale_args), "0:0:%s", graph->scale_sws_opts);
                    if ((ret = avfilter_graph_create_filter(&scale, avfilter_get_by_name("scale"),
                                                            inst_name, scale_args, NULL, graph)) < 0)
                        return ret;
                    if ((ret = avfilter_insert_filter(link, scale, 0, 0)) < 0)
                        return ret;

                    scale->filter->query_formats(scale);
                    if (((link = scale-> inputs[0]) &&
                         !avfilter_merge_formats(link->in_formats, link->out_formats)) ||
                        ((link = scale->outputs[0]) &&
                         !avfilter_merge_formats(link->in_formats, link->out_formats))) {
                        av_log(log_ctx, AV_LOG_ERROR,
                               "Impossible to convert between the formats supported by the filter "
                               "'%s' and the filter '%s'\n", link->src->name, link->dst->name);
                        return -1;
                    }
                }
            }
        }
    }

    return 0;
}

static void pick_format(AVFilterLink *link)
{
    if (!link || !link->in_formats)
        return;

    link->in_formats->format_count = 1;
    link->format = link->in_formats->formats[0];

    avfilter_formats_unref(&link->in_formats);
    avfilter_formats_unref(&link->out_formats);
}

static void pick_formats(AVFilterGraph *graph)
{
    int i, j;

    for (i = 0; i < graph->filter_count; i++) {
        AVFilterContext *filter = graph->filters[i];

        for (j = 0; j < filter->input_count; j++)
            pick_format(filter->inputs[j]);
        for (j = 0; j < filter->output_count; j++)
            pick_format(filter->outputs[j]);
    }
}

int ff_avfilter_graph_config_formats(AVFilterGraph *graph, AVClass *log_ctx)
{
    /* find supported formats from sub-filters, and merge along links */
    if (query_formats(graph, log_ctx))
        return -1;

    /* Once everything is merged, it's possible that we'll still have
     * multiple valid media format choices. We pick the first one. */
    pick_formats(graph);

    return 0;
}

int avfilter_graph_config(AVFilterGraph *graphctx, AVClass *log_ctx)
{
    int ret;

    if ((ret = ff_avfilter_graph_check_validity(graphctx, log_ctx)))
        return ret;
    if ((ret = ff_avfilter_graph_config_formats(graphctx, log_ctx)))
        return ret;
    if ((ret = ff_avfilter_graph_config_links(graphctx, log_ctx)))
        return ret;

    return 0;
}
