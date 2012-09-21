/*
 * Copyright (c) 2007 Nicolas George <nicolas.george@normalesup.org>
 * Copyright (c) 2011 Stefano Sabatini
 * Copyright (c) 2012 Paul B Mahol
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
 * Misc test sources.
 *
 * testsrc is based on the test pattern generator demuxer by Nicolas George:
 * http://lists.ffmpeg.org/pipermail/ffmpeg-devel/2007-October/037845.html
 *
 * rgbtestsrc is ported from MPlayer libmpcodecs/vf_rgbtest.c by
 * Michael Niedermayer.
 *
 * smptebars is by Paul B Mahol.
 */

#include <float.h>

#include "libavutil/common.h"
#include "libavutil/opt.h"
#include "libavutil/imgutils.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/parseutils.h"
#include "avfilter.h"
#include "drawutils.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

typedef struct {
    const AVClass *class;
    int w, h;
    unsigned int nb_frame;
    AVRational time_base, frame_rate;
    int64_t pts;
    char *frame_rate_str;       ///< video frame rate
    char *duration_str;         ///< total duration of the generated video
    int64_t duration;           ///< duration expressed in microseconds
    AVRational sar;             ///< sample aspect ratio
    int nb_decimals;
    int draw_once;              ///< draw only the first frame, always put out the same picture
    AVFilterBufferRef *picref;  ///< cached reference containing the painted picture

    void (* fill_picture_fn)(AVFilterContext *ctx, AVFilterBufferRef *picref);

    /* only used by color */
    char *color_str;
    FFDrawContext draw;
    FFDrawColor color;
    uint8_t color_rgba[4];

    /* only used by rgbtest */
    uint8_t rgba_map[4];
} TestSourceContext;

#define OFFSET(x) offsetof(TestSourceContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption options[] = {
    { "size",     "set video size",     OFFSET(w),        AV_OPT_TYPE_IMAGE_SIZE, {.str = "320x240"}, 0, 0, FLAGS },
    { "s",        "set video size",     OFFSET(w),        AV_OPT_TYPE_IMAGE_SIZE, {.str = "320x240"}, 0, 0, FLAGS },
    { "rate",     "set video rate",     OFFSET(frame_rate_str), AV_OPT_TYPE_STRING, {.str = "25"}, 0, 0, FLAGS },
    { "r",        "set video rate",     OFFSET(frame_rate_str), AV_OPT_TYPE_STRING, {.str = "25"}, 0, 0, FLAGS },
    { "duration", "set video duration", OFFSET(duration_str), AV_OPT_TYPE_STRING, {.str = NULL},   0, 0, FLAGS },
    { "d",        "set video duration", OFFSET(duration_str), AV_OPT_TYPE_STRING, {.str = NULL},   0, 0, FLAGS },
    { "sar",      "set video sample aspect ratio", OFFSET(sar), AV_OPT_TYPE_RATIONAL, {.dbl= 1},  0, INT_MAX, FLAGS },

    /* only used by color */
    { "color", "set color", OFFSET(color_str), AV_OPT_TYPE_STRING, {.str = NULL}, CHAR_MIN, CHAR_MAX, FLAGS },
    { "c",     "set color", OFFSET(color_str), AV_OPT_TYPE_STRING, {.str = NULL}, CHAR_MIN, CHAR_MAX, FLAGS },

    /* only used by testsrc */
    { "decimals", "set number of decimals to show", OFFSET(nb_decimals), AV_OPT_TYPE_INT, {.i64=0},  INT_MIN, INT_MAX, FLAGS },
    { "n",        "set number of decimals to show", OFFSET(nb_decimals), AV_OPT_TYPE_INT, {.i64=0},  INT_MIN, INT_MAX, FLAGS },
    { NULL },
};

