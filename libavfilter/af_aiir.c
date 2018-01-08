/*
 * Copyright (c) 2018 Paul B Mahol
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

#include <float.h>

#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/opt.h"
#include "audio.h"
#include "avfilter.h"
#include "internal.h"

typedef struct AudioIIRContext {
    const AVClass *class;
    char *a_str, *b_str, *g_str;
    double dry_gain, wet_gain;
    int format;

    int *nb_a, *nb_b;
    double **a, **b;
    double *g;
    double **input, **output;
    int clippings;
    int channels;

    void (*iir_frame)(AVFilterContext *ctx, AVFrame *in, AVFrame *out);
} AudioIIRContext;

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *formats;
    AVFilterChannelLayouts *layouts;
    static const enum AVSampleFormat sample_fmts[] = {
        AV_SAMPLE_FMT_DBLP,
        AV_SAMPLE_FMT_FLTP,
        AV_SAMPLE_FMT_S32P,
        AV_SAMPLE_FMT_S16P,
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

#define IIR_FRAME(name, type, min, max, need_clipping)                  \
static void iir_frame_## name(AVFilterContext *ctx, AVFrame *in, AVFrame *out)  \
{                                                                       \
    AudioIIRContext *s = ctx->priv;                                     \
    const double ig = s->dry_gain;                                      \
    const double og = s->wet_gain;                                      \
    int ch, n;                                                          \
                                                                        \
    for (ch = 0; ch < out->channels; ch++) {                            \
        const type *src = (const type *)in->extended_data[ch];          \
        double *ic = (double *)s->input[ch];                            \
        double *oc = (double *)s->output[ch];                           \
        const int nb_a = s->nb_a[ch];                                   \
        const int nb_b = s->nb_b[ch];                                   \
        const double *a = s->a[ch];                                     \
        const double *b = s->b[ch];                                     \
        type *dst = (type *)out->extended_data[ch];                     \
                                                                        \
        for (n = 0; n < in->nb_samples; n++) {                          \
            double sample = 0.;                                         \
            int x;                                                      \
                                                                        \
            memmove(&ic[1], &ic[0], (nb_b - 1) * sizeof(*ic));          \
            memmove(&oc[1], &oc[0], (nb_a - 1) * sizeof(*oc));          \
            ic[0] = src[n] * ig;                                        \
            for (x = 0; x < nb_b; x++)                                  \
                sample += b[x] * ic[x];                                 \
                                                                        \
            for (x = 1; x < nb_a; x++)                                  \
                sample -= a[x] * oc[x];                                 \
                                                                        \
            oc[0] = sample;                                             \
            sample *= og;                                               \
            if (need_clipping && sample < min) {                        \
                s->clippings++;                                         \
                dst[n] = min;                                           \
            } else if (need_clipping && sample > max) {                 \
                s->clippings++;                                         \
                dst[n] = max;                                           \
            } else {                                                    \
                dst[n] = sample;                                        \
            }                                                           \
        }                                                               \
    }                                                                   \
}

IIR_FRAME(s16p, int16_t, INT16_MIN, INT16_MAX, 1)
IIR_FRAME(s32p, int32_t, INT32_MIN, INT32_MAX, 1)
IIR_FRAME(fltp, float,         -1.,        1., 0)
IIR_FRAME(dblp, double,        -1.,        1., 0)

static void count_coefficients(char *item_str, int *nb_items)
{
    char *p;

    if (!item_str)
        return;

    *nb_items = 1;
    for (p = item_str; *p && *p != '|'; p++) {
        if (*p == ' ')
            (*nb_items)++;
    }
}

static int read_gains(AVFilterContext *ctx, char *item_str, int nb_items, double *dst)
{
    char *p, *arg, *old_str, *prev_arg = NULL, *saveptr = NULL;
    int i;

    p = old_str = av_strdup(item_str);
    if (!p)
        return AVERROR(ENOMEM);
    for (i = 0; i < nb_items; i++) {
        if (!(arg = av_strtok(p, "|", &saveptr)))
            arg = prev_arg;

        if (!arg) {
            av_freep(&old_str);
            return AVERROR(EINVAL);
        }

        p = NULL;
        if (sscanf(arg, "%lf", &dst[i]) != 1) {
            av_log(ctx, AV_LOG_ERROR, "Invalid gains supplied: %s\n", arg);
            av_freep(&old_str);
            return AVERROR(EINVAL);
        }

        prev_arg = arg;
    }

    av_freep(&old_str);

    return 0;
}

static int read_tf_coefficients(AVFilterContext *ctx, char *item_str, int nb_items, double *dst)
{
    char *p, *arg, *old_str, *saveptr = NULL;
    int i;

    p = old_str = av_strdup(item_str);
    if (!p)
        return AVERROR(ENOMEM);
    for (i = 0; i < nb_items; i++) {
        if (!(arg = av_strtok(p, " ", &saveptr)))
            break;

        p = NULL;
        if (sscanf(arg, "%lf", &dst[i]) != 1) {
            av_log(ctx, AV_LOG_ERROR, "Invalid coefficients supplied: %s\n", arg);
            av_freep(&old_str);
            return AVERROR(EINVAL);
        }
    }

    av_freep(&old_str);

    return 0;
}

static int read_zp_coefficients(AVFilterContext *ctx, char *item_str, int nb_items, double *dst)
{
    char *p, *arg, *old_str, *saveptr = NULL;
    int i;

    p = old_str = av_strdup(item_str);
    if (!p)
        return AVERROR(ENOMEM);
    for (i = 0; i < nb_items; i++) {
        if (!(arg = av_strtok(p, " ", &saveptr)))
            break;

        p = NULL;
        if (sscanf(arg, "%lf %lfi", &dst[i*2], &dst[i*2+1]) != 2) {
            av_log(ctx, AV_LOG_ERROR, "Invalid coefficients supplied: %s\n", arg);
            av_freep(&old_str);
            return AVERROR(EINVAL);
        }
    }

    av_freep(&old_str);

    return 0;
}

static int read_channels(AVFilterContext *ctx, int channels, uint8_t *item_str, int *nb, double **c, double **cache)
{
    AudioIIRContext *s = ctx->priv;
    char *p, *arg, *old_str, *prev_arg = NULL, *saveptr = NULL;
    int i, ret;

    p = old_str = av_strdup(item_str);
    if (!p)
        return AVERROR(ENOMEM);
    for (i = 0; i < channels; i++) {
        if (!(arg = av_strtok(p, "|", &saveptr)))
            arg = prev_arg;

        if (!arg) {
            av_freep(&old_str);
            return AVERROR(EINVAL);
        }

        count_coefficients(arg, &nb[i]);

        p = NULL;
        cache[i] = av_calloc(nb[i] + 1, sizeof(double));
        c[i] = av_calloc(nb[i] * (s->format + 1), sizeof(double));
        if (!c[i] || !cache[i]) {
            av_freep(&old_str);
            return AVERROR(ENOMEM);
        }

        if (s->format) {
            ret = read_zp_coefficients(ctx, arg, nb[i], c[i]);
        } else {
            ret = read_tf_coefficients(ctx, arg, nb[i], c[i]);
        }
        if (ret < 0) {
            av_freep(&old_str);
            return ret;
        }
        prev_arg = arg;
    }

    av_freep(&old_str);

    return 0;
}

static void multiply(double wre, double wim, int npz, double *coeffs)
{
    double nwre = -wre, nwim = -wim;
    double cre, cim;
    int i;

    for (i = npz; i >= 1; i--) {
        cre = coeffs[2 * i + 0];
        cim = coeffs[2 * i + 1];

        coeffs[2 * i + 0] = (nwre * cre - nwim * cim) + coeffs[2 * (i - 1) + 0];
        coeffs[2 * i + 1] = (nwre * cim + nwim * cre) + coeffs[2 * (i - 1) + 1];
    }

    cre = coeffs[0];
    cim = coeffs[1];
    coeffs[0] = nwre * cre - nwim * cim;
    coeffs[1] = nwre * cim + nwim * cre;
}

static int expand(AVFilterContext *ctx, double *pz, int nb, double *coeffs)
{
    int i;

    coeffs[0] = 1.0;
    coeffs[1] = 0.0;

    for (i = 0; i < nb; i++) {
        coeffs[2 * (i + 1)    ] = 0.0;
        coeffs[2 * (i + 1) + 1] = 0.0;
    }

    for (i = 0; i < nb; i++)
        multiply(pz[2 * i], pz[2 * i + 1], nb, coeffs);

    for (i = 0; i < nb + 1; i++) {
        if (fabs(coeffs[2 * i + 1]) > DBL_EPSILON) {
            av_log(ctx, AV_LOG_ERROR, "coeff: %lf of z^%d is not real; poles/zeros are not complex conjugates.\n",
                   coeffs[2 * i + 1], i);
            return AVERROR(EINVAL);
        }
    }

    return 0;
}

static int convert_zp2tf(AVFilterContext *ctx, int channels)
{
    AudioIIRContext *s = ctx->priv;
    int ch, i, j, ret;

    for (ch = 0; ch < channels; ch++) {
        double *topc, *botc;

        topc = av_calloc((s->nb_b[ch] + 1) * 2, sizeof(*topc));
        botc = av_calloc((s->nb_a[ch] + 1) * 2, sizeof(*botc));
        if (!topc || !botc)
            return AVERROR(ENOMEM);

        ret = expand(ctx, s->a[ch], s->nb_a[ch], botc);
        if (ret < 0) {
            av_free(topc);
            av_free(botc);
            return ret;
        }

        ret = expand(ctx, s->b[ch], s->nb_b[ch], topc);
        if (ret < 0) {
            av_free(topc);
            av_free(botc);
            return ret;
        }

        for (j = 0, i = s->nb_b[ch]; i >= 0; j++, i--) {
            s->b[ch][j] = topc[2 * i];
        }
        s->nb_b[ch]++;

        for (j = 0, i = s->nb_a[ch]; i >= 0; j++, i--) {
            s->a[ch][j] = botc[2 * i];
        }
        s->nb_a[ch]++;

        av_free(topc);
        av_free(botc);
    }

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AudioIIRContext *s = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    int ch, ret, i;

    s->channels = inlink->channels;
    s->a = av_calloc(inlink->channels, sizeof(*s->a));
    s->b = av_calloc(inlink->channels, sizeof(*s->b));
    s->g = av_calloc(inlink->channels, sizeof(*s->g));
    s->nb_a = av_calloc(inlink->channels, sizeof(*s->nb_a));
    s->nb_b = av_calloc(inlink->channels, sizeof(*s->nb_b));
    s->input = av_calloc(inlink->channels, sizeof(*s->input));
    s->output = av_calloc(inlink->channels, sizeof(*s->output));
    if (!s->a || !s->b || !s->nb_a || !s->nb_b || !s->input || !s->output)
        return AVERROR(ENOMEM);

    ret = read_gains(ctx, s->g_str, inlink->channels, s->g);
    if (ret < 0)
        return ret;

    ret = read_channels(ctx, inlink->channels, s->a_str, s->nb_a, s->a, s->output);
    if (ret < 0)
        return ret;

    ret = read_channels(ctx, inlink->channels, s->b_str, s->nb_b, s->b, s->input);
    if (ret < 0)
        return ret;

    if (s->format) {
        ret = convert_zp2tf(ctx, inlink->channels);
        if (ret < 0)
            return ret;
    }

    for (ch = 0; ch < inlink->channels; ch++) {
        for (i = 1; i < s->nb_a[ch]; i++) {
            s->a[ch][i] /= s->a[ch][0];
        }

        for (i = 0; i < s->nb_b[ch]; i++) {
            s->b[ch][i] *= s->g[ch] / s->a[ch][0];
        }
    }

    switch (inlink->format) {
    case AV_SAMPLE_FMT_DBLP: s->iir_frame = iir_frame_dblp; break;
    case AV_SAMPLE_FMT_FLTP: s->iir_frame = iir_frame_fltp; break;
    case AV_SAMPLE_FMT_S32P: s->iir_frame = iir_frame_s32p; break;
    case AV_SAMPLE_FMT_S16P: s->iir_frame = iir_frame_s16p; break;
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AudioIIRContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out;

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

    s->iir_frame(ctx, in, out);

    if (s->clippings > 0)
        av_log(ctx, AV_LOG_WARNING, "clipping %d times. Please reduce gain.\n", s->clippings);
    s->clippings = 0;

    if (in != out)
        av_frame_free(&in);

    return ff_filter_frame(outlink, out);
}

static av_cold int init(AVFilterContext *ctx)
{
    AudioIIRContext *s = ctx->priv;

    if (!s->a_str || !s->b_str || !s->g_str) {
        av_log(ctx, AV_LOG_ERROR, "Valid coefficients are mandatory.\n");
        return AVERROR(EINVAL);
    }

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    AudioIIRContext *s = ctx->priv;
    int ch;

    if (s->a) {
        for (ch = 0; ch < s->channels; ch++) {
            av_freep(&s->a[ch]);
            av_freep(&s->output[ch]);
        }
    }
    av_freep(&s->a);

    if (s->b) {
        for (ch = 0; ch < s->channels; ch++) {
            av_freep(&s->b[ch]);
            av_freep(&s->input[ch]);
        }
    }
    av_freep(&s->b);

    av_freep(&s->g);

    av_freep(&s->input);
    av_freep(&s->output);

    av_freep(&s->nb_a);
    av_freep(&s->nb_b);
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

#define OFFSET(x) offsetof(AudioIIRContext, x)
#define AF AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption aiir_options[] = {
    { "z", "set B/numerator/zeros coefficients",   OFFSET(b_str),    AV_OPT_TYPE_STRING, {.str="1 1"}, 0, 0, AF },
    { "p", "set A/denominator/poles coefficients", OFFSET(a_str),    AV_OPT_TYPE_STRING, {.str="1 1"}, 0, 0, AF },
    { "k", "set channels gains",                   OFFSET(g_str),    AV_OPT_TYPE_STRING, {.str="1|1"}, 0, 0, AF },
    { "dry", "set dry gain",                       OFFSET(dry_gain), AV_OPT_TYPE_DOUBLE, {.dbl=1},     0, 1, AF },
    { "wet", "set wet gain",                       OFFSET(wet_gain), AV_OPT_TYPE_DOUBLE, {.dbl=1},     0, 1, AF },
    { "f", "set coefficients format",              OFFSET(format),   AV_OPT_TYPE_INT,    {.i64=0},     0, 1, AF, "format" },
    { "tf", "transfer function",                   0,                AV_OPT_TYPE_CONST,  {.i64=0},     0, 0, AF, "format" },
    { "zp", "Z-plane zeros/poles",                 0,                AV_OPT_TYPE_CONST,  {.i64=1},     0, 0, AF, "format" },
    { NULL },
};

AVFILTER_DEFINE_CLASS(aiir);

AVFilter ff_af_aiir = {
    .name          = "aiir",
    .description   = NULL_IF_CONFIG_SMALL("Apply Infinite Impulse Response filter with supplied coefficients."),
    .priv_size     = sizeof(AudioIIRContext),
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = inputs,
    .outputs       = outputs,
    .priv_class    = &aiir_class,
};
