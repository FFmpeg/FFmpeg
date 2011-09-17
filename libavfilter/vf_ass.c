/*
 * Copyright (c) 2011 Baptiste Coudurier
 * Copyright (c) 2011 Stefano Sabatini
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
 * Libass subtitles burning filter.
 *
 * @see{http://www.matroska.org/technical/specs/subtitles/ssa.html}
 */

#include <ass/ass.h>

#include "libavutil/avstring.h"
#include "libavutil/imgutils.h"
#include "libavutil/pixdesc.h"
#include "drawutils.h"
#include "avfilter.h"

typedef struct {
    ASS_Library  *library;
    ASS_Renderer *renderer;
    ASS_Track    *track;
    int hsub, vsub;
    char *filename;
    uint8_t rgba_map[4];
    int     pix_step[4];       ///< steps per pixel for each plane of the main output
} AssContext;

/* libass supports a log level ranging from 0 to 7 */
int ass_libav_log_level_map[] = {
    AV_LOG_QUIET,               /* 0 */
    AV_LOG_PANIC,               /* 1 */
    AV_LOG_FATAL,               /* 2 */
    AV_LOG_ERROR,               /* 3 */
    AV_LOG_WARNING,             /* 4 */
    AV_LOG_INFO,                /* 5 */
    AV_LOG_VERBOSE,             /* 6 */
    AV_LOG_DEBUG,               /* 7 */
};

static void ass_log(int ass_level, const char *fmt, va_list args, void *ctx)
{
    int level = ass_libav_log_level_map[ass_level];

    av_vlog(ctx, level, fmt, args);
    av_log(ctx, level, "\n");
}

static av_cold int init(AVFilterContext *ctx, const char *args, void *opaque)
{
    AssContext *ass = ctx->priv;

    if (args)
        ass->filename = av_get_token(&args, ":");
    if (!ass->filename || !*ass->filename) {
        av_log(ctx, AV_LOG_ERROR, "No filename provided!\n");
        return AVERROR(EINVAL);
    }

    ass->library = ass_library_init();
    if (!ass->library) {
        av_log(ctx, AV_LOG_ERROR, "Could not initialize libass.\n");
        return AVERROR(EINVAL);
    }
    ass_set_message_cb(ass->library, ass_log, ctx);

    ass->renderer = ass_renderer_init(ass->library);
    if (!ass->renderer) {
        av_log(ctx, AV_LOG_ERROR, "Could not initialize libass renderer.\n");
        return AVERROR(EINVAL);
    }

    ass->track = ass_read_file(ass->library, ass->filename, NULL);
    if (!ass->track) {
        av_log(ctx, AV_LOG_ERROR,
               "Could not create a libass track when reading file '%s'\n",
               ass->filename);
        return AVERROR(EINVAL);
    }

    ass_set_fonts(ass->renderer, NULL, NULL, 1, NULL, 1);

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    AssContext *ass = ctx->priv;

    av_freep(&ass->filename);
    if (ass->track)
        ass_free_track(ass->track);
    if (ass->renderer)
        ass_renderer_done(ass->renderer);
    if (ass->library)
        ass_library_done(ass->library);
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum PixelFormat pix_fmts[] = {
        PIX_FMT_ARGB,  PIX_FMT_RGBA,
        PIX_FMT_ABGR,  PIX_FMT_BGRA,
        PIX_FMT_RGB24, PIX_FMT_BGR24,
        PIX_FMT_NONE
    };

    avfilter_set_common_pixel_formats(ctx, avfilter_make_format_list(pix_fmts));

    return 0;
}

static int config_input(AVFilterLink *inlink)
{
    AssContext *ass = inlink->dst->priv;
    const AVPixFmtDescriptor *pix_desc = &av_pix_fmt_descriptors[inlink->format];
    double sar = inlink->sample_aspect_ratio.num ?
        av_q2d(inlink->sample_aspect_ratio) : 1;
    double dar = inlink->w / inlink->h * sar;

    av_image_fill_max_pixsteps(ass->pix_step, NULL, pix_desc);
    ff_fill_rgba_map(ass->rgba_map, inlink->format);

    ass->hsub = pix_desc->log2_chroma_w;
    ass->vsub = pix_desc->log2_chroma_h;

    ass_set_frame_size  (ass->renderer, inlink->w, inlink->h);
    ass_set_aspect_ratio(ass->renderer, dar, sar);

    return 0;
}

