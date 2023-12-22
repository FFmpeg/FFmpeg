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

#include "libavutil/channel_layout.h"
#include "libavutil/common.h"
#include "libavutil/fifo.h"
#include "libavutil/opt.h"

#include "audio.h"
#include "avfilter.h"
#include "internal.h"

typedef struct MetaItem {
    int64_t pts;
    int nb_samples;
} MetaItem;

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

    int in_trim;
    int out_pad;
    int64_t next_in_pts;
    int64_t next_out_pts;
    int latency;

    AVFifo *fifo;

    double delta;
    int nextiter;
    int nextlen;
    int asc_changed;
} AudioLimiterContext;

#define OFFSET(x) offsetof(AudioLimiterContext, x)
#define AF AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption alimiter_options[] = {
    { "level_in",  "set input level",  OFFSET(level_in),     AV_OPT_TYPE_DOUBLE, {.dbl=1},.015625,   64, AF },
    { "level_out", "set output level", OFFSET(level_out),    AV_OPT_TYPE_DOUBLE, {.dbl=1},.015625,   64, AF },
    { "limit",     "set limit",        OFFSET(limit),        AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0.0625,    1, AF },
    { "attack",    "set attack",       OFFSET(attack),       AV_OPT_TYPE_DOUBLE, {.dbl=5},    0.1,   80, AF },
    { "release",   "set release",      OFFSET(release),      AV_OPT_TYPE_DOUBLE, {.dbl=50},     1, 8000, AF },
    { "asc",       "enable asc",       OFFSET(auto_release), AV_OPT_TYPE_BOOL,   {.i64=0},      0,    1, AF },
    { "asc_level", "set asc level",    OFFSET(asc_coeff),    AV_OPT_TYPE_DOUBLE, {.dbl=0.5},    0,    1, AF },
    { "level",     "auto level",       OFFSET(auto_level),   AV_OPT_TYPE_BOOL,   {.i64=1},      0,    1, AF },
    { "latency",   "compensate delay", OFFSET(latency),      AV_OPT_TYPE_BOOL,   {.i64=0},      0,    1, AF },
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
    const int channels = inlink->ch_layout.nb_channels;
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
    int new_out_samples;
    int64_t out_duration;
    int64_t in_duration;
    int64_t in_pts;
    MetaItem meta;

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
                    double ppeak = 0, pdelta;

                    if (nextpos[j] >= 0)
                        for (c = 0; c < channels; c++) {
                            ppeak = FFMAX(ppeak, fabs(buffer[nextpos[j] + c]));
                        }
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
                    double ppeak = 0, pdelta;
                    int pnextpos = nextpos[(s->nextiter + 1) % buffer_size];

                    for (c = 0; c < channels; c++) {
                        ppeak = FFMAX(ppeak, fabs(buffer[pnextpos + c]));
                    }
                    pdelta = (limit / ppeak - s->att) /
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

    in_duration = av_rescale_q(in->nb_samples,  inlink->time_base, av_make_q(1,  in->sample_rate));
    in_pts = in->pts;
    meta = (MetaItem){ in->pts, in->nb_samples };
    av_fifo_write(s->fifo, &meta, 1);
    if (in != out)
        av_frame_free(&in);

    new_out_samples = out->nb_samples;
    if (s->in_trim > 0) {
        int trim = FFMIN(new_out_samples, s->in_trim);
        new_out_samples -= trim;
        s->in_trim -= trim;
    }

    if (new_out_samples <= 0) {
        av_frame_free(&out);
        return 0;
    } else if (new_out_samples < out->nb_samples) {
        int offset = out->nb_samples - new_out_samples;
        memmove(out->extended_data[0], out->extended_data[0] + sizeof(double) * offset * out->ch_layout.nb_channels,
                sizeof(double) * new_out_samples * out->ch_layout.nb_channels);
        out->nb_samples = new_out_samples;
        s->in_trim = 0;
    }

    av_fifo_read(s->fifo, &meta, 1);

    out_duration = av_rescale_q(out->nb_samples, inlink->time_base, av_make_q(1, out->sample_rate));
    in_duration  = av_rescale_q(meta.nb_samples, inlink->time_base, av_make_q(1, out->sample_rate));
    in_pts       = meta.pts;

    if (s->next_out_pts != AV_NOPTS_VALUE && out->pts != s->next_out_pts &&
        s->next_in_pts  != AV_NOPTS_VALUE && in_pts   == s->next_in_pts) {
        out->pts = s->next_out_pts;
    } else {
        out->pts = in_pts;
    }
    s->next_in_pts  = in_pts   + in_duration;
    s->next_out_pts = out->pts + out_duration;

    return ff_filter_frame(outlink, out);
}

