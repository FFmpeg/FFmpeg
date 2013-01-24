/*
 * Copyright (c) 2011 Baptiste Coudurier
 * Copyright (c) 2011 Stefano Sabatini
 * Copyright (c) 2012 Clément Bœsch
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

#include "config.h"
#if CONFIG_SUBTITLES_FILTER
# include "libavcodec/avcodec.h"
# include "libavformat/avformat.h"
#endif
#include "libavutil/avstring.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "drawutils.h"
#include "avfilter.h"
#include "internal.h"
#include "formats.h"
#include "video.h"

typedef struct {
    const AVClass *class;
    ASS_Library  *library;
    ASS_Renderer *renderer;
    ASS_Track    *track;
    char *filename;
    uint8_t rgba_map[4];
    int     pix_step[4];       ///< steps per pixel for each plane of the main output
    int original_w, original_h;
    FFDrawContext draw;
} AssContext;

#define OFFSET(x) offsetof(AssContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption options[] = {
    {"filename",       "set the filename of file to read",                         OFFSET(filename),   AV_OPT_TYPE_STRING,     {.str = NULL},  CHAR_MIN, CHAR_MAX, FLAGS },
    {"f",              "set the filename of file to read",                         OFFSET(filename),   AV_OPT_TYPE_STRING,     {.str = NULL},  CHAR_MIN, CHAR_MAX, FLAGS },
    {"original_size",  "set the size of the original video (used to scale fonts)", OFFSET(original_w), AV_OPT_TYPE_IMAGE_SIZE, {.str = NULL},  CHAR_MIN, CHAR_MAX, FLAGS },
    {NULL},
};

/* libass supports a log level ranging from 0 to 7 */
static const int ass_libavfilter_log_level_map[] = {
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
    int level = ass_libavfilter_log_level_map[ass_level];

    av_vlog(ctx, level, fmt, args);
    av_log(ctx, level, "\n");
}

static av_cold int init(AVFilterContext *ctx, const char *args, const AVClass *class)
{
    AssContext *ass = ctx->priv;
    static const char *shorthand[] = { "filename", NULL };
    int ret;

    ass->class = class;
    av_opt_set_defaults(ass);

    if ((ret = av_opt_set_from_string(ass, args, shorthand, "=", ":")) < 0)
        return ret;

    if (!ass->filename) {
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

    ass_set_fonts(ass->renderer, NULL, NULL, 1, NULL, 1);
    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    AssContext *ass = ctx->priv;

    av_opt_free(ass);
    if (ass->track)
        ass_free_track(ass->track);
    if (ass->renderer)
        ass_renderer_done(ass->renderer);
    if (ass->library)
        ass_library_done(ass->library);
}

static int query_formats(AVFilterContext *ctx)
{
    ff_set_common_formats(ctx, ff_draw_supported_pixel_formats(0));
    return 0;
}

static int config_input(AVFilterLink *inlink)
{
    AssContext *ass = inlink->dst->priv;

    ff_draw_init(&ass->draw, inlink->format, 0);

    ass_set_frame_size  (ass->renderer, inlink->w, inlink->h);
    if (ass->original_w && ass->original_h)
        ass_set_aspect_ratio(ass->renderer, (double)inlink->w / inlink->h,
                             (double)ass->original_w / ass->original_h);

    return 0;
}

/* libass stores an RGBA color in the format RRGGBBTT, where TT is the transparency level */
#define AR(c)  ( (c)>>24)
#define AG(c)  (((c)>>16)&0xFF)
#define AB(c)  (((c)>>8) &0xFF)
#define AA(c)  ((0xFF-c) &0xFF)

static void overlay_ass_image(AssContext *ass, AVFilterBufferRef *picref,
                              const ASS_Image *image)
{
    for (; image; image = image->next) {
        uint8_t rgba_color[] = {AR(image->color), AG(image->color), AB(image->color), AA(image->color)};
        FFDrawColor color;
        ff_draw_color(&ass->draw, &color, rgba_color);
        ff_blend_mask(&ass->draw, &color,
                      picref->data, picref->linesize,
                      picref->video->w, picref->video->h,
                      image->bitmap, image->stride, image->w, image->h,
                      3, 0, image->dst_x, image->dst_y);
    }
}

static int filter_frame(AVFilterLink *inlink, AVFilterBufferRef *picref)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    AssContext *ass = ctx->priv;
    int detect_change = 0;
    double time_ms = picref->pts * av_q2d(inlink->time_base) * 1000;
    ASS_Image *image = ass_render_frame(ass->renderer, ass->track,
                                        time_ms, &detect_change);

    if (detect_change)
        av_log(ctx, AV_LOG_DEBUG, "Change happened at time ms:%f\n", time_ms);

    overlay_ass_image(ass, picref, image);

    return ff_filter_frame(outlink, picref);
}

