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
 * H.264 / AVC / MPEG4 part10 codec.
 * @author Michael Niedermayer <michaelni@gmx.at>
 */

#include "libavutil/avassert.h"
#include "libavutil/imgutils.h"
#include "libavutil/timer.h"
#include "internal.h"
#include "cabac.h"
#include "cabac_functions.h"
#include "error_resilience.h"
#include "avcodec.h"
#include "h264.h"
#include "h264data.h"
#include "h264chroma.h"
#include "h264_mvpred.h"
#include "golomb.h"
#include "mathops.h"
#include "mpegutils.h"
#include "rectangle.h"
#include "thread.h"


static const uint8_t rem6[QP_MAX_NUM + 1] = {
    0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, 0, 1, 2,
    3, 4, 5, 0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5,
    0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, 0, 1, 2,
    3, 4, 5, 0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5,
    0, 1, 2, 3,
};

static const uint8_t div6[QP_MAX_NUM + 1] = {
    0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3,  3,  3,
    3, 3, 3, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 6, 6, 6, 6,  6,  6,
    7, 7, 7, 7, 7, 7, 8, 8, 8, 8, 8, 8, 9, 9, 9, 9, 9, 9, 10, 10, 10,
   10,10,10,11,11,11,11,11,11,12,12,12,12,12,12,13,13,13, 13, 13, 13,
   14,14,14,14,
};

static const uint8_t field_scan[16+1] = {
    0 + 0 * 4, 0 + 1 * 4, 1 + 0 * 4, 0 + 2 * 4,
    0 + 3 * 4, 1 + 1 * 4, 1 + 2 * 4, 1 + 3 * 4,
    2 + 0 * 4, 2 + 1 * 4, 2 + 2 * 4, 2 + 3 * 4,
    3 + 0 * 4, 3 + 1 * 4, 3 + 2 * 4, 3 + 3 * 4,
};

static const uint8_t field_scan8x8[64+1] = {
    0 + 0 * 8, 0 + 1 * 8, 0 + 2 * 8, 1 + 0 * 8,
    1 + 1 * 8, 0 + 3 * 8, 0 + 4 * 8, 1 + 2 * 8,
    2 + 0 * 8, 1 + 3 * 8, 0 + 5 * 8, 0 + 6 * 8,
    0 + 7 * 8, 1 + 4 * 8, 2 + 1 * 8, 3 + 0 * 8,
    2 + 2 * 8, 1 + 5 * 8, 1 + 6 * 8, 1 + 7 * 8,
    2 + 3 * 8, 3 + 1 * 8, 4 + 0 * 8, 3 + 2 * 8,
    2 + 4 * 8, 2 + 5 * 8, 2 + 6 * 8, 2 + 7 * 8,
    3 + 3 * 8, 4 + 1 * 8, 5 + 0 * 8, 4 + 2 * 8,
    3 + 4 * 8, 3 + 5 * 8, 3 + 6 * 8, 3 + 7 * 8,
    4 + 3 * 8, 5 + 1 * 8, 6 + 0 * 8, 5 + 2 * 8,
    4 + 4 * 8, 4 + 5 * 8, 4 + 6 * 8, 4 + 7 * 8,
    5 + 3 * 8, 6 + 1 * 8, 6 + 2 * 8, 5 + 4 * 8,
    5 + 5 * 8, 5 + 6 * 8, 5 + 7 * 8, 6 + 3 * 8,
    7 + 0 * 8, 7 + 1 * 8, 6 + 4 * 8, 6 + 5 * 8,
    6 + 6 * 8, 6 + 7 * 8, 7 + 2 * 8, 7 + 3 * 8,
    7 + 4 * 8, 7 + 5 * 8, 7 + 6 * 8, 7 + 7 * 8,
};

static const uint8_t field_scan8x8_cavlc[64+1] = {
    0 + 0 * 8, 1 + 1 * 8, 2 + 0 * 8, 0 + 7 * 8,
    2 + 2 * 8, 2 + 3 * 8, 2 + 4 * 8, 3 + 3 * 8,
    3 + 4 * 8, 4 + 3 * 8, 4 + 4 * 8, 5 + 3 * 8,
    5 + 5 * 8, 7 + 0 * 8, 6 + 6 * 8, 7 + 4 * 8,
    0 + 1 * 8, 0 + 3 * 8, 1 + 3 * 8, 1 + 4 * 8,
    1 + 5 * 8, 3 + 1 * 8, 2 + 5 * 8, 4 + 1 * 8,
    3 + 5 * 8, 5 + 1 * 8, 4 + 5 * 8, 6 + 1 * 8,
    5 + 6 * 8, 7 + 1 * 8, 6 + 7 * 8, 7 + 5 * 8,
    0 + 2 * 8, 0 + 4 * 8, 0 + 5 * 8, 2 + 1 * 8,
    1 + 6 * 8, 4 + 0 * 8, 2 + 6 * 8, 5 + 0 * 8,
    3 + 6 * 8, 6 + 0 * 8, 4 + 6 * 8, 6 + 2 * 8,
    5 + 7 * 8, 6 + 4 * 8, 7 + 2 * 8, 7 + 6 * 8,
    1 + 0 * 8, 1 + 2 * 8, 0 + 6 * 8, 3 + 0 * 8,
    1 + 7 * 8, 3 + 2 * 8, 2 + 7 * 8, 4 + 2 * 8,
    3 + 7 * 8, 5 + 2 * 8, 4 + 7 * 8, 5 + 4 * 8,
    6 + 3 * 8, 6 + 5 * 8, 7 + 3 * 8, 7 + 7 * 8,
};

// zigzag_scan8x8_cavlc[i] = zigzag_scan8x8[(i/4) + 16*(i%4)]
static const uint8_t zigzag_scan8x8_cavlc[64+1] = {
    0 + 0 * 8, 1 + 1 * 8, 1 + 2 * 8, 2 + 2 * 8,
    4 + 1 * 8, 0 + 5 * 8, 3 + 3 * 8, 7 + 0 * 8,
    3 + 4 * 8, 1 + 7 * 8, 5 + 3 * 8, 6 + 3 * 8,
    2 + 7 * 8, 6 + 4 * 8, 5 + 6 * 8, 7 + 5 * 8,
    1 + 0 * 8, 2 + 0 * 8, 0 + 3 * 8, 3 + 1 * 8,
    3 + 2 * 8, 0 + 6 * 8, 4 + 2 * 8, 6 + 1 * 8,
    2 + 5 * 8, 2 + 6 * 8, 6 + 2 * 8, 5 + 4 * 8,
    3 + 7 * 8, 7 + 3 * 8, 4 + 7 * 8, 7 + 6 * 8,
    0 + 1 * 8, 3 + 0 * 8, 0 + 4 * 8, 4 + 0 * 8,
    2 + 3 * 8, 1 + 5 * 8, 5 + 1 * 8, 5 + 2 * 8,
    1 + 6 * 8, 3 + 5 * 8, 7 + 1 * 8, 4 + 5 * 8,
    4 + 6 * 8, 7 + 4 * 8, 5 + 7 * 8, 6 + 7 * 8,
    0 + 2 * 8, 2 + 1 * 8, 1 + 3 * 8, 5 + 0 * 8,
    1 + 4 * 8, 2 + 4 * 8, 6 + 0 * 8, 4 + 3 * 8,
    0 + 7 * 8, 4 + 4 * 8, 7 + 2 * 8, 3 + 6 * 8,
    5 + 5 * 8, 6 + 5 * 8, 6 + 6 * 8, 7 + 7 * 8,
};

static const uint8_t dequant4_coeff_init[6][3] = {
    { 10, 13, 16 },
    { 11, 14, 18 },
    { 13, 16, 20 },
    { 14, 18, 23 },
    { 16, 20, 25 },
    { 18, 23, 29 },
};

static const uint8_t dequant8_coeff_init_scan[16] = {
    0, 3, 4, 3, 3, 1, 5, 1, 4, 5, 2, 5, 3, 1, 5, 1
};

static const uint8_t dequant8_coeff_init[6][6] = {
    { 20, 18, 32, 19, 25, 24 },
    { 22, 19, 35, 21, 28, 26 },
    { 26, 23, 42, 24, 33, 31 },
    { 28, 25, 45, 26, 35, 33 },
    { 32, 28, 51, 30, 40, 38 },
    { 36, 32, 58, 34, 46, 43 },
};


static void release_unused_pictures(H264Context *h, int remove_current)
{
    int i;

    /* release non reference frames */
    for (i = 0; i < H264_MAX_PICTURE_COUNT; i++) {
        if (h->DPB[i].f->buf[0] && !h->DPB[i].reference &&
            (remove_current || &h->DPB[i] != h->cur_pic_ptr)) {
            ff_h264_unref_picture(h, &h->DPB[i]);
        }
    }
}

static int alloc_scratch_buffers(H264SliceContext *sl, int linesize)
{
    const H264Context *h = sl->h264;
    int alloc_size = FFALIGN(FFABS(linesize) + 32, 32);

    av_fast_malloc(&sl->bipred_scratchpad, &sl->bipred_scratchpad_allocated, 16 * 6 * alloc_size);
    // edge emu needs blocksize + filter length - 1
    // (= 21x21 for  h264)
    av_fast_malloc(&sl->edge_emu_buffer, &sl->edge_emu_buffer_allocated, alloc_size * 2 * 21);

    av_fast_mallocz(&sl->top_borders[0], &sl->top_borders_allocated[0],
                   h->mb_width * 16 * 3 * sizeof(uint8_t) * 2);
    av_fast_mallocz(&sl->top_borders[1], &sl->top_borders_allocated[1],
                   h->mb_width * 16 * 3 * sizeof(uint8_t) * 2);

    if (!sl->bipred_scratchpad || !sl->edge_emu_buffer ||
        !sl->top_borders[0]    || !sl->top_borders[1]) {
        av_freep(&sl->bipred_scratchpad);
        av_freep(&sl->edge_emu_buffer);
        av_freep(&sl->top_borders[0]);
        av_freep(&sl->top_borders[1]);

        sl->bipred_scratchpad_allocated = 0;
        sl->edge_emu_buffer_allocated   = 0;
        sl->top_borders_allocated[0]    = 0;
        sl->top_borders_allocated[1]    = 0;
        return AVERROR(ENOMEM);
    }

    return 0;
}

static int init_table_pools(H264Context *h)
{
    const int big_mb_num    = h->mb_stride * (h->mb_height + 1) + 1;
    const int mb_array_size = h->mb_stride * h->mb_height;
    const int b4_stride     = h->mb_width * 4 + 1;
    const int b4_array_size = b4_stride * h->mb_height * 4;

    h->qscale_table_pool = av_buffer_pool_init(big_mb_num + h->mb_stride,
                                               av_buffer_allocz);
    h->mb_type_pool      = av_buffer_pool_init((big_mb_num + h->mb_stride) *
                                               sizeof(uint32_t), av_buffer_allocz);
    h->motion_val_pool   = av_buffer_pool_init(2 * (b4_array_size + 4) *
                                               sizeof(int16_t), av_buffer_allocz);
    h->ref_index_pool    = av_buffer_pool_init(4 * mb_array_size, av_buffer_allocz);

    if (!h->qscale_table_pool || !h->mb_type_pool || !h->motion_val_pool ||
        !h->ref_index_pool) {
        av_buffer_pool_uninit(&h->qscale_table_pool);
        av_buffer_pool_uninit(&h->mb_type_pool);
        av_buffer_pool_uninit(&h->motion_val_pool);
        av_buffer_pool_uninit(&h->ref_index_pool);
        return AVERROR(ENOMEM);
    }

    return 0;
}

static int alloc_picture(H264Context *h, H264Picture *pic)
{
    int i, ret = 0;

    av_assert0(!pic->f->data[0]);

    pic->tf.f = pic->f;
    ret = ff_thread_get_buffer(h->avctx, &pic->tf, pic->reference ?
                                                   AV_GET_BUFFER_FLAG_REF : 0);
    if (ret < 0)
        goto fail;

    pic->crop     = h->sps.crop;
    pic->crop_top = h->sps.crop_top;
    pic->crop_left= h->sps.crop_left;

    if (h->avctx->hwaccel) {
        const AVHWAccel *hwaccel = h->avctx->hwaccel;
        av_assert0(!pic->hwaccel_picture_private);
        if (hwaccel->frame_priv_data_size) {
            pic->hwaccel_priv_buf = av_buffer_allocz(hwaccel->frame_priv_data_size);
            if (!pic->hwaccel_priv_buf)
                return AVERROR(ENOMEM);
            pic->hwaccel_picture_private = pic->hwaccel_priv_buf->data;
        }
    }
    if (CONFIG_GRAY && !h->avctx->hwaccel && h->flags & AV_CODEC_FLAG_GRAY && pic->f->data[2]) {
        int h_chroma_shift, v_chroma_shift;
        av_pix_fmt_get_chroma_sub_sample(pic->f->format,
                                         &h_chroma_shift, &v_chroma_shift);

        for(i=0; i<FF_CEIL_RSHIFT(pic->f->height, v_chroma_shift); i++) {
            memset(pic->f->data[1] + pic->f->linesize[1]*i,
                   0x80, FF_CEIL_RSHIFT(pic->f->width, h_chroma_shift));
            memset(pic->f->data[2] + pic->f->linesize[2]*i,
                   0x80, FF_CEIL_RSHIFT(pic->f->width, h_chroma_shift));
        }
    }

    if (!h->qscale_table_pool) {
        ret = init_table_pools(h);
        if (ret < 0)
            goto fail;
    }

    pic->qscale_table_buf = av_buffer_pool_get(h->qscale_table_pool);
    pic->mb_type_buf      = av_buffer_pool_get(h->mb_type_pool);
    if (!pic->qscale_table_buf || !pic->mb_type_buf)
        goto fail;

    pic->mb_type      = (uint32_t*)pic->mb_type_buf->data + 2 * h->mb_stride + 1;
    pic->qscale_table = pic->qscale_table_buf->data + 2 * h->mb_stride + 1;

    for (i = 0; i < 2; i++) {
        pic->motion_val_buf[i] = av_buffer_pool_get(h->motion_val_pool);
        pic->ref_index_buf[i]  = av_buffer_pool_get(h->ref_index_pool);
        if (!pic->motion_val_buf[i] || !pic->ref_index_buf[i])
            goto fail;

        pic->motion_val[i] = (int16_t (*)[2])pic->motion_val_buf[i]->data + 4;
        pic->ref_index[i]  = pic->ref_index_buf[i]->data;
    }

    return 0;
fail:
    ff_h264_unref_picture(h, pic);
    return (ret < 0) ? ret : AVERROR(ENOMEM);
}

