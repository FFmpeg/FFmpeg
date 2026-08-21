/*
 * Copyright (c) 2026 Romain Beauxis
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

#include "libavformat/http.h"

static void test(const char *line)
{
    HTTPStatusLine st;
    int ret;

    printf("\"%s\"\n", line);

    ret = ff_http_parse_status_line(NULL, line, &st);
    if (ret < 0) {
        printf("  rejected\n");
        return;
    }

    printf("  version=\"%s\" code=%d willclose=%d reason=\"%s\"\n",
           st.version, st.code, st.willclose, st.reason);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <status line>...\n", argv[0]);
        return 1;
    }

    for (int i = 1; i < argc; i++)
        test(argv[i]);

    return 0;
}
