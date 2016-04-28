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
 * allyuv, smptebars and smptehdbars are by Paul B Mahol.
 */

#include <float.h>

#include "libavutil/avassert.h"
#include "libavutil/common.h"
#include "libavutil/ffmath.h"
#include "libavutil/opt.h"
#include "libavutil/imgutils.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/parseutils.h"
#include "libavutil/xga_font_data.h"
#include "avfilter.h"
#include "drawutils.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

typedef struct TestSourceContext {
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
    return ff_set_common_formats(ctx, ff_draw_supported_pixel_formats(0));
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

AVFilter ff_vsrc_color = {
    .name            = "color",
    .description     = NULL_IF_CONFIG_SMALL("Provide an uniformly colored input."),
    .priv_class      = &color_class,
    .priv_size       = sizeof(TestSourceContext),
    .init            = color_init,
    .uninit          = uninit,
    .query_formats   = color_query_formats,
    .inputs          = NULL,
    .outputs         = color_outputs,
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

    AVFilterFormats *fmts_list = ff_make_format_list(pix_fmts);
    if (!fmts_list)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, fmts_list);
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

AVFilter ff_vsrc_haldclutsrc = {
    .name          = "haldclutsrc",
    .description   = NULL_IF_CONFIG_SMALL("Provide an identity Hald CLUT."),
    .priv_class    = &haldclutsrc_class,
    .priv_size     = sizeof(TestSourceContext),
    .init          = haldclutsrc_init,
    .uninit        = uninit,
    .query_formats = haldclutsrc_query_formats,
    .inputs        = NULL,
    .outputs       = haldclutsrc_outputs,
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

AVFilter ff_vsrc_nullsrc = {
    .name        = "nullsrc",
    .description = NULL_IF_CONFIG_SMALL("Null video source, return unprocessed video frames."),
    .init        = nullsrc_init,
    .uninit      = uninit,
    .priv_size   = sizeof(TestSourceContext),
    .priv_class  = &nullsrc_class,
    .inputs      = NULL,
    .outputs     = nullsrc_outputs,
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
    struct segments {
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
    p0 = p = data + frame->linesize[0] * (height * 3/4);
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
                      ff_exp10(test->nb_decimals);
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

    AVFilterFormats *fmts_list = ff_make_format_list(pix_fmts);
    if (!fmts_list)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, fmts_list);
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

AVFilter ff_vsrc_testsrc = {
    .name          = "testsrc",
    .description   = NULL_IF_CONFIG_SMALL("Generate test pattern."),
    .priv_size     = sizeof(TestSourceContext),
    .priv_class    = &testsrc_class,
    .init          = test_init,
    .uninit        = uninit,
    .query_formats = test_query_formats,
    .inputs        = NULL,
    .outputs       = avfilter_vsrc_testsrc_outputs,
};

#endif /* CONFIG_TESTSRC_FILTER */

#if CONFIG_TESTSRC2_FILTER

static const AVOption testsrc2_options[] = {
    COMMON_OPTIONS
    { NULL }
};

AVFILTER_DEFINE_CLASS(testsrc2);

static void set_color(TestSourceContext *s, FFDrawColor *color, uint32_t argb)
{
    uint8_t rgba[4] = { (argb >> 16) & 0xFF,
                        (argb >>  8) & 0xFF,
                        (argb >>  0) & 0xFF,
                        (argb >> 24) & 0xFF, };
    ff_draw_color(&s->draw, color, rgba);
}

static uint32_t color_gradient(unsigned index)
{
    unsigned si = index & 0xFF, sd = 0xFF - si;
    switch (index >> 8) {
    case 0: return 0xFF0000 + (si <<  8);
    case 1: return 0x00FF00 + (sd << 16);
    case 2: return 0x00FF00 + (si <<  0);
    case 3: return 0x0000FF + (sd <<  8);
    case 4: return 0x0000FF + (si << 16);
    case 5: return 0xFF0000 + (sd <<  0);
    }
    av_assert0(0);
}

static void draw_text(TestSourceContext *s, AVFrame *frame, FFDrawColor *color,
                      int x0, int y0, const uint8_t *text)
{
    int x = x0;

    for (; *text; text++) {
        if (*text == '\n') {
            x = x0;
            y0 += 16;
            continue;
        }
        ff_blend_mask(&s->draw, color, frame->data, frame->linesize,
                      frame->width, frame->height,
                      avpriv_vga16_font + *text * 16, 1, 8, 16, 0, 0, x, y0);
        x += 8;
    }
}

static void test2_fill_picture(AVFilterContext *ctx, AVFrame *frame)
{
    TestSourceContext *s = ctx->priv;
    FFDrawColor color;

    /* colored background */
    {
        unsigned i, x = 0, x2;

        x = 0;
        for (i = 1; i < 7; i++) {
            x2 = av_rescale(i, s->w, 6);
            x2 = ff_draw_round_to_sub(&s->draw, 0, 0, x2);
            set_color(s, &color, ((i & 1) ? 0xFF0000 : 0) |
                                 ((i & 2) ? 0x00FF00 : 0) |
                                 ((i & 4) ? 0x0000FF : 0));
            ff_fill_rectangle(&s->draw, &color, frame->data, frame->linesize,
                              x, 0, x2 - x, frame->height);
            x = x2;
        }
    }

    /* oblique gradient */
    /* note: too slow if using blending */
    if (s->h >= 64) {
        unsigned x, dx, y0, y, g0, g;

        dx = ff_draw_round_to_sub(&s->draw, 0, +1, 1);
        y0 = av_rescale_q(s->pts, s->time_base, av_make_q(2, s->h - 16));
        g0 = av_rescale_q(s->pts, s->time_base, av_make_q(1, 128));
        for (x = 0; x < s->w; x += dx) {
            g = (av_rescale(x, 6 * 256, s->w) + g0) % (6 * 256);
            set_color(s, &color, color_gradient(g));
            y = y0 + av_rescale(x, s->h / 2, s->w);
            y %= 2 * (s->h - 16);
            if (y > s->h - 16)
                y = 2 * (s->h - 16) - y;
            y = ff_draw_round_to_sub(&s->draw, 1, 0, y);
            ff_fill_rectangle(&s->draw, &color, frame->data, frame->linesize,
                              x, y, dx, 16);
        }
    }

    /* top right: draw clock hands */
    if (s->w >= 64 && s->h >= 64) {
        int l = (FFMIN(s->w, s->h) - 32) >> 1;
        int steps = FFMAX(4, l >> 5);
        int xc = (s->w >> 2) + (s->w >> 1);
        int yc = (s->h >> 2);
        int cycle = l << 2;
        int pos, xh, yh;
        int c, i;

        for (c = 0; c < 3; c++) {
            set_color(s, &color, 0xBBBBBB ^ (0xFF << (c << 3)));
            pos = av_rescale_q(s->pts, s->time_base, av_make_q(64 >> (c << 1), cycle)) % cycle;
            xh = pos < 1 * l ? pos :
                 pos < 2 * l ? l :
                 pos < 3 * l ? 3 * l - pos : 0;
            yh = pos < 1 * l ? 0 :
                 pos < 2 * l ? pos - l :
                 pos < 3 * l ? l :
                               cycle - pos;
            xh -= l >> 1;
            yh -= l >> 1;
            for (i = 1; i <= steps; i++) {
                int x = av_rescale(xh, i, steps) + xc;
                int y = av_rescale(yh, i, steps) + yc;
                x = ff_draw_round_to_sub(&s->draw, 0, -1, x);
                y = ff_draw_round_to_sub(&s->draw, 1, -1, y);
                ff_fill_rectangle(&s->draw, &color, frame->data, frame->linesize,
                                  x, y, 8, 8);
            }
        }
    }

    /* bottom left: beating rectangles */
    if (s->w >= 64 && s->h >= 64) {
        int l = (FFMIN(s->w, s->h) - 16) >> 2;
        int cycle = l << 3;
        int xc = (s->w >> 2);
        int yc = (s->h >> 2) + (s->h >> 1);
        int xm1 = ff_draw_round_to_sub(&s->draw, 0, -1, xc - 8);
        int xm2 = ff_draw_round_to_sub(&s->draw, 0, +1, xc + 8);
        int ym1 = ff_draw_round_to_sub(&s->draw, 1, -1, yc - 8);
        int ym2 = ff_draw_round_to_sub(&s->draw, 1, +1, yc + 8);
        int size, step, x1, x2, y1, y2;

        size = av_rescale_q(s->pts, s->time_base, av_make_q(4, cycle));
        step = size / l;
        size %= l;
        if (step & 1)
            size = l - size;
        step = (step >> 1) & 3;
        set_color(s, &color, 0xFF808080);
        x1 = ff_draw_round_to_sub(&s->draw, 0, -1, xc - 4 - size);
        x2 = ff_draw_round_to_sub(&s->draw, 0, +1, xc + 4 + size);
        y1 = ff_draw_round_to_sub(&s->draw, 1, -1, yc - 4 - size);
        y2 = ff_draw_round_to_sub(&s->draw, 1, +1, yc + 4 + size);
        if (step == 0 || step == 2)
            ff_fill_rectangle(&s->draw, &color, frame->data, frame->linesize,
                              x1, ym1, x2 - x1, ym2 - ym1);
        if (step == 1 || step == 2)
            ff_fill_rectangle(&s->draw, &color, frame->data, frame->linesize,
                              xm1, y1, xm2 - xm1, y2 - y1);
        if (step == 3)
            ff_fill_rectangle(&s->draw, &color, frame->data, frame->linesize,
                              x1, y1, x2 - x1, y2 - y1);
    }

    /* bottom right: checker with random noise */
    {
        unsigned xmin = av_rescale(5, s->w, 8);
        unsigned xmax = av_rescale(7, s->w, 8);
        unsigned ymin = av_rescale(5, s->h, 8);
        unsigned ymax = av_rescale(7, s->h, 8);
        unsigned x, y, i, r;
        uint8_t alpha[256];

        r = s->pts;
        for (y = ymin; y < ymax - 15; y += 16) {
            for (x = xmin; x < xmax - 15; x += 16) {
                if ((x ^ y) & 16)
                    continue;
                for (i = 0; i < 256; i++) {
                    r = r * 1664525 + 1013904223;
                    alpha[i] = r >> 24;
                }
                set_color(s, &color, 0xFF00FF80);
                ff_blend_mask(&s->draw, &color, frame->data, frame->linesize,
                                   frame->width, frame->height,
                                   alpha, 16, 16, 16, 3, 0, x, y);
            }
        }
    }

    /* bouncing square */
    if (s->w >= 16 && s->h >= 16) {
        unsigned w = s->w - 8;
        unsigned h = s->h - 8;
        unsigned x = av_rescale_q(s->pts, s->time_base, av_make_q(233, 55 * w)) % (w << 1);
        unsigned y = av_rescale_q(s->pts, s->time_base, av_make_q(233, 89 * h)) % (h << 1);
        if (x > w)
            x = (w << 1) - x;
        if (y > h)
            y = (h << 1) - y;
        x = ff_draw_round_to_sub(&s->draw, 0, -1, x);
        y = ff_draw_round_to_sub(&s->draw, 1, -1, y);
        set_color(s, &color, 0xFF8000FF);
        ff_fill_rectangle(&s->draw, &color, frame->data, frame->linesize,
                          x, y, 8, 8);
    }

    /* top right: draw frame time and frame number */
    {
        char buf[256];
        unsigned time;

        time = av_rescale_q(s->pts, s->time_base, av_make_q(1, 1000)) % 86400000;
        set_color(s, &color, 0xC0000000);
        ff_blend_rectangle(&s->draw, &color, frame->data, frame->linesize,
                           frame->width, frame->height,
                           2, 2, 100, 36);
        set_color(s, &color, 0xFFFF8000);
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d\n%12"PRIi64,
                 time / 3600000, (time / 60000) % 60, (time / 1000) % 60,
                 time % 1000, s->pts);
        draw_text(s, frame, &color, 4, 4, buf);
    }
}
static av_cold int test2_init(AVFilterContext *ctx)
{
    TestSourceContext *s = ctx->priv;

    s->fill_picture_fn = test2_fill_picture;
    return init(ctx);
}

static int test2_query_formats(AVFilterContext *ctx)
{
    return ff_set_common_formats(ctx, ff_draw_supported_pixel_formats(0));
}

static int test2_config_props(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->src;
    TestSourceContext *s = ctx->priv;

    av_assert0(ff_draw_init(&s->draw, inlink->format, 0) >= 0);
    s->w = ff_draw_round_to_sub(&s->draw, 0, -1, s->w);
    s->h = ff_draw_round_to_sub(&s->draw, 1, -1, s->h);
    if (av_image_check_size(s->w, s->h, 0, ctx) < 0)
        return AVERROR(EINVAL);
    return config_props(inlink);
}

static const AVFilterPad avfilter_vsrc_testsrc2_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .request_frame = request_frame,
        .config_props  = test2_config_props,
    },
    { NULL }
};