static inline int pic_is_unused(H264Context *h, H264Picture *pic)
{
    if (!pic->f->buf[0])
        return 1;
    return 0;
}

static int find_unused_picture(H264Context *h)
{
    int i;

    for (i = 0; i < H264_MAX_PICTURE_COUNT; i++) {
        if (pic_is_unused(h, &h->DPB[i]))
            break;
    }
    if (i == H264_MAX_PICTURE_COUNT)
        return AVERROR_INVALIDDATA;

    return i;
}


static void init_dequant8_coeff_table(H264Context *h)
{
    int i, j, q, x;
    const int max_qp = 51 + 6 * (h->sps.bit_depth_luma - 8);

    for (i = 0; i < 6; i++) {
        h->dequant8_coeff[i] = h->dequant8_buffer[i];
        for (j = 0; j < i; j++)
            if (!memcmp(h->pps.scaling_matrix8[j], h->pps.scaling_matrix8[i],
                        64 * sizeof(uint8_t))) {
                h->dequant8_coeff[i] = h->dequant8_buffer[j];
                break;
            }
        if (j < i)
            continue;

        for (q = 0; q < max_qp + 1; q++) {
            int shift = div6[q];
            int idx   = rem6[q];
            for (x = 0; x < 64; x++)
                h->dequant8_coeff[i][q][(x >> 3) | ((x & 7) << 3)] =
                    ((uint32_t)dequant8_coeff_init[idx][dequant8_coeff_init_scan[((x >> 1) & 12) | (x & 3)]] *
                     h->pps.scaling_matrix8[i][x]) << shift;
        }
    }
}

static void init_dequant4_coeff_table(H264Context *h)
{
    int i, j, q, x;
    const int max_qp = 51 + 6 * (h->sps.bit_depth_luma - 8);
    for (i = 0; i < 6; i++) {
        h->dequant4_coeff[i] = h->dequant4_buffer[i];
        for (j = 0; j < i; j++)
            if (!memcmp(h->pps.scaling_matrix4[j], h->pps.scaling_matrix4[i],
                        16 * sizeof(uint8_t))) {
                h->dequant4_coeff[i] = h->dequant4_buffer[j];
                break;
            }
        if (j < i)
            continue;

        for (q = 0; q < max_qp + 1; q++) {
            int shift = div6[q] + 2;
            int idx   = rem6[q];
            for (x = 0; x < 16; x++)
                h->dequant4_coeff[i][q][(x >> 2) | ((x << 2) & 0xF)] =
                    ((uint32_t)dequant4_coeff_init[idx][(x & 1) + ((x >> 2) & 1)] *
                     h->pps.scaling_matrix4[i][x]) << shift;
        }
    }
}

void ff_h264_init_dequant_tables(H264Context *h)
{
    int i, x;
    init_dequant4_coeff_table(h);
    memset(h->dequant8_coeff, 0, sizeof(h->dequant8_coeff));

    if (h->pps.transform_8x8_mode)
        init_dequant8_coeff_table(h);
    if (h->sps.transform_bypass) {
        for (i = 0; i < 6; i++)
            for (x = 0; x < 16; x++)
                h->dequant4_coeff[i][0][x] = 1 << 6;
        if (h->pps.transform_8x8_mode)
            for (i = 0; i < 6; i++)
                for (x = 0; x < 64; x++)
                    h->dequant8_coeff[i][0][x] = 1 << 6;
    }
}

#define IN_RANGE(a, b, size) (((void*)(a) >= (void*)(b)) && ((void*)(a) < (void*)((b) + (size))))

#define REBASE_PICTURE(pic, new_ctx, old_ctx)             \
    (((pic) && (pic) >= (old_ctx)->DPB &&                       \
      (pic) < (old_ctx)->DPB + H264_MAX_PICTURE_COUNT) ?          \
     &(new_ctx)->DPB[(pic) - (old_ctx)->DPB] : NULL)

static void copy_picture_range(H264Picture **to, H264Picture **from, int count,
                               H264Context *new_base,
                               H264Context *old_base)
{
    int i;

    for (i = 0; i < count; i++) {
        av_assert1(!from[i] ||
                   IN_RANGE(from[i], old_base, 1) ||
                   IN_RANGE(from[i], old_base->DPB, H264_MAX_PICTURE_COUNT));
        to[i] = REBASE_PICTURE(from[i], new_base, old_base);
    }
}

static int copy_parameter_set(void **to, void **from, int count, int size)
{
    int i;

    for (i = 0; i < count; i++) {
        if (to[i] && !from[i]) {
            av_freep(&to[i]);
        } else if (from[i] && !to[i]) {
            to[i] = av_malloc(size);
            if (!to[i])
                return AVERROR(ENOMEM);
        }

        if (from[i])
            memcpy(to[i], from[i], size);
    }

    return 0;
}

#define copy_fields(to, from, start_field, end_field)                   \
    memcpy(&(to)->start_field, &(from)->start_field,                        \
           (char *)&(to)->end_field - (char *)&(to)->start_field)

static int h264_slice_header_init(H264Context *h);

int ff_h264_update_thread_context(AVCodecContext *dst,
                                  const AVCodecContext *src)
{
    H264Context *h = dst->priv_data, *h1 = src->priv_data;
    int inited = h->context_initialized, err = 0;
    int need_reinit = 0;
    int i, ret;

    if (dst == src)
        return 0;

    if (inited &&
        (h->width                 != h1->width                 ||
         h->height                != h1->height                ||
         h->mb_width              != h1->mb_width              ||
         h->mb_height             != h1->mb_height             ||
         h->sps.bit_depth_luma    != h1->sps.bit_depth_luma    ||
         h->sps.chroma_format_idc != h1->sps.chroma_format_idc ||
         h->sps.colorspace        != h1->sps.colorspace)) {

        need_reinit = 1;
    }

    /* copy block_offset since frame_start may not be called */
    memcpy(h->block_offset, h1->block_offset, sizeof(h->block_offset));

    // SPS/PPS
    if ((ret = copy_parameter_set((void **)h->sps_buffers,
                                  (void **)h1->sps_buffers,
                                  MAX_SPS_COUNT, sizeof(SPS))) < 0)
        return ret;
    h->sps = h1->sps;
    if ((ret = copy_parameter_set((void **)h->pps_buffers,
                                  (void **)h1->pps_buffers,
                                  MAX_PPS_COUNT, sizeof(PPS))) < 0)
        return ret;
    h->pps = h1->pps;

    if (need_reinit || !inited) {
        h->width     = h1->width;
        h->height    = h1->height;
        h->mb_height = h1->mb_height;
        h->mb_width  = h1->mb_width;
        h->mb_num    = h1->mb_num;
        h->mb_stride = h1->mb_stride;
        h->b_stride  = h1->b_stride;

        if (h->context_initialized || h1->context_initialized) {
            if ((err = h264_slice_header_init(h)) < 0) {
                av_log(h->avctx, AV_LOG_ERROR, "h264_slice_header_init() failed");
                return err;
            }
        }
        /* copy block_offset since frame_start may not be called */
        memcpy(h->block_offset, h1->block_offset, sizeof(h->block_offset));
    }

    h->avctx->coded_height  = h1->avctx->coded_height;
    h->avctx->coded_width   = h1->avctx->coded_width;
    h->avctx->width         = h1->avctx->width;
    h->avctx->height        = h1->avctx->height;
    h->coded_picture_number = h1->coded_picture_number;
    h->first_field          = h1->first_field;
    h->picture_structure    = h1->picture_structure;
    h->droppable            = h1->droppable;
    h->low_delay            = h1->low_delay;
    h->backup_width         = h1->backup_width;
    h->backup_height        = h1->backup_height;
    h->backup_pix_fmt       = h1->backup_pix_fmt;

    for (i = 0; i < H264_MAX_PICTURE_COUNT; i++) {
        ff_h264_unref_picture(h, &h->DPB[i]);
        if (h1->DPB[i].f->buf[0] &&
            (ret = ff_h264_ref_picture(h, &h->DPB[i], &h1->DPB[i])) < 0)
            return ret;
    }

    h->cur_pic_ptr = REBASE_PICTURE(h1->cur_pic_ptr, h, h1);
    ff_h264_unref_picture(h, &h->cur_pic);
    if (h1->cur_pic.f->buf[0]) {
        ret = ff_h264_ref_picture(h, &h->cur_pic, &h1->cur_pic);
        if (ret < 0)
            return ret;
    }

    h->enable_er       = h1->enable_er;
    h->workaround_bugs = h1->workaround_bugs;
    h->low_delay       = h1->low_delay;
    h->droppable       = h1->droppable;

    // extradata/NAL handling
    h->is_avc = h1->is_avc;
    h->nal_length_size = h1->nal_length_size;
    h->x264_build      = h1->x264_build;

    // Dequantization matrices
    // FIXME these are big - can they be only copied when PPS changes?
    copy_fields(h, h1, dequant4_buffer, dequant4_coeff);

    for (i = 0; i < 6; i++)
        h->dequant4_coeff[i] = h->dequant4_buffer[0] +
                               (h1->dequant4_coeff[i] - h1->dequant4_buffer[0]);

    for (i = 0; i < 6; i++)
        h->dequant8_coeff[i] = h->dequant8_buffer[0] +
                               (h1->dequant8_coeff[i] - h1->dequant8_buffer[0]);

    h->dequant_coeff_pps = h1->dequant_coeff_pps;

    // POC timing
    copy_fields(h, h1, poc_lsb, default_ref_list);

    // reference lists
    copy_fields(h, h1, short_ref, current_slice);

    copy_picture_range(h->short_ref, h1->short_ref, 32, h, h1);
    copy_picture_range(h->long_ref, h1->long_ref, 32, h, h1);
    copy_picture_range(h->delayed_pic, h1->delayed_pic,
                       MAX_DELAYED_PIC_COUNT + 2, h, h1);

    h->frame_recovered       = h1->frame_recovered;

    if (!h->cur_pic_ptr)
        return 0;

    if (!h->droppable) {
        err = ff_h264_execute_ref_pic_marking(h, h->mmco, h->mmco_index);
        h->prev_poc_msb = h->poc_msb;
        h->prev_poc_lsb = h->poc_lsb;
    }
    h->prev_frame_num_offset = h->frame_num_offset;
    h->prev_frame_num        = h->frame_num;

    h->recovery_frame        = h1->recovery_frame;

    return err;
}

static int h264_frame_start(H264Context *h)
{
    H264Picture *pic;
    int i, ret;
    const int pixel_shift = h->pixel_shift;
    int c[4] = {
        1<<(h->sps.bit_depth_luma-1),
        1<<(h->sps.bit_depth_chroma-1),
        1<<(h->sps.bit_depth_chroma-1),
        -1
    };

    if (!ff_thread_can_start_frame(h->avctx)) {
        av_log(h->avctx, AV_LOG_ERROR, "Attempt to start a frame outside SETUP state\n");
        return -1;
    }

    release_unused_pictures(h, 1);
    h->cur_pic_ptr = NULL;

    i = find_unused_picture(h);
    if (i < 0) {
        av_log(h->avctx, AV_LOG_ERROR, "no frame buffer available\n");
        return i;
    }
    pic = &h->DPB[i];

    pic->reference              = h->droppable ? 0 : h->picture_structure;
    pic->f->coded_picture_number = h->coded_picture_number++;
    pic->field_picture          = h->picture_structure != PICT_FRAME;

    /*
     * Zero key_frame here; IDR markings per slice in frame or fields are ORed
     * in later.
     * See decode_nal_units().
     */
    pic->f->key_frame = 0;
    pic->mmco_reset  = 0;
    pic->recovered   = 0;
    pic->invalid_gap = 0;
    pic->sei_recovery_frame_cnt = h->sei_recovery_frame_cnt;

    if ((ret = alloc_picture(h, pic)) < 0)
        return ret;
    if(!h->frame_recovered && !h->avctx->hwaccel
#if FF_API_CAP_VDPAU
       && !(h->avctx->codec->capabilities & AV_CODEC_CAP_HWACCEL_VDPAU)
#endif
       )
        ff_color_frame(pic->f, c);

    h->cur_pic_ptr = pic;
    ff_h264_unref_picture(h, &h->cur_pic);
    if (CONFIG_ERROR_RESILIENCE) {
        ff_h264_set_erpic(&h->slice_ctx[0].er.cur_pic, NULL);
    }

    if ((ret = ff_h264_ref_picture(h, &h->cur_pic, h->cur_pic_ptr)) < 0)
        return ret;

    for (i = 0; i < h->nb_slice_ctx; i++) {
        h->slice_ctx[i].linesize   = h->cur_pic_ptr->f->linesize[0];
        h->slice_ctx[i].uvlinesize = h->cur_pic_ptr->f->linesize[1];
    }

    if (CONFIG_ERROR_RESILIENCE && h->enable_er) {
        ff_er_frame_start(&h->slice_ctx[0].er);
        ff_h264_set_erpic(&h->slice_ctx[0].er.last_pic, NULL);
        ff_h264_set_erpic(&h->slice_ctx[0].er.next_pic, NULL);
    }

    for (i = 0; i < 16; i++) {
        h->block_offset[i]           = (4 * ((scan8[i] - scan8[0]) & 7) << pixel_shift) + 4 * pic->f->linesize[0] * ((scan8[i] - scan8[0]) >> 3);
        h->block_offset[48 + i]      = (4 * ((scan8[i] - scan8[0]) & 7) << pixel_shift) + 8 * pic->f->linesize[0] * ((scan8[i] - scan8[0]) >> 3);
    }
    for (i = 0; i < 16; i++) {
        h->block_offset[16 + i]      =
        h->block_offset[32 + i]      = (4 * ((scan8[i] - scan8[0]) & 7) << pixel_shift) + 4 * pic->f->linesize[1] * ((scan8[i] - scan8[0]) >> 3);
        h->block_offset[48 + 16 + i] =
        h->block_offset[48 + 32 + i] = (4 * ((scan8[i] - scan8[0]) & 7) << pixel_shift) + 8 * pic->f->linesize[1] * ((scan8[i] - scan8[0]) >> 3);
    }

    /* We mark the current picture as non-reference after allocating it, so
     * that if we break out due to an error it can be released automatically
     * in the next ff_mpv_frame_start().
     */
    h->cur_pic_ptr->reference = 0;

    h->cur_pic_ptr->field_poc[0] = h->cur_pic_ptr->field_poc[1] = INT_MAX;

    h->next_output_pic = NULL;

    assert(h->cur_pic_ptr->long_ref == 0);

    return 0;
}

