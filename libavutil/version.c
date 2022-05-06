/*
 * Version functions.
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

#include <stdlib.h>

#include "config.h"
#include "avassert.h"
#include "avutil.h"
#include "common.h"
#include "libm.h"
#include "log.h"
#include "samplefmt.h"
#include "version.h"

#include "libavutil/ffversion.h"
const char av_util_ffversion[] = "FFmpeg version " FFMPEG_VERSION;

const char *av_version_info(void)
{
    return FFMPEG_VERSION;
}

unsigned avutil_version(void)
{
    av_assert0(AV_SAMPLE_FMT_DBLP == 9);
    av_assert0(AVMEDIA_TYPE_ATTACHMENT == 4);
    av_assert0(AV_PICTURE_TYPE_BI == 7);
    av_assert0(LIBAVUTIL_VERSION_MICRO >= 100);
    av_assert0(HAVE_MMX2 == HAVE_MMXEXT);

    av_assert0(((size_t)-1) > 0); // C guarantees this but if false on a platform we care about revert at least b284e1ffe343d6697fb950d1ee517bafda8a9844

    if (av_sat_dadd32(1, 2) != 5) {
        av_log(NULL, AV_LOG_FATAL, "Libavutil has been built with a broken binutils, please upgrade binutils and rebuild\n");
        abort();
    }

    if (llrint(1LL<<60) != 1LL<<60) {
        av_log(NULL, AV_LOG_ERROR, "Libavutil has been linked to a broken llrint()\n");
    }

    return LIBAVUTIL_VERSION_INT;
}

const char *avutil_configuration(void)
{
    return FFMPEG_CONFIGURATION;
}

const char *avutil_license(void)
{
#define LICENSE_PREFIX "libavutil license: "
    return &LICENSE_PREFIX FFMPEG_LICENSE[sizeof(LICENSE_PREFIX) - 1];
}
