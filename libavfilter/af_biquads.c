/*
 * Copyright (c) 2013 Paul B Mahol
 * Copyright (c) 2006-2008 Rob Sykes <robs@users.sourceforge.net>
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

/*
 * 2-pole filters designed by Robert Bristow-Johnson <rbj@audioimagination.com>
 *   see http://www.musicdsp.org/files/Audio-EQ-Cookbook.txt
 *
 * 1-pole filters based on code (c) 2000 Chris Bagwell <cbagwell@sprynet.com>
 *   Algorithms: Recursive single pole low/high pass filter
 *   Reference: The Scientist and Engineer's Guide to Digital Signal Processing
 *
 *   low-pass: output[N] = input[N] * A + output[N-1] * B
 *     X = exp(-2.0 * pi * Fc)
 *     A = 1 - X
 *     B = X
 *     Fc = cutoff freq / sample rate
 *
 *     Mimics an RC low-pass filter:
 *
 *     ---/\/\/\/\----------->
 *                   |
 *                  --- C
 *                  ---
 *                   |
 *                   |
 *                   V
 *
 *   high-pass: output[N] = A0 * input[N] + A1 * input[N-1] + B1 * output[N-1]
 *     X  = exp(-2.0 * pi * Fc)
 *     A0 = (1 + X) / 2
 *     A1 = -(1 + X) / 2
 *     B1 = X
 *     Fc = cutoff freq / sample rate
 *
 *     Mimics an RC high-pass filter:
 *
 *         || C
 *     ----||--------->
 *         ||    |
 *               <
 *               > R
 *               <
 *               |
 *               V
 */

#include "libavutil/avassert.h"
#include "libavutil/ffmath.h"
#include "libavutil/opt.h"
#include "audio.h"
#include "avfilter.h"
#include "internal.h"

enum FilterType {
    biquad,
    equalizer,
    bass,
    treble,
    bandpass,
    bandreject,
    allpass,
    highpass,
    lowpass,
    lowshelf,
    highshelf,
};

enum WidthType {
    NONE,
    HERTZ,
    OCTAVE,
    QFACTOR,
    SLOPE,
    KHERTZ,
    NB_WTYPE,
};

typedef struct ChanCache {
    double i1, i2;
    double o1, o2;
    int clippings;
} ChanCache;

typedef struct BiquadsContext {
    const AVClass *class;

    enum FilterType filter_type;
    int width_type;
    int poles;
    int csg;

    double gain;
    double frequency;
    double width;
    double mix;
    uint64_t channels;

    double a0, a1, a2;
    double b0, b1, b2;

    ChanCache *cache;
    int block_align;

    void (*filter)(struct BiquadsContext *s, const void *ibuf, void *obuf, int len,
                   double *i1, double *i2, double *o1, double *o2,
                   double b0, double b1, double b2, double a1, double a2, int *clippings,
                   int disabled);
} BiquadsContext;