AVFilter ff_vsrc_testsrc2 = {
    .name          = "testsrc2",
    .description   = NULL_IF_CONFIG_SMALL("Generate another test pattern."),
    .priv_size     = sizeof(TestSourceContext),
    .priv_class    = &testsrc2_class,
    .init          = test2_init,
    .uninit        = uninit,
    .query_formats = test2_query_formats,
    .inputs        = NULL,
    .outputs       = avfilter_vsrc_testsrc2_outputs,
};

#endif /* CONFIG_TESTSRC2_FILTER */

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

    AVFilterFormats *fmts_list = ff_make_format_list(pix_fmts);
    if (!fmts_list)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, fmts_list);
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

AVFilter ff_vsrc_rgbtestsrc = {
    .name          = "rgbtestsrc",
    .description   = NULL_IF_CONFIG_SMALL("Generate RGB test pattern."),
    .priv_size     = sizeof(TestSourceContext),
    .priv_class    = &rgbtestsrc_class,
    .init          = rgbtest_init,
    .uninit        = uninit,
    .query_formats = rgbtest_query_formats,
    .inputs        = NULL,
    .outputs       = avfilter_vsrc_rgbtestsrc_outputs,
};

#endif /* CONFIG_RGBTESTSRC_FILTER */

#if CONFIG_SMPTEBARS_FILTER || CONFIG_SMPTEHDBARS_FILTER