static av_cold int init(AVFilterContext *ctx, const char *args)
{
    TestSourceContext *test = ctx->priv;
    int ret = 0;

    av_opt_set_defaults(test);

    if ((ret = (av_set_options_string(test, args, "=", ":"))) < 0)
        return ret;

    if ((ret = av_parse_video_rate(&test->frame_rate, test->frame_rate_str)) < 0) {
        av_log(ctx, AV_LOG_ERROR, "Invalid frame rate: '%s'\n", test->frame_rate_str);
        return ret;
    }

    test->duration = -1;
    if (test->duration_str &&
        (ret = av_parse_time(&test->duration, test->duration_str, 1)) < 0) {
        av_log(ctx, AV_LOG_ERROR, "Invalid duration: '%s'\n", test->duration_str);
        return ret;
    }

    if (test->nb_decimals && strcmp(ctx->filter->name, "testsrc")) {
        av_log(ctx, AV_LOG_WARNING,
               "Option 'decimals' is ignored with source '%s'\n",
               ctx->filter->name);
    }

    if (test->color_str) {
        if (!strcmp(ctx->filter->name, "color")) {
            ret = av_parse_color(test->color_rgba, test->color_str, -1, ctx);
            if (ret < 0)
                return ret;
        } else {
            av_log(ctx, AV_LOG_WARNING,
                   "Option 'color' is ignored with source '%s'\n",
                   ctx->filter->name);
        }
    }

    test->time_base = av_inv_q(test->frame_rate);
    test->nb_frame = 0;
    test->pts = 0;

    av_log(ctx, AV_LOG_VERBOSE, "size:%dx%d rate:%d/%d duration:%f sar:%d/%d\n",
           test->w, test->h, test->frame_rate.num, test->frame_rate.den,
           test->duration < 0 ? -1 : (double)test->duration/1000000,
           test->sar.num, test->sar.den);
    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    TestSourceContext *test = ctx->priv;

    av_opt_free(test);
    avfilter_unref_bufferp(&test->picref);
}

static int config_props(AVFilterLink *outlink)
{
    TestSourceContext *test = outlink->src->priv;

    outlink->w = test->w;
    outlink->h = test->h;
    outlink->sample_aspect_ratio = test->sar;
    outlink->frame_rate = test->frame_rate;
    outlink->time_base  = test->time_base;

    return 0;
}

static int request_frame(AVFilterLink *outlink)
{
    TestSourceContext *test = outlink->src->priv;
    AVFilterBufferRef *outpicref;
    int ret = 0;

    if (test->duration >= 0 &&
        av_rescale_q(test->pts, test->time_base, AV_TIME_BASE_Q) >= test->duration)
        return AVERROR_EOF;

    if (test->draw_once) {
        if (!test->picref) {
            test->picref =
                ff_get_video_buffer(outlink, AV_PERM_WRITE|AV_PERM_PRESERVE|AV_PERM_REUSE,
                                    test->w, test->h);
            if (!test->picref)
                return AVERROR(ENOMEM);
            test->fill_picture_fn(outlink->src, test->picref);
        }
        outpicref = avfilter_ref_buffer(test->picref, ~AV_PERM_WRITE);
    } else
        outpicref = ff_get_video_buffer(outlink, AV_PERM_WRITE, test->w, test->h);

    if (!outpicref)
        return AVERROR(ENOMEM);
    outpicref->pts = test->pts;
    outpicref->pos = -1;
    outpicref->video->key_frame = 1;
    outpicref->video->interlaced = 0;
    outpicref->video->pict_type = AV_PICTURE_TYPE_I;
    outpicref->video->sample_aspect_ratio = test->sar;
    if (!test->draw_once)
        test->fill_picture_fn(outlink->src, outpicref);

    test->pts++;
    test->nb_frame++;

    if ((ret = ff_start_frame(outlink, outpicref)) < 0 ||
        (ret = ff_draw_slice(outlink, 0, test->h, 1)) < 0 ||
        (ret = ff_end_frame(outlink)) < 0)
        return ret;

    return 0;
}

#if CONFIG_COLOR_FILTER

#define color_options options
AVFILTER_DEFINE_CLASS(color);

static void color_fill_picture(AVFilterContext *ctx, AVFilterBufferRef *picref)
{
    TestSourceContext *test = ctx->priv;
    ff_fill_rectangle(&test->draw, &test->color,
                      picref->data, picref->linesize,
                      0, 0, test->w, test->h);
}

static av_cold int color_init(AVFilterContext *ctx, const char *args)
{
    TestSourceContext *test = ctx->priv;
    test->class = &color_class;
    test->fill_picture_fn = color_fill_picture;
    test->draw_once = 1;
    av_opt_set(test, "color", "black", 0);
    return init(ctx, args);
}

static int color_query_formats(AVFilterContext *ctx)
{
    ff_set_common_formats(ctx, ff_draw_supported_pixel_formats(0));
    return 0;
}

