/*
 * Copyright (c) 2010 Brandon Mintern
 * Copyright (c) 2007 Bobby Bingham
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
 * video fade filter
 * based heavily on vf_negate.c by Bobby Bingham
 */

#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/common.h"
#include "libavutil/eval.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "drawutils.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

#define R 0
#define G 1
#define B 2
#define A 3

#define Y 0
#define U 1
#define V 2

#define FADE_IN  0
#define FADE_OUT 1

typedef struct FadeContext {
    const AVClass *class;
    int type;
    int factor, fade_per_frame;
    int start_frame, nb_frames;
    int hsub, vsub, bpp;
    unsigned int black_level, black_level_scaled;
    uint8_t is_packed_rgb;
    uint8_t rgba_map[4];
    int alpha;
    uint64_t start_time, duration;
    enum {VF_FADE_WAITING=0, VF_FADE_FADING, VF_FADE_DONE} fade_state;
    uint8_t color_rgba[4];  ///< fade color
    int black_fade;         ///< if color_rgba is black
} FadeContext;

static av_cold int init(AVFilterContext *ctx)
{
    FadeContext *s = ctx->priv;

    s->fade_per_frame = (1 << 16) / s->nb_frames;
    s->fade_state = VF_FADE_WAITING;

    if (s->duration != 0) {
        // If duration (seconds) is non-zero, assume that we are not fading based on frames
        s->nb_frames = 0; // Mostly to clean up logging
    }

    // Choose what to log. If both time-based and frame-based options, both lines will be in the log
    if (s->start_frame || s->nb_frames) {
        av_log(ctx, AV_LOG_VERBOSE,
               "type:%s start_frame:%d nb_frames:%d alpha:%d\n",
               s->type == FADE_IN ? "in" : "out", s->start_frame,
               s->nb_frames,s->alpha);
    }
    if (s->start_time || s->duration) {
        av_log(ctx, AV_LOG_VERBOSE,
               "type:%s start_time:%f duration:%f alpha:%d\n",
               s->type == FADE_IN ? "in" : "out", (s->start_time / (double)AV_TIME_BASE),
               (s->duration / (double)AV_TIME_BASE),s->alpha);
    }

    s->black_fade = !memcmp(s->color_rgba, "\x00\x00\x00\xff", 4);
    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    const FadeContext *s = ctx->priv;
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_YUV444P,  AV_PIX_FMT_YUV422P,  AV_PIX_FMT_YUV420P,
        AV_PIX_FMT_YUV411P,  AV_PIX_FMT_YUV410P,
        AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUVJ420P,
        AV_PIX_FMT_YUV440P,  AV_PIX_FMT_YUVJ440P,
        AV_PIX_FMT_YUVA420P, AV_PIX_FMT_YUVA422P, AV_PIX_FMT_YUVA444P,
        AV_PIX_FMT_RGB24,    AV_PIX_FMT_BGR24,
        AV_PIX_FMT_ARGB,     AV_PIX_FMT_ABGR,
        AV_PIX_FMT_RGBA,     AV_PIX_FMT_BGRA,
        AV_PIX_FMT_NONE
    };
    static const enum AVPixelFormat pix_fmts_rgb[] = {
        AV_PIX_FMT_RGB24,    AV_PIX_FMT_BGR24,
        AV_PIX_FMT_ARGB,     AV_PIX_FMT_ABGR,
        AV_PIX_FMT_RGBA,     AV_PIX_FMT_BGRA,
        AV_PIX_FMT_NONE
    };
    static const enum AVPixelFormat pix_fmts_alpha[] = {
        AV_PIX_FMT_YUVA420P, AV_PIX_FMT_YUVA422P, AV_PIX_FMT_YUVA444P,
        AV_PIX_FMT_ARGB,     AV_PIX_FMT_ABGR,
        AV_PIX_FMT_RGBA,     AV_PIX_FMT_BGRA,
        AV_PIX_FMT_NONE
    };
    static const enum AVPixelFormat pix_fmts_rgba[] = {
        AV_PIX_FMT_ARGB,     AV_PIX_FMT_ABGR,
        AV_PIX_FMT_RGBA,     AV_PIX_FMT_BGRA,
        AV_PIX_FMT_NONE
    };
    AVFilterFormats *fmts_list;

    if (s->alpha) {
        if (s->black_fade)
            fmts_list = ff_make_format_list(pix_fmts_alpha);
        else
            fmts_list = ff_make_format_list(pix_fmts_rgba);
    } else {
        if (s->black_fade)
            fmts_list = ff_make_format_list(pix_fmts);
        else
            fmts_list = ff_make_format_list(pix_fmts_rgb);
    }
    if (!fmts_list)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, fmts_list);
}

