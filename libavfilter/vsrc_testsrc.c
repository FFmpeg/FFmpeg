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
 * smptebars and smptehdbars are by Paul B Mahol.
 */

#include <float.h>

#include "libavutil/avassert.h"
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
    int64_t duration;           ///< duration expressed in microseconds
    AVRational sar;             ///< sample aspect ratio
    int draw_once;              ///< draw only the first frame, always put out the same picture
    int draw_once_reset;        ///< draw only the first frame or in case of reset
    AVFrame *picref;            ///< cached reference containing the painted picture

    void (* fill_picture_fn)(AVFilterContext *ctx, AVFrame *frame);

    /* only used by testsrc */
    int nb_decimals;

    /* only used by color */
    FFDrawContext draw;
    FFDrawColor color;
    uint8_t color_rgba[4];

    /* only used by rgbtest */
    uint8_t rgba_map[4];

    /* only used by haldclut */
    int level;
} TestSourceContext;

#define OFFSET(x) offsetof(TestSourceContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

#define SIZE_OPTIONS \
    { "size",     "set video size",     OFFSET(w),        AV_OPT_TYPE_IMAGE_SIZE, {.str = "320x240"}, 0, 0, FLAGS },\
    { "s",        "set video size",     OFFSET(w),        AV_OPT_TYPE_IMAGE_SIZE, {.str = "320x240"}, 0, 0, FLAGS },\

#define COMMON_OPTIONS_NOSIZE \
    { "rate",     "set video rate",     OFFSET(frame_rate), AV_OPT_TYPE_VIDEO_RATE, {.str = "25"}, 0, 0, FLAGS },\
    { "r",        "set video rate",     OFFSET(frame_rate), AV_OPT_TYPE_VIDEO_RATE, {.str = "25"}, 0, 0, FLAGS },\
    { "duration", "set video duration", OFFSET(duration), AV_OPT_TYPE_DURATION, {.i64 = -1}, -1, INT64_MAX, FLAGS },\
    { "d",        "set video duration", OFFSET(duration), AV_OPT_TYPE_DURATION, {.i64 = -1}, -1, INT64_MAX, FLAGS },\
    { "sar",      "set video sample aspect ratio", OFFSET(sar), AV_OPT_TYPE_RATIONAL, {.dbl= 1},  0, INT_MAX, FLAGS },

#define COMMON_OPTIONS SIZE_OPTIONS COMMON_OPTIONS_NOSIZE

static const AVOption options[] = {
    COMMON_OPTIONS
    { NULL }
};

static av_cold int init(AVFilterContext *ctx)
{
    TestSourceContext *test = ctx->priv;

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

    av_frame_free(&test->picref);
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
    AVFrame *frame;

    if (test->duration >= 0 &&
        av_rescale_q(test->pts, test->time_base, AV_TIME_BASE_Q) >= test->duration)
        return AVERROR_EOF;

    if (test->draw_once) {
        if (test->draw_once_reset) {
            av_frame_free(&test->picref);
            test->draw_once_reset = 0;
        }
        if (!test->picref) {
            test->picref =
                ff_get_video_buffer(outlink, test->w, test->h);
            if (!test->picref)
                return AVERROR(ENOMEM);
            test->fill_picture_fn(outlink->src, test->picref);
        }
        frame = av_frame_clone(test->picref);
    } else
        frame = ff_get_video_buffer(outlink, test->w, test->h);

    if (!frame)
        return AVERROR(ENOMEM);
    frame->pts                 = test->pts;
    frame->key_frame           = 1;
    frame->interlaced_frame    = 0;
    frame->pict_type           = AV_PICTURE_TYPE_I;
    frame->sample_aspect_ratio = test->sar;
    if (!test->draw_once)
        test->fill_picture_fn(outlink->src, frame);

    test->pts++;
    test->nb_frame++;

    return ff_filter_frame(outlink, frame);
}

#if CONFIG_COLOR_FILTER

