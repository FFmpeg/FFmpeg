/*
 * Copyright (c) 2020 Paul B Mahol
 *
 * Speech Normalizer
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
 * Speech Normalizer
 */

#include <float.h>

#include "libavutil/avassert.h"
#include "libavutil/opt.h"

#define FF_BUFQUEUE_SIZE (1024)
#include "bufferqueue.h"

#include "audio.h"
#include "avfilter.h"
#include "filters.h"
#include "internal.h"

#define MAX_ITEMS  882000
#define MIN_PEAK (1. / 32768.)

typedef struct PeriodItem {
    int size;
    int type;
    double max_peak;
} PeriodItem;

typedef struct ChannelContext {
    int state;
    int bypass;
    PeriodItem pi[MAX_ITEMS];
    double gain_state;
    double pi_max_peak;
    int pi_start;
    int pi_end;
    int pi_size;
} ChannelContext;

typedef struct SpeechNormalizerContext {
    const AVClass *class;

    double peak_value;
    double max_expansion;
    double max_compression;
    double threshold_value;
    double raise_amount;
    double fall_amount;
    uint64_t channels;
    int invert;
    int link;

    ChannelContext *cc;
    double prev_gain;

    int max_period;
    int eof;
    int64_t pts;

    struct FFBufQueue queue;

    void (*analyze_channel)(AVFilterContext *ctx, ChannelContext *cc,
                            const uint8_t *srcp, int nb_samples);
    void (*filter_channels[2])(AVFilterContext *ctx,
                               AVFrame *in, int nb_samples);
} SpeechNormalizerContext;

