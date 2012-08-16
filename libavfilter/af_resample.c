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
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"

#include "libavresample/avresample.h"

#include "audio.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"

typedef struct ResampleContext {
    AVAudioResampleContext *avr;

    int64_t next_pts;

    /* set by filter_samples() to signal an output frame to request_frame() */
    int got_output;
} ResampleContext;

static av_cold void uninit(AVFilterContext *ctx)
{
    ResampleContext *s = ctx->priv;

    if (s->avr) {
        avresample_close(s->avr);
        avresample_free(&s->avr);
    }
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
        inlink->format         == outlink->format)
        return 0;

    if (!(s->avr = avresample_alloc_context()))
        return AVERROR(ENOMEM);

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
        AVFilterBufferRef *buf;
        int nb_samples = av_rescale_rnd(avresample_get_delay(s->avr),
                                        outlink->sample_rate,
                                        ctx->inputs[0]->sample_rate,
                                        AV_ROUND_UP);

        if (!nb_samples)
            return ret;

        buf = ff_get_audio_buffer(outlink, AV_PERM_WRITE, nb_samples);
        if (!buf)
            return AVERROR(ENOMEM);

        ret = avresample_convert(s->avr, (void**)buf->extended_data,
                                 buf->linesize[0], nb_samples,
                                 NULL, 0, 0);
        if (ret <= 0) {
            avfilter_unref_buffer(buf);
            return (ret == 0) ? AVERROR_EOF : ret;
        }

        buf->pts = s->next_pts;
        return ff_filter_samples(outlink, buf);
    }
    return ret;
}

static int filter_samples(AVFilterLink *inlink, AVFilterBufferRef *buf)
{
    AVFilterContext  *ctx = inlink->dst;
    ResampleContext    *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    int ret;

    if (s->avr) {
        AVFilterBufferRef *buf_out;
        int delay, nb_samples;

        /* maximum possible samples lavr can output */
        delay      = avresample_get_delay(s->avr);
        nb_samples = av_rescale_rnd(buf->audio->nb_samples + delay,
                                    outlink->sample_rate, inlink->sample_rate,
                                    AV_ROUND_UP);

        buf_out = ff_get_audio_buffer(outlink, AV_PERM_WRITE, nb_samples);
        if (!buf_out) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        ret     = avresample_convert(s->avr, (void**)buf_out->extended_data,
                                     buf_out->linesize[0], nb_samples,
                                     (void**)buf->extended_data, buf->linesize[0],
                                     buf->audio->nb_samples);
        if (ret < 0) {
            avfilter_unref_buffer(buf_out);
            goto fail;
        }

        av_assert0(!avresample_available(s->avr));

        if (s->next_pts == AV_NOPTS_VALUE) {
            if (buf->pts == AV_NOPTS_VALUE) {
                av_log(ctx, AV_LOG_WARNING, "First timestamp is missing, "
                       "assuming 0.\n");
                s->next_pts = 0;
            } else
                s->next_pts = av_rescale_q(buf->pts, inlink->time_base,
                                           outlink->time_base);
        }

        if (ret > 0) {
            buf_out->audio->nb_samples = ret;
            if (buf->pts != AV_NOPTS_VALUE) {
                buf_out->pts = av_rescale_q(buf->pts, inlink->time_base,
                                            outlink->time_base) -
                               av_rescale(delay, outlink->sample_rate,
                                          inlink->sample_rate);
            } else
                buf_out->pts = s->next_pts;

            s->next_pts = buf_out->pts + buf_out->audio->nb_samples;

            ret = ff_filter_samples(outlink, buf_out);
            s->got_output = 1;
        }

fail:
        avfilter_unref_buffer(buf);
    } else {
        ret = ff_filter_samples(outlink, buf);
        s->got_output = 1;
    }

    return ret;
}

AVFilter avfilter_af_resample = {
    .name          = "resample",
    .description   = NULL_IF_CONFIG_SMALL("Audio resampling and conversion."),
    .priv_size     = sizeof(ResampleContext),

    .uninit         = uninit,
    .query_formats  = query_formats,

    .inputs    = (const AVFilterPad[]) {{ .name            = "default",
                                          .type            = AVMEDIA_TYPE_AUDIO,
                                          .filter_samples  = filter_samples,
                                          .min_perms       = AV_PERM_READ },
                                        { .name = NULL}},
    .outputs   = (const AVFilterPad[]) {{ .name          = "default",
                                          .type          = AVMEDIA_TYPE_AUDIO,
                                          .config_props  = config_output,
                                          .request_frame = request_frame },
                                        { .name = NULL}},
};