static const AVOption color_options[] = {
    { "color", "set color", OFFSET(color_rgba), AV_OPT_TYPE_COLOR, {.str = "black"}, CHAR_MIN, CHAR_MAX, FLAGS },
    { "c",     "set color", OFFSET(color_rgba), AV_OPT_TYPE_COLOR, {.str = "black"}, CHAR_MIN, CHAR_MAX, FLAGS },
    COMMON_OPTIONS
    { NULL }
};

AVFILTER_DEFINE_CLASS(color);

static void color_fill_picture(AVFilterContext *ctx, AVFrame *picref)
{
    TestSourceContext *test = ctx->priv;
    ff_fill_rectangle(&test->draw, &test->color,
                      picref->data, picref->linesize,
                      0, 0, test->w, test->h);
}

static av_cold int color_init(AVFilterContext *ctx)
{
    TestSourceContext *test = ctx->priv;
    test->fill_picture_fn = color_fill_picture;
    test->draw_once = 1;
    return init(ctx);
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

    if ((ret = config_props(inlink)) < 0)
        return ret;

    return 0;
}

static int color_process_command(AVFilterContext *ctx, const char *cmd, const char *args,
                                 char *res, int res_len, int flags)
{
    TestSourceContext *test = ctx->priv;
    int ret;

    if (!strcmp(cmd, "color") || !strcmp(cmd, "c")) {
        uint8_t color_rgba[4];

        ret = av_parse_color(color_rgba, args, -1, ctx);
        if (ret < 0)
            return ret;

        memcpy(test->color_rgba, color_rgba, sizeof(color_rgba));
        ff_draw_color(&test->draw, &test->color, test->color_rgba);
        test->draw_once_reset = 1;
        return 0;
    }

    return AVERROR(ENOSYS);
}

static const AVFilterPad color_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .request_frame = request_frame,
        .config_props  = color_config_props,
    },
    {  NULL }
};

AVFilter avfilter_vsrc_color = {
    .name        = "color",
    .description = NULL_IF_CONFIG_SMALL("Provide an uniformly colored input."),

    .priv_class = &color_class,
    .priv_size = sizeof(TestSourceContext),
    .init      = color_init,
    .uninit    = uninit,

    .query_formats = color_query_formats,
    .inputs        = NULL,
    .outputs       = color_outputs,
    .process_command = color_process_command,
};

#endif /* CONFIG_COLOR_FILTER */

#if CONFIG_HALDCLUTSRC_FILTER

static const AVOption haldclutsrc_options[] = {
    { "level", "set level", OFFSET(level), AV_OPT_TYPE_INT, {.i64 = 6}, 2, 8, FLAGS },
    COMMON_OPTIONS_NOSIZE
    { NULL }
};

AVFILTER_DEFINE_CLASS(haldclutsrc);

static void haldclutsrc_fill_picture(AVFilterContext *ctx, AVFrame *frame)
{
    int i, j, k, x = 0, y = 0, is16bit = 0, step;
    uint32_t alpha = 0;
    const TestSourceContext *hc = ctx->priv;
    int level = hc->level;
    float scale;
    const int w = frame->width;
    const int h = frame->height;
    const uint8_t *data = frame->data[0];
    const int linesize  = frame->linesize[0];
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(frame->format);
    uint8_t rgba_map[4];

    av_assert0(w == h && w == level*level*level);

    ff_fill_rgba_map(rgba_map, frame->format);

    switch (frame->format) {
    case AV_PIX_FMT_RGB48:
    case AV_PIX_FMT_BGR48:
    case AV_PIX_FMT_RGBA64:
    case AV_PIX_FMT_BGRA64:
        is16bit = 1;
        alpha = 0xffff;
        break;
    case AV_PIX_FMT_RGBA:
    case AV_PIX_FMT_BGRA:
    case AV_PIX_FMT_ARGB:
    case AV_PIX_FMT_ABGR:
        alpha = 0xff;
        break;
    }

    step  = av_get_padded_bits_per_pixel(desc) >> (3 + is16bit);
    scale = ((float)(1 << (8*(is16bit+1))) - 1) / (level*level - 1);

#define LOAD_CLUT(nbits) do {                                                   \
    uint##nbits##_t *dst = ((uint##nbits##_t *)(data + y*linesize)) + x*step;   \
    dst[rgba_map[0]] = av_clip_uint##nbits(i * scale);                          \
    dst[rgba_map[1]] = av_clip_uint##nbits(j * scale);                          \
    dst[rgba_map[2]] = av_clip_uint##nbits(k * scale);                          \
    if (step == 4)                                                              \
        dst[rgba_map[3]] = alpha;                                               \
} while (0)

    level *= level;
    for (k = 0; k < level; k++) {
        for (j = 0; j < level; j++) {
            for (i = 0; i < level; i++) {
                if (!is16bit)
                    LOAD_CLUT(8);
                else
                    LOAD_CLUT(16);
                if (++x == w) {
                    x = 0;
                    y++;
                }
            }
        }
    }
}

