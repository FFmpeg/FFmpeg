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
    int64_t delay;
    size_t delay_index;
    size_t index;
    unsigned int samples_size;
    uint8_t *samples;
} ChanDelay;

typedef struct AudioDelayContext {
    const AVClass *class;
    int all;
    char *delays;
    ChanDelay *chandelay;
    int nb_delays;
    int block_align;
    int64_t padding;
    int64_t max_delay;
    int64_t offset;
    int64_t next_pts;
    int eof;

    AVFrame *input;

    void (*delay_channel)(ChanDelay *d, int nb_samples,
                          const uint8_t *src, uint8_t *dst);
    int (*resize_channel_samples)(ChanDelay *d, int64_t new_delay);
} AudioDelayContext;

#define OFFSET(x) offsetof(AudioDelayContext, x)
#define A AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption adelay_options[] = {
    { "delays", "set list of delays for each channel", OFFSET(delays), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, A | AV_OPT_FLAG_RUNTIME_PARAM },
    { "all",    "use last available delay for remained channels", OFFSET(all), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, A },
    { NULL }
};

AVFILTER_DEFINE_CLASS(adelay);

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

#define CHANGE_DELAY(name, type, fill)                                                                  \
static int resize_samples_## name ##p(ChanDelay *d, int64_t new_delay)                                  \
{                                                                                                       \
    type *samples;                                                                                      \
                                                                                                        \
    if (new_delay == d->delay) {                                                                        \
        return 0;                                                                                       \
    }                                                                                                   \
                                                                                                        \
    if (new_delay == 0) {                                                                               \
        av_freep(&d->samples);                                                                          \
        d->samples_size = 0;                                                                            \
        d->delay = 0;                                                                                   \
        d->index = 0;                                                                                   \
        d->delay_index = 0;                                                                             \
        return 0;                                                                                       \
    }                                                                                                   \
                                                                                                        \
    samples = (type *) av_fast_realloc(d->samples, &d->samples_size, new_delay * sizeof(type));         \
    if (!samples) {                                                                                     \
        return AVERROR(ENOMEM);                                                                         \
    }                                                                                                   \
                                                                                                        \
    if (new_delay < d->delay) {                                                                         \
        if (d->index > new_delay) {                                                                     \
            d->index -= new_delay;                                                                      \
            memmove(samples, &samples[new_delay], d->index * sizeof(type));                             \
            d->delay_index = new_delay;                                                                 \
        } else if (d->delay_index > d->index) {                                                         \
            memmove(&samples[d->index], &samples[d->index+(d->delay-new_delay)],                        \
                    (new_delay - d->index) * sizeof(type));                                             \
            d->delay_index -= d->delay - new_delay;                                                     \
        }                                                                                               \
    } else {                                                                                            \
        size_t block_size;                                                                              \
        if (d->delay_index >= d->delay) {                                                               \
            block_size = (d->delay - d->index) * sizeof(type);                                          \
            memmove(&samples[d->index+(new_delay - d->delay)], &samples[d->index], block_size);         \
            d->delay_index = new_delay;                                                                 \
        } else {                                                                                        \
            d->delay_index += new_delay - d->delay;                                                     \
        }                                                                                               \
        block_size = (new_delay - d->delay) * sizeof(type);                                             \
        memset(&samples[d->index], fill, block_size);                                                   \
    }                                                                                                   \
    d->delay = new_delay;                                                                               \
    d->samples = (void *) samples;                                                                      \
    return 0;                                                                                           \
}

CHANGE_DELAY(u8,  uint8_t, 0x80)
CHANGE_DELAY(s16, int16_t, 0)
CHANGE_DELAY(s32, int32_t, 0)
CHANGE_DELAY(flt, float,   0)
CHANGE_DELAY(dbl, double,  0)

