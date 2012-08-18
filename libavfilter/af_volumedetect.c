/*
 * Copyright (c) 2012 Nicolas George
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/audioconvert.h"
#include "libavutil/avassert.h"
#include "audio.h"
#include "avfilter.h"
#include "internal.h"

typedef struct {
    /**
     * Number of samples at each PCM value.
     * histogram[0x8000 + i] is the number of samples at value i.
     * The extra element is there for symmetry.
     */
    uint64_t histogram[0x10001];
} VolDetectContext;

static int query_formats(AVFilterContext *ctx)
{
    enum AVSampleFormat sample_fmts[] = {
        AV_SAMPLE_FMT_S16,
        AV_SAMPLE_FMT_S16P,
        AV_SAMPLE_FMT_NONE
    };
    AVFilterFormats *formats;

    if (!(formats = ff_make_format_list(sample_fmts)))
        return AVERROR(ENOMEM);
    ff_set_common_formats(ctx, formats);

    return 0;
}

static int filter_samples(AVFilterLink *inlink, AVFilterBufferRef *samples)
{
    AVFilterContext *ctx = inlink->dst;
    VolDetectContext *vd = ctx->priv;
    int64_t layout  = samples->audio->channel_layout;
    int nb_samples  = samples->audio->nb_samples;
    int nb_channels = av_get_channel_layout_nb_channels(layout);
    int nb_planes   = nb_planes;
    int plane, i;
    int16_t *pcm;

    if (!av_sample_fmt_is_planar(samples->format)) {
        nb_samples *= nb_channels;
        nb_planes = 1;
    }
    for (plane = 0; plane < nb_planes; plane++) {
        pcm = (int16_t *)samples->extended_data[plane];
        for (i = 0; i < nb_samples; i++)
            vd->histogram[pcm[i] + 0x8000]++;
    }

    return ff_filter_samples(inlink->dst->outputs[0], samples);
}

#define MAX_DB 91

static inline double logdb(uint64_t v)
{
    double d = v / (double)(0x8000 * 0x8000);
    if (!v)
        return MAX_DB;
    return log(d) * -4.3429448190325182765112891891660508229; /* -10/log(10) */
}

static void print_stats(AVFilterContext *ctx)
{
    VolDetectContext *vd = ctx->priv;
    int i, max_volume, shift;
    uint64_t nb_samples = 0, power = 0, nb_samples_shift = 0, sum = 0;
    uint64_t histdb[MAX_DB + 1] = { 0 };

    for (i = 0; i < 0x10000; i++)
        nb_samples += vd->histogram[i];
    av_log(ctx, AV_LOG_INFO, "n_samples: %"PRId64"\n", nb_samples);
    if (!nb_samples)
        return;

    /* If nb_samples > 1<<34, there is a risk of overflow in the
       multiplication or the sum: shift all histogram values to avoid that.
       The total number of samples must be recomputed to avoid rounding
       errors. */
    shift = av_log2(nb_samples >> 33);
    for (i = 0; i < 0x10000; i++) {
        nb_samples_shift += vd->histogram[i] >> shift;
        power += (i - 0x8000) * (i - 0x8000) * (vd->histogram[i] >> shift);
    }
    if (!nb_samples_shift)
        return;
    power = (power + nb_samples_shift / 2) / nb_samples_shift;
    av_assert0(power <= 0x8000 * 0x8000);
    av_log(ctx, AV_LOG_INFO, "mean_volume: %.1f dB\n", -logdb(power));

    max_volume = 0x8000;
    while (max_volume > 0 && !vd->histogram[0x8000 + max_volume] &&
                             !vd->histogram[0x8000 - max_volume])
        max_volume--;
    av_log(ctx, AV_LOG_INFO, "max_volume: %.1f dB\n", -logdb(max_volume * max_volume));

    for (i = 0; i < 0x10000; i++)
        histdb[(int)logdb((i - 0x8000) * (i - 0x8000))] += vd->histogram[i];
    for (i = 0; i <= MAX_DB && !histdb[i]; i++);
    for (; i <= MAX_DB && sum < nb_samples / 1000; i++) {
        av_log(ctx, AV_LOG_INFO, "histogram_%ddb: %"PRId64"\n", i, histdb[i]);
        sum += histdb[i];
    }
}

static int request_frame(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    int ret = ff_request_frame(ctx->inputs[0]);
    if (ret == AVERROR_EOF)
        print_stats(ctx);
    return ret;
}

AVFilter avfilter_af_volumedetect = {
    .name          = "volumedetect",
    .description   = NULL_IF_CONFIG_SMALL("Detect audio volume."),

    .priv_size     = sizeof(VolDetectContext),
    .query_formats = query_formats,

    .inputs    = (const AVFilterPad[]) {
        { .name             = "default",
          .type             = AVMEDIA_TYPE_AUDIO,
          .get_audio_buffer = ff_null_get_audio_buffer,
          .filter_samples   = filter_samples,
          .min_perms        = AV_PERM_READ, },
        { .name = NULL }
    },
    .outputs   = (const AVFilterPad[]) {
        { .name = "default",
          .type = AVMEDIA_TYPE_AUDIO,
          .request_frame = request_frame, },
        { .name = NULL }
    },
};