static av_cold int init(AVFilterContext *ctx)
{
    BiquadsContext *s = ctx->priv;

    if (s->filter_type != biquad) {
        if (s->frequency <= 0 || s->width <= 0) {
            av_log(ctx, AV_LOG_ERROR, "Invalid frequency %f and/or width %f <= 0\n",
                   s->frequency, s->width);
            return AVERROR(EINVAL);
        }
    }

    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *formats;
    AVFilterChannelLayouts *layouts;
    static const enum AVSampleFormat sample_fmts[] = {
        AV_SAMPLE_FMT_S16P,
        AV_SAMPLE_FMT_S32P,
        AV_SAMPLE_FMT_FLTP,
        AV_SAMPLE_FMT_DBLP,
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

#define BIQUAD_FILTER(name, type, min, max, need_clipping)                    \
static void biquad_## name (BiquadsContext *s,                                \
                            const void *input, void *output, int len,         \
                            double *in1, double *in2,                         \
                            double *out1, double *out2,                       \
                            double b0, double b1, double b2,                  \
                            double a1, double a2, int *clippings,             \
                            int disabled)                                     \
{                                                                             \
    const type *ibuf = input;                                                 \
    type *obuf = output;                                                      \
    double i1 = *in1;                                                         \
    double i2 = *in2;                                                         \
    double o1 = *out1;                                                        \
    double o2 = *out2;                                                        \
    double wet = s->mix;                                                      \
    double dry = 1. - wet;                                                    \
    double out;                                                               \
    int i;                                                                    \
    a1 = -a1;                                                                 \
    a2 = -a2;                                                                 \
                                                                              \
    for (i = 0; i+1 < len; i++) {                                             \
        o2 = i2 * b2 + i1 * b1 + ibuf[i] * b0 + o2 * a2 + o1 * a1;            \
        i2 = ibuf[i];                                                         \
        out = o2 * wet + i2 * dry;                                            \
        if (disabled) {                                                       \
            obuf[i] = i2;                                                     \
        } else if (need_clipping && out < min) {                              \
            (*clippings)++;                                                   \
            obuf[i] = min;                                                    \
        } else if (need_clipping && out > max) {                              \
            (*clippings)++;                                                   \
            obuf[i] = max;                                                    \
        } else {                                                              \
            obuf[i] = out;                                                    \
        }                                                                     \
        i++;                                                                  \
        o1 = i1 * b2 + i2 * b1 + ibuf[i] * b0 + o1 * a2 + o2 * a1;            \
        i1 = ibuf[i];                                                         \
        out = o1 * wet + i1 * dry;                                            \
        if (disabled) {                                                       \
            obuf[i] = i1;                                                     \
        } else if (need_clipping && out < min) {                              \
            (*clippings)++;                                                   \
            obuf[i] = min;                                                    \
        } else if (need_clipping && out > max) {                              \
            (*clippings)++;                                                   \
            obuf[i] = max;                                                    \
        } else {                                                              \
            obuf[i] = out;                                                    \
        }                                                                     \
    }                                                                         \
    if (i < len) {                                                            \
        double o0 = ibuf[i] * b0 + i1 * b1 + i2 * b2 + o1 * a1 + o2 * a2;     \
        i2 = i1;                                                              \
        i1 = ibuf[i];                                                         \
        o2 = o1;                                                              \
        o1 = o0;                                                              \
        out = o0 * wet + i1 * dry;                                            \
        if (disabled) {                                                       \
            obuf[i] = i1;                                                     \
        } else if (need_clipping && out < min) {                              \
            (*clippings)++;                                                   \
            obuf[i] = min;                                                    \
        } else if (need_clipping && out > max) {                              \
            (*clippings)++;                                                   \
            obuf[i] = max;                                                    \
        } else {                                                              \
            obuf[i] = out;                                                    \
        }                                                                     \
    }                                                                         \
    *in1  = i1;                                                               \
    *in2  = i2;                                                               \
    *out1 = o1;                                                               \
    *out2 = o2;                                                               \
}

BIQUAD_FILTER(s16, int16_t, INT16_MIN, INT16_MAX, 1)
BIQUAD_FILTER(s32, int32_t, INT32_MIN, INT32_MAX, 1)
BIQUAD_FILTER(flt, float,   -1., 1., 0)
BIQUAD_FILTER(dbl, double,  -1., 1., 0)

static int config_filter(AVFilterLink *outlink, int reset)
{
    AVFilterContext *ctx    = outlink->src;
    BiquadsContext *s       = ctx->priv;
    AVFilterLink *inlink    = ctx->inputs[0];
    double A = ff_exp10(s->gain / 40);
    double w0 = 2 * M_PI * s->frequency / inlink->sample_rate;
    double alpha, beta;

    if (w0 > M_PI) {
        av_log(ctx, AV_LOG_ERROR,
               "Invalid frequency %f. Frequency must be less than half the sample-rate %d.\n",
               s->frequency, inlink->sample_rate);
        return AVERROR(EINVAL);
    }

    switch (s->width_type) {
    case NONE:
        alpha = 0.0;
        break;
    case HERTZ:
        alpha = sin(w0) / (2 * s->frequency / s->width);
        break;
    case KHERTZ:
        alpha = sin(w0) / (2 * s->frequency / (s->width * 1000));
        break;
    case OCTAVE:
        alpha = sin(w0) * sinh(log(2.) / 2 * s->width * w0 / sin(w0));
        break;
    case QFACTOR:
        alpha = sin(w0) / (2 * s->width);
        break;
    case SLOPE:
        alpha = sin(w0) / 2 * sqrt((A + 1 / A) * (1 / s->width - 1) + 2);
        break;
    default:
        av_assert0(0);
    }

    beta = 2 * sqrt(A);

    switch (s->filter_type) {
    case biquad:
        break;
    case equalizer:
        s->a0 =   1 + alpha / A;
        s->a1 =  -2 * cos(w0);
        s->a2 =   1 - alpha / A;
        s->b0 =   1 + alpha * A;
        s->b1 =  -2 * cos(w0);
        s->b2 =   1 - alpha * A;
        break;
    case bass:
        beta = sqrt((A * A + 1) - (A - 1) * (A - 1));
    case lowshelf:
        s->a0 =          (A + 1) + (A - 1) * cos(w0) + beta * alpha;
        s->a1 =    -2 * ((A - 1) + (A + 1) * cos(w0));
        s->a2 =          (A + 1) + (A - 1) * cos(w0) - beta * alpha;
        s->b0 =     A * ((A + 1) - (A - 1) * cos(w0) + beta * alpha);
        s->b1 = 2 * A * ((A - 1) - (A + 1) * cos(w0));
        s->b2 =     A * ((A + 1) - (A - 1) * cos(w0) - beta * alpha);
        break;
    case treble:
        beta = sqrt((A * A + 1) - (A - 1) * (A - 1));
    case highshelf:
        s->a0 =          (A + 1) - (A - 1) * cos(w0) + beta * alpha;
        s->a1 =     2 * ((A - 1) - (A + 1) * cos(w0));
        s->a2 =          (A + 1) - (A - 1) * cos(w0) - beta * alpha;
        s->b0 =     A * ((A + 1) + (A - 1) * cos(w0) + beta * alpha);
        s->b1 =-2 * A * ((A - 1) + (A + 1) * cos(w0));
        s->b2 =     A * ((A + 1) + (A - 1) * cos(w0) - beta * alpha);
        break;
    case bandpass:
        if (s->csg) {
            s->a0 =  1 + alpha;
            s->a1 = -2 * cos(w0);
            s->a2 =  1 - alpha;
            s->b0 =  sin(w0) / 2;
            s->b1 =  0;
            s->b2 = -sin(w0) / 2;
        } else {
            s->a0 =  1 + alpha;
            s->a1 = -2 * cos(w0);
            s->a2 =  1 - alpha;
            s->b0 =  alpha;
            s->b1 =  0;
            s->b2 = -alpha;
        }
        break;
    case bandreject:
        s->a0 =  1 + alpha;
        s->a1 = -2 * cos(w0);
        s->a2 =  1 - alpha;
        s->b0 =  1;
        s->b1 = -2 * cos(w0);
        s->b2 =  1;
        break;
    case lowpass:
        if (s->poles == 1) {
            s->a0 = 1;
            s->a1 = -exp(-w0);
            s->a2 = 0;
            s->b0 = 1 + s->a1;
            s->b1 = 0;
            s->b2 = 0;
        } else {
            s->a0 =  1 + alpha;
            s->a1 = -2 * cos(w0);
            s->a2 =  1 - alpha;
            s->b0 = (1 - cos(w0)) / 2;
            s->b1 =  1 - cos(w0);
            s->b2 = (1 - cos(w0)) / 2;
        }
        break;
    case highpass:
        if (s->poles == 1) {
            s->a0 = 1;
            s->a1 = -exp(-w0);
            s->a2 = 0;
            s->b0 = (1 - s->a1) / 2;
            s->b1 = -s->b0;
            s->b2 = 0;
        } else {
            s->a0 =   1 + alpha;
            s->a1 =  -2 * cos(w0);
            s->a2 =   1 - alpha;
            s->b0 =  (1 + cos(w0)) / 2;
            s->b1 = -(1 + cos(w0));
            s->b2 =  (1 + cos(w0)) / 2;
        }
        break;
    case allpass:
        s->a0 =  1 + alpha;
        s->a1 = -2 * cos(w0);
        s->a2 =  1 - alpha;
        s->b0 =  1 - alpha;
        s->b1 = -2 * cos(w0);
        s->b2 =  1 + alpha;
        break;
    default:
        av_assert0(0);
    }

    av_log(ctx, AV_LOG_VERBOSE, "a=%f %f %f:b=%f %f %f\n", s->a0, s->a1, s->a2, s->b0, s->b1, s->b2);

    s->a1 /= s->a0;
    s->a2 /= s->a0;
    s->b0 /= s->a0;
    s->b1 /= s->a0;
    s->b2 /= s->a0;
    s->a0 /= s->a0;

    s->cache = av_realloc_f(s->cache, sizeof(ChanCache), inlink->channels);
    if (!s->cache)
        return AVERROR(ENOMEM);
    if (reset)
        memset(s->cache, 0, sizeof(ChanCache) * inlink->channels);

    switch (inlink->format) {
    case AV_SAMPLE_FMT_S16P: s->filter = biquad_s16; break;
    case AV_SAMPLE_FMT_S32P: s->filter = biquad_s32; break;
    case AV_SAMPLE_FMT_FLTP: s->filter = biquad_flt; break;
    case AV_SAMPLE_FMT_DBLP: s->filter = biquad_dbl; break;
    default: av_assert0(0);
    }

    s->block_align = av_get_bytes_per_sample(inlink->format);

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    return config_filter(outlink, 1);
}

typedef struct ThreadData {
    AVFrame *in, *out;
} ThreadData;

static int filter_channel(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    AVFilterLink *inlink = ctx->inputs[0];
    ThreadData *td = arg;
    AVFrame *buf = td->in;
    AVFrame *out_buf = td->out;
    BiquadsContext *s = ctx->priv;
    const int start = (buf->channels * jobnr) / nb_jobs;
    const int end = (buf->channels * (jobnr+1)) / nb_jobs;
    int ch;

    for (ch = start; ch < end; ch++) {
        if (!((av_channel_layout_extract_channel(inlink->channel_layout, ch) & s->channels))) {
            if (buf != out_buf)
                memcpy(out_buf->extended_data[ch], buf->extended_data[ch],
                       buf->nb_samples * s->block_align);
            continue;
        }

        s->filter(s, buf->extended_data[ch], out_buf->extended_data[ch], buf->nb_samples,
                  &s->cache[ch].i1, &s->cache[ch].i2, &s->cache[ch].o1, &s->cache[ch].o2,
                  s->b0, s->b1, s->b2, s->a1, s->a2, &s->cache[ch].clippings, ctx->is_disabled);
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *buf)
{
    AVFilterContext  *ctx = inlink->dst;
    BiquadsContext *s     = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out_buf;
    ThreadData td;
    int ch;

    if (av_frame_is_writable(buf)) {
        out_buf = buf;
    } else {
        out_buf = ff_get_audio_buffer(outlink, buf->nb_samples);
        if (!out_buf) {
            av_frame_free(&buf);
            return AVERROR(ENOMEM);
        }
        av_frame_copy_props(out_buf, buf);
    }

    td.in = buf;
    td.out = out_buf;
    ctx->internal->execute(ctx, filter_channel, &td, NULL, FFMIN(outlink->channels, ff_filter_get_nb_threads(ctx)));

    for (ch = 0; ch < outlink->channels; ch++) {
        if (s->cache[ch].clippings > 0)
            av_log(ctx, AV_LOG_WARNING, "Channel %d clipping %d times. Please reduce gain.\n",
                   ch, s->cache[ch].clippings);
        s->cache[ch].clippings = 0;
    }

    if (buf != out_buf)
        av_frame_free(&buf);

    return ff_filter_frame(outlink, out_buf);
}

static int process_command(AVFilterContext *ctx, const char *cmd, const char *args,
                           char *res, int res_len, int flags)
{
    AVFilterLink *outlink = ctx->outputs[0];
    int ret;

    ret = ff_filter_process_command(ctx, cmd, args, res, res_len, flags);
    if (ret < 0)
        return ret;

    return config_filter(outlink, 0);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    BiquadsContext *s = ctx->priv;

    av_freep(&s->cache);
}

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .config_props = config_output,
    },
    { NULL }
};

