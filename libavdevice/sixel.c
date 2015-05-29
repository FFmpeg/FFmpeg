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

#include <stdio.h>
#include <unistd.h>
#include <sys/signal.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sixel.h>
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avdevice.h"
#include "libavutil/time.h"


typedef struct SIXELContext {
    AVClass *class;
    AVRational time_base;   /* time base */
    int64_t    time_frame;  /* current time */
    AVRational framerate;
    int row;
    int col;
    int reqcolors;
    sixel_output_t *output;
    sixel_dither_t *dither;
    sixel_dither_t *testdither;
    int fixedpal;
    enum methodForDiffuse diffuse;
    int threshold;
    int dropframe;
    int ignoredelay;
} SIXELContext;

static FILE *sixel_output_file = NULL;

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
    static int previous_histgram_colors = 0;
    int histgram_colors = 0;
    int palette_colors = 0;
    unsigned char const* palette;

    histgram_colors = sixel_dither_get_num_of_histogram_colors(c->testdither);

    if (c->dither == NULL)
        goto detected;

    /* detect scene change if number of colors increses 20% */
    if (previous_histgram_colors * 6 < histgram_colors * 5)
        goto detected;

    /* detect scene change if number of colors decreses 20% */
    if (previous_histgram_colors * 4 > histgram_colors * 5)
        goto detected;

    palette_colors = sixel_dither_get_num_of_palette_colors(c->testdither);
    palette = sixel_dither_get_palette(c->testdither);

    /* compare color difference between current
     * palette and previous one */
    for (i = 0; i < palette_colors; i++) {
        r += palette[i * 3 + 0];
        g += palette[i * 3 + 1];
        b += palette[i * 3 + 2];
    }
    score = (r - average_r) * (r - average_r)
          + (g - average_g) * (g - average_g)
          + (b - average_b) * (b - average_b);
    if (score > c->threshold * palette_colors
                             * palette_colors)
        goto detected;

    return 0;

detected:
    previous_histgram_colors = histgram_colors;
    average_r = r;
    average_g = g;
    average_b = b;
    return 1;
}

static int prepare_static_palette(SIXELContext *const c,
                                  AVCodecContext *const codec)
{
    if (c->dither) {
        sixel_dither_set_body_only(c->dither, 1);
    } else {
        c->dither = sixel_dither_get(BUILTIN_XTERM256);
        if (c->dither == NULL)
            return (-1);
        sixel_dither_set_diffusion_type(c->dither, c->diffuse);
    }
    return 0;
}


static void scroll_on_demand(int pixelheight)
{
    struct winsize size = {0, 0, 0, 0};
    struct termios old_termios;
    struct termios new_termios;
    int row = 0;
    int col = 0;
    int cellheight;
    int scroll;
    fd_set rfds;
    struct timeval tv;
    int ret = 0;

    ioctl(STDOUT_FILENO, TIOCGWINSZ, &size);
    if (size.ws_ypixel <= 0) {
        fprintf(sixel_output_file, "\033[H\0337");
        return;
    }
    /* set the terminal to cbreak mode */
    tcgetattr(STDIN_FILENO, &old_termios);
    memcpy(&new_termios, &old_termios, sizeof(old_termios));
    new_termios.c_lflag &= ~(ECHO | ICANON);
    new_termios.c_cc[VMIN] = 1;
    new_termios.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &new_termios);

    /* request cursor position report */
    fprintf(sixel_output_file, "\033[6n");
    /* wait 1 sec */
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    FD_ZERO(&rfds);
    FD_SET(STDIN_FILENO, &rfds);
    ret = select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv);
    if (ret != (-1)) {
        if (scanf("\033[%d;%dR", &row, &col) == 2) {
            cellheight = pixelheight * size.ws_row / size.ws_ypixel + 1;
            scroll = cellheight + row - size.ws_row + 1;
            if (scroll > 0) {
                fprintf(sixel_output_file, "\033[%dS\033[%dA", scroll, scroll);
            }
            fprintf(sixel_output_file, "\0337");
        } else {
            fprintf(sixel_output_file, "\033[H\0337");
        }
    }

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &old_termios);
}


