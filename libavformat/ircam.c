/*
 * IRCAM common code
 * Copyright (c) 2012 Paul B Mahol
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

#include "avformat.h"
#include "internal.h"

const AVCodecTag ff_codec_ircam_le_tags[] = {
    { AV_CODEC_ID_PCM_ALAW,  0x10001 },
    { AV_CODEC_ID_PCM_F32LE, 0x00004 },
    { AV_CODEC_ID_PCM_F64LE, 0x00008 },
    { AV_CODEC_ID_PCM_MULAW, 0x20001 },
    { AV_CODEC_ID_PCM_S16LE, 0x00002 },
    { AV_CODEC_ID_PCM_S24LE, 0x00003 },
    { AV_CODEC_ID_PCM_S32LE, 0x40004 },
    { AV_CODEC_ID_PCM_S8,    0x00001 },
    { AV_CODEC_ID_NONE,      0       },
};

const AVCodecTag ff_codec_ircam_be_tags[] = {
    { AV_CODEC_ID_PCM_ALAW,  0x10001 },
    { AV_CODEC_ID_PCM_F32BE, 0x00004 },
    { AV_CODEC_ID_PCM_F64BE, 0x00008 },
    { AV_CODEC_ID_PCM_MULAW, 0x20001 },
    { AV_CODEC_ID_PCM_S16BE, 0x00002 },
    { AV_CODEC_ID_PCM_S24BE, 0x00003 },
    { AV_CODEC_ID_PCM_S32BE, 0x40004 },
    { AV_CODEC_ID_PCM_S8,    0x00001 },
    { AV_CODEC_ID_NONE,      0       },
};
