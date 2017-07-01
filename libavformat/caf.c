/*
 * CAF common code
 * Copyright (c) 2007  Justin Ruggles
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
 * CAF common code
 */

#include "avformat.h"
#include "internal.h"
#include "caf.h"

/**
 * Known codec tags for CAF
 */
const AVCodecTag ff_codec_caf_tags[] = {
    { AV_CODEC_ID_AAC,             MKBETAG('a','a','c',' ') },
    { AV_CODEC_ID_AC3,             MKBETAG('a','c','-','3') },
    { AV_CODEC_ID_ALAC,            MKBETAG('a','l','a','c') },
  /* FIXME: use DV demuxer, as done in MOV */
  /*{ AV_CODEC_ID_DVAUDIO,         MKBETAG('v','d','v','a') },*/
  /*{ AV_CODEC_ID_DVAUDIO,         MKBETAG('d','v','c','a') },*/
    { AV_CODEC_ID_ADPCM_IMA_QT,    MKBETAG('i','m','a','4') },
    { AV_CODEC_ID_MACE3,           MKBETAG('M','A','C','3') },
    { AV_CODEC_ID_MACE6,           MKBETAG('M','A','C','6') },
    { AV_CODEC_ID_MP3,             MKBETAG('.','m','p','3') },
    { AV_CODEC_ID_MP2,             MKBETAG('.','m','p','2') },
    { AV_CODEC_ID_MP1,             MKBETAG('.','m','p','1') },
    { AV_CODEC_ID_PCM_ALAW,        MKBETAG('a','l','a','w') },
    { AV_CODEC_ID_PCM_MULAW,       MKBETAG('u','l','a','w') },
    { AV_CODEC_ID_QCELP,           MKBETAG('Q','c','l','p') },
    { AV_CODEC_ID_QDM2,            MKBETAG('Q','D','M','2') },
    { AV_CODEC_ID_QDM2,            MKBETAG('Q','D','M','C') },
    { AV_CODEC_ID_OPUS,            MKBETAG('o','p','u','s') },
  /* currently unsupported codecs */
  /*{ AC-3 over S/PDIF          MKBETAG('c','a','c','3') },*/
  /*{ MPEG4CELP                 MKBETAG('c','e','l','p') },*/
  /*{ MPEG4HVXC                 MKBETAG('h','v','x','c') },*/
  /*{ MPEG4TwinVQ               MKBETAG('t','w','v','q') },*/
    { AV_CODEC_ID_NONE,            0 },
};
