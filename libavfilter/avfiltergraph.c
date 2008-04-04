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

#include "avstring.h"
#include "avfilter.h"
#include "avfiltergraph.h"

typedef struct AVFilterGraph {
    unsigned filter_count;
    AVFilterContext **filters;
} GraphContext;

static void uninit(AVFilterContext *ctx)
{
    GraphContext *graph = ctx->priv;

    for(; graph->filter_count > 0; graph->filter_count --)
        avfilter_destroy(graph->filters[graph->filter_count - 1]);
    av_freep(&graph->filters);
}

void avfilter_graph_add_filter(AVFilterContext *graphctx, AVFilterContext *filter)
{
    GraphContext *graph = graphctx->priv;

    graph->filters = av_realloc(graph->filters,
                                sizeof(AVFilterContext*) * ++graph->filter_count);
    graph->filters[graph->filter_count - 1] = filter;
}

static AVFilterContext *create_filter_with_args(const char *filt, void *opaque)
{
    AVFilterContext *ret;
    char *filter = av_strdup(filt); /* copy - don't mangle the input string */
    char *name, *args;

    name = filter;
    if((args = strchr(filter, '='))) {
        /* ensure we at least have a name */
        if(args == filter)
            goto fail;

        *args ++ = 0;
    }

    av_log(NULL, AV_LOG_INFO, "creating filter \"%s\" with args \"%s\"\n",
           name, args ? args : "(none)");

    if((ret = avfilter_create_by_name(name, NULL))) {
        if(avfilter_init_filter(ret, args, opaque)) {
            av_log(NULL, AV_LOG_ERROR, "error initializing filter!\n");
            avfilter_destroy(ret);
            goto fail;
        }
    } else av_log(NULL, AV_LOG_ERROR, "error creating filter!\n");

    av_free(filter);

    return ret;

fail:
    av_free(filter);
    return NULL;
}

static int graph_load_chain(AVFilterContext *graphctx,
                              unsigned count, char **filter_list, void **opaque,
                              AVFilterContext **first, AVFilterContext **last)
{
    unsigned i;
    AVFilterContext *filters[2] = {NULL,NULL};

    for(i = 0; i < count; i ++) {
        void *op;

        if(opaque) op = opaque[i];
        else       op = NULL;

        if(!(filters[1] = create_filter_with_args(filter_list[i], op)))
            goto fail;
        if(i == 0) {
            if(first) *first = filters[1];
        } else {
            if(avfilter_link(filters[0], 0, filters[1], 0)) {
                av_log(NULL, AV_LOG_ERROR, "error linking filters!\n");
                goto fail;
            }
        }
        avfilter_graph_add_filter(graphctx, filters[1]);
        filters[0] = filters[1];
    }

    if(last) *last = filters[1];
    return 0;

fail:
    uninit(graphctx);
    if(first) *first = NULL;
    if(last)  *last  = NULL;
    return -1;
}

static int graph_load_chain_from_string(AVFilterContext *ctx, const char *str,
                                        AVFilterContext **first,
                                        AVFilterContext **last)
{
    int count, ret = 0;
    char **strings;
    char *filt;

    strings    = av_malloc(sizeof(char *));
    strings[0] = av_strdup(str);

    filt = strchr(strings[0], ',');
    for(count = 1; filt; count ++) {
        if(filt == strings[count-1]) {
            ret = -1;
            goto done;
        }

        strings = av_realloc(strings, sizeof(char *) * (count+1));
        strings[count] = filt + 1;
        *filt = '\0';
        filt = strchr(strings[count], ',');
    }

    ret = graph_load_chain(ctx, count, strings, NULL, first, last);

done:
    av_free(strings[0]);
    av_free(strings);

    return ret;
}

static int init(AVFilterContext *ctx, const char *args, void *opaque)
{
    AVFilterContext **filters = opaque;

    if(!args)
        return 0;
    if(!opaque)
        return -1;

    return graph_load_chain_from_string(ctx, args, filters, filters + 1);
}

AVFilter vf_graph =
{
    .name      = "graph",
    .author    = "Bobby Bingham",

    .priv_size = sizeof(GraphContext),

    .init      = init,
    .uninit    = uninit,

    .inputs    = (AVFilterPad[]) {{ .name = NULL, }},
    .outputs   = (AVFilterPad[]) {{ .name = NULL, }},
};