static av_cold int haldclutsrc_init(AVFilterContext *ctx)
{
    TestSourceContext *hc = ctx->priv;
    hc->fill_picture_fn = haldclutsrc_fill_picture;
    hc->draw_once = 1;
    return init(ctx);
}

static int haldclutsrc_query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_RGB24,  AV_PIX_FMT_BGR24,
        AV_PIX_FMT_RGBA,   AV_PIX_FMT_BGRA,
        AV_PIX_FMT_ARGB,   AV_PIX_FMT_ABGR,
        AV_PIX_FMT_0RGB,   AV_PIX_FMT_0BGR,
        AV_PIX_FMT_RGB0,   AV_PIX_FMT_BGR0,
        AV_PIX_FMT_RGB48,  AV_PIX_FMT_BGR48,
        AV_PIX_FMT_RGBA64, AV_PIX_FMT_BGRA64,
        AV_PIX_FMT_NONE,
    };
    ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));
    return 0;
}

static int haldclutsrc_config_props(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    TestSourceContext *hc = ctx->priv;

    hc->w = hc->h = hc->level * hc->level * hc->level;
    return config_props(outlink);
}

static const AVFilterPad haldclutsrc_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .request_frame = request_frame,
        .config_props  = haldclutsrc_config_props,
    },
    {  NULL }
};

AVFilter avfilter_vsrc_haldclutsrc = {
    .name            = "haldclutsrc",
    .description     = NULL_IF_CONFIG_SMALL("Provide an identity Hald CLUT."),
    .priv_class      = &haldclutsrc_class,
    .priv_size       = sizeof(TestSourceContext),
    .init            = haldclutsrc_init,
    .uninit          = uninit,
    .query_formats   = haldclutsrc_query_formats,
    .inputs          = NULL,
    .outputs         = haldclutsrc_outputs,
};
#endif /* CONFIG_HALDCLUTSRC_FILTER */

#if CONFIG_NULLSRC_FILTER

#define nullsrc_options options
AVFILTER_DEFINE_CLASS(nullsrc);

static void nullsrc_fill_picture(AVFilterContext *ctx, AVFrame *picref) { }

static av_cold int nullsrc_init(AVFilterContext *ctx)
{
    TestSourceContext *test = ctx->priv;

    test->fill_picture_fn = nullsrc_fill_picture;
    return init(ctx);
}

static const AVFilterPad nullsrc_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .request_frame = request_frame,
        .config_props  = config_props,
    },
    { NULL },
};

AVFilter avfilter_vsrc_nullsrc = {
    .name        = "nullsrc",
    .description = NULL_IF_CONFIG_SMALL("Null video source, return unprocessed video frames."),
    .init       = nullsrc_init,
    .uninit     = uninit,
    .priv_size  = sizeof(TestSourceContext),
    .priv_class = &nullsrc_class,
    .inputs     = NULL,
    .outputs    = nullsrc_outputs,
};

#endif /* CONFIG_NULLSRC_FILTER */

#if CONFIG_TESTSRC_FILTER

static const AVOption testsrc_options[] = {
    COMMON_OPTIONS
    { "decimals", "set number of decimals to show", OFFSET(nb_decimals), AV_OPT_TYPE_INT, {.i64=0},  0, 17, FLAGS },
    { "n",        "set number of decimals to show", OFFSET(nb_decimals), AV_OPT_TYPE_INT, {.i64=0},  0, 17, FLAGS },
    { NULL }
};

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
static void draw_rectangle(unsigned val, uint8_t *dst, int dst_linesize, int segment_width,
                           int x, int y, int w, int h)
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

