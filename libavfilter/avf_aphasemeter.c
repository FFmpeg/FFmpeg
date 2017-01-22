/*
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
 * audio to video multimedia aphasemeter filter
 */

#include "libavutil/avassert.h"
#include "libavutil/channel_layout.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "avfilter.h"
#include "formats.h"
#include "audio.h"
#include "video.h"
#include "internal.h"

typedef struct AudioPhaseMeterContext {
    const AVClass *class;
    AVFrame *out;
    int do_video;
    int w, h;
    AVRational frame_rate;
    int contrast[4];
    uint8_t *mpc_str;
    uint8_t mpc[4];
    int draw_median_phase;
} AudioPhaseMeterContext;

#define OFFSET(x) offsetof(AudioPhaseMeterContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption aphasemeter_options[] = {
    { "rate", "set video rate", OFFSET(frame_rate), AV_OPT_TYPE_VIDEO_RATE, {.str="25"}, 0, INT_MAX, FLAGS },
    { "r",    "set video rate", OFFSET(frame_rate), AV_OPT_TYPE_VIDEO_RATE, {.str="25"}, 0, INT_MAX, FLAGS },
    { "size", "set video size", OFFSET(w), AV_OPT_TYPE_IMAGE_SIZE, {.str="800x400"}, 0, 0, FLAGS },
    { "s",    "set video size", OFFSET(w), AV_OPT_TYPE_IMAGE_SIZE, {.str="800x400"}, 0, 0, FLAGS },
    { "rc", "set red contrast",   OFFSET(contrast[0]), AV_OPT_TYPE_INT, {.i64=2}, 0, 255, FLAGS },
    { "gc", "set green contrast", OFFSET(contrast[1]), AV_OPT_TYPE_INT, {.i64=7}, 0, 255, FLAGS },
    { "bc", "set blue contrast",  OFFSET(contrast[2]), AV_OPT_TYPE_INT, {.i64=1}, 0, 255, FLAGS },
    { "mpc", "set median phase color", OFFSET(mpc_str), AV_OPT_TYPE_STRING, {.str = "none"}, 0, 0, FLAGS },
    { "video", "set video output", OFFSET(do_video), AV_OPT_TYPE_BOOL, {.i64 = 1}, 0, 1, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(aphasemeter);

static int query_formats(AVFilterContext *ctx)
{
    AudioPhaseMeterContext *s = ctx->priv;
    AVFilterFormats *formats = NULL;
    AVFilterChannelLayouts *layout = NULL;
    AVFilterLink *inlink = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];
    static const enum AVSampleFormat sample_fmts[] = { AV_SAMPLE_FMT_FLT, AV_SAMPLE_FMT_NONE };
    static const enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_RGBA, AV_PIX_FMT_NONE };
    int ret;

    formats = ff_make_format_list(sample_fmts);
    if ((ret = ff_formats_ref         (formats, &inlink->out_formats        )) < 0 ||
        (ret = ff_formats_ref         (formats, &outlink->in_formats        )) < 0 ||
        (ret = ff_add_channel_layout  (&layout, AV_CH_LAYOUT_STEREO         )) < 0 ||
        (ret = ff_channel_layouts_ref (layout , &inlink->out_channel_layouts)) < 0 ||
        (ret = ff_channel_layouts_ref (layout , &outlink->in_channel_layouts)) < 0)
        return ret;

    formats = ff_all_samplerates();
    if ((ret = ff_formats_ref(formats, &inlink->out_samplerates)) < 0 ||
        (ret = ff_formats_ref(formats, &outlink->in_samplerates)) < 0)
        return ret;

    if (s->do_video) {
        AVFilterLink *outlink = ctx->outputs[1];

        formats = ff_make_format_list(pix_fmts);
        if ((ret = ff_formats_ref(formats, &outlink->in_formats)) < 0)
            return ret;
    }

    return 0;
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    AudioPhaseMeterContext *s = ctx->priv;
    int nb_samples;

    if (s->do_video) {
        nb_samples = FFMAX(1024, ((double)inlink->sample_rate / av_q2d(s->frame_rate)) + 0.5);
        inlink->partial_buf_size =
        inlink->min_samples =
        inlink->max_samples = nb_samples;
    }

    return 0;
}

static int config_video_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AudioPhaseMeterContext *s = ctx->priv;

    outlink->w = s->w;
    outlink->h = s->h;
    outlink->sample_aspect_ratio = (AVRational){1,1};
    outlink->frame_rate = s->frame_rate;

    if (!strcmp(s->mpc_str, "none"))
        s->draw_median_phase = 0;
    else if (av_parse_color(s->mpc, s->mpc_str, -1, ctx) >= 0)
        s->draw_median_phase = 1;
    else
        return AVERROR(EINVAL);

    return 0;
}

