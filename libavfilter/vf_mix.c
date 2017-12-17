/*
 * Copyright (c) 2017 Paul B Mahol
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
#include "libavutil/imgutils.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"

#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "framesync.h"
#include "video.h"

typedef struct MixContext {
    const AVClass *class;
    const AVPixFmtDescriptor *desc;
    char *weights_str;
    int nb_inputs;
    int duration;
    float *weights;
    float wfactor;

    int depth;
    int nb_planes;
    int linesize[4];
    int height[4];

    AVFrame **frames;
    FFFrameSync fs;
} MixContext;

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *pix_fmts = NULL;
    int fmt, ret;

    for (fmt = 0; av_pix_fmt_desc_get(fmt); fmt++) {
        const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(fmt);
        if (!(desc->flags & AV_PIX_FMT_FLAG_PAL ||
              desc->flags & AV_PIX_FMT_FLAG_HWACCEL ||
              desc->flags & AV_PIX_FMT_FLAG_BITSTREAM) &&
            (ret = ff_add_format(&pix_fmts, fmt)) < 0)
            return ret;
    }

    return ff_set_common_formats(ctx, pix_fmts);
}

static av_cold int init(AVFilterContext *ctx)
{
    MixContext *s = ctx->priv;
    char *p, *arg, *saveptr = NULL;
    int i, ret;

    s->frames = av_calloc(s->nb_inputs, sizeof(*s->frames));
    if (!s->frames)
        return AVERROR(ENOMEM);

    s->weights = av_calloc(s->nb_inputs, sizeof(*s->weights));
    if (!s->weights)
        return AVERROR(ENOMEM);

    for (i = 0; i < s->nb_inputs; i++) {
        AVFilterPad pad = { 0 };

        pad.type = AVMEDIA_TYPE_VIDEO;
        pad.name = av_asprintf("input%d", i);
        if (!pad.name)
            return AVERROR(ENOMEM);

        if ((ret = ff_insert_inpad(ctx, i, &pad)) < 0) {
            av_freep(&pad.name);
            return ret;
        }
    }

    p = s->weights_str;
    for (i = 0; i < s->nb_inputs; i++) {
        if (!(arg = av_strtok(p, " ", &saveptr)))
            break;

        p = NULL;
        sscanf(arg, "%f", &s->weights[i]);
        s->wfactor += s->weights[i];
    }
    s->wfactor = 1 / s->wfactor;

    return 0;
}

static int process_frame(FFFrameSync *fs)
{
    AVFilterContext *ctx = fs->parent;
    AVFilterLink *outlink = ctx->outputs[0];
    MixContext *s = fs->opaque;
    AVFrame **in = s->frames;
    AVFrame *out;
    int i, p, ret, x, y;

    for (i = 0; i < s->nb_inputs; i++) {
        if ((ret = ff_framesync_get_frame(&s->fs, i, &in[i], 0)) < 0)
            return ret;
    }

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out)
        return AVERROR(ENOMEM);
    out->pts = av_rescale_q(s->fs.pts, s->fs.time_base, outlink->time_base);

    if (s->depth <= 8) {
        for (p = 0; p < s->nb_planes; p++) {
            uint8_t *dst = out->data[p];

            for (y = 0; y < s->height[p]; y++) {
                for (x = 0; x < s->linesize[p]; x++) {
                    int val = 0;

                    for (i = 0; i < s->nb_inputs; i++) {
                        uint8_t src = in[i]->data[p][y * s->linesize[p] + x];

                        val += src * s->weights[i];
                    }

                    dst[x] = val * s->wfactor;
                }

                dst += out->linesize[p];
            }
        }
    } else {
        for (p = 0; p < s->nb_planes; p++) {
            uint16_t *dst = (uint16_t *)out->data[p];

            for (y = 0; y < s->height[p]; y++) {
                for (x = 0; x < s->linesize[p]; x++) {
                    int val = 0;

                    for (i = 0; i < s->nb_inputs; i++) {
                        uint16_t src = AV_RN16(in[i]->data[p] + y * s->linesize[p] + x * 2);

                        val += src * s->weights[i];
                    }

                    dst[x] = val * s->wfactor;
                }

                dst += out->linesize[p] / 2;
            }
        }
    }

    return ff_filter_frame(outlink, out);
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    MixContext *s = ctx->priv;
    AVRational time_base = ctx->inputs[0]->time_base;
    AVRational frame_rate = ctx->inputs[0]->frame_rate;
    AVFilterLink *inlink = ctx->inputs[0];
    int height = ctx->inputs[0]->h;
    int width = ctx->inputs[0]->w;
    FFFrameSyncIn *in;
    int i, ret;

    for (i = 1; i < s->nb_inputs; i++) {
        if (ctx->inputs[i]->h != height || ctx->inputs[i]->w != width) {
            av_log(ctx, AV_LOG_ERROR, "Input %d size (%dx%d) does not match input %d size (%dx%d).\n", i, ctx->inputs[i]->w, ctx->inputs[i]->h, 0, width, height);
            return AVERROR(EINVAL);
        }
    }

    s->desc = av_pix_fmt_desc_get(outlink->format);
    if (!s->desc)
        return AVERROR_BUG;
    s->nb_planes = av_pix_fmt_count_planes(outlink->format);
    s->depth = s->desc->comp[0].depth;

    outlink->w          = width;
    outlink->h          = height;
    outlink->time_base  = time_base;
    outlink->frame_rate = frame_rate;

    if ((ret = ff_framesync_init(&s->fs, ctx, s->nb_inputs)) < 0)
        return ret;

    in = s->fs.in;
    s->fs.opaque = s;
    s->fs.on_event = process_frame;

    if ((ret = av_image_fill_linesizes(s->linesize, inlink->format, inlink->w)) < 0)
        return ret;

    s->height[1] = s->height[2] = AV_CEIL_RSHIFT(inlink->h, s->desc->log2_chroma_h);
    s->height[0] = s->height[3] = inlink->h;

    for (i = 0; i < s->nb_inputs; i++) {
        AVFilterLink *inlink = ctx->inputs[i];

        in[i].time_base = inlink->time_base;
        in[i].sync   = 1;
        in[i].before = EXT_STOP;
        in[i].after  = (s->duration == 1 || (s->duration == 2 && i == 0)) ? EXT_STOP : EXT_INFINITY;
    }

    return ff_framesync_configure(&s->fs);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    MixContext *s = ctx->priv;
    int i;

    ff_framesync_uninit(&s->fs);
    av_freep(&s->frames);
    av_freep(&s->weights);

    for (i = 0; i < ctx->nb_inputs; i++)
        av_freep(&ctx->input_pads[i].name);
}

static int activate(AVFilterContext *ctx)
{
    MixContext *s = ctx->priv;
    return ff_framesync_activate(&s->fs);
}

#define OFFSET(x) offsetof(MixContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_FILTERING_PARAM

static const AVOption mix_options[] = {
    { "inputs", "set number of inputs", OFFSET(nb_inputs), AV_OPT_TYPE_INT, {.i64=2}, 2, INT_MAX, .flags = FLAGS },
    { "weights", "set weight for each input", OFFSET(weights_str), AV_OPT_TYPE_STRING, {.str="1 1"}, 0, 0, .flags = FLAGS },
    { "duration", "how to determine end of stream", OFFSET(duration), AV_OPT_TYPE_INT, {.i64=0}, 0, 2, .flags = FLAGS, "duration" },
        { "longest",  "Duration of longest input",  0, AV_OPT_TYPE_CONST, {.i64=0}, 0, 0, FLAGS, "duration" },
        { "shortest", "Duration of shortest input", 0, AV_OPT_TYPE_CONST, {.i64=1}, 0, 0, FLAGS, "duration" },
        { "first",    "Duration of first input",    0, AV_OPT_TYPE_CONST, {.i64=2}, 0, 0, FLAGS, "duration" },
    { NULL },
};

static const AVFilterPad outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
    },
    { NULL }
};

AVFILTER_DEFINE_CLASS(mix);

AVFilter ff_vf_mix = {
    .name          = "mix",
    .description   = NULL_IF_CONFIG_SMALL("Mix video inputs."),
    .priv_size     = sizeof(MixContext),
    .priv_class    = &mix_class,
    .query_formats = query_formats,
    .outputs       = outputs,
    .init          = init,
    .uninit        = uninit,
    .activate      = activate,
    .flags         = AVFILTER_FLAG_DYNAMIC_INPUTS,
};
