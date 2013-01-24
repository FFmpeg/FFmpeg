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

/* #define DEBUG */

#include <stdio.h>
#include <string.h>

#include "libavutil/imgutils.h"
#include "libavutil/internal.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

typedef struct
{
    unsigned int dst_tff;      ///< output bff/tff
    int          line_size[4]; ///< bytes of pixel data per line for each plane
} FieldOrderContext;

static av_cold int init(AVFilterContext *ctx, const char *args)
{
    FieldOrderContext *fieldorder = ctx->priv;

    const char *tff = "tff";
    const char *bff = "bff";

    if (!args) {
        fieldorder->dst_tff = 1;
    } else if (sscanf(args, "%u", &fieldorder->dst_tff) == 1) {
        fieldorder->dst_tff = !!fieldorder->dst_tff;
    } else if (!strcmp(tff, args)) {
        fieldorder->dst_tff = 1;
    } else if (!strcmp(bff, args)) {
        fieldorder->dst_tff = 0;
    } else {
        av_log(ctx, AV_LOG_ERROR, "Invalid argument '%s'.\n", args);
        return AVERROR(EINVAL);
    }

    av_log(ctx, AV_LOG_VERBOSE, "output field order: %s\n",
            fieldorder->dst_tff ? tff : bff);

    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats  *formats;
    enum AVPixelFormat pix_fmt;
    int              ret;

    /** accept any input pixel format that is not hardware accelerated, not
     *  a bitstream format, and does not have vertically sub-sampled chroma */
    if (ctx->inputs[0]) {
        formats = NULL;
        for (pix_fmt = 0; pix_fmt < AV_PIX_FMT_NB; pix_fmt++) {
            const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pix_fmt);
            if (!(desc->flags & PIX_FMT_HWACCEL ||
                  desc->flags & PIX_FMT_BITSTREAM) &&
                desc->nb_components && !desc->log2_chroma_h &&
                (ret = ff_add_format(&formats, pix_fmt)) < 0) {
                ff_formats_unref(&formats);
                return ret;
            }
        }
        ff_formats_ref(formats, &ctx->inputs[0]->out_formats);
        ff_formats_ref(formats, &ctx->outputs[0]->in_formats);
    }

    return 0;
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext   *ctx        = inlink->dst;
    FieldOrderContext *fieldorder = ctx->priv;
    int               plane;

    /** full an array with the number of bytes that the video
     *  data occupies per line for each plane of the input video */
    for (plane = 0; plane < 4; plane++) {
        fieldorder->line_size[plane] = av_image_get_linesize(
                inlink->format,
                inlink->w,
                plane);
    }

    return 0;
}

static AVFilterBufferRef *get_video_buffer(AVFilterLink *inlink, int perms, int w, int h)
{
    AVFilterContext   *ctx        = inlink->dst;
    AVFilterLink      *outlink    = ctx->outputs[0];

    return ff_get_video_buffer(outlink, perms, w, h);
}

static int filter_frame(AVFilterLink *inlink, AVFilterBufferRef *frame)
{
    AVFilterContext   *ctx     = inlink->dst;
    FieldOrderContext *s       = ctx->priv;
    AVFilterLink      *outlink = ctx->outputs[0];
    int h, plane, line_step, line_size, line;
    uint8_t *data;

    if (!frame->video->interlaced ||
        frame->video->top_field_first == s->dst_tff)
        return ff_filter_frame(outlink, frame);

    av_dlog(ctx,
            "picture will move %s one line\n",
            s->dst_tff ? "up" : "down");
    h = frame->video->h;
    for (plane = 0; plane < 4 && frame->data[plane]; plane++) {
        line_step = frame->linesize[plane];
        line_size = s->line_size[plane];
        data = frame->data[plane];
        if (s->dst_tff) {
            /** Move every line up one line, working from
             *  the top to the bottom of the frame.
             *  The original top line is lost.
             *  The new last line is created as a copy of the
             *  penultimate line from that field. */
            for (line = 0; line < h; line++) {
                if (1 + line < frame->video->h) {
                    memcpy(data, data + line_step, line_size);
                } else {
                    memcpy(data, data - line_step - line_step, line_size);
                }
                data += line_step;
            }
        } else {
            /** Move every line down one line, working from
             *  the bottom to the top of the frame.
             *  The original bottom line is lost.
             *  The new first line is created as a copy of the
             *  second line from that field. */
            data += (h - 1) * line_step;
            for (line = h - 1; line >= 0 ; line--) {
                if (line > 0) {
                    memcpy(data, data - line_step, line_size);
                } else {
                    memcpy(data, data + line_step + line_step, line_size);
                }
                data -= line_step;
            }
        }
    }
    frame->video->top_field_first = s->dst_tff;

    return ff_filter_frame(outlink, frame);
}

static const AVFilterPad avfilter_vf_fieldorder_inputs[] = {
    {
        .name             = "default",
        .type             = AVMEDIA_TYPE_VIDEO,
        .config_props     = config_input,
        .get_video_buffer = get_video_buffer,
        .filter_frame     = filter_frame,
        .min_perms        = AV_PERM_READ | AV_PERM_WRITE,
    },
    { NULL }
};

static const AVFilterPad avfilter_vf_fieldorder_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter avfilter_vf_fieldorder = {
    .name          = "fieldorder",
    .description   = NULL_IF_CONFIG_SMALL("Set the field order."),
    .init          = init,
    .priv_size     = sizeof(FieldOrderContext),
    .query_formats = query_formats,
    .inputs        = avfilter_vf_fieldorder_inputs,
    .outputs       = avfilter_vf_fieldorder_outputs,
};
