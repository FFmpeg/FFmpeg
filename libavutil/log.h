/*
 * copyright (c) 2006 Michael Niedermayer <michaelni@gmx.at>
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

#ifndef FFMPEG_LOG_H
#define FFMPEG_LOG_H

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
    const struct AVOption *option;
};

/* av_log API */

#if LIBAVUTIL_VERSION_INT < (50<<16)
#define AV_LOG_QUIET -1
#define AV_LOG_FATAL 0
#define AV_LOG_ERROR 0
#define AV_LOG_WARNING 1
#define AV_LOG_INFO 1
#define AV_LOG_VERBOSE 1
#define AV_LOG_DEBUG 2
#else
#define AV_LOG_QUIET    -8

/**
 * something went really wrong and we will crash now
 */
#define AV_LOG_PANIC     0

/**
 * something went wrong and recovery is not possible
 * like no header in a format which depends on it or a combination
 * of parameters which are not allowed
 */
#define AV_LOG_FATAL     8

/**
 * something went wrong and cannot losslessly be recovered
 * but not all future data is affected
 */
#define AV_LOG_ERROR    16

/**
 * something somehow does not look correct / something which may or may not
 * lead to some problems like use of -vstrict -2
 */
#define AV_LOG_WARNING  24

#define AV_LOG_INFO     32
#define AV_LOG_VERBOSE  40

/**
 * stuff which is only useful for libav* developers
 */
#define AV_LOG_DEBUG    48
#endif

#if LIBAVUTIL_VERSION_INT < (50<<16)
extern int av_log_level;
#endif

/**
 * Send the specified message to the log if the level is less than or equal to
 * the current av_log_level. By default, all logging messages are sent to
 * stderr. This behavior can be altered by setting a different av_vlog callback
 * function.
 *
 * @param avcl A pointer to an arbitrary struct of which the first field is a
 * pointer to an AVClass struct.
 * @param level The importance level of the message, lower values signifying
 * higher importance.
 * @param fmt The format string (printf-compatible) that specifies how
 * subsequent arguments are converted to output.
 * @see av_vlog
 */
#ifdef __GNUC__
extern void av_log(void*, int level, const char *fmt, ...) __attribute__ ((__format__ (__printf__, 3, 4)));
#else
extern void av_log(void*, int level, const char *fmt, ...);
#endif

extern void av_vlog(void*, int level, const char *fmt, va_list);
extern int av_log_get_level(void);
extern void av_log_set_level(int);
extern void av_log_set_callback(void (*)(void*, int, const char*, va_list));
extern void av_log_default_callback(void* ptr, int level, const char* fmt, va_list vl);

#endif /* FFMPEG_LOG_H */
