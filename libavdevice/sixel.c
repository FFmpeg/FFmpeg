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
    char *window_title;
    char *reset_position;
    int row;
    int col;
    int reqcolors;
    int colors;
    LSOutputContextPtr output;
    LSImagePtr im;
    unsigned char *palette;
    unsigned short *cachetable;
} SIXELContext;


static int sixel_write_header(AVFormatContext *s)
{
    SIXELContext *c = s->priv_data;
    AVCodecContext *codec = s->streams[0]->codec;

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
    c->palette = NULL;
    c->output = LSOutputContext_create(putchar, printf);
    c->im = LSImage_create(codec->width, codec->height, 1, c->reqcolors);
    c->cachetable = av_calloc(1 << 3 * 5, sizeof(unsigned short));
    if (c->cachetable == 0) {
        return AVERROR(ENOMEM);
    }
    c->reset_position = malloc(64);
    if (c->row <= 1 && c->col <= 1) {
        strcpy(c->reset_position, "\033[H");
    } else {
        sprintf(c->reset_position, "\033[%d;%dH", c->row, c->col);
    }
    if (c->window_title) {
        printf("\033]0;%s\007", c->window_title);
    }
    printf("\033[?25l\0337");
    c->time_base = s->streams[0]->codec->time_base;
    c->time_frame = av_gettime() / av_q2d(c->time_base);

    return 0;
}

static int sixel_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    SIXELContext * const c = s->priv_data;
    AVCodecContext * const codec = s->streams[0]->codec;
    int ret = 0;
    int i;
    int64_t curtime, delay;
    struct timespec ts;

    /* Calculate the time of the next frame */
    c->time_frame += INT64_C(1000000);

    /* wait based on the frame rate */
    for(;;) {
        curtime = av_gettime();
        delay = c->time_frame * av_q2d(c->time_base) - curtime;
        if (delay <= 0) {
            if (delay < INT64_C(-1000000) * av_q2d(c->time_base)) {
                return 0;
            }
            break;
        }
        ts.tv_sec = delay / 1000000;
        ts.tv_nsec = (delay % 1000000) * 1000;
        nanosleep(&ts, NULL);
    }

    if (c->palette == NULL) {
        c->palette = LSQ_MakePalette(pkt->data, codec->width, codec->height,
                                     3, c->reqcolors, &c->colors, NULL,
                                     LARGE_NORM, REP_CENTER_BOX, QUALITY_LOW);
        for (i = 0; i < c->colors; i++) {
            LSImage_setpalette(c->im, i,
                               c->palette[i * 3],
                               c->palette[i * 3 + 1],
                               c->palette[i * 3 + 2]);
        }
    }
    ret = LSQ_ApplyPalette(pkt->data, codec->width, codec->height, 3,
                           c->palette, c->reqcolors,
                           DIFFUSE_ATKINSON, /* foptimize */ 1,
                           c->cachetable, c->im->pixels);
    if (ret != 0) {
        return ret;
    }
    printf("%s", c->reset_position);
    LibSixel_LSImageToSixel(c->im, c->output);
    fflush(stdout);
    return 0;
}

static int sixel_write_trailer(AVFormatContext *s)
{
    SIXELContext * const c = s->priv_data;

    if (c->window_title) {
        printf("\033]0;\007");
    }

    if (c->output) {
        LSOutputContext_destroy(c->output);
        c->output = NULL;
    }
    if (c->im) {
        LSImage_destroy(c->im);
        c->im = NULL;
    }
    if (c->palette) {
        free(c->palette);
        c->palette = NULL;
    }
    if (c->cachetable) {
        free(c->cachetable);
        c->cachetable = NULL;
    }
    if (c->reset_position) {
        free(c->reset_position);
        c->reset_position = NULL;
    }
    printf("\0338\033[?25h");
    fflush(stdout);
    return 0;
}

#define OFFSET(x) offsetof(SIXELContext, x)
#define ENC AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "window_title", "set window title",  OFFSET(window_title), AV_OPT_TYPE_STRING,     {.str = NULL}, 0, 0,   ENC },
    { "col",          "left position",     OFFSET(col),          AV_OPT_TYPE_INT,        {.i64 = 1},    1, 256, ENC },
    { "row",          "top position",      OFFSET(row),          AV_OPT_TYPE_INT,        {.i64 = 1},    1, 256, ENC },
    { "reqcolors",    "number of colors",  OFFSET(reqcolors),    AV_OPT_TYPE_INT,        {.i64 = 16},   2, 256, ENC },
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
    .priv_class     = &sixel_class,
};
