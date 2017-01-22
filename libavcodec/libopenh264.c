/*
 * OpenH264 shared utils
 * Copyright (C) 2014 Martin Storsjo
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

#include <string.h>
#include <wels/codec_api.h>
#include <wels/codec_ver.h>

#include "libavutil/log.h"

#include "libopenh264.h"

// Convert libopenh264 log level to equivalent ffmpeg log level.
static int libopenh264_to_ffmpeg_log_level(int libopenh264_log_level)
{
    if      (libopenh264_log_level >= WELS_LOG_DETAIL)  return AV_LOG_TRACE;
    else if (libopenh264_log_level >= WELS_LOG_DEBUG)   return AV_LOG_DEBUG;
    else if (libopenh264_log_level >= WELS_LOG_INFO)    return AV_LOG_VERBOSE;
    else if (libopenh264_log_level >= WELS_LOG_WARNING) return AV_LOG_WARNING;
    else if (libopenh264_log_level >= WELS_LOG_ERROR)   return AV_LOG_ERROR;
    else                                                return AV_LOG_QUIET;
}

void ff_libopenh264_trace_callback(void *ctx, int level, const char *msg)
{
    // The message will be logged only if the requested EQUIVALENT ffmpeg log level is
    // less than or equal to the current ffmpeg log level.
    int equiv_ffmpeg_log_level = libopenh264_to_ffmpeg_log_level(level);
    av_log(ctx, equiv_ffmpeg_log_level, "%s\n", msg);
}

int ff_libopenh264_check_version(void *logctx)
{
    // Mingw GCC < 4.7 on x86_32 uses an incorrect/buggy ABI for the WelsGetCodecVersion
    // function (for functions returning larger structs), thus skip the check in those
    // configurations.
#if !defined(_WIN32) || !defined(__GNUC__) || !ARCH_X86_32 || AV_GCC_VERSION_AT_LEAST(4, 7)
    OpenH264Version libver = WelsGetCodecVersion();
    if (memcmp(&libver, &g_stCodecVersion, sizeof(libver))) {
        av_log(logctx, AV_LOG_ERROR, "Incorrect library version loaded\n");
        return AVERROR(EINVAL);
    }
#endif
    return 0;
}