static av_always_inline void backup_mb_border(const H264Context *h, H264SliceContext *sl,
                                              uint8_t *src_y,
                                              uint8_t *src_cb, uint8_t *src_cr,
                                              int linesize, int uvlinesize,
                                              int simple)
{
    uint8_t *top_border;
    int top_idx = 1;
    const int pixel_shift = h->pixel_shift;
    int chroma444 = CHROMA444(h);
    int chroma422 = CHROMA422(h);

    src_y  -= linesize;
    src_cb -= uvlinesize;
    src_cr -= uvlinesize;

    if (!simple && FRAME_MBAFF(h)) {
        if (sl->mb_y & 1) {
            if (!MB_MBAFF(sl)) {
                top_border = sl->top_borders[0][sl->mb_x];
                AV_COPY128(top_border, src_y + 15 * linesize);
                if (pixel_shift)
                    AV_COPY128(top_border + 16, src_y + 15 * linesize + 16);
                if (simple || !CONFIG_GRAY || !(h->flags & AV_CODEC_FLAG_GRAY)) {
                    if (chroma444) {
                        if (pixel_shift) {
                            AV_COPY128(top_border + 32, src_cb + 15 * uvlinesize);
                            AV_COPY128(top_border + 48, src_cb + 15 * uvlinesize + 16);
                            AV_COPY128(top_border + 64, src_cr + 15 * uvlinesize);
                            AV_COPY128(top_border + 80, src_cr + 15 * uvlinesize + 16);
                        } else {
                            AV_COPY128(top_border + 16, src_cb + 15 * uvlinesize);
                            AV_COPY128(top_border + 32, src_cr + 15 * uvlinesize);
                        }
                    } else if (chroma422) {
                        if (pixel_shift) {
                            AV_COPY128(top_border + 32, src_cb + 15 * uvlinesize);
                            AV_COPY128(top_border + 48, src_cr + 15 * uvlinesize);
                        } else {
                            AV_COPY64(top_border + 16, src_cb + 15 * uvlinesize);
                            AV_COPY64(top_border + 24, src_cr + 15 * uvlinesize);
                        }
                    } else {
                        if (pixel_shift) {
                            AV_COPY128(top_border + 32, src_cb + 7 * uvlinesize);
                            AV_COPY128(top_border + 48, src_cr + 7 * uvlinesize);
                        } else {
                            AV_COPY64(top_border + 16, src_cb + 7 * uvlinesize);
                            AV_COPY64(top_border + 24, src_cr + 7 * uvlinesize);
                        }
                    }
                }
            }
        } else if (MB_MBAFF(sl)) {
            top_idx = 0;
        } else
            return;
    }

    top_border = sl->top_borders[top_idx][sl->mb_x];
    /* There are two lines saved, the line above the top macroblock
     * of a pair, and the line above the bottom macroblock. */
    AV_COPY128(top_border, src_y + 16 * linesize);
    if (pixel_shift)
        AV_COPY128(top_border + 16, src_y + 16 * linesize + 16);

    if (simple || !CONFIG_GRAY || !(h->flags & AV_CODEC_FLAG_GRAY)) {
        if (chroma444) {
            if (pixel_shift) {
                AV_COPY128(top_border + 32, src_cb + 16 * linesize);
                AV_COPY128(top_border + 48, src_cb + 16 * linesize + 16);
                AV_COPY128(top_border + 64, src_cr + 16 * linesize);
                AV_COPY128(top_border + 80, src_cr + 16 * linesize + 16);
            } else {
                AV_COPY128(top_border + 16, src_cb + 16 * linesize);
                AV_COPY128(top_border + 32, src_cr + 16 * linesize);
            }
        } else if (chroma422) {
            if (pixel_shift) {
                AV_COPY128(top_border + 32, src_cb + 16 * uvlinesize);
                AV_COPY128(top_border + 48, src_cr + 16 * uvlinesize);
            } else {
                AV_COPY64(top_border + 16, src_cb + 16 * uvlinesize);
                AV_COPY64(top_border + 24, src_cr + 16 * uvlinesize);
            }
        } else {
            if (pixel_shift) {
                AV_COPY128(top_border + 32, src_cb + 8 * uvlinesize);
                AV_COPY128(top_border + 48, src_cr + 8 * uvlinesize);
            } else {
                AV_COPY64(top_border + 16, src_cb + 8 * uvlinesize);
                AV_COPY64(top_border + 24, src_cr + 8 * uvlinesize);
            }
        }
    }
}

/**
 * Initialize implicit_weight table.
 * @param field  0/1 initialize the weight for interlaced MBAFF
 *                -1 initializes the rest
 */
static void implicit_weight_table(const H264Context *h, H264SliceContext *sl, int field)
{
    int ref0, ref1, i, cur_poc, ref_start, ref_count0, ref_count1;

    for (i = 0; i < 2; i++) {
        sl->luma_weight_flag[i]   = 0;
        sl->chroma_weight_flag[i] = 0;
    }

    if (field < 0) {
        if (h->picture_structure == PICT_FRAME) {
            cur_poc = h->cur_pic_ptr->poc;
        } else {
            cur_poc = h->cur_pic_ptr->field_poc[h->picture_structure - 1];
        }
        if (sl->ref_count[0] == 1 && sl->ref_count[1] == 1 && !FRAME_MBAFF(h) &&
            sl->ref_list[0][0].poc + sl->ref_list[1][0].poc == 2 * cur_poc) {
            sl->use_weight        = 0;
            sl->use_weight_chroma = 0;
            return;
        }
        ref_start  = 0;
        ref_count0 = sl->ref_count[0];
        ref_count1 = sl->ref_count[1];
    } else {
        cur_poc    = h->cur_pic_ptr->field_poc[field];
        ref_start  = 16;
        ref_count0 = 16 + 2 * sl->ref_count[0];
        ref_count1 = 16 + 2 * sl->ref_count[1];
    }

    sl->use_weight               = 2;
    sl->use_weight_chroma        = 2;
    sl->luma_log2_weight_denom   = 5;
    sl->chroma_log2_weight_denom = 5;

    for (ref0 = ref_start; ref0 < ref_count0; ref0++) {
        int poc0 = sl->ref_list[0][ref0].poc;
        for (ref1 = ref_start; ref1 < ref_count1; ref1++) {
            int w = 32;
            if (!sl->ref_list[0][ref0].parent->long_ref && !sl->ref_list[1][ref1].parent->long_ref) {
                int poc1 = sl->ref_list[1][ref1].poc;
                int td   = av_clip_int8(poc1 - poc0);
                if (td) {
                    int tb = av_clip_int8(cur_poc - poc0);
                    int tx = (16384 + (FFABS(td) >> 1)) / td;
                    int dist_scale_factor = (tb * tx + 32) >> 8;
                    if (dist_scale_factor >= -64 && dist_scale_factor <= 128)
                        w = 64 - dist_scale_factor;
                }
            }
            if (field < 0) {
                sl->implicit_weight[ref0][ref1][0] =
                sl->implicit_weight[ref0][ref1][1] = w;
            } else {
                sl->implicit_weight[ref0][ref1][field] = w;
            }
        }
    }
}

/**
 * initialize scan tables
 */
static void init_scan_tables(H264Context *h)
{
    int i;
    for (i = 0; i < 16; i++) {
#define TRANSPOSE(x) ((x) >> 2) | (((x) << 2) & 0xF)
        h->zigzag_scan[i] = TRANSPOSE(zigzag_scan[i]);
        h->field_scan[i]  = TRANSPOSE(field_scan[i]);
#undef TRANSPOSE
    }
    for (i = 0; i < 64; i++) {
#define TRANSPOSE(x) ((x) >> 3) | (((x) & 7) << 3)
        h->zigzag_scan8x8[i]       = TRANSPOSE(ff_zigzag_direct[i]);
        h->zigzag_scan8x8_cavlc[i] = TRANSPOSE(zigzag_scan8x8_cavlc[i]);
        h->field_scan8x8[i]        = TRANSPOSE(field_scan8x8[i]);
        h->field_scan8x8_cavlc[i]  = TRANSPOSE(field_scan8x8_cavlc[i]);
#undef TRANSPOSE
    }
    if (h->sps.transform_bypass) { // FIXME same ugly
        memcpy(h->zigzag_scan_q0          , zigzag_scan             , sizeof(h->zigzag_scan_q0         ));
        memcpy(h->zigzag_scan8x8_q0       , ff_zigzag_direct        , sizeof(h->zigzag_scan8x8_q0      ));
        memcpy(h->zigzag_scan8x8_cavlc_q0 , zigzag_scan8x8_cavlc    , sizeof(h->zigzag_scan8x8_cavlc_q0));
        memcpy(h->field_scan_q0           , field_scan              , sizeof(h->field_scan_q0          ));
        memcpy(h->field_scan8x8_q0        , field_scan8x8           , sizeof(h->field_scan8x8_q0       ));
        memcpy(h->field_scan8x8_cavlc_q0  , field_scan8x8_cavlc     , sizeof(h->field_scan8x8_cavlc_q0 ));
    } else {
        memcpy(h->zigzag_scan_q0          , h->zigzag_scan          , sizeof(h->zigzag_scan_q0         ));
        memcpy(h->zigzag_scan8x8_q0       , h->zigzag_scan8x8       , sizeof(h->zigzag_scan8x8_q0      ));
        memcpy(h->zigzag_scan8x8_cavlc_q0 , h->zigzag_scan8x8_cavlc , sizeof(h->zigzag_scan8x8_cavlc_q0));
        memcpy(h->field_scan_q0           , h->field_scan           , sizeof(h->field_scan_q0          ));
        memcpy(h->field_scan8x8_q0        , h->field_scan8x8        , sizeof(h->field_scan8x8_q0       ));
        memcpy(h->field_scan8x8_cavlc_q0  , h->field_scan8x8_cavlc  , sizeof(h->field_scan8x8_cavlc_q0 ));
    }
}

static enum AVPixelFormat get_pixel_format(H264Context *h, int force_callback)
{
#define HWACCEL_MAX (CONFIG_H264_DXVA2_HWACCEL + \
                     CONFIG_H264_D3D11VA_HWACCEL + \
                     CONFIG_H264_VAAPI_HWACCEL + \
                     (CONFIG_H264_VDA_HWACCEL * 2) + \
                     CONFIG_H264_VIDEOTOOLBOX_HWACCEL + \
                     CONFIG_H264_VDPAU_HWACCEL)
    enum AVPixelFormat pix_fmts[HWACCEL_MAX + 2], *fmt = pix_fmts;
    const enum AVPixelFormat *choices = pix_fmts;
    int i;

    switch (h->sps.bit_depth_luma) {
    case 9:
        if (CHROMA444(h)) {
            if (h->avctx->colorspace == AVCOL_SPC_RGB) {
                *fmt++ = AV_PIX_FMT_GBRP9;
            } else
                *fmt++ = AV_PIX_FMT_YUV444P9;
        } else if (CHROMA422(h))
            *fmt++ = AV_PIX_FMT_YUV422P9;
        else
            *fmt++ = AV_PIX_FMT_YUV420P9;
        break;
    case 10:
        if (CHROMA444(h)) {
            if (h->avctx->colorspace == AVCOL_SPC_RGB) {
                *fmt++ = AV_PIX_FMT_GBRP10;
            } else
                *fmt++ = AV_PIX_FMT_YUV444P10;
        } else if (CHROMA422(h))
            *fmt++ = AV_PIX_FMT_YUV422P10;
        else
            *fmt++ = AV_PIX_FMT_YUV420P10;
        break;
    case 12:
        if (CHROMA444(h)) {
            if (h->avctx->colorspace == AVCOL_SPC_RGB) {
                *fmt++ = AV_PIX_FMT_GBRP12;
            } else
                *fmt++ = AV_PIX_FMT_YUV444P12;
        } else if (CHROMA422(h))
            *fmt++ = AV_PIX_FMT_YUV422P12;
        else
            *fmt++ = AV_PIX_FMT_YUV420P12;
        break;
    case 14:
        if (CHROMA444(h)) {
            if (h->avctx->colorspace == AVCOL_SPC_RGB) {
                *fmt++ = AV_PIX_FMT_GBRP14;
            } else
                *fmt++ = AV_PIX_FMT_YUV444P14;
        } else if (CHROMA422(h))
            *fmt++ = AV_PIX_FMT_YUV422P14;
        else
            *fmt++ = AV_PIX_FMT_YUV420P14;
        break;
    case 8:
#if CONFIG_H264_VDPAU_HWACCEL
        *fmt++ = AV_PIX_FMT_VDPAU;
#endif
        if (CHROMA444(h)) {
            if (h->avctx->colorspace == AVCOL_SPC_RGB)
                *fmt++ = AV_PIX_FMT_GBRP;
            else if (h->avctx->color_range == AVCOL_RANGE_JPEG)
                *fmt++ = AV_PIX_FMT_YUVJ444P;
            else
                *fmt++ = AV_PIX_FMT_YUV444P;
        } else if (CHROMA422(h)) {
            if (h->avctx->color_range == AVCOL_RANGE_JPEG)
                *fmt++ = AV_PIX_FMT_YUVJ422P;
            else
                *fmt++ = AV_PIX_FMT_YUV422P;
        } else {
#if CONFIG_H264_DXVA2_HWACCEL
            *fmt++ = AV_PIX_FMT_DXVA2_VLD;
#endif
#if CONFIG_H264_D3D11VA_HWACCEL
            *fmt++ = AV_PIX_FMT_D3D11VA_VLD;
#endif
#if CONFIG_H264_VAAPI_HWACCEL
            *fmt++ = AV_PIX_FMT_VAAPI;
#endif
#if CONFIG_H264_VDA_HWACCEL
            *fmt++ = AV_PIX_FMT_VDA_VLD;
            *fmt++ = AV_PIX_FMT_VDA;
#endif
#if CONFIG_H264_VIDEOTOOLBOX_HWACCEL
            *fmt++ = AV_PIX_FMT_VIDEOTOOLBOX;
#endif
            if (h->avctx->codec->pix_fmts)
                choices = h->avctx->codec->pix_fmts;
            else if (h->avctx->color_range == AVCOL_RANGE_JPEG)
                *fmt++ = AV_PIX_FMT_YUVJ420P;
            else
                *fmt++ = AV_PIX_FMT_YUV420P;
        }
        break;
    default:
        av_log(h->avctx, AV_LOG_ERROR,
               "Unsupported bit depth %d\n", h->sps.bit_depth_luma);
        return AVERROR_INVALIDDATA;
    }

    *fmt = AV_PIX_FMT_NONE;

    for (i=0; choices[i] != AV_PIX_FMT_NONE; i++)
        if (choices[i] == h->avctx->pix_fmt && !force_callback)
            return choices[i];
    return ff_thread_get_format(h->avctx, choices);
}

