/*
 * filter graphs
 * Copyright (c) 2008 Vitor Sessak
 * Copyright (c) 2007 Bobby Bingham
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "config.h"

#include <string.h>

#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/channel_layout.h"
#include "libavutil/common.h"
#include "libavutil/internal.h"
#include "libavutil/log.h"
#include "libavutil/opt.h"

#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "thread.h"

#define OFFSET(x) offsetof(AVFilterGraph, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM
static const AVOption filtergraph_options[] = {
    { "thread_type", "Allowed thread types", OFFSET(thread_type), AV_OPT_TYPE_FLAGS,
        { .i64 = AVFILTER_THREAD_SLICE }, 0, INT_MAX, FLAGS, "thread_type" },
        { "slice", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = AVFILTER_THREAD_SLICE }, .flags = FLAGS, .unit = "thread_type" },
    { "threads",     "Maximum number of threads", OFFSET(nb_threads),
        AV_OPT_TYPE_INT,   { .i64 = 0 }, 0, INT_MAX, FLAGS },
    { NULL },
};

static const AVClass filtergraph_class = {
    .class_name = "AVFilterGraph",
    .item_name  = av_default_item_name,
    .version    = LIBAVUTIL_VERSION_INT,
    .option     = filtergraph_options,
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

    return ret;
}

void ff_filter_graph_remove_filter(AVFilterGraph *graph, AVFilterContext *filter)
{
    int i;
    for (i = 0; i < graph->nb_filters; i++) {
        if (graph->filters[i] == filter) {
            FFSWAP(AVFilterContext*, graph->filters[i],
                   graph->filters[graph->nb_filters - 1]);
            graph->nb_filters--;
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

    av_freep(&(*graph)->scale_sws_opts);
    av_freep(&(*graph)->resample_lavr_opts);
    av_freep(&(*graph)->filters);
    av_freep(&(*graph)->internal);
    av_freep(graph);
}

#if FF_API_AVFILTER_OPEN
int avfilter_graph_add_filter(AVFilterGraph *graph, AVFilterContext *filter)
{
    AVFilterContext **filters = av_realloc(graph->filters,
                                           sizeof(*filters) * (graph->nb_filters + 1));
    if (!filters)
        return AVERROR(ENOMEM);

    graph->filters = filters;
    graph->filters[graph->nb_filters++] = filter;

    filter->graph = graph;

    return 0;
}
#endif

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
                av_log(graph, AV_LOG_ERROR, "Error initializing threading.\n");
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
 * @return 0 in case of success, a negative value otherwise
 */
