/*
 * MSMPEG4 backend for ffmpeg encoder and decoder
 * copyright (c) 2001 Fabrice Bellard
 * copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
 *
 * msmpeg4v1 & v2 stuff by Michael Niedermayer <michaelni@gmx.at>
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
 * @file msmpeg4data.h
 * MSMPEG4 data tables.
 */

#ifndef MSMPEG4DATA_H
#define MSMPEG4DATA_H

#include "common.h"
#include "bitstream.h"

extern VLC ff_msmp4_mb_i_vlc;
extern VLC ff_msmp4_dc_luma_vlc[2];
extern VLC ff_msmp4_dc_chroma_vlc[2];

/* intra picture macro block coded block pattern */
extern const uint16_t ff_msmp4_mb_i_table[64][2];

#endif /* MSMPEG4DATA_H */
