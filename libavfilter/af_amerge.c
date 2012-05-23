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
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * Audio merging filter
 */

#include "libswresample/swresample.h" // only for SWR_CH_MAX
#include "avfilter.h"
#include "audio.h"
#include "internal.h"

#define QUEUE_SIZE 16

typedef struct {
    int nb_in_ch[2];       /**< number of channels for each input */
    int route[SWR_CH_MAX]; /**< channels routing, see copy_samples */
    int bps;
    struct amerge_queue {
        AVFilterBufferRef *buf[QUEUE_SIZE];
        int nb_buf, nb_samples, pos;
    } queue[2];
} AMergeContext;

static av_cold void uninit(AVFilterContext *ctx)
{
    AMergeContext *am = ctx->priv;
    int i, j;

    for (i = 0; i < 2; i++)
        for (j = 0; j < am->queue[i].nb_buf; j++)
            avfilter_unref_buffer(am->queue[i].buf[j]);
}

static int query_formats(AVFilterContext *ctx)
{
    AMergeContext *am = ctx->priv;
    int64_t inlayout[2], outlayout;
    AVFilterFormats *formats;
    AVFilterChannelLayouts *layouts;
    int i;

    for (i = 0; i < 2; i++) {
        if (!ctx->inputs[i]->in_channel_layouts ||
            !ctx->inputs[i]->in_channel_layouts->nb_channel_layouts) {
            av_log(ctx, AV_LOG_ERROR,
                   "No channel layout for input %d\n", i + 1);
            return AVERROR(EINVAL);
        }
        inlayout[i] = ctx->inputs[i]->in_channel_layouts->channel_layouts[0];
        if (ctx->inputs[i]->in_channel_layouts->nb_channel_layouts > 1) {
            char buf[256];
            av_get_channel_layout_string(buf, sizeof(buf), 0, inlayout[i]);
            av_log(ctx, AV_LOG_INFO, "Using \"%s\" for input %d\n", buf, i + 1);
        }
        am->nb_in_ch[i] = av_get_channel_layout_nb_channels(inlayout[i]);
    }
    if (am->nb_in_ch[0] + am->nb_in_ch[1] > SWR_CH_MAX) {
        av_log(ctx, AV_LOG_ERROR, "Too many channels (max %d)\n", SWR_CH_MAX);
        return AVERROR(EINVAL);
    }
    if (inlayout[0] & inlayout[1]) {
        av_log(ctx, AV_LOG_WARNING,
               "Inputs overlap: output layout will be meaningless\n");
        for (i = 0; i < am->nb_in_ch[0] + am->nb_in_ch[1]; i++)
            am->route[i] = i;
        outlayout = av_get_default_channel_layout(am->nb_in_ch[0] +
                                                  am->nb_in_ch[1]);
        if (!outlayout)
            outlayout = ((int64_t)1 << (am->nb_in_ch[0] + am->nb_in_ch[1])) - 1;
    } else {
        int *route[2] = { am->route, am->route + am->nb_in_ch[0] };
        int c, out_ch_number = 0;

        outlayout = inlayout[0] | inlayout[1];
        for (c = 0; c < 64; c++)
            for (i = 0; i < 2; i++)
                if ((inlayout[i] >> c) & 1)
                    *(route[i]++) = out_ch_number++;
    }
    formats = avfilter_make_format_list(ff_packed_sample_fmts);
    avfilter_set_common_sample_formats(ctx, formats);
    for (i = 0; i < 2; i++) {
        layouts = NULL;
        ff_add_channel_layout(&layouts, inlayout[i]);
        ff_channel_layouts_ref(layouts, &ctx->inputs[i]->out_channel_layouts);
    }
    layouts = NULL;
    ff_add_channel_layout(&layouts, outlayout);
    ff_channel_layouts_ref(layouts, &ctx->outputs[0]->in_channel_layouts);
    ff_set_common_samplerates(ctx, ff_all_samplerates());
    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AMergeContext *am = ctx->priv;
    int64_t layout;
    char name[3][256];
    int i;

    if (ctx->inputs[0]->sample_rate != ctx->inputs[1]->sample_rate) {
        av_log(ctx, AV_LOG_ERROR,
               "Inputs must have the same sample rate "
               "(%"PRIi64" vs %"PRIi64")\n",
               ctx->inputs[0]->sample_rate, ctx->inputs[1]->sample_rate);
        return AVERROR(EINVAL);
    }
    am->bps = av_get_bytes_per_sample(ctx->outputs[0]->format);
    outlink->sample_rate = ctx->inputs[0]->sample_rate;
    outlink->time_base   = ctx->inputs[0]->time_base;
    for (i = 0; i < 3; i++) {
        layout = (i < 2 ? ctx->inputs[i] : ctx->outputs[0])->channel_layout;
        av_get_channel_layout_string(name[i], sizeof(name[i]), -1, layout);
    }
    av_log(ctx, AV_LOG_INFO,
           "in1:%s + in2:%s -> out:%s\n", name[0], name[1], name[2]);
    return 0;
}

static int request_frame(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AMergeContext *am = ctx->priv;
    int i, ret;

    for (i = 0; i < 2; i++)
        if (!am->queue[i].nb_samples)
            if ((ret = avfilter_request_frame(ctx->inputs[i])) < 0)
                return ret;
    return 0;
}

/**
 * Copy samples from two input streams to one output stream.
 * @param nb_in_ch  number of channels in each input stream
 * @param route     routing values;
 *                  input channel i goes to output channel route[i];
 *                  i <  nb_in_ch[0] are the channels from the first output;
 *                  i >= nb_in_ch[0] are the channels from the second output
 * @param ins       pointer to the samples of each inputs, in packed format;
 *                  will be left at the end of the copied samples
 * @param outs      pointer to the samples of the output, in packet format;
 *                  must point to a buffer big enough;
 *                  will be left at the end of the copied samples
 * @param ns        number of samples to copy
 * @param bps       bytes per sample
 */
