/*
 * Copyright (c) 2007 Nicolas George <nicolas.george@normalesup.org>
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
 * Misc test sources.
 *
 * testsrc is based on the test pattern generator demuxer by Nicolas George:
 * http://lists.ffmpeg.org/pipermail/ffmpeg-devel/2007-October/037845.html
 *
 * rgbtestsrc is ported from MPlayer libmpcodecs/vf_rgbtest.c by
 * Michael Niedermayer.
 */

#include <float.h>

#include "libavutil/opt.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/parseutils.h"
#include "avfilter.h"

typedef struct {
    const AVClass *class;
    int h, w;
    unsigned int nb_frame;
    AVRational time_base;
    int64_t pts, max_pts;
    char *size;                 ///< video frame size
    char *rate;                 ///< video frame rate
    char *duration;             ///< total duration of the generated video
    AVRational sar;             ///< sample aspect ratio
    int nb_decimals;

    void (* fill_picture_fn)(AVFilterContext *ctx, AVFilterBufferRef *picref);

    /* only used by rgbtest */
    int rgba_map[4];
} TestSourceContext;

#define OFFSET(x) offsetof(TestSourceContext, x)

static const AVOption testsrc_options[]= {
    { "size",     "set video size",     OFFSET(size),     AV_OPT_TYPE_STRING, {.str = "320x240"}, 0, 0 },
    { "s",        "set video size",     OFFSET(size),     AV_OPT_TYPE_STRING, {.str = "320x240"}, 0, 0 },
    { "rate",     "set video rate",     OFFSET(rate),     AV_OPT_TYPE_STRING, {.str = "25"},      0, 0 },
    { "r",        "set video rate",     OFFSET(rate),     AV_OPT_TYPE_STRING, {.str = "25"},      0, 0 },
    { "duration", "set video duration", OFFSET(duration), AV_OPT_TYPE_STRING, {.str = NULL},      0, 0 },
    { "d",        "set video duration", OFFSET(duration), AV_OPT_TYPE_STRING, {.str = NULL},      0, 0 },
    { "sar",      "set video sample aspect ratio", OFFSET(sar), AV_OPT_TYPE_RATIONAL, {.dbl= 1},  0, INT_MAX },
    { "decimals", "set number of decimals to show", OFFSET(nb_decimals), AV_OPT_TYPE_INT, {.dbl=0},  INT_MIN, INT_MAX },
    { "n",        "set number of decimals to show", OFFSET(nb_decimals), AV_OPT_TYPE_INT, {.dbl=0},  INT_MIN, INT_MAX },
    { NULL },
};

static av_cold int init(AVFilterContext *ctx, const char *args, void *opaque)
{
    TestSourceContext *test = ctx->priv;
    AVRational frame_rate_q;
    int64_t duration = -1;
    int ret = 0;

    av_opt_set_defaults(test);

    if ((ret = (av_set_options_string(test, args, "=", ":"))) < 0) {
        av_log(ctx, AV_LOG_ERROR, "Error parsing options string: '%s'\n", args);
        return ret;
    }

    if ((ret = av_parse_video_size(&test->w, &test->h, test->size)) < 0) {
        av_log(ctx, AV_LOG_ERROR, "Invalid frame size: '%s'\n", test->size);
        return ret;
    }
    av_freep(&test->size);

    if ((ret = av_parse_video_rate(&frame_rate_q, test->rate)) < 0 ||
        frame_rate_q.den <= 0 || frame_rate_q.num <= 0) {
        av_log(ctx, AV_LOG_ERROR, "Invalid frame rate: '%s'\n", test->rate);
        return ret;
    }
    av_freep(&test->rate);

    if ((test->duration) && (ret = av_parse_time(&duration, test->duration, 1)) < 0) {
        av_log(ctx, AV_LOG_ERROR, "Invalid duration: '%s'\n", test->duration);
        return ret;
    }
    av_freep(&test->duration);

    if (test->nb_decimals && strcmp(ctx->filter->name, "testsrc")) {
        av_log(ctx, AV_LOG_WARNING,
               "Option 'decimals' is ignored with source '%s'\n",
               ctx->filter->name);
    }

    test->time_base.num = frame_rate_q.den;
    test->time_base.den = frame_rate_q.num;
    test->max_pts = duration >= 0 ?
        av_rescale_q(duration, AV_TIME_BASE_Q, test->time_base) : -1;
    test->nb_frame = 0;
    test->pts = 0;

    av_log(ctx, AV_LOG_INFO, "size:%dx%d rate:%d/%d duration:%f sar:%d/%d\n",
           test->w, test->h, frame_rate_q.num, frame_rate_q.den,
           duration < 0 ? -1 : test->max_pts * av_q2d(test->time_base),
           test->sar.num, test->sar.den);
    return 0;
}

