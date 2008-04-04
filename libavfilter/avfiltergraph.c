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

#include "allfilters.h"

typedef struct AVFilterGraph {
    unsigned filter_count;
    AVFilterContext **filters;

    /** fake filter to handle links to internal filters */
    AVFilterContext *link_filter;
} GraphContext;

typedef struct {
    AVFilterContext *graph;
} GraphLinkContext;

static int link_init(AVFilterContext *ctx, const char *args, void *opaque)
{
    GraphLinkContext *linkctx = ctx->priv;
    linkctx->graph = opaque;
    return !opaque;
}

/**
 * Given the link between the dummy filter and an internal filter whose input
 * is being exported outside the graph, this returns the externally visible
 * link
 */
static inline AVFilterLink *get_extern_input_link(AVFilterLink *link)
{
    GraphLinkContext *lctx = link->src->priv;
    return lctx->graph->inputs[link->srcpad];
}

/** request a frame from a filter providing input to the graph */
static int link_in_request_frame(AVFilterLink *link)
{
    AVFilterLink *link2 = get_extern_input_link(link);

    if(!link2)
        return -1;
    return avfilter_request_frame(link2);
}

static int link_in_config_props(AVFilterLink *link)
{
    AVFilterLink *link2 = get_extern_input_link(link);
    int (*config_props)(AVFilterLink *);
    int ret;

    if(!link2)
        return -1;
    if(!(config_props = link2->src->output_pads[link2->srcpad].config_props))
        config_props = avfilter_default_config_output_link;
    ret = config_props(link2);

    link->w = link2->w;
    link->h = link2->h;

    return ret;
}

/**
 * Given the link between the dummy filter and an internal filter whose input
 * is being exported outside the graph, this returns the externally visible
 * link
 */
static inline AVFilterLink *get_extern_output_link(AVFilterLink *link)
{
    GraphLinkContext *lctx = link->dst->priv;
    return lctx->graph->outputs[link->dstpad];
}

static int link_out_config_props(AVFilterLink *link)
{
    AVFilterLink *link2 = get_extern_output_link(link);
    int (*config_props)(AVFilterLink *);

    if(!link2)
        return 0;

    link2->w = link->w;
    link2->h = link->h;

    if(!(config_props = link2->dst->input_pads[link2->dstpad].config_props))
        config_props = avfilter_default_config_input_link;
    return config_props(link2);
}

static void link_out_start_frame(AVFilterLink *link, AVFilterPicRef *picref)
{
    AVFilterLink *link2 = get_extern_output_link(link);

    if(!link2)
        avfilter_unref_pic(picref);
    else
        avfilter_start_frame(link2, picref);
}

static void link_out_end_frame(AVFilterLink *link)
{
    AVFilterLink *link2 = get_extern_output_link(link);

    if(link2)
        avfilter_end_frame(link2);
}

static AVFilterPicRef *link_out_get_video_buffer(AVFilterLink *link, int perms)
{
    AVFilterLink *link2 = get_extern_output_link(link);

    if(!link2)
        return NULL;
    else
        return avfilter_get_video_buffer(link2, perms);
}

static void link_out_draw_slice(AVFilterLink *link, int y, int height)
{
    AVFilterLink *link2 = get_extern_output_link(link);

    if(link2)
        avfilter_draw_slice(link2, y, height);
}

/** dummy filter used to help export filters pads outside the graph */
static AVFilter vf_graph_dummy =
{
    .name      = "graph_dummy",
    .author    = "Bobby Bingham",

    .priv_size = sizeof(GraphLinkContext),

    .init      = link_init,

    .inputs    = (AVFilterPad[]) {{ .name = NULL, }},
    .outputs   = (AVFilterPad[]) {{ .name = NULL, }},
};

static AVFilterLink *get_intern_input_link(AVFilterLink *link)
{
    GraphContext *graph = link->dst->priv;
    return graph->link_filter->outputs[link->dstpad];
}

static void graph_in_start_frame(AVFilterLink *link, AVFilterPicRef *picref)
{
    AVFilterLink *link2 = get_intern_input_link(link);
    if(link2)
        avfilter_start_frame(link2, picref);
}

static void graph_in_end_frame(AVFilterLink *link)
{
    AVFilterLink *link2 = get_intern_input_link(link);
    if(link2)
        avfilter_end_frame(link2);
}

static AVFilterPicRef *graph_in_get_video_buffer(AVFilterLink *link, int perms)
{
    AVFilterLink *link2 = get_intern_input_link(link);
    if(link2)
        return avfilter_get_video_buffer(link2, perms);
    return NULL;
}

