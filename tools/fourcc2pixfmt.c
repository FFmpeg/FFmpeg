/*
 * Copyright (c) 2012 Stefano Sabatini
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

#include "config.h"
#if HAVE_UNISTD_H
#include <unistd.h>             /* getopt */
#endif

#include "libavutil/pixdesc.h"
#include "libavcodec/avcodec.h"
#include "libavutil/common.h"
#include "libavcodec/raw.h"

#undef printf
#undef fprintf

#if !HAVE_GETOPT
#include "compat/getopt.c"
#endif

static void usage(void)
{
    printf("Show the relationships between rawvideo pixel formats and FourCC tags.\n");
    printf("usage: fourcc2pixfmt [OPTIONS]\n");
    printf("\n"
           "Options:\n"
           "-l                list the pixel format for each fourcc\n"
           "-L                list the fourccs for each pixel format\n"
           "-p PIX_FMT        given a pixel format, print the list of associated fourccs (one per line)\n"
           "-h                print this help\n");
}

static void print_pix_fmt_fourccs(enum AVPixelFormat pix_fmt, char sep)
{
    int i;

    for (i = 0; ff_raw_pix_fmt_tags[i].pix_fmt != AV_PIX_FMT_NONE; i++) {
        if (ff_raw_pix_fmt_tags[i].pix_fmt == pix_fmt) {
            char buf[32];
            av_get_codec_tag_string(buf, sizeof(buf), ff_raw_pix_fmt_tags[i].fourcc);
            printf("%s%c", buf, sep);
        }
    }
}

int main(int argc, char **argv)
{
    int i, list_fourcc_pix_fmt = 0, list_pix_fmt_fourccs = 0;
    const char *pix_fmt_name = NULL;
    char c;

    if (argc == 1) {
        usage();
        return 0;
    }

    while ((c = getopt(argc, argv, "hp:lL")) != -1) {
        switch (c) {
        case 'h':
            usage();
            return 0;
        case 'l':
            list_fourcc_pix_fmt = 1;
            break;
        case 'L':
            list_pix_fmt_fourccs = 1;
            break;
        case 'p':
            pix_fmt_name = optarg;
            break;
        case '?':
            usage();
            return 1;
        }
    }

    if (list_fourcc_pix_fmt) {
        for (i = 0; ff_raw_pix_fmt_tags[i].pix_fmt != AV_PIX_FMT_NONE; i++) {
            char buf[32];
            av_get_codec_tag_string(buf, sizeof(buf), ff_raw_pix_fmt_tags[i].fourcc);
            printf("%s: %s\n", buf, av_get_pix_fmt_name(ff_raw_pix_fmt_tags[i].pix_fmt));
        }
    }

    if (list_pix_fmt_fourccs) {
        for (i = 0; i < AV_PIX_FMT_NB; i++) {
            const AVPixFmtDescriptor *pix_desc = av_pix_fmt_desc_get(i);
            if (!pix_desc->name || pix_desc->flags & PIX_FMT_HWACCEL)
                continue;
            printf("%s: ", pix_desc->name);
            print_pix_fmt_fourccs(i, ' ');
            printf("\n");
        }
    }

    if (pix_fmt_name) {
        enum AVPixelFormat pix_fmt = av_get_pix_fmt(pix_fmt_name);
        if (pix_fmt == AV_PIX_FMT_NONE) {
            fprintf(stderr, "Invalid pixel format selected '%s'\n", pix_fmt_name);
            return 1;
        }
        print_pix_fmt_fourccs(pix_fmt, '\n');
    }

    return 0;
}