#define OFFSET(x) offsetof(SpeechNormalizerContext, x)
#define FLAGS AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption speechnorm_options[] = {
    { "peak", "set the peak value", OFFSET(peak_value), AV_OPT_TYPE_DOUBLE, {.dbl=0.95}, 0.0, 1.0, FLAGS },
    { "p",    "set the peak value", OFFSET(peak_value), AV_OPT_TYPE_DOUBLE, {.dbl=0.95}, 0.0, 1.0, FLAGS },
    { "expansion", "set the max expansion factor", OFFSET(max_expansion), AV_OPT_TYPE_DOUBLE, {.dbl=2.0}, 1.0, 50.0, FLAGS },
    { "e",         "set the max expansion factor", OFFSET(max_expansion), AV_OPT_TYPE_DOUBLE, {.dbl=2.0}, 1.0, 50.0, FLAGS },
    { "compression", "set the max compression factor", OFFSET(max_compression), AV_OPT_TYPE_DOUBLE, {.dbl=2.0}, 1.0, 50.0, FLAGS },
    { "c",           "set the max compression factor", OFFSET(max_compression), AV_OPT_TYPE_DOUBLE, {.dbl=2.0}, 1.0, 50.0, FLAGS },
    { "threshold", "set the threshold value", OFFSET(threshold_value), AV_OPT_TYPE_DOUBLE, {.dbl=0}, 0.0, 1.0, FLAGS },
    { "t",         "set the threshold value", OFFSET(threshold_value), AV_OPT_TYPE_DOUBLE, {.dbl=0}, 0.0, 1.0, FLAGS },
    { "raise", "set the expansion raising amount", OFFSET(raise_amount), AV_OPT_TYPE_DOUBLE, {.dbl=0.001}, 0.0, 1.0, FLAGS },
    { "r",     "set the expansion raising amount", OFFSET(raise_amount), AV_OPT_TYPE_DOUBLE, {.dbl=0.001}, 0.0, 1.0, FLAGS },
    { "fall", "set the compression raising amount", OFFSET(fall_amount), AV_OPT_TYPE_DOUBLE, {.dbl=0.001}, 0.0, 1.0, FLAGS },
    { "f",    "set the compression raising amount", OFFSET(fall_amount), AV_OPT_TYPE_DOUBLE, {.dbl=0.001}, 0.0, 1.0, FLAGS },
    { "channels", "set channels to filter", OFFSET(channels), AV_OPT_TYPE_CHANNEL_LAYOUT, {.i64=-1}, INT64_MIN, INT64_MAX, FLAGS },
    { "h",        "set channels to filter", OFFSET(channels), AV_OPT_TYPE_CHANNEL_LAYOUT, {.i64=-1}, INT64_MIN, INT64_MAX, FLAGS },
    { "invert", "set inverted filtering", OFFSET(invert), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, FLAGS },
    { "i",      "set inverted filtering", OFFSET(invert), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, FLAGS },
    { "link", "set linked channels filtering", OFFSET(link), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, FLAGS },
    { "l",    "set linked channels filtering", OFFSET(link), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(speechnorm);

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *formats;
    AVFilterChannelLayouts *layouts;
    static const enum AVSampleFormat sample_fmts[] = {
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

static int get_pi_samples(PeriodItem *pi, int start, int end, int remain)
{
    int sum;

    if (pi[start].type == 0)
        return remain;

    sum = remain;
    while (start != end) {
        start++;
        if (start >= MAX_ITEMS)
            start = 0;
        if (pi[start].type == 0)
            break;
        av_assert0(pi[start].size > 0);
        sum += pi[start].size;
    }

    return sum;
}

static int available_samples(AVFilterContext *ctx)
{
    SpeechNormalizerContext *s = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    int min_pi_nb_samples;

    min_pi_nb_samples = get_pi_samples(s->cc[0].pi, s->cc[0].pi_start, s->cc[0].pi_end, s->cc[0].pi_size);
    for (int ch = 1; ch < inlink->channels && min_pi_nb_samples > 0; ch++) {
        ChannelContext *cc = &s->cc[ch];

        min_pi_nb_samples = FFMIN(min_pi_nb_samples, get_pi_samples(cc->pi, cc->pi_start, cc->pi_end, cc->pi_size));
    }

    return min_pi_nb_samples;
}

static void consume_pi(ChannelContext *cc, int nb_samples)
{
    if (cc->pi_size >= nb_samples) {
        cc->pi_size -= nb_samples;
    } else {
        av_assert0(0);
    }
}

static double next_gain(AVFilterContext *ctx, double pi_max_peak, int bypass, double state)
{
    SpeechNormalizerContext *s = ctx->priv;
    const double expansion = FFMIN(s->max_expansion, s->peak_value / pi_max_peak);
    const double compression = 1. / s->max_compression;
    const int type = s->invert ? pi_max_peak <= s->threshold_value : pi_max_peak >= s->threshold_value;

    if (bypass) {
        return 1.;
    } else if (type) {
        return FFMIN(expansion, state + s->raise_amount);
    } else {
        return FFMIN(expansion, FFMAX(compression, state - s->fall_amount));
    }
}

static void next_pi(AVFilterContext *ctx, ChannelContext *cc, int bypass)
{
    av_assert0(cc->pi_size >= 0);
    if (cc->pi_size == 0) {
        SpeechNormalizerContext *s = ctx->priv;
        int start = cc->pi_start;

        av_assert0(cc->pi[start].size > 0);
        av_assert0(cc->pi[start].type > 0 || s->eof);
        cc->pi_size = cc->pi[start].size;
        cc->pi_max_peak = cc->pi[start].max_peak;
        av_assert0(cc->pi_start != cc->pi_end || s->eof);
        start++;
        if (start >= MAX_ITEMS)
            start = 0;
        cc->pi_start = start;
        cc->gain_state = next_gain(ctx, cc->pi_max_peak, bypass, cc->gain_state);
    }
}

static double min_gain(AVFilterContext *ctx, ChannelContext *cc, int max_size)
{
    SpeechNormalizerContext *s = ctx->priv;
    double min_gain = s->max_expansion;
    double gain_state = cc->gain_state;
    int size = cc->pi_size;
    int idx = cc->pi_start;

    min_gain = FFMIN(min_gain, gain_state);
    while (size <= max_size) {
        if (idx == cc->pi_end)
            break;
        gain_state = next_gain(ctx, cc->pi[idx].max_peak, 0, gain_state);
        min_gain = FFMIN(min_gain, gain_state);
        size += cc->pi[idx].size;
        idx++;
        if (idx >= MAX_ITEMS)
            idx = 0;
    }

    return min_gain;
}

#define ANALYZE_CHANNEL(name, ptype, zero)                                                 \
static void analyze_channel_## name (AVFilterContext *ctx, ChannelContext *cc,             \
                                     const uint8_t *srcp, int nb_samples)                  \
{                                                                                          \
    SpeechNormalizerContext *s = ctx->priv;                                                \
    const ptype *src = (const ptype *)srcp;                                                \
    int n = 0;                                                                             \
                                                                                           \
    if (cc->state < 0)                                                                     \
        cc->state = src[0] >= zero;                                                        \
                                                                                           \
    while (n < nb_samples) {                                                               \
        if ((cc->state != (src[n] >= zero)) ||                                             \
            (cc->pi[cc->pi_end].size > s->max_period)) {                                   \
            double max_peak = cc->pi[cc->pi_end].max_peak;                                 \
            int state = cc->state;                                                         \
            cc->state = src[n] >= zero;                                                    \
            av_assert0(cc->pi[cc->pi_end].size > 0);                                       \
            if (cc->pi[cc->pi_end].max_peak >= MIN_PEAK ||                                 \
                cc->pi[cc->pi_end].size > s->max_period) {                                 \
                cc->pi[cc->pi_end].type = 1;                                               \
                cc->pi_end++;                                                              \
                if (cc->pi_end >= MAX_ITEMS)                                               \
                    cc->pi_end = 0;                                                        \
                if (cc->state != state)                                                    \
                    cc->pi[cc->pi_end].max_peak = DBL_MIN;                                 \
                else                                                                       \
                    cc->pi[cc->pi_end].max_peak = max_peak;                                \
                cc->pi[cc->pi_end].type = 0;                                               \
                cc->pi[cc->pi_end].size = 0;                                               \
                av_assert0(cc->pi_end != cc->pi_start);                                    \
            }                                                                              \
        }                                                                                  \
                                                                                           \
        if (cc->state) {                                                                   \
            while (src[n] >= zero) {                                                       \
                cc->pi[cc->pi_end].max_peak = FFMAX(cc->pi[cc->pi_end].max_peak,  src[n]); \
                cc->pi[cc->pi_end].size++;                                                 \
                n++;                                                                       \
                if (n >= nb_samples)                                                       \
                    break;                                                                 \
            }                                                                              \
        } else {                                                                           \
            while (src[n] < zero) {                                                        \
                cc->pi[cc->pi_end].max_peak = FFMAX(cc->pi[cc->pi_end].max_peak, -src[n]); \
                cc->pi[cc->pi_end].size++;                                                 \
                n++;                                                                       \
                if (n >= nb_samples)                                                       \
                    break;                                                                 \
            }                                                                              \
        }                                                                                  \
    }                                                                                      \
}

ANALYZE_CHANNEL(dbl, double, 0.0)
ANALYZE_CHANNEL(flt, float,  0.f)

#define FILTER_CHANNELS(name, ptype)                                            \
static void filter_channels_## name (AVFilterContext *ctx,                      \
                                     AVFrame *in, int nb_samples)               \
{                                                                               \
    SpeechNormalizerContext *s = ctx->priv;                                     \
    AVFilterLink *inlink = ctx->inputs[0];                                      \
                                                                                \
    for (int ch = 0; ch < inlink->channels; ch++) {                             \
        ChannelContext *cc = &s->cc[ch];                                        \
        ptype *dst = (ptype *)in->extended_data[ch];                            \
        const int bypass = !(av_channel_layout_extract_channel(inlink->channel_layout, ch) & s->channels); \
        int n = 0;                                                              \
                                                                                \
        while (n < nb_samples) {                                                \
            ptype gain;                                                         \
            int size;                                                           \
                                                                                \
            next_pi(ctx, cc, bypass);                                           \
            size = FFMIN(nb_samples - n, cc->pi_size);                          \
            av_assert0(size > 0);                                               \
            gain = cc->gain_state;                                              \
            consume_pi(cc, size);                                               \
            for (int i = n; i < n + size; i++)                                  \
                dst[i] *= gain;                                                 \
            n += size;                                                          \
        }                                                                       \
    }                                                                           \
}

