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

#include "libavutil/avstring.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "avfilter.h"
#include "filters.h"
#include "formats.h"
#include "audio.h"
#include "video.h"

typedef struct AudioBitScopeContext {
    const AVClass *class;
    int w, h;
    AVRational frame_rate;
    char *colors;
    int mode;

    int nb_channels;
    int nb_samples;
    int depth;
    int current_vpos;
    uint8_t *fg;

    uint64_t counter[64];

    AVFrame *outpicref;
} AudioBitScopeContext;

#define OFFSET(x) offsetof(AudioBitScopeContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption abitscope_options[] = {
    { "rate", "set video rate", OFFSET(frame_rate), AV_OPT_TYPE_VIDEO_RATE, {.str="25"}, 0, INT_MAX, FLAGS },
    { "r",    "set video rate", OFFSET(frame_rate), AV_OPT_TYPE_VIDEO_RATE, {.str="25"}, 0, INT_MAX, FLAGS },
    { "size", "set video size", OFFSET(w), AV_OPT_TYPE_IMAGE_SIZE, {.str="1024x256"}, 0, 0, FLAGS },
    { "s",    "set video size", OFFSET(w), AV_OPT_TYPE_IMAGE_SIZE, {.str="1024x256"}, 0, 0, FLAGS },
    { "colors", "set channels colors", OFFSET(colors), AV_OPT_TYPE_STRING, {.str = "red|green|blue|yellow|orange|lime|pink|magenta|brown" }, 0, 0, FLAGS },
    { "mode", "set output mode", OFFSET(mode), AV_OPT_TYPE_INT, {.i64 = 0 }, 0, 1, FLAGS, .unit = "mode" },
    { "m",    "set output mode", OFFSET(mode), AV_OPT_TYPE_INT, {.i64 = 0 }, 0, 1, FLAGS, .unit = "mode" },
    { "bars",  NULL, 0, AV_OPT_TYPE_CONST, {.i64 = 0 }, 0, 0, FLAGS, .unit = "mode" },
    { "trace", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = 1 }, 0, 0, FLAGS, .unit = "mode" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(abitscope);

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *formats = NULL;
    AVFilterChannelLayouts *layouts;
    AVFilterLink *inlink = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];
    static const enum AVSampleFormat sample_fmts[] = { AV_SAMPLE_FMT_S16P, AV_SAMPLE_FMT_S32P,
                                                       AV_SAMPLE_FMT_U8P,  AV_SAMPLE_FMT_S64P,
                                                       AV_SAMPLE_FMT_FLTP, AV_SAMPLE_FMT_DBLP,
                                                       AV_SAMPLE_FMT_NONE };
    static const enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_RGBA, AV_PIX_FMT_NONE };
    int ret;

    formats = ff_make_format_list(sample_fmts);
    if ((ret = ff_formats_ref(formats, &inlink->outcfg.formats)) < 0)
        return ret;

    layouts = ff_all_channel_counts();
    if (!layouts)
        return AVERROR(ENOMEM);
    if ((ret = ff_channel_layouts_ref(layouts, &inlink->outcfg.channel_layouts)) < 0)
        return ret;

    formats = ff_all_samplerates();
    if ((ret = ff_formats_ref(formats, &inlink->outcfg.samplerates)) < 0)
        return ret;

    formats = ff_make_format_list(pix_fmts);
    if ((ret = ff_formats_ref(formats, &outlink->incfg.formats)) < 0)
        return ret;

    return 0;
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    AudioBitScopeContext *s = ctx->priv;
    int ch;
    char *colors, *saveptr = NULL;

    s->nb_samples = FFMAX(1, av_rescale(inlink->sample_rate, s->frame_rate.den, s->frame_rate.num));
    s->nb_channels = inlink->ch_layout.nb_channels;
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
    FilterLink *l = ff_filter_link(outlink);

    outlink->w = s->w;
    outlink->h = s->h;
    outlink->sample_aspect_ratio = (AVRational){1,1};
    l->frame_rate = s->frame_rate;
    outlink->time_base = av_inv_q(l->frame_rate);

    return 0;
}

#define BITCOUNTER(type, depth, one)                                        \
        memset(counter, 0, sizeof(s->counter));                             \
        for (int i = 0; i < nb_samples; i++) {                              \
            const type x = in[i];                                           \
            for (int j = 0; j < depth && x; j++)                            \
                counter[j] += !!(x & (one << j));                           \
        }

#define BARS(type, depth, one)                                              \
    for (int ch = 0; ch < inlink->ch_layout.nb_channels; ch++) {            \
        const int nb_samples = insamples->nb_samples;                       \
        const type *in = (const type *)insamples->extended_data[ch];        \
        const int w = outpicref->width / inlink->ch_layout.nb_channels;     \
        const int h = outpicref->height / depth;                            \
        const uint32_t color = AV_RN32(&s->fg[4 * ch]);                     \
        uint64_t *counter = s->counter;                                     \
                                                                            \
        BITCOUNTER(type, depth, one)                                        \
                                                                            \
        for (int b = 0; b < depth; b++) {                                   \
            for (int j = 1; j < h - 1; j++) {                               \
                uint8_t *dst = outpicref->data[0] + (b * h + j) * outpicref->linesize[0] + w * ch * 4; \
                const int ww = (counter[depth - b - 1] / (float)nb_samples) * (w - 1); \
                                                                            \
                for (int i = 0; i < ww; i++) {                              \
                    AV_WN32(&dst[i * 4], color);                            \
                }                                                           \
            }                                                               \
        }                                                                   \
    }

