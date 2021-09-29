/*
 * Copyright (c) 2003 Tobias Diedrich
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "libavutil/imgutils.h"
#include "avfilter.h"
#include "internal.h"

typedef struct RepeatFieldsContext {
    const AVClass *class;
    int state;
    int nb_planes;
    int linesize[4];
    int planeheight[4];
    AVFrame *frame;
} RepeatFieldsContext;

static av_cold void uninit(AVFilterContext *ctx)
{
    RepeatFieldsContext *s = ctx->priv;

    av_frame_free(&s->frame);
}

static const enum AVPixelFormat pixel_fmts_eq[] = {
    AV_PIX_FMT_GRAY8,
    AV_PIX_FMT_YUV410P,
    AV_PIX_FMT_YUV411P,
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUV422P,
    AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_NONE
};

static int config_input(AVFilterLink *inlink)
{
    RepeatFieldsContext *s = inlink->dst->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    int ret;

    if ((ret = av_image_fill_linesizes(s->linesize, inlink->format, inlink->w)) < 0)
        return ret;

    s->planeheight[1] = s->planeheight[2] = AV_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
    s->planeheight[0] = s->planeheight[3] = inlink->h;

    s->nb_planes = av_pix_fmt_count_planes(inlink->format);

    return 0;
}

static void update_pts(AVFilterLink *link, AVFrame *f, int64_t pts, int fields)
{
    if (av_cmp_q(link->frame_rate, (AVRational){30000, 1001}) == 0 &&
         av_cmp_q(link->time_base, (AVRational){1001, 60000}) <= 0
    ) {
        f->pts = pts + av_rescale_q(fields, (AVRational){1001, 60000}, link->time_base);
    } else
        f->pts = AV_NOPTS_VALUE;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in) {
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = inlink->dst->outputs[0];
    RepeatFieldsContext *s = ctx->priv;
    AVFrame *out;
    int ret, i;
    int state = s->state;

    if (!s->frame) {
        s->frame = av_frame_clone(in);
        if (!s->frame)
            return AVERROR(ENOMEM);
        s->frame->pts = AV_NOPTS_VALUE;
    }

    out = s->frame;

    if ((state == 0 && !in->top_field_first) ||
        (state == 1 &&  in->top_field_first)) {
        av_log(ctx, AV_LOG_WARNING, "Unexpected field flags: "
                                    "state=%d top_field_first=%d repeat_first_field=%d\n",
                                    state, in->top_field_first, in->repeat_pict);
        state ^= 1;
    }

    if (state == 0) {
        AVFrame *new;

        new = av_frame_clone(in);
        if (!new)
            return AVERROR(ENOMEM);

        ret = ff_filter_frame(outlink, new);

        if (in->repeat_pict) {
            av_frame_make_writable(out);
            update_pts(outlink, out, in->pts, 2);
            for (i = 0; i < s->nb_planes; i++) {
                av_image_copy_plane(out->data[i], out->linesize[i] * 2,
                                    in->data[i], in->linesize[i] * 2,
                                    s->linesize[i], s->planeheight[i] / 2);
            }
            state = 1;
        }
    } else {
        for (i = 0; i < s->nb_planes; i++) {
            av_frame_make_writable(out);
            av_image_copy_plane(out->data[i] + out->linesize[i], out->linesize[i] * 2,
                                in->data[i] + in->linesize[i], in->linesize[i] * 2,
                                s->linesize[i], s->planeheight[i] / 2);
        }

        ret = ff_filter_frame(outlink, av_frame_clone(out));

        if (in->repeat_pict) {
            AVFrame *new;

            new = av_frame_clone(in);
            if (!new)
                return AVERROR(ENOMEM);

            ret = ff_filter_frame(outlink, new);
            state = 0;
        } else {
            av_frame_make_writable(out);
            update_pts(outlink, out, in->pts, 1);
            for (i = 0; i < s->nb_planes; i++) {
                av_image_copy_plane(out->data[i], out->linesize[i] * 2,
                                    in->data[i], in->linesize[i] * 2,
                                    s->linesize[i], s->planeheight[i] / 2);
            }
        }
    }

    s->state = state;

    av_frame_free(&in);
    return ret;
}

static const AVFilterPad repeatfields_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = config_input,
    },
};

static const AVFilterPad repeatfields_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
};

const AVFilter ff_vf_repeatfields = {
    .name          = "repeatfields",
    .description   = NULL_IF_CONFIG_SMALL("Hard repeat fields based on MPEG repeat field flag."),
    .priv_size     = sizeof(RepeatFieldsContext),
    .uninit        = uninit,
    FILTER_INPUTS(repeatfields_inputs),
    FILTER_OUTPUTS(repeatfields_outputs),
    FILTER_PIXFMTS_ARRAY(pixel_fmts_eq),
};
