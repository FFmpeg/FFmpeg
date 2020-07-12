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

#include "config.h"

#include <string.h>

#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/bprint.h"
#include "libavutil/channel_layout.h"
#include "libavutil/imgutils.h"
#include "libavutil/internal.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"

#define FF_INTERNAL_FIELDS 1
#include "framequeue.h"

#include "avfilter.h"
#include "buffersink.h"
#include "formats.h"
#include "internal.h"
#include "thread.h"

#define OFFSET(x) offsetof(AVFilterGraph, x)
#define F AV_OPT_FLAG_FILTERING_PARAM
#define V AV_OPT_FLAG_VIDEO_PARAM
#define A AV_OPT_FLAG_AUDIO_PARAM
static const AVOption filtergraph_options[] = {
    { "thread_type", "Allowed thread types", OFFSET(thread_type), AV_OPT_TYPE_FLAGS,
        { .i64 = AVFILTER_THREAD_SLICE }, 0, INT_MAX, F|V|A, "thread_type" },
        { "slice", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = AVFILTER_THREAD_SLICE }, .flags = F|V|A, .unit = "thread_type" },
    { "threads",     "Maximum number of threads", OFFSET(nb_threads),
        AV_OPT_TYPE_INT,   { .i64 = 0 }, 0, INT_MAX, F|V|A },
    {"scale_sws_opts"       , "default scale filter options"        , OFFSET(scale_sws_opts)        ,
        AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, F|V },
    {"aresample_swr_opts"   , "default aresample filter options"    , OFFSET(aresample_swr_opts)    ,
        AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, F|A },
    { NULL },
};

static const AVClass filtergraph_class = {
    .class_name = "AVFilterGraph",
    .item_name  = av_default_item_name,
    .version    = LIBAVUTIL_VERSION_INT,
    .option     = filtergraph_options,
    .category   = AV_CLASS_CATEGORY_FILTER,
};

#if !HAVE_THREADS
void ff_graph_thread_free(AVFilterGraph *graph)
{
}

int ff_graph_thread_init(AVFilterGraph *graph)
{
    graph->thread_type = 0;
    graph->nb_threads  = 1;
    return 0;
}
#endif

AVFilterGraph *avfilter_graph_alloc(void)
{
    AVFilterGraph *ret = av_mallocz(sizeof(*ret));
    if (!ret)
        return NULL;

    ret->internal = av_mallocz(sizeof(*ret->internal));
    if (!ret->internal) {
        av_freep(&ret);
        return NULL;
    }

    ret->av_class = &filtergraph_class;
    av_opt_set_defaults(ret);
    ff_framequeue_global_init(&ret->internal->frame_queues);

    return ret;
}

void ff_filter_graph_remove_filter(AVFilterGraph *graph, AVFilterContext *filter)
{
    int i, j;
    for (i = 0; i < graph->nb_filters; i++) {
        if (graph->filters[i] == filter) {
            FFSWAP(AVFilterContext*, graph->filters[i],
                   graph->filters[graph->nb_filters - 1]);
            graph->nb_filters--;
            filter->graph = NULL;
            for (j = 0; j<filter->nb_outputs; j++)
                if (filter->outputs[j])
                    filter->outputs[j]->graph = NULL;

            return;
        }
    }
}

void avfilter_graph_free(AVFilterGraph **graph)
{
    if (!*graph)
        return;

    while ((*graph)->nb_filters)
        avfilter_free((*graph)->filters[0]);

    ff_graph_thread_free(*graph);

    av_freep(&(*graph)->sink_links);

    av_freep(&(*graph)->scale_sws_opts);
    av_freep(&(*graph)->aresample_swr_opts);
#if FF_API_LAVR_OPTS
    av_freep(&(*graph)->resample_lavr_opts);
#endif
    av_freep(&(*graph)->filters);
    av_freep(&(*graph)->internal);
    av_freep(graph);
}

int avfilter_graph_create_filter(AVFilterContext **filt_ctx, const AVFilter *filt,
                                 const char *name, const char *args, void *opaque,
                                 AVFilterGraph *graph_ctx)
{
    int ret;

    *filt_ctx = avfilter_graph_alloc_filter(graph_ctx, filt, name);
    if (!*filt_ctx)
        return AVERROR(ENOMEM);

    ret = avfilter_init_str(*filt_ctx, args);
    if (ret < 0)
        goto fail;

    return 0;

fail:
    if (*filt_ctx)
        avfilter_free(*filt_ctx);
    *filt_ctx = NULL;
    return ret;
}

void avfilter_graph_set_auto_convert(AVFilterGraph *graph, unsigned flags)
{
    graph->disable_auto_convert = flags;
}

AVFilterContext *avfilter_graph_alloc_filter(AVFilterGraph *graph,
                                             const AVFilter *filter,
                                             const char *name)
{
    AVFilterContext **filters, *s;

    if (graph->thread_type && !graph->internal->thread_execute) {
        if (graph->execute) {
            graph->internal->thread_execute = graph->execute;
        } else {
            int ret = ff_graph_thread_init(graph);
            if (ret < 0) {
                av_log(graph, AV_LOG_ERROR, "Error initializing threading: %s.\n", av_err2str(ret));
                return NULL;
            }
        }
    }

    s = ff_filter_alloc(filter, name);
    if (!s)
        return NULL;

    filters = av_realloc(graph->filters, sizeof(*filters) * (graph->nb_filters + 1));
    if (!filters) {
        avfilter_free(s);
        return NULL;
    }

    graph->filters = filters;
    graph->filters[graph->nb_filters++] = s;

    s->graph = graph;

    return s;
}

/**
 * Check for the validity of graph.
 *
 * A graph is considered valid if all its input and output pads are
 * connected.
 *
 * @return >= 0 in case of success, a negative value otherwise
 */