static void null_draw_slice(AVFilterLink *link, int y, int h, int slice_dir) { }

#define R 0
#define G 1
#define B 2
#define A 3

#define SET_PIXEL_RGB(picref, rgba_color, val, x, y, pixel_step, r_off, g_off, b_off) { \
    p   = picref->data[0] + (x) * pixel_step + ((y) * picref->linesize[0]);             \
    alpha = rgba_color[A] * (val) * 129;                                                \
    *(p+r_off) = (alpha * rgba_color[R] + (255*255*129 - alpha) * *(p+r_off)) >> 23;    \
    *(p+g_off) = (alpha * rgba_color[G] + (255*255*129 - alpha) * *(p+g_off)) >> 23;    \
    *(p+b_off) = (alpha * rgba_color[B] + (255*255*129 - alpha) * *(p+b_off)) >> 23;    \
}

/* libass stores an RGBA color in the format RRGGBBTT, where TT is the transparency level */
#define AR(c)  ( (c)>>24)
#define AG(c)  (((c)>>16)&0xFF)
#define AB(c)  (((c)>>8) &0xFF)
#define AA(c)  ((0xFF-c) &0xFF)

static void overlay_ass_image(AVFilterBufferRef *picref, const uint8_t *rgba_map, const int pix_step,
                              const ASS_Image *image)
{
    int i, j;
    int alpha;
    uint8_t *p;
    const int ro = rgba_map[R];
    const int go = rgba_map[G];
    const int bo = rgba_map[B];

    for (; image; image = image->next) {
        uint8_t rgba_color[] = {AR(image->color), AG(image->color), AB(image->color), AA(image->color)};
        unsigned char *row = image->bitmap;

        for (i = 0; i < image->h; i++) {
            for (j = 0; j < image->w; j++) {
                if (row[j]) {
                    SET_PIXEL_RGB(picref, rgba_color, row[j], image->dst_x + j, image->dst_y + i, pix_step, ro, go, bo);
                }
            }
            row += image->stride;
        }
    }
}

static void end_frame(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    AssContext *ass = ctx->priv;
    AVFilterBufferRef *picref = inlink->cur_buf;
    int detect_change = 0;
    double time_ms = picref->pts * av_q2d(inlink->time_base) * 1000;
    ASS_Image *image = ass_render_frame(ass->renderer, ass->track,
                                        time_ms, &detect_change);

    if (detect_change)
        av_log(ctx, AV_LOG_DEBUG, "Change happened at time ms:%f\n", time_ms);

    overlay_ass_image(picref, ass->rgba_map, ass->pix_step[0], image);

    avfilter_draw_slice(outlink, 0, picref->video->h, 1);
    avfilter_end_frame(outlink);
}

AVFilter avfilter_vf_ass = {
    .name          = "ass",
    .description   = NULL_IF_CONFIG_SMALL("Render subtitles onto input video using the libass library."),
    .priv_size     = sizeof(AssContext),
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,

    .inputs = (const AVFilterPad[]) {
        { .name             = "default",
          .type             = AVMEDIA_TYPE_VIDEO,
          .get_video_buffer = avfilter_null_get_video_buffer,
          .start_frame      = avfilter_null_start_frame,
          .draw_slice       = null_draw_slice,
          .end_frame        = end_frame,
          .config_props     = config_input,
          .min_perms        = AV_PERM_WRITE | AV_PERM_READ,
          .rej_perms        = AV_PERM_PRESERVE },
        { .name = NULL}
    },
    .outputs = (const AVFilterPad[]) {
        { .name             = "default",
          .type             = AVMEDIA_TYPE_VIDEO, },
        { .name = NULL}
    },
};