#define OFFSET(x) offsetof(BiquadsContext, x)
#define FLAGS AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_RUNTIME_PARAM
#define AF AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

#define DEFINE_BIQUAD_FILTER(name_, description_)                       \
AVFILTER_DEFINE_CLASS(name_);                                           \
static av_cold int name_##_init(AVFilterContext *ctx) \
{                                                                       \
    BiquadsContext *s = ctx->priv;                                      \
    s->class = &name_##_class;                                          \
    s->filter_type = name_;                                             \
    return init(ctx);                                             \
}                                                                       \
                                                         \
AVFilter ff_af_##name_ = {                         \
    .name          = #name_,                             \
    .description   = NULL_IF_CONFIG_SMALL(description_), \
    .priv_size     = sizeof(BiquadsContext),             \
    .init          = name_##_init,                       \
    .uninit        = uninit,                             \
    .query_formats = query_formats,                      \
    .inputs        = inputs,                             \
    .outputs       = outputs,                            \
    .priv_class    = &name_##_class,                     \
    .process_command = process_command,                  \
    .flags         = AVFILTER_FLAG_SLICE_THREADS | AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL, \
}

#if CONFIG_EQUALIZER_FILTER
static const AVOption equalizer_options[] = {
    {"frequency", "set central frequency", OFFSET(frequency), AV_OPT_TYPE_DOUBLE, {.dbl=0}, 0, 999999, FLAGS},
    {"f",         "set central frequency", OFFSET(frequency), AV_OPT_TYPE_DOUBLE, {.dbl=0}, 0, 999999, FLAGS},
    {"width_type", "set filter-width type", OFFSET(width_type), AV_OPT_TYPE_INT, {.i64=QFACTOR}, HERTZ, NB_WTYPE-1, FLAGS, "width_type"},
    {"t",          "set filter-width type", OFFSET(width_type), AV_OPT_TYPE_INT, {.i64=QFACTOR}, HERTZ, NB_WTYPE-1, FLAGS, "width_type"},
    {"h", "Hz", 0, AV_OPT_TYPE_CONST, {.i64=HERTZ}, 0, 0, FLAGS, "width_type"},
    {"q", "Q-Factor", 0, AV_OPT_TYPE_CONST, {.i64=QFACTOR}, 0, 0, FLAGS, "width_type"},
    {"o", "octave", 0, AV_OPT_TYPE_CONST, {.i64=OCTAVE}, 0, 0, FLAGS, "width_type"},
    {"s", "slope", 0, AV_OPT_TYPE_CONST, {.i64=SLOPE}, 0, 0, FLAGS, "width_type"},
    {"k", "kHz", 0, AV_OPT_TYPE_CONST, {.i64=KHERTZ}, 0, 0, FLAGS, "width_type"},
    {"width", "set band-width", OFFSET(width), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0, 99999, FLAGS},
    {"w",     "set band-width", OFFSET(width), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0, 99999, FLAGS},
    {"gain", "set gain", OFFSET(gain), AV_OPT_TYPE_DOUBLE, {.dbl=0}, -900, 900, FLAGS},
    {"g",    "set gain", OFFSET(gain), AV_OPT_TYPE_DOUBLE, {.dbl=0}, -900, 900, FLAGS},
    {"mix", "set mix", OFFSET(mix), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0, 1, FLAGS},
    {"m",   "set mix", OFFSET(mix), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0, 1, FLAGS},
    {"channels", "set channels to filter", OFFSET(channels), AV_OPT_TYPE_CHANNEL_LAYOUT, {.i64=-1}, INT64_MIN, INT64_MAX, FLAGS},
    {"c",        "set channels to filter", OFFSET(channels), AV_OPT_TYPE_CHANNEL_LAYOUT, {.i64=-1}, INT64_MIN, INT64_MAX, FLAGS},
    {NULL}
};

