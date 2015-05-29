/*
 * Copyright (c) 2006 Rob Sykes <robs@users.sourceforge.net>
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
#include "libavutil/opt.h"
#include "libavutil/samplefmt.h"
#include "avfilter.h"
#include "audio.h"
#include "internal.h"
#include "generate_wave_table.h"

#define INTERPOLATION_LINEAR    0
#define INTERPOLATION_QUADRATIC 1

typedef struct FlangerContext {
    const AVClass *class;
    double delay_min;
    double delay_depth;
    double feedback_gain;
    double delay_gain;
    double speed;
    int wave_shape;
    double channel_phase;
    int interpolation;
    double in_gain;
    int max_samples;
    uint8_t **delay_buffer;
    int delay_buf_pos;
    double *delay_last;
    float *lfo;
    int lfo_length;
    int lfo_pos;
} FlangerContext;

#define OFFSET(x) offsetof(FlangerContext, x)
#define A AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption flanger_options[] = {
    { "delay", "base delay in milliseconds",        OFFSET(delay_min),   AV_OPT_TYPE_DOUBLE, {.dbl=0}, 0, 30, A },
    { "depth", "added swept delay in milliseconds", OFFSET(delay_depth), AV_OPT_TYPE_DOUBLE, {.dbl=2}, 0, 10, A },
    { "regen", "percentage regeneration (delayed signal feedback)", OFFSET(feedback_gain), AV_OPT_TYPE_DOUBLE, {.dbl=0}, -95, 95, A },
    { "width", "percentage of delayed signal mixed with original", OFFSET(delay_gain), AV_OPT_TYPE_DOUBLE, {.dbl=71}, 0, 100, A },
    { "speed", "sweeps per second (Hz)", OFFSET(speed), AV_OPT_TYPE_DOUBLE, {.dbl=0.5}, 0.1, 10, A },
    { "shape", "swept wave shape", OFFSET(wave_shape), AV_OPT_TYPE_INT, {.i64=WAVE_SIN}, WAVE_SIN, WAVE_NB-1, A, "type" },
    { "triangular",  NULL, 0, AV_OPT_TYPE_CONST,  {.i64=WAVE_TRI}, 0, 0, A, "type" },
    { "t",           NULL, 0, AV_OPT_TYPE_CONST,  {.i64=WAVE_TRI}, 0, 0, A, "type" },
    { "sinusoidal",  NULL, 0, AV_OPT_TYPE_CONST,  {.i64=WAVE_SIN}, 0, 0, A, "type" },
    { "s",           NULL, 0, AV_OPT_TYPE_CONST,  {.i64=WAVE_SIN}, 0, 0, A, "type" },
    { "phase", "swept wave percentage phase-shift for multi-channel", OFFSET(channel_phase), AV_OPT_TYPE_DOUBLE, {.dbl=25}, 0, 100, A },
    { "interp", "delay-line interpolation", OFFSET(interpolation), AV_OPT_TYPE_INT, {.i64=0}, 0, 1, A, "itype" },
    { "linear",     NULL, 0, AV_OPT_TYPE_CONST,  {.i64=INTERPOLATION_LINEAR},    0, 0, A, "itype" },
    { "quadratic",  NULL, 0, AV_OPT_TYPE_CONST,  {.i64=INTERPOLATION_QUADRATIC}, 0, 0, A, "itype" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(flanger);

static int init(AVFilterContext *ctx)
{
    FlangerContext *s = ctx->priv;

    s->feedback_gain /= 100;
    s->delay_gain    /= 100;
    s->channel_phase /= 100;
    s->delay_min     /= 1000;
    s->delay_depth   /= 1000;
    s->in_gain        = 1 / (1 + s->delay_gain);
    s->delay_gain    /= 1 + s->delay_gain;
    s->delay_gain    *= 1 - fabs(s->feedback_gain);

    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    AVFilterChannelLayouts *layouts;
    AVFilterFormats *formats;
    static const enum AVSampleFormat sample_fmts[] = {
        AV_SAMPLE_FMT_DBLP, AV_SAMPLE_FMT_NONE
    };
    int ret;

    layouts = ff_all_channel_layouts();
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
    FlangerContext *s = ctx->priv;

    s->max_samples = (s->delay_min + s->delay_depth) * inlink->sample_rate + 2.5;
    s->lfo_length  = inlink->sample_rate / s->speed;
    s->delay_last  = av_calloc(inlink->channels, sizeof(*s->delay_last));
    s->lfo         = av_calloc(s->lfo_length, sizeof(*s->lfo));
    if (!s->lfo || !s->delay_last)
        return AVERROR(ENOMEM);

    ff_generate_wave_table(s->wave_shape, AV_SAMPLE_FMT_FLT, s->lfo, s->lfo_length,
                           floor(s->delay_min * inlink->sample_rate + 0.5),
                           s->max_samples - 2., 3 * M_PI_2);

    return av_samples_alloc_array_and_samples(&s->delay_buffer, NULL,
                                              inlink->channels, s->max_samples,
                                              inlink->format, 0);
}

static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    AVFilterContext *ctx = inlink->dst;
    FlangerContext *s = ctx->priv;
    AVFrame *out_frame;
    int chan, i;

    if (av_frame_is_writable(frame)) {
        out_frame = frame;
    } else {
        out_frame = ff_get_audio_buffer(inlink, frame->nb_samples);
        if (!out_frame)
            return AVERROR(ENOMEM);
        av_frame_copy_props(out_frame, frame);
    }

    for (i = 0; i < frame->nb_samples; i++) {

        s->delay_buf_pos = (s->delay_buf_pos + s->max_samples - 1) % s->max_samples;

        for (chan = 0; chan < inlink->channels; chan++) {
            double *src = (double *)frame->extended_data[chan];
            double *dst = (double *)out_frame->extended_data[chan];
            double delayed_0, delayed_1;
            double delayed;
            double in, out;
            int channel_phase = chan * s->lfo_length * s->channel_phase + .5;
            double delay = s->lfo[(s->lfo_pos + channel_phase) % s->lfo_length];
            int int_delay = (int)delay;
            double frac_delay = modf(delay, &delay);
            double *delay_buffer = (double *)s->delay_buffer[chan];

            in = src[i];
            delay_buffer[s->delay_buf_pos] = in + s->delay_last[chan] *
                                                           s->feedback_gain;
            delayed_0 = delay_buffer[(s->delay_buf_pos + int_delay++) % s->max_samples];
            delayed_1 = delay_buffer[(s->delay_buf_pos + int_delay++) % s->max_samples];

            if (s->interpolation == INTERPOLATION_LINEAR) {
                delayed = delayed_0 + (delayed_1 - delayed_0) * frac_delay;
            } else {
                double a, b;
                double delayed_2 = delay_buffer[(s->delay_buf_pos + int_delay++) % s->max_samples];
                delayed_2 -= delayed_0;
                delayed_1 -= delayed_0;
                a = delayed_2 * .5 - delayed_1;
                b = delayed_1 *  2 - delayed_2 *.5;
                delayed = delayed_0 + (a * frac_delay + b) * frac_delay;
            }

            s->delay_last[chan] = delayed;
            out = in * s->in_gain + delayed * s->delay_gain;
            dst[i] = out;
        }
        s->lfo_pos = (s->lfo_pos + 1) % s->lfo_length;
    }

    if (frame != out_frame)
        av_frame_free(&frame);

    return ff_filter_frame(ctx->outputs[0], out_frame);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    FlangerContext *s = ctx->priv;

    av_freep(&s->lfo);
    av_freep(&s->delay_last);

    if (s->delay_buffer)
        av_freep(&s->delay_buffer[0]);
    av_freep(&s->delay_buffer);
}

static const AVFilterPad flanger_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .config_props = config_input,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad flanger_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_AUDIO,
    },
    { NULL }
};

AVFilter ff_af_flanger = {
    .name          = "flanger",
    .description   = NULL_IF_CONFIG_SMALL("Apply a flanging effect to the audio."),
    .query_formats = query_formats,
    .priv_size     = sizeof(FlangerContext),
    .priv_class    = &flanger_class,
    .init          = init,
    .uninit        = uninit,
    .inputs        = flanger_inputs,
    .outputs       = flanger_outputs,
};