FILTER_CHANNELS(dbl, double)
FILTER_CHANNELS(flt, float)

static double lerp(double min, double max, double mix)
{
    return min + (max - min) * mix;
}

#define FILTER_LINK_CHANNELS(name, ptype)                                       \
static void filter_link_channels_## name (AVFilterContext *ctx,                 \
                                          AVFrame *in, int nb_samples)          \
{                                                                               \
    SpeechNormalizerContext *s = ctx->priv;                                     \
    AVFilterLink *inlink = ctx->inputs[0];                                      \
    int n = 0;                                                                  \
                                                                                \
    while (n < nb_samples) {                                                    \
        int min_size = nb_samples - n;                                          \
        int max_size = 1;                                                       \
        ptype gain = s->max_expansion;                                          \
                                                                                \
        for (int ch = 0; ch < inlink->channels; ch++) {                         \
            ChannelContext *cc = &s->cc[ch];                                    \
                                                                                \
            cc->bypass = !(av_channel_layout_extract_channel(inlink->channel_layout, ch) & s->channels); \
                                                                                \
            next_pi(ctx, cc, cc->bypass);                                       \
            min_size = FFMIN(min_size, cc->pi_size);                            \
            max_size = FFMAX(max_size, cc->pi_size);                            \
        }                                                                       \
                                                                                \
        av_assert0(min_size > 0);                                               \
        for (int ch = 0; ch < inlink->channels; ch++) {                         \
            ChannelContext *cc = &s->cc[ch];                                    \
                                                                                \
            if (cc->bypass)                                                     \
                continue;                                                       \
            gain = FFMIN(gain, min_gain(ctx, cc, max_size));                    \
        }                                                                       \
                                                                                \
        for (int ch = 0; ch < inlink->channels; ch++) {                         \
            ChannelContext *cc = &s->cc[ch];                                    \
            ptype *dst = (ptype *)in->extended_data[ch];                        \
                                                                                \
            consume_pi(cc, min_size);                                           \
            if (cc->bypass)                                                     \
                continue;                                                       \
                                                                                \
            for (int i = n; i < n + min_size; i++) {                            \
                ptype g = lerp(s->prev_gain, gain, (i - n) / (double)min_size); \
                dst[i] *= g;                                                    \
            }                                                                   \
        }                                                                       \
                                                                                \
        s->prev_gain = gain;                                                    \
        n += min_size;                                                          \
    }                                                                           \
}

