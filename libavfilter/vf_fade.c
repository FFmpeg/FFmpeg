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

typedef struct {
    const AVClass *class;
    int factor, fade_per_frame;
    unsigned int frame_index, start_frame, stop_frame, nb_frames;
    int hsub, vsub, bpp;
    unsigned int black_level, black_level_scaled;
    uint8_t is_packed_rgb;
    uint8_t rgba_map[4];
    int alpha;

    char *type;
} FadeContext;

#define OFFSET(x) offsetof(FadeContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption fade_options[] = {
    { "type",        "set the fade direction",                     OFFSET(type),        AV_OPT_TYPE_STRING, {.str = "in" }, CHAR_MIN, CHAR_MAX, FLAGS },
    { "t",           "set the fade direction",                     OFFSET(type),        AV_OPT_TYPE_STRING, {.str = "in" }, CHAR_MIN, CHAR_MAX, FLAGS },
    { "start_frame", "set expression of frame to start fading",    OFFSET(start_frame), AV_OPT_TYPE_INT, {.i64 = 0    }, 0, INT_MAX, FLAGS },
    { "s",           "set expression of frame to start fading",    OFFSET(start_frame), AV_OPT_TYPE_INT, {.i64 = 0    }, 0, INT_MAX, FLAGS },
    { "nb_frames",   "set expression for fade duration in frames", OFFSET(nb_frames),   AV_OPT_TYPE_INT, {.i64 = 25   }, 0, INT_MAX, FLAGS },
    { "n",           "set expression for fade duration in frames", OFFSET(nb_frames),   AV_OPT_TYPE_INT, {.i64 = 25   }, 0, INT_MAX, FLAGS },
    { "alpha",       "fade alpha if it is available on the input", OFFSET(alpha),       AV_OPT_TYPE_INT, {.i64 = 0    }, 0,       1, FLAGS },
    {NULL},
};

AVFILTER_DEFINE_CLASS(fade);

static av_cold int init(AVFilterContext *ctx, const char *args)
{
    FadeContext *fade = ctx->priv;
    int ret = 0;
    char *args1, *expr, *bufptr = NULL;

    fade->class = &fade_class;
    av_opt_set_defaults(fade);

    if (!(args1 = av_strdup(args))) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    if (expr = av_strtok(args1, ":", &bufptr)) {
        av_free(fade->type);
        if (!(fade->type = av_strdup(expr))) {
            ret = AVERROR(ENOMEM);
            goto end;
        }
    }
    if (expr = av_strtok(NULL, ":", &bufptr)) {
        if ((ret = av_opt_set(fade, "start_frame", expr, 0)) < 0) {
            av_log(ctx, AV_LOG_ERROR,
                   "Invalid value '%s' for start_frame option\n", expr);
            return ret;
        }
    }
    if (expr = av_strtok(NULL, ":", &bufptr)) {
        if ((ret = av_opt_set(fade, "nb_frames", expr, 0)) < 0) {
            av_log(ctx, AV_LOG_ERROR,
                   "Invalid value '%s' for nb_frames option\n", expr);
            return ret;
        }
    }

    if (bufptr && (ret = av_set_options_string(fade, bufptr, "=", ":")) < 0)
        goto end;

    fade->fade_per_frame = (1 << 16) / fade->nb_frames;
    if (!strcmp(fade->type, "in"))
        fade->factor = 0;
    else if (!strcmp(fade->type, "out")) {
        fade->fade_per_frame = -fade->fade_per_frame;
        fade->factor = (1 << 16);
    } else {
        av_log(ctx, AV_LOG_ERROR,
               "Type argument must be 'in' or 'out' but '%s' was specified\n", fade->type);
        ret = AVERROR(EINVAL);
        goto end;
    }
    fade->stop_frame = fade->start_frame + fade->nb_frames;

    av_log(ctx, AV_LOG_VERBOSE,
           "type:%s start_frame:%d nb_frames:%d alpha:%d\n",
           fade->type, fade->start_frame, fade->nb_frames, fade->alpha);

end:
    av_free(args1);
    return ret;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    FadeContext *fade = ctx->priv;

    av_freep(&fade->type);
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum PixelFormat pix_fmts[] = {
        PIX_FMT_YUV444P,  PIX_FMT_YUV422P,  PIX_FMT_YUV420P,
        PIX_FMT_YUV411P,  PIX_FMT_YUV410P,
        PIX_FMT_YUVJ444P, PIX_FMT_YUVJ422P, PIX_FMT_YUVJ420P,
        PIX_FMT_YUV440P,  PIX_FMT_YUVJ440P,
        PIX_FMT_YUVA420P,
        PIX_FMT_RGB24,    PIX_FMT_BGR24,
        PIX_FMT_ARGB,     PIX_FMT_ABGR,
        PIX_FMT_RGBA,     PIX_FMT_BGRA,
        PIX_FMT_NONE
    };

    ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));
    return 0;
}

