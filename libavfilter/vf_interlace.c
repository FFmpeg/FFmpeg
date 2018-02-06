/*
 * Copyright (c) 2003 Michael Zucchi <notzed@ximian.com>
 * Copyright (c) 2010 Baptiste Coudurier
 * Copyright (c) 2011 Stefano Sabatini
 * Copyright (c) 2013 Vittorio Giovara <vittorio.giovara@gmail.com>
 * Copyright (c) 2017 Thomas Mundt <tmundt75@gmail.com>
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
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption interlace_options[] = {
    { "scan", "scanning mode", OFFSET(scan),
        AV_OPT_TYPE_INT,   {.i64 = MODE_TFF }, 0, 1, .flags = FLAGS, .unit = "scan" },
    { "tff", "top field first", 0,
        AV_OPT_TYPE_CONST, {.i64 = MODE_TFF }, INT_MIN, INT_MAX, .flags = FLAGS, .unit = "scan" },
    { "bff", "bottom field first", 0,
        AV_OPT_TYPE_CONST, {.i64 = MODE_BFF }, INT_MIN, INT_MAX, .flags = FLAGS, .unit = "scan" },
    { "lowpass", "set vertical low-pass filter", OFFSET(lowpass),
        AV_OPT_TYPE_INT,   {.i64 = VLPF_LIN }, 0, 2, .flags = FLAGS, .unit = "lowpass" },
    { "off",     "disable vertical low-pass filter", 0,
        AV_OPT_TYPE_CONST, {.i64 = VLPF_OFF }, INT_MIN, INT_MAX, .flags = FLAGS, .unit = "lowpass" },
    { "linear",  "linear vertical low-pass filter",  0,
        AV_OPT_TYPE_CONST, {.i64 = VLPF_LIN }, INT_MIN, INT_MAX, .flags = FLAGS, .unit = "lowpass" },
    { "complex", "complex vertical low-pass filter", 0,
        AV_OPT_TYPE_CONST, {.i64 = VLPF_CMP }, INT_MIN, INT_MAX, .flags = FLAGS, .unit = "lowpass" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(interlace);

static void lowpass_line_c(uint8_t *dstp, ptrdiff_t linesize,
                           const uint8_t *srcp, ptrdiff_t mref,
                           ptrdiff_t pref, int clip_max)
{
    const uint8_t *srcp_above = srcp + mref;
    const uint8_t *srcp_below = srcp + pref;
    int i;
    for (i = 0; i < linesize; i++) {
        // this calculation is an integer representation of
        // '0.5 * current + 0.25 * above + 0.25 * below'
        // '1 +' is for rounding.
        dstp[i] = (1 + srcp[i] + srcp[i] + srcp_above[i] + srcp_below[i]) >> 2;
    }
}

static void lowpass_line_c_16(uint8_t *dst8, ptrdiff_t linesize,
                              const uint8_t *src8, ptrdiff_t mref,
                              ptrdiff_t pref, int clip_max)
{
    uint16_t *dstp = (uint16_t *)dst8;
    const uint16_t *srcp = (const uint16_t *)src8;
    const uint16_t *srcp_above = srcp + mref / 2;
    const uint16_t *srcp_below = srcp + pref / 2;
    int i, src_x;
    for (i = 0; i < linesize; i++) {
        // this calculation is an integer representation of
        // '0.5 * current + 0.25 * above + 0.25 * below'
        // '1 +' is for rounding.
        src_x   = av_le2ne16(srcp[i]) << 1;
        dstp[i] = av_le2ne16((1 + src_x + av_le2ne16(srcp_above[i])
                             + av_le2ne16(srcp_below[i])) >> 2);
    }
}

static void lowpass_line_complex_c(uint8_t *dstp, ptrdiff_t linesize,
                                   const uint8_t *srcp, ptrdiff_t mref,
                                   ptrdiff_t pref, int clip_max)
{
    const uint8_t *srcp_above = srcp + mref;
    const uint8_t *srcp_below = srcp + pref;
    const uint8_t *srcp_above2 = srcp + mref * 2;
    const uint8_t *srcp_below2 = srcp + pref * 2;
    int i, src_x, src_ab;
    for (i = 0; i < linesize; i++) {
        // this calculation is an integer representation of
        // '0.75 * current + 0.25 * above + 0.25 * below - 0.125 * above2 - 0.125 * below2'
        // '4 +' is for rounding.
        src_x   = srcp[i] << 1;
        src_ab  = srcp_above[i] + srcp_below[i];
        dstp[i] = av_clip_uint8((4 + ((srcp[i] + src_x + src_ab) << 1)
                                - srcp_above2[i] - srcp_below2[i]) >> 3);
        // Prevent over-sharpening:
        // dst must not exceed src when the average of above and below
        // is less than src. And the other way around.
        if (src_ab > src_x) {
            if (dstp[i] < srcp[i])
                dstp[i] = srcp[i];
        } else if (dstp[i] > srcp[i])
            dstp[i] = srcp[i];
    }
}

static void lowpass_line_complex_c_16(uint8_t *dst8, ptrdiff_t linesize,
                                      const uint8_t *src8, ptrdiff_t mref,
                                      ptrdiff_t pref, int clip_max)
{
    uint16_t *dstp = (uint16_t *)dst8;
    const uint16_t *srcp = (const uint16_t *)src8;
    const uint16_t *srcp_above = srcp + mref / 2;
    const uint16_t *srcp_below = srcp + pref / 2;
    const uint16_t *srcp_above2 = srcp + mref;
    const uint16_t *srcp_below2 = srcp + pref;
    int i, dst_le, src_le, src_x, src_ab;
    for (i = 0; i < linesize; i++) {
        // this calculation is an integer representation of
        // '0.75 * current + 0.25 * above + 0.25 * below - 0.125 * above2 - 0.125 * below2'
        // '4 +' is for rounding.
        src_le = av_le2ne16(srcp[i]);
        src_x  = src_le << 1;
        src_ab = av_le2ne16(srcp_above[i]) + av_le2ne16(srcp_below[i]);
        dst_le = av_clip((4 + ((src_le + src_x + src_ab) << 1)
                         - av_le2ne16(srcp_above2[i])
                         - av_le2ne16(srcp_below2[i])) >> 3, 0, clip_max);
        // Prevent over-sharpening:
        // dst must not exceed src when the average of above and below
        // is less than src. And the other way around.
        if (src_ab > src_x) {
            if (dst_le < src_le)
                dstp[i] = av_le2ne16(src_le);
            else
                dstp[i] = av_le2ne16(dst_le);
        } else if (dst_le > src_le) {
            dstp[i] = av_le2ne16(src_le);
        } else
            dstp[i] = av_le2ne16(dst_le);
    }
}

static const enum AVPixelFormat formats_supported[] = {
    AV_PIX_FMT_YUV410P,      AV_PIX_FMT_YUV411P,
    AV_PIX_FMT_YUV420P,      AV_PIX_FMT_YUV422P,      AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_YUV420P10LE,  AV_PIX_FMT_YUV422P10LE,  AV_PIX_FMT_YUV444P10LE,
    AV_PIX_FMT_YUV420P12LE,  AV_PIX_FMT_YUV422P12LE,  AV_PIX_FMT_YUV444P12LE,
    AV_PIX_FMT_YUVA420P,     AV_PIX_FMT_YUVA422P,     AV_PIX_FMT_YUVA444P,
    AV_PIX_FMT_YUVA420P10LE, AV_PIX_FMT_YUVA422P10LE, AV_PIX_FMT_YUVA444P10LE,
    AV_PIX_FMT_GRAY8,        AV_PIX_FMT_YUVJ420P,     AV_PIX_FMT_YUVJ422P,
    AV_PIX_FMT_YUVJ444P,     AV_PIX_FMT_YUVJ440P,     AV_PIX_FMT_NONE
};

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *fmts_list = ff_make_format_list(formats_supported);
    if (!fmts_list)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, fmts_list);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    InterlaceContext *s = ctx->priv;

    av_frame_free(&s->cur);
    av_frame_free(&s->next);
}

void ff_interlace_init(InterlaceContext *s, int depth)
{
    if (s->lowpass) {
        if (s->lowpass == VLPF_LIN) {
            if (depth > 8)
                s->lowpass_line = lowpass_line_c_16;
            else
                s->lowpass_line = lowpass_line_c;
        } else if (s->lowpass == VLPF_CMP) {
            if (depth > 8)
                s->lowpass_line = lowpass_line_complex_c_16;
            else
                s->lowpass_line = lowpass_line_complex_c;
        }
        if (ARCH_X86)
            ff_interlace_init_x86(s, depth);
    }
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

    s->csp = av_pix_fmt_desc_get(outlink->format);
    ff_interlace_init(s, s->csp->comp[0].depth);

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
        int cols  = (plane == 1 || plane == 2) ? -(-inlink->w) >> hsub : inlink->w;
        int lines = (plane == 1 || plane == 2) ? AV_CEIL_RSHIFT(inlink->h, vsub) : inlink->h;
        uint8_t *dstp = dst_frame->data[plane];
        const uint8_t *srcp = src_frame->data[plane];
        int srcp_linesize = src_frame->linesize[plane] * 2;
        int dstp_linesize = dst_frame->linesize[plane] * 2;
        int clip_max = (1 << s->csp->comp[plane].depth) - 1;

        av_assert0(cols >= 0 || lines >= 0);

        lines = (lines + (field_type == FIELD_UPPER)) / 2;
        if (field_type == FIELD_LOWER) {
            srcp += src_frame->linesize[plane];
            dstp += dst_frame->linesize[plane];
        }
        if (lowpass) {
            int x = 0;
            if (lowpass == VLPF_CMP)
                x = 1;
            for (j = lines; j > 0; j--) {
                ptrdiff_t pref = src_frame->linesize[plane];
                ptrdiff_t mref = -pref;
                if (j >= (lines - x))
                    mref = 0;
                else if (j <= (1 + x))
                    pref = 0;
                s->lowpass_line(dstp, cols, srcp, mref, pref, clip_max);
                dstp += dstp_linesize;
                srcp += srcp_linesize;
            }
        } else {
            if (s->csp->comp[plane].depth > 8)
                cols *= 2;
            av_image_copy_plane(dstp, dstp_linesize, srcp, srcp_linesize, cols, lines);
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
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_out_props,
    },
    { NULL }
};

AVFilter ff_vf_interlace = {
    .name          = "interlace",
    .description   = NULL_IF_CONFIG_SMALL("Convert progressive video into interlaced."),
    .uninit        = uninit,
    .priv_class    = &interlace_class,
    .priv_size     = sizeof(InterlaceContext),
    .query_formats = query_formats,
    .inputs        = inputs,
    .outputs       = outputs,
};
