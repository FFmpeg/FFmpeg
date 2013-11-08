/*
 * Copyright (c) 2012 Andrey Utkin
 * Copyright (c) 2012 Stefano Sabatini
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
 * Filter that changes number of samples on single output operation
 */

#include "libavutil/audio_fifo.h"
#include "libavutil/avassert.h"
#include "libavutil/channel_layout.h"
#include "libavutil/opt.h"
#include "avfilter.h"
#include "audio.h"
#include "internal.h"
#include "formats.h"

typedef struct {
    const AVClass *class;
    int nb_out_samples;  ///< how many samples to output
    AVAudioFifo *fifo;   ///< samples are queued here
    int64_t next_out_pts;
    int pad;
} ASNSContext;

#define OFFSET(x) offsetof(ASNSContext, x)
#define FLAGS AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption asetnsamples_options[] = {
    { "nb_out_samples", "set the number of per-frame output samples", OFFSET(nb_out_samples), AV_OPT_TYPE_INT, {.i64=1024}, 1, INT_MAX, FLAGS },
    { "n",              "set the number of per-frame output samples", OFFSET(nb_out_samples), AV_OPT_TYPE_INT, {.i64=1024}, 1, INT_MAX, FLAGS },
    { "pad", "pad last frame with zeros", OFFSET(pad), AV_OPT_TYPE_INT, {.i64=1}, 0, 1, FLAGS },
    { "p",   "pad last frame with zeros", OFFSET(pad), AV_OPT_TYPE_INT, {.i64=1}, 0, 1, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(asetnsamples);

static av_cold int init(AVFilterContext *ctx)
{
    ASNSContext *asns = ctx->priv;

    asns->next_out_pts = AV_NOPTS_VALUE;
    av_log(ctx, AV_LOG_VERBOSE, "nb_out_samples:%d pad:%d\n", asns->nb_out_samples, asns->pad);

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    ASNSContext *asns = ctx->priv;
    av_audio_fifo_free(asns->fifo);
}

static int config_props_output(AVFilterLink *outlink)
{
    ASNSContext *asns = outlink->src->priv;

    asns->fifo = av_audio_fifo_alloc(outlink->format, outlink->channels, asns->nb_out_samples);
    if (!asns->fifo)
        return AVERROR(ENOMEM);
    outlink->flags |= FF_LINK_FLAG_REQUEST_LOOP;

    return 0;
}

static int push_samples(AVFilterLink *outlink)
{
    ASNSContext *asns = outlink->src->priv;
    AVFrame *outsamples = NULL;
    int ret, nb_out_samples, nb_pad_samples;

    if (asns->pad) {
        nb_out_samples = av_audio_fifo_size(asns->fifo) ? asns->nb_out_samples : 0;
        nb_pad_samples = nb_out_samples - FFMIN(nb_out_samples, av_audio_fifo_size(asns->fifo));
    } else {
        nb_out_samples = FFMIN(asns->nb_out_samples, av_audio_fifo_size(asns->fifo));
        nb_pad_samples = 0;
    }

    if (!nb_out_samples)
        return 0;

    outsamples = ff_get_audio_buffer(outlink, nb_out_samples);
    if (!outsamples)
        return AVERROR(ENOMEM);

    av_audio_fifo_read(asns->fifo,
                       (void **)outsamples->extended_data, nb_out_samples);

    if (nb_pad_samples)
        av_samples_set_silence(outsamples->extended_data, nb_out_samples - nb_pad_samples,
                               nb_pad_samples, outlink->channels,
                               outlink->format);
    outsamples->nb_samples     = nb_out_samples;
    outsamples->channel_layout = outlink->channel_layout;
    outsamples->sample_rate    = outlink->sample_rate;
    outsamples->pts = asns->next_out_pts;

    if (asns->next_out_pts != AV_NOPTS_VALUE)
        asns->next_out_pts += nb_out_samples;

    ret = ff_filter_frame(outlink, outsamples);
    if (ret < 0)
        return ret;
    return nb_out_samples;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *insamples)
{
    AVFilterContext *ctx = inlink->dst;
    ASNSContext *asns = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    int ret;
    int nb_samples = insamples->nb_samples;

    if (av_audio_fifo_space(asns->fifo) < nb_samples) {
        av_log(ctx, AV_LOG_DEBUG, "No space for %d samples, stretching audio fifo\n", nb_samples);
        ret = av_audio_fifo_realloc(asns->fifo, av_audio_fifo_size(asns->fifo) + nb_samples);
        if (ret < 0) {
            av_log(ctx, AV_LOG_ERROR,
                   "Stretching audio fifo failed, discarded %d samples\n", nb_samples);
            return -1;
        }
    }
    av_audio_fifo_write(asns->fifo, (void **)insamples->extended_data, nb_samples);
    if (asns->next_out_pts == AV_NOPTS_VALUE)
        asns->next_out_pts = insamples->pts;
    av_frame_free(&insamples);

    while (av_audio_fifo_size(asns->fifo) >= asns->nb_out_samples)
        push_samples(outlink);
    return 0;
}

static int request_frame(AVFilterLink *outlink)
{
    AVFilterLink *inlink = outlink->src->inputs[0];
    int ret;

    ret = ff_request_frame(inlink);
    if (ret == AVERROR_EOF) {
        ret = push_samples(outlink);
        return ret < 0 ? ret : ret > 0 ? 0 : AVERROR_EOF;
    }

    return ret;
}

static const AVFilterPad asetnsamples_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad asetnsamples_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_AUDIO,
        .request_frame = request_frame,
        .config_props  = config_props_output,
    },
    { NULL }
};

AVFilter ff_af_asetnsamples = {
    .name        = "asetnsamples",
    .description = NULL_IF_CONFIG_SMALL("Set the number of samples for each output audio frames."),
    .priv_size   = sizeof(ASNSContext),
    .priv_class  = &asetnsamples_class,
    .init        = init,
    .uninit      = uninit,
    .inputs      = asetnsamples_inputs,
    .outputs     = asetnsamples_outputs,
};
