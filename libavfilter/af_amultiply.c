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

#include "libavutil/avassert.h"
#include "libavutil/channel_layout.h"
#include "libavutil/common.h"
#include "libavutil/float_dsp.h"
#include "libavutil/opt.h"

#define FF_INTERNAL_FIELDS 1
#include "framequeue.h"

#include "audio.h"
#include "avfilter.h"
#include "formats.h"
#include "filters.h"
#include "internal.h"

typedef struct AudioMultiplyContext {
    const AVClass *class;

    AVFrame *frames[2];
    int64_t pts;
    int planes;
    int channels;
    int samples_align;

    AVFloatDSPContext *fdsp;
} AudioMultiplyContext;

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *formats;
    AVFilterChannelLayouts *layouts;
    static const enum AVSampleFormat sample_fmts[] = {
        AV_SAMPLE_FMT_FLT, AV_SAMPLE_FMT_FLTP,
        AV_SAMPLE_FMT_DBL, AV_SAMPLE_FMT_DBLP,
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

static int activate(AVFilterContext *ctx)
{
    AudioMultiplyContext *s = ctx->priv;
    int i, ret, status;
    int nb_samples;
    int64_t pts;

    FF_FILTER_FORWARD_STATUS_BACK_ALL(ctx->outputs[0], ctx);

    nb_samples = FFMIN(ff_framequeue_queued_samples(&ctx->inputs[0]->fifo),
                       ff_framequeue_queued_samples(&ctx->inputs[1]->fifo));
    for (i = 0; i < ctx->nb_inputs && nb_samples > 0; i++) {
        if (s->frames[i])
            continue;

        if (ff_inlink_check_available_samples(ctx->inputs[i], nb_samples) > 0) {
            ret = ff_inlink_consume_samples(ctx->inputs[i], nb_samples, nb_samples, &s->frames[i]);
            if (ret < 0)
                return ret;
        }
    }

    if (nb_samples > 0 && s->frames[0] && s->frames[1]) {
        AVFrame *out;
        int plane_samples;

        if (av_sample_fmt_is_planar(ctx->inputs[0]->format))
            plane_samples = FFALIGN(nb_samples, s->samples_align);
        else
            plane_samples = FFALIGN(nb_samples * s->channels, s->samples_align);

        out = ff_get_audio_buffer(ctx->outputs[0], nb_samples);
        if (!out)
            return AVERROR(ENOMEM);

        out->pts = s->pts;
        s->pts += nb_samples;

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
        emms_c();

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
            if (ff_framequeue_queued_samples(&ctx->inputs[i]->fifo) > 0)
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

    s->channels = inlink->channels;
    s->planes = av_sample_fmt_is_planar(inlink->format) ? inlink->channels : 1;
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
    { NULL }
};

static const AVFilterPad outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .config_props = config_output,
    },
    { NULL }
};

AVFilter ff_af_amultiply = {
    .name           = "amultiply",
    .description    = NULL_IF_CONFIG_SMALL("Multiply two audio streams."),
    .priv_size      = sizeof(AudioMultiplyContext),
    .init           = init,
    .uninit         = uninit,
    .activate       = activate,
    .query_formats  = query_formats,
    .inputs         = inputs,
    .outputs        = outputs,
};
