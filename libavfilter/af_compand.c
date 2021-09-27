/*
 * Copyright (c) 1999 Chris Bagwell
 * Copyright (c) 1999 Nick Bailey
 * Copyright (c) 2007 Rob Sykes <robs@users.sourceforge.net>
 * Copyright (c) 2013 Paul B Mahol
 * Copyright (c) 2014 Andrew Kelley
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
 * audio compand filter
 */

#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/ffmath.h"
#include "libavutil/opt.h"
#include "libavutil/samplefmt.h"
#include "audio.h"
#include "avfilter.h"
#include "internal.h"

typedef struct ChanParam {
    double attack;
    double decay;
    double volume;
} ChanParam;

typedef struct CompandSegment {
    double x, y;
    double a, b;
} CompandSegment;

typedef struct CompandContext {
    const AVClass *class;
    int nb_segments;
    char *attacks, *decays, *points;
    CompandSegment *segments;
    ChanParam *channels;
    double in_min_lin;
    double out_min_lin;
    double curve_dB;
    double gain_dB;
    double initial_volume;
    double delay;
    AVFrame *delay_frame;
    int delay_samples;
    int delay_count;
    int delay_index;
    int64_t pts;

    int (*compand)(AVFilterContext *ctx, AVFrame *frame);
} CompandContext;

#define OFFSET(x) offsetof(CompandContext, x)
#define A AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption compand_options[] = {
    { "attacks", "set time over which increase of volume is determined", OFFSET(attacks), AV_OPT_TYPE_STRING, { .str = "0" }, 0, 0, A },
    { "decays", "set time over which decrease of volume is determined", OFFSET(decays), AV_OPT_TYPE_STRING, { .str = "0.8" }, 0, 0, A },
    { "points", "set points of transfer function", OFFSET(points), AV_OPT_TYPE_STRING, { .str = "-70/-70|-60/-20|1/0" }, 0, 0, A },
    { "soft-knee", "set soft-knee", OFFSET(curve_dB), AV_OPT_TYPE_DOUBLE, { .dbl = 0.01 }, 0.01, 900, A },
    { "gain", "set output gain", OFFSET(gain_dB), AV_OPT_TYPE_DOUBLE, { .dbl = 0 }, -900, 900, A },
    { "volume", "set initial volume", OFFSET(initial_volume), AV_OPT_TYPE_DOUBLE, { .dbl = 0 }, -900, 0, A },
    { "delay", "set delay for samples before sending them to volume adjuster", OFFSET(delay), AV_OPT_TYPE_DOUBLE, { .dbl = 0 }, 0, 20, A },
    { NULL }
};

AVFILTER_DEFINE_CLASS(compand);

static av_cold int init(AVFilterContext *ctx)
{
    CompandContext *s = ctx->priv;
    s->pts            = AV_NOPTS_VALUE;
    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    CompandContext *s = ctx->priv;

    av_freep(&s->channels);
    av_freep(&s->segments);
    av_frame_free(&s->delay_frame);
}

static void count_items(char *item_str, int *nb_items)
{
    char *p;

    *nb_items = 1;
    for (p = item_str; *p; p++) {
        if (*p == ' ' || *p == '|')
            (*nb_items)++;
    }
}

static void update_volume(ChanParam *cp, double in)
{
    double delta = in - cp->volume;

    if (delta > 0.0)
        cp->volume += delta * cp->attack;
    else
        cp->volume += delta * cp->decay;
}

static double get_volume(CompandContext *s, double in_lin)
{
    CompandSegment *cs;
    double in_log, out_log;
    int i;

    if (in_lin < s->in_min_lin)
        return s->out_min_lin;

    in_log = log(in_lin);

    for (i = 1; i < s->nb_segments; i++)
        if (in_log <= s->segments[i].x)
            break;
    cs = &s->segments[i - 1];
    in_log -= cs->x;
    out_log = cs->y + in_log * (cs->a * in_log + cs->b);

    return exp(out_log);
}

