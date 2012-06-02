/*
 *
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

#ifndef AVCODEC_VERSION_H
#define AVCODEC_VERSION_H

/**
 * @file
 * @ingroup libavc
 * Libavcodec version macros.
 */

#define LIBAVCODEC_VERSION_MAJOR 54
#define LIBAVCODEC_VERSION_MINOR 14
#define LIBAVCODEC_VERSION_MICRO  0

#define LIBAVCODEC_VERSION_INT  AV_VERSION_INT(LIBAVCODEC_VERSION_MAJOR, \
                                               LIBAVCODEC_VERSION_MINOR, \
                                               LIBAVCODEC_VERSION_MICRO)
#define LIBAVCODEC_VERSION      AV_VERSION(LIBAVCODEC_VERSION_MAJOR,    \
                                           LIBAVCODEC_VERSION_MINOR,    \
                                           LIBAVCODEC_VERSION_MICRO)
#define LIBAVCODEC_BUILD        LIBAVCODEC_VERSION_INT

#define LIBAVCODEC_IDENT        "Lavc" AV_STRINGIFY(LIBAVCODEC_VERSION)

/**
 * Those FF_API_* defines are not part of public API.
 * They may change, break or disappear at any time.
 */
#ifndef FF_API_REQUEST_CHANNELS
#define FF_API_REQUEST_CHANNELS (LIBAVCODEC_VERSION_MAJOR < 55)
#endif
#ifndef FF_API_OLD_DECODE_AUDIO
#define FF_API_OLD_DECODE_AUDIO (LIBAVCODEC_VERSION_MAJOR < 55)
#endif
#ifndef FF_API_OLD_ENCODE_AUDIO
#define FF_API_OLD_ENCODE_AUDIO (LIBAVCODEC_VERSION_MAJOR < 55)
#endif
#ifndef FF_API_OLD_ENCODE_VIDEO
#define FF_API_OLD_ENCODE_VIDEO (LIBAVCODEC_VERSION_MAJOR < 55)
#endif
#ifndef FF_API_MPV_GLOBAL_OPTS
#define FF_API_MPV_GLOBAL_OPTS  (LIBAVCODEC_VERSION_MAJOR < 55)
#endif
#ifndef FF_API_COLOR_TABLE_ID
#define FF_API_COLOR_TABLE_ID   (LIBAVCODEC_VERSION_MAJOR < 55)
#endif
#ifndef FF_API_INTER_THRESHOLD
#define FF_API_INTER_THRESHOLD  (LIBAVCODEC_VERSION_MAJOR < 55)
#endif
#ifndef FF_API_SUB_ID
#define FF_API_SUB_ID           (LIBAVCODEC_VERSION_MAJOR < 55)
#endif
#ifndef FF_API_DSP_MASK
#define FF_API_DSP_MASK         (LIBAVCODEC_VERSION_MAJOR < 55)
#endif

#endif /* AVCODEC_VERSION_H */