DEFINE_BIQUAD_FILTER(equalizer, "Apply two-pole peaking equalization (EQ) filter.");
#endif  /* CONFIG_EQUALIZER_FILTER */
#if CONFIG_BASS_FILTER
static const AVOption bass_options[] = {
    {"frequency", "set central frequency", OFFSET(frequency), AV_OPT_TYPE_DOUBLE, {.dbl=100}, 0, 999999, FLAGS},
    {"f",         "set central frequency", OFFSET(frequency), AV_OPT_TYPE_DOUBLE, {.dbl=100}, 0, 999999, FLAGS},
    {"width_type", "set filter-width type", OFFSET(width_type), AV_OPT_TYPE_INT, {.i64=QFACTOR}, HERTZ, NB_WTYPE-1, FLAGS, "width_type"},
    {"t",          "set filter-width type", OFFSET(width_type), AV_OPT_TYPE_INT, {.i64=QFACTOR}, HERTZ, NB_WTYPE-1, FLAGS, "width_type"},
    {"h", "Hz", 0, AV_OPT_TYPE_CONST, {.i64=HERTZ}, 0, 0, FLAGS, "width_type"},
    {"q", "Q-Factor", 0, AV_OPT_TYPE_CONST, {.i64=QFACTOR}, 0, 0, FLAGS, "width_type"},
    {"o", "octave", 0, AV_OPT_TYPE_CONST, {.i64=OCTAVE}, 0, 0, FLAGS, "width_type"},
    {"s", "slope", 0, AV_OPT_TYPE_CONST, {.i64=SLOPE}, 0, 0, FLAGS, "width_type"},
    {"k", "kHz", 0, AV_OPT_TYPE_CONST, {.i64=KHERTZ}, 0, 0, FLAGS, "width_type"},
    {"width", "set shelf transition steep", OFFSET(width), AV_OPT_TYPE_DOUBLE, {.dbl=0.5}, 0, 99999, FLAGS},
    {"w",     "set shelf transition steep", OFFSET(width), AV_OPT_TYPE_DOUBLE, {.dbl=0.5}, 0, 99999, FLAGS},
    {"gain", "set gain", OFFSET(gain), AV_OPT_TYPE_DOUBLE, {.dbl=0}, -900, 900, FLAGS},
    {"g",    "set gain", OFFSET(gain), AV_OPT_TYPE_DOUBLE, {.dbl=0}, -900, 900, FLAGS},
    {"mix", "set mix", OFFSET(mix), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0, 1, FLAGS},
    {"m",   "set mix", OFFSET(mix), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0, 1, FLAGS},
    {"channels", "set channels to filter", OFFSET(channels), AV_OPT_TYPE_CHANNEL_LAYOUT, {.i64=-1}, INT64_MIN, INT64_MAX, FLAGS},
    {"c",        "set channels to filter", OFFSET(channels), AV_OPT_TYPE_CHANNEL_LAYOUT, {.i64=-1}, INT64_MIN, INT64_MAX, FLAGS},
    {NULL}
};

