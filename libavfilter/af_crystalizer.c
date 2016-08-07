/*
 * Copyright (c) 2016 The FFmpeg Project
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
#include "formats.h"

typedef struct CrystalizerContext {
    const AVClass *class;
    float mult;
    int clip;
    AVFrame *prev;
} CrystalizerContext;

#define OFFSET(x) offsetof(CrystalizerContext, x)
#define A AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption crystalizer_options[] = {
    { "i", "set intensity",    OFFSET(mult), AV_OPT_TYPE_FLOAT, {.dbl=2.0}, 0, 10, A },
    { "c", "enable clipping",  OFFSET(clip), AV_OPT_TYPE_BOOL,  {.i64=1},   0,  1, A },
    { NULL }
};

AVFILTER_DEFINE_CLASS(crystalizer);

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *formats = NULL;
    AVFilterChannelLayouts *layouts = NULL;
    int ret;

    if ((ret = ff_add_format        (&formats, AV_SAMPLE_FMT_FLT )) < 0 ||
        (ret = ff_set_common_formats(ctx     , formats           )) < 0)
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

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    CrystalizerContext *s = ctx->priv;
    const float *src = (const float *)in->data[0];
    const float mult = s->mult;
    AVFrame *out;
    float *dst, *prv;
    int n, c;

    if (!s->prev) {
        s->prev = ff_get_audio_buffer(inlink, 1);
        if (!s->prev) {
            av_frame_free(&in);
            return AVERROR(ENOMEM);
        }
    }

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

    dst = (float *)out->data[0];
    prv = (float *)s->prev->data[0];

    for (n = 0; n < in->nb_samples; n++) {
        for (c = 0; c < in->channels; c++) {
            float current = src[c];

            dst[c] = current + (current - prv[c]) * mult;
            prv[c] = current;
            if (s->clip) {
                dst[c] = av_clipf(dst[c], -1, 1);
            }
        }
        dst += c;
        src += c;
    }

    if (out != in)
        av_frame_free(&in);

    return ff_filter_frame(outlink, out);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    CrystalizerContext *s = ctx->priv;

    av_frame_free(&s->prev);
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
        .name = "default",
        .type = AVMEDIA_TYPE_AUDIO,
    },
    { NULL }
};

AVFilter ff_af_crystalizer = {
    .name           = "crystalizer",
    .description    = NULL_IF_CONFIG_SMALL("Simple expand audio dynamic range filter."),
    .query_formats  = query_formats,
    .priv_size      = sizeof(CrystalizerContext),
    .priv_class     = &crystalizer_class,
    .uninit         = uninit,
    .inputs         = inputs,
    .outputs        = outputs,
};
