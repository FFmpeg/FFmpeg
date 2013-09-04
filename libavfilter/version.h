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

#ifndef AVFILTER_VERSION_H
#define AVFILTER_VERSION_H

/**
 * @file
 * @ingroup lavfi
 * Libavfilter version macros
 */

#include "libavutil/avutil.h"

#define LIBAVFILTER_VERSION_MAJOR  3
#define LIBAVFILTER_VERSION_MINOR  84
#define LIBAVFILTER_VERSION_MICRO 100

#define LIBAVFILTER_VERSION_INT AV_VERSION_INT(LIBAVFILTER_VERSION_MAJOR, \
                                               LIBAVFILTER_VERSION_MINOR, \
                                               LIBAVFILTER_VERSION_MICRO)
#define LIBAVFILTER_VERSION     AV_VERSION(LIBAVFILTER_VERSION_MAJOR,   \
                                           LIBAVFILTER_VERSION_MINOR,   \
                                           LIBAVFILTER_VERSION_MICRO)
#define LIBAVFILTER_BUILD       LIBAVFILTER_VERSION_INT

#define LIBAVFILTER_IDENT       "Lavfi" AV_STRINGIFY(LIBAVFILTER_VERSION)

/**
 * FF_API_* defines may be placed below to indicate public API that will be
 * dropped at a future version bump. The defines themselves are not part of
 * the public API and may change, break or disappear at any time.
 */

#ifndef FF_API_AVFILTERPAD_PUBLIC
#define FF_API_AVFILTERPAD_PUBLIC           (LIBAVFILTER_VERSION_MAJOR < 4)
#endif
#ifndef FF_API_FOO_COUNT
#define FF_API_FOO_COUNT                    (LIBAVFILTER_VERSION_MAJOR < 4)
#endif
#ifndef FF_API_FILL_FRAME
#define FF_API_FILL_FRAME                   (LIBAVFILTER_VERSION_MAJOR < 4)
#endif
#ifndef FF_API_BUFFERSRC_BUFFER
#define FF_API_BUFFERSRC_BUFFER             (LIBAVFILTER_VERSION_MAJOR < 4)
#endif
#ifndef FF_API_AVFILTERBUFFER
#define FF_API_AVFILTERBUFFER               (LIBAVFILTER_VERSION_MAJOR < 4)
#endif
#ifndef FF_API_OLD_FILTER_OPTS
#define FF_API_OLD_FILTER_OPTS              (LIBAVFILTER_VERSION_MAJOR < 4)
#endif
#ifndef FF_API_ACONVERT_FILTER
#define FF_API_ACONVERT_FILTER              (LIBAVFILTER_VERSION_MAJOR < 4)
#endif
#ifndef FF_API_AVFILTER_OPEN
#define FF_API_AVFILTER_OPEN                (LIBAVFILTER_VERSION_MAJOR < 4)
#endif
#ifndef FF_API_AVFILTER_INIT_FILTER
#define FF_API_AVFILTER_INIT_FILTER         (LIBAVFILTER_VERSION_MAJOR < 4)
#endif
#ifndef FF_API_OLD_FILTER_REGISTER
#define FF_API_OLD_FILTER_REGISTER          (LIBAVFILTER_VERSION_MAJOR < 4)
#endif
#ifndef FF_API_OLD_GRAPH_PARSE
#define FF_API_OLD_GRAPH_PARSE              (LIBAVFILTER_VERSION_MAJOR < 4)
#endif
#ifndef FF_API_DRAWTEXT_OLD_TIMELINE
#define FF_API_DRAWTEXT_OLD_TIMELINE        (LIBAVFILTER_VERSION_MAJOR < 4)
#endif

#endif /* AVFILTER_VERSION_H */
