/*
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

#include "intmath.h"

/* undef these to get the function prototypes from common.h */
#undef av_log2
#undef av_log2_16bit
#include "common.h"

int av_log2(unsigned v)
{
    return ff_log2(v);
}

int av_log2_16bit(unsigned v)
{
    return ff_log2_16bit(v);
}

int av_ctz(int v)
{
    return ff_ctz(v);
}
