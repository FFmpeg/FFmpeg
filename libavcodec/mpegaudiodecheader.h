/*
 * MPEG Audio header decoder
 * Copyright (c) 2001, 2002 Fabrice Bellard
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
 * @file libavcodec/mpegaudiodecheader.c
 * MPEG Audio header decoder.
 */

#ifndef AVCODEC_MPEGAUDIODECHEADER_H
#define AVCODEC_MPEGAUDIODECHEADER_H

#include "libavutil/common.h"
#include "mpegaudio.h"


/* header decoding. MUST check the header before because no
   consistency check is done there. Return 1 if free format found and
   that the frame size must be computed externally */
int ff_mpegaudio_decode_header(MPADecodeHeader *s, uint32_t header);

#endif /* AVCODEC_MPEGAUDIODECHEADER_H */