static void graph_in_draw_slice(AVFilterLink *link, int y, int height)
{
    AVFilterLink *link2 = get_intern_input_link(link);
    if(link2)
        avfilter_draw_slice(link2, y, height);
}

static int graph_in_config_props(AVFilterLink *link)
{
    AVFilterLink *link2 = get_intern_input_link(link);
    int (*config_props)(AVFilterLink *);

    if(!link2)
        return -1;

    /* copy link properties over to the dummy internal link */
    link2->w = link->w;
    link2->h = link->h;
    link2->format = link->format;

    if(!(config_props = link2->dst->input_pads[link2->dstpad].config_props))
        return 0;   /* FIXME? */
        //config_props = avfilter_default_config_input_link;
    return config_props(link2);
}

static AVFilterLink *get_intern_output_link(AVFilterLink *link)
{
    GraphContext *graph = link->src->priv;
    return graph->link_filter->inputs[link->srcpad];
}

static int graph_out_request_frame(AVFilterLink *link)
{
    AVFilterLink *link2 = get_intern_output_link(link);

    if(link2)
        return avfilter_request_frame(link2);
    return -1;
}

static int graph_out_config_props(AVFilterLink *link)
{
    AVFilterLink *link2 = get_intern_output_link(link);
    int (*config_props)(AVFilterLink *);
    int ret;

    if(!link2)
        return 0;

    link2->w = link->w;
    link2->h = link->h;
    link2->format = link->format;

    if(!(config_props = link2->src->output_pads[link2->srcpad].config_props))
        config_props = avfilter_default_config_output_link;
    ret = config_props(link2);

    link->w = link2->w;
    link->h = link2->h;
    link->format = link2->format;

    return ret;
}

static int add_graph_input(AVFilterContext *gctx, AVFilterContext *filt, unsigned idx,
                           char *name)
{
    GraphContext *graph = gctx->priv;

    AVFilterPad graph_inpad =
    {
        .name             = name,
        .type             = AV_PAD_VIDEO,
        .start_frame      = graph_in_start_frame,
        .end_frame        = graph_in_end_frame,
        .get_video_buffer = graph_in_get_video_buffer,
        .draw_slice       = graph_in_draw_slice,
        .config_props     = graph_in_config_props,
        /* XXX */
    };
    AVFilterPad dummy_outpad =
    {
        .name          = NULL,          /* FIXME? */
        .type          = AV_PAD_VIDEO,
        .request_frame = link_in_request_frame,
        .config_props  = link_in_config_props,
    };

    avfilter_insert_inpad (gctx, gctx->input_count, &graph_inpad);
    avfilter_insert_outpad(graph->link_filter, graph->link_filter->output_count,
                           &dummy_outpad);
    return avfilter_link(graph->link_filter,
                         graph->link_filter->output_count-1, filt, idx);
}

static int add_graph_output(AVFilterContext *gctx, AVFilterContext *filt, unsigned idx,
                            char *name)
{
    GraphContext *graph = gctx->priv;

    AVFilterPad graph_outpad =
    {
        .name             = name,
        .type             = AV_PAD_VIDEO,
        .request_frame    = graph_out_request_frame,
        .config_props     = graph_out_config_props,
    };
    AVFilterPad dummy_inpad =
    {
        .name             = NULL,          /* FIXME? */
        .type             = AV_PAD_VIDEO,
        .start_frame      = link_out_start_frame,
        .end_frame        = link_out_end_frame,
        .draw_slice       = link_out_draw_slice,
        .get_video_buffer = link_out_get_video_buffer,
        .config_props     = link_out_config_props,
    };

    avfilter_insert_outpad(gctx, gctx->output_count, &graph_outpad);
    avfilter_insert_inpad (graph->link_filter, graph->link_filter->input_count,
                           &dummy_inpad);
    return avfilter_link(filt, idx, graph->link_filter,
                         graph->link_filter->input_count-1);
}

static void uninit(AVFilterContext *ctx)
{
    GraphContext *graph = ctx->priv;

    if(graph->link_filter) {
        avfilter_destroy(graph->link_filter);
        graph->link_filter = NULL;
    }
    for(; graph->filter_count > 0; graph->filter_count --)
        avfilter_destroy(graph->filters[graph->filter_count - 1]);
    av_freep(&graph->filters);
}

/* TODO: insert in sorted order */
void avfilter_graph_add_filter(AVFilterContext *graphctx, AVFilterContext *filter)
{
    GraphContext *graph = graphctx->priv;

    graph->filters = av_realloc(graph->filters,
                                sizeof(AVFilterContext*) * ++graph->filter_count);
    graph->filters[graph->filter_count - 1] = filter;
}

