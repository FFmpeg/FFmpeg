/*
 * Sony OpenMG (OMA) common data
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

#include <stddef.h>
#include "libavcodec/codec_id.h"
#include "internal.h"
#include "oma.h"

const uint16_t ff_oma_srate_tab[8] = { 320, 441, 480, 882, 960, 0 };

const AVCodecTag ff_oma_codec_tags[] = {
    { AV_CODEC_ID_ATRAC3,      OMA_CODECID_ATRAC3    },
    { AV_CODEC_ID_ATRAC3P,     OMA_CODECID_ATRAC3P   },
    { AV_CODEC_ID_MP3,         OMA_CODECID_MP3       },
    { AV_CODEC_ID_PCM_S16BE,   OMA_CODECID_LPCM      },
    { AV_CODEC_ID_ATRAC3PAL,   OMA_CODECID_ATRAC3PAL },
    { AV_CODEC_ID_ATRAC3AL,    OMA_CODECID_ATRAC3AL  },
    { 0 },
};

const AVCodecTag *const ff_oma_codec_tags_list[] = { ff_oma_codec_tags, NULL };