static int compand_nodelay(AVFilterContext *ctx, AVFrame *frame)
{
    CompandContext *s    = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    const int channels   = inlink->channels;
    const int nb_samples = frame->nb_samples;
    AVFrame *out_frame;
    int chan, i;
    int err;

    if (av_frame_is_writable(frame)) {
        out_frame = frame;
    } else {
        out_frame = ff_get_audio_buffer(ctx->outputs[0], nb_samples);
        if (!out_frame) {
            av_frame_free(&frame);
            return AVERROR(ENOMEM);
        }
        err = av_frame_copy_props(out_frame, frame);
        if (err < 0) {
            av_frame_free(&out_frame);
            av_frame_free(&frame);
            return err;
        }
    }

    for (chan = 0; chan < channels; chan++) {
        const double *src = (double *)frame->extended_data[chan];
        double *dst = (double *)out_frame->extended_data[chan];
        ChanParam *cp = &s->channels[chan];

        for (i = 0; i < nb_samples; i++) {
            update_volume(cp, fabs(src[i]));

            dst[i] = src[i] * get_volume(s, cp->volume);
        }
    }

    if (frame != out_frame)
        av_frame_free(&frame);

    return ff_filter_frame(ctx->outputs[0], out_frame);
}

#define MOD(a, b) (((a) >= (b)) ? (a) - (b) : (a))

static int compand_delay(AVFilterContext *ctx, AVFrame *frame)
{
    CompandContext *s    = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    const int channels = inlink->channels;
    const int nb_samples = frame->nb_samples;
    int chan, i, av_uninit(dindex), oindex, av_uninit(count);
    AVFrame *out_frame   = NULL;
    int err;

    if (s->pts == AV_NOPTS_VALUE) {
        s->pts = (frame->pts == AV_NOPTS_VALUE) ? 0 : frame->pts;
    }

    av_assert1(channels > 0); /* would corrupt delay_count and delay_index */

    for (chan = 0; chan < channels; chan++) {
        AVFrame *delay_frame = s->delay_frame;
        const double *src    = (double *)frame->extended_data[chan];
        double *dbuf         = (double *)delay_frame->extended_data[chan];
        ChanParam *cp        = &s->channels[chan];
        double *dst;

        count  = s->delay_count;
        dindex = s->delay_index;
        for (i = 0, oindex = 0; i < nb_samples; i++) {
            const double in = src[i];
            update_volume(cp, fabs(in));

            if (count >= s->delay_samples) {
                if (!out_frame) {
                    out_frame = ff_get_audio_buffer(ctx->outputs[0], nb_samples - i);
                    if (!out_frame) {
                        av_frame_free(&frame);
                        return AVERROR(ENOMEM);
                    }
                    err = av_frame_copy_props(out_frame, frame);
                    if (err < 0) {
                        av_frame_free(&out_frame);
                        av_frame_free(&frame);
                        return err;
                    }
                    out_frame->pts = s->pts;
                    s->pts += av_rescale_q(nb_samples - i,
                        (AVRational){ 1, inlink->sample_rate },
                        inlink->time_base);
                }

                dst = (double *)out_frame->extended_data[chan];
                dst[oindex++] = dbuf[dindex] * get_volume(s, cp->volume);
            } else {
                count++;
            }

            dbuf[dindex] = in;
            dindex = MOD(dindex + 1, s->delay_samples);
        }
    }

    s->delay_count = count;
    s->delay_index = dindex;

    av_frame_free(&frame);

    if (out_frame) {
        err = ff_filter_frame(ctx->outputs[0], out_frame);
        return err;
    }

    return 0;
}