const static enum AVPixelFormat studio_level_pix_fmts[] = {
    AV_PIX_FMT_YUV444P,  AV_PIX_FMT_YUV422P,  AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUV411P,  AV_PIX_FMT_YUV410P,
    AV_PIX_FMT_YUV440P,
    AV_PIX_FMT_NONE
};

static int config_props(AVFilterLink *inlink)
{
    FadeContext *s = inlink->dst->priv;
    const AVPixFmtDescriptor *pixdesc = av_pix_fmt_desc_get(inlink->format);

    s->hsub = pixdesc->log2_chroma_w;
    s->vsub = pixdesc->log2_chroma_h;

    s->bpp = pixdesc->flags & AV_PIX_FMT_FLAG_PLANAR ?
             1 :
             av_get_bits_per_pixel(pixdesc) >> 3;
    s->alpha &= !!(pixdesc->flags & AV_PIX_FMT_FLAG_ALPHA);
    s->is_packed_rgb = ff_fill_rgba_map(s->rgba_map, inlink->format) >= 0;

    /* use CCIR601/709 black level for studio-level pixel non-alpha components */
    s->black_level =
            ff_fmt_is_in(inlink->format, studio_level_pix_fmts) && !s->alpha ? 16 : 0;
    /* 32768 = 1 << 15, it is an integer representation
     * of 0.5 and is for rounding. */
    s->black_level_scaled = (s->black_level << 16) + 32768;
    return 0;
}

static av_always_inline void filter_rgb(FadeContext *s, const AVFrame *frame,
                                        int slice_start, int slice_end,
                                        int do_alpha, int step)
{
    int i, j;
    const uint8_t r_idx  = s->rgba_map[R];
    const uint8_t g_idx  = s->rgba_map[G];
    const uint8_t b_idx  = s->rgba_map[B];
    const uint8_t a_idx  = s->rgba_map[A];
    const uint8_t *c = s->color_rgba;

    for (i = slice_start; i < slice_end; i++) {
        uint8_t *p = frame->data[0] + i * frame->linesize[0];
        for (j = 0; j < frame->width; j++) {
#define INTERP(c_name, c_idx) av_clip_uint8(((c[c_idx]<<16) + ((int)p[c_name] - (int)c[c_idx]) * s->factor + (1<<15)) >> 16)
            p[r_idx] = INTERP(r_idx, 0);
            p[g_idx] = INTERP(g_idx, 1);
            p[b_idx] = INTERP(b_idx, 2);
            if (do_alpha)
                p[a_idx] = INTERP(a_idx, 3);
            p += step;
        }
    }
}

static int filter_slice_rgb(AVFilterContext *ctx, void *arg, int jobnr,
                            int nb_jobs)
{
    FadeContext *s = ctx->priv;
    AVFrame *frame = arg;
    int slice_start = (frame->height *  jobnr   ) / nb_jobs;
    int slice_end   = (frame->height * (jobnr+1)) / nb_jobs;

    if      (s->alpha)    filter_rgb(s, frame, slice_start, slice_end, 1, 4);
    else if (s->bpp == 3) filter_rgb(s, frame, slice_start, slice_end, 0, 3);
    else if (s->bpp == 4) filter_rgb(s, frame, slice_start, slice_end, 0, 4);
    else                  av_assert0(0);

    return 0;
}

static int filter_slice_luma(AVFilterContext *ctx, void *arg, int jobnr,
                             int nb_jobs)
{
    FadeContext *s = ctx->priv;
    AVFrame *frame = arg;
    int slice_start = (frame->height *  jobnr   ) / nb_jobs;
    int slice_end   = (frame->height * (jobnr+1)) / nb_jobs;
    int i, j;

    for (i = slice_start; i < slice_end; i++) {
        uint8_t *p = frame->data[0] + i * frame->linesize[0];
        for (j = 0; j < frame->width * s->bpp; j++) {
            /* s->factor is using 16 lower-order bits for decimal
             * places. 32768 = 1 << 15, it is an integer representation
             * of 0.5 and is for rounding. */
            *p = ((*p - s->black_level) * s->factor + s->black_level_scaled) >> 16;
            p++;
        }
    }

    return 0;
}