const static enum PixelFormat studio_level_pix_fmts[] = {
    PIX_FMT_YUV444P,  PIX_FMT_YUV422P,  PIX_FMT_YUV420P,
    PIX_FMT_YUV411P,  PIX_FMT_YUV410P,
    PIX_FMT_YUV440P,
    PIX_FMT_NONE
};

static enum PixelFormat alpha_pix_fmts[] = {
    PIX_FMT_YUVA420P,
    PIX_FMT_ARGB, PIX_FMT_ABGR,
    PIX_FMT_RGBA, PIX_FMT_BGRA,
    PIX_FMT_NONE
};

static int config_props(AVFilterLink *inlink)
{
    FadeContext *fade = inlink->dst->priv;
    const AVPixFmtDescriptor *pixdesc = &av_pix_fmt_descriptors[inlink->format];

    fade->hsub = pixdesc->log2_chroma_w;
    fade->vsub = pixdesc->log2_chroma_h;

    fade->bpp = av_get_bits_per_pixel(pixdesc) >> 3;
    fade->alpha = fade->alpha ? ff_fmt_is_in(inlink->format, alpha_pix_fmts) : 0;
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

static int draw_slice(AVFilterLink *inlink, int y, int h, int slice_dir)
{
    FadeContext *fade = inlink->dst->priv;
    AVFilterBufferRef *outpic = inlink->cur_buf;
    uint8_t *p;
    int i, j, plane;

    if (fade->factor < UINT16_MAX) {
        if (fade->alpha) {
            // alpha only
            plane = fade->is_packed_rgb ? 0 : A; // alpha is on plane 0 for packed formats
                                                 // or plane 3 for planar formats
            fade_plane(y, h, inlink->w,
                       fade->factor, fade->black_level, fade->black_level_scaled,
                       fade->is_packed_rgb ? fade->rgba_map[A] : 0, // alpha offset for packed formats
                       fade->is_packed_rgb ? 4 : 1,                 // pixstep for 8 bit packed formats
                       1, outpic->data[plane], outpic->linesize[plane]);
        } else {
            /* luma or rgb plane */
            fade_plane(y, h, inlink->w,
                       fade->factor, fade->black_level, fade->black_level_scaled,
                       0, 1, // offset & pixstep for Y plane or RGB packed format
                       fade->bpp, outpic->data[0], outpic->linesize[0]);
            if (outpic->data[1] && outpic->data[2]) {
                /* chroma planes */
                for (plane = 1; plane < 3; plane++) {
                    for (i = 0; i < h; i++) {
                        p = outpic->data[plane] + ((y+i) >> fade->vsub) * outpic->linesize[plane];
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

    return ff_draw_slice(inlink->dst->outputs[0], y, h, slice_dir);
}

static int end_frame(AVFilterLink *inlink)
{
    FadeContext *fade = inlink->dst->priv;
    int ret;

    ret = ff_end_frame(inlink->dst->outputs[0]);

    if (fade->frame_index >= fade->start_frame &&
        fade->frame_index <= fade->stop_frame)
        fade->factor += fade->fade_per_frame;
    fade->factor = av_clip_uint16(fade->factor);
    fade->frame_index++;

    return ret;
}

AVFilter avfilter_vf_fade = {
    .name          = "fade",
    .description   = NULL_IF_CONFIG_SMALL("Fade in/out input video."),
    .init          = init,
    .uninit        = uninit,
    .priv_size     = sizeof(FadeContext),
    .query_formats = query_formats,

    .inputs    = (const AVFilterPad[]) {{ .name            = "default",
                                          .type            = AVMEDIA_TYPE_VIDEO,
                                          .config_props    = config_props,
                                          .get_video_buffer = ff_null_get_video_buffer,
                                          .start_frame      = ff_null_start_frame,
                                          .draw_slice      = draw_slice,
                                          .end_frame       = end_frame,
                                          .min_perms       = AV_PERM_READ | AV_PERM_WRITE },
                                        { .name = NULL}},
    .outputs   = (const AVFilterPad[]) {{ .name            = "default",
                                          .type            = AVMEDIA_TYPE_VIDEO, },
                                        { .name = NULL}},
    .priv_class = &fade_class,
};
