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

static void mpeg_er_decode_mb(void *opaque, int ref, int mv_dir, int mv_type,
                              int (*mv)[2][4][2], int mb_x, int mb_y,
                              int mb_intra, int mb_skipped)
{
    MpegEncContext *s = opaque;

    s->mv_dir     = mv_dir;
    s->mv_type    = mv_type;
    s->mb_intra   = mb_intra;
    s->mb_skipped = mb_skipped;
    s->mb_x       = mb_x;
    s->mb_y       = mb_y;
    memcpy(s->mv, mv, sizeof(*mv));

    ff_init_block_index(s);
    ff_update_block_index(s);

    s->bdsp.clear_blocks(s->block[0]);

    s->dest[0] = s->current_picture.f->data[0] +
                 s->mb_y * 16 * s->linesize +
                 s->mb_x * 16;
    s->dest[1] = s->current_picture.f->data[1] +
                 s->mb_y * (16 >> s->chroma_y_shift) * s->uvlinesize +
                 s->mb_x * (16 >> s->chroma_x_shift);
    s->dest[2] = s->current_picture.f->data[2] +
                 s->mb_y * (16 >> s->chroma_y_shift) * s->uvlinesize +
                 s->mb_x * (16 >> s->chroma_x_shift);

    if (ref)
        av_log(s->avctx, AV_LOG_DEBUG,
               "Interlaced error concealment is not fully implemented\n");
    ff_mpv_decode_mb(s, s->block);
}

int ff_mpeg_er_init(MpegEncContext *s)
{
    ERContext *er = &s->er;
    int mb_array_size = s->mb_height * s->mb_stride;
    int i;

    er->avctx       = s->avctx;

    er->mb_index2xy = s->mb_index2xy;
    er->mb_num      = s->mb_num;
    er->mb_width    = s->mb_width;
    er->mb_height   = s->mb_height;
    er->mb_stride   = s->mb_stride;
    er->b8_stride   = s->b8_stride;

    er->er_temp_buffer     = av_malloc(s->mb_height * s->mb_stride);
    er->error_status_table = av_mallocz(mb_array_size);
    if (!er->er_temp_buffer || !er->error_status_table)
        goto fail;

    er->mbskip_table  = s->mbskip_table;
    er->mbintra_table = s->mbintra_table;

    for (i = 0; i < FF_ARRAY_ELEMS(s->dc_val); i++)
        er->dc_val[i] = s->dc_val[i];

    er->decode_mb = mpeg_er_decode_mb;
    er->opaque    = s;

    return 0;
fail:
    av_freep(&er->er_temp_buffer);
    av_freep(&er->error_status_table);
    return AVERROR(ENOMEM);
}