static int config_props(AVFilterLink *outlink)
{
    TestSourceContext *test = outlink->src->priv;

    outlink->w = test->w;
    outlink->h = test->h;
    outlink->sample_aspect_ratio = test->sar;
    outlink->time_base = test->time_base;

    return 0;
}

static int request_frame(AVFilterLink *outlink)
{
    TestSourceContext *test = outlink->src->priv;
    AVFilterBufferRef *picref;

    if (test->max_pts >= 0 && test->pts >= test->max_pts)
        return AVERROR_EOF;
    picref = avfilter_get_video_buffer(outlink, AV_PERM_WRITE,
                                       test->w, test->h);
    picref->pts = test->pts++;
    picref->pos = -1;
    picref->video->key_frame = 1;
    picref->video->interlaced = 0;
    picref->video->pict_type = AV_PICTURE_TYPE_I;
    picref->video->sample_aspect_ratio = test->sar;
    test->fill_picture_fn(outlink->src, picref);
    test->nb_frame++;

    avfilter_start_frame(outlink, avfilter_ref_buffer(picref, ~0));
    avfilter_draw_slice(outlink, 0, picref->video->h, 1);
    avfilter_end_frame(outlink);
    avfilter_unref_buffer(picref);

    return 0;
}

#if CONFIG_NULLSRC_FILTER

static const char *nullsrc_get_name(void *ctx)
{
    return "nullsrc";
}

static const AVClass nullsrc_class = {
    .class_name = "NullSourceContext",
    .item_name  = nullsrc_get_name,
    .option     = testsrc_options,
};

static void nullsrc_fill_picture(AVFilterContext *ctx, AVFilterBufferRef *picref) { }

static av_cold int nullsrc_init(AVFilterContext *ctx, const char *args, void *opaque)
{
    TestSourceContext *test = ctx->priv;

    test->class = &nullsrc_class;
    test->fill_picture_fn = nullsrc_fill_picture;
    return init(ctx, args, opaque);
}

AVFilter avfilter_vsrc_nullsrc = {
    .name        = "nullsrc",
    .description = NULL_IF_CONFIG_SMALL("Null video source, return unprocessed video frames."),
    .init       = nullsrc_init,
    .priv_size  = sizeof(TestSourceContext),

    .inputs    = (const AVFilterPad[]) {{ .name = NULL}},
    .outputs   = (const AVFilterPad[]) {{ .name = "default",
                                    .type = AVMEDIA_TYPE_VIDEO,
                                    .request_frame = request_frame,
                                    .config_props  = config_props, },
                                  { .name = NULL}},
};

#endif /* CONFIG_NULLSRC_FILTER */

#if CONFIG_TESTSRC_FILTER

static const char *testsrc_get_name(void *ctx)
{
    return "testsrc";
}

static const AVClass testsrc_class = {
    .class_name = "TestSourceContext",
    .item_name  = testsrc_get_name,
    .option     = testsrc_options,
};

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

