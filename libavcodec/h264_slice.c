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
#include "dsputil.h"
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

static const enum AVPixelFormat h264_hwaccel_pixfmt_list_420[] = {
#if CONFIG_H264_DXVA2_HWACCEL
    AV_PIX_FMT_DXVA2_VLD,
#endif
#if CONFIG_H264_VAAPI_HWACCEL
    AV_PIX_FMT_VAAPI_VLD,
#endif
#if CONFIG_H264_VDA_HWACCEL
    AV_PIX_FMT_VDA_VLD,
#endif
#if CONFIG_H264_VDPAU_HWACCEL
    AV_PIX_FMT_VDPAU,
#endif
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_NONE
};

static const enum AVPixelFormat h264_hwaccel_pixfmt_list_jpeg_420[] = {
#if CONFIG_H264_DXVA2_HWACCEL
    AV_PIX_FMT_DXVA2_VLD,
#endif
#if CONFIG_H264_VAAPI_HWACCEL
    AV_PIX_FMT_VAAPI_VLD,
#endif
#if CONFIG_H264_VDA_HWACCEL
    AV_PIX_FMT_VDA_VLD,
#endif
#if CONFIG_H264_VDPAU_HWACCEL
    AV_PIX_FMT_VDPAU,
#endif
    AV_PIX_FMT_YUVJ420P,
    AV_PIX_FMT_NONE
};


static void release_unused_pictures(H264Context *h, int remove_current)
{
    int i;

    /* release non reference frames */
    for (i = 0; i < H264_MAX_PICTURE_COUNT; i++) {
        if (h->DPB[i].f.buf[0] && !h->DPB[i].reference &&
            (remove_current || &h->DPB[i] != h->cur_pic_ptr)) {
            ff_h264_unref_picture(h, &h->DPB[i]);
        }
    }
}

