/*
 * Copyright (C) 2001-2010 Krzysztof Foltman, Markus Schmidt, Thor Harald Johansen and others
 * Copyright (c) 2015 Paul B Mahol
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
 * Lookahead limiter filter
 */

#include "libavutil/avassert.h"
#include "libavutil/channel_layout.h"
#include "libavutil/common.h"
#include "libavutil/opt.h"

#include "audio.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"

typedef struct AudioLimiterContext {
    const AVClass *class;

    double limit;
    double attack;
    double release;
    double att;
    double level_in;
    double level_out;
    int auto_release;
    int auto_level;
    double asc;
    int asc_c;
    int asc_pos;
    double asc_coeff;

    double *buffer;
    int buffer_size;
    int pos;
    int *nextpos;
    double *nextdelta;

    double delta;
    int nextiter;
    int nextlen;
    int asc_changed;
} AudioLimiterContext;

#define OFFSET(x) offsetof(AudioLimiterContext, x)
#define A AV_OPT_FLAG_AUDIO_PARAM
#define F AV_OPT_FLAG_FILTERING_PARAM

static const AVOption alimiter_options[] = {
    { "level_in",  "set input level",  OFFSET(level_in),     AV_OPT_TYPE_DOUBLE, {.dbl=1},.015625,   64, A|F },
    { "level_out", "set output level", OFFSET(level_out),    AV_OPT_TYPE_DOUBLE, {.dbl=1},.015625,   64, A|F },
    { "limit",     "set limit",        OFFSET(limit),        AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0.0625,    1, A|F },
    { "attack",    "set attack",       OFFSET(attack),       AV_OPT_TYPE_DOUBLE, {.dbl=5},    0.1,   80, A|F },
    { "release",   "set release",      OFFSET(release),      AV_OPT_TYPE_DOUBLE, {.dbl=50},     1, 8000, A|F },
    { "asc",       "enable asc",       OFFSET(auto_release), AV_OPT_TYPE_BOOL,   {.i64=0},      0,    1, A|F },
    { "asc_level", "set asc level",    OFFSET(asc_coeff),    AV_OPT_TYPE_DOUBLE, {.dbl=0.5},    0,    1, A|F },
    { "level",     "auto level",       OFFSET(auto_level),   AV_OPT_TYPE_BOOL,   {.i64=1},      0,    1, A|F },
    { NULL }
};

AVFILTER_DEFINE_CLASS(alimiter);

static av_cold int init(AVFilterContext *ctx)
{
    AudioLimiterContext *s = ctx->priv;

    s->attack   /= 1000.;
    s->release  /= 1000.;
    s->att       = 1.;
    s->asc_pos   = -1;
    s->asc_coeff = pow(0.5, s->asc_coeff - 0.5) * 2 * -1;

    return 0;
}

