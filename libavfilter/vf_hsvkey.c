/*
 * Copyright (c) 2021 Paul B Mahol
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

#include "libavutil/opt.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "internal.h"

typedef struct HSVKeyContext {
    const AVClass *class;

    float hue, hue_opt, sat, val;
    float similarity;
    float blend;

    float scale;

    float half;

    int depth;
    int max;

    int hsub_log2;
    int vsub_log2;

    int (*do_slice)(AVFilterContext *ctx, void *arg,
                    int jobnr, int nb_jobs);
} HSVKeyContext;

#define SQR(x) ((x)*(x))

static int do_hsvkey_pixel(HSVKeyContext *s, int y, int u, int v,
                           float hue_key, float sat_key, float val_key)
{
    const float similarity = s->similarity;
    const float scale = s->scale;
    const float blend = s->blend;
    const int imax = s->max;
    const float max = imax;
    const float half = s->half;
    const float uf = u - half;
    const float vf = v - half;
    const float hue = hue_key < 0.f ? -hue_key : atan2f(uf, vf) + M_PI;
    const float sat = sat_key < 0.f ? -sat_key : sqrtf((uf * uf + vf * vf) / (half * half * 2.f));
    const float val = val_key < 0.f ? -val_key : scale * y;
    float diff;

    hue_key = fabsf(hue_key);
    sat_key = fabsf(sat_key);
    val_key = fabsf(val_key);

    diff = sqrtf(fmaxf(SQR(sat) * SQR(val) +
                 SQR(sat_key) * SQR(val_key) -
                 2.f * sat * val * sat_key * val_key * cosf(hue_key - hue) +
                 SQR(val - val_key), 0.f));
    if (diff < similarity) {
        return 0;
    } else if (blend > FLT_MIN) {
        return av_clipf((diff - similarity) / blend, 0.f, 1.f) * max;
    } else {
        return imax;
    }

    return 0;
}

static int do_hsvkey_slice(AVFilterContext *avctx, void *arg, int jobnr, int nb_jobs)
{
    HSVKeyContext *s = avctx->priv;
    AVFrame *frame = arg;
    const int slice_start = (frame->height * jobnr) / nb_jobs;
    const int slice_end = (frame->height * (jobnr + 1)) / nb_jobs;
    const int hsub_log2 = s->hsub_log2;
    const int vsub_log2 = s->vsub_log2;
    const float hue = s->hue;
    const float sat = s->sat;
    const float val = s->val;

    for (int y = slice_start; y < slice_end; y++) {
        for (int x = 0; x < frame->width; x++) {
            int Y = frame->data[0][frame->linesize[0] * y + x];
            int u = frame->data[1][frame->linesize[1] * (y >> vsub_log2) + (x >> hsub_log2)];
            int v = frame->data[2][frame->linesize[2] * (y >> vsub_log2) + (x >> hsub_log2)];

            frame->data[3][frame->linesize[3] * y + x] = do_hsvkey_pixel(s, Y, u, v, hue, sat, val);
        }
    }

    return 0;
}

static int do_hsvkey16_slice(AVFilterContext *avctx, void *arg, int jobnr, int nb_jobs)
{
    HSVKeyContext *s = avctx->priv;
    AVFrame *frame = arg;
    const int slice_start = (frame->height * jobnr) / nb_jobs;
    const int slice_end = (frame->height * (jobnr + 1)) / nb_jobs;
    const int hsub_log2 = s->hsub_log2;
    const int vsub_log2 = s->vsub_log2;
    const float hue = s->hue;
    const float sat = s->sat;
    const float val = s->val;

    for (int y = slice_start; y < slice_end; ++y) {
        for (int x = 0; x < frame->width; ++x) {
            uint16_t *dst = (uint16_t *)(frame->data[3] + frame->linesize[3] * y);
            int Y = AV_RN16(&frame->data[0][frame->linesize[0] * y + 2 * x]);
            int u = AV_RN16(&frame->data[1][frame->linesize[1] * (y >> vsub_log2) + 2 * (x >> hsub_log2)]);
            int v = AV_RN16(&frame->data[2][frame->linesize[2] * (y >> vsub_log2) + 2 * (x >> hsub_log2)]);

            dst[x] = do_hsvkey_pixel(s, Y, u, v, hue, sat, val);
        }
    }

    return 0;
}

static int do_hsvhold_slice(AVFilterContext *avctx, void *arg, int jobnr, int nb_jobs)
{
    HSVKeyContext *s = avctx->priv;
    AVFrame *frame = arg;
    const int hsub_log2 = s->hsub_log2;
    const int vsub_log2 = s->vsub_log2;
    const int width = frame->width >> hsub_log2;
    const int height = frame->height >> vsub_log2;
    const int slice_start = (height * jobnr) / nb_jobs;
    const int slice_end = (height * (jobnr + 1)) / nb_jobs;
    const float scale = s->scale;
    const float hue = s->hue;
    const float sat = s->sat;
    const float val = s->val;

    for (int y = slice_start; y < slice_end; ++y) {
        for (int x = 0; x < width; ++x) {
            uint8_t *dstu = frame->data[1] + frame->linesize[1] * y;
            uint8_t *dstv = frame->data[2] + frame->linesize[2] * y;
            int Y = frame->data[0][frame->linesize[0] * (y << vsub_log2) + (x << hsub_log2)];
            int u = frame->data[1][frame->linesize[1] * y + x];
            int v = frame->data[2][frame->linesize[2] * y + x];
            int t = do_hsvkey_pixel(s, Y, u, v, hue, sat, val);

            if (t > 0) {
                float f = 1.f - t * scale;

                dstu[x] = 128 + (u - 128) * f;
                dstv[x] = 128 + (v - 128) * f;
            }
        }
    }

    return 0;
}

static int do_hsvhold16_slice(AVFilterContext *avctx, void *arg, int jobnr, int nb_jobs)
{
    HSVKeyContext *s = avctx->priv;
    AVFrame *frame = arg;
    const int hsub_log2 = s->hsub_log2;
    const int vsub_log2 = s->vsub_log2;
    const int width = frame->width >> hsub_log2;
    const int height = frame->height >> vsub_log2;
    const int slice_start = (height * jobnr) / nb_jobs;
    const int slice_end = (height * (jobnr + 1)) / nb_jobs;
    const float scale = s->scale;
    const float half = s->half;
    const float hue = s->hue;
    const float sat = s->sat;
    const float val = s->val;

    for (int y = slice_start; y < slice_end; ++y) {
        for (int x = 0; x < width; ++x) {
            uint16_t *dstu = (uint16_t *)(frame->data[1] + frame->linesize[1] * y);
            uint16_t *dstv = (uint16_t *)(frame->data[2] + frame->linesize[2] * y);
            int Y = AV_RN16(&frame->data[0][frame->linesize[0] * (y << vsub_log2) + 2 * (x << hsub_log2)]);
            int u = AV_RN16(&frame->data[1][frame->linesize[1] * y + 2 * x]);
            int v = AV_RN16(&frame->data[2][frame->linesize[2] * y + 2 * x]);
            int t = do_hsvkey_pixel(s, Y, u, v, hue, sat, val);

            if (t > 0) {
                float f = 1.f - t * scale;

                dstu[x] = half + (u - half) * f;
                dstv[x] = half + (v - half) * f;
            }
        }
    }

    return 0;
}

static int filter_frame(AVFilterLink *link, AVFrame *frame)
{
    AVFilterContext *avctx = link->dst;
    HSVKeyContext *s = avctx->priv;
    int res;

    s->hue = FFSIGN(s->hue_opt) *M_PI * fmodf(526.f - fabsf(s->hue_opt), 360.f) / 180.f;
    if (res = ff_filter_execute(avctx, s->do_slice, frame, NULL,
                                FFMIN(frame->height, ff_filter_get_nb_threads(avctx))))
        return res;

    return ff_filter_frame(avctx->outputs[0], frame);
}

static av_cold int config_output(AVFilterLink *outlink)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(outlink->format);
    AVFilterContext *avctx = outlink->src;
    HSVKeyContext *s = avctx->priv;

    s->depth = desc->comp[0].depth;
    s->max = (1 << s->depth) - 1;
    s->half = 0.5f * s->max;
    s->scale = 1.f / s->max;

    if (!strcmp(avctx->filter->name, "hsvkey")) {
        s->do_slice = s->depth <= 8 ? do_hsvkey_slice : do_hsvkey16_slice;
    } else {
        s->do_slice = s->depth <= 8 ? do_hsvhold_slice: do_hsvhold16_slice;
    }

    return 0;
}

static av_cold int config_input(AVFilterLink *inlink)
{
    AVFilterContext *avctx = inlink->dst;
    HSVKeyContext *s = avctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);

    s->hsub_log2 = desc->log2_chroma_w;
    s->vsub_log2 = desc->log2_chroma_h;

    return 0;
}

static const enum AVPixelFormat key_pixel_fmts[] = {
    AV_PIX_FMT_YUVA420P,
    AV_PIX_FMT_YUVA422P,
    AV_PIX_FMT_YUVA444P,
    AV_PIX_FMT_YUVA420P9,  AV_PIX_FMT_YUVA422P9,  AV_PIX_FMT_YUVA444P9,
    AV_PIX_FMT_YUVA420P10, AV_PIX_FMT_YUVA422P10, AV_PIX_FMT_YUVA444P10,
    AV_PIX_FMT_YUVA422P12, AV_PIX_FMT_YUVA444P12,
    AV_PIX_FMT_YUVA420P16, AV_PIX_FMT_YUVA422P16, AV_PIX_FMT_YUVA444P16,
    AV_PIX_FMT_NONE
};

static const AVFilterPad inputs[] = {
    {
        .name           = "default",
        .type           = AVMEDIA_TYPE_VIDEO,
        .flags          = AVFILTERPAD_FLAG_NEEDS_WRITABLE,
        .filter_frame   = filter_frame,
        .config_props   = config_input,
    },
};

static const AVFilterPad outputs[] = {
    {
        .name           = "default",
        .type           = AVMEDIA_TYPE_VIDEO,
        .config_props   = config_output,
    },
};

#define OFFSET(x) offsetof(HSVKeyContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption hsvkey_options[] = {
    { "hue", "set the hue value", OFFSET(hue_opt), AV_OPT_TYPE_FLOAT, { .dbl = 0 }, -360, 360, FLAGS },
    { "sat", "set the saturation value", OFFSET(sat), AV_OPT_TYPE_FLOAT, { .dbl = 0 }, -1, 1, FLAGS },
    { "val", "set the value value", OFFSET(val), AV_OPT_TYPE_FLOAT, { .dbl = 0 }, -1, 1, FLAGS },
    { "similarity", "set the hsvkey similarity value", OFFSET(similarity), AV_OPT_TYPE_FLOAT, { .dbl = 0.01}, 0.00001, 1.0, FLAGS },
    { "blend", "set the hsvkey blend value", OFFSET(blend), AV_OPT_TYPE_FLOAT, { .dbl = 0.0 }, 0.0, 1.0, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(hsvkey);

const AVFilter ff_vf_hsvkey = {
    .name          = "hsvkey",
    .description   = NULL_IF_CONFIG_SMALL("Turns a certain HSV range into transparency. Operates on YUV colors."),
    .priv_size     = sizeof(HSVKeyContext),
    .priv_class    = &hsvkey_class,
    FILTER_INPUTS(inputs),
    FILTER_OUTPUTS(outputs),
    FILTER_PIXFMTS_ARRAY(key_pixel_fmts),
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC | AVFILTER_FLAG_SLICE_THREADS,
    .process_command = ff_filter_process_command,
};

static const enum AVPixelFormat hold_pixel_fmts[] = {
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUV422P,
    AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_YUVA420P,
    AV_PIX_FMT_YUVA422P,
    AV_PIX_FMT_YUVA444P,
    AV_PIX_FMT_YUV420P9,   AV_PIX_FMT_YUV422P9,   AV_PIX_FMT_YUV444P9,
    AV_PIX_FMT_YUV420P10,  AV_PIX_FMT_YUV422P10,  AV_PIX_FMT_YUV444P10,
    AV_PIX_FMT_YUV444P12,  AV_PIX_FMT_YUV422P12,  AV_PIX_FMT_YUV420P12,
    AV_PIX_FMT_YUV444P14,  AV_PIX_FMT_YUV422P14,  AV_PIX_FMT_YUV420P14,
    AV_PIX_FMT_YUV420P16,  AV_PIX_FMT_YUV422P16,  AV_PIX_FMT_YUV444P16,
    AV_PIX_FMT_YUVA420P9,  AV_PIX_FMT_YUVA422P9,  AV_PIX_FMT_YUVA444P9,
    AV_PIX_FMT_YUVA420P10, AV_PIX_FMT_YUVA422P10, AV_PIX_FMT_YUVA444P10,
    AV_PIX_FMT_YUVA422P12, AV_PIX_FMT_YUVA444P12,
    AV_PIX_FMT_YUVA420P16, AV_PIX_FMT_YUVA422P16, AV_PIX_FMT_YUVA444P16,
    AV_PIX_FMT_NONE
};

static const AVOption hsvhold_options[] = {
    { "hue", "set the hue value", OFFSET(hue_opt), AV_OPT_TYPE_FLOAT, { .dbl = 0 }, -360, 360, FLAGS },
    { "sat", "set the saturation value", OFFSET(sat), AV_OPT_TYPE_FLOAT, { .dbl = 0 }, -1, 1, FLAGS },
    { "val", "set the value value", OFFSET(val), AV_OPT_TYPE_FLOAT, { .dbl = 0 }, -1, 1, FLAGS },
    { "similarity", "set the hsvhold similarity value", OFFSET(similarity), AV_OPT_TYPE_FLOAT, { .dbl = 0.01 }, 0.00001, 1.0, FLAGS },
    { "blend", "set the hsvhold blend value", OFFSET(blend), AV_OPT_TYPE_FLOAT, { .dbl = 0.0 }, 0.0, 1.0, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(hsvhold);

const AVFilter ff_vf_hsvhold = {
    .name          = "hsvhold",
    .description   = NULL_IF_CONFIG_SMALL("Turns a certain HSV range into gray."),
    .priv_size     = sizeof(HSVKeyContext),
    .priv_class    = &hsvhold_class,
    FILTER_INPUTS(inputs),
    FILTER_OUTPUTS(outputs),
    FILTER_PIXFMTS_ARRAY(hold_pixel_fmts),
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC | AVFILTER_FLAG_SLICE_THREADS,
    .process_command = ff_filter_process_command,
};