static int request_frame(AVFilterLink* outlink)
{
    AVFilterContext *ctx = outlink->src;
    AudioLimiterContext *s = (AudioLimiterContext*)ctx->priv;
    int ret;

    ret = ff_request_frame(ctx->inputs[0]);

    if (ret == AVERROR_EOF && s->out_pad > 0) {
        AVFrame *frame = ff_get_audio_buffer(outlink, FFMIN(1024, s->out_pad));
        if (!frame)
            return AVERROR(ENOMEM);

        s->out_pad -= frame->nb_samples;
        frame->pts = s->next_in_pts;
        return filter_frame(ctx->inputs[0], frame);
    }
    return ret;
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    AudioLimiterContext *s = ctx->priv;
    int obuffer_size;

    obuffer_size = inlink->sample_rate * inlink->ch_layout.nb_channels * 100 / 1000. + inlink->ch_layout.nb_channels;
    if (obuffer_size < inlink->ch_layout.nb_channels)
        return AVERROR(EINVAL);

    s->buffer = av_calloc(obuffer_size, sizeof(*s->buffer));
    s->nextdelta = av_calloc(obuffer_size, sizeof(*s->nextdelta));
    s->nextpos = av_malloc_array(obuffer_size, sizeof(*s->nextpos));
    if (!s->buffer || !s->nextdelta || !s->nextpos)
        return AVERROR(ENOMEM);

    memset(s->nextpos, -1, obuffer_size * sizeof(*s->nextpos));
    s->buffer_size = inlink->sample_rate * s->attack * inlink->ch_layout.nb_channels;
    s->buffer_size -= s->buffer_size % inlink->ch_layout.nb_channels;
    if (s->latency)
        s->in_trim = s->out_pad = s->buffer_size / inlink->ch_layout.nb_channels - 1;
    s->next_out_pts = AV_NOPTS_VALUE;
    s->next_in_pts  = AV_NOPTS_VALUE;

    s->fifo = av_fifo_alloc2(8, sizeof(MetaItem), AV_FIFO_FLAG_AUTO_GROW);
    if (!s->fifo) {
        return AVERROR(ENOMEM);
    }

    if (s->buffer_size <= 0) {
        av_log(ctx, AV_LOG_ERROR, "Attack is too small.\n");
        return AVERROR(EINVAL);
    }

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    AudioLimiterContext *s = ctx->priv;

    av_freep(&s->buffer);
    av_freep(&s->nextdelta);
    av_freep(&s->nextpos);

    av_fifo_freep2(&s->fifo);
}

static const AVFilterPad alimiter_inputs[] = {
    {
        .name         = "main",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = filter_frame,
        .config_props = config_input,
    },
};

static const AVFilterPad alimiter_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_AUDIO,
        .request_frame = request_frame,
    },
};

const AVFilter ff_af_alimiter = {
    .name           = "alimiter",
    .description    = NULL_IF_CONFIG_SMALL("Audio lookahead limiter."),
    .priv_size      = sizeof(AudioLimiterContext),
    .priv_class     = &alimiter_class,
    .init           = init,
    .uninit         = uninit,
    FILTER_INPUTS(alimiter_inputs),
    FILTER_OUTPUTS(alimiter_outputs),
    FILTER_SINGLE_SAMPLEFMT(AV_SAMPLE_FMT_DBL),
    .process_command = ff_filter_process_command,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