/* search intelligently, once we insert in order */
AVFilterContext *avfilter_graph_get_filter(AVFilterContext *ctx, char *name)
{
    GraphContext *graph = ctx->priv;
    int i;

    if(!name)
        return NULL;

    for(i = 0; i < graph->filter_count; i ++)
        if(graph->filters[i]->name && !strcmp(name, graph->filters[i]->name))
            return graph->filters[i];

    return NULL;
}

static int query_formats(AVFilterContext *graphctx)
{
    GraphContext *graph = graphctx->priv;
    AVFilterContext *linkfilt = graph->link_filter;
    int i, j;

    /* ask all the sub-filters for their supported colorspaces */
    for(i = 0; i < graph->filter_count; i ++) {
        if(graph->filters[i]->filter->query_formats)
            graph->filters[i]->filter->query_formats(graph->filters[i]);
        else
            avfilter_default_query_formats(graph->filters[i]);
    }

    /* use these formats on our exported links */
    for(i = 0; i < linkfilt->input_count; i ++) {
        avfilter_formats_ref( linkfilt->inputs[i]->in_formats,
                             &linkfilt->inputs[i]->out_formats);

        if(graphctx->outputs[i])
            avfilter_formats_ref( linkfilt-> inputs[i]->in_formats,
                                 &graphctx->outputs[i]->in_formats);
    }
    for(i = 0; i < linkfilt->output_count; i ++) {
        avfilter_formats_ref( linkfilt->outputs[i]->out_formats,
                             &linkfilt->outputs[i]->in_formats);

        if(graphctx->inputs[i])
            avfilter_formats_ref( linkfilt->outputs[i]->out_formats,
                                 &graphctx-> inputs[i]->out_formats);
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

                    if(!(scale = avfilter_open(&avfilter_vf_scale, NULL)))
                        return -1;
                    if(scale->filter->init(scale, NULL, NULL) ||
                       avfilter_insert_filter(link, scale, 0, 0)) {
                        avfilter_destroy(scale);
                        return -1;
                    }

                    avfilter_graph_add_filter(graphctx, scale);
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

        if(filter->filter == &avfilter_vf_graph     ||
           filter->filter == &avfilter_vf_graphfile ||
           filter->filter == &avfilter_vf_graphdesc)
            pick_formats(filter->priv);

        for(j = 0; j < filter->input_count; j ++)
            pick_format(filter->inputs[j]);
        for(j = 0; j < filter->output_count; j ++)
            pick_format(filter->outputs[j]);
    }
}

int avfilter_graph_config_formats(AVFilterContext *graphctx)
{
    GraphContext *graph = graphctx->priv;

    /* Find supported formats from sub-filters, and merge along links */
    if(query_formats(graphctx))
        return -1;

    /* Once everything is merged, it's possible that we'll still have
     * multiple valid choices of colorspace. We pick the first one. */
    pick_formats(graph);

    return 0;
}

int avfilter_graph_config_links(AVFilterContext *graphctx)
{
    GraphContext *graph = graphctx->priv;
    int i, j;

    for(i = 0; i < graph->filter_count; i ++) {
        for(j = 0; j < graph->filters[i]->input_count; j ++) {
            /* ensure that graphs contained within graphs are configured */
            if((graph->filters[i]->filter == &avfilter_vf_graph     ||
                graph->filters[i]->filter == &avfilter_vf_graphfile ||
                graph->filters[i]->filter == &avfilter_vf_graphdesc) &&
                avfilter_graph_config_links(graph->filters[i]))
                return -1;
            if(avfilter_config_link(graph->filters[i]->inputs[j]))
                return -1;
        }
    }

    return 0;
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

    if((ret = avfilter_open(avfilter_get_by_name(name), NULL))) {
        if(avfilter_init_filter(ret, args, opaque)) {
            av_log(NULL, AV_LOG_ERROR, "error initializing filter!\n");
            avfilter_destroy(ret);
            goto fail;
        }
    } else {
        av_log(NULL, AV_LOG_ERROR,
               "error creating filter \"%s\" with args \"%s\"\n",
               name, args ? args : "(none)");
    }

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
        if(i == 0 && filters[1]->input_count > 0)
            add_graph_input(graphctx, filters[1], 0, "default");
        filters[0] = filters[1];
    }

    if(filters[1]->output_count > 0)
        add_graph_output(graphctx, filters[1], 0, "default");

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
    GraphContext *gctx = ctx->priv;

    if(!(gctx->link_filter = avfilter_open(&vf_graph_dummy, NULL)))
        return -1;
    if(avfilter_init_filter(gctx->link_filter, NULL, ctx))
        goto fail;

    if(!args)
        return 0;

    return graph_load_chain_from_string(ctx, args, NULL, NULL);

