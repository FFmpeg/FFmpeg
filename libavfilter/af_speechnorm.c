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
#include "libavutil/channel_layout.h"
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
    double rms_sum;
} PeriodItem;

typedef struct ChannelContext {
    int state;
    int bypass;
    PeriodItem pi[MAX_ITEMS];
    double gain_state;
    double pi_max_peak;
    double pi_rms_sum;
    int pi_start;
    int pi_end;
    int pi_size;
} ChannelContext;

typedef struct SpeechNormalizerContext {
    const AVClass *class;

    double rms_value;
    double peak_value;
    double max_expansion;
    double max_compression;
    double threshold_value;
    double raise_amount;
    double fall_amount;
    char *ch_layout_str;
    AVChannelLayout ch_layout;
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
                               AVFrame *in, AVFrame *out, int nb_samples);
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
    { "channels", "set channels to filter", OFFSET(ch_layout_str), AV_OPT_TYPE_STRING, {.str="all"}, 0, 0, FLAGS },
    { "h",        "set channels to filter", OFFSET(ch_layout_str), AV_OPT_TYPE_STRING, {.str="all"}, 0, 0, FLAGS },
    { "invert", "set inverted filtering", OFFSET(invert), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, FLAGS },
    { "i",      "set inverted filtering", OFFSET(invert), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, FLAGS },
    { "link", "set linked channels filtering", OFFSET(link), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, FLAGS },
    { "l",    "set linked channels filtering", OFFSET(link), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, FLAGS },
    { "rms", "set the RMS value", OFFSET(rms_value), AV_OPT_TYPE_DOUBLE, {.dbl=0.0}, 0.0, 1.0, FLAGS },
    { "m",   "set the RMS value", OFFSET(rms_value), AV_OPT_TYPE_DOUBLE, {.dbl=0.0}, 0.0, 1.0, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(speechnorm);

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
        av_assert1(pi[start].size > 0);
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
    for (int ch = 1; ch < inlink->ch_layout.nb_channels && min_pi_nb_samples > 0; ch++) {
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
        av_assert1(0);
    }
}

static double next_gain(AVFilterContext *ctx, double pi_max_peak, int bypass, double state,
                        double pi_rms_sum, int pi_size)
{
    SpeechNormalizerContext *s = ctx->priv;
    const double compression = 1. / s->max_compression;
    const int type = s->invert ? pi_max_peak <= s->threshold_value : pi_max_peak >= s->threshold_value;
    double expansion = FFMIN(s->max_expansion, s->peak_value / pi_max_peak);

    if (s->rms_value > DBL_EPSILON)
        expansion = FFMIN(expansion, s->rms_value / sqrt(pi_rms_sum / pi_size));

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
    av_assert1(cc->pi_size >= 0);
    if (cc->pi_size == 0) {
        SpeechNormalizerContext *s = ctx->priv;
        int start = cc->pi_start;

        av_assert1(cc->pi[start].size > 0);
        av_assert0(cc->pi[start].type > 0 || s->eof);
        cc->pi_size = cc->pi[start].size;
        cc->pi_rms_sum = cc->pi[start].rms_sum;
        cc->pi_max_peak = cc->pi[start].max_peak;
        av_assert1(cc->pi_start != cc->pi_end || s->eof);
        start++;
        if (start >= MAX_ITEMS)
            start = 0;
        cc->pi_start = start;
        cc->gain_state = next_gain(ctx, cc->pi_max_peak, bypass, cc->gain_state,
                                   cc->pi_rms_sum, cc->pi_size);
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
        gain_state = next_gain(ctx, cc->pi[idx].max_peak, 0, gain_state,
                               cc->pi[idx].rms_sum, cc->pi[idx].size);
        min_gain = FFMIN(min_gain, gain_state);
        size += cc->pi[idx].size;
        idx++;
        if (idx >= MAX_ITEMS)
            idx = 0;
    }

    return min_gain;
}

