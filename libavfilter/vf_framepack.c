/*
 * Copyright (c) 2013 Vittorio Giovara
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
 * Generate a frame packed video, by combining two views in a single surface.
 */

#include <string.h>

#include "libavutil/common.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/rational.h"
#include "libavutil/stereo3d.h"

#include "avfilter.h"
#include "filters.h"
#include "video.h"

#define LEFT  0
#define RIGHT 1

typedef struct FramepackContext {
    const AVClass *class;

    int depth;
    const AVPixFmtDescriptor *pix_desc; ///< agreed pixel format

    enum AVStereo3DType format;         ///< frame pack type output

    AVFrame *input_views[2];            ///< input frames
} FramepackContext;

static const enum AVPixelFormat formats_supported[] = {
    AV_PIX_FMT_GRAY8, AV_PIX_FMT_GRAY9,
    AV_PIX_FMT_GRAY10, AV_PIX_FMT_GRAY12, AV_PIX_FMT_GRAY14,
    AV_PIX_FMT_GRAY16,
    AV_PIX_FMT_YUV410P, AV_PIX_FMT_YUV411P,
    AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV422P,
    AV_PIX_FMT_YUV440P, AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUVJ422P,
    AV_PIX_FMT_YUVJ440P, AV_PIX_FMT_YUVJ444P,
    AV_PIX_FMT_YUVJ411P,
    AV_PIX_FMT_YUV420P9, AV_PIX_FMT_YUV422P9, AV_PIX_FMT_YUV444P9,
    AV_PIX_FMT_YUV420P10, AV_PIX_FMT_YUV422P10, AV_PIX_FMT_YUV444P10,
    AV_PIX_FMT_YUV440P10,
    AV_PIX_FMT_YUV444P12, AV_PIX_FMT_YUV422P12, AV_PIX_FMT_YUV420P12,
    AV_PIX_FMT_YUV440P12,
    AV_PIX_FMT_YUV444P14, AV_PIX_FMT_YUV422P14, AV_PIX_FMT_YUV420P14,
    AV_PIX_FMT_YUV420P16, AV_PIX_FMT_YUV422P16, AV_PIX_FMT_YUV444P16,
    AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRP9, AV_PIX_FMT_GBRP10,
    AV_PIX_FMT_GBRP12, AV_PIX_FMT_GBRP14, AV_PIX_FMT_GBRP16,
    AV_PIX_FMT_YUVA420P,  AV_PIX_FMT_YUVA422P,   AV_PIX_FMT_YUVA444P,
    AV_PIX_FMT_YUVA444P9, AV_PIX_FMT_YUVA444P10, AV_PIX_FMT_YUVA444P12, AV_PIX_FMT_YUVA444P16,
    AV_PIX_FMT_YUVA422P9, AV_PIX_FMT_YUVA422P10, AV_PIX_FMT_YUVA422P12, AV_PIX_FMT_YUVA422P16,
    AV_PIX_FMT_YUVA420P9, AV_PIX_FMT_YUVA420P10, AV_PIX_FMT_YUVA420P16,
    AV_PIX_FMT_GBRAP,     AV_PIX_FMT_GBRAP10,    AV_PIX_FMT_GBRAP12,    AV_PIX_FMT_GBRAP16,
    AV_PIX_FMT_NONE
};

