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

#include <assert.h>

#include "config.h"

#include "avcodec.h"
#include "codec_id.h"
#include "version.h"

#include "libavutil/ffversion.h"
const char av_codec_ffversion[] = "FFmpeg version " FFMPEG_VERSION;

unsigned avcodec_version(void)
{
    static_assert(AV_CODEC_ID_DNXUC        ==   270 &&
                  AV_CODEC_ID_PCM_SGA      == 65572 &&
                  AV_CODEC_ID_ADPCM_XMD    == 69683 &&
                  AV_CODEC_ID_CBD2_DPCM    == 81928 &&
                  AV_CODEC_ID_QOA          == 86121 &&
                  AV_CODEC_ID_IVTV_VBI     == 94234 &&
                  AV_CODEC_ID_SMPTE_2038   == 98315,
                  "Don't insert new codec ids in the middle of a list");
    static_assert(LIBAVCODEC_VERSION_MICRO >= 100, "micro version starts at 100");

    return LIBAVCODEC_VERSION_INT;
}

const char *avcodec_configuration(void)
{
    return FFMPEG_CONFIGURATION;
}

const char *avcodec_license(void)
{
#define LICENSE_PREFIX "libavcodec license: "
    return &LICENSE_PREFIX FFMPEG_LICENSE[sizeof(LICENSE_PREFIX) - 1];
}
