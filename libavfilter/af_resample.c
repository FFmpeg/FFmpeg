/*
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

/**
 * @file
 * sample format and channel layout conversion audio filter
 */

#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/common.h"
#include "libavutil/dict.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"

#include "libavresample/avresample.h"

#include "audio.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"

typedef struct ResampleContext {
    const AVClass *class;
    AVAudioResampleContext *avr;
    AVDictionary *options;

    int64_t next_pts;

    /* set by filter_frame() to signal an output frame to request_frame() */
    int got_output;
} ResampleContext;

static av_cold int init(AVFilterContext *ctx, AVDictionary **opts)
{
    ResampleContext *s = ctx->priv;
    const AVClass *avr_class = avresample_get_class();
    AVDictionaryEntry *e = NULL;

    while ((e = av_dict_get(*opts, "", e, AV_DICT_IGNORE_SUFFIX))) {
        if (av_opt_find(&avr_class, e->key, NULL, 0,
                        AV_OPT_SEARCH_FAKE_OBJ | AV_OPT_SEARCH_CHILDREN))
            av_dict_set(&s->options, e->key, e->value, 0);
    }

    e = NULL;
    while ((e = av_dict_get(s->options, "", e, AV_DICT_IGNORE_SUFFIX)))
        av_dict_set(opts, e->key, NULL, 0);

    /* do not allow the user to override basic format options */
    av_dict_set(&s->options,  "in_channel_layout", NULL, 0);
    av_dict_set(&s->options, "out_channel_layout", NULL, 0);
    av_dict_set(&s->options,  "in_sample_fmt",     NULL, 0);
    av_dict_set(&s->options, "out_sample_fmt",     NULL, 0);
    av_dict_set(&s->options,  "in_sample_rate",    NULL, 0);
    av_dict_set(&s->options, "out_sample_rate",    NULL, 0);

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    ResampleContext *s = ctx->priv;

    if (s->avr) {
        avresample_close(s->avr);
        avresample_free(&s->avr);
    }
    av_dict_free(&s->options);
}

static int query_formats(AVFilterContext *ctx)
{
    AVFilterLink *inlink  = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];

    AVFilterFormats        *in_formats      = ff_all_formats(AVMEDIA_TYPE_AUDIO);
    AVFilterFormats        *out_formats     = ff_all_formats(AVMEDIA_TYPE_AUDIO);
    AVFilterFormats        *in_samplerates  = ff_all_samplerates();
    AVFilterFormats        *out_samplerates = ff_all_samplerates();
    AVFilterChannelLayouts *in_layouts      = ff_all_channel_layouts();
    AVFilterChannelLayouts *out_layouts     = ff_all_channel_layouts();

    ff_formats_ref(in_formats,  &inlink->out_formats);
    ff_formats_ref(out_formats, &outlink->in_formats);

    ff_formats_ref(in_samplerates,  &inlink->out_samplerates);
    ff_formats_ref(out_samplerates, &outlink->in_samplerates);

    ff_channel_layouts_ref(in_layouts,  &inlink->out_channel_layouts);
    ff_channel_layouts_ref(out_layouts, &outlink->in_channel_layouts);

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AVFilterLink *inlink = ctx->inputs[0];
    ResampleContext   *s = ctx->priv;
    char buf1[64], buf2[64];
    int ret;

    if (s->avr) {
        avresample_close(s->avr);
        avresample_free(&s->avr);
    }

    if (inlink->channel_layout == outlink->channel_layout &&
        inlink->sample_rate    == outlink->sample_rate    &&
        (inlink->format        == outlink->format ||
        (av_get_channel_layout_nb_channels(inlink->channel_layout)  == 1 &&
         av_get_channel_layout_nb_channels(outlink->channel_layout) == 1 &&
         av_get_planar_sample_fmt(inlink->format) ==
         av_get_planar_sample_fmt(outlink->format))))
        return 0;

    if (!(s->avr = avresample_alloc_context()))
        return AVERROR(ENOMEM);

    if (s->options) {
        AVDictionaryEntry *e = NULL;
        while ((e = av_dict_get(s->options, "", e, AV_DICT_IGNORE_SUFFIX)))
            av_log(ctx, AV_LOG_VERBOSE, "lavr option: %s=%s\n", e->key, e->value);

        av_opt_set_dict(s->avr, &s->options);
    }

    av_opt_set_int(s->avr,  "in_channel_layout", inlink ->channel_layout, 0);
    av_opt_set_int(s->avr, "out_channel_layout", outlink->channel_layout, 0);
    av_opt_set_int(s->avr,  "in_sample_fmt",     inlink ->format,         0);
    av_opt_set_int(s->avr, "out_sample_fmt",     outlink->format,         0);
    av_opt_set_int(s->avr,  "in_sample_rate",    inlink ->sample_rate,    0);
    av_opt_set_int(s->avr, "out_sample_rate",    outlink->sample_rate,    0);

    if ((ret = avresample_open(s->avr)) < 0)
        return ret;

    outlink->time_base = (AVRational){ 1, outlink->sample_rate };
    s->next_pts        = AV_NOPTS_VALUE;

    av_get_channel_layout_string(buf1, sizeof(buf1),
                                 -1, inlink ->channel_layout);
    av_get_channel_layout_string(buf2, sizeof(buf2),
                                 -1, outlink->channel_layout);
    av_log(ctx, AV_LOG_VERBOSE,
           "fmt:%s srate:%d cl:%s -> fmt:%s srate:%d cl:%s\n",
           av_get_sample_fmt_name(inlink ->format), inlink ->sample_rate, buf1,
           av_get_sample_fmt_name(outlink->format), outlink->sample_rate, buf2);

    return 0;
}