static const uint8_t rainbow[7][4] = {
    { 180, 128, 128, 255 },     /* 75% white */
    { 162,  44, 142, 255 },     /* 75% yellow */
    { 131, 156,  44, 255 },     /* 75% cyan */
    { 112,  72,  58, 255 },     /* 75% green */
    {  84, 184, 198, 255 },     /* 75% magenta */
    {  65, 100, 212, 255 },     /* 75% red */
    {  35, 212, 114, 255 },     /* 75% blue */
};

static const uint8_t rainbowhd[7][4] = {
    { 180, 128, 128, 255 },     /* 75% white */
    { 168,  44, 136, 255 },     /* 75% yellow */
    { 145, 147,  44, 255 },     /* 75% cyan */
    { 133,  63,  52, 255 },     /* 75% green */
    {  63, 193, 204, 255 },     /* 75% magenta */
    {  51, 109, 212, 255 },     /* 75% red */
    {  28, 212, 120, 255 },     /* 75% blue */
};

static const uint8_t wobnair[7][4] = {
    {  35, 212, 114, 255 },     /* 75% blue */
    {  19, 128, 128, 255 },     /* 7.5% intensity black */
    {  84, 184, 198, 255 },     /* 75% magenta */
    {  19, 128, 128, 255 },     /* 7.5% intensity black */
    { 131, 156,  44, 255 },     /* 75% cyan */
    {  19, 128, 128, 255 },     /* 7.5% intensity black */
    { 180, 128, 128, 255 },     /* 75% white */
};

