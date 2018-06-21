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
#include "avdevice.h"

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

    char            *list_dither;
    int             list_drivers;
} CACAContext;

static int caca_write_trailer(AVFormatContext *s)
{
    CACAContext *c = s->priv_data;

    av_freep(&c->window_title);

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
    return 0;
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
        if (!strcmp(c->list_dither, "colors")) {
            list_dither_color(c);
        } else if (!strcmp(c->list_dither, "charsets")) {
            list_dither_charset(c);
        } else if (!strcmp(c->list_dither, "algorithms")) {
            list_dither_algorithm(c);
        } else if (!strcmp(c->list_dither, "antialiases")) {
            list_dither_antialias(c);
        } else {
            av_log(s, AV_LOG_ERROR,
                   "Invalid argument '%s', for 'list_dither' option\n"
                   "Argument must be one of 'algorithms, 'antialiases', 'charsets', 'colors'\n",
                   c->list_dither);
            return AVERROR(EINVAL);
        }
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
        goto fail;
    }

    bpp = av_get_bits_per_pixel(av_pix_fmt_desc_get(encctx->format));
    c->dither = caca_create_dither(bpp, encctx->width, encctx->height,
                                   bpp / 8 * encctx->width,
                                   0x0000ff, 0x00ff00, 0xff0000, 0);
    if (!c->dither) {
        ret =  AVERROR(errno);
        av_log(s, AV_LOG_ERROR, "Failed to create dither\n");
        goto fail;
    }

#define CHECK_DITHER_OPT(opt) do {                                              \
    if (caca_set_dither_##opt(c->dither, c->opt) < 0)  {                        \
        ret = AVERROR(errno);                                                   \
        av_log(s, AV_LOG_ERROR, "Failed to set value '%s' for option '%s'\n",   \
               c->opt, #opt);                                                   \
        goto fail;                                                              \
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
        goto fail;
    }

    if (!c->window_width || !c->window_height) {
        c->window_width  = caca_get_canvas_width(c->canvas);
        c->window_height = caca_get_canvas_height(c->canvas);
    }

    if (!c->window_title)
        c->window_title = av_strdup(s->url);
    caca_set_display_title(c->display, c->window_title);
    caca_set_display_time(c->display, av_rescale_q(1, st->codec->time_base, AV_TIME_BASE_Q));

    return 0;

fail:
    caca_write_trailer(s);
    return ret;
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
    { "list_dither", "list available dither options", OFFSET(list_dither), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 1, ENC, "list_dither" },
    { "algorithms",   NULL, 0, AV_OPT_TYPE_CONST, {.str = "algorithms"}, 0, 0, ENC, "list_dither" },
    { "antialiases",  NULL, 0, AV_OPT_TYPE_CONST, {.str = "antialiases"},0, 0, ENC, "list_dither" },
    { "charsets",     NULL, 0, AV_OPT_TYPE_CONST, {.str = "charsets"},   0, 0, ENC, "list_dither" },
    { "colors",       NULL, 0, AV_OPT_TYPE_CONST, {.str = "colors"},     0, 0, ENC, "list_dither" },
    { NULL },
};

static const AVClass caca_class = {
    .class_name = "caca_outdev",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
    .category   = AV_CLASS_CATEGORY_DEVICE_VIDEO_OUTPUT,
};

AVOutputFormat ff_caca_muxer = {
    .name           = "caca",
    .long_name      = NULL_IF_CONFIG_SMALL("caca (color ASCII art) output device"),
    .priv_data_size = sizeof(CACAContext),
    .audio_codec    = AV_CODEC_ID_NONE,
    .video_codec    = AV_CODEC_ID_RAWVIDEO,
    .write_header   = caca_write_header,
    .write_packet   = caca_write_packet,
    .write_trailer  = caca_write_trailer,
    .flags          = AVFMT_NOFILE,
    .priv_class     = &caca_class,
};