static av_cold void framepack_uninit(AVFilterContext *ctx)
{
    FramepackContext *s = ctx->priv;

    // clean any leftover frame
    av_frame_free(&s->input_views[LEFT]);
    av_frame_free(&s->input_views[RIGHT]);
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx  = outlink->src;
    FramepackContext *s   = outlink->src->priv;
    FilterLink     *leftl = ff_filter_link(ctx->inputs[LEFT]);
    FilterLink    *rightl = ff_filter_link(ctx->inputs[RIGHT]);
    FilterLink        *ol = ff_filter_link(outlink);

    int width             = ctx->inputs[LEFT]->w;
    int height            = ctx->inputs[LEFT]->h;
    AVRational time_base  = ctx->inputs[LEFT]->time_base;
    AVRational frame_rate = leftl->frame_rate;

    // check size and fps match on the other input
    if (width  != ctx->inputs[RIGHT]->w ||
        height != ctx->inputs[RIGHT]->h) {
        av_log(ctx, AV_LOG_ERROR,
               "Left and right sizes differ (%dx%d vs %dx%d).\n",
               width, height,
               ctx->inputs[RIGHT]->w, ctx->inputs[RIGHT]->h);
        return AVERROR_INVALIDDATA;
    } else if (av_cmp_q(time_base, ctx->inputs[RIGHT]->time_base) != 0) {
        av_log(ctx, AV_LOG_ERROR,
               "Left and right time bases differ (%d/%d vs %d/%d).\n",
               time_base.num, time_base.den,
               ctx->inputs[RIGHT]->time_base.num,
               ctx->inputs[RIGHT]->time_base.den);
        return AVERROR_INVALIDDATA;
    } else if (av_cmp_q(frame_rate, rightl->frame_rate) != 0) {
        av_log(ctx, AV_LOG_ERROR,
               "Left and right framerates differ (%d/%d vs %d/%d).\n",
               frame_rate.num, frame_rate.den,
               rightl->frame_rate.num,
               rightl->frame_rate.den);
        return AVERROR_INVALIDDATA;
    }

    s->pix_desc = av_pix_fmt_desc_get(outlink->format);
    if (!s->pix_desc)
        return AVERROR_BUG;
    s->depth = s->pix_desc->comp[0].depth;

    // modify output properties as needed
    switch (s->format) {
    case AV_STEREO3D_FRAMESEQUENCE:
        time_base.den  *= 2;
        frame_rate.num *= 2;
        break;
    case AV_STEREO3D_COLUMNS:
    case AV_STEREO3D_SIDEBYSIDE:
        width *= 2;
        break;
    case AV_STEREO3D_LINES:
    case AV_STEREO3D_TOPBOTTOM:
        height *= 2;
        break;
    default:
        av_log(ctx, AV_LOG_ERROR, "Unknown packing mode.\n");
        return AVERROR_INVALIDDATA;
    }

    outlink->w          = width;
    outlink->h          = height;
    outlink->time_base  = time_base;
    ol->frame_rate      = frame_rate;

    return 0;
}

static void horizontal_frame_pack(AVFilterLink *outlink,
                                  AVFrame *out,
                                  int interleaved)
{
    AVFilterContext *ctx = outlink->src;
    FramepackContext *s = ctx->priv;
    int i, plane;

    if (interleaved && s->depth <= 8) {
        const uint8_t *leftp  = s->input_views[LEFT]->data[0];
        const uint8_t *rightp = s->input_views[RIGHT]->data[0];
        uint8_t *dstp         = out->data[0];
        int length = out->width / 2;
        int lines  = out->height;

        for (plane = 0; plane < s->pix_desc->nb_components; plane++) {
            if (plane == 1 || plane == 2) {
                length = AV_CEIL_RSHIFT(out->width / 2, s->pix_desc->log2_chroma_w);
                lines  = AV_CEIL_RSHIFT(out->height,    s->pix_desc->log2_chroma_h);
            }
            for (i = 0; i < lines; i++) {
                int j;
                leftp  = s->input_views[LEFT]->data[plane] +
                         s->input_views[LEFT]->linesize[plane] * i;
                rightp = s->input_views[RIGHT]->data[plane] +
                         s->input_views[RIGHT]->linesize[plane] * i;
                dstp   = out->data[plane] + out->linesize[plane] * i;
                for (j = 0; j < length; j++) {
                    // interpolate chroma as necessary
                    if ((s->pix_desc->log2_chroma_w ||
                         s->pix_desc->log2_chroma_h) &&
                        (plane == 1 || plane == 2)) {
                        *dstp++ = (*leftp + *rightp) / 2;
                        *dstp++ = (*leftp + *rightp) / 2;
                    } else {
                        *dstp++ = *leftp;
                        *dstp++ = *rightp;
                    }
                    leftp += 1;
                    rightp += 1;
                }
            }
        }
    } else if (interleaved && s->depth > 8) {
        const uint16_t *leftp  = (const uint16_t *)s->input_views[LEFT]->data[0];
        const uint16_t *rightp = (const uint16_t *)s->input_views[RIGHT]->data[0];
        uint16_t *dstp         = (uint16_t *)out->data[0];
        int length = out->width / 2;
        int lines  = out->height;

        for (plane = 0; plane < s->pix_desc->nb_components; plane++) {
            if (plane == 1 || plane == 2) {
                length = AV_CEIL_RSHIFT(out->width / 2, s->pix_desc->log2_chroma_w);
                lines  = AV_CEIL_RSHIFT(out->height,    s->pix_desc->log2_chroma_h);
            }
            for (i = 0; i < lines; i++) {
                int j;
                leftp  = (const uint16_t *)s->input_views[LEFT]->data[plane] +
                         s->input_views[LEFT]->linesize[plane] * i / 2;
                rightp = (const uint16_t *)s->input_views[RIGHT]->data[plane] +
                         s->input_views[RIGHT]->linesize[plane] * i / 2;
                dstp   = (uint16_t *)out->data[plane] + out->linesize[plane] * i / 2;
                for (j = 0; j < length; j++) {
                    // interpolate chroma as necessary
                    if ((s->pix_desc->log2_chroma_w ||
                         s->pix_desc->log2_chroma_h) &&
                        (plane == 1 || plane == 2)) {
                        *dstp++ = (*leftp + *rightp) / 2;
                        *dstp++ = (*leftp + *rightp) / 2;
                    } else {
                        *dstp++ = *leftp;
                        *dstp++ = *rightp;
                    }
                    leftp += 1;
                    rightp += 1;
                }
            }
        }
    } else {
        for (i = 0; i < 2; i++) {
            const AVFrame *const input_view = s->input_views[i];
            const int psize = 1 + (s->depth > 8);
            uint8_t *dst[4];
            int sub_w = psize * input_view->width >> s->pix_desc->log2_chroma_w;

            dst[0] = out->data[0] + i * input_view->width * psize;
            dst[1] = out->data[1] + i * sub_w;
            dst[2] = out->data[2] + i * sub_w;

            av_image_copy2(dst, out->linesize,
                           input_view->data, input_view->linesize,
                           input_view->format,
                           input_view->width,
                           input_view->height);
        }
    }
}