static int prepare_dynamic_palette(SIXELContext *const c,
                                   AVCodecContext *const codec,
                                   AVPacket *const pkt)
{
    int ret;

    /* create histgram and construct color palette
     * with median cut algorithm. */
    ret = sixel_dither_initialize(c->testdither, pkt->data,
                                  codec->width, codec->height, 3,
                                  LARGE_NORM, REP_CENTER_BOX,
                                  QUALITY_LOW);
    if (ret != 0)
        return (-1);

    /* check whether the scence is changed. use old palette
     * if scene is not changed. */
    if (detect_scene_change(c)) {
        if (c->dither)
            sixel_dither_unref(c->dither);
        c->dither = c->testdither;
        c->testdither = sixel_dither_create(c->reqcolors);
        sixel_dither_set_diffusion_type(c->dither, c->diffuse);
    } else {
        sixel_dither_set_body_only(c->dither, 1);
    }
    return 0;
}

static int sixel_write(char *data, int size, void *priv)
{
    return fwrite(data, 1, size, (FILE *)priv);
}

static int sixel_write_header(AVFormatContext *s)
{
    SIXELContext *c = s->priv_data;
    AVCodecContext *codec = s->streams[0]->codec;
    int ret = 0;

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
        c->output = sixel_output_create(sixel_write, stdout);
    } else {
        sixel_output_file = fopen(s->filename, "w");
        c->output = sixel_output_create(sixel_write, sixel_output_file);
    }
    if (isatty(fileno(sixel_output_file))) {
        fprintf(sixel_output_file, "\033[?25l");      /* hide cursor */
    } else {
        c->ignoredelay = 1;
    }

    /* don't use private color registers for each frame. */
    fprintf(sixel_output_file, "\033[?1070l");

    c->dither = NULL;
    c->testdither = sixel_dither_create(c->reqcolors);

    c->time_base = s->streams[0]->codec->time_base;
    c->time_frame = av_gettime() / av_q2d(c->time_base);

    return ret;
}

static int sixel_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    SIXELContext * const c = s->priv_data;
    AVCodecContext * const codec = s->streams[0]->codec;
    int ret = 0;
    int64_t curtime, delay;
    struct timespec ts;
    int late_threshold;
    static int dirty = 0;

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

    if (dirty == 0) {
        if (c->row <= 1 && c->col <= 1)
            scroll_on_demand(codec->height);
        else
            fprintf(sixel_output_file, "\033[%d;%dH\0337", c->row, c->col);
        dirty = 1;
    }
    fprintf(sixel_output_file, "\0338");

    if (c->fixedpal) {
        ret = prepare_static_palette(c, codec);
        if (ret != 0)
            return ret;
    } else {
        ret = prepare_dynamic_palette(c, codec, pkt);
        if (ret != 0)
            return ret;
    }
    ret = sixel_encode(pkt->data, codec->width, codec->height,
                      /* pixel depth */ 3, c->dither, c->output);
    if (ret != 0)
        return AVERROR(ret);
    fflush(sixel_output_file);
    return 0;
}

static int sixel_write_trailer(AVFormatContext *s)
{
    SIXELContext * const c = s->priv_data;

    if (isatty(fileno(sixel_output_file))) {
        fprintf(sixel_output_file,
                "\033\\"      /* terminate DCS sequence */
                "\033[?25h"); /* show cursor */
    }

    fflush(sixel_output_file);
    if (sixel_output_file && sixel_output_file != stdout) {
        fclose(sixel_output_file);
        sixel_output_file = NULL;
    }
    if (c->output) {
        sixel_output_unref(c->output);
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
    .flags          = AVFMT_NOFILE, /* | AVFMT_VARIABLE_FPS, */
    .priv_class     = &sixel_class,
};