static int graph_check_validity(AVFilterGraph *graph, AVClass *log_ctx)
{
    AVFilterContext *filt;
    int i, j;

    for (i = 0; i < graph->nb_filters; i++) {
        filt = graph->filters[i];

        for (j = 0; j < filt->nb_inputs; j++) {
            if (!filt->inputs[j] || !filt->inputs[j]->src) {
                av_log(log_ctx, AV_LOG_ERROR,
                       "Input pad \"%s\" for the filter \"%s\" of type \"%s\" not connected to any source\n",
                       filt->input_pads[j].name, filt->name, filt->filter->name);
                return AVERROR(EINVAL);
            }
        }

        for (j = 0; j < filt->nb_outputs; j++) {
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

/**
 * Configure all the links of graphctx.
 *
 * @return 0 in case of success, a negative value otherwise
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

AVFilterContext *avfilter_graph_get_filter(AVFilterGraph *graph, char *name)
{
    int i;

    for (i = 0; i < graph->nb_filters; i++)
        if (graph->filters[i]->name && !strcmp(name, graph->filters[i]->name))
            return graph->filters[i];

    return NULL;
}

static int query_formats(AVFilterGraph *graph, AVClass *log_ctx)
{
    int i, j, ret;
    int scaler_count = 0, resampler_count = 0;

    /* ask all the sub-filters for their supported media formats */
    for (i = 0; i < graph->nb_filters; i++) {
        if (graph->filters[i]->filter->query_formats)
            ret = graph->filters[i]->filter->query_formats(graph->filters[i]);
        else
            ret = ff_default_query_formats(graph->filters[i]);
        if (ret < 0) {
            av_log(log_ctx, AV_LOG_ERROR,
                   "Error querying formats for the filter %s (%s)\n",
                   graph->filters[i]->name, graph->filters[i]->filter->name);
            return ret;
        }
    }

    /* go through and merge as many format lists as possible */
    for (i = 0; i < graph->nb_filters; i++) {
        AVFilterContext *filter = graph->filters[i];

        for (j = 0; j < filter->nb_inputs; j++) {
            AVFilterLink *link = filter->inputs[j];
            int convert_needed = 0;

            if (!link)
                continue;

            if (link->in_formats != link->out_formats &&
                !ff_merge_formats(link->in_formats,
                                        link->out_formats))
                convert_needed = 1;
            if (link->type == AVMEDIA_TYPE_AUDIO) {
                if (link->in_channel_layouts != link->out_channel_layouts &&
                    !ff_merge_channel_layouts(link->in_channel_layouts,
                                              link->out_channel_layouts))
                    convert_needed = 1;
                if (link->in_samplerates != link->out_samplerates &&
                    !ff_merge_samplerates(link->in_samplerates,
                                          link->out_samplerates))
                    convert_needed = 1;
            }

            if (convert_needed) {
                AVFilterContext *convert;
                AVFilter *filter;
                AVFilterLink *inlink, *outlink;
                char scale_args[256];
                char inst_name[30];

                /* couldn't merge format lists. auto-insert conversion filter */
                switch (link->type) {
                case AVMEDIA_TYPE_VIDEO:
                    if (!(filter = avfilter_get_by_name("scale"))) {
                        av_log(log_ctx, AV_LOG_ERROR, "'scale' filter "
                               "not present, cannot convert pixel formats.\n");
                        return AVERROR(EINVAL);
                    }

                    snprintf(inst_name, sizeof(inst_name), "auto-inserted scaler %d",
                             scaler_count++);

                    if ((ret = avfilter_graph_create_filter(&convert, filter,
                                                            inst_name, graph->scale_sws_opts, NULL,
                                                            graph)) < 0)
                        return ret;
                    break;
                case AVMEDIA_TYPE_AUDIO:
                    if (!(filter = avfilter_get_by_name("resample"))) {
                        av_log(log_ctx, AV_LOG_ERROR, "'resample' filter "
                               "not present, cannot convert audio formats.\n");
                        return AVERROR(EINVAL);
                    }

                    snprintf(inst_name, sizeof(inst_name), "auto-inserted resampler %d",
                             resampler_count++);
                    scale_args[0] = '\0';
                    if (graph->resample_lavr_opts)
                        snprintf(scale_args, sizeof(scale_args), "%s",
                                 graph->resample_lavr_opts);
                    if ((ret = avfilter_graph_create_filter(&convert, filter,
                                                            inst_name, scale_args,
                                                            NULL, graph)) < 0)
                        return ret;
                    break;
                default:
                    return AVERROR(EINVAL);
                }

                if ((ret = avfilter_insert_filter(link, convert, 0, 0)) < 0)
                    return ret;

                convert->filter->query_formats(convert);
                inlink  = convert->inputs[0];
                outlink = convert->outputs[0];
                if (!ff_merge_formats( inlink->in_formats,  inlink->out_formats) ||
                    !ff_merge_formats(outlink->in_formats, outlink->out_formats))
                    ret |= AVERROR(ENOSYS);
                if (inlink->type == AVMEDIA_TYPE_AUDIO &&
                    (!ff_merge_samplerates(inlink->in_samplerates,
                                           inlink->out_samplerates) ||
                     !ff_merge_channel_layouts(inlink->in_channel_layouts,
                                               inlink->out_channel_layouts)))
                    ret |= AVERROR(ENOSYS);
                if (outlink->type == AVMEDIA_TYPE_AUDIO &&
                    (!ff_merge_samplerates(outlink->in_samplerates,
                                           outlink->out_samplerates) ||
                     !ff_merge_channel_layouts(outlink->in_channel_layouts,
                                               outlink->out_channel_layouts)))
                    ret |= AVERROR(ENOSYS);

                if (ret < 0) {
                    av_log(log_ctx, AV_LOG_ERROR,
                           "Impossible to convert between the formats supported by the filter "
                           "'%s' and the filter '%s'\n", link->src->name, link->dst->name);
                    return ret;
                }
            }
        }
    }

    return 0;
}

static int pick_format(AVFilterLink *link)
{
    if (!link || !link->in_formats)
        return 0;

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

        if (!link->in_channel_layouts->nb_channel_layouts) {
            av_log(link->src, AV_LOG_ERROR, "Cannot select channel layout for"
                   "the link between filters %s and %s.\n", link->src->name,
                   link->dst->name);
            return AVERROR(EINVAL);
        }
        link->in_channel_layouts->nb_channel_layouts = 1;
        link->channel_layout = link->in_channel_layouts->channel_layouts[0];
    }

    ff_formats_unref(&link->in_formats);
    ff_formats_unref(&link->out_formats);
    ff_formats_unref(&link->in_samplerates);
    ff_formats_unref(&link->out_samplerates);
    ff_channel_layouts_unref(&link->in_channel_layouts);
    ff_channel_layouts_unref(&link->out_channel_layouts);

    return 0;
}

#define REDUCE_FORMATS(fmt_type, list_type, list, var, nb, add_format) \
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
                add_format(&out_link->in_ ##list, fmt);                \
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
                   nb_formats, ff_add_format);
    REDUCE_FORMATS(int,      AVFilterFormats,        samplerates,     formats,
                   nb_formats, ff_add_format);
    REDUCE_FORMATS(uint64_t, AVFilterChannelLayouts, channel_layouts,
                   channel_layouts, nb_channel_layouts, ff_add_channel_layout);

    return ret;
}

