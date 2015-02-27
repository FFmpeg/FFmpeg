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

#include "macros.h"

/**
 * @addtogroup version_utils
 *
 * Useful to check and match library version in order to maintain
 * backward compatibility.
 *
 * @{
 */

#define AV_VERSION_INT(a, b, c) ((a)<<16 | (b)<<8 | (c))
#define AV_VERSION_DOT(a, b, c) a ##.## b ##.## c
#define AV_VERSION(a, b, c) AV_VERSION_DOT(a, b, c)

/**
 * @}
 */

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

#define LIBAVUTIL_VERSION_MAJOR  54
#define LIBAVUTIL_VERSION_MINOR  19
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

#ifndef FF_API_OLD_AVOPTIONS
#define FF_API_OLD_AVOPTIONS            (LIBAVUTIL_VERSION_MAJOR < 55)
#endif
#ifndef FF_API_PIX_FMT
#define FF_API_PIX_FMT                  (LIBAVUTIL_VERSION_MAJOR < 55)
#endif
#ifndef FF_API_CONTEXT_SIZE
#define FF_API_CONTEXT_SIZE             (LIBAVUTIL_VERSION_MAJOR < 55)
#endif
#ifndef FF_API_PIX_FMT_DESC
#define FF_API_PIX_FMT_DESC             (LIBAVUTIL_VERSION_MAJOR < 55)
#endif
#ifndef FF_API_AV_REVERSE
#define FF_API_AV_REVERSE               (LIBAVUTIL_VERSION_MAJOR < 55)
#endif
#ifndef FF_API_AUDIOCONVERT
#define FF_API_AUDIOCONVERT             (LIBAVUTIL_VERSION_MAJOR < 55)
#endif
#ifndef FF_API_CPU_FLAG_MMX2
#define FF_API_CPU_FLAG_MMX2            (LIBAVUTIL_VERSION_MAJOR < 55)
#endif
#ifndef FF_API_LLS_PRIVATE
#define FF_API_LLS_PRIVATE              (LIBAVUTIL_VERSION_MAJOR < 55)
#endif
#ifndef FF_API_AVFRAME_LAVC
#define FF_API_AVFRAME_LAVC             (LIBAVUTIL_VERSION_MAJOR < 55)
#endif
#ifndef FF_API_VDPAU
#define FF_API_VDPAU                    (LIBAVUTIL_VERSION_MAJOR < 55)
#endif
#ifndef FF_API_GET_CHANNEL_LAYOUT_COMPAT
#define FF_API_GET_CHANNEL_LAYOUT_COMPAT (LIBAVUTIL_VERSION_MAJOR < 55)
#endif
#ifndef FF_API_XVMC
#define FF_API_XVMC                     (LIBAVUTIL_VERSION_MAJOR < 55)
#endif
#ifndef FF_API_OPT_TYPE_METADATA
#define FF_API_OPT_TYPE_METADATA        (LIBAVUTIL_VERSION_MAJOR < 55)
#endif

#ifndef FF_CONST_AVUTIL55
#if LIBAVUTIL_VERSION_MAJOR >= 55
#define FF_CONST_AVUTIL55 const
#else
#define FF_CONST_AVUTIL55
#endif
#endif

/**
 * @}
 */

#endif /* AVUTIL_VERSION_H */