static int color_config_props(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->src;
    TestSourceContext *test = ctx->priv;
    int ret;

    ff_draw_init(&test->draw, inlink->format, 0);
    ff_draw_color(&test->draw, &test->color, test->color_rgba);

    test->w = ff_draw_round_to_sub(&test->draw, 0, -1, test->w);
    test->h = ff_draw_round_to_sub(&test->draw, 1, -1, test->h);
    if (av_image_check_size(test->w, test->h, 0, ctx) < 0)
        return AVERROR(EINVAL);

    if (ret = config_props(inlink) < 0)
        return ret;

    av_log(ctx, AV_LOG_VERBOSE, "color:0x%02x%02x%02x%02x\n",
           test->color_rgba[0], test->color_rgba[1], test->color_rgba[2], test->color_rgba[3]);
    return 0;
}

AVFilter avfilter_vsrc_color = {
    .name        = "color",
    .description = NULL_IF_CONFIG_SMALL("Provide an uniformly colored input."),

    .priv_size = sizeof(TestSourceContext),
    .init      = color_init,
    .uninit    = uninit,

    .query_formats = color_query_formats,

    .inputs = (const AVFilterPad[]) {
        { .name = NULL }
    },

    .outputs = (const AVFilterPad[]) {
        {
            .name            = "default",
            .type            = AVMEDIA_TYPE_VIDEO,
            .request_frame   = request_frame,
            .config_props    = color_config_props,
        },
        { .name = NULL }
    },

    .priv_class = &color_class,
};

#endif /* CONFIG_COLOR_FILTER */

#if CONFIG_NULLSRC_FILTER

#define nullsrc_options options
AVFILTER_DEFINE_CLASS(nullsrc);

static void nullsrc_fill_picture(AVFilterContext *ctx, AVFilterBufferRef *picref) { }

static av_cold int nullsrc_init(AVFilterContext *ctx, const char *args)
{
    TestSourceContext *test = ctx->priv;

    test->class = &nullsrc_class;
    test->fill_picture_fn = nullsrc_fill_picture;
    return init(ctx, args);
}

AVFilter avfilter_vsrc_nullsrc = {
    .name        = "nullsrc",
    .description = NULL_IF_CONFIG_SMALL("Null video source, return unprocessed video frames."),
    .init       = nullsrc_init,
    .uninit     = uninit,
    .priv_size  = sizeof(TestSourceContext),

    .inputs    = (const AVFilterPad[]) {{ .name = NULL}},
    .outputs   = (const AVFilterPad[]) {{ .name = "default",
                                    .type = AVMEDIA_TYPE_VIDEO,
                                    .request_frame = request_frame,
                                    .config_props  = config_props, },
                                  { .name = NULL}},
    .priv_class = &nullsrc_class,
};

#endif /* CONFIG_NULLSRC_FILTER */

#if CONFIG_TESTSRC_FILTER

#define testsrc_options options
AVFILTER_DEFINE_CLASS(testsrc);

/**
 * Fill a rectangle with value val.
 *
 * @param val the RGB value to set
 * @param dst pointer to the destination buffer to fill
 * @param dst_linesize linesize of destination
 * @param segment_width width of the segment
 * @param x horizontal coordinate where to draw the rectangle in the destination buffer
 * @param y horizontal coordinate where to draw the rectangle in the destination buffer
 * @param w width  of the rectangle to draw, expressed as a number of segment_width units
 * @param h height of the rectangle to draw, expressed as a number of segment_width units
 */
static void draw_rectangle(unsigned val, uint8_t *dst, int dst_linesize, unsigned segment_width,
                           unsigned x, unsigned y, unsigned w, unsigned h)
{
    int i;
    int step = 3;

    dst += segment_width * (step * x + y * dst_linesize);
    w *= segment_width * step;
    h *= segment_width;
    for (i = 0; i < h; i++) {
        memset(dst, val, w);
        dst += dst_linesize;
    }
}

