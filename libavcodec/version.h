/*
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

#ifndef AVCODEC_VERSION_H
#define AVCODEC_VERSION_H

/**
 * @file
 * @ingroup libavc
 * Libavcodec version macros.
 */

#include "libavutil/version.h"

#define LIBAVCODEC_VERSION_MAJOR 55
#define LIBAVCODEC_VERSION_MINOR  69
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
 */

#ifndef FF_API_REQUEST_CHANNELS
#define FF_API_REQUEST_CHANNELS (LIBAVCODEC_VERSION_MAJOR < 56)
#endif
#ifndef FF_API_OLD_DECODE_AUDIO
#define FF_API_OLD_DECODE_AUDIO (LIBAVCODEC_VERSION_MAJOR < 56)
#endif
#ifndef FF_API_OLD_ENCODE_AUDIO
#define FF_API_OLD_ENCODE_AUDIO (LIBAVCODEC_VERSION_MAJOR < 56)
#endif
#ifndef FF_API_OLD_ENCODE_VIDEO
#define FF_API_OLD_ENCODE_VIDEO (LIBAVCODEC_VERSION_MAJOR < 56)
#endif
#ifndef FF_API_CODEC_ID
#define FF_API_CODEC_ID          (LIBAVCODEC_VERSION_MAJOR < 56)
#endif
#ifndef FF_API_AUDIO_CONVERT
#define FF_API_AUDIO_CONVERT     (LIBAVCODEC_VERSION_MAJOR < 56)
#endif
#ifndef FF_API_AVCODEC_RESAMPLE
#define FF_API_AVCODEC_RESAMPLE  FF_API_AUDIO_CONVERT
#endif
#ifndef FF_API_DEINTERLACE
#define FF_API_DEINTERLACE       (LIBAVCODEC_VERSION_MAJOR < 56)
#endif
#ifndef FF_API_DESTRUCT_PACKET
#define FF_API_DESTRUCT_PACKET   (LIBAVCODEC_VERSION_MAJOR < 56)
#endif
#ifndef FF_API_GET_BUFFER
#define FF_API_GET_BUFFER        (LIBAVCODEC_VERSION_MAJOR < 56)
#endif
#ifndef FF_API_MISSING_SAMPLE
#define FF_API_MISSING_SAMPLE    (LIBAVCODEC_VERSION_MAJOR < 56)
#endif
#ifndef FF_API_LOWRES
#define FF_API_LOWRES            (LIBAVCODEC_VERSION_MAJOR < 56)
#endif
#ifndef FF_API_CAP_VDPAU
#define FF_API_CAP_VDPAU         (LIBAVCODEC_VERSION_MAJOR < 56)
#endif
#ifndef FF_API_BUFS_VDPAU
#define FF_API_BUFS_VDPAU        (LIBAVCODEC_VERSION_MAJOR < 56)
#endif
#ifndef FF_API_VOXWARE
#define FF_API_VOXWARE           (LIBAVCODEC_VERSION_MAJOR < 56)
#endif
#ifndef FF_API_SET_DIMENSIONS
#define FF_API_SET_DIMENSIONS    (LIBAVCODEC_VERSION_MAJOR < 56)
#endif
#ifndef FF_API_DEBUG_MV
#define FF_API_DEBUG_MV          (LIBAVCODEC_VERSION_MAJOR < 56)
#endif
#ifndef FF_API_AC_VLC
#define FF_API_AC_VLC            (LIBAVCODEC_VERSION_MAJOR < 56)
#endif
#ifndef FF_API_OLD_MSMPEG4
#define FF_API_OLD_MSMPEG4       (LIBAVCODEC_VERSION_MAJOR < 56)
#endif
#ifndef FF_API_ASPECT_EXTENDED
#define FF_API_ASPECT_EXTENDED   (LIBAVCODEC_VERSION_MAJOR < 56)
#endif
#ifndef FF_API_THREAD_OPAQUE
#define FF_API_THREAD_OPAQUE     (LIBAVCODEC_VERSION_MAJOR < 56)
#endif
#ifndef FF_API_CODEC_PKT
#define FF_API_CODEC_PKT         (LIBAVCODEC_VERSION_MAJOR < 56)
#endif
#ifndef FF_API_ARCH_ALPHA
#define FF_API_ARCH_ALPHA        (LIBAVCODEC_VERSION_MAJOR < 56)
#endif
#ifndef FF_API_XVMC
#define FF_API_XVMC              (LIBAVCODEC_VERSION_MAJOR < 56)
#endif
#ifndef FF_API_ERROR_RATE
#define FF_API_ERROR_RATE        (LIBAVCODEC_VERSION_MAJOR < 56)
#endif
#ifndef FF_API_QSCALE_TYPE
#define FF_API_QSCALE_TYPE       (LIBAVCODEC_VERSION_MAJOR < 56)
#endif
#ifndef FF_API_MB_TYPE
#define FF_API_MB_TYPE           (LIBAVCODEC_VERSION_MAJOR < 56)
#endif
#ifndef FF_API_MAX_BFRAMES
#define FF_API_MAX_BFRAMES       (LIBAVCODEC_VERSION_MAJOR < 56)
#endif
#ifndef FF_API_FAST_MALLOC
#define FF_API_FAST_MALLOC       (LIBAVCODEC_VERSION_MAJOR < 56)
#endif
#ifndef FF_API_NEG_LINESIZES
#define FF_API_NEG_LINESIZES     (LIBAVCODEC_VERSION_MAJOR < 56)
#endif
#ifndef FF_API_EMU_EDGE
#define FF_API_EMU_EDGE          (LIBAVCODEC_VERSION_MAJOR < 56)
#endif
#ifndef FF_API_ARCH_SH4
#define FF_API_ARCH_SH4          (LIBAVCODEC_VERSION_MAJOR < 56)
#endif
#ifndef FF_API_ARCH_SPARC
#define FF_API_ARCH_SPARC        (LIBAVCODEC_VERSION_MAJOR < 56)
#endif
#ifndef FF_API_INPUT_PRESERVED
#define FF_API_INPUT_PRESERVED   (LIBAVCODEC_VERSION_MAJOR < 57)
#endif
#ifndef FF_API_NORMALIZE_AQP
#define FF_API_NORMALIZE_AQP     (LIBAVCODEC_VERSION_MAJOR < 57)
#endif
#ifndef FF_API_GMC
#define FF_API_GMC               (LIBAVCODEC_VERSION_MAJOR < 57)
#endif
#ifndef FF_API_MV0
#define FF_API_MV0               (LIBAVCODEC_VERSION_MAJOR < 57)
#endif
#ifndef FF_API_CODEC_NAME
#define FF_API_CODEC_NAME        (LIBAVCODEC_VERSION_MAJOR < 57)
#endif

#endif /* AVCODEC_VERSION_H */
