/*
 * nut
 * Copyright (c) 2004-2007 Michael Niedermayer
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

#include "nut.h"

void ff_nut_reset_ts(NUTContext *nut, AVRational time_base, int64_t val){
    int i;
    for(i=0; i<nut->avf->nb_streams; i++){
        nut->stream[i].last_pts= av_rescale_rnd(
            val / nut->time_base_count,
            time_base.num * (int64_t)nut->stream[i].time_base->den,
            time_base.den * (int64_t)nut->stream[i].time_base->num,
            AV_ROUND_DOWN);
    }
}