static void vertical_frame_pack(AVFilterLink *outlink,
                                AVFrame *out,
                                int interleaved)
{
    AVFilterContext *ctx = outlink->src;
    FramepackContext *s = ctx->priv;
    int i;

    for (i = 0; i < 2; i++) {
        const AVFrame *const input_view = s->input_views[i];
        uint8_t *dst[4];
        int linesizes[4];
        int sub_h = input_view->height >> s->pix_desc->log2_chroma_h;

        dst[0] = out->data[0] + i * out->linesize[0] *
                 (interleaved + input_view->height * (1 - interleaved));
        dst[1] = out->data[1] + i * out->linesize[1] *
                 (interleaved + sub_h * (1 - interleaved));
        dst[2] = out->data[2] + i * out->linesize[2] *
                 (interleaved + sub_h * (1 - interleaved));

        linesizes[0] = out->linesize[0] +
                       interleaved * out->linesize[0];
        linesizes[1] = out->linesize[1] +
                       interleaved * out->linesize[1];
        linesizes[2] = out->linesize[2] +
                       interleaved * out->linesize[2];

        av_image_copy2(dst, linesizes,
                       input_view->data, input_view->linesize,
                       input_view->format,
                       input_view->width,
                       input_view->height);
    }
}

static av_always_inline void spatial_frame_pack(AVFilterLink *outlink,
                                                AVFrame *dst)
{
    AVFilterContext *ctx = outlink->src;
    FramepackContext *s = ctx->priv;
    switch (s->format) {
    case AV_STEREO3D_SIDEBYSIDE:
        horizontal_frame_pack(outlink, dst, 0);
        break;
    case AV_STEREO3D_COLUMNS:
        horizontal_frame_pack(outlink, dst, 1);
        break;
    case AV_STEREO3D_TOPBOTTOM:
        vertical_frame_pack(outlink, dst, 0);
        break;
    case AV_STEREO3D_LINES:
        vertical_frame_pack(outlink, dst, 1);
        break;
    }
}

static int try_push_frame(AVFilterContext *ctx)
{
    FramepackContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    FilterLink       *l = ff_filter_link(outlink);
    AVStereo3D *stereo;
    int ret, i;

    if (!(s->input_views[0] && s->input_views[1]))
        return 0;
    if (s->format == AV_STEREO3D_FRAMESEQUENCE) {
        int64_t pts = s->input_views[0]->pts;

        for (i = 0; i < 2; i++) {
            // set correct timestamps
            if (pts != AV_NOPTS_VALUE) {
                s->input_views[i]->pts = i == 0 ? pts * 2 : pts * 2 + av_rescale_q(1, av_inv_q(l->frame_rate), outlink->time_base);
                s->input_views[i]->duration = av_rescale_q(1, av_inv_q(l->frame_rate), outlink->time_base);
            }

            // set stereo3d side data
            stereo = av_stereo3d_create_side_data(s->input_views[i]);
            if (!stereo)
                return AVERROR(ENOMEM);
            stereo->type = s->format;
            stereo->view = i == LEFT ? AV_STEREO3D_VIEW_LEFT
                                     : AV_STEREO3D_VIEW_RIGHT;

            // filter the frame and immediately relinquish its pointer
            ret = ff_filter_frame(outlink, s->input_views[i]);
            s->input_views[i] = NULL;
            if (ret < 0)
                return ret;
        }
        return ret;
    } else {
        AVFrame *dst = ff_get_video_buffer(outlink, outlink->w, outlink->h);
        if (!dst)
            return AVERROR(ENOMEM);

        spatial_frame_pack(outlink, dst);

        // get any property from the original frame
        ret = av_frame_copy_props(dst, s->input_views[LEFT]);
        if (ret < 0) {
            av_frame_free(&dst);
            return ret;
        }

        for (i = 0; i < 2; i++)
            av_frame_free(&s->input_views[i]);

        // set stereo3d side data
        stereo = av_stereo3d_create_side_data(dst);
        if (!stereo) {
            av_frame_free(&dst);
            return AVERROR(ENOMEM);
        }
        stereo->type = s->format;

        return ff_filter_frame(outlink, dst);
    }
}

