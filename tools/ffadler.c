/*
* Copyright (c) 2002 Fabrice Bellard
* Copyright (c) 2013 Michael Niedermayer
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
#include "libavutil/adler32.h"

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

static int check(char *file)
{
    uint8_t buffer[SIZE];
    uint32_t checksum = 1;
    int fd;
    int ret = 0;

    if (file) fd = open(file, O_RDONLY);
    else      fd = 0;
    if (fd == -1) {
        printf("A32=OPEN-FAILED-%d", errno);
        ret = 1;
        goto end;
    }

    for (;;) {
        ssize_t size = read(fd, buffer, SIZE);
        if (size < 0) {
            printf("A32=0x%08x+READ-FAILED-%d", checksum, errno);
            ret = 2;
            goto end;
        } else if(!size)
            break;
        checksum = av_adler32_update(checksum, buffer, size);
    }
    close(fd);

    printf("A32=0x%08x", checksum);
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

    for (i = 1; i<argc; i++)
        ret |= check(argv[i]);

    if (argc == 1)
        ret |= check(NULL);

    return ret;
}