static void draw_digit(int digit, uint8_t *dst, int dst_linesize,
                       int segment_width)
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

static void test_fill_picture(AVFilterContext *ctx, AVFrame *frame)
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
    uint8_t *data = frame->data[0];
    int width  = frame->width;
    int height = frame->height;

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
        p0 += frame->linesize[0];
    }

    /* draw sliding color line */
    p0 = p = data + frame->linesize[0] * height * 3/4;
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
        memcpy(p+frame->linesize[0], p, 3 * width);
        p += frame->linesize[0];
    }

    /* draw digits */
    seg_size = width / 80;
    if (seg_size >= 1 && height >= 13 * seg_size) {
        int64_t p10decimals = 1;
        double time = av_q2d(test->time_base) * test->nb_frame *
                      pow(10, test->nb_decimals);
        if (time >= INT_MAX)
            return;

        for (x = 0; x < test->nb_decimals; x++)
            p10decimals *= 10;

        second = av_rescale_rnd(test->nb_frame * test->time_base.num, p10decimals, test->time_base.den, AV_ROUND_ZERO);
        x = width - (width - seg_size * 64) / 2;
        y = (height - seg_size * 13) / 2;
        p = data + (x*3 + y * frame->linesize[0]);
        for (i = 0; i < 8; i++) {
            p -= 3 * 8 * seg_size;
            draw_digit(second % 10, p, frame->linesize[0], seg_size);
            second /= 10;
            if (second == 0)
                break;
        }
    }
}

static av_cold int test_init(AVFilterContext *ctx)
{
    TestSourceContext *test = ctx->priv;

    test->fill_picture_fn = test_fill_picture;
    return init(ctx);
}

static int test_query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_RGB24, AV_PIX_FMT_NONE
    };
    ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));
    return 0;
}

static const AVFilterPad avfilter_vsrc_testsrc_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .request_frame = request_frame,
        .config_props  = config_props,
    },
    { NULL }
};

AVFilter avfilter_vsrc_testsrc = {
    .name          = "testsrc",
    .description   = NULL_IF_CONFIG_SMALL("Generate test pattern."),
    .priv_size     = sizeof(TestSourceContext),
    .priv_class    = &testsrc_class,
    .init          = test_init,
    .uninit        = uninit,

    .query_formats = test_query_formats,

    .inputs    = NULL,
    .outputs   = avfilter_vsrc_testsrc_outputs,
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
                              int x, int y, int r, int g, int b, enum AVPixelFormat fmt,
                              uint8_t rgba_map[4])
{
    int32_t v;
    uint8_t *p;

    switch (fmt) {
    case AV_PIX_FMT_BGR444: ((uint16_t*)(dst + y*dst_linesize))[x] = ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4); break;
    case AV_PIX_FMT_RGB444: ((uint16_t*)(dst + y*dst_linesize))[x] = ((b >> 4) << 8) | ((g >> 4) << 4) | (r >> 4); break;
    case AV_PIX_FMT_BGR555: ((uint16_t*)(dst + y*dst_linesize))[x] = ((r>>3)<<10) | ((g>>3)<<5) | (b>>3); break;
    case AV_PIX_FMT_RGB555: ((uint16_t*)(dst + y*dst_linesize))[x] = ((b>>3)<<10) | ((g>>3)<<5) | (r>>3); break;
    case AV_PIX_FMT_BGR565: ((uint16_t*)(dst + y*dst_linesize))[x] = ((r>>3)<<11) | ((g>>2)<<5) | (b>>3); break;
    case AV_PIX_FMT_RGB565: ((uint16_t*)(dst + y*dst_linesize))[x] = ((b>>3)<<11) | ((g>>2)<<5) | (r>>3); break;
    case AV_PIX_FMT_RGB24:
    case AV_PIX_FMT_BGR24:
        v = (r << (rgba_map[R]*8)) + (g << (rgba_map[G]*8)) + (b << (rgba_map[B]*8));
        p = dst + 3*x + y*dst_linesize;
        AV_WL24(p, v);
        break;
    case AV_PIX_FMT_RGBA:
    case AV_PIX_FMT_BGRA:
    case AV_PIX_FMT_ARGB:
    case AV_PIX_FMT_ABGR:
        v = (r << (rgba_map[R]*8)) + (g << (rgba_map[G]*8)) + (b << (rgba_map[B]*8)) + (255 << (rgba_map[A]*8));
        p = dst + 4*x + y*dst_linesize;
        AV_WL32(p, v);
        break;
    }
}