/* export coded and cropped frame dimensions to AVCodecContext */
static int init_dimensions(H264Context *h)
{
    int width  = h->width  - (h->sps.crop_right + h->sps.crop_left);
    int height = h->height - (h->sps.crop_top   + h->sps.crop_bottom);
    av_assert0(h->sps.crop_right + h->sps.crop_left < (unsigned)h->width);
    av_assert0(h->sps.crop_top + h->sps.crop_bottom < (unsigned)h->height);

    /* handle container cropping */
    if (FFALIGN(h->avctx->width,  16) == FFALIGN(width,  16) &&
        FFALIGN(h->avctx->height, 16) == FFALIGN(height, 16) &&
        h->avctx->width  <= width &&
        h->avctx->height <= height
    ) {
        width  = h->avctx->width;
        height = h->avctx->height;
    }

    if (width <= 0 || height <= 0) {
        av_log(h->avctx, AV_LOG_ERROR, "Invalid cropped dimensions: %dx%d.\n",
               width, height);
        if (h->avctx->err_recognition & AV_EF_EXPLODE)
            return AVERROR_INVALIDDATA;

        av_log(h->avctx, AV_LOG_WARNING, "Ignoring cropping information.\n");
        h->sps.crop_bottom =
        h->sps.crop_top    =
        h->sps.crop_right  =
        h->sps.crop_left   =
        h->sps.crop        = 0;

        width  = h->width;
        height = h->height;
    }

    h->avctx->coded_width  = h->width;
    h->avctx->coded_height = h->height;
    h->avctx->width        = width;
    h->avctx->height       = height;

    return 0;
}

static int h264_slice_header_init(H264Context *h)
{
    int nb_slices = (HAVE_THREADS &&
                     h->avctx->active_thread_type & FF_THREAD_SLICE) ?
                    h->avctx->thread_count : 1;
    int i, ret;

    ff_set_sar(h->avctx, h->sps.sar);
    av_pix_fmt_get_chroma_sub_sample(h->avctx->pix_fmt,
                                     &h->chroma_x_shift, &h->chroma_y_shift);

    if (h->sps.timing_info_present_flag) {
        int64_t den = h->sps.time_scale;
        if (h->x264_build < 44U)
            den *= 2;
        av_reduce(&h->avctx->framerate.den, &h->avctx->framerate.num,
                  h->sps.num_units_in_tick * h->avctx->ticks_per_frame, den, 1 << 30);
    }

    ff_h264_free_tables(h);

    h->first_field           = 0;
    h->prev_interlaced_frame = 1;

    init_scan_tables(h);
    ret = ff_h264_alloc_tables(h);
    if (ret < 0) {
        av_log(h->avctx, AV_LOG_ERROR, "Could not allocate memory\n");
        goto fail;
    }

#if FF_API_CAP_VDPAU
    if (h->avctx->codec &&
        h->avctx->codec->capabilities & AV_CODEC_CAP_HWACCEL_VDPAU &&
        (h->sps.bit_depth_luma != 8 || h->sps.chroma_format_idc > 1)) {
        av_log(h->avctx, AV_LOG_ERROR,
                "VDPAU decoding does not support video colorspace.\n");
        ret = AVERROR_INVALIDDATA;
        goto fail;
    }
#endif

    if (h->sps.bit_depth_luma < 8 || h->sps.bit_depth_luma > 14 ||
        h->sps.bit_depth_luma == 11 || h->sps.bit_depth_luma == 13
    ) {
        av_log(h->avctx, AV_LOG_ERROR, "Unsupported bit depth %d\n",
               h->sps.bit_depth_luma);
        ret = AVERROR_INVALIDDATA;
        goto fail;
    }

    h->cur_bit_depth_luma         =
    h->avctx->bits_per_raw_sample = h->sps.bit_depth_luma;
    h->cur_chroma_format_idc      = h->sps.chroma_format_idc;
    h->pixel_shift                = h->sps.bit_depth_luma > 8;
    h->chroma_format_idc          = h->sps.chroma_format_idc;
    h->bit_depth_luma             = h->sps.bit_depth_luma;

    ff_h264dsp_init(&h->h264dsp, h->sps.bit_depth_luma,
                    h->sps.chroma_format_idc);
    ff_h264chroma_init(&h->h264chroma, h->sps.bit_depth_chroma);
    ff_h264qpel_init(&h->h264qpel, h->sps.bit_depth_luma);
    ff_h264_pred_init(&h->hpc, h->avctx->codec_id, h->sps.bit_depth_luma,
                      h->sps.chroma_format_idc);
    ff_videodsp_init(&h->vdsp, h->sps.bit_depth_luma);

    if (nb_slices > H264_MAX_THREADS || (nb_slices > h->mb_height && h->mb_height)) {
        int max_slices;
        if (h->mb_height)
            max_slices = FFMIN(H264_MAX_THREADS, h->mb_height);
        else
            max_slices = H264_MAX_THREADS;
        av_log(h->avctx, AV_LOG_WARNING, "too many threads/slices %d,"
               " reducing to %d\n", nb_slices, max_slices);
        nb_slices = max_slices;
    }
    h->slice_context_count = nb_slices;
    h->max_contexts = FFMIN(h->max_contexts, nb_slices);

    if (!HAVE_THREADS || !(h->avctx->active_thread_type & FF_THREAD_SLICE)) {
        ret = ff_h264_slice_context_init(h, &h->slice_ctx[0]);
        if (ret < 0) {
            av_log(h->avctx, AV_LOG_ERROR, "context_init() failed.\n");
            goto fail;
        }
    } else {
        for (i = 0; i < h->slice_context_count; i++) {
            H264SliceContext *sl = &h->slice_ctx[i];

            sl->h264               = h;
            sl->intra4x4_pred_mode = h->intra4x4_pred_mode + i * 8 * 2 * h->mb_stride;
            sl->mvd_table[0]       = h->mvd_table[0]       + i * 8 * 2 * h->mb_stride;
            sl->mvd_table[1]       = h->mvd_table[1]       + i * 8 * 2 * h->mb_stride;

            if ((ret = ff_h264_slice_context_init(h, sl)) < 0) {
                av_log(h->avctx, AV_LOG_ERROR, "context_init() failed.\n");
                goto fail;
            }
        }
    }

    h->context_initialized = 1;

    return 0;
fail:
    ff_h264_free_tables(h);
    h->context_initialized = 0;
    return ret;
}

static enum AVPixelFormat non_j_pixfmt(enum AVPixelFormat a)
{
    switch (a) {
    case AV_PIX_FMT_YUVJ420P: return AV_PIX_FMT_YUV420P;
    case AV_PIX_FMT_YUVJ422P: return AV_PIX_FMT_YUV422P;
    case AV_PIX_FMT_YUVJ444P: return AV_PIX_FMT_YUV444P;
    default:
        return a;
    }
}

/**
 * Decode a slice header.
 * This will (re)intialize the decoder and call h264_frame_start() as needed.
 *
 * @param h h264context
 *
 * @return 0 if okay, <0 if an error occurred, 1 if decoding must not be multithreaded
 */
