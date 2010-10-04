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
#include "libavutil/eval.h"

typedef struct {
    const char *abbr;
    int width, height;
} VideoSizeAbbr;

typedef struct {
    const char *abbr;
    AVRational rate;
} VideoRateAbbr;

static const VideoSizeAbbr video_size_abbrs[] = {
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

static const VideoRateAbbr video_rate_abbrs[]= {
    { "ntsc",      { 30000, 1001 } },
    { "pal",       {    25,    1 } },
    { "qntsc",     { 30000, 1001 } }, /* VCD compliant NTSC */
    { "qpal",      {    25,    1 } }, /* VCD compliant PAL */
    { "sntsc",     { 30000, 1001 } }, /* square pixel NTSC */
    { "spal",      {    25,    1 } }, /* square pixel PAL */
    { "film",      {    24,    1 } },
    { "ntsc-film", { 24000, 1001 } },
};

int av_parse_video_size(int *width_ptr, int *height_ptr, const char *str)
{
    int i;
    int n = FF_ARRAY_ELEMS(video_size_abbrs);
    char *p;
    int width = 0, height = 0;

    for (i = 0; i < n; i++) {
        if (!strcmp(video_size_abbrs[i].abbr, str)) {
            width  = video_size_abbrs[i].width;
            height = video_size_abbrs[i].height;
            break;
        }
    }
    if (i == n) {
        p = str;
        width = strtol(p, &p, 10);
        if (*p)
            p++;
        height = strtol(p, &p, 10);
    }
    if (width <= 0 || height <= 0)
        return AVERROR(EINVAL);
    *width_ptr  = width;
    *height_ptr = height;
    return 0;
}

int av_parse_video_rate(AVRational *rate, const char *arg)
{
    int i, ret;
    int n = FF_ARRAY_ELEMS(video_rate_abbrs);
    double res;

    /* First, we check our abbreviation table */
    for (i = 0; i < n; ++i)
        if (!strcmp(video_rate_abbrs[i].abbr, arg)) {
            *rate = video_rate_abbrs[i].rate;
            return 0;
        }

    /* Then, we try to parse it as fraction */
    if ((ret = av_parse_and_eval_expr(&res, arg, NULL, NULL, NULL, NULL, NULL, NULL,
                                      NULL, 0, NULL)) < 0)
        return ret;
    *rate = av_d2q(res, 1001000);
    if (rate->num <= 0 || rate->den <= 0)
        return AVERROR(EINVAL);
    return 0;
}

#ifdef TEST

#undef printf

int main(void)
{
    printf("Testing av_parse_video_rate()\n");
    {
        int i;
        const char *rates[] = {
            "-inf",
            "inf",
            "nan",
            "123/0",
            "-123 / 0",
            "",
            "/",
            " 123  /  321",
            "foo/foo",
            "foo/1",
            "1/foo",
            "0/0",
            "/0",
            "1/",
            "1",
            "0",
            "-123/123",
            "-foo",
            "123.23",
            ".23",
            "-.23",
            "-0.234",
            "-0.0000001",
            "  21332.2324   ",
            " -21332.2324   ",
        };

        for (i = 0; i < FF_ARRAY_ELEMS(rates); i++) {
            int ret;
            AVRational q = (AVRational){0, 0};
            ret = av_parse_video_rate(&q, rates[i]),
            printf("'%s' -> %d/%d ret:%d\n",
                   rates[i], q.num, q.den, ret);
        }
    }

    return 0;
}

#endif /* TEST */
