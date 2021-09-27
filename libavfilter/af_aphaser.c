/*
 * Copyright (c) 2013 Paul B Mahol
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
 * phaser audio filter
 */

#include "libavutil/avassert.h"
#include "libavutil/opt.h"
#include "audio.h"
#include "avfilter.h"
#include "internal.h"
#include "generate_wave_table.h"

typedef struct AudioPhaserContext {
    const AVClass *class;
    double in_gain, out_gain;
    double delay;
    double decay;
    double speed;

    int type;

    int delay_buffer_length;
    double *delay_buffer;

    int modulation_buffer_length;
    int32_t *modulation_buffer;

    int delay_pos, modulation_pos;

    void (*phaser)(struct AudioPhaserContext *s,
                   uint8_t * const *src, uint8_t **dst,
                   int nb_samples, int channels);
} AudioPhaserContext;

#define OFFSET(x) offsetof(AudioPhaserContext, x)
#define FLAGS AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption aphaser_options[] = {
    { "in_gain",  "set input gain",            OFFSET(in_gain),  AV_OPT_TYPE_DOUBLE, {.dbl=.4},  0,  1,   FLAGS },
    { "out_gain", "set output gain",           OFFSET(out_gain), AV_OPT_TYPE_DOUBLE, {.dbl=.74}, 0,  1e9, FLAGS },
    { "delay",    "set delay in milliseconds", OFFSET(delay),    AV_OPT_TYPE_DOUBLE, {.dbl=3.},  0,  5,   FLAGS },
    { "decay",    "set decay",                 OFFSET(decay),    AV_OPT_TYPE_DOUBLE, {.dbl=.4},  0, .99,  FLAGS },
    { "speed",    "set modulation speed",      OFFSET(speed),    AV_OPT_TYPE_DOUBLE, {.dbl=.5}, .1,  2,   FLAGS },
    { "type",     "set modulation type",       OFFSET(type),     AV_OPT_TYPE_INT,    {.i64=WAVE_TRI}, 0, WAVE_NB-1, FLAGS, "type" },
    { "triangular",  NULL, 0, AV_OPT_TYPE_CONST,  {.i64=WAVE_TRI}, 0, 0, FLAGS, "type" },
    { "t",           NULL, 0, AV_OPT_TYPE_CONST,  {.i64=WAVE_TRI}, 0, 0, FLAGS, "type" },
    { "sinusoidal",  NULL, 0, AV_OPT_TYPE_CONST,  {.i64=WAVE_SIN}, 0, 0, FLAGS, "type" },
    { "s",           NULL, 0, AV_OPT_TYPE_CONST,  {.i64=WAVE_SIN}, 0, 0, FLAGS, "type" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(aphaser);

static av_cold int init(AVFilterContext *ctx)
{
    AudioPhaserContext *s = ctx->priv;

    if (s->in_gain > (1 - s->decay * s->decay))
        av_log(ctx, AV_LOG_WARNING, "in_gain may cause clipping\n");
    if (s->in_gain / (1 - s->decay) > 1 / s->out_gain)
        av_log(ctx, AV_LOG_WARNING, "out_gain may cause clipping\n");

    return 0;
}

#define MOD(a, b) (((a) >= (b)) ? (a) - (b) : (a))

#define PHASER_PLANAR(name, type)                                      \
static void phaser_## name ##p(AudioPhaserContext *s,                  \
                               uint8_t * const *ssrc, uint8_t **ddst,  \
                               int nb_samples, int channels)           \
{                                                                      \
    int i, c, delay_pos, modulation_pos;                               \
                                                                       \
    av_assert0(channels > 0);                                          \
    for (c = 0; c < channels; c++) {                                   \
        type *src = (type *)ssrc[c];                                   \
        type *dst = (type *)ddst[c];                                   \
        double *buffer = s->delay_buffer +                             \
                         c * s->delay_buffer_length;                   \
                                                                       \
        delay_pos      = s->delay_pos;                                 \
        modulation_pos = s->modulation_pos;                            \
                                                                       \
        for (i = 0; i < nb_samples; i++, src++, dst++) {               \
            double v = *src * s->in_gain + buffer[                     \
                       MOD(delay_pos + s->modulation_buffer[           \
                       modulation_pos],                                \
                       s->delay_buffer_length)] * s->decay;            \
                                                                       \
            modulation_pos = MOD(modulation_pos + 1,                   \
                             s->modulation_buffer_length);             \
            delay_pos = MOD(delay_pos + 1, s->delay_buffer_length);    \
            buffer[delay_pos] = v;                                     \
                                                                       \
            *dst = v * s->out_gain;                                    \
        }                                                              \
    }                                                                  \
                                                                       \
    s->delay_pos      = delay_pos;                                     \
    s->modulation_pos = modulation_pos;                                \
}

