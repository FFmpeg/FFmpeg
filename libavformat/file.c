/*
 * Buffered file io for ffmpeg system
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

#include "libavutil/avstring.h"
#include "avformat.h"
#include <fcntl.h>
#if HAVE_SETMODE
#include <io.h>
#endif
#include <unistd.h>
#include <sys/time.h>
#include <stdlib.h>
#include "os_support.h"


/* standard file protocol */

static int file_open(URLContext *h, const char *filename, int flags)
{
    int access;
    int fd;

    av_strstart(filename, "file:", &filename);

    if (flags & URL_RDWR) {
        access = O_CREAT | O_TRUNC | O_RDWR;
    } else if (flags & URL_WRONLY) {
        access = O_CREAT | O_TRUNC | O_WRONLY;
    } else {
        access = O_RDONLY;
    }
#ifdef O_BINARY
    access |= O_BINARY;
#endif
    fd = open(filename, access, 0666);
    if (fd < 0)
        return AVERROR(ENOENT);
    h->priv_data = (void *) fd;
    return 0;
}

static int file_read(URLContext *h, unsigned char *buf, int size)
{
    int fd = (int) h->priv_data;
    return read(fd, buf, size);
}

static int file_write(URLContext *h, unsigned char *buf, int size)
{
    int fd = (int) h->priv_data;
    return write(fd, buf, size);
}

/* XXX: use llseek */
static int64_t file_seek(URLContext *h, int64_t pos, int whence)
{
    int fd = (int) h->priv_data;
    return lseek(fd, pos, whence);
}

static int file_close(URLContext *h)
{
    int fd = (int) h->priv_data;
    return close(fd);
}

static int file_get_handle(URLContext *h)
{
    return (int) h->priv_data;
}

URLProtocol file_protocol = {
    "file",
    file_open,
    file_read,
    file_write,
    file_seek,
    file_close,
    .url_get_file_handle = file_get_handle,
};

/* pipe protocol */

static int pipe_open(URLContext *h, const char *filename, int flags)
{
    int fd;
    char *final;
    av_strstart(filename, "pipe:", &filename);

    fd = strtol(filename, &final, 10);
    if((filename == final) || *final ) {/* No digits found, or something like 10ab */
        if (flags & URL_WRONLY) {
            fd = 1;
        } else {
            fd = 0;
        }
    }
#if HAVE_SETMODE
    setmode(fd, O_BINARY);
#endif
    h->priv_data = (void *) fd;
    h->is_streamed = 1;
    return 0;
}

URLProtocol pipe_protocol = {
    "pipe",
    pipe_open,
    file_read,
    file_write,
    .url_get_file_handle = file_get_handle,
};