static void rgbtest_fill_picture(AVFilterContext *ctx, AVFrame *frame)
{
    TestSourceContext *test = ctx->priv;
    int x, y, w = frame->width, h = frame->height;

    for (y = 0; y < h; y++) {
         for (x = 0; x < w; x++) {
             int c = 256*x/w;
             int r = 0, g = 0, b = 0;

             if      (3*y < h  ) r = c;
             else if (3*y < 2*h) g = c;
             else                b = c;

             rgbtest_put_pixel(frame->data[0], frame->linesize[0], x, y, r, g, b,
                               ctx->outputs[0]->format, test->rgba_map);
         }
     }
}

static av_cold int rgbtest_init(AVFilterContext *ctx)
{
    TestSourceContext *test = ctx->priv;

    test->draw_once = 1;
    test->fill_picture_fn = rgbtest_fill_picture;
    return init(ctx);
}

static int rgbtest_query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_RGBA, AV_PIX_FMT_ARGB, AV_PIX_FMT_BGRA, AV_PIX_FMT_ABGR,
        AV_PIX_FMT_BGR24, AV_PIX_FMT_RGB24,
        AV_PIX_FMT_RGB444, AV_PIX_FMT_BGR444,
        AV_PIX_FMT_RGB565, AV_PIX_FMT_BGR565,
        AV_PIX_FMT_RGB555, AV_PIX_FMT_BGR555,
        AV_PIX_FMT_NONE
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

static const AVFilterPad avfilter_vsrc_rgbtestsrc_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .request_frame = request_frame,
        .config_props  = rgbtest_config_props,
    },
    { NULL }
};

AVFilter avfilter_vsrc_rgbtestsrc = {
    .name          = "rgbtestsrc",
    .description   = NULL_IF_CONFIG_SMALL("Generate RGB test pattern."),
    .priv_size     = sizeof(TestSourceContext),
    .priv_class    = &rgbtestsrc_class,
    .init          = rgbtest_init,
    .uninit        = uninit,

    .query_formats = rgbtest_query_formats,

    .inputs    = NULL,

    .outputs   = avfilter_vsrc_rgbtestsrc_outputs,
};

#endif /* CONFIG_RGBTESTSRC_FILTER */

#if CONFIG_SMPTEBARS_FILTER || CONFIG_SMPTEHDBARS_FILTER

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

static const uint8_t gray40[4] = { 102, 102, 102, 255 };
static const uint8_t gray15[4] = {  38,  38,  38, 255 };
static const uint8_t   cyan[4] = {   0, 255, 255, 255 };
static const uint8_t yellow[4] = { 255, 255,   0, 255 };
static const uint8_t   blue[4] = {   0,   0, 255, 255 };
static const uint8_t    red[4] = { 255,   0,   0, 255 };
static const uint8_t black0[4] = {   5,   5,   5, 255 };
static const uint8_t black2[4] = {  10,  10,  10, 255 };
static const uint8_t black4[4] = {  15,  15,  15, 255 };
static const uint8_t   neg2[4] = {   0,   0,   0, 255 };

static void inline draw_bar(TestSourceContext *test, const uint8_t *color,
                            unsigned x, unsigned y, unsigned w, unsigned h,
                            AVFrame *frame)
{
    FFDrawColor draw_color;

    x = FFMIN(x, test->w - 1);
    y = FFMIN(y, test->h - 1);
    w = FFMIN(w, test->w - x);
    h = FFMIN(h, test->h - y);

    av_assert0(x + w <= test->w);
    av_assert0(y + h <= test->h);

    ff_draw_color(&test->draw, &draw_color, color);
    ff_fill_rectangle(&test->draw, &draw_color,
                      frame->data, frame->linesize, x, y, w, h);
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

static const AVFilterPad smptebars_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .request_frame = request_frame,
        .config_props  = smptebars_config_props,
    },
    { NULL }
};