int ff_h264_decode_slice_header(H264Context *h, H264SliceContext *sl)
{
    unsigned int first_mb_in_slice;
    unsigned int pps_id;
    int ret;
    unsigned int slice_type, tmp, i, j;
    int last_pic_structure, last_pic_droppable;
    int must_reinit;
    int needs_reinit = 0;
    int field_pic_flag, bottom_field_flag;
    int first_slice = sl == h->slice_ctx && !h->current_slice;
    int frame_num, droppable, picture_structure;
    int mb_aff_frame, last_mb_aff_frame;
    PPS *pps;

    if (first_slice)
        av_assert0(!h->setup_finished);

    h->qpel_put = h->h264qpel.put_h264_qpel_pixels_tab;
    h->qpel_avg = h->h264qpel.avg_h264_qpel_pixels_tab;

    first_mb_in_slice = get_ue_golomb_long(&sl->gb);

    if (first_mb_in_slice == 0) { // FIXME better field boundary detection
        if (h->current_slice) {
            if (h->setup_finished) {
                av_log(h->avctx, AV_LOG_ERROR, "Too many fields\n");
                return AVERROR_INVALIDDATA;
            }
            if (h->max_contexts > 1) {
                if (!h->single_decode_warning) {
                    av_log(h->avctx, AV_LOG_WARNING, "Cannot decode multiple access units as slice threads\n");
                    h->single_decode_warning = 1;
                }
                h->max_contexts = 1;
                return SLICE_SINGLETHREAD;
            }

            if (h->cur_pic_ptr && FIELD_PICTURE(h) && h->first_field) {
                ret = ff_h264_field_end(h, h->slice_ctx, 1);
                h->current_slice = 0;
                if (ret < 0)
                    return ret;
            } else if (h->cur_pic_ptr && !FIELD_PICTURE(h) && !h->first_field && h->nal_unit_type  == NAL_IDR_SLICE) {
                av_log(h, AV_LOG_WARNING, "Broken frame packetizing\n");
                ret = ff_h264_field_end(h, h->slice_ctx, 1);
                h->current_slice = 0;
                ff_thread_report_progress(&h->cur_pic_ptr->tf, INT_MAX, 0);
                ff_thread_report_progress(&h->cur_pic_ptr->tf, INT_MAX, 1);
                h->cur_pic_ptr = NULL;
                if (ret < 0)
                    return ret;
            } else
                return AVERROR_INVALIDDATA;
        }

        if (!h->first_field) {
            if (h->cur_pic_ptr && !h->droppable) {
                ff_thread_report_progress(&h->cur_pic_ptr->tf, INT_MAX,
                                          h->picture_structure == PICT_BOTTOM_FIELD);
            }
            h->cur_pic_ptr = NULL;
        }
    }

    slice_type = get_ue_golomb_31(&sl->gb);
    if (slice_type > 9) {
        av_log(h->avctx, AV_LOG_ERROR,
               "slice type %d too large at %d\n",
               slice_type, first_mb_in_slice);
        return AVERROR_INVALIDDATA;
    }
    if (slice_type > 4) {
        slice_type -= 5;
        sl->slice_type_fixed = 1;
    } else
        sl->slice_type_fixed = 0;

    slice_type = golomb_to_pict_type[slice_type];

    sl->slice_type     = slice_type;
    sl->slice_type_nos = slice_type & 3;

    if (h->nal_unit_type  == NAL_IDR_SLICE &&
        sl->slice_type_nos != AV_PICTURE_TYPE_I) {
        av_log(h->avctx, AV_LOG_ERROR, "A non-intra slice in an IDR NAL unit.\n");
        return AVERROR_INVALIDDATA;
    }

    if (h->current_slice == 0 && !h->first_field) {
        if (
            (h->avctx->skip_frame >= AVDISCARD_NONREF && !h->nal_ref_idc) ||
            (h->avctx->skip_frame >= AVDISCARD_BIDIR  && sl->slice_type_nos == AV_PICTURE_TYPE_B) ||
            (h->avctx->skip_frame >= AVDISCARD_NONINTRA && sl->slice_type_nos != AV_PICTURE_TYPE_I) ||
            (h->avctx->skip_frame >= AVDISCARD_NONKEY && h->nal_unit_type != NAL_IDR_SLICE && h->sei_recovery_frame_cnt < 0) ||
            h->avctx->skip_frame >= AVDISCARD_ALL) {
            return SLICE_SKIPED;
        }
    }

    // to make a few old functions happy, it's wrong though
    if (!h->setup_finished)
        h->pict_type = sl->slice_type;

    pps_id = get_ue_golomb(&sl->gb);
    if (pps_id >= MAX_PPS_COUNT) {
        av_log(h->avctx, AV_LOG_ERROR, "pps_id %u out of range\n", pps_id);
        return AVERROR_INVALIDDATA;
    }
    if (!h->pps_buffers[pps_id]) {
        av_log(h->avctx, AV_LOG_ERROR,
               "non-existing PPS %u referenced\n",
               pps_id);
        return AVERROR_INVALIDDATA;
    }
    if (h->au_pps_id >= 0 && pps_id != h->au_pps_id) {
        av_log(h->avctx, AV_LOG_ERROR,
               "PPS change from %d to %d forbidden\n",
               h->au_pps_id, pps_id);
        return AVERROR_INVALIDDATA;
    }

    pps = h->pps_buffers[pps_id];

    if (!h->sps_buffers[pps->sps_id]) {
        av_log(h->avctx, AV_LOG_ERROR,
               "non-existing SPS %u referenced\n",
               h->pps.sps_id);
        return AVERROR_INVALIDDATA;
    }

    if (first_slice) {
        h->pps = *h->pps_buffers[pps_id];
    } else if (h->setup_finished && h->dequant_coeff_pps != pps_id) {
        av_log(h->avctx, AV_LOG_ERROR, "PPS changed between slices\n");
        return AVERROR_INVALIDDATA;
    }

    if (pps->sps_id != h->sps.sps_id ||
        pps->sps_id != h->current_sps_id ||
        h->sps_buffers[pps->sps_id]->new) {

        if (!first_slice) {
            av_log(h->avctx, AV_LOG_ERROR,
               "SPS changed in the middle of the frame\n");
            return AVERROR_INVALIDDATA;
        }

        h->sps = *h->sps_buffers[h->pps.sps_id];

        if (h->mb_width  != h->sps.mb_width ||
            h->mb_height != h->sps.mb_height * (2 - h->sps.frame_mbs_only_flag) ||
            h->cur_bit_depth_luma    != h->sps.bit_depth_luma ||
            h->cur_chroma_format_idc != h->sps.chroma_format_idc
        )
            needs_reinit = 1;

        if (h->bit_depth_luma    != h->sps.bit_depth_luma ||
            h->chroma_format_idc != h->sps.chroma_format_idc)
            needs_reinit         = 1;

        if (h->flags & AV_CODEC_FLAG_LOW_DELAY ||
            (h->sps.bitstream_restriction_flag &&
             !h->sps.num_reorder_frames)) {
            if (h->avctx->has_b_frames > 1 || h->delayed_pic[0])
                av_log(h->avctx, AV_LOG_WARNING, "Delayed frames seen. "
                       "Reenabling low delay requires a codec flush.\n");
            else
                h->low_delay = 1;
        }

        if (h->avctx->has_b_frames < 2)
            h->avctx->has_b_frames = !h->low_delay;

    }

    must_reinit = (h->context_initialized &&
                    (   16*h->sps.mb_width != h->avctx->coded_width
                     || 16*h->sps.mb_height * (2 - h->sps.frame_mbs_only_flag) != h->avctx->coded_height
                     || h->cur_bit_depth_luma    != h->sps.bit_depth_luma
                     || h->cur_chroma_format_idc != h->sps.chroma_format_idc
                     || h->mb_width  != h->sps.mb_width
                     || h->mb_height != h->sps.mb_height * (2 - h->sps.frame_mbs_only_flag)
                    ));
    if (h->avctx->pix_fmt == AV_PIX_FMT_NONE
        || (non_j_pixfmt(h->avctx->pix_fmt) != non_j_pixfmt(get_pixel_format(h, 0))))
        must_reinit = 1;

    if (first_slice && av_cmp_q(h->sps.sar, h->avctx->sample_aspect_ratio))
        must_reinit = 1;

    if (!h->setup_finished) {
        h->avctx->profile = ff_h264_get_profile(&h->sps);
        h->avctx->level   = h->sps.level_idc;
        h->avctx->refs    = h->sps.ref_frame_count;

        h->mb_width  = h->sps.mb_width;
        h->mb_height = h->sps.mb_height * (2 - h->sps.frame_mbs_only_flag);
        h->mb_num    = h->mb_width * h->mb_height;
        h->mb_stride = h->mb_width + 1;

        h->b_stride = h->mb_width * 4;

        h->chroma_y_shift = h->sps.chroma_format_idc <= 1; // 400 uses yuv420p

        h->width  = 16 * h->mb_width;
        h->height = 16 * h->mb_height;

        ret = init_dimensions(h);
        if (ret < 0)
            return ret;

        if (h->sps.video_signal_type_present_flag) {
            h->avctx->color_range = h->sps.full_range>0 ? AVCOL_RANGE_JPEG
                                                        : AVCOL_RANGE_MPEG;
            if (h->sps.colour_description_present_flag) {
                if (h->avctx->colorspace != h->sps.colorspace)
                    needs_reinit = 1;
                h->avctx->color_primaries = h->sps.color_primaries;
                h->avctx->color_trc       = h->sps.color_trc;
                h->avctx->colorspace      = h->sps.colorspace;
            }
        }
    }

    if (h->context_initialized &&
        (must_reinit || needs_reinit)) {
        h->context_initialized = 0;
        if (sl != h->slice_ctx) {
            av_log(h->avctx, AV_LOG_ERROR,
                   "changing width %d -> %d / height %d -> %d on "
                   "slice %d\n",
                   h->width, h->avctx->coded_width,
                   h->height, h->avctx->coded_height,
                   h->current_slice + 1);
            return AVERROR_INVALIDDATA;
        }

        av_assert1(first_slice);

        ff_h264_flush_change(h);

        if ((ret = get_pixel_format(h, 1)) < 0)
            return ret;
        h->avctx->pix_fmt = ret;

        av_log(h->avctx, AV_LOG_INFO, "Reinit context to %dx%d, "
               "pix_fmt: %s\n", h->width, h->height, av_get_pix_fmt_name(h->avctx->pix_fmt));

        if ((ret = h264_slice_header_init(h)) < 0) {
            av_log(h->avctx, AV_LOG_ERROR,
                   "h264_slice_header_init() failed\n");
            return ret;
        }
    }
    if (!h->context_initialized) {
        if (sl != h->slice_ctx) {
            av_log(h->avctx, AV_LOG_ERROR,
                   "Cannot (re-)initialize context during parallel decoding.\n");
            return AVERROR_PATCHWELCOME;
        }

        if ((ret = get_pixel_format(h, 1)) < 0)
            return ret;
        h->avctx->pix_fmt = ret;

        if ((ret = h264_slice_header_init(h)) < 0) {
            av_log(h->avctx, AV_LOG_ERROR,
                   "h264_slice_header_init() failed\n");
            return ret;
        }
    }

    if (first_slice && h->dequant_coeff_pps != pps_id) {
        h->dequant_coeff_pps = pps_id;
        ff_h264_init_dequant_tables(h);
    }

    frame_num = get_bits(&sl->gb, h->sps.log2_max_frame_num);
    if (!first_slice) {
        if (h->frame_num != frame_num) {
            av_log(h->avctx, AV_LOG_ERROR, "Frame num change from %d to %d\n",
                   h->frame_num, frame_num);
            return AVERROR_INVALIDDATA;
        }
    }

    if (!h->setup_finished)
        h->frame_num = frame_num;

    sl->mb_mbaff       = 0;
    mb_aff_frame       = 0;
    last_mb_aff_frame  = h->mb_aff_frame;
    last_pic_structure = h->picture_structure;
    last_pic_droppable = h->droppable;

    droppable = h->nal_ref_idc == 0;
    if (h->sps.frame_mbs_only_flag) {
        picture_structure = PICT_FRAME;
    } else {
        if (!h->sps.direct_8x8_inference_flag && slice_type == AV_PICTURE_TYPE_B) {
            av_log(h->avctx, AV_LOG_ERROR, "This stream was generated by a broken encoder, invalid 8x8 inference\n");
            return -1;
        }
        field_pic_flag = get_bits1(&sl->gb);

        if (field_pic_flag) {
            bottom_field_flag = get_bits1(&sl->gb);
            picture_structure = PICT_TOP_FIELD + bottom_field_flag;
        } else {
            picture_structure = PICT_FRAME;
            mb_aff_frame      = h->sps.mb_aff;
        }
    }

    if (h->current_slice) {
        if (last_pic_structure != picture_structure ||
            last_pic_droppable != droppable ||
            last_mb_aff_frame  != mb_aff_frame) {
            av_log(h->avctx, AV_LOG_ERROR,
                   "Changing field mode (%d -> %d) between slices is not allowed\n",
                   last_pic_structure, h->picture_structure);
            return AVERROR_INVALIDDATA;
        } else if (!h->cur_pic_ptr) {
            av_log(h->avctx, AV_LOG_ERROR,
                   "unset cur_pic_ptr on slice %d\n",
                   h->current_slice + 1);
            return AVERROR_INVALIDDATA;
        }
    }

    h->picture_structure = picture_structure;
    if (!h->setup_finished) {
        h->droppable         = droppable;
        h->picture_structure = picture_structure;
        h->mb_aff_frame      = mb_aff_frame;
    }
    sl->mb_field_decoding_flag = picture_structure != PICT_FRAME;

    if (h->current_slice == 0) {
        /* Shorten frame num gaps so we don't have to allocate reference
         * frames just to throw them away */
        if (h->frame_num != h->prev_frame_num) {
            int unwrap_prev_frame_num = h->prev_frame_num;
            int max_frame_num         = 1 << h->sps.log2_max_frame_num;

            if (unwrap_prev_frame_num > h->frame_num)
                unwrap_prev_frame_num -= max_frame_num;

            if ((h->frame_num - unwrap_prev_frame_num) > h->sps.ref_frame_count) {
                unwrap_prev_frame_num = (h->frame_num - h->sps.ref_frame_count) - 1;
                if (unwrap_prev_frame_num < 0)
                    unwrap_prev_frame_num += max_frame_num;

                h->prev_frame_num = unwrap_prev_frame_num;
            }
        }

        /* See if we have a decoded first field looking for a pair...
         * Here, we're using that to see if we should mark previously
         * decode frames as "finished".
         * We have to do that before the "dummy" in-between frame allocation,
         * since that can modify h->cur_pic_ptr. */
        if (h->first_field) {
            av_assert0(h->cur_pic_ptr);
            av_assert0(h->cur_pic_ptr->f->buf[0]);
            assert(h->cur_pic_ptr->reference != DELAYED_PIC_REF);

            /* Mark old field/frame as completed */
            if (h->cur_pic_ptr->tf.owner == h->avctx) {
                ff_thread_report_progress(&h->cur_pic_ptr->tf, INT_MAX,
                                          last_pic_structure == PICT_BOTTOM_FIELD);
            }

            /* figure out if we have a complementary field pair */
            if (!FIELD_PICTURE(h) || h->picture_structure == last_pic_structure) {
                /* Previous field is unmatched. Don't display it, but let it
                 * remain for reference if marked as such. */
                if (last_pic_structure != PICT_FRAME) {
                    ff_thread_report_progress(&h->cur_pic_ptr->tf, INT_MAX,
                                              last_pic_structure == PICT_TOP_FIELD);
                }
            } else {
                if (h->cur_pic_ptr->frame_num != h->frame_num) {
                    /* This and previous field were reference, but had
                     * different frame_nums. Consider this field first in
                     * pair. Throw away previous field except for reference
                     * purposes. */
                    if (last_pic_structure != PICT_FRAME) {
                        ff_thread_report_progress(&h->cur_pic_ptr->tf, INT_MAX,
                                                  last_pic_structure == PICT_TOP_FIELD);
                    }
                } else {
                    /* Second field in complementary pair */
                    if (!((last_pic_structure   == PICT_TOP_FIELD &&
                           h->picture_structure == PICT_BOTTOM_FIELD) ||
                          (last_pic_structure   == PICT_BOTTOM_FIELD &&
                           h->picture_structure == PICT_TOP_FIELD))) {
                        av_log(h->avctx, AV_LOG_ERROR,
                               "Invalid field mode combination %d/%d\n",
                               last_pic_structure, h->picture_structure);
                        h->picture_structure = last_pic_structure;
                        h->droppable         = last_pic_droppable;
                        return AVERROR_INVALIDDATA;
                    } else if (last_pic_droppable != h->droppable) {
                        avpriv_request_sample(h->avctx,
                                              "Found reference and non-reference fields in the same frame, which");
                        h->picture_structure = last_pic_structure;
                        h->droppable         = last_pic_droppable;
                        return AVERROR_PATCHWELCOME;
                    }
                }
            }
        }

        while (h->frame_num != h->prev_frame_num && !h->first_field &&
               h->frame_num != (h->prev_frame_num + 1) % (1 << h->sps.log2_max_frame_num)) {
            H264Picture *prev = h->short_ref_count ? h->short_ref[0] : NULL;
            av_log(h->avctx, AV_LOG_DEBUG, "Frame num gap %d %d\n",
                   h->frame_num, h->prev_frame_num);
            if (!h->sps.gaps_in_frame_num_allowed_flag)
                for(i=0; i<FF_ARRAY_ELEMS(h->last_pocs); i++)
                    h->last_pocs[i] = INT_MIN;
            ret = h264_frame_start(h);
            if (ret < 0) {
                h->first_field = 0;
                return ret;
            }

            h->prev_frame_num++;
            h->prev_frame_num        %= 1 << h->sps.log2_max_frame_num;
            h->cur_pic_ptr->frame_num = h->prev_frame_num;
            h->cur_pic_ptr->invalid_gap = !h->sps.gaps_in_frame_num_allowed_flag;
            ff_thread_report_progress(&h->cur_pic_ptr->tf, INT_MAX, 0);
            ff_thread_report_progress(&h->cur_pic_ptr->tf, INT_MAX, 1);
            ret = ff_generate_sliding_window_mmcos(h, 1);
            if (ret < 0 && (h->avctx->err_recognition & AV_EF_EXPLODE))
                return ret;
            ret = ff_h264_execute_ref_pic_marking(h, h->mmco, h->mmco_index);
            if (ret < 0 && (h->avctx->err_recognition & AV_EF_EXPLODE))
                return ret;
            /* Error concealment: If a ref is missing, copy the previous ref
             * in its place.
             * FIXME: Avoiding a memcpy would be nice, but ref handling makes
             * many assumptions about there being no actual duplicates.
             * FIXME: This does not copy padding for out-of-frame motion
             * vectors.  Given we are concealing a lost frame, this probably
             * is not noticeable by comparison, but it should be fixed. */
            if (h->short_ref_count) {
                if (prev &&
                    h->short_ref[0]->f->width == prev->f->width &&
                    h->short_ref[0]->f->height == prev->f->height &&
                    h->short_ref[0]->f->format == prev->f->format) {
                    av_image_copy(h->short_ref[0]->f->data,
                                  h->short_ref[0]->f->linesize,
                                  (const uint8_t **)prev->f->data,
                                  prev->f->linesize,
                                  prev->f->format,
                                  prev->f->width,
                                  prev->f->height);
                    h->short_ref[0]->poc = prev->poc + 2;
                }
                h->short_ref[0]->frame_num = h->prev_frame_num;
            }
        }

        /* See if we have a decoded first field looking for a pair...
         * We're using that to see whether to continue decoding in that
         * frame, or to allocate a new one. */
        if (h->first_field) {
            av_assert0(h->cur_pic_ptr);
            av_assert0(h->cur_pic_ptr->f->buf[0]);
            assert(h->cur_pic_ptr->reference != DELAYED_PIC_REF);

            /* figure out if we have a complementary field pair */
            if (!FIELD_PICTURE(h) || h->picture_structure == last_pic_structure) {
                /* Previous field is unmatched. Don't display it, but let it
                 * remain for reference if marked as such. */
                h->missing_fields ++;
                h->cur_pic_ptr = NULL;
                h->first_field = FIELD_PICTURE(h);
            } else {
                h->missing_fields = 0;
                if (h->cur_pic_ptr->frame_num != h->frame_num) {
                    ff_thread_report_progress(&h->cur_pic_ptr->tf, INT_MAX,
                                              h->picture_structure==PICT_BOTTOM_FIELD);
                    /* This and the previous field had different frame_nums.
                     * Consider this field first in pair. Throw away previous
                     * one except for reference purposes. */
                    h->first_field = 1;
                    h->cur_pic_ptr = NULL;
                } else {
                    /* Second field in complementary pair */
                    h->first_field = 0;
                }
            }
        } else {
            /* Frame or first field in a potentially complementary pair */
            h->first_field = FIELD_PICTURE(h);
        }

        if (!FIELD_PICTURE(h) || h->first_field) {
            if (h264_frame_start(h) < 0) {
                h->first_field = 0;
                return AVERROR_INVALIDDATA;
            }
        } else {
            release_unused_pictures(h, 0);
        }
        /* Some macroblocks can be accessed before they're available in case
        * of lost slices, MBAFF or threading. */
        if (FIELD_PICTURE(h)) {
            for(i = (h->picture_structure == PICT_BOTTOM_FIELD); i<h->mb_height; i++)
                memset(h->slice_table + i*h->mb_stride, -1, (h->mb_stride - (i+1==h->mb_height)) * sizeof(*h->slice_table));
        } else {
            memset(h->slice_table, -1,
                (h->mb_height * h->mb_stride - 1) * sizeof(*h->slice_table));
        }
        h->last_slice_type = -1;
    }

    if (!h->setup_finished)
        h->cur_pic_ptr->frame_num = h->frame_num; // FIXME frame_num cleanup

    av_assert1(h->mb_num == h->mb_width * h->mb_height);
    if (first_mb_in_slice << FIELD_OR_MBAFF_PICTURE(h) >= h->mb_num ||
        first_mb_in_slice >= h->mb_num) {
        av_log(h->avctx, AV_LOG_ERROR, "first_mb_in_slice overflow\n");
        return AVERROR_INVALIDDATA;
    }
    sl->resync_mb_x = sl->mb_x =  first_mb_in_slice % h->mb_width;
    sl->resync_mb_y = sl->mb_y = (first_mb_in_slice / h->mb_width) <<
                                 FIELD_OR_MBAFF_PICTURE(h);
    if (h->picture_structure == PICT_BOTTOM_FIELD)
        sl->resync_mb_y = sl->mb_y = sl->mb_y + 1;
    av_assert1(sl->mb_y < h->mb_height);

    if (h->picture_structure == PICT_FRAME) {
        h->curr_pic_num = h->frame_num;
        h->max_pic_num  = 1 << h->sps.log2_max_frame_num;
    } else {
        h->curr_pic_num = 2 * h->frame_num + 1;
        h->max_pic_num  = 1 << (h->sps.log2_max_frame_num + 1);
    }

    if (h->nal_unit_type == NAL_IDR_SLICE)
        get_ue_golomb(&sl->gb); /* idr_pic_id */

    if (h->sps.poc_type == 0) {
        int poc_lsb = get_bits(&sl->gb, h->sps.log2_max_poc_lsb);

        if (!h->setup_finished)
            h->poc_lsb = poc_lsb;

        if (h->pps.pic_order_present == 1 && h->picture_structure == PICT_FRAME) {
            int delta_poc_bottom = get_se_golomb(&sl->gb);
            if (!h->setup_finished)
                h->delta_poc_bottom = delta_poc_bottom;
        }
    }

    if (h->sps.poc_type == 1 && !h->sps.delta_pic_order_always_zero_flag) {
        int delta_poc = get_se_golomb(&sl->gb);

        if (!h->setup_finished)
            h->delta_poc[0] = delta_poc;

        if (h->pps.pic_order_present == 1 && h->picture_structure == PICT_FRAME) {
            delta_poc = get_se_golomb(&sl->gb);

            if (!h->setup_finished)
                h->delta_poc[1] = delta_poc;
        }
    }

    if (!h->setup_finished)
        ff_init_poc(h, h->cur_pic_ptr->field_poc, &h->cur_pic_ptr->poc);

    if (h->pps.redundant_pic_cnt_present)
        sl->redundant_pic_count = get_ue_golomb(&sl->gb);

    ret = ff_set_ref_count(h, sl);
    if (ret < 0)
        return ret;

    if (slice_type != AV_PICTURE_TYPE_I &&
        (h->current_slice == 0 ||
         slice_type != h->last_slice_type ||
         memcmp(h->last_ref_count, sl->ref_count, sizeof(sl->ref_count)))) {

        ff_h264_fill_default_ref_list(h, sl);
    }

    if (sl->slice_type_nos != AV_PICTURE_TYPE_I) {
       ret = ff_h264_decode_ref_pic_list_reordering(h, sl);
       if (ret < 0) {
           sl->ref_count[1] = sl->ref_count[0] = 0;
           return ret;
       }
    }

    if ((h->pps.weighted_pred && sl->slice_type_nos == AV_PICTURE_TYPE_P) ||
        (h->pps.weighted_bipred_idc == 1 &&
         sl->slice_type_nos == AV_PICTURE_TYPE_B))
        ff_pred_weight_table(h, sl);
    else if (h->pps.weighted_bipred_idc == 2 &&
             sl->slice_type_nos == AV_PICTURE_TYPE_B) {
        implicit_weight_table(h, sl, -1);
    } else {
        sl->use_weight = 0;
        for (i = 0; i < 2; i++) {
            sl->luma_weight_flag[i]   = 0;
            sl->chroma_weight_flag[i] = 0;
        }
    }

    // If frame-mt is enabled, only update mmco tables for the first slice
    // in a field. Subsequent slices can temporarily clobber h->mmco_index
    // or h->mmco, which will cause ref list mix-ups and decoding errors
    // further down the line. This may break decoding if the first slice is
    // corrupt, thus we only do this if frame-mt is enabled.
    if (h->nal_ref_idc) {
        ret = ff_h264_decode_ref_pic_marking(h, &sl->gb,
                                             !(h->avctx->active_thread_type & FF_THREAD_FRAME) ||
                                             h->current_slice == 0);
        if (ret < 0 && (h->avctx->err_recognition & AV_EF_EXPLODE))
            return AVERROR_INVALIDDATA;
    }

    if (FRAME_MBAFF(h)) {
        ff_h264_fill_mbaff_ref_list(h, sl);

        if (h->pps.weighted_bipred_idc == 2 && sl->slice_type_nos == AV_PICTURE_TYPE_B) {
            implicit_weight_table(h, sl, 0);
            implicit_weight_table(h, sl, 1);
        }
    }

    if (sl->slice_type_nos == AV_PICTURE_TYPE_B && !sl->direct_spatial_mv_pred)
        ff_h264_direct_dist_scale_factor(h, sl);
    ff_h264_direct_ref_list_init(h, sl);

    if (sl->slice_type_nos != AV_PICTURE_TYPE_I && h->pps.cabac) {
        tmp = get_ue_golomb_31(&sl->gb);
        if (tmp > 2) {
            av_log(h->avctx, AV_LOG_ERROR, "cabac_init_idc %u overflow\n", tmp);
            return AVERROR_INVALIDDATA;
        }
        sl->cabac_init_idc = tmp;
    }

    sl->last_qscale_diff = 0;
    tmp = h->pps.init_qp + get_se_golomb(&sl->gb);
    if (tmp > 51 + 6 * (h->sps.bit_depth_luma - 8)) {
        av_log(h->avctx, AV_LOG_ERROR, "QP %u out of range\n", tmp);
        return AVERROR_INVALIDDATA;
    }
    sl->qscale       = tmp;
    sl->chroma_qp[0] = get_chroma_qp(h, 0, sl->qscale);
    sl->chroma_qp[1] = get_chroma_qp(h, 1, sl->qscale);
    // FIXME qscale / qp ... stuff
    if (sl->slice_type == AV_PICTURE_TYPE_SP)
        get_bits1(&sl->gb); /* sp_for_switch_flag */
    if (sl->slice_type == AV_PICTURE_TYPE_SP ||
        sl->slice_type == AV_PICTURE_TYPE_SI)
        get_se_golomb(&sl->gb); /* slice_qs_delta */

    sl->deblocking_filter     = 1;
    sl->slice_alpha_c0_offset = 0;
    sl->slice_beta_offset     = 0;
    if (h->pps.deblocking_filter_parameters_present) {
        tmp = get_ue_golomb_31(&sl->gb);
        if (tmp > 2) {
            av_log(h->avctx, AV_LOG_ERROR,
                   "deblocking_filter_idc %u out of range\n", tmp);
            return AVERROR_INVALIDDATA;
        }
        sl->deblocking_filter = tmp;
        if (sl->deblocking_filter < 2)
            sl->deblocking_filter ^= 1;  // 1<->0

        if (sl->deblocking_filter) {
            sl->slice_alpha_c0_offset = get_se_golomb(&sl->gb) * 2;
            sl->slice_beta_offset     = get_se_golomb(&sl->gb) * 2;
            if (sl->slice_alpha_c0_offset >  12 ||
                sl->slice_alpha_c0_offset < -12 ||
                sl->slice_beta_offset >  12     ||
                sl->slice_beta_offset < -12) {
                av_log(h->avctx, AV_LOG_ERROR,
                       "deblocking filter parameters %d %d out of range\n",
                       sl->slice_alpha_c0_offset, sl->slice_beta_offset);
                return AVERROR_INVALIDDATA;
            }
        }
    }

    if (h->avctx->skip_loop_filter >= AVDISCARD_ALL ||
        (h->avctx->skip_loop_filter >= AVDISCARD_NONKEY &&
         h->nal_unit_type != NAL_IDR_SLICE) ||
        (h->avctx->skip_loop_filter >= AVDISCARD_NONINTRA &&
         sl->slice_type_nos != AV_PICTURE_TYPE_I) ||
        (h->avctx->skip_loop_filter >= AVDISCARD_BIDIR  &&
         sl->slice_type_nos == AV_PICTURE_TYPE_B) ||
        (h->avctx->skip_loop_filter >= AVDISCARD_NONREF &&
         h->nal_ref_idc == 0))
        sl->deblocking_filter = 0;

    if (sl->deblocking_filter == 1 && h->max_contexts > 1) {
        if (h->avctx->flags2 & AV_CODEC_FLAG2_FAST) {
            /* Cheat slightly for speed:
             * Do not bother to deblock across slices. */
            sl->deblocking_filter = 2;
        } else {
            h->max_contexts = 1;
            if (!h->single_decode_warning) {
                av_log(h->avctx, AV_LOG_INFO,
                       "Cannot parallelize slice decoding with deblocking filter type 1, decoding such frames in sequential order\n"
                       "To parallelize slice decoding you need video encoded with disable_deblocking_filter_idc set to 2 (deblock only edges that do not cross slices).\n"
                       "Setting the flags2 libavcodec option to +fast (-flags2 +fast) will disable deblocking across slices and enable parallel slice decoding "
                       "but will generate non-standard-compliant output.\n");
                h->single_decode_warning = 1;
            }
            if (sl != h->slice_ctx) {
                av_log(h->avctx, AV_LOG_ERROR,
                       "Deblocking switched inside frame.\n");
                return SLICE_SINGLETHREAD;
            }
        }
    }
    sl->qp_thresh = 15 -
                   FFMIN(sl->slice_alpha_c0_offset, sl->slice_beta_offset) -
                   FFMAX3(0,
                          h->pps.chroma_qp_index_offset[0],
                          h->pps.chroma_qp_index_offset[1]) +
                   6 * (h->sps.bit_depth_luma - 8);

    h->last_slice_type = slice_type;
    memcpy(h->last_ref_count, sl->ref_count, sizeof(h->last_ref_count));
    sl->slice_num       = ++h->current_slice;

    if (sl->slice_num)
        h->slice_row[(sl->slice_num-1)&(MAX_SLICES-1)]= sl->resync_mb_y;
    if (   h->slice_row[sl->slice_num&(MAX_SLICES-1)] + 3 >= sl->resync_mb_y
        && h->slice_row[sl->slice_num&(MAX_SLICES-1)] <= sl->resync_mb_y
        && sl->slice_num >= MAX_SLICES) {
        //in case of ASO this check needs to be updated depending on how we decide to assign slice numbers in this case
        av_log(h->avctx, AV_LOG_WARNING, "Possibly too many slices (%d >= %d), increase MAX_SLICES and recompile if there are artifacts\n", sl->slice_num, MAX_SLICES);
    }

    for (j = 0; j < 2; j++) {
        int id_list[16];
        int *ref2frm = sl->ref2frm[sl->slice_num & (MAX_SLICES - 1)][j];
        for (i = 0; i < 16; i++) {
            id_list[i] = 60;
            if (j < sl->list_count && i < sl->ref_count[j] &&
                sl->ref_list[j][i].parent->f->buf[0]) {
                int k;
                AVBuffer *buf = sl->ref_list[j][i].parent->f->buf[0]->buffer;
                for (k = 0; k < h->short_ref_count; k++)
                    if (h->short_ref[k]->f->buf[0]->buffer == buf) {
                        id_list[i] = k;
                        break;
                    }
                for (k = 0; k < h->long_ref_count; k++)
                    if (h->long_ref[k] && h->long_ref[k]->f->buf[0]->buffer == buf) {
                        id_list[i] = h->short_ref_count + k;
                        break;
                    }
            }
        }

        ref2frm[0] =
        ref2frm[1] = -1;
        for (i = 0; i < 16; i++)
            ref2frm[i + 2] = 4 * id_list[i] + (sl->ref_list[j][i].reference & 3);
        ref2frm[18 + 0] =
        ref2frm[18 + 1] = -1;
        for (i = 16; i < 48; i++)
            ref2frm[i + 4] = 4 * id_list[(i - 16) >> 1] +
                             (sl->ref_list[j][i].reference & 3);
    }

    h->au_pps_id = pps_id;
    h->sps.new =
    h->sps_buffers[h->pps.sps_id]->new = 0;
    h->current_sps_id = h->pps.sps_id;

    if (h->avctx->debug & FF_DEBUG_PICT_INFO) {
        av_log(h->avctx, AV_LOG_DEBUG,
               "slice:%d %s mb:%d %c%s%s pps:%u frame:%d poc:%d/%d ref:%d/%d qp:%d loop:%d:%d:%d weight:%d%s %s\n",
               sl->slice_num,
               (h->picture_structure == PICT_FRAME ? "F" : h->picture_structure == PICT_TOP_FIELD ? "T" : "B"),
               first_mb_in_slice,
               av_get_picture_type_char(sl->slice_type),
               sl->slice_type_fixed ? " fix" : "",
               h->nal_unit_type == NAL_IDR_SLICE ? " IDR" : "",
               pps_id, h->frame_num,
               h->cur_pic_ptr->field_poc[0],
               h->cur_pic_ptr->field_poc[1],
               sl->ref_count[0], sl->ref_count[1],
               sl->qscale,
               sl->deblocking_filter,
               sl->slice_alpha_c0_offset, sl->slice_beta_offset,
               sl->use_weight,
               sl->use_weight == 1 && sl->use_weight_chroma ? "c" : "",
               sl->slice_type == AV_PICTURE_TYPE_B ? (sl->direct_spatial_mv_pred ? "SPAT" : "TEMP") : "");
    }

    return 0;
}

