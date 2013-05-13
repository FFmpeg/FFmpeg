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

#include "libavutil/avstring.h"
#include "libavutil/common.h"
#include "libavutil/eval.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "drawutils.h"
#include "internal.h"
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

typedef struct {
    const AVClass *class;
    int type;
    int factor, fade_per_frame;
    int start_frame, nb_frames;
    unsigned int frame_index;
    int hsub, vsub, bpp;
    unsigned int black_level, black_level_scaled;
    uint8_t is_packed_rgb;
    uint8_t rgba_map[4];
    int alpha;
    uint64_t start_time, duration;
    enum {VF_FADE_WAITING=0, VF_FADE_FADING, VF_FADE_DONE} fade_state;
} FadeContext;

static av_cold int init(AVFilterContext *ctx)
{
    FadeContext *fade = ctx->priv;

    fade->fade_per_frame = (1 << 16) / fade->nb_frames;
    fade->fade_state = VF_FADE_WAITING;

    if (fade->duration != 0) {
        // If duration (seconds) is non-zero, assume that we are not fading based on frames
        fade->nb_frames = 0; // Mostly to clean up logging
    }

    // Choose what to log. If both time-based and frame-based options, both lines will be in the log
    if (fade->start_frame || fade->nb_frames) {
        av_log(ctx, AV_LOG_VERBOSE,
               "type:%s start_frame:%d nb_frames:%d alpha:%d\n",
               fade->type == FADE_IN ? "in" : "out", fade->start_frame,
               fade->nb_frames,fade->alpha);
    }
    if (fade->start_time || fade->duration) {
        av_log(ctx, AV_LOG_VERBOSE,
               "type:%s start_time:%f duration:%f alpha:%d\n",
               fade->type == FADE_IN ? "in" : "out", (fade->start_time / (double)AV_TIME_BASE),
               (fade->duration / (double)AV_TIME_BASE),fade->alpha);
    }

    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
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

    ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));
    return 0;
}

const static enum AVPixelFormat studio_level_pix_fmts[] = {
    AV_PIX_FMT_YUV444P,  AV_PIX_FMT_YUV422P,  AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUV411P,  AV_PIX_FMT_YUV410P,
    AV_PIX_FMT_YUV440P,
    AV_PIX_FMT_NONE
};

static int config_props(AVFilterLink *inlink)
{
    FadeContext *fade = inlink->dst->priv;
    const AVPixFmtDescriptor *pixdesc = av_pix_fmt_desc_get(inlink->format);

    fade->hsub = pixdesc->log2_chroma_w;
    fade->vsub = pixdesc->log2_chroma_h;

    fade->bpp = av_get_bits_per_pixel(pixdesc) >> 3;
    fade->alpha &= pixdesc->flags & PIX_FMT_ALPHA;
    fade->is_packed_rgb = ff_fill_rgba_map(fade->rgba_map, inlink->format) >= 0;

    /* use CCIR601/709 black level for studio-level pixel non-alpha components */
    fade->black_level =
            ff_fmt_is_in(inlink->format, studio_level_pix_fmts) && !fade->alpha ? 16 : 0;
    /* 32768 = 1 << 15, it is an integer representation
     * of 0.5 and is for rounding. */
    fade->black_level_scaled = (fade->black_level << 16) + 32768;
    return 0;
}

static void fade_plane(int y, int h, int w,
                       int fade_factor, int black_level, int black_level_scaled,
                       uint8_t offset, uint8_t step, int bytes_per_plane,
                       uint8_t *data, int line_size)
{
    uint8_t *p;
    int i, j;

    /* luma, alpha or rgb plane */
    for (i = 0; i < h; i++) {
        p = data + offset + (y+i) * line_size;
        for (j = 0; j < w * bytes_per_plane; j++) {
            /* fade->factor is using 16 lower-order bits for decimal places. */
            *p = ((*p - black_level) * fade_factor + black_level_scaled) >> 16;
            p+=step;
        }
    }
}

