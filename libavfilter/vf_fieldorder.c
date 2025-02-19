/*
 * Copyright (c) 2011 Mark Himsley
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
 * video field order filter, heavily influenced by vf_pad.c
 */

#include "libavutil/imgutils.h"
#include "libavutil/internal.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "filters.h"
#include "formats.h"
#include "video.h"

typedef struct FieldOrderContext {
    const AVClass *class;
    int dst_tff;               ///< output bff/tff
    int          line_size[4]; ///< bytes of pixel data per line for each plane
} FieldOrderContext;

static int query_formats(const AVFilterContext *ctx,
                         AVFilterFormatsConfig **cfg_in,
                         AVFilterFormatsConfig **cfg_out)
{
    const AVPixFmtDescriptor *desc = NULL;
    AVFilterFormats  *formats;
    int              ret;

    /** accept any input pixel format that is not hardware accelerated, not
     *  a bitstream format, and does not have vertically sub-sampled chroma */
    formats = NULL;
    while ((desc = av_pix_fmt_desc_next(desc))) {
        enum AVPixelFormat pix_fmt = av_pix_fmt_desc_get_id(desc);
        if (!(desc->flags & AV_PIX_FMT_FLAG_HWACCEL ||
                desc->flags & AV_PIX_FMT_FLAG_PAL     ||
                desc->flags & AV_PIX_FMT_FLAG_BITSTREAM) &&
            desc->nb_components && !desc->log2_chroma_h &&
            (ret = ff_add_format(&formats, pix_fmt)) < 0)
            return ret;
    }
    return ff_set_common_formats2(ctx, cfg_in, cfg_out, formats);
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext   *ctx = inlink->dst;
    FieldOrderContext *s   = ctx->priv;

    return av_image_fill_linesizes(s->line_size, inlink->format, inlink->w);
}

static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    AVFilterContext   *ctx     = inlink->dst;
    FieldOrderContext *s       = ctx->priv;
    AVFilterLink      *outlink = ctx->outputs[0];
    int h, plane, src_line_step, dst_line_step, line_size, line;
    uint8_t *dst, *src;
    AVFrame *out;

    if (!(frame->flags & AV_FRAME_FLAG_INTERLACED) ||
        !!(frame->flags & AV_FRAME_FLAG_TOP_FIELD_FIRST) == s->dst_tff) {
        av_log(ctx, AV_LOG_VERBOSE,
               "Skipping %s.\n",
               (frame->flags & AV_FRAME_FLAG_INTERLACED) ?
               "frame with same field order" : "progressive frame");
        return ff_filter_frame(outlink, frame);
    }

    if (av_frame_is_writable(frame)) {
        out = frame;
    } else {
        out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
        if (!out) {
            av_frame_free(&frame);
            return AVERROR(ENOMEM);
        }
        av_frame_copy_props(out, frame);
    }

    av_log(ctx, AV_LOG_TRACE,
            "picture will move %s one line\n",
            s->dst_tff ? "up" : "down");
    h = frame->height;
    for (plane = 0; plane < 4 && frame->data[plane] && frame->linesize[plane]; plane++) {
        dst_line_step = out->linesize[plane] * (h > 2);
        src_line_step = frame->linesize[plane] * (h > 2);
        line_size = s->line_size[plane];
        dst = out->data[plane];
        src = frame->data[plane];
        if (s->dst_tff) {
            /** Move every line up one line, working from
             *  the top to the bottom of the frame.
             *  The original top line is lost.
             *  The new last line is created as a copy of the
             *  penultimate line from that field. */
            for (line = 0; line < h; line++) {
                if (1 + line < frame->height) {
                    memcpy(dst, src + src_line_step, line_size);
                } else {
                    memcpy(dst, src - 2 * src_line_step, line_size);
                }
                dst += dst_line_step;
                src += src_line_step;
            }
        } else {
            /** Move every line down one line, working from
             *  the bottom to the top of the frame.
             *  The original bottom line is lost.
             *  The new first line is created as a copy of the
             *  second line from that field. */
            dst += (h - 1) * dst_line_step;
            src += (h - 1) * src_line_step;
            for (line = h - 1; line >= 0 ; line--) {
                if (line > 0) {
                    memcpy(dst, src - src_line_step, line_size);
                } else {
                    memcpy(dst, src + 2 * src_line_step, line_size);
                }
                dst -= dst_line_step;
                src -= src_line_step;
            }
        }
    }
    if (s->dst_tff)
        out->flags |= AV_FRAME_FLAG_TOP_FIELD_FIRST;
    else
        out->flags &= ~AV_FRAME_FLAG_TOP_FIELD_FIRST;

    if (frame != out)
        av_frame_free(&frame);
    return ff_filter_frame(outlink, out);
}

#define OFFSET(x) offsetof(FieldOrderContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption fieldorder_options[] = {
    { "order", "output field order", OFFSET(dst_tff), AV_OPT_TYPE_INT, { .i64 = 1 }, 0, 1, FLAGS, .unit = "order" },
        { "bff", "bottom field first", 0, AV_OPT_TYPE_CONST, { .i64 = 0 }, .flags=FLAGS, .unit = "order" },
        { "tff", "top field first",    0, AV_OPT_TYPE_CONST, { .i64 = 1 }, .flags=FLAGS, .unit = "order" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(fieldorder);

static const AVFilterPad avfilter_vf_fieldorder_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_input,
        .filter_frame = filter_frame,
    },
};

const FFFilter ff_vf_fieldorder = {
    .p.name        = "fieldorder",
    .p.description = NULL_IF_CONFIG_SMALL("Set the field order."),
    .p.priv_class  = &fieldorder_class,
    .p.flags       = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
    .priv_size     = sizeof(FieldOrderContext),
    FILTER_INPUTS(avfilter_vf_fieldorder_inputs),
    FILTER_OUTPUTS(ff_video_default_filterpad),
    FILTER_QUERY_FUNC2(query_formats),
};
