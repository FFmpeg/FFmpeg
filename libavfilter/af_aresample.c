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

#include "libavutil/eval.h"
#include "libavcodec/avcodec.h"
#include "avfilter.h"
#include "internal.h"

typedef struct {
    struct AVResampleContext *resample;
    int out_rate;
    double ratio;
    AVFilterBufferRef *outsamplesref;
    int unconsumed_nb_samples,
        max_cached_nb_samples;
    int16_t *cached_data[8],
            *resampled_data[8];
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
    if (aresample->outsamplesref) {
        int nb_channels =
            av_get_channel_layout_nb_channels(
                aresample->outsamplesref->audio->channel_layout);
        avfilter_unref_buffer(aresample->outsamplesref);
        while (nb_channels--) {
            av_freep(&(aresample->cached_data[nb_channels]));
            av_freep(&(aresample->resampled_data[nb_channels]));
        }
    }

    if (aresample->resample)
        av_resample_close(aresample->resample);
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AVFilterLink *inlink = ctx->inputs[0];
    AResampleContext *aresample = ctx->priv;

    if (aresample->out_rate == -1)
        aresample->out_rate = outlink->sample_rate;
    else
        outlink->sample_rate = aresample->out_rate;
    outlink->time_base = (AVRational) {1, aresample->out_rate};

    //TODO: make the resampling parameters configurable
    aresample->resample = av_resample_init(aresample->out_rate, inlink->sample_rate,
                                           16, 10, 0, 0.8);

    aresample->ratio = (double)outlink->sample_rate / inlink->sample_rate;

    av_log(ctx, AV_LOG_INFO, "r:%"PRId64"Hz -> r:%"PRId64"Hz\n",
           inlink->sample_rate, outlink->sample_rate);
    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *formats = NULL;

    avfilter_add_format(&formats, AV_SAMPLE_FMT_S16);
    if (!formats)
        return AVERROR(ENOMEM);
    avfilter_set_common_sample_formats(ctx, formats);

    formats = avfilter_make_all_channel_layouts();
    if (!formats)
        return AVERROR(ENOMEM);
    avfilter_set_common_channel_layouts(ctx, formats);

    formats = avfilter_make_all_packing_formats();
    if (!formats)
        return AVERROR(ENOMEM);
    avfilter_set_common_packing_formats(ctx, formats);

    return 0;
}

static void deinterleave(int16_t **outp, int16_t *in,
                         int nb_channels, int nb_samples)
{
    int16_t *out[8];
    memcpy(out, outp, nb_channels * sizeof(int16_t*));

    switch (nb_channels) {
    case 2:
        while (nb_samples--) {
            *out[0]++ = *in++;
            *out[1]++ = *in++;
        }
        break;
    case 3:
        while (nb_samples--) {
            *out[0]++ = *in++;
            *out[1]++ = *in++;
            *out[2]++ = *in++;
        }
        break;
    case 4:
        while (nb_samples--) {
            *out[0]++ = *in++;
            *out[1]++ = *in++;
            *out[2]++ = *in++;
            *out[3]++ = *in++;
        }
        break;
    case 5:
        while (nb_samples--) {
            *out[0]++ = *in++;
            *out[1]++ = *in++;
            *out[2]++ = *in++;
            *out[3]++ = *in++;
            *out[4]++ = *in++;
        }
        break;
    case 6:
        while (nb_samples--) {
            *out[0]++ = *in++;
            *out[1]++ = *in++;
            *out[2]++ = *in++;
            *out[3]++ = *in++;
            *out[4]++ = *in++;
            *out[5]++ = *in++;
        }
        break;
    case 8:
        while (nb_samples--) {
            *out[0]++ = *in++;
            *out[1]++ = *in++;
            *out[2]++ = *in++;
            *out[3]++ = *in++;
            *out[4]++ = *in++;
            *out[5]++ = *in++;
            *out[6]++ = *in++;
            *out[7]++ = *in++;
        }
        break;
    }
}

