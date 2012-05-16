/*
 * Copyright (c) 2011 Stefano Sabatini
 * Copyright (c) 2011 Mina Nagy Zaki
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

/**
 * @file
 * resampling audio filter
 */

#include "libswresample/swresample.h"
#include "avfilter.h"
#include "audio.h"
#include "internal.h"

typedef struct {
    int out_rate;
    double ratio;
    struct SwrContext *swr;
} AResampleContext;

static av_cold int init(AVFilterContext *ctx, const char *args, void *opaque)
{
    AResampleContext *aresample = ctx->priv;
    int ret;

    if (args) {
        if ((ret = ff_parse_sample_rate(&aresample->out_rate, args, ctx)) < 0)
            return ret;
    } else {
        aresample->out_rate = -1;
    }

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    AResampleContext *aresample = ctx->priv;
    swr_free(&aresample->swr);
}

static int query_formats(AVFilterContext *ctx)
{
    AResampleContext *aresample = ctx->priv;

    AVFilterLink *inlink  = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];

    AVFilterFormats        *in_formats      = avfilter_all_formats(AVMEDIA_TYPE_AUDIO);
    AVFilterFormats        *out_formats     = avfilter_all_formats(AVMEDIA_TYPE_AUDIO);
    AVFilterFormats        *in_samplerates  = ff_all_samplerates();
    AVFilterFormats        *out_samplerates;


    AVFilterChannelLayouts *in_layouts      = ff_all_channel_layouts();
    AVFilterChannelLayouts *out_layouts     = ff_all_channel_layouts();

    avfilter_formats_ref(in_formats,  &inlink->out_formats);
    avfilter_formats_ref(out_formats, &outlink->in_formats);

    avfilter_formats_ref(in_samplerates,  &inlink->out_samplerates);

    ff_channel_layouts_ref(in_layouts,  &inlink->out_channel_layouts);
    ff_channel_layouts_ref(out_layouts, &outlink->in_channel_layouts);

    if(aresample->out_rate > 0) {
        int sample_rates[] = { aresample->out_rate, -1 };
        ff_set_common_samplerates(ctx, avfilter_make_format_list(sample_rates));
    } else {
        out_samplerates = ff_all_samplerates();
        avfilter_formats_ref(out_samplerates, &outlink->in_samplerates);
    }

    return 0;
}


static int config_output(AVFilterLink *outlink)
{
    int ret;
    AVFilterContext *ctx = outlink->src;
    AVFilterLink *inlink = ctx->inputs[0];
    AResampleContext *aresample = ctx->priv;

    if (aresample->out_rate == -1)
        aresample->out_rate = outlink->sample_rate;
    else
        outlink->sample_rate = aresample->out_rate;
    outlink->time_base = (AVRational) {1, aresample->out_rate};

    //TODO: make the resampling parameters (filter size, phrase shift, linear, cutoff) configurable
    aresample->swr = swr_alloc_set_opts(aresample->swr,
                                        outlink->channel_layout, outlink->format, outlink->sample_rate,
                                        inlink->channel_layout, inlink->format, inlink->sample_rate,
                                        0, ctx);
    if (!aresample->swr)
        return AVERROR(ENOMEM);
    ret = swr_init(aresample->swr);
    if (ret < 0)
        return ret;

    aresample->ratio = (double)outlink->sample_rate / inlink->sample_rate;

    av_log(ctx, AV_LOG_INFO, "r:%"PRId64"Hz -> r:%"PRId64"Hz\n",
           inlink->sample_rate, outlink->sample_rate);
    return 0;
}

static void filter_samples(AVFilterLink *inlink, AVFilterBufferRef *insamplesref)
{
    AResampleContext *aresample = inlink->dst->priv;
    const int n_in  = insamplesref->audio->nb_samples;
    int n_out       = n_in * aresample->ratio;
    AVFilterLink *const outlink = inlink->dst->outputs[0];
    AVFilterBufferRef *outsamplesref = ff_get_audio_buffer(outlink, AV_PERM_WRITE, n_out);

    n_out = swr_convert(aresample->swr, outsamplesref->data, n_out,
                                 (void *)insamplesref->data, n_in);

    avfilter_copy_buffer_ref_props(outsamplesref, insamplesref);
    outsamplesref->audio->sample_rate = outlink->sample_rate;
    outsamplesref->audio->nb_samples  = n_out;
    outsamplesref->pts = insamplesref->pts == AV_NOPTS_VALUE ? AV_NOPTS_VALUE :
        av_rescale_q(insamplesref->pts, inlink->time_base, outlink->time_base);

    ff_filter_samples(outlink, outsamplesref);
    avfilter_unref_buffer(insamplesref);
}

AVFilter avfilter_af_aresample = {
    .name          = "aresample",
    .description   = NULL_IF_CONFIG_SMALL("Resample audio data."),
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .priv_size     = sizeof(AResampleContext),

    .inputs    = (const AVFilterPad[]) {{ .name      = "default",
                                    .type            = AVMEDIA_TYPE_AUDIO,
                                    .filter_samples  = filter_samples,
                                    .min_perms       = AV_PERM_READ, },
                                  { .name = NULL}},
    .outputs   = (const AVFilterPad[]) {{ .name      = "default",
                                    .config_props    = config_output,
                                    .type            = AVMEDIA_TYPE_AUDIO, },
                                  { .name = NULL}},
};
