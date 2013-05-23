/*
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
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

#define LIBAVUTIL_VERSION_MAJOR 52
#define LIBAVUTIL_VERSION_MINOR 12
#define LIBAVUTIL_VERSION_MICRO  0

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

#ifndef FF_API_PIX_FMT
#define FF_API_PIX_FMT                  (LIBAVUTIL_VERSION_MAJOR < 53)
#endif
#ifndef FF_API_CONTEXT_SIZE
#define FF_API_CONTEXT_SIZE             (LIBAVUTIL_VERSION_MAJOR < 53)
#endif
#ifndef FF_API_PIX_FMT_DESC
#define FF_API_PIX_FMT_DESC             (LIBAVUTIL_VERSION_MAJOR < 53)
#endif
#ifndef FF_API_AV_REVERSE
#define FF_API_AV_REVERSE               (LIBAVUTIL_VERSION_MAJOR < 53)
#endif
#ifndef FF_API_AUDIOCONVERT
#define FF_API_AUDIOCONVERT             (LIBAVUTIL_VERSION_MAJOR < 53)
#endif
#ifndef FF_API_CPU_FLAG_MMX2
#define FF_API_CPU_FLAG_MMX2            (LIBAVUTIL_VERSION_MAJOR < 53)
#endif
#ifndef FF_API_LLS_PRIVATE
#define FF_API_LLS_PRIVATE              (LIBAVUTIL_VERSION_MAJOR < 53)
#endif
#ifndef FF_API_AVFRAME_LAVC
#define FF_API_AVFRAME_LAVC             (LIBAVUTIL_VERSION_MAJOR < 53)
#endif

/**
 * @}
 */

#endif /* AVUTIL_VERSION_H */
