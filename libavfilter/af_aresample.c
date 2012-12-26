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
    double ratio;
    struct SwrContext *swr;
    int64_t next_pts;
    int req_fullfilled;
} AResampleContext;

static av_cold int init(AVFilterContext *ctx, const char *args)
{
    AResampleContext *aresample = ctx->priv;
    int ret = 0;
    char *argd = av_strdup(args);

    aresample->next_pts = AV_NOPTS_VALUE;
    aresample->swr = swr_alloc();
    if (!aresample->swr) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    if (args) {
        char *ptr = argd, *token;

        while (token = av_strtok(ptr, ":", &ptr)) {
            char *value;
            av_strtok(token, "=", &value);

            if (value) {
                if ((ret = av_opt_set(aresample->swr, token, value, 0)) < 0)
                    goto end;
            } else {
                int out_rate;
                if ((ret = ff_parse_sample_rate(&out_rate, token, ctx)) < 0)
                    goto end;
                if ((ret = av_opt_set_int(aresample->swr, "osr", out_rate, 0)) < 0)
                    goto end;
            }
        }
    }
end:
    av_free(argd);
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
        out_samplerates = ff_make_format_list((int[]){ out_rate, -1 });
    } else {
        out_samplerates = ff_all_samplerates();
    }
    ff_formats_ref(out_samplerates, &outlink->in_samplerates);

    if(out_format != AV_SAMPLE_FMT_NONE) {
        out_formats = ff_make_format_list((int[]){ out_format, -1 });
    } else
        out_formats = ff_all_formats(AVMEDIA_TYPE_AUDIO);
    ff_formats_ref(out_formats, &outlink->in_formats);

    if(out_layout) {
        out_layouts = avfilter_make_format64_list((int64_t[]){ out_layout, -1 });
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
    av_assert0(outlink->channel_layout == out_layout);
    av_assert0(outlink->format == out_format);

    aresample->ratio = (double)outlink->sample_rate / inlink->sample_rate;

    av_get_channel_layout_string(inchl_buf,  sizeof(inchl_buf),  -1, inlink ->channel_layout);
    av_get_channel_layout_string(outchl_buf, sizeof(outchl_buf), -1, outlink->channel_layout);

    av_log(ctx, AV_LOG_VERBOSE, "ch:%d chl:%s fmt:%s r:%dHz -> ch:%d chl:%s fmt:%s r:%dHz\n",
           inlink ->channels, inchl_buf,  av_get_sample_fmt_name(inlink->format),  inlink->sample_rate,
           outlink->channels, outchl_buf, av_get_sample_fmt_name(outlink->format), outlink->sample_rate);
    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFilterBufferRef *insamplesref)
{
    AResampleContext *aresample = inlink->dst->priv;
    const int n_in  = insamplesref->audio->nb_samples;
    int n_out       = n_in * aresample->ratio * 2 + 256;
    AVFilterLink *const outlink = inlink->dst->outputs[0];
    AVFilterBufferRef *outsamplesref = ff_get_audio_buffer(outlink, AV_PERM_WRITE, n_out);
    int ret;

    if(!outsamplesref)
        return AVERROR(ENOMEM);

    avfilter_copy_buffer_ref_props(outsamplesref, insamplesref);
    outsamplesref->format                = outlink->format;
    outsamplesref->audio->channels       = outlink->channels;
    outsamplesref->audio->channel_layout = outlink->channel_layout;
    outsamplesref->audio->sample_rate    = outlink->sample_rate;

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
        avfilter_unref_buffer(outsamplesref);
        avfilter_unref_buffer(insamplesref);
        return 0;
    }

    outsamplesref->audio->nb_samples  = n_out;

    ret = ff_filter_frame(outlink, outsamplesref);
    aresample->req_fullfilled= 1;
    avfilter_unref_buffer(insamplesref);
    return ret;
}

static int request_frame(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AResampleContext *aresample = ctx->priv;
    AVFilterLink *const inlink = outlink->src->inputs[0];
    int ret;

    aresample->req_fullfilled = 0;
    do{
        ret = ff_request_frame(ctx->inputs[0]);
    }while(!aresample->req_fullfilled && ret>=0);

    if (ret == AVERROR_EOF) {
        AVFilterBufferRef *outsamplesref;
        int n_out = 4096;

        outsamplesref = ff_get_audio_buffer(outlink, AV_PERM_WRITE, n_out);
        if (!outsamplesref)
            return AVERROR(ENOMEM);
        n_out = swr_convert(aresample->swr, outsamplesref->extended_data, n_out, 0, 0);
        if (n_out <= 0) {
            avfilter_unref_buffer(outsamplesref);
            return (n_out == 0) ? AVERROR_EOF : n_out;
        }

        outsamplesref->audio->sample_rate = outlink->sample_rate;
        outsamplesref->audio->nb_samples  = n_out;
#if 0
        outsamplesref->pts = aresample->next_pts;
        if(aresample->next_pts != AV_NOPTS_VALUE)
            aresample->next_pts += av_rescale_q(n_out, (AVRational){1 ,outlink->sample_rate}, outlink->time_base);
#else
        outsamplesref->pts = swr_next_pts(aresample->swr, INT64_MIN);
        outsamplesref->pts = ROUNDED_DIV(outsamplesref->pts, inlink->sample_rate);
#endif

        ff_filter_frame(outlink, outsamplesref);
        return 0;
    }
    return ret;
}

static const AVFilterPad aresample_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = filter_frame,
        .min_perms    = AV_PERM_READ,
    },
    { NULL },
};

static const AVFilterPad aresample_outputs[] = {
    {
        .name          = "default",
        .config_props  = config_output,
        .request_frame = request_frame,
        .type          = AVMEDIA_TYPE_AUDIO,
    },
    { NULL },
};

AVFilter avfilter_af_aresample = {
    .name          = "aresample",
    .description   = NULL_IF_CONFIG_SMALL("Resample audio data."),
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .priv_size     = sizeof(AResampleContext),
    .inputs        = aresample_inputs,
    .outputs       = aresample_outputs,
};
