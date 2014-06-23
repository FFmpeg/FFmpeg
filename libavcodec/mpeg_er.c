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

#include "error_resilience.h"
#include "mpegvideo.h"
#include "mpeg_er.h"

static void set_erpic(ERPicture *dst, Picture *src)
{
    int i;

    memset(dst, 0, sizeof(*dst));
    if (!src) {
        dst->f  = NULL;
        dst->tf = NULL;
        return;
    }

    dst->f = src->f;
    dst->tf = &src->tf;

    for (i = 0; i < 2; i++) {
        dst->motion_val[i] = src->motion_val[i];
        dst->ref_index[i] = src->ref_index[i];
    }

    dst->mb_type = src->mb_type;
    dst->field_picture = src->field_picture;
}

void ff_mpeg_er_frame_start(MpegEncContext *s)
{
    ERContext *er = &s->er;

    set_erpic(&er->cur_pic,  s->current_picture_ptr);
    set_erpic(&er->next_pic, s->next_picture_ptr);
    set_erpic(&er->last_pic, s->last_picture_ptr);

    er->pp_time           = s->pp_time;
    er->pb_time           = s->pb_time;
    er->quarter_sample    = s->quarter_sample;
    er->partitioned_frame = s->partitioned_frame;

    ff_er_frame_start(er);
}
