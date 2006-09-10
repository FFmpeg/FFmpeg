/*
 * various utilities for ffmpeg system
 * copyright (c) 2000, 2001, 2002 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef _OS_SUPPORT_H
#define _OS_SUPPORT_H

/**
 * @file os_support.h
 * miscellaneous OS support macros and functions.
 *
 * - usleep() (Win32, BeOS, OS/2)
 * - floatf() (OS/2)
 * - strcasecmp() (OS/2)
 */

#ifdef __MINGW32__
__declspec(dllimport) void __stdcall Sleep(unsigned long dwMilliseconds);
// #  include <windows.h>
#  define usleep(t)    Sleep((t) / 1000)
#endif

#ifdef __BEOS__
#  ifndef usleep
#    include <OS.h>
#    define usleep(t)  snooze((bigtime_t)(t))
#  endif
#endif

#if defined(CONFIG_OS2)
#include <stdlib.h>
static inline int usleep(unsigned int t) { return _sleep2(t / 1000); }
static inline int strcasecmp(const char* s1, const char* s2) { return stricmp(s1,s2); }
#endif

#endif /* _OS_SUPPORT_H */
