/*
 * Copyright (c) 2019 The FFmpeg Project
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

#include "libavutil/avassert.h"
#include "libavutil/channel_layout.h"
#include "libavutil/opt.h"
#include "libswresample/swresample.h"
#include "avfilter.h"
#include "audio.h"
#include "formats.h"

enum ASoftClipTypes {
    ASC_HARD = -1,
    ASC_TANH,
    ASC_ATAN,
    ASC_CUBIC,
    ASC_EXP,
    ASC_ALG,
    ASC_QUINTIC,
    ASC_SIN,
    ASC_ERF,
    NB_TYPES,
};

typedef struct ASoftClipContext {
    const AVClass *class;

    int type;
    int oversample;
    int64_t delay;
    double threshold;
    double output;
    double param;

    SwrContext *up_ctx;
    SwrContext *down_ctx;

    AVFrame *frame;

    void (*filter)(struct ASoftClipContext *s, void **dst, const void **src,
                   int nb_samples, int channels, int start, int end);
} ASoftClipContext;

#define OFFSET(x) offsetof(ASoftClipContext, x)
#define A AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_RUNTIME_PARAM
#define F AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption asoftclip_options[] = {
    { "type", "set softclip type", OFFSET(type), AV_OPT_TYPE_INT,    {.i64=0},         -1, NB_TYPES-1, A, "types" },
    { "hard",                NULL,            0, AV_OPT_TYPE_CONST,  {.i64=ASC_HARD},   0,          0, A, "types" },
    { "tanh",                NULL,            0, AV_OPT_TYPE_CONST,  {.i64=ASC_TANH},   0,          0, A, "types" },
    { "atan",                NULL,            0, AV_OPT_TYPE_CONST,  {.i64=ASC_ATAN},   0,          0, A, "types" },
    { "cubic",               NULL,            0, AV_OPT_TYPE_CONST,  {.i64=ASC_CUBIC},  0,          0, A, "types" },
    { "exp",                 NULL,            0, AV_OPT_TYPE_CONST,  {.i64=ASC_EXP},    0,          0, A, "types" },
    { "alg",                 NULL,            0, AV_OPT_TYPE_CONST,  {.i64=ASC_ALG},    0,          0, A, "types" },
    { "quintic",             NULL,            0, AV_OPT_TYPE_CONST,  {.i64=ASC_QUINTIC},0,          0, A, "types" },
    { "sin",                 NULL,            0, AV_OPT_TYPE_CONST,  {.i64=ASC_SIN},    0,          0, A, "types" },
    { "erf",                 NULL,            0, AV_OPT_TYPE_CONST,  {.i64=ASC_ERF},    0,          0, A, "types" },
    { "threshold", "set softclip threshold", OFFSET(threshold), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0.000001, 1, A },
    { "output", "set softclip output gain", OFFSET(output), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0.000001, 16, A },
    { "param", "set softclip parameter", OFFSET(param), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0.01,        3, A },
    { "oversample", "set oversample factor", OFFSET(oversample), AV_OPT_TYPE_INT, {.i64=1}, 1, 32, F },
    { NULL }
};

AVFILTER_DEFINE_CLASS(asoftclip);

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *formats = NULL;
    AVFilterChannelLayouts *layouts = NULL;
    static const enum AVSampleFormat sample_fmts[] = {
        AV_SAMPLE_FMT_FLT, AV_SAMPLE_FMT_FLTP,
        AV_SAMPLE_FMT_DBL, AV_SAMPLE_FMT_DBLP,
        AV_SAMPLE_FMT_NONE
    };
    int ret;

    formats = ff_make_format_list(sample_fmts);
    if (!formats)
        return AVERROR(ENOMEM);
    ret = ff_set_common_formats(ctx, formats);
    if (ret < 0)
        return ret;

    layouts = ff_all_channel_counts();
    if (!layouts)
        return AVERROR(ENOMEM);

    ret = ff_set_common_channel_layouts(ctx, layouts);
    if (ret < 0)
        return ret;

    formats = ff_all_samplerates();
    return ff_set_common_samplerates(ctx, formats);
}

static void filter_flt(ASoftClipContext *s,
                       void **dptr, const void **sptr,
                       int nb_samples, int channels,
                       int start, int end)
{
    float threshold = s->threshold;
    float gain = s->output * threshold;
    float factor = 1.f / threshold;
    float param = s->param;

    for (int c = start; c < end; c++) {
        const float *src = sptr[c];
        float *dst = dptr[c];

        switch (s->type) {
        case ASC_HARD:
            for (int n = 0; n < nb_samples; n++) {
                dst[n] = av_clipf(src[n] * factor, -1.f, 1.f);
                dst[n] *= gain;
            }
            break;
        case ASC_TANH:
            for (int n = 0; n < nb_samples; n++) {
                dst[n] = tanhf(src[n] * factor * param);
                dst[n] *= gain;
            }
            break;
        case ASC_ATAN:
            for (int n = 0; n < nb_samples; n++) {
                dst[n] = 2.f / M_PI * atanf(src[n] * factor * param);
                dst[n] *= gain;
            }
            break;
        case ASC_CUBIC:
            for (int n = 0; n < nb_samples; n++) {
                float sample = src[n] * factor;

                if (FFABS(sample) >= 1.5f)
                    dst[n] = FFSIGN(sample);
                else
                    dst[n] = sample - 0.1481f * powf(sample, 3.f);
                dst[n] *= gain;
            }
            break;
        case ASC_EXP:
            for (int n = 0; n < nb_samples; n++) {
                dst[n] = 2.f / (1.f + expf(-2.f * src[n] * factor)) - 1.;
                dst[n] *= gain;
            }
            break;
        case ASC_ALG:
            for (int n = 0; n < nb_samples; n++) {
                float sample = src[n] * factor;

                dst[n] = sample / (sqrtf(param + sample * sample));
                dst[n] *= gain;
            }
            break;
        case ASC_QUINTIC:
            for (int n = 0; n < nb_samples; n++) {
                float sample = src[n] * factor;

                if (FFABS(sample) >= 1.25)
                    dst[n] = FFSIGN(sample);
                else
                    dst[n] = sample - 0.08192f * powf(sample, 5.f);
                dst[n] *= gain;
            }
            break;
        case ASC_SIN:
            for (int n = 0; n < nb_samples; n++) {
                float sample = src[n] * factor;

                if (FFABS(sample) >= M_PI_2)
                    dst[n] = FFSIGN(sample);
                else
                    dst[n] = sinf(sample);
                dst[n] *= gain;
            }
            break;
        case ASC_ERF:
            for (int n = 0; n < nb_samples; n++) {
                dst[n] = erff(src[n] * factor);
                dst[n] *= gain;
            }
            break;
        default:
            av_assert0(0);
        }
    }
}

static void filter_dbl(ASoftClipContext *s,
                       void **dptr, const void **sptr,
                       int nb_samples, int channels,
                       int start, int end)
{
    double threshold = s->threshold;
    double gain = s->output * threshold;
    double factor = 1. / threshold;
    double param = s->param;

    for (int c = start; c < end; c++) {
        const double *src = sptr[c];
        double *dst = dptr[c];

        switch (s->type) {
        case ASC_HARD:
            for (int n = 0; n < nb_samples; n++) {
                dst[n] = av_clipd(src[n] * factor, -1., 1.);
                dst[n] *= gain;
            }
            break;
        case ASC_TANH:
            for (int n = 0; n < nb_samples; n++) {
                dst[n] = tanh(src[n] * factor * param);
                dst[n] *= gain;
            }
            break;
        case ASC_ATAN:
            for (int n = 0; n < nb_samples; n++) {
                dst[n] = 2. / M_PI * atan(src[n] * factor * param);
                dst[n] *= gain;
            }
            break;
        case ASC_CUBIC:
            for (int n = 0; n < nb_samples; n++) {
                double sample = src[n] * factor;

                if (FFABS(sample) >= 1.5)
                    dst[n] = FFSIGN(sample);
                else
                    dst[n] = sample - 0.1481 * pow(sample, 3.);
                dst[n] *= gain;
            }
            break;
        case ASC_EXP:
            for (int n = 0; n < nb_samples; n++) {
                dst[n] = 2. / (1. + exp(-2. * src[n] * factor)) - 1.;
                dst[n] *= gain;
            }
            break;
        case ASC_ALG:
            for (int n = 0; n < nb_samples; n++) {
                double sample = src[n] * factor;

                dst[n] = sample / (sqrt(param + sample * sample));
                dst[n] *= gain;
            }
            break;
        case ASC_QUINTIC:
            for (int n = 0; n < nb_samples; n++) {
                double sample = src[n] * factor;

                if (FFABS(sample) >= 1.25)
                    dst[n] = FFSIGN(sample);
                else
                    dst[n] = sample - 0.08192 * pow(sample, 5.);
                dst[n] *= gain;
            }
            break;
        case ASC_SIN:
            for (int n = 0; n < nb_samples; n++) {
                double sample = src[n] * factor;

                if (FFABS(sample) >= M_PI_2)
                    dst[n] = FFSIGN(sample);
                else
                    dst[n] = sin(sample);
                dst[n] *= gain;
            }
            break;
        case ASC_ERF:
            for (int n = 0; n < nb_samples; n++) {
                dst[n] = erf(src[n] * factor);
                dst[n] *= gain;
            }
            break;
        default:
            av_assert0(0);
        }
    }
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    ASoftClipContext *s = ctx->priv;
    int ret;

    switch (inlink->format) {
    case AV_SAMPLE_FMT_FLT:
    case AV_SAMPLE_FMT_FLTP: s->filter = filter_flt; break;
    case AV_SAMPLE_FMT_DBL:
    case AV_SAMPLE_FMT_DBLP: s->filter = filter_dbl; break;
    default: av_assert0(0);
    }

    if (s->oversample <= 1)
        return 0;

    s->up_ctx = swr_alloc();
    s->down_ctx = swr_alloc();
    if (!s->up_ctx || !s->down_ctx)
        return AVERROR(ENOMEM);

    av_opt_set_int(s->up_ctx, "in_channel_layout",    inlink->channel_layout, 0);
    av_opt_set_int(s->up_ctx, "in_sample_rate",       inlink->sample_rate, 0);
    av_opt_set_sample_fmt(s->up_ctx, "in_sample_fmt", inlink->format, 0);

    av_opt_set_int(s->up_ctx, "out_channel_layout",    inlink->channel_layout, 0);
    av_opt_set_int(s->up_ctx, "out_sample_rate",       inlink->sample_rate * s->oversample, 0);
    av_opt_set_sample_fmt(s->up_ctx, "out_sample_fmt", inlink->format, 0);

    av_opt_set_int(s->down_ctx, "in_channel_layout",    inlink->channel_layout, 0);
    av_opt_set_int(s->down_ctx, "in_sample_rate",       inlink->sample_rate * s->oversample, 0);
    av_opt_set_sample_fmt(s->down_ctx, "in_sample_fmt", inlink->format, 0);

    av_opt_set_int(s->down_ctx, "out_channel_layout",    inlink->channel_layout, 0);
    av_opt_set_int(s->down_ctx, "out_sample_rate",       inlink->sample_rate, 0);
    av_opt_set_sample_fmt(s->down_ctx, "out_sample_fmt", inlink->format, 0);

    ret = swr_init(s->up_ctx);
    if (ret < 0)
        return ret;

    ret = swr_init(s->down_ctx);
    if (ret < 0)
        return ret;

    return 0;
}

typedef struct ThreadData {
    AVFrame *in, *out;
    int nb_samples;
    int channels;
} ThreadData;

static int filter_channels(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ASoftClipContext *s = ctx->priv;
    ThreadData *td = arg;
    AVFrame *out = td->out;
    AVFrame *in = td->in;
    const int channels = td->channels;
    const int nb_samples = td->nb_samples;
    const int start = (channels * jobnr) / nb_jobs;
    const int end = (channels * (jobnr+1)) / nb_jobs;

    s->filter(s, (void **)out->extended_data, (const void **)in->extended_data,
              nb_samples, channels, start, end);

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    ASoftClipContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    int ret, nb_samples, channels;
    ThreadData td;
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

    if (av_sample_fmt_is_planar(in->format)) {
        nb_samples = in->nb_samples;
        channels = in->channels;
    } else {
        nb_samples = in->channels * in->nb_samples;
        channels = 1;
    }

    if (s->oversample > 1) {
        s->frame = ff_get_audio_buffer(outlink, in->nb_samples * s->oversample);
        if (!s->frame) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        ret = swr_convert(s->up_ctx, (uint8_t**)s->frame->extended_data, in->nb_samples * s->oversample,
                          (const uint8_t **)in->extended_data, in->nb_samples);
        if (ret < 0)
            goto fail;

        td.in = s->frame;
        td.out = s->frame;
        td.nb_samples = av_sample_fmt_is_planar(in->format) ? ret : ret * in->channels;
        td.channels = channels;
        ctx->internal->execute(ctx, filter_channels, &td, NULL, FFMIN(channels,
                                                                ff_filter_get_nb_threads(ctx)));

        ret = swr_convert(s->down_ctx, (uint8_t**)out->extended_data, out->nb_samples,
                          (const uint8_t **)s->frame->extended_data, ret);
        if (ret < 0)
            goto fail;

        if (out->pts)
            out->pts -= s->delay;
        s->delay += in->nb_samples - ret;
        out->nb_samples = ret;

        av_frame_free(&s->frame);
    } else {
        td.in = in;
        td.out = out;
        td.nb_samples = nb_samples;
        td.channels = channels;
        ctx->internal->execute(ctx, filter_channels, &td, NULL, FFMIN(channels,
                                                                ff_filter_get_nb_threads(ctx)));
    }

    if (out != in)
        av_frame_free(&in);

    return ff_filter_frame(outlink, out);
fail:
    if (out != in)
        av_frame_free(&out);
    av_frame_free(&in);
    av_frame_free(&s->frame);

    return ret;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    ASoftClipContext *s = ctx->priv;

    swr_free(&s->up_ctx);
    swr_free(&s->down_ctx);
}

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = filter_frame,
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

AVFilter ff_af_asoftclip = {
    .name           = "asoftclip",
    .description    = NULL_IF_CONFIG_SMALL("Audio Soft Clipper."),
    .query_formats  = query_formats,
    .priv_size      = sizeof(ASoftClipContext),
    .priv_class     = &asoftclip_class,
    .inputs         = inputs,
    .outputs        = outputs,
    .uninit         = uninit,
    .process_command = ff_filter_process_command,
    .flags          = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC |
                      AVFILTER_FLAG_SLICE_THREADS,
};
