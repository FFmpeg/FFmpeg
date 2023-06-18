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

#define BINS 32768

typedef struct ChannelStats {
    uint64_t nb_samples;
    uint64_t blknum;
    float peak;
    float sum;
    uint32_t peaks[BINS+1];
    uint32_t rms[BINS+1];
} ChannelStats;

typedef struct DRMeterContext {
    const AVClass *class;
    ChannelStats *chstats;
    int nb_channels;
    int64_t tc_samples;
    double time_constant;
} DRMeterContext;

#define OFFSET(x) offsetof(DRMeterContext, x)
#define FLAGS AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption drmeter_options[] = {
    { "length", "set the window length", OFFSET(time_constant), AV_OPT_TYPE_DOUBLE, {.dbl=3}, .01, 10, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(drmeter);

static int config_output(AVFilterLink *outlink)
{
    DRMeterContext *s = outlink->src->priv;

    s->chstats = av_calloc(outlink->ch_layout.nb_channels, sizeof(*s->chstats));
    if (!s->chstats)
        return AVERROR(ENOMEM);
    s->nb_channels = outlink->ch_layout.nb_channels;
    s->tc_samples = lrint(s->time_constant * outlink->sample_rate);

    return 0;
}

static void finish_block(ChannelStats *p)
{
    int peak_bin, rms_bin;
    float peak, rms;

    rms = sqrtf(2.f * p->sum / p->nb_samples);
    peak = p->peak;
    rms_bin = av_clip(lrintf(rms * BINS), 0, BINS);
    peak_bin = av_clip(lrintf(peak * BINS), 0, BINS);
    p->rms[rms_bin]++;
    p->peaks[peak_bin]++;

    p->peak = 0;
    p->sum = 0;
    p->nb_samples = 0;
    p->blknum++;
}

static void update_stat(DRMeterContext *s, ChannelStats *p, float sample)
{
    p->peak = fmaxf(fabsf(sample), p->peak);
    p->sum += sample * sample;
    p->nb_samples++;
    if (p->nb_samples >= s->tc_samples)
        finish_block(p);
}

static int filter_frame(AVFilterLink *inlink, AVFrame *buf)
{
    DRMeterContext *s = inlink->dst->priv;
    const int channels = s->nb_channels;

    switch (inlink->format) {
    case AV_SAMPLE_FMT_FLTP:
        for (int c = 0; c < channels; c++) {
            ChannelStats *p = &s->chstats[c];
            const float *src = (const float *)buf->extended_data[c];

            for (int i = 0; i < buf->nb_samples; i++, src++)
                update_stat(s, p, *src);
        }
        break;
    case AV_SAMPLE_FMT_FLT: {
        const float *src = (const float *)buf->extended_data[0];

        for (int i = 0; i < buf->nb_samples; i++) {
            for (int c = 0; c < channels; c++, src++)
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
    float dr = 0.f;

    for (int ch = 0; ch < s->nb_channels; ch++) {
        ChannelStats *p = &s->chstats[ch];
        float chdr, secondpeak, rmssum = 0.f;
        int first = 0, last = lrintf(0.2f * p->blknum);
        int peak_bin = BINS;

        if (!p->nb_samples) {
            av_log(ctx, AV_LOG_INFO, "No data, dynamic range not meassurable\n");
            return;
        }

        if (p->nb_samples)
            finish_block(p);

        for (int i = BINS; i >= 0; i--) {
            if (p->peaks[i]) {
                if (first || p->peaks[i] > 1) {
                    peak_bin = i;
                    break;
                }
                first = 1;
            }
        }

        secondpeak = peak_bin / (float)BINS;

        for (int64_t i = BINS, j = 0; i >= 0 && j < last; i--) {
            if (p->rms[i]) {
                rmssum += SQR(i / (float)BINS) * p->rms[i];
                j += p->rms[i];
            }
        }

        chdr = 20.f * log10f(secondpeak / sqrtf(rmssum / (float)last));
        dr += chdr;
        av_log(ctx, AV_LOG_INFO, "Channel %d: DR: %g\n", ch + 1, chdr);
    }

    av_log(ctx, AV_LOG_INFO, "Overall DR: %g\n", dr / s->nb_channels);
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
};

static const AVFilterPad drmeter_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .config_props = config_output,
    },
};

const AVFilter ff_af_drmeter = {
    .name          = "drmeter",
    .description   = NULL_IF_CONFIG_SMALL("Measure audio dynamic range."),
    .priv_size     = sizeof(DRMeterContext),
    .priv_class    = &drmeter_class,
    .uninit        = uninit,
    .flags         = AVFILTER_FLAG_METADATA_ONLY,
    FILTER_INPUTS(drmeter_inputs),
    FILTER_OUTPUTS(drmeter_outputs),
    FILTER_SAMPLEFMTS(AV_SAMPLE_FMT_FLTP, AV_SAMPLE_FMT_FLT),
};
