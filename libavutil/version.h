/*
 * copyright (c) 2003 Fabrice Bellard
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

#ifndef AVUTIL_VERSION_H
#define AVUTIL_VERSION_H

#include "avutil.h"

/**
 * @file
 * @ingroup lavu
 * Libavutil version macros
 */

/**
 * @defgroup lavu_ver Version and Build diagnostics
 *
 * Macros and function useful to check at compiletime and at runtime
 * which version of libavutil is in use.
 *
 * @{
 */

#define LIBAVUTIL_VERSION_MAJOR 51
#define LIBAVUTIL_VERSION_MINOR 72
#define LIBAVUTIL_VERSION_MICRO 100

#define LIBAVUTIL_VERSION_INT   AV_VERSION_INT(LIBAVUTIL_VERSION_MAJOR, \
                                               LIBAVUTIL_VERSION_MINOR, \
                                               LIBAVUTIL_VERSION_MICRO)
#define LIBAVUTIL_VERSION       AV_VERSION(LIBAVUTIL_VERSION_MAJOR,     \
                                           LIBAVUTIL_VERSION_MINOR,     \
                                           LIBAVUTIL_VERSION_MICRO)
#define LIBAVUTIL_BUILD         LIBAVUTIL_VERSION_INT

#define LIBAVUTIL_IDENT         "Lavu" AV_STRINGIFY(LIBAVUTIL_VERSION)

/**
 * @}
 *
 * @defgroup depr_guards Deprecation guards
 * FF_API_* defines may be placed below to indicate public API that will be
 * dropped at a future version bump. The defines themselves are not part of
 * the public API and may change, break or disappear at any time.
 *
 * @{
 */

#ifndef FF_API_OLD_EVAL_NAMES
#define FF_API_OLD_EVAL_NAMES           (LIBAVUTIL_VERSION_MAJOR < 52)
#endif
#ifndef FF_API_GET_BITS_PER_SAMPLE_FMT
#define FF_API_GET_BITS_PER_SAMPLE_FMT (LIBAVUTIL_VERSION_MAJOR < 52)
#endif
#ifndef FF_API_FIND_OPT
#define FF_API_FIND_OPT                 (LIBAVUTIL_VERSION_MAJOR < 52)
#endif
#ifndef FF_API_AV_FIFO_PEEK
#define FF_API_AV_FIFO_PEEK             (LIBAVUTIL_VERSION_MAJOR < 52)
#endif
#ifndef FF_API_OLD_AVOPTIONS
#define FF_API_OLD_AVOPTIONS            (LIBAVUTIL_VERSION_MAJOR < 52)
#endif
#ifndef FF_API_OLD_TC_ADJUST_FRAMENUM
#define FF_API_OLD_TC_ADJUST_FRAMENUM   (LIBAVUTIL_VERSION_MAJOR < 52)
#endif

/**
 * @}
 */

#endif /* AVUTIL_VERSION_H */

