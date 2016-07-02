/*
 * MPEG-1/2 VLC
 * copyright (c) 2000,2001 Fabrice Bellard
 * copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
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
 * MPEG-1/2 VLC.
 */

#ifndef AVCODEC_MPEG12VLC_H
#define AVCODEC_MPEG12VLC_H

#include "vlc.h"

#define DC_VLC_BITS 9
#define MV_VLC_BITS 9
#define TEX_VLC_BITS 9

#define MBINCR_VLC_BITS 9
#define MB_PAT_VLC_BITS 9
#define MB_PTYPE_VLC_BITS 6
#define MB_BTYPE_VLC_BITS 6

extern VLC ff_dc_lum_vlc;
extern VLC ff_dc_chroma_vlc;
extern VLC ff_mbincr_vlc;
extern VLC ff_mb_ptype_vlc;
extern VLC ff_mb_btype_vlc;
extern VLC ff_mb_pat_vlc;
extern VLC ff_mv_vlc;

void ff_mpeg12_init_vlcs(void);

#endif /* AVCODEC_MPEG12VLC_H */
