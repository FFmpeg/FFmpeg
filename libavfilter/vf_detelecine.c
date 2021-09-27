/*
 * Copyright (c) 2015 Himangi Saraogi <himangi774@gmail.com>
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
 * @file detelecine filter.
 */


#include "libavutil/avstring.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

typedef struct DetelecineContext {
    const AVClass *class;
    int first_field;
    char *pattern;
    int start_frame;
    int init_len;
    unsigned int pattern_pos;
    unsigned int nskip_fields;
    int64_t start_time;

    AVRational pts;
    AVRational ts_unit;
    int occupied;

    int nb_planes;
    int planeheight[4];
    int stride[4];

    AVFrame *frame[2];
    AVFrame *temp;
} DetelecineContext;

#define OFFSET(x) offsetof(DetelecineContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption detelecine_options[] = {
    {"first_field", "select first field", OFFSET(first_field), AV_OPT_TYPE_INT,   {.i64=0}, 0, 1, FLAGS, "field"},
        {"top",    "select top field first",                0, AV_OPT_TYPE_CONST, {.i64=0}, 0, 0, FLAGS, "field"},
        {"t",      "select top field first",                0, AV_OPT_TYPE_CONST, {.i64=0}, 0, 0, FLAGS, "field"},
        {"bottom", "select bottom field first",             0, AV_OPT_TYPE_CONST, {.i64=1}, 0, 0, FLAGS, "field"},
        {"b",      "select bottom field first",             0, AV_OPT_TYPE_CONST, {.i64=1}, 0, 0, FLAGS, "field"},
    {"pattern", "pattern that describe for how many fields a frame is to be displayed", OFFSET(pattern), AV_OPT_TYPE_STRING, {.str="23"}, 0, 0, FLAGS},
    {"start_frame", "position of first frame with respect to the pattern if stream is cut", OFFSET(start_frame), AV_OPT_TYPE_INT, {.i64=0}, 0, 13, FLAGS},
    {NULL}
};

AVFILTER_DEFINE_CLASS(detelecine);

static av_cold int init(AVFilterContext *ctx)
{
    DetelecineContext *s = ctx->priv;
    const char *p;
    int max = 0;
    int sum = 0;

    if (!strlen(s->pattern)) {
        av_log(ctx, AV_LOG_ERROR, "No pattern provided.\n");
        return AVERROR_INVALIDDATA;
    }

    for (p = s->pattern; *p; p++) {
        if (!av_isdigit(*p)) {
            av_log(ctx, AV_LOG_ERROR, "Provided pattern includes non-numeric characters.\n");
            return AVERROR_INVALIDDATA;
        }

        sum += *p - '0';
        max = FFMAX(*p - '0', max);
        s->pts.num += *p - '0';
        s->pts.den += 2;
    }

    if (s->start_frame >= sum) {
        av_log(ctx, AV_LOG_ERROR, "Provided start_frame is too big.\n");
        return AVERROR_INVALIDDATA;
    }

    s->nskip_fields = 0;
    s->pattern_pos = 0;
    s->start_time = AV_NOPTS_VALUE;
    s->init_len = 0;

    if (s->start_frame != 0) {
        int nfields = 0;
        for (p = s->pattern; *p; p++) {
            nfields += *p - '0';
            s->pattern_pos++;
            if (nfields >= 2*s->start_frame) {
                s->init_len = nfields - 2*s->start_frame;
                break;
            }
        }
    }

    av_log(ctx, AV_LOG_INFO, "Detelecine pattern %s removes up to %d frames per frame, pts advance factor: %d/%d\n",
           s->pattern, (max + 1) / 2, s->pts.num, s->pts.den);

    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    int reject_flags = AV_PIX_FMT_FLAG_BITSTREAM |
                       AV_PIX_FMT_FLAG_PAL       |
                       AV_PIX_FMT_FLAG_HWACCEL;

    return ff_set_common_formats(ctx, ff_formats_pixdesc_filter(0, reject_flags));
}