static inline int get_x(float phase, int w)
{
  return (phase + 1.) / 2. * (w - 1);
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AudioPhaseMeterContext *s = ctx->priv;
    AVFilterLink *outlink = s->do_video ? ctx->outputs[1] : NULL;
    AVFilterLink *aoutlink = ctx->outputs[0];
    AVDictionary **metadata;
    const int rc = s->contrast[0];
    const int gc = s->contrast[1];
    const int bc = s->contrast[2];
    float fphase = 0;
    AVFrame *out;
    uint8_t *dst;
    int i;

    if (s->do_video && (!s->out || s->out->width  != outlink->w ||
                                   s->out->height != outlink->h)) {
        av_frame_free(&s->out);
        s->out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
        if (!s->out) {
            av_frame_free(&in);
            return AVERROR(ENOMEM);
        }

        out = s->out;
        for (i = 0; i < outlink->h; i++)
            memset(out->data[0] + i * out->linesize[0], 0, outlink->w * 4);
    } else if (s->do_video) {
        out = s->out;
        for (i = outlink->h - 1; i >= 10; i--)
            memmove(out->data[0] + (i  ) * out->linesize[0],
                    out->data[0] + (i-1) * out->linesize[0],
                    outlink->w * 4);
        for (i = 0; i < outlink->w; i++)
            AV_WL32(out->data[0] + i * 4, 0);
    }

    for (i = 0; i < in->nb_samples; i++) {
        const float *src = (float *)in->data[0] + i * 2;
        const float f = src[0] * src[1] / (src[0]*src[0] + src[1] * src[1]) * 2;
        const float phase = isnan(f) ? 1 : f;
        const int x = get_x(phase, s->w);

        if (s->do_video) {
            dst = out->data[0] + x * 4;
            dst[0] = FFMIN(255, dst[0] + rc);
            dst[1] = FFMIN(255, dst[1] + gc);
            dst[2] = FFMIN(255, dst[2] + bc);
            dst[3] = 255;
        }
        fphase += phase;
    }
    fphase /= in->nb_samples;

    if (s->do_video) {
        if (s->draw_median_phase) {
            dst = out->data[0] + get_x(fphase, s->w) * 4;
            AV_WL32(dst, AV_RL32(s->mpc));
        }

        for (i = 1; i < 10 && i < outlink->h; i++)
            memcpy(out->data[0] + i * out->linesize[0], out->data[0], outlink->w * 4);
    }

    metadata = avpriv_frame_get_metadatap(in);
    if (metadata) {
        uint8_t value[128];

        snprintf(value, sizeof(value), "%f", fphase);
        av_dict_set(metadata, "lavfi.aphasemeter.phase", value, 0);
    }

    if (s->do_video) {
        s->out->pts = in->pts;
        ff_filter_frame(outlink, av_frame_clone(s->out));
    }
    return ff_filter_frame(aoutlink, in);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    AudioPhaseMeterContext *s = ctx->priv;
    int i;

    av_frame_free(&s->out);
    for (i = 0; i < ctx->nb_outputs; i++)
        av_freep(&ctx->output_pads[i].name);
}

static av_cold int init(AVFilterContext *ctx)
{
    AudioPhaseMeterContext *s = ctx->priv;
    AVFilterPad pad;

    pad = (AVFilterPad){
        .name         = av_strdup("out0"),
        .type         = AVMEDIA_TYPE_AUDIO,
    };
    if (!pad.name)
        return AVERROR(ENOMEM);
    ff_insert_outpad(ctx, 0, &pad);

    if (s->do_video) {
        pad = (AVFilterPad){
            .name         = av_strdup("out1"),
            .type         = AVMEDIA_TYPE_VIDEO,
            .config_props = config_video_output,
        };
        if (!pad.name)
            return AVERROR(ENOMEM);
        ff_insert_outpad(ctx, 1, &pad);
    }

    return 0;
}

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .config_props = config_input,
        .filter_frame = filter_frame,
    },
    { NULL }
};

AVFilter ff_avf_aphasemeter = {
    .name          = "aphasemeter",
    .description   = NULL_IF_CONFIG_SMALL("Convert input audio to phase meter video output."),
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .priv_size     = sizeof(AudioPhaseMeterContext),
    .inputs        = inputs,
    .outputs       = NULL,
    .priv_class    = &aphasemeter_class,
    .flags         = AVFILTER_FLAG_DYNAMIC_OUTPUTS,
};
