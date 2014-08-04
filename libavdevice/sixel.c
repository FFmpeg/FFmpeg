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
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avdevice.h"

typedef struct SIXELContext {
    AVClass         *class;
    AVFormatContext *ctx;
    char            *window_size;
    char            *colors;
} SIXELContext;

static int sixel_write_trailer(AVFormatContext *s)
{
    return 0;
}

static int sixel_write_header(AVFormatContext *s)
{
    SIXELContext *c = s->priv_data;
    AVCodecContext *encctx = s->streams[0]->codec;

    c->ctx = s;

    if (   s->nb_streams > 1
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

    return 0;
}

static int sixel_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    SIXELContext *c = s->priv_data;
    AVCodecContext *encctx = s->streams[0]->codec;
    int sx = encctx->width;
    int sy = encctx->height;
    int reqcolors = 16;
    int ncolors;
    int origcolors;
    int i;
    unsigned char *pixels = pkt->data;
    unsigned char *palette = NULL;
    unsigned char *data = NULL;

    LSOutputContextPtr context = LSOutputContext_create(putchar, printf);
    palette = LSQ_MakePalette(pixels, sx, sy, 3,
                              reqcolors,
                              &ncolors,
                              &origcolors,
                              LARGE_NORM,
                              REP_CENTER_BOX,
                              QUALITY_LOW);
    data = LSQ_ApplyPalette(pixels, sx, sy, 3,
                            palette, ncolors,
                            DIFFUSE_FS,
                            /* foptimize */ 1);
    LSImagePtr im = LSImage_create(sx, sy, 1, ncolors);
    for (i = 0; i < ncolors; i++) {
        LSImage_setpalette(im, i,
                           palette[i * 3],
                           palette[i * 3 + 1],
                           palette[i * 3 + 2]);
    }
    LSImage_setpixels(im, data);
    printf("\033[H");
    LibSixel_LSImageToSixel(im, context);
    LSImage_destroy(im);
    fflush(stdout);
    return 0;
}

#define OFFSET(x) offsetof(SIXELContext,x)
#define ENC AV_OPT_FLAG_ENCODING_PARAM

static const AVOption options[] = {
    { NULL },
};

static const AVClass sixel_class = {
    .class_name = "sixel_outdev",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
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
