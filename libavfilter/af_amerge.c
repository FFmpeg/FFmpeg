/*
 * Copyright (c) 2011 Nicolas George <nicolas.george@normalesup.org>
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * Audio merging filter
 */

#include "libavutil/avstring.h"
#include "libavutil/bprint.h"
#include "libavutil/channel_layout.h"
#include "libavutil/opt.h"
#include "avfilter.h"
#include "audio.h"
#include "bufferqueue.h"
#include "internal.h"

#define SWR_CH_MAX 64

typedef struct {
    const AVClass *class;
    int nb_inputs;
    int route[SWR_CH_MAX]; /**< channels routing, see copy_samples */
    int bps;
    struct amerge_input {
        struct FFBufQueue queue;
        int nb_ch;         /**< number of channels for the input */
        int nb_samples;
        int pos;
    } *in;
} AMergeContext;

#define OFFSET(x) offsetof(AMergeContext, x)
#define FLAGS AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption amerge_options[] = {
    { "inputs", "specify the number of inputs", OFFSET(nb_inputs),
      AV_OPT_TYPE_INT, { .i64 = 2 }, 2, SWR_CH_MAX, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(amerge);

static av_cold void uninit(AVFilterContext *ctx)
{
    AMergeContext *s = ctx->priv;
    int i;

    for (i = 0; i < s->nb_inputs; i++) {
        if (s->in)
            ff_bufqueue_discard_all(&s->in[i].queue);
        if (ctx->input_pads)
            av_freep(&ctx->input_pads[i].name);
    }
    av_freep(&s->in);
}

static int query_formats(AVFilterContext *ctx)
{
    AMergeContext *s = ctx->priv;
    int64_t inlayout[SWR_CH_MAX], outlayout = 0;
    AVFilterFormats *formats;
    AVFilterChannelLayouts *layouts;
    int i, ret, overlap = 0, nb_ch = 0;

    for (i = 0; i < s->nb_inputs; i++) {
        if (!ctx->inputs[i]->in_channel_layouts ||
            !ctx->inputs[i]->in_channel_layouts->nb_channel_layouts) {
            av_log(ctx, AV_LOG_WARNING,
                   "No channel layout for input %d\n", i + 1);
            return AVERROR(EAGAIN);
        }
        inlayout[i] = ctx->inputs[i]->in_channel_layouts->channel_layouts[0];
        if (ctx->inputs[i]->in_channel_layouts->nb_channel_layouts > 1) {
            char buf[256];
            av_get_channel_layout_string(buf, sizeof(buf), 0, inlayout[i]);
            av_log(ctx, AV_LOG_INFO, "Using \"%s\" for input %d\n", buf, i + 1);
        }
        s->in[i].nb_ch = av_get_channel_layout_nb_channels(inlayout[i]);
        if (outlayout & inlayout[i])
            overlap++;
        outlayout |= inlayout[i];
        nb_ch += s->in[i].nb_ch;
    }
    if (nb_ch > SWR_CH_MAX) {
        av_log(ctx, AV_LOG_ERROR, "Too many channels (max %d)\n", SWR_CH_MAX);
        return AVERROR(EINVAL);
    }
    if (overlap) {
        av_log(ctx, AV_LOG_WARNING,
               "Input channel layouts overlap: "
               "output layout will be determined by the number of distinct input channels\n");
        for (i = 0; i < nb_ch; i++)
            s->route[i] = i;
        outlayout = av_get_default_channel_layout(nb_ch);
        if (!outlayout && nb_ch)
            outlayout = 0xFFFFFFFFFFFFFFFFULL >> (64 - nb_ch);
    } else {
        int *route[SWR_CH_MAX];
        int c, out_ch_number = 0;

        route[0] = s->route;
        for (i = 1; i < s->nb_inputs; i++)
            route[i] = route[i - 1] + s->in[i - 1].nb_ch;
        for (c = 0; c < 64; c++)
            for (i = 0; i < s->nb_inputs; i++)
                if ((inlayout[i] >> c) & 1)
                    *(route[i]++) = out_ch_number++;
    }
    formats = ff_make_format_list(ff_packed_sample_fmts_array);
    if ((ret = ff_set_common_formats(ctx, formats)) < 0)
        return ret;
    for (i = 0; i < s->nb_inputs; i++) {
        layouts = NULL;
        if ((ret = ff_add_channel_layout(&layouts, inlayout[i])) < 0)
            return ret;
        if ((ret = ff_channel_layouts_ref(layouts, &ctx->inputs[i]->out_channel_layouts)) < 0)
            return ret;
    }
    layouts = NULL;
    if ((ret = ff_add_channel_layout(&layouts, outlayout)) < 0)
        return ret;
    if ((ret = ff_channel_layouts_ref(layouts, &ctx->outputs[0]->in_channel_layouts)) < 0)
        return ret;

    return ff_set_common_samplerates(ctx, ff_all_samplerates());
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AMergeContext *s = ctx->priv;
    AVBPrint bp;
    int i;

    for (i = 1; i < s->nb_inputs; i++) {
        if (ctx->inputs[i]->sample_rate != ctx->inputs[0]->sample_rate) {
            av_log(ctx, AV_LOG_ERROR,
                   "Inputs must have the same sample rate "
                   "%d for in%d vs %d\n",
                   ctx->inputs[i]->sample_rate, i, ctx->inputs[0]->sample_rate);
            return AVERROR(EINVAL);
        }
    }
    s->bps = av_get_bytes_per_sample(ctx->outputs[0]->format);
    outlink->sample_rate = ctx->inputs[0]->sample_rate;
    outlink->time_base   = ctx->inputs[0]->time_base;

    av_bprint_init(&bp, 0, 1);
    for (i = 0; i < s->nb_inputs; i++) {
        av_bprintf(&bp, "%sin%d:", i ? " + " : "", i);
        av_bprint_channel_layout(&bp, -1, ctx->inputs[i]->channel_layout);
    }
    av_bprintf(&bp, " -> out:");
    av_bprint_channel_layout(&bp, -1, ctx->outputs[0]->channel_layout);
    av_log(ctx, AV_LOG_VERBOSE, "%s\n", bp.str);

    return 0;
}

static int request_frame(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AMergeContext *s = ctx->priv;
    int i, ret;

    for (i = 0; i < s->nb_inputs; i++)
        if (!s->in[i].nb_samples)
            if ((ret = ff_request_frame(ctx->inputs[i])) < 0)
                return ret;
    return 0;
}

/**
 * Copy samples from several input streams to one output stream.
 * @param nb_inputs number of inputs
 * @param in        inputs; used only for the nb_ch field;
 * @param route     routing values;
 *                  input channel i goes to output channel route[i];
 *                  i <  in[0].nb_ch are the channels from the first output;
 *                  i >= in[0].nb_ch are the channels from the second output
 * @param ins       pointer to the samples of each inputs, in packed format;
 *                  will be left at the end of the copied samples
 * @param outs      pointer to the samples of the output, in packet format;
 *                  must point to a buffer big enough;
 *                  will be left at the end of the copied samples
 * @param ns        number of samples to copy
 * @param bps       bytes per sample
 */
static inline void copy_samples(int nb_inputs, struct amerge_input in[],
                                int *route, uint8_t *ins[],
                                uint8_t **outs, int ns, int bps)
{
    int *route_cur;
    int i, c, nb_ch = 0;

    for (i = 0; i < nb_inputs; i++)
        nb_ch += in[i].nb_ch;
    while (ns--) {
        route_cur = route;
        for (i = 0; i < nb_inputs; i++) {
            for (c = 0; c < in[i].nb_ch; c++) {
                memcpy((*outs) + bps * *(route_cur++), ins[i], bps);
                ins[i] += bps;
            }
        }
        *outs += nb_ch * bps;
    }
}

static int filter_frame(AVFilterLink *inlink, AVFrame *insamples)
{
    AVFilterContext *ctx = inlink->dst;
    AMergeContext *s = ctx->priv;
    AVFilterLink *const outlink = ctx->outputs[0];
    int input_number;
    int nb_samples, ns, i;
    AVFrame *outbuf, *inbuf[SWR_CH_MAX];
    uint8_t *ins[SWR_CH_MAX], *outs;

    for (input_number = 0; input_number < s->nb_inputs; input_number++)
        if (inlink == ctx->inputs[input_number])
            break;
    av_assert1(input_number < s->nb_inputs);
    if (ff_bufqueue_is_full(&s->in[input_number].queue)) {
        av_frame_free(&insamples);
        return AVERROR(ENOMEM);
    }
    ff_bufqueue_add(ctx, &s->in[input_number].queue, av_frame_clone(insamples));
    s->in[input_number].nb_samples += insamples->nb_samples;
    av_frame_free(&insamples);
    nb_samples = s->in[0].nb_samples;
    for (i = 1; i < s->nb_inputs; i++)
        nb_samples = FFMIN(nb_samples, s->in[i].nb_samples);
    if (!nb_samples)
        return 0;

    outbuf = ff_get_audio_buffer(ctx->outputs[0], nb_samples);
    if (!outbuf)
        return AVERROR(ENOMEM);
    outs = outbuf->data[0];
    for (i = 0; i < s->nb_inputs; i++) {
        inbuf[i] = ff_bufqueue_peek(&s->in[i].queue, 0);
        ins[i] = inbuf[i]->data[0] +
                 s->in[i].pos * s->in[i].nb_ch * s->bps;
    }
    av_frame_copy_props(outbuf, inbuf[0]);
    outbuf->pts = inbuf[0]->pts == AV_NOPTS_VALUE ? AV_NOPTS_VALUE :
                  inbuf[0]->pts +
                  av_rescale_q(s->in[0].pos,
                               av_make_q(1, ctx->inputs[0]->sample_rate),
                               ctx->outputs[0]->time_base);

    outbuf->nb_samples     = nb_samples;
    outbuf->channel_layout = outlink->channel_layout;
    av_frame_set_channels(outbuf, outlink->channels);

    while (nb_samples) {
        ns = nb_samples;
        for (i = 0; i < s->nb_inputs; i++)
            ns = FFMIN(ns, inbuf[i]->nb_samples - s->in[i].pos);
        /* Unroll the most common sample formats: speed +~350% for the loop,
           +~13% overall (including two common decoders) */
        switch (s->bps) {
            case 1:
                copy_samples(s->nb_inputs, s->in, s->route, ins, &outs, ns, 1);
                break;
            case 2:
                copy_samples(s->nb_inputs, s->in, s->route, ins, &outs, ns, 2);
                break;
            case 4:
                copy_samples(s->nb_inputs, s->in, s->route, ins, &outs, ns, 4);
                break;
            default:
                copy_samples(s->nb_inputs, s->in, s->route, ins, &outs, ns, s->bps);
                break;
        }

        nb_samples -= ns;
        for (i = 0; i < s->nb_inputs; i++) {
            s->in[i].nb_samples -= ns;
            s->in[i].pos += ns;
            if (s->in[i].pos == inbuf[i]->nb_samples) {
                s->in[i].pos = 0;
                av_frame_free(&inbuf[i]);
                ff_bufqueue_get(&s->in[i].queue);
                inbuf[i] = ff_bufqueue_peek(&s->in[i].queue, 0);
                ins[i] = inbuf[i] ? inbuf[i]->data[0] : NULL;
            }
        }
    }
    return ff_filter_frame(ctx->outputs[0], outbuf);
}

static av_cold int init(AVFilterContext *ctx)
{
    AMergeContext *s = ctx->priv;
    int i;

    s->in = av_calloc(s->nb_inputs, sizeof(*s->in));
    if (!s->in)
        return AVERROR(ENOMEM);
    for (i = 0; i < s->nb_inputs; i++) {
        char *name = av_asprintf("in%d", i);
        AVFilterPad pad = {
            .name             = name,
            .type             = AVMEDIA_TYPE_AUDIO,
            .filter_frame     = filter_frame,
        };
        if (!name)
            return AVERROR(ENOMEM);
        ff_insert_inpad(ctx, i, &pad);
    }
    return 0;
}

static const AVFilterPad amerge_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_AUDIO,
        .config_props  = config_output,
        .request_frame = request_frame,
    },
    { NULL }
};

AVFilter ff_af_amerge = {
    .name          = "amerge",
    .description   = NULL_IF_CONFIG_SMALL("Merge two or more audio streams into "
                                          "a single multi-channel stream."),
    .priv_size     = sizeof(AMergeContext),
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = NULL,
    .outputs       = amerge_outputs,
    .priv_class    = &amerge_class,
    .flags         = AVFILTER_FLAG_DYNAMIC_INPUTS,
};