static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    FadeContext *fade = inlink->dst->priv;
    uint8_t *p;
    int i, j, plane;
    double frame_timestamp = frame->pts == AV_NOPTS_VALUE ? -1 : frame->pts * av_q2d(inlink->time_base);

    // Calculate Fade assuming this is a Fade In
    if (fade->fade_state == VF_FADE_WAITING) {
        fade->factor=0;
        if ((frame_timestamp >= (fade->start_time/(double)AV_TIME_BASE))
            && (fade->frame_index >= fade->start_frame)) {
            // Time to start fading
            fade->fade_state = VF_FADE_FADING;

            // Save start time in case we are starting based on frames and fading based on time
            if ((fade->start_time == 0) && (fade->start_frame != 0)) {
                fade->start_time = frame_timestamp*(double)AV_TIME_BASE;
            }

            // Save start frame in case we are starting based on time and fading based on frames
            if ((fade->start_time != 0) && (fade->start_frame == 0)) {
                fade->start_frame = fade->frame_index;
            }
        }
    }
    if (fade->fade_state == VF_FADE_FADING) {
        if (fade->duration == 0) {
            // Fading based on frame count
            fade->factor = (fade->frame_index - fade->start_frame) * fade->fade_per_frame;
            if (fade->frame_index > (fade->start_frame + fade->nb_frames)) {
                fade->fade_state = VF_FADE_DONE;
            }

        } else {
            // Fading based on duration
            fade->factor = (frame_timestamp - (fade->start_time/(double)AV_TIME_BASE))
                            * (float) UINT16_MAX / (fade->duration/(double)AV_TIME_BASE);
            if (frame_timestamp > ((fade->start_time/(double)AV_TIME_BASE)
                                    + (fade->duration/(double)AV_TIME_BASE))) {
                fade->fade_state = VF_FADE_DONE;
            }
        }
    }
    if (fade->fade_state == VF_FADE_DONE) {
        fade->factor=UINT16_MAX;
    }

    fade->factor = av_clip_uint16(fade->factor);

    // Invert fade_factor if Fading Out
    if (fade->type == 1) {
        fade->factor=UINT16_MAX-fade->factor;
    }

    if (fade->factor < UINT16_MAX) {
        if (fade->alpha) {
            // alpha only
            plane = fade->is_packed_rgb ? 0 : A; // alpha is on plane 0 for packed formats
                                                 // or plane 3 for planar formats
            fade_plane(0, frame->height, inlink->w,
                       fade->factor, fade->black_level, fade->black_level_scaled,
                       fade->is_packed_rgb ? fade->rgba_map[A] : 0, // alpha offset for packed formats
                       fade->is_packed_rgb ? 4 : 1,                 // pixstep for 8 bit packed formats
                       1, frame->data[plane], frame->linesize[plane]);
        } else {
            /* luma or rgb plane */
            fade_plane(0, frame->height, inlink->w,
                       fade->factor, fade->black_level, fade->black_level_scaled,
                       0, 1, // offset & pixstep for Y plane or RGB packed format
                       fade->bpp, frame->data[0], frame->linesize[0]);
            if (frame->data[1] && frame->data[2]) {
                /* chroma planes */
                for (plane = 1; plane < 3; plane++) {
                    for (i = 0; i < frame->height; i++) {
                        p = frame->data[plane] + (i >> fade->vsub) * frame->linesize[plane];
                        for (j = 0; j < inlink->w >> fade->hsub; j++) {
                            /* 8421367 = ((128 << 1) + 1) << 15. It is an integer
                             * representation of 128.5. The .5 is for rounding
                             * purposes. */
                            *p = ((*p - 128) * fade->factor + 8421367) >> 16;
                            p++;
                        }
                    }
                }
            }
        }
    }

    fade->frame_index++;

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
    { "alpha",       "fade alpha if it is available on the input", OFFSET(alpha),       AV_OPT_TYPE_INT, {.i64 = 0    }, 0,       1, FLAGS },
    { "start_time",  "Number of seconds of the beginning of the effect.",
                                                    OFFSET(start_time),  AV_OPT_TYPE_DURATION, {.i64 = 0. }, 0, INT32_MAX, FLAGS },
    { "st",          "Number of seconds of the beginning of the effect.",
                                                    OFFSET(start_time),  AV_OPT_TYPE_DURATION, {.i64 = 0. }, 0, INT32_MAX, FLAGS },
    { "duration",    "Duration of the effect in seconds.",
                                                    OFFSET(duration),    AV_OPT_TYPE_DURATION, {.i64 = 0. }, 0, INT32_MAX, FLAGS },
    { "d",           "Duration of the effect in seconds.",
                                                    OFFSET(duration),    AV_OPT_TYPE_DURATION, {.i64 = 0. }, 0, INT32_MAX, FLAGS },
    { NULL },
};

AVFILTER_DEFINE_CLASS(fade);

static const AVFilterPad avfilter_vf_fade_inputs[] = {
    {
        .name             = "default",
        .type             = AVMEDIA_TYPE_VIDEO,
        .config_props     = config_props,
        .get_video_buffer = ff_null_get_video_buffer,
        .filter_frame     = filter_frame,
        .needs_writable   = 1,
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

AVFilter avfilter_vf_fade = {
    .name          = "fade",
    .description   = NULL_IF_CONFIG_SMALL("Fade in/out input video."),
    .init          = init,
    .priv_size     = sizeof(FadeContext),
    .priv_class    = &fade_class,
    .query_formats = query_formats,

    .inputs    = avfilter_vf_fade_inputs,
    .outputs   = avfilter_vf_fade_outputs,
};
