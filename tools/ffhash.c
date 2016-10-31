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
#include "libavutil/avstring.h"
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
static int out_b64;

static void usage(void)
{
    int i = 0;
    const char *name;

    printf("usage: ffhash [b64:]algorithm [input]...\n");
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
    char res[2 * AV_HASH_MAX_SIZE + 4];

    printf("%s=", av_hash_get_name(hash));
    if (out_b64) {
        av_hash_final_b64(hash, res, sizeof(res));
        printf("b64:%s", res);
    } else {
        av_hash_final_hex(hash, res, sizeof(res));
        printf("0x%s", res);
    }
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
            int err = errno;
            close(fd);
            finish();
            printf("+READ-FAILED: %s", strerror(err));
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
    const char *hash_name;

    if (argc == 1) {
        usage();
        return 0;
    }

    hash_name = argv[1];
    out_b64 = av_strstart(hash_name, "b64:", &hash_name);
    if ((ret = av_hash_alloc(&hash, hash_name)) < 0) {
        switch(ret) {
        case AVERROR(EINVAL):
            printf("Invalid hash type: %s\n", hash_name);
            break;
        case AVERROR(ENOMEM):
            printf("%s\n", strerror(errno));
            break;
        }
        return 1;
    }

    for (i = 2; i < argc; i++)
        ret |= check(argv[i]);

    if (argc < 3)
        ret |= check(NULL);

    av_hash_freep(&hash);

    return ret;
}
