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

#ifndef AVRESAMPLE_VERSION_H
#define AVRESAMPLE_VERSION_H

#define LIBAVRESAMPLE_VERSION_MAJOR  1
#define LIBAVRESAMPLE_VERSION_MINOR  0
#define LIBAVRESAMPLE_VERSION_MICRO  1

#define LIBAVRESAMPLE_VERSION_INT  AV_VERSION_INT(LIBAVRESAMPLE_VERSION_MAJOR, \
                                                  LIBAVRESAMPLE_VERSION_MINOR, \
                                                  LIBAVRESAMPLE_VERSION_MICRO)
#define LIBAVRESAMPLE_VERSION          AV_VERSION(LIBAVRESAMPLE_VERSION_MAJOR, \
                                                  LIBAVRESAMPLE_VERSION_MINOR, \
                                                  LIBAVRESAMPLE_VERSION_MICRO)
#define LIBAVRESAMPLE_BUILD        LIBAVRESAMPLE_VERSION_INT

#define LIBAVRESAMPLE_IDENT        "Lavr" AV_STRINGIFY(LIBAVRESAMPLE_VERSION)

/**
 * FF_API_* defines may be placed below to indicate public API that will be
 * dropped at a future version bump. The defines themselves are not part of
 * the public API and may change, break or disappear at any time.
 */

#ifndef FF_API_RESAMPLE_CLOSE_OPEN
#define FF_API_RESAMPLE_CLOSE_OPEN (LIBAVRESAMPLE_VERSION_MAJOR < 2)
#endif

#endif /* AVRESAMPLE_VERSION_H */
