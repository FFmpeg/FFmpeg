/*
 * various OS-feature replacement utilities
 * copyright (c) 2000, 2001, 2002 Fabrice Bellard
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

#ifndef AVFORMAT_OS_SUPPORT_H
#define AVFORMAT_OS_SUPPORT_H

/**
 * @file
 * miscellaneous OS support macros and functions.
 */

#include "config.h"

#include <sys/stat.h>

#ifdef _WIN32
#if HAVE_DIRECT_H
#include <direct.h>
#endif
#if HAVE_IO_H
#include <io.h>
#endif
#endif

#if defined(_WIN32) && !defined(__MINGW32CE__)
#  include <fcntl.h>
#  ifdef lseek
#   undef lseek
#  endif
#  define lseek(f,p,w) _lseeki64((f), (p), (w))
#  ifdef stat
#   undef stat
#  endif
#  define stat _stati64
#  ifdef fstat
#   undef fstat
#  endif
#  define fstat(f,s) _fstati64((f), (s))
#endif /* defined(_WIN32) && !defined(__MINGW32CE__) */


#ifdef __ANDROID__
#  if HAVE_UNISTD_H
#    include <unistd.h>
#  endif
#  ifdef lseek
#   undef lseek
#  endif
#  define lseek(f,p,w) lseek64((f), (p), (w))
#endif

static inline int is_dos_path(const char *path)
{
#if HAVE_DOS_PATHS
    if (path[0] && path[1] == ':')
        return 1;
#endif
    return 0;
}

#if defined(__OS2__) || defined(__Plan9__)
#define SHUT_RD 0
#define SHUT_WR 1
#define SHUT_RDWR 2
#endif

#if defined(_WIN32)
#define SHUT_RD SD_RECEIVE
#define SHUT_WR SD_SEND
#define SHUT_RDWR SD_BOTH

#ifndef S_IRUSR
#define S_IRUSR S_IREAD
#endif
#ifndef S_IWUSR
#define S_IWUSR S_IWRITE
#endif
#endif

#if CONFIG_NETWORK
#if !HAVE_SOCKLEN_T
typedef int socklen_t;
#endif

/* most of the time closing a socket is just closing an fd */
#if !HAVE_CLOSESOCKET
#define closesocket close
#endif

#if !HAVE_POLL_H
typedef unsigned long nfds_t;

#if HAVE_WINSOCK2_H
#include <winsock2.h>
#endif
#if !HAVE_STRUCT_POLLFD
struct pollfd {
    int fd;
    short events;  /* events to look for */
    short revents; /* events that occurred */
};

/* events & revents */
#define POLLIN     0x0001  /* any readable data available */
#define POLLOUT    0x0002  /* file descriptor is writeable */
#define POLLRDNORM POLLIN
#define POLLWRNORM POLLOUT
#define POLLRDBAND 0x0008  /* priority readable data */
#define POLLWRBAND 0x0010  /* priority data can be written */
#define POLLPRI    0x0020  /* high priority readable data */

/* revents only */
#define POLLERR    0x0004  /* errors pending */
#define POLLHUP    0x0080  /* disconnected */
#define POLLNVAL   0x1000  /* invalid file descriptor */
#endif


int ff_poll(struct pollfd *fds, nfds_t numfds, int timeout);
#define poll ff_poll
#endif /* HAVE_POLL_H */
#endif /* CONFIG_NETWORK */

#if defined(__MINGW32CE__)
#define mkdir(a, b) _mkdir(a)
#elif defined(_WIN32)
#include <stdio.h>
#include <windows.h>
#include "libavutil/wchar_filename.h"

#ifdef WINAPI_FAMILY
#include <winapifamily.h>
// If a WINAPI_FAMILY is defined, check that the desktop API subset
// is enabled
#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
#define USE_MOVEFILEEXA
#endif
#else
// If no WINAPI_FAMILY is defined, assume the full API subset
#define USE_MOVEFILEEXA
#endif

#define DEF_FS_FUNCTION(name, wfunc, afunc)               \
static inline int win32_##name(const char *filename_utf8) \
{                                                         \
    wchar_t *filename_w;                                  \
    int ret;                                              \
                                                          \
    if (utf8towchar(filename_utf8, &filename_w))          \
        return -1;                                        \
    if (!filename_w)                                      \
        goto fallback;                                    \
                                                          \
    ret = wfunc(filename_w);                              \
    av_free(filename_w);                                  \
    return ret;                                           \
                                                          \
fallback:                                                 \
    /* filename may be be in CP_ACP */                    \
    return afunc(filename_utf8);                          \
}

DEF_FS_FUNCTION(unlink, _wunlink, _unlink)
DEF_FS_FUNCTION(mkdir,  _wmkdir,  _mkdir)
DEF_FS_FUNCTION(rmdir,  _wrmdir , _rmdir)

#define DEF_FS_FUNCTION2(name, wfunc, afunc, partype)     \
static inline int win32_##name(const char *filename_utf8, partype par) \
{                                                         \
    wchar_t *filename_w;                                  \
    int ret;                                              \
                                                          \
    if (utf8towchar(filename_utf8, &filename_w))          \
        return -1;                                        \
    if (!filename_w)                                      \
        goto fallback;                                    \
                                                          \
    ret = wfunc(filename_w, par);                         \
    av_free(filename_w);                                  \
    return ret;                                           \
                                                          \
fallback:                                                 \
    /* filename may be be in CP_ACP */                    \
    return afunc(filename_utf8, par);                     \
}

DEF_FS_FUNCTION2(access, _waccess, _access, int)
DEF_FS_FUNCTION2(stat, _wstati64, _stati64, struct stat*)

static inline int win32_rename(const char *src_utf8, const char *dest_utf8)
{
    wchar_t *src_w, *dest_w;
    int ret;

    if (utf8towchar(src_utf8, &src_w))
        return -1;
    if (utf8towchar(dest_utf8, &dest_w)) {
        av_free(src_w);
        return -1;
    }
    if (!src_w || !dest_w) {
        av_free(src_w);
        av_free(dest_w);
        goto fallback;
    }

    ret = MoveFileExW(src_w, dest_w, MOVEFILE_REPLACE_EXISTING);
    av_free(src_w);
    av_free(dest_w);
    // Lacking proper mapping from GetLastError() error codes to errno codes
    if (ret)
        errno = EPERM;
    return ret;

fallback:
    /* filename may be be in CP_ACP */
#ifdef USE_MOVEFILEEXA
    ret = MoveFileExA(src_utf8, dest_utf8, MOVEFILE_REPLACE_EXISTING);
    if (ret)
        errno = EPERM;
#else
    /* Windows Phone doesn't have MoveFileExA, and for Windows Store apps,
     * it is available but not allowed by the app certification kit. However,
     * it's unlikely that anybody would input filenames in CP_ACP there, so this
     * fallback is kept mostly for completeness. Alternatively we could
     * do MultiByteToWideChar(CP_ACP) and use MoveFileExW, but doing
     * explicit conversions with CP_ACP is allegedly forbidden in windows
     * store apps (or windows phone), and the notion of a native code page
     * doesn't make much sense there. */
    ret = rename(src_utf8, dest_utf8);
#endif
    return ret;
}

#define mkdir(a, b) win32_mkdir(a)
#define rename      win32_rename
#define rmdir       win32_rmdir
#define unlink      win32_unlink
#define access      win32_access

#endif

#endif /* AVFORMAT_OS_SUPPORT_H */
