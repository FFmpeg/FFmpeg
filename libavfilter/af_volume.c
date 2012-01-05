/*
 * Copyright (c) 2011 Stefano Sabatini
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
 * audio volume filter
 * based on ffmpeg.c code
 */

#include "libavutil/audioconvert.h"
#include "libavutil/eval.h"
#include "avfilter.h"

typedef struct {
    double volume;
    int    volume_i;
} VolumeContext;

static av_cold int init(AVFilterContext *ctx, const char *args, void *opaque)
{
    VolumeContext *vol = ctx->priv;
    char *tail;
    int ret = 0;

    vol->volume = 1.0;

    if (args) {
        /* parse the number as a decimal number */
        double d = strtod(args, &tail);

        if (*tail) {
            if (!strcmp(tail, "dB")) {
                /* consider the argument an adjustement in decibels */
                d = pow(10, d/20);
            } else {
                /* parse the argument as an expression */
                ret = av_expr_parse_and_eval(&d, args, NULL, NULL,
                                             NULL, NULL, NULL, NULL,
                                             NULL, 0, ctx);
            }
        }

        if (ret < 0) {
            av_log(ctx, AV_LOG_ERROR,
                   "Invalid volume argument '%s'\n", args);
            return AVERROR(EINVAL);
        }

        if (d < 0 || d > 65536) { /* 65536 = INT_MIN / (128 * 256) */
            av_log(ctx, AV_LOG_ERROR,
                   "Negative or too big volume value %f\n", d);
            return AVERROR(EINVAL);
        }

        vol->volume = d;
    }

    vol->volume_i = (int)(vol->volume * 256 + 0.5);
    av_log(ctx, AV_LOG_INFO, "volume=%f\n", vol->volume);
    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *formats = NULL;
    enum AVSampleFormat sample_fmts[] = {
        AV_SAMPLE_FMT_U8,
        AV_SAMPLE_FMT_S16,
        AV_SAMPLE_FMT_S32,
        AV_SAMPLE_FMT_FLT,
        AV_SAMPLE_FMT_DBL,
        AV_SAMPLE_FMT_NONE
    };
    int packing_fmts[] = { AVFILTER_PACKED, -1 };

    formats = avfilter_make_all_channel_layouts();
    if (!formats)
        return AVERROR(ENOMEM);
    avfilter_set_common_channel_layouts(ctx, formats);

    formats = avfilter_make_format_list(sample_fmts);
    if (!formats)
        return AVERROR(ENOMEM);
    avfilter_set_common_sample_formats(ctx, formats);

    formats = avfilter_make_format_list(packing_fmts);
    if (!formats)
        return AVERROR(ENOMEM);
    avfilter_set_common_packing_formats(ctx, formats);

    return 0;
}

static void filter_samples(AVFilterLink *inlink, AVFilterBufferRef *insamples)
{
    VolumeContext *vol = inlink->dst->priv;
    AVFilterLink *outlink = inlink->dst->outputs[0];
    const int nb_samples = insamples->audio->nb_samples *
        av_get_channel_layout_nb_channels(insamples->audio->channel_layout);
    const double volume   = vol->volume;
    const int    volume_i = vol->volume_i;
    int i;

    if (volume_i != 256) {
        switch (insamples->format) {
        case AV_SAMPLE_FMT_U8:
        {
            uint8_t *p = (void *)insamples->data[0];
            for (i = 0; i < nb_samples; i++) {
                int v = (((*p - 128) * volume_i + 128) >> 8) + 128;
                *p++ = av_clip_uint8(v);
            }
            break;
        }
        case AV_SAMPLE_FMT_S16:
        {
            int16_t *p = (void *)insamples->data[0];
            for (i = 0; i < nb_samples; i++) {
                int v = ((int64_t)*p * volume_i + 128) >> 8;
                *p++ = av_clip_int16(v);
            }
            break;
        }
        case AV_SAMPLE_FMT_S32:
        {
            int32_t *p = (void *)insamples->data[0];
            for (i = 0; i < nb_samples; i++) {
                int64_t v = (((int64_t)*p * volume_i + 128) >> 8);
                *p++ = av_clipl_int32(v);
            }
            break;
        }
        case AV_SAMPLE_FMT_FLT:
        {
            float *p = (void *)insamples->data[0];
            float scale = (float)volume;
            for (i = 0; i < nb_samples; i++) {
                *p++ *= scale;
            }
            break;
        }
        case AV_SAMPLE_FMT_DBL:
        {
            double *p = (void *)insamples->data[0];
            for (i = 0; i < nb_samples; i++) {
                *p *= volume;
                p++;
            }
            break;
        }
        }
    }
    avfilter_filter_samples(outlink, insamples);
}

AVFilter avfilter_af_volume = {
    .name           = "volume",
    .description    = NULL_IF_CONFIG_SMALL("Change input volume."),
    .query_formats  = query_formats,
    .priv_size      = sizeof(VolumeContext),
    .init           = init,

    .inputs  = (const AVFilterPad[])  {{ .name     = "default",
                                   .type           = AVMEDIA_TYPE_AUDIO,
                                   .filter_samples = filter_samples,
                                   .min_perms      = AV_PERM_READ|AV_PERM_WRITE},
                                 { .name = NULL}},

    .outputs = (const AVFilterPad[])  {{ .name     = "default",
                                   .type           = AVMEDIA_TYPE_AUDIO, },
                                 { .name = NULL}},
};
