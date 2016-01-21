/*
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with Libav; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/**
 * @file
 * progressive to interlaced content filter, inspired by heavy debugging of tinterlace filter
 */

#include "libavutil/common.h"
#include "libavutil/opt.h"
#include "libavutil/imgutils.h"
#include "libavutil/avassert.h"

#include "formats.h"
#include "avfilter.h"
#include "interlace.h"
#include "internal.h"
#include "video.h"

#define OFFSET(x) offsetof(InterlaceContext, x)
#define V AV_OPT_FLAG_VIDEO_PARAM
static const AVOption options[] = {
    { "scan", "scanning mode", OFFSET(scan),
        AV_OPT_TYPE_INT,   {.i64 = MODE_TFF }, 0, 1, .flags = V, .unit = "scan" },
    { "tff", "top field first", 0,
        AV_OPT_TYPE_CONST, {.i64 = MODE_TFF }, INT_MIN, INT_MAX, .flags = V, .unit = "scan" },
    { "bff", "bottom field first", 0,
        AV_OPT_TYPE_CONST, {.i64 = MODE_BFF }, INT_MIN, INT_MAX, .flags = V, .unit = "scan" },
    { "lowpass", "enable vertical low-pass filter", OFFSET(lowpass),
        AV_OPT_TYPE_INT,   {.i64 = 1 },        0, 1, .flags = V },
    { NULL }
};

static const AVClass class = {
    .class_name = "interlace filter",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static void lowpass_line_c(uint8_t *dstp, ptrdiff_t linesize,
                           const uint8_t *srcp,
                           const uint8_t *srcp_above,
                           const uint8_t *srcp_below)
{
    int i;
    for (i = 0; i < linesize; i++) {
        // this calculation is an integer representation of
        // '0.5 * current + 0.25 * above + 0.25 * below'
        // '1 +' is for rounding.
        dstp[i] = (1 + srcp[i] + srcp[i] + srcp_above[i] + srcp_below[i]) >> 2;
    }
}

static const enum AVPixelFormat formats_supported[] = {
    AV_PIX_FMT_YUV420P,  AV_PIX_FMT_YUV422P,  AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_YUV444P,  AV_PIX_FMT_YUV410P,  AV_PIX_FMT_YUVA420P,
    AV_PIX_FMT_GRAY8,    AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUVJ422P,
    AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVJ440P, AV_PIX_FMT_NONE
};

static int query_formats(AVFilterContext *ctx)
{
    ff_set_common_formats(ctx, ff_make_format_list(formats_supported));
    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    InterlaceContext *s = ctx->priv;

    av_frame_free(&s->cur);
    av_frame_free(&s->next);

    av_opt_free(s);
}

static int config_out_props(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AVFilterLink *inlink = outlink->src->inputs[0];
    InterlaceContext *s = ctx->priv;

    if (inlink->h < 2) {
        av_log(ctx, AV_LOG_ERROR, "input video height is too small\n");
        return AVERROR_INVALIDDATA;
    }

    if (!s->lowpass)
        av_log(ctx, AV_LOG_WARNING, "Lowpass filter is disabled, "
               "the resulting video will be aliased rather than interlaced.\n");

    // same input size
    outlink->w = inlink->w;
    outlink->h = inlink->h;
    outlink->time_base = inlink->time_base;
    outlink->frame_rate = inlink->frame_rate;
    // half framerate
    outlink->time_base.num *= 2;
    outlink->frame_rate.den *= 2;


    if (s->lowpass) {
        s->lowpass_line = lowpass_line_c;
        if (ARCH_X86)
            ff_interlace_init_x86(s);
    }

    av_log(ctx, AV_LOG_VERBOSE, "%s interlacing %s lowpass filter\n",
           s->scan == MODE_TFF ? "tff" : "bff", (s->lowpass) ? "with" : "without");

    return 0;
}

static void copy_picture_field(InterlaceContext *s,
                               AVFrame *src_frame, AVFrame *dst_frame,
                               AVFilterLink *inlink, enum FieldType field_type,
                               int lowpass)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    int hsub = desc->log2_chroma_w;
    int vsub = desc->log2_chroma_h;
    int plane, j;

    for (plane = 0; plane < desc->nb_components; plane++) {
        int cols  = (plane == 1 || plane == 2) ? AV_CEIL_RSHIFT(inlink->w, hsub)
                                               : inlink->w;
        int lines = (plane == 1 || plane == 2) ? AV_CEIL_RSHIFT(inlink->h, vsub)
                                               : inlink->h;
        uint8_t *dstp = dst_frame->data[plane];
        const uint8_t *srcp = src_frame->data[plane];

        av_assert0(cols >= 0 || lines >= 0);

        lines = (lines + (field_type == FIELD_UPPER)) / 2;
        if (field_type == FIELD_LOWER) {
            srcp += src_frame->linesize[plane];
            dstp += dst_frame->linesize[plane];
        }
        if (lowpass) {
            int srcp_linesize = src_frame->linesize[plane] * 2;
            int dstp_linesize = dst_frame->linesize[plane] * 2;
            for (j = lines; j > 0; j--) {
                const uint8_t *srcp_above = srcp - src_frame->linesize[plane];
                const uint8_t *srcp_below = srcp + src_frame->linesize[plane];
                if (j == lines)
                    srcp_above = srcp; // there is no line above
                if (j == 1)
                    srcp_below = srcp; // there is no line below
                s->lowpass_line(dstp, cols, srcp, srcp_above, srcp_below);
                dstp += dstp_linesize;
                srcp += srcp_linesize;
            }
        } else {
            av_image_copy_plane(dstp, dst_frame->linesize[plane] * 2,
                                srcp, src_frame->linesize[plane] * 2,
                                cols, lines);
        }
    }
}

