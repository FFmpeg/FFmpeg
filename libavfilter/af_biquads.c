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
#include "libavutil/opt.h"
#include "audio.h"
#include "avfilter.h"
#include "internal.h"

enum FilterType {
    biquad,
    equalizer,
    bass,
    treble,
    band,
    bandpass,
    bandreject,
    allpass,
    highpass,
    lowpass,
};

enum WidthType {
    NONE,
    HERTZ,
    OCTAVE,
    QFACTOR,
    SLOPE,
};

typedef struct ChanCache {
    double i1, i2;
    double o1, o2;
} ChanCache;

typedef struct {
    const AVClass *class;

    enum FilterType filter_type;
    enum WidthType width_type;
    int poles;
    int csg;

    double gain;
    double frequency;
    double width;

    double a0, a1, a2;
    double b0, b1, b2;

    ChanCache *cache;

    void (*filter)(const void *ibuf, void *obuf, int len,
                   double *i1, double *i2, double *o1, double *o2,
                   double b0, double b1, double b2, double a1, double a2);
} BiquadsContext;

static av_cold int init(AVFilterContext *ctx)
{
    BiquadsContext *p = ctx->priv;

    if (p->filter_type != biquad) {
        if (p->frequency <= 0 || p->width <= 0) {
            av_log(ctx, AV_LOG_ERROR, "Invalid frequency %f and/or width %f <= 0\n",
                   p->frequency, p->width);
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

    layouts = ff_all_channel_layouts();
    if (!layouts)
        return AVERROR(ENOMEM);
    ff_set_common_channel_layouts(ctx, layouts);

    formats = ff_make_format_list(sample_fmts);
    if (!formats)
        return AVERROR(ENOMEM);
    ff_set_common_formats(ctx, formats);

    formats = ff_all_samplerates();
    if (!formats)
        return AVERROR(ENOMEM);
    ff_set_common_samplerates(ctx, formats);

    return 0;
}

#define BIQUAD_FILTER(name, type, min, max, need_clipping)                    \
static void biquad_## name (const void *input, void *output, int len,         \
                            double *in1, double *in2,                         \
                            double *out1, double *out2,                       \
                            double b0, double b1, double b2,                  \
                            double a1, double a2)                             \
{                                                                             \
    const type *ibuf = input;                                                 \
    type *obuf = output;                                                      \
    double i1 = *in1;                                                         \
    double i2 = *in2;                                                         \
    double o1 = *out1;                                                        \
    double o2 = *out2;                                                        \
    int i;                                                                    \
    a1 = -a1;                                                                 \
    a2 = -a2;                                                                 \
                                                                              \
    for (i = 0; i+1 < len; i++) {                                             \
        o2 = i2 * b2 + i1 * b1 + ibuf[i] * b0 + o2 * a2 + o1 * a1;            \
        i2 = ibuf[i];                                                         \
        if (need_clipping && o2 < min) {                                      \
            av_log(NULL, AV_LOG_WARNING, "clipping\n");                       \
            obuf[i] = min;                                                    \
        } else if (need_clipping && o2 > max) {                               \
            av_log(NULL, AV_LOG_WARNING, "clipping\n");                       \
            obuf[i] = max;                                                    \
        } else {                                                              \
            obuf[i] = o2;                                                     \
        }                                                                     \
        i++;                                                                  \
        o1 = i1 * b2 + i2 * b1 + ibuf[i] * b0 + o1 * a2 + o2 * a1;            \
        i1 = ibuf[i];                                                         \
        if (need_clipping && o1 < min) {                                      \
            av_log(NULL, AV_LOG_WARNING, "clipping\n");                       \
            obuf[i] = min;                                                    \
        } else if (need_clipping && o1 > max) {                               \
            av_log(NULL, AV_LOG_WARNING, "clipping\n");                       \
            obuf[i] = max;                                                    \
        } else {                                                              \
            obuf[i] = o1;                                                     \
        }                                                                     \
    }                                                                         \
    if (i < len) {                                                            \
        double o0 = ibuf[i] * b0 + i1 * b1 + i2 * b2 + o1 * a1 + o2 * a2;     \
        i2 = i1;                                                              \
        i1 = ibuf[i];                                                         \
        o2 = o1;                                                              \
        o1 = o0;                                                              \
        if (need_clipping && o0 < min) {                                      \
            av_log(NULL, AV_LOG_WARNING, "clipping\n");                       \
            obuf[i] = min;                                                    \
        } else if (need_clipping && o0 > max) {                               \
            av_log(NULL, AV_LOG_WARNING, "clipping\n");                       \
            obuf[i] = max;                                                    \
        } else {                                                              \
            obuf[i] = o0;                                                     \
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

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx    = outlink->src;
    BiquadsContext *p       = ctx->priv;
    AVFilterLink *inlink    = ctx->inputs[0];
    double A = exp(p->gain / 40 * log(10.));
    double w0 = 2 * M_PI * p->frequency / inlink->sample_rate;
    double alpha;

    if (w0 > M_PI) {
        av_log(ctx, AV_LOG_ERROR,
               "Invalid frequency %f. Frequency must be less than half the sample-rate %d.\n",
               p->frequency, inlink->sample_rate);
        return AVERROR(EINVAL);
    }

    switch (p->width_type) {
    case NONE:
        alpha = 0.0;
        break;
    case HERTZ:
        alpha = sin(w0) / (2 * p->frequency / p->width);
        break;
    case OCTAVE:
        alpha = sin(w0) * sinh(log(2.) / 2 * p->width * w0 / sin(w0));
        break;
    case QFACTOR:
        alpha = sin(w0) / (2 * p->width);
        break;
    case SLOPE:
        alpha = sin(w0) / 2 * sqrt((A + 1 / A) * (1 / p->width - 1) + 2);
        break;
    default:
        av_assert0(0);
    }

    switch (p->filter_type) {
    case biquad:
        break;
    case equalizer:
        p->a0 =   1 + alpha / A;
        p->a1 =  -2 * cos(w0);
        p->a2 =   1 - alpha / A;
        p->b0 =   1 + alpha * A;
        p->b1 =  -2 * cos(w0);
        p->b2 =   1 - alpha * A;
        break;
    case bass:
        p->a0 =          (A + 1) + (A - 1) * cos(w0) + 2 * sqrt(A) * alpha;
        p->a1 =    -2 * ((A - 1) + (A + 1) * cos(w0));
        p->a2 =          (A + 1) + (A - 1) * cos(w0) - 2 * sqrt(A) * alpha;
        p->b0 =     A * ((A + 1) - (A - 1) * cos(w0) + 2 * sqrt(A) * alpha);
        p->b1 = 2 * A * ((A - 1) - (A + 1) * cos(w0));
        p->b2 =     A * ((A + 1) - (A - 1) * cos(w0) - 2 * sqrt(A) * alpha);
        break;
    case treble:
        p->a0 =          (A + 1) - (A - 1) * cos(w0) + 2 * sqrt(A) * alpha;
        p->a1 =     2 * ((A - 1) - (A + 1) * cos(w0));
        p->a2 =          (A + 1) - (A - 1) * cos(w0) - 2 * sqrt(A) * alpha;
        p->b0 =     A * ((A + 1) + (A - 1) * cos(w0) + 2 * sqrt(A) * alpha);
        p->b1 =-2 * A * ((A - 1) + (A + 1) * cos(w0));
        p->b2 =     A * ((A + 1) + (A - 1) * cos(w0) - 2 * sqrt(A) * alpha);
        break;
    case bandpass:
        if (p->csg) {
            p->a0 =  1 + alpha;
            p->a1 = -2 * cos(w0);
            p->a2 =  1 - alpha;
            p->b0 =  sin(w0) / 2;
            p->b1 =  0;
            p->b2 = -sin(w0) / 2;
        } else {
            p->a0 =  1 + alpha;
            p->a1 = -2 * cos(w0);
            p->a2 =  1 - alpha;
            p->b0 =  alpha;
            p->b1 =  0;
            p->b2 = -alpha;
        }
        break;
    case bandreject:
        p->a0 =  1 + alpha;
        p->a1 = -2 * cos(w0);
        p->a2 =  1 - alpha;
        p->b0 =  1;
        p->b1 = -2 * cos(w0);
        p->b2 =  1;
        break;
    case lowpass:
        if (p->poles == 1) {
            p->a0 = 1;
            p->a1 = -exp(-w0);
            p->a2 = 0;
            p->b0 = 1 + p->a1;
            p->b1 = 0;
            p->b2 = 0;
        } else {
            p->a0 =  1 + alpha;
            p->a1 = -2 * cos(w0);
            p->a2 =  1 - alpha;
            p->b0 = (1 - cos(w0)) / 2;
            p->b1 =  1 - cos(w0);
            p->b2 = (1 - cos(w0)) / 2;
        }
        break;
    case highpass:
        if (p->poles == 1) {
            p->a0 = 1;
            p->a1 = -exp(-w0);
            p->a2 = 0;
            p->b0 = (1 - p->a1) / 2;
            p->b1 = -p->b0;
            p->b2 = 0;
        } else {
            p->a0 =   1 + alpha;
            p->a1 =  -2 * cos(w0);
            p->a2 =   1 - alpha;
            p->b0 =  (1 + cos(w0)) / 2;
            p->b1 = -(1 + cos(w0));
            p->b2 =  (1 + cos(w0)) / 2;
        }
        break;
    case allpass:
        p->a0 =  1 + alpha;
        p->a1 = -2 * cos(w0);
        p->a2 =  1 - alpha;
        p->b0 =  1 - alpha;
        p->b1 = -2 * cos(w0);
        p->b2 =  1 + alpha;
        break;
    default:
        av_assert0(0);
    }

    p->a1 /= p->a0;
    p->a2 /= p->a0;
    p->b0 /= p->a0;
    p->b1 /= p->a0;
    p->b2 /= p->a0;

    p->cache = av_realloc_f(p->cache, sizeof(ChanCache), inlink->channels);
    if (!p->cache)
        return AVERROR(ENOMEM);
    memset(p->cache, 0, sizeof(ChanCache) * inlink->channels);

    switch (inlink->format) {
    case AV_SAMPLE_FMT_S16P: p->filter = biquad_s16; break;
    case AV_SAMPLE_FMT_S32P: p->filter = biquad_s32; break;
    case AV_SAMPLE_FMT_FLTP: p->filter = biquad_flt; break;
    case AV_SAMPLE_FMT_DBLP: p->filter = biquad_dbl; break;
    default: av_assert0(0);
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *buf)
{
    BiquadsContext *p       = inlink->dst->priv;
    AVFilterLink *outlink   = inlink->dst->outputs[0];
    AVFrame *out_buf;
    int nb_samples = buf->nb_samples;
    int ch;

    if (av_frame_is_writable(buf)) {
        out_buf = buf;
    } else {
        out_buf = ff_get_audio_buffer(inlink, nb_samples);
        if (!out_buf)
            return AVERROR(ENOMEM);
        av_frame_copy_props(out_buf, buf);
    }

    for (ch = 0; ch < av_frame_get_channels(buf); ch++)
        p->filter(buf->extended_data[ch],
                  out_buf->extended_data[ch], nb_samples,
                  &p->cache[ch].i1, &p->cache[ch].i2,
                  &p->cache[ch].o1, &p->cache[ch].o2,
                  p->b0, p->b1, p->b2, p->a1, p->a2);

    if (buf != out_buf)
        av_frame_free(&buf);

    return ff_filter_frame(outlink, out_buf);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    BiquadsContext *p = ctx->priv;

    av_freep(&p->cache);
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
#define FLAGS AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

#define DEFINE_BIQUAD_FILTER(name_, description_)                       \
AVFILTER_DEFINE_CLASS(name_);                                           \
static av_cold int name_##_init(AVFilterContext *ctx) \
{                                                                       \
    BiquadsContext *p = ctx->priv;                                      \
    p->class = &name_##_class;                                          \
    p->filter_type = name_;                                             \
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
}

#if CONFIG_EQUALIZER_FILTER
static const AVOption equalizer_options[] = {
    {"frequency", "set central frequency", OFFSET(frequency), AV_OPT_TYPE_DOUBLE, {.dbl=0}, 0, 999999, FLAGS},
    {"f",         "set central frequency", OFFSET(frequency), AV_OPT_TYPE_DOUBLE, {.dbl=0}, 0, 999999, FLAGS},
    {"width_type", "set filter-width type", OFFSET(width_type), AV_OPT_TYPE_INT, {.i64=QFACTOR}, HERTZ, SLOPE, FLAGS, "width_type"},
    {"h", "Hz", 0, AV_OPT_TYPE_CONST, {.i64=HERTZ}, 0, 0, FLAGS, "width_type"},
    {"q", "Q-Factor", 0, AV_OPT_TYPE_CONST, {.i64=QFACTOR}, 0, 0, FLAGS, "width_type"},
    {"o", "octave", 0, AV_OPT_TYPE_CONST, {.i64=OCTAVE}, 0, 0, FLAGS, "width_type"},
    {"s", "slope", 0, AV_OPT_TYPE_CONST, {.i64=SLOPE}, 0, 0, FLAGS, "width_type"},
    {"width", "set band-width", OFFSET(width), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0, 999, FLAGS},
    {"w",     "set band-width", OFFSET(width), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0, 999, FLAGS},
    {"gain", "set gain", OFFSET(gain), AV_OPT_TYPE_DOUBLE, {.dbl=0}, -900, 900, FLAGS},
    {"g",    "set gain", OFFSET(gain), AV_OPT_TYPE_DOUBLE, {.dbl=0}, -900, 900, FLAGS},
    {NULL}
};

DEFINE_BIQUAD_FILTER(equalizer, "Apply two-pole peaking equalization (EQ) filter.");
#endif  /* CONFIG_EQUALIZER_FILTER */
#if CONFIG_BASS_FILTER
static const AVOption bass_options[] = {
    {"frequency", "set central frequency", OFFSET(frequency), AV_OPT_TYPE_DOUBLE, {.dbl=100}, 0, 999999, FLAGS},
    {"f",         "set central frequency", OFFSET(frequency), AV_OPT_TYPE_DOUBLE, {.dbl=100}, 0, 999999, FLAGS},
    {"width_type", "set filter-width type", OFFSET(width_type), AV_OPT_TYPE_INT, {.i64=QFACTOR}, HERTZ, SLOPE, FLAGS, "width_type"},
    {"h", "Hz", 0, AV_OPT_TYPE_CONST, {.i64=HERTZ}, 0, 0, FLAGS, "width_type"},
    {"q", "Q-Factor", 0, AV_OPT_TYPE_CONST, {.i64=QFACTOR}, 0, 0, FLAGS, "width_type"},
    {"o", "octave", 0, AV_OPT_TYPE_CONST, {.i64=OCTAVE}, 0, 0, FLAGS, "width_type"},
    {"s", "slope", 0, AV_OPT_TYPE_CONST, {.i64=SLOPE}, 0, 0, FLAGS, "width_type"},
    {"width", "set shelf transition steep", OFFSET(width), AV_OPT_TYPE_DOUBLE, {.dbl=0.5}, 0, 99999, FLAGS},
    {"w",     "set shelf transition steep", OFFSET(width), AV_OPT_TYPE_DOUBLE, {.dbl=0.5}, 0, 99999, FLAGS},
    {"gain", "set gain", OFFSET(gain), AV_OPT_TYPE_DOUBLE, {.dbl=0}, -900, 900, FLAGS},
    {"g",    "set gain", OFFSET(gain), AV_OPT_TYPE_DOUBLE, {.dbl=0}, -900, 900, FLAGS},
    {NULL}
};

DEFINE_BIQUAD_FILTER(bass, "Boost or cut lower frequencies.");
#endif  /* CONFIG_BASS_FILTER */
#if CONFIG_TREBLE_FILTER
static const AVOption treble_options[] = {
    {"frequency", "set central frequency", OFFSET(frequency), AV_OPT_TYPE_DOUBLE, {.dbl=3000}, 0, 999999, FLAGS},
    {"f",         "set central frequency", OFFSET(frequency), AV_OPT_TYPE_DOUBLE, {.dbl=3000}, 0, 999999, FLAGS},
    {"width_type", "set filter-width type", OFFSET(width_type), AV_OPT_TYPE_INT, {.i64=QFACTOR}, HERTZ, SLOPE, FLAGS, "width_type"},
    {"h", "Hz", 0, AV_OPT_TYPE_CONST, {.i64=HERTZ}, 0, 0, FLAGS, "width_type"},
    {"q", "Q-Factor", 0, AV_OPT_TYPE_CONST, {.i64=QFACTOR}, 0, 0, FLAGS, "width_type"},
    {"o", "octave", 0, AV_OPT_TYPE_CONST, {.i64=OCTAVE}, 0, 0, FLAGS, "width_type"},
    {"s", "slope", 0, AV_OPT_TYPE_CONST, {.i64=SLOPE}, 0, 0, FLAGS, "width_type"},
    {"width", "set shelf transition steep", OFFSET(width), AV_OPT_TYPE_DOUBLE, {.dbl=0.5}, 0, 99999, FLAGS},
    {"w",     "set shelf transition steep", OFFSET(width), AV_OPT_TYPE_DOUBLE, {.dbl=0.5}, 0, 99999, FLAGS},
    {"gain", "set gain", OFFSET(gain), AV_OPT_TYPE_DOUBLE, {.dbl=0}, -900, 900, FLAGS},
    {"g",    "set gain", OFFSET(gain), AV_OPT_TYPE_DOUBLE, {.dbl=0}, -900, 900, FLAGS},
    {NULL}
};

DEFINE_BIQUAD_FILTER(treble, "Boost or cut upper frequencies.");
#endif  /* CONFIG_TREBLE_FILTER */
#if CONFIG_BANDPASS_FILTER
static const AVOption bandpass_options[] = {
    {"frequency", "set central frequency", OFFSET(frequency), AV_OPT_TYPE_DOUBLE, {.dbl=3000}, 0, 999999, FLAGS},
    {"f",         "set central frequency", OFFSET(frequency), AV_OPT_TYPE_DOUBLE, {.dbl=3000}, 0, 999999, FLAGS},
    {"width_type", "set filter-width type", OFFSET(width_type), AV_OPT_TYPE_INT, {.i64=QFACTOR}, HERTZ, SLOPE, FLAGS, "width_type"},
    {"h", "Hz", 0, AV_OPT_TYPE_CONST, {.i64=HERTZ}, 0, 0, FLAGS, "width_type"},
    {"q", "Q-Factor", 0, AV_OPT_TYPE_CONST, {.i64=QFACTOR}, 0, 0, FLAGS, "width_type"},
    {"o", "octave", 0, AV_OPT_TYPE_CONST, {.i64=OCTAVE}, 0, 0, FLAGS, "width_type"},
    {"s", "slope", 0, AV_OPT_TYPE_CONST, {.i64=SLOPE}, 0, 0, FLAGS, "width_type"},
    {"width", "set band-width", OFFSET(width), AV_OPT_TYPE_DOUBLE, {.dbl=0.5}, 0, 999, FLAGS},
    {"w",     "set band-width", OFFSET(width), AV_OPT_TYPE_DOUBLE, {.dbl=0.5}, 0, 999, FLAGS},
    {"csg",   "use constant skirt gain", OFFSET(csg), AV_OPT_TYPE_INT, {.i64=0}, 0, 1, FLAGS},
    {NULL}
};

DEFINE_BIQUAD_FILTER(bandpass, "Apply a two-pole Butterworth band-pass filter.");
#endif  /* CONFIG_BANDPASS_FILTER */
#if CONFIG_BANDREJECT_FILTER
static const AVOption bandreject_options[] = {
    {"frequency", "set central frequency", OFFSET(frequency), AV_OPT_TYPE_DOUBLE, {.dbl=3000}, 0, 999999, FLAGS},
    {"f",         "set central frequency", OFFSET(frequency), AV_OPT_TYPE_DOUBLE, {.dbl=3000}, 0, 999999, FLAGS},
    {"width_type", "set filter-width type", OFFSET(width_type), AV_OPT_TYPE_INT, {.i64=QFACTOR}, HERTZ, SLOPE, FLAGS, "width_type"},
    {"h", "Hz", 0, AV_OPT_TYPE_CONST, {.i64=HERTZ}, 0, 0, FLAGS, "width_type"},
    {"q", "Q-Factor", 0, AV_OPT_TYPE_CONST, {.i64=QFACTOR}, 0, 0, FLAGS, "width_type"},
    {"o", "octave", 0, AV_OPT_TYPE_CONST, {.i64=OCTAVE}, 0, 0, FLAGS, "width_type"},
    {"s", "slope", 0, AV_OPT_TYPE_CONST, {.i64=SLOPE}, 0, 0, FLAGS, "width_type"},
    {"width", "set band-width", OFFSET(width), AV_OPT_TYPE_DOUBLE, {.dbl=0.5}, 0, 999, FLAGS},
    {"w",     "set band-width", OFFSET(width), AV_OPT_TYPE_DOUBLE, {.dbl=0.5}, 0, 999, FLAGS},
    {NULL}
};

DEFINE_BIQUAD_FILTER(bandreject, "Apply a two-pole Butterworth band-reject filter.");
#endif  /* CONFIG_BANDREJECT_FILTER */
#if CONFIG_LOWPASS_FILTER
static const AVOption lowpass_options[] = {
    {"frequency", "set frequency", OFFSET(frequency), AV_OPT_TYPE_DOUBLE, {.dbl=500}, 0, 999999, FLAGS},
    {"f",         "set frequency", OFFSET(frequency), AV_OPT_TYPE_DOUBLE, {.dbl=500}, 0, 999999, FLAGS},
    {"width_type", "set filter-width type", OFFSET(width_type), AV_OPT_TYPE_INT, {.i64=QFACTOR}, HERTZ, SLOPE, FLAGS, "width_type"},
    {"h", "Hz", 0, AV_OPT_TYPE_CONST, {.i64=HERTZ}, 0, 0, FLAGS, "width_type"},
    {"q", "Q-Factor", 0, AV_OPT_TYPE_CONST, {.i64=QFACTOR}, 0, 0, FLAGS, "width_type"},
    {"o", "octave", 0, AV_OPT_TYPE_CONST, {.i64=OCTAVE}, 0, 0, FLAGS, "width_type"},
    {"s", "slope", 0, AV_OPT_TYPE_CONST, {.i64=SLOPE}, 0, 0, FLAGS, "width_type"},
    {"width", "set width", OFFSET(width), AV_OPT_TYPE_DOUBLE, {.dbl=0.707}, 0, 99999, FLAGS},
    {"w",     "set width", OFFSET(width), AV_OPT_TYPE_DOUBLE, {.dbl=0.707}, 0, 99999, FLAGS},
    {"poles", "set number of poles", OFFSET(poles), AV_OPT_TYPE_INT, {.i64=2}, 1, 2, FLAGS},
    {"p",     "set number of poles", OFFSET(poles), AV_OPT_TYPE_INT, {.i64=2}, 1, 2, FLAGS},
    {NULL}
};

DEFINE_BIQUAD_FILTER(lowpass, "Apply a low-pass filter with 3dB point frequency.");
#endif  /* CONFIG_LOWPASS_FILTER */
#if CONFIG_HIGHPASS_FILTER
static const AVOption highpass_options[] = {
    {"frequency", "set frequency", OFFSET(frequency), AV_OPT_TYPE_DOUBLE, {.dbl=3000}, 0, 999999, FLAGS},
    {"f",         "set frequency", OFFSET(frequency), AV_OPT_TYPE_DOUBLE, {.dbl=3000}, 0, 999999, FLAGS},
    {"width_type", "set filter-width type", OFFSET(width_type), AV_OPT_TYPE_INT, {.i64=QFACTOR}, HERTZ, SLOPE, FLAGS, "width_type"},
    {"h", "Hz", 0, AV_OPT_TYPE_CONST, {.i64=HERTZ}, 0, 0, FLAGS, "width_type"},
    {"q", "Q-Factor", 0, AV_OPT_TYPE_CONST, {.i64=QFACTOR}, 0, 0, FLAGS, "width_type"},
    {"o", "octave", 0, AV_OPT_TYPE_CONST, {.i64=OCTAVE}, 0, 0, FLAGS, "width_type"},
    {"s", "slope", 0, AV_OPT_TYPE_CONST, {.i64=SLOPE}, 0, 0, FLAGS, "width_type"},
    {"width", "set width", OFFSET(width), AV_OPT_TYPE_DOUBLE, {.dbl=0.707}, 0, 99999, FLAGS},
    {"w",     "set width", OFFSET(width), AV_OPT_TYPE_DOUBLE, {.dbl=0.707}, 0, 99999, FLAGS},
    {"poles", "set number of poles", OFFSET(poles), AV_OPT_TYPE_INT, {.i64=2}, 1, 2, FLAGS},
    {"p",     "set number of poles", OFFSET(poles), AV_OPT_TYPE_INT, {.i64=2}, 1, 2, FLAGS},
    {NULL}
};

DEFINE_BIQUAD_FILTER(highpass, "Apply a high-pass filter with 3dB point frequency.");
#endif  /* CONFIG_HIGHPASS_FILTER */
#if CONFIG_ALLPASS_FILTER
static const AVOption allpass_options[] = {
    {"frequency", "set central frequency", OFFSET(frequency), AV_OPT_TYPE_DOUBLE, {.dbl=3000}, 0, 999999, FLAGS},
    {"f",         "set central frequency", OFFSET(frequency), AV_OPT_TYPE_DOUBLE, {.dbl=3000}, 0, 999999, FLAGS},
    {"width_type", "set filter-width type", OFFSET(width_type), AV_OPT_TYPE_INT, {.i64=HERTZ}, HERTZ, SLOPE, FLAGS, "width_type"},
    {"h", "Hz", 0, AV_OPT_TYPE_CONST, {.i64=HERTZ}, 0, 0, FLAGS, "width_type"},
    {"q", "Q-Factor", 0, AV_OPT_TYPE_CONST, {.i64=QFACTOR}, 0, 0, FLAGS, "width_type"},
    {"o", "octave", 0, AV_OPT_TYPE_CONST, {.i64=OCTAVE}, 0, 0, FLAGS, "width_type"},
    {"s", "slope", 0, AV_OPT_TYPE_CONST, {.i64=SLOPE}, 0, 0, FLAGS, "width_type"},
    {"width", "set filter-width", OFFSET(width), AV_OPT_TYPE_DOUBLE, {.dbl=707.1}, 0, 99999, FLAGS},
    {"w",     "set filter-width", OFFSET(width), AV_OPT_TYPE_DOUBLE, {.dbl=707.1}, 0, 99999, FLAGS},
    {NULL}
};

DEFINE_BIQUAD_FILTER(allpass, "Apply a two-pole all-pass filter.");
#endif  /* CONFIG_ALLPASS_FILTER */
#if CONFIG_BIQUAD_FILTER
static const AVOption biquad_options[] = {
    {"a0", NULL, OFFSET(a0), AV_OPT_TYPE_DOUBLE, {.dbl=1}, INT16_MIN, INT16_MAX, FLAGS},
    {"a1", NULL, OFFSET(a1), AV_OPT_TYPE_DOUBLE, {.dbl=1}, INT16_MIN, INT16_MAX, FLAGS},
    {"a2", NULL, OFFSET(a2), AV_OPT_TYPE_DOUBLE, {.dbl=1}, INT16_MIN, INT16_MAX, FLAGS},
    {"b0", NULL, OFFSET(b0), AV_OPT_TYPE_DOUBLE, {.dbl=1}, INT16_MIN, INT16_MAX, FLAGS},
    {"b1", NULL, OFFSET(b1), AV_OPT_TYPE_DOUBLE, {.dbl=1}, INT16_MIN, INT16_MAX, FLAGS},
    {"b2", NULL, OFFSET(b2), AV_OPT_TYPE_DOUBLE, {.dbl=1}, INT16_MIN, INT16_MAX, FLAGS},
    {NULL}
};

DEFINE_BIQUAD_FILTER(biquad, "Apply a biquad IIR filter with the given coefficients.");
#endif  /* CONFIG_BIQUAD_FILTER */