static void interleave(int16_t *out, int16_t **inp,
        int nb_channels, int nb_samples)
{
    int16_t *in[8];
    memcpy(in, inp, nb_channels * sizeof(int16_t*));

    switch (nb_channels) {
    case 2:
        while (nb_samples--) {
            *out++ = *in[0]++;
            *out++ = *in[1]++;
        }
        break;
    case 3:
        while (nb_samples--) {
            *out++ = *in[0]++;
            *out++ = *in[1]++;
            *out++ = *in[2]++;
        }
        break;
    case 4:
        while (nb_samples--) {
            *out++ = *in[0]++;
            *out++ = *in[1]++;
            *out++ = *in[2]++;
            *out++ = *in[3]++;
        }
        break;
    case 5:
        while (nb_samples--) {
            *out++ = *in[0]++;
            *out++ = *in[1]++;
            *out++ = *in[2]++;
            *out++ = *in[3]++;
            *out++ = *in[4]++;
        }
        break;
    case 6:
        while (nb_samples--) {
            *out++ = *in[0]++;
            *out++ = *in[1]++;
            *out++ = *in[2]++;
            *out++ = *in[3]++;
            *out++ = *in[4]++;
            *out++ = *in[5]++;
        }
        break;
    case 8:
        while (nb_samples--) {
            *out++ = *in[0]++;
            *out++ = *in[1]++;
            *out++ = *in[2]++;
            *out++ = *in[3]++;
            *out++ = *in[4]++;
            *out++ = *in[5]++;
            *out++ = *in[6]++;
            *out++ = *in[7]++;
        }
        break;
    }
}

static void filter_samples(AVFilterLink *inlink, AVFilterBufferRef *insamplesref)
{
    AResampleContext *aresample  = inlink->dst->priv;
    AVFilterLink * const outlink = inlink->dst->outputs[0];
    int i,
        in_nb_samples            = insamplesref->audio->nb_samples,
        cached_nb_samples        = in_nb_samples + aresample->unconsumed_nb_samples,
        requested_out_nb_samples = aresample->ratio * cached_nb_samples,
        nb_channels              =
            av_get_channel_layout_nb_channels(inlink->channel_layout);

    if (cached_nb_samples > aresample->max_cached_nb_samples) {
        for (i = 0; i < nb_channels; i++) {
            aresample->cached_data[i]    =
                av_realloc(aresample->cached_data[i], cached_nb_samples * sizeof(int16_t));
            aresample->resampled_data[i] =
                av_realloc(aresample->resampled_data[i],
                           FFALIGN(sizeof(int16_t) * requested_out_nb_samples, 16));

            if (aresample->cached_data[i] == NULL || aresample->resampled_data[i] == NULL)
                return;
        }
        aresample->max_cached_nb_samples = cached_nb_samples;

        if (aresample->outsamplesref)
            avfilter_unref_buffer(aresample->outsamplesref);

        aresample->outsamplesref =
            avfilter_get_audio_buffer(outlink, AV_PERM_WRITE, requested_out_nb_samples);
        outlink->out_buf = aresample->outsamplesref;
    }

    avfilter_copy_buffer_ref_props(aresample->outsamplesref, insamplesref);
    aresample->outsamplesref->audio->sample_rate = outlink->sample_rate;
    aresample->outsamplesref->pts =
        av_rescale(outlink->sample_rate, insamplesref->pts, inlink->sample_rate);

    /* av_resample() works with planar audio buffers */
    if (!inlink->planar && nb_channels > 1) {
        int16_t *out[8];
        for (i = 0; i < nb_channels; i++)
            out[i] = aresample->cached_data[i] + aresample->unconsumed_nb_samples;

        deinterleave(out, (int16_t *)insamplesref->data[0],
                     nb_channels, in_nb_samples);
    } else {
        for (i = 0; i < nb_channels; i++)
            memcpy(aresample->cached_data[i] + aresample->unconsumed_nb_samples,
                   insamplesref->data[i],
                   in_nb_samples * sizeof(int16_t));
    }

    for (i = 0; i < nb_channels; i++) {
        int consumed_nb_samples;
        const int is_last = i+1 == nb_channels;

        aresample->outsamplesref->audio->nb_samples =
            av_resample(aresample->resample,
                        aresample->resampled_data[i], aresample->cached_data[i],
                        &consumed_nb_samples,
                        cached_nb_samples,
                        requested_out_nb_samples, is_last);

        /* move unconsumed data back to the beginning of the cache */
        aresample->unconsumed_nb_samples = cached_nb_samples - consumed_nb_samples;
        memmove(aresample->cached_data[i],
                aresample->cached_data[i] + consumed_nb_samples,
                aresample->unconsumed_nb_samples * sizeof(int16_t));
    }


    /* copy resampled data to the output samplesref */
    if (!inlink->planar && nb_channels > 1) {
        interleave((int16_t *)aresample->outsamplesref->data[0],
                   aresample->resampled_data,
                   nb_channels, aresample->outsamplesref->audio->nb_samples);
    } else {
        for (i = 0; i < nb_channels; i++)
            memcpy(aresample->outsamplesref->data[i], aresample->resampled_data[i],
                   aresample->outsamplesref->audio->nb_samples * sizeof(int16_t));
    }

    avfilter_filter_samples(outlink, avfilter_ref_buffer(aresample->outsamplesref, ~0));
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
