/*
 * Raw Video Codec
 * Copyright (c) 2001 Fabrice Bellard
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
 * Raw Video Codec
 */

#include "avcodec.h"
#include "raw.h"
#include "libavutil/common.h"

const PixelFormatTag ff_raw_pix_fmt_tags[] = {
    { PIX_FMT_YUV420P, MKTAG('I', '4', '2', '0') }, /* Planar formats */
    { PIX_FMT_YUV420P, MKTAG('I', 'Y', 'U', 'V') },
    { PIX_FMT_YUV420P, MKTAG('Y', 'V', '1', '2') },
    { PIX_FMT_YUV410P, MKTAG('Y', 'U', 'V', '9') },
    { PIX_FMT_YUV410P, MKTAG('Y', 'V', 'U', '9') },
    { PIX_FMT_YUV411P, MKTAG('Y', '4', '1', 'B') },
    { PIX_FMT_YUV422P, MKTAG('Y', '4', '2', 'B') },
    { PIX_FMT_YUV422P, MKTAG('P', '4', '2', '2') },
    { PIX_FMT_YUV422P, MKTAG('Y', 'V', '1', '6') },
    /* yuvjXXX formats are deprecated hacks specific to libav*,
       they are identical to yuvXXX  */
    { PIX_FMT_YUVJ420P, MKTAG('I', '4', '2', '0') }, /* Planar formats */
    { PIX_FMT_YUVJ420P, MKTAG('I', 'Y', 'U', 'V') },
    { PIX_FMT_YUVJ420P, MKTAG('Y', 'V', '1', '2') },
    { PIX_FMT_YUVJ422P, MKTAG('Y', '4', '2', 'B') },
    { PIX_FMT_YUVJ422P, MKTAG('P', '4', '2', '2') },
    { PIX_FMT_GRAY8,    MKTAG('Y', '8', '0', '0') },
    { PIX_FMT_GRAY8,    MKTAG('Y', '8', ' ', ' ') },

    { PIX_FMT_YUYV422, MKTAG('Y', 'U', 'Y', '2') }, /* Packed formats */
    { PIX_FMT_YUYV422, MKTAG('Y', '4', '2', '2') },
    { PIX_FMT_YUYV422, MKTAG('V', '4', '2', '2') },
    { PIX_FMT_YUYV422, MKTAG('V', 'Y', 'U', 'Y') },
    { PIX_FMT_YUYV422, MKTAG('Y', 'U', 'N', 'V') },
    { PIX_FMT_UYVY422, MKTAG('U', 'Y', 'V', 'Y') },
    { PIX_FMT_UYVY422, MKTAG('H', 'D', 'Y', 'C') },
    { PIX_FMT_UYVY422, MKTAG('U', 'Y', 'N', 'V') },
    { PIX_FMT_UYVY422, MKTAG('U', 'Y', 'N', 'Y') },
    { PIX_FMT_UYVY422, MKTAG('u', 'y', 'v', '1') },
    { PIX_FMT_UYVY422, MKTAG('2', 'V', 'u', '1') },
    { PIX_FMT_UYVY422, MKTAG('A', 'V', 'R', 'n') }, /* Avid AVI Codec 1:1 */
    { PIX_FMT_UYVY422, MKTAG('A', 'V', '1', 'x') }, /* Avid 1:1x */
    { PIX_FMT_UYVY422, MKTAG('A', 'V', 'u', 'p') },
    { PIX_FMT_UYVY422, MKTAG('V', 'D', 'T', 'Z') }, /* SoftLab-NSK VideoTizer */
    { PIX_FMT_UYVY422, MKTAG('a', 'u', 'v', '2') },
    { PIX_FMT_UYVY422, MKTAG('c', 'y', 'u', 'v') }, /* CYUV is also Creative YUV */
    { PIX_FMT_UYYVYY411, MKTAG('Y', '4', '1', '1') },
    { PIX_FMT_GRAY8,   MKTAG('G', 'R', 'E', 'Y') },
    { PIX_FMT_NV12,    MKTAG('N', 'V', '1', '2') },
    { PIX_FMT_NV21,    MKTAG('N', 'V', '2', '1') },

    /* nut */
    { PIX_FMT_RGB555LE, MKTAG('R', 'G', 'B', 15) },
    { PIX_FMT_BGR555LE, MKTAG('B', 'G', 'R', 15) },
    { PIX_FMT_RGB565LE, MKTAG('R', 'G', 'B', 16) },
    { PIX_FMT_BGR565LE, MKTAG('B', 'G', 'R', 16) },
    { PIX_FMT_RGB555BE, MKTAG(15 , 'B', 'G', 'R') },
    { PIX_FMT_BGR555BE, MKTAG(15 , 'R', 'G', 'B') },
    { PIX_FMT_RGB565BE, MKTAG(16 , 'B', 'G', 'R') },
    { PIX_FMT_BGR565BE, MKTAG(16 , 'R', 'G', 'B') },
    { PIX_FMT_RGB444LE, MKTAG('R', 'G', 'B', 12) },
    { PIX_FMT_BGR444LE, MKTAG('B', 'G', 'R', 12) },
    { PIX_FMT_RGB444BE, MKTAG(12 , 'B', 'G', 'R') },
    { PIX_FMT_BGR444BE, MKTAG(12 , 'R', 'G', 'B') },
    { PIX_FMT_RGBA64LE, MKTAG('R', 'B', 'A', 64 ) },
    { PIX_FMT_BGRA64LE, MKTAG('B', 'R', 'A', 64 ) },
    { PIX_FMT_RGBA64BE, MKTAG(64 , 'R', 'B', 'A') },
    { PIX_FMT_BGRA64BE, MKTAG(64 , 'B', 'R', 'A') },
    { PIX_FMT_RGBA,     MKTAG('R', 'G', 'B', 'A') },
    { PIX_FMT_RGB0,     MKTAG('R', 'G', 'B',  0 ) },
    { PIX_FMT_BGRA,     MKTAG('B', 'G', 'R', 'A') },
    { PIX_FMT_BGR0,     MKTAG('B', 'G', 'R',  0 ) },
    { PIX_FMT_ABGR,     MKTAG('A', 'B', 'G', 'R') },
    { PIX_FMT_0BGR,     MKTAG( 0 , 'B', 'G', 'R') },
    { PIX_FMT_ARGB,     MKTAG('A', 'R', 'G', 'B') },
    { PIX_FMT_0RGB,     MKTAG( 0 , 'R', 'G', 'B') },
    { PIX_FMT_RGB24,    MKTAG('R', 'G', 'B', 24 ) },
    { PIX_FMT_BGR24,    MKTAG('B', 'G', 'R', 24 ) },
    { PIX_FMT_YUV411P,  MKTAG('4', '1', '1', 'P') },
    { PIX_FMT_YUV422P,  MKTAG('4', '2', '2', 'P') },
    { PIX_FMT_YUVJ422P, MKTAG('4', '2', '2', 'P') },
    { PIX_FMT_YUV440P,  MKTAG('4', '4', '0', 'P') },
    { PIX_FMT_YUVJ440P, MKTAG('4', '4', '0', 'P') },
    { PIX_FMT_YUV444P,  MKTAG('4', '4', '4', 'P') },
    { PIX_FMT_YUVJ444P, MKTAG('4', '4', '4', 'P') },
    { PIX_FMT_MONOWHITE,MKTAG('B', '1', 'W', '0') },
    { PIX_FMT_MONOBLACK,MKTAG('B', '0', 'W', '1') },
    { PIX_FMT_BGR8,     MKTAG('B', 'G', 'R',  8 ) },
    { PIX_FMT_RGB8,     MKTAG('R', 'G', 'B',  8 ) },
    { PIX_FMT_BGR4,     MKTAG('B', 'G', 'R',  4 ) },
    { PIX_FMT_RGB4,     MKTAG('R', 'G', 'B',  4 ) },
    { PIX_FMT_RGB4_BYTE,MKTAG('B', '4', 'B', 'Y') },
    { PIX_FMT_BGR4_BYTE,MKTAG('R', '4', 'B', 'Y') },
    { PIX_FMT_RGB48LE,  MKTAG('R', 'G', 'B', 48 ) },
    { PIX_FMT_RGB48BE,  MKTAG( 48, 'R', 'G', 'B') },
    { PIX_FMT_BGR48LE,  MKTAG('B', 'G', 'R', 48 ) },
    { PIX_FMT_BGR48BE,  MKTAG( 48, 'B', 'G', 'R') },
    { PIX_FMT_GRAY16LE,    MKTAG('Y', '1',  0 , 16 ) },
    { PIX_FMT_GRAY16BE,    MKTAG(16 ,  0 , '1', 'Y') },
    { PIX_FMT_YUV420P10LE, MKTAG('Y', '3', 11 , 10 ) },
    { PIX_FMT_YUV420P10BE, MKTAG(10 , 11 , '3', 'Y') },
    { PIX_FMT_YUV422P10LE, MKTAG('Y', '3', 10 , 10 ) },
    { PIX_FMT_YUV422P10BE, MKTAG(10 , 10 , '3', 'Y') },
    { PIX_FMT_YUV444P10LE, MKTAG('Y', '3',  0 , 10 ) },
    { PIX_FMT_YUV444P10BE, MKTAG(10 ,  0 , '3', 'Y') },
    { PIX_FMT_YUV420P12LE, MKTAG('Y', '3', 11 , 12 ) },
    { PIX_FMT_YUV420P12BE, MKTAG(12 , 11 , '3', 'Y') },
    { PIX_FMT_YUV422P12LE, MKTAG('Y', '3', 10 , 12 ) },
    { PIX_FMT_YUV422P12BE, MKTAG(12 , 10 , '3', 'Y') },
    { PIX_FMT_YUV444P12LE, MKTAG('Y', '3',  0 , 12 ) },
    { PIX_FMT_YUV444P12BE, MKTAG(12 ,  0 , '3', 'Y') },
    { PIX_FMT_YUV420P14LE, MKTAG('Y', '3', 11 , 14 ) },
    { PIX_FMT_YUV420P14BE, MKTAG(14 , 11 , '3', 'Y') },
    { PIX_FMT_YUV422P14LE, MKTAG('Y', '3', 10 , 14 ) },
    { PIX_FMT_YUV422P14BE, MKTAG(14 , 10 , '3', 'Y') },
    { PIX_FMT_YUV444P14LE, MKTAG('Y', '3',  0 , 14 ) },
    { PIX_FMT_YUV444P14BE, MKTAG(14 ,  0 , '3', 'Y') },
    { PIX_FMT_YUV420P16LE, MKTAG('Y', '3', 11 , 16 ) },
    { PIX_FMT_YUV420P16BE, MKTAG(16 , 11 , '3', 'Y') },
    { PIX_FMT_YUV422P16LE, MKTAG('Y', '3', 10 , 16 ) },
    { PIX_FMT_YUV422P16BE, MKTAG(16 , 10 , '3', 'Y') },
    { PIX_FMT_YUV444P16LE, MKTAG('Y', '3',  0 , 16 ) },
    { PIX_FMT_YUV444P16BE, MKTAG(16 ,  0 , '3', 'Y') },
    { PIX_FMT_YUVA420P,    MKTAG('Y', '4', 11 ,  8 ) },
    { PIX_FMT_YUVA422P,    MKTAG('Y', '4', 10 ,  8 ) },
    { PIX_FMT_YUVA444P,    MKTAG('Y', '4',  0 ,  8 ) },
    { PIX_FMT_GRAY8A,      MKTAG('Y', '2',  0 ,  8 ) },

    /* quicktime */
    { PIX_FMT_YUV420P, MKTAG('R', '4', '2', '0') }, /* Radius DV YUV PAL */
    { PIX_FMT_YUV411P, MKTAG('R', '4', '1', '1') }, /* Radius DV YUV NTSC */
    { PIX_FMT_UYVY422, MKTAG('2', 'v', 'u', 'y') },
    { PIX_FMT_UYVY422, MKTAG('2', 'V', 'u', 'y') },
    { PIX_FMT_UYVY422, MKTAG('A', 'V', 'U', 'I') }, /* FIXME merge both fields */
    { PIX_FMT_UYVY422, MKTAG('b', 'x', 'y', 'v') },
    { PIX_FMT_YUYV422, MKTAG('y', 'u', 'v', '2') },
    { PIX_FMT_YUYV422, MKTAG('y', 'u', 'v', 's') },
    { PIX_FMT_YUYV422, MKTAG('D', 'V', 'O', 'O') }, /* Digital Voodoo SD 8 Bit */
    { PIX_FMT_RGB555LE,MKTAG('L', '5', '5', '5') },
    { PIX_FMT_RGB565LE,MKTAG('L', '5', '6', '5') },
    { PIX_FMT_RGB565BE,MKTAG('B', '5', '6', '5') },
    { PIX_FMT_BGR24,   MKTAG('2', '4', 'B', 'G') },
    { PIX_FMT_BGR24,   MKTAG('b', 'x', 'b', 'g') },
    { PIX_FMT_BGRA,    MKTAG('B', 'G', 'R', 'A') },
    { PIX_FMT_RGBA,    MKTAG('R', 'G', 'B', 'A') },
    { PIX_FMT_RGB24,   MKTAG('b', 'x', 'r', 'g') },
    { PIX_FMT_ABGR,    MKTAG('A', 'B', 'G', 'R') },
    { PIX_FMT_GRAY16BE,MKTAG('b', '1', '6', 'g') },
    { PIX_FMT_RGB48BE, MKTAG('b', '4', '8', 'r') },

    /* special */
    { PIX_FMT_RGB565LE,MKTAG( 3 ,  0 ,  0 ,  0 ) }, /* flipped RGB565LE */
    { PIX_FMT_YUV444P, MKTAG('Y', 'V', '2', '4') }, /* YUV444P, swapped UV */
    { PIX_FMT_YUYV422, MKTAG('Y', 'V', 'Y', 'U') }, /* YUYV, swapped UV */

    { PIX_FMT_NONE, 0 },
};

