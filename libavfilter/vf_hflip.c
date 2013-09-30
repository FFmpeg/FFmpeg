/*
 * Copyright (c) 2007 Benoit Fouet
 * Copyright (c) 2010 Stefano Sabatini
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
 * horizontal flip filter
 */

#include <string.h>

#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"
#include "libavutil/pixdesc.h"
#include "libavutil/internal.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/imgutils.h"

typedef struct {
    int max_step[4];    ///< max pixel step for each plane, expressed as a number of bytes
    int planewidth[4];  ///< width of each plane
    int planeheight[4]; ///< height of each plane
} FlipContext;

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *pix_fmts = NULL;
    int fmt;

    for (fmt = 0; fmt < AV_PIX_FMT_NB; fmt++) {
        const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(fmt);
        if (!(desc->flags & AV_PIX_FMT_FLAG_HWACCEL ||
              desc->flags & AV_PIX_FMT_FLAG_BITSTREAM ||
              (desc->log2_chroma_w != desc->log2_chroma_h &&
               desc->comp[0].plane == desc->comp[1].plane)))
            ff_add_format(&pix_fmts, fmt);
    }

    ff_set_common_formats(ctx, pix_fmts);
    return 0;
}

static int config_props(AVFilterLink *inlink)
{
    FlipContext *s = inlink->dst->priv;
    const AVPixFmtDescriptor *pix_desc = av_pix_fmt_desc_get(inlink->format);
    const int hsub = pix_desc->log2_chroma_w;
    const int vsub = pix_desc->log2_chroma_h;

    av_image_fill_max_pixsteps(s->max_step, NULL, pix_desc);
    s->planewidth[0]  = s->planewidth[3]  = inlink->w;
    s->planewidth[1]  = s->planewidth[2]  = FF_CEIL_RSHIFT(inlink->w, hsub);
    s->planeheight[0] = s->planeheight[3] = inlink->h;
    s->planeheight[1] = s->planeheight[2] = FF_CEIL_RSHIFT(inlink->h, vsub);

    return 0;
}

typedef struct ThreadData {
    AVFrame *in, *out;
} ThreadData;

static int filter_slice(AVFilterContext *ctx, void *arg, int job, int nb_jobs)
{
    FlipContext *s = ctx->priv;
    ThreadData *td = arg;
    AVFrame *in = td->in;
    AVFrame *out = td->out;
    uint8_t *inrow, *outrow;
    int i, j, plane, step;

    for (plane = 0; plane < 4 && in->data[plane] && in->linesize[plane]; plane++) {
        const int width  = s->planewidth[plane];
        const int height = s->planeheight[plane];
        const int start = (height *  job   ) / nb_jobs;
        const int end   = (height * (job+1)) / nb_jobs;

        step = s->max_step[plane];

        outrow = out->data[plane] + start * out->linesize[plane];
        inrow  = in ->data[plane] + start * in->linesize[plane] + (width - 1) * step;
        for (i = start; i < end; i++) {
            switch (step) {
            case 1:
                for (j = 0; j < width; j++)
                    outrow[j] = inrow[-j];
            break;

            case 2:
            {
                uint16_t *outrow16 = (uint16_t *)outrow;
                uint16_t * inrow16 = (uint16_t *) inrow;
                for (j = 0; j < width; j++)
                    outrow16[j] = inrow16[-j];
            }
            break;

            case 3:
            {
                uint8_t *in  =  inrow;
                uint8_t *out = outrow;
                for (j = 0; j < width; j++, out += 3, in -= 3) {
                    int32_t v = AV_RB24(in);
                    AV_WB24(out, v);
                }
            }
            break;

            case 4:
            {
                uint32_t *outrow32 = (uint32_t *)outrow;
                uint32_t * inrow32 = (uint32_t *) inrow;
                for (j = 0; j < width; j++)
                    outrow32[j] = inrow32[-j];
            }
            break;

            default:
                for (j = 0; j < width; j++)
                    memcpy(outrow + j*step, inrow - j*step, step);
            }

            inrow  += in ->linesize[plane];
            outrow += out->linesize[plane];
        }
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx  = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    ThreadData td;
    AVFrame *out;

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out, in);

    /* copy palette if required */
    if (av_pix_fmt_desc_get(inlink->format)->flags & AV_PIX_FMT_FLAG_PAL)
        memcpy(out->data[1], in->data[1], AVPALETTE_SIZE);

    td.in = in, td.out = out;
    ctx->internal->execute(ctx, filter_slice, &td, NULL, FFMIN(outlink->h, ctx->graph->nb_threads));

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static const AVFilterPad avfilter_vf_hflip_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = config_props,
    },
    { NULL }
};

static const AVFilterPad avfilter_vf_hflip_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter avfilter_vf_hflip = {
    .name          = "hflip",
    .description   = NULL_IF_CONFIG_SMALL("Horizontally flip the input video."),
    .priv_size     = sizeof(FlipContext),
    .query_formats = query_formats,
    .inputs        = avfilter_vf_hflip_inputs,
    .outputs       = avfilter_vf_hflip_outputs,
    .flags         = AVFILTER_FLAG_SLICE_THREADS,
};
