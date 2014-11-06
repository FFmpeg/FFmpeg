/*
 * Copyright (c) 2011 Stefano Sabatini
 * Copyright (c) 2011 Mina Nagy Zaki
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
 * resampling audio filter
 */

#include "libavutil/avstring.h"
#include "libavutil/channel_layout.h"
#include "libavutil/opt.h"
#include "libavutil/samplefmt.h"
#include "libavutil/avassert.h"
#include "libswresample/swresample.h"
#include "avfilter.h"
#include "audio.h"
#include "internal.h"

typedef struct {
    const AVClass *class;
    int sample_rate_arg;
    double ratio;
    struct SwrContext *swr;
    int64_t next_pts;
    int req_fullfilled;
    int more_data;
} AResampleContext;

static av_cold int init_dict(AVFilterContext *ctx, AVDictionary **opts)
{
    AResampleContext *aresample = ctx->priv;
    int ret = 0;

    aresample->next_pts = AV_NOPTS_VALUE;
    aresample->swr = swr_alloc();
    if (!aresample->swr) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    if (opts) {
        AVDictionaryEntry *e = NULL;

        while ((e = av_dict_get(*opts, "", e, AV_DICT_IGNORE_SUFFIX))) {
            if ((ret = av_opt_set(aresample->swr, e->key, e->value, 0)) < 0)
                goto end;
        }
        av_dict_free(opts);
    }
    if (aresample->sample_rate_arg > 0)
        av_opt_set_int(aresample->swr, "osr", aresample->sample_rate_arg, 0);
end:
    return ret;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    AResampleContext *aresample = ctx->priv;
    swr_free(&aresample->swr);
}

static int query_formats(AVFilterContext *ctx)
{
    AResampleContext *aresample = ctx->priv;
    int out_rate                   = av_get_int(aresample->swr, "osr", NULL);
    uint64_t out_layout            = av_get_int(aresample->swr, "ocl", NULL);
    enum AVSampleFormat out_format = av_get_int(aresample->swr, "osf", NULL);

    AVFilterLink *inlink  = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];

    AVFilterFormats        *in_formats      = ff_all_formats(AVMEDIA_TYPE_AUDIO);
    AVFilterFormats        *out_formats;
    AVFilterFormats        *in_samplerates  = ff_all_samplerates();
    AVFilterFormats        *out_samplerates;
    AVFilterChannelLayouts *in_layouts      = ff_all_channel_counts();
    AVFilterChannelLayouts *out_layouts;

    ff_formats_ref  (in_formats,      &inlink->out_formats);
    ff_formats_ref  (in_samplerates,  &inlink->out_samplerates);
    ff_channel_layouts_ref(in_layouts,      &inlink->out_channel_layouts);

    if(out_rate > 0) {
        int ratelist[] = { out_rate, -1 };
        out_samplerates = ff_make_format_list(ratelist);
    } else {
        out_samplerates = ff_all_samplerates();
    }
    if (!out_samplerates) {
        av_log(ctx, AV_LOG_ERROR, "Cannot allocate output samplerates.\n");
        return AVERROR(ENOMEM);
    }

    ff_formats_ref(out_samplerates, &outlink->in_samplerates);

    if(out_format != AV_SAMPLE_FMT_NONE) {
        int formatlist[] = { out_format, -1 };
        out_formats = ff_make_format_list(formatlist);
    } else
        out_formats = ff_all_formats(AVMEDIA_TYPE_AUDIO);
    ff_formats_ref(out_formats, &outlink->in_formats);

    if(out_layout) {
        int64_t layout_list[] = { out_layout, -1 };
        out_layouts = avfilter_make_format64_list(layout_list);
    } else
        out_layouts = ff_all_channel_counts();
    ff_channel_layouts_ref(out_layouts, &outlink->in_channel_layouts);

    return 0;
}