static av_cold int test_init(AVFilterContext *ctx, const char *args, void *opaque)
{
    TestSourceContext *test = ctx->priv;

    test->class = &testsrc_class;
    test->fill_picture_fn = test_fill_picture;
    return init(ctx, args, opaque);
}

static int test_query_formats(AVFilterContext *ctx)
{
    static const enum PixelFormat pix_fmts[] = {
        PIX_FMT_RGB24, PIX_FMT_NONE
    };
    avfilter_set_common_pixel_formats(ctx, avfilter_make_format_list(pix_fmts));
    return 0;
}

AVFilter avfilter_vsrc_testsrc = {
    .name      = "testsrc",
    .description = NULL_IF_CONFIG_SMALL("Generate test pattern."),
    .priv_size = sizeof(TestSourceContext),
    .init      = test_init,

    .query_formats   = test_query_formats,

    .inputs    = (const AVFilterPad[]) {{ .name = NULL}},

    .outputs   = (const AVFilterPad[]) {{ .name = "default",
                                    .type = AVMEDIA_TYPE_VIDEO,
                                    .request_frame = request_frame,
                                    .config_props  = config_props, },
                                  { .name = NULL }},
};

#endif /* CONFIG_TESTSRC_FILTER */

#if CONFIG_RGBTESTSRC_FILTER

static const char *rgbtestsrc_get_name(void *ctx)
{
    return "rgbtestsrc";
}

static const AVClass rgbtestsrc_class = {
    .class_name = "RGBTestSourceContext",
    .item_name  = rgbtestsrc_get_name,
    .option     = testsrc_options,
};

#define R 0
#define G 1
#define B 2
#define A 3

static void rgbtest_put_pixel(uint8_t *dst, int dst_linesize,
                              int x, int y, int r, int g, int b, enum PixelFormat fmt,
                              int rgba_map[4])
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

static av_cold int rgbtest_init(AVFilterContext *ctx, const char *args, void *opaque)
{
    TestSourceContext *test = ctx->priv;

    test->class = &rgbtestsrc_class;
    test->fill_picture_fn = rgbtest_fill_picture;
    return init(ctx, args, opaque);
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
    avfilter_set_common_pixel_formats(ctx, avfilter_make_format_list(pix_fmts));
    return 0;
}

static int rgbtest_config_props(AVFilterLink *outlink)
{
    TestSourceContext *test = outlink->src->priv;

    switch (outlink->format) {
    case PIX_FMT_ARGB:  test->rgba_map[A] = 0; test->rgba_map[R] = 1; test->rgba_map[G] = 2; test->rgba_map[B] = 3; break;
    case PIX_FMT_ABGR:  test->rgba_map[A] = 0; test->rgba_map[B] = 1; test->rgba_map[G] = 2; test->rgba_map[R] = 3; break;
    case PIX_FMT_RGBA:
    case PIX_FMT_RGB24: test->rgba_map[R] = 0; test->rgba_map[G] = 1; test->rgba_map[B] = 2; test->rgba_map[A] = 3; break;
    case PIX_FMT_BGRA:
    case PIX_FMT_BGR24: test->rgba_map[B] = 0; test->rgba_map[G] = 1; test->rgba_map[R] = 2; test->rgba_map[A] = 3; break;
    }

    return config_props(outlink);
}

AVFilter avfilter_vsrc_rgbtestsrc = {
    .name      = "rgbtestsrc",
    .description = NULL_IF_CONFIG_SMALL("Generate RGB test pattern."),
    .priv_size = sizeof(TestSourceContext),
    .init      = rgbtest_init,

    .query_formats   = rgbtest_query_formats,

    .inputs    = (const AVFilterPad[]) {{ .name = NULL}},

    .outputs   = (const AVFilterPad[]) {{ .name = "default",
                                    .type = AVMEDIA_TYPE_VIDEO,
                                    .request_frame = request_frame,
                                    .config_props  = rgbtest_config_props, },
                                  { .name = NULL }},
};

#endif /* CONFIG_RGBTESTSRC_FILTER */
