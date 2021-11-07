/*
 * Copyright (c) 2015 Derek Buitenhuis
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

#include "libavutil/opt.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

#define DEFAULT_LENGTH 300

typedef struct ReverseContext {
    int nb_frames;
    AVFrame **frames;
    unsigned int frames_size;
    unsigned int pts_size;
    int64_t *pts;
    int flush_idx;
    int64_t nb_samples;
} ReverseContext;

static av_cold int init(AVFilterContext *ctx)
{
    ReverseContext *s = ctx->priv;

    s->pts = av_fast_realloc(NULL, &s->pts_size,
                             DEFAULT_LENGTH * sizeof(*(s->pts)));
    if (!s->pts)
        return AVERROR(ENOMEM);

    s->frames = av_fast_realloc(NULL, &s->frames_size,
                                DEFAULT_LENGTH * sizeof(*(s->frames)));
    if (!s->frames) {
        av_freep(&s->pts);
        return AVERROR(ENOMEM);
    }

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    ReverseContext *s = ctx->priv;

    while (s->nb_frames > 0) {
        av_frame_free(&s->frames[s->nb_frames - 1]);
        s->nb_frames--;
    }

    av_freep(&s->pts);
    av_freep(&s->frames);
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    ReverseContext *s    = ctx->priv;
    void *ptr;

    if (s->nb_frames + 1 > s->pts_size / sizeof(*(s->pts))) {
        ptr = av_fast_realloc(s->pts, &s->pts_size, s->pts_size * 2);
        if (!ptr)
            return AVERROR(ENOMEM);
        s->pts = ptr;
    }

    if (s->nb_frames + 1 > s->frames_size / sizeof(*(s->frames))) {
        ptr = av_fast_realloc(s->frames, &s->frames_size, s->frames_size * 2);
        if (!ptr)
            return AVERROR(ENOMEM);
        s->frames = ptr;
    }

    s->frames[s->nb_frames] = in;
    s->pts[s->nb_frames]    = in->pts;
    s->nb_frames++;

    return 0;
}

#if CONFIG_REVERSE_FILTER

static int request_frame(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    ReverseContext *s = ctx->priv;
    int ret;

    ret = ff_request_frame(ctx->inputs[0]);

    if (ret == AVERROR_EOF && s->nb_frames > 0) {
        AVFrame *out = s->frames[s->nb_frames - 1];
        out->pts     = s->pts[s->flush_idx++];
        ret          = ff_filter_frame(outlink, out);
        s->frames[s->nb_frames - 1] = NULL;
        s->nb_frames--;
    }

    return ret;
}

static const AVFilterPad reverse_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
};

static const AVFilterPad reverse_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .request_frame = request_frame,
    },
};

const AVFilter ff_vf_reverse = {
    .name        = "reverse",
    .description = NULL_IF_CONFIG_SMALL("Reverse a clip."),
    .priv_size   = sizeof(ReverseContext),
    .init        = init,
    .uninit      = uninit,
    FILTER_INPUTS(reverse_inputs),
    FILTER_OUTPUTS(reverse_outputs),
};

#endif /* CONFIG_REVERSE_FILTER */

#if CONFIG_AREVERSE_FILTER

static void reverse_samples_planar(AVFrame *out)
{
    for (int p = 0; p < out->channels; p++) {
        switch (out->format) {
        case AV_SAMPLE_FMT_U8P: {
            uint8_t *dst = (uint8_t *)out->extended_data[p];
            for (int i = 0, j = out->nb_samples - 1; i < j; i++, j--)
                FFSWAP(uint8_t, dst[i], dst[j]);
        }
            break;
        case AV_SAMPLE_FMT_S16P: {
            int16_t *dst = (int16_t *)out->extended_data[p];
            for (int i = 0, j = out->nb_samples - 1; i < j; i++, j--)
                FFSWAP(int16_t, dst[i], dst[j]);
        }
            break;
        case AV_SAMPLE_FMT_S32P: {
            int32_t *dst = (int32_t *)out->extended_data[p];
            for (int i = 0, j = out->nb_samples - 1; i < j; i++, j--)
                FFSWAP(int32_t, dst[i], dst[j]);
        }
            break;
        case AV_SAMPLE_FMT_S64P: {
            int64_t *dst = (int64_t *)out->extended_data[p];
            for (int i = 0, j = out->nb_samples - 1; i < j; i++, j--)
                FFSWAP(int64_t, dst[i], dst[j]);
        }
            break;
        case AV_SAMPLE_FMT_FLTP: {
            float *dst = (float *)out->extended_data[p];
            for (int i = 0, j = out->nb_samples - 1; i < j; i++, j--)
                FFSWAP(float, dst[i], dst[j]);
        }
            break;
        case AV_SAMPLE_FMT_DBLP: {
            double *dst = (double *)out->extended_data[p];
            for (int i = 0, j = out->nb_samples - 1; i < j; i++, j--)
                FFSWAP(double, dst[i], dst[j]);
        }
            break;
        }
    }
}