static int config_output(AVFilterLink *outlink)
{
    int ret;
    AVFilterContext *ctx = outlink->src;
    AVFilterLink *inlink = ctx->inputs[0];
    AResampleContext *aresample = ctx->priv;
    int out_rate;
    uint64_t out_layout;
    enum AVSampleFormat out_format;
    char inchl_buf[128], outchl_buf[128];

    aresample->swr = swr_alloc_set_opts(aresample->swr,
                                        outlink->channel_layout, outlink->format, outlink->sample_rate,
                                        inlink->channel_layout, inlink->format, inlink->sample_rate,
                                        0, ctx);
    if (!aresample->swr)
        return AVERROR(ENOMEM);
    if (!inlink->channel_layout)
        av_opt_set_int(aresample->swr, "ich", inlink->channels, 0);
    if (!outlink->channel_layout)
        av_opt_set_int(aresample->swr, "och", outlink->channels, 0);

    ret = swr_init(aresample->swr);
    if (ret < 0)
        return ret;

    out_rate   = av_get_int(aresample->swr, "osr", NULL);
    out_layout = av_get_int(aresample->swr, "ocl", NULL);
    out_format = av_get_int(aresample->swr, "osf", NULL);
    outlink->time_base = (AVRational) {1, out_rate};

    av_assert0(outlink->sample_rate == out_rate);
    av_assert0(outlink->channel_layout == out_layout || !outlink->channel_layout);
    av_assert0(outlink->format == out_format);

    aresample->ratio = (double)outlink->sample_rate / inlink->sample_rate;

    av_get_channel_layout_string(inchl_buf,  sizeof(inchl_buf),  inlink ->channels, inlink ->channel_layout);
    av_get_channel_layout_string(outchl_buf, sizeof(outchl_buf), outlink->channels, outlink->channel_layout);

    av_log(ctx, AV_LOG_VERBOSE, "ch:%d chl:%s fmt:%s r:%dHz -> ch:%d chl:%s fmt:%s r:%dHz\n",
           inlink ->channels, inchl_buf,  av_get_sample_fmt_name(inlink->format),  inlink->sample_rate,
           outlink->channels, outchl_buf, av_get_sample_fmt_name(outlink->format), outlink->sample_rate);
    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *insamplesref)
{
    AResampleContext *aresample = inlink->dst->priv;
    const int n_in  = insamplesref->nb_samples;
    int64_t delay;
    int n_out       = n_in * aresample->ratio + 32;
    AVFilterLink *const outlink = inlink->dst->outputs[0];
    AVFrame *outsamplesref;
    int ret;

    delay = swr_get_delay(aresample->swr, outlink->sample_rate);
    if (delay > 0)
        n_out += FFMIN(delay, FFMAX(4096, n_out));

    outsamplesref = ff_get_audio_buffer(outlink, n_out);

    if(!outsamplesref)
        return AVERROR(ENOMEM);

    av_frame_copy_props(outsamplesref, insamplesref);
    outsamplesref->format                = outlink->format;
    av_frame_set_channels(outsamplesref, outlink->channels);
    outsamplesref->channel_layout        = outlink->channel_layout;
    outsamplesref->sample_rate           = outlink->sample_rate;

    if(insamplesref->pts != AV_NOPTS_VALUE) {
        int64_t inpts = av_rescale(insamplesref->pts, inlink->time_base.num * (int64_t)outlink->sample_rate * inlink->sample_rate, inlink->time_base.den);
        int64_t outpts= swr_next_pts(aresample->swr, inpts);
        aresample->next_pts =
        outsamplesref->pts  = ROUNDED_DIV(outpts, inlink->sample_rate);
    } else {
        outsamplesref->pts  = AV_NOPTS_VALUE;
    }
    n_out = swr_convert(aresample->swr, outsamplesref->extended_data, n_out,
                                 (void *)insamplesref->extended_data, n_in);
    if (n_out <= 0) {
        av_frame_free(&outsamplesref);
        av_frame_free(&insamplesref);
        return 0;
    }

    aresample->more_data = outsamplesref->nb_samples == n_out; // Indicate that there is probably more data in our buffers

    outsamplesref->nb_samples  = n_out;

    ret = ff_filter_frame(outlink, outsamplesref);
    aresample->req_fullfilled= 1;
    av_frame_free(&insamplesref);
    return ret;
}

