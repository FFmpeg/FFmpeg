/*
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
 * misc parsing utilities for libavcore
 */

#include "parseutils.h"
#include "libavutil/avutil.h"

typedef struct {
    const char *abbr;
    int width, height;
} VideoFrameSizeAbbr;

typedef struct {
    const char *abbr;
    int rate_num, rate_den;
} VideoFrameRateAbbr;

static const VideoFrameSizeAbbr video_frame_size_abbrs[] = {
    { "ntsc",      720, 480 },
    { "pal",       720, 576 },
    { "qntsc",     352, 240 }, /* VCD compliant NTSC */
    { "qpal",      352, 288 }, /* VCD compliant PAL */
    { "sntsc",     640, 480 }, /* square pixel NTSC */
    { "spal",      768, 576 }, /* square pixel PAL */
    { "film",      352, 240 },
    { "ntsc-film", 352, 240 },
    { "sqcif",     128,  96 },
    { "qcif",      176, 144 },
    { "cif",       352, 288 },
    { "4cif",      704, 576 },
    { "16cif",    1408,1152 },
    { "qqvga",     160, 120 },
    { "qvga",      320, 240 },
    { "vga",       640, 480 },
    { "svga",      800, 600 },
    { "xga",      1024, 768 },
    { "uxga",     1600,1200 },
    { "qxga",     2048,1536 },
    { "sxga",     1280,1024 },
    { "qsxga",    2560,2048 },
    { "hsxga",    5120,4096 },
    { "wvga",      852, 480 },
    { "wxga",     1366, 768 },
    { "wsxga",    1600,1024 },
    { "wuxga",    1920,1200 },
    { "woxga",    2560,1600 },
    { "wqsxga",   3200,2048 },
    { "wquxga",   3840,2400 },
    { "whsxga",   6400,4096 },
    { "whuxga",   7680,4800 },
    { "cga",       320, 200 },
    { "ega",       640, 350 },
    { "hd480",     852, 480 },
    { "hd720",    1280, 720 },
    { "hd1080",   1920,1080 },
};

static const VideoFrameRateAbbr video_frame_rate_abbrs[]= {
    { "ntsc",      30000, 1001 },
    { "pal",          25,    1 },
    { "qntsc",     30000, 1001 }, /* VCD compliant NTSC */
    { "qpal",         25,    1 }, /* VCD compliant PAL */
    { "sntsc",     30000, 1001 }, /* square pixel NTSC */
    { "spal",         25,    1 }, /* square pixel PAL */
    { "film",         24,    1 },
    { "ntsc-film", 24000, 1001 },
};

int av_parse_video_size(int *width_ptr, int *height_ptr, const char *str)
{
    int i;
    int n = FF_ARRAY_ELEMS(video_frame_size_abbrs);
    char *p;
    int frame_width = 0, frame_height = 0;

    for (i = 0; i < n; i++) {
        if (!strcmp(video_frame_size_abbrs[i].abbr, str)) {
            frame_width = video_frame_size_abbrs[i].width;
            frame_height = video_frame_size_abbrs[i].height;
            break;
        }
    }
    if (i == n) {
        p = str;
        frame_width = strtol(p, &p, 10);
        if (*p)
            p++;
        frame_height = strtol(p, &p, 10);
    }
    if (frame_width <= 0 || frame_height <= 0)
        return AVERROR(EINVAL);
    *width_ptr  = frame_width;
    *height_ptr = frame_height;
    return 0;
}

int av_parse_video_rate(AVRational *frame_rate, const char *arg)
{
    int i;
    int n = FF_ARRAY_ELEMS(video_frame_rate_abbrs);
    char *cp;

    /* First, we check our abbreviation table */
    for (i = 0; i < n; ++i)
         if (!strcmp(video_frame_rate_abbrs[i].abbr, arg)) {
             frame_rate->num = video_frame_rate_abbrs[i].rate_num;
             frame_rate->den = video_frame_rate_abbrs[i].rate_den;
             return 0;
         }

    /* Then, we try to parse it as fraction */
    cp = strchr(arg, '/');
    if (!cp)
        cp = strchr(arg, ':');
    if (cp) {
        char *cpp;
        frame_rate->num = strtol(arg, &cpp, 10);
        if (cpp != arg || cpp == cp)
            frame_rate->den = strtol(cp+1, &cpp, 10);
        else
            frame_rate->num = 0;
    } else {
        /* Finally we give up and parse it as double */
        AVRational time_base = av_d2q(strtod(arg, 0), 1001000);
        frame_rate->den = time_base.den;
        frame_rate->num = time_base.num;
    }
    if (frame_rate->num <= 0 || frame_rate->den <= 0)
        return AVERROR(EINVAL);
    return 0;
}
