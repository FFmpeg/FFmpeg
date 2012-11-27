/*
 * AIFF/AIFF-C muxer/demuxer common header
 * Copyright (c) 2006  Patrick Guimond
 *
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

/**
 * @file
 * common header for AIFF muxer and demuxer
 */

#ifndef AVFORMAT_AIFF_H
#define AVFORMAT_AIFF_H

#include "avformat.h"
#include "internal.h"

static const AVCodecTag ff_codec_aiff_tags[] = {
    { AV_CODEC_ID_PCM_S16BE,    MKTAG('N','O','N','E') },
    { AV_CODEC_ID_PCM_S8,       MKTAG('N','O','N','E') },
    { AV_CODEC_ID_PCM_S24BE,    MKTAG('N','O','N','E') },
    { AV_CODEC_ID_PCM_S32BE,    MKTAG('N','O','N','E') },
    { AV_CODEC_ID_PCM_F32BE,    MKTAG('f','l','3','2') },
    { AV_CODEC_ID_PCM_F64BE,    MKTAG('f','l','6','4') },
    { AV_CODEC_ID_PCM_ALAW,     MKTAG('a','l','a','w') },
    { AV_CODEC_ID_PCM_MULAW,    MKTAG('u','l','a','w') },
    { AV_CODEC_ID_MACE3,        MKTAG('M','A','C','3') },
    { AV_CODEC_ID_MACE6,        MKTAG('M','A','C','6') },
    { AV_CODEC_ID_GSM,          MKTAG('G','S','M',' ') },
    { AV_CODEC_ID_ADPCM_G726,   MKTAG('G','7','2','6') },
    { AV_CODEC_ID_PCM_S16BE,    MKTAG('t','w','o','s') },
    { AV_CODEC_ID_PCM_S16LE,    MKTAG('s','o','w','t') },
    { AV_CODEC_ID_ADPCM_IMA_QT, MKTAG('i','m','a','4') },
    { AV_CODEC_ID_QDM2,         MKTAG('Q','D','M','2') },
    { AV_CODEC_ID_QCELP,        MKTAG('Q','c','l','p') },
    { AV_CODEC_ID_NONE,         0 },
};

#endif /* AVFORMAT_AIFF_H */