DEFINE_BIQUAD_FILTER(bass, "Boost or cut lower frequencies.");
#endif  /* CONFIG_BASS_FILTER */
#if CONFIG_TREBLE_FILTER
static const AVOption treble_options[] = {
    {"frequency", "set central frequency", OFFSET(frequency), AV_OPT_TYPE_DOUBLE, {.dbl=3000}, 0, 999999, FLAGS},
    {"f",         "set central frequency", OFFSET(frequency), AV_OPT_TYPE_DOUBLE, {.dbl=3000}, 0, 999999, FLAGS},
    {"width_type", "set filter-width type", OFFSET(width_type), AV_OPT_TYPE_INT, {.i64=QFACTOR}, HERTZ, NB_WTYPE-1, FLAGS, "width_type"},
    {"t",          "set filter-width type", OFFSET(width_type), AV_OPT_TYPE_INT, {.i64=QFACTOR}, HERTZ, NB_WTYPE-1, FLAGS, "width_type"},
    {"h", "Hz", 0, AV_OPT_TYPE_CONST, {.i64=HERTZ}, 0, 0, FLAGS, "width_type"},
    {"q", "Q-Factor", 0, AV_OPT_TYPE_CONST, {.i64=QFACTOR}, 0, 0, FLAGS, "width_type"},
    {"o", "octave", 0, AV_OPT_TYPE_CONST, {.i64=OCTAVE}, 0, 0, FLAGS, "width_type"},
    {"s", "slope", 0, AV_OPT_TYPE_CONST, {.i64=SLOPE}, 0, 0, FLAGS, "width_type"},
    {"k", "kHz", 0, AV_OPT_TYPE_CONST, {.i64=KHERTZ}, 0, 0, FLAGS, "width_type"},
    {"width", "set shelf transition steep", OFFSET(width), AV_OPT_TYPE_DOUBLE, {.dbl=0.5}, 0, 99999, FLAGS},
    {"w",     "set shelf transition steep", OFFSET(width), AV_OPT_TYPE_DOUBLE, {.dbl=0.5}, 0, 99999, FLAGS},
    {"gain", "set gain", OFFSET(gain), AV_OPT_TYPE_DOUBLE, {.dbl=0}, -900, 900, FLAGS},
    {"g",    "set gain", OFFSET(gain), AV_OPT_TYPE_DOUBLE, {.dbl=0}, -900, 900, FLAGS},
    {"mix", "set mix", OFFSET(mix), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0, 1, FLAGS},
    {"m",   "set mix", OFFSET(mix), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0, 1, FLAGS},
    {"channels", "set channels to filter", OFFSET(channels), AV_OPT_TYPE_CHANNEL_LAYOUT, {.i64=-1}, INT64_MIN, INT64_MAX, FLAGS},
    {"c",        "set channels to filter", OFFSET(channels), AV_OPT_TYPE_CHANNEL_LAYOUT, {.i64=-1}, INT64_MIN, INT64_MAX, FLAGS},
    {NULL}
};

DEFINE_BIQUAD_FILTER(treble, "Boost or cut upper frequencies.");
#endif  /* CONFIG_TREBLE_FILTER */
#if CONFIG_BANDPASS_FILTER
static const AVOption bandpass_options[] = {
    {"frequency", "set central frequency", OFFSET(frequency), AV_OPT_TYPE_DOUBLE, {.dbl=3000}, 0, 999999, FLAGS},
    {"f",         "set central frequency", OFFSET(frequency), AV_OPT_TYPE_DOUBLE, {.dbl=3000}, 0, 999999, FLAGS},
    {"width_type", "set filter-width type", OFFSET(width_type), AV_OPT_TYPE_INT, {.i64=QFACTOR}, HERTZ, NB_WTYPE-1, FLAGS, "width_type"},
    {"t",          "set filter-width type", OFFSET(width_type), AV_OPT_TYPE_INT, {.i64=QFACTOR}, HERTZ, NB_WTYPE-1, FLAGS, "width_type"},
    {"h", "Hz", 0, AV_OPT_TYPE_CONST, {.i64=HERTZ}, 0, 0, FLAGS, "width_type"},
    {"q", "Q-Factor", 0, AV_OPT_TYPE_CONST, {.i64=QFACTOR}, 0, 0, FLAGS, "width_type"},
    {"o", "octave", 0, AV_OPT_TYPE_CONST, {.i64=OCTAVE}, 0, 0, FLAGS, "width_type"},
    {"s", "slope", 0, AV_OPT_TYPE_CONST, {.i64=SLOPE}, 0, 0, FLAGS, "width_type"},
    {"k", "kHz", 0, AV_OPT_TYPE_CONST, {.i64=KHERTZ}, 0, 0, FLAGS, "width_type"},
    {"width", "set band-width", OFFSET(width), AV_OPT_TYPE_DOUBLE, {.dbl=0.5}, 0, 99999, FLAGS},
    {"w",     "set band-width", OFFSET(width), AV_OPT_TYPE_DOUBLE, {.dbl=0.5}, 0, 99999, FLAGS},
    {"csg",   "use constant skirt gain", OFFSET(csg), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, FLAGS},
    {"mix", "set mix", OFFSET(mix), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0, 1, FLAGS},
    {"m",   "set mix", OFFSET(mix), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0, 1, FLAGS},
    {"channels", "set channels to filter", OFFSET(channels), AV_OPT_TYPE_CHANNEL_LAYOUT, {.i64=-1}, INT64_MIN, INT64_MAX, FLAGS},
    {"c",        "set channels to filter", OFFSET(channels), AV_OPT_TYPE_CHANNEL_LAYOUT, {.i64=-1}, INT64_MIN, INT64_MAX, FLAGS},
    {NULL}
};

