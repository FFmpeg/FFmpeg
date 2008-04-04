/*
 * filter graphs
 * copyright (c) 2007 Bobby Bingham
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

#include "avfilter.h"
#include "avfiltergraph.h"

/**
 * For use in av_log
 */
static const char *log_name(void *p)
{
    return "Filter parser";
}

static const AVClass filter_parser_class = {
    "Filter parser",
    log_name
};

static const AVClass *log_ctx = &filter_parser_class;

static void uninit(GraphContext *graph)
{
    for(; graph->filter_count > 0; graph->filter_count --)
        avfilter_destroy(graph->filters[graph->filter_count - 1]);
    av_freep(&graph->filters);
}

/* TODO: insert in sorted order */
void avfilter_graph_add_filter(GraphContext *graph, AVFilterContext *filter)
{
    graph->filters = av_realloc(graph->filters,
                                sizeof(AVFilterContext*) * ++graph->filter_count);
    graph->filters[graph->filter_count - 1] = filter;
}

/* search intelligently, once we insert in order */
AVFilterContext *avfilter_graph_get_filter(GraphContext *graph, char *name)
{
    int i;

    if(!name)
        return NULL;

    for(i = 0; i < graph->filter_count; i ++)
        if(graph->filters[i]->name && !strcmp(name, graph->filters[i]->name))
            return graph->filters[i];

    return NULL;
}

static int query_formats(GraphContext *graph)
{
    int i, j;

    /* ask all the sub-filters for their supported colorspaces */
    for(i = 0; i < graph->filter_count; i ++) {
        if(graph->filters[i]->filter->query_formats)
            graph->filters[i]->filter->query_formats(graph->filters[i]);
        else
            avfilter_default_query_formats(graph->filters[i]);
    }

    /* go through and merge as many format lists as possible */
    for(i = 0; i < graph->filter_count; i ++) {
        AVFilterContext *filter = graph->filters[i];

        for(j = 0; j < filter->input_count; j ++) {
            AVFilterLink *link;
            if(!(link = filter->inputs[j]))
                continue;
            if(link->in_formats != link->out_formats) {
                if(!avfilter_merge_formats(link->in_formats,
                                           link->out_formats)) {
                    /* couldn't merge format lists. auto-insert scale filter */
                    AVFilterContext *scale;

                    if(!(scale =
                         avfilter_open(avfilter_get_by_name("scale"), NULL)))
                        return -1;
                    if(scale->filter->init(scale, NULL, NULL) ||
                       avfilter_insert_filter(link, scale, 0, 0)) {
                        avfilter_destroy(scale);
                        return -1;
                    }

                    avfilter_graph_add_filter(graph, scale);
                    scale->filter->query_formats(scale);
                    if(!avfilter_merge_formats(scale-> inputs[0]->in_formats,
                                              scale-> inputs[0]->out_formats) ||
                       !avfilter_merge_formats(scale->outputs[0]->in_formats,
                                              scale->outputs[0]->out_formats))
                        return -1;
                }
            }
        }
    }

    return 0;
}

static void pick_format(AVFilterLink *link)
{
    if(!link || !link->in_formats)
        return;

    link->in_formats->format_count = 1;
    link->format = link->in_formats->formats[0];

    avfilter_formats_unref(&link->in_formats);
    avfilter_formats_unref(&link->out_formats);
}

static void pick_formats(GraphContext *graph)
{
    int i, j;

    for(i = 0; i < graph->filter_count; i ++) {
        AVFilterContext *filter = graph->filters[i];

        for(j = 0; j < filter->input_count; j ++)
            pick_format(filter->inputs[j]);
        for(j = 0; j < filter->output_count; j ++)
            pick_format(filter->outputs[j]);
    }
}

int avfilter_graph_config_formats(GraphContext *graph)
{
    /* find supported formats from sub-filters, and merge along links */
    if(query_formats(graph))
        return -1;

    /* Once everything is merged, it's possible that we'll still have
     * multiple valid colorspace choices. We pick the first one. */
    pick_formats(graph);

    return 0;
}

static int graph_load_from_desc2(GraphContext *ctx, AVFilterGraphDesc *desc)
{
    AVFilterGraphDescFilter *curfilt;
    AVFilterGraphDescLink   *curlink;
    AVFilterContext *filt, *filtb;

    AVFilter *filterdef;
    char tmp[20];

    /* create all filters */
    for(curfilt = desc->filters; curfilt; curfilt = curfilt->next) {
        snprintf(tmp, 20, "%d", curfilt->index);
        if(!(filterdef = avfilter_get_by_name(curfilt->filter)) ||
           !(filt = avfilter_open(filterdef, tmp))) {
            av_log(&log_ctx, AV_LOG_ERROR,
               "error creating filter '%s'\n", curfilt->filter);
            goto fail;
        }
        avfilter_graph_add_filter(ctx, filt);
        if(avfilter_init_filter(filt, curfilt->args, NULL)) {
            av_log(&log_ctx, AV_LOG_ERROR,
                "error initializing filter '%s'\n", curfilt->filter);
            goto fail;
        }
    }

    /* create all links */
    for(curlink = desc->links; curlink; curlink = curlink->next) {
        snprintf(tmp, 20, "%d", curlink->src);
        if(!(filt = avfilter_graph_get_filter(ctx, tmp))) {
            av_log(&log_ctx, AV_LOG_ERROR, "link source does not exist in graph\n");
            goto fail;
        }
        snprintf(tmp, 20, "%d", curlink->dst);
        if(!(filtb = avfilter_graph_get_filter(ctx, tmp))) {
            av_log(&log_ctx, AV_LOG_ERROR, "link destination does not exist in graph\n");
            goto fail;
        }
        if(avfilter_link(filt, curlink->srcpad, filtb, curlink->dstpad)) {
            av_log(&log_ctx, AV_LOG_ERROR, "cannot create link between source and destination filters\n");
            goto fail;
        }
    }

    return 0;

fail:
    uninit(ctx);
    return -1;
}

int graph_load_from_desc3(GraphContext *graph, AVFilterGraphDesc *desc, AVFilterContext *in, int inpad, AVFilterContext *out, int outpad)
{
    AVFilterGraphDescExport *curpad;
    char tmp[20];
    AVFilterContext *filt;

    if (graph_load_from_desc2(graph, desc) < 0)
        goto fail;

    /* export all input pads */
    for(curpad = desc->inputs; curpad; curpad = curpad->next) {
        snprintf(tmp, 20, "%d", curpad->filter);
        if(!(filt = avfilter_graph_get_filter(graph, tmp))) {
            av_log(&log_ctx, AV_LOG_ERROR, "filter owning exported pad does not exist\n");
            goto fail;
        }
        if(avfilter_link(in, inpad, filt, curpad->pad)) {
            av_log(&log_ctx, AV_LOG_ERROR, "cannot create link between source and destination filters\n");
            goto fail;
        }
    }

    /* export all output pads */
    for(curpad = desc->outputs; curpad; curpad = curpad->next) {
        snprintf(tmp, 20, "%d", curpad->filter);
        if(!(filt = avfilter_graph_get_filter(graph, tmp))) {
            av_log(&log_ctx, AV_LOG_ERROR, "filter owning exported pad does not exist\n");
            goto fail;
        }

        if(avfilter_link(filt, curpad->pad, out, outpad)) {
            av_log(&log_ctx, AV_LOG_ERROR, "cannot create link between source and destination filters\n");
            goto fail;
        }
    }

    return 0;

fail:
    uninit(graph);
    return -1;
}