static int flush_frame(AVFilterLink *outlink, int final, AVFrame **outsamplesref_ret)
{
    AVFilterContext *ctx = outlink->src;
    AResampleContext *aresample = ctx->priv;
    AVFilterLink *const inlink = outlink->src->inputs[0];
    AVFrame *outsamplesref;
    int n_out = 4096;
    int64_t pts;

    outsamplesref = ff_get_audio_buffer(outlink, n_out);
    *outsamplesref_ret = outsamplesref;
    if (!outsamplesref)
        return AVERROR(ENOMEM);

    pts = swr_next_pts(aresample->swr, INT64_MIN);
    pts = ROUNDED_DIV(pts, inlink->sample_rate);

    n_out = swr_convert(aresample->swr, outsamplesref->extended_data, n_out, final ? NULL : (void*)outsamplesref->extended_data, 0);
    if (n_out <= 0) {
        av_frame_free(&outsamplesref);
        return (n_out == 0) ? AVERROR_EOF : n_out;
    }

    outsamplesref->sample_rate = outlink->sample_rate;
    outsamplesref->nb_samples  = n_out;

    outsamplesref->pts = pts;

    return 0;
}

static int request_frame(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AResampleContext *aresample = ctx->priv;
    int ret;

    // First try to get data from the internal buffers
    if (aresample->more_data) {
        AVFrame *outsamplesref;

        if (flush_frame(outlink, 0, &outsamplesref) >= 0) {
            return ff_filter_frame(outlink, outsamplesref);
        }
    }
    aresample->more_data = 0;

    // Second request more data from the input
    aresample->req_fullfilled = 0;
    do{
        ret = ff_request_frame(ctx->inputs[0]);
    }while(!aresample->req_fullfilled && ret>=0);

    // Third if we hit the end flush
    if (ret == AVERROR_EOF) {
        AVFrame *outsamplesref;

        if ((ret = flush_frame(outlink, 1, &outsamplesref)) < 0)
            return ret;

        return ff_filter_frame(outlink, outsamplesref);
    }
    return ret;
}

static const AVClass *resample_child_class_next(const AVClass *prev)
{
    return prev ? NULL : swr_get_class();
}

static void *resample_child_next(void *obj, void *prev)
{
    AResampleContext *s = obj;
    return prev ? NULL : s->swr;
}

#define OFFSET(x) offsetof(AResampleContext, x)
#define FLAGS AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption options[] = {
    {"sample_rate", NULL, OFFSET(sample_rate_arg), AV_OPT_TYPE_INT, {.i64=0},  0,        INT_MAX, FLAGS },
    {NULL}
};

static const AVClass aresample_class = {
    .class_name       = "aresample",
    .item_name        = av_default_item_name,
    .option           = options,
    .version          = LIBAVUTIL_VERSION_INT,
    .child_class_next = resample_child_class_next,
    .child_next       = resample_child_next,
};

static const AVFilterPad aresample_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad aresample_outputs[] = {
    {
        .name          = "default",
        .config_props  = config_output,
        .request_frame = request_frame,
        .type          = AVMEDIA_TYPE_AUDIO,
    },
    { NULL }
};

AVFilter ff_af_aresample = {
    .name          = "aresample",
    .description   = NULL_IF_CONFIG_SMALL("Resample audio data."),
    .init_dict     = init_dict,
    .uninit        = uninit,
    .query_formats = query_formats,
    .priv_size     = sizeof(AResampleContext),
    .priv_class    = &aresample_class,
    .inputs        = aresample_inputs,
    .outputs       = aresample_outputs,
};
