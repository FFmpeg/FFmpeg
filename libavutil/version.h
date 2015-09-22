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

#define LIBAVUTIL_VERSION_MAJOR  55
#define LIBAVUTIL_VERSION_MINOR   2
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
 * @note, when bumping the major version it is recommended to manually
 * disable each FF_API_* in its own commit instead of disabling them all
 * at once through the bump. This improves the git bisect-ability of the change.
 *
 * @{
 */

#ifndef FF_API_VDPAU
#define FF_API_VDPAU                    (LIBAVUTIL_VERSION_MAJOR < 56)
#endif
#ifndef FF_API_XVMC
#define FF_API_XVMC                     (LIBAVUTIL_VERSION_MAJOR < 56)
#endif
#ifndef FF_API_OPT_TYPE_METADATA
#define FF_API_OPT_TYPE_METADATA        (LIBAVUTIL_VERSION_MAJOR < 56)
#endif
#ifndef FF_API_DLOG
#define FF_API_DLOG                     (LIBAVUTIL_VERSION_MAJOR < 56)
#endif
#ifndef FF_API_CRYPTO_CONTEXT
#define FF_API_CRYPTO_CONTEXT           (LIBAVUTIL_VERSION_MAJOR < 56)
#endif
#ifndef FF_API_VAAPI
#define FF_API_VAAPI                    (LIBAVUTIL_VERSION_MAJOR < 56)
#endif
#ifndef FF_API_FRAME_QP
#define FF_API_FRAME_QP                 (LIBAVUTIL_VERSION_MAJOR < 56)
#endif
#ifndef FF_API_PLUS1_MINUS1
#define FF_API_PLUS1_MINUS1             (LIBAVUTIL_VERSION_MAJOR < 56)
#endif


/**
 * @}
 */

#endif /* AVUTIL_VERSION_H */