int ff_h264_get_slice_type(const H264SliceContext *sl)
{
    switch (sl->slice_type) {
    case AV_PICTURE_TYPE_P:
        return 0;
    case AV_PICTURE_TYPE_B:
        return 1;
    case AV_PICTURE_TYPE_I:
        return 2;
    case AV_PICTURE_TYPE_SP:
        return 3;
    case AV_PICTURE_TYPE_SI:
        return 4;
    default:
        return AVERROR_INVALIDDATA;
    }
}

static av_always_inline void fill_filter_caches_inter(const H264Context *h,
                                                      H264SliceContext *sl,
                                                      int mb_type, int top_xy,
                                                      int left_xy[LEFT_MBS],
                                                      int top_type,
                                                      int left_type[LEFT_MBS],
                                                      int mb_xy, int list)
{
    int b_stride = h->b_stride;
    int16_t(*mv_dst)[2] = &sl->mv_cache[list][scan8[0]];
    int8_t *ref_cache   = &sl->ref_cache[list][scan8[0]];
    if (IS_INTER(mb_type) || IS_DIRECT(mb_type)) {
        if (USES_LIST(top_type, list)) {
            const int b_xy  = h->mb2b_xy[top_xy] + 3 * b_stride;
            const int b8_xy = 4 * top_xy + 2;
            int (*ref2frm)[64] = (void*)(sl->ref2frm[h->slice_table[top_xy] & (MAX_SLICES - 1)][0] + (MB_MBAFF(sl) ? 20 : 2));
            AV_COPY128(mv_dst - 1 * 8, h->cur_pic.motion_val[list][b_xy + 0]);
            ref_cache[0 - 1 * 8] =
            ref_cache[1 - 1 * 8] = ref2frm[list][h->cur_pic.ref_index[list][b8_xy + 0]];
            ref_cache[2 - 1 * 8] =
            ref_cache[3 - 1 * 8] = ref2frm[list][h->cur_pic.ref_index[list][b8_xy + 1]];
        } else {
            AV_ZERO128(mv_dst - 1 * 8);
            AV_WN32A(&ref_cache[0 - 1 * 8], ((LIST_NOT_USED) & 0xFF) * 0x01010101u);
        }

        if (!IS_INTERLACED(mb_type ^ left_type[LTOP])) {
            if (USES_LIST(left_type[LTOP], list)) {
                const int b_xy  = h->mb2b_xy[left_xy[LTOP]] + 3;
                const int b8_xy = 4 * left_xy[LTOP] + 1;
                int (*ref2frm)[64] =(void*)( sl->ref2frm[h->slice_table[left_xy[LTOP]] & (MAX_SLICES - 1)][0] + (MB_MBAFF(sl) ? 20 : 2));
                AV_COPY32(mv_dst - 1 +  0, h->cur_pic.motion_val[list][b_xy + b_stride * 0]);
                AV_COPY32(mv_dst - 1 +  8, h->cur_pic.motion_val[list][b_xy + b_stride * 1]);
                AV_COPY32(mv_dst - 1 + 16, h->cur_pic.motion_val[list][b_xy + b_stride * 2]);
                AV_COPY32(mv_dst - 1 + 24, h->cur_pic.motion_val[list][b_xy + b_stride * 3]);
                ref_cache[-1 +  0] =
                ref_cache[-1 +  8] = ref2frm[list][h->cur_pic.ref_index[list][b8_xy + 2 * 0]];
                ref_cache[-1 + 16] =
                ref_cache[-1 + 24] = ref2frm[list][h->cur_pic.ref_index[list][b8_xy + 2 * 1]];
            } else {
                AV_ZERO32(mv_dst - 1 +  0);
                AV_ZERO32(mv_dst - 1 +  8);
                AV_ZERO32(mv_dst - 1 + 16);
                AV_ZERO32(mv_dst - 1 + 24);
                ref_cache[-1 +  0] =
                ref_cache[-1 +  8] =
                ref_cache[-1 + 16] =
                ref_cache[-1 + 24] = LIST_NOT_USED;
            }
        }
    }

    if (!USES_LIST(mb_type, list)) {
        fill_rectangle(mv_dst, 4, 4, 8, pack16to32(0, 0), 4);
        AV_WN32A(&ref_cache[0 * 8], ((LIST_NOT_USED) & 0xFF) * 0x01010101u);
        AV_WN32A(&ref_cache[1 * 8], ((LIST_NOT_USED) & 0xFF) * 0x01010101u);
        AV_WN32A(&ref_cache[2 * 8], ((LIST_NOT_USED) & 0xFF) * 0x01010101u);
        AV_WN32A(&ref_cache[3 * 8], ((LIST_NOT_USED) & 0xFF) * 0x01010101u);
        return;
    }

    {
        int8_t *ref = &h->cur_pic.ref_index[list][4 * mb_xy];
        int (*ref2frm)[64] = (void*)(sl->ref2frm[sl->slice_num & (MAX_SLICES - 1)][0] + (MB_MBAFF(sl) ? 20 : 2));
        uint32_t ref01 = (pack16to32(ref2frm[list][ref[0]], ref2frm[list][ref[1]]) & 0x00FF00FF) * 0x0101;
        uint32_t ref23 = (pack16to32(ref2frm[list][ref[2]], ref2frm[list][ref[3]]) & 0x00FF00FF) * 0x0101;
        AV_WN32A(&ref_cache[0 * 8], ref01);
        AV_WN32A(&ref_cache[1 * 8], ref01);
        AV_WN32A(&ref_cache[2 * 8], ref23);
        AV_WN32A(&ref_cache[3 * 8], ref23);
    }

    {
        int16_t(*mv_src)[2] = &h->cur_pic.motion_val[list][4 * sl->mb_x + 4 * sl->mb_y * b_stride];
        AV_COPY128(mv_dst + 8 * 0, mv_src + 0 * b_stride);
        AV_COPY128(mv_dst + 8 * 1, mv_src + 1 * b_stride);
        AV_COPY128(mv_dst + 8 * 2, mv_src + 2 * b_stride);
        AV_COPY128(mv_dst + 8 * 3, mv_src + 3 * b_stride);
    }
}

