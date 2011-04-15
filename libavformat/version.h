/*
 * Version macros.
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

#ifndef AVFORMAT_VERSION_H
#define AVFORMAT_VERSION_H

#include "libavutil/avutil.h"

#define LIBAVFORMAT_VERSION_MAJOR 52
#define LIBAVFORMAT_VERSION_MINOR 108
#define LIBAVFORMAT_VERSION_MICRO  0

#define LIBAVFORMAT_VERSION_INT AV_VERSION_INT(LIBAVFORMAT_VERSION_MAJOR, \
                                               LIBAVFORMAT_VERSION_MINOR, \
                                               LIBAVFORMAT_VERSION_MICRO)
#define LIBAVFORMAT_VERSION     AV_VERSION(LIBAVFORMAT_VERSION_MAJOR,   \
                                           LIBAVFORMAT_VERSION_MINOR,   \
                                           LIBAVFORMAT_VERSION_MICRO)
#define LIBAVFORMAT_BUILD       LIBAVFORMAT_VERSION_INT

#define LIBAVFORMAT_IDENT       "Lavf" AV_STRINGIFY(LIBAVFORMAT_VERSION)

/**
 * Those FF_API_* defines are not part of public API.
 * They may change, break or disappear at any time.
 */
#ifndef FF_API_MAX_STREAMS
#define FF_API_MAX_STREAMS             (LIBAVFORMAT_VERSION_MAJOR < 53)
#endif
#ifndef FF_API_OLD_METADATA
#define FF_API_OLD_METADATA            (LIBAVFORMAT_VERSION_MAJOR < 53)
#endif
#ifndef FF_API_OLD_METADATA2
#define FF_API_OLD_METADATA2           (LIBAVFORMAT_VERSION_MAJOR < 54)
#endif
#ifndef FF_API_URL_CLASS
#define FF_API_URL_CLASS               (LIBAVFORMAT_VERSION_MAJOR >= 53)
#endif
#ifndef FF_API_URL_RESETBUF
#define FF_API_URL_RESETBUF            (LIBAVFORMAT_VERSION_MAJOR < 53)
#endif
#ifndef FF_API_REGISTER_PROTOCOL
#define FF_API_REGISTER_PROTOCOL       (LIBAVFORMAT_VERSION_MAJOR < 53)
#endif
#ifndef FF_API_GUESS_FORMAT
#define FF_API_GUESS_FORMAT            (LIBAVFORMAT_VERSION_MAJOR < 53)
#endif
#ifndef FF_API_UDP_GET_FILE
#define FF_API_UDP_GET_FILE            (LIBAVFORMAT_VERSION_MAJOR < 53)
#endif
#ifndef FF_API_URL_SPLIT
#define FF_API_URL_SPLIT               (LIBAVFORMAT_VERSION_MAJOR < 53)
#endif
#ifndef FF_API_ALLOC_FORMAT_CONTEXT
#define FF_API_ALLOC_FORMAT_CONTEXT    (LIBAVFORMAT_VERSION_MAJOR < 53)
#endif
#ifndef FF_API_PARSE_FRAME_PARAM
#define FF_API_PARSE_FRAME_PARAM       (LIBAVFORMAT_VERSION_MAJOR < 53)
#endif
#ifndef FF_API_READ_SEEK
#define FF_API_READ_SEEK               (LIBAVFORMAT_VERSION_MAJOR < 54)
#endif
#ifndef FF_API_LAVF_UNUSED
#define FF_API_LAVF_UNUSED             (LIBAVFORMAT_VERSION_MAJOR < 53)
#endif
#ifndef FF_API_PARAMETERS_CODEC_ID
#define FF_API_PARAMETERS_CODEC_ID     (LIBAVFORMAT_VERSION_MAJOR < 53)
#endif
#ifndef FF_API_FIRST_FORMAT
#define FF_API_FIRST_FORMAT            (LIBAVFORMAT_VERSION_MAJOR < 53)
#endif
#ifndef FF_API_SYMVER
#define FF_API_SYMVER                  (LIBAVFORMAT_VERSION_MAJOR < 53)
#endif
#ifndef FF_API_OLD_AVIO
#define FF_API_OLD_AVIO                (LIBAVFORMAT_VERSION_MAJOR < 54)
#endif
#ifndef FF_API_INDEX_BUILT
#define FF_API_INDEX_BUILT             (LIBAVFORMAT_VERSION_MAJOR < 53)
#endif
#ifndef FF_API_DUMP_FORMAT
#define FF_API_DUMP_FORMAT             (LIBAVFORMAT_VERSION_MAJOR < 54)
#endif
#ifndef FF_API_PARSE_DATE
#define FF_API_PARSE_DATE              (LIBAVFORMAT_VERSION_MAJOR < 54)
#endif
#ifndef FF_API_FIND_INFO_TAG
#define FF_API_FIND_INFO_TAG           (LIBAVFORMAT_VERSION_MAJOR < 54)
#endif
#ifndef FF_API_PKT_DUMP
#define FF_API_PKT_DUMP                (LIBAVFORMAT_VERSION_MAJOR < 54)
#endif
#ifndef FF_API_GUESS_IMG2_CODEC
#define FF_API_GUESS_IMG2_CODEC        (LIBAVFORMAT_VERSION_MAJOR < 54)
#endif
#ifndef FF_API_SDP_CREATE
#define FF_API_SDP_CREATE              (LIBAVFORMAT_VERSION_MAJOR < 54)
#endif

#endif //AVFORMAT_VERSION_H
