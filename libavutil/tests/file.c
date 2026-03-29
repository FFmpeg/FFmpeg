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

#include <stdarg.h>
#include <stdio.h>

#include "libavutil/file.c"
#include "libavutil/log.h"

static int last_log_level = -1;

static void log_callback(void *ctx, int level, const char *fmt, va_list args)
{
    (void)ctx; (void)fmt; (void)args;
    last_log_level = level;
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "file.c";
    uint8_t *buf;
    size_t size;

    av_log_set_callback(log_callback);

    /* map an existing file and verify it is non-empty and readable */
    if (av_file_map(path, &buf, &size, 0, NULL) < 0)
        return 1;
    av_file_unmap(buf, size);
    if (size == 0)
        return 1;

    /* for offset i, error must be logged at AV_LOG_ERROR + i */
    for (int i = 0; i < 2; i++) {
        last_log_level = -1;
        if (av_file_map("no_such_file_xyz", &buf, &size, i, NULL) >= 0) {
            av_file_unmap(buf, size);
            return 2;
        }
        if (last_log_level != AV_LOG_ERROR + i) {
            fprintf(stderr, "expected level %d with offset=%d, got %d\n",
                    AV_LOG_ERROR + i, i, last_log_level);
            return 3;
        }
    }

    return 0;
}
