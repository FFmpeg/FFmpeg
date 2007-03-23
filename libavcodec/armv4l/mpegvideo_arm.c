/*
 * Copyright (c) 2002 Michael Niedermayer
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
 *
 */

#include "../dsputil.h"
#include "../mpegvideo.h"
#include "../avcodec.h"

extern void MPV_common_init_iwmmxt(MpegEncContext *s);
extern void MPV_common_init_armv5te(MpegEncContext *s);

void MPV_common_init_armv4l(MpegEncContext *s)
{
    /* IWMMXT support is a superset of armv5te, so
     * allow optimised functions for armv5te unless
     * a better iwmmxt function exists
     */
#ifdef HAVE_ARMV5TE
    MPV_common_init_armv5te(s);
#endif
#ifdef HAVE_IWMMXT
    MPV_common_init_iwmmxt(s);
#endif
}