FILTER_LINK_CHANNELS(dbl, double)
FILTER_LINK_CHANNELS(flt, float)

static int filter_frame(AVFilterContext *ctx)
{
    SpeechNormalizerContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFilterLink *inlink = ctx->inputs[0];
    int ret;

    while (s->queue.available > 0) {
        int min_pi_nb_samples;
        AVFrame *in;

        in = ff_bufqueue_peek(&s->queue, 0);
        if (!in)
            break;

        min_pi_nb_samples = available_samples(ctx);
        if (min_pi_nb_samples < in->nb_samples && !s->eof)
            break;

        in = ff_bufqueue_get(&s->queue);

        av_frame_make_writable(in);

        s->filter_channels[s->link](ctx, in, in->nb_samples);

        s->pts = in->pts + in->nb_samples;

        return ff_filter_frame(outlink, in);
    }

    for (int f = 0; f < ff_inlink_queued_frames(inlink); f++) {
        AVFrame *in;

        ret = ff_inlink_consume_frame(inlink, &in);
        if (ret < 0)
            return ret;
        if (ret == 0)
            break;

        ff_bufqueue_add(ctx, &s->queue, in);

        for (int ch = 0; ch < inlink->channels; ch++) {
            ChannelContext *cc = &s->cc[ch];

            s->analyze_channel(ctx, cc, in->extended_data[ch], in->nb_samples);
        }
    }

    return 1;
}

