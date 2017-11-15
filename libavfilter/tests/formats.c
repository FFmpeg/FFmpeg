/*
 * Copyright (c) 2007 Bobby Bingham
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

#include "libavfilter/formats.c"

#undef printf

int main(void)
{
    const int64_t *cl;
    char buf[512];
    int i;
    const char *teststrings[] ={
        "blah",
        "1",
        "2",
        "-1",
        "60",
        "65",
        "1c",
        "2c",
        "-1c",
        "60c",
        "65c",
        "2C",
        "60C",
        "65C",
        "5.1",
        "stereo",
        "1+1+1+1",
        "1c+1c+1c+1c",
        "2c+1c",
        "0x3",
    };

    for (cl = avfilter_all_channel_layouts; *cl != -1; cl++) {
        av_get_channel_layout_string(buf, sizeof(buf), -1, *cl);
        printf("%s\n", buf);
    }

    for ( i = 0; i<FF_ARRAY_ELEMS(teststrings); i++) {
        int64_t layout = -1;
        int count = -1;
        int ret;
        ret = ff_parse_channel_layout(&layout, &count, teststrings[i], NULL);

        printf ("%d = ff_parse_channel_layout(%016"PRIX64", %2d, %s);\n", ret ? -1 : 0, layout, count, teststrings[i]);
    }

    return 0;
}