static void draw_digit(int digit, uint8_t *dst, unsigned dst_linesize,
                       unsigned segment_width)
{
#define TOP_HBAR        1
#define MID_HBAR        2
#define BOT_HBAR        4
#define LEFT_TOP_VBAR   8
#define LEFT_BOT_VBAR  16
#define RIGHT_TOP_VBAR 32
#define RIGHT_BOT_VBAR 64
    struct {
        int x, y, w, h;
    } segments[] = {
        { 1,  0, 5, 1 }, /* TOP_HBAR */
        { 1,  6, 5, 1 }, /* MID_HBAR */
        { 1, 12, 5, 1 }, /* BOT_HBAR */
        { 0,  1, 1, 5 }, /* LEFT_TOP_VBAR */
        { 0,  7, 1, 5 }, /* LEFT_BOT_VBAR */
        { 6,  1, 1, 5 }, /* RIGHT_TOP_VBAR */
        { 6,  7, 1, 5 }  /* RIGHT_BOT_VBAR */
    };
    static const unsigned char masks[10] = {
        /* 0 */ TOP_HBAR         |BOT_HBAR|LEFT_TOP_VBAR|LEFT_BOT_VBAR|RIGHT_TOP_VBAR|RIGHT_BOT_VBAR,
        /* 1 */                                                        RIGHT_TOP_VBAR|RIGHT_BOT_VBAR,
        /* 2 */ TOP_HBAR|MID_HBAR|BOT_HBAR|LEFT_BOT_VBAR                             |RIGHT_TOP_VBAR,
        /* 3 */ TOP_HBAR|MID_HBAR|BOT_HBAR                            |RIGHT_TOP_VBAR|RIGHT_BOT_VBAR,
        /* 4 */          MID_HBAR         |LEFT_TOP_VBAR              |RIGHT_TOP_VBAR|RIGHT_BOT_VBAR,
        /* 5 */ TOP_HBAR|BOT_HBAR|MID_HBAR|LEFT_TOP_VBAR                             |RIGHT_BOT_VBAR,
        /* 6 */ TOP_HBAR|BOT_HBAR|MID_HBAR|LEFT_TOP_VBAR|LEFT_BOT_VBAR               |RIGHT_BOT_VBAR,
        /* 7 */ TOP_HBAR                                              |RIGHT_TOP_VBAR|RIGHT_BOT_VBAR,
        /* 8 */ TOP_HBAR|BOT_HBAR|MID_HBAR|LEFT_TOP_VBAR|LEFT_BOT_VBAR|RIGHT_TOP_VBAR|RIGHT_BOT_VBAR,
        /* 9 */ TOP_HBAR|BOT_HBAR|MID_HBAR|LEFT_TOP_VBAR              |RIGHT_TOP_VBAR|RIGHT_BOT_VBAR,
    };
    unsigned mask = masks[digit];
    int i;

    draw_rectangle(0, dst, dst_linesize, segment_width, 0, 0, 8, 13);
    for (i = 0; i < FF_ARRAY_ELEMS(segments); i++)
        if (mask & (1<<i))
            draw_rectangle(255, dst, dst_linesize, segment_width,
                           segments[i].x, segments[i].y, segments[i].w, segments[i].h);
}

#define GRADIENT_SIZE (6 * 256)