static int activate(AVFilterContext *ctx)
{
    AVFilterLink *outlink = ctx->outputs[0];
    FramepackContext *s = ctx->priv;
    int ret;

    FF_FILTER_FORWARD_STATUS_BACK_ALL(outlink, ctx);

    if (!s->input_views[0]) {
        ret = ff_inlink_consume_frame(ctx->inputs[0], &s->input_views[0]);
        if (ret < 0)
            return ret;
    }

    if (!s->input_views[1]) {
        ret = ff_inlink_consume_frame(ctx->inputs[1], &s->input_views[1]);
        if (ret < 0)
            return ret;
    }

    if (s->input_views[0] && s->input_views[1])
        return try_push_frame(ctx);

    FF_FILTER_FORWARD_STATUS(ctx->inputs[0], outlink);
    FF_FILTER_FORWARD_STATUS(ctx->inputs[1], outlink);

    if (ff_outlink_frame_wanted(ctx->outputs[0]) &&
        !s->input_views[0]) {
        ff_inlink_request_frame(ctx->inputs[0]);
        return 0;
    }

    if (ff_outlink_frame_wanted(ctx->outputs[0]) &&
        !s->input_views[1]) {
        ff_inlink_request_frame(ctx->inputs[1]);
        return 0;
    }

    return FFERROR_NOT_READY;
}

#define OFFSET(x) offsetof(FramepackContext, x)
#define VF AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_FILTERING_PARAM
static const AVOption framepack_options[] = {
    { "format", "Frame pack output format", OFFSET(format), AV_OPT_TYPE_INT,
        { .i64 = AV_STEREO3D_SIDEBYSIDE }, 0, INT_MAX, .flags = VF, .unit = "format" },
    { "sbs", "Views are packed next to each other", 0, AV_OPT_TYPE_CONST,
        { .i64 = AV_STEREO3D_SIDEBYSIDE }, INT_MIN, INT_MAX, .flags = VF, .unit = "format" },
    { "tab", "Views are packed on top of each other", 0, AV_OPT_TYPE_CONST,
        { .i64 = AV_STEREO3D_TOPBOTTOM }, INT_MIN, INT_MAX, .flags = VF, .unit = "format" },
    { "frameseq", "Views are one after the other", 0, AV_OPT_TYPE_CONST,
        { .i64 = AV_STEREO3D_FRAMESEQUENCE }, INT_MIN, INT_MAX, .flags = VF, .unit = "format" },
    { "lines", "Views are interleaved by lines", 0, AV_OPT_TYPE_CONST,
        { .i64 = AV_STEREO3D_LINES }, INT_MIN, INT_MAX, .flags = VF, .unit = "format" },
    { "columns", "Views are interleaved by columns", 0, AV_OPT_TYPE_CONST,
        { .i64 = AV_STEREO3D_COLUMNS }, INT_MIN, INT_MAX, .flags = VF, .unit = "format" },
    { NULL },
};

AVFILTER_DEFINE_CLASS(framepack);

static const AVFilterPad framepack_inputs[] = {
    {
        .name         = "left",
        .type         = AVMEDIA_TYPE_VIDEO,
    },
    {
        .name         = "right",
        .type         = AVMEDIA_TYPE_VIDEO,
    },
};

static const AVFilterPad framepack_outputs[] = {
    {
        .name          = "packed",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
    },
};

const FFFilter ff_vf_framepack = {
    .p.name        = "framepack",
    .p.description = NULL_IF_CONFIG_SMALL("Generate a frame packed stereoscopic video."),
    .p.priv_class  = &framepack_class,
    .priv_size     = sizeof(FramepackContext),
    FILTER_INPUTS(framepack_inputs),
    FILTER_OUTPUTS(framepack_outputs),
    FILTER_PIXFMTS_ARRAY(formats_supported),
    .activate      = activate,
    .uninit        = framepack_uninit,
};
