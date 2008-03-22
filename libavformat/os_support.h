/*
 * various utilities for ffmpeg system
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

#ifndef FFMPEG_OS_SUPPORT_H
#define FFMPEG_OS_SUPPORT_H

/**
 * @file os_support.h
 * miscellaneous OS support macros and functions.
 */

#ifdef __MINGW32__
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  define usleep(t)    Sleep((t) / 1000)
#  include <fcntl.h>
#  define lseek(f,p,w) _lseeki64((f), (p), (w))
#endif

#ifdef __BEOS__
#  include <sys/socket.h>
#  include <netinet/in.h>
   /* not net_server ? */
#  include <BeBuild.h>
   /* R5 didn't have usleep, fake it. Haiku and Zeta has it now. */
#  if B_BEOS_VERSION <= B_BEOS_VERSION_5
#    include <OS.h>
     /* doesn't set errno but that's enough */
#    define usleep(t)  snooze((bigtime_t)(t))
#  endif
#  ifndef SA_RESTART
#    warning SA_RESTART not implemented; ffserver might misbehave.
#    define SA_RESTART 0
#  endif
#endif

#ifdef CONFIG_NETWORK
#ifndef HAVE_SOCKLEN_T
typedef int socklen_t;
#endif

/* most of the time closing a socket is just closing an fd */
#ifndef HAVE_CLOSESOCKET
#define closesocket close
#endif

#ifdef CONFIG_FFSERVER
#ifndef HAVE_POLL_H
typedef unsigned long nfds_t;

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


extern int poll(struct pollfd *fds, nfds_t numfds, int timeout);
#endif /* HAVE_POLL_H */
#endif /* CONFIG_FFSERVER */
#endif /* CONFIG_NETWORK */

#endif /* FFMPEG_OS_SUPPORT_H */