#if CONFIG_SMPTEBARS_FILTER

#define smptebars_options options
AVFILTER_DEFINE_CLASS(smptebars);

static void smptebars_fill_picture(AVFilterContext *ctx, AVFrame *picref)
{
    TestSourceContext *test = ctx->priv;
    int r_w, r_h, w_h, p_w, p_h, i, tmp, x = 0;
    const AVPixFmtDescriptor *pixdesc = av_pix_fmt_desc_get(picref->format);

    r_w = FFALIGN((test->w + 6) / 7, 1 << pixdesc->log2_chroma_w);
    r_h = FFALIGN(test->h * 2 / 3, 1 << pixdesc->log2_chroma_h);
    w_h = FFALIGN(test->h * 3 / 4 - r_h,  1 << pixdesc->log2_chroma_h);
    p_w = FFALIGN(r_w * 5 / 4, 1 << pixdesc->log2_chroma_w);
    p_h = test->h - w_h - r_h;

    for (i = 0; i < 7; i++) {
        draw_bar(test, rainbow[i], x, 0,   r_w, r_h, picref);
        draw_bar(test, wobnair[i], x, r_h, r_w, w_h, picref);
        x += r_w;
    }
    x = 0;
    draw_bar(test, i_pixel, x, r_h + w_h, p_w, p_h, picref);
    x += p_w;
    draw_bar(test, white, x, r_h + w_h, p_w, p_h, picref);
    x += p_w;
    draw_bar(test, q_pixel, x, r_h + w_h, p_w, p_h, picref);
    x += p_w;
    tmp = FFALIGN(5 * r_w - x,  1 << pixdesc->log2_chroma_w);
    draw_bar(test, black, x, r_h + w_h, tmp, p_h, picref);
    x += tmp;
    tmp = FFALIGN(r_w / 3,  1 << pixdesc->log2_chroma_w);
    draw_bar(test, neg4ire, x, r_h + w_h, tmp, p_h, picref);
    x += tmp;
    draw_bar(test, black, x, r_h + w_h, tmp, p_h, picref);
    x += tmp;
    draw_bar(test, pos4ire, x, r_h + w_h, tmp, p_h, picref);
    x += tmp;
    draw_bar(test, black, x, r_h + w_h, test->w - x, p_h, picref);
}

static av_cold int smptebars_init(AVFilterContext *ctx)
{
    TestSourceContext *test = ctx->priv;

    test->fill_picture_fn = smptebars_fill_picture;
    test->draw_once = 1;
    return init(ctx);
}

AVFilter avfilter_vsrc_smptebars = {
    .name      = "smptebars",
    .description = NULL_IF_CONFIG_SMALL("Generate SMPTE color bars."),
    .priv_size = sizeof(TestSourceContext),
    .init      = smptebars_init,
    .uninit    = uninit,

    .query_formats = smptebars_query_formats,
    .inputs        = NULL,
    .outputs       = smptebars_outputs,
    .priv_class    = &smptebars_class,
};

#endif  /* CONFIG_SMPTEBARS_FILTER */

#if CONFIG_SMPTEHDBARS_FILTER

#define smptehdbars_options options
AVFILTER_DEFINE_CLASS(smptehdbars);

