/*
 * Buffered file io for ffmpeg system
 * Copyright (c) 2001 Gerard Lantau
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include "avformat.h"
#include <fcntl.h>
#ifndef CONFIG_WIN32
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#else
#include <io.h>
#define open(fname,oflag,pmode) _open(fname,oflag,pmode)
#endif /* CONFIG_WIN32 */


/* standard file protocol */

static int file_open(URLContext *h, const char *filename, int flags)
{
    int access;
    int fd;

    if (flags & URL_WRONLY) {
        access = O_CREAT | O_TRUNC | O_WRONLY;
    } else {
        access = O_RDONLY;
    }
#ifdef CONFIG_WIN32
    access |= O_BINARY;
#endif
    fd = open(filename, access, 0666);
    if (fd < 0)
        return -ENOENT;
    h->priv_data = (void *)fd;
    return 0;
}

static int file_read(URLContext *h, unsigned char *buf, int size)
{
    int fd = (int)h->priv_data;
    return read(fd, buf, size);
}

static int file_write(URLContext *h, unsigned char *buf, int size)
{
    int fd = (int)h->priv_data;
    return write(fd, buf, size);
}

/* XXX: use llseek */
static offset_t file_seek(URLContext *h, offset_t pos, int whence)
{
    int fd = (int)h->priv_data;
#ifdef CONFIG_WIN32
    return _lseeki64(fd, pos, whence);
#else
    return lseek(fd, pos, whence);
#endif
}

static int file_close(URLContext *h)
{
    int fd = (int)h->priv_data;
    return close(fd);
}

URLProtocol file_protocol = {
    "file",
    file_open,
    file_read,
    file_write,
    file_seek,
    file_close,
};

/* pipe protocol */

static int pipe_open(URLContext *h, const char *filename, int flags)
{
    int fd;

    if (flags & URL_WRONLY) {
        fd = 1;
    } else {
        fd = 0;
    }
    h->priv_data = (void *)fd;
    return 0;
}

static int pipe_read(URLContext *h, unsigned char *buf, int size)
{
    int fd = (int)h->priv_data;
    return read(fd, buf, size);
}

static int pipe_write(URLContext *h, unsigned char *buf, int size)
{
    int fd = (int)h->priv_data;
    return write(fd, buf, size);
}

static int pipe_close(URLContext *h)
{
    return 0;
}

URLProtocol pipe_protocol = {
    "pipe",
    pipe_open,
    pipe_read,
    pipe_write,
    NULL,
    pipe_close,
};