static int request_frame(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    ResampleContext   *s = ctx->priv;
    int ret = 0;

    s->got_output = 0;
    while (ret >= 0 && !s->got_output)
        ret = ff_request_frame(ctx->inputs[0]);

    /* flush the lavr delay buffer */
    if (ret == AVERROR_EOF && s->avr) {
        AVFrame *frame;
        int nb_samples = av_rescale_rnd(avresample_get_delay(s->avr),
                                        outlink->sample_rate,
                                        ctx->inputs[0]->sample_rate,
                                        AV_ROUND_UP);

        if (!nb_samples)
            return ret;

        frame = ff_get_audio_buffer(outlink, nb_samples);
        if (!frame)
            return AVERROR(ENOMEM);

        ret = avresample_convert(s->avr, frame->extended_data,
                                 frame->linesize[0], nb_samples,
                                 NULL, 0, 0);
        if (ret <= 0) {
            av_frame_free(&frame);
            return (ret == 0) ? AVERROR_EOF : ret;
        }

        frame->pts = s->next_pts;
        return ff_filter_frame(outlink, frame);
    }
    return ret;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext  *ctx = inlink->dst;
    ResampleContext    *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    int ret;

    if (s->avr) {
        AVFrame *out;
        int delay, nb_samples;

        /* maximum possible samples lavr can output */
        delay      = avresample_get_delay(s->avr);
        nb_samples = av_rescale_rnd(in->nb_samples + delay,
                                    outlink->sample_rate, inlink->sample_rate,
                                    AV_ROUND_UP);

        out = ff_get_audio_buffer(outlink, nb_samples);
        if (!out) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        ret = avresample_convert(s->avr, out->extended_data, out->linesize[0],
                                 nb_samples, in->extended_data, in->linesize[0],
                                 in->nb_samples);
        if (ret <= 0) {
            av_frame_free(&out);
            if (ret < 0)
                goto fail;
        }

        av_assert0(!avresample_available(s->avr));

        if (s->next_pts == AV_NOPTS_VALUE) {
            if (in->pts == AV_NOPTS_VALUE) {
                av_log(ctx, AV_LOG_WARNING, "First timestamp is missing, "
                       "assuming 0.\n");
                s->next_pts = 0;
            } else
                s->next_pts = av_rescale_q(in->pts, inlink->time_base,
                                           outlink->time_base);
        }

        if (ret > 0) {
            out->nb_samples = ret;
            if (in->pts != AV_NOPTS_VALUE) {
                out->pts = av_rescale_q(in->pts, inlink->time_base,
                                            outlink->time_base) -
                               av_rescale(delay, outlink->sample_rate,
                                          inlink->sample_rate);
            } else
                out->pts = s->next_pts;

            s->next_pts = out->pts + out->nb_samples;

            ret = ff_filter_frame(outlink, out);
            s->got_output = 1;
        }

fail:
        av_frame_free(&in);
    } else {
        in->format = outlink->format;
        ret = ff_filter_frame(outlink, in);
        s->got_output = 1;
    }

    return ret;
}

static const AVClass *resample_child_class_next(const AVClass *prev)
{
    return prev ? NULL : avresample_get_class();
}

static void *resample_child_next(void *obj, void *prev)
{
    ResampleContext *s = obj;
    return prev ? NULL : s->avr;
}

static const AVClass resample_class = {
    .class_name       = "resample",
    .item_name        = av_default_item_name,
    .version          = LIBAVUTIL_VERSION_INT,
    .child_class_next = resample_child_class_next,
    .child_next       = resample_child_next,
};

static const AVFilterPad avfilter_af_resample_inputs[] = {
    {
        .name           = "default",
        .type           = AVMEDIA_TYPE_AUDIO,
        .filter_frame   = filter_frame,
    },
    { NULL }
};

static const AVFilterPad avfilter_af_resample_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_AUDIO,
        .config_props  = config_output,
        .request_frame = request_frame
    },
    { NULL }
};

AVFilter avfilter_af_resample = {
    .name          = "resample",
    .description   = NULL_IF_CONFIG_SMALL("Audio resampling and conversion."),
    .priv_size     = sizeof(ResampleContext),
    .priv_class    = &resample_class,

    .init_dict      = init,
    .uninit         = uninit,
    .query_formats  = query_formats,

    .inputs    = avfilter_af_resample_inputs,
    .outputs   = avfilter_af_resample_outputs,
};
