/*
 * Copyright (c) 2002 Fabrice Bellard
 * Copyright (c) 2013 Michael Niedermayer
 * Copyright (c) 2013 James Almer
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
#include "libavutil/error.h"
#include "libavutil/hash.h"
#include "libavutil/mem.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>

#if HAVE_IO_H
#include <io.h>
#endif
#if HAVE_UNISTD_H
#include <unistd.h>
#endif

#define SIZE 65536

static struct AVHashContext *hash;
static uint8_t *res;

static void usage(void)
{
    int i = 0;
    const char *name;

    printf("usage: ffhash [algorithm] [input]...\n");
    printf("Supported hash algorithms:");
    do {
        name = av_hash_names(i);
        if (name)
            printf(" %s", name);
        i++;
    } while(name);
    printf("\n");
}

static void finish(void)
{
    int i, len = av_hash_get_size(hash);

    printf("%s=0x", av_hash_get_name(hash));
    av_hash_final(hash, res);
    for (i = 0; i < len; i++)
        printf("%02x", res[i]);
}

static int check(char *file)
{
    uint8_t buffer[SIZE];
    int fd, flags = O_RDONLY;
    int ret = 0;

#ifdef O_BINARY
    flags |= O_BINARY;
#endif
    if (file) fd = open(file, flags);
    else      fd = 0;
    if (fd == -1) {
        printf("%s=OPEN-FAILED: %s:", av_hash_get_name(hash), strerror(errno));
        ret = 1;
        goto end;
    }

    av_hash_init(hash);
    for (;;) {
        int size = read(fd, buffer, SIZE);
        if (size < 0) {
            close(fd);
            finish();
            printf("+READ-FAILED: %s", strerror(errno));
            ret = 2;
            goto end;
        } else if(!size)
            break;
        av_hash_update(hash, buffer, size);
    }
    close(fd);

    finish();
end:
    if (file)
        printf(" *%s", file);
    printf("\n");

    return ret;
}

int main(int argc, char **argv)
{
    int i;
    int ret = 0;

    if (argc == 1) {
        usage();
        return 0;
    }

    if ((ret = av_hash_alloc(&hash, argv[1])) < 0) {
        switch(ret) {
        case AVERROR(EINVAL):
            printf("Invalid hash type: %s\n", argv[1]);
            break;
        case AVERROR(ENOMEM):
            printf("%s\n", strerror(errno));
            break;
        }
        return 1;
    }
    res = av_malloc(av_hash_get_size(hash));
    if (!res) {
        printf("%s\n", strerror(errno));
        return 1;
    }

    for (i = 2; i < argc; i++)
        ret |= check(argv[i]);

    if (argc < 3)
        ret |= check(NULL);

    av_hash_freep(&hash);
    av_freep(&res);

    return ret;
}
