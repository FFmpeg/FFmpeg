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

#include "voc.h"

const unsigned char voc_magic[21] = "Creative Voice File\x1A";

const AVCodecTag voc_codec_tags[] = {
    {CODEC_ID_PCM_U8,        0x00},
    {CODEC_ID_ADPCM_SBPRO_4, 0x01},
    {CODEC_ID_ADPCM_SBPRO_3, 0x02},
    {CODEC_ID_ADPCM_SBPRO_2, 0x03},
    {CODEC_ID_PCM_S16LE,     0x04},
    {CODEC_ID_PCM_ALAW,      0x06},
    {CODEC_ID_PCM_MULAW,     0x07},
    {CODEC_ID_ADPCM_CT,    0x0200},
    {0, 0},
};
