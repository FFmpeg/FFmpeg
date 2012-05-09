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
#include "libavutil/avassert.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "avfiltergraph.h"
#include "internal.h"

#include "libavutil/log.h"

static const AVClass filtergraph_class = {
    .class_name = "AVFilterGraph",
    .item_name  = av_default_item_name,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVFilterGraph *avfilter_graph_alloc(void)
{
    AVFilterGraph *ret = av_mallocz(sizeof(AVFilterGraph));
    if (!ret)
        return NULL;
#if FF_API_GRAPH_AVCLASS
    ret->av_class = &filtergraph_class;
#endif
    return ret;
}

void avfilter_graph_free(AVFilterGraph **graph)
{
    if (!*graph)
        return;
    for (; (*graph)->filter_count > 0; (*graph)->filter_count--)
        avfilter_free((*graph)->filters[(*graph)->filter_count - 1]);
    av_freep(&(*graph)->sink_links);
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
    AVFilterFormats *formats, *chlayouts, *packing;

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

                /* Merge all three list before checking: that way, in all
                 * three categories, aconvert will use a common format
                 * whenever possible. */
                formats   = avfilter_merge_formats(link->in_formats,   link->out_formats);
                chlayouts = avfilter_merge_formats(link->in_chlayouts, link->out_chlayouts);
                packing   = avfilter_merge_formats(link->in_packing,   link->out_packing);
                if (!formats || !chlayouts || !packing)
                    if (ret = insert_conv_filter(graph, link, "aconvert", NULL))
                       return ret;
            }
        }
    }

    return 0;
}

