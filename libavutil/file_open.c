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

#include "config.h"
#include "internal.h"
#include "mem.h"
#include <stdarg.h>
#include <fcntl.h>
#include <sys/stat.h>
#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#if HAVE_IO_H
#include <io.h>
#endif

#if defined(_WIN32) && !defined(__MINGW32CE__)
#undef open
#undef lseek
#undef stat
#undef fstat
#include <windows.h>
#include <share.h>
#include <errno.h>

static int win32_open(const char *filename_utf8, int oflag, int pmode)
{
    int fd;
    int num_chars;
    wchar_t *filename_w;

    /* convert UTF-8 to wide chars */
    num_chars = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, filename_utf8, -1, NULL, 0);
    if (num_chars <= 0)
        goto fallback;
    filename_w = av_mallocz(sizeof(wchar_t) * num_chars);
    if (!filename_w) {
        errno = ENOMEM;
        return -1;
    }
    MultiByteToWideChar(CP_UTF8, 0, filename_utf8, -1, filename_w, num_chars);

    fd = _wsopen(filename_w, oflag, SH_DENYNO, pmode);
    av_freep(&filename_w);

    if (fd != -1 || (oflag & O_CREAT))
        return fd;

fallback:
    /* filename may be in CP_ACP */
    return _sopen(filename_utf8, oflag, SH_DENYNO, pmode);
}
#define open win32_open
#endif

int avpriv_open(const char *filename, int flags, ...)
{
    int fd;
    unsigned int mode = 0;
    va_list ap;

    va_start(ap, flags);
    if (flags & O_CREAT)
        mode = va_arg(ap, unsigned int);
    va_end(ap);

#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif

    fd = open(filename, flags, mode);
#if HAVE_FCNTL
    if (fd != -1) {
        if (fcntl(fd, F_SETFD, FD_CLOEXEC) == -1)
            av_log(NULL, AV_LOG_DEBUG, "Failed to set close on exec\n");
    }
#endif

    return fd;
}
