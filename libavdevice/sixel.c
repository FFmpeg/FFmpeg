/*
 * Copyright (c) 2014 Hayaki Saito
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

#include <sixel.h>
#include <stdio.h>
#include <unistd.h>
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avdevice.h"
#include "libavutil/time.h"


typedef struct SIXELContext {
    AVClass *class;
    AVRational time_base;   /**< Time base */
    int64_t    time_frame;  /**< Current time */
    AVRational framerate;
    char *reset_position;
    int row;
    int col;
    int reqcolors;
    LSOutputContextPtr output;
    sixel_dither_t *dither;
    sixel_dither_t *testdither;
    int fixedpal;
    enum methodForDiffuse diffuse;
    int threshold;
    int dropframe;
    int ignoredelay;
} SIXELContext;

static int detect_scene_change(SIXELContext *const c)
{
    int score;
    int i;
    unsigned int r = 0;
    unsigned int g = 0;
    unsigned int b = 0;
    static unsigned int average_r = 0;
    static unsigned int average_g = 0;
    static unsigned int average_b = 0;

    if (c->dither == NULL)
        goto detected;

    /* detect scene change if number of colors increses 20% */
    if (c->dither->origcolors * 6 < c->testdither->origcolors * 5)
        goto detected;

    /* detect scene change if number of colors decreses 20% */
    if (c->dither->origcolors * 4 > c->testdither->origcolors * 5)
        goto detected;

    /* compare color average difference between current
     * palette and previous one */
    for (i = 0; i < c->testdither->ncolors; i++) {
        r += c->testdither->palette[i * 3 + 0];
        g += c->testdither->palette[i * 3 + 1];
        b += c->testdither->palette[i * 3 + 2];
    }
    score = (r - average_r) * (r - average_r)
          + (g - average_g) * (g - average_g)
          + (b - average_b) * (b - average_b);
    if (score > c->threshold * c->testdither->ncolors
                             * c->testdither->ncolors)
        goto detected;

    return 0;

detected:
    average_r = r;
    average_g = g;
    average_b = b;
    return 1;
}

static int prepare_static_palette(SIXELContext *const c,
                                  AVCodecContext *const codec)
{
    c->dither = sixel_dither_get(BUILTIN_XTERM256);
    if (c->dither == NULL)
        return (-1);
    c->dither->ncolors = c->reqcolors;
    return 0;
}

static int prepare_dynamic_palette(SIXELContext *const c,
                                   AVCodecContext *const codec,
                                   AVPacket *const pkt)
{
    int ret;
    unsigned char *palette;

    /* create histgram and construct color palette
     * with median cut algorithm. */
    ret = sixel_prepare_palette(c->testdither, pkt->data,
                                codec->width, codec->height, 3);
    if (ret != 0)
        return (-1);

    palette = c->testdither->palette;

    /* check whether the scence is changed. use old palette
     * if scene is not changed. */
    if (detect_scene_change(c)) {
        if (c->dither)
            sixel_dither_unref(c->dither);
        c->dither = c->testdither;
        c->testdither = sixel_dither_create(c->reqcolors);
    }
    return 0;
}

static FILE *sixel_output_file = NULL;

static int sixel_putchar(int c)
{
    return fputc(c, sixel_output_file);
}

static int sixel_printf(char const *fmt, ...)
{
    int ret;
    va_list ap;

    va_start(ap, fmt);
    ret = vfprintf(sixel_output_file, fmt, ap);
    va_end(ap);

    return ret;
}

