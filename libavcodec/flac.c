/*
 * FLAC common code
 * Copyright (c) 2009 Justin Ruggles
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

#include "flac.h"

int ff_flac_get_max_frame_size(int blocksize, int ch, int bps)
{
    /* Technically, there is no limit to FLAC frame size, but an encoder
       should not write a frame that is larger than if verbatim encoding mode
       were to be used. */

    int count;

    count = 16;                  /* frame header */
    count += ch * ((7+bps+7)/8); /* subframe headers */
    if (ch == 2) {
        /* for stereo, need to account for using decorrelation */
        count += (( 2*bps+1) * blocksize + 7) / 8;
    } else {
        count += ( ch*bps    * blocksize + 7) / 8;
    }
    count += 2; /* frame footer */

    return count;
}
