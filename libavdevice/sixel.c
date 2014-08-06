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
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avdevice.h"

typedef struct SIXELContext {
    AVClass *class;
    AVFormatContext *ctx;
    char *window_size;
    int colors;
    LSOutputContextPtr output;
    LSImagePtr im;
    unsigned char *palette;
    unsigned short *cachetable;
} SIXELContext;


static int sixel_write_header(AVFormatContext *s)
{
    SIXELContext *c = s->priv_data;
    AVCodecContext *encctx = s->streams[0]->codec;

    if (s->nb_streams > 1
        || encctx->codec_type != AVMEDIA_TYPE_VIDEO
        || encctx->codec_id   != CODEC_ID_RAWVIDEO) {
        av_log(s, AV_LOG_ERROR, "Only supports one rawvideo stream\n");
        return AVERROR(EINVAL);
    }

    if (encctx->pix_fmt != PIX_FMT_RGB24) {
        av_log(s, AV_LOG_ERROR,
               "Unsupported pixel format '%s', choose rgb24\n",
               av_get_pix_fmt_name(encctx->pix_fmt));
        return AVERROR(EINVAL);
    }

    c->palette = NULL;
    c->window_size = NULL;
    c->colors = 16;
    c->ctx = s;
    c->output = LSOutputContext_create(putchar, printf);
    c->cachetable = malloc((1 << 3 * 5) * sizeof(unsigned short));
    if (c->cachetable == 0) {
        return AVERROR(ENOMEM);
    }
    memset(c->cachetable, 0, (1 << 3 * 5) * sizeof(unsigned short));

    printf("\033[?25l\0337");

    return 0;
}

static int sixel_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    SIXELContext * const c = s->priv_data;
    AVCodecContext * const encctx = s->streams[0]->codec;
    int sx = encctx->width;
    int sy = encctx->height;
    int i;
    unsigned char *pixels = pkt->data;
    int ret = 0;

    if (c->im == NULL) {
        c->im = LSImage_create(sx, sy, 1, c->colors);
    }
    if (c->palette == NULL) {
        c->palette = LSQ_MakePalette(pixels, sx, sy, 3,
                                     c->colors, &c->colors, NULL,
                                     LARGE_NORM, REP_CENTER_BOX, QUALITY_HIGH);
        for (i = 0; i < c->colors; i++) {
            LSImage_setpalette(c->im, i,
                               c->palette[i * 3],
                               c->palette[i * 3 + 1],
                               c->palette[i * 3 + 2]);
        }
    }
    ret = LSQ_ApplyPalette(pixels, sx, sy, 3, c->palette, c->colors,
                           DIFFUSE_ATKINSON, /* foptimize */ 1, c->cachetable,
                           c->im->pixels);
    if (ret != 0) {
        return ret;
    }
    printf("\033[H");
    LibSixel_LSImageToSixel(c->im, c->output);
    fflush(stdout);
    return 0;
}

static int sixel_write_trailer(AVFormatContext *s)
{
    SIXELContext * const c = s->priv_data;

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

    printf("\0338\033[?25h");
    fflush(stdout);
    return 0;
}

static const AVOption options[] = {
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
    .flags          = AVFMT_NOFILE | AVFMT_NOTIMESTAMPS,
    .priv_class     = &sixel_class,
};