static int graph_check_validity(AVFilterGraph *graph, AVClass *log_ctx)
{
    AVFilterContext *filt;
    int i, j;

    for (i = 0; i < graph->nb_filters; i++) {
        const AVFilterPad *pad;
        filt = graph->filters[i];

        for (j = 0; j < filt->nb_inputs; j++) {
            if (!filt->inputs[j] || !filt->inputs[j]->src) {
                pad = &filt->input_pads[j];
                av_log(log_ctx, AV_LOG_ERROR,
                       "Input pad \"%s\" with type %s of the filter instance \"%s\" of %s not connected to any source\n",
                       pad->name, av_get_media_type_string(pad->type), filt->name, filt->filter->name);
                return AVERROR(EINVAL);
            }
        }

        for (j = 0; j < filt->nb_outputs; j++) {
            if (!filt->outputs[j] || !filt->outputs[j]->dst) {
                pad = &filt->output_pads[j];
                av_log(log_ctx, AV_LOG_ERROR,
                       "Output pad \"%s\" with type %s of the filter instance \"%s\" of %s not connected to any destination\n",
                       pad->name, av_get_media_type_string(pad->type), filt->name, filt->filter->name);
                return AVERROR(EINVAL);
            }
        }
    }

    return 0;
}

/**
 * Configure all the links of graphctx.
 *
 * @return >= 0 in case of success, a negative value otherwise
 */
static int graph_config_links(AVFilterGraph *graph, AVClass *log_ctx)
{
    AVFilterContext *filt;
    int i, ret;

    for (i = 0; i < graph->nb_filters; i++) {
        filt = graph->filters[i];

        if (!filt->nb_outputs) {
            if ((ret = avfilter_config_links(filt)))
                return ret;
        }
    }

    return 0;
}

static int graph_check_links(AVFilterGraph *graph, AVClass *log_ctx)
{
    AVFilterContext *f;
    AVFilterLink *l;
    unsigned i, j;
    int ret;

    for (i = 0; i < graph->nb_filters; i++) {
        f = graph->filters[i];
        for (j = 0; j < f->nb_outputs; j++) {
            l = f->outputs[j];
            if (l->type == AVMEDIA_TYPE_VIDEO) {
                ret = av_image_check_size2(l->w, l->h, INT64_MAX, l->format, 0, f);
                if (ret < 0)
                    return ret;
            }
        }
    }
    return 0;
}

AVFilterContext *avfilter_graph_get_filter(AVFilterGraph *graph, const char *name)
{
    int i;

    for (i = 0; i < graph->nb_filters; i++)
        if (graph->filters[i]->name && !strcmp(name, graph->filters[i]->name))
            return graph->filters[i];

    return NULL;
}

static void sanitize_channel_layouts(void *log, AVFilterChannelLayouts *l)
{
    if (!l)
        return;
    if (l->nb_channel_layouts) {
        if (l->all_layouts || l->all_counts)
            av_log(log, AV_LOG_WARNING, "All layouts set on non-empty list\n");
        l->all_layouts = l->all_counts = 0;
    } else {
        if (l->all_counts && !l->all_layouts)
            av_log(log, AV_LOG_WARNING, "All counts without all layouts\n");
        l->all_layouts = 1;
    }
}

static int filter_query_formats(AVFilterContext *ctx)
{
    int ret, i;
    AVFilterFormats *formats;
    AVFilterChannelLayouts *chlayouts;
    AVFilterFormats *samplerates;
    enum AVMediaType type = ctx->inputs  && ctx->inputs [0] ? ctx->inputs [0]->type :
                            ctx->outputs && ctx->outputs[0] ? ctx->outputs[0]->type :
                            AVMEDIA_TYPE_VIDEO;

    if ((ret = ctx->filter->query_formats(ctx)) < 0) {
        if (ret != AVERROR(EAGAIN))
            av_log(ctx, AV_LOG_ERROR, "Query format failed for '%s': %s\n",
                   ctx->name, av_err2str(ret));
        return ret;
    }

    for (i = 0; i < ctx->nb_inputs; i++)
        sanitize_channel_layouts(ctx, ctx->inputs[i]->out_channel_layouts);
    for (i = 0; i < ctx->nb_outputs; i++)
        sanitize_channel_layouts(ctx, ctx->outputs[i]->in_channel_layouts);

    formats = ff_all_formats(type);
    if ((ret = ff_set_common_formats(ctx, formats)) < 0)
        return ret;
    if (type == AVMEDIA_TYPE_AUDIO) {
        samplerates = ff_all_samplerates();
        if ((ret = ff_set_common_samplerates(ctx, samplerates)) < 0)
            return ret;
        chlayouts = ff_all_channel_layouts();
        if ((ret = ff_set_common_channel_layouts(ctx, chlayouts)) < 0)
            return ret;
    }
    return 0;
}

static int formats_declared(AVFilterContext *f)
{
    int i;

    for (i = 0; i < f->nb_inputs; i++) {
        if (!f->inputs[i]->out_formats)
            return 0;
        if (f->inputs[i]->type == AVMEDIA_TYPE_AUDIO &&
            !(f->inputs[i]->out_samplerates &&
              f->inputs[i]->out_channel_layouts))
            return 0;
    }
    for (i = 0; i < f->nb_outputs; i++) {
        if (!f->outputs[i]->in_formats)
            return 0;
        if (f->outputs[i]->type == AVMEDIA_TYPE_AUDIO &&
            !(f->outputs[i]->in_samplerates &&
              f->outputs[i]->in_channel_layouts))
            return 0;
    }
    return 1;
}

static AVFilterFormats *clone_filter_formats(AVFilterFormats *arg)
{
    AVFilterFormats *a = av_memdup(arg, sizeof(*arg));
    if (a) {
        a->refcount = 0;
        a->refs     = NULL;
        a->formats  = av_memdup(a->formats, sizeof(*a->formats) * a->nb_formats);
        if (!a->formats && arg->formats)
            av_freep(&a);
    }
    return a;
}

static int can_merge_formats(AVFilterFormats *a_arg,
                             AVFilterFormats *b_arg,
                             enum AVMediaType type,
                             int is_sample_rate)
{
    AVFilterFormats *a, *b, *ret;
    if (a_arg == b_arg)
        return 1;
    a = clone_filter_formats(a_arg);
    b = clone_filter_formats(b_arg);

    if (!a || !b) {
        if (a)
            av_freep(&a->formats);
        if (b)
            av_freep(&b->formats);

        av_freep(&a);
        av_freep(&b);

        return 0;
    }

    if (is_sample_rate) {
        ret = ff_merge_samplerates(a, b);
    } else {
        ret = ff_merge_formats(a, b, type);
    }
    if (ret) {
        av_freep(&ret->formats);
        av_freep(&ret->refs);
        av_freep(&ret);
        return 1;
    } else {
        if (a)
            av_freep(&a->formats);
        if (b)
            av_freep(&b->formats);
        av_freep(&a);
        av_freep(&b);
        return 0;
    }
}

