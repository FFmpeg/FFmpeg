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

#include "libavutil/audioconvert.h"
#include "avfilter.h"
#include "avfiltergraph.h"
#include "internal.h"

AVFilterGraph *avfilter_graph_alloc(void)
{
    return av_mallocz(sizeof(AVFilterGraph));
}

void avfilter_graph_free(AVFilterGraph **graph)
{
    if (!*graph)
        return;
    for (; (*graph)->filter_count > 0; (*graph)->filter_count--)
        avfilter_free((*graph)->filters[(*graph)->filter_count - 1]);
    av_freep(&(*graph)->scale_sws_opts);
    av_freep(&(*graph)->filters);
    av_freep(graph);
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
                return AVERROR(EINVAL);
            }
        }

        for (j = 0; j < filt->output_count; j++) {
            if (!filt->outputs[j] || !filt->outputs[j]->dst) {
                av_log(log_ctx, AV_LOG_ERROR,
                       "Output pad \"%s\" for the filter \"%s\" of type \"%s\" not connected to any destination\n",
                       filt->output_pads[j].name, filt->name, filt->filter->name);
                return AVERROR(EINVAL);
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

static int insert_conv_filter(AVFilterGraph *graph, AVFilterLink *link,
                              const char *filt_name, const char *filt_args)
{
    static int auto_count = 0, ret;
    char inst_name[32];
    AVFilterContext *filt_ctx;

    snprintf(inst_name, sizeof(inst_name), "auto-inserted %s %d",
            filt_name, auto_count++);

    if ((ret = avfilter_graph_create_filter(&filt_ctx,
                                            avfilter_get_by_name(filt_name),
                                            inst_name, filt_args, NULL, graph)) < 0)
        return ret;
    if ((ret = avfilter_insert_filter(link, filt_ctx, 0, 0)) < 0)
        return ret;

    filt_ctx->filter->query_formats(filt_ctx);

    if ( ((link = filt_ctx-> inputs[0]) &&
           !avfilter_merge_formats(link->in_formats, link->out_formats)) ||
         ((link = filt_ctx->outputs[0]) &&
           !avfilter_merge_formats(link->in_formats, link->out_formats))
       ) {
        av_log(NULL, AV_LOG_ERROR,
               "Impossible to convert between the formats supported by the filter "
               "'%s' and the filter '%s'\n", link->src->name, link->dst->name);
        return AVERROR(EINVAL);
    }

    if (link->type == AVMEDIA_TYPE_AUDIO &&
         (((link = filt_ctx-> inputs[0]) &&
           (!avfilter_merge_formats(link->in_chlayouts, link->out_chlayouts) ||
            !avfilter_merge_formats(link->in_packing,   link->out_packing))) ||
         ((link = filt_ctx->outputs[0]) &&
           (!avfilter_merge_formats(link->in_chlayouts, link->out_chlayouts) ||
            !avfilter_merge_formats(link->in_packing,   link->out_packing))))
       ) {
        av_log(NULL, AV_LOG_ERROR,
               "Impossible to convert between the channel layouts/packing formats supported by the filter "
               "'%s' and the filter '%s'\n", link->src->name, link->dst->name);
        return AVERROR(EINVAL);
    }

    return 0;
}

static int query_formats(AVFilterGraph *graph, AVClass *log_ctx)
{
    int i, j, ret;
    char filt_args[128];

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
            if (!link) continue;

            if (!link->in_formats || !link->out_formats)
                return AVERROR(EINVAL);

            if (link->type == AVMEDIA_TYPE_VIDEO &&
                !avfilter_merge_formats(link->in_formats, link->out_formats)) {

                /* couldn't merge format lists, auto-insert scale filter */
                snprintf(filt_args, sizeof(filt_args), "0:0:%s",
                         graph->scale_sws_opts);
                if (ret = insert_conv_filter(graph, link, "scale", filt_args))
                    return ret;
            }
            else if (link->type == AVMEDIA_TYPE_AUDIO) {
                if (!link->in_chlayouts || !link->out_chlayouts ||
                    !link->in_packing   || !link->out_packing)
                    return AVERROR(EINVAL);

                if (!avfilter_merge_formats(link->in_formats,   link->out_formats)   ||
                    !avfilter_merge_formats(link->in_chlayouts, link->out_chlayouts) ||
                    !avfilter_merge_formats(link->in_packing,   link->out_packing))
                    if (ret = insert_conv_filter(graph, link, "aconvert", NULL))
                       return ret;
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

    if (link->type == AVMEDIA_TYPE_AUDIO) {
        link->in_chlayouts->format_count = 1;
        link->channel_layout = link->in_chlayouts->formats[0];
        avfilter_formats_unref(&link->in_chlayouts);
        avfilter_formats_unref(&link->out_chlayouts);

        link->in_packing->format_count = 1;
        link->planar = link->in_packing->formats[0] == AVFILTER_PLANAR;
        avfilter_formats_unref(&link->in_packing);
        avfilter_formats_unref(&link->out_packing);
    }
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
    int ret;

    /* find supported formats from sub-filters, and merge along links */
    if ((ret = query_formats(graph, log_ctx)) < 0)
        return ret;

    /* Once everything is merged, it's possible that we'll still have
     * multiple valid media format choices. We pick the first one. */
    pick_formats(graph);

    return 0;
}

int avfilter_graph_config(AVFilterGraph *graphctx, void *log_ctx)
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

int avfilter_graph_send_command(AVFilterGraph *graph, const char *target, const char *cmd, const char *arg, char *res, int res_len, int flags)
{
    int i, r = AVERROR(ENOSYS);

    if(!graph)
        return r;

    if((flags & AVFILTER_CMD_FLAG_ONE) && !(flags & AVFILTER_CMD_FLAG_FAST)) {
        r=avfilter_graph_send_command(graph, target, cmd, arg, res, res_len, flags | AVFILTER_CMD_FLAG_FAST);
        if(r != AVERROR(ENOSYS))
            return r;
    }

    if(res_len && res)
        res[0]= 0;

    for (i = 0; i < graph->filter_count; i++) {
        AVFilterContext *filter = graph->filters[i];
        if(!strcmp(target, "all") || (filter->name && !strcmp(target, filter->name)) || !strcmp(target, filter->filter->name)){
            r = avfilter_process_command(filter, cmd, arg, res, res_len, flags);
            if(r != AVERROR(ENOSYS)) {
                if((flags & AVFILTER_CMD_FLAG_ONE) || r<0)
                    return r;
            }
        }
    }

    return r;
}

int avfilter_graph_queue_command(AVFilterGraph *graph, const char *target, const char *command, const char *arg, int flags, double ts)
{
    int i;

    if(!graph)
        return 0;

    for (i = 0; i < graph->filter_count; i++) {
        AVFilterContext *filter = graph->filters[i];
        if(filter && (!strcmp(target, "all") || !strcmp(target, filter->name) || !strcmp(target, filter->filter->name))){
            AVFilterCommand **que = &filter->command_queue, *next;
            while(*que && (*que)->time <= ts)
                que = &(*que)->next;
            next= *que;
            *que= av_mallocz(sizeof(AVFilterCommand));
            (*que)->command = av_strdup(command);
            (*que)->arg     = av_strdup(arg);
            (*que)->time    = ts;
            (*que)->flags   = flags;
            (*que)->next    = next;
            if(flags & AVFILTER_CMD_FLAG_ONE)
                return 0;
        }
    }

    return 0;
}