static void reverse_samples_packed(AVFrame *out)
{
    const int channels = out->channels;

    switch (out->format) {
    case AV_SAMPLE_FMT_U8: {
        uint8_t *dst = (uint8_t *)out->extended_data[0];
        for (int i = 0, j = out->nb_samples - 1; i < j; i++, j--)
            for (int p = 0; p < channels; p++)
                FFSWAP(uint8_t, dst[i * channels + p], dst[j * channels + p]);
    }
        break;
    case AV_SAMPLE_FMT_S16: {
        int16_t *dst = (int16_t *)out->extended_data[0];
        for (int i = 0, j = out->nb_samples - 1; i < j; i++, j--)
            for (int p = 0; p < channels; p++)
                FFSWAP(int16_t, dst[i * channels + p], dst[j * channels + p]);
    }
        break;
    case AV_SAMPLE_FMT_S32: {
        int32_t *dst = (int32_t *)out->extended_data[0];
        for (int i = 0, j = out->nb_samples - 1; i < j; i++, j--)
            for (int p = 0; p < channels; p++)
                FFSWAP(int32_t, dst[i * channels + p], dst[j * channels + p]);
    }
        break;
    case AV_SAMPLE_FMT_S64: {
        int64_t *dst = (int64_t *)out->extended_data[0];
        for (int i = 0, j = out->nb_samples - 1; i < j; i++, j--)
            for (int p = 0; p < channels; p++)
                FFSWAP(int64_t, dst[i * channels + p], dst[j * channels + p]);
    }
        break;
    case AV_SAMPLE_FMT_FLT: {
        float *dst = (float *)out->extended_data[0];
        for (int i = 0, j = out->nb_samples - 1; i < j; i++, j--)
            for (int p = 0; p < channels; p++)
                FFSWAP(float, dst[i * channels + p], dst[j * channels + p]);
    }
        break;
    case AV_SAMPLE_FMT_DBL: {
        double *dst = (double *)out->extended_data[0];
        for (int i = 0, j = out->nb_samples - 1; i < j; i++, j--)
            for (int p = 0; p < channels; p++)
                FFSWAP(double, dst[i * channels + p], dst[j * channels + p]);
    }
        break;
    }
}

static int areverse_request_frame(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    ReverseContext *s = ctx->priv;
    int ret;

    ret = ff_request_frame(ctx->inputs[0]);

    if (ret == AVERROR_EOF && s->nb_frames > 0) {
        AVFrame *out = s->frames[s->nb_frames - 1];
        out->pts     = s->pts[s->flush_idx++] - s->nb_samples;
        s->nb_samples += s->pts[s->flush_idx] - s->pts[s->flush_idx - 1] - out->nb_samples;

        if (av_sample_fmt_is_planar(out->format))
            reverse_samples_planar(out);
        else
            reverse_samples_packed(out);
        ret = ff_filter_frame(outlink, out);
        s->frames[s->nb_frames - 1] = NULL;
        s->nb_frames--;
    }

    return ret;
}

static const AVFilterPad areverse_inputs[] = {
    {
        .name           = "default",
        .type           = AVMEDIA_TYPE_AUDIO,
        .flags          = AVFILTERPAD_FLAG_NEEDS_WRITABLE,
        .filter_frame   = filter_frame,
    },
};

static const AVFilterPad areverse_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_AUDIO,
        .request_frame = areverse_request_frame,
    },
};

const AVFilter ff_af_areverse = {
    .name          = "areverse",
    .description   = NULL_IF_CONFIG_SMALL("Reverse an audio clip."),
    .priv_size     = sizeof(ReverseContext),
    .init          = init,
    .uninit        = uninit,
    FILTER_INPUTS(areverse_inputs),
    FILTER_OUTPUTS(areverse_outputs),
};

#endif /* CONFIG_AREVERSE_FILTER */