static int compand_drain(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    CompandContext *s    = ctx->priv;
    const int channels   = outlink->channels;
    AVFrame *frame       = NULL;
    int chan, i, dindex;

    /* 2048 is to limit output frame size during drain */
    frame = ff_get_audio_buffer(outlink, FFMIN(2048, s->delay_count));
    if (!frame)
        return AVERROR(ENOMEM);
    frame->pts = s->pts;
    s->pts += av_rescale_q(frame->nb_samples,
            (AVRational){ 1, outlink->sample_rate }, outlink->time_base);

    av_assert0(channels > 0);
    for (chan = 0; chan < channels; chan++) {
        AVFrame *delay_frame = s->delay_frame;
        double *dbuf = (double *)delay_frame->extended_data[chan];
        double *dst = (double *)frame->extended_data[chan];
        ChanParam *cp = &s->channels[chan];

        dindex = s->delay_index;
        for (i = 0; i < frame->nb_samples; i++) {
            dst[i] = dbuf[dindex] * get_volume(s, cp->volume);
            dindex = MOD(dindex + 1, s->delay_samples);
        }
    }
    s->delay_count -= frame->nb_samples;
    s->delay_index = dindex;

    return ff_filter_frame(outlink, frame);
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx  = outlink->src;
    CompandContext *s     = ctx->priv;
    const int sample_rate = outlink->sample_rate;
    double radius         = s->curve_dB * M_LN10 / 20.0;
    char *p, *saveptr     = NULL;
    const int channels    = outlink->channels;
    int nb_attacks, nb_decays, nb_points;
    int new_nb_items, num;
    int i;
    int err;


    count_items(s->attacks, &nb_attacks);
    count_items(s->decays, &nb_decays);
    count_items(s->points, &nb_points);

    if (channels <= 0) {
        av_log(ctx, AV_LOG_ERROR, "Invalid number of channels: %d\n", channels);
        return AVERROR(EINVAL);
    }

    if (nb_attacks > channels || nb_decays > channels) {
        av_log(ctx, AV_LOG_WARNING,
                "Number of attacks/decays bigger than number of channels. Ignoring rest of entries.\n");
        nb_attacks = FFMIN(nb_attacks, channels);
        nb_decays  = FFMIN(nb_decays, channels);
    }

    uninit(ctx);

    s->channels = av_calloc(channels, sizeof(*s->channels));
    s->nb_segments = (nb_points + 4) * 2;
    s->segments = av_calloc(s->nb_segments, sizeof(*s->segments));

    if (!s->channels || !s->segments) {
        uninit(ctx);
        return AVERROR(ENOMEM);
    }

    p = s->attacks;
    for (i = 0, new_nb_items = 0; i < nb_attacks; i++) {
        char *tstr = av_strtok(p, " |", &saveptr);
        if (!tstr) {
            uninit(ctx);
            return AVERROR(EINVAL);
        }
        p = NULL;
        new_nb_items += sscanf(tstr, "%lf", &s->channels[i].attack) == 1;
        if (s->channels[i].attack < 0) {
            uninit(ctx);
            return AVERROR(EINVAL);
        }
    }
    nb_attacks = new_nb_items;

    p = s->decays;
    for (i = 0, new_nb_items = 0; i < nb_decays; i++) {
        char *tstr = av_strtok(p, " |", &saveptr);
        if (!tstr) {
            uninit(ctx);
            return AVERROR(EINVAL);
        }
        p = NULL;
        new_nb_items += sscanf(tstr, "%lf", &s->channels[i].decay) == 1;
        if (s->channels[i].decay < 0) {
            uninit(ctx);
            return AVERROR(EINVAL);
        }
    }
    nb_decays = new_nb_items;

    if (nb_attacks != nb_decays) {
        av_log(ctx, AV_LOG_ERROR,
                "Number of attacks %d differs from number of decays %d.\n",
                nb_attacks, nb_decays);
        uninit(ctx);
        return AVERROR(EINVAL);
    }

    for (i = nb_decays; i < channels; i++) {
        s->channels[i].attack = s->channels[nb_decays - 1].attack;
        s->channels[i].decay = s->channels[nb_decays - 1].decay;
    }

#define S(x) s->segments[2 * ((x) + 1)]
    p = s->points;
    for (i = 0, new_nb_items = 0; i < nb_points; i++) {
        char *tstr = av_strtok(p, " |", &saveptr);
        p = NULL;
        if (!tstr || sscanf(tstr, "%lf/%lf", &S(i).x, &S(i).y) != 2) {
            av_log(ctx, AV_LOG_ERROR,
                    "Invalid and/or missing input/output value.\n");
            uninit(ctx);
            return AVERROR(EINVAL);
        }
        if (i && S(i - 1).x > S(i).x) {
            av_log(ctx, AV_LOG_ERROR,
                    "Transfer function input values must be increasing.\n");
            uninit(ctx);
            return AVERROR(EINVAL);
        }
        S(i).y -= S(i).x;
        av_log(ctx, AV_LOG_DEBUG, "%d: x=%f y=%f\n", i, S(i).x, S(i).y);
        new_nb_items++;
    }
    num = new_nb_items;

    /* Add 0,0 if necessary */
    if (num == 0 || S(num - 1).x)
        num++;

#undef S
#define S(x) s->segments[2 * (x)]
    /* Add a tail off segment at the start */
    S(0).x = S(1).x - 2 * s->curve_dB;
    S(0).y = S(1).y;
    num++;

    /* Join adjacent colinear segments */
    for (i = 2; i < num; i++) {
        double g1 = (S(i - 1).y - S(i - 2).y) * (S(i - 0).x - S(i - 1).x);
        double g2 = (S(i - 0).y - S(i - 1).y) * (S(i - 1).x - S(i - 2).x);
        int j;

        if (fabs(g1 - g2))
            continue;
        num--;
        for (j = --i; j < num; j++)
            S(j) = S(j + 1);
    }

    for (i = 0; i < s->nb_segments; i += 2) {
        s->segments[i].y += s->gain_dB;
        s->segments[i].x *= M_LN10 / 20;
        s->segments[i].y *= M_LN10 / 20;
    }

#define L(x) s->segments[i - (x)]
    for (i = 4; i < s->nb_segments; i += 2) {
        double x, y, cx, cy, in1, in2, out1, out2, theta, len, r;

        L(4).a = 0;
        L(4).b = (L(2).y - L(4).y) / (L(2).x - L(4).x);

        L(2).a = 0;
        L(2).b = (L(0).y - L(2).y) / (L(0).x - L(2).x);

        theta = atan2(L(2).y - L(4).y, L(2).x - L(4).x);
        len = hypot(L(2).x - L(4).x, L(2).y - L(4).y);
        r = FFMIN(radius, len);
        L(3).x = L(2).x - r * cos(theta);
        L(3).y = L(2).y - r * sin(theta);

        theta = atan2(L(0).y - L(2).y, L(0).x - L(2).x);
        len = hypot(L(0).x - L(2).x, L(0).y - L(2).y);
        r = FFMIN(radius, len / 2);
        x = L(2).x + r * cos(theta);
        y = L(2).y + r * sin(theta);

        cx = (L(3).x + L(2).x + x) / 3;
        cy = (L(3).y + L(2).y + y) / 3;

        L(2).x = x;
        L(2).y = y;

        in1  = cx - L(3).x;
        out1 = cy - L(3).y;
        in2  = L(2).x - L(3).x;
        out2 = L(2).y - L(3).y;
        L(3).a = (out2 / in2 - out1 / in1) / (in2 - in1);
        L(3).b = out1 / in1 - L(3).a * in1;
    }
    L(3).x = 0;
    L(3).y = L(2).y;

    s->in_min_lin  = exp(s->segments[1].x);
    s->out_min_lin = exp(s->segments[1].y);

    for (i = 0; i < channels; i++) {
        ChanParam *cp = &s->channels[i];

        if (cp->attack > 1.0 / sample_rate)
            cp->attack = 1.0 - exp(-1.0 / (sample_rate * cp->attack));
        else
            cp->attack = 1.0;
        if (cp->decay > 1.0 / sample_rate)
            cp->decay = 1.0 - exp(-1.0 / (sample_rate * cp->decay));
        else
            cp->decay = 1.0;
        cp->volume = ff_exp10(s->initial_volume / 20);
    }

    s->delay_samples = s->delay * sample_rate;
    if (s->delay_samples <= 0) {
        s->compand = compand_nodelay;
        return 0;
    }

    s->delay_frame = av_frame_alloc();
    if (!s->delay_frame) {
        uninit(ctx);
        return AVERROR(ENOMEM);
    }

    s->delay_frame->format         = outlink->format;
    s->delay_frame->nb_samples     = s->delay_samples;
    s->delay_frame->channel_layout = outlink->channel_layout;

    err = av_frame_get_buffer(s->delay_frame, 0);
    if (err)
        return err;

    s->compand = compand_delay;
    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    AVFilterContext *ctx = inlink->dst;
    CompandContext *s    = ctx->priv;

    return s->compand(ctx, frame);
}

static int request_frame(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    CompandContext *s    = ctx->priv;
    int ret = 0;

    ret = ff_request_frame(ctx->inputs[0]);

    if (ret == AVERROR_EOF && !ctx->is_disabled && s->delay_count)
        ret = compand_drain(outlink);

    return ret;
}

static const AVFilterPad compand_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = filter_frame,
    },
};

static const AVFilterPad compand_outputs[] = {
    {
        .name          = "default",
        .request_frame = request_frame,
        .config_props  = config_output,
        .type          = AVMEDIA_TYPE_AUDIO,
    },
};


const AVFilter ff_af_compand = {
    .name           = "compand",
    .description    = NULL_IF_CONFIG_SMALL(
            "Compress or expand audio dynamic range."),
    .priv_size      = sizeof(CompandContext),
    .priv_class     = &compand_class,
    .init           = init,
    .uninit         = uninit,
    FILTER_INPUTS(compand_inputs),
    FILTER_OUTPUTS(compand_outputs),
    FILTER_SINGLE_SAMPLEFMT(AV_SAMPLE_FMT_DBLP),
};