static int sixel_write_header(AVFormatContext *s)
{
    SIXELContext *c = s->priv_data;
    AVCodecContext *codec = s->streams[0]->codec;
    int ret = 0;
    static char reset_position[256];

    if (s->nb_streams > 1
        || codec->codec_type != AVMEDIA_TYPE_VIDEO
        || codec->codec_id   != CODEC_ID_RAWVIDEO) {
        av_log(s, AV_LOG_ERROR, "Only supports one rawvideo stream\n");
        return AVERROR(EINVAL);
    }

    if (codec->pix_fmt != PIX_FMT_RGB24) {
        av_log(s, AV_LOG_ERROR,
               "Unsupported pixel format '%s', choose rgb24\n",
               av_get_pix_fmt_name(codec->pix_fmt));
        return AVERROR(EINVAL);
    }
    if (!s->filename || strcmp(s->filename, "pipe:") == 0) {
        sixel_output_file = stdout;
        c->output = LSOutputContext_create(putchar, printf);
    } else {
        sixel_output_file = fopen(s->filename, "w");
        c->output = LSOutputContext_create(sixel_putchar, sixel_printf);
    }
    if (!isatty(fileno(sixel_output_file))) {
        c->ignoredelay = 1;
    }

    c->dither = NULL;
    c->testdither = sixel_dither_create(c->reqcolors);

    if (c->row <= 1 && c->col <= 1)
        strcpy(reset_position, "\033[H");
    else
        sprintf(reset_position, "\033[%d;%dH", c->row, c->col);
    c->reset_position = reset_position;
    fprintf(sixel_output_file, "\033[?25l\0337");
    c->time_base = s->streams[0]->codec->time_base;
    c->time_frame = av_gettime() / av_q2d(c->time_base);

    if (c->fixedpal) {
        ret = prepare_static_palette(c, codec);
        if (ret != 0)
            return ret;
    }

    return ret;
}

static int sixel_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    SIXELContext * const c = s->priv_data;
    AVCodecContext * const codec = s->streams[0]->codec;
    int ret = 0;
    int64_t curtime, delay;
    struct timespec ts;
    LSImagePtr im;
    int late_threshold;

    if (!c->ignoredelay) {
        /* calculate the time of the next frame */
        c->time_frame += INT64_C(1000000);
        curtime = av_gettime();
        delay = c->time_frame * av_q2d(c->time_base) - curtime;
        if (delay <= 0) {
            if (c->dropframe) {
                /* late threshold of dropping this frame */
                late_threshold = INT64_C(-1000000) * av_q2d(c->time_base);
                if (delay < late_threshold)
                    return 0;
            }
        } else {
            ts.tv_sec = delay / 1000000;
            ts.tv_nsec = (delay % 1000000) * 1000;
            nanosleep(&ts, NULL);
        }
    }

    fprintf(sixel_output_file, "%s", c->reset_position);

    if (!c->fixedpal) {
        ret = prepare_dynamic_palette(c, codec, pkt);
        if (ret != 0)
            return ret;
    }
    /* create intermidiate bitmap image */
    im = sixel_create_image(pkt->data, codec->width, codec->height,
                            /* pixel depth */ 3,
                            /* pkt->data is borrowed reference */ 1,
                            c->dither);
    if (!im)
        return AVERROR(ENOMEM);
    c->dither->method_for_diffuse = c->diffuse;
    ret = sixel_apply_palette(im);
    if (ret != 0)
        return AVERROR(ret);
    LibSixel_LSImageToSixel(im, c->output);
    LSImage_destroy(im);
    fflush(stdout);
    return 0;
}

static int sixel_write_trailer(AVFormatContext *s)
{
    SIXELContext * const c = s->priv_data;

    if (sixel_output_file && sixel_output_file != stdout) {
        fclose(sixel_output_file);
        sixel_output_file = NULL;
    }
    if (c->output) {
        LSOutputContext_destroy(c->output);
        c->output = NULL;
    }
    if (c->testdither) {
        sixel_dither_unref(c->testdither);
        c->testdither = NULL;
    }
    if (c->dither) {
        sixel_dither_unref(c->dither);
        c->dither = NULL;
    }
    fprintf(sixel_output_file, "\0338\033[?25h");
    fflush(stdout);
    return 0;
}

