/*
 * "Real" compatible muxer and demuxer common code.
 * Copyright (c) 2009  Aurelien Jacobs <aurel@gnuage.org>
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

#include "rm.h"

const char * const ff_rm_metadata[4] = {
    "title",
    "author",
    "copyright",
    "comment"
};

const AVCodecTag ff_rm_codec_tags[] = {
    { CODEC_ID_RV10,   MKTAG('R','V','1','0') },
    { CODEC_ID_RV20,   MKTAG('R','V','2','0') },
    { CODEC_ID_RV20,   MKTAG('R','V','T','R') },
    { CODEC_ID_RV30,   MKTAG('R','V','3','0') },
    { CODEC_ID_RV40,   MKTAG('R','V','4','0') },
    { CODEC_ID_AC3,    MKTAG('d','n','e','t') },
    { CODEC_ID_RA_144, MKTAG('l','p','c','J') },
    { CODEC_ID_RA_288, MKTAG('2','8','_','8') },
    { CODEC_ID_COOK,   MKTAG('c','o','o','k') },
    { CODEC_ID_ATRAC3, MKTAG('a','t','r','c') },
    { CODEC_ID_SIPR,   MKTAG('s','i','p','r') },
    { CODEC_ID_AAC,    MKTAG('r','a','a','c') },
    { CODEC_ID_AAC,    MKTAG('r','a','c','p') },
    { CODEC_ID_RALF,   MKTAG('L','S','D',':') },
    { CODEC_ID_NONE },
};
