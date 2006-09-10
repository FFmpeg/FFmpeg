/*
 * copyright (c) 2006 Michael Niedermayer <michaelni@gmx.at>
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

#ifndef LOG_H
#define LOG_H

#include <stdarg.h>

/**
 * Used by av_log
 */
typedef struct AVCLASS AVClass;
struct AVCLASS {
    const char* class_name;
    const char* (*item_name)(void*); /* actually passing a pointer to an AVCodecContext
                                        or AVFormatContext, which begin with an AVClass.
                                        Needed because av_log is in libavcodec and has no visibility
                                        of AVIn/OutputFormat */
    struct AVOption *option;
};

/* av_log API */

#define AV_LOG_QUIET -1
#define AV_LOG_ERROR 0
#define AV_LOG_INFO 1
#define AV_LOG_DEBUG 2
extern int av_log_level;

#ifdef __GNUC__
extern void av_log(void*, int level, const char *fmt, ...) __attribute__ ((__format__ (__printf__, 3, 4)));
#else
extern void av_log(void*, int level, const char *fmt, ...);
#endif

#if LIBAVUTIL_VERSION_INT < (50<<16)
extern void av_vlog(void*, int level, const char *fmt, va_list);
extern int av_log_get_level(void);
extern void av_log_set_level(int);
extern void av_log_set_callback(void (*)(void*, int, const char*, va_list));
#else
extern void (*av_vlog)(void*, int, const char*, va_list);
#endif

#endif /* LOG_H */
