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

#include <float.h>

#include "libavutil/ffmath.h"
#include "libavutil/opt.h"
#include "audio.h"
#include "avfilter.h"
#include "internal.h"

typedef struct ChannelStats {
    uint64_t nb_samples;
    uint64_t blknum;
    float peak;
    float sum;
    uint32_t peaks[10001];
    uint32_t rms[10001];
} ChannelStats;

typedef struct DRMeterContext {
    const AVClass *class;
    ChannelStats *chstats;
    int nb_channels;
    uint64_t tc_samples;
    double time_constant;
} DRMeterContext;

#define OFFSET(x) offsetof(DRMeterContext, x)
#define FLAGS AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption drmeter_options[] = {
    { "length", "set the window length", OFFSET(time_constant), AV_OPT_TYPE_DOUBLE, {.dbl=3}, .01, 10, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(drmeter);

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *formats;
    AVFilterChannelLayouts *layouts;
    static const enum AVSampleFormat sample_fmts[] = {
        AV_SAMPLE_FMT_FLTP, AV_SAMPLE_FMT_FLT,
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

static int config_output(AVFilterLink *outlink)
{
    DRMeterContext *s = outlink->src->priv;

    s->chstats = av_calloc(sizeof(*s->chstats), outlink->channels);
    if (!s->chstats)
        return AVERROR(ENOMEM);
    s->nb_channels = outlink->channels;
    s->tc_samples = s->time_constant * outlink->sample_rate + .5;

    return 0;
}

static void finish_block(ChannelStats *p)
{
    int peak_bin, rms_bin;
    float peak, rms;

    rms = sqrt(2 * p->sum / p->nb_samples);
    peak = p->peak;
    rms_bin = av_clip(rms * 10000, 0, 10000);
    peak_bin = av_clip(peak * 10000, 0, 10000);
    p->rms[rms_bin]++;
    p->peaks[peak_bin]++;

    p->peak = 0;
    p->sum = 0;
    p->nb_samples = 0;
    p->blknum++;
}

static void update_stat(DRMeterContext *s, ChannelStats *p, float sample)
{
    if (p->nb_samples >= s->tc_samples) {
        finish_block(p);
    }

    p->peak = FFMAX(FFABS(sample), p->peak);
    p->sum += sample * sample;
    p->nb_samples++;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *buf)
{
    DRMeterContext *s = inlink->dst->priv;
    const int channels = s->nb_channels;
    int i, c;

    switch (inlink->format) {
    case AV_SAMPLE_FMT_FLTP:
        for (c = 0; c < channels; c++) {
            ChannelStats *p = &s->chstats[c];
            const float *src = (const float *)buf->extended_data[c];

            for (i = 0; i < buf->nb_samples; i++, src++)
                update_stat(s, p, *src);
        }
        break;
    case AV_SAMPLE_FMT_FLT: {
        const float *src = (const float *)buf->extended_data[0];

        for (i = 0; i < buf->nb_samples; i++) {
            for (c = 0; c < channels; c++, src++)
                update_stat(s, &s->chstats[c], *src);
        }}
        break;
    }

    return ff_filter_frame(inlink->dst->outputs[0], buf);
}

#define SQR(a) ((a)*(a))

static void print_stats(AVFilterContext *ctx)
{
    DRMeterContext *s = ctx->priv;
    float dr = 0;
    int ch;

    for (ch = 0; ch < s->nb_channels; ch++) {
        ChannelStats *p = &s->chstats[ch];
        float chdr, secondpeak, rmssum = 0;
        int i, j, first = 0;

        finish_block(p);

        for (i = 0; i <= 10000; i++) {
            if (p->peaks[10000 - i]) {
                if (first)
                    break;
                first = 1;
            }
        }

        secondpeak = (10000 - i) / 10000.;

        for (i = 10000, j = 0; i >= 0 && j < 0.2 * p->blknum; i--) {
            if (p->rms[i]) {
                rmssum += SQR(i / 10000.) * p->rms[i];
                j += p->rms[i];
            }
        }

        chdr = 20 * log10(secondpeak / sqrt(rmssum / (0.2 * p->blknum)));
        dr += chdr;
        av_log(ctx, AV_LOG_INFO, "Channel %d: DR: %.1f\n", ch + 1, chdr);
    }

    av_log(ctx, AV_LOG_INFO, "Overall DR: %.1f\n", dr / s->nb_channels);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    DRMeterContext *s = ctx->priv;

    if (s->nb_channels)
        print_stats(ctx);
    av_freep(&s->chstats);
}

static const AVFilterPad drmeter_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad drmeter_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .config_props = config_output,
    },
    { NULL }
};

AVFilter ff_af_drmeter = {
    .name          = "drmeter",
    .description   = NULL_IF_CONFIG_SMALL("Measure audio dynamic range."),
    .query_formats = query_formats,
    .priv_size     = sizeof(DRMeterContext),
    .priv_class    = &drmeter_class,
    .uninit        = uninit,
    .inputs        = drmeter_inputs,
    .outputs       = drmeter_outputs,
};