static const AVFilterPad ass_inputs[] = {
    {
        .name             = "default",
        .type             = AVMEDIA_TYPE_VIDEO,
        .filter_frame     = filter_frame,
        .config_props     = config_input,
        .min_perms        = AV_PERM_READ | AV_PERM_WRITE,
    },
    { NULL }
};

static const AVFilterPad ass_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

#if CONFIG_ASS_FILTER

#define ass_options options
AVFILTER_DEFINE_CLASS(ass);

static av_cold int init_ass(AVFilterContext *ctx, const char *args)
{
    AssContext *ass = ctx->priv;
    int ret = init(ctx, args, &ass_class);

    if (ret < 0)
        return ret;

    ass->track = ass_read_file(ass->library, ass->filename, NULL);
    if (!ass->track) {
        av_log(ctx, AV_LOG_ERROR,
               "Could not create a libass track when reading file '%s'\n",
               ass->filename);
        return AVERROR(EINVAL);
    }
    return 0;
}

AVFilter avfilter_vf_ass = {
    .name          = "ass",
    .description   = NULL_IF_CONFIG_SMALL("Render subtitles onto input video using the libass library."),
    .priv_size     = sizeof(AssContext),
    .init          = init_ass,
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = ass_inputs,
    .outputs       = ass_outputs,
    .priv_class    = &ass_class,
};
#endif

#if CONFIG_SUBTITLES_FILTER

#define subtitles_options options
AVFILTER_DEFINE_CLASS(subtitles);

static av_cold int init_subtitles(AVFilterContext *ctx, const char *args)
{
    int ret, sid;
    AVFormatContext *fmt = NULL;
    AVCodecContext *dec_ctx = NULL;
    AVCodec *dec = NULL;
    AVStream *st;
    AVPacket pkt;
    AssContext *ass = ctx->priv;

    /* Init libass */
    ret = init(ctx, args, &subtitles_class);
    if (ret < 0)
        return ret;
    ass->track = ass_new_track(ass->library);
    if (!ass->track) {
        av_log(ctx, AV_LOG_ERROR, "Could not create a libass track\n");
        return AVERROR(EINVAL);
    }

    /* Open subtitles file */
    ret = avformat_open_input(&fmt, ass->filename, NULL, NULL);
    if (ret < 0) {
        av_log(ctx, AV_LOG_ERROR, "Unable to open %s\n", ass->filename);
        goto end;
    }
    ret = avformat_find_stream_info(fmt, NULL);
    if (ret < 0)
        goto end;

    /* Locate subtitles stream */
    ret = av_find_best_stream(fmt, AVMEDIA_TYPE_SUBTITLE, -1, -1, NULL, 0);
    if (ret < 0) {
        av_log(ctx, AV_LOG_ERROR, "Unable to locate subtitle stream in %s\n",
               ass->filename);
        goto end;
    }
    sid = ret;
    st = fmt->streams[sid];

    /* Open decoder */
    dec_ctx = st->codec;
    dec = avcodec_find_decoder(dec_ctx->codec_id);
    if (!dec) {
        av_log(ctx, AV_LOG_ERROR, "Failed to find subtitle codec %s\n",
               avcodec_get_name(dec_ctx->codec_id));
        return AVERROR(EINVAL);
    }
    ret = avcodec_open2(dec_ctx, dec, NULL);
    if (ret < 0)
        goto end;

    /* Decode subtitles and push them into the renderer (libass) */
    if (dec_ctx->subtitle_header)
        ass_process_codec_private(ass->track,
                                  dec_ctx->subtitle_header,
                                  dec_ctx->subtitle_header_size);
    av_init_packet(&pkt);
    pkt.data = NULL;
    pkt.size = 0;
    while (av_read_frame(fmt, &pkt) >= 0) {
        int i, got_subtitle;
        AVSubtitle sub;

        if (pkt.stream_index == sid) {
            ret = avcodec_decode_subtitle2(dec_ctx, &sub, &got_subtitle, &pkt);
            if (ret < 0 || !got_subtitle)
                break;
            for (i = 0; i < sub.num_rects; i++) {
                char *ass_line = sub.rects[i]->ass;
                if (!ass_line)
                    break;
                ass_process_data(ass->track, ass_line, strlen(ass_line));
            }
        }
        av_free_packet(&pkt);
        avsubtitle_free(&sub);
    }

end:
    if (dec_ctx)
        avcodec_close(dec_ctx);
    if (fmt)
        avformat_close_input(&fmt);
    return ret;
}

AVFilter avfilter_vf_subtitles = {
    .name          = "subtitles",
    .description   = NULL_IF_CONFIG_SMALL("Render subtitles onto input video using the libass library."),
    .priv_size     = sizeof(AssContext),
    .init          = init_subtitles,
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = ass_inputs,
    .outputs       = ass_outputs,
    .priv_class    = &subtitles_class,
};
#endif
