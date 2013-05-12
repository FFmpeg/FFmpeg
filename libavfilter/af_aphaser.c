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

enum WaveType {
    WAVE_SIN,
    WAVE_TRI,
    WAVE_NB,
};

typedef struct AudioPhaserContext {
    const AVClass *class;
    double in_gain, out_gain;
    double delay;
    double decay;
    double speed;

    enum WaveType type;

    int delay_buffer_length;
    double *delay_buffer;

    int modulation_buffer_length;
    int32_t *modulation_buffer;

    int delay_pos, modulation_pos;

    void (*phaser)(struct AudioPhaserContext *p,
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
    { NULL },
};

AVFILTER_DEFINE_CLASS(aphaser);

static av_cold int init(AVFilterContext *ctx)
{
    AudioPhaserContext *p = ctx->priv;

    if (p->in_gain > (1 - p->decay * p->decay))
        av_log(ctx, AV_LOG_WARNING, "in_gain may cause clipping\n");
    if (p->in_gain / (1 - p->decay) > 1 / p->out_gain)
        av_log(ctx, AV_LOG_WARNING, "out_gain may cause clipping\n");

    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *formats;
    AVFilterChannelLayouts *layouts;
    static const enum AVSampleFormat sample_fmts[] = {
        AV_SAMPLE_FMT_DBL, AV_SAMPLE_FMT_DBLP,
        AV_SAMPLE_FMT_FLT, AV_SAMPLE_FMT_FLTP,
        AV_SAMPLE_FMT_S32, AV_SAMPLE_FMT_S32P,
        AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_S16P,
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

static void generate_wave_table(enum WaveType wave_type, enum AVSampleFormat sample_fmt,
                                void *table, int table_size,
                                double min, double max, double phase)
{
    uint32_t i, phase_offset = phase / M_PI / 2 * table_size + 0.5;

    for (i = 0; i < table_size; i++) {
        uint32_t point = (i + phase_offset) % table_size;
        double d;

        switch (wave_type) {
        case WAVE_SIN:
            d = (sin((double)point / table_size * 2 * M_PI) + 1) / 2;
            break;
        case WAVE_TRI:
            d = (double)point * 2 / table_size;
            switch (4 * point / table_size) {
            case 0: d = d + 0.5; break;
            case 1:
            case 2: d = 1.5 - d; break;
            case 3: d = d - 1.5; break;
            }
            break;
        default:
            av_assert0(0);
        }

        d  = d * (max - min) + min;
        switch (sample_fmt) {
        case AV_SAMPLE_FMT_FLT: {
            float *fp = (float *)table;
            *fp++ = (float)d;
            table = fp;
            continue; }
        case AV_SAMPLE_FMT_DBL: {
            double *dp = (double *)table;
            *dp++ = d;
            table = dp;
            continue; }
        }

        d += d < 0 ? -0.5 : 0.5;
        switch (sample_fmt) {
        case AV_SAMPLE_FMT_S16: {
            int16_t *sp = table;
            *sp++ = (int16_t)d;
            table = sp;
            continue; }
        case AV_SAMPLE_FMT_S32: {
            int32_t *ip = table;
            *ip++ = (int32_t)d;
            table = ip;
            continue; }
        default:
            av_assert0(0);
        }
    }
}

#define MOD(a, b) (((a) >= (b)) ? (a) - (b) : (a))

#define PHASER_PLANAR(name, type)                                      \
static void phaser_## name ##p(AudioPhaserContext *p,                  \
                               uint8_t * const *src, uint8_t **dst,    \
                               int nb_samples, int channels)           \
{                                                                      \
    int i, c, delay_pos, modulation_pos;                               \
                                                                       \
    av_assert0(channels > 0);                                          \
    for (c = 0; c < channels; c++) {                                   \
        type *s = (type *)src[c];                                      \
        type *d = (type *)dst[c];                                      \
        double *buffer = p->delay_buffer +                             \
                         c * p->delay_buffer_length;                   \
                                                                       \
        delay_pos      = p->delay_pos;                                 \
        modulation_pos = p->modulation_pos;                            \
                                                                       \
        for (i = 0; i < nb_samples; i++, s++, d++) {                   \
            double v = *s * p->in_gain + buffer[                       \
                       MOD(delay_pos + p->modulation_buffer[           \
                       modulation_pos],                                \
                       p->delay_buffer_length)] * p->decay;            \
                                                                       \
            modulation_pos = MOD(modulation_pos + 1,                   \
                             p->modulation_buffer_length);             \
            delay_pos = MOD(delay_pos + 1, p->delay_buffer_length);    \
            buffer[delay_pos] = v;                                     \
                                                                       \
            *d = v * p->out_gain;                                      \
        }                                                              \
    }                                                                  \
                                                                       \
    p->delay_pos      = delay_pos;                                     \
    p->modulation_pos = modulation_pos;                                \
}

#define PHASER(name, type)                                              \
static void phaser_## name (AudioPhaserContext *p,                      \
                            uint8_t * const *src, uint8_t **dst,        \
                            int nb_samples, int channels)               \
{                                                                       \
    int i, c, delay_pos, modulation_pos;                                \
    type *s = (type *)src[0];                                           \
    type *d = (type *)dst[0];                                           \
    double *buffer = p->delay_buffer;                                   \
                                                                        \
    delay_pos      = p->delay_pos;                                      \
    modulation_pos = p->modulation_pos;                                 \
                                                                        \
    for (i = 0; i < nb_samples; i++) {                                  \
        int pos = MOD(delay_pos + p->modulation_buffer[modulation_pos], \
                   p->delay_buffer_length) * channels;                  \
        int npos;                                                       \
                                                                        \
        delay_pos = MOD(delay_pos + 1, p->delay_buffer_length);         \
        npos = delay_pos * channels;                                    \
        for (c = 0; c < channels; c++, s++, d++) {                      \
            double v = *s * p->in_gain + buffer[pos + c] * p->decay;    \
                                                                        \
            buffer[npos + c] = v;                                       \
                                                                        \
            *d = v * p->out_gain;                                       \
        }                                                               \
                                                                        \
        modulation_pos = MOD(modulation_pos + 1,                        \
                         p->modulation_buffer_length);                  \
    }                                                                   \
                                                                        \
    p->delay_pos      = delay_pos;                                      \
    p->modulation_pos = modulation_pos;                                 \
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
    AudioPhaserContext *p = outlink->src->priv;
    AVFilterLink *inlink = outlink->src->inputs[0];

    p->delay_buffer_length = p->delay * 0.001 * inlink->sample_rate + 0.5;
    p->delay_buffer = av_calloc(p->delay_buffer_length, sizeof(*p->delay_buffer) * inlink->channels);
    p->modulation_buffer_length = inlink->sample_rate / p->speed + 0.5;
    p->modulation_buffer = av_malloc(p->modulation_buffer_length * sizeof(*p->modulation_buffer));

    if (!p->modulation_buffer || !p->delay_buffer)
        return AVERROR(ENOMEM);

    generate_wave_table(p->type, AV_SAMPLE_FMT_S32,
                        p->modulation_buffer, p->modulation_buffer_length,
                        1., p->delay_buffer_length, M_PI / 2.0);

    p->delay_pos = p->modulation_pos = 0;

    switch (inlink->format) {
    case AV_SAMPLE_FMT_DBL:  p->phaser = phaser_dbl;  break;
    case AV_SAMPLE_FMT_DBLP: p->phaser = phaser_dblp; break;
    case AV_SAMPLE_FMT_FLT:  p->phaser = phaser_flt;  break;
    case AV_SAMPLE_FMT_FLTP: p->phaser = phaser_fltp; break;
    case AV_SAMPLE_FMT_S16:  p->phaser = phaser_s16;  break;
    case AV_SAMPLE_FMT_S16P: p->phaser = phaser_s16p; break;
    case AV_SAMPLE_FMT_S32:  p->phaser = phaser_s32;  break;
    case AV_SAMPLE_FMT_S32P: p->phaser = phaser_s32p; break;
    default: av_assert0(0);
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *inbuf)
{
    AudioPhaserContext *p = inlink->dst->priv;
    AVFilterLink *outlink = inlink->dst->outputs[0];
    AVFrame *outbuf;

    if (av_frame_is_writable(inbuf)) {
        outbuf = inbuf;
    } else {
        outbuf = ff_get_audio_buffer(inlink, inbuf->nb_samples);
        if (!outbuf)
            return AVERROR(ENOMEM);
        av_frame_copy_props(outbuf, inbuf);
    }

    p->phaser(p, inbuf->extended_data, outbuf->extended_data,
              outbuf->nb_samples, av_frame_get_channels(outbuf));

    if (inbuf != outbuf)
        av_frame_free(&inbuf);

    return ff_filter_frame(outlink, outbuf);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    AudioPhaserContext *p = ctx->priv;

    av_freep(&p->delay_buffer);
    av_freep(&p->modulation_buffer);
}

static const AVFilterPad aphaser_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad aphaser_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .config_props = config_output,
    },
    { NULL }
};

AVFilter avfilter_af_aphaser = {
    .name          = "aphaser",
    .description   = NULL_IF_CONFIG_SMALL("Add a phasing effect to the audio."),
    .query_formats = query_formats,
    .priv_size     = sizeof(AudioPhaserContext),
    .init          = init,
    .uninit        = uninit,
    .inputs        = aphaser_inputs,
    .outputs       = aphaser_outputs,
    .priv_class    = &aphaser_class,
};