DEFINE_BIQUAD_FILTER(bandpass, "Apply a two-pole Butterworth band-pass filter.");
#endif  /* CONFIG_BANDPASS_FILTER */
#if CONFIG_BANDREJECT_FILTER
static const AVOption bandreject_options[] = {
    {"frequency", "set central frequency", OFFSET(frequency), AV_OPT_TYPE_DOUBLE, {.dbl=3000}, 0, 999999, FLAGS},
    {"f",         "set central frequency", OFFSET(frequency), AV_OPT_TYPE_DOUBLE, {.dbl=3000}, 0, 999999, FLAGS},
    {"width_type", "set filter-width type", OFFSET(width_type), AV_OPT_TYPE_INT, {.i64=QFACTOR}, HERTZ, NB_WTYPE-1, FLAGS, "width_type"},
    {"t",          "set filter-width type", OFFSET(width_type), AV_OPT_TYPE_INT, {.i64=QFACTOR}, HERTZ, NB_WTYPE-1, FLAGS, "width_type"},
    {"h", "Hz", 0, AV_OPT_TYPE_CONST, {.i64=HERTZ}, 0, 0, FLAGS, "width_type"},
    {"q", "Q-Factor", 0, AV_OPT_TYPE_CONST, {.i64=QFACTOR}, 0, 0, FLAGS, "width_type"},
    {"o", "octave", 0, AV_OPT_TYPE_CONST, {.i64=OCTAVE}, 0, 0, FLAGS, "width_type"},
    {"s", "slope", 0, AV_OPT_TYPE_CONST, {.i64=SLOPE}, 0, 0, FLAGS, "width_type"},
    {"k", "kHz", 0, AV_OPT_TYPE_CONST, {.i64=KHERTZ}, 0, 0, FLAGS, "width_type"},
    {"width", "set band-width", OFFSET(width), AV_OPT_TYPE_DOUBLE, {.dbl=0.5}, 0, 99999, FLAGS},
    {"w",     "set band-width", OFFSET(width), AV_OPT_TYPE_DOUBLE, {.dbl=0.5}, 0, 99999, FLAGS},
    {"mix", "set mix", OFFSET(mix), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0, 1, FLAGS},
    {"m",   "set mix", OFFSET(mix), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0, 1, FLAGS},
    {"channels", "set channels to filter", OFFSET(channels), AV_OPT_TYPE_CHANNEL_LAYOUT, {.i64=-1}, INT64_MIN, INT64_MAX, FLAGS},
    {"c",        "set channels to filter", OFFSET(channels), AV_OPT_TYPE_CHANNEL_LAYOUT, {.i64=-1}, INT64_MIN, INT64_MAX, FLAGS},
    {NULL}
};

DEFINE_BIQUAD_FILTER(bandreject, "Apply a two-pole Butterworth band-reject filter.");
#endif  /* CONFIG_BANDREJECT_FILTER */
#if CONFIG_LOWPASS_FILTER
static const AVOption lowpass_options[] = {
    {"frequency", "set frequency", OFFSET(frequency), AV_OPT_TYPE_DOUBLE, {.dbl=500}, 0, 999999, FLAGS},
    {"f",         "set frequency", OFFSET(frequency), AV_OPT_TYPE_DOUBLE, {.dbl=500}, 0, 999999, FLAGS},
    {"width_type", "set filter-width type", OFFSET(width_type), AV_OPT_TYPE_INT, {.i64=QFACTOR}, HERTZ, NB_WTYPE-1, FLAGS, "width_type"},
    {"t",          "set filter-width type", OFFSET(width_type), AV_OPT_TYPE_INT, {.i64=QFACTOR}, HERTZ, NB_WTYPE-1, FLAGS, "width_type"},
    {"h", "Hz", 0, AV_OPT_TYPE_CONST, {.i64=HERTZ}, 0, 0, FLAGS, "width_type"},
    {"q", "Q-Factor", 0, AV_OPT_TYPE_CONST, {.i64=QFACTOR}, 0, 0, FLAGS, "width_type"},
    {"o", "octave", 0, AV_OPT_TYPE_CONST, {.i64=OCTAVE}, 0, 0, FLAGS, "width_type"},
    {"s", "slope", 0, AV_OPT_TYPE_CONST, {.i64=SLOPE}, 0, 0, FLAGS, "width_type"},
    {"k", "kHz", 0, AV_OPT_TYPE_CONST, {.i64=KHERTZ}, 0, 0, FLAGS, "width_type"},
    {"width", "set width", OFFSET(width), AV_OPT_TYPE_DOUBLE, {.dbl=0.707}, 0, 99999, FLAGS},
    {"w",     "set width", OFFSET(width), AV_OPT_TYPE_DOUBLE, {.dbl=0.707}, 0, 99999, FLAGS},
    {"poles", "set number of poles", OFFSET(poles), AV_OPT_TYPE_INT, {.i64=2}, 1, 2, AF},
    {"p",     "set number of poles", OFFSET(poles), AV_OPT_TYPE_INT, {.i64=2}, 1, 2, AF},
    {"mix", "set mix", OFFSET(mix), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0, 1, FLAGS},
    {"m",   "set mix", OFFSET(mix), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0, 1, FLAGS},
    {"channels", "set channels to filter", OFFSET(channels), AV_OPT_TYPE_CHANNEL_LAYOUT, {.i64=-1}, INT64_MIN, INT64_MAX, FLAGS},
    {"c",        "set channels to filter", OFFSET(channels), AV_OPT_TYPE_CHANNEL_LAYOUT, {.i64=-1}, INT64_MIN, INT64_MAX, FLAGS},
    {NULL}
};

DEFINE_BIQUAD_FILTER(lowpass, "Apply a low-pass filter with 3dB point frequency.");
#endif  /* CONFIG_LOWPASS_FILTER */
#if CONFIG_HIGHPASS_FILTER
static const AVOption highpass_options[] = {
    {"frequency", "set frequency", OFFSET(frequency), AV_OPT_TYPE_DOUBLE, {.dbl=3000}, 0, 999999, FLAGS},
    {"f",         "set frequency", OFFSET(frequency), AV_OPT_TYPE_DOUBLE, {.dbl=3000}, 0, 999999, FLAGS},
    {"width_type", "set filter-width type", OFFSET(width_type), AV_OPT_TYPE_INT, {.i64=QFACTOR}, HERTZ, NB_WTYPE-1, FLAGS, "width_type"},
    {"t",          "set filter-width type", OFFSET(width_type), AV_OPT_TYPE_INT, {.i64=QFACTOR}, HERTZ, NB_WTYPE-1, FLAGS, "width_type"},
    {"h", "Hz", 0, AV_OPT_TYPE_CONST, {.i64=HERTZ}, 0, 0, FLAGS, "width_type"},
    {"q", "Q-Factor", 0, AV_OPT_TYPE_CONST, {.i64=QFACTOR}, 0, 0, FLAGS, "width_type"},
    {"o", "octave", 0, AV_OPT_TYPE_CONST, {.i64=OCTAVE}, 0, 0, FLAGS, "width_type"},
    {"s", "slope", 0, AV_OPT_TYPE_CONST, {.i64=SLOPE}, 0, 0, FLAGS, "width_type"},
    {"k", "kHz", 0, AV_OPT_TYPE_CONST, {.i64=KHERTZ}, 0, 0, FLAGS, "width_type"},
    {"width", "set width", OFFSET(width), AV_OPT_TYPE_DOUBLE, {.dbl=0.707}, 0, 99999, FLAGS},
    {"w",     "set width", OFFSET(width), AV_OPT_TYPE_DOUBLE, {.dbl=0.707}, 0, 99999, FLAGS},
    {"poles", "set number of poles", OFFSET(poles), AV_OPT_TYPE_INT, {.i64=2}, 1, 2, AF},
    {"p",     "set number of poles", OFFSET(poles), AV_OPT_TYPE_INT, {.i64=2}, 1, 2, AF},
    {"mix", "set mix", OFFSET(mix), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0, 1, FLAGS},
    {"m",   "set mix", OFFSET(mix), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0, 1, FLAGS},
    {"channels", "set channels to filter", OFFSET(channels), AV_OPT_TYPE_CHANNEL_LAYOUT, {.i64=-1}, INT64_MIN, INT64_MAX, FLAGS},
    {"c",        "set channels to filter", OFFSET(channels), AV_OPT_TYPE_CHANNEL_LAYOUT, {.i64=-1}, INT64_MIN, INT64_MAX, FLAGS},
    {NULL}
};

