/*
 * CAF common code
 * Copyright (c) 2007  Justin Ruggles
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

/**
 * @file
 * CAF common code
 */

#include "avformat.h"
#include "internal.h"
#include "caf.h"

/**
 * Known codec tags for CAF
 */
const AVCodecTag ff_codec_caf_tags[] = {
    { AV_CODEC_ID_AAC,             MKTAG('a','a','c',' ') },
    { AV_CODEC_ID_AC3,             MKTAG('a','c','-','3') },
    { AV_CODEC_ID_ADPCM_IMA_QT,    MKTAG('i','m','a','4') },
    { AV_CODEC_ID_ADPCM_IMA_WAV,   MKTAG('m','s', 0, 17 ) },
    { AV_CODEC_ID_ADPCM_MS,        MKTAG('m','s', 0,  2 ) },
    { AV_CODEC_ID_ALAC,            MKTAG('a','l','a','c') },
    { AV_CODEC_ID_AMR_NB,          MKTAG('s','a','m','r') },
  /* FIXME: use DV demuxer, as done in MOV */
  /*{ AV_CODEC_ID_DVAUDIO,         MKTAG('v','d','v','a') },*/
  /*{ AV_CODEC_ID_DVAUDIO,         MKTAG('d','v','c','a') },*/
    { AV_CODEC_ID_GSM,             MKTAG('a','g','s','m') },
    { AV_CODEC_ID_GSM_MS,          MKTAG('m','s', 0, '1') },
    { AV_CODEC_ID_ILBC,            MKTAG('i','l','b','c') },
    { AV_CODEC_ID_MACE3,           MKTAG('M','A','C','3') },
    { AV_CODEC_ID_MACE6,           MKTAG('M','A','C','6') },
    { AV_CODEC_ID_MP1,             MKTAG('.','m','p','1') },
    { AV_CODEC_ID_MP2,             MKTAG('.','m','p','2') },
    { AV_CODEC_ID_MP3,             MKTAG('.','m','p','3') },
    { AV_CODEC_ID_MP3,             MKTAG('m','s', 0 ,'U') },
    { AV_CODEC_ID_PCM_ALAW,        MKTAG('a','l','a','w') },
    { AV_CODEC_ID_PCM_MULAW,       MKTAG('u','l','a','w') },
    { AV_CODEC_ID_QCELP,           MKTAG('Q','c','l','p') },
    { AV_CODEC_ID_QDM2,            MKTAG('Q','D','M','2') },
    { AV_CODEC_ID_QDM2,            MKTAG('Q','D','M','C') },
  /* currently unsupported codecs */
  /*{ AC-3 over S/PDIF          MKTAG('c','a','c','3') },*/
  /*{ MPEG4CELP                 MKTAG('c','e','l','p') },*/
  /*{ MPEG4HVXC                 MKTAG('h','v','x','c') },*/
  /*{ MPEG4TwinVQ               MKTAG('t','w','v','q') },*/
    { AV_CODEC_ID_NONE,            0 },
};