static int config_input(AVFilterLink *inlink)
{
    DetelecineContext *s = inlink->dst->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    int ret;

    s->temp = ff_get_video_buffer(inlink, inlink->w, inlink->h);
    if (!s->temp)
        return AVERROR(ENOMEM);

    s->frame[0] = ff_get_video_buffer(inlink, inlink->w, inlink->h);
    if (!s->frame[0])
        return AVERROR(ENOMEM);

    s->frame[1] = ff_get_video_buffer(inlink, inlink->w, inlink->h);
    if (!s->frame[1])
        return AVERROR(ENOMEM);

    if ((ret = av_image_fill_linesizes(s->stride, inlink->format, inlink->w)) < 0)
        return ret;

    s->planeheight[1] = s->planeheight[2] = AV_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
    s->planeheight[0] = s->planeheight[3] = inlink->h;

    s->nb_planes = av_pix_fmt_count_planes(inlink->format);

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    DetelecineContext *s = ctx->priv;
    const AVFilterLink *inlink = ctx->inputs[0];
    AVRational fps = inlink->frame_rate;

    if (!fps.num || !fps.den) {
        av_log(ctx, AV_LOG_ERROR, "The input needs a constant frame rate; "
               "current rate of %d/%d is invalid\n", fps.num, fps.den);
        return AVERROR(EINVAL);
    }
    fps = av_mul_q(fps, av_inv_q(s->pts));
    av_log(ctx, AV_LOG_VERBOSE, "FPS: %d/%d -> %d/%d\n",
           inlink->frame_rate.num, inlink->frame_rate.den, fps.num, fps.den);

    outlink->frame_rate = fps;
    outlink->time_base = av_mul_q(inlink->time_base, s->pts);
    av_log(ctx, AV_LOG_VERBOSE, "TB: %d/%d -> %d/%d\n",
           inlink->time_base.num, inlink->time_base.den, outlink->time_base.num, outlink->time_base.den);

    s->ts_unit = av_inv_q(av_mul_q(fps, outlink->time_base));

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *inpicref)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    DetelecineContext *s = ctx->priv;
    int i, len = 0, ret = 0, out = 0;

    if (s->start_time == AV_NOPTS_VALUE)
        s->start_time = inpicref->pts;

    if (s->nskip_fields >= 2) {
        s->nskip_fields -= 2;
        av_frame_free(&inpicref);
        return 0;
    } else if (s->nskip_fields >= 1) {
        for (i = 0; i < s->nb_planes; i++) {
            av_image_copy_plane(s->temp->data[i], s->temp->linesize[i],
                                inpicref->data[i], inpicref->linesize[i],
                                s->stride[i],
                                s->planeheight[i]);
        }
        s->occupied = 1;
        s->nskip_fields--;
        av_frame_free(&inpicref);
        return 0;
    }

    if (s->nskip_fields == 0) {
        len = s->init_len;
        s->init_len = 0;
        while(!len && s->pattern[s->pattern_pos]) {
            len = s->pattern[s->pattern_pos] - '0';
            s->pattern_pos++;
        }

        if (!s->pattern[s->pattern_pos])
            s->pattern_pos = 0;

        if(!len) { // do not output any field as the entire pattern is zero
            av_frame_free(&inpicref);
            return 0;
        }

        if (len == 1 && s->occupied) {
            s->occupied = 0;
            // output THIS image as-is
            for (i = 0; i < s->nb_planes; i++)
                av_image_copy_plane(s->frame[out]->data[i], s->frame[out]->linesize[i],
                                    s->temp->data[i], s->temp->linesize[i],
                                    s->stride[i],
                                    s->planeheight[i]);
            len = 0;
            while(!len && s->pattern[s->pattern_pos]) {
                len = s->pattern[s->pattern_pos] - '0';
                s->pattern_pos++;
            }

            if (!s->pattern[s->pattern_pos])
                s->pattern_pos = 0;

            s->occupied = 0;
            ++out;
        }

        if (s->occupied) {
            for (i = 0; i < s->nb_planes; i++) {
                // fill in the EARLIER field from the new pic
                av_image_copy_plane(s->frame[out]->data[i] + s->frame[out]->linesize[i] * s->first_field,
                                    s->frame[out]->linesize[i] * 2,
                                    inpicref->data[i] + inpicref->linesize[i] * s->first_field,
                                    inpicref->linesize[i] * 2,
                                    s->stride[i],
                                    (s->planeheight[i] - s->first_field + 1) / 2);
                // fill in the LATER field from the buffered pic
                av_image_copy_plane(s->frame[out]->data[i] + s->frame[out]->linesize[i] * !s->first_field,
                                    s->frame[out]->linesize[i] * 2,
                                    s->temp->data[i] + s->temp->linesize[i] * !s->first_field,
                                    s->temp->linesize[i] * 2,
                                    s->stride[i],
                                    (s->planeheight[i] - !s->first_field + 1) / 2);
            }

            s->occupied = 0;
            if (len <= 2) {
                for (i = 0; i < s->nb_planes; i++) {
                    av_image_copy_plane(s->temp->data[i], s->temp->linesize[i],
                                        inpicref->data[i], inpicref->linesize[i],
                                        s->stride[i],
                                        s->planeheight[i]);
                }
                s->occupied = 1;
            }
            ++out;
            len = (len >= 3) ? len - 3 : 0;
        } else {
            if (len >= 2) {
                // output THIS image as-is
                for (i = 0; i < s->nb_planes; i++)
                    av_image_copy_plane(s->frame[out]->data[i], s->frame[out]->linesize[i],
                                        inpicref->data[i], inpicref->linesize[i],
                                        s->stride[i],
                                        s->planeheight[i]);
                len -= 2;
                ++out;
            } else if (len == 1) {
                // output THIS image as-is
                for (i = 0; i < s->nb_planes; i++)
                    av_image_copy_plane(s->frame[out]->data[i], s->frame[out]->linesize[i],
                                        inpicref->data[i], inpicref->linesize[i],
                                        s->stride[i],
                                        s->planeheight[i]);

                for (i = 0; i < s->nb_planes; i++) {
                    av_image_copy_plane(s->temp->data[i], s->temp->linesize[i],
                                        inpicref->data[i], inpicref->linesize[i],
                                        s->stride[i],
                                        s->planeheight[i]);
                }
                s->occupied = 1;

                len--;
                ++out;
            }
        }

        if (len == 1 && s->occupied)
        {
            len--;
            s->occupied = 0;
        }
    }
    s->nskip_fields = len;

    for (i = 0; i < out; ++i) {
        AVFrame *frame = av_frame_clone(s->frame[i]);

        if (!frame) {
            av_frame_free(&inpicref);
            return AVERROR(ENOMEM);
        }

        av_frame_copy_props(frame, inpicref);
        frame->pts = ((s->start_time == AV_NOPTS_VALUE) ? 0 : s->start_time) +
                     av_rescale(outlink->frame_count_in, s->ts_unit.num,
                                s->ts_unit.den);
        ret = ff_filter_frame(outlink, frame);
    }

    av_frame_free(&inpicref);

    return ret;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    DetelecineContext *s = ctx->priv;

    av_frame_free(&s->temp);
    av_frame_free(&s->frame[0]);
    av_frame_free(&s->frame[1]);
}

static const AVFilterPad detelecine_inputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .filter_frame  = filter_frame,
        .config_props  = config_input,
    },
};

static const AVFilterPad detelecine_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
    },
};

const AVFilter ff_vf_detelecine = {
    .name          = "detelecine",
    .description   = NULL_IF_CONFIG_SMALL("Apply an inverse telecine pattern."),
    .priv_size     = sizeof(DetelecineContext),
    .priv_class    = &detelecine_class,
    .init          = init,
    .uninit        = uninit,
    FILTER_INPUTS(detelecine_inputs),
    FILTER_OUTPUTS(detelecine_outputs),
    FILTER_QUERY_FUNC(query_formats),
};