DEFINE_BIQUAD_FILTER(highpass, "Apply a high-pass filter with 3dB point frequency.");
#endif  /* CONFIG_HIGHPASS_FILTER */
#if CONFIG_ALLPASS_FILTER
static const AVOption allpass_options[] = {
    {"frequency", "set central frequency", OFFSET(frequency), AV_OPT_TYPE_DOUBLE, {.dbl=3000}, 0, 999999, FLAGS},
    {"f",         "set central frequency", OFFSET(frequency), AV_OPT_TYPE_DOUBLE, {.dbl=3000}, 0, 999999, FLAGS},
    {"width_type", "set filter-width type", OFFSET(width_type), AV_OPT_TYPE_INT, {.i64=HERTZ}, HERTZ, NB_WTYPE-1, FLAGS, "width_type"},
    {"t",          "set filter-width type", OFFSET(width_type), AV_OPT_TYPE_INT, {.i64=HERTZ}, HERTZ, NB_WTYPE-1, FLAGS, "width_type"},
    {"h", "Hz", 0, AV_OPT_TYPE_CONST, {.i64=HERTZ}, 0, 0, FLAGS, "width_type"},
    {"q", "Q-Factor", 0, AV_OPT_TYPE_CONST, {.i64=QFACTOR}, 0, 0, FLAGS, "width_type"},
    {"o", "octave", 0, AV_OPT_TYPE_CONST, {.i64=OCTAVE}, 0, 0, FLAGS, "width_type"},
    {"s", "slope", 0, AV_OPT_TYPE_CONST, {.i64=SLOPE}, 0, 0, FLAGS, "width_type"},
    {"k", "kHz", 0, AV_OPT_TYPE_CONST, {.i64=KHERTZ}, 0, 0, FLAGS, "width_type"},
    {"width", "set filter-width", OFFSET(width), AV_OPT_TYPE_DOUBLE, {.dbl=707.1}, 0, 99999, FLAGS},
    {"w",     "set filter-width", OFFSET(width), AV_OPT_TYPE_DOUBLE, {.dbl=707.1}, 0, 99999, FLAGS},
    {"mix", "set mix", OFFSET(mix), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0, 1, FLAGS},
    {"m",   "set mix", OFFSET(mix), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0, 1, FLAGS},
    {"channels", "set channels to filter", OFFSET(channels), AV_OPT_TYPE_CHANNEL_LAYOUT, {.i64=-1}, INT64_MIN, INT64_MAX, FLAGS},
    {"c",        "set channels to filter", OFFSET(channels), AV_OPT_TYPE_CHANNEL_LAYOUT, {.i64=-1}, INT64_MIN, INT64_MAX, FLAGS},
    {NULL}
};

DEFINE_BIQUAD_FILTER(allpass, "Apply a two-pole all-pass filter.");
#endif  /* CONFIG_ALLPASS_FILTER */
#if CONFIG_LOWSHELF_FILTER
static const AVOption lowshelf_options[] = {
    {"frequency", "set central frequency", OFFSET(frequency), AV_OPT_TYPE_DOUBLE, {.dbl=100}, 0, 999999, FLAGS},
    {"f",         "set central frequency", OFFSET(frequency), AV_OPT_TYPE_DOUBLE, {.dbl=100}, 0, 999999, FLAGS},
    {"width_type", "set filter-width type", OFFSET(width_type), AV_OPT_TYPE_INT, {.i64=QFACTOR}, HERTZ, NB_WTYPE-1, FLAGS, "width_type"},
    {"t",          "set filter-width type", OFFSET(width_type), AV_OPT_TYPE_INT, {.i64=QFACTOR}, HERTZ, NB_WTYPE-1, FLAGS, "width_type"},
    {"h", "Hz", 0, AV_OPT_TYPE_CONST, {.i64=HERTZ}, 0, 0, FLAGS, "width_type"},
    {"q", "Q-Factor", 0, AV_OPT_TYPE_CONST, {.i64=QFACTOR}, 0, 0, FLAGS, "width_type"},
    {"o", "octave", 0, AV_OPT_TYPE_CONST, {.i64=OCTAVE}, 0, 0, FLAGS, "width_type"},
    {"s", "slope", 0, AV_OPT_TYPE_CONST, {.i64=SLOPE}, 0, 0, FLAGS, "width_type"},
    {"k", "kHz", 0, AV_OPT_TYPE_CONST, {.i64=KHERTZ}, 0, 0, FLAGS, "width_type"},
    {"width", "set shelf transition steep", OFFSET(width), AV_OPT_TYPE_DOUBLE, {.dbl=0.5}, 0, 99999, FLAGS},
    {"w",     "set shelf transition steep", OFFSET(width), AV_OPT_TYPE_DOUBLE, {.dbl=0.5}, 0, 99999, FLAGS},
    {"gain", "set gain", OFFSET(gain), AV_OPT_TYPE_DOUBLE, {.dbl=0}, -900, 900, FLAGS},
    {"g",    "set gain", OFFSET(gain), AV_OPT_TYPE_DOUBLE, {.dbl=0}, -900, 900, FLAGS},
    {"mix", "set mix", OFFSET(mix), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0, 1, FLAGS},
    {"m",   "set mix", OFFSET(mix), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0, 1, FLAGS},
    {"channels", "set channels to filter", OFFSET(channels), AV_OPT_TYPE_CHANNEL_LAYOUT, {.i64=-1}, INT64_MIN, INT64_MAX, FLAGS},
    {"c",        "set channels to filter", OFFSET(channels), AV_OPT_TYPE_CHANNEL_LAYOUT, {.i64=-1}, INT64_MIN, INT64_MAX, FLAGS},
    {NULL}
};