static int filter_slice_chroma(AVFilterContext *ctx, void *arg, int jobnr,
                               int nb_jobs)
{
    FadeContext *s = ctx->priv;
    AVFrame *frame = arg;
    int i, j, plane;
    const int width = AV_CEIL_RSHIFT(frame->width, s->hsub);
    const int height= AV_CEIL_RSHIFT(frame->height, s->vsub);
    int slice_start = (height *  jobnr   ) / nb_jobs;
    int slice_end   = (height * (jobnr+1)) / nb_jobs;

    for (plane = 1; plane < 3; plane++) {
        for (i = slice_start; i < slice_end; i++) {
            uint8_t *p = frame->data[plane] + i * frame->linesize[plane];
            for (j = 0; j < width; j++) {
                /* 8421367 = ((128 << 1) + 1) << 15. It is an integer
                 * representation of 128.5. The .5 is for rounding
                 * purposes. */
                *p = ((*p - 128) * s->factor + 8421367) >> 16;
                p++;
            }
        }
    }

    return 0;
}

static int filter_slice_alpha(AVFilterContext *ctx, void *arg, int jobnr,
                              int nb_jobs)
{
    FadeContext *s = ctx->priv;
    AVFrame *frame = arg;
    int plane = s->is_packed_rgb ? 0 : A;
    int slice_start = (frame->height *  jobnr   ) / nb_jobs;
    int slice_end   = (frame->height * (jobnr+1)) / nb_jobs;
    int i, j;

    for (i = slice_start; i < slice_end; i++) {
        uint8_t *p = frame->data[plane] + i * frame->linesize[plane] + s->is_packed_rgb*s->rgba_map[A];
        int step = s->is_packed_rgb ? 4 : 1;
        for (j = 0; j < frame->width; j++) {
            /* s->factor is using 16 lower-order bits for decimal
             * places. 32768 = 1 << 15, it is an integer representation
             * of 0.5 and is for rounding. */
            *p = ((*p - s->black_level) * s->factor + s->black_level_scaled) >> 16;
            p += step;
        }
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    AVFilterContext *ctx = inlink->dst;
    FadeContext *s       = ctx->priv;
    double frame_timestamp = frame->pts == AV_NOPTS_VALUE ? -1 : frame->pts * av_q2d(inlink->time_base);

    // Calculate Fade assuming this is a Fade In
    if (s->fade_state == VF_FADE_WAITING) {
        s->factor=0;
        if (frame_timestamp >= s->start_time/(double)AV_TIME_BASE
            && inlink->frame_count >= s->start_frame) {
            // Time to start fading
            s->fade_state = VF_FADE_FADING;

            // Save start time in case we are starting based on frames and fading based on time
            if (s->start_time == 0 && s->start_frame != 0) {
                s->start_time = frame_timestamp*(double)AV_TIME_BASE;
            }

            // Save start frame in case we are starting based on time and fading based on frames
            if (s->start_time != 0 && s->start_frame == 0) {
                s->start_frame = inlink->frame_count;
            }
        }
    }
    if (s->fade_state == VF_FADE_FADING) {
        if (s->duration == 0) {
            // Fading based on frame count
            s->factor = (inlink->frame_count - s->start_frame) * s->fade_per_frame;
            if (inlink->frame_count > s->start_frame + s->nb_frames) {
                s->fade_state = VF_FADE_DONE;
            }

        } else {
            // Fading based on duration
            s->factor = (frame_timestamp - s->start_time/(double)AV_TIME_BASE)
                            * (float) UINT16_MAX / (s->duration/(double)AV_TIME_BASE);
            if (frame_timestamp > s->start_time/(double)AV_TIME_BASE
                                  + s->duration/(double)AV_TIME_BASE) {
                s->fade_state = VF_FADE_DONE;
            }
        }
    }
    if (s->fade_state == VF_FADE_DONE) {
        s->factor=UINT16_MAX;
    }

    s->factor = av_clip_uint16(s->factor);

    // Invert fade_factor if Fading Out
    if (s->type == FADE_OUT) {
        s->factor=UINT16_MAX-s->factor;
    }

    if (s->factor < UINT16_MAX) {
        if (s->alpha) {
            ctx->internal->execute(ctx, filter_slice_alpha, frame, NULL,
                                FFMIN(frame->height, ctx->graph->nb_threads));
        } else if (s->is_packed_rgb && !s->black_fade) {
            ctx->internal->execute(ctx, filter_slice_rgb, frame, NULL,
                                   FFMIN(frame->height, ctx->graph->nb_threads));
        } else {
            /* luma, or rgb plane in case of black */
            ctx->internal->execute(ctx, filter_slice_luma, frame, NULL,
                                FFMIN(frame->height, ctx->graph->nb_threads));

            if (frame->data[1] && frame->data[2]) {
                /* chroma planes */
                ctx->internal->execute(ctx, filter_slice_chroma, frame, NULL,
                                    FFMIN(frame->height, ctx->graph->nb_threads));
            }
        }
    }

    return ff_filter_frame(inlink->dst->outputs[0], frame);
}


#define OFFSET(x) offsetof(FadeContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption fade_options[] = {
    { "type", "'in' or 'out' for fade-in/fade-out", OFFSET(type), AV_OPT_TYPE_INT, { .i64 = FADE_IN }, FADE_IN, FADE_OUT, FLAGS, "type" },
    { "t",    "'in' or 'out' for fade-in/fade-out", OFFSET(type), AV_OPT_TYPE_INT, { .i64 = FADE_IN }, FADE_IN, FADE_OUT, FLAGS, "type" },
        { "in",  "fade-in",  0, AV_OPT_TYPE_CONST, { .i64 = FADE_IN },  .unit = "type" },
        { "out", "fade-out", 0, AV_OPT_TYPE_CONST, { .i64 = FADE_OUT }, .unit = "type" },
    { "start_frame", "Number of the first frame to which to apply the effect.",
                                                    OFFSET(start_frame), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX, FLAGS },
    { "s",           "Number of the first frame to which to apply the effect.",
                                                    OFFSET(start_frame), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX, FLAGS },
    { "nb_frames",   "Number of frames to which the effect should be applied.",
                                                    OFFSET(nb_frames),   AV_OPT_TYPE_INT, { .i64 = 25 }, 0, INT_MAX, FLAGS },
    { "n",           "Number of frames to which the effect should be applied.",
                                                    OFFSET(nb_frames),   AV_OPT_TYPE_INT, { .i64 = 25 }, 0, INT_MAX, FLAGS },
    { "alpha",       "fade alpha if it is available on the input", OFFSET(alpha),       AV_OPT_TYPE_BOOL, {.i64 = 0    }, 0,       1, FLAGS },
    { "start_time",  "Number of seconds of the beginning of the effect.",
                                                    OFFSET(start_time),  AV_OPT_TYPE_DURATION, {.i64 = 0. }, 0, INT32_MAX, FLAGS },
    { "st",          "Number of seconds of the beginning of the effect.",
                                                    OFFSET(start_time),  AV_OPT_TYPE_DURATION, {.i64 = 0. }, 0, INT32_MAX, FLAGS },
    { "duration",    "Duration of the effect in seconds.",
                                                    OFFSET(duration),    AV_OPT_TYPE_DURATION, {.i64 = 0. }, 0, INT32_MAX, FLAGS },
    { "d",           "Duration of the effect in seconds.",
                                                    OFFSET(duration),    AV_OPT_TYPE_DURATION, {.i64 = 0. }, 0, INT32_MAX, FLAGS },
    { "color",       "set color",                   OFFSET(color_rgba),  AV_OPT_TYPE_COLOR,    {.str = "black"}, CHAR_MIN, CHAR_MAX, FLAGS },
    { "c",           "set color",                   OFFSET(color_rgba),  AV_OPT_TYPE_COLOR,    {.str = "black"}, CHAR_MIN, CHAR_MAX, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(fade);

static const AVFilterPad avfilter_vf_fade_inputs[] = {
    {
        .name           = "default",
        .type           = AVMEDIA_TYPE_VIDEO,
        .config_props   = config_props,
        .filter_frame   = filter_frame,
        .needs_writable = 1,
    },
    { NULL }
};

static const AVFilterPad avfilter_vf_fade_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter ff_vf_fade = {
    .name          = "fade",
    .description   = NULL_IF_CONFIG_SMALL("Fade in/out input video."),
    .init          = init,
    .priv_size     = sizeof(FadeContext),
    .priv_class    = &fade_class,
    .query_formats = query_formats,
    .inputs        = avfilter_vf_fade_inputs,
    .outputs       = avfilter_vf_fade_outputs,
    .flags         = AVFILTER_FLAG_SLICE_THREADS,
};