static int alloc_scratch_buffers(H264Context *h, int linesize)
{
    int alloc_size = FFALIGN(FFABS(linesize) + 32, 32);

    if (h->bipred_scratchpad)
        return 0;

    h->bipred_scratchpad = av_malloc(16 * 6 * alloc_size);
    // edge emu needs blocksize + filter length - 1
    // (= 21x21 for  h264)
    h->edge_emu_buffer = av_mallocz(alloc_size * 2 * 21);

    if (!h->bipred_scratchpad || !h->edge_emu_buffer) {
        av_freep(&h->bipred_scratchpad);
        av_freep(&h->edge_emu_buffer);
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
    h->motion_val_pool = av_buffer_pool_init(2 * (b4_array_size + 4) *
                                             sizeof(int16_t), av_buffer_allocz);
    h->ref_index_pool  = av_buffer_pool_init(4 * mb_array_size, av_buffer_allocz);

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

    av_assert0(!pic->f.data[0]);

    pic->tf.f = &pic->f;
    ret = ff_thread_get_buffer(h->avctx, &pic->tf, pic->reference ?
                                                   AV_GET_BUFFER_FLAG_REF : 0);
    if (ret < 0)
        goto fail;

    h->linesize   = pic->f.linesize[0];
    h->uvlinesize = pic->f.linesize[1];
    pic->crop     = h->sps.crop;
    pic->crop_top = h->sps.crop_top;
    pic->crop_left= h->sps.crop_left;

    if (h->avctx->hwaccel) {
        const AVHWAccel *hwaccel = h->avctx->hwaccel;
        av_assert0(!pic->hwaccel_picture_private);
        if (hwaccel->priv_data_size) {
            pic->hwaccel_priv_buf = av_buffer_allocz(hwaccel->priv_data_size);
            if (!pic->hwaccel_priv_buf)
                return AVERROR(ENOMEM);
            pic->hwaccel_picture_private = pic->hwaccel_priv_buf->data;
        }
    }
    if (!h->avctx->hwaccel && CONFIG_GRAY && h->flags & CODEC_FLAG_GRAY && pic->f.data[2]) {
        int h_chroma_shift, v_chroma_shift;
        av_pix_fmt_get_chroma_sub_sample(pic->f.format,
                                         &h_chroma_shift, &v_chroma_shift);

        for(i=0; i<FF_CEIL_RSHIFT(h->avctx->height, v_chroma_shift); i++) {
            memset(pic->f.data[1] + pic->f.linesize[1]*i,
                   0x80, FF_CEIL_RSHIFT(h->avctx->width, h_chroma_shift));
            memset(pic->f.data[2] + pic->f.linesize[2]*i,
                   0x80, FF_CEIL_RSHIFT(h->avctx->width, h_chroma_shift));
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
    if (!pic->f.buf[0])
        return 1;
    if (pic->needs_realloc && !(pic->reference & DELAYED_PIC_REF))
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

    if (h->DPB[i].needs_realloc) {
        h->DPB[i].needs_realloc = 0;
        ff_h264_unref_picture(h, &h->DPB[i]);
    }

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

void h264_init_dequant_tables(H264Context *h)
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

/**
 * Mimic alloc_tables(), but for every context thread.
 */
static void clone_tables(H264Context *dst, H264Context *src, int i)
{
    dst->intra4x4_pred_mode     = src->intra4x4_pred_mode + i * 8 * 2 * src->mb_stride;
    dst->non_zero_count         = src->non_zero_count;
    dst->slice_table            = src->slice_table;
    dst->cbp_table              = src->cbp_table;
    dst->mb2b_xy                = src->mb2b_xy;
    dst->mb2br_xy               = src->mb2br_xy;
    dst->chroma_pred_mode_table = src->chroma_pred_mode_table;
    dst->mvd_table[0]           = src->mvd_table[0] + i * 8 * 2 * src->mb_stride;
    dst->mvd_table[1]           = src->mvd_table[1] + i * 8 * 2 * src->mb_stride;
    dst->direct_table           = src->direct_table;
    dst->list_counts            = src->list_counts;
    dst->DPB                    = src->DPB;
    dst->cur_pic_ptr            = src->cur_pic_ptr;
    dst->cur_pic                = src->cur_pic;
    dst->bipred_scratchpad      = NULL;
    dst->edge_emu_buffer        = NULL;
    ff_h264_pred_init(&dst->hpc, src->avctx->codec_id, src->sps.bit_depth_luma,
                      src->sps.chroma_format_idc);
}

#define IN_RANGE(a, b, size) (((a) >= (b)) && ((a) < ((b) + (size))))
#undef REBASE_PICTURE
#define REBASE_PICTURE(pic, new_ctx, old_ctx)             \
    ((pic && pic >= old_ctx->DPB &&                       \
      pic < old_ctx->DPB + H264_MAX_PICTURE_COUNT) ?          \
     &new_ctx->DPB[pic - old_ctx->DPB] : NULL)

static void copy_picture_range(H264Picture **to, H264Picture **from, int count,
                               H264Context *new_base,
                               H264Context *old_base)
{
    int i;

    for (i = 0; i < count; i++) {
        assert((IN_RANGE(from[i], old_base, sizeof(*old_base)) ||
                IN_RANGE(from[i], old_base->DPB,
                         sizeof(H264Picture) * H264_MAX_PICTURE_COUNT) ||
                !from[i]));
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
    memcpy(&to->start_field, &from->start_field,                        \
           (char *)&to->end_field - (char *)&to->start_field)

static int h264_slice_header_init(H264Context *h, int reinit);

int ff_h264_update_thread_context(AVCodecContext *dst,
                                  const AVCodecContext *src)
{
    H264Context *h = dst->priv_data, *h1 = src->priv_data;
    int inited = h->context_initialized, err = 0;
    int context_reinitialized = 0;
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

        /* set bits_per_raw_sample to the previous value. the check for changed
         * bit depth in h264_set_parameter_from_sps() uses it and sets it to
         * the current value */
        h->avctx->bits_per_raw_sample = h->sps.bit_depth_luma;

        av_freep(&h->bipred_scratchpad);

        h->width     = h1->width;
        h->height    = h1->height;
        h->mb_height = h1->mb_height;
        h->mb_width  = h1->mb_width;
        h->mb_num    = h1->mb_num;
        h->mb_stride = h1->mb_stride;
        h->b_stride  = h1->b_stride;
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

        if ((err = h264_slice_header_init(h, 1)) < 0) {
            av_log(h->avctx, AV_LOG_ERROR, "h264_slice_header_init() failed");
            return err;
        }
        context_reinitialized = 1;

#if 0
        h264_set_parameter_from_sps(h);
        //Note we set context_reinitialized which will cause h264_set_parameter_from_sps to be reexecuted
        h->cur_chroma_format_idc = h1->cur_chroma_format_idc;
#endif
    }
    /* update linesize on resize for h264. The h264 decoder doesn't
     * necessarily call ff_MPV_frame_start in the new thread */
    h->linesize   = h1->linesize;
    h->uvlinesize = h1->uvlinesize;

    /* copy block_offset since frame_start may not be called */
    memcpy(h->block_offset, h1->block_offset, sizeof(h->block_offset));

    if (!inited) {
        for (i = 0; i < MAX_SPS_COUNT; i++)
            av_freep(h->sps_buffers + i);

        for (i = 0; i < MAX_PPS_COUNT; i++)
            av_freep(h->pps_buffers + i);

        av_freep(&h->rbsp_buffer[0]);
        av_freep(&h->rbsp_buffer[1]);
        memcpy(h, h1, offsetof(H264Context, intra_pcm_ptr));
        memcpy(&h->cabac, &h1->cabac,
               sizeof(H264Context) - offsetof(H264Context, cabac));
        av_assert0((void*)&h->cabac == &h->mb_padding + 1);

        memset(h->sps_buffers, 0, sizeof(h->sps_buffers));
        memset(h->pps_buffers, 0, sizeof(h->pps_buffers));

        memset(&h->er, 0, sizeof(h->er));
        memset(&h->mb, 0, sizeof(h->mb));
        memset(&h->mb_luma_dc, 0, sizeof(h->mb_luma_dc));
        memset(&h->mb_padding, 0, sizeof(h->mb_padding));

        h->avctx             = dst;
        h->DPB               = NULL;
        h->qscale_table_pool = NULL;
        h->mb_type_pool      = NULL;
        h->ref_index_pool    = NULL;
        h->motion_val_pool   = NULL;
        for (i = 0; i < 2; i++) {
            h->rbsp_buffer[i] = NULL;
            h->rbsp_buffer_size[i] = 0;
        }

        if (h1->context_initialized) {
        h->context_initialized = 0;

        memset(&h->cur_pic, 0, sizeof(h->cur_pic));
        av_frame_unref(&h->cur_pic.f);
        h->cur_pic.tf.f = &h->cur_pic.f;

        ret = ff_h264_alloc_tables(h);
        if (ret < 0) {
            av_log(dst, AV_LOG_ERROR, "Could not allocate memory\n");
            return ret;
        }
        ret = ff_h264_context_init(h);
        if (ret < 0) {
            av_log(dst, AV_LOG_ERROR, "context_init() failed.\n");
            return ret;
        }
        }

        h->bipred_scratchpad = NULL;
        h->edge_emu_buffer   = NULL;

        h->thread_context[0] = h;
        h->context_initialized = h1->context_initialized;
    }

    h->avctx->coded_height  = h1->avctx->coded_height;
    h->avctx->coded_width   = h1->avctx->coded_width;
    h->avctx->width         = h1->avctx->width;
    h->avctx->height        = h1->avctx->height;
    h->coded_picture_number = h1->coded_picture_number;
    h->first_field          = h1->first_field;
    h->picture_structure    = h1->picture_structure;
    h->qscale               = h1->qscale;
    h->droppable            = h1->droppable;
    h->low_delay            = h1->low_delay;

    for (i = 0; h->DPB && i < H264_MAX_PICTURE_COUNT; i++) {
        ff_h264_unref_picture(h, &h->DPB[i]);
        if (h1->DPB && h1->DPB[i].f.buf[0] &&
            (ret = ff_h264_ref_picture(h, &h->DPB[i], &h1->DPB[i])) < 0)
            return ret;
    }

    h->cur_pic_ptr = REBASE_PICTURE(h1->cur_pic_ptr, h, h1);
    ff_h264_unref_picture(h, &h->cur_pic);
    if (h1->cur_pic.f.buf[0] && (ret = ff_h264_ref_picture(h, &h->cur_pic, &h1->cur_pic)) < 0)
        return ret;

    h->workaround_bugs = h1->workaround_bugs;
    h->low_delay       = h1->low_delay;
    h->droppable       = h1->droppable;

    // extradata/NAL handling
    h->is_avc = h1->is_avc;

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
    copy_fields(h, h1, poc_lsb, redundant_pic_count);

    // reference lists
    copy_fields(h, h1, short_ref, cabac_init_idc);

    copy_picture_range(h->short_ref, h1->short_ref, 32, h, h1);
    copy_picture_range(h->long_ref, h1->long_ref, 32, h, h1);
    copy_picture_range(h->delayed_pic, h1->delayed_pic,
                       MAX_DELAYED_PIC_COUNT + 2, h, h1);

    h->frame_recovered       = h1->frame_recovered;

    if (context_reinitialized)
        ff_h264_set_parameter_from_sps(h);

    if (!h->cur_pic_ptr)
        return 0;

    if (!h->droppable) {
        err = ff_h264_execute_ref_pic_marking(h, h->mmco, h->mmco_index);
        h->prev_poc_msb = h->poc_msb;
        h->prev_poc_lsb = h->poc_lsb;
    }
    h->prev_frame_num_offset = h->frame_num_offset;
    h->prev_frame_num        = h->frame_num;
    h->outputed_poc          = h->next_outputed_poc;

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
    pic->f.coded_picture_number = h->coded_picture_number++;
    pic->field_picture          = h->picture_structure != PICT_FRAME;

    /*
     * Zero key_frame here; IDR markings per slice in frame or fields are ORed
     * in later.
     * See decode_nal_units().
     */
    pic->f.key_frame = 0;
    pic->mmco_reset  = 0;
    pic->recovered   = 0;
    pic->invalid_gap = 0;

    if ((ret = alloc_picture(h, pic)) < 0)
        return ret;
    if(!h->frame_recovered && !h->avctx->hwaccel &&
       !(h->avctx->codec->capabilities & CODEC_CAP_HWACCEL_VDPAU))
        avpriv_color_frame(&pic->f, c);

    h->cur_pic_ptr = pic;
    ff_h264_unref_picture(h, &h->cur_pic);
    if (CONFIG_ERROR_RESILIENCE) {
        ff_h264_set_erpic(&h->er.cur_pic, NULL);
    }

    if ((ret = ff_h264_ref_picture(h, &h->cur_pic, h->cur_pic_ptr)) < 0)
        return ret;

    if (CONFIG_ERROR_RESILIENCE) {
        ff_er_frame_start(&h->er);
        ff_h264_set_erpic(&h->er.last_pic, NULL);
        ff_h264_set_erpic(&h->er.next_pic, NULL);
    }

    assert(h->linesize && h->uvlinesize);

    for (i = 0; i < 16; i++) {
        h->block_offset[i]           = (4 * ((scan8[i] - scan8[0]) & 7) << pixel_shift) + 4 * h->linesize * ((scan8[i] - scan8[0]) >> 3);
        h->block_offset[48 + i]      = (4 * ((scan8[i] - scan8[0]) & 7) << pixel_shift) + 8 * h->linesize * ((scan8[i] - scan8[0]) >> 3);
    }
    for (i = 0; i < 16; i++) {
        h->block_offset[16 + i]      =
        h->block_offset[32 + i]      = (4 * ((scan8[i] - scan8[0]) & 7) << pixel_shift) + 4 * h->uvlinesize * ((scan8[i] - scan8[0]) >> 3);
        h->block_offset[48 + 16 + i] =
        h->block_offset[48 + 32 + i] = (4 * ((scan8[i] - scan8[0]) & 7) << pixel_shift) + 8 * h->uvlinesize * ((scan8[i] - scan8[0]) >> 3);
    }

    // s->decode = (h->flags & CODEC_FLAG_PSNR) || !s->encoding ||
    //             h->cur_pic.reference /* || h->contains_intra */ || 1;

    /* We mark the current picture as non-reference after allocating it, so
     * that if we break out due to an error it can be released automatically
     * in the next ff_MPV_frame_start().
     */
    h->cur_pic_ptr->reference = 0;

    h->cur_pic_ptr->field_poc[0] = h->cur_pic_ptr->field_poc[1] = INT_MAX;

    h->next_output_pic = NULL;

    assert(h->cur_pic_ptr->long_ref == 0);

    return 0;
}

static av_always_inline void backup_mb_border(H264Context *h, uint8_t *src_y,
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
        if (h->mb_y & 1) {
            if (!MB_MBAFF(h)) {
                top_border = h->top_borders[0][h->mb_x];
                AV_COPY128(top_border, src_y + 15 * linesize);
                if (pixel_shift)
                    AV_COPY128(top_border + 16, src_y + 15 * linesize + 16);
                if (simple || !CONFIG_GRAY || !(h->flags & CODEC_FLAG_GRAY)) {
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
        } else if (MB_MBAFF(h)) {
            top_idx = 0;
        } else
            return;
    }

    top_border = h->top_borders[top_idx][h->mb_x];
    /* There are two lines saved, the line above the top macroblock
     * of a pair, and the line above the bottom macroblock. */
    AV_COPY128(top_border, src_y + 16 * linesize);
    if (pixel_shift)
        AV_COPY128(top_border + 16, src_y + 16 * linesize + 16);

    if (simple || !CONFIG_GRAY || !(h->flags & CODEC_FLAG_GRAY)) {
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
static void implicit_weight_table(H264Context *h, int field)
{
    int ref0, ref1, i, cur_poc, ref_start, ref_count0, ref_count1;

    for (i = 0; i < 2; i++) {
        h->luma_weight_flag[i]   = 0;
        h->chroma_weight_flag[i] = 0;
    }

    if (field < 0) {
        if (h->picture_structure == PICT_FRAME) {
            cur_poc = h->cur_pic_ptr->poc;
        } else {
            cur_poc = h->cur_pic_ptr->field_poc[h->picture_structure - 1];
        }
        if (h->ref_count[0] == 1 && h->ref_count[1] == 1 && !FRAME_MBAFF(h) &&
            h->ref_list[0][0].poc + h->ref_list[1][0].poc == 2 * cur_poc) {
            h->use_weight        = 0;
            h->use_weight_chroma = 0;
            return;
        }
        ref_start  = 0;
        ref_count0 = h->ref_count[0];
        ref_count1 = h->ref_count[1];
    } else {
        cur_poc    = h->cur_pic_ptr->field_poc[field];
        ref_start  = 16;
        ref_count0 = 16 + 2 * h->ref_count[0];
        ref_count1 = 16 + 2 * h->ref_count[1];
    }

    h->use_weight               = 2;
    h->use_weight_chroma        = 2;
    h->luma_log2_weight_denom   = 5;
    h->chroma_log2_weight_denom = 5;

    for (ref0 = ref_start; ref0 < ref_count0; ref0++) {
        int poc0 = h->ref_list[0][ref0].poc;
        for (ref1 = ref_start; ref1 < ref_count1; ref1++) {
            int w = 32;
            if (!h->ref_list[0][ref0].long_ref && !h->ref_list[1][ref1].long_ref) {
                int poc1 = h->ref_list[1][ref1].poc;
                int td   = av_clip(poc1 - poc0, -128, 127);
                if (td) {
                    int tb = av_clip(cur_poc - poc0, -128, 127);
                    int tx = (16384 + (FFABS(td) >> 1)) / td;
                    int dist_scale_factor = (tb * tx + 32) >> 8;
                    if (dist_scale_factor >= -64 && dist_scale_factor <= 128)
                        w = 64 - dist_scale_factor;
                }
            }
            if (field < 0) {
                h->implicit_weight[ref0][ref1][0] =
                h->implicit_weight[ref0][ref1][1] = w;
            } else {
                h->implicit_weight[ref0][ref1][field] = w;
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
#define TRANSPOSE(x) (x >> 2) | ((x << 2) & 0xF)
        h->zigzag_scan[i] = TRANSPOSE(zigzag_scan[i]);
        h->field_scan[i]  = TRANSPOSE(field_scan[i]);
#undef TRANSPOSE
    }
    for (i = 0; i < 64; i++) {
#define TRANSPOSE(x) (x >> 3) | ((x & 7) << 3)
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

/**
 * Replicate H264 "master" context to thread contexts.
 */
static int clone_slice(H264Context *dst, H264Context *src)
{
    memcpy(dst->block_offset, src->block_offset, sizeof(dst->block_offset));
    dst->cur_pic_ptr = src->cur_pic_ptr;
    dst->cur_pic     = src->cur_pic;
    dst->linesize    = src->linesize;
    dst->uvlinesize  = src->uvlinesize;
    dst->first_field = src->first_field;

    dst->prev_poc_msb          = src->prev_poc_msb;
    dst->prev_poc_lsb          = src->prev_poc_lsb;
    dst->prev_frame_num_offset = src->prev_frame_num_offset;
    dst->prev_frame_num        = src->prev_frame_num;
    dst->short_ref_count       = src->short_ref_count;

    memcpy(dst->short_ref,        src->short_ref,        sizeof(dst->short_ref));
    memcpy(dst->long_ref,         src->long_ref,         sizeof(dst->long_ref));
    memcpy(dst->default_ref_list, src->default_ref_list, sizeof(dst->default_ref_list));

    memcpy(dst->dequant4_coeff,   src->dequant4_coeff,   sizeof(src->dequant4_coeff));
    memcpy(dst->dequant8_coeff,   src->dequant8_coeff,   sizeof(src->dequant8_coeff));

    return 0;
}

static enum AVPixelFormat get_pixel_format(H264Context *h, int force_callback)
{
    switch (h->sps.bit_depth_luma) {
    case 9:
        if (CHROMA444(h)) {
            if (h->avctx->colorspace == AVCOL_SPC_RGB) {
                return AV_PIX_FMT_GBRP9;
            } else
                return AV_PIX_FMT_YUV444P9;
        } else if (CHROMA422(h))
            return AV_PIX_FMT_YUV422P9;
        else
            return AV_PIX_FMT_YUV420P9;
        break;
    case 10:
        if (CHROMA444(h)) {
            if (h->avctx->colorspace == AVCOL_SPC_RGB) {
                return AV_PIX_FMT_GBRP10;
            } else
                return AV_PIX_FMT_YUV444P10;
        } else if (CHROMA422(h))
            return AV_PIX_FMT_YUV422P10;
        else
            return AV_PIX_FMT_YUV420P10;
        break;
    case 12:
        if (CHROMA444(h)) {
            if (h->avctx->colorspace == AVCOL_SPC_RGB) {
                return AV_PIX_FMT_GBRP12;
            } else
                return AV_PIX_FMT_YUV444P12;
        } else if (CHROMA422(h))
            return AV_PIX_FMT_YUV422P12;
        else
            return AV_PIX_FMT_YUV420P12;
        break;
    case 14:
        if (CHROMA444(h)) {
            if (h->avctx->colorspace == AVCOL_SPC_RGB) {
                return AV_PIX_FMT_GBRP14;
            } else
                return AV_PIX_FMT_YUV444P14;
        } else if (CHROMA422(h))
            return AV_PIX_FMT_YUV422P14;
        else
            return AV_PIX_FMT_YUV420P14;
        break;
    case 8:
        if (CHROMA444(h)) {
            if (h->avctx->colorspace == AVCOL_SPC_RGB) {
                av_log(h->avctx, AV_LOG_DEBUG, "Detected GBR colorspace.\n");
                return AV_PIX_FMT_GBR24P;
            } else if (h->avctx->colorspace == AVCOL_SPC_YCGCO) {
                av_log(h->avctx, AV_LOG_WARNING, "Detected unsupported YCgCo colorspace.\n");
            }
            return h->avctx->color_range == AVCOL_RANGE_JPEG ? AV_PIX_FMT_YUVJ444P
                                                                : AV_PIX_FMT_YUV444P;
        } else if (CHROMA422(h)) {
            return h->avctx->color_range == AVCOL_RANGE_JPEG ? AV_PIX_FMT_YUVJ422P
                                                             : AV_PIX_FMT_YUV422P;
        } else {
            int i;
            const enum AVPixelFormat * fmt = h->avctx->codec->pix_fmts ?
                                        h->avctx->codec->pix_fmts :
                                        h->avctx->color_range == AVCOL_RANGE_JPEG ?
                                        h264_hwaccel_pixfmt_list_jpeg_420 :
                                        h264_hwaccel_pixfmt_list_420;

            for (i=0; fmt[i] != AV_PIX_FMT_NONE; i++)
                if (fmt[i] == h->avctx->pix_fmt && !force_callback)
                    return fmt[i];
            return ff_thread_get_format(h->avctx, fmt);
        }
        break;
    default:
        av_log(h->avctx, AV_LOG_ERROR,
               "Unsupported bit depth %d\n", h->sps.bit_depth_luma);
        return AVERROR_INVALIDDATA;
    }
}

/* export coded and cropped frame dimensions to AVCodecContext */
static int init_dimensions(H264Context *h)
{
    int width  = h->width  - (h->sps.crop_right + h->sps.crop_left);
    int height = h->height - (h->sps.crop_top   + h->sps.crop_bottom);
    av_assert0(h->sps.crop_right + h->sps.crop_left < (unsigned)h->width);
    av_assert0(h->sps.crop_top + h->sps.crop_bottom < (unsigned)h->height);

    /* handle container cropping */
    if (!h->sps.crop &&
        FFALIGN(h->avctx->width,  16) == h->width &&
        FFALIGN(h->avctx->height, 16) == h->height) {
        width  = h->avctx->width;
        height = h->avctx->height;
    }

    if (width <= 0 || height <= 0) {
        av_log(h->avctx, AV_LOG_ERROR, "Invalid cropped dimensions: %dx%d.\n",
               width, height);
        if (h->avctx->err_recognition & AV_EF_EXPLODE)
            return AVERROR_INVALIDDATA;

        av_log(h->avctx, AV_LOG_WARNING, "Ignoring cropping information.\n");
        h->sps.crop_bottom = h->sps.crop_top = h->sps.crop_right = h->sps.crop_left = 0;
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

static int h264_slice_header_init(H264Context *h, int reinit)
{
    int nb_slices = (HAVE_THREADS &&
                     h->avctx->active_thread_type & FF_THREAD_SLICE) ?
                    h->avctx->thread_count : 1;
    int i, ret;

    h->avctx->sample_aspect_ratio = h->sps.sar;
    av_assert0(h->avctx->sample_aspect_ratio.den);
    av_pix_fmt_get_chroma_sub_sample(h->avctx->pix_fmt,
                                     &h->chroma_x_shift, &h->chroma_y_shift);

    if (h->sps.timing_info_present_flag) {
        int64_t den = h->sps.time_scale;
        if (h->x264_build < 44U)
            den *= 2;
        av_reduce(&h->avctx->time_base.num, &h->avctx->time_base.den,
                  h->sps.num_units_in_tick, den, 1 << 30);
    }

    h->avctx->hwaccel = ff_find_hwaccel(h->avctx);

    if (reinit)
        ff_h264_free_tables(h, 0);
    h->first_field           = 0;
    h->prev_interlaced_frame = 1;

    init_scan_tables(h);
    ret = ff_h264_alloc_tables(h);
    if (ret < 0) {
        av_log(h->avctx, AV_LOG_ERROR, "Could not allocate memory\n");
        return ret;
    }

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

    if (!HAVE_THREADS || !(h->avctx->active_thread_type & FF_THREAD_SLICE)) {
        ret = ff_h264_context_init(h);
        if (ret < 0) {
            av_log(h->avctx, AV_LOG_ERROR, "context_init() failed.\n");
            return ret;
        }
    } else {
        for (i = 1; i < h->slice_context_count; i++) {
            H264Context *c;
            c                    = h->thread_context[i] = av_mallocz(sizeof(H264Context));
            if (!c)
                return AVERROR(ENOMEM);
            c->avctx             = h->avctx;
            if (CONFIG_ERROR_RESILIENCE) {
                c->dsp               = h->dsp;
            }
            c->vdsp              = h->vdsp;
            c->h264dsp           = h->h264dsp;
            c->h264qpel          = h->h264qpel;
            c->h264chroma        = h->h264chroma;
            c->sps               = h->sps;
            c->pps               = h->pps;
            c->pixel_shift       = h->pixel_shift;
            c->cur_chroma_format_idc = h->cur_chroma_format_idc;
            c->width             = h->width;
            c->height            = h->height;
            c->linesize          = h->linesize;
            c->uvlinesize        = h->uvlinesize;
            c->chroma_x_shift    = h->chroma_x_shift;
            c->chroma_y_shift    = h->chroma_y_shift;
            c->qscale            = h->qscale;
            c->droppable         = h->droppable;
            c->data_partitioning = h->data_partitioning;
            c->low_delay         = h->low_delay;
            c->mb_width          = h->mb_width;
            c->mb_height         = h->mb_height;
            c->mb_stride         = h->mb_stride;
            c->mb_num            = h->mb_num;
            c->flags             = h->flags;
            c->workaround_bugs   = h->workaround_bugs;
            c->pict_type         = h->pict_type;

            init_scan_tables(c);
            clone_tables(c, h, i);
            c->context_initialized = 1;
        }

        for (i = 0; i < h->slice_context_count; i++)
            if ((ret = ff_h264_context_init(h->thread_context[i])) < 0) {
                av_log(h->avctx, AV_LOG_ERROR, "context_init() failed.\n");
                return ret;
            }
    }

    h->context_initialized = 1;

    return 0;
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
 * @param h0 h264 master context (differs from 'h' when doing sliced based
 *           parallel decoding)
 *
 * @return 0 if okay, <0 if an error occurred, 1 if decoding must not be multithreaded
 */
int ff_h264_decode_slice_header(H264Context *h, H264Context *h0)
{
    unsigned int first_mb_in_slice;
    unsigned int pps_id;
    int ret;
    unsigned int slice_type, tmp, i, j;
    int last_pic_structure, last_pic_droppable;
    int must_reinit;
    int needs_reinit = 0;
    int field_pic_flag, bottom_field_flag;

    h->qpel_put = h->h264qpel.put_h264_qpel_pixels_tab;
    h->qpel_avg = h->h264qpel.avg_h264_qpel_pixels_tab;

    first_mb_in_slice = get_ue_golomb_long(&h->gb);

    if (first_mb_in_slice == 0) { // FIXME better field boundary detection
        if (h0->current_slice && h->cur_pic_ptr && FIELD_PICTURE(h)) {
            ff_h264_field_end(h, 1);
        }

        h0->current_slice = 0;
        if (!h0->first_field) {
            if (h->cur_pic_ptr && !h->droppable) {
                ff_thread_report_progress(&h->cur_pic_ptr->tf, INT_MAX,
                                          h->picture_structure == PICT_BOTTOM_FIELD);
            }
            h->cur_pic_ptr = NULL;
        }
    }

    slice_type = get_ue_golomb_31(&h->gb);
    if (slice_type > 9) {
        av_log(h->avctx, AV_LOG_ERROR,
               "slice type %d too large at %d %d\n",
               slice_type, h->mb_x, h->mb_y);
        return AVERROR_INVALIDDATA;
    }
    if (slice_type > 4) {
        slice_type -= 5;
        h->slice_type_fixed = 1;
    } else
        h->slice_type_fixed = 0;

    slice_type = golomb_to_pict_type[slice_type];
    h->slice_type     = slice_type;
    h->slice_type_nos = slice_type & 3;

    if (h->nal_unit_type  == NAL_IDR_SLICE &&
        h->slice_type_nos != AV_PICTURE_TYPE_I) {
        av_log(h->avctx, AV_LOG_ERROR, "A non-intra slice in an IDR NAL unit.\n");
        return AVERROR_INVALIDDATA;
    }

    // to make a few old functions happy, it's wrong though
    h->pict_type = h->slice_type;

    pps_id = get_ue_golomb(&h->gb);
    if (pps_id >= MAX_PPS_COUNT) {
        av_log(h->avctx, AV_LOG_ERROR, "pps_id %u out of range\n", pps_id);
        return AVERROR_INVALIDDATA;
    }
    if (!h0->pps_buffers[pps_id]) {
        av_log(h->avctx, AV_LOG_ERROR,
               "non-existing PPS %u referenced\n",
               pps_id);
        return AVERROR_INVALIDDATA;
    }
    if (h0->au_pps_id >= 0 && pps_id != h0->au_pps_id) {
        av_log(h->avctx, AV_LOG_ERROR,
               "PPS change from %d to %d forbidden\n",
               h0->au_pps_id, pps_id);
        return AVERROR_INVALIDDATA;
    }
    h->pps = *h0->pps_buffers[pps_id];

    if (!h0->sps_buffers[h->pps.sps_id]) {
        av_log(h->avctx, AV_LOG_ERROR,
               "non-existing SPS %u referenced\n",
               h->pps.sps_id);
        return AVERROR_INVALIDDATA;
    }

    if (h->pps.sps_id != h->sps.sps_id ||
        h->pps.sps_id != h->current_sps_id ||
        h0->sps_buffers[h->pps.sps_id]->new) {

        h->sps = *h0->sps_buffers[h->pps.sps_id];

        if (h->mb_width  != h->sps.mb_width ||
            h->mb_height != h->sps.mb_height * (2 - h->sps.frame_mbs_only_flag) ||
            h->avctx->bits_per_raw_sample != h->sps.bit_depth_luma ||
            h->cur_chroma_format_idc != h->sps.chroma_format_idc
        )
            needs_reinit = 1;

        if (h->bit_depth_luma    != h->sps.bit_depth_luma ||
            h->chroma_format_idc != h->sps.chroma_format_idc) {
            h->bit_depth_luma    = h->sps.bit_depth_luma;
            h->chroma_format_idc = h->sps.chroma_format_idc;
            needs_reinit         = 1;
        }
        if ((ret = ff_h264_set_parameter_from_sps(h)) < 0)
            return ret;
    }

    h->avctx->profile = ff_h264_get_profile(&h->sps);
    h->avctx->level   = h->sps.level_idc;
    h->avctx->refs    = h->sps.ref_frame_count;

    must_reinit = (h->context_initialized &&
                    (   16*h->sps.mb_width != h->avctx->coded_width
                     || 16*h->sps.mb_height * (2 - h->sps.frame_mbs_only_flag) != h->avctx->coded_height
                     || h->avctx->bits_per_raw_sample != h->sps.bit_depth_luma
                     || h->cur_chroma_format_idc != h->sps.chroma_format_idc
                     || av_cmp_q(h->sps.sar, h->avctx->sample_aspect_ratio)
                     || h->mb_width  != h->sps.mb_width
                     || h->mb_height != h->sps.mb_height * (2 - h->sps.frame_mbs_only_flag)
                    ));
    if (non_j_pixfmt(h0->avctx->pix_fmt) != non_j_pixfmt(get_pixel_format(h0, 0)))
        must_reinit = 1;

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

    if (h->context_initialized &&
        (h->width  != h->avctx->coded_width   ||
         h->height != h->avctx->coded_height  ||
         must_reinit ||
         needs_reinit)) {
        if (h != h0) {
            av_log(h->avctx, AV_LOG_ERROR,
                   "changing width %d -> %d / height %d -> %d on "
                   "slice %d\n",
                   h->width, h->avctx->coded_width,
                   h->height, h->avctx->coded_height,
                   h0->current_slice + 1);
            return AVERROR_INVALIDDATA;
        }

        ff_h264_flush_change(h);

        if ((ret = get_pixel_format(h, 1)) < 0)
            return ret;
        h->avctx->pix_fmt = ret;

        av_log(h->avctx, AV_LOG_INFO, "Reinit context to %dx%d, "
               "pix_fmt: %s\n", h->width, h->height, av_get_pix_fmt_name(h->avctx->pix_fmt));

        if ((ret = h264_slice_header_init(h, 1)) < 0) {
            av_log(h->avctx, AV_LOG_ERROR,
                   "h264_slice_header_init() failed\n");
            return ret;
        }
    }
    if (!h->context_initialized) {
        if (h != h0) {
            av_log(h->avctx, AV_LOG_ERROR,
                   "Cannot (re-)initialize context during parallel decoding.\n");
            return AVERROR_PATCHWELCOME;
        }

        if ((ret = get_pixel_format(h, 1)) < 0)
            return ret;
        h->avctx->pix_fmt = ret;

        if ((ret = h264_slice_header_init(h, 0)) < 0) {
            av_log(h->avctx, AV_LOG_ERROR,
                   "h264_slice_header_init() failed\n");
            return ret;
        }
    }

    if (h == h0 && h->dequant_coeff_pps != pps_id) {
        h->dequant_coeff_pps = pps_id;
        h264_init_dequant_tables(h);
    }

    h->frame_num = get_bits(&h->gb, h->sps.log2_max_frame_num);

    h->mb_mbaff        = 0;
    h->mb_aff_frame    = 0;
    last_pic_structure = h0->picture_structure;
    last_pic_droppable = h0->droppable;
    h->droppable       = h->nal_ref_idc == 0;
    if (h->sps.frame_mbs_only_flag) {
        h->picture_structure = PICT_FRAME;
    } else {
        if (!h->sps.direct_8x8_inference_flag && slice_type == AV_PICTURE_TYPE_B) {
            av_log(h->avctx, AV_LOG_ERROR, "This stream was generated by a broken encoder, invalid 8x8 inference\n");
            return -1;
        }
        field_pic_flag = get_bits1(&h->gb);
        if (field_pic_flag) {
            bottom_field_flag = get_bits1(&h->gb);
            h->picture_structure = PICT_TOP_FIELD + bottom_field_flag;
        } else {
            h->picture_structure = PICT_FRAME;
            h->mb_aff_frame      = h->sps.mb_aff;
        }
    }
    h->mb_field_decoding_flag = h->picture_structure != PICT_FRAME;

    if (h0->current_slice != 0) {
        if (last_pic_structure != h->picture_structure ||
            last_pic_droppable != h->droppable) {
            av_log(h->avctx, AV_LOG_ERROR,
                   "Changing field mode (%d -> %d) between slices is not allowed\n",
                   last_pic_structure, h->picture_structure);
            h->picture_structure = last_pic_structure;
            h->droppable         = last_pic_droppable;
            return AVERROR_INVALIDDATA;
        } else if (!h0->cur_pic_ptr) {
            av_log(h->avctx, AV_LOG_ERROR,
                   "unset cur_pic_ptr on slice %d\n",
                   h0->current_slice + 1);
            return AVERROR_INVALIDDATA;
        }
    } else {
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
        if (h0->first_field) {
            assert(h0->cur_pic_ptr);
            assert(h0->cur_pic_ptr->f.buf[0]);
            assert(h0->cur_pic_ptr->reference != DELAYED_PIC_REF);

            /* Mark old field/frame as completed */
            if (h0->cur_pic_ptr->tf.owner == h0->avctx) {
                ff_thread_report_progress(&h0->cur_pic_ptr->tf, INT_MAX,
                                          last_pic_structure == PICT_BOTTOM_FIELD);
            }

            /* figure out if we have a complementary field pair */
            if (!FIELD_PICTURE(h) || h->picture_structure == last_pic_structure) {
                /* Previous field is unmatched. Don't display it, but let it
                 * remain for reference if marked as such. */
                if (last_pic_structure != PICT_FRAME) {
                    ff_thread_report_progress(&h0->cur_pic_ptr->tf, INT_MAX,
                                              last_pic_structure == PICT_TOP_FIELD);
                }
            } else {
                if (h0->cur_pic_ptr->frame_num != h->frame_num) {
                    /* This and previous field were reference, but had
                     * different frame_nums. Consider this field first in
                     * pair. Throw away previous field except for reference
                     * purposes. */
                    if (last_pic_structure != PICT_FRAME) {
                        ff_thread_report_progress(&h0->cur_pic_ptr->tf, INT_MAX,
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

        while (h->frame_num != h->prev_frame_num && !h0->first_field &&
               h->frame_num != (h->prev_frame_num + 1) % (1 << h->sps.log2_max_frame_num)) {
            H264Picture *prev = h->short_ref_count ? h->short_ref[0] : NULL;
            av_log(h->avctx, AV_LOG_DEBUG, "Frame num gap %d %d\n",
                   h->frame_num, h->prev_frame_num);
            if (!h->sps.gaps_in_frame_num_allowed_flag)
                for(i=0; i<FF_ARRAY_ELEMS(h->last_pocs); i++)
                    h->last_pocs[i] = INT_MIN;
            ret = h264_frame_start(h);
            if (ret < 0) {
                h0->first_field = 0;
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
                if (prev) {
                    av_image_copy(h->short_ref[0]->f.data,
                                  h->short_ref[0]->f.linesize,
                                  (const uint8_t **)prev->f.data,
                                  prev->f.linesize,
                                  h->avctx->pix_fmt,
                                  h->mb_width  * 16,
                                  h->mb_height * 16);
                    h->short_ref[0]->poc = prev->poc + 2;
                }
                h->short_ref[0]->frame_num = h->prev_frame_num;
            }
        }

        /* See if we have a decoded first field looking for a pair...
         * We're using that to see whether to continue decoding in that
         * frame, or to allocate a new one. */
        if (h0->first_field) {
            assert(h0->cur_pic_ptr);
            assert(h0->cur_pic_ptr->f.buf[0]);
            assert(h0->cur_pic_ptr->reference != DELAYED_PIC_REF);

            /* figure out if we have a complementary field pair */
            if (!FIELD_PICTURE(h) || h->picture_structure == last_pic_structure) {
                /* Previous field is unmatched. Don't display it, but let it
                 * remain for reference if marked as such. */
                h0->cur_pic_ptr = NULL;
                h0->first_field = FIELD_PICTURE(h);
            } else {
                if (h0->cur_pic_ptr->frame_num != h->frame_num) {
                    ff_thread_report_progress(&h0->cur_pic_ptr->tf, INT_MAX,
                                              h0->picture_structure==PICT_BOTTOM_FIELD);
                    /* This and the previous field had different frame_nums.
                     * Consider this field first in pair. Throw away previous
                     * one except for reference purposes. */
                    h0->first_field = 1;
                    h0->cur_pic_ptr = NULL;
                } else {
                    /* Second field in complementary pair */
                    h0->first_field = 0;
                }
            }
        } else {
            /* Frame or first field in a potentially complementary pair */
            h0->first_field = FIELD_PICTURE(h);
        }

        if (!FIELD_PICTURE(h) || h0->first_field) {
            if (h264_frame_start(h) < 0) {
                h0->first_field = 0;
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
        h0->last_slice_type = -1;
    }
    if (h != h0 && (ret = clone_slice(h, h0)) < 0)
        return ret;

    /* can't be in alloc_tables because linesize isn't known there.
     * FIXME: redo bipred weight to not require extra buffer? */
    for (i = 0; i < h->slice_context_count; i++)
        if (h->thread_context[i]) {
            ret = alloc_scratch_buffers(h->thread_context[i], h->linesize);
            if (ret < 0)
                return ret;
        }

    h->cur_pic_ptr->frame_num = h->frame_num; // FIXME frame_num cleanup

    av_assert1(h->mb_num == h->mb_width * h->mb_height);
    if (first_mb_in_slice << FIELD_OR_MBAFF_PICTURE(h) >= h->mb_num ||
        first_mb_in_slice >= h->mb_num) {
        av_log(h->avctx, AV_LOG_ERROR, "first_mb_in_slice overflow\n");
        return AVERROR_INVALIDDATA;
    }
    h->resync_mb_x = h->mb_x =  first_mb_in_slice % h->mb_width;
    h->resync_mb_y = h->mb_y = (first_mb_in_slice / h->mb_width) <<
                               FIELD_OR_MBAFF_PICTURE(h);
    if (h->picture_structure == PICT_BOTTOM_FIELD)
        h->resync_mb_y = h->mb_y = h->mb_y + 1;
    av_assert1(h->mb_y < h->mb_height);

    if (h->picture_structure == PICT_FRAME) {
        h->curr_pic_num = h->frame_num;
        h->max_pic_num  = 1 << h->sps.log2_max_frame_num;
    } else {
        h->curr_pic_num = 2 * h->frame_num + 1;
        h->max_pic_num  = 1 << (h->sps.log2_max_frame_num + 1);
    }

    if (h->nal_unit_type == NAL_IDR_SLICE)
        get_ue_golomb(&h->gb); /* idr_pic_id */

    if (h->sps.poc_type == 0) {
        h->poc_lsb = get_bits(&h->gb, h->sps.log2_max_poc_lsb);

        if (h->pps.pic_order_present == 1 && h->picture_structure == PICT_FRAME)
            h->delta_poc_bottom = get_se_golomb(&h->gb);
    }

    if (h->sps.poc_type == 1 && !h->sps.delta_pic_order_always_zero_flag) {
        h->delta_poc[0] = get_se_golomb(&h->gb);

        if (h->pps.pic_order_present == 1 && h->picture_structure == PICT_FRAME)
            h->delta_poc[1] = get_se_golomb(&h->gb);
    }

    ff_init_poc(h, h->cur_pic_ptr->field_poc, &h->cur_pic_ptr->poc);

    if (h->pps.redundant_pic_cnt_present)
        h->redundant_pic_count = get_ue_golomb(&h->gb);

    ret = ff_set_ref_count(h);
    if (ret < 0)
        return ret;

    if (slice_type != AV_PICTURE_TYPE_I &&
        (h0->current_slice == 0 ||
         slice_type != h0->last_slice_type ||
         memcmp(h0->last_ref_count, h0->ref_count, sizeof(h0->ref_count)))) {

        ff_h264_fill_default_ref_list(h);
    }

    if (h->slice_type_nos != AV_PICTURE_TYPE_I) {
       ret = ff_h264_decode_ref_pic_list_reordering(h);
       if (ret < 0) {
           h->ref_count[1] = h->ref_count[0] = 0;
           return ret;
       }
    }

    if ((h->pps.weighted_pred && h->slice_type_nos == AV_PICTURE_TYPE_P) ||
        (h->pps.weighted_bipred_idc == 1 &&
         h->slice_type_nos == AV_PICTURE_TYPE_B))
        ff_pred_weight_table(h);
    else if (h->pps.weighted_bipred_idc == 2 &&
             h->slice_type_nos == AV_PICTURE_TYPE_B) {
        implicit_weight_table(h, -1);
    } else {
        h->use_weight = 0;
        for (i = 0; i < 2; i++) {
            h->luma_weight_flag[i]   = 0;
            h->chroma_weight_flag[i] = 0;
        }
    }

    // If frame-mt is enabled, only update mmco tables for the first slice
    // in a field. Subsequent slices can temporarily clobber h->mmco_index
    // or h->mmco, which will cause ref list mix-ups and decoding errors
    // further down the line. This may break decoding if the first slice is
    // corrupt, thus we only do this if frame-mt is enabled.
    if (h->nal_ref_idc) {
        ret = ff_h264_decode_ref_pic_marking(h0, &h->gb,
                                             !(h->avctx->active_thread_type & FF_THREAD_FRAME) ||
                                             h0->current_slice == 0);
        if (ret < 0 && (h->avctx->err_recognition & AV_EF_EXPLODE))
            return AVERROR_INVALIDDATA;
    }

    if (FRAME_MBAFF(h)) {
        ff_h264_fill_mbaff_ref_list(h);

        if (h->pps.weighted_bipred_idc == 2 && h->slice_type_nos == AV_PICTURE_TYPE_B) {
            implicit_weight_table(h, 0);
            implicit_weight_table(h, 1);
        }
    }

    if (h->slice_type_nos == AV_PICTURE_TYPE_B && !h->direct_spatial_mv_pred)
        ff_h264_direct_dist_scale_factor(h);
    ff_h264_direct_ref_list_init(h);

    if (h->slice_type_nos != AV_PICTURE_TYPE_I && h->pps.cabac) {
        tmp = get_ue_golomb_31(&h->gb);
        if (tmp > 2) {
            av_log(h->avctx, AV_LOG_ERROR, "cabac_init_idc %u overflow\n", tmp);
            return AVERROR_INVALIDDATA;
        }
        h->cabac_init_idc = tmp;
    }

    h->last_qscale_diff = 0;
    tmp = h->pps.init_qp + get_se_golomb(&h->gb);
    if (tmp > 51 + 6 * (h->sps.bit_depth_luma - 8)) {
        av_log(h->avctx, AV_LOG_ERROR, "QP %u out of range\n", tmp);
        return AVERROR_INVALIDDATA;
    }
    h->qscale       = tmp;
    h->chroma_qp[0] = get_chroma_qp(h, 0, h->qscale);
    h->chroma_qp[1] = get_chroma_qp(h, 1, h->qscale);
    // FIXME qscale / qp ... stuff
    if (h->slice_type == AV_PICTURE_TYPE_SP)
        get_bits1(&h->gb); /* sp_for_switch_flag */
    if (h->slice_type == AV_PICTURE_TYPE_SP ||
        h->slice_type == AV_PICTURE_TYPE_SI)
        get_se_golomb(&h->gb); /* slice_qs_delta */

    h->deblocking_filter     = 1;
    h->slice_alpha_c0_offset = 0;
    h->slice_beta_offset     = 0;
    if (h->pps.deblocking_filter_parameters_present) {
        tmp = get_ue_golomb_31(&h->gb);
        if (tmp > 2) {
            av_log(h->avctx, AV_LOG_ERROR,
                   "deblocking_filter_idc %u out of range\n", tmp);
            return AVERROR_INVALIDDATA;
        }
        h->deblocking_filter = tmp;
        if (h->deblocking_filter < 2)
            h->deblocking_filter ^= 1;  // 1<->0

        if (h->deblocking_filter) {
            h->slice_alpha_c0_offset = get_se_golomb(&h->gb) * 2;
            h->slice_beta_offset     = get_se_golomb(&h->gb) * 2;
            if (h->slice_alpha_c0_offset >  12 ||
                h->slice_alpha_c0_offset < -12 ||
                h->slice_beta_offset >  12     ||
                h->slice_beta_offset < -12) {
                av_log(h->avctx, AV_LOG_ERROR,
                       "deblocking filter parameters %d %d out of range\n",
                       h->slice_alpha_c0_offset, h->slice_beta_offset);
                return AVERROR_INVALIDDATA;
            }
        }
    }

    if (h->avctx->skip_loop_filter >= AVDISCARD_ALL ||
        (h->avctx->skip_loop_filter >= AVDISCARD_NONKEY &&
         h->slice_type_nos != AV_PICTURE_TYPE_I) ||
        (h->avctx->skip_loop_filter >= AVDISCARD_BIDIR  &&
         h->slice_type_nos == AV_PICTURE_TYPE_B) ||
        (h->avctx->skip_loop_filter >= AVDISCARD_NONREF &&
         h->nal_ref_idc == 0))
        h->deblocking_filter = 0;

    if (h->deblocking_filter == 1 && h0->max_contexts > 1) {
        if (h->avctx->flags2 & CODEC_FLAG2_FAST) {
            /* Cheat slightly for speed:
             * Do not bother to deblock across slices. */
            h->deblocking_filter = 2;
        } else {
            h0->max_contexts = 1;
            if (!h0->single_decode_warning) {
                av_log(h->avctx, AV_LOG_INFO,
                       "Cannot parallelize deblocking type 1, decoding such frames in sequential order\n");
                h0->single_decode_warning = 1;
            }
            if (h != h0) {
                av_log(h->avctx, AV_LOG_ERROR,
                       "Deblocking switched inside frame.\n");
                return 1;
            }
        }
    }
    h->qp_thresh = 15 -
                   FFMIN(h->slice_alpha_c0_offset, h->slice_beta_offset) -
                   FFMAX3(0,
                          h->pps.chroma_qp_index_offset[0],
                          h->pps.chroma_qp_index_offset[1]) +
                   6 * (h->sps.bit_depth_luma - 8);

    h0->last_slice_type = slice_type;
    memcpy(h0->last_ref_count, h0->ref_count, sizeof(h0->last_ref_count));
    h->slice_num        = ++h0->current_slice;

    if (h->slice_num)
        h0->slice_row[(h->slice_num-1)&(MAX_SLICES-1)]= h->resync_mb_y;
    if (   h0->slice_row[h->slice_num&(MAX_SLICES-1)] + 3 >= h->resync_mb_y
        && h0->slice_row[h->slice_num&(MAX_SLICES-1)] <= h->resync_mb_y
        && h->slice_num >= MAX_SLICES) {
        //in case of ASO this check needs to be updated depending on how we decide to assign slice numbers in this case
        av_log(h->avctx, AV_LOG_WARNING, "Possibly too many slices (%d >= %d), increase MAX_SLICES and recompile if there are artifacts\n", h->slice_num, MAX_SLICES);
    }

    for (j = 0; j < 2; j++) {
        int id_list[16];
        int *ref2frm = h->ref2frm[h->slice_num & (MAX_SLICES - 1)][j];
        for (i = 0; i < 16; i++) {
            id_list[i] = 60;
            if (j < h->list_count && i < h->ref_count[j] &&
                h->ref_list[j][i].f.buf[0]) {
                int k;
                AVBuffer *buf = h->ref_list[j][i].f.buf[0]->buffer;
                for (k = 0; k < h->short_ref_count; k++)
                    if (h->short_ref[k]->f.buf[0]->buffer == buf) {
                        id_list[i] = k;
                        break;
                    }
                for (k = 0; k < h->long_ref_count; k++)
                    if (h->long_ref[k] && h->long_ref[k]->f.buf[0]->buffer == buf) {
                        id_list[i] = h->short_ref_count + k;
                        break;
                    }
            }
        }

        ref2frm[0] =
        ref2frm[1] = -1;
        for (i = 0; i < 16; i++)
            ref2frm[i + 2] = 4 * id_list[i] + (h->ref_list[j][i].reference & 3);
        ref2frm[18 + 0] =
        ref2frm[18 + 1] = -1;
        for (i = 16; i < 48; i++)
            ref2frm[i + 4] = 4 * id_list[(i - 16) >> 1] +
                             (h->ref_list[j][i].reference & 3);
    }

    if (h->ref_count[0]) ff_h264_set_erpic(&h->er.last_pic, &h->ref_list[0][0]);
    if (h->ref_count[1]) ff_h264_set_erpic(&h->er.next_pic, &h->ref_list[1][0]);

    h->er.ref_count = h->ref_count[0];
    h0->au_pps_id = pps_id;
    h->sps.new =
    h0->sps_buffers[h->pps.sps_id]->new = 0;
    h->current_sps_id = h->pps.sps_id;

    if (h->avctx->debug & FF_DEBUG_PICT_INFO) {
        av_log(h->avctx, AV_LOG_DEBUG,
               "slice:%d %s mb:%d %c%s%s pps:%u frame:%d poc:%d/%d ref:%d/%d qp:%d loop:%d:%d:%d weight:%d%s %s\n",
               h->slice_num,
               (h->picture_structure == PICT_FRAME ? "F" : h->picture_structure == PICT_TOP_FIELD ? "T" : "B"),
               first_mb_in_slice,
               av_get_picture_type_char(h->slice_type),
               h->slice_type_fixed ? " fix" : "",
               h->nal_unit_type == NAL_IDR_SLICE ? " IDR" : "",
               pps_id, h->frame_num,
               h->cur_pic_ptr->field_poc[0],
               h->cur_pic_ptr->field_poc[1],
               h->ref_count[0], h->ref_count[1],
               h->qscale,
               h->deblocking_filter,
               h->slice_alpha_c0_offset, h->slice_beta_offset,
               h->use_weight,
               h->use_weight == 1 && h->use_weight_chroma ? "c" : "",
               h->slice_type == AV_PICTURE_TYPE_B ? (h->direct_spatial_mv_pred ? "SPAT" : "TEMP") : "");
    }

    return 0;
}

int ff_h264_get_slice_type(const H264Context *h)
{
    switch (h->slice_type) {
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

static av_always_inline void fill_filter_caches_inter(H264Context *h,
                                                      int mb_type, int top_xy,
                                                      int left_xy[LEFT_MBS],
                                                      int top_type,
                                                      int left_type[LEFT_MBS],
                                                      int mb_xy, int list)
{
    int b_stride = h->b_stride;
    int16_t(*mv_dst)[2] = &h->mv_cache[list][scan8[0]];
    int8_t *ref_cache = &h->ref_cache[list][scan8[0]];
    if (IS_INTER(mb_type) || IS_DIRECT(mb_type)) {
        if (USES_LIST(top_type, list)) {
            const int b_xy  = h->mb2b_xy[top_xy] + 3 * b_stride;
            const int b8_xy = 4 * top_xy + 2;
            int (*ref2frm)[64] = (void*)(h->ref2frm[h->slice_table[top_xy] & (MAX_SLICES - 1)][0] + (MB_MBAFF(h) ? 20 : 2));
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
                int (*ref2frm)[64] =(void*)( h->ref2frm[h->slice_table[left_xy[LTOP]] & (MAX_SLICES - 1)][0] + (MB_MBAFF(h) ? 20 : 2));
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
        int (*ref2frm)[64] = (void*)(h->ref2frm[h->slice_num & (MAX_SLICES - 1)][0] + (MB_MBAFF(h) ? 20 : 2));
        uint32_t ref01 = (pack16to32(ref2frm[list][ref[0]], ref2frm[list][ref[1]]) & 0x00FF00FF) * 0x0101;
        uint32_t ref23 = (pack16to32(ref2frm[list][ref[2]], ref2frm[list][ref[3]]) & 0x00FF00FF) * 0x0101;
        AV_WN32A(&ref_cache[0 * 8], ref01);
        AV_WN32A(&ref_cache[1 * 8], ref01);
        AV_WN32A(&ref_cache[2 * 8], ref23);
        AV_WN32A(&ref_cache[3 * 8], ref23);
    }

    {
        int16_t(*mv_src)[2] = &h->cur_pic.motion_val[list][4 * h->mb_x + 4 * h->mb_y * b_stride];
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
static int fill_filter_caches(H264Context *h, int mb_type)
{
    const int mb_xy = h->mb_xy;
    int top_xy, left_xy[LEFT_MBS];
    int top_type, left_type[LEFT_MBS];
    uint8_t *nnz;
    uint8_t *nnz_cache;

    top_xy = mb_xy - (h->mb_stride << MB_FIELD(h));

    /* Wow, what a mess, why didn't they simplify the interlacing & intra
     * stuff, I can't imagine that these complex rules are worth it. */

    left_xy[LBOT] = left_xy[LTOP] = mb_xy - 1;
    if (FRAME_MBAFF(h)) {
        const int left_mb_field_flag = IS_INTERLACED(h->cur_pic.mb_type[mb_xy - 1]);
        const int curr_mb_field_flag = IS_INTERLACED(mb_type);
        if (h->mb_y & 1) {
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

    h->top_mb_xy        = top_xy;
    h->left_mb_xy[LTOP] = left_xy[LTOP];
    h->left_mb_xy[LBOT] = left_xy[LBOT];
    {
        /* For sufficiently low qp, filtering wouldn't do anything.
         * This is a conservative estimate: could also check beta_offset
         * and more accurate chroma_qp. */
        int qp_thresh = h->qp_thresh; // FIXME strictly we should store qp_thresh for each mb of a slice
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
    if (h->deblocking_filter == 2) {
        if (h->slice_table[top_xy] != h->slice_num)
            top_type = 0;
        if (h->slice_table[left_xy[LBOT]] != h->slice_num)
            left_type[LTOP] = left_type[LBOT] = 0;
    } else {
        if (h->slice_table[top_xy] == 0xFFFF)
            top_type = 0;
        if (h->slice_table[left_xy[LBOT]] == 0xFFFF)
            left_type[LTOP] = left_type[LBOT] = 0;
    }
    h->top_type        = top_type;
    h->left_type[LTOP] = left_type[LTOP];
    h->left_type[LBOT] = left_type[LBOT];

    if (IS_INTRA(mb_type))
        return 0;

    fill_filter_caches_inter(h, mb_type, top_xy, left_xy,
                             top_type, left_type, mb_xy, 0);
    if (h->list_count == 2)
        fill_filter_caches_inter(h, mb_type, top_xy, left_xy,
                                 top_type, left_type, mb_xy, 1);

    nnz       = h->non_zero_count[mb_xy];
    nnz_cache = h->non_zero_count_cache;
    AV_COPY32(&nnz_cache[4 + 8 * 1], &nnz[0]);
    AV_COPY32(&nnz_cache[4 + 8 * 2], &nnz[4]);
    AV_COPY32(&nnz_cache[4 + 8 * 3], &nnz[8]);
    AV_COPY32(&nnz_cache[4 + 8 * 4], &nnz[12]);
    h->cbp = h->cbp_table[mb_xy];

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
            nnz_cache[scan8[3]] = (h->cbp & 0x1000) >> 12;

            nnz_cache[scan8[0 + 4]] =
            nnz_cache[scan8[1 + 4]] =
            nnz_cache[scan8[2 + 4]] =
            nnz_cache[scan8[3 + 4]] = (h->cbp & 0x2000) >> 12;

            nnz_cache[scan8[0 + 8]] =
            nnz_cache[scan8[1 + 8]] =
            nnz_cache[scan8[2 + 8]] =
            nnz_cache[scan8[3 + 8]] = (h->cbp & 0x4000) >> 12;

            nnz_cache[scan8[0 + 12]] =
            nnz_cache[scan8[1 + 12]] =
            nnz_cache[scan8[2 + 12]] =
            nnz_cache[scan8[3 + 12]] = (h->cbp & 0x8000) >> 12;
        }
    }

    return 0;
}

static void loop_filter(H264Context *h, int start_x, int end_x)
{
    uint8_t *dest_y, *dest_cb, *dest_cr;
    int linesize, uvlinesize, mb_x, mb_y;
    const int end_mb_y       = h->mb_y + FRAME_MBAFF(h);
    const int old_slice_type = h->slice_type;
    const int pixel_shift    = h->pixel_shift;
    const int block_h        = 16 >> h->chroma_y_shift;

    if (h->deblocking_filter) {
        for (mb_x = start_x; mb_x < end_x; mb_x++)
            for (mb_y = end_mb_y - FRAME_MBAFF(h); mb_y <= end_mb_y; mb_y++) {
                int mb_xy, mb_type;
                mb_xy         = h->mb_xy = mb_x + mb_y * h->mb_stride;
                h->slice_num  = h->slice_table[mb_xy];
                mb_type       = h->cur_pic.mb_type[mb_xy];
                h->list_count = h->list_counts[mb_xy];

                if (FRAME_MBAFF(h))
                    h->mb_mbaff               =
                    h->mb_field_decoding_flag = !!IS_INTERLACED(mb_type);

                h->mb_x = mb_x;
                h->mb_y = mb_y;
                dest_y  = h->cur_pic.f.data[0] +
                          ((mb_x << pixel_shift) + mb_y * h->linesize) * 16;
                dest_cb = h->cur_pic.f.data[1] +
                          (mb_x << pixel_shift) * (8 << CHROMA444(h)) +
                          mb_y * h->uvlinesize * block_h;
                dest_cr = h->cur_pic.f.data[2] +
                          (mb_x << pixel_shift) * (8 << CHROMA444(h)) +
                          mb_y * h->uvlinesize * block_h;
                // FIXME simplify above

                if (MB_FIELD(h)) {
                    linesize   = h->mb_linesize   = h->linesize   * 2;
                    uvlinesize = h->mb_uvlinesize = h->uvlinesize * 2;
                    if (mb_y & 1) { // FIXME move out of this function?
                        dest_y  -= h->linesize   * 15;
                        dest_cb -= h->uvlinesize * (block_h - 1);
                        dest_cr -= h->uvlinesize * (block_h - 1);
                    }
                } else {
                    linesize   = h->mb_linesize   = h->linesize;
                    uvlinesize = h->mb_uvlinesize = h->uvlinesize;
                }
                backup_mb_border(h, dest_y, dest_cb, dest_cr, linesize,
                                 uvlinesize, 0);
                if (fill_filter_caches(h, mb_type))
                    continue;
                h->chroma_qp[0] = get_chroma_qp(h, 0, h->cur_pic.qscale_table[mb_xy]);
                h->chroma_qp[1] = get_chroma_qp(h, 1, h->cur_pic.qscale_table[mb_xy]);

                if (FRAME_MBAFF(h)) {
                    ff_h264_filter_mb(h, mb_x, mb_y, dest_y, dest_cb, dest_cr,
                                      linesize, uvlinesize);
                } else {
                    ff_h264_filter_mb_fast(h, mb_x, mb_y, dest_y, dest_cb,
                                           dest_cr, linesize, uvlinesize);
                }
            }
    }
    h->slice_type   = old_slice_type;
    h->mb_x         = end_x;
    h->mb_y         = end_mb_y - FRAME_MBAFF(h);
    h->chroma_qp[0] = get_chroma_qp(h, 0, h->qscale);
    h->chroma_qp[1] = get_chroma_qp(h, 1, h->qscale);
}

static void predict_field_decoding_flag(H264Context *h)
{
    const int mb_xy = h->mb_x + h->mb_y * h->mb_stride;
    int mb_type     = (h->slice_table[mb_xy - 1] == h->slice_num) ?
                      h->cur_pic.mb_type[mb_xy - 1] :
                      (h->slice_table[mb_xy - h->mb_stride] == h->slice_num) ?
                      h->cur_pic.mb_type[mb_xy - h->mb_stride] : 0;
    h->mb_mbaff     = h->mb_field_decoding_flag = IS_INTERLACED(mb_type) ? 1 : 0;
}

/**
 * Draw edges and report progress for the last MB row.
 */
static void decode_finish_row(H264Context *h)
{
    int top            = 16 * (h->mb_y      >> FIELD_PICTURE(h));
    int pic_height     = 16 *  h->mb_height >> FIELD_PICTURE(h);
    int height         =  16      << FRAME_MBAFF(h);
    int deblock_border = (16 + 4) << FRAME_MBAFF(h);

    if (h->deblocking_filter) {
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

    ff_h264_draw_horiz_band(h, top, height);

    if (h->droppable || h->er.error_occurred)
        return;

    ff_thread_report_progress(&h->cur_pic_ptr->tf, top + height - 1,
                              h->picture_structure == PICT_BOTTOM_FIELD);
}

static void er_add_slice(H264Context *h, int startx, int starty,
                         int endx, int endy, int status)
{
    if (CONFIG_ERROR_RESILIENCE) {
        ERContext *er = &h->er;

        ff_er_add_slice(er, startx, starty, endx, endy, status);
    }
}

static int decode_slice(struct AVCodecContext *avctx, void *arg)
{
    H264Context *h = *(void **)arg;
    int lf_x_start = h->mb_x;

    h->mb_skip_run = -1;

    av_assert0(h->block_offset[15] == (4 * ((scan8[15] - scan8[0]) & 7) << h->pixel_shift) + 4 * h->linesize * ((scan8[15] - scan8[0]) >> 3));

    h->is_complex = FRAME_MBAFF(h) || h->picture_structure != PICT_FRAME ||
                    avctx->codec_id != AV_CODEC_ID_H264 ||
                    (CONFIG_GRAY && (h->flags & CODEC_FLAG_GRAY));

    if (!(h->avctx->active_thread_type & FF_THREAD_SLICE) && h->picture_structure == PICT_FRAME && h->er.error_status_table) {
        const int start_i  = av_clip(h->resync_mb_x + h->resync_mb_y * h->mb_width, 0, h->mb_num - 1);
        if (start_i) {
            int prev_status = h->er.error_status_table[h->er.mb_index2xy[start_i - 1]];
            prev_status &= ~ VP_START;
            if (prev_status != (ER_MV_END | ER_DC_END | ER_AC_END))
                h->er.error_occurred = 1;
        }
    }

    if (h->pps.cabac) {
        /* realign */
        align_get_bits(&h->gb);

        /* init cabac */
        ff_init_cabac_decoder(&h->cabac,
                              h->gb.buffer + get_bits_count(&h->gb) / 8,
                              (get_bits_left(&h->gb) + 7) / 8);

        ff_h264_init_cabac_states(h);

        for (;;) {
            // START_TIMER
            int ret = ff_h264_decode_mb_cabac(h);
            int eos;
            // STOP_TIMER("decode_mb_cabac")

            if (ret >= 0)
                ff_h264_hl_decode_mb(h);

            // FIXME optimal? or let mb_decode decode 16x32 ?
            if (ret >= 0 && FRAME_MBAFF(h)) {
                h->mb_y++;

                ret = ff_h264_decode_mb_cabac(h);

                if (ret >= 0)
                    ff_h264_hl_decode_mb(h);
                h->mb_y--;
            }
            eos = get_cabac_terminate(&h->cabac);

            if ((h->workaround_bugs & FF_BUG_TRUNCATED) &&
                h->cabac.bytestream > h->cabac.bytestream_end + 2) {
                er_add_slice(h, h->resync_mb_x, h->resync_mb_y, h->mb_x - 1,
                             h->mb_y, ER_MB_END);
                if (h->mb_x >= lf_x_start)
                    loop_filter(h, lf_x_start, h->mb_x + 1);
                return 0;
            }
            if (h->cabac.bytestream > h->cabac.bytestream_end + 2 )
                av_log(h->avctx, AV_LOG_DEBUG, "bytestream overread %td\n", h->cabac.bytestream_end - h->cabac.bytestream);
            if (ret < 0 || h->cabac.bytestream > h->cabac.bytestream_end + 4) {
                av_log(h->avctx, AV_LOG_ERROR,
                       "error while decoding MB %d %d, bytestream %td\n",
                       h->mb_x, h->mb_y,
                       h->cabac.bytestream_end - h->cabac.bytestream);
                er_add_slice(h, h->resync_mb_x, h->resync_mb_y, h->mb_x,
                             h->mb_y, ER_MB_ERROR);
                return AVERROR_INVALIDDATA;
            }

            if (++h->mb_x >= h->mb_width) {
                loop_filter(h, lf_x_start, h->mb_x);
                h->mb_x = lf_x_start = 0;
                decode_finish_row(h);
                ++h->mb_y;
                if (FIELD_OR_MBAFF_PICTURE(h)) {
                    ++h->mb_y;
                    if (FRAME_MBAFF(h) && h->mb_y < h->mb_height)
                        predict_field_decoding_flag(h);
                }
            }

            if (eos || h->mb_y >= h->mb_height) {
                tprintf(h->avctx, "slice end %d %d\n",
                        get_bits_count(&h->gb), h->gb.size_in_bits);
                er_add_slice(h, h->resync_mb_x, h->resync_mb_y, h->mb_x - 1,
                             h->mb_y, ER_MB_END);
                if (h->mb_x > lf_x_start)
                    loop_filter(h, lf_x_start, h->mb_x);
                return 0;
            }
        }
    } else {
        for (;;) {
            int ret = ff_h264_decode_mb_cavlc(h);

            if (ret >= 0)
                ff_h264_hl_decode_mb(h);

            // FIXME optimal? or let mb_decode decode 16x32 ?
            if (ret >= 0 && FRAME_MBAFF(h)) {
                h->mb_y++;
                ret = ff_h264_decode_mb_cavlc(h);

                if (ret >= 0)
                    ff_h264_hl_decode_mb(h);
                h->mb_y--;
            }

            if (ret < 0) {
                av_log(h->avctx, AV_LOG_ERROR,
                       "error while decoding MB %d %d\n", h->mb_x, h->mb_y);
                er_add_slice(h, h->resync_mb_x, h->resync_mb_y, h->mb_x,
                             h->mb_y, ER_MB_ERROR);
                return ret;
            }

            if (++h->mb_x >= h->mb_width) {
                loop_filter(h, lf_x_start, h->mb_x);
                h->mb_x = lf_x_start = 0;
                decode_finish_row(h);
                ++h->mb_y;
                if (FIELD_OR_MBAFF_PICTURE(h)) {
                    ++h->mb_y;
                    if (FRAME_MBAFF(h) && h->mb_y < h->mb_height)
                        predict_field_decoding_flag(h);
                }
                if (h->mb_y >= h->mb_height) {
                    tprintf(h->avctx, "slice end %d %d\n",
                            get_bits_count(&h->gb), h->gb.size_in_bits);

                    if (   get_bits_left(&h->gb) == 0
                        || get_bits_left(&h->gb) > 0 && !(h->avctx->err_recognition & AV_EF_AGGRESSIVE)) {
                        er_add_slice(h, h->resync_mb_x, h->resync_mb_y,
                                     h->mb_x - 1, h->mb_y,
                                     ER_MB_END);

                        return 0;
                    } else {
                        er_add_slice(h, h->resync_mb_x, h->resync_mb_y,
                                     h->mb_x, h->mb_y,
                                     ER_MB_END);

                        return AVERROR_INVALIDDATA;
                    }
                }
            }

            if (get_bits_left(&h->gb) <= 0 && h->mb_skip_run <= 0) {
                tprintf(h->avctx, "slice end %d %d\n",
                        get_bits_count(&h->gb), h->gb.size_in_bits);

                if (get_bits_left(&h->gb) == 0) {
                    er_add_slice(h, h->resync_mb_x, h->resync_mb_y,
                                 h->mb_x - 1, h->mb_y,
                                 ER_MB_END);
                    if (h->mb_x > lf_x_start)
                        loop_filter(h, lf_x_start, h->mb_x);

                    return 0;
                } else {
                    er_add_slice(h, h->resync_mb_x, h->resync_mb_y, h->mb_x,
                                 h->mb_y, ER_MB_ERROR);

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
    H264Context *hx;
    int i;

    av_assert0(h->mb_y < h->mb_height);

    if (h->avctx->hwaccel ||
        h->avctx->codec->capabilities & CODEC_CAP_HWACCEL_VDPAU)
        return 0;
    if (context_count == 1) {
        return decode_slice(avctx, &h);
    } else {
        av_assert0(context_count > 0);
        for (i = 1; i < context_count; i++) {
            hx                 = h->thread_context[i];
            if (CONFIG_ERROR_RESILIENCE) {
                hx->er.error_count = 0;
            }
            hx->x264_build     = h->x264_build;
        }

        avctx->execute(avctx, decode_slice, h->thread_context,
                       NULL, context_count, sizeof(void *));

        /* pull back stuff from slices to master context */
        hx                   = h->thread_context[context_count - 1];
        h->mb_x              = hx->mb_x;
        h->mb_y              = hx->mb_y;
        h->droppable         = hx->droppable;
        h->picture_structure = hx->picture_structure;
        if (CONFIG_ERROR_RESILIENCE) {
            for (i = 1; i < context_count; i++)
                h->er.error_count += h->thread_context[i]->er.error_count;
        }
    }

    return 0;
}