static int parse_delays(char *p, char **saveptr, int64_t *result, AVFilterContext *ctx, int sample_rate) {
    float delay, div;
    int ret;
    char *arg;
    char type = 0;

    if (!(arg = av_strtok(p, "|", saveptr)))
        return 1;

    ret = av_sscanf(arg, "%"SCNd64"%c", result, &type);
    if (ret != 2 || type != 'S') {
        div = type == 's' ? 1.0 : 1000.0;
        if (av_sscanf(arg, "%f", &delay) != 1) {
            av_log(ctx, AV_LOG_ERROR, "Invalid syntax for delay.\n");
            return AVERROR(EINVAL);
        }
        *result = delay * sample_rate / div;
    }

    if (*result < 0) {
        av_log(ctx, AV_LOG_ERROR, "Delay must be non negative number.\n");
        return AVERROR(EINVAL);
    }
    return 0;
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    AudioDelayContext *s = ctx->priv;
    char *p, *saveptr = NULL;
    int i;

    s->next_pts = AV_NOPTS_VALUE;
    s->chandelay = av_calloc(inlink->ch_layout.nb_channels, sizeof(*s->chandelay));
    if (!s->chandelay)
        return AVERROR(ENOMEM);
    s->nb_delays = inlink->ch_layout.nb_channels;
    s->block_align = av_get_bytes_per_sample(inlink->format);

    p = s->delays;
    for (i = 0; i < s->nb_delays; i++) {
        ChanDelay *d = &s->chandelay[i];
        int ret;

        ret = parse_delays(p, &saveptr, &d->delay, ctx, inlink->sample_rate);
        if (ret == 1)
            break;
        else if (ret < 0)
            return ret;
        p = NULL;
    }

    if (s->all && i) {
        for (int j = i; j < s->nb_delays; j++)
            s->chandelay[j].delay = s->chandelay[i-1].delay;
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

        s->offset = av_rescale_q(s->padding,
                                 av_make_q(1, inlink->sample_rate),
                                 inlink->time_base);
    }

    for (i = 0; i < s->nb_delays; i++) {
        ChanDelay *d = &s->chandelay[i];

        if (!d->delay)
            continue;

        if (d->delay > SIZE_MAX) {
            av_log(ctx, AV_LOG_ERROR, "Requested delay is too big.\n");
            return AVERROR(EINVAL);
        }

        d->samples = av_malloc_array(d->delay, s->block_align);
        if (!d->samples)
            return AVERROR(ENOMEM);
        d->samples_size = d->delay * s->block_align;

        s->max_delay = FFMAX(s->max_delay, d->delay);
    }

    switch (inlink->format) {
    case AV_SAMPLE_FMT_U8P : s->delay_channel = delay_channel_u8p ;
                             s->resize_channel_samples = resize_samples_u8p; break;
    case AV_SAMPLE_FMT_S16P: s->delay_channel = delay_channel_s16p;
                             s->resize_channel_samples = resize_samples_s16p; break;
    case AV_SAMPLE_FMT_S32P: s->delay_channel = delay_channel_s32p;
                             s->resize_channel_samples = resize_samples_s32p; break;
    case AV_SAMPLE_FMT_FLTP: s->delay_channel = delay_channel_fltp;
                             s->resize_channel_samples = resize_samples_fltp; break;
    case AV_SAMPLE_FMT_DBLP: s->delay_channel = delay_channel_dblp;
                             s->resize_channel_samples = resize_samples_dblp; break;
    }

    return 0;
}

