/*
 * Copyright (c) 2013 Paul B Mahol
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

#include "libavutil/avstring.h"
#include "libavutil/eval.h"
#include "libavutil/opt.h"
#include "libavutil/samplefmt.h"
#include "avfilter.h"
#include "audio.h"
#include "filters.h"
#include "internal.h"

typedef struct ChanDelay {
    int delay;
    unsigned delay_index;
    unsigned index;
    uint8_t *samples;
} ChanDelay;

typedef struct AudioDelayContext {
    const AVClass *class;
    char *delays;
    ChanDelay *chandelay;
    int nb_delays;
    int block_align;
    int64_t padding;
    int64_t max_delay;
    int64_t next_pts;
    int eof;

    void (*delay_channel)(ChanDelay *d, int nb_samples,
                          const uint8_t *src, uint8_t *dst);
} AudioDelayContext;

#define OFFSET(x) offsetof(AudioDelayContext, x)
#define A AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption adelay_options[] = {
    { "delays", "set list of delays for each channel", OFFSET(delays), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, A },
    { NULL }
};

AVFILTER_DEFINE_CLASS(adelay);

static int query_formats(AVFilterContext *ctx)
{
    AVFilterChannelLayouts *layouts;
    AVFilterFormats *formats;
    static const enum AVSampleFormat sample_fmts[] = {
        AV_SAMPLE_FMT_U8P, AV_SAMPLE_FMT_S16P, AV_SAMPLE_FMT_S32P,
        AV_SAMPLE_FMT_FLTP, AV_SAMPLE_FMT_DBLP,
        AV_SAMPLE_FMT_NONE
    };
    int ret;

    layouts = ff_all_channel_counts();
    if (!layouts)
        return AVERROR(ENOMEM);
    ret = ff_set_common_channel_layouts(ctx, layouts);
    if (ret < 0)
        return ret;

    formats = ff_make_format_list(sample_fmts);
    if (!formats)
        return AVERROR(ENOMEM);
    ret = ff_set_common_formats(ctx, formats);
    if (ret < 0)
        return ret;

    formats = ff_all_samplerates();
    if (!formats)
        return AVERROR(ENOMEM);
    return ff_set_common_samplerates(ctx, formats);
}

#define DELAY(name, type, fill)                                           \
static void delay_channel_## name ##p(ChanDelay *d, int nb_samples,       \
                                      const uint8_t *ssrc, uint8_t *ddst) \
{                                                                         \
    const type *src = (type *)ssrc;                                       \
    type *dst = (type *)ddst;                                             \
    type *samples = (type *)d->samples;                                   \
                                                                          \
    while (nb_samples) {                                                  \
        if (d->delay_index < d->delay) {                                  \
            const int len = FFMIN(nb_samples, d->delay - d->delay_index); \
                                                                          \
            memcpy(&samples[d->delay_index], src, len * sizeof(type));    \
            memset(dst, fill, len * sizeof(type));                        \
            d->delay_index += len;                                        \
            src += len;                                                   \
            dst += len;                                                   \
            nb_samples -= len;                                            \
        } else {                                                          \
            *dst = samples[d->index];                                     \
            samples[d->index] = *src;                                     \
            nb_samples--;                                                 \
            d->index++;                                                   \
            src++, dst++;                                                 \
            d->index = d->index >= d->delay ? 0 : d->index;               \
        }                                                                 \
    }                                                                     \
}

DELAY(u8,  uint8_t, 0x80)
DELAY(s16, int16_t, 0)
DELAY(s32, int32_t, 0)
DELAY(flt, float,   0)
DELAY(dbl, double,  0)

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    AudioDelayContext *s = ctx->priv;
    char *p, *arg, *saveptr = NULL;
    int i;

    s->chandelay = av_calloc(inlink->channels, sizeof(*s->chandelay));
    if (!s->chandelay)
        return AVERROR(ENOMEM);
    s->nb_delays = inlink->channels;
    s->block_align = av_get_bytes_per_sample(inlink->format);

    p = s->delays;
    for (i = 0; i < s->nb_delays; i++) {
        ChanDelay *d = &s->chandelay[i];
        float delay;
        char type = 0;
        int ret;

        if (!(arg = av_strtok(p, "|", &saveptr)))
            break;

        p = NULL;

        ret = av_sscanf(arg, "%d%c", &d->delay, &type);
        if (ret != 2 || type != 'S') {
            av_sscanf(arg, "%f", &delay);
            d->delay = delay * inlink->sample_rate / 1000.0;
        }

        if (d->delay < 0) {
            av_log(ctx, AV_LOG_ERROR, "Delay must be non negative number.\n");
            return AVERROR(EINVAL);
        }
    }

    s->padding = s->chandelay[0].delay;
    for (i = 1; i < s->nb_delays; i++) {
        ChanDelay *d = &s->chandelay[i];

        s->padding = FFMIN(s->padding, d->delay);
    }

    if (s->padding) {
        for (i = 0; i < s->nb_delays; i++) {
            ChanDelay *d = &s->chandelay[i];

            d->delay -= s->padding;
        }
    }

    for (i = 0; i < s->nb_delays; i++) {
        ChanDelay *d = &s->chandelay[i];

        if (!d->delay)
            continue;

        d->samples = av_malloc_array(d->delay, s->block_align);
        if (!d->samples)
            return AVERROR(ENOMEM);

        s->max_delay = FFMAX(s->max_delay, d->delay);
    }

    switch (inlink->format) {
    case AV_SAMPLE_FMT_U8P : s->delay_channel = delay_channel_u8p ; break;
    case AV_SAMPLE_FMT_S16P: s->delay_channel = delay_channel_s16p; break;
    case AV_SAMPLE_FMT_S32P: s->delay_channel = delay_channel_s32p; break;
    case AV_SAMPLE_FMT_FLTP: s->delay_channel = delay_channel_fltp; break;
    case AV_SAMPLE_FMT_DBLP: s->delay_channel = delay_channel_dblp; break;
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    AVFilterContext *ctx = inlink->dst;
    AudioDelayContext *s = ctx->priv;
    AVFrame *out_frame;
    int i;

    if (ctx->is_disabled || !s->delays)
        return ff_filter_frame(ctx->outputs[0], frame);

    out_frame = ff_get_audio_buffer(ctx->outputs[0], frame->nb_samples);
    if (!out_frame) {
        av_frame_free(&frame);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out_frame, frame);

    for (i = 0; i < s->nb_delays; i++) {
        ChanDelay *d = &s->chandelay[i];
        const uint8_t *src = frame->extended_data[i];
        uint8_t *dst = out_frame->extended_data[i];

        if (!d->delay)
            memcpy(dst, src, frame->nb_samples * s->block_align);
        else
            s->delay_channel(d, frame->nb_samples, src, dst);
    }

    out_frame->pts = s->next_pts;
    s->next_pts += av_rescale_q(frame->nb_samples, (AVRational){1, inlink->sample_rate}, inlink->time_base);
    av_frame_free(&frame);
    return ff_filter_frame(ctx->outputs[0], out_frame);
}

static int activate(AVFilterContext *ctx)
{
    AVFilterLink *inlink = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];
    AudioDelayContext *s = ctx->priv;
    AVFrame *frame = NULL;
    int ret, status;
    int64_t pts;

    FF_FILTER_FORWARD_STATUS_BACK(outlink, inlink);

    if (s->padding) {
        int nb_samples = FFMIN(s->padding, 2048);

        frame = ff_get_audio_buffer(outlink, nb_samples);
        if (!frame)
            return AVERROR(ENOMEM);
        s->padding -= nb_samples;

        av_samples_set_silence(frame->extended_data, 0,
                               frame->nb_samples,
                               outlink->channels,
                               frame->format);

        frame->pts = s->next_pts;
        if (s->next_pts != AV_NOPTS_VALUE)
            s->next_pts += av_rescale_q(nb_samples, (AVRational){1, outlink->sample_rate}, outlink->time_base);

        return ff_filter_frame(outlink, frame);
    }

    ret = ff_inlink_consume_frame(inlink, &frame);
    if (ret < 0)
        return ret;

    if (ret > 0)
        return filter_frame(inlink, frame);

    if (ff_inlink_acknowledge_status(inlink, &status, &pts)) {
        if (status == AVERROR_EOF)
            s->eof = 1;
    }

    if (s->eof && s->max_delay) {
        int nb_samples = FFMIN(s->max_delay, 2048);

        frame = ff_get_audio_buffer(outlink, nb_samples);
        if (!frame)
            return AVERROR(ENOMEM);
        s->max_delay -= nb_samples;

        av_samples_set_silence(frame->extended_data, 0,
                               frame->nb_samples,
                               outlink->channels,
                               frame->format);

        frame->pts = s->next_pts;
        return filter_frame(inlink, frame);
    }

    if (s->eof && s->max_delay == 0) {
        ff_outlink_set_status(outlink, AVERROR_EOF, s->next_pts);
        return 0;
    }

    if (!s->eof)
        FF_FILTER_FORWARD_WANTED(outlink, inlink);

    return FFERROR_NOT_READY;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    AudioDelayContext *s = ctx->priv;

    if (s->chandelay) {
        for (int i = 0; i < s->nb_delays; i++)
            av_freep(&s->chandelay[i].samples);
    }
    av_freep(&s->chandelay);
}

static const AVFilterPad adelay_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .config_props = config_input,
    },
    { NULL }
};

static const AVFilterPad adelay_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_AUDIO,
    },
    { NULL }
};

AVFilter ff_af_adelay = {
    .name          = "adelay",
    .description   = NULL_IF_CONFIG_SMALL("Delay one or more audio channels."),
    .query_formats = query_formats,
    .priv_size     = sizeof(AudioDelayContext),
    .priv_class    = &adelay_class,
    .activate      = activate,
    .uninit        = uninit,
    .inputs        = adelay_inputs,
    .outputs       = adelay_outputs,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL,
};