static int activate(AVFilterContext *ctx)
{
    AVFilterLink *inlink = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];
    SpeechNormalizerContext *s = ctx->priv;
    int ret, status;
    int64_t pts;

    FF_FILTER_FORWARD_STATUS_BACK(outlink, inlink);

    ret = filter_frame(ctx);
    if (ret <= 0)
        return ret;

    if (!s->eof && ff_inlink_acknowledge_status(inlink, &status, &pts)) {
        if (status == AVERROR_EOF)
            s->eof = 1;
    }

    if (s->eof && ff_inlink_queued_samples(inlink) == 0 &&
        s->queue.available == 0) {
        ff_outlink_set_status(outlink, AVERROR_EOF, s->pts);
        return 0;
    }

    if (s->queue.available > 0) {
        AVFrame *in = ff_bufqueue_peek(&s->queue, 0);
        const int nb_samples = available_samples(ctx);

        if (nb_samples >= in->nb_samples || s->eof) {
            ff_filter_set_ready(ctx, 10);
            return 0;
        }
    }

    FF_FILTER_FORWARD_WANTED(outlink, inlink);

    return FFERROR_NOT_READY;
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    SpeechNormalizerContext *s = ctx->priv;

    s->max_period = inlink->sample_rate / 10;

    s->prev_gain = 1.;
    s->cc = av_calloc(inlink->channels, sizeof(*s->cc));
    if (!s->cc)
        return AVERROR(ENOMEM);

    for (int ch = 0; ch < inlink->channels; ch++) {
        ChannelContext *cc = &s->cc[ch];

        cc->state = -1;
        cc->gain_state = 1.;
    }

    switch (inlink->format) {
    case AV_SAMPLE_FMT_FLTP:
        s->analyze_channel = analyze_channel_flt;
        s->filter_channels[0] = filter_channels_flt;
        s->filter_channels[1] = filter_link_channels_flt;
        break;
    case AV_SAMPLE_FMT_DBLP:
        s->analyze_channel = analyze_channel_dbl;
        s->filter_channels[0] = filter_channels_dbl;
        s->filter_channels[1] = filter_link_channels_dbl;
        break;
    default:
        av_assert0(0);
    }

    return 0;
}

static int process_command(AVFilterContext *ctx, const char *cmd, const char *args,
                           char *res, int res_len, int flags)
{
    SpeechNormalizerContext *s = ctx->priv;
    int link = s->link;
    int ret;

    ret = ff_filter_process_command(ctx, cmd, args, res, res_len, flags);
    if (ret < 0)
        return ret;
    if (link != s->link)
        s->prev_gain = 1.;

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    SpeechNormalizerContext *s = ctx->priv;

    ff_bufqueue_discard_all(&s->queue);
    av_freep(&s->cc);
}

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .config_props = config_input,
    },
    { NULL }
};

static const AVFilterPad outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_AUDIO,
    },
    { NULL }
};

AVFilter ff_af_speechnorm = {
    .name            = "speechnorm",
    .description     = NULL_IF_CONFIG_SMALL("Speech Normalizer."),
    .query_formats   = query_formats,
    .priv_size       = sizeof(SpeechNormalizerContext),
    .priv_class      = &speechnorm_class,
    .activate        = activate,
    .uninit          = uninit,
    .inputs          = inputs,
    .outputs         = outputs,
    .process_command = process_command,
};