/**
 *
 * @return non zero if the loop filter can be skipped
 */
static int fill_filter_caches(const H264Context *h, H264SliceContext *sl, int mb_type)
{
    const int mb_xy = sl->mb_xy;
    int top_xy, left_xy[LEFT_MBS];
    int top_type, left_type[LEFT_MBS];
    uint8_t *nnz;
    uint8_t *nnz_cache;

    top_xy = mb_xy - (h->mb_stride << MB_FIELD(sl));

    /* Wow, what a mess, why didn't they simplify the interlacing & intra
     * stuff, I can't imagine that these complex rules are worth it. */

    left_xy[LBOT] = left_xy[LTOP] = mb_xy - 1;
    if (FRAME_MBAFF(h)) {
        const int left_mb_field_flag = IS_INTERLACED(h->cur_pic.mb_type[mb_xy - 1]);
        const int curr_mb_field_flag = IS_INTERLACED(mb_type);
        if (sl->mb_y & 1) {
            if (left_mb_field_flag != curr_mb_field_flag)
                left_xy[LTOP] -= h->mb_stride;
        } else {
            if (curr_mb_field_flag)
                top_xy += h->mb_stride &
                          (((h->cur_pic.mb_type[top_xy] >> 7) & 1) - 1);
            if (left_mb_field_flag != curr_mb_field_flag)
                left_xy[LBOT] += h->mb_stride;
        }
    }

    sl->top_mb_xy        = top_xy;
    sl->left_mb_xy[LTOP] = left_xy[LTOP];
    sl->left_mb_xy[LBOT] = left_xy[LBOT];
    {
        /* For sufficiently low qp, filtering wouldn't do anything.
         * This is a conservative estimate: could also check beta_offset
         * and more accurate chroma_qp. */
        int qp_thresh = sl->qp_thresh; // FIXME strictly we should store qp_thresh for each mb of a slice
        int qp        = h->cur_pic.qscale_table[mb_xy];
        if (qp <= qp_thresh &&
            (left_xy[LTOP] < 0 ||
             ((qp + h->cur_pic.qscale_table[left_xy[LTOP]] + 1) >> 1) <= qp_thresh) &&
            (top_xy < 0 ||
             ((qp + h->cur_pic.qscale_table[top_xy] + 1) >> 1) <= qp_thresh)) {
            if (!FRAME_MBAFF(h))
                return 1;
            if ((left_xy[LTOP] < 0 ||
                 ((qp + h->cur_pic.qscale_table[left_xy[LBOT]] + 1) >> 1) <= qp_thresh) &&
                (top_xy < h->mb_stride ||
                 ((qp + h->cur_pic.qscale_table[top_xy - h->mb_stride] + 1) >> 1) <= qp_thresh))
                return 1;
        }
    }

    top_type        = h->cur_pic.mb_type[top_xy];
    left_type[LTOP] = h->cur_pic.mb_type[left_xy[LTOP]];
    left_type[LBOT] = h->cur_pic.mb_type[left_xy[LBOT]];
    if (sl->deblocking_filter == 2) {
        if (h->slice_table[top_xy] != sl->slice_num)
            top_type = 0;
        if (h->slice_table[left_xy[LBOT]] != sl->slice_num)
            left_type[LTOP] = left_type[LBOT] = 0;
    } else {
        if (h->slice_table[top_xy] == 0xFFFF)
            top_type = 0;
        if (h->slice_table[left_xy[LBOT]] == 0xFFFF)
            left_type[LTOP] = left_type[LBOT] = 0;
    }
    sl->top_type        = top_type;
    sl->left_type[LTOP] = left_type[LTOP];
    sl->left_type[LBOT] = left_type[LBOT];

    if (IS_INTRA(mb_type))
        return 0;

    fill_filter_caches_inter(h, sl, mb_type, top_xy, left_xy,
                             top_type, left_type, mb_xy, 0);
    if (sl->list_count == 2)
        fill_filter_caches_inter(h, sl, mb_type, top_xy, left_xy,
                                 top_type, left_type, mb_xy, 1);

    nnz       = h->non_zero_count[mb_xy];
    nnz_cache = sl->non_zero_count_cache;
    AV_COPY32(&nnz_cache[4 + 8 * 1], &nnz[0]);
    AV_COPY32(&nnz_cache[4 + 8 * 2], &nnz[4]);
    AV_COPY32(&nnz_cache[4 + 8 * 3], &nnz[8]);
    AV_COPY32(&nnz_cache[4 + 8 * 4], &nnz[12]);
    sl->cbp = h->cbp_table[mb_xy];

    if (top_type) {
        nnz = h->non_zero_count[top_xy];
        AV_COPY32(&nnz_cache[4 + 8 * 0], &nnz[3 * 4]);
    }

    if (left_type[LTOP]) {
        nnz = h->non_zero_count[left_xy[LTOP]];
        nnz_cache[3 + 8 * 1] = nnz[3 + 0 * 4];
        nnz_cache[3 + 8 * 2] = nnz[3 + 1 * 4];
        nnz_cache[3 + 8 * 3] = nnz[3 + 2 * 4];
        nnz_cache[3 + 8 * 4] = nnz[3 + 3 * 4];
    }

    /* CAVLC 8x8dct requires NNZ values for residual decoding that differ
     * from what the loop filter needs */
    if (!CABAC(h) && h->pps.transform_8x8_mode) {
        if (IS_8x8DCT(top_type)) {
            nnz_cache[4 + 8 * 0] =
            nnz_cache[5 + 8 * 0] = (h->cbp_table[top_xy] & 0x4000) >> 12;
            nnz_cache[6 + 8 * 0] =
            nnz_cache[7 + 8 * 0] = (h->cbp_table[top_xy] & 0x8000) >> 12;
        }
        if (IS_8x8DCT(left_type[LTOP])) {
            nnz_cache[3 + 8 * 1] =
            nnz_cache[3 + 8 * 2] = (h->cbp_table[left_xy[LTOP]] & 0x2000) >> 12; // FIXME check MBAFF
        }
        if (IS_8x8DCT(left_type[LBOT])) {
            nnz_cache[3 + 8 * 3] =
            nnz_cache[3 + 8 * 4] = (h->cbp_table[left_xy[LBOT]] & 0x8000) >> 12; // FIXME check MBAFF
        }

        if (IS_8x8DCT(mb_type)) {
            nnz_cache[scan8[0]] =
            nnz_cache[scan8[1]] =
            nnz_cache[scan8[2]] =
            nnz_cache[scan8[3]] = (sl->cbp & 0x1000) >> 12;

            nnz_cache[scan8[0 + 4]] =
            nnz_cache[scan8[1 + 4]] =
            nnz_cache[scan8[2 + 4]] =
            nnz_cache[scan8[3 + 4]] = (sl->cbp & 0x2000) >> 12;

            nnz_cache[scan8[0 + 8]] =
            nnz_cache[scan8[1 + 8]] =
            nnz_cache[scan8[2 + 8]] =
            nnz_cache[scan8[3 + 8]] = (sl->cbp & 0x4000) >> 12;

            nnz_cache[scan8[0 + 12]] =
            nnz_cache[scan8[1 + 12]] =
            nnz_cache[scan8[2 + 12]] =
            nnz_cache[scan8[3 + 12]] = (sl->cbp & 0x8000) >> 12;
        }
    }

    return 0;
}

static void loop_filter(const H264Context *h, H264SliceContext *sl, int start_x, int end_x)
{
    uint8_t *dest_y, *dest_cb, *dest_cr;
    int linesize, uvlinesize, mb_x, mb_y;
    const int end_mb_y       = sl->mb_y + FRAME_MBAFF(h);
    const int old_slice_type = sl->slice_type;
    const int pixel_shift    = h->pixel_shift;
    const int block_h        = 16 >> h->chroma_y_shift;

    if (sl->deblocking_filter) {
        for (mb_x = start_x; mb_x < end_x; mb_x++)
            for (mb_y = end_mb_y - FRAME_MBAFF(h); mb_y <= end_mb_y; mb_y++) {
                int mb_xy, mb_type;
                mb_xy         = sl->mb_xy = mb_x + mb_y * h->mb_stride;
                sl->slice_num = h->slice_table[mb_xy];
                mb_type       = h->cur_pic.mb_type[mb_xy];
                sl->list_count = h->list_counts[mb_xy];

                if (FRAME_MBAFF(h))
                    sl->mb_mbaff               =
                    sl->mb_field_decoding_flag = !!IS_INTERLACED(mb_type);

                sl->mb_x = mb_x;
                sl->mb_y = mb_y;
                dest_y  = h->cur_pic.f->data[0] +
                          ((mb_x << pixel_shift) + mb_y * sl->linesize) * 16;
                dest_cb = h->cur_pic.f->data[1] +
                          (mb_x << pixel_shift) * (8 << CHROMA444(h)) +
                          mb_y * sl->uvlinesize * block_h;
                dest_cr = h->cur_pic.f->data[2] +
                          (mb_x << pixel_shift) * (8 << CHROMA444(h)) +
                          mb_y * sl->uvlinesize * block_h;
                // FIXME simplify above

                if (MB_FIELD(sl)) {
                    linesize   = sl->mb_linesize   = sl->linesize   * 2;
                    uvlinesize = sl->mb_uvlinesize = sl->uvlinesize * 2;
                    if (mb_y & 1) { // FIXME move out of this function?
                        dest_y  -= sl->linesize   * 15;
                        dest_cb -= sl->uvlinesize * (block_h - 1);
                        dest_cr -= sl->uvlinesize * (block_h - 1);
                    }
                } else {
                    linesize   = sl->mb_linesize   = sl->linesize;
                    uvlinesize = sl->mb_uvlinesize = sl->uvlinesize;
                }
                backup_mb_border(h, sl, dest_y, dest_cb, dest_cr, linesize,
                                 uvlinesize, 0);
                if (fill_filter_caches(h, sl, mb_type))
                    continue;
                sl->chroma_qp[0] = get_chroma_qp(h, 0, h->cur_pic.qscale_table[mb_xy]);
                sl->chroma_qp[1] = get_chroma_qp(h, 1, h->cur_pic.qscale_table[mb_xy]);

                if (FRAME_MBAFF(h)) {
                    ff_h264_filter_mb(h, sl, mb_x, mb_y, dest_y, dest_cb, dest_cr,
                                      linesize, uvlinesize);
                } else {
                    ff_h264_filter_mb_fast(h, sl, mb_x, mb_y, dest_y, dest_cb,
                                           dest_cr, linesize, uvlinesize);
                }
            }
    }
    sl->slice_type  = old_slice_type;
    sl->mb_x         = end_x;
    sl->mb_y         = end_mb_y - FRAME_MBAFF(h);
    sl->chroma_qp[0] = get_chroma_qp(h, 0, sl->qscale);
    sl->chroma_qp[1] = get_chroma_qp(h, 1, sl->qscale);
}

