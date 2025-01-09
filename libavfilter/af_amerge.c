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
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "avfilter.h"
#include "filters.h"
#include "audio.h"
#include "formats.h"

#define SWR_CH_MAX 64

typedef struct AMergeContext {
    const AVClass *class;
    int nb_inputs;
    int route[SWR_CH_MAX]; /**< channels routing, see copy_samples */
    int bps;
    struct amerge_input {
        int nb_ch;         /**< number of channels for the input */
    } *in;
} AMergeContext;

#define OFFSET(x) offsetof(AMergeContext, x)
#define FLAGS AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption amerge_options[] = {
    { "inputs", "specify the number of inputs", OFFSET(nb_inputs),
      AV_OPT_TYPE_INT, { .i64 = 2 }, 1, SWR_CH_MAX, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(amerge);

static av_cold void uninit(AVFilterContext *ctx)
{
    AMergeContext *s = ctx->priv;

    av_freep(&s->in);
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVSampleFormat packed_sample_fmts[] = {
        AV_SAMPLE_FMT_U8,
        AV_SAMPLE_FMT_S16,
        AV_SAMPLE_FMT_S32,
        AV_SAMPLE_FMT_FLT,
        AV_SAMPLE_FMT_DBL,
        AV_SAMPLE_FMT_NONE
    };
    AMergeContext *s = ctx->priv;
    AVChannelLayout *inlayout[SWR_CH_MAX] = { NULL }, outlayout = { 0 };
    uint64_t outmask = 0;
    AVFilterChannelLayouts *layouts;
    int i, ret, overlap = 0, nb_ch = 0;

    for (i = 0; i < s->nb_inputs; i++) {
        if (!ctx->inputs[i]->incfg.channel_layouts ||
            !ctx->inputs[i]->incfg.channel_layouts->nb_channel_layouts) {
            av_log(ctx, AV_LOG_WARNING,
                   "No channel layout for input %d\n", i + 1);
            return AVERROR(EAGAIN);
        }
        inlayout[i] = &ctx->inputs[i]->incfg.channel_layouts->channel_layouts[0];
        if (ctx->inputs[i]->incfg.channel_layouts->nb_channel_layouts > 1) {
            char buf[256];
            av_channel_layout_describe(inlayout[i], buf, sizeof(buf));
            av_log(ctx, AV_LOG_INFO, "Using \"%s\" for input %d\n", buf, i + 1);
        }
        s->in[i].nb_ch = FF_LAYOUT2COUNT(inlayout[i]);
        if (s->in[i].nb_ch) {
            overlap++;
        } else {
            s->in[i].nb_ch = inlayout[i]->nb_channels;
            if (av_channel_layout_subset(inlayout[i], outmask))
                overlap++;
            outmask |= inlayout[i]->order == AV_CHANNEL_ORDER_NATIVE ?
                       inlayout[i]->u.mask : 0;
        }
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
        av_channel_layout_default(&outlayout, nb_ch);
        if (!KNOWN(&outlayout) && nb_ch)
            av_channel_layout_from_mask(&outlayout, 0xFFFFFFFFFFFFFFFFULL >> (64 - nb_ch));
    } else {
        int *route[SWR_CH_MAX];
        int c, out_ch_number = 0;

        av_channel_layout_from_mask(&outlayout, outmask);
        route[0] = s->route;
        for (i = 1; i < s->nb_inputs; i++)
            route[i] = route[i - 1] + s->in[i - 1].nb_ch;
        for (c = 0; c < 64; c++)
            for (i = 0; i < s->nb_inputs; i++)
                if (av_channel_layout_index_from_channel(inlayout[i], c) >= 0)
                    *(route[i]++) = out_ch_number++;
    }
    if ((ret = ff_set_common_formats_from_list(ctx, packed_sample_fmts)) < 0)
        return ret;
    for (i = 0; i < s->nb_inputs; i++) {
        layouts = NULL;
        if ((ret = ff_add_channel_layout(&layouts, inlayout[i])) < 0)
            return ret;
        if ((ret = ff_channel_layouts_ref(layouts, &ctx->inputs[i]->outcfg.channel_layouts)) < 0)
            return ret;
    }
    layouts = NULL;
    if ((ret = ff_add_channel_layout(&layouts, &outlayout)) < 0)
        return ret;
    if ((ret = ff_channel_layouts_ref(layouts, &ctx->outputs[0]->incfg.channel_layouts)) < 0)
        return ret;

    return ff_set_common_all_samplerates(ctx);
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AMergeContext *s = ctx->priv;
    AVBPrint bp;
    int i;

    s->bps = av_get_bytes_per_sample(outlink->format);
    outlink->time_base   = ctx->inputs[0]->time_base;

    av_bprint_init(&bp, 0, AV_BPRINT_SIZE_AUTOMATIC);
    for (i = 0; i < s->nb_inputs; i++) {
        av_bprintf(&bp, "%sin%d:", i ? " + " : "", i);
        av_channel_layout_describe_bprint(&ctx->inputs[i]->ch_layout, &bp);
    }
    av_bprintf(&bp, " -> out:");
    av_channel_layout_describe_bprint(&outlink->ch_layout, &bp);
    av_log(ctx, AV_LOG_VERBOSE, "%s\n", bp.str);

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

static void free_frames(int nb_inputs, AVFrame **input_frames)
{
    int i;
    for (i = 0; i < nb_inputs; i++)
        av_frame_free(&input_frames[i]);
}

static int try_push_frame(AVFilterContext *ctx, int nb_samples)
{
    AMergeContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    int i, ret;
    AVFrame *outbuf, *inbuf[SWR_CH_MAX] = { NULL };
    uint8_t *outs, *ins[SWR_CH_MAX];

    for (i = 0; i < ctx->nb_inputs; i++) {
        ret = ff_inlink_consume_samples(ctx->inputs[i], nb_samples, nb_samples, &inbuf[i]);
        if (ret < 0) {
            free_frames(i, inbuf);
            return ret;
        }
        ins[i] = inbuf[i]->data[0];
    }

    outbuf = ff_get_audio_buffer(outlink, nb_samples);
    if (!outbuf) {
        free_frames(s->nb_inputs, inbuf);
        return AVERROR(ENOMEM);
    }

    outs = outbuf->data[0];
    outbuf->pts = inbuf[0]->pts;

    outbuf->nb_samples     = nb_samples;
    outbuf->duration = av_rescale_q(outbuf->nb_samples,
                                    av_make_q(1, outlink->sample_rate),
                                    outlink->time_base);

    if ((ret = av_channel_layout_copy(&outbuf->ch_layout, &outlink->ch_layout)) < 0) {
        free_frames(s->nb_inputs, inbuf);
        av_frame_free(&outbuf);
        return ret;
    }

    while (nb_samples) {
        /* Unroll the most common sample formats: speed +~350% for the loop,
           +~13% overall (including two common decoders) */
        switch (s->bps) {
            case 1:
                copy_samples(s->nb_inputs, s->in, s->route, ins, &outs, nb_samples, 1);
                break;
            case 2:
                copy_samples(s->nb_inputs, s->in, s->route, ins, &outs, nb_samples, 2);
                break;
            case 4:
                copy_samples(s->nb_inputs, s->in, s->route, ins, &outs, nb_samples, 4);
                break;
            default:
                copy_samples(s->nb_inputs, s->in, s->route, ins, &outs, nb_samples, s->bps);
                break;
        }

        nb_samples = 0;
    }

    free_frames(s->nb_inputs, inbuf);
    return ff_filter_frame(outlink, outbuf);
}

static int activate(AVFilterContext *ctx)
{
    int i, status;
    int ret, nb_samples;
    int64_t pts;

    FF_FILTER_FORWARD_STATUS_BACK_ALL(ctx->outputs[0], ctx);

    nb_samples = ff_inlink_queued_samples(ctx->inputs[0]);
    for (i = 1; i < ctx->nb_inputs && nb_samples > 0; i++) {
        nb_samples = FFMIN(ff_inlink_queued_samples(ctx->inputs[i]), nb_samples);
    }

    if (nb_samples) {
        ret = try_push_frame(ctx, nb_samples);
        if (ret < 0)
            return ret;
    }

    for (i = 0; i < ctx->nb_inputs; i++) {
        if (ff_inlink_queued_samples(ctx->inputs[i]))
            continue;

        if (ff_inlink_acknowledge_status(ctx->inputs[i], &status, &pts)) {
            ff_outlink_set_status(ctx->outputs[0], status, pts);
            return 0;
        } else if (ff_outlink_frame_wanted(ctx->outputs[0])) {
            ff_inlink_request_frame(ctx->inputs[i]);
            return 0;
        }
    }

    return 0;
}

static av_cold int init(AVFilterContext *ctx)
{
    AMergeContext *s = ctx->priv;
    int i, ret;

    s->in = av_calloc(s->nb_inputs, sizeof(*s->in));
    if (!s->in)
        return AVERROR(ENOMEM);
    for (i = 0; i < s->nb_inputs; i++) {
        char *name = av_asprintf("in%d", i);
        AVFilterPad pad = {
            .name             = name,
            .type             = AVMEDIA_TYPE_AUDIO,
        };
        if (!name)
            return AVERROR(ENOMEM);
        if ((ret = ff_append_inpad_free_name(ctx, &pad)) < 0)
            return ret;
    }
    return 0;
}

static const AVFilterPad amerge_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_AUDIO,
        .config_props  = config_output,
    },
};

const FFFilter ff_af_amerge = {
    .p.name        = "amerge",
    .p.description = NULL_IF_CONFIG_SMALL("Merge two or more audio streams into "
                                          "a single multi-channel stream."),
    .p.inputs      = NULL,
    .p.priv_class  = &amerge_class,
    .p.flags       = AVFILTER_FLAG_DYNAMIC_INPUTS,
    .priv_size     = sizeof(AMergeContext),
    .init          = init,
    .uninit        = uninit,
    .activate      = activate,
    FILTER_OUTPUTS(amerge_outputs),
    FILTER_QUERY_FUNC(query_formats),
};
