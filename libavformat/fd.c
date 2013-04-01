/*
 * Copyright (c) 2010 Cedric Fung (wolfplanet@gmail.com)
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
#include <sys/stat.h>
#include <stdlib.h>
#include "os_support.h"
#include "url.h"


static int file_read(URLContext *h, unsigned char *buf, int size) {
	int fd = (intptr_t) h->priv_data;
	return read(fd, buf, size);
}

static int file_write(URLContext *h, const unsigned char *buf, int size) {
	int fd = (intptr_t) h->priv_data;
	return write(fd, buf, size);
}

static int file_get_handle(URLContext *h) {
	return (intptr_t) h->priv_data;
}

/* XXX: use llseek */
static int64_t file_seek(URLContext *h, int64_t pos, int whence) {
	int fd = (intptr_t) h->priv_data;
	if (whence == AVSEEK_SIZE) {
		struct stat st;
		int ret = fstat(fd, &st);
		return ret < 0 ? AVERROR(errno) : st.st_size;
	}
	return lseek64(fd, pos, whence);
}


static int fd_open(URLContext *h, const char *filename, int flags) {
	int fd;
  struct stat st;
	char *final;
	av_strstart(filename, "fd:", &filename);

	fd = strtol(filename, &final, 10);
	if((filename == final) || *final ) {/* No digits found, or something like 10ab */
		if (flags & O_WRONLY) {
			fd = 1;
		} else {
			fd = 0;
		}
	}
#if HAVE_SETMODE
	setmode(fd, O_BINARY | O_RDONLY);
#endif
	h->priv_data = (void *) (intptr_t) fd;
  h->is_streamed = !fstat(fd, &st) && S_ISFIFO(st.st_mode);
	return 0;
}

static int file_close(URLContext *h) {
    int fd = (intptr_t) h->priv_data;
    return close(fd);
}

URLProtocol ff_fd_protocol = {
	.name                = "fd",
	.url_open            = fd_open,
	.url_read            = file_read,
	.url_write           = file_write,
	.url_seek            = file_seek,
	.url_close           = file_close,
	.url_get_file_handle = file_get_handle,
};