static void reduce_formats(AVFilterGraph *graph)
{
    int i, reduced;

    do {
        reduced = 0;

        for (i = 0; i < graph->nb_filters; i++)
            reduced |= reduce_formats_on_filter(graph->filters[i]);
    } while (reduced);
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
            int score = 0;

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

    for (i = 0; i < graph->nb_filters; i++) {
        AVFilterContext *filter = graph->filters[i];

        for (j = 0; j < filter->nb_inputs; j++)
            if ((ret = pick_format(filter->inputs[j])) < 0)
                return ret;
        for (j = 0; j < filter->nb_outputs; j++)
            if ((ret = pick_format(filter->outputs[j])) < 0)
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
    if ((ret = query_formats(graph, log_ctx)) < 0)
        return ret;

    /* Once everything is merged, it's possible that we'll still have
     * multiple valid media format choices. We try to minimize the amount
     * of format conversion inside filters */
    reduce_formats(graph);

    /* for audio filters, ensure the best format, sample rate and channel layout
     * is selected */
    swap_sample_fmts(graph);
    swap_samplerates(graph);
    swap_channel_layouts(graph);

    if ((ret = pick_formats(graph)) < 0)
        return ret;

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
            AVFilter *fifo;
            char name[32];

            if (!link->dstpad->needs_fifo)
                continue;

            fifo = f->inputs[j]->type == AVMEDIA_TYPE_VIDEO ?
                   avfilter_get_by_name("fifo") :
                   avfilter_get_by_name("afifo");

            snprintf(name, sizeof(name), "auto-inserted fifo %d", fifo_count++);

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

    return 0;
}
