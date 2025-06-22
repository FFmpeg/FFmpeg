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

#ifndef AVCODEC_MPEG_ER_H
#define AVCODEC_MPEG_ER_H

#include "mpegvideo.h"

int ff_mpeg_er_init(MpegEncContext *s);
void ff_mpeg_er_frame_start(MpegEncContext *s);

static inline void ff_mpv_er_frame_start_ext(MPVContext *const s, int partitioned_frame,
                                             uint16_t pp_time, uint16_t pb_time)
{
    s->er.partitioned_frame = partitioned_frame;
    s->er.pp_time           = pp_time;
    s->er.pb_time           = pb_time;
    ff_mpeg_er_frame_start(s);
}

#endif /* AVCODEC_MPEG_ER_H */