static void smptehdbars_fill_picture(AVFilterContext *ctx, AVFrame *picref)
{
    TestSourceContext *test = ctx->priv;
    int d_w, r_w, r_h, l_w, i, tmp, x = 0, y = 0;
    const AVPixFmtDescriptor *pixdesc = av_pix_fmt_desc_get(picref->format);

    d_w = FFALIGN(test->w / 8, 1 << pixdesc->log2_chroma_w);
    r_h = FFALIGN(test->h * 7 / 12, 1 << pixdesc->log2_chroma_h);
    draw_bar(test, gray40, x, 0, d_w, r_h, picref);
    x += d_w;

    r_w = FFALIGN((((test->w + 3) / 4) * 3) / 7, 1 << pixdesc->log2_chroma_w);
    for (i = 0; i < 7; i++) {
        draw_bar(test, rainbow[i], x, 0, r_w, r_h, picref);
        x += r_w;
    }
    draw_bar(test, gray40, x, 0, test->w - x, r_h, picref);
    y = r_h;
    r_h = FFALIGN(test->h / 12, 1 << pixdesc->log2_chroma_h);
    draw_bar(test, cyan, 0, y, d_w, r_h, picref);
    x = d_w;
    draw_bar(test, i_pixel, x, y, r_w, r_h, picref);
    x += r_w;
    tmp = r_w * 6;
    draw_bar(test, rainbow[0], x, y, tmp, r_h, picref);
    x += tmp;
    l_w = x;
    draw_bar(test, blue, x, y, test->w - x, r_h, picref);
    y += r_h;
    draw_bar(test, yellow, 0, y, d_w, r_h, picref);
    x = d_w;
    draw_bar(test, q_pixel, x, y, r_w, r_h, picref);
    x += r_w;

    for (i = 0; i < tmp; i += 1 << pixdesc->log2_chroma_w) {
        uint8_t yramp[4] = {0};

        yramp[0] =
        yramp[1] =
        yramp[2] = i * 255 / tmp;
        yramp[3] = 255;

        draw_bar(test, yramp, x, y, 1 << pixdesc->log2_chroma_w, r_h, picref);
        x += 1 << pixdesc->log2_chroma_w;
    }
    draw_bar(test, red, x, y, test->w - x, r_h, picref);
    y += r_h;
    draw_bar(test, gray15, 0, y, d_w, test->h - y, picref);
    x = d_w;
    tmp = FFALIGN(r_w * 3 / 2, 1 << pixdesc->log2_chroma_w);
    draw_bar(test, black0, x, y, tmp, test->h - y, picref);
    x += tmp;
    tmp = FFALIGN(r_w * 2, 1 << pixdesc->log2_chroma_w);
    draw_bar(test, white, x, y, tmp, test->h - y, picref);
    x += tmp;
    tmp = FFALIGN(r_w * 5 / 6, 1 << pixdesc->log2_chroma_w);
    draw_bar(test, black0, x, y, tmp, test->h - y, picref);
    x += tmp;
    tmp = FFALIGN(r_w / 3, 1 << pixdesc->log2_chroma_w);
    draw_bar(test,   neg2, x, y, tmp, test->h - y, picref);
    x += tmp;
    draw_bar(test, black0, x, y, tmp, test->h - y, picref);
    x += tmp;
    draw_bar(test, black2, x, y, tmp, test->h - y, picref);
    x += tmp;
    draw_bar(test, black0, x, y, tmp, test->h - y, picref);
    x += tmp;
    draw_bar(test, black4, x, y, tmp, test->h - y, picref);
    x += tmp;
    r_w = l_w - x;
    draw_bar(test, black0, x, y, r_w, test->h - y, picref);
    x += r_w;
    draw_bar(test, gray15, x, y, test->w - x, test->h - y, picref);
}

static av_cold int smptehdbars_init(AVFilterContext *ctx)
{
    TestSourceContext *test = ctx->priv;

    test->fill_picture_fn = smptehdbars_fill_picture;
    test->draw_once = 1;
    return init(ctx);
}

AVFilter avfilter_vsrc_smptehdbars = {
    .name      = "smptehdbars",
    .description = NULL_IF_CONFIG_SMALL("Generate SMPTE HD color bars."),
    .priv_size = sizeof(TestSourceContext),
    .init      = smptehdbars_init,
    .uninit    = uninit,

    .query_formats = smptebars_query_formats,
    .inputs        = NULL,
    .outputs       = smptebars_outputs,
    .priv_class    = &smptehdbars_class,
};

#endif  /* CONFIG_SMPTEHDBARS_FILTER */
#endif  /* CONFIG_SMPTEBARS_FILTER || CONFIG_SMPTEHDBARS_FILTER */
