/*
 * Copyright (c) 2012 Stefano Sabatini
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
 * audio to video multimedia filter
 */

#include "libavutil/audioconvert.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "avfilter.h"
#include "formats.h"
#include "audio.h"
#include "video.h"
#include "internal.h"

typedef struct {
    const AVClass *class;
    int w, h;
    char *rate_str;
    AVRational rate;
    int buf_idx;
    AVFilterBufferRef *outpicref;
    int req_fullfilled;
    int n;
    int sample_count_mod;
} ShowWavesContext;

#define OFFSET(x) offsetof(ShowWavesContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption showwaves_options[] = {
    { "rate", "set video rate", OFFSET(rate_str), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, FLAGS },
    { "r",    "set video rate", OFFSET(rate_str), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, FLAGS },
    { "size", "set video size", OFFSET(w), AV_OPT_TYPE_IMAGE_SIZE, {.str = "600x240"}, 0, 0, FLAGS },
    { "s",    "set video size", OFFSET(w), AV_OPT_TYPE_IMAGE_SIZE, {.str = "600x240"}, 0, 0, FLAGS },
    { "n",    "set how many samples to show in the same point", OFFSET(n), AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT_MAX, FLAGS },
    { NULL },
};

AVFILTER_DEFINE_CLASS(showwaves);

static av_cold int init(AVFilterContext *ctx, const char *args)
{
    ShowWavesContext *showwaves = ctx->priv;
    int err;

    showwaves->class = &showwaves_class;
    av_opt_set_defaults(showwaves);
    showwaves->buf_idx = 0;

    if ((err = av_set_options_string(showwaves, args, "=", ":")) < 0)
        return err;

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    ShowWavesContext *showwaves = ctx->priv;

    av_freep(&showwaves->rate_str);
    avfilter_unref_bufferp(&showwaves->outpicref);
}

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *formats = NULL;
    AVFilterChannelLayouts *layouts = NULL;
    AVFilterLink *inlink = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];
    static const enum AVSampleFormat sample_fmts[] = { AV_SAMPLE_FMT_S16, -1 };
    static const enum PixelFormat pix_fmts[] = { PIX_FMT_GRAY8, -1 };

    /* set input audio formats */
    formats = ff_make_format_list(sample_fmts);
    if (!formats)
        return AVERROR(ENOMEM);
    ff_formats_ref(formats, &inlink->out_formats);

    layouts = ff_all_channel_layouts();
    if (!layouts)
        return AVERROR(ENOMEM);
    ff_channel_layouts_ref(layouts, &inlink->out_channel_layouts);

    formats = ff_all_samplerates();
    if (!formats)
        return AVERROR(ENOMEM);
    ff_formats_ref(formats, &inlink->out_samplerates);

    /* set output video format */
    formats = ff_make_format_list(pix_fmts);
    if (!formats)
        return AVERROR(ENOMEM);
    ff_formats_ref(formats, &outlink->in_formats);

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AVFilterLink *inlink = ctx->inputs[0];
    ShowWavesContext *showwaves = ctx->priv;
    int err;

    if (showwaves->n && showwaves->rate_str) {
        av_log(ctx, AV_LOG_ERROR, "Options 'n' and 'rate' cannot be set at the same time\n");
        return AVERROR(EINVAL);
    }

    if (!showwaves->n) {
        if (!showwaves->rate_str)
            showwaves->rate = (AVRational){25,1}; /* set default value */
        else if ((err = av_parse_video_rate(&showwaves->rate, showwaves->rate_str)) < 0) {
            av_log(ctx, AV_LOG_ERROR, "Invalid frame rate: '%s'\n", showwaves->rate_str);
            return err;
        }
        showwaves->n = FFMAX(1, ((double)inlink->sample_rate / (showwaves->w * av_q2d(showwaves->rate))) + 0.5);
    }

    outlink->w = showwaves->w;
    outlink->h = showwaves->h;
    outlink->sample_aspect_ratio = (AVRational){1,1};

    outlink->frame_rate = av_div_q((AVRational){inlink->sample_rate,showwaves->n},
                                   (AVRational){showwaves->w,1});

    av_log(ctx, AV_LOG_VERBOSE, "s:%dx%d r:%f n:%d\n",
           showwaves->w, showwaves->h, av_q2d(outlink->frame_rate), showwaves->n);
    return 0;
}

