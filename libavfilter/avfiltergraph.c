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

#include <string.h>
#include <stddef.h>

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

static AVFilterContext *create_filter_with_args(char *filter)
{
    AVFilterContext *ret;
    char *name, *args;

    name = filter;
    if((args = strchr(filter, '='))) {
        /* ensure we at least have a name */
        if(args == filter)
            return NULL;

        *args ++ = 0;
    }

    av_log(NULL, AV_LOG_INFO, "creating filter \"%s\" with args \"%s\"\n",
           name, args ? args : "(none)");

    if((ret = avfilter_create_by_name(name, NULL))) {
        if(avfilter_init_filter(ret, args)) {
            av_log(NULL, AV_LOG_ERROR, "error initializing filter!\n");
            avfilter_destroy(ret);
            ret = NULL;
        }
    } else av_log(NULL, AV_LOG_ERROR, "error creating filter!\n");

    return ret;
}

int avfilter_graph_load_chain(AVFilterGraph *graph,
                              unsigned count, char **filter_list,
                              AVFilterContext **first, AVFilterContext **last)
{
    unsigned i;
    AVFilterContext *filters[2] = {NULL,NULL};

    for(i = 0; i < count; i ++) {
        if(!(filters[1] = create_filter_with_args(filter_list[i])))
            goto fail;
        if(i == 0) {
            if(first) *first = filters[1];
        } else {
            if(avfilter_link(filters[0], 0, filters[1], 0)) {
                av_log(NULL, AV_LOG_ERROR, "error linking filters!\n");
                goto fail;
            }
        }
        avfilter_graph_add_filter(graph, filters[1]);
        filters[0] = filters[1];
    }

    if(last) *last = filters[1];
    return 0;

fail:
    destroy_graph_filters(graph);
    *first = *last = NULL;
    return -1;
}