DEFINE_BIQUAD_FILTER(lowshelf, "Apply a low shelf filter.");
#endif  /* CONFIG_LOWSHELF_FILTER */
#if CONFIG_HIGHSHELF_FILTER
static const AVOption highshelf_options[] = {
    {"frequency", "set central frequency", OFFSET(frequency), AV_OPT_TYPE_DOUBLE, {.dbl=3000}, 0, 999999, FLAGS},
    {"f",         "set central frequency", OFFSET(frequency), AV_OPT_TYPE_DOUBLE, {.dbl=3000}, 0, 999999, FLAGS},
    {"width_type", "set filter-width type", OFFSET(width_type), AV_OPT_TYPE_INT, {.i64=QFACTOR}, HERTZ, NB_WTYPE-1, FLAGS, "width_type"},
    {"t",          "set filter-width type", OFFSET(width_type), AV_OPT_TYPE_INT, {.i64=QFACTOR}, HERTZ, NB_WTYPE-1, FLAGS, "width_type"},
    {"h", "Hz", 0, AV_OPT_TYPE_CONST, {.i64=HERTZ}, 0, 0, FLAGS, "width_type"},
    {"q", "Q-Factor", 0, AV_OPT_TYPE_CONST, {.i64=QFACTOR}, 0, 0, FLAGS, "width_type"},
    {"o", "octave", 0, AV_OPT_TYPE_CONST, {.i64=OCTAVE}, 0, 0, FLAGS, "width_type"},
    {"s", "slope", 0, AV_OPT_TYPE_CONST, {.i64=SLOPE}, 0, 0, FLAGS, "width_type"},
    {"k", "kHz", 0, AV_OPT_TYPE_CONST, {.i64=KHERTZ}, 0, 0, FLAGS, "width_type"},
    {"width", "set shelf transition steep", OFFSET(width), AV_OPT_TYPE_DOUBLE, {.dbl=0.5}, 0, 99999, FLAGS},
    {"w",     "set shelf transition steep", OFFSET(width), AV_OPT_TYPE_DOUBLE, {.dbl=0.5}, 0, 99999, FLAGS},
    {"gain", "set gain", OFFSET(gain), AV_OPT_TYPE_DOUBLE, {.dbl=0}, -900, 900, FLAGS},
    {"g",    "set gain", OFFSET(gain), AV_OPT_TYPE_DOUBLE, {.dbl=0}, -900, 900, FLAGS},
    {"mix", "set mix", OFFSET(mix), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0, 1, FLAGS},
    {"m",   "set mix", OFFSET(mix), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0, 1, FLAGS},
    {"channels", "set channels to filter", OFFSET(channels), AV_OPT_TYPE_CHANNEL_LAYOUT, {.i64=-1}, INT64_MIN, INT64_MAX, FLAGS},
    {"c",        "set channels to filter", OFFSET(channels), AV_OPT_TYPE_CHANNEL_LAYOUT, {.i64=-1}, INT64_MIN, INT64_MAX, FLAGS},
    {NULL}
};

DEFINE_BIQUAD_FILTER(highshelf, "Apply a high shelf filter.");
#endif  /* CONFIG_HIGHSHELF_FILTER */
#if CONFIG_BIQUAD_FILTER
static const AVOption biquad_options[] = {
    {"a0", NULL, OFFSET(a0), AV_OPT_TYPE_DOUBLE, {.dbl=1}, INT32_MIN, INT32_MAX, FLAGS},
    {"a1", NULL, OFFSET(a1), AV_OPT_TYPE_DOUBLE, {.dbl=0}, INT32_MIN, INT32_MAX, FLAGS},
    {"a2", NULL, OFFSET(a2), AV_OPT_TYPE_DOUBLE, {.dbl=0}, INT32_MIN, INT32_MAX, FLAGS},
    {"b0", NULL, OFFSET(b0), AV_OPT_TYPE_DOUBLE, {.dbl=0}, INT32_MIN, INT32_MAX, FLAGS},
    {"b1", NULL, OFFSET(b1), AV_OPT_TYPE_DOUBLE, {.dbl=0}, INT32_MIN, INT32_MAX, FLAGS},
    {"b2", NULL, OFFSET(b2), AV_OPT_TYPE_DOUBLE, {.dbl=0}, INT32_MIN, INT32_MAX, FLAGS},
    {"mix", "set mix", OFFSET(mix), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0, 1, FLAGS},
    {"m",   "set mix", OFFSET(mix), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0, 1, FLAGS},
    {"channels", "set channels to filter", OFFSET(channels), AV_OPT_TYPE_CHANNEL_LAYOUT, {.i64=-1}, INT64_MIN, INT64_MAX, FLAGS},
    {"c",        "set channels to filter", OFFSET(channels), AV_OPT_TYPE_CHANNEL_LAYOUT, {.i64=-1}, INT64_MIN, INT64_MAX, FLAGS},
    {NULL}
};

DEFINE_BIQUAD_FILTER(biquad, "Apply a biquad IIR filter with the given coefficients.");
#endif  /* CONFIG_BIQUAD_FILTER */