/**
 * Perform one round of query_formats() and merging formats lists on the
 * filter graph.
 * @return  >=0 if all links formats lists could be queried and merged;
 *          AVERROR(EAGAIN) some progress was made in the queries or merging
 *          and a later call may succeed;
 *          AVERROR(EIO) (may be changed) plus a log message if no progress
 *          was made and the negotiation is stuck;
 *          a negative error code if some other error happened
 */
static int query_formats(AVFilterGraph *graph, AVClass *log_ctx)
{
    int i, j, ret;
    int scaler_count = 0, resampler_count = 0;
    int count_queried = 0;        /* successful calls to query_formats() */
    int count_merged = 0;         /* successful merge of formats lists */
    int count_already_merged = 0; /* lists already merged */
    int count_delayed = 0;        /* lists that need to be merged later */

    for (i = 0; i < graph->nb_filters; i++) {
        AVFilterContext *f = graph->filters[i];
        if (formats_declared(f))
            continue;
        if (f->filter->query_formats)
            ret = filter_query_formats(f);
        else
            ret = ff_default_query_formats(f);
        if (ret < 0 && ret != AVERROR(EAGAIN))
            return ret;
        /* note: EAGAIN could indicate a partial success, not counted yet */
        count_queried += ret >= 0;
    }

    /* go through and merge as many format lists as possible */
    for (i = 0; i < graph->nb_filters; i++) {
        AVFilterContext *filter = graph->filters[i];

        for (j = 0; j < filter->nb_inputs; j++) {
            AVFilterLink *link = filter->inputs[j];
            int convert_needed = 0;

            if (!link)
                continue;

            if (link->in_formats != link->out_formats
                && link->in_formats && link->out_formats)
                if (!can_merge_formats(link->in_formats, link->out_formats,
                                      link->type, 0))
                    convert_needed = 1;
            if (link->type == AVMEDIA_TYPE_AUDIO) {
                if (link->in_samplerates != link->out_samplerates
                    && link->in_samplerates && link->out_samplerates)
                    if (!can_merge_formats(link->in_samplerates,
                                           link->out_samplerates,
                                           0, 1))
                        convert_needed = 1;
            }

#define MERGE_DISPATCH(field, statement)                                     \
            if (!(link->in_ ## field && link->out_ ## field)) {              \
                count_delayed++;                                             \
            } else if (link->in_ ## field == link->out_ ## field) {          \
                count_already_merged++;                                      \
            } else if (!convert_needed) {                                    \
                count_merged++;                                              \
                statement                                                    \
            }

            if (link->type == AVMEDIA_TYPE_AUDIO) {
                MERGE_DISPATCH(channel_layouts,
                    if (!ff_merge_channel_layouts(link->in_channel_layouts,
                                                  link->out_channel_layouts))
                        convert_needed = 1;
                )
                MERGE_DISPATCH(samplerates,
                    if (!ff_merge_samplerates(link->in_samplerates,
                                              link->out_samplerates))
                        convert_needed = 1;
                )
            }
            MERGE_DISPATCH(formats,
                if (!ff_merge_formats(link->in_formats, link->out_formats,
                                      link->type))
                    convert_needed = 1;
            )
#undef MERGE_DISPATCH

            if (convert_needed) {
                AVFilterContext *convert;
                const AVFilter *filter;
                AVFilterLink *inlink, *outlink;
                char inst_name[30];

                if (graph->disable_auto_convert) {
                    av_log(log_ctx, AV_LOG_ERROR,
                           "The filters '%s' and '%s' do not have a common format "
                           "and automatic conversion is disabled.\n",
                           link->src->name, link->dst->name);
                    return AVERROR(EINVAL);
                }

                /* couldn't merge format lists. auto-insert conversion filter */
                switch (link->type) {
                case AVMEDIA_TYPE_VIDEO:
                    if (!(filter = avfilter_get_by_name("scale"))) {
                        av_log(log_ctx, AV_LOG_ERROR, "'scale' filter "
                               "not present, cannot convert pixel formats.\n");
                        return AVERROR(EINVAL);
                    }

                    snprintf(inst_name, sizeof(inst_name), "auto_scaler_%d",
                             scaler_count++);

                    if ((ret = avfilter_graph_create_filter(&convert, filter,
                                                            inst_name, graph->scale_sws_opts, NULL,
                                                            graph)) < 0)
                        return ret;
                    break;
                case AVMEDIA_TYPE_AUDIO:
                    if (!(filter = avfilter_get_by_name("aresample"))) {
                        av_log(log_ctx, AV_LOG_ERROR, "'aresample' filter "
                               "not present, cannot convert audio formats.\n");
                        return AVERROR(EINVAL);
                    }

                    snprintf(inst_name, sizeof(inst_name), "auto_resampler_%d",
                             resampler_count++);
                    if ((ret = avfilter_graph_create_filter(&convert, filter,
                                                            inst_name, graph->aresample_swr_opts,
                                                            NULL, graph)) < 0)
                        return ret;
                    break;
                default:
                    return AVERROR(EINVAL);
                }

                if ((ret = avfilter_insert_filter(link, convert, 0, 0)) < 0)
                    return ret;

                if ((ret = filter_query_formats(convert)) < 0)
                    return ret;

                inlink  = convert->inputs[0];
                outlink = convert->outputs[0];
                av_assert0( inlink-> in_formats->refcount > 0);
                av_assert0( inlink->out_formats->refcount > 0);
                av_assert0(outlink-> in_formats->refcount > 0);
                av_assert0(outlink->out_formats->refcount > 0);
                if (outlink->type == AVMEDIA_TYPE_AUDIO) {
                    av_assert0( inlink-> in_samplerates->refcount > 0);
                    av_assert0( inlink->out_samplerates->refcount > 0);
                    av_assert0(outlink-> in_samplerates->refcount > 0);
                    av_assert0(outlink->out_samplerates->refcount > 0);
                    av_assert0( inlink-> in_channel_layouts->refcount > 0);
                    av_assert0( inlink->out_channel_layouts->refcount > 0);
                    av_assert0(outlink-> in_channel_layouts->refcount > 0);
                    av_assert0(outlink->out_channel_layouts->refcount > 0);
                }
                if (!ff_merge_formats( inlink->in_formats,  inlink->out_formats,  inlink->type) ||
                    !ff_merge_formats(outlink->in_formats, outlink->out_formats, outlink->type))
                    ret = AVERROR(ENOSYS);
                if (inlink->type == AVMEDIA_TYPE_AUDIO &&
                    (!ff_merge_samplerates(inlink->in_samplerates,
                                           inlink->out_samplerates) ||
                     !ff_merge_channel_layouts(inlink->in_channel_layouts,
                                               inlink->out_channel_layouts)))
                    ret = AVERROR(ENOSYS);
                if (outlink->type == AVMEDIA_TYPE_AUDIO &&
                    (!ff_merge_samplerates(outlink->in_samplerates,
                                           outlink->out_samplerates) ||
                     !ff_merge_channel_layouts(outlink->in_channel_layouts,
                                               outlink->out_channel_layouts)))
                    ret = AVERROR(ENOSYS);

                if (ret < 0) {
                    av_log(log_ctx, AV_LOG_ERROR,
                           "Impossible to convert between the formats supported by the filter "
                           "'%s' and the filter '%s'\n", link->src->name, link->dst->name);
                    return ret;
                }
            }
        }
    }

    av_log(graph, AV_LOG_DEBUG, "query_formats: "
           "%d queried, %d merged, %d already done, %d delayed\n",
           count_queried, count_merged, count_already_merged, count_delayed);
    if (count_delayed) {
        AVBPrint bp;

        /* if count_queried > 0, one filter at least did set its formats,
           that will give additional information to its neighbour;
           if count_merged > 0, one pair of formats lists at least was merged,
           that will give additional information to all connected filters;
           in both cases, progress was made and a new round must be done */
        if (count_queried || count_merged)
            return AVERROR(EAGAIN);
        av_bprint_init(&bp, 0, AV_BPRINT_SIZE_AUTOMATIC);
        for (i = 0; i < graph->nb_filters; i++)
            if (!formats_declared(graph->filters[i]))
                av_bprintf(&bp, "%s%s", bp.len ? ", " : "",
                          graph->filters[i]->name);
        av_log(graph, AV_LOG_ERROR,
               "The following filters could not choose their formats: %s\n"
               "Consider inserting the (a)format filter near their input or "
               "output.\n", bp.str);
        return AVERROR(EIO);
    }
    return 0;
}

static int get_fmt_score(enum AVSampleFormat dst_fmt, enum AVSampleFormat src_fmt)
{
    int score = 0;

    if (av_sample_fmt_is_planar(dst_fmt) != av_sample_fmt_is_planar(src_fmt))
        score ++;

    if (av_get_bytes_per_sample(dst_fmt) < av_get_bytes_per_sample(src_fmt)) {
        score += 100 * (av_get_bytes_per_sample(src_fmt) - av_get_bytes_per_sample(dst_fmt));
    }else
        score += 10  * (av_get_bytes_per_sample(dst_fmt) - av_get_bytes_per_sample(src_fmt));

    if (av_get_packed_sample_fmt(dst_fmt) == AV_SAMPLE_FMT_S32 &&
        av_get_packed_sample_fmt(src_fmt) == AV_SAMPLE_FMT_FLT)
        score += 20;

    if (av_get_packed_sample_fmt(dst_fmt) == AV_SAMPLE_FMT_FLT &&
        av_get_packed_sample_fmt(src_fmt) == AV_SAMPLE_FMT_S32)
        score += 2;

    return score;
}

static enum AVSampleFormat find_best_sample_fmt_of_2(enum AVSampleFormat dst_fmt1, enum AVSampleFormat dst_fmt2,
                                                     enum AVSampleFormat src_fmt)
{
    int score1, score2;

    score1 = get_fmt_score(dst_fmt1, src_fmt);
    score2 = get_fmt_score(dst_fmt2, src_fmt);

    return score1 < score2 ? dst_fmt1 : dst_fmt2;
}

static int pick_format(AVFilterLink *link, AVFilterLink *ref)
{
    if (!link || !link->in_formats)
        return 0;

    if (link->type == AVMEDIA_TYPE_VIDEO) {
        if(ref && ref->type == AVMEDIA_TYPE_VIDEO){
            //FIXME: This should check for AV_PIX_FMT_FLAG_ALPHA after PAL8 pixel format without alpha is implemented
            int has_alpha= av_pix_fmt_desc_get(ref->format)->nb_components % 2 == 0;
            enum AVPixelFormat best= AV_PIX_FMT_NONE;
            int i;
            for (i=0; i<link->in_formats->nb_formats; i++) {
                enum AVPixelFormat p = link->in_formats->formats[i];
                best= av_find_best_pix_fmt_of_2(best, p, ref->format, has_alpha, NULL);
            }
            av_log(link->src,AV_LOG_DEBUG, "picking %s out of %d ref:%s alpha:%d\n",
                   av_get_pix_fmt_name(best), link->in_formats->nb_formats,
                   av_get_pix_fmt_name(ref->format), has_alpha);
            link->in_formats->formats[0] = best;
        }
    } else if (link->type == AVMEDIA_TYPE_AUDIO) {
        if(ref && ref->type == AVMEDIA_TYPE_AUDIO){
            enum AVSampleFormat best= AV_SAMPLE_FMT_NONE;
            int i;
            for (i=0; i<link->in_formats->nb_formats; i++) {
                enum AVSampleFormat p = link->in_formats->formats[i];
                best = find_best_sample_fmt_of_2(best, p, ref->format);
            }
            av_log(link->src,AV_LOG_DEBUG, "picking %s out of %d ref:%s\n",
                   av_get_sample_fmt_name(best), link->in_formats->nb_formats,
                   av_get_sample_fmt_name(ref->format));
            link->in_formats->formats[0] = best;
        }
    }

    link->in_formats->nb_formats = 1;
    link->format = link->in_formats->formats[0];

    if (link->type == AVMEDIA_TYPE_AUDIO) {
        if (!link->in_samplerates->nb_formats) {
            av_log(link->src, AV_LOG_ERROR, "Cannot select sample rate for"
                   " the link between filters %s and %s.\n", link->src->name,
                   link->dst->name);
            return AVERROR(EINVAL);
        }
        link->in_samplerates->nb_formats = 1;
        link->sample_rate = link->in_samplerates->formats[0];

        if (link->in_channel_layouts->all_layouts) {
            av_log(link->src, AV_LOG_ERROR, "Cannot select channel layout for"
                   " the link between filters %s and %s.\n", link->src->name,
                   link->dst->name);
            if (!link->in_channel_layouts->all_counts)
                av_log(link->src, AV_LOG_ERROR, "Unknown channel layouts not "
                       "supported, try specifying a channel layout using "
                       "'aformat=channel_layouts=something'.\n");
            return AVERROR(EINVAL);
        }
        link->in_channel_layouts->nb_channel_layouts = 1;
        link->channel_layout = link->in_channel_layouts->channel_layouts[0];
        if ((link->channels = FF_LAYOUT2COUNT(link->channel_layout)))
            link->channel_layout = 0;
        else
            link->channels = av_get_channel_layout_nb_channels(link->channel_layout);
    }

    ff_formats_unref(&link->in_formats);
    ff_formats_unref(&link->out_formats);
    ff_formats_unref(&link->in_samplerates);
    ff_formats_unref(&link->out_samplerates);
    ff_channel_layouts_unref(&link->in_channel_layouts);
    ff_channel_layouts_unref(&link->out_channel_layouts);

    return 0;
}

#define REDUCE_FORMATS(fmt_type, list_type, list, var, nb, add_format, unref_format) \
do {                                                                   \
    for (i = 0; i < filter->nb_inputs; i++) {                          \
        AVFilterLink *link = filter->inputs[i];                        \
        fmt_type fmt;                                                  \
                                                                       \
        if (!link->out_ ## list || link->out_ ## list->nb != 1)        \
            continue;                                                  \
        fmt = link->out_ ## list->var[0];                              \
                                                                       \
        for (j = 0; j < filter->nb_outputs; j++) {                     \
            AVFilterLink *out_link = filter->outputs[j];               \
            list_type *fmts;                                           \
                                                                       \
            if (link->type != out_link->type ||                        \
                out_link->in_ ## list->nb == 1)                        \
                continue;                                              \
            fmts = out_link->in_ ## list;                              \
                                                                       \
            if (!out_link->in_ ## list->nb) {                          \
                if ((ret = add_format(&out_link->in_ ##list, fmt)) < 0)\
                    return ret;                                        \
                ret = 1;                                               \
                break;                                                 \
            }                                                          \
                                                                       \
            for (k = 0; k < out_link->in_ ## list->nb; k++)            \
                if (fmts->var[k] == fmt) {                             \
                    fmts->var[0]  = fmt;                               \
                    fmts->nb = 1;                                      \
                    ret = 1;                                           \
                    break;                                             \
                }                                                      \
        }                                                              \
    }                                                                  \
} while (0)

static int reduce_formats_on_filter(AVFilterContext *filter)
{
    int i, j, k, ret = 0;

    REDUCE_FORMATS(int,      AVFilterFormats,        formats,         formats,
                   nb_formats, ff_add_format, ff_formats_unref);
    REDUCE_FORMATS(int,      AVFilterFormats,        samplerates,     formats,
                   nb_formats, ff_add_format, ff_formats_unref);

    /* reduce channel layouts */
    for (i = 0; i < filter->nb_inputs; i++) {
        AVFilterLink *inlink = filter->inputs[i];
        uint64_t fmt;

        if (!inlink->out_channel_layouts ||
            inlink->out_channel_layouts->nb_channel_layouts != 1)
            continue;
        fmt = inlink->out_channel_layouts->channel_layouts[0];

        for (j = 0; j < filter->nb_outputs; j++) {
            AVFilterLink *outlink = filter->outputs[j];
            AVFilterChannelLayouts *fmts;

            fmts = outlink->in_channel_layouts;
            if (inlink->type != outlink->type || fmts->nb_channel_layouts == 1)
                continue;

            if (fmts->all_layouts &&
                (!FF_LAYOUT2COUNT(fmt) || fmts->all_counts)) {
                /* Turn the infinite list into a singleton */
                fmts->all_layouts = fmts->all_counts  = 0;
                if (ff_add_channel_layout(&outlink->in_channel_layouts, fmt) < 0)
                    ret = 1;
                break;
            }

            for (k = 0; k < outlink->in_channel_layouts->nb_channel_layouts; k++) {
                if (fmts->channel_layouts[k] == fmt) {
                    fmts->channel_layouts[0]  = fmt;
                    fmts->nb_channel_layouts = 1;
                    ret = 1;
                    break;
                }
            }
        }
    }

    return ret;
}

static int reduce_formats(AVFilterGraph *graph)
{
    int i, reduced, ret;

    do {
        reduced = 0;

        for (i = 0; i < graph->nb_filters; i++) {
            if ((ret = reduce_formats_on_filter(graph->filters[i])) < 0)
                return ret;
            reduced |= ret;
        }
    } while (reduced);

    return 0;
}

static void swap_samplerates_on_filter(AVFilterContext *filter)
{
    AVFilterLink *link = NULL;
    int sample_rate;
    int i, j;

    for (i = 0; i < filter->nb_inputs; i++) {
        link = filter->inputs[i];

        if (link->type == AVMEDIA_TYPE_AUDIO &&
            link->out_samplerates->nb_formats== 1)
            break;
    }
    if (i == filter->nb_inputs)
        return;

    sample_rate = link->out_samplerates->formats[0];

    for (i = 0; i < filter->nb_outputs; i++) {
        AVFilterLink *outlink = filter->outputs[i];
        int best_idx, best_diff = INT_MAX;

        if (outlink->type != AVMEDIA_TYPE_AUDIO ||
            outlink->in_samplerates->nb_formats < 2)
            continue;

        for (j = 0; j < outlink->in_samplerates->nb_formats; j++) {
            int diff = abs(sample_rate - outlink->in_samplerates->formats[j]);

            av_assert0(diff < INT_MAX); // This would lead to the use of uninitialized best_diff but is only possible with invalid sample rates

            if (diff < best_diff) {
                best_diff = diff;
                best_idx  = j;
            }
        }
        FFSWAP(int, outlink->in_samplerates->formats[0],
               outlink->in_samplerates->formats[best_idx]);
    }
}

static void swap_samplerates(AVFilterGraph *graph)
{
    int i;

    for (i = 0; i < graph->nb_filters; i++)
        swap_samplerates_on_filter(graph->filters[i]);
}

#define CH_CENTER_PAIR (AV_CH_FRONT_LEFT_OF_CENTER | AV_CH_FRONT_RIGHT_OF_CENTER)
#define CH_FRONT_PAIR  (AV_CH_FRONT_LEFT           | AV_CH_FRONT_RIGHT)
#define CH_STEREO_PAIR (AV_CH_STEREO_LEFT          | AV_CH_STEREO_RIGHT)
#define CH_WIDE_PAIR   (AV_CH_WIDE_LEFT            | AV_CH_WIDE_RIGHT)
#define CH_SIDE_PAIR   (AV_CH_SIDE_LEFT            | AV_CH_SIDE_RIGHT)
#define CH_DIRECT_PAIR (AV_CH_SURROUND_DIRECT_LEFT | AV_CH_SURROUND_DIRECT_RIGHT)
#define CH_BACK_PAIR   (AV_CH_BACK_LEFT            | AV_CH_BACK_RIGHT)

/* allowable substitutions for channel pairs when comparing layouts,
 * ordered by priority for both values */
static const uint64_t ch_subst[][2] = {
    { CH_FRONT_PAIR,      CH_CENTER_PAIR     },
    { CH_FRONT_PAIR,      CH_WIDE_PAIR       },
    { CH_FRONT_PAIR,      AV_CH_FRONT_CENTER },
    { CH_CENTER_PAIR,     CH_FRONT_PAIR      },
    { CH_CENTER_PAIR,     CH_WIDE_PAIR       },
    { CH_CENTER_PAIR,     AV_CH_FRONT_CENTER },
    { CH_WIDE_PAIR,       CH_FRONT_PAIR      },
    { CH_WIDE_PAIR,       CH_CENTER_PAIR     },
    { CH_WIDE_PAIR,       AV_CH_FRONT_CENTER },
    { AV_CH_FRONT_CENTER, CH_FRONT_PAIR      },
    { AV_CH_FRONT_CENTER, CH_CENTER_PAIR     },
    { AV_CH_FRONT_CENTER, CH_WIDE_PAIR       },
    { CH_SIDE_PAIR,       CH_DIRECT_PAIR     },
    { CH_SIDE_PAIR,       CH_BACK_PAIR       },
    { CH_SIDE_PAIR,       AV_CH_BACK_CENTER  },
    { CH_BACK_PAIR,       CH_DIRECT_PAIR     },
    { CH_BACK_PAIR,       CH_SIDE_PAIR       },
    { CH_BACK_PAIR,       AV_CH_BACK_CENTER  },
    { AV_CH_BACK_CENTER,  CH_BACK_PAIR       },
    { AV_CH_BACK_CENTER,  CH_DIRECT_PAIR     },
    { AV_CH_BACK_CENTER,  CH_SIDE_PAIR       },
};

static void swap_channel_layouts_on_filter(AVFilterContext *filter)
{
    AVFilterLink *link = NULL;
    int i, j, k;

    for (i = 0; i < filter->nb_inputs; i++) {
        link = filter->inputs[i];

        if (link->type == AVMEDIA_TYPE_AUDIO &&
            link->out_channel_layouts->nb_channel_layouts == 1)
            break;
    }
    if (i == filter->nb_inputs)
        return;

    for (i = 0; i < filter->nb_outputs; i++) {
        AVFilterLink *outlink = filter->outputs[i];
        int best_idx = -1, best_score = INT_MIN, best_count_diff = INT_MAX;

        if (outlink->type != AVMEDIA_TYPE_AUDIO ||
            outlink->in_channel_layouts->nb_channel_layouts < 2)
            continue;

        for (j = 0; j < outlink->in_channel_layouts->nb_channel_layouts; j++) {
            uint64_t  in_chlayout = link->out_channel_layouts->channel_layouts[0];
            uint64_t out_chlayout = outlink->in_channel_layouts->channel_layouts[j];
            int  in_channels      = av_get_channel_layout_nb_channels(in_chlayout);
            int out_channels      = av_get_channel_layout_nb_channels(out_chlayout);
            int count_diff        = out_channels - in_channels;
            int matched_channels, extra_channels;
            int score = 100000;

            if (FF_LAYOUT2COUNT(in_chlayout) || FF_LAYOUT2COUNT(out_chlayout)) {
                /* Compute score in case the input or output layout encodes
                   a channel count; in this case the score is not altered by
                   the computation afterwards, as in_chlayout and
                   out_chlayout have both been set to 0 */
                if (FF_LAYOUT2COUNT(in_chlayout))
                    in_channels = FF_LAYOUT2COUNT(in_chlayout);
                if (FF_LAYOUT2COUNT(out_chlayout))
                    out_channels = FF_LAYOUT2COUNT(out_chlayout);
                score -= 10000 + FFABS(out_channels - in_channels) +
                         (in_channels > out_channels ? 10000 : 0);
                in_chlayout = out_chlayout = 0;
                /* Let the remaining computation run, even if the score
                   value is not altered */
            }

            /* channel substitution */
            for (k = 0; k < FF_ARRAY_ELEMS(ch_subst); k++) {
                uint64_t cmp0 = ch_subst[k][0];
                uint64_t cmp1 = ch_subst[k][1];
                if (( in_chlayout & cmp0) && (!(out_chlayout & cmp0)) &&
                    (out_chlayout & cmp1) && (!( in_chlayout & cmp1))) {
                    in_chlayout  &= ~cmp0;
                    out_chlayout &= ~cmp1;
                    /* add score for channel match, minus a deduction for
                       having to do the substitution */
                    score += 10 * av_get_channel_layout_nb_channels(cmp1) - 2;
                }
            }

            /* no penalty for LFE channel mismatch */
            if ( (in_chlayout & AV_CH_LOW_FREQUENCY) &&
                (out_chlayout & AV_CH_LOW_FREQUENCY))
                score += 10;
            in_chlayout  &= ~AV_CH_LOW_FREQUENCY;
            out_chlayout &= ~AV_CH_LOW_FREQUENCY;

            matched_channels = av_get_channel_layout_nb_channels(in_chlayout &
                                                                 out_chlayout);
            extra_channels   = av_get_channel_layout_nb_channels(out_chlayout &
                                                                 (~in_chlayout));
            score += 10 * matched_channels - 5 * extra_channels;

            if (score > best_score ||
                (count_diff < best_count_diff && score == best_score)) {
                best_score = score;
                best_idx   = j;
                best_count_diff = count_diff;
            }
        }
        av_assert0(best_idx >= 0);
        FFSWAP(uint64_t, outlink->in_channel_layouts->channel_layouts[0],
               outlink->in_channel_layouts->channel_layouts[best_idx]);
    }

}

static void swap_channel_layouts(AVFilterGraph *graph)
{
    int i;

    for (i = 0; i < graph->nb_filters; i++)
        swap_channel_layouts_on_filter(graph->filters[i]);
}

static void swap_sample_fmts_on_filter(AVFilterContext *filter)
{
    AVFilterLink *link = NULL;
    int format, bps;
    int i, j;

    for (i = 0; i < filter->nb_inputs; i++) {
        link = filter->inputs[i];

        if (link->type == AVMEDIA_TYPE_AUDIO &&
            link->out_formats->nb_formats == 1)
            break;
    }
    if (i == filter->nb_inputs)
        return;

    format = link->out_formats->formats[0];
    bps    = av_get_bytes_per_sample(format);

    for (i = 0; i < filter->nb_outputs; i++) {
        AVFilterLink *outlink = filter->outputs[i];
        int best_idx = -1, best_score = INT_MIN;

        if (outlink->type != AVMEDIA_TYPE_AUDIO ||
            outlink->in_formats->nb_formats < 2)
            continue;

        for (j = 0; j < outlink->in_formats->nb_formats; j++) {
            int out_format = outlink->in_formats->formats[j];
            int out_bps    = av_get_bytes_per_sample(out_format);
            int score;

            if (av_get_packed_sample_fmt(out_format) == format ||
                av_get_planar_sample_fmt(out_format) == format) {
                best_idx   = j;
                break;
            }

            /* for s32 and float prefer double to prevent loss of information */
            if (bps == 4 && out_bps == 8) {
                best_idx = j;
                break;
            }

            /* prefer closest higher or equal bps */
            score = -abs(out_bps - bps);
            if (out_bps >= bps)
                score += INT_MAX/2;

            if (score > best_score) {
                best_score = score;
                best_idx   = j;
            }
        }
        av_assert0(best_idx >= 0);
        FFSWAP(int, outlink->in_formats->formats[0],
               outlink->in_formats->formats[best_idx]);
    }
}

static void swap_sample_fmts(AVFilterGraph *graph)
{
    int i;

    for (i = 0; i < graph->nb_filters; i++)
        swap_sample_fmts_on_filter(graph->filters[i]);

}

static int pick_formats(AVFilterGraph *graph)
{
    int i, j, ret;
    int change;

    do{
        change = 0;
        for (i = 0; i < graph->nb_filters; i++) {
            AVFilterContext *filter = graph->filters[i];
            if (filter->nb_inputs){
                for (j = 0; j < filter->nb_inputs; j++){
                    if(filter->inputs[j]->in_formats && filter->inputs[j]->in_formats->nb_formats == 1) {
                        if ((ret = pick_format(filter->inputs[j], NULL)) < 0)
                            return ret;
                        change = 1;
                    }
                }
            }
            if (filter->nb_outputs){
                for (j = 0; j < filter->nb_outputs; j++){
                    if(filter->outputs[j]->in_formats && filter->outputs[j]->in_formats->nb_formats == 1) {
                        if ((ret = pick_format(filter->outputs[j], NULL)) < 0)
                            return ret;
                        change = 1;
                    }
                }
            }
            if (filter->nb_inputs && filter->nb_outputs && filter->inputs[0]->format>=0) {
                for (j = 0; j < filter->nb_outputs; j++) {
                    if(filter->outputs[j]->format<0) {
                        if ((ret = pick_format(filter->outputs[j], filter->inputs[0])) < 0)
                            return ret;
                        change = 1;
                    }
                }
            }
        }
    }while(change);

    for (i = 0; i < graph->nb_filters; i++) {
        AVFilterContext *filter = graph->filters[i];

        for (j = 0; j < filter->nb_inputs; j++)
            if ((ret = pick_format(filter->inputs[j], NULL)) < 0)
                return ret;
        for (j = 0; j < filter->nb_outputs; j++)
            if ((ret = pick_format(filter->outputs[j], NULL)) < 0)
                return ret;
    }
    return 0;
}

/**
 * Configure the formats of all the links in the graph.
 */
static int graph_config_formats(AVFilterGraph *graph, AVClass *log_ctx)
{
    int ret;

    /* find supported formats from sub-filters, and merge along links */
    while ((ret = query_formats(graph, log_ctx)) == AVERROR(EAGAIN))
        av_log(graph, AV_LOG_DEBUG, "query_formats not finished\n");
    if (ret < 0)
        return ret;

    /* Once everything is merged, it's possible that we'll still have
     * multiple valid media format choices. We try to minimize the amount
     * of format conversion inside filters */
    if ((ret = reduce_formats(graph)) < 0)
        return ret;

    /* for audio filters, ensure the best format, sample rate and channel layout
     * is selected */
    swap_sample_fmts(graph);
    swap_samplerates(graph);
    swap_channel_layouts(graph);

    if ((ret = pick_formats(graph)) < 0)
        return ret;

    return 0;
}

static int graph_config_pointers(AVFilterGraph *graph,
                                             AVClass *log_ctx)
{
    unsigned i, j;
    int sink_links_count = 0, n = 0;
    AVFilterContext *f;
    AVFilterLink **sinks;

    for (i = 0; i < graph->nb_filters; i++) {
        f = graph->filters[i];
        for (j = 0; j < f->nb_inputs; j++) {
            f->inputs[j]->graph     = graph;
            f->inputs[j]->age_index = -1;
        }
        for (j = 0; j < f->nb_outputs; j++) {
            f->outputs[j]->graph    = graph;
            f->outputs[j]->age_index= -1;
        }
        if (!f->nb_outputs) {
            if (f->nb_inputs > INT_MAX - sink_links_count)
                return AVERROR(EINVAL);
            sink_links_count += f->nb_inputs;
        }
    }
    sinks = av_calloc(sink_links_count, sizeof(*sinks));
    if (!sinks)
        return AVERROR(ENOMEM);
    for (i = 0; i < graph->nb_filters; i++) {
        f = graph->filters[i];
        if (!f->nb_outputs) {
            for (j = 0; j < f->nb_inputs; j++) {
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

static int graph_insert_fifos(AVFilterGraph *graph, AVClass *log_ctx)
{
    AVFilterContext *f;
    int i, j, ret;
    int fifo_count = 0;

    for (i = 0; i < graph->nb_filters; i++) {
        f = graph->filters[i];

        for (j = 0; j < f->nb_inputs; j++) {
            AVFilterLink *link = f->inputs[j];
            AVFilterContext *fifo_ctx;
            const AVFilter *fifo;
            char name[32];

            if (!link->dstpad->needs_fifo)
                continue;

            fifo = f->inputs[j]->type == AVMEDIA_TYPE_VIDEO ?
                   avfilter_get_by_name("fifo") :
                   avfilter_get_by_name("afifo");

            snprintf(name, sizeof(name), "auto_fifo_%d", fifo_count++);

            ret = avfilter_graph_create_filter(&fifo_ctx, fifo, name, NULL,
                                               NULL, graph);
            if (ret < 0)
                return ret;

            ret = avfilter_insert_filter(link, fifo_ctx, 0, 0);
            if (ret < 0)
                return ret;
        }
    }

    return 0;
}

int avfilter_graph_config(AVFilterGraph *graphctx, void *log_ctx)
{
    int ret;

    if ((ret = graph_check_validity(graphctx, log_ctx)))
        return ret;
    if ((ret = graph_insert_fifos(graphctx, log_ctx)) < 0)
        return ret;
    if ((ret = graph_config_formats(graphctx, log_ctx)))
        return ret;
    if ((ret = graph_config_links(graphctx, log_ctx)))
        return ret;
    if ((ret = graph_check_links(graphctx, log_ctx)))
        return ret;
    if ((ret = graph_config_pointers(graphctx, log_ctx)))
        return ret;

    return 0;
}

int avfilter_graph_send_command(AVFilterGraph *graph, const char *target, const char *cmd, const char *arg, char *res, int res_len, int flags)
{
    int i, r = AVERROR(ENOSYS);

    if (!graph)
        return r;

    if ((flags & AVFILTER_CMD_FLAG_ONE) && !(flags & AVFILTER_CMD_FLAG_FAST)) {
        r = avfilter_graph_send_command(graph, target, cmd, arg, res, res_len, flags | AVFILTER_CMD_FLAG_FAST);
        if (r != AVERROR(ENOSYS))
            return r;
    }

    if (res_len && res)
        res[0] = 0;

    for (i = 0; i < graph->nb_filters; i++) {
        AVFilterContext *filter = graph->filters[i];
        if (!strcmp(target, "all") || (filter->name && !strcmp(target, filter->name)) || !strcmp(target, filter->filter->name)) {
            r = avfilter_process_command(filter, cmd, arg, res, res_len, flags);
            if (r != AVERROR(ENOSYS)) {
                if ((flags & AVFILTER_CMD_FLAG_ONE) || r < 0)
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

    for (i = 0; i < graph->nb_filters; i++) {
        AVFilterContext *filter = graph->filters[i];
        if(filter && (!strcmp(target, "all") || !strcmp(target, filter->name) || !strcmp(target, filter->filter->name))){
            AVFilterCommand **queue = &filter->command_queue, *next;
            while (*queue && (*queue)->time <= ts)
                queue = &(*queue)->next;
            next = *queue;
            *queue = av_mallocz(sizeof(AVFilterCommand));
            if (!*queue)
                return AVERROR(ENOMEM);

            (*queue)->command = av_strdup(command);
            (*queue)->arg     = av_strdup(arg);
            (*queue)->time    = ts;
            (*queue)->flags   = flags;
            (*queue)->next    = next;
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

    av_assert0(index >= 0);

    while (index) {
        int parent = (index - 1) >> 1;
        if (links[parent]->current_pts_us >= link->current_pts_us)
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

    av_assert0(index >= 0);

    while (1) {
        int child = 2 * index + 1;
        if (child >= graph->sink_links_count)
            break;
        if (child + 1 < graph->sink_links_count &&
            links[child + 1]->current_pts_us < links[child]->current_pts_us)
            child++;
        if (link->current_pts_us < links[child]->current_pts_us)
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
    AVFilterLink *oldest = graph->sink_links[0];
    int64_t frame_count;
    int r;

    while (graph->sink_links_count) {
        oldest = graph->sink_links[0];
        if (oldest->dst->filter->activate) {
            /* For now, buffersink is the only filter implementing activate. */
            r = av_buffersink_get_frame_flags(oldest->dst, NULL,
                                              AV_BUFFERSINK_FLAG_PEEK);
            if (r != AVERROR_EOF)
                return r;
        } else {
            r = ff_request_frame(oldest);
        }
        if (r != AVERROR_EOF)
            break;
        av_log(oldest->dst, AV_LOG_DEBUG, "EOF on sink link %s:%s.\n",
               oldest->dst ? oldest->dst->name : "unknown",
               oldest->dstpad ? oldest->dstpad->name : "unknown");
        /* EOF: remove the link from the heap */
        if (oldest->age_index < --graph->sink_links_count)
            heap_bubble_down(graph, graph->sink_links[graph->sink_links_count],
                             oldest->age_index);
        oldest->age_index = -1;
    }
    if (!graph->sink_links_count)
        return AVERROR_EOF;
    av_assert1(!oldest->dst->filter->activate);
    av_assert1(oldest->age_index >= 0);
    frame_count = oldest->frame_count_out;
    while (frame_count == oldest->frame_count_out) {
        r = ff_filter_graph_run_once(graph);
        if (r == AVERROR(EAGAIN) &&
            !oldest->frame_wanted_out && !oldest->frame_blocked_in &&
            !oldest->status_in)
            ff_request_frame(oldest);
        else if (r < 0)
            return r;
    }
    return 0;
}

int ff_filter_graph_run_once(AVFilterGraph *graph)
{
    AVFilterContext *filter;
    unsigned i;

    av_assert0(graph->nb_filters);
    filter = graph->filters[0];
    for (i = 1; i < graph->nb_filters; i++)
        if (graph->filters[i]->ready > filter->ready)
            filter = graph->filters[i];
    if (!filter->ready)
        return AVERROR(EAGAIN);
    return ff_filter_activate(filter);
}