inline static void push_frame(AVFilterLink *outlink)
{
    ShowWavesContext *showwaves = outlink->src->priv;

    ff_start_frame(outlink, showwaves->outpicref);
    ff_draw_slice(outlink, 0, outlink->h, 1);
    ff_end_frame(outlink);
    showwaves->req_fullfilled = 1;
    showwaves->outpicref = NULL;
    showwaves->buf_idx = 0;
}

static int request_frame(AVFilterLink *outlink)
{
    ShowWavesContext *showwaves = outlink->src->priv;
    AVFilterLink *inlink = outlink->src->inputs[0];
    int ret;

    showwaves->req_fullfilled = 0;
    do {
        ret = ff_request_frame(inlink);
    } while (!showwaves->req_fullfilled && ret >= 0);

    if (ret == AVERROR_EOF && showwaves->outpicref)
        push_frame(outlink);
    return ret;
}

#define MAX_INT16 ((1<<15) -1)

static int filter_samples(AVFilterLink *inlink, AVFilterBufferRef *insamples)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    ShowWavesContext *showwaves = ctx->priv;
    const int nb_samples = insamples->audio->nb_samples;
    AVFilterBufferRef *outpicref = showwaves->outpicref;
    int linesize = outpicref ? outpicref->linesize[0] : 0;
    int16_t *p = (int16_t *)insamples->data[0];
    int nb_channels = av_get_channel_layout_nb_channels(insamples->audio->channel_layout);
    int i, j, h;
    const int n = showwaves->n;
    const int x = 255 / (nb_channels * n); /* multiplication factor, pre-computed to avoid in-loop divisions */

    /* draw data in the buffer */
    for (i = 0; i < nb_samples; i++) {
        if (showwaves->buf_idx == 0 && showwaves->sample_count_mod == 0) {
            showwaves->outpicref = outpicref =
                ff_get_video_buffer(outlink, AV_PERM_WRITE|AV_PERM_ALIGN,
                                    outlink->w, outlink->h);
            outpicref->video->w = outlink->w;
            outpicref->video->h = outlink->h;
            outpicref->pts = insamples->pts +
                             av_rescale_q((p - (int16_t *)insamples->data[0]) / nb_channels,
                                          (AVRational){ 1, inlink->sample_rate },
                                          outlink->time_base);
            linesize = outpicref->linesize[0];
            memset(outpicref->data[0], 0, showwaves->h*linesize);
        }
        for (j = 0; j < nb_channels; j++) {
            h = showwaves->h/2 - av_rescale(*p++, showwaves->h/2, MAX_INT16);
            if (h >= 0 && h < outlink->h)
                *(outpicref->data[0] + showwaves->buf_idx + h * linesize) += x;
        }
        showwaves->sample_count_mod++;
        if (showwaves->sample_count_mod == n) {
            showwaves->sample_count_mod = 0;
            showwaves->buf_idx++;
        }
        if (showwaves->buf_idx == showwaves->w)
            push_frame(outlink);
    }

    avfilter_unref_buffer(insamples);
    return 0;
}

AVFilter avfilter_avf_showwaves = {
    .name           = "showwaves",
    .description    = NULL_IF_CONFIG_SMALL("Convert input audio to a video output."),
    .init           = init,
    .uninit         = uninit,
    .query_formats  = query_formats,
    .priv_size      = sizeof(ShowWavesContext),

    .inputs  = (const AVFilterPad[]) {
        {
            .name           = "default",
            .type           = AVMEDIA_TYPE_AUDIO,
            .filter_samples = filter_samples,
            .min_perms      = AV_PERM_READ,
        },
        { .name = NULL }
    },

    .outputs = (const AVFilterPad[]) {
        {
            .name           = "default",
            .type           = AVMEDIA_TYPE_VIDEO,
            .config_props   = config_output,
            .request_frame  = request_frame,
        },
        { .name = NULL }
    },

    .priv_class = &showwaves_class,
};
