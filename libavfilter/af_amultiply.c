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

#include "libavutil/channel_layout.h"
#include "libavutil/common.h"
#include "libavutil/float_dsp.h"
#include "libavutil/mem.h"

#include "audio.h"
#include "avfilter.h"
#include "filters.h"

typedef struct AudioMultiplyContext {
    const AVClass *class;

    AVFrame *frames[2];
    int planes;
    int channels;
    int samples_align;

    AVFloatDSPContext *fdsp;
} AudioMultiplyContext;

static int activate(AVFilterContext *ctx)
{
    AudioMultiplyContext *s = ctx->priv;
    int i, ret, status;
    int nb_samples;
    int64_t pts;

    FF_FILTER_FORWARD_STATUS_BACK_ALL(ctx->outputs[0], ctx);

    nb_samples = FFMIN(ff_inlink_queued_samples(ctx->inputs[0]),
                       ff_inlink_queued_samples(ctx->inputs[1]));
    for (i = 0; i < ctx->nb_inputs && nb_samples > 0; i++) {
        if (s->frames[i])
            continue;

        if (ff_inlink_check_available_samples(ctx->inputs[i], nb_samples) > 0) {
            ret = ff_inlink_consume_samples(ctx->inputs[i], nb_samples, nb_samples, &s->frames[i]);
            if (ret < 0)
                return ret;
        }
    }

    if (s->frames[0] && s->frames[1]) {
        AVFrame *out;
        int plane_samples;

        if (av_sample_fmt_is_planar(ctx->inputs[0]->format))
            plane_samples = FFALIGN(s->frames[0]->nb_samples, s->samples_align);
        else
            plane_samples = FFALIGN(s->frames[0]->nb_samples * s->channels, s->samples_align);

        out = ff_get_audio_buffer(ctx->outputs[0], s->frames[0]->nb_samples);
        if (!out)
            return AVERROR(ENOMEM);

        out->pts = s->frames[0]->pts;
        out->duration = s->frames[0]->duration;

        if (av_get_packed_sample_fmt(ctx->inputs[0]->format) == AV_SAMPLE_FMT_FLT) {
            for (i = 0; i < s->planes; i++) {
                s->fdsp->vector_fmul((float *)out->extended_data[i],
                                     (const float *)s->frames[0]->extended_data[i],
                                     (const float *)s->frames[1]->extended_data[i],
                                     plane_samples);
            }
        } else {
            for (i = 0; i < s->planes; i++) {
                s->fdsp->vector_dmul((double *)out->extended_data[i],
                                     (const double *)s->frames[0]->extended_data[i],
                                     (const double *)s->frames[1]->extended_data[i],
                                     plane_samples);
            }
        }

        av_frame_free(&s->frames[0]);
        av_frame_free(&s->frames[1]);

        ret = ff_filter_frame(ctx->outputs[0], out);
        if (ret < 0)
            return ret;
    }

    if (!nb_samples) {
        for (i = 0; i < 2; i++) {
            if (ff_inlink_acknowledge_status(ctx->inputs[i], &status, &pts)) {
                ff_outlink_set_status(ctx->outputs[0], status, pts);
                return 0;
            }
        }
    }

    if (ff_outlink_frame_wanted(ctx->outputs[0])) {
        for (i = 0; i < 2; i++) {
            if (s->frames[i] || ff_inlink_queued_samples(ctx->inputs[i]) > 0)
                continue;
            ff_inlink_request_frame(ctx->inputs[i]);
            return 0;
        }
    }
    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AudioMultiplyContext *s = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];

    s->channels = inlink->ch_layout.nb_channels;
    s->planes = av_sample_fmt_is_planar(inlink->format) ? inlink->ch_layout.nb_channels : 1;
    s->samples_align = 16;

    return 0;
}

static av_cold int init(AVFilterContext *ctx)
{
    AudioMultiplyContext *s = ctx->priv;

    s->fdsp = avpriv_float_dsp_alloc(0);
    if (!s->fdsp)
        return AVERROR(ENOMEM);

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    AudioMultiplyContext *s = ctx->priv;
    av_freep(&s->fdsp);
}

static const AVFilterPad inputs[] = {
    {
        .name = "multiply0",
        .type = AVMEDIA_TYPE_AUDIO,
    },
    {
        .name = "multiply1",
        .type = AVMEDIA_TYPE_AUDIO,
    },
};

static const AVFilterPad outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .config_props = config_output,
    },
};

const FFFilter ff_af_amultiply = {
    .p.name         = "amultiply",
    .p.description  = NULL_IF_CONFIG_SMALL("Multiply two audio streams."),
    .priv_size      = sizeof(AudioMultiplyContext),
    .init           = init,
    .uninit         = uninit,
    .activate       = activate,
    FILTER_INPUTS(inputs),
    FILTER_OUTPUTS(outputs),
    FILTER_SAMPLEFMTS(AV_SAMPLE_FMT_FLT, AV_SAMPLE_FMT_FLTP,
                      AV_SAMPLE_FMT_DBL, AV_SAMPLE_FMT_DBLP),
};
