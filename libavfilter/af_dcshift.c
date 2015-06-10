/*
 * Copyright (c) 2000 Chris Ausbrooks <weed@bucket.pp.ualr.edu>
 * Copyright (c) 2000 Fabien COELHO <fabien@coelho.net>
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

#include "libavutil/opt.h"
#include "libavutil/samplefmt.h"
#include "avfilter.h"
#include "audio.h"
#include "internal.h"

typedef struct DCShiftContext {
    const AVClass *class;
    double dcshift;
    double limiterthreshhold;
    double limitergain;
} DCShiftContext;

#define OFFSET(x) offsetof(DCShiftContext, x)
#define A AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption dcshift_options[] = {
    { "shift",       "set DC shift",     OFFSET(dcshift),       AV_OPT_TYPE_DOUBLE, {.dbl=0}, -1, 1, A },
    { "limitergain", "set limiter gain", OFFSET(limitergain), AV_OPT_TYPE_DOUBLE, {.dbl=0}, 0, 1, A },
    { NULL }
};

AVFILTER_DEFINE_CLASS(dcshift);

static av_cold int init(AVFilterContext *ctx)
{
    DCShiftContext *s = ctx->priv;

    s->limiterthreshhold = INT32_MAX * (1.0 - (fabs(s->dcshift) - s->limitergain));

    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    AVFilterChannelLayouts *layouts;
    AVFilterFormats *formats;
    static const enum AVSampleFormat sample_fmts[] = {
        AV_SAMPLE_FMT_S32P, AV_SAMPLE_FMT_NONE
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

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out = ff_get_audio_buffer(inlink, in->nb_samples);
    DCShiftContext *s = ctx->priv;
    int i, j;
    double dcshift = s->dcshift;

    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out, in);

    if (s->limitergain > 0) {
        for (i = 0; i < inlink->channels; i++) {
            const int32_t *src = (int32_t *)in->extended_data[i];
            int32_t *dst = (int32_t *)out->extended_data[i];

            for (j = 0; j < in->nb_samples; j++) {
                double d;

                d = src[j];

                if (d > s->limiterthreshhold && dcshift > 0) {
                    d = (d - s->limiterthreshhold) * s->limitergain /
                             (INT32_MAX - s->limiterthreshhold) +
                             s->limiterthreshhold + dcshift;
                } else if (d < -s->limiterthreshhold && dcshift < 0) {
                    d = (d + s->limiterthreshhold) * s->limitergain /
                             (INT32_MAX - s->limiterthreshhold) -
                             s->limiterthreshhold + dcshift;
                } else {
                    d = dcshift * INT32_MAX + d;
                }

                dst[j] = av_clipl_int32(d);
            }
        }
    } else {
        for (i = 0; i < inlink->channels; i++) {
            const int32_t *src = (int32_t *)in->extended_data[i];
            int32_t *dst = (int32_t *)out->extended_data[i];

            for (j = 0; j < in->nb_samples; j++) {
                double d = dcshift * (INT32_MAX + 1.) + src[j];

                dst[j] = av_clipl_int32(d);
            }
        }
    }

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}
static const AVFilterPad dcshift_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad dcshift_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_AUDIO,
    },
    { NULL }
};

AVFilter ff_af_dcshift = {
    .name           = "dcshift",
    .description    = NULL_IF_CONFIG_SMALL("Apply a DC shift to the audio."),
    .query_formats  = query_formats,
    .priv_size      = sizeof(DCShiftContext),
    .priv_class     = &dcshift_class,
    .init           = init,
    .inputs         = dcshift_inputs,
    .outputs        = dcshift_outputs,
    .flags          = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
