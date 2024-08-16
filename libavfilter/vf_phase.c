/*
 * Copyright (c) 2004 Ville Saari
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/avassert.h"
#include "libavutil/imgutils.h"
#include "libavutil/pixdesc.h"
#include "libavutil/opt.h"
#include "avfilter.h"
#include "filters.h"
#include "video.h"

enum PhaseMode {
    PROGRESSIVE,
    TOP_FIRST,
    BOTTOM_FIRST,
    TOP_FIRST_ANALYZE,
    BOTTOM_FIRST_ANALYZE,
    ANALYZE,
    FULL_ANALYZE,
    AUTO,
    AUTO_ANALYZE
};

#define DEPTH 8
#include "phase_template.c"

#undef DEPTH
#define DEPTH 9
#include "phase_template.c"

#undef DEPTH
#define DEPTH 10
#include "phase_template.c"

#undef DEPTH
#define DEPTH 12
#include "phase_template.c"

#undef DEPTH
#define DEPTH 14
#include "phase_template.c"

#undef DEPTH
#define DEPTH 16
#include "phase_template.c"

typedef struct PhaseContext {
    const AVClass *class;
    int mode;                   ///<PhaseMode
    AVFrame *frame; /* previous frame */
    int nb_planes;
    int planeheight[4];
    int linesize[4];

    enum PhaseMode (*analyze_plane)(void *ctx, enum PhaseMode mode, AVFrame *old, AVFrame *new);
} PhaseContext;

#define OFFSET(x) offsetof(PhaseContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_RUNTIME_PARAM
#define CONST(name, help, val, u) { name, help, 0, AV_OPT_TYPE_CONST, {.i64=val}, 0, 0, FLAGS, .unit = u }

static const AVOption phase_options[] = {
    { "mode", "set phase mode", OFFSET(mode), AV_OPT_TYPE_INT, {.i64=AUTO_ANALYZE}, PROGRESSIVE, AUTO_ANALYZE, FLAGS, .unit = "mode" },
    CONST("p", "progressive",          PROGRESSIVE,          "mode"),
    CONST("t", "top first",            TOP_FIRST,            "mode"),
    CONST("b", "bottom first",         BOTTOM_FIRST,         "mode"),
    CONST("T", "top first analyze",    TOP_FIRST_ANALYZE,    "mode"),
    CONST("B", "bottom first analyze", BOTTOM_FIRST_ANALYZE, "mode"),
    CONST("u", "analyze",              ANALYZE,              "mode"),
    CONST("U", "full analyze",         FULL_ANALYZE,         "mode"),
    CONST("a", "auto",                 AUTO,                 "mode"),
    CONST("A", "auto analyze",         AUTO_ANALYZE,         "mode"),
    { NULL }
};

AVFILTER_DEFINE_CLASS(phase);

static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_GRAY8,
    AV_PIX_FMT_GRAY9,
    AV_PIX_FMT_GRAY10,
    AV_PIX_FMT_GRAY12,
    AV_PIX_FMT_GRAY14,
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
    AV_PIX_FMT_GBRP,   AV_PIX_FMT_GBRP9,  AV_PIX_FMT_GBRP10,
    AV_PIX_FMT_GBRP12, AV_PIX_FMT_GBRP14, AV_PIX_FMT_GBRP16,
    AV_PIX_FMT_YUVA420P,  AV_PIX_FMT_YUVA422P,   AV_PIX_FMT_YUVA444P,
    AV_PIX_FMT_YUVA444P9, AV_PIX_FMT_YUVA444P10, AV_PIX_FMT_YUVA444P12, AV_PIX_FMT_YUVA444P16,
    AV_PIX_FMT_YUVA422P9, AV_PIX_FMT_YUVA422P10, AV_PIX_FMT_YUVA422P12, AV_PIX_FMT_YUVA422P16,
    AV_PIX_FMT_YUVA420P9, AV_PIX_FMT_YUVA420P10, AV_PIX_FMT_YUVA420P16,
    AV_PIX_FMT_GBRAP,     AV_PIX_FMT_GBRAP10,    AV_PIX_FMT_GBRAP12,    AV_PIX_FMT_GBRAP16,
    AV_PIX_FMT_NONE
};

static int config_input(AVFilterLink *inlink)
{
    PhaseContext *s = inlink->dst->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    int ret;

    switch (desc->comp[0].depth) {
    case  8: s->analyze_plane = analyze_plane_8;  break;
    case  9: s->analyze_plane = analyze_plane_9;  break;
    case 10: s->analyze_plane = analyze_plane_10; break;
    case 12: s->analyze_plane = analyze_plane_12; break;
    case 14: s->analyze_plane = analyze_plane_14; break;
    case 16: s->analyze_plane = analyze_plane_16; break;
    default: av_assert0(0);
    };

    if ((ret = av_image_fill_linesizes(s->linesize, inlink->format, inlink->w)) < 0)
        return ret;

    s->planeheight[1] = s->planeheight[2] = AV_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
    s->planeheight[0] = s->planeheight[3] = inlink->h;

    s->nb_planes = av_pix_fmt_count_planes(inlink->format);

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    PhaseContext *s = ctx->priv;
    enum PhaseMode mode;
    int plane, top, y;
    AVFrame *out;

    if (ctx->is_disabled) {
        av_frame_free(&s->frame);
        /* we keep a reference to the previous frame so the filter can start
         * being useful as soon as it's not disabled, avoiding the 1-frame
         * delay. */
        s->frame = av_frame_clone(in);
        return ff_filter_frame(outlink, in);
    }

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out, in);

    if (!s->frame) {
        s->frame = in;
        mode = PROGRESSIVE;
    } else {
        mode = s->analyze_plane(ctx, s->mode, s->frame, in);
    }

    for (plane = 0; plane < s->nb_planes; plane++) {
        const uint8_t *buf = s->frame->data[plane];
        const uint8_t *from = in->data[plane];
        uint8_t *to = out->data[plane];

        for (y = 0, top = 1; y < s->planeheight[plane]; y++, top ^= 1) {
            memcpy(to, mode == (top ? BOTTOM_FIRST : TOP_FIRST) ? buf : from, s->linesize[plane]);

            buf += s->frame->linesize[plane];
            from += in->linesize[plane];
            to += out->linesize[plane];
        }
    }

    if (in != s->frame)
        av_frame_free(&s->frame);
    s->frame = in;
    return ff_filter_frame(outlink, out);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    PhaseContext *s = ctx->priv;

    av_frame_free(&s->frame);
}

static const AVFilterPad phase_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = config_input,
    },
};

const AVFilter ff_vf_phase = {
    .name          = "phase",
    .description   = NULL_IF_CONFIG_SMALL("Phase shift fields."),
    .priv_size     = sizeof(PhaseContext),
    .priv_class    = &phase_class,
    .uninit        = uninit,
    FILTER_INPUTS(phase_inputs),
    FILTER_OUTPUTS(ff_video_default_filterpad),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL,
    .process_command = ff_filter_process_command,
};