static const uint8_t white[4] = { 235, 128, 128, 255 };

/* pluge pulses */
static const uint8_t neg4ire[4] = {  7, 128, 128, 255 };
static const uint8_t pos4ire[4] = { 24, 128, 128, 255 };

/* fudged Q/-I */
static const uint8_t i_pixel[4] = { 57, 156,  97, 255 };
static const uint8_t q_pixel[4] = { 44, 171, 147, 255 };

static const uint8_t gray40[4] = { 104, 128, 128, 255 };
static const uint8_t gray15[4] = {  49, 128, 128, 255 };
static const uint8_t   cyan[4] = { 188, 154,  16, 255 };
static const uint8_t yellow[4] = { 219,  16, 138, 255 };
static const uint8_t   blue[4] = {  32, 240, 118, 255 };
static const uint8_t    red[4] = {  63, 102, 240, 255 };
static const uint8_t black0[4] = {  16, 128, 128, 255 };
static const uint8_t black2[4] = {  20, 128, 128, 255 };
static const uint8_t black4[4] = {  25, 128, 128, 255 };
static const uint8_t   neg2[4] = {  12, 128, 128, 255 };

static void draw_bar(TestSourceContext *test, const uint8_t color[4],
                     int x, int y, int w, int h,
                     AVFrame *frame)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(frame->format);
    uint8_t *p, *p0;
    int plane;

    x = FFMIN(x, test->w - 1);
    y = FFMIN(y, test->h - 1);
    w = FFMIN(w, test->w - x);
    h = FFMIN(h, test->h - y);

    av_assert0(x + w <= test->w);
    av_assert0(y + h <= test->h);

    for (plane = 0; frame->data[plane]; plane++) {
        const int c = color[plane];
        const int linesize = frame->linesize[plane];
        int i, px, py, pw, ph;

        if (plane == 1 || plane == 2) {
            px = x >> desc->log2_chroma_w;
            pw = AV_CEIL_RSHIFT(w, desc->log2_chroma_w);
            py = y >> desc->log2_chroma_h;
            ph = AV_CEIL_RSHIFT(h, desc->log2_chroma_h);
        } else {
            px = x;
            pw = w;
            py = y;
            ph = h;
        }

        p0 = p = frame->data[plane] + py * linesize + px;
        memset(p, c, pw);
        p += linesize;
        for (i = 1; i < ph; i++, p += linesize)
            memcpy(p, p0, pw);
    }
}