#define ANALYZE_CHANNEL(name, ptype, zero, min_peak)                            \
static void analyze_channel_## name (AVFilterContext *ctx, ChannelContext *cc,  \
                                     const uint8_t *srcp, int nb_samples)       \
{                                                                               \
    SpeechNormalizerContext *s = ctx->priv;                                     \
    const ptype *src = (const ptype *)srcp;                                     \
    const int max_period = s->max_period;                                       \
    PeriodItem *pi = (PeriodItem *)&cc->pi;                                     \
    int pi_end = cc->pi_end;                                                    \
    int n = 0;                                                                  \
                                                                                \
    if (cc->state < 0)                                                          \
        cc->state = src[0] >= zero;                                             \
                                                                                \
    while (n < nb_samples) {                                                    \
        ptype new_max_peak;                                                     \
        ptype new_rms_sum;                                                      \
        int new_size;                                                           \
                                                                                \
        if ((cc->state != (src[n] >= zero)) ||                                  \
            (pi[pi_end].size > max_period)) {                                   \
            ptype max_peak = pi[pi_end].max_peak;                               \
            ptype rms_sum = pi[pi_end].rms_sum;                                 \
            int state = cc->state;                                              \
                                                                                \
            cc->state = src[n] >= zero;                                         \
            av_assert1(pi[pi_end].size > 0);                                    \
            if (max_peak >= min_peak ||                                         \
                pi[pi_end].size > max_period) {                                 \
                pi[pi_end].type = 1;                                            \
                pi_end++;                                                       \
                if (pi_end >= MAX_ITEMS)                                        \
                    pi_end = 0;                                                 \
                if (cc->state != state) {                                       \
                    pi[pi_end].max_peak = DBL_MIN;                              \
                    pi[pi_end].rms_sum = 0.0;                                   \
                } else {                                                        \
                    pi[pi_end].max_peak = max_peak;                             \
                    pi[pi_end].rms_sum = rms_sum;                               \
                }                                                               \
                pi[pi_end].type = 0;                                            \
                pi[pi_end].size = 0;                                            \
                av_assert1(pi_end != cc->pi_start);                             \
            }                                                                   \
        }                                                                       \
                                                                                \
        new_max_peak = pi[pi_end].max_peak;                                     \
        new_rms_sum = pi[pi_end].rms_sum;                                       \
        new_size = pi[pi_end].size;                                             \
        if (cc->state) {                                                        \
            while (src[n] >= zero) {                                            \
                new_max_peak = FFMAX(new_max_peak,  src[n]);                    \
                new_rms_sum += src[n] * src[n];                                 \
                new_size++;                                                     \
                n++;                                                            \
                if (n >= nb_samples)                                            \
                    break;                                                      \
            }                                                                   \
        } else {                                                                \
            while (src[n] < zero) {                                             \
                new_max_peak = FFMAX(new_max_peak, -src[n]);                    \
                new_rms_sum += src[n] * src[n];                                 \
                new_size++;                                                     \
                n++;                                                            \
                if (n >= nb_samples)                                            \
                    break;                                                      \
            }                                                                   \
        }                                                                       \
                                                                                \
        pi[pi_end].max_peak = new_max_peak;                                     \
        pi[pi_end].rms_sum = new_rms_sum;                                       \
        pi[pi_end].size = new_size;                                             \
    }                                                                           \
    cc->pi_end = pi_end;                                                        \
}

ANALYZE_CHANNEL(dbl, double, 0.0, MIN_PEAK)
ANALYZE_CHANNEL(flt, float,  0.f, (float)MIN_PEAK)

#define FILTER_CHANNELS(name, ptype)                                            \
static void filter_channels_## name (AVFilterContext *ctx,                      \
                                     AVFrame *in, AVFrame *out, int nb_samples) \
{                                                                               \
    SpeechNormalizerContext *s = ctx->priv;                                     \
    AVFilterLink *inlink = ctx->inputs[0];                                      \
                                                                                \
    for (int ch = 0; ch < inlink->ch_layout.nb_channels; ch++) {                \
        ChannelContext *cc = &s->cc[ch];                                        \
        const ptype *src = (const ptype *)in->extended_data[ch];                \
        ptype *dst = (ptype *)out->extended_data[ch];                           \
        enum AVChannel channel = av_channel_layout_channel_from_index(&inlink->ch_layout, ch); \
        const int bypass = av_channel_layout_index_from_channel(&s->ch_layout, channel) < 0; \
        int n = 0;                                                              \
                                                                                \
        while (n < nb_samples) {                                                \
            ptype gain;                                                         \
            int size;                                                           \
                                                                                \
            next_pi(ctx, cc, bypass);                                           \
            size = FFMIN(nb_samples - n, cc->pi_size);                          \
            av_assert1(size > 0);                                               \
            gain = cc->gain_state;                                              \
            consume_pi(cc, size);                                               \
            for (int i = n; !ctx->is_disabled && i < n + size; i++)             \
                dst[i] = src[i] * gain;                                         \
            n += size;                                                          \
        }                                                                       \
    }                                                                           \
}

FILTER_CHANNELS(dbl, double)
FILTER_CHANNELS(flt, float)

static double dlerp(double min, double max, double mix)
{
    return min + (max - min) * mix;
}

static float flerp(float min, float max, float mix)
{
    return min + (max - min) * mix;
}