static void predict_field_decoding_flag(const H264Context *h, H264SliceContext *sl)
{
    const int mb_xy = sl->mb_x + sl->mb_y * h->mb_stride;
    int mb_type     = (h->slice_table[mb_xy - 1] == sl->slice_num) ?
                      h->cur_pic.mb_type[mb_xy - 1] :
                      (h->slice_table[mb_xy - h->mb_stride] == sl->slice_num) ?
                      h->cur_pic.mb_type[mb_xy - h->mb_stride] : 0;
    sl->mb_mbaff    = sl->mb_field_decoding_flag = IS_INTERLACED(mb_type) ? 1 : 0;
}

/**
 * Draw edges and report progress for the last MB row.
 */
static void decode_finish_row(const H264Context *h, H264SliceContext *sl)
{
    int top            = 16 * (sl->mb_y      >> FIELD_PICTURE(h));
    int pic_height     = 16 *  h->mb_height >> FIELD_PICTURE(h);
    int height         =  16      << FRAME_MBAFF(h);
    int deblock_border = (16 + 4) << FRAME_MBAFF(h);

    if (sl->deblocking_filter) {
        if ((top + height) >= pic_height)
            height += deblock_border;
        top -= deblock_border;
    }

    if (top >= pic_height || (top + height) < 0)
        return;

    height = FFMIN(height, pic_height - top);
    if (top < 0) {
        height = top + height;
        top    = 0;
    }

    ff_h264_draw_horiz_band(h, sl, top, height);

    if (h->droppable || sl->h264->slice_ctx[0].er.error_occurred)
        return;

    ff_thread_report_progress(&h->cur_pic_ptr->tf, top + height - 1,
                              h->picture_structure == PICT_BOTTOM_FIELD);
}

static void er_add_slice(H264SliceContext *sl,
                         int startx, int starty,
                         int endx, int endy, int status)
{
    if (!sl->h264->enable_er)
        return;

    if (CONFIG_ERROR_RESILIENCE) {
        ERContext *er = &sl->h264->slice_ctx[0].er;

        ff_er_add_slice(er, startx, starty, endx, endy, status);
    }
}

static int decode_slice(struct AVCodecContext *avctx, void *arg)
{
    H264SliceContext *sl = arg;
    const H264Context *h = sl->h264;
    int lf_x_start = sl->mb_x;
    int ret;

    sl->linesize   = h->cur_pic_ptr->f->linesize[0];
    sl->uvlinesize = h->cur_pic_ptr->f->linesize[1];

    ret = alloc_scratch_buffers(sl, sl->linesize);
    if (ret < 0)
        return ret;

    sl->mb_skip_run = -1;

    av_assert0(h->block_offset[15] == (4 * ((scan8[15] - scan8[0]) & 7) << h->pixel_shift) + 4 * sl->linesize * ((scan8[15] - scan8[0]) >> 3));

    sl->is_complex = FRAME_MBAFF(h) || h->picture_structure != PICT_FRAME ||
                     avctx->codec_id != AV_CODEC_ID_H264 ||
                     (CONFIG_GRAY && (h->flags & AV_CODEC_FLAG_GRAY));

    if (!(h->avctx->active_thread_type & FF_THREAD_SLICE) && h->picture_structure == PICT_FRAME && h->slice_ctx[0].er.error_status_table) {
        const int start_i  = av_clip(sl->resync_mb_x + sl->resync_mb_y * h->mb_width, 0, h->mb_num - 1);
        if (start_i) {
            int prev_status = h->slice_ctx[0].er.error_status_table[h->slice_ctx[0].er.mb_index2xy[start_i - 1]];
            prev_status &= ~ VP_START;
            if (prev_status != (ER_MV_END | ER_DC_END | ER_AC_END))
                h->slice_ctx[0].er.error_occurred = 1;
        }
    }

    if (h->pps.cabac) {
        /* realign */
        align_get_bits(&sl->gb);

        /* init cabac */
        ret = ff_init_cabac_decoder(&sl->cabac,
                              sl->gb.buffer + get_bits_count(&sl->gb) / 8,
                              (get_bits_left(&sl->gb) + 7) / 8);
        if (ret < 0)
            return ret;

        ff_h264_init_cabac_states(h, sl);

        for (;;) {
            // START_TIMER
            int ret, eos;
            if (sl->mb_x + sl->mb_y * h->mb_width >= sl->next_slice_idx) {
                av_log(h->avctx, AV_LOG_ERROR, "Slice overlaps with next at %d\n",
                       sl->next_slice_idx);
                er_add_slice(sl, sl->resync_mb_x, sl->resync_mb_y, sl->mb_x,
                             sl->mb_y, ER_MB_ERROR);
                return AVERROR_INVALIDDATA;
            }

            ret = ff_h264_decode_mb_cabac(h, sl);
            // STOP_TIMER("decode_mb_cabac")

            if (ret >= 0)
                ff_h264_hl_decode_mb(h, sl);

            // FIXME optimal? or let mb_decode decode 16x32 ?
            if (ret >= 0 && FRAME_MBAFF(h)) {
                sl->mb_y++;

                ret = ff_h264_decode_mb_cabac(h, sl);

                if (ret >= 0)
                    ff_h264_hl_decode_mb(h, sl);
                sl->mb_y--;
            }
            eos = get_cabac_terminate(&sl->cabac);

            if ((h->workaround_bugs & FF_BUG_TRUNCATED) &&
                sl->cabac.bytestream > sl->cabac.bytestream_end + 2) {
                er_add_slice(sl, sl->resync_mb_x, sl->resync_mb_y, sl->mb_x - 1,
                             sl->mb_y, ER_MB_END);
                if (sl->mb_x >= lf_x_start)
                    loop_filter(h, sl, lf_x_start, sl->mb_x + 1);
                return 0;
            }
            if (sl->cabac.bytestream > sl->cabac.bytestream_end + 2 )
                av_log(h->avctx, AV_LOG_DEBUG, "bytestream overread %"PTRDIFF_SPECIFIER"\n", sl->cabac.bytestream_end - sl->cabac.bytestream);
            if (ret < 0 || sl->cabac.bytestream > sl->cabac.bytestream_end + 4) {
                av_log(h->avctx, AV_LOG_ERROR,
                       "error while decoding MB %d %d, bytestream %"PTRDIFF_SPECIFIER"\n",
                       sl->mb_x, sl->mb_y,
                       sl->cabac.bytestream_end - sl->cabac.bytestream);
                er_add_slice(sl, sl->resync_mb_x, sl->resync_mb_y, sl->mb_x,
                             sl->mb_y, ER_MB_ERROR);
                return AVERROR_INVALIDDATA;
            }

            if (++sl->mb_x >= h->mb_width) {
                loop_filter(h, sl, lf_x_start, sl->mb_x);
                sl->mb_x = lf_x_start = 0;
                decode_finish_row(h, sl);
                ++sl->mb_y;
                if (FIELD_OR_MBAFF_PICTURE(h)) {
                    ++sl->mb_y;
                    if (FRAME_MBAFF(h) && sl->mb_y < h->mb_height)
                        predict_field_decoding_flag(h, sl);
                }
            }

            if (eos || sl->mb_y >= h->mb_height) {
                ff_tlog(h->avctx, "slice end %d %d\n",
                        get_bits_count(&sl->gb), sl->gb.size_in_bits);
                er_add_slice(sl, sl->resync_mb_x, sl->resync_mb_y, sl->mb_x - 1,
                             sl->mb_y, ER_MB_END);
                if (sl->mb_x > lf_x_start)
                    loop_filter(h, sl, lf_x_start, sl->mb_x);
                return 0;
            }
        }
    } else {
        for (;;) {
            int ret;

            if (sl->mb_x + sl->mb_y * h->mb_width >= sl->next_slice_idx) {
                av_log(h->avctx, AV_LOG_ERROR, "Slice overlaps with next at %d\n",
                       sl->next_slice_idx);
                er_add_slice(sl, sl->resync_mb_x, sl->resync_mb_y, sl->mb_x,
                             sl->mb_y, ER_MB_ERROR);
                return AVERROR_INVALIDDATA;
            }

            ret = ff_h264_decode_mb_cavlc(h, sl);

            if (ret >= 0)
                ff_h264_hl_decode_mb(h, sl);

            // FIXME optimal? or let mb_decode decode 16x32 ?
            if (ret >= 0 && FRAME_MBAFF(h)) {
                sl->mb_y++;
                ret = ff_h264_decode_mb_cavlc(h, sl);

                if (ret >= 0)
                    ff_h264_hl_decode_mb(h, sl);
                sl->mb_y--;
            }

            if (ret < 0) {
                av_log(h->avctx, AV_LOG_ERROR,
                       "error while decoding MB %d %d\n", sl->mb_x, sl->mb_y);
                er_add_slice(sl, sl->resync_mb_x, sl->resync_mb_y, sl->mb_x,
                             sl->mb_y, ER_MB_ERROR);
                return ret;
            }

            if (++sl->mb_x >= h->mb_width) {
                loop_filter(h, sl, lf_x_start, sl->mb_x);
                sl->mb_x = lf_x_start = 0;
                decode_finish_row(h, sl);
                ++sl->mb_y;
                if (FIELD_OR_MBAFF_PICTURE(h)) {
                    ++sl->mb_y;
                    if (FRAME_MBAFF(h) && sl->mb_y < h->mb_height)
                        predict_field_decoding_flag(h, sl);
                }
                if (sl->mb_y >= h->mb_height) {
                    ff_tlog(h->avctx, "slice end %d %d\n",
                            get_bits_count(&sl->gb), sl->gb.size_in_bits);

                    if (   get_bits_left(&sl->gb) == 0
                        || get_bits_left(&sl->gb) > 0 && !(h->avctx->err_recognition & AV_EF_AGGRESSIVE)) {
                        er_add_slice(sl, sl->resync_mb_x, sl->resync_mb_y,
                                     sl->mb_x - 1, sl->mb_y, ER_MB_END);

                        return 0;
                    } else {
                        er_add_slice(sl, sl->resync_mb_x, sl->resync_mb_y,
                                     sl->mb_x, sl->mb_y, ER_MB_END);

                        return AVERROR_INVALIDDATA;
                    }
                }
            }

            if (get_bits_left(&sl->gb) <= 0 && sl->mb_skip_run <= 0) {
                ff_tlog(h->avctx, "slice end %d %d\n",
                        get_bits_count(&sl->gb), sl->gb.size_in_bits);

                if (get_bits_left(&sl->gb) == 0) {
                    er_add_slice(sl, sl->resync_mb_x, sl->resync_mb_y,
                                 sl->mb_x - 1, sl->mb_y, ER_MB_END);
                    if (sl->mb_x > lf_x_start)
                        loop_filter(h, sl, lf_x_start, sl->mb_x);

                    return 0;
                } else {
                    er_add_slice(sl, sl->resync_mb_x, sl->resync_mb_y, sl->mb_x,
                                 sl->mb_y, ER_MB_ERROR);

                    return AVERROR_INVALIDDATA;
                }
            }
        }
    }
}

/**
 * Call decode_slice() for each context.
 *
 * @param h h264 master context
 * @param context_count number of contexts to execute
 */
int ff_h264_execute_decode_slices(H264Context *h, unsigned context_count)
{
    AVCodecContext *const avctx = h->avctx;
    H264SliceContext *sl;
    int i, j;

    av_assert0(context_count && h->slice_ctx[context_count - 1].mb_y < h->mb_height);

    h->slice_ctx[0].next_slice_idx = INT_MAX;

    if (h->avctx->hwaccel
#if FF_API_CAP_VDPAU
        || h->avctx->codec->capabilities & AV_CODEC_CAP_HWACCEL_VDPAU
#endif
        )
        return 0;
    if (context_count == 1) {
        int ret;

        h->slice_ctx[0].next_slice_idx = h->mb_width * h->mb_height;

        ret = decode_slice(avctx, &h->slice_ctx[0]);
        h->mb_y = h->slice_ctx[0].mb_y;
        return ret;
    } else {
        av_assert0(context_count > 0);
        for (i = 0; i < context_count; i++) {
            int next_slice_idx = h->mb_width * h->mb_height;
            int slice_idx;

            sl                 = &h->slice_ctx[i];
            if (CONFIG_ERROR_RESILIENCE) {
                sl->er.error_count = 0;
            }

            /* make sure none of those slices overlap */
            slice_idx = sl->mb_y * h->mb_width + sl->mb_x;
            for (j = 0; j < context_count; j++) {
                H264SliceContext *sl2 = &h->slice_ctx[j];
                int        slice_idx2 = sl2->mb_y * h->mb_width + sl2->mb_x;

                if (i == j || slice_idx2 < slice_idx)
                    continue;
                next_slice_idx = FFMIN(next_slice_idx, slice_idx2);
            }
            sl->next_slice_idx = next_slice_idx;
        }

        avctx->execute(avctx, decode_slice, h->slice_ctx,
                       NULL, context_count, sizeof(h->slice_ctx[0]));

        /* pull back stuff from slices to master context */
        sl                   = &h->slice_ctx[context_count - 1];
        h->mb_y              = sl->mb_y;
        if (CONFIG_ERROR_RESILIENCE) {
            for (i = 1; i < context_count; i++)
                h->slice_ctx[0].er.error_count += h->slice_ctx[i].er.error_count;
        }
    }

    return 0;
}
