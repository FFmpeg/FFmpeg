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
#include "filters.h"
#include "formats.h"
#include "hflip.h"
#include "vf_hflip_init.h"
#include "video.h"
#include "libavutil/pixdesc.h"
#include "libavutil/internal.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/imgutils.h"

static int query_formats(const AVFilterContext *ctx,
                         AVFilterFormatsConfig **cfg_in,
                         AVFilterFormatsConfig **cfg_out)
{
    AVFilterFormats *pix_fmts = NULL;
    const AVPixFmtDescriptor *desc;
    int fmt, ret;

    for (fmt = 0; desc = av_pix_fmt_desc_get(fmt); fmt++) {
        if (!(desc->flags & AV_PIX_FMT_FLAG_HWACCEL ||
              desc->flags & AV_PIX_FMT_FLAG_BITSTREAM ||
              (desc->log2_chroma_w != desc->log2_chroma_h &&
               desc->comp[0].plane == desc->comp[1].plane)) &&
            (ret = ff_add_format(&pix_fmts, fmt)) < 0)
            return ret;
    }

    return ff_set_common_formats2(ctx, cfg_in, cfg_out, pix_fmts);
}

static int config_props(AVFilterLink *inlink)
{
    FlipContext *s = inlink->dst->priv;
    const AVPixFmtDescriptor *pix_desc = av_pix_fmt_desc_get(inlink->format);
    const int hsub = pix_desc->log2_chroma_w;
    const int vsub = pix_desc->log2_chroma_h;
    int nb_planes;

    av_image_fill_max_pixsteps(s->max_step, NULL, pix_desc);
    s->planewidth[0]  = s->planewidth[3]  = inlink->w;
    s->planewidth[1]  = s->planewidth[2]  = AV_CEIL_RSHIFT(inlink->w, hsub);
    s->planeheight[0] = s->planeheight[3] = inlink->h;
    s->planeheight[1] = s->planeheight[2] = AV_CEIL_RSHIFT(inlink->h, vsub);
    s->bayer_plus1    = !!(pix_desc->flags & AV_PIX_FMT_FLAG_BAYER) + 1;

    nb_planes = av_pix_fmt_count_planes(inlink->format);

    return ff_hflip_init(s, s->max_step, nb_planes);
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
    int i, plane, step;

    for (plane = 0; plane < 4 && in->data[plane] && in->linesize[plane]; plane++) {
        const int width  = s->planewidth[plane] / s->bayer_plus1;
        const int height = s->planeheight[plane];
        const int start = (height *  job   ) / nb_jobs;
        const int end   = (height * (job+1)) / nb_jobs;

        step = s->max_step[plane];

        outrow = out->data[plane] + start * out->linesize[plane];
        inrow  = in ->data[plane] + start * in->linesize[plane] + (width - 1) * step;
        for (i = start; i < end; i++) {
            s->flip_line[plane](inrow, outrow, width);

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
    ff_filter_execute(ctx, filter_slice, &td, NULL,
                      FFMIN(outlink->h, ff_filter_get_nb_threads(ctx)));

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
};

const AVFilter ff_vf_hflip = {
    .name          = "hflip",
    .description   = NULL_IF_CONFIG_SMALL("Horizontally flip the input video."),
    .priv_size     = sizeof(FlipContext),
    FILTER_INPUTS(avfilter_vf_hflip_inputs),
    FILTER_OUTPUTS(ff_video_default_filterpad),
    FILTER_QUERY_FUNC2(query_formats),
    .flags         = AVFILTER_FLAG_SLICE_THREADS | AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