static int smptebars_query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV422P,
        AV_PIX_FMT_YUV440P, AV_PIX_FMT_YUV444P,
        AV_PIX_FMT_YUV410P, AV_PIX_FMT_YUV411P,
        AV_PIX_FMT_NONE,
    };

    AVFilterFormats *fmts_list = ff_make_format_list(pix_fmts);
    if (!fmts_list)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, fmts_list);
}

static const AVFilterPad smptebars_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .request_frame = request_frame,
        .config_props  = config_props,
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

    av_frame_set_colorspace(picref, AVCOL_SPC_BT470BG);

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
    draw_bar(test, black0, x, r_h + w_h, tmp, p_h, picref);
    x += tmp;
    tmp = FFALIGN(r_w / 3,  1 << pixdesc->log2_chroma_w);
    draw_bar(test, neg4ire, x, r_h + w_h, tmp, p_h, picref);
    x += tmp;
    draw_bar(test, black0, x, r_h + w_h, tmp, p_h, picref);
    x += tmp;
    draw_bar(test, pos4ire, x, r_h + w_h, tmp, p_h, picref);
    x += tmp;
    draw_bar(test, black0, x, r_h + w_h, test->w - x, p_h, picref);
}

static av_cold int smptebars_init(AVFilterContext *ctx)
{
    TestSourceContext *test = ctx->priv;

    test->fill_picture_fn = smptebars_fill_picture;
    test->draw_once = 1;
    return init(ctx);
}

AVFilter ff_vsrc_smptebars = {
    .name          = "smptebars",
    .description   = NULL_IF_CONFIG_SMALL("Generate SMPTE color bars."),
    .priv_size     = sizeof(TestSourceContext),
    .priv_class    = &smptebars_class,
    .init          = smptebars_init,
    .uninit        = uninit,
    .query_formats = smptebars_query_formats,
    .inputs        = NULL,
    .outputs       = smptebars_outputs,
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

    av_frame_set_colorspace(picref, AVCOL_SPC_BT709);

    d_w = FFALIGN(test->w / 8, 1 << pixdesc->log2_chroma_w);
    r_h = FFALIGN(test->h * 7 / 12, 1 << pixdesc->log2_chroma_h);
    draw_bar(test, gray40, x, 0, d_w, r_h, picref);
    x += d_w;

    r_w = FFALIGN((((test->w + 3) / 4) * 3) / 7, 1 << pixdesc->log2_chroma_w);
    for (i = 0; i < 7; i++) {
        draw_bar(test, rainbowhd[i], x, 0, r_w, r_h, picref);
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
    draw_bar(test, rainbowhd[0], x, y, tmp, r_h, picref);
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

        yramp[0] = i * 255 / tmp;
        yramp[1] = 128;
        yramp[2] = 128;
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

AVFilter ff_vsrc_smptehdbars = {
    .name          = "smptehdbars",
    .description   = NULL_IF_CONFIG_SMALL("Generate SMPTE HD color bars."),
    .priv_size     = sizeof(TestSourceContext),
    .priv_class    = &smptehdbars_class,
    .init          = smptehdbars_init,
    .uninit        = uninit,
    .query_formats = smptebars_query_formats,
    .inputs        = NULL,
    .outputs       = smptebars_outputs,
};

#endif  /* CONFIG_SMPTEHDBARS_FILTER */
#endif  /* CONFIG_SMPTEBARS_FILTER || CONFIG_SMPTEHDBARS_FILTER */

#if CONFIG_ALLYUV_FILTER

static const AVOption allyuv_options[] = {
    COMMON_OPTIONS_NOSIZE
    { NULL }
};

AVFILTER_DEFINE_CLASS(allyuv);

static void allyuv_fill_picture(AVFilterContext *ctx, AVFrame *frame)
{
    const int ys = frame->linesize[0];
    const int us = frame->linesize[1];
    const int vs = frame->linesize[2];
    int x, y, j;

    for (y = 0; y < 4096; y++) {
        for (x = 0; x < 2048; x++) {
            frame->data[0][y * ys + x] = ((x / 8) % 256);
            frame->data[0][y * ys + 4095 - x] = ((x / 8) % 256);
        }

        for (x = 0; x < 2048; x+=8) {
            for (j = 0; j < 8; j++) {
                frame->data[1][vs * y + x + j]        = (y%16 + (j % 8) * 16);
                frame->data[1][vs * y + 4095 - x - j] = (128 + y%16 + (j % 8) * 16);
            }
        }

        for (x = 0; x < 4096; x++)
            frame->data[2][y * us + x] = 256 * y / 4096;
    }
}

static av_cold int allyuv_init(AVFilterContext *ctx)
{
    TestSourceContext *test = ctx->priv;

    test->w = test->h = 4096;
    test->draw_once = 1;
    test->fill_picture_fn = allyuv_fill_picture;
    return init(ctx);
}

static int allyuv_query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_YUV444P, AV_PIX_FMT_GBRP,
        AV_PIX_FMT_NONE
    };

    AVFilterFormats *fmts_list = ff_make_format_list(pix_fmts);
    if (!fmts_list)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, fmts_list);
}

