/*
 * Copyright (c) 2008 Rob Sykes
 * Copyright (c) 2017 Paul B Mahol
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

#include "libavutil/channel_layout.h"
#include "libavutil/opt.h"
#include "avfilter.h"
#include "audio.h"

typedef struct AudioContrastContext {
    const AVClass *class;
    float contrast;
    void (*filter)(void **dst, const void **src,
                   int nb_samples, int channels, float contrast);
} AudioContrastContext;

#define OFFSET(x) offsetof(AudioContrastContext, x)
#define A AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption acontrast_options[] = {
    { "contrast", "set contrast", OFFSET(contrast), AV_OPT_TYPE_FLOAT, {.dbl=33}, 0, 100, A },
    { NULL }
};

AVFILTER_DEFINE_CLASS(acontrast);

static void filter_flt(void **d, const void **s,
                       int nb_samples, int channels,
                       float contrast)
{
    const float *src = s[0];
    float *dst = d[0];
    int n, c;

    for (n = 0; n < nb_samples; n++) {
        for (c = 0; c < channels; c++) {
            float d = src[c] * M_PI_2;

            dst[c] = sinf(d + contrast * sinf(d * 4));
        }

        dst += c;
        src += c;
    }
}

static void filter_dbl(void **d, const void **s,
                       int nb_samples, int channels,
                       float contrast)
{
    const double *src = s[0];
    double *dst = d[0];
    int n, c;

    for (n = 0; n < nb_samples; n++) {
        for (c = 0; c < channels; c++) {
            double d = src[c] * M_PI_2;

            dst[c] = sin(d + contrast * sin(d * 4));
        }

        dst += c;
        src += c;
    }
}

static void filter_fltp(void **d, const void **s,
                        int nb_samples, int channels,
                        float contrast)
{
    int n, c;

    for (c = 0; c < channels; c++) {
        const float *src = s[c];
        float *dst = d[c];

        for (n = 0; n < nb_samples; n++) {
            float d = src[n] * M_PI_2;

            dst[n] = sinf(d + contrast * sinf(d * 4));
        }
    }
}

static void filter_dblp(void **d, const void **s,
                        int nb_samples, int channels,
                        float contrast)
{
    int n, c;

    for (c = 0; c < channels; c++) {
        const double *src = s[c];
        double *dst = d[c];

        for (n = 0; n < nb_samples; n++) {
            double d = src[n] * M_PI_2;

            dst[n] = sin(d + contrast * sin(d * 4));
        }
    }
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    AudioContrastContext *s    = ctx->priv;

    switch (inlink->format) {
    case AV_SAMPLE_FMT_FLT:  s->filter = filter_flt;  break;
    case AV_SAMPLE_FMT_DBL:  s->filter = filter_dbl;  break;
    case AV_SAMPLE_FMT_FLTP: s->filter = filter_fltp; break;
    case AV_SAMPLE_FMT_DBLP: s->filter = filter_dblp; break;
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    AudioContrastContext *s = ctx->priv;
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

    s->filter((void **)out->extended_data, (const void **)in->extended_data,
              in->nb_samples, in->ch_layout.nb_channels, s->contrast / 750);

    if (out != in)
        av_frame_free(&in);

    return ff_filter_frame(outlink, out);
}

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = filter_frame,
        .config_props = config_input,
    },
};

const AVFilter ff_af_acontrast = {
    .name           = "acontrast",
    .description    = NULL_IF_CONFIG_SMALL("Simple audio dynamic range compression/expansion filter."),
    .priv_size      = sizeof(AudioContrastContext),
    .priv_class     = &acontrast_class,
    FILTER_INPUTS(inputs),
    FILTER_OUTPUTS(ff_audio_default_filterpad),
    FILTER_SAMPLEFMTS(AV_SAMPLE_FMT_FLT, AV_SAMPLE_FMT_FLTP,
                      AV_SAMPLE_FMT_DBL, AV_SAMPLE_FMT_DBLP),
};
