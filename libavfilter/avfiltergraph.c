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

#include <ctype.h>
#include <string.h>

#include "avfilter.h"
#include "avfiltergraph.h"
#include "formats.h"
#include "internal.h"

#include "libavutil/audioconvert.h"
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

/**
 * Configure all the links of graphctx.
 *
 * @return 0 in case of success, a negative value otherwise
 */
static int graph_config_links(AVFilterGraph *graph, AVClass *log_ctx)
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
    int scaler_count = 0, resampler_count = 0;

    /* ask all the sub-filters for their supported media formats */
    for (i = 0; i < graph->filter_count; i++) {
        if (graph->filters[i]->filter->query_formats)
            graph->filters[i]->filter->query_formats(graph->filters[i]);
        else
            ff_default_query_formats(graph->filters[i]);
    }

    /* go through and merge as many format lists as possible */
    for (i = 0; i < graph->filter_count; i++) {
        AVFilterContext *filter = graph->filters[i];

        for (j = 0; j < filter->input_count; j++) {
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
                    snprintf(inst_name, sizeof(inst_name), "auto-inserted scaler %d",
                             scaler_count++);
                    snprintf(scale_args, sizeof(scale_args), "0:0:%s", graph->scale_sws_opts);
                    if ((ret = avfilter_graph_create_filter(&convert,
                                                            avfilter_get_by_name("scale"),
                                                            inst_name, scale_args, NULL,
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
                    if ((ret = avfilter_graph_create_filter(&convert,
                                                            avfilter_get_by_name("resample"),
                                                            inst_name, NULL, NULL, graph)) < 0)
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

    link->in_formats->format_count = 1;
    link->format = link->in_formats->formats[0];

    if (link->type == AVMEDIA_TYPE_AUDIO) {
        if (!link->in_samplerates->format_count) {
            av_log(link->src, AV_LOG_ERROR, "Cannot select sample rate for"
                   " the link between filters %s and %s.\n", link->src->name,
                   link->dst->name);
            return AVERROR(EINVAL);
        }
        link->in_samplerates->format_count = 1;
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
    for (i = 0; i < filter->input_count; i++) {                        \
        AVFilterLink *link = filter->inputs[i];                        \
        fmt_type fmt;                                                  \
                                                                       \
        if (!link->out_ ## list || link->out_ ## list->nb != 1)        \
            continue;                                                  \
        fmt = link->out_ ## list->var[0];                              \
                                                                       \
        for (j = 0; j < filter->output_count; j++) {                   \
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
                   format_count, ff_add_format);
    REDUCE_FORMATS(int,      AVFilterFormats,        samplerates,     formats,
                   format_count, ff_add_format);
    REDUCE_FORMATS(uint64_t, AVFilterChannelLayouts, channel_layouts,
                   channel_layouts, nb_channel_layouts, ff_add_channel_layout);

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

static void swap_samplerates_on_filter(AVFilterContext *filter)
{
    AVFilterLink *link = NULL;
    int sample_rate;
    int i, j;

    for (i = 0; i < filter->input_count; i++) {
        link = filter->inputs[i];

        if (link->type == AVMEDIA_TYPE_AUDIO &&
            link->out_samplerates->format_count == 1)
            break;
    }
    if (i == filter->input_count)
        return;

    sample_rate = link->out_samplerates->formats[0];

    for (i = 0; i < filter->output_count; i++) {
        AVFilterLink *outlink = filter->outputs[i];
        int best_idx, best_diff = INT_MAX;

        if (outlink->type != AVMEDIA_TYPE_AUDIO ||
            outlink->in_samplerates->format_count < 2)
            continue;

        for (j = 0; j < outlink->in_samplerates->format_count; j++) {
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

    for (i = 0; i < graph->filter_count; i++)
        swap_samplerates_on_filter(graph->filters[i]);
}

static void swap_channel_layouts_on_filter(AVFilterContext *filter)
{
    AVFilterLink *link = NULL;
    uint64_t chlayout;
    int i, j;

    for (i = 0; i < filter->input_count; i++) {
        link = filter->inputs[i];

        if (link->type == AVMEDIA_TYPE_AUDIO &&
            link->out_channel_layouts->nb_channel_layouts == 1)
            break;
    }
    if (i == filter->input_count)
        return;

    chlayout = link->out_channel_layouts->channel_layouts[0];

    for (i = 0; i < filter->output_count; i++) {
        AVFilterLink *outlink = filter->outputs[i];
        int best_idx, best_score = INT_MIN;

        if (outlink->type != AVMEDIA_TYPE_AUDIO ||
            outlink->in_channel_layouts->nb_channel_layouts < 2)
            continue;

        for (j = 0; j < outlink->in_channel_layouts->nb_channel_layouts; j++) {
            uint64_t out_chlayout = outlink->in_channel_layouts->channel_layouts[j];
            int matched_channels  = av_get_channel_layout_nb_channels(chlayout &
                                                                      out_chlayout);
            int extra_channels     = av_get_channel_layout_nb_channels(out_chlayout &
                                                                       (~chlayout));
            int score = matched_channels - extra_channels;

            if (score > best_score) {
                best_score = score;
                best_idx   = j;
            }
        }
        FFSWAP(uint64_t, outlink->in_channel_layouts->channel_layouts[0],
               outlink->in_channel_layouts->channel_layouts[best_idx]);
    }

}

static void swap_channel_layouts(AVFilterGraph *graph)
{
    int i;

    for (i = 0; i < graph->filter_count; i++)
        swap_channel_layouts_on_filter(graph->filters[i]);
}

static void swap_sample_fmts_on_filter(AVFilterContext *filter)
{
    AVFilterLink *link = NULL;
    int format, bps;
    int i, j;

    for (i = 0; i < filter->input_count; i++) {
        link = filter->inputs[i];

        if (link->type == AVMEDIA_TYPE_AUDIO &&
            link->out_formats->format_count == 1)
            break;
    }
    if (i == filter->input_count)
        return;

    format = link->out_formats->formats[0];
    bps    = av_get_bytes_per_sample(format);

    for (i = 0; i < filter->output_count; i++) {
        AVFilterLink *outlink = filter->outputs[i];
        int best_idx, best_score = INT_MIN;

        if (outlink->type != AVMEDIA_TYPE_AUDIO ||
            outlink->in_formats->format_count < 2)
            continue;

        for (j = 0; j < outlink->in_formats->format_count; j++) {
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
        FFSWAP(int, outlink->in_formats->formats[0],
               outlink->in_formats->formats[best_idx]);
    }
}

static void swap_sample_fmts(AVFilterGraph *graph)
{
    int i;

    for (i = 0; i < graph->filter_count; i++)
        swap_sample_fmts_on_filter(graph->filters[i]);

}

static int pick_formats(AVFilterGraph *graph)
{
    int i, j, ret;

    for (i = 0; i < graph->filter_count; i++) {
        AVFilterContext *filter = graph->filters[i];

        for (j = 0; j < filter->input_count; j++)
            if ((ret = pick_format(filter->inputs[j])) < 0)
                return ret;
        for (j = 0; j < filter->output_count; j++)
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

int avfilter_graph_config(AVFilterGraph *graphctx, void *log_ctx)
{
    int ret;

    if ((ret = graph_check_validity(graphctx, log_ctx)))
        return ret;
    if ((ret = graph_config_formats(graphctx, log_ctx)))
        return ret;
    if ((ret = graph_config_links(graphctx, log_ctx)))
        return ret;

    return 0;
}
