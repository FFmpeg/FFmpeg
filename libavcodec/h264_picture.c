/*
 * H.26L/H.264/AVC/JVT/14496-10/... decoder
 * Copyright (c) 2003 Michael Niedermayer <michaelni@gmx.at>
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
 * H.264 / AVC / MPEG-4 part10 codec.
 * @author Michael Niedermayer <michaelni@gmx.at>
 */

#include "libavutil/avassert.h"
#include "libavutil/emms.h"
#include "error_resilience.h"
#include "avcodec.h"
#include "h264dec.h"
#include "hwaccel_internal.h"
#include "mpegutils.h"
#include "libavutil/refstruct.h"
#include "thread.h"
#include "threadframe.h"

void ff_h264_unref_picture(H264Picture *pic)
{
    int off = offsetof(H264Picture, f_grain) + sizeof(pic->f_grain);
    int i;

    if (!pic->f || !pic->f->buf[0])
        return;

    ff_thread_release_ext_buffer(&pic->tf);
    av_frame_unref(pic->f_grain);
    av_refstruct_unref(&pic->hwaccel_picture_private);

    av_refstruct_unref(&pic->qscale_table_base);
    av_refstruct_unref(&pic->mb_type_base);
    av_refstruct_unref(&pic->pps);
    for (i = 0; i < 2; i++) {
        av_refstruct_unref(&pic->motion_val_base[i]);
        av_refstruct_unref(&pic->ref_index[i]);
    }
    av_refstruct_unref(&pic->decode_error_flags);

    memset((uint8_t*)pic + off, 0, sizeof(*pic) - off);
}

static void h264_copy_picture_params(H264Picture *dst, const H264Picture *src)
{
    av_refstruct_replace(&dst->qscale_table_base, src->qscale_table_base);
    av_refstruct_replace(&dst->mb_type_base,      src->mb_type_base);
    av_refstruct_replace(&dst->pps, src->pps);

    for (int i = 0; i < 2; i++) {
        av_refstruct_replace(&dst->motion_val_base[i], src->motion_val_base[i]);
        av_refstruct_replace(&dst->ref_index[i],       src->ref_index[i]);
    }

    av_refstruct_replace(&dst->hwaccel_picture_private,
                          src->hwaccel_picture_private);

    av_refstruct_replace(&dst->decode_error_flags, src->decode_error_flags);

    dst->qscale_table = src->qscale_table;
    dst->mb_type      = src->mb_type;

    for (int i = 0; i < 2; i++)
        dst->motion_val[i] = src->motion_val[i];

    for (int i = 0; i < 2; i++)
        dst->field_poc[i] = src->field_poc[i];

    memcpy(dst->ref_poc,   src->ref_poc,   sizeof(src->ref_poc));
    memcpy(dst->ref_count, src->ref_count, sizeof(src->ref_count));

    dst->poc           = src->poc;
    dst->frame_num     = src->frame_num;
    dst->mmco_reset    = src->mmco_reset;
    dst->long_ref      = src->long_ref;
    dst->mbaff         = src->mbaff;
    dst->field_picture = src->field_picture;
    dst->reference     = src->reference;
    dst->recovered     = src->recovered;
    dst->gray          = src->gray;
    dst->invalid_gap   = src->invalid_gap;
    dst->sei_recovery_frame_cnt = src->sei_recovery_frame_cnt;
    dst->mb_width      = src->mb_width;
    dst->mb_height     = src->mb_height;
    dst->mb_stride     = src->mb_stride;
    dst->needs_fg      = src->needs_fg;
}

int ff_h264_ref_picture(H264Picture *dst, const H264Picture *src)
{
    int ret;

    av_assert0(!dst->f->buf[0]);
    av_assert0(src->f->buf[0]);
    av_assert0(src->tf.f == src->f);

    dst->tf.f = dst->f;
    ret = ff_thread_ref_frame(&dst->tf, &src->tf);
    if (ret < 0)
        goto fail;

    if (src->needs_fg) {
        ret = av_frame_ref(dst->f_grain, src->f_grain);
        if (ret < 0)
            goto fail;
    }

    h264_copy_picture_params(dst, src);

    return 0;
fail:
    ff_h264_unref_picture(dst);
    return ret;
}

int ff_h264_replace_picture(H264Picture *dst, const H264Picture *src)
{
    int ret;

    if (!src->f || !src->f->buf[0]) {
        ff_h264_unref_picture(dst);
        return 0;
    }

    av_assert0(src->tf.f == src->f);

    dst->tf.f = dst->f;
    ret = ff_thread_replace_frame(&dst->tf, &src->tf);
    if (ret < 0)
        goto fail;

    if (src->needs_fg) {
        av_frame_unref(dst->f_grain);
        ret = av_frame_ref(dst->f_grain, src->f_grain);
        if (ret < 0)
            goto fail;
    }

    h264_copy_picture_params(dst, src);

    return 0;
fail:
    ff_h264_unref_picture(dst);
    return ret;
}

void ff_h264_set_erpic(ERPicture *dst, const H264Picture *src)
{
#if CONFIG_ERROR_RESILIENCE
    int i;

    memset(dst, 0, sizeof(*dst));

    if (!src)
        return;

    dst->f = src->f;
    dst->tf = &src->tf;

    for (i = 0; i < 2; i++) {
        dst->motion_val[i] = src->motion_val[i];
        dst->ref_index[i] = src->ref_index[i];
    }

    dst->mb_type = src->mb_type;
    dst->field_picture = src->field_picture;
#endif /* CONFIG_ERROR_RESILIENCE */
}

int ff_h264_field_end(H264Context *h, H264SliceContext *sl, int in_setup)
{
    AVCodecContext *const avctx = h->avctx;
    H264Picture *cur = h->cur_pic_ptr;
    int err = 0;
    h->mb_y = 0;

    if (in_setup || !(avctx->active_thread_type & FF_THREAD_FRAME)) {
        if (!h->droppable) {
            err = ff_h264_execute_ref_pic_marking(h);
            h->poc.prev_poc_msb = h->poc.poc_msb;
            h->poc.prev_poc_lsb = h->poc.poc_lsb;
        }
        h->poc.prev_frame_num_offset = h->poc.frame_num_offset;
        h->poc.prev_frame_num        = h->poc.frame_num;
    }

    if (avctx->hwaccel) {
        err = FF_HW_SIMPLE_CALL(avctx, end_frame);
        if (err < 0)
            av_log(avctx, AV_LOG_ERROR,
                   "hardware accelerator failed to decode picture\n");
    } else if (!in_setup && cur->needs_fg && (!FIELD_PICTURE(h) || !h->first_field)) {
        const AVFrameSideData *sd = av_frame_get_side_data(cur->f, AV_FRAME_DATA_FILM_GRAIN_PARAMS);

        err = AVERROR_INVALIDDATA;
        if (sd) // a decoding error may have happened before the side data could be allocated
            err = ff_h274_apply_film_grain(cur->f_grain, cur->f, &h->h274db,
                                           (AVFilmGrainParams *) sd->data);
        if (err < 0) {
            av_log(h->avctx, AV_LOG_WARNING, "Failed synthesizing film "
                   "grain, ignoring: %s\n", av_err2str(err));
            cur->needs_fg = 0;
            err = 0;
        }
    }

    if (!in_setup && !h->droppable)
        ff_thread_report_progress(&cur->tf, INT_MAX,
                                  h->picture_structure == PICT_BOTTOM_FIELD);
    emms_c();

    h->current_slice = 0;

    return err;
}
