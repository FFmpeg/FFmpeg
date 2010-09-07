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

#ifndef AVCORE_AVCORE_H
#define AVCORE_AVCORE_H

/**
 * @file
 * shared media utilities for the libav* libraries
 */

#include "libavutil/avutil.h"

#define LIBAVCORE_VERSION_MAJOR  0
#define LIBAVCORE_VERSION_MINOR  9
#define LIBAVCORE_VERSION_MICRO  0

#define LIBAVCORE_VERSION_INT   AV_VERSION_INT(LIBAVCORE_VERSION_MAJOR, \
                                               LIBAVCORE_VERSION_MINOR, \
                                               LIBAVCORE_VERSION_MICRO)
#define LIBAVCORE_VERSION       AV_VERSION(LIBAVCORE_VERSION_MAJOR,     \
                                           LIBAVCORE_VERSION_MINOR,     \
                                           LIBAVCORE_VERSION_MICRO)
#define LIBAVCORE_BUILD         LIBAVCORE_VERSION_INT

#define LIBAVCORE_IDENT         "Lavcore" AV_STRINGIFY(LIBAVCORE_VERSION)

/**
 * Return the LIBAVCORE_VERSION_INT constant.
 */
unsigned avcore_version(void);

/**
 * Return the libavcore build-time configuration.
 */
const char *avcore_configuration(void);

/**
 * Return the libavcore license.
 */
const char *avcore_license(void);

/**
 * Those FF_API_* defines are not part of public API.
 * They may change, break or disappear at any time.
 */
#ifndef FF_API_OLD_IMAGE_NAMES
#define FF_API_OLD_IMAGE_NAMES (LIBAVCORE_VERSION_MAJOR < 1)
#endif

#endif /* AVCORE_AVCORE_H */