#define FILTER_LINK_CHANNELS(name, ptype, tlerp)                                \
static void filter_link_channels_## name (AVFilterContext *ctx,                 \
                                          AVFrame *in, AVFrame *out,            \
                                          int nb_samples)                       \
{                                                                               \
    SpeechNormalizerContext *s = ctx->priv;                                     \
    AVFilterLink *inlink = ctx->inputs[0];                                      \
    int n = 0;                                                                  \
                                                                                \
    while (n < nb_samples) {                                                    \
        int min_size = nb_samples - n;                                          \
        ptype gain = s->max_expansion;                                          \
                                                                                \
        for (int ch = 0; ch < inlink->ch_layout.nb_channels; ch++) {            \
            ChannelContext *cc = &s->cc[ch];                                    \
                                                                                \
            enum AVChannel channel = av_channel_layout_channel_from_index(&inlink->ch_layout, ch); \
            cc->bypass = av_channel_layout_index_from_channel(&s->ch_layout, channel) < 0; \
                                                                                \
            next_pi(ctx, cc, cc->bypass);                                       \
            min_size = FFMIN(min_size, cc->pi_size);                            \
        }                                                                       \
                                                                                \
        av_assert1(min_size > 0);                                               \
        for (int ch = 0; ch < inlink->ch_layout.nb_channels; ch++) {            \
            ChannelContext *cc = &s->cc[ch];                                    \
                                                                                \
            if (cc->bypass)                                                     \
                continue;                                                       \
            gain = FFMIN(gain, min_gain(ctx, cc, min_size));                    \
        }                                                                       \
                                                                                \
        for (int ch = 0; ch < inlink->ch_layout.nb_channels; ch++) {            \
            ChannelContext *cc = &s->cc[ch];                                    \
            const ptype *src = (const ptype *)in->extended_data[ch];            \
            ptype *dst = (ptype *)out->extended_data[ch];                       \
                                                                                \
            consume_pi(cc, min_size);                                           \
            if (cc->bypass)                                                     \
                continue;                                                       \
                                                                                \
            for (int i = n; !ctx->is_disabled && i < n + min_size; i++) {       \
                ptype g = tlerp(s->prev_gain, gain, (i - n) / (ptype)min_size); \
                dst[i] = src[i] * g;                                            \
            }                                                                   \
        }                                                                       \
                                                                                \
        s->prev_gain = gain;                                                    \
        n += min_size;                                                          \
    }                                                                           \
}

FILTER_LINK_CHANNELS(dbl, double, dlerp)
FILTER_LINK_CHANNELS(flt, float, flerp)

static int filter_frame(AVFilterContext *ctx)
{
    SpeechNormalizerContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFilterLink *inlink = ctx->inputs[0];
    int ret;

    while (s->queue.available > 0) {
        int min_pi_nb_samples;
        AVFrame *in, *out;

        in = ff_bufqueue_peek(&s->queue, 0);
        if (!in)
            break;

        min_pi_nb_samples = available_samples(ctx);
        if (min_pi_nb_samples < in->nb_samples && !s->eof)
            break;

        in = ff_bufqueue_get(&s->queue);

        if (av_frame_is_writable(in)) {
            out = in;
        } else {
            out = ff_get_audio_buffer(outlink, in->nb_samples);
            if (!out) {
                av_frame_free(&in);
                return AVERROR(ENOMEM);
            }
            av_frame_copy_props(out, in);
        }

        s->filter_channels[s->link](ctx, in, out, in->nb_samples);

        s->pts = in->pts + av_rescale_q(in->nb_samples, av_make_q(1, outlink->sample_rate),
                                        outlink->time_base);

        if (out != in)
            av_frame_free(&in);
        return ff_filter_frame(outlink, out);
    }

    for (int f = 0; f < ff_inlink_queued_frames(inlink); f++) {
        AVFrame *in;

        ret = ff_inlink_consume_frame(inlink, &in);
        if (ret < 0)
            return ret;
        if (ret == 0)
            break;

        ff_bufqueue_add(ctx, &s->queue, in);

        for (int ch = 0; ch < inlink->ch_layout.nb_channels; ch++) {
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

    ret = av_channel_layout_copy(&s->ch_layout, &inlink->ch_layout);
    if (ret < 0)
        return ret;
    if (strcmp(s->ch_layout_str, "all"))
        av_channel_layout_from_string(&s->ch_layout,
                                      s->ch_layout_str);

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
    s->cc = av_calloc(inlink->ch_layout.nb_channels, sizeof(*s->cc));
    if (!s->cc)
        return AVERROR(ENOMEM);

    for (int ch = 0; ch < inlink->ch_layout.nb_channels; ch++) {
        ChannelContext *cc = &s->cc[ch];

        cc->state = -1;
        cc->gain_state = s->max_expansion;
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
        av_assert1(0);
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
    av_channel_layout_uninit(&s->ch_layout);
    av_freep(&s->cc);
}

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .config_props = config_input,
    },
};

static const AVFilterPad outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_AUDIO,
    },
};

const AVFilter ff_af_speechnorm = {
    .name            = "speechnorm",
    .description     = NULL_IF_CONFIG_SMALL("Speech Normalizer."),
    .priv_size       = sizeof(SpeechNormalizerContext),
    .priv_class      = &speechnorm_class,
    .activate        = activate,
    .uninit          = uninit,
    FILTER_INPUTS(inputs),
    FILTER_OUTPUTS(outputs),
    FILTER_SAMPLEFMTS(AV_SAMPLE_FMT_FLTP, AV_SAMPLE_FMT_DBLP),
    .flags           = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL,
    .process_command = process_command,
};