static void test_fill_picture(AVFilterContext *ctx, AVFilterBufferRef *picref)
{
    TestSourceContext *test = ctx->priv;
    uint8_t *p, *p0;
    int x, y;
    int color, color_rest;
    int icolor;
    int radius;
    int quad0, quad;
    int dquad_x, dquad_y;
    int grad, dgrad, rgrad, drgrad;
    int seg_size;
    int second;
    int i;
    uint8_t *data = picref->data[0];
    int width  = picref->video->w;
    int height = picref->video->h;

    /* draw colored bars and circle */
    radius = (width + height) / 4;
    quad0 = width * width / 4 + height * height / 4 - radius * radius;
    dquad_y = 1 - height;
    p0 = data;
    for (y = 0; y < height; y++) {
        p = p0;
        color = 0;
        color_rest = 0;
        quad = quad0;
        dquad_x = 1 - width;
        for (x = 0; x < width; x++) {
            icolor = color;
            if (quad < 0)
                icolor ^= 7;
            quad += dquad_x;
            dquad_x += 2;
            *(p++) = icolor & 1 ? 255 : 0;
            *(p++) = icolor & 2 ? 255 : 0;
            *(p++) = icolor & 4 ? 255 : 0;
            color_rest += 8;
            if (color_rest >= width) {
                color_rest -= width;
                color++;
            }
        }
        quad0 += dquad_y;
        dquad_y += 2;
        p0 += picref->linesize[0];
    }

    /* draw sliding color line */
    p0 = p = data + picref->linesize[0] * height * 3/4;
    grad = (256 * test->nb_frame * test->time_base.num / test->time_base.den) %
        GRADIENT_SIZE;
    rgrad = 0;
    dgrad = GRADIENT_SIZE / width;
    drgrad = GRADIENT_SIZE % width;
    for (x = 0; x < width; x++) {
        *(p++) =
            grad < 256 || grad >= 5 * 256 ? 255 :
            grad >= 2 * 256 && grad < 4 * 256 ? 0 :
            grad < 2 * 256 ? 2 * 256 - 1 - grad : grad - 4 * 256;
        *(p++) =
            grad >= 4 * 256 ? 0 :
            grad >= 1 * 256 && grad < 3 * 256 ? 255 :
            grad < 1 * 256 ? grad : 4 * 256 - 1 - grad;
        *(p++) =
            grad < 2 * 256 ? 0 :
            grad >= 3 * 256 && grad < 5 * 256 ? 255 :
            grad < 3 * 256 ? grad - 2 * 256 : 6 * 256 - 1 - grad;
        grad += dgrad;
        rgrad += drgrad;
        if (rgrad >= GRADIENT_SIZE) {
            grad++;
            rgrad -= GRADIENT_SIZE;
        }
        if (grad >= GRADIENT_SIZE)
            grad -= GRADIENT_SIZE;
    }
    p = p0;
    for (y = height / 8; y > 0; y--) {
        memcpy(p+picref->linesize[0], p, 3 * width);
        p += picref->linesize[0];
    }

    /* draw digits */
    seg_size = width / 80;
    if (seg_size >= 1 && height >= 13 * seg_size) {
        double time = av_q2d(test->time_base) * test->nb_frame *
                      pow(10, test->nb_decimals);
        if (time > INT_MAX)
            return;
        second = (int)time;
        x = width - (width - seg_size * 64) / 2;
        y = (height - seg_size * 13) / 2;
        p = data + (x*3 + y * picref->linesize[0]);
        for (i = 0; i < 8; i++) {
            p -= 3 * 8 * seg_size;
            draw_digit(second % 10, p, picref->linesize[0], seg_size);
            second /= 10;
            if (second == 0)
                break;
        }
    }
}

static av_cold int test_init(AVFilterContext *ctx, const char *args)
{
    TestSourceContext *test = ctx->priv;

    test->class = &testsrc_class;
    test->fill_picture_fn = test_fill_picture;
    return init(ctx, args);
}

static int test_query_formats(AVFilterContext *ctx)
{
    static const enum PixelFormat pix_fmts[] = {
        PIX_FMT_RGB24, PIX_FMT_NONE
    };
    ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));
    return 0;
}

AVFilter avfilter_vsrc_testsrc = {
    .name      = "testsrc",
    .description = NULL_IF_CONFIG_SMALL("Generate test pattern."),
    .priv_size = sizeof(TestSourceContext),
    .init      = test_init,
    .uninit    = uninit,

    .query_formats   = test_query_formats,

    .inputs    = NULL,

    .outputs   = (const AVFilterPad[]) {{ .name = "default",
                                          .type = AVMEDIA_TYPE_VIDEO,
                                          .request_frame = request_frame,
                                          .config_props  = config_props, },
                                        { .name = NULL }},
    .priv_class = &testsrc_class,
};

#endif /* CONFIG_TESTSRC_FILTER */

#if CONFIG_RGBTESTSRC_FILTER

#define rgbtestsrc_options options
AVFILTER_DEFINE_CLASS(rgbtestsrc);

#define R 0
#define G 1
#define B 2
#define A 3