static double get_rdelta(AudioLimiterContext *s, double release, int sample_rate,
                         double peak, double limit, double patt, int asc)
{
    double rdelta = (1.0 - patt) / (sample_rate * release);

    if (asc && s->auto_release && s->asc_c > 0) {
        double a_att = limit / (s->asc_coeff * s->asc) * (double)s->asc_c;

        if (a_att > patt) {
            double delta = FFMAX((a_att - patt) / (sample_rate * release), rdelta / 10);

            if (delta < rdelta)
                rdelta = delta;
        }
    }

    return rdelta;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AudioLimiterContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    const double *src = (const double *)in->data[0];
    const int channels = inlink->channels;
    const int buffer_size = s->buffer_size;
    double *dst, *buffer = s->buffer;
    const double release = s->release;
    const double limit = s->limit;
    double *nextdelta = s->nextdelta;
    double level = s->auto_level ? 1 / limit : 1;
    const double level_out = s->level_out;
    const double level_in = s->level_in;
    int *nextpos = s->nextpos;
    AVFrame *out;
    double *buf;
    int n, c, i;

    if (av_frame_is_writable(in)) {
        out = in;
    } else {
        out = ff_get_audio_buffer(inlink, in->nb_samples);
        if (!out) {
            av_frame_free(&in);
            return AVERROR(ENOMEM);
        }
        av_frame_copy_props(out, in);
    }
    dst = (double *)out->data[0];

    for (n = 0; n < in->nb_samples; n++) {
        double peak = 0;

        for (c = 0; c < channels; c++) {
            double sample = src[c] * level_in;

            buffer[s->pos + c] = sample;
            peak = FFMAX(peak, fabs(sample));
        }

        if (s->auto_release && peak > limit) {
            s->asc += peak;
            s->asc_c++;
        }

        if (peak > limit) {
            double patt = FFMIN(limit / peak, 1.);
            double rdelta = get_rdelta(s, release, inlink->sample_rate,
                                       peak, limit, patt, 0);
            double delta = (limit / peak - s->att) / buffer_size * channels;
            int found = 0;

            if (delta < s->delta) {
                s->delta = delta;
                nextpos[0] = s->pos;
                nextpos[1] = -1;
                nextdelta[0] = rdelta;
                s->nextlen = 1;
                s->nextiter= 0;
            } else {
                for (i = s->nextiter; i < s->nextiter + s->nextlen; i++) {
                    int j = i % buffer_size;
                    double ppeak, pdelta;

                    ppeak = fabs(buffer[nextpos[j]]) > fabs(buffer[nextpos[j] + 1]) ?
                            fabs(buffer[nextpos[j]]) : fabs(buffer[nextpos[j] + 1]);
                    pdelta = (limit / peak - limit / ppeak) / (((buffer_size - nextpos[j] + s->pos) % buffer_size) / channels);
                    if (pdelta < nextdelta[j]) {
                        nextdelta[j] = pdelta;
                        found = 1;
                        break;
                    }
                }
                if (found) {
                    s->nextlen = i - s->nextiter + 1;
                    nextpos[(s->nextiter + s->nextlen) % buffer_size] = s->pos;
                    nextdelta[(s->nextiter + s->nextlen) % buffer_size] = rdelta;
                    nextpos[(s->nextiter + s->nextlen + 1) % buffer_size] = -1;
                    s->nextlen++;
                }
            }
        }

        buf = &s->buffer[(s->pos + channels) % buffer_size];
        peak = 0;
        for (c = 0; c < channels; c++) {
            double sample = buf[c];

            peak = FFMAX(peak, fabs(sample));
        }

        if (s->pos == s->asc_pos && !s->asc_changed)
            s->asc_pos = -1;

        if (s->auto_release && s->asc_pos == -1 && peak > limit) {
            s->asc -= peak;
            s->asc_c--;
        }

        s->att += s->delta;

        for (c = 0; c < channels; c++)
            dst[c] = buf[c] * s->att;

        if ((s->pos + channels) % buffer_size == nextpos[s->nextiter]) {
            if (s->auto_release) {
                s->delta = get_rdelta(s, release, inlink->sample_rate,
                                      peak, limit, s->att, 1);
                if (s->nextlen > 1) {
                    int pnextpos = nextpos[(s->nextiter + 1) % buffer_size];
                    double ppeak = fabs(buffer[pnextpos]) > fabs(buffer[pnextpos + 1]) ?
                                                            fabs(buffer[pnextpos]) :
                                                            fabs(buffer[pnextpos + 1]);
                    double pdelta = (limit / ppeak - s->att) /
                                    (((buffer_size + pnextpos -
                                    ((s->pos + channels) % buffer_size)) %
                                    buffer_size) / channels);
                    if (pdelta < s->delta)
                        s->delta = pdelta;
                }
            } else {
                s->delta = nextdelta[s->nextiter];
                s->att = limit / peak;
            }

            s->nextlen -= 1;
            nextpos[s->nextiter] = -1;
            s->nextiter = (s->nextiter + 1) % buffer_size;
        }

        if (s->att > 1.) {
            s->att = 1.;
            s->delta = 0.;
            s->nextiter = 0;
            s->nextlen = 0;
            nextpos[0] = -1;
        }

        if (s->att <= 0.) {
            s->att = 0.0000000000001;
            s->delta = (1.0 - s->att) / (inlink->sample_rate * release);
        }

        if (s->att != 1. && (1. - s->att) < 0.0000000000001)
            s->att = 1.;

        if (s->delta != 0. && fabs(s->delta) < 0.00000000000001)
            s->delta = 0.;

        for (c = 0; c < channels; c++)
            dst[c] = av_clipd(dst[c], -limit, limit) * level * level_out;

        s->pos = (s->pos + channels) % buffer_size;
        src += channels;
        dst += channels;
    }

    if (in != out)
        av_frame_free(&in);

    return ff_filter_frame(outlink, out);
}

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *formats;
    AVFilterChannelLayouts *layouts;
    static const enum AVSampleFormat sample_fmts[] = {
        AV_SAMPLE_FMT_DBL,
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

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    AudioLimiterContext *s = ctx->priv;
    int obuffer_size;

    obuffer_size = inlink->sample_rate * inlink->channels * 100 / 1000. + inlink->channels;
    if (obuffer_size < inlink->channels)
        return AVERROR(EINVAL);

    s->buffer = av_calloc(obuffer_size, sizeof(*s->buffer));
    s->nextdelta = av_calloc(obuffer_size, sizeof(*s->nextdelta));
    s->nextpos = av_malloc_array(obuffer_size, sizeof(*s->nextpos));
    if (!s->buffer || !s->nextdelta || !s->nextpos)
        return AVERROR(ENOMEM);

    memset(s->nextpos, -1, obuffer_size * sizeof(*s->nextpos));
    s->buffer_size = inlink->sample_rate * s->attack * inlink->channels;
    s->buffer_size -= s->buffer_size % inlink->channels;

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    AudioLimiterContext *s = ctx->priv;

    av_freep(&s->buffer);
    av_freep(&s->nextdelta);
    av_freep(&s->nextpos);
}

static const AVFilterPad alimiter_inputs[] = {
    {
        .name         = "main",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = filter_frame,
        .config_props = config_input,
    },
    { NULL }
};

static const AVFilterPad alimiter_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_AUDIO,
    },
    { NULL }
};

AVFilter ff_af_alimiter = {
    .name           = "alimiter",
    .description    = NULL_IF_CONFIG_SMALL("Audio lookahead limiter."),
    .priv_size      = sizeof(AudioLimiterContext),
    .priv_class     = &alimiter_class,
    .init           = init,
    .uninit         = uninit,
    .query_formats  = query_formats,
    .inputs         = alimiter_inputs,
    .outputs        = alimiter_outputs,
};
