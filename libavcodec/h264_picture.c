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
#include "libavutil/imgutils.h"
#include "internal.h"
#include "cabac.h"
#include "cabac_functions.h"
#include "error_resilience.h"
#include "avcodec.h"
#include "h264dec.h"
#include "h264data.h"
#include "h264chroma.h"
#include "h264_mvpred.h"
#include "mathops.h"
#include "mpegutils.h"
#include "rectangle.h"
#include "thread.h"

void ff_h264_unref_picture(H264Context *h, H264Picture *pic)
{
    int off = offsetof(H264Picture, tf_grain) + sizeof(pic->tf_grain);
    int i;

    if (!pic->f || !pic->f->buf[0])
        return;

    ff_thread_release_buffer(h->avctx, &pic->tf);
    ff_thread_release_buffer(h->avctx, &pic->tf_grain);
    av_buffer_unref(&pic->hwaccel_priv_buf);

    av_buffer_unref(&pic->qscale_table_buf);
    av_buffer_unref(&pic->mb_type_buf);
    av_buffer_unref(&pic->pps_buf);
    for (i = 0; i < 2; i++) {
        av_buffer_unref(&pic->motion_val_buf[i]);
        av_buffer_unref(&pic->ref_index_buf[i]);
    }

    memset((uint8_t*)pic + off, 0, sizeof(*pic) - off);
}

static void h264_copy_picture_params(H264Picture *dst, const H264Picture *src)
{
    dst->qscale_table = src->qscale_table;
    dst->mb_type      = src->mb_type;
    dst->pps          = src->pps;

    for (int i = 0; i < 2; i++) {
        dst->motion_val[i] = src->motion_val[i];
        dst->ref_index[i]  = src->ref_index[i];
    }

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
    dst->invalid_gap   = src->invalid_gap;
    dst->sei_recovery_frame_cnt = src->sei_recovery_frame_cnt;
    dst->mb_width      = src->mb_width;
    dst->mb_height     = src->mb_height;
    dst->mb_stride     = src->mb_stride;
    dst->needs_fg      = src->needs_fg;
}

int ff_h264_ref_picture(H264Context *h, H264Picture *dst, H264Picture *src)
{
    int ret, i;

    av_assert0(!dst->f->buf[0]);
    av_assert0(src->f->buf[0]);
    av_assert0(src->tf.f == src->f);

    dst->tf.f = dst->f;
    ret = ff_thread_ref_frame(&dst->tf, &src->tf);
    if (ret < 0)
        goto fail;

    if (src->needs_fg) {
        av_assert0(src->tf_grain.f == src->f_grain);
        dst->tf_grain.f = dst->f_grain;
        ret = ff_thread_ref_frame(&dst->tf_grain, &src->tf_grain);
        if (ret < 0)
            goto fail;
    }

    dst->qscale_table_buf = av_buffer_ref(src->qscale_table_buf);
    dst->mb_type_buf      = av_buffer_ref(src->mb_type_buf);
    dst->pps_buf          = av_buffer_ref(src->pps_buf);
    if (!dst->qscale_table_buf || !dst->mb_type_buf || !dst->pps_buf) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    for (i = 0; i < 2; i++) {
        dst->motion_val_buf[i] = av_buffer_ref(src->motion_val_buf[i]);
        dst->ref_index_buf[i]  = av_buffer_ref(src->ref_index_buf[i]);
        if (!dst->motion_val_buf[i] || !dst->ref_index_buf[i]) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
    }

    if (src->hwaccel_picture_private) {
        dst->hwaccel_priv_buf = av_buffer_ref(src->hwaccel_priv_buf);
        if (!dst->hwaccel_priv_buf) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        dst->hwaccel_picture_private = dst->hwaccel_priv_buf->data;
    }

    h264_copy_picture_params(dst, src);

    return 0;
fail:
    ff_h264_unref_picture(h, dst);
    return ret;
}

int ff_h264_replace_picture(H264Context *h, H264Picture *dst, const H264Picture *src)
{
    int ret, i;

    if (!src->f || !src->f->buf[0]) {
        ff_h264_unref_picture(h, dst);
        return 0;
    }

    av_assert0(src->tf.f == src->f);

    dst->tf.f = dst->f;
    ff_thread_release_buffer(h->avctx, &dst->tf);
    ret = ff_thread_ref_frame(&dst->tf, &src->tf);
    if (ret < 0)
        goto fail;

    if (src->needs_fg) {
        av_assert0(src->tf_grain.f == src->f_grain);
        dst->tf_grain.f = dst->f_grain;
        ff_thread_release_buffer(h->avctx, &dst->tf_grain);
        ret = ff_thread_ref_frame(&dst->tf_grain, &src->tf_grain);
        if (ret < 0)
            goto fail;
    }

    ret  = av_buffer_replace(&dst->qscale_table_buf, src->qscale_table_buf);
    ret |= av_buffer_replace(&dst->mb_type_buf, src->mb_type_buf);
    ret |= av_buffer_replace(&dst->pps_buf, src->pps_buf);
    if (ret < 0)
        goto fail;

    for (i = 0; i < 2; i++) {
        ret  = av_buffer_replace(&dst->motion_val_buf[i], src->motion_val_buf[i]);
        ret |= av_buffer_replace(&dst->ref_index_buf[i], src->ref_index_buf[i]);
        if (ret < 0)
            goto fail;
    }

    ret = av_buffer_replace(&dst->hwaccel_priv_buf, src->hwaccel_priv_buf);
    if (ret < 0)
        goto fail;

    dst->hwaccel_picture_private = src->hwaccel_picture_private;

    h264_copy_picture_params(dst, src);

    return 0;
fail:
    ff_h264_unref_picture(h, dst);
    return ret;
}

void ff_h264_set_erpic(ERPicture *dst, H264Picture *src)
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
        err = avctx->hwaccel->end_frame(avctx);
        if (err < 0)
            av_log(avctx, AV_LOG_ERROR,
                   "hardware accelerator failed to decode picture\n");
    } else if (!in_setup && cur->needs_fg && (!FIELD_PICTURE(h) || !h->first_field)) {
        AVFrameSideData *sd = av_frame_get_side_data(cur->f, AV_FRAME_DATA_FILM_GRAIN_PARAMS);

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