static void rgbtest_put_pixel(uint8_t *dst, int dst_linesize,
                              int x, int y, int r, int g, int b, enum PixelFormat fmt,
                              uint8_t rgba_map[4])
{
    int32_t v;
    uint8_t *p;

    switch (fmt) {
    case PIX_FMT_BGR444: ((uint16_t*)(dst + y*dst_linesize))[x] = ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4); break;
    case PIX_FMT_RGB444: ((uint16_t*)(dst + y*dst_linesize))[x] = ((b >> 4) << 8) | ((g >> 4) << 4) | (r >> 4); break;
    case PIX_FMT_BGR555: ((uint16_t*)(dst + y*dst_linesize))[x] = ((r>>3)<<10) | ((g>>3)<<5) | (b>>3); break;
    case PIX_FMT_RGB555: ((uint16_t*)(dst + y*dst_linesize))[x] = ((b>>3)<<10) | ((g>>3)<<5) | (r>>3); break;
    case PIX_FMT_BGR565: ((uint16_t*)(dst + y*dst_linesize))[x] = ((r>>3)<<11) | ((g>>2)<<5) | (b>>3); break;
    case PIX_FMT_RGB565: ((uint16_t*)(dst + y*dst_linesize))[x] = ((b>>3)<<11) | ((g>>2)<<5) | (r>>3); break;
    case PIX_FMT_RGB24:
    case PIX_FMT_BGR24:
        v = (r << (rgba_map[R]*8)) + (g << (rgba_map[G]*8)) + (b << (rgba_map[B]*8));
        p = dst + 3*x + y*dst_linesize;
        AV_WL24(p, v);
        break;
    case PIX_FMT_RGBA:
    case PIX_FMT_BGRA:
    case PIX_FMT_ARGB:
    case PIX_FMT_ABGR:
        v = (r << (rgba_map[R]*8)) + (g << (rgba_map[G]*8)) + (b << (rgba_map[B]*8)) + (255 << (rgba_map[A]*8));
        p = dst + 4*x + y*dst_linesize;
        AV_WL32(p, v);
        break;
    }
}

static void rgbtest_fill_picture(AVFilterContext *ctx, AVFilterBufferRef *picref)
{
    TestSourceContext *test = ctx->priv;
    int x, y, w = picref->video->w, h = picref->video->h;

    for (y = 0; y < h; y++) {
         for (x = 0; x < picref->video->w; x++) {
             int c = 256*x/w;
             int r = 0, g = 0, b = 0;

             if      (3*y < h  ) r = c;
             else if (3*y < 2*h) g = c;
             else                b = c;

             rgbtest_put_pixel(picref->data[0], picref->linesize[0], x, y, r, g, b,
                               ctx->outputs[0]->format, test->rgba_map);
         }
     }
}

static av_cold int rgbtest_init(AVFilterContext *ctx, const char *args)
{
    TestSourceContext *test = ctx->priv;

    test->draw_once = 1;
    test->class = &rgbtestsrc_class;
    test->fill_picture_fn = rgbtest_fill_picture;
    return init(ctx, args);
}

static int rgbtest_query_formats(AVFilterContext *ctx)
{
    static const enum PixelFormat pix_fmts[] = {
        PIX_FMT_RGBA, PIX_FMT_ARGB, PIX_FMT_BGRA, PIX_FMT_ABGR,
        PIX_FMT_BGR24, PIX_FMT_RGB24,
        PIX_FMT_RGB444, PIX_FMT_BGR444,
        PIX_FMT_RGB565, PIX_FMT_BGR565,
        PIX_FMT_RGB555, PIX_FMT_BGR555,
        PIX_FMT_NONE
    };
    ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));
    return 0;
}

static int rgbtest_config_props(AVFilterLink *outlink)
{
    TestSourceContext *test = outlink->src->priv;

    ff_fill_rgba_map(test->rgba_map, outlink->format);
    return config_props(outlink);
}

AVFilter avfilter_vsrc_rgbtestsrc = {
    .name      = "rgbtestsrc",
    .description = NULL_IF_CONFIG_SMALL("Generate RGB test pattern."),
    .priv_size = sizeof(TestSourceContext),
    .init      = rgbtest_init,
    .uninit    = uninit,

    .query_formats   = rgbtest_query_formats,

    .inputs    = NULL,

    .outputs   = (const AVFilterPad[]) {{ .name = "default",
                                          .type = AVMEDIA_TYPE_VIDEO,
                                          .request_frame = request_frame,
                                          .config_props  = rgbtest_config_props, },
                                        { .name = NULL }},
    .priv_class = &rgbtestsrc_class,
};

#endif /* CONFIG_RGBTESTSRC_FILTER */

#if CONFIG_SMPTEBARS_FILTER

#define smptebars_options options
AVFILTER_DEFINE_CLASS(smptebars);

static const uint8_t rainbow[7][4] = {
    { 191, 191, 191, 255 },     /* gray */
    { 191, 191,   0, 255 },     /* yellow */
    {   0, 191, 191, 255 },     /* cyan */
    {   0, 191,   0, 255 },     /* green */
    { 191,   0, 191, 255 },     /* magenta */
    { 191,   0,   0, 255 },     /* red */
    {   0,   0, 191, 255 },     /* blue */
};