#define PHASER(name, type)                                              \
static void phaser_## name (AudioPhaserContext *s,                      \
                            uint8_t * const *ssrc, uint8_t **ddst,      \
                            int nb_samples, int channels)               \
{                                                                       \
    int i, c, delay_pos, modulation_pos;                                \
    type *src = (type *)ssrc[0];                                        \
    type *dst = (type *)ddst[0];                                        \
    double *buffer = s->delay_buffer;                                   \
                                                                        \
    delay_pos      = s->delay_pos;                                      \
    modulation_pos = s->modulation_pos;                                 \
                                                                        \
    for (i = 0; i < nb_samples; i++) {                                  \
        int pos = MOD(delay_pos + s->modulation_buffer[modulation_pos], \
                      s->delay_buffer_length) * channels;               \
        int npos;                                                       \
                                                                        \
        delay_pos = MOD(delay_pos + 1, s->delay_buffer_length);         \
        npos = delay_pos * channels;                                    \
        for (c = 0; c < channels; c++, src++, dst++) {                  \
            double v = *src * s->in_gain + buffer[pos + c] * s->decay;  \
                                                                        \
            buffer[npos + c] = v;                                       \
                                                                        \
            *dst = v * s->out_gain;                                     \
        }                                                               \
                                                                        \
        modulation_pos = MOD(modulation_pos + 1,                        \
                         s->modulation_buffer_length);                  \
    }                                                                   \
                                                                        \
    s->delay_pos      = delay_pos;                                      \
    s->modulation_pos = modulation_pos;                                 \
}

PHASER_PLANAR(dbl, double)
PHASER_PLANAR(flt, float)
PHASER_PLANAR(s16, int16_t)
PHASER_PLANAR(s32, int32_t)

PHASER(dbl, double)
PHASER(flt, float)
PHASER(s16, int16_t)
PHASER(s32, int32_t)

static int config_output(AVFilterLink *outlink)
{
    AudioPhaserContext *s = outlink->src->priv;
    AVFilterLink *inlink = outlink->src->inputs[0];

    s->delay_buffer_length = s->delay * 0.001 * inlink->sample_rate + 0.5;
    if (s->delay_buffer_length <= 0) {
        av_log(outlink->src, AV_LOG_ERROR, "delay is too small\n");
        return AVERROR(EINVAL);
    }
    s->delay_buffer = av_calloc(s->delay_buffer_length, sizeof(*s->delay_buffer) * inlink->channels);
    s->modulation_buffer_length = inlink->sample_rate / s->speed + 0.5;
    s->modulation_buffer = av_malloc_array(s->modulation_buffer_length, sizeof(*s->modulation_buffer));

    if (!s->modulation_buffer || !s->delay_buffer)
        return AVERROR(ENOMEM);

    ff_generate_wave_table(s->type, AV_SAMPLE_FMT_S32,
                           s->modulation_buffer, s->modulation_buffer_length,
                           1., s->delay_buffer_length, M_PI / 2.0);

    s->delay_pos = s->modulation_pos = 0;

    switch (inlink->format) {
    case AV_SAMPLE_FMT_DBL:  s->phaser = phaser_dbl;  break;
    case AV_SAMPLE_FMT_DBLP: s->phaser = phaser_dblp; break;
    case AV_SAMPLE_FMT_FLT:  s->phaser = phaser_flt;  break;
    case AV_SAMPLE_FMT_FLTP: s->phaser = phaser_fltp; break;
    case AV_SAMPLE_FMT_S16:  s->phaser = phaser_s16;  break;
    case AV_SAMPLE_FMT_S16P: s->phaser = phaser_s16p; break;
    case AV_SAMPLE_FMT_S32:  s->phaser = phaser_s32;  break;
    case AV_SAMPLE_FMT_S32P: s->phaser = phaser_s32p; break;
    default: av_assert0(0);
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *inbuf)
{
    AudioPhaserContext *s = inlink->dst->priv;
    AVFilterLink *outlink = inlink->dst->outputs[0];
    AVFrame *outbuf;

    if (av_frame_is_writable(inbuf)) {
        outbuf = inbuf;
    } else {
        outbuf = ff_get_audio_buffer(outlink, inbuf->nb_samples);
        if (!outbuf) {
            av_frame_free(&inbuf);
            return AVERROR(ENOMEM);
        }
        av_frame_copy_props(outbuf, inbuf);
    }

    s->phaser(s, inbuf->extended_data, outbuf->extended_data,
              outbuf->nb_samples, outbuf->channels);

    if (inbuf != outbuf)
        av_frame_free(&inbuf);

    return ff_filter_frame(outlink, outbuf);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    AudioPhaserContext *s = ctx->priv;

    av_freep(&s->delay_buffer);
    av_freep(&s->modulation_buffer);
}

static const AVFilterPad aphaser_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = filter_frame,
    },
};

static const AVFilterPad aphaser_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .config_props = config_output,
    },
};

const AVFilter ff_af_aphaser = {
    .name          = "aphaser",
    .description   = NULL_IF_CONFIG_SMALL("Add a phasing effect to the audio."),
    .priv_size     = sizeof(AudioPhaserContext),
    .init          = init,
    .uninit        = uninit,
    FILTER_INPUTS(aphaser_inputs),
    FILTER_OUTPUTS(aphaser_outputs),
    FILTER_SAMPLEFMTS(AV_SAMPLE_FMT_DBL, AV_SAMPLE_FMT_DBLP,
                      AV_SAMPLE_FMT_FLT, AV_SAMPLE_FMT_FLTP,
                      AV_SAMPLE_FMT_S32, AV_SAMPLE_FMT_S32P,
                      AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_S16P),
    .priv_class    = &aphaser_class,
};