unsigned int avcodec_pix_fmt_to_codec_tag(enum PixelFormat fmt)
{
    const PixelFormatTag *tags = ff_raw_pix_fmt_tags;
    while (tags->pix_fmt >= 0) {
        if (tags->pix_fmt == fmt)
            return tags->fourcc;
        tags++;
    }
    return 0;
}

#ifdef TEST

#include <unistd.h>             /* getopt */
#include "libavutil/pixdesc.h"

#undef printf
#undef fprintf

static void usage(void)
{
    printf("\n"
           "Options:\n"
           "-l                list the pixel format for each fourcc\n"
           "-L                list the fourccs for each pixel format\n"
           "-p PIX_FMT        given a pixel format, print the list of associated fourccs\n"
           "-h                print this help\n");
}

static void print_pix_fmt_fourccs(enum PixelFormat pix_fmt, char sep)
{
    int i;

    for (i = 0; i < FF_ARRAY_ELEMS(ff_raw_pix_fmt_tags); i++) {
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
        /* print a list of pixel format / fourcc */
        for (i = 0; i < FF_ARRAY_ELEMS(ff_raw_pix_fmt_tags); i++) {
            char buf[32];
            av_get_codec_tag_string(buf, sizeof(buf), ff_raw_pix_fmt_tags[i].fourcc);
            if (ff_raw_pix_fmt_tags[i].pix_fmt != PIX_FMT_NONE)
                printf("%s: %s\n", av_get_pix_fmt_name(ff_raw_pix_fmt_tags[i].pix_fmt), buf);
        }
    }

    if (list_pix_fmt_fourccs) {
        for (i = 0; i < PIX_FMT_NB; i++) {
            const AVPixFmtDescriptor *pix_desc = &av_pix_fmt_descriptors[i];
            if (!pix_desc->name || pix_desc->flags & PIX_FMT_HWACCEL)
                continue;
            printf("%s: ", pix_desc->name);
            print_pix_fmt_fourccs(i, ' ');
            printf("\n");
        }
    }

    if (pix_fmt_name) {
        enum PixelFormat pix_fmt = av_get_pix_fmt(pix_fmt_name);
        if (pix_fmt == PIX_FMT_NONE) {
            fprintf(stderr, "Invalid pixel format selected '%s'\n", pix_fmt_name);
            return 1;
        }
        print_pix_fmt_fourccs(pix_fmt, '\n');
    }

    return 0;
}
#endif