fail:
    avfilter_destroy(gctx->link_filter);
    return -1;
}

AVFilter avfilter_vf_graph =
{
    .name      = "graph",
    .author    = "Bobby Bingham",

    .priv_size = sizeof(GraphContext),

    .init      = init,
    .uninit    = uninit,

    .query_formats = query_formats,

    .inputs    = (AVFilterPad[]) {{ .name = NULL, }},
    .outputs   = (AVFilterPad[]) {{ .name = NULL, }},
};

static int graph_load_from_desc(AVFilterContext *ctx, AVFilterGraphDesc *desc)
{
    AVFilterGraphDescFilter *curfilt;
    AVFilterGraphDescLink   *curlink;
    AVFilterGraphDescExport *curpad;
    AVFilterContext *filt, *filtb;

    AVFilter *filterdef;

    /* create all filters */
    for(curfilt = desc->filters; curfilt; curfilt = curfilt->next) {
        if(!(filterdef = avfilter_get_by_name(curfilt->filter)) ||
           !(filt = avfilter_open(filterdef, curfilt->name))) {
            av_log(ctx, AV_LOG_ERROR, "error creating filter\n");
            goto fail;
        }
        avfilter_graph_add_filter(ctx, filt);
        if(avfilter_init_filter(filt, curfilt->args, NULL)) {
            av_log(ctx, AV_LOG_ERROR, "error initializing filter\n");
            goto fail;
        }
    }

    /* create all links */
    for(curlink = desc->links; curlink; curlink = curlink->next) {
        if(!(filt = avfilter_graph_get_filter(ctx, curlink->src))) {
            av_log(ctx, AV_LOG_ERROR, "link source does not exist in graph\n");
            goto fail;
        }
        if(!(filtb = avfilter_graph_get_filter(ctx, curlink->dst))) {
            av_log(ctx, AV_LOG_ERROR, "link destination does not exist in graph\n");
            goto fail;
        }
        if(avfilter_link(filt, curlink->srcpad, filtb, curlink->dstpad)) {
            av_log(ctx, AV_LOG_ERROR, "cannot create link between source and destination filters\n");
            goto fail;
        }
    }

    /* export all input pads */
    for(curpad = desc->inputs; curpad; curpad = curpad->next) {
        if(!(filt = avfilter_graph_get_filter(ctx, curpad->filter))) {
            av_log(ctx, AV_LOG_ERROR, "filter owning exported pad does not exist\n");
            goto fail;
        }
        add_graph_input(ctx, filt, curpad->pad, curpad->name);
    }

    /* export all output pads */
    for(curpad = desc->outputs; curpad; curpad = curpad->next) {
        if(!(filt = avfilter_graph_get_filter(ctx, curpad->filter))) {
            av_log(ctx, AV_LOG_ERROR, "filter owning exported pad does not exist\n");
            goto fail;
        }
        add_graph_output(ctx, filt, curpad->pad, curpad->name);
    }

    return 0;

fail:
    uninit(ctx);
    return -1;
}

static int init_desc(AVFilterContext *ctx, const char *args, void *opaque)
{
    GraphContext *gctx = ctx->priv;

    if(!opaque)
        return -1;

    if(!(gctx->link_filter = avfilter_open(&vf_graph_dummy, NULL)))
        return -1;
    if(avfilter_init_filter(gctx->link_filter, NULL, ctx))
        goto fail;

    return graph_load_from_desc(ctx, opaque);

fail:
    avfilter_destroy(gctx->link_filter);
    return -1;
}

AVFilter avfilter_vf_graphdesc =
{
    .name      = "graph_desc",
    .author    = "Bobby Bingham",

    .priv_size = sizeof(GraphContext),

    .init      = init_desc,
    .uninit    = uninit,

    .query_formats = query_formats,

    .inputs    = (AVFilterPad[]) {{ .name = NULL, }},
    .outputs   = (AVFilterPad[]) {{ .name = NULL, }},
};

static int init_file(AVFilterContext *ctx, const char *args, void *opaque)
{
    AVFilterGraphDesc *desc;
    int ret;

    if(!args)
        return -1;
    if(!(desc = avfilter_graph_load_desc(args)))
        return -1;

    ret = init_desc(ctx, NULL, desc);
    avfilter_graph_free_desc(desc);
    return ret;
}

AVFilter avfilter_vf_graphfile =
{
    .name      = "graph_file",
    .author    = "Bobby Bingham",

    .priv_size = sizeof(GraphContext),

    .init      = init_file,
    .uninit    = uninit,

    .query_formats = query_formats,

    .inputs    = (AVFilterPad[]) {{ .name = NULL, }},
    .outputs   = (AVFilterPad[]) {{ .name = NULL, }},
};