#define OFFSET(x) offsetof(SIXELContext, x)
#define ENC AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "col",             "left position",          OFFSET(col),         AV_OPT_TYPE_INT,    {.i64 = 1},                1, 256,  ENC },
    { "row",             "top position",           OFFSET(row),         AV_OPT_TYPE_INT,    {.i64 = 1},                1, 256,  ENC },
    { "reqcolors",       "number of colors",       OFFSET(reqcolors),   AV_OPT_TYPE_INT,    {.i64 = 16},               2, 256,  ENC },
    { "fixedpal",        "use fixed palette",      OFFSET(fixedpal),    AV_OPT_TYPE_INT,    {.i64 = 0},                0, 1,    ENC, "fixedpal" },
    { "true",            NULL,                     0,                   AV_OPT_TYPE_CONST,  {.i64 = 1},                0, 0,    ENC, "fixedpal" },
    { "false",           NULL,                     0,                   AV_OPT_TYPE_CONST,  {.i64 = 0},                0, 0,    ENC, "fixedpal" },
    { "diffuse",         "dithering method",       OFFSET(diffuse),     AV_OPT_TYPE_INT,    {.i64 = DIFFUSE_ATKINSON}, 1, 6,    ENC, "diffuse" },
    { "none",            NULL,                     0,                   AV_OPT_TYPE_CONST,  {.i64 = DIFFUSE_NONE},     0, 0,    ENC, "diffuse" },
    { "fs",              NULL,                     0,                   AV_OPT_TYPE_CONST,  {.i64 = DIFFUSE_FS},       0, 0,    ENC, "diffuse" },
    { "atkinson",        NULL,                     0,                   AV_OPT_TYPE_CONST,  {.i64 = DIFFUSE_ATKINSON}, 0, 0,    ENC, "diffuse" },
    { "jajuni",          NULL,                     0,                   AV_OPT_TYPE_CONST,  {.i64 = DIFFUSE_JAJUNI},   0, 0,    ENC, "diffuse" },
    { "stucki",          NULL,                     0,                   AV_OPT_TYPE_CONST,  {.i64 = DIFFUSE_STUCKI},   0, 0,    ENC, "diffuse" },
    { "burkes",          NULL,                     0,                   AV_OPT_TYPE_CONST,  {.i64 = DIFFUSE_BURKES},   0, 0,    ENC, "diffuse" },
    { "scene-threshold", "scene change threshold", OFFSET(threshold),   AV_OPT_TYPE_INT,    {.i64 = 500},              0, 10000,ENC },
    { "dropframe",       "drop late frames",       OFFSET(dropframe),   AV_OPT_TYPE_INT,    {.i64 = 1},                0, 1,    ENC, "dropframe" },
    { "true",            NULL,                     0,                   AV_OPT_TYPE_CONST,  {.i64 = 1},                0, 0,    ENC, "dropframe" },
    { "false",           NULL,                     0,                   AV_OPT_TYPE_CONST,  {.i64 = 0},                0, 0,    ENC, "dropframe" },
    { "ignoredelay",     "ignore frame timestamp", OFFSET(ignoredelay), AV_OPT_TYPE_INT,    {.i64 = 0},                0, 1,    ENC, "ignoredelay" },
    { "true",            NULL,                     0,                   AV_OPT_TYPE_CONST,  {.i64 = 1},                0, 0,    ENC, "ignoredelay" },
    { "false",           NULL,                     0,                   AV_OPT_TYPE_CONST,  {.i64 = 0},                0, 0,    ENC, "ignoredelay" },
    { NULL },
};

static const AVClass sixel_class = {
    .class_name = "sixel_outdev",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
    .category   = AV_CLASS_CATEGORY_DEVICE_VIDEO_OUTPUT,
};

AVOutputFormat ff_sixel_muxer = {
    .name           = "sixel",
    .long_name      = NULL_IF_CONFIG_SMALL("SIXEL terminal device"),
    .priv_data_size = sizeof(SIXELContext),
    .audio_codec    = CODEC_ID_NONE,
    .video_codec    = CODEC_ID_RAWVIDEO,
    .write_header   = sixel_write_header,
    .write_packet   = sixel_write_packet,
    .write_trailer  = sixel_write_trailer,
    .flags          = AVFMT_NOFILE,
//    .flags          = AVFMT_NOFILE | AVFMT_VARIABLE_FPS | AVFMT_NOTIMESTAMPS,
    .priv_class     = &sixel_class,
};
