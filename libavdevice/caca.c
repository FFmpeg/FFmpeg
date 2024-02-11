/*
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

#include <caca.h>
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavformat/mux.h"
#include "avdevice.h"

enum {
    LIST_ALGORITHMS  = 1 << 0,
    LIST_ANTIALIASES = 1 << 1,
    LIST_CHARSETS    = 1 << 2,
    LIST_COLORS      = 1 << 3,
};

typedef struct CACAContext {
    AVClass         *class;
    AVFormatContext *ctx;
    char            *window_title;
    int             window_width,  window_height;

    caca_canvas_t   *canvas;
    caca_display_t  *display;
    caca_dither_t   *dither;

    char            *algorithm, *antialias;
    char            *charset, *color;
    char            *driver;

    int             list_dither;
    int             list_drivers;
} CACAContext;

static void caca_deinit(AVFormatContext *s)
{
    CACAContext *c = s->priv_data;

    if (c->display) {
        caca_free_display(c->display);
        c->display = NULL;
    }
    if (c->dither) {
        caca_free_dither(c->dither);
        c->dither = NULL;
    }
    if (c->canvas) {
        caca_free_canvas(c->canvas);
        c->canvas = NULL;
    }
}

static void list_drivers(CACAContext *c)
{
    const char *const *drivers = caca_get_display_driver_list();
    int i;

    av_log(c->ctx, AV_LOG_INFO, "Available drivers:\n");
    for (i = 0; drivers[i]; i += 2)
        av_log(c->ctx, AV_LOG_INFO, "%s: %s\n", drivers[i], drivers[i + 1]);
}

#define DEFINE_LIST_DITHER(thing, thing_str)                                 \
static void list_dither_## thing(CACAContext *c)                             \
{                                                                            \
    const char *const *thing = caca_get_dither_## thing ##_list(c->dither);  \
    int i;                                                                   \
                                                                             \
    av_log(c->ctx, AV_LOG_INFO, "Available %s:\n", thing_str);               \
    for (i = 0; thing[i]; i += 2)                                            \
        av_log(c->ctx, AV_LOG_INFO, "%s: %s\n", thing[i], thing[i + 1]);     \
}

DEFINE_LIST_DITHER(color, "colors");
DEFINE_LIST_DITHER(charset, "charsets");
DEFINE_LIST_DITHER(algorithm, "algorithms");
DEFINE_LIST_DITHER(antialias, "antialias");

static int caca_write_header(AVFormatContext *s)
{
    CACAContext *c = s->priv_data;
    AVStream *st = s->streams[0];
    AVCodecParameters *encctx = st->codecpar;
    int ret, bpp;

    c->ctx = s;
    if (c->list_drivers) {
        list_drivers(c);
        return AVERROR_EXIT;
    }
    if (c->list_dither) {
        if (c->list_dither & LIST_COLORS)
            list_dither_color(c);
        if (c->list_dither & LIST_CHARSETS)
            list_dither_charset(c);
        if (c->list_dither & LIST_ALGORITHMS)
            list_dither_algorithm(c);
        if (c->list_dither & LIST_ANTIALIASES)
            list_dither_antialias(c);
        return AVERROR_EXIT;
    }

    if (   s->nb_streams > 1
        || encctx->codec_type != AVMEDIA_TYPE_VIDEO
        || encctx->codec_id   != AV_CODEC_ID_RAWVIDEO) {
        av_log(s, AV_LOG_ERROR, "Only supports one rawvideo stream\n");
        return AVERROR(EINVAL);
    }

    if (encctx->format != AV_PIX_FMT_RGB24) {
        av_log(s, AV_LOG_ERROR,
               "Unsupported pixel format '%s', choose rgb24\n",
               av_get_pix_fmt_name(encctx->format));
        return AVERROR(EINVAL);
    }

    c->canvas = caca_create_canvas(c->window_width, c->window_height);
    if (!c->canvas) {
        ret = AVERROR(errno);
        av_log(s, AV_LOG_ERROR, "Failed to create canvas\n");
        return ret;
    }

    bpp = av_get_bits_per_pixel(av_pix_fmt_desc_get(encctx->format));
    c->dither = caca_create_dither(bpp, encctx->width, encctx->height,
                                   bpp / 8 * encctx->width,
                                   0x0000ff, 0x00ff00, 0xff0000, 0);
    if (!c->dither) {
        ret =  AVERROR(errno);
        av_log(s, AV_LOG_ERROR, "Failed to create dither\n");
        return ret;
    }

#define CHECK_DITHER_OPT(opt) do {                                              \
    if (caca_set_dither_##opt(c->dither, c->opt) < 0)  {                        \
        ret = AVERROR(errno);                                                   \
        av_log(s, AV_LOG_ERROR, "Failed to set value '%s' for option '%s'\n",   \
               c->opt, #opt);                                                   \
        return ret;                                                             \
    }                                                                           \
} while (0)

    CHECK_DITHER_OPT(algorithm);
    CHECK_DITHER_OPT(antialias);
    CHECK_DITHER_OPT(charset);
    CHECK_DITHER_OPT(color);

    c->display = caca_create_display_with_driver(c->canvas, c->driver);
    if (!c->display) {
        ret = AVERROR(errno);
        av_log(s, AV_LOG_ERROR, "Failed to create display\n");
        list_drivers(c);
        return ret;
    }

    if (!c->window_width || !c->window_height) {
        c->window_width  = caca_get_canvas_width(c->canvas);
        c->window_height = caca_get_canvas_height(c->canvas);
    }

    if (!c->window_title)
        c->window_title = av_strdup(s->url);
    caca_set_display_title(c->display, c->window_title);
    caca_set_display_time(c->display, av_rescale_q(1, st->time_base, AV_TIME_BASE_Q));

    return 0;
}

static int caca_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    CACAContext *c = s->priv_data;

    caca_dither_bitmap(c->canvas, 0, 0, c->window_width, c->window_height, c->dither, pkt->data);
    caca_refresh_display(c->display);

    return 0;
}

#define OFFSET(x) offsetof(CACAContext,x)
#define ENC AV_OPT_FLAG_ENCODING_PARAM

static const AVOption options[] = {
    { "window_size",  "set window forced size",  OFFSET(window_width), AV_OPT_TYPE_IMAGE_SIZE, {.str = NULL }, 0, 0, ENC},
    { "window_title", "set window title",        OFFSET(window_title), AV_OPT_TYPE_STRING,     {.str = NULL }, 0, 0, ENC },
    { "driver",       "set display driver",      OFFSET(driver),    AV_OPT_TYPE_STRING, {.str = NULL }, 0, 0, ENC },
    { "algorithm",    "set dithering algorithm", OFFSET(algorithm), AV_OPT_TYPE_STRING, {.str = "default" }, 0, 0, ENC },
    { "antialias",    "set antialias method",    OFFSET(antialias), AV_OPT_TYPE_STRING, {.str = "default" }, 0, 0, ENC },
    { "charset",      "set charset used to render output", OFFSET(charset), AV_OPT_TYPE_STRING, {.str = "default" }, 0, 0, ENC },
    { "color",        "set color used to render output",   OFFSET(color),   AV_OPT_TYPE_STRING, {.str = "default" }, 0, 0, ENC },
    { "list_drivers", "list available drivers",  OFFSET(list_drivers), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, ENC },
    { "list_dither", "list available dither options", OFFSET(list_dither), AV_OPT_TYPE_FLAGS, { .i64 = 0 }, 0, INT_MAX, ENC, .unit = "list_dither" },
    { "algorithms",   NULL, 0, AV_OPT_TYPE_CONST, {.i64 = LIST_ALGORITHMS },  0, 0, ENC, .unit = "list_dither" },
    { "antialiases",  NULL, 0, AV_OPT_TYPE_CONST, {.i64 = LIST_ANTIALIASES }, 0, 0, ENC, .unit = "list_dither" },
    { "charsets",     NULL, 0, AV_OPT_TYPE_CONST, {.i64 = LIST_CHARSETS },    0, 0, ENC, .unit = "list_dither" },
    { "colors",       NULL, 0, AV_OPT_TYPE_CONST, {.i64 = LIST_COLORS },      0, 0, ENC, .unit = "list_dither" },
    { NULL },
};

static const AVClass caca_class = {
    .class_name = "caca outdev",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
    .category   = AV_CLASS_CATEGORY_DEVICE_VIDEO_OUTPUT,
};

const FFOutputFormat ff_caca_muxer = {
    .p.name         = "caca",
    .p.long_name    = NULL_IF_CONFIG_SMALL("caca (color ASCII art) output device"),
    .priv_data_size = sizeof(CACAContext),
    .p.audio_codec  = AV_CODEC_ID_NONE,
    .p.video_codec  = AV_CODEC_ID_RAWVIDEO,
    .write_header   = caca_write_header,
    .write_packet   = caca_write_packet,
    .deinit         = caca_deinit,
    .p.flags        = AVFMT_NOFILE,
    .p.priv_class   = &caca_class,
};