static const AVFilterPad avfilter_vsrc_allyuv_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .request_frame = request_frame,
        .config_props  = config_props,
    },
    { NULL }
};

AVFilter ff_vsrc_allyuv = {
    .name          = "allyuv",
    .description   = NULL_IF_CONFIG_SMALL("Generate all yuv colors."),
    .priv_size     = sizeof(TestSourceContext),
    .priv_class    = &allyuv_class,
    .init          = allyuv_init,
    .uninit        = uninit,
    .query_formats = allyuv_query_formats,
    .inputs        = NULL,
    .outputs       = avfilter_vsrc_allyuv_outputs,
};

#endif /* CONFIG_ALLYUV_FILTER */

#if CONFIG_ALLRGB_FILTER

static const AVOption allrgb_options[] = {
    COMMON_OPTIONS_NOSIZE
    { NULL }
};

AVFILTER_DEFINE_CLASS(allrgb);

static void allrgb_fill_picture(AVFilterContext *ctx, AVFrame *frame)
{
    unsigned x, y;
    const int linesize = frame->linesize[0];
    uint8_t *line = frame->data[0];

    for (y = 0; y < 4096; y++) {
        uint8_t *dst = line;

        for (x = 0; x < 4096; x++) {
            *dst++ = x;
            *dst++ = y;
            *dst++ = (x >> 8) | ((y >> 8) << 4);
        }
        line += linesize;
    }
}

static av_cold int allrgb_init(AVFilterContext *ctx)
{
    TestSourceContext *test = ctx->priv;

    test->w = test->h = 4096;
    test->draw_once = 1;
    test->fill_picture_fn = allrgb_fill_picture;
    return init(ctx);
}

static int allrgb_config_props(AVFilterLink *outlink)
{
    TestSourceContext *test = outlink->src->priv;

    ff_fill_rgba_map(test->rgba_map, outlink->format);
    return config_props(outlink);
}

static int allrgb_query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_RGB24, AV_PIX_FMT_NONE
    };

    AVFilterFormats *fmts_list = ff_make_format_list(pix_fmts);
    if (!fmts_list)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, fmts_list);
}

static const AVFilterPad avfilter_vsrc_allrgb_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .request_frame = request_frame,
        .config_props  = allrgb_config_props,
    },
    { NULL }
};

AVFilter ff_vsrc_allrgb = {
    .name          = "allrgb",
    .description   = NULL_IF_CONFIG_SMALL("Generate all RGB colors."),
    .priv_size     = sizeof(TestSourceContext),
    .priv_class    = &allrgb_class,
    .init          = allrgb_init,
    .uninit        = uninit,
    .query_formats = allrgb_query_formats,
    .inputs        = NULL,
    .outputs       = avfilter_vsrc_allrgb_outputs,
};

#endif /* CONFIG_ALLRGB_FILTER */
