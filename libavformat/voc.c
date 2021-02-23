/*
 * Creative Voice File common data.
 * Copyright (c) 2006  Aurelien Jacobs <aurel@gnuage.org>
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

#include "config.h"
#include "voc.h"
#include "internal.h"


const AVCodecTag ff_voc_codec_tags[] = {
    {AV_CODEC_ID_PCM_U8,        0x00},
    {AV_CODEC_ID_ADPCM_SBPRO_4, 0x01},
    {AV_CODEC_ID_ADPCM_SBPRO_3, 0x02},
    {AV_CODEC_ID_ADPCM_SBPRO_2, 0x03},
    {AV_CODEC_ID_PCM_S16LE,     0x04},
    {AV_CODEC_ID_PCM_ALAW,      0x06},
    {AV_CODEC_ID_PCM_MULAW,     0x07},
    {AV_CODEC_ID_ADPCM_CT,    0x0200},
    {AV_CODEC_ID_NONE,             0},
};

#if CONFIG_VOC_DEMUXER || CONFIG_VOC_MUXER
const unsigned char ff_voc_magic[21] = "Creative Voice File\x1A";
const AVCodecTag *const ff_voc_codec_tags_list[] = { ff_voc_codec_tags, NULL };
#endif
