/*
 * Copyright (c) 2011 Mark Himsley
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * video field order filter, heavily influenced by vf_pad.c
 */

/* #define DEBUG */

#include "libavutil/imgutils.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"

typedef struct
{
    unsigned int dst_tff;      ///< output bff/tff
    int          line_size[4]; ///< bytes of pixel data per line for each plane
} FieldOrderContext;

static av_cold int init(AVFilterContext *ctx, const char *args, void *opaque)
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

    av_log(ctx, AV_LOG_INFO, "output field order: %s\n",
            fieldorder->dst_tff ? tff : bff);

    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats  *formats;
    enum PixelFormat pix_fmt;
    int              ret;

    /** accept any input pixel format that is not hardware accelerated, not
     *  a bitstream format, and does not have vertically sub-sampled chroma */
    if (ctx->inputs[0]) {
        formats = NULL;
        for (pix_fmt = 0; pix_fmt < PIX_FMT_NB; pix_fmt++)
            if (!(  av_pix_fmt_descriptors[pix_fmt].flags & PIX_FMT_HWACCEL
                 || av_pix_fmt_descriptors[pix_fmt].flags & PIX_FMT_BITSTREAM)
                && av_pix_fmt_descriptors[pix_fmt].nb_components
                && !av_pix_fmt_descriptors[pix_fmt].log2_chroma_h
                && (ret = avfilter_add_format(&formats, pix_fmt)) < 0) {
                avfilter_formats_unref(&formats);
                return ret;
            }
        avfilter_formats_ref(formats, &ctx->inputs[0]->out_formats);
        avfilter_formats_ref(formats, &ctx->outputs[0]->in_formats);
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

    return avfilter_get_video_buffer(outlink, perms, w, h);
}

static void start_frame(AVFilterLink *inlink, AVFilterBufferRef *inpicref)
{
    AVFilterContext   *ctx        = inlink->dst;
    AVFilterLink      *outlink    = ctx->outputs[0];

    AVFilterBufferRef *outpicref;

    outpicref = avfilter_ref_buffer(inpicref, ~0);
    outlink->out_buf = outpicref;

    avfilter_start_frame(outlink, outpicref);
}

static void draw_slice(AVFilterLink *inlink, int y, int h, int slice_dir)
{
    AVFilterContext   *ctx        = inlink->dst;
    FieldOrderContext *fieldorder = ctx->priv;
    AVFilterLink      *outlink    = ctx->outputs[0];

    AVFilterBufferRef *inpicref   = inlink->cur_buf;

    /** can only currently do slices if this filter is doing nothing
     *  because this filter is moving picture content, the output
     *  slice will contain different video lines than the input slice
     *  and that complexity will be added later */
    if (  !inpicref->video->interlaced
        || inpicref->video->top_field_first == fieldorder->dst_tff) {
        avfilter_draw_slice(outlink, y, h, slice_dir);
    }
}

static void end_frame(AVFilterLink *inlink)
{
    AVFilterContext   *ctx        = inlink->dst;
    FieldOrderContext *fieldorder = ctx->priv;
    AVFilterLink      *outlink    = ctx->outputs[0];

    AVFilterBufferRef *inpicref   = inlink->cur_buf;
    AVFilterBufferRef *outpicref  = outlink->out_buf;

    int               h, plane, line_step, line_size, line;
    uint8_t           *cpy_src, *cpy_dst;

    if (    inpicref->video->interlaced
         && inpicref->video->top_field_first != fieldorder->dst_tff) {
        av_dlog(ctx,
                "picture will move %s one line\n",
                fieldorder->dst_tff ? "up" : "down");
        h = inpicref->video->h;
        for (plane = 0; plane < 4 && inpicref->data[plane]; plane++) {
            line_step = inpicref->linesize[plane];
            line_size = fieldorder->line_size[plane];
            cpy_src = inpicref->data[plane];
            cpy_dst = outpicref->data[plane];
            if (fieldorder->dst_tff) {
                /** Move every line up one line, working from
                 *  the top to the bottom of the frame.
                 *  The original top line is lost.
                 *  The new last line is created as a copy of the
                 *  penultimate line from that field. */
                for (line = 0; line < h; line++) {
                    if (1 + line < outpicref->video->h) {
                        memcpy(cpy_dst, cpy_src + line_step, line_size);
                    } else {
                        memcpy(cpy_dst, cpy_src - line_step - line_step, line_size);
                    }
                    cpy_src += line_step;
                    cpy_dst += line_step;
                }
            } else {
                /** Move every line down one line, working from
                 *  the bottom to the top of the frame.
                 *  The original bottom line is lost.
                 *  The new first line is created as a copy of the
                 *  second line from that field. */
                cpy_src += (h - 1) * line_step;
                cpy_dst += (h - 1) * line_step;
                for (line = h - 1; line >= 0 ; line--) {
                    if (line > 0) {
                        memcpy(cpy_dst, cpy_src - line_step, line_size);
                    } else {
                        memcpy(cpy_dst, cpy_src + line_step + line_step, line_size);
                    }
                    cpy_src -= line_step;
                    cpy_dst -= line_step;
                }
            }
        }
        outpicref->video->top_field_first = fieldorder->dst_tff;
        avfilter_draw_slice(outlink, 0, h, 1);
    } else {
        av_dlog(ctx,
                "not interlaced or field order already correct\n");
    }

    avfilter_end_frame(outlink);
    avfilter_unref_buffer(inpicref);
}

AVFilter avfilter_vf_fieldorder = {
    .name          = "fieldorder",
    .description   = NULL_IF_CONFIG_SMALL("Set the field order."),
    .init          = init,
    .priv_size     = sizeof(FieldOrderContext),
    .query_formats = query_formats,
    .inputs        = (AVFilterPad[]) {{ .name             = "default",
                                        .type             = AVMEDIA_TYPE_VIDEO,
                                        .config_props     = config_input,
                                        .start_frame      = start_frame,
                                        .get_video_buffer = get_video_buffer,
                                        .draw_slice       = draw_slice,
                                        .end_frame        = end_frame,
                                        .min_perms        = AV_PERM_READ,
                                        .rej_perms        = AV_PERM_REUSE2|AV_PERM_PRESERVE,},
                                      { .name = NULL}},
    .outputs       = (AVFilterPad[]) {{ .name             = "default",
                                        .type             = AVMEDIA_TYPE_VIDEO, },
                                      { .name = NULL}},
};
