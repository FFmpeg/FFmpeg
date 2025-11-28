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

#include "libavutil/avassert.h"
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
    int layout_mode;       /**< the method for determining the output channel layout */
} AMergeContext;

#define OFFSET(x) offsetof(AMergeContext, x)
#define FLAGS AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

enum LayoutModes {
    LM_LEGACY,
    LM_RESET,
    LM_NORMAL,
    NB_LAYOUTMODES
};

static const AVOption amerge_options[] = {
    { "inputs", "specify the number of inputs", OFFSET(nb_inputs),
      AV_OPT_TYPE_INT, { .i64 = 2 }, 1, SWR_CH_MAX, FLAGS },
    { "layout_mode",   "method used to determine the output channel layout", OFFSET(layout_mode),
      AV_OPT_TYPE_INT, { .i64 = LM_LEGACY }, 0, NB_LAYOUTMODES - 1, FLAGS, .unit = "layout_mode"},
        { "legacy",   NULL, 0, AV_OPT_TYPE_CONST, {.i64 = LM_LEGACY   }, 0, 0, FLAGS, .unit = "layout_mode" },
        { "reset",    NULL, 0, AV_OPT_TYPE_CONST, {.i64 = LM_RESET    }, 0, 0, FLAGS, .unit = "layout_mode" },
        { "normal",   NULL, 0, AV_OPT_TYPE_CONST, {.i64 = LM_NORMAL   }, 0, 0, FLAGS, .unit = "layout_mode" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(amerge);

static av_cold void uninit(AVFilterContext *ctx)
{
    AMergeContext *s = ctx->priv;

    av_freep(&s->in);
}

#define INLAYOUT(ctx, i) (&(ctx)->inputs[i]->incfg.channel_layouts->channel_layouts[0])

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
    AVChannelLayout outlayout = { 0 };
    uint64_t outmask = 0;
    AVFilterChannelLayouts *layouts;
    int i, ret, nb_ch = 0;
    int native_layout_routes[SWR_CH_MAX] = { 0 };

    for (i = 0; i < s->nb_inputs; i++) {
        if (!ctx->inputs[i]->incfg.channel_layouts ||
            !ctx->inputs[i]->incfg.channel_layouts->nb_channel_layouts) {
            av_log(ctx, AV_LOG_WARNING,
                   "No channel layout for input %d\n", i + 1);
            return AVERROR(EAGAIN);
        }
        if (ctx->inputs[i]->incfg.channel_layouts->nb_channel_layouts > 1) {
            char buf[256];
            av_channel_layout_describe(INLAYOUT(ctx, i), buf, sizeof(buf));
            av_log(ctx, AV_LOG_INFO, "Using \"%s\" for input %d\n", buf, i + 1);
        }
        s->in[i].nb_ch = INLAYOUT(ctx, i)->nb_channels;
        nb_ch += s->in[i].nb_ch;
    }
    if (nb_ch > SWR_CH_MAX) {
        av_log(ctx, AV_LOG_ERROR, "Too many channels (max %d)\n", SWR_CH_MAX);
        return AVERROR(EINVAL);
    }
    ret = av_channel_layout_custom_init(&outlayout, nb_ch);
    if (ret < 0)
        return ret;
    for (int i = 0, ch_idx = 0; i < s->nb_inputs; i++) {
        for (int j = 0; j < s->in[i].nb_ch; j++) {
            enum AVChannel id = av_channel_layout_channel_from_index(INLAYOUT(ctx, i), j);
            if (INLAYOUT(ctx, i)->order == AV_CHANNEL_ORDER_CUSTOM)
                outlayout.u.map[ch_idx] = INLAYOUT(ctx, i)->u.map[j];
            else
                outlayout.u.map[ch_idx].id = (id == AV_CHAN_NONE ? AV_CHAN_UNKNOWN : id);
            if (id >= 0 && id < 64) {
                outmask |= (1ULL << id);
                native_layout_routes[id] = ch_idx;
            }
            s->route[ch_idx] = ch_idx;
            ch_idx++;
        }
    }
    switch (s->layout_mode) {
    case LM_LEGACY:
        av_channel_layout_uninit(&outlayout);
        if (av_popcount64(outmask) != nb_ch) {
            av_log(ctx, AV_LOG_WARNING,
                   "Input channel layouts overlap: "
                   "output layout will be determined by the number of distinct input channels\n");
            av_channel_layout_default(&outlayout, nb_ch);
            if (!KNOWN(&outlayout) && nb_ch)
                av_channel_layout_from_mask(&outlayout, 0xFFFFFFFFFFFFFFFFULL >> (64 - nb_ch));
        } else {
            for (int c = 0, ch_idx = 0; c < 64; c++)
                if ((1ULL << c) & outmask)
                    s->route[native_layout_routes[c]] = ch_idx++;
            av_channel_layout_from_mask(&outlayout, outmask);
        }
        break;
    case LM_RESET:
        av_channel_layout_uninit(&outlayout);
        outlayout.order = AV_CHANNEL_ORDER_UNSPEC;
        outlayout.nb_channels = nb_ch;
        break;
    case LM_NORMAL:
        ret = av_channel_layout_retype(&outlayout, 0, AV_CHANNEL_LAYOUT_RETYPE_FLAG_CANONICAL);
        if (ret < 0)
            goto out;
        break;
    default:
        av_unreachable("Invalid layout_mode");
    }
    if ((ret = ff_set_common_formats_from_list(ctx, packed_sample_fmts)) < 0)
        goto out;
    for (i = 0; i < s->nb_inputs; i++) {
        layouts = NULL;
        if ((ret = ff_add_channel_layout(&layouts, INLAYOUT(ctx, i))) < 0)
            goto out;
        if ((ret = ff_channel_layouts_ref(layouts, &ctx->inputs[i]->outcfg.channel_layouts)) < 0)
            goto out;
    }
    layouts = NULL;
    if ((ret = ff_add_channel_layout(&layouts, &outlayout)) < 0)
        goto out;
    if ((ret = ff_channel_layouts_ref(layouts, &ctx->outputs[0]->incfg.channel_layouts)) < 0)
        goto out;

    ret = ff_set_common_all_samplerates(ctx);
out:
    av_channel_layout_uninit(&outlayout);
    return ret;
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