static void pick_format(AVFilterLink *link, AVFilterLink *ref)
{
    if (!link || !link->in_formats)
        return;

    if (link->type == AVMEDIA_TYPE_VIDEO) {
        if(ref && ref->type == AVMEDIA_TYPE_VIDEO){
            int has_alpha= av_pix_fmt_descriptors[ref->format].nb_components % 2 == 0;
            enum PixelFormat best= PIX_FMT_NONE;
            int i;
            for (i=0; i<link->in_formats->format_count; i++) {
                enum PixelFormat p = link->in_formats->formats[i];
                best= avcodec_find_best_pix_fmt2(best, p, ref->format, has_alpha, NULL);
            }
            link->in_formats->formats[0] = best;
        }
    }

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

static int reduce_formats_on_filter(AVFilterContext *filter)
{
    int i, j, k, ret = 0;

    for (i = 0; i < filter->input_count; i++) {
        AVFilterLink *link = filter->inputs[i];
        int         format = link->out_formats->formats[0];

        if (link->out_formats->format_count != 1)
            continue;

        for (j = 0; j < filter->output_count; j++) {
            AVFilterLink *out_link = filter->outputs[j];
            AVFilterFormats  *fmts = out_link->in_formats;

            if (link->type != out_link->type ||
                out_link->in_formats->format_count == 1)
                continue;

            for (k = 0; k < out_link->in_formats->format_count; k++)
                if (fmts->formats[k] == format) {
                    fmts->formats[0]   = format;
                    fmts->format_count = 1;
                    ret = 1;
                    break;
                }
        }
    }
    return ret;
}

static void reduce_formats(AVFilterGraph *graph)
{
    int i, reduced;

    do {
        reduced = 0;

        for (i = 0; i < graph->filter_count; i++)
            reduced |= reduce_formats_on_filter(graph->filters[i]);
    } while (reduced);
}

static void pick_formats(AVFilterGraph *graph)
{
    int i, j;
    int change;

    do{
        change = 0;
        for (i = 0; i < graph->filter_count; i++) {
            AVFilterContext *filter = graph->filters[i];
            if (filter->input_count){
                for (j = 0; j < filter->input_count; j++){
                    if(filter->inputs[j]->in_formats && filter->inputs[j]->in_formats->format_count == 1) {
                        pick_format(filter->inputs[j], NULL);
                        change = 1;
                    }
                }
            }
            if (filter->output_count){
                for (j = 0; j < filter->output_count; j++){
                    if(filter->outputs[j]->in_formats && filter->outputs[j]->in_formats->format_count == 1) {
                        pick_format(filter->outputs[j], NULL);
                        change = 1;
                    }
                }
            }
            if (filter->input_count && filter->output_count && filter->inputs[0]->format>=0) {
                for (j = 0; j < filter->output_count; j++) {
                    if(filter->outputs[j]->format<0) {
                        pick_format(filter->outputs[j], filter->inputs[0]);
                        change = 1;
                    }
                }
            }
        }
    }while(change);

    for (i = 0; i < graph->filter_count; i++) {
        AVFilterContext *filter = graph->filters[i];
        if (1) {
            for (j = 0; j < filter->input_count; j++)
                pick_format(filter->inputs[j], NULL);
            for (j = 0; j < filter->output_count; j++)
                pick_format(filter->outputs[j], NULL);
        }
    }
}

int ff_avfilter_graph_config_formats(AVFilterGraph *graph, AVClass *log_ctx)
{
    int ret;

    /* find supported formats from sub-filters, and merge along links */
    if ((ret = query_formats(graph, log_ctx)) < 0)
        return ret;

    /* Once everything is merged, it's possible that we'll still have
     * multiple valid media format choices. We try to minimize the amount
     * of format conversion inside filters */
    reduce_formats(graph);

    pick_formats(graph);

    return 0;
}

static int ff_avfilter_graph_config_pointers(AVFilterGraph *graph,
                                             AVClass *log_ctx)
{
    unsigned i, j;
    int sink_links_count = 0, n = 0;
    AVFilterContext *f;
    AVFilterLink **sinks;

    for (i = 0; i < graph->filter_count; i++) {
        f = graph->filters[i];
        for (j = 0; j < f->input_count; j++) {
            f->inputs[j]->graph     = graph;
            f->inputs[j]->age_index = -1;
        }
        for (j = 0; j < f->output_count; j++) {
            f->outputs[j]->graph    = graph;
            f->outputs[j]->age_index= -1;
        }
        if (!f->output_count) {
            if (f->input_count > INT_MAX - sink_links_count)
                return AVERROR(EINVAL);
            sink_links_count += f->input_count;
        }
    }
    sinks = av_calloc(sink_links_count, sizeof(*sinks));
    if (!sinks)
        return AVERROR(ENOMEM);
    for (i = 0; i < graph->filter_count; i++) {
        f = graph->filters[i];
        if (!f->output_count) {
            for (j = 0; j < f->input_count; j++) {
                sinks[n] = f->inputs[j];
                f->inputs[j]->age_index = n++;
            }
        }
    }
    av_assert0(n == sink_links_count);
    graph->sink_links       = sinks;
    graph->sink_links_count = sink_links_count;
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
    if ((ret = ff_avfilter_graph_config_pointers(graphctx, log_ctx)))
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

static void heap_bubble_up(AVFilterGraph *graph,
                           AVFilterLink *link, int index)
{
    AVFilterLink **links = graph->sink_links;

    while (index) {
        int parent = (index - 1) >> 1;
        if (links[parent]->current_pts >= link->current_pts)
            break;
        links[index] = links[parent];
        links[index]->age_index = index;
        index = parent;
    }
    links[index] = link;
    link->age_index = index;
}

static void heap_bubble_down(AVFilterGraph *graph,
                             AVFilterLink *link, int index)
{
    AVFilterLink **links = graph->sink_links;

    while (1) {
        int child = 2 * index + 1;
        if (child >= graph->sink_links_count)
            break;
        if (child + 1 < graph->sink_links_count &&
            links[child + 1]->current_pts < links[child]->current_pts)
            child++;
        if (link->current_pts < links[child]->current_pts)
            break;
        links[index] = links[child];
        links[index]->age_index = index;
        index = child;
    }
    links[index] = link;
    link->age_index = index;
}

void ff_avfilter_graph_update_heap(AVFilterGraph *graph, AVFilterLink *link)
{
    heap_bubble_up  (graph, link, link->age_index);
    heap_bubble_down(graph, link, link->age_index);
}


int avfilter_graph_request_oldest(AVFilterGraph *graph)
{
    while (graph->sink_links_count) {
        AVFilterLink *oldest = graph->sink_links[0];
        int r = avfilter_request_frame(oldest);
        if (r != AVERROR_EOF)
            return r;
        /* EOF: remove the link from the heap */
        if (oldest->age_index < --graph->sink_links_count)
            heap_bubble_down(graph, graph->sink_links[graph->sink_links_count],
                             oldest->age_index);
        oldest->age_index = -1;
    }
    return AVERROR_EOF;
}