static int process_command(AVFilterContext *ctx, const char *cmd, const char *args,
                           char *res, int res_len, int flags)
{
    int ret = AVERROR(ENOSYS);
    AVFilterLink *inlink = ctx->inputs[0];
    AudioDelayContext *s = ctx->priv;

    if (!strcmp(cmd, "delays")) {
        int64_t delay;
        char *p, *saveptr = NULL;
        int64_t all_delay = -1;
        int64_t max_delay = 0;
        char *args_cpy = av_strdup(args);
        if (args_cpy == NULL) {
            return AVERROR(ENOMEM);
        }

        ret = 0;
        p = args_cpy;

        if (!strncmp(args, "all:", 4)) {
            p = &args_cpy[4];
            ret = parse_delays(p, &saveptr, &all_delay, ctx, inlink->sample_rate);
            if (ret == 1)
                ret = AVERROR(EINVAL);
            else if (ret == 0)
                delay = all_delay;
        }

        if (!ret) {
            for (int i = 0; i < s->nb_delays; i++) {
                ChanDelay *d = &s->chandelay[i];

                if (all_delay < 0) {
                    ret = parse_delays(p, &saveptr, &delay, ctx, inlink->sample_rate);
                    if (ret != 0) {
                        ret = 0;
                        break;
                    }
                    p = NULL;
                }

                ret = s->resize_channel_samples(d, delay);
                if (ret)
                    break;
                max_delay = FFMAX(max_delay, d->delay);
            }
            s->max_delay = FFMAX(s->max_delay, max_delay);
        }
        av_freep(&args_cpy);
    }
    return ret;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    AudioDelayContext *s = ctx->priv;
    AVFrame *out_frame;
    int i;

    if (ctx->is_disabled || !s->delays) {
        s->input = NULL;
        return ff_filter_frame(outlink, frame);
    }

    s->next_pts = av_rescale_q(frame->pts, inlink->time_base, outlink->time_base);

    out_frame = ff_get_audio_buffer(outlink, frame->nb_samples);
    if (!out_frame) {
        s->input = NULL;
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

    out_frame->pts = s->next_pts + s->offset;
    out_frame->duration = av_rescale_q(out_frame->nb_samples, (AVRational){1, outlink->sample_rate}, outlink->time_base);
    s->next_pts += out_frame->duration;
    av_frame_free(&frame);
    s->input = NULL;
    return ff_filter_frame(outlink, out_frame);
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

    if (!s->input) {
        ret = ff_inlink_consume_frame(inlink, &s->input);
        if (ret < 0)
            return ret;
    }

    if (ff_inlink_acknowledge_status(inlink, &status, &pts)) {
        if (status == AVERROR_EOF)
            s->eof = 1;
    }

    if (s->next_pts == AV_NOPTS_VALUE && pts != AV_NOPTS_VALUE)
        s->next_pts = av_rescale_q(pts, inlink->time_base, outlink->time_base);

    if (s->padding) {
        int nb_samples = FFMIN(s->padding, 2048);

        frame = ff_get_audio_buffer(outlink, nb_samples);
        if (!frame)
            return AVERROR(ENOMEM);
        s->padding -= nb_samples;

        av_samples_set_silence(frame->extended_data, 0,
                               frame->nb_samples,
                               outlink->ch_layout.nb_channels,
                               frame->format);

        frame->duration = av_rescale_q(frame->nb_samples,
                                       (AVRational){1, outlink->sample_rate},
                                       outlink->time_base);
        frame->pts = s->next_pts;
        s->next_pts += frame->duration;

        return ff_filter_frame(outlink, frame);
    }

    if (s->input)
        return filter_frame(inlink, s->input);

    if (s->eof && s->max_delay) {
        int nb_samples = FFMIN(s->max_delay, 2048);

        frame = ff_get_audio_buffer(outlink, nb_samples);
        if (!frame)
            return AVERROR(ENOMEM);
        s->max_delay -= nb_samples;

        av_samples_set_silence(frame->extended_data, 0,
                               frame->nb_samples,
                               outlink->ch_layout.nb_channels,
                               frame->format);

        frame->duration = av_rescale_q(frame->nb_samples,
                                       (AVRational){1, outlink->sample_rate},
                                       outlink->time_base);
        frame->pts = s->next_pts;
        s->next_pts += frame->duration;
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
};

const AVFilter ff_af_adelay = {
    .name          = "adelay",
    .description   = NULL_IF_CONFIG_SMALL("Delay one or more audio channels."),
    .priv_size     = sizeof(AudioDelayContext),
    .priv_class    = &adelay_class,
    .activate      = activate,
    .uninit        = uninit,
    FILTER_INPUTS(adelay_inputs),
    FILTER_OUTPUTS(ff_audio_default_filterpad),
    FILTER_SAMPLEFMTS(AV_SAMPLE_FMT_U8P, AV_SAMPLE_FMT_S16P, AV_SAMPLE_FMT_S32P,
                      AV_SAMPLE_FMT_FLTP, AV_SAMPLE_FMT_DBLP),
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL,
    .process_command = process_command,
};
