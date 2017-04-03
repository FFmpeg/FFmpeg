/*
 * Copyright (c) 2016 Paul B Mahol
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
#include "libavutil/avstring.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "avfilter.h"
#include "formats.h"
#include "audio.h"
#include "video.h"
#include "internal.h"

typedef struct AudioBitScopeContext {
    const AVClass *class;
    int w, h;
    AVRational frame_rate;
    char *colors;

    int nb_channels;
    int depth;
    uint8_t *fg;

    uint64_t counter[64];
} AudioBitScopeContext;

#define OFFSET(x) offsetof(AudioBitScopeContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption abitscope_options[] = {
    { "rate", "set video rate", OFFSET(frame_rate), AV_OPT_TYPE_VIDEO_RATE, {.str="25"}, 0, INT_MAX, FLAGS },
    { "r",    "set video rate", OFFSET(frame_rate), AV_OPT_TYPE_VIDEO_RATE, {.str="25"}, 0, INT_MAX, FLAGS },
    { "size", "set video size", OFFSET(w), AV_OPT_TYPE_IMAGE_SIZE, {.str="1024x256"}, 0, 0, FLAGS },
    { "s",    "set video size", OFFSET(w), AV_OPT_TYPE_IMAGE_SIZE, {.str="1024x256"}, 0, 0, FLAGS },
    { "colors", "set channels colors", OFFSET(colors), AV_OPT_TYPE_STRING, {.str = "red|green|blue|yellow|orange|lime|pink|magenta|brown" }, 0, 0, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(abitscope);

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *formats = NULL;
    AVFilterChannelLayouts *layouts;
    AVFilterLink *inlink = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];
    static const enum AVSampleFormat sample_fmts[] = { AV_SAMPLE_FMT_S16P, AV_SAMPLE_FMT_S32P, AV_SAMPLE_FMT_NONE };
    static const enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_RGBA, AV_PIX_FMT_NONE };
    int ret;

    formats = ff_make_format_list(sample_fmts);
    if ((ret = ff_formats_ref(formats, &inlink->out_formats)) < 0)
        return ret;

    layouts = ff_all_channel_counts();
    if (!layouts)
        return AVERROR(ENOMEM);
    if ((ret = ff_channel_layouts_ref(layouts, &inlink->out_channel_layouts)) < 0)
        return ret;

    formats = ff_all_samplerates();
    if ((ret = ff_formats_ref(formats, &inlink->out_samplerates)) < 0)
        return ret;

    formats = ff_make_format_list(pix_fmts);
    if ((ret = ff_formats_ref(formats, &outlink->in_formats)) < 0)
        return ret;

    return 0;
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    AudioBitScopeContext *s = ctx->priv;
    int ch, nb_samples;
    char *colors, *saveptr = NULL;

    nb_samples = FFMAX(1024, ((double)inlink->sample_rate / av_q2d(s->frame_rate)) + 0.5);
    inlink->partial_buf_size =
    inlink->min_samples =
    inlink->max_samples = nb_samples;
    s->nb_channels = inlink->channels;
    s->depth = inlink->format == AV_SAMPLE_FMT_S16P ? 16 : 32;

    s->fg = av_malloc_array(s->nb_channels, 4 * sizeof(*s->fg));
    if (!s->fg)
        return AVERROR(ENOMEM);

    colors = av_strdup(s->colors);
    if (!colors)
        return AVERROR(ENOMEM);

    for (ch = 0; ch < s->nb_channels; ch++) {
        uint8_t fg[4] = { 0xff, 0xff, 0xff, 0xff };
        char *color;

        color = av_strtok(ch == 0 ? colors : NULL, " |", &saveptr);
        if (color)
            av_parse_color(fg, color, -1, ctx);
        s->fg[4 * ch + 0] = fg[0];
        s->fg[4 * ch + 1] = fg[1];
        s->fg[4 * ch + 2] = fg[2];
        s->fg[4 * ch + 3] = fg[3];
    }
    av_free(colors);

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AudioBitScopeContext *s = outlink->src->priv;

    outlink->w = s->w;
    outlink->h = s->h;
    outlink->sample_aspect_ratio = (AVRational){1,1};
    outlink->frame_rate = s->frame_rate;

    return 0;
}

static void count_bits(AudioBitScopeContext *s, uint32_t sample, int max)
{
    int i;

    for (i = 0; i < max; i++) {
        if (sample & (1 << i))
            s->counter[i]++;
    }
}

static int filter_frame(AVFilterLink *inlink, AVFrame *insamples)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    AudioBitScopeContext *s = ctx->priv;
    AVFrame *outpicref;
    int ch, i, j, b;

    outpicref = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!outpicref) {
        av_frame_free(&insamples);
        return AVERROR(ENOMEM);
    }

    for (i = 0; i < outlink->h; i++)
        memset(outpicref->data[0] + i * outpicref->linesize[0], 0, outlink->w * 4);

    outpicref->pts = insamples->pts;
    switch (insamples->format) {
    case AV_SAMPLE_FMT_S16P:
        for (ch = 0; ch < inlink->channels; ch++) {
            uint16_t *in = (uint16_t *)insamples->extended_data[ch];
            int w = outpicref->width / inlink->channels;
            int h = outpicref->height / 16;
            uint32_t color = AV_RN32(&s->fg[4 * ch]);

            memset(s->counter, 0, sizeof(s->counter));
            for (i = 0; i < insamples->nb_samples; i++)
                count_bits(s, in[i], 16);

            for (b = 0; b < 16; b++) {
                for (j = 1; j < h - 1; j++) {
                    uint8_t *dst = outpicref->data[0] + (b * h + j) * outpicref->linesize[0] + w * ch * 4;
                    int ww = (s->counter[16 - b - 1] / (float)insamples->nb_samples) * (w - 1);

                    for (i = 0; i < ww; i++) {
                        AV_WN32(&dst[i * 4], color);
                    }
                }
            }
        }
        break;
    case AV_SAMPLE_FMT_S32P:
        for (ch = 0; ch < inlink->channels; ch++) {
            uint32_t *in = (uint32_t *)insamples->extended_data[ch];
            int w = outpicref->width / inlink->channels;
            int h = outpicref->height / 32;
            uint32_t color = AV_RN32(&s->fg[4 * ch]);

            memset(s->counter, 0, sizeof(s->counter));
            for (i = 0; i < insamples->nb_samples; i++)
                count_bits(s, in[i], 32);

            for (b = 0; b < 32; b++) {
                for (j = 1; j < h - 1; j++) {
                    uint8_t *dst = outpicref->data[0] + (b * h + j) * outpicref->linesize[0] + w * ch * 4;
                    int ww = (s->counter[32 - b - 1] / (float)insamples->nb_samples) * (w - 1);

                    for (i = 0; i < ww; i++) {
                        AV_WN32(&dst[i * 4], color);
                    }
                }
            }
        }
        break;
    }

    av_frame_free(&insamples);

    return ff_filter_frame(outlink, outpicref);
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

static const AVFilterPad outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_output,
    },
    { NULL }
};

AVFilter ff_avf_abitscope = {
    .name          = "abitscope",
    .description   = NULL_IF_CONFIG_SMALL("Convert input audio to audio bit scope video output."),
    .query_formats = query_formats,
    .priv_size     = sizeof(AudioBitScopeContext),
    .inputs        = inputs,
    .outputs       = outputs,
    .priv_class    = &abitscope_class,
};
