/*
 * Filter graphs
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

struct AVFilterGraph {
    unsigned filter_count;
    AVFilterContext **filters;
};

AVFilterGraph *avfilter_create_graph(void)
{
    return av_mallocz(sizeof(AVFilterGraph));
}

static void destroy_graph_filters(AVFilterGraph *graph)
{
    unsigned i;

    for(i = 0; i < graph->filter_count; i ++)
        avfilter_destroy(graph->filters[i]);
    av_freep(&graph->filters);
}

void avfilter_destroy_graph(AVFilterGraph *graph)
{
    destroy_graph_filters(graph);
    av_free(graph);
}

void avfilter_graph_add_filter(AVFilterGraph *graph, AVFilterContext *filter)
{
    graph->filters = av_realloc(graph->filters,
                                sizeof(AVFilterContext*) * ++graph->filter_count);
    graph->filters[graph->filter_count - 1] = filter;
}