static int filter_frame(AVFilterLink *inlink, AVFrame *buf)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    InterlaceContext *s = ctx->priv;
    AVFrame *out;
    int tff, ret;

    av_frame_free(&s->cur);
    s->cur  = s->next;
    s->next = buf;

    /* we need at least two frames */
    if (!s->cur || !s->next)
        return 0;

    if (s->cur->interlaced_frame) {
        av_log(ctx, AV_LOG_WARNING,
               "video is already interlaced, adjusting framerate only\n");
        out = av_frame_clone(s->cur);
        if (!out)
            return AVERROR(ENOMEM);
        out->pts /= 2;  // adjust pts to new framerate
        ret = ff_filter_frame(outlink, out);
        s->got_output = 1;
        return ret;
    }

    tff = (s->scan == MODE_TFF);
    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out)
        return AVERROR(ENOMEM);

    av_frame_copy_props(out, s->cur);
    out->interlaced_frame = 1;
    out->top_field_first  = tff;
    out->pts             /= 2;  // adjust pts to new framerate

    /* copy upper/lower field from cur */
    copy_picture_field(s, s->cur, out, inlink, tff ? FIELD_UPPER : FIELD_LOWER, s->lowpass);
    av_frame_free(&s->cur);

    /* copy lower/upper field from next */
    copy_picture_field(s, s->next, out, inlink, tff ? FIELD_LOWER : FIELD_UPPER, s->lowpass);
    av_frame_free(&s->next);

    ret = ff_filter_frame(outlink, out);
    s->got_output = 1;

    return ret;
}

static int request_frame(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    InterlaceContext *s  = ctx->priv;
    int ret = 0;

    s->got_output = 0;
    while (ret >= 0 && !s->got_output)
        ret = ff_request_frame(ctx->inputs[0]);

    return ret;
}

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_out_props,
        .request_frame = request_frame,
    },
    { NULL }
};

AVFilter ff_vf_interlace = {
    .name          = "interlace",
    .description   = NULL_IF_CONFIG_SMALL("Convert progressive video into interlaced."),
    .uninit        = uninit,

    .priv_class    = &class,
    .priv_size     = sizeof(InterlaceContext),
    .query_formats = query_formats,

    .inputs        = inputs,
    .outputs       = outputs,
};

