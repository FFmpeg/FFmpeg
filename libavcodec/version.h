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

#ifndef AVCODEC_VERSION_H
#define AVCODEC_VERSION_H

/**
 * @file
 * @ingroup libavc
 * Libavcodec version macros.
 */

#include "libavutil/version.h"

#define LIBAVCODEC_VERSION_MAJOR  59
#define LIBAVCODEC_VERSION_MINOR  18
#define LIBAVCODEC_VERSION_MICRO 100

#define LIBAVCODEC_VERSION_INT  AV_VERSION_INT(LIBAVCODEC_VERSION_MAJOR, \
                                               LIBAVCODEC_VERSION_MINOR, \
                                               LIBAVCODEC_VERSION_MICRO)
#define LIBAVCODEC_VERSION      AV_VERSION(LIBAVCODEC_VERSION_MAJOR,    \
                                           LIBAVCODEC_VERSION_MINOR,    \
                                           LIBAVCODEC_VERSION_MICRO)
#define LIBAVCODEC_BUILD        LIBAVCODEC_VERSION_INT

#define LIBAVCODEC_IDENT        "Lavc" AV_STRINGIFY(LIBAVCODEC_VERSION)

/**
 * FF_API_* defines may be placed below to indicate public API that will be
 * dropped at a future version bump. The defines themselves are not part of
 * the public API and may change, break or disappear at any time.
 *
 * @note, when bumping the major version it is recommended to manually
 * disable each FF_API_* in its own commit instead of disabling them all
 * at once through the bump. This improves the git bisect-ability of the change.
 */

#define FF_API_OPENH264_SLICE_MODE (LIBAVCODEC_VERSION_MAJOR < 60)
#define FF_API_OPENH264_CABAC      (LIBAVCODEC_VERSION_MAJOR < 60)
#define FF_API_UNUSED_CODEC_CAPS   (LIBAVCODEC_VERSION_MAJOR < 60)
#define FF_API_THREAD_SAFE_CALLBACKS (LIBAVCODEC_VERSION_MAJOR < 60)
#define FF_API_DEBUG_MV          (LIBAVCODEC_VERSION_MAJOR < 60)
#define FF_API_GET_FRAME_CLASS     (LIBAVCODEC_VERSION_MAJOR < 60)
#define FF_API_AUTO_THREADS        (LIBAVCODEC_VERSION_MAJOR < 60)
#define FF_API_INIT_PACKET         (LIBAVCODEC_VERSION_MAJOR < 60)
#define FF_API_AVCTX_TIMEBASE    (LIBAVCODEC_VERSION_MAJOR < 60)
#define FF_API_MPEGVIDEO_OPTS      (LIBAVCODEC_VERSION_MAJOR < 60)
#define FF_API_FLAG_TRUNCATED      (LIBAVCODEC_VERSION_MAJOR < 60)
#define FF_API_SUB_TEXT_FORMAT     (LIBAVCODEC_VERSION_MAJOR < 60)

#endif /* AVCODEC_VERSION_H */
