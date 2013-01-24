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

#include "libavutil/avassert.h"
#include "libavutil/channel_layout.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavcodec/avcodec.h" // avcodec_find_best_pix_fmt_of_2()
#include "avfilter.h"
#include "avfiltergraph.h"
#include "formats.h"
#include "internal.h"

#define OFFSET(x) offsetof(AVFilterGraph,x)

static const AVOption options[]={
{"scale_sws_opts"       , "default scale filter options"        , OFFSET(scale_sws_opts)        ,  AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, 0 },
{"aresample_swr_opts"   , "default aresample filter options"    , OFFSET(aresample_swr_opts)    ,  AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, 0 },
{0}
};


static const AVClass filtergraph_class = {
    .class_name = "AVFilterGraph",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
    .category   = AV_CLASS_CATEGORY_FILTER,
};

AVFilterGraph *avfilter_graph_alloc(void)
{
    AVFilterGraph *ret = av_mallocz(sizeof(AVFilterGraph));
    if (!ret)
        return NULL;
    ret->av_class = &filtergraph_class;
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
    av_freep(&(*graph)->aresample_swr_opts);
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

void avfilter_graph_set_auto_convert(AVFilterGraph *graph, unsigned flags)
{
    graph->disable_auto_convert = flags;
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
 * @return 0 in case of success, a negative value otherwise
 */
static int graph_config_links(AVFilterGraph *graph, AVClass *log_ctx)
{
    AVFilterContext *filt;
    int i, ret;

    for (i=0; i < graph->filter_count; i++) {
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

    for (i = 0; i < graph->filter_count; i++)
        if (graph->filters[i]->name && !strcmp(name, graph->filters[i]->name))
            return graph->filters[i];

    return NULL;
}

static int filter_query_formats(AVFilterContext *ctx)
{
    int ret;
    AVFilterFormats *formats;
    AVFilterChannelLayouts *chlayouts;
    AVFilterFormats *samplerates;
    enum AVMediaType type = ctx->inputs  && ctx->inputs [0] ? ctx->inputs [0]->type :
                            ctx->outputs && ctx->outputs[0] ? ctx->outputs[0]->type :
                            AVMEDIA_TYPE_VIDEO;

    if ((ret = ctx->filter->query_formats(ctx)) < 0) {
        av_log(ctx, AV_LOG_ERROR, "Query format failed for '%s': %s\n",
               ctx->name, av_err2str(ret));
        return ret;
    }

    formats = ff_all_formats(type);
    if (!formats)
        return AVERROR(ENOMEM);
    ff_set_common_formats(ctx, formats);
    if (type == AVMEDIA_TYPE_AUDIO) {
        samplerates = ff_all_samplerates();
        if (!samplerates)
            return AVERROR(ENOMEM);
        ff_set_common_samplerates(ctx, samplerates);
        chlayouts = ff_all_channel_layouts();
        if (!chlayouts)
            return AVERROR(ENOMEM);
        ff_set_common_channel_layouts(ctx, chlayouts);
    }
    return 0;
}

static int insert_conv_filter(AVFilterGraph *graph, AVFilterLink *link,
                              const char *filt_name, const char *filt_args)
{
    static int auto_count = 0, ret;
    char inst_name[32];
    AVFilterContext *filt_ctx;

    if (graph->disable_auto_convert) {
        av_log(NULL, AV_LOG_ERROR,
               "The filters '%s' and '%s' do not have a common format "
               "and automatic conversion is disabled.\n",
               link->src->name, link->dst->name);
        return AVERROR(EINVAL);
    }

    snprintf(inst_name, sizeof(inst_name), "auto-inserted %s %d",
            filt_name, auto_count++);

    if ((ret = avfilter_graph_create_filter(&filt_ctx,
                                            avfilter_get_by_name(filt_name),
                                            inst_name, filt_args, NULL, graph)) < 0)
        return ret;
    if ((ret = avfilter_insert_filter(link, filt_ctx, 0, 0)) < 0)
        return ret;

    filter_query_formats(filt_ctx);

    if ( ((link = filt_ctx-> inputs[0]) &&
           !ff_merge_formats(link->in_formats, link->out_formats)) ||
         ((link = filt_ctx->outputs[0]) &&
           !ff_merge_formats(link->in_formats, link->out_formats))
       ) {
        av_log(NULL, AV_LOG_ERROR,
               "Impossible to convert between the formats supported by the filter "
               "'%s' and the filter '%s'\n", link->src->name, link->dst->name);
        return AVERROR(EINVAL);
    }

    if (link->type == AVMEDIA_TYPE_AUDIO &&
         (((link = filt_ctx-> inputs[0]) &&
           !ff_merge_channel_layouts(link->in_channel_layouts, link->out_channel_layouts)) ||
         ((link = filt_ctx->outputs[0]) &&
           !ff_merge_channel_layouts(link->in_channel_layouts, link->out_channel_layouts)))
       ) {
        av_log(NULL, AV_LOG_ERROR,
               "Impossible to convert between the channel layouts formats supported by the filter "
               "'%s' and the filter '%s'\n", link->src->name, link->dst->name);
        return AVERROR(EINVAL);
    }

    return 0;
}

static int query_formats(AVFilterGraph *graph, AVClass *log_ctx)
{
    int i, j, ret;
#if 0
    char filt_args[128];
    AVFilterFormats *formats;
    AVFilterChannelLayouts *chlayouts;
    AVFilterFormats *samplerates;
#endif
    int scaler_count = 0, resampler_count = 0;

    for (j = 0; j < 2; j++) {
    /* ask all the sub-filters for their supported media formats */
    for (i = 0; i < graph->filter_count; i++) {
        /* Call query_formats on sources first.
           This is a temporary workaround for amerge,
           until format renegociation is implemented. */
        if (!graph->filters[i]->nb_inputs == j)
            continue;
        if (graph->filters[i]->filter->query_formats)
            ret = filter_query_formats(graph->filters[i]);
        else
            ret = ff_default_query_formats(graph->filters[i]);
        if (ret < 0)
            return ret;
    }
    }

    /* go through and merge as many format lists as possible */
    for (i = 0; i < graph->filter_count; i++) {
        AVFilterContext *filter = graph->filters[i];

        for (j = 0; j < filter->nb_inputs; j++) {
            AVFilterLink *link = filter->inputs[j];
#if 0
            if (!link) continue;

            if (!link->in_formats || !link->out_formats)
                return AVERROR(EINVAL);

            if (link->type == AVMEDIA_TYPE_VIDEO &&
                !ff_merge_formats(link->in_formats, link->out_formats)) {

                /* couldn't merge format lists, auto-insert scale filter */
                snprintf(filt_args, sizeof(filt_args), "0:0:%s",
                         graph->scale_sws_opts);
                if (ret = insert_conv_filter(graph, link, "scale", filt_args))
                    return ret;
            }
            else if (link->type == AVMEDIA_TYPE_AUDIO) {
                if (!link->in_channel_layouts || !link->out_channel_layouts)
                    return AVERROR(EINVAL);

                /* Merge all three list before checking: that way, in all
                 * three categories, aconvert will use a common format
                 * whenever possible. */
                formats     = ff_merge_formats(link->in_formats,   link->out_formats);
                chlayouts   = ff_merge_channel_layouts(link->in_channel_layouts  , link->out_channel_layouts);
                samplerates = ff_merge_samplerates    (link->in_samplerates, link->out_samplerates);

                if (!formats || !chlayouts || !samplerates)
                    if (ret = insert_conv_filter(graph, link, "aresample", NULL))
                       return ret;
#else
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
                    if (graph->scale_sws_opts)
                        snprintf(scale_args, sizeof(scale_args), "0:0:%s", graph->scale_sws_opts);
                    else
                        snprintf(scale_args, sizeof(scale_args), "0:0");

                    if ((ret = avfilter_graph_create_filter(&convert, filter,
                                                            inst_name, scale_args, NULL,
                                                            graph)) < 0)
                        return ret;
                    break;
                case AVMEDIA_TYPE_AUDIO:
                    if (!(filter = avfilter_get_by_name("aresample"))) {
                        av_log(log_ctx, AV_LOG_ERROR, "'aresample' filter "
                               "not present, cannot convert audio formats.\n");
                        return AVERROR(EINVAL);
                    }

                    snprintf(inst_name, sizeof(inst_name), "auto-inserted resampler %d",
                             resampler_count++);
                    if ((ret = avfilter_graph_create_filter(&convert, filter,
                                                            inst_name, graph->aresample_swr_opts, NULL, graph)) < 0)
                        return ret;
                    break;
                default:
                    return AVERROR(EINVAL);
                }

                if ((ret = avfilter_insert_filter(link, convert, 0, 0)) < 0)
                    return ret;

                filter_query_formats(convert);
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
#endif
            }
        }
    }

    return 0;
}

static int pick_format(AVFilterLink *link, AVFilterLink *ref)
{
    if (!link || !link->in_formats)
        return 0;

    if (link->type == AVMEDIA_TYPE_VIDEO) {
        if(ref && ref->type == AVMEDIA_TYPE_VIDEO){
            int has_alpha= av_pix_fmt_desc_get(ref->format)->nb_components % 2 == 0;
            enum AVPixelFormat best= AV_PIX_FMT_NONE;
            int i;
            for (i=0; i<link->in_formats->format_count; i++) {
                enum AVPixelFormat p = link->in_formats->formats[i];
                best= avcodec_find_best_pix_fmt_of_2(best, p, ref->format, has_alpha, NULL);
            }
            av_log(link->src,AV_LOG_DEBUG, "picking %s out of %d ref:%s alpha:%d\n",
                   av_get_pix_fmt_name(best), link->in_formats->format_count,
                   av_get_pix_fmt_name(ref->format), has_alpha);
            link->in_formats->formats[0] = best;
        }
    }

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

    for (i = 0; i < filter->nb_inputs; i++) {
        link = filter->inputs[i];

        if (link->type == AVMEDIA_TYPE_AUDIO &&
            link->out_samplerates->format_count == 1)
            break;
    }
    if (i == filter->nb_inputs)
        return;

    sample_rate = link->out_samplerates->formats[0];

    for (i = 0; i < filter->nb_outputs; i++) {
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

    for (i = 0; i < graph->filter_count; i++)
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
            link->out_formats->format_count == 1)
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
        av_assert0(best_idx >= 0);
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
    int change;

    do{
        change = 0;
        for (i = 0; i < graph->filter_count; i++) {
            AVFilterContext *filter = graph->filters[i];
            if (filter->nb_inputs){
                for (j = 0; j < filter->nb_inputs; j++){
                    if(filter->inputs[j]->in_formats && filter->inputs[j]->in_formats->format_count == 1) {
                        if ((ret = pick_format(filter->inputs[j], NULL)) < 0)
                            return ret;
                        change = 1;
                    }
                }
            }
            if (filter->nb_outputs){
                for (j = 0; j < filter->nb_outputs; j++){
                    if(filter->outputs[j]->in_formats && filter->outputs[j]->in_formats->format_count == 1) {
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

    for (i = 0; i < graph->filter_count; i++) {
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

static int ff_avfilter_graph_config_pointers(AVFilterGraph *graph,
                                             AVClass *log_ctx)
{
    unsigned i, j;
    int sink_links_count = 0, n = 0;
    AVFilterContext *f;
    AVFilterLink **sinks;

    for (i = 0; i < graph->filter_count; i++) {
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
    for (i = 0; i < graph->filter_count; i++) {
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

    for (i = 0; i < graph->filter_count; i++) {
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
            AVFilterCommand **queue = &filter->command_queue, *next;
            while (*queue && (*queue)->time <= ts)
                queue = &(*queue)->next;
            next = *queue;
            *queue = av_mallocz(sizeof(AVFilterCommand));
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
        int r = ff_request_frame(oldest);
        if (r != AVERROR_EOF)
            return r;
        av_log(oldest->dst, AV_LOG_DEBUG, "EOF on sink link %s:%s.\n",
               oldest->dst ? oldest->dst->name : "unknown",
               oldest->dstpad ? oldest->dstpad->name : "unknown");
        /* EOF: remove the link from the heap */
        if (oldest->age_index < --graph->sink_links_count)
            heap_bubble_down(graph, graph->sink_links[graph->sink_links_count],
                             oldest->age_index);
        oldest->age_index = -1;
    }
    return AVERROR_EOF;
}