#define DO_TRACE(type, depth, one)                                          \
    for (int ch = 0; ch < inlink->ch_layout.nb_channels; ch++) {            \
        const int nb_samples = insamples->nb_samples;                       \
        const int w = outpicref->width / inlink->ch_layout.nb_channels;     \
        const type *in = (const type *)insamples->extended_data[ch];        \
        uint64_t *counter = s->counter;                                     \
        const int wb = w / depth;                                           \
        int wv;                                                             \
                                                                            \
        BITCOUNTER(type, depth, one)                                        \
                                                                            \
        for (int b = 0; b < depth; b++) {                                   \
            uint8_t colors[4];                                              \
            uint32_t color;                                                 \
            uint8_t *dst = outpicref->data[0] + w * ch * 4 + wb * b * 4 +   \
                           s->current_vpos * outpicref->linesize[0];        \
            wv = (counter[depth - b - 1] * 255) / nb_samples;               \
            colors[0] = (wv * s->fg[ch * 4 + 0] + 127) / 255;               \
            colors[1] = (wv * s->fg[ch * 4 + 1] + 127) / 255;               \
            colors[2] = (wv * s->fg[ch * 4 + 2] + 127) / 255;               \
            colors[3] = (wv * s->fg[ch * 4 + 3] + 127) / 255;               \
            color = AV_RN32(colors);                                        \
                                                                            \
            for (int x = 0; x < wb; x++)                                    \
                AV_WN32(&dst[x * 4], color);                                \
        }                                                                   \
    }

static int filter_frame(AVFilterLink *inlink, AVFrame *insamples)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    AudioBitScopeContext *s = ctx->priv;
    AVFrame *outpicref;
    int ret;

    if (s->mode == 0 || !s->outpicref) {
        outpicref = ff_get_video_buffer(outlink, outlink->w, outlink->h);
        if (!outpicref) {
            av_frame_free(&insamples);
            return AVERROR(ENOMEM);
        }

        for (int i = 0; i < outlink->h; i++)
            memset(outpicref->data[0] + i * outpicref->linesize[0], 0, outlink->w * 4);
        if (!s->outpicref && s->mode == 1)
            s->outpicref = outpicref;
    }

    if (s->mode == 1) {
        ret = ff_inlink_make_frame_writable(outlink, &s->outpicref);
        if (ret < 0) {
            av_frame_free(&insamples);
            return ret;
        }
        outpicref = av_frame_clone(s->outpicref);
        if (!outpicref) {
            av_frame_free(&insamples);
            return AVERROR(ENOMEM);
        }
    }

    outpicref->pts = av_rescale_q(insamples->pts, inlink->time_base, outlink->time_base);
    outpicref->duration = 1;
    outpicref->sample_aspect_ratio = (AVRational){1,1};

    switch (insamples->format) {
    case AV_SAMPLE_FMT_U8P:
        if (s->mode == 0) { BARS(uint8_t,   8, 1) } else { DO_TRACE(uint8_t,   8, 1) }
        break;
    case AV_SAMPLE_FMT_S16P:
        if (s->mode == 0) { BARS(uint16_t, 16, 1) } else { DO_TRACE(uint16_t, 16, 1) }
        break;
    case AV_SAMPLE_FMT_FLTP:
    case AV_SAMPLE_FMT_S32P:
        if (s->mode == 0) { BARS(uint32_t, 32, 1U) } else { DO_TRACE(uint32_t, 32, 1U) }
        break;
    case AV_SAMPLE_FMT_DBLP:
    case AV_SAMPLE_FMT_S64P:
        if (s->mode == 0) { BARS(uint64_t, 64, 1ULL) } else { DO_TRACE(uint64_t, 64, 1ULL) }
        break;
    }

    s->current_vpos++;
    if (s->current_vpos >= outlink->h)
        s->current_vpos = 0;
    av_frame_free(&insamples);

    return ff_filter_frame(outlink, outpicref);
}

static int activate(AVFilterContext *ctx)
{
    AVFilterLink *inlink = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];
    AudioBitScopeContext *s = ctx->priv;
    AVFrame *in;
    int ret;

    FF_FILTER_FORWARD_STATUS_BACK(outlink, inlink);

    ret = ff_inlink_consume_samples(inlink, s->nb_samples, s->nb_samples, &in);
    if (ret < 0)
        return ret;
    if (ret > 0)
        return filter_frame(inlink, in);

    FF_FILTER_FORWARD_STATUS(inlink, outlink);
    FF_FILTER_FORWARD_WANTED(outlink, inlink);

    return FFERROR_NOT_READY;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    AudioBitScopeContext *s = ctx->priv;

    av_frame_free(&s->outpicref);
}

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .config_props = config_input,
    },
};

static const AVFilterPad outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_output,
    },
};

const AVFilter ff_avf_abitscope = {
    .name          = "abitscope",
    .description   = NULL_IF_CONFIG_SMALL("Convert input audio to audio bit scope video output."),
    .priv_size     = sizeof(AudioBitScopeContext),
    FILTER_INPUTS(inputs),
    FILTER_OUTPUTS(outputs),
    FILTER_QUERY_FUNC(query_formats),
    .uninit        = uninit,
    .activate      = activate,
    .priv_class    = &abitscope_class,
};