static inline void copy_samples(int nb_in_ch[2], int *route, uint8_t *ins[2],
                                uint8_t **outs, int ns, int bps)
{
    int *route_cur;
    int i, c;

    while (ns--) {
        route_cur = route;
        for (i = 0; i < 2; i++) {
            for (c = 0; c < nb_in_ch[i]; c++) {
                memcpy((*outs) + bps * *(route_cur++), ins[i], bps);
                ins[i] += bps;
            }
        }
        *outs += (nb_in_ch[0] + nb_in_ch[1]) * bps;
    }
}

static void filter_samples(AVFilterLink *inlink, AVFilterBufferRef *insamples)
{
    AVFilterContext *ctx = inlink->dst;
    AMergeContext *am = ctx->priv;
    AVFilterLink *const outlink = ctx->outputs[0];
    int input_number = inlink == ctx->inputs[1];
    struct amerge_queue *inq = &am->queue[input_number];
    int nb_samples, ns, i;
    AVFilterBufferRef *outbuf, **inbuf[2];
    uint8_t *ins[2], *outs;

    if (inq->nb_buf == QUEUE_SIZE) {
        av_log(ctx, AV_LOG_ERROR, "Packet queue overflow; dropped\n");
        avfilter_unref_buffer(insamples);
        return;
    }
    inq->buf[inq->nb_buf++] = avfilter_ref_buffer(insamples, AV_PERM_READ |
                                                             AV_PERM_PRESERVE);
    inq->nb_samples += insamples->audio->nb_samples;
    avfilter_unref_buffer(insamples);
    if (!am->queue[!input_number].nb_samples)
        return;

    nb_samples = FFMIN(am->queue[0].nb_samples,
                       am->queue[1].nb_samples);
    outbuf = ff_get_audio_buffer(ctx->outputs[0], AV_PERM_WRITE,
                                       nb_samples);
    outs = outbuf->data[0];
    for (i = 0; i < 2; i++) {
        inbuf[i] = am->queue[i].buf;
        ins[i] = (*inbuf[i])->data[0] +
                 am->queue[i].pos * am->nb_in_ch[i] * am->bps;
    }
    outbuf->pts = (*inbuf[0])->pts == AV_NOPTS_VALUE ? AV_NOPTS_VALUE :
                  (*inbuf[0])->pts +
                  av_rescale_q(am->queue[0].pos,
                               (AVRational){ 1, ctx->inputs[0]->sample_rate },
                               ctx->outputs[0]->time_base);

    avfilter_copy_buffer_ref_props(outbuf, *inbuf[0]);
    outbuf->audio->nb_samples     = nb_samples;
    outbuf->audio->channel_layout = outlink->channel_layout;

    while (nb_samples) {
        ns = nb_samples;
        for (i = 0; i < 2; i++)
            ns = FFMIN(ns, (*inbuf[i])->audio->nb_samples - am->queue[i].pos);
        /* Unroll the most common sample formats: speed +~350% for the loop,
           +~13% overall (including two common decoders) */
        switch (am->bps) {
            case 1:
                copy_samples(am->nb_in_ch, am->route, ins, &outs, ns, 1);
                break;
            case 2:
                copy_samples(am->nb_in_ch, am->route, ins, &outs, ns, 2);
                break;
            case 4:
                copy_samples(am->nb_in_ch, am->route, ins, &outs, ns, 4);
                break;
            default:
                copy_samples(am->nb_in_ch, am->route, ins, &outs, ns, am->bps);
                break;
        }

        nb_samples -= ns;
        for (i = 0; i < 2; i++) {
            am->queue[i].nb_samples -= ns;
            am->queue[i].pos += ns;
            if (am->queue[i].pos == (*inbuf[i])->audio->nb_samples) {
                am->queue[i].pos = 0;
                avfilter_unref_buffer(*inbuf[i]);
                *inbuf[i] = NULL;
                inbuf[i]++;
                ins[i] = *inbuf[i] ? (*inbuf[i])->data[0] : NULL;
            }
        }
    }
    for (i = 0; i < 2; i++) {
        int nbufused = inbuf[i] - am->queue[i].buf;
        if (nbufused) {
            am->queue[i].nb_buf -= nbufused;
            memmove(am->queue[i].buf, inbuf[i],
                    am->queue[i].nb_buf * sizeof(**inbuf));
        }
    }
    ff_filter_samples(ctx->outputs[0], outbuf);
}

AVFilter avfilter_af_amerge = {
    .name          = "amerge",
    .description   = NULL_IF_CONFIG_SMALL("Merge two audio streams into "
                                          "a single multi-channel stream."),
    .priv_size     = sizeof(AMergeContext),
    .uninit        = uninit,
    .query_formats = query_formats,

    .inputs    = (const AVFilterPad[]) {
        { .name             = "in1",
          .type             = AVMEDIA_TYPE_AUDIO,
          .filter_samples   = filter_samples,
          .min_perms        = AV_PERM_READ, },
        { .name             = "in2",
          .type             = AVMEDIA_TYPE_AUDIO,
          .filter_samples   = filter_samples,
          .min_perms        = AV_PERM_READ, },
        { .name = NULL }
    },
    .outputs   = (const AVFilterPad[]) {
        { .name             = "default",
          .type             = AVMEDIA_TYPE_AUDIO,
          .config_props     = config_output,
          .request_frame    = request_frame, },
        { .name = NULL }
    },
};
