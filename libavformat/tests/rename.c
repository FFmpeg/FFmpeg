/*
 * Copyright (c) 2026 Christopher Decker
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

#include <stdio.h>

#if HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "libavutil/random_seed.h"

#include "libavformat/os_support.h"

static int create_file(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    fputs("ffmpeg rename test\n", f);
    fclose(f);
    return 0;
}

static int file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    fclose(f);
    return 1;
}

int main(void)
{
    char src[64];
    char dst[64];
    unsigned seed = av_get_random_seed();
    int ret = 0;

    snprintf(src, sizeof(src), "ff-rename-test-%08x.src", seed);
    snprintf(dst, sizeof(dst), "ff-rename-test-%08x.dst", seed);

    if (create_file(src) < 0) {
        perror("create src");
        return 1;
    }

    /* rename() must follow POSIX semantics and return 0 on success. */
    if (rename(src, dst) != 0) {
        perror("rename");
        ret = 1;
        goto cleanup;
    }

    if (file_exists(src)) {
        fprintf(stderr, "source still exists after rename\n");
        ret = 1;
        goto cleanup;
    }

    if (!file_exists(dst)) {
        fprintf(stderr, "destination missing after rename\n");
        ret = 1;
        goto cleanup;
    }

    /* Renaming a nonexistent source must fail with a -1 return. */
    if (rename(src, dst) != -1) {
        fprintf(stderr, "rename of nonexistent source unexpectedly succeeded\n");
        ret = 1;
        goto cleanup;
    }

cleanup:
    unlink(src);
    unlink(dst);
    return ret;
}