static const uint8_t wobnair[7][4] = {
    {   0,   0, 191, 255 },     /* blue */
    {  19,  19,  19, 255 },     /* 7.5% intensity black */
    { 191,   0, 191, 255 },     /* magenta */
    {  19,  19,  19, 255 },     /* 7.5% intensity black */
    {   0, 191, 191, 255 },     /* cyan */
    {  19,  19,  19, 255 },     /* 7.5% intensity black */
    { 191, 191, 191, 255 },     /* gray */
};

static const uint8_t white[4] = { 255, 255, 255, 255 };
static const uint8_t black[4] = {  19,  19,  19, 255 }; /* 7.5% intensity black */

/* pluge pulses */
static const uint8_t neg4ire[4] = {   9,   9,   9, 255 }; /*  3.5% intensity black */
static const uint8_t pos4ire[4] = {  29,  29,  29, 255 }; /* 11.5% intensity black */

/* fudged Q/-I */
static const uint8_t i_pixel[4] = {   0,  68, 130, 255 };
static const uint8_t q_pixel[4] = {  67,   0, 130, 255 };

static void smptebars_fill_picture(AVFilterContext *ctx, AVFilterBufferRef *picref)
{
    TestSourceContext *test = ctx->priv;
    FFDrawColor color;
    int r_w, r_h, w_h, p_w, p_h, i, x = 0;

    r_w = (test->w + 6) / 7;
    r_h = test->h * 2 / 3;
    w_h = test->h * 3 / 4 - r_h;
    p_w = r_w * 5 / 4;
    p_h = test->h - w_h - r_h;

#define DRAW_COLOR(rgba, x, y, w, h)                                    \
    ff_draw_color(&test->draw, &color, rgba);                           \
    ff_fill_rectangle(&test->draw, &color,                              \
                      picref->data, picref->linesize, x, y, w, h)       \

    for (i = 0; i < 7; i++) {
        DRAW_COLOR(rainbow[i], x, 0,   FFMIN(r_w, test->w - x), r_h);
        DRAW_COLOR(wobnair[i], x, r_h, FFMIN(r_w, test->w - x), w_h);
        x += r_w;
    }
    x = 0;
    DRAW_COLOR(i_pixel, x, r_h + w_h, p_w, p_h);
    x += p_w;
    DRAW_COLOR(white, x, r_h + w_h, p_w, p_h);
    x += p_w;
    DRAW_COLOR(q_pixel, x, r_h + w_h, p_w, p_h);
    x += p_w;
    DRAW_COLOR(black, x, r_h + w_h, 5 * r_w - x, p_h);
    x += 5 * r_w - x;
    DRAW_COLOR(neg4ire, x, r_h + w_h, r_w / 3, p_h);
    x += r_w / 3;
    DRAW_COLOR(black, x, r_h + w_h, r_w / 3, p_h);
    x += r_w / 3;
    DRAW_COLOR(pos4ire, x, r_h + w_h, r_w / 3, p_h);
    x += r_w / 3;
    DRAW_COLOR(black, x, r_h + w_h, test->w - x, p_h);
}

static av_cold int smptebars_init(AVFilterContext *ctx, const char *args)
{
    TestSourceContext *test = ctx->priv;

    test->class = &smptebars_class;
    test->fill_picture_fn = smptebars_fill_picture;
    test->draw_once = 1;
    return init(ctx, args);
}

static int smptebars_query_formats(AVFilterContext *ctx)
{
    ff_set_common_formats(ctx, ff_draw_supported_pixel_formats(0));
    return 0;
}

static int smptebars_config_props(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    TestSourceContext *test = ctx->priv;

    ff_draw_init(&test->draw, outlink->format, 0);

    return config_props(outlink);
}

AVFilter avfilter_vsrc_smptebars = {
    .name      = "smptebars",
    .description = NULL_IF_CONFIG_SMALL("Generate SMPTE color bars."),
    .priv_size = sizeof(TestSourceContext),
    .init      = smptebars_init,
    .uninit    = uninit,

    .query_formats = smptebars_query_formats,

    .inputs = (const AVFilterPad[]) {
        { .name = NULL }
    },

    .outputs = (const AVFilterPad[]) {
        {
            .name = "default",
            .type = AVMEDIA_TYPE_VIDEO,
            .request_frame = request_frame,
            .config_props  = smptebars_config_props,
        },
        { .name = NULL }
    },

    .priv_class = &smptebars_class,
};

#endif  /* CONFIG_SMPTEBARS_FILTER */
