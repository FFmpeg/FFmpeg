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

#define UNCHECKED_BITSTREAM_READER 1

#include "libavutil/avassert.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "internal.h"
#include "cabac.h"
#include "cabac_functions.h"
#include "dsputil.h"
#include "error_resilience.h"
#include "avcodec.h"
#include "mpegvideo.h"
#include "h264.h"
#include "h264data.h"
#include "h264chroma.h"
#include "h264_mvpred.h"
#include "golomb.h"
#include "mathops.h"
#include "rectangle.h"
#include "svq3.h"
#include "thread.h"
#include "vdpau_internal.h"

// #undef NDEBUG
#include <assert.h>

const uint16_t ff_h264_mb_sizes[4] = { 256, 384, 512, 768 };

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

int avpriv_h264_has_num_reorder_frames(AVCodecContext *avctx)
{
    H264Context *h = avctx->priv_data;
    return h ? h->sps.num_reorder_frames : 0;
}

static void h264_er_decode_mb(void *opaque, int ref, int mv_dir, int mv_type,
                              int (*mv)[2][4][2],
                              int mb_x, int mb_y, int mb_intra, int mb_skipped)
{
    H264Context *h = opaque;

    h->mb_x  = mb_x;
    h->mb_y  = mb_y;
    h->mb_xy = mb_x + mb_y * h->mb_stride;
    memset(h->non_zero_count_cache, 0, sizeof(h->non_zero_count_cache));
    av_assert1(ref >= 0);
    /* FIXME: It is possible albeit uncommon that slice references
     * differ between slices. We take the easy approach and ignore
     * it for now. If this turns out to have any relevance in
     * practice then correct remapping should be added. */
    if (ref >= h->ref_count[0])
        ref = 0;
    if (!h->ref_list[0][ref].f.data[0]) {
        av_log(h->avctx, AV_LOG_DEBUG, "Reference not available for error concealing\n");
        ref = 0;
    }
    if ((h->ref_list[0][ref].reference&3) != 3) {
        av_log(h->avctx, AV_LOG_DEBUG, "Reference invalid\n");
        return;
    }
    fill_rectangle(&h->cur_pic.ref_index[0][4 * h->mb_xy],
                   2, 2, 2, ref, 1);
    fill_rectangle(&h->ref_cache[0][scan8[0]], 4, 4, 8, ref, 1);
    fill_rectangle(h->mv_cache[0][scan8[0]], 4, 4, 8,
                   pack16to32((*mv)[0][0][0], (*mv)[0][0][1]), 4);
    h->mb_mbaff =
    h->mb_field_decoding_flag = 0;
    ff_h264_hl_decode_mb(h);
}

void ff_h264_draw_horiz_band(H264Context *h, int y, int height)
{
    AVCodecContext *avctx = h->avctx;
    Picture *cur  = &h->cur_pic;
    Picture *last = h->ref_list[0][0].f.data[0] ? &h->ref_list[0][0] : NULL;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(avctx->pix_fmt);
    int vshift = desc->log2_chroma_h;
    const int field_pic = h->picture_structure != PICT_FRAME;
    if (field_pic) {
        height <<= 1;
        y      <<= 1;
    }

    height = FFMIN(height, avctx->height - y);

    if (field_pic && h->first_field && !(avctx->slice_flags & SLICE_FLAG_ALLOW_FIELD))
        return;

    if (avctx->draw_horiz_band) {
        AVFrame *src;
        int offset[AV_NUM_DATA_POINTERS];
        int i;

        if (cur->f.pict_type == AV_PICTURE_TYPE_B || h->low_delay ||
            (avctx->slice_flags & SLICE_FLAG_CODED_ORDER))
            src = &cur->f;
        else if (last)
            src = &last->f;
        else
            return;

        offset[0] = y * src->linesize[0];
        offset[1] =
        offset[2] = (y >> vshift) * src->linesize[1];
        for (i = 3; i < AV_NUM_DATA_POINTERS; i++)
            offset[i] = 0;

        emms_c();

        avctx->draw_horiz_band(avctx, src, offset,
                               y, h->picture_structure, height);
    }
}

static void unref_picture(H264Context *h, Picture *pic)
{
    int off = offsetof(Picture, tf) + sizeof(pic->tf);
    int i;

    if (!pic->f.data[0])
        return;

    ff_thread_release_buffer(h->avctx, &pic->tf);
    av_buffer_unref(&pic->hwaccel_priv_buf);

    av_buffer_unref(&pic->qscale_table_buf);
    av_buffer_unref(&pic->mb_type_buf);
    for (i = 0; i < 2; i++) {
        av_buffer_unref(&pic->motion_val_buf[i]);
        av_buffer_unref(&pic->ref_index_buf[i]);
    }

    memset((uint8_t*)pic + off, 0, sizeof(*pic) - off);
}

static void release_unused_pictures(H264Context *h, int remove_current)
{
    int i;

    /* release non reference frames */
    for (i = 0; i < MAX_PICTURE_COUNT; i++) {
        if (h->DPB[i].f.data[0] && !h->DPB[i].reference &&
            (remove_current || &h->DPB[i] != h->cur_pic_ptr)) {
            unref_picture(h, &h->DPB[i]);
        }
    }
}

static int ref_picture(H264Context *h, Picture *dst, Picture *src)
{
    int ret, i;

    av_assert0(!dst->f.buf[0]);
    av_assert0(src->f.buf[0]);

    src->tf.f = &src->f;
    dst->tf.f = &dst->f;
    ret = ff_thread_ref_frame(&dst->tf, &src->tf);
    if (ret < 0)
        goto fail;

    dst->qscale_table_buf = av_buffer_ref(src->qscale_table_buf);
    dst->mb_type_buf      = av_buffer_ref(src->mb_type_buf);
    if (!dst->qscale_table_buf || !dst->mb_type_buf)
        goto fail;
    dst->qscale_table = src->qscale_table;
    dst->mb_type      = src->mb_type;

    for (i = 0; i < 2; i++) {
        dst->motion_val_buf[i] = av_buffer_ref(src->motion_val_buf[i]);
        dst->ref_index_buf[i]  = av_buffer_ref(src->ref_index_buf[i]);
        if (!dst->motion_val_buf[i] || !dst->ref_index_buf[i])
            goto fail;
        dst->motion_val[i] = src->motion_val[i];
        dst->ref_index[i]  = src->ref_index[i];
    }

    if (src->hwaccel_picture_private) {
        dst->hwaccel_priv_buf = av_buffer_ref(src->hwaccel_priv_buf);
        if (!dst->hwaccel_priv_buf)
            goto fail;
        dst->hwaccel_picture_private = dst->hwaccel_priv_buf->data;
    }

    for (i = 0; i < 2; i++)
        dst->field_poc[i] = src->field_poc[i];

    memcpy(dst->ref_poc,   src->ref_poc,   sizeof(src->ref_poc));
    memcpy(dst->ref_count, src->ref_count, sizeof(src->ref_count));

    dst->poc           = src->poc;
    dst->frame_num     = src->frame_num;
    dst->mmco_reset    = src->mmco_reset;
    dst->pic_id        = src->pic_id;
    dst->long_ref      = src->long_ref;
    dst->mbaff         = src->mbaff;
    dst->field_picture = src->field_picture;
    dst->needs_realloc = src->needs_realloc;
    dst->reference     = src->reference;
    dst->sync          = src->sync;
    dst->crop          = src->crop;
    dst->crop_left     = src->crop_left;
    dst->crop_top      = src->crop_top;

    return 0;
fail:
    unref_picture(h, dst);
    return ret;
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
    h->me.scratchpad   = av_mallocz(alloc_size * 2 * 16 * 2);

    if (!h->bipred_scratchpad || !h->edge_emu_buffer || !h->me.scratchpad) {
        av_freep(&h->bipred_scratchpad);
        av_freep(&h->edge_emu_buffer);
        av_freep(&h->me.scratchpad);
        return AVERROR(ENOMEM);
    }

    h->me.temp = h->me.scratchpad;

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

static int alloc_picture(H264Context *h, Picture *pic)
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
    unref_picture(h, pic);
    return (ret < 0) ? ret : AVERROR(ENOMEM);
}

static inline int pic_is_unused(H264Context *h, Picture *pic)
{
    if (pic->f.data[0] == NULL)
        return 1;
    if (pic->needs_realloc && !(pic->reference & DELAYED_PIC_REF))
        return 1;
    return 0;
}

static int find_unused_picture(H264Context *h)
{
    int i;

    for (i = 0; i < MAX_PICTURE_COUNT; i++) {
        if (pic_is_unused(h, &h->DPB[i]))
            break;
    }
    if (i == MAX_PICTURE_COUNT)
        return AVERROR_INVALIDDATA;

    if (h->DPB[i].needs_realloc) {
        h->DPB[i].needs_realloc = 0;
        unref_picture(h, &h->DPB[i]);
    }

    return i;
}

/**
 * Check if the top & left blocks are available if needed and
 * change the dc mode so it only uses the available blocks.
 */
int ff_h264_check_intra4x4_pred_mode(H264Context *h)
{
    static const int8_t top[12] = {
        -1, 0, LEFT_DC_PRED, -1, -1, -1, -1, -1, 0
    };
    static const int8_t left[12] = {
        0, -1, TOP_DC_PRED, 0, -1, -1, -1, 0, -1, DC_128_PRED
    };
    int i;

    if (!(h->top_samples_available & 0x8000)) {
        for (i = 0; i < 4; i++) {
            int status = top[h->intra4x4_pred_mode_cache[scan8[0] + i]];
            if (status < 0) {
                av_log(h->avctx, AV_LOG_ERROR,
                       "top block unavailable for requested intra4x4 mode %d at %d %d\n",
                       status, h->mb_x, h->mb_y);
                return AVERROR_INVALIDDATA;
            } else if (status) {
                h->intra4x4_pred_mode_cache[scan8[0] + i] = status;
            }
        }
    }

    if ((h->left_samples_available & 0x8888) != 0x8888) {
        static const int mask[4] = { 0x8000, 0x2000, 0x80, 0x20 };
        for (i = 0; i < 4; i++)
            if (!(h->left_samples_available & mask[i])) {
                int status = left[h->intra4x4_pred_mode_cache[scan8[0] + 8 * i]];
                if (status < 0) {
                    av_log(h->avctx, AV_LOG_ERROR,
                           "left block unavailable for requested intra4x4 mode %d at %d %d\n",
                           status, h->mb_x, h->mb_y);
                    return AVERROR_INVALIDDATA;
                } else if (status) {
                    h->intra4x4_pred_mode_cache[scan8[0] + 8 * i] = status;
                }
            }
    }

    return 0;
} // FIXME cleanup like ff_h264_check_intra_pred_mode

/**
 * Check if the top & left blocks are available if needed and
 * change the dc mode so it only uses the available blocks.
 */
int ff_h264_check_intra_pred_mode(H264Context *h, int mode, int is_chroma)
{
    static const int8_t top[4]  = { LEFT_DC_PRED8x8, 1, -1, -1 };
    static const int8_t left[5] = { TOP_DC_PRED8x8, -1, 2, -1, DC_128_PRED8x8 };

    if (mode > 3U) {
        av_log(h->avctx, AV_LOG_ERROR,
               "out of range intra chroma pred mode at %d %d\n",
               h->mb_x, h->mb_y);
        return AVERROR_INVALIDDATA;
    }

    if (!(h->top_samples_available & 0x8000)) {
        mode = top[mode];
        if (mode < 0) {
            av_log(h->avctx, AV_LOG_ERROR,
                   "top block unavailable for requested intra mode at %d %d\n",
                   h->mb_x, h->mb_y);
            return AVERROR_INVALIDDATA;
        }
    }

    if ((h->left_samples_available & 0x8080) != 0x8080) {
        mode = left[mode];
        if (is_chroma && (h->left_samples_available & 0x8080)) {
            // mad cow disease mode, aka MBAFF + constrained_intra_pred
            mode = ALZHEIMER_DC_L0T_PRED8x8 +
                   (!(h->left_samples_available & 0x8000)) +
                   2 * (mode == DC_128_PRED8x8);
        }
        if (mode < 0) {
            av_log(h->avctx, AV_LOG_ERROR,
                   "left block unavailable for requested intra mode at %d %d\n",
                   h->mb_x, h->mb_y);
            return AVERROR_INVALIDDATA;
        }
    }

    return mode;
}

const uint8_t *ff_h264_decode_nal(H264Context *h, const uint8_t *src,
                                  int *dst_length, int *consumed, int length)
{
    int i, si, di;
    uint8_t *dst;
    int bufidx;

    // src[0]&0x80; // forbidden bit
    h->nal_ref_idc   = src[0] >> 5;
    h->nal_unit_type = src[0] & 0x1F;

    src++;
    length--;

#define STARTCODE_TEST                                                  \
    if (i + 2 < length && src[i + 1] == 0 && src[i + 2] <= 3) {         \
        if (src[i + 2] != 3) {                                          \
            /* startcode, so we must be past the end */                 \
            length = i;                                                 \
        }                                                               \
        break;                                                          \
    }

#if HAVE_FAST_UNALIGNED
#define FIND_FIRST_ZERO                                                 \
    if (i > 0 && !src[i])                                               \
        i--;                                                            \
    while (src[i])                                                      \
        i++

#if HAVE_FAST_64BIT
    for (i = 0; i + 1 < length; i += 9) {
        if (!((~AV_RN64A(src + i) &
               (AV_RN64A(src + i) - 0x0100010001000101ULL)) &
              0x8000800080008080ULL))
            continue;
        FIND_FIRST_ZERO;
        STARTCODE_TEST;
        i -= 7;
    }
#else
    for (i = 0; i + 1 < length; i += 5) {
        if (!((~AV_RN32A(src + i) &
               (AV_RN32A(src + i) - 0x01000101U)) &
              0x80008080U))
            continue;
        FIND_FIRST_ZERO;
        STARTCODE_TEST;
        i -= 3;
    }
#endif
#else
    for (i = 0; i + 1 < length; i += 2) {
        if (src[i])
            continue;
        if (i > 0 && src[i - 1] == 0)
            i--;
        STARTCODE_TEST;
    }
#endif

    // use second escape buffer for inter data
    bufidx = h->nal_unit_type == NAL_DPC ? 1 : 0;

    si = h->rbsp_buffer_size[bufidx];
    av_fast_padded_malloc(&h->rbsp_buffer[bufidx], &h->rbsp_buffer_size[bufidx], length+MAX_MBPAIR_SIZE);
    dst = h->rbsp_buffer[bufidx];

    if (dst == NULL)
        return NULL;

    if(i>=length-1){ //no escaped 0
        *dst_length= length;
        *consumed= length+1; //+1 for the header
        if(h->avctx->flags2 & CODEC_FLAG2_FAST){
            return src;
        }else{
            memcpy(dst, src, length);
            return dst;
        }
    }

    memcpy(dst, src, i);
    si = di = i;
    while (si + 2 < length) {
        // remove escapes (very rare 1:2^22)
        if (src[si + 2] > 3) {
            dst[di++] = src[si++];
            dst[di++] = src[si++];
        } else if (src[si] == 0 && src[si + 1] == 0) {
            if (src[si + 2] == 3) { // escape
                dst[di++]  = 0;
                dst[di++]  = 0;
                si        += 3;
                continue;
            } else // next start code
                goto nsc;
        }

        dst[di++] = src[si++];
    }
    while (si < length)
        dst[di++] = src[si++];

nsc:
    memset(dst + di, 0, FF_INPUT_BUFFER_PADDING_SIZE);

    *dst_length = di;
    *consumed   = si + 1; // +1 for the header
    /* FIXME store exact number of bits in the getbitcontext
     * (it is needed for decoding) */
    return dst;
}

/**
 * Identify the exact end of the bitstream
 * @return the length of the trailing, or 0 if damaged
 */
static int decode_rbsp_trailing(H264Context *h, const uint8_t *src)
{
    int v = *src;
    int r;

    tprintf(h->avctx, "rbsp trailing %X\n", v);

    for (r = 1; r < 9; r++) {
        if (v & 1)
            return r;
        v >>= 1;
    }
    return 0;
}

static inline int get_lowest_part_list_y(H264Context *h, Picture *pic, int n,
                                         int height, int y_offset, int list)
{
    int raw_my             = h->mv_cache[list][scan8[n]][1];
    int filter_height_down = (raw_my & 3) ? 3 : 0;
    int full_my            = (raw_my >> 2) + y_offset;
    int bottom             = full_my + filter_height_down + height;

    av_assert2(height >= 0);

    return FFMAX(0, bottom);
}

static inline void get_lowest_part_y(H264Context *h, int refs[2][48], int n,
                                     int height, int y_offset, int list0,
                                     int list1, int *nrefs)
{
    int my;

    y_offset += 16 * (h->mb_y >> MB_FIELD(h));

    if (list0) {
        int ref_n    = h->ref_cache[0][scan8[n]];
        Picture *ref = &h->ref_list[0][ref_n];

        // Error resilience puts the current picture in the ref list.
        // Don't try to wait on these as it will cause a deadlock.
        // Fields can wait on each other, though.
        if (ref->tf.progress->data != h->cur_pic.tf.progress->data ||
            (ref->reference & 3) != h->picture_structure) {
            my = get_lowest_part_list_y(h, ref, n, height, y_offset, 0);
            if (refs[0][ref_n] < 0)
                nrefs[0] += 1;
            refs[0][ref_n] = FFMAX(refs[0][ref_n], my);
        }
    }

    if (list1) {
        int ref_n    = h->ref_cache[1][scan8[n]];
        Picture *ref = &h->ref_list[1][ref_n];

        if (ref->tf.progress->data != h->cur_pic.tf.progress->data ||
            (ref->reference & 3) != h->picture_structure) {
            my = get_lowest_part_list_y(h, ref, n, height, y_offset, 1);
            if (refs[1][ref_n] < 0)
                nrefs[1] += 1;
            refs[1][ref_n] = FFMAX(refs[1][ref_n], my);
        }
    }
}

/**
 * Wait until all reference frames are available for MC operations.
 *
 * @param h the H264 context
 */
static void await_references(H264Context *h)
{
    const int mb_xy   = h->mb_xy;
    const int mb_type = h->cur_pic.mb_type[mb_xy];
    int refs[2][48];
    int nrefs[2] = { 0 };
    int ref, list;

    memset(refs, -1, sizeof(refs));

    if (IS_16X16(mb_type)) {
        get_lowest_part_y(h, refs, 0, 16, 0,
                          IS_DIR(mb_type, 0, 0), IS_DIR(mb_type, 0, 1), nrefs);
    } else if (IS_16X8(mb_type)) {
        get_lowest_part_y(h, refs, 0, 8, 0,
                          IS_DIR(mb_type, 0, 0), IS_DIR(mb_type, 0, 1), nrefs);
        get_lowest_part_y(h, refs, 8, 8, 8,
                          IS_DIR(mb_type, 1, 0), IS_DIR(mb_type, 1, 1), nrefs);
    } else if (IS_8X16(mb_type)) {
        get_lowest_part_y(h, refs, 0, 16, 0,
                          IS_DIR(mb_type, 0, 0), IS_DIR(mb_type, 0, 1), nrefs);
        get_lowest_part_y(h, refs, 4, 16, 0,
                          IS_DIR(mb_type, 1, 0), IS_DIR(mb_type, 1, 1), nrefs);
    } else {
        int i;

        av_assert2(IS_8X8(mb_type));

        for (i = 0; i < 4; i++) {
            const int sub_mb_type = h->sub_mb_type[i];
            const int n           = 4 * i;
            int y_offset          = (i & 2) << 2;

            if (IS_SUB_8X8(sub_mb_type)) {
                get_lowest_part_y(h, refs, n, 8, y_offset,
                                  IS_DIR(sub_mb_type, 0, 0),
                                  IS_DIR(sub_mb_type, 0, 1),
                                  nrefs);
            } else if (IS_SUB_8X4(sub_mb_type)) {
                get_lowest_part_y(h, refs, n, 4, y_offset,
                                  IS_DIR(sub_mb_type, 0, 0),
                                  IS_DIR(sub_mb_type, 0, 1),
                                  nrefs);
                get_lowest_part_y(h, refs, n + 2, 4, y_offset + 4,
                                  IS_DIR(sub_mb_type, 0, 0),
                                  IS_DIR(sub_mb_type, 0, 1),
                                  nrefs);
            } else if (IS_SUB_4X8(sub_mb_type)) {
                get_lowest_part_y(h, refs, n, 8, y_offset,
                                  IS_DIR(sub_mb_type, 0, 0),
                                  IS_DIR(sub_mb_type, 0, 1),
                                  nrefs);
                get_lowest_part_y(h, refs, n + 1, 8, y_offset,
                                  IS_DIR(sub_mb_type, 0, 0),
                                  IS_DIR(sub_mb_type, 0, 1),
                                  nrefs);
            } else {
                int j;
                av_assert2(IS_SUB_4X4(sub_mb_type));
                for (j = 0; j < 4; j++) {
                    int sub_y_offset = y_offset + 2 * (j & 2);
                    get_lowest_part_y(h, refs, n + j, 4, sub_y_offset,
                                      IS_DIR(sub_mb_type, 0, 0),
                                      IS_DIR(sub_mb_type, 0, 1),
                                      nrefs);
                }
            }
        }
    }

    for (list = h->list_count - 1; list >= 0; list--)
        for (ref = 0; ref < 48 && nrefs[list]; ref++) {
            int row = refs[list][ref];
            if (row >= 0) {
                Picture *ref_pic      = &h->ref_list[list][ref];
                int ref_field         = ref_pic->reference - 1;
                int ref_field_picture = ref_pic->field_picture;
                int pic_height        = 16 * h->mb_height >> ref_field_picture;

                row <<= MB_MBAFF(h);
                nrefs[list]--;

                if (!FIELD_PICTURE(h) && ref_field_picture) { // frame referencing two fields
                    ff_thread_await_progress(&ref_pic->tf,
                                             FFMIN((row >> 1) - !(row & 1),
                                                   pic_height - 1),
                                             1);
                    ff_thread_await_progress(&ref_pic->tf,
                                             FFMIN((row >> 1), pic_height - 1),
                                             0);
                } else if (FIELD_PICTURE(h) && !ref_field_picture) { // field referencing one field of a frame
                    ff_thread_await_progress(&ref_pic->tf,
                                             FFMIN(row * 2 + ref_field,
                                                   pic_height - 1),
                                             0);
                } else if (FIELD_PICTURE(h)) {
                    ff_thread_await_progress(&ref_pic->tf,
                                             FFMIN(row, pic_height - 1),
                                             ref_field);
                } else {
                    ff_thread_await_progress(&ref_pic->tf,
                                             FFMIN(row, pic_height - 1),
                                             0);
                }
            }
        }
}

static av_always_inline void mc_dir_part(H264Context *h, Picture *pic,
                                         int n, int square, int height,
                                         int delta, int list,
                                         uint8_t *dest_y, uint8_t *dest_cb,
                                         uint8_t *dest_cr,
                                         int src_x_offset, int src_y_offset,
                                         qpel_mc_func *qpix_op,
                                         h264_chroma_mc_func chroma_op,
                                         int pixel_shift, int chroma_idc)
{
    const int mx      = h->mv_cache[list][scan8[n]][0] + src_x_offset * 8;
    int my            = h->mv_cache[list][scan8[n]][1] + src_y_offset * 8;
    const int luma_xy = (mx & 3) + ((my & 3) << 2);
    int offset        = ((mx >> 2) << pixel_shift) + (my >> 2) * h->mb_linesize;
    uint8_t *src_y    = pic->f.data[0] + offset;
    uint8_t *src_cb, *src_cr;
    int extra_width  = 0;
    int extra_height = 0;
    int emu = 0;
    const int full_mx    = mx >> 2;
    const int full_my    = my >> 2;
    const int pic_width  = 16 * h->mb_width;
    const int pic_height = 16 * h->mb_height >> MB_FIELD(h);
    int ysh;

    if (mx & 7)
        extra_width -= 3;
    if (my & 7)
        extra_height -= 3;

    if (full_mx                <          0 - extra_width  ||
        full_my                <          0 - extra_height ||
        full_mx + 16 /*FIXME*/ > pic_width  + extra_width  ||
        full_my + 16 /*FIXME*/ > pic_height + extra_height) {
        h->vdsp.emulated_edge_mc(h->edge_emu_buffer,
                                 src_y - (2 << pixel_shift) - 2 * h->mb_linesize,
                                 h->mb_linesize,
                                 16 + 5, 16 + 5 /*FIXME*/, full_mx - 2,
                                 full_my - 2, pic_width, pic_height);
        src_y = h->edge_emu_buffer + (2 << pixel_shift) + 2 * h->mb_linesize;
        emu   = 1;
    }

    qpix_op[luma_xy](dest_y, src_y, h->mb_linesize); // FIXME try variable height perhaps?
    if (!square)
        qpix_op[luma_xy](dest_y + delta, src_y + delta, h->mb_linesize);

    if (CONFIG_GRAY && h->flags & CODEC_FLAG_GRAY)
        return;

    if (chroma_idc == 3 /* yuv444 */) {
        src_cb = pic->f.data[1] + offset;
        if (emu) {
            h->vdsp.emulated_edge_mc(h->edge_emu_buffer,
                                     src_cb - (2 << pixel_shift) - 2 * h->mb_linesize,
                                     h->mb_linesize,
                                     16 + 5, 16 + 5 /*FIXME*/,
                                     full_mx - 2, full_my - 2,
                                     pic_width, pic_height);
            src_cb = h->edge_emu_buffer + (2 << pixel_shift) + 2 * h->mb_linesize;
        }
        qpix_op[luma_xy](dest_cb, src_cb, h->mb_linesize); // FIXME try variable height perhaps?
        if (!square)
            qpix_op[luma_xy](dest_cb + delta, src_cb + delta, h->mb_linesize);

        src_cr = pic->f.data[2] + offset;
        if (emu) {
            h->vdsp.emulated_edge_mc(h->edge_emu_buffer,
                                     src_cr - (2 << pixel_shift) - 2 * h->mb_linesize,
                                     h->mb_linesize,
                                     16 + 5, 16 + 5 /*FIXME*/,
                                     full_mx - 2, full_my - 2,
                                     pic_width, pic_height);
            src_cr = h->edge_emu_buffer + (2 << pixel_shift) + 2 * h->mb_linesize;
        }
        qpix_op[luma_xy](dest_cr, src_cr, h->mb_linesize); // FIXME try variable height perhaps?
        if (!square)
            qpix_op[luma_xy](dest_cr + delta, src_cr + delta, h->mb_linesize);
        return;
    }

    ysh = 3 - (chroma_idc == 2 /* yuv422 */);
    if (chroma_idc == 1 /* yuv420 */ && MB_FIELD(h)) {
        // chroma offset when predicting from a field of opposite parity
        my  += 2 * ((h->mb_y & 1) - (pic->reference - 1));
        emu |= (my >> 3) < 0 || (my >> 3) + 8 >= (pic_height >> 1);
    }

    src_cb = pic->f.data[1] + ((mx >> 3) << pixel_shift) +
             (my >> ysh) * h->mb_uvlinesize;
    src_cr = pic->f.data[2] + ((mx >> 3) << pixel_shift) +
             (my >> ysh) * h->mb_uvlinesize;

    if (emu) {
        h->vdsp.emulated_edge_mc(h->edge_emu_buffer, src_cb, h->mb_uvlinesize,
                                 9, 8 * chroma_idc + 1, (mx >> 3), (my >> ysh),
                                 pic_width >> 1, pic_height >> (chroma_idc == 1 /* yuv420 */));
        src_cb = h->edge_emu_buffer;
    }
    chroma_op(dest_cb, src_cb, h->mb_uvlinesize,
              height >> (chroma_idc == 1 /* yuv420 */),
              mx & 7, (my << (chroma_idc == 2 /* yuv422 */)) & 7);

    if (emu) {
        h->vdsp.emulated_edge_mc(h->edge_emu_buffer, src_cr, h->mb_uvlinesize,
                                 9, 8 * chroma_idc + 1, (mx >> 3), (my >> ysh),
                                 pic_width >> 1, pic_height >> (chroma_idc == 1 /* yuv420 */));
        src_cr = h->edge_emu_buffer;
    }
    chroma_op(dest_cr, src_cr, h->mb_uvlinesize, height >> (chroma_idc == 1 /* yuv420 */),
              mx & 7, (my << (chroma_idc == 2 /* yuv422 */)) & 7);
}

static av_always_inline void mc_part_std(H264Context *h, int n, int square,
                                         int height, int delta,
                                         uint8_t *dest_y, uint8_t *dest_cb,
                                         uint8_t *dest_cr,
                                         int x_offset, int y_offset,
                                         qpel_mc_func *qpix_put,
                                         h264_chroma_mc_func chroma_put,
                                         qpel_mc_func *qpix_avg,
                                         h264_chroma_mc_func chroma_avg,
                                         int list0, int list1,
                                         int pixel_shift, int chroma_idc)
{
    qpel_mc_func *qpix_op         = qpix_put;
    h264_chroma_mc_func chroma_op = chroma_put;

    dest_y += (2 * x_offset << pixel_shift) + 2 * y_offset * h->mb_linesize;
    if (chroma_idc == 3 /* yuv444 */) {
        dest_cb += (2 * x_offset << pixel_shift) + 2 * y_offset * h->mb_linesize;
        dest_cr += (2 * x_offset << pixel_shift) + 2 * y_offset * h->mb_linesize;
    } else if (chroma_idc == 2 /* yuv422 */) {
        dest_cb += (x_offset << pixel_shift) + 2 * y_offset * h->mb_uvlinesize;
        dest_cr += (x_offset << pixel_shift) + 2 * y_offset * h->mb_uvlinesize;
    } else { /* yuv420 */
        dest_cb += (x_offset << pixel_shift) + y_offset * h->mb_uvlinesize;
        dest_cr += (x_offset << pixel_shift) + y_offset * h->mb_uvlinesize;
    }
    x_offset += 8 * h->mb_x;
    y_offset += 8 * (h->mb_y >> MB_FIELD(h));

    if (list0) {
        Picture *ref = &h->ref_list[0][h->ref_cache[0][scan8[n]]];
        mc_dir_part(h, ref, n, square, height, delta, 0,
                    dest_y, dest_cb, dest_cr, x_offset, y_offset,
                    qpix_op, chroma_op, pixel_shift, chroma_idc);

        qpix_op   = qpix_avg;
        chroma_op = chroma_avg;
    }

    if (list1) {
        Picture *ref = &h->ref_list[1][h->ref_cache[1][scan8[n]]];
        mc_dir_part(h, ref, n, square, height, delta, 1,
                    dest_y, dest_cb, dest_cr, x_offset, y_offset,
                    qpix_op, chroma_op, pixel_shift, chroma_idc);
    }
}

static av_always_inline void mc_part_weighted(H264Context *h, int n, int square,
                                              int height, int delta,
                                              uint8_t *dest_y, uint8_t *dest_cb,
                                              uint8_t *dest_cr,
                                              int x_offset, int y_offset,
                                              qpel_mc_func *qpix_put,
                                              h264_chroma_mc_func chroma_put,
                                              h264_weight_func luma_weight_op,
                                              h264_weight_func chroma_weight_op,
                                              h264_biweight_func luma_weight_avg,
                                              h264_biweight_func chroma_weight_avg,
                                              int list0, int list1,
                                              int pixel_shift, int chroma_idc)
{
    int chroma_height;

    dest_y += (2 * x_offset << pixel_shift) + 2 * y_offset * h->mb_linesize;
    if (chroma_idc == 3 /* yuv444 */) {
        chroma_height     = height;
        chroma_weight_avg = luma_weight_avg;
        chroma_weight_op  = luma_weight_op;
        dest_cb += (2 * x_offset << pixel_shift) + 2 * y_offset * h->mb_linesize;
        dest_cr += (2 * x_offset << pixel_shift) + 2 * y_offset * h->mb_linesize;
    } else if (chroma_idc == 2 /* yuv422 */) {
        chroma_height = height;
        dest_cb      += (x_offset << pixel_shift) + 2 * y_offset * h->mb_uvlinesize;
        dest_cr      += (x_offset << pixel_shift) + 2 * y_offset * h->mb_uvlinesize;
    } else { /* yuv420 */
        chroma_height = height >> 1;
        dest_cb      += (x_offset << pixel_shift) + y_offset * h->mb_uvlinesize;
        dest_cr      += (x_offset << pixel_shift) + y_offset * h->mb_uvlinesize;
    }
    x_offset += 8 * h->mb_x;
    y_offset += 8 * (h->mb_y >> MB_FIELD(h));

    if (list0 && list1) {
        /* don't optimize for luma-only case, since B-frames usually
         * use implicit weights => chroma too. */
        uint8_t *tmp_cb = h->bipred_scratchpad;
        uint8_t *tmp_cr = h->bipred_scratchpad + (16 << pixel_shift);
        uint8_t *tmp_y  = h->bipred_scratchpad + 16 * h->mb_uvlinesize;
        int refn0       = h->ref_cache[0][scan8[n]];
        int refn1       = h->ref_cache[1][scan8[n]];

        mc_dir_part(h, &h->ref_list[0][refn0], n, square, height, delta, 0,
                    dest_y, dest_cb, dest_cr,
                    x_offset, y_offset, qpix_put, chroma_put,
                    pixel_shift, chroma_idc);
        mc_dir_part(h, &h->ref_list[1][refn1], n, square, height, delta, 1,
                    tmp_y, tmp_cb, tmp_cr,
                    x_offset, y_offset, qpix_put, chroma_put,
                    pixel_shift, chroma_idc);

        if (h->use_weight == 2) {
            int weight0 = h->implicit_weight[refn0][refn1][h->mb_y & 1];
            int weight1 = 64 - weight0;
            luma_weight_avg(dest_y, tmp_y, h->mb_linesize,
                            height, 5, weight0, weight1, 0);
            chroma_weight_avg(dest_cb, tmp_cb, h->mb_uvlinesize,
                              chroma_height, 5, weight0, weight1, 0);
            chroma_weight_avg(dest_cr, tmp_cr, h->mb_uvlinesize,
                              chroma_height, 5, weight0, weight1, 0);
        } else {
            luma_weight_avg(dest_y, tmp_y, h->mb_linesize, height,
                            h->luma_log2_weight_denom,
                            h->luma_weight[refn0][0][0],
                            h->luma_weight[refn1][1][0],
                            h->luma_weight[refn0][0][1] +
                            h->luma_weight[refn1][1][1]);
            chroma_weight_avg(dest_cb, tmp_cb, h->mb_uvlinesize, chroma_height,
                              h->chroma_log2_weight_denom,
                              h->chroma_weight[refn0][0][0][0],
                              h->chroma_weight[refn1][1][0][0],
                              h->chroma_weight[refn0][0][0][1] +
                              h->chroma_weight[refn1][1][0][1]);
            chroma_weight_avg(dest_cr, tmp_cr, h->mb_uvlinesize, chroma_height,
                              h->chroma_log2_weight_denom,
                              h->chroma_weight[refn0][0][1][0],
                              h->chroma_weight[refn1][1][1][0],
                              h->chroma_weight[refn0][0][1][1] +
                              h->chroma_weight[refn1][1][1][1]);
        }
    } else {
        int list     = list1 ? 1 : 0;
        int refn     = h->ref_cache[list][scan8[n]];
        Picture *ref = &h->ref_list[list][refn];
        mc_dir_part(h, ref, n, square, height, delta, list,
                    dest_y, dest_cb, dest_cr, x_offset, y_offset,
                    qpix_put, chroma_put, pixel_shift, chroma_idc);

        luma_weight_op(dest_y, h->mb_linesize, height,
                       h->luma_log2_weight_denom,
                       h->luma_weight[refn][list][0],
                       h->luma_weight[refn][list][1]);
        if (h->use_weight_chroma) {
            chroma_weight_op(dest_cb, h->mb_uvlinesize, chroma_height,
                             h->chroma_log2_weight_denom,
                             h->chroma_weight[refn][list][0][0],
                             h->chroma_weight[refn][list][0][1]);
            chroma_weight_op(dest_cr, h->mb_uvlinesize, chroma_height,
                             h->chroma_log2_weight_denom,
                             h->chroma_weight[refn][list][1][0],
                             h->chroma_weight[refn][list][1][1]);
        }
    }
}

static av_always_inline void prefetch_motion(H264Context *h, int list,
                                             int pixel_shift, int chroma_idc)
{
    /* fetch pixels for estimated mv 4 macroblocks ahead
     * optimized for 64byte cache lines */
    const int refn = h->ref_cache[list][scan8[0]];
    if (refn >= 0) {
        const int mx  = (h->mv_cache[list][scan8[0]][0] >> 2) + 16 * h->mb_x + 8;
        const int my  = (h->mv_cache[list][scan8[0]][1] >> 2) + 16 * h->mb_y;
        uint8_t **src = h->ref_list[list][refn].f.data;
        int off       = (mx << pixel_shift) +
                        (my + (h->mb_x & 3) * 4) * h->mb_linesize +
                        (64 << pixel_shift);
        h->vdsp.prefetch(src[0] + off, h->linesize, 4);
        if (chroma_idc == 3 /* yuv444 */) {
            h->vdsp.prefetch(src[1] + off, h->linesize, 4);
            h->vdsp.prefetch(src[2] + off, h->linesize, 4);
        } else {
            off= (((mx>>1)+64)<<pixel_shift) + ((my>>1) + (h->mb_x&7))*h->uvlinesize;
            h->vdsp.prefetch(src[1] + off, src[2] - src[1], 2);
        }
    }
}

static void free_tables(H264Context *h, int free_rbsp)
{
    int i;
    H264Context *hx;

    av_freep(&h->intra4x4_pred_mode);
    av_freep(&h->chroma_pred_mode_table);
    av_freep(&h->cbp_table);
    av_freep(&h->mvd_table[0]);
    av_freep(&h->mvd_table[1]);
    av_freep(&h->direct_table);
    av_freep(&h->non_zero_count);
    av_freep(&h->slice_table_base);
    h->slice_table = NULL;
    av_freep(&h->list_counts);

    av_freep(&h->mb2b_xy);
    av_freep(&h->mb2br_xy);

    for (i = 0; i < 3; i++)
        av_freep(&h->visualization_buffer[i]);

    av_buffer_pool_uninit(&h->qscale_table_pool);
    av_buffer_pool_uninit(&h->mb_type_pool);
    av_buffer_pool_uninit(&h->motion_val_pool);
    av_buffer_pool_uninit(&h->ref_index_pool);

    if (free_rbsp && h->DPB) {
        for (i = 0; i < MAX_PICTURE_COUNT; i++)
            unref_picture(h, &h->DPB[i]);
        av_freep(&h->DPB);
    } else if (h->DPB) {
        for (i = 0; i < MAX_PICTURE_COUNT; i++)
            h->DPB[i].needs_realloc = 1;
    }

    h->cur_pic_ptr = NULL;

    for (i = 0; i < MAX_THREADS; i++) {
        hx = h->thread_context[i];
        if (!hx)
            continue;
        av_freep(&hx->top_borders[1]);
        av_freep(&hx->top_borders[0]);
        av_freep(&hx->bipred_scratchpad);
        av_freep(&hx->edge_emu_buffer);
        av_freep(&hx->dc_val_base);
        av_freep(&hx->me.scratchpad);
        av_freep(&hx->er.mb_index2xy);
        av_freep(&hx->er.error_status_table);
        av_freep(&hx->er.er_temp_buffer);
        av_freep(&hx->er.mbintra_table);
        av_freep(&hx->er.mbskip_table);

        if (free_rbsp) {
            av_freep(&hx->rbsp_buffer[1]);
            av_freep(&hx->rbsp_buffer[0]);
            hx->rbsp_buffer_size[0] = 0;
            hx->rbsp_buffer_size[1] = 0;
        }
        if (i)
            av_freep(&h->thread_context[i]);
    }
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

static void init_dequant_tables(H264Context *h)
{
    int i, x;
    init_dequant4_coeff_table(h);
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

int ff_h264_alloc_tables(H264Context *h)
{
    const int big_mb_num = h->mb_stride * (h->mb_height + 1);
    const int row_mb_num = 2*h->mb_stride*FFMAX(h->avctx->thread_count, 1);
    int x, y, i;

    FF_ALLOCZ_OR_GOTO(h->avctx, h->intra4x4_pred_mode,
                      row_mb_num * 8 * sizeof(uint8_t), fail)
    FF_ALLOCZ_OR_GOTO(h->avctx, h->non_zero_count,
                      big_mb_num * 48 * sizeof(uint8_t), fail)
    FF_ALLOCZ_OR_GOTO(h->avctx, h->slice_table_base,
                      (big_mb_num + h->mb_stride) * sizeof(*h->slice_table_base), fail)
    FF_ALLOCZ_OR_GOTO(h->avctx, h->cbp_table,
                      big_mb_num * sizeof(uint16_t), fail)
    FF_ALLOCZ_OR_GOTO(h->avctx, h->chroma_pred_mode_table,
                      big_mb_num * sizeof(uint8_t), fail)
    FF_ALLOCZ_OR_GOTO(h->avctx, h->mvd_table[0],
                      16 * row_mb_num * sizeof(uint8_t), fail);
    FF_ALLOCZ_OR_GOTO(h->avctx, h->mvd_table[1],
                      16 * row_mb_num * sizeof(uint8_t), fail);
    FF_ALLOCZ_OR_GOTO(h->avctx, h->direct_table,
                      4 * big_mb_num * sizeof(uint8_t), fail);
    FF_ALLOCZ_OR_GOTO(h->avctx, h->list_counts,
                      big_mb_num * sizeof(uint8_t), fail)

    memset(h->slice_table_base, -1,
           (big_mb_num + h->mb_stride) * sizeof(*h->slice_table_base));
    h->slice_table = h->slice_table_base + h->mb_stride * 2 + 1;

    FF_ALLOCZ_OR_GOTO(h->avctx, h->mb2b_xy,
                      big_mb_num * sizeof(uint32_t), fail);
    FF_ALLOCZ_OR_GOTO(h->avctx, h->mb2br_xy,
                      big_mb_num * sizeof(uint32_t), fail);
    for (y = 0; y < h->mb_height; y++)
        for (x = 0; x < h->mb_width; x++) {
            const int mb_xy = x + y * h->mb_stride;
            const int b_xy  = 4 * x + 4 * y * h->b_stride;

            h->mb2b_xy[mb_xy]  = b_xy;
            h->mb2br_xy[mb_xy] = 8 * (FMO ? mb_xy : (mb_xy % (2 * h->mb_stride)));
        }

    if (!h->dequant4_coeff[0])
        init_dequant_tables(h);

    if (!h->DPB) {
        h->DPB = av_mallocz_array(MAX_PICTURE_COUNT, sizeof(*h->DPB));
        if (!h->DPB)
            return AVERROR(ENOMEM);
        for (i = 0; i < MAX_PICTURE_COUNT; i++)
            avcodec_get_frame_defaults(&h->DPB[i].f);
        avcodec_get_frame_defaults(&h->cur_pic.f);
    }

    return 0;

fail:
    free_tables(h, 1);
    return AVERROR(ENOMEM);
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
    dst->me.scratchpad          = NULL;
    ff_h264_pred_init(&dst->hpc, src->avctx->codec_id, src->sps.bit_depth_luma,
                      src->sps.chroma_format_idc);
}

/**
 * Init context
 * Allocate buffers which are not shared amongst multiple threads.
 */
static int context_init(H264Context *h)
{
    ERContext *er = &h->er;
    int mb_array_size = h->mb_height * h->mb_stride;
    int y_size  = (2 * h->mb_width + 1) * (2 * h->mb_height + 1);
    int c_size  = h->mb_stride * (h->mb_height + 1);
    int yc_size = y_size + 2   * c_size;
    int x, y, i;

    FF_ALLOCZ_OR_GOTO(h->avctx, h->top_borders[0],
                      h->mb_width * 16 * 3 * sizeof(uint8_t) * 2, fail)
    FF_ALLOCZ_OR_GOTO(h->avctx, h->top_borders[1],
                      h->mb_width * 16 * 3 * sizeof(uint8_t) * 2, fail)

    h->ref_cache[0][scan8[5]  + 1] =
    h->ref_cache[0][scan8[7]  + 1] =
    h->ref_cache[0][scan8[13] + 1] =
    h->ref_cache[1][scan8[5]  + 1] =
    h->ref_cache[1][scan8[7]  + 1] =
    h->ref_cache[1][scan8[13] + 1] = PART_NOT_AVAILABLE;

    if (CONFIG_ERROR_RESILIENCE) {
        /* init ER */
        er->avctx          = h->avctx;
        er->dsp            = &h->dsp;
        er->decode_mb      = h264_er_decode_mb;
        er->opaque         = h;
        er->quarter_sample = 1;

        er->mb_num      = h->mb_num;
        er->mb_width    = h->mb_width;
        er->mb_height   = h->mb_height;
        er->mb_stride   = h->mb_stride;
        er->b8_stride   = h->mb_width * 2 + 1;

        FF_ALLOCZ_OR_GOTO(h->avctx, er->mb_index2xy, (h->mb_num + 1) * sizeof(int),
                          fail); // error ressilience code looks cleaner with this
        for (y = 0; y < h->mb_height; y++)
            for (x = 0; x < h->mb_width; x++)
                er->mb_index2xy[x + y * h->mb_width] = x + y * h->mb_stride;

        er->mb_index2xy[h->mb_height * h->mb_width] = (h->mb_height - 1) *
                                                      h->mb_stride + h->mb_width;

        FF_ALLOCZ_OR_GOTO(h->avctx, er->error_status_table,
                          mb_array_size * sizeof(uint8_t), fail);

        FF_ALLOC_OR_GOTO(h->avctx, er->mbintra_table, mb_array_size, fail);
        memset(er->mbintra_table, 1, mb_array_size);

        FF_ALLOCZ_OR_GOTO(h->avctx, er->mbskip_table, mb_array_size + 2, fail);

        FF_ALLOC_OR_GOTO(h->avctx, er->er_temp_buffer, h->mb_height * h->mb_stride,
                         fail);

        FF_ALLOCZ_OR_GOTO(h->avctx, h->dc_val_base, yc_size * sizeof(int16_t), fail);
        er->dc_val[0] = h->dc_val_base + h->mb_width * 2 + 2;
        er->dc_val[1] = h->dc_val_base + y_size + h->mb_stride + 1;
        er->dc_val[2] = er->dc_val[1] + c_size;
        for (i = 0; i < yc_size; i++)
            h->dc_val_base[i] = 1024;
    }

    return 0;

fail:
    return AVERROR(ENOMEM); // free_tables will clean up for us
}

static int decode_nal_units(H264Context *h, const uint8_t *buf, int buf_size,
                            int parse_extradata);

int ff_h264_decode_extradata(H264Context *h, const uint8_t *buf, int size)
{
    AVCodecContext *avctx = h->avctx;
    int ret;

    if (!buf || size <= 0)
        return -1;

    if (buf[0] == 1) {
        int i, cnt, nalsize;
        const unsigned char *p = buf;

        h->is_avc = 1;

        if (size < 7) {
            av_log(avctx, AV_LOG_ERROR, "avcC too short\n");
            return AVERROR_INVALIDDATA;
        }
        /* sps and pps in the avcC always have length coded with 2 bytes,
         * so put a fake nal_length_size = 2 while parsing them */
        h->nal_length_size = 2;
        // Decode sps from avcC
        cnt = *(p + 5) & 0x1f; // Number of sps
        p  += 6;
        for (i = 0; i < cnt; i++) {
            nalsize = AV_RB16(p) + 2;
            if(nalsize > size - (p-buf))
                return AVERROR_INVALIDDATA;
            ret = decode_nal_units(h, p, nalsize, 1);
            if (ret < 0) {
                av_log(avctx, AV_LOG_ERROR,
                       "Decoding sps %d from avcC failed\n", i);
                return ret;
            }
            p += nalsize;
        }
        // Decode pps from avcC
        cnt = *(p++); // Number of pps
        for (i = 0; i < cnt; i++) {
            nalsize = AV_RB16(p) + 2;
            if(nalsize > size - (p-buf))
                return AVERROR_INVALIDDATA;
            ret = decode_nal_units(h, p, nalsize, 1);
            if (ret < 0) {
                av_log(avctx, AV_LOG_ERROR,
                       "Decoding pps %d from avcC failed\n", i);
                return ret;
            }
            p += nalsize;
        }
        // Now store right nal length size, that will be used to parse all other nals
        h->nal_length_size = (buf[4] & 0x03) + 1;
    } else {
        h->is_avc = 0;
        ret = decode_nal_units(h, buf, size, 1);
        if (ret < 0)
            return ret;
    }
    return size;
}

av_cold int ff_h264_decode_init(AVCodecContext *avctx)
{
    H264Context *h = avctx->priv_data;
    int i;
    int ret;

    h->avctx = avctx;

    h->bit_depth_luma    = 8;
    h->chroma_format_idc = 1;

    h->avctx->bits_per_raw_sample = 8;
    h->cur_chroma_format_idc = 1;

    ff_h264dsp_init(&h->h264dsp, 8, 1);
    av_assert0(h->sps.bit_depth_chroma == 0);
    ff_h264chroma_init(&h->h264chroma, h->sps.bit_depth_chroma);
    ff_h264qpel_init(&h->h264qpel, 8);
    ff_h264_pred_init(&h->hpc, h->avctx->codec_id, 8, 1);

    h->dequant_coeff_pps = -1;
    h->current_sps_id = -1;

    /* needed so that IDCT permutation is known early */
    if (CONFIG_ERROR_RESILIENCE)
        ff_dsputil_init(&h->dsp, h->avctx);
    ff_videodsp_init(&h->vdsp, 8);

    memset(h->pps.scaling_matrix4, 16, 6 * 16 * sizeof(uint8_t));
    memset(h->pps.scaling_matrix8, 16, 2 * 64 * sizeof(uint8_t));

    h->picture_structure   = PICT_FRAME;
    h->slice_context_count = 1;
    h->workaround_bugs     = avctx->workaround_bugs;
    h->flags               = avctx->flags;

    /* set defaults */
    // s->decode_mb = ff_h263_decode_mb;
    if (!avctx->has_b_frames)
        h->low_delay = 1;

    avctx->chroma_sample_location = AVCHROMA_LOC_LEFT;

    ff_h264_decode_init_vlc();

    h->pixel_shift        = 0;
    h->sps.bit_depth_luma = avctx->bits_per_raw_sample = 8;

    h->thread_context[0] = h;
    h->outputed_poc      = h->next_outputed_poc = INT_MIN;
    for (i = 0; i < MAX_DELAYED_PIC_COUNT; i++)
        h->last_pocs[i] = INT_MIN;
    h->prev_poc_msb = 1 << 16;
    h->prev_frame_num = -1;
    h->x264_build   = -1;
    h->sei_fpa.frame_packing_arrangement_cancel_flag = -1;
    ff_h264_reset_sei(h);
    if (avctx->codec_id == AV_CODEC_ID_H264) {
        if (avctx->ticks_per_frame == 1) {
            if(h->avctx->time_base.den < INT_MAX/2) {
                h->avctx->time_base.den *= 2;
            } else
                h->avctx->time_base.num /= 2;
        }
        avctx->ticks_per_frame = 2;
    }

    if (avctx->extradata_size > 0 && avctx->extradata) {
        ret = ff_h264_decode_extradata(h, avctx->extradata, avctx->extradata_size);
        if (ret < 0) {
            ff_h264_free_context(h);
            return ret;
        }
    }

    if (h->sps.bitstream_restriction_flag &&
        h->avctx->has_b_frames < h->sps.num_reorder_frames) {
        h->avctx->has_b_frames = h->sps.num_reorder_frames;
        h->low_delay           = 0;
    }

    ff_init_cabac_states();
    avctx->internal->allocate_progress = 1;

    return 0;
}

#define IN_RANGE(a, b, size) (((a) >= (b)) && ((a) < ((b) + (size))))
#undef REBASE_PICTURE
#define REBASE_PICTURE(pic, new_ctx, old_ctx)             \
    ((pic && pic >= old_ctx->DPB &&                       \
      pic < old_ctx->DPB + MAX_PICTURE_COUNT) ?           \
     &new_ctx->DPB[pic - old_ctx->DPB] : NULL)

static void copy_picture_range(Picture **to, Picture **from, int count,
                               H264Context *new_base,
                               H264Context *old_base)
{
    int i;

    for (i = 0; i < count; i++) {
        assert((IN_RANGE(from[i], old_base, sizeof(*old_base)) ||
                IN_RANGE(from[i], old_base->DPB,
                         sizeof(Picture) * MAX_PICTURE_COUNT) ||
                !from[i]));
        to[i] = REBASE_PICTURE(from[i], new_base, old_base);
    }
}

static void copy_parameter_set(void **to, void **from, int count, int size)
{
    int i;

    for (i = 0; i < count; i++) {
        if (to[i] && !from[i])
            av_freep(&to[i]);
        else if (from[i] && !to[i])
            to[i] = av_malloc(size);

        if (from[i])
            memcpy(to[i], from[i], size);
    }
}

static int decode_init_thread_copy(AVCodecContext *avctx)
{
    H264Context *h = avctx->priv_data;

    if (!avctx->internal->is_copy)
        return 0;
    memset(h->sps_buffers, 0, sizeof(h->sps_buffers));
    memset(h->pps_buffers, 0, sizeof(h->pps_buffers));

    h->context_initialized = 0;

    return 0;
}

#define copy_fields(to, from, start_field, end_field)                   \
    memcpy(&to->start_field, &from->start_field,                        \
           (char *)&to->end_field - (char *)&to->start_field)

static int h264_slice_header_init(H264Context *, int);

static int h264_set_parameter_from_sps(H264Context *h);

static int decode_update_thread_context(AVCodecContext *dst,
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
        copy_parameter_set((void **)h->sps_buffers, (void **)h1->sps_buffers,
                        MAX_SPS_COUNT, sizeof(SPS));
        h->sps = h1->sps;
        copy_parameter_set((void **)h->pps_buffers, (void **)h1->pps_buffers,
                        MAX_PPS_COUNT, sizeof(PPS));
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

        memcpy(h, h1, offsetof(H264Context, intra_pcm_ptr));
        memcpy(&h->cabac, &h1->cabac,
               sizeof(H264Context) - offsetof(H264Context, cabac));
        av_assert0((void*)&h->cabac == &h->mb_padding + 1);

        memset(h->sps_buffers, 0, sizeof(h->sps_buffers));
        memset(h->pps_buffers, 0, sizeof(h->pps_buffers));

        memset(&h->er, 0, sizeof(h->er));
        memset(&h->me, 0, sizeof(h->me));
        memset(&h->mb, 0, sizeof(h->mb));
        memset(&h->mb_luma_dc, 0, sizeof(h->mb_luma_dc));
        memset(&h->mb_padding, 0, sizeof(h->mb_padding));

        h->avctx             = dst;
        h->DPB               = NULL;
        h->qscale_table_pool = NULL;
        h->mb_type_pool      = NULL;
        h->ref_index_pool    = NULL;
        h->motion_val_pool   = NULL;

        if (h1->context_initialized) {
        h->context_initialized = 0;

        memset(&h->cur_pic, 0, sizeof(h->cur_pic));
        avcodec_get_frame_defaults(&h->cur_pic.f);
        h->cur_pic.tf.f = &h->cur_pic.f;

        ret = ff_h264_alloc_tables(h);
        if (ret < 0) {
            av_log(dst, AV_LOG_ERROR, "Could not allocate memory for h264\n");
            return ret;
        }
        ret = context_init(h);
        if (ret < 0) {
            av_log(dst, AV_LOG_ERROR, "context_init() failed.\n");
            return ret;
        }
        }

        for (i = 0; i < 2; i++) {
            h->rbsp_buffer[i]      = NULL;
            h->rbsp_buffer_size[i] = 0;
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
    h->data_partitioning    = h1->data_partitioning;
    h->low_delay            = h1->low_delay;

    for (i = 0; h->DPB && i < MAX_PICTURE_COUNT; i++) {
        unref_picture(h, &h->DPB[i]);
        if (h1->DPB[i].f.data[0] &&
            (ret = ref_picture(h, &h->DPB[i], &h1->DPB[i])) < 0)
            return ret;
    }

    h->cur_pic_ptr = REBASE_PICTURE(h1->cur_pic_ptr, h, h1);
    unref_picture(h, &h->cur_pic);
    if (h1->cur_pic.f.buf[0] && (ret = ref_picture(h, &h->cur_pic, &h1->cur_pic)) < 0)
        return ret;

    h->workaround_bugs = h1->workaround_bugs;
    h->low_delay       = h1->low_delay;
    h->droppable       = h1->droppable;

    // extradata/NAL handling
    h->is_avc = h1->is_avc;

    // SPS/PPS
    copy_parameter_set((void **)h->sps_buffers, (void **)h1->sps_buffers,
                       MAX_SPS_COUNT, sizeof(SPS));
    h->sps = h1->sps;
    copy_parameter_set((void **)h->pps_buffers, (void **)h1->pps_buffers,
                       MAX_PPS_COUNT, sizeof(PPS));
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

    h->sync            = h1->sync;

    if (context_reinitialized)
        h264_set_parameter_from_sps(h);

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

    return err;
}

static int h264_frame_start(H264Context *h)
{
    Picture *pic;
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
    pic->sync        = 0;
    pic->mmco_reset  = 0;

    if ((ret = alloc_picture(h, pic)) < 0)
        return ret;
    if(!h->sync && !h->avctx->hwaccel &&
       !(h->avctx->codec->capabilities & CODEC_CAP_HWACCEL_VDPAU))
        avpriv_color_frame(&pic->f, c);

    h->cur_pic_ptr = pic;
    unref_picture(h, &h->cur_pic);
    if ((ret = ref_picture(h, &h->cur_pic, h->cur_pic_ptr)) < 0)
        return ret;

    if (CONFIG_ERROR_RESILIENCE) {
        ff_er_frame_start(&h->er);
        h->er.last_pic =
        h->er.next_pic = NULL;
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

/**
 * Run setup operations that must be run after slice header decoding.
 * This includes finding the next displayed frame.
 *
 * @param h h264 master context
 * @param setup_finished enough NALs have been read that we can call
 * ff_thread_finish_setup()
 */
static void decode_postinit(H264Context *h, int setup_finished)
{
    Picture *out = h->cur_pic_ptr;
    Picture *cur = h->cur_pic_ptr;
    int i, pics, out_of_order, out_idx;

    h->cur_pic_ptr->f.pict_type = h->pict_type;

    if (h->next_output_pic)
        return;

    if (cur->field_poc[0] == INT_MAX || cur->field_poc[1] == INT_MAX) {
        /* FIXME: if we have two PAFF fields in one packet, we can't start
         * the next thread here. If we have one field per packet, we can.
         * The check in decode_nal_units() is not good enough to find this
         * yet, so we assume the worst for now. */
        // if (setup_finished)
        //    ff_thread_finish_setup(h->avctx);
        return;
    }

    cur->f.interlaced_frame = 0;
    cur->f.repeat_pict      = 0;

    /* Signal interlacing information externally. */
    /* Prioritize picture timing SEI information over used
     * decoding process if it exists. */

    if (h->sps.pic_struct_present_flag) {
        switch (h->sei_pic_struct) {
        case SEI_PIC_STRUCT_FRAME:
            break;
        case SEI_PIC_STRUCT_TOP_FIELD:
        case SEI_PIC_STRUCT_BOTTOM_FIELD:
            cur->f.interlaced_frame = 1;
            break;
        case SEI_PIC_STRUCT_TOP_BOTTOM:
        case SEI_PIC_STRUCT_BOTTOM_TOP:
            if (FIELD_OR_MBAFF_PICTURE(h))
                cur->f.interlaced_frame = 1;
            else
                // try to flag soft telecine progressive
                cur->f.interlaced_frame = h->prev_interlaced_frame;
            break;
        case SEI_PIC_STRUCT_TOP_BOTTOM_TOP:
        case SEI_PIC_STRUCT_BOTTOM_TOP_BOTTOM:
            /* Signal the possibility of telecined film externally
             * (pic_struct 5,6). From these hints, let the applications
             * decide if they apply deinterlacing. */
            cur->f.repeat_pict = 1;
            break;
        case SEI_PIC_STRUCT_FRAME_DOUBLING:
            cur->f.repeat_pict = 2;
            break;
        case SEI_PIC_STRUCT_FRAME_TRIPLING:
            cur->f.repeat_pict = 4;
            break;
        }

        if ((h->sei_ct_type & 3) &&
            h->sei_pic_struct <= SEI_PIC_STRUCT_BOTTOM_TOP)
            cur->f.interlaced_frame = (h->sei_ct_type & (1 << 1)) != 0;
    } else {
        /* Derive interlacing flag from used decoding process. */
        cur->f.interlaced_frame = FIELD_OR_MBAFF_PICTURE(h);
    }
    h->prev_interlaced_frame = cur->f.interlaced_frame;

    if (cur->field_poc[0] != cur->field_poc[1]) {
        /* Derive top_field_first from field pocs. */
        cur->f.top_field_first = cur->field_poc[0] < cur->field_poc[1];
    } else {
        if (cur->f.interlaced_frame || h->sps.pic_struct_present_flag) {
            /* Use picture timing SEI information. Even if it is a
             * information of a past frame, better than nothing. */
            if (h->sei_pic_struct == SEI_PIC_STRUCT_TOP_BOTTOM ||
                h->sei_pic_struct == SEI_PIC_STRUCT_TOP_BOTTOM_TOP)
                cur->f.top_field_first = 1;
            else
                cur->f.top_field_first = 0;
        } else {
            /* Most likely progressive */
            cur->f.top_field_first = 0;
        }
    }

    cur->mmco_reset = h->mmco_reset;
    h->mmco_reset = 0;
    // FIXME do something with unavailable reference frames

    /* Sort B-frames into display order */

    if (h->sps.bitstream_restriction_flag &&
        h->avctx->has_b_frames < h->sps.num_reorder_frames) {
        h->avctx->has_b_frames = h->sps.num_reorder_frames;
        h->low_delay           = 0;
    }

    if (h->avctx->strict_std_compliance >= FF_COMPLIANCE_STRICT &&
        !h->sps.bitstream_restriction_flag) {
        h->avctx->has_b_frames = MAX_DELAYED_PIC_COUNT - 1;
        h->low_delay           = 0;
    }

    for (i = 0; 1; i++) {
        if(i == MAX_DELAYED_PIC_COUNT || cur->poc < h->last_pocs[i]){
            if(i)
                h->last_pocs[i-1] = cur->poc;
            break;
        } else if(i) {
            h->last_pocs[i-1]= h->last_pocs[i];
        }
    }
    out_of_order = MAX_DELAYED_PIC_COUNT - i;
    if(   cur->f.pict_type == AV_PICTURE_TYPE_B
       || (h->last_pocs[MAX_DELAYED_PIC_COUNT-2] > INT_MIN && h->last_pocs[MAX_DELAYED_PIC_COUNT-1] - h->last_pocs[MAX_DELAYED_PIC_COUNT-2] > 2))
        out_of_order = FFMAX(out_of_order, 1);
    if (out_of_order == MAX_DELAYED_PIC_COUNT) {
        av_log(h->avctx, AV_LOG_VERBOSE, "Invalid POC %d<%d\n", cur->poc, h->last_pocs[0]);
        for (i = 1; i < MAX_DELAYED_PIC_COUNT; i++)
            h->last_pocs[i] = INT_MIN;
        h->last_pocs[0] = cur->poc;
        cur->mmco_reset = 1;
    } else if(h->avctx->has_b_frames < out_of_order && !h->sps.bitstream_restriction_flag){
        av_log(h->avctx, AV_LOG_VERBOSE, "Increasing reorder buffer to %d\n", out_of_order);
        h->avctx->has_b_frames = out_of_order;
        h->low_delay = 0;
    }

    pics = 0;
    while (h->delayed_pic[pics])
        pics++;

    av_assert0(pics <= MAX_DELAYED_PIC_COUNT);

    h->delayed_pic[pics++] = cur;
    if (cur->reference == 0)
        cur->reference = DELAYED_PIC_REF;

    out = h->delayed_pic[0];
    out_idx = 0;
    for (i = 1; h->delayed_pic[i] &&
                !h->delayed_pic[i]->f.key_frame &&
                !h->delayed_pic[i]->mmco_reset;
         i++)
        if (h->delayed_pic[i]->poc < out->poc) {
            out     = h->delayed_pic[i];
            out_idx = i;
        }
    if (h->avctx->has_b_frames == 0 &&
        (h->delayed_pic[0]->f.key_frame || h->delayed_pic[0]->mmco_reset))
        h->next_outputed_poc = INT_MIN;
    out_of_order = out->poc < h->next_outputed_poc;

    if (out_of_order || pics > h->avctx->has_b_frames) {
        out->reference &= ~DELAYED_PIC_REF;
        // for frame threading, the owner must be the second field's thread or
        // else the first thread can release the picture and reuse it unsafely
        for (i = out_idx; h->delayed_pic[i]; i++)
            h->delayed_pic[i] = h->delayed_pic[i + 1];
    }
    if (!out_of_order && pics > h->avctx->has_b_frames) {
        h->next_output_pic = out;
        if (out_idx == 0 && h->delayed_pic[0] && (h->delayed_pic[0]->f.key_frame || h->delayed_pic[0]->mmco_reset)) {
            h->next_outputed_poc = INT_MIN;
        } else
            h->next_outputed_poc = out->poc;
    } else {
        av_log(h->avctx, AV_LOG_DEBUG, "no picture %s\n", out_of_order ? "ooo" : "");
    }

    if (h->next_output_pic && h->next_output_pic->sync) {
        h->sync |= 2;
    }

    if (setup_finished && !h->avctx->hwaccel)
        ff_thread_finish_setup(h->avctx);
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

static av_always_inline void xchg_mb_border(H264Context *h, uint8_t *src_y,
                                            uint8_t *src_cb, uint8_t *src_cr,
                                            int linesize, int uvlinesize,
                                            int xchg, int chroma444,
                                            int simple, int pixel_shift)
{
    int deblock_topleft;
    int deblock_top;
    int top_idx = 1;
    uint8_t *top_border_m1;
    uint8_t *top_border;

    if (!simple && FRAME_MBAFF(h)) {
        if (h->mb_y & 1) {
            if (!MB_MBAFF(h))
                return;
        } else {
            top_idx = MB_MBAFF(h) ? 0 : 1;
        }
    }

    if (h->deblocking_filter == 2) {
        deblock_topleft = h->slice_table[h->mb_xy - 1 - h->mb_stride] == h->slice_num;
        deblock_top     = h->top_type;
    } else {
        deblock_topleft = (h->mb_x > 0);
        deblock_top     = (h->mb_y > !!MB_FIELD(h));
    }

    src_y  -= linesize   + 1 + pixel_shift;
    src_cb -= uvlinesize + 1 + pixel_shift;
    src_cr -= uvlinesize + 1 + pixel_shift;

    top_border_m1 = h->top_borders[top_idx][h->mb_x - 1];
    top_border    = h->top_borders[top_idx][h->mb_x];

#define XCHG(a, b, xchg)                        \
    if (pixel_shift) {                          \
        if (xchg) {                             \
            AV_SWAP64(b + 0, a + 0);            \
            AV_SWAP64(b + 8, a + 8);            \
        } else {                                \
            AV_COPY128(b, a);                   \
        }                                       \
    } else if (xchg)                            \
        AV_SWAP64(b, a);                        \
    else                                        \
        AV_COPY64(b, a);

    if (deblock_top) {
        if (deblock_topleft) {
            XCHG(top_border_m1 + (8 << pixel_shift),
                 src_y - (7 << pixel_shift), 1);
        }
        XCHG(top_border + (0 << pixel_shift), src_y + (1 << pixel_shift), xchg);
        XCHG(top_border + (8 << pixel_shift), src_y + (9 << pixel_shift), 1);
        if (h->mb_x + 1 < h->mb_width) {
            XCHG(h->top_borders[top_idx][h->mb_x + 1],
                 src_y + (17 << pixel_shift), 1);
        }
        if (simple || !CONFIG_GRAY || !(h->flags & CODEC_FLAG_GRAY)) {
            if (chroma444) {
                if (deblock_topleft) {
                    XCHG(top_border_m1 + (24 << pixel_shift), src_cb - (7 << pixel_shift), 1);
                    XCHG(top_border_m1 + (40 << pixel_shift), src_cr - (7 << pixel_shift), 1);
                }
                XCHG(top_border + (16 << pixel_shift), src_cb + (1 << pixel_shift), xchg);
                XCHG(top_border + (24 << pixel_shift), src_cb + (9 << pixel_shift), 1);
                XCHG(top_border + (32 << pixel_shift), src_cr + (1 << pixel_shift), xchg);
                XCHG(top_border + (40 << pixel_shift), src_cr + (9 << pixel_shift), 1);
                if (h->mb_x + 1 < h->mb_width) {
                    XCHG(h->top_borders[top_idx][h->mb_x + 1] + (16 << pixel_shift), src_cb + (17 << pixel_shift), 1);
                    XCHG(h->top_borders[top_idx][h->mb_x + 1] + (32 << pixel_shift), src_cr + (17 << pixel_shift), 1);
                }
            } else {
                if (deblock_topleft) {
                    XCHG(top_border_m1 + (16 << pixel_shift), src_cb - (7 << pixel_shift), 1);
                    XCHG(top_border_m1 + (24 << pixel_shift), src_cr - (7 << pixel_shift), 1);
                }
                XCHG(top_border + (16 << pixel_shift), src_cb + 1 + pixel_shift, 1);
                XCHG(top_border + (24 << pixel_shift), src_cr + 1 + pixel_shift, 1);
            }
        }
    }
}

static av_always_inline int dctcoef_get(int16_t *mb, int high_bit_depth,
                                        int index)
{
    if (high_bit_depth) {
        return AV_RN32A(((int32_t *)mb) + index);
    } else
        return AV_RN16A(mb + index);
}

static av_always_inline void dctcoef_set(int16_t *mb, int high_bit_depth,
                                         int index, int value)
{
    if (high_bit_depth) {
        AV_WN32A(((int32_t *)mb) + index, value);
    } else
        AV_WN16A(mb + index, value);
}

static av_always_inline void hl_decode_mb_predict_luma(H264Context *h,
                                                       int mb_type, int is_h264,
                                                       int simple,
                                                       int transform_bypass,
                                                       int pixel_shift,
                                                       int *block_offset,
                                                       int linesize,
                                                       uint8_t *dest_y, int p)
{
    void (*idct_add)(uint8_t *dst, int16_t *block, int stride);
    void (*idct_dc_add)(uint8_t *dst, int16_t *block, int stride);
    int i;
    int qscale = p == 0 ? h->qscale : h->chroma_qp[p - 1];
    block_offset += 16 * p;
    if (IS_INTRA4x4(mb_type)) {
        if (IS_8x8DCT(mb_type)) {
            if (transform_bypass) {
                idct_dc_add =
                idct_add    = h->h264dsp.h264_add_pixels8_clear;
            } else {
                idct_dc_add = h->h264dsp.h264_idct8_dc_add;
                idct_add    = h->h264dsp.h264_idct8_add;
            }
            for (i = 0; i < 16; i += 4) {
                uint8_t *const ptr = dest_y + block_offset[i];
                const int dir      = h->intra4x4_pred_mode_cache[scan8[i]];
                if (transform_bypass && h->sps.profile_idc == 244 && dir <= 1) {
                    h->hpc.pred8x8l_add[dir](ptr, h->mb + (i * 16 + p * 256 << pixel_shift), linesize);
                } else {
                    const int nnz = h->non_zero_count_cache[scan8[i + p * 16]];
                    h->hpc.pred8x8l[dir](ptr, (h->topleft_samples_available << i) & 0x8000,
                                         (h->topright_samples_available << i) & 0x4000, linesize);
                    if (nnz) {
                        if (nnz == 1 && dctcoef_get(h->mb, pixel_shift, i * 16 + p * 256))
                            idct_dc_add(ptr, h->mb + (i * 16 + p * 256 << pixel_shift), linesize);
                        else
                            idct_add(ptr, h->mb + (i * 16 + p * 256 << pixel_shift), linesize);
                    }
                }
            }
        } else {
            if (transform_bypass) {
                idct_dc_add  =
                idct_add     = h->h264dsp.h264_add_pixels4_clear;
            } else {
                idct_dc_add = h->h264dsp.h264_idct_dc_add;
                idct_add    = h->h264dsp.h264_idct_add;
            }
            for (i = 0; i < 16; i++) {
                uint8_t *const ptr = dest_y + block_offset[i];
                const int dir      = h->intra4x4_pred_mode_cache[scan8[i]];

                if (transform_bypass && h->sps.profile_idc == 244 && dir <= 1) {
                    h->hpc.pred4x4_add[dir](ptr, h->mb + (i * 16 + p * 256 << pixel_shift), linesize);
                } else {
                    uint8_t *topright;
                    int nnz, tr;
                    uint64_t tr_high;
                    if (dir == DIAG_DOWN_LEFT_PRED || dir == VERT_LEFT_PRED) {
                        const int topright_avail = (h->topright_samples_available << i) & 0x8000;
                        av_assert2(h->mb_y || linesize <= block_offset[i]);
                        if (!topright_avail) {
                            if (pixel_shift) {
                                tr_high  = ((uint16_t *)ptr)[3 - linesize / 2] * 0x0001000100010001ULL;
                                topright = (uint8_t *)&tr_high;
                            } else {
                                tr       = ptr[3 - linesize] * 0x01010101u;
                                topright = (uint8_t *)&tr;
                            }
                        } else
                            topright = ptr + (4 << pixel_shift) - linesize;
                    } else
                        topright = NULL;

                    h->hpc.pred4x4[dir](ptr, topright, linesize);
                    nnz = h->non_zero_count_cache[scan8[i + p * 16]];
                    if (nnz) {
                        if (is_h264) {
                            if (nnz == 1 && dctcoef_get(h->mb, pixel_shift, i * 16 + p * 256))
                                idct_dc_add(ptr, h->mb + (i * 16 + p * 256 << pixel_shift), linesize);
                            else
                                idct_add(ptr, h->mb + (i * 16 + p * 256 << pixel_shift), linesize);
                        } else if (CONFIG_SVQ3_DECODER)
                            ff_svq3_add_idct_c(ptr, h->mb + i * 16 + p * 256, linesize, qscale, 0);
                    }
                }
            }
        }
    } else {
        h->hpc.pred16x16[h->intra16x16_pred_mode](dest_y, linesize);
        if (is_h264) {
            if (h->non_zero_count_cache[scan8[LUMA_DC_BLOCK_INDEX + p]]) {
                if (!transform_bypass)
                    h->h264dsp.h264_luma_dc_dequant_idct(h->mb + (p * 256 << pixel_shift),
                                                         h->mb_luma_dc[p],
                                                         h->dequant4_coeff[p][qscale][0]);
                else {
                    static const uint8_t dc_mapping[16] = {
                         0 * 16,  1 * 16,  4 * 16,  5 * 16,
                         2 * 16,  3 * 16,  6 * 16,  7 * 16,
                         8 * 16,  9 * 16, 12 * 16, 13 * 16,
                        10 * 16, 11 * 16, 14 * 16, 15 * 16
                    };
                    for (i = 0; i < 16; i++)
                        dctcoef_set(h->mb + (p * 256 << pixel_shift),
                                    pixel_shift, dc_mapping[i],
                                    dctcoef_get(h->mb_luma_dc[p],
                                                pixel_shift, i));
                }
            }
        } else if (CONFIG_SVQ3_DECODER)
            ff_svq3_luma_dc_dequant_idct_c(h->mb + p * 256,
                                           h->mb_luma_dc[p], qscale);
    }
}

static av_always_inline void hl_decode_mb_idct_luma(H264Context *h, int mb_type,
                                                    int is_h264, int simple,
                                                    int transform_bypass,
                                                    int pixel_shift,
                                                    int *block_offset,
                                                    int linesize,
                                                    uint8_t *dest_y, int p)
{
    void (*idct_add)(uint8_t *dst, int16_t *block, int stride);
    int i;
    block_offset += 16 * p;
    if (!IS_INTRA4x4(mb_type)) {
        if (is_h264) {
            if (IS_INTRA16x16(mb_type)) {
                if (transform_bypass) {
                    if (h->sps.profile_idc == 244 &&
                        (h->intra16x16_pred_mode == VERT_PRED8x8 ||
                         h->intra16x16_pred_mode == HOR_PRED8x8)) {
                        h->hpc.pred16x16_add[h->intra16x16_pred_mode](dest_y, block_offset,
                                                                      h->mb + (p * 256 << pixel_shift),
                                                                      linesize);
                    } else {
                        for (i = 0; i < 16; i++)
                            if (h->non_zero_count_cache[scan8[i + p * 16]] ||
                                dctcoef_get(h->mb, pixel_shift, i * 16 + p * 256))
                                h->h264dsp.h264_add_pixels4_clear(dest_y + block_offset[i],
                                                                  h->mb + (i * 16 + p * 256 << pixel_shift),
                                                                  linesize);
                    }
                } else {
                    h->h264dsp.h264_idct_add16intra(dest_y, block_offset,
                                                    h->mb + (p * 256 << pixel_shift),
                                                    linesize,
                                                    h->non_zero_count_cache + p * 5 * 8);
                }
            } else if (h->cbp & 15) {
                if (transform_bypass) {
                    const int di = IS_8x8DCT(mb_type) ? 4 : 1;
                    idct_add = IS_8x8DCT(mb_type) ? h->h264dsp.h264_add_pixels8_clear
                                                  : h->h264dsp.h264_add_pixels4_clear;
                    for (i = 0; i < 16; i += di)
                        if (h->non_zero_count_cache[scan8[i + p * 16]])
                            idct_add(dest_y + block_offset[i],
                                     h->mb + (i * 16 + p * 256 << pixel_shift),
                                     linesize);
                } else {
                    if (IS_8x8DCT(mb_type))
                        h->h264dsp.h264_idct8_add4(dest_y, block_offset,
                                                   h->mb + (p * 256 << pixel_shift),
                                                   linesize,
                                                   h->non_zero_count_cache + p * 5 * 8);
                    else
                        h->h264dsp.h264_idct_add16(dest_y, block_offset,
                                                   h->mb + (p * 256 << pixel_shift),
                                                   linesize,
                                                   h->non_zero_count_cache + p * 5 * 8);
                }
            }
        } else if (CONFIG_SVQ3_DECODER) {
            for (i = 0; i < 16; i++)
                if (h->non_zero_count_cache[scan8[i + p * 16]] || h->mb[i * 16 + p * 256]) {
                    // FIXME benchmark weird rule, & below
                    uint8_t *const ptr = dest_y + block_offset[i];
                    ff_svq3_add_idct_c(ptr, h->mb + i * 16 + p * 256, linesize,
                                       h->qscale, IS_INTRA(mb_type) ? 1 : 0);
                }
        }
    }
}

#define BITS   8
#define SIMPLE 1
#include "h264_mb_template.c"

#undef  BITS
#define BITS   16
#include "h264_mb_template.c"

#undef  SIMPLE
#define SIMPLE 0
#include "h264_mb_template.c"

void ff_h264_hl_decode_mb(H264Context *h)
{
    const int mb_xy   = h->mb_xy;
    const int mb_type = h->cur_pic.mb_type[mb_xy];
    int is_complex    = CONFIG_SMALL || h->is_complex ||
                        IS_INTRA_PCM(mb_type) || h->qscale == 0;

    if (CHROMA444(h)) {
        if (is_complex || h->pixel_shift)
            hl_decode_mb_444_complex(h);
        else
            hl_decode_mb_444_simple_8(h);
    } else if (is_complex) {
        hl_decode_mb_complex(h);
    } else if (h->pixel_shift) {
        hl_decode_mb_simple_16(h);
    } else
        hl_decode_mb_simple_8(h);
}

static int pred_weight_table(H264Context *h)
{
    int list, i;
    int luma_def, chroma_def;

    h->use_weight             = 0;
    h->use_weight_chroma      = 0;
    h->luma_log2_weight_denom = get_ue_golomb(&h->gb);
    if (h->sps.chroma_format_idc)
        h->chroma_log2_weight_denom = get_ue_golomb(&h->gb);
    luma_def   = 1 << h->luma_log2_weight_denom;
    chroma_def = 1 << h->chroma_log2_weight_denom;

    for (list = 0; list < 2; list++) {
        h->luma_weight_flag[list]   = 0;
        h->chroma_weight_flag[list] = 0;
        for (i = 0; i < h->ref_count[list]; i++) {
            int luma_weight_flag, chroma_weight_flag;

            luma_weight_flag = get_bits1(&h->gb);
            if (luma_weight_flag) {
                h->luma_weight[i][list][0] = get_se_golomb(&h->gb);
                h->luma_weight[i][list][1] = get_se_golomb(&h->gb);
                if (h->luma_weight[i][list][0] != luma_def ||
                    h->luma_weight[i][list][1] != 0) {
                    h->use_weight             = 1;
                    h->luma_weight_flag[list] = 1;
                }
            } else {
                h->luma_weight[i][list][0] = luma_def;
                h->luma_weight[i][list][1] = 0;
            }

            if (h->sps.chroma_format_idc) {
                chroma_weight_flag = get_bits1(&h->gb);
                if (chroma_weight_flag) {
                    int j;
                    for (j = 0; j < 2; j++) {
                        h->chroma_weight[i][list][j][0] = get_se_golomb(&h->gb);
                        h->chroma_weight[i][list][j][1] = get_se_golomb(&h->gb);
                        if (h->chroma_weight[i][list][j][0] != chroma_def ||
                            h->chroma_weight[i][list][j][1] != 0) {
                            h->use_weight_chroma        = 1;
                            h->chroma_weight_flag[list] = 1;
                        }
                    }
                } else {
                    int j;
                    for (j = 0; j < 2; j++) {
                        h->chroma_weight[i][list][j][0] = chroma_def;
                        h->chroma_weight[i][list][j][1] = 0;
                    }
                }
            }
        }
        if (h->slice_type_nos != AV_PICTURE_TYPE_B)
            break;
    }
    h->use_weight = h->use_weight || h->use_weight_chroma;
    return 0;
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
 * instantaneous decoder refresh.
 */
static void idr(H264Context *h)
{
    int i;
    ff_h264_remove_all_refs(h);
    h->prev_frame_num        = 0;
    h->prev_frame_num_offset = 0;
    h->prev_poc_msb          = 1<<16;
    h->prev_poc_lsb          = 0;
    for (i = 0; i < MAX_DELAYED_PIC_COUNT; i++)
        h->last_pocs[i] = INT_MIN;
}

/* forget old pics after a seek */
static void flush_change(H264Context *h)
{
    int i, j;

    h->outputed_poc          = h->next_outputed_poc = INT_MIN;
    h->prev_interlaced_frame = 1;
    idr(h);

    h->prev_frame_num = -1;
    if (h->cur_pic_ptr) {
        h->cur_pic_ptr->reference = 0;
        for (j=i=0; h->delayed_pic[i]; i++)
            if (h->delayed_pic[i] != h->cur_pic_ptr)
                h->delayed_pic[j++] = h->delayed_pic[i];
        h->delayed_pic[j] = NULL;
    }
    h->first_field = 0;
    memset(h->ref_list[0], 0, sizeof(h->ref_list[0]));
    memset(h->ref_list[1], 0, sizeof(h->ref_list[1]));
    memset(h->default_ref_list[0], 0, sizeof(h->default_ref_list[0]));
    memset(h->default_ref_list[1], 0, sizeof(h->default_ref_list[1]));
    ff_h264_reset_sei(h);
    h->recovery_frame= -1;
    h->sync= 0;
    h->list_count = 0;
    h->current_slice = 0;
}

/* forget old pics after a seek */
static void flush_dpb(AVCodecContext *avctx)
{
    H264Context *h = avctx->priv_data;
    int i;

    for (i = 0; i <= MAX_DELAYED_PIC_COUNT; i++) {
        if (h->delayed_pic[i])
            h->delayed_pic[i]->reference = 0;
        h->delayed_pic[i] = NULL;
    }

    flush_change(h);

    if (h->DPB)
        for (i = 0; i < MAX_PICTURE_COUNT; i++)
            unref_picture(h, &h->DPB[i]);
    h->cur_pic_ptr = NULL;
    unref_picture(h, &h->cur_pic);

    h->mb_x = h->mb_y = 0;

    h->parse_context.state             = -1;
    h->parse_context.frame_start_found = 0;
    h->parse_context.overread          = 0;
    h->parse_context.overread_index    = 0;
    h->parse_context.index             = 0;
    h->parse_context.last_index        = 0;
}

int ff_init_poc(H264Context *h, int pic_field_poc[2], int *pic_poc)
{
    const int max_frame_num = 1 << h->sps.log2_max_frame_num;
    int field_poc[2];

    h->frame_num_offset = h->prev_frame_num_offset;
    if (h->frame_num < h->prev_frame_num)
        h->frame_num_offset += max_frame_num;

    if (h->sps.poc_type == 0) {
        const int max_poc_lsb = 1 << h->sps.log2_max_poc_lsb;

        if (h->poc_lsb < h->prev_poc_lsb &&
            h->prev_poc_lsb - h->poc_lsb >= max_poc_lsb / 2)
            h->poc_msb = h->prev_poc_msb + max_poc_lsb;
        else if (h->poc_lsb > h->prev_poc_lsb &&
                 h->prev_poc_lsb - h->poc_lsb < -max_poc_lsb / 2)
            h->poc_msb = h->prev_poc_msb - max_poc_lsb;
        else
            h->poc_msb = h->prev_poc_msb;
        field_poc[0] =
        field_poc[1] = h->poc_msb + h->poc_lsb;
        if (h->picture_structure == PICT_FRAME)
            field_poc[1] += h->delta_poc_bottom;
    } else if (h->sps.poc_type == 1) {
        int abs_frame_num, expected_delta_per_poc_cycle, expectedpoc;
        int i;

        if (h->sps.poc_cycle_length != 0)
            abs_frame_num = h->frame_num_offset + h->frame_num;
        else
            abs_frame_num = 0;

        if (h->nal_ref_idc == 0 && abs_frame_num > 0)
            abs_frame_num--;

        expected_delta_per_poc_cycle = 0;
        for (i = 0; i < h->sps.poc_cycle_length; i++)
            // FIXME integrate during sps parse
            expected_delta_per_poc_cycle += h->sps.offset_for_ref_frame[i];

        if (abs_frame_num > 0) {
            int poc_cycle_cnt          = (abs_frame_num - 1) / h->sps.poc_cycle_length;
            int frame_num_in_poc_cycle = (abs_frame_num - 1) % h->sps.poc_cycle_length;

            expectedpoc = poc_cycle_cnt * expected_delta_per_poc_cycle;
            for (i = 0; i <= frame_num_in_poc_cycle; i++)
                expectedpoc = expectedpoc + h->sps.offset_for_ref_frame[i];
        } else
            expectedpoc = 0;

        if (h->nal_ref_idc == 0)
            expectedpoc = expectedpoc + h->sps.offset_for_non_ref_pic;

        field_poc[0] = expectedpoc + h->delta_poc[0];
        field_poc[1] = field_poc[0] + h->sps.offset_for_top_to_bottom_field;

        if (h->picture_structure == PICT_FRAME)
            field_poc[1] += h->delta_poc[1];
    } else {
        int poc = 2 * (h->frame_num_offset + h->frame_num);

        if (!h->nal_ref_idc)
            poc--;

        field_poc[0] = poc;
        field_poc[1] = poc;
    }

    if (h->picture_structure != PICT_BOTTOM_FIELD)
        pic_field_poc[0] = field_poc[0];
    if (h->picture_structure != PICT_TOP_FIELD)
        pic_field_poc[1] = field_poc[1];
    *pic_poc = FFMIN(pic_field_poc[0], pic_field_poc[1]);

    return 0;
}

/**
 * initialize scan tables
 */
static void init_scan_tables(H264Context *h)
{
    int i;
    for (i = 0; i < 16; i++) {
#define T(x) (x >> 2) | ((x << 2) & 0xF)
        h->zigzag_scan[i] = T(zigzag_scan[i]);
        h->field_scan[i]  = T(field_scan[i]);
#undef T
    }
    for (i = 0; i < 64; i++) {
#define T(x) (x >> 3) | ((x & 7) << 3)
        h->zigzag_scan8x8[i]       = T(ff_zigzag_direct[i]);
        h->zigzag_scan8x8_cavlc[i] = T(zigzag_scan8x8_cavlc[i]);
        h->field_scan8x8[i]        = T(field_scan8x8[i]);
        h->field_scan8x8_cavlc[i]  = T(field_scan8x8_cavlc[i]);
#undef T
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

static int field_end(H264Context *h, int in_setup)
{
    AVCodecContext *const avctx = h->avctx;
    int err = 0;
    h->mb_y = 0;

    if (CONFIG_H264_VDPAU_DECODER &&
        h->avctx->codec->capabilities & CODEC_CAP_HWACCEL_VDPAU)
        ff_vdpau_h264_set_reference_frames(h);

    if (in_setup || !(avctx->active_thread_type & FF_THREAD_FRAME)) {
        if (!h->droppable) {
            err = ff_h264_execute_ref_pic_marking(h, h->mmco, h->mmco_index);
            h->prev_poc_msb = h->poc_msb;
            h->prev_poc_lsb = h->poc_lsb;
        }
        h->prev_frame_num_offset = h->frame_num_offset;
        h->prev_frame_num        = h->frame_num;
        h->outputed_poc          = h->next_outputed_poc;
    }

    if (avctx->hwaccel) {
        if (avctx->hwaccel->end_frame(avctx) < 0)
            av_log(avctx, AV_LOG_ERROR,
                   "hardware accelerator failed to decode picture\n");
    }

    if (CONFIG_H264_VDPAU_DECODER &&
        h->avctx->codec->capabilities & CODEC_CAP_HWACCEL_VDPAU)
        ff_vdpau_h264_picture_complete(h);

    /*
     * FIXME: Error handling code does not seem to support interlaced
     * when slices span multiple rows
     * The ff_er_add_slice calls don't work right for bottom
     * fields; they cause massive erroneous error concealing
     * Error marking covers both fields (top and bottom).
     * This causes a mismatched s->error_count
     * and a bad error table. Further, the error count goes to
     * INT_MAX when called for bottom field, because mb_y is
     * past end by one (callers fault) and resync_mb_y != 0
     * causes problems for the first MB line, too.
     */
    if (CONFIG_ERROR_RESILIENCE &&
        !FIELD_PICTURE(h) && h->current_slice && !h->sps.new) {
        h->er.cur_pic  = h->cur_pic_ptr;
        ff_er_frame_end(&h->er);
    }
    if (!in_setup && !h->droppable)
        ff_thread_report_progress(&h->cur_pic_ptr->tf, INT_MAX,
                                  h->picture_structure == PICT_BOTTOM_FIELD);
    emms_c();

    h->current_slice = 0;

    return err;
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

/**
 * Compute profile from profile_idc and constraint_set?_flags.
 *
 * @param sps SPS
 *
 * @return profile as defined by FF_PROFILE_H264_*
 */
int ff_h264_get_profile(SPS *sps)
{
    int profile = sps->profile_idc;

    switch (sps->profile_idc) {
    case FF_PROFILE_H264_BASELINE:
        // constraint_set1_flag set to 1
        profile |= (sps->constraint_set_flags & 1 << 1) ? FF_PROFILE_H264_CONSTRAINED : 0;
        break;
    case FF_PROFILE_H264_HIGH_10:
    case FF_PROFILE_H264_HIGH_422:
    case FF_PROFILE_H264_HIGH_444_PREDICTIVE:
        // constraint_set3_flag set to 1
        profile |= (sps->constraint_set_flags & 1 << 3) ? FF_PROFILE_H264_INTRA : 0;
        break;
    }

    return profile;
}

static int h264_set_parameter_from_sps(H264Context *h)
{
    if (h->flags & CODEC_FLAG_LOW_DELAY ||
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

    if (h->sps.bit_depth_luma != h->sps.bit_depth_chroma) {
        avpriv_request_sample(h->avctx,
                              "Different chroma and luma bit depth");
        return AVERROR_PATCHWELCOME;
    }

    if (h->avctx->bits_per_raw_sample != h->sps.bit_depth_luma ||
        h->cur_chroma_format_idc      != h->sps.chroma_format_idc) {
        if (h->avctx->codec &&
            h->avctx->codec->capabilities & CODEC_CAP_HWACCEL_VDPAU &&
            (h->sps.bit_depth_luma != 8 || h->sps.chroma_format_idc > 1)) {
            av_log(h->avctx, AV_LOG_ERROR,
                   "VDPAU decoding does not support video colorspace.\n");
            return AVERROR_INVALIDDATA;
        }
        if (h->sps.bit_depth_luma >= 8 && h->sps.bit_depth_luma <= 14 &&
            h->sps.bit_depth_luma != 11 && h->sps.bit_depth_luma != 13) {
            h->avctx->bits_per_raw_sample = h->sps.bit_depth_luma;
            h->cur_chroma_format_idc      = h->sps.chroma_format_idc;
            h->pixel_shift                = h->sps.bit_depth_luma > 8;

            ff_h264dsp_init(&h->h264dsp, h->sps.bit_depth_luma,
                            h->sps.chroma_format_idc);
            ff_h264chroma_init(&h->h264chroma, h->sps.bit_depth_chroma);
            ff_h264qpel_init(&h->h264qpel, h->sps.bit_depth_luma);
            ff_h264_pred_init(&h->hpc, h->avctx->codec_id, h->sps.bit_depth_luma,
                              h->sps.chroma_format_idc);

            if (CONFIG_ERROR_RESILIENCE)
                ff_dsputil_init(&h->dsp, h->avctx);
            ff_videodsp_init(&h->vdsp, h->sps.bit_depth_luma);
        } else {
            av_log(h->avctx, AV_LOG_ERROR, "Unsupported bit depth: %d\n",
                   h->sps.bit_depth_luma);
            return AVERROR_INVALIDDATA;
        }
    }
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
               "Unsupported bit depth: %d\n", h->sps.bit_depth_luma);
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

    h->avctx->hwaccel = ff_find_hwaccel(h->avctx->codec->id, h->avctx->pix_fmt);

    if (reinit)
        free_tables(h, 0);
    h->first_field           = 0;
    h->prev_interlaced_frame = 1;

    init_scan_tables(h);
    ret = ff_h264_alloc_tables(h);
    if (ret < 0) {
        av_log(h->avctx, AV_LOG_ERROR,
               "Could not allocate memory for h264\n");
        return ret;
    }

    if (nb_slices > MAX_THREADS || (nb_slices > h->mb_height && h->mb_height)) {
        int max_slices;
        if (h->mb_height)
            max_slices = FFMIN(MAX_THREADS, h->mb_height);
        else
            max_slices = MAX_THREADS;
        av_log(h->avctx, AV_LOG_WARNING, "too many threads/slices (%d),"
               " reducing to %d\n", nb_slices, max_slices);
        nb_slices = max_slices;
    }
    h->slice_context_count = nb_slices;

    if (!HAVE_THREADS || !(h->avctx->active_thread_type & FF_THREAD_SLICE)) {
        ret = context_init(h);
        if (ret < 0) {
            av_log(h->avctx, AV_LOG_ERROR, "context_init() failed.\n");
            return ret;
        }
    } else {
        for (i = 1; i < h->slice_context_count; i++) {
            H264Context *c;
            c                    = h->thread_context[i] = av_mallocz(sizeof(H264Context));
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
            c->chroma_x_shift = h->chroma_x_shift;
            c->chroma_y_shift = h->chroma_y_shift;
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
            if ((ret = context_init(h->thread_context[i])) < 0) {
                av_log(h->avctx, AV_LOG_ERROR, "context_init() failed.\n");
                return ret;
            }
    }

    h->context_initialized = 1;

    return 0;
}

/**
 * Decode a slice header.
 * This will also call ff_MPV_common_init() and frame_start() as needed.
 *
 * @param h h264context
 * @param h0 h264 master context (differs from 'h' when doing sliced based
 *           parallel decoding)
 *
 * @return 0 if okay, <0 if an error occurred, 1 if decoding must not be multithreaded
 */
static int decode_slice_header(H264Context *h, H264Context *h0)
{
    unsigned int first_mb_in_slice;
    unsigned int pps_id;
    int num_ref_idx_active_override_flag, ret;
    unsigned int slice_type, tmp, i, j;
    int last_pic_structure, last_pic_droppable;
    int must_reinit;
    int needs_reinit = 0;
    int field_pic_flag, bottom_field_flag;

    h->me.qpel_put = h->h264qpel.put_h264_qpel_pixels_tab;
    h->me.qpel_avg = h->h264qpel.avg_h264_qpel_pixels_tab;

    first_mb_in_slice = get_ue_golomb_long(&h->gb);

    if (first_mb_in_slice == 0) { // FIXME better field boundary detection
        if (h0->current_slice && FIELD_PICTURE(h)) {
            field_end(h, 1);
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
               "slice type too large (%d) at %d %d\n",
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

    // to make a few old functions happy, it's wrong though
    h->pict_type = h->slice_type;

    pps_id = get_ue_golomb(&h->gb);
    if (pps_id >= MAX_PPS_COUNT) {
        av_log(h->avctx, AV_LOG_ERROR, "pps_id %d out of range\n", pps_id);
        return AVERROR_INVALIDDATA;
    }
    if (!h0->pps_buffers[pps_id]) {
        av_log(h->avctx, AV_LOG_ERROR,
               "non-existing PPS %u referenced\n",
               pps_id);
        return AVERROR_INVALIDDATA;
    }
    h->pps = *h0->pps_buffers[pps_id];

    if (!h0->sps_buffers[h->pps.sps_id]) {
        av_log(h->avctx, AV_LOG_ERROR,
               "non-existing SPS %u referenced\n",
               h->pps.sps_id);
        return AVERROR_INVALIDDATA;
    }

    if (h->pps.sps_id != h->current_sps_id ||
        h0->sps_buffers[h->pps.sps_id]->new) {
        h0->sps_buffers[h->pps.sps_id]->new = 0;

        h->current_sps_id = h->pps.sps_id;
        h->sps            = *h0->sps_buffers[h->pps.sps_id];

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
        if ((ret = h264_set_parameter_from_sps(h)) < 0)
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
    if (h0->avctx->pix_fmt != get_pixel_format(h0, 0))
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
            av_log(h->avctx, AV_LOG_ERROR, "changing width/height on "
                   "slice %d\n", h0->current_slice + 1);
            return AVERROR_INVALIDDATA;
        }

        flush_change(h);

        if ((ret = get_pixel_format(h, 1)) < 0)
            return ret;
        h->avctx->pix_fmt = ret;

        av_log(h->avctx, AV_LOG_INFO, "Reinit context to %dx%d, "
               "pix_fmt: %d\n", h->width, h->height, h->avctx->pix_fmt);

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
        init_dequant_tables(h);
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
                   "unset cur_pic_ptr on %d. slice\n",
                   h0->current_slice + 1);
            return AVERROR_INVALIDDATA;
        }
    } else {
        /* Shorten frame num gaps so we don't have to allocate reference
         * frames just to throw them away */
        if (h->frame_num != h->prev_frame_num && h->prev_frame_num >= 0) {
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
            assert(h0->cur_pic_ptr->f.data[0]);
            assert(h0->cur_pic_ptr->reference != DELAYED_PIC_REF);

            /* Mark old field/frame as completed */
            if (!last_pic_droppable && h0->cur_pic_ptr->tf.owner == h0->avctx) {
                ff_thread_report_progress(&h0->cur_pic_ptr->tf, INT_MAX,
                                          last_pic_structure == PICT_BOTTOM_FIELD);
            }

            /* figure out if we have a complementary field pair */
            if (!FIELD_PICTURE(h) || h->picture_structure == last_pic_structure) {
                /* Previous field is unmatched. Don't display it, but let it
                 * remain for reference if marked as such. */
                if (!last_pic_droppable && last_pic_structure != PICT_FRAME) {
                    ff_thread_report_progress(&h0->cur_pic_ptr->tf, INT_MAX,
                                              last_pic_structure == PICT_TOP_FIELD);
                }
            } else {
                if (h0->cur_pic_ptr->frame_num != h->frame_num) {
                    /* This and previous field were reference, but had
                     * different frame_nums. Consider this field first in
                     * pair. Throw away previous field except for reference
                     * purposes. */
                    if (!last_pic_droppable && last_pic_structure != PICT_FRAME) {
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

        while (h->frame_num != h->prev_frame_num && h->prev_frame_num >= 0 && !h0->first_field &&
               h->frame_num != (h->prev_frame_num + 1) % (1 << h->sps.log2_max_frame_num)) {
            Picture *prev = h->short_ref_count ? h->short_ref[0] : NULL;
            av_log(h->avctx, AV_LOG_DEBUG, "Frame num gap %d %d\n",
                   h->frame_num, h->prev_frame_num);
            if (!h->sps.gaps_in_frame_num_allowed_flag)
                for(i=0; i<FF_ARRAY_ELEMS(h->last_pocs); i++)
                    h->last_pocs[i] = INT_MIN;
            ret = h264_frame_start(h);
            if (ret < 0)
                return ret;
            h->prev_frame_num++;
            h->prev_frame_num        %= 1 << h->sps.log2_max_frame_num;
            h->cur_pic_ptr->frame_num = h->prev_frame_num;
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
            assert(h0->cur_pic_ptr->f.data[0]);
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

    // set defaults, might be overridden a few lines later
    h->ref_count[0] = h->pps.ref_count[0];
    h->ref_count[1] = h->pps.ref_count[1];

    if (h->slice_type_nos != AV_PICTURE_TYPE_I) {
        unsigned max[2];
        max[0] = max[1] = h->picture_structure == PICT_FRAME ? 15 : 31;

        if (h->slice_type_nos == AV_PICTURE_TYPE_B)
            h->direct_spatial_mv_pred = get_bits1(&h->gb);
        num_ref_idx_active_override_flag = get_bits1(&h->gb);

        if (num_ref_idx_active_override_flag) {
            h->ref_count[0] = get_ue_golomb(&h->gb) + 1;
            if (h->slice_type_nos == AV_PICTURE_TYPE_B) {
                h->ref_count[1] = get_ue_golomb(&h->gb) + 1;
            } else
                // full range is spec-ok in this case, even for frames
                h->ref_count[1] = 1;
        }

        if (h->ref_count[0]-1 > max[0] || h->ref_count[1]-1 > max[1]){
            av_log(h->avctx, AV_LOG_ERROR, "reference overflow %u > %u or %u > %u\n", h->ref_count[0]-1, max[0], h->ref_count[1]-1, max[1]);
            h->ref_count[0] = h->ref_count[1] = 0;
            return AVERROR_INVALIDDATA;
        }

        if (h->slice_type_nos == AV_PICTURE_TYPE_B)
            h->list_count = 2;
        else
            h->list_count = 1;
    } else {
        h->list_count   = 0;
        h->ref_count[0] = h->ref_count[1] = 0;
    }
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
        pred_weight_table(h);
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
            av_log(h->avctx, AV_LOG_ERROR, "cabac_init_idc overflow\n");
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
    h->slice_alpha_c0_offset = 52;
    h->slice_beta_offset     = 52;
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
            h->slice_alpha_c0_offset += get_se_golomb(&h->gb) << 1;
            h->slice_beta_offset     += get_se_golomb(&h->gb) << 1;
            if (h->slice_alpha_c0_offset > 104U ||
                h->slice_beta_offset     > 104U) {
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
    h->qp_thresh = 15 + 52 -
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

    if (h->ref_count[0]) h->er.last_pic = &h->ref_list[0][0];
    if (h->ref_count[1]) h->er.next_pic = &h->ref_list[1][0];

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
               h->slice_alpha_c0_offset / 2 - 26, h->slice_beta_offset / 2 - 26,
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

        er->ref_count = h->ref_count[0];
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
                       "error while decoding MB %d %d, bytestream (%td)\n",
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
static int execute_decode_slices(H264Context *h, int context_count)
{
    AVCodecContext *const avctx = h->avctx;
    H264Context *hx;
    int i;

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

static const uint8_t start_code[] = { 0x00, 0x00, 0x01 };

static int decode_nal_units(H264Context *h, const uint8_t *buf, int buf_size,
                            int parse_extradata)
{
    AVCodecContext *const avctx = h->avctx;
    H264Context *hx; ///< thread context
    int buf_index;
    int context_count;
    int next_avc;
    int pass = !(avctx->active_thread_type & FF_THREAD_FRAME);
    int nals_needed = 0; ///< number of NALs that need decoding before the next frame thread starts
    int nal_index;
    int idr_cleared=0;
    int first_slice = 0;
    int ret = 0;

    h->nal_unit_type= 0;

    if(!h->slice_context_count)
         h->slice_context_count= 1;
    h->max_contexts = h->slice_context_count;
    if (!(avctx->flags2 & CODEC_FLAG2_CHUNKS)) {
        h->current_slice = 0;
        if (!h->first_field)
            h->cur_pic_ptr = NULL;
        ff_h264_reset_sei(h);
    }

    if (h->nal_length_size == 4) {
        if (buf_size > 8 && AV_RB32(buf) == 1 && AV_RB32(buf+5) > (unsigned)buf_size) {
            h->is_avc = 0;
        }else if(buf_size > 3 && AV_RB32(buf) > 1 && AV_RB32(buf) <= (unsigned)buf_size)
            h->is_avc = 1;
    }

    for (; pass <= 1; pass++) {
        buf_index     = 0;
        context_count = 0;
        next_avc      = h->is_avc ? 0 : buf_size;
        nal_index     = 0;
        for (;;) {
            int consumed;
            int dst_length;
            int bit_length;
            const uint8_t *ptr;
            int i, nalsize = 0;
            int err;

            if (buf_index >= next_avc) {
                if (buf_index >= buf_size - h->nal_length_size)
                    break;
                nalsize = 0;
                for (i = 0; i < h->nal_length_size; i++)
                    nalsize = (nalsize << 8) | buf[buf_index++];
                if (nalsize <= 0 || nalsize > buf_size - buf_index) {
                    av_log(h->avctx, AV_LOG_ERROR,
                           "AVC: nal size %d\n", nalsize);
                    break;
                }
                next_avc = buf_index + nalsize;
            } else {
                // start code prefix search
                for (; buf_index + 3 < next_avc; buf_index++)
                    // This should always succeed in the first iteration.
                    if (buf[buf_index]     == 0 &&
                        buf[buf_index + 1] == 0 &&
                        buf[buf_index + 2] == 1)
                        break;

                if (buf_index + 3 >= buf_size) {
                    buf_index = buf_size;
                    break;
                }

                buf_index += 3;
                if (buf_index >= next_avc)
                    continue;
            }

            hx = h->thread_context[context_count];

            ptr = ff_h264_decode_nal(hx, buf + buf_index, &dst_length,
                                     &consumed, next_avc - buf_index);
            if (ptr == NULL || dst_length < 0) {
                ret = -1;
                goto end;
            }
            i = buf_index + consumed;
            if ((h->workaround_bugs & FF_BUG_AUTODETECT) && i + 3 < next_avc &&
                buf[i]     == 0x00 && buf[i + 1] == 0x00 &&
                buf[i + 2] == 0x01 && buf[i + 3] == 0xE0)
                h->workaround_bugs |= FF_BUG_TRUNCATED;

            if (!(h->workaround_bugs & FF_BUG_TRUNCATED))
                while(dst_length > 0 && ptr[dst_length - 1] == 0)
                    dst_length--;
            bit_length = !dst_length ? 0
                                     : (8 * dst_length -
                                        decode_rbsp_trailing(h, ptr + dst_length - 1));

            if (h->avctx->debug & FF_DEBUG_STARTCODE)
                av_log(h->avctx, AV_LOG_DEBUG, "NAL %d/%d at %d/%d length %d pass %d\n", hx->nal_unit_type, hx->nal_ref_idc, buf_index, buf_size, dst_length, pass);

            if (h->is_avc && (nalsize != consumed) && nalsize)
                av_log(h->avctx, AV_LOG_DEBUG,
                       "AVC: Consumed only %d bytes instead of %d\n",
                       consumed, nalsize);

            buf_index += consumed;
            nal_index++;

            if (pass == 0) {
                /* packets can sometimes contain multiple PPS/SPS,
                 * e.g. two PAFF field pictures in one packet, or a demuxer
                 * which splits NALs strangely if so, when frame threading we
                 * can't start the next thread until we've read all of them */
                switch (hx->nal_unit_type) {
                case NAL_SPS:
                case NAL_PPS:
                    nals_needed = nal_index;
                    break;
                case NAL_DPA:
                case NAL_IDR_SLICE:
                case NAL_SLICE:
                    init_get_bits(&hx->gb, ptr, bit_length);
                    if (!get_ue_golomb(&hx->gb) || !first_slice)
                        nals_needed = nal_index;
                    if (!first_slice)
                        first_slice = hx->nal_unit_type;
                }
                continue;
            }

            if (!first_slice)
                switch (hx->nal_unit_type) {
                case NAL_DPA:
                case NAL_IDR_SLICE:
                case NAL_SLICE:
                    first_slice = hx->nal_unit_type;
                }

            // FIXME do not discard SEI id
            if (avctx->skip_frame >= AVDISCARD_NONREF && h->nal_ref_idc == 0)
                continue;

again:
            /* Ignore per frame NAL unit type during extradata
             * parsing. Decoding slices is not possible in codec init
             * with frame-mt */
            if (parse_extradata) {
                switch (hx->nal_unit_type) {
                case NAL_IDR_SLICE:
                case NAL_SLICE:
                case NAL_DPA:
                case NAL_DPB:
                case NAL_DPC:
                case NAL_AUXILIARY_SLICE:
                    av_log(h->avctx, AV_LOG_WARNING, "Ignoring NAL %d in global header/extradata\n", hx->nal_unit_type);
                    hx->nal_unit_type = NAL_FF_IGNORE;
                }
            }

            err = 0;

            switch (hx->nal_unit_type) {
            case NAL_IDR_SLICE:
                if (first_slice != NAL_IDR_SLICE) {
                    av_log(h->avctx, AV_LOG_ERROR,
                           "Invalid mix of idr and non-idr slices\n");
                    ret = -1;
                    goto end;
                }
                if(!idr_cleared)
                    idr(h); // FIXME ensure we don't lose some frames if there is reordering
                idr_cleared = 1;
            case NAL_SLICE:
                init_get_bits(&hx->gb, ptr, bit_length);
                hx->intra_gb_ptr      =
                hx->inter_gb_ptr      = &hx->gb;
                hx->data_partitioning = 0;

                if ((err = decode_slice_header(hx, h)))
                    break;

                if (h->sei_recovery_frame_cnt >= 0 && (h->frame_num != h->sei_recovery_frame_cnt || hx->slice_type_nos != AV_PICTURE_TYPE_I))
                    h->valid_recovery_point = 1;

                if (   h->sei_recovery_frame_cnt >= 0
                    && (   h->recovery_frame<0
                        || ((h->recovery_frame - h->frame_num) & ((1 << h->sps.log2_max_frame_num)-1)) > h->sei_recovery_frame_cnt)) {
                    h->recovery_frame = (h->frame_num + h->sei_recovery_frame_cnt) %
                                        (1 << h->sps.log2_max_frame_num);

                    if (!h->valid_recovery_point)
                        h->recovery_frame = h->frame_num;
                }

                h->cur_pic_ptr->f.key_frame |=
                        (hx->nal_unit_type == NAL_IDR_SLICE);

                if (h->recovery_frame == h->frame_num) {
                    h->cur_pic_ptr->sync |= 1;
                    h->recovery_frame = -1;
                }

                h->sync |= !!h->cur_pic_ptr->f.key_frame;
                h->sync |= 3*!!(avctx->flags2 & CODEC_FLAG2_SHOW_ALL);
                h->cur_pic_ptr->sync |= h->sync;

                if (h->current_slice == 1) {
                    if (!(avctx->flags2 & CODEC_FLAG2_CHUNKS))
                        decode_postinit(h, nal_index >= nals_needed);

                    if (h->avctx->hwaccel &&
                        (ret = h->avctx->hwaccel->start_frame(h->avctx, NULL, 0)) < 0)
                        return ret;
                    if (CONFIG_H264_VDPAU_DECODER &&
                        h->avctx->codec->capabilities & CODEC_CAP_HWACCEL_VDPAU)
                        ff_vdpau_h264_picture_start(h);
                }

                if (hx->redundant_pic_count == 0 &&
                    (avctx->skip_frame < AVDISCARD_NONREF ||
                     hx->nal_ref_idc) &&
                    (avctx->skip_frame < AVDISCARD_BIDIR  ||
                     hx->slice_type_nos != AV_PICTURE_TYPE_B) &&
                    (avctx->skip_frame < AVDISCARD_NONKEY ||
                     hx->slice_type_nos == AV_PICTURE_TYPE_I) &&
                    avctx->skip_frame < AVDISCARD_ALL) {
                    if (avctx->hwaccel) {
                        ret = avctx->hwaccel->decode_slice(avctx,
                                                           &buf[buf_index - consumed],
                                                           consumed);
                        if (ret < 0)
                            return ret;
                    } else if (CONFIG_H264_VDPAU_DECODER &&
                               h->avctx->codec->capabilities & CODEC_CAP_HWACCEL_VDPAU) {
                        ff_vdpau_add_data_chunk(h->cur_pic_ptr->f.data[0],
                                                start_code,
                                                sizeof(start_code));
                        ff_vdpau_add_data_chunk(h->cur_pic_ptr->f.data[0],
                                                &buf[buf_index - consumed],
                                                consumed);
                    } else
                        context_count++;
                }
                break;
            case NAL_DPA:
                init_get_bits(&hx->gb, ptr, bit_length);
                hx->intra_gb_ptr =
                hx->inter_gb_ptr = NULL;

                if ((err = decode_slice_header(hx, h)) < 0)
                    break;

                hx->data_partitioning = 1;
                break;
            case NAL_DPB:
                init_get_bits(&hx->intra_gb, ptr, bit_length);
                hx->intra_gb_ptr = &hx->intra_gb;
                break;
            case NAL_DPC:
                init_get_bits(&hx->inter_gb, ptr, bit_length);
                hx->inter_gb_ptr = &hx->inter_gb;

                av_log(h->avctx, AV_LOG_ERROR, "Partitioned H.264 support is incomplete\n");
                break;

                if (hx->redundant_pic_count == 0 &&
                    hx->intra_gb_ptr &&
                    hx->data_partitioning &&
                    h->cur_pic_ptr && h->context_initialized &&
                    (avctx->skip_frame < AVDISCARD_NONREF || hx->nal_ref_idc) &&
                    (avctx->skip_frame < AVDISCARD_BIDIR  ||
                     hx->slice_type_nos != AV_PICTURE_TYPE_B) &&
                    (avctx->skip_frame < AVDISCARD_NONKEY ||
                     hx->slice_type_nos == AV_PICTURE_TYPE_I) &&
                    avctx->skip_frame < AVDISCARD_ALL)
                    context_count++;
                break;
            case NAL_SEI:
                init_get_bits(&h->gb, ptr, bit_length);
                ff_h264_decode_sei(h);
                break;
            case NAL_SPS:
                init_get_bits(&h->gb, ptr, bit_length);
                if (ff_h264_decode_seq_parameter_set(h) < 0 && (h->is_avc ? nalsize : 1)) {
                    av_log(h->avctx, AV_LOG_DEBUG,
                           "SPS decoding failure, trying again with the complete NAL\n");
                    if (h->is_avc)
                        av_assert0(next_avc - buf_index + consumed == nalsize);
                    if ((next_avc - buf_index + consumed - 1) >= INT_MAX/8)
                        break;
                    init_get_bits(&h->gb, &buf[buf_index + 1 - consumed],
                                  8*(next_avc - buf_index + consumed - 1));
                    ff_h264_decode_seq_parameter_set(h);
                }

                break;
            case NAL_PPS:
                init_get_bits(&h->gb, ptr, bit_length);
                ff_h264_decode_picture_parameter_set(h, bit_length);
                break;
            case NAL_AUD:
            case NAL_END_SEQUENCE:
            case NAL_END_STREAM:
            case NAL_FILLER_DATA:
            case NAL_SPS_EXT:
            case NAL_AUXILIARY_SLICE:
                break;
            case NAL_FF_IGNORE:
                break;
            default:
                av_log(avctx, AV_LOG_DEBUG, "Unknown NAL code: %d (%d bits)\n",
                       hx->nal_unit_type, bit_length);
            }

            if (context_count == h->max_contexts) {
                execute_decode_slices(h, context_count);
                context_count = 0;
            }

            if (err < 0)
                av_log(h->avctx, AV_LOG_ERROR, "decode_slice_header error\n");
            else if (err == 1) {
                /* Slice could not be decoded in parallel mode, copy down
                 * NAL unit stuff to context 0 and restart. Note that
                 * rbsp_buffer is not transferred, but since we no longer
                 * run in parallel mode this should not be an issue. */
                h->nal_unit_type = hx->nal_unit_type;
                h->nal_ref_idc   = hx->nal_ref_idc;
                hx               = h;
                goto again;
            }
        }
    }
    if (context_count)
        execute_decode_slices(h, context_count);

end:
    /* clean up */
    if (h->cur_pic_ptr && !h->droppable) {
        ff_thread_report_progress(&h->cur_pic_ptr->tf, INT_MAX,
                                  h->picture_structure == PICT_BOTTOM_FIELD);
    }

    return (ret < 0) ? ret : buf_index;
}

/**
 * Return the number of bytes consumed for building the current frame.
 */
static int get_consumed_bytes(int pos, int buf_size)
{
    if (pos == 0)
        pos = 1;          // avoid infinite loops (i doubt that is needed but ...)
    if (pos + 10 > buf_size)
        pos = buf_size;                   // oops ;)

    return pos;
}

static int output_frame(H264Context *h, AVFrame *dst, Picture *srcp)
{
    AVFrame *src = &srcp->f;
    int i;
    int ret = av_frame_ref(dst, src);
    if (ret < 0)
        return ret;

    av_dict_set(&dst->metadata, "stereo_mode", ff_h264_sei_stereo_mode(h), 0);

    if (!srcp->crop)
        return 0;

    for (i = 0; i < 3; i++) {
        int hshift = (i > 0) ? h->chroma_x_shift : 0;
        int vshift = (i > 0) ? h->chroma_y_shift : 0;
        int off    = ((srcp->crop_left >> hshift) << h->pixel_shift) +
                      (srcp->crop_top  >> vshift) * dst->linesize[i];
        dst->data[i] += off;
    }
    return 0;
}

static int decode_frame(AVCodecContext *avctx, void *data,
                        int *got_frame, AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size       = avpkt->size;
    H264Context *h     = avctx->priv_data;
    AVFrame *pict      = data;
    int buf_index      = 0;
    Picture *out;
    int i, out_idx;
    int ret;

    h->flags = avctx->flags;

    /* end of stream, output what is still in the buffers */
    if (buf_size == 0) {
 out:

        h->cur_pic_ptr = NULL;
        h->first_field = 0;

        // FIXME factorize this with the output code below
        out     = h->delayed_pic[0];
        out_idx = 0;
        for (i = 1;
             h->delayed_pic[i] &&
             !h->delayed_pic[i]->f.key_frame &&
             !h->delayed_pic[i]->mmco_reset;
             i++)
            if (h->delayed_pic[i]->poc < out->poc) {
                out     = h->delayed_pic[i];
                out_idx = i;
            }

        for (i = out_idx; h->delayed_pic[i]; i++)
            h->delayed_pic[i] = h->delayed_pic[i + 1];

        if (out) {
            out->reference &= ~DELAYED_PIC_REF;
            ret = output_frame(h, pict, out);
            if (ret < 0)
                return ret;
            *got_frame = 1;
        }

        return buf_index;
    }
    if(h->is_avc && buf_size >= 9 && buf[0]==1 && buf[2]==0 && (buf[4]&0xFC)==0xFC && (buf[5]&0x1F) && buf[8]==0x67){
        int cnt= buf[5]&0x1f;
        const uint8_t *p= buf+6;
        while(cnt--){
            int nalsize= AV_RB16(p) + 2;
            if(nalsize > buf_size - (p-buf) || p[2]!=0x67)
                goto not_extra;
            p += nalsize;
        }
        cnt = *(p++);
        if(!cnt)
            goto not_extra;
        while(cnt--){
            int nalsize= AV_RB16(p) + 2;
            if(nalsize > buf_size - (p-buf) || p[2]!=0x68)
                goto not_extra;
            p += nalsize;
        }

        return ff_h264_decode_extradata(h, buf, buf_size);
    }
not_extra:

    buf_index = decode_nal_units(h, buf, buf_size, 0);
    if (buf_index < 0)
        return AVERROR_INVALIDDATA;

    if (!h->cur_pic_ptr && h->nal_unit_type == NAL_END_SEQUENCE) {
        av_assert0(buf_index <= buf_size);
        goto out;
    }

    if (!(avctx->flags2 & CODEC_FLAG2_CHUNKS) && !h->cur_pic_ptr) {
        if (avctx->skip_frame >= AVDISCARD_NONREF ||
            buf_size >= 4 && !memcmp("Q264", buf, 4))
            return buf_size;
        av_log(avctx, AV_LOG_ERROR, "no frame!\n");
        return AVERROR_INVALIDDATA;
    }

    if (!(avctx->flags2 & CODEC_FLAG2_CHUNKS) ||
        (h->mb_y >= h->mb_height && h->mb_height)) {
        if (avctx->flags2 & CODEC_FLAG2_CHUNKS)
            decode_postinit(h, 1);

        field_end(h, 0);

        /* Wait for second field. */
        *got_frame = 0;
        if (h->next_output_pic && (h->next_output_pic->sync || h->sync>1)) {
            ret = output_frame(h, pict, h->next_output_pic);
            if (ret < 0)
                return ret;
            *got_frame = 1;
            if (CONFIG_MPEGVIDEO) {
                ff_print_debug_info2(h->avctx, h->next_output_pic, pict, h->er.mbskip_table,
                                    &h->low_delay,
                                    h->mb_width, h->mb_height, h->mb_stride, 1);
            }
        }
    }

    assert(pict->data[0] || !*got_frame);

    return get_consumed_bytes(buf_index, buf_size);
}

av_cold void ff_h264_free_context(H264Context *h)
{
    int i;

    free_tables(h, 1); // FIXME cleanup init stuff perhaps

    for (i = 0; i < MAX_SPS_COUNT; i++)
        av_freep(h->sps_buffers + i);

    for (i = 0; i < MAX_PPS_COUNT; i++)
        av_freep(h->pps_buffers + i);
}

static av_cold int h264_decode_end(AVCodecContext *avctx)
{
    H264Context *h = avctx->priv_data;

    ff_h264_remove_all_refs(h);
    ff_h264_free_context(h);

    unref_picture(h, &h->cur_pic);

    return 0;
}

static const AVProfile profiles[] = {
    { FF_PROFILE_H264_BASELINE,             "Baseline"              },
    { FF_PROFILE_H264_CONSTRAINED_BASELINE, "Constrained Baseline"  },
    { FF_PROFILE_H264_MAIN,                 "Main"                  },
    { FF_PROFILE_H264_EXTENDED,             "Extended"              },
    { FF_PROFILE_H264_HIGH,                 "High"                  },
    { FF_PROFILE_H264_HIGH_10,              "High 10"               },
    { FF_PROFILE_H264_HIGH_10_INTRA,        "High 10 Intra"         },
    { FF_PROFILE_H264_HIGH_422,             "High 4:2:2"            },
    { FF_PROFILE_H264_HIGH_422_INTRA,       "High 4:2:2 Intra"      },
    { FF_PROFILE_H264_HIGH_444,             "High 4:4:4"            },
    { FF_PROFILE_H264_HIGH_444_PREDICTIVE,  "High 4:4:4 Predictive" },
    { FF_PROFILE_H264_HIGH_444_INTRA,       "High 4:4:4 Intra"      },
    { FF_PROFILE_H264_CAVLC_444,            "CAVLC 4:4:4"           },
    { FF_PROFILE_UNKNOWN },
};

static const AVOption h264_options[] = {
    {"is_avc", "is avc", offsetof(H264Context, is_avc), FF_OPT_TYPE_INT, {.i64 = 0}, 0, 1, 0},
    {"nal_length_size", "nal_length_size", offsetof(H264Context, nal_length_size), FF_OPT_TYPE_INT, {.i64 = 0}, 0, 4, 0},
    {NULL}
};

static const AVClass h264_class = {
    .class_name = "H264 Decoder",
    .item_name  = av_default_item_name,
    .option     = h264_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const AVClass h264_vdpau_class = {
    .class_name = "H264 VDPAU Decoder",
    .item_name  = av_default_item_name,
    .option     = h264_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_h264_decoder = {
    .name                  = "h264",
    .type                  = AVMEDIA_TYPE_VIDEO,
    .id                    = AV_CODEC_ID_H264,
    .priv_data_size        = sizeof(H264Context),
    .init                  = ff_h264_decode_init,
    .close                 = h264_decode_end,
    .decode                = decode_frame,
    .capabilities          = /*CODEC_CAP_DRAW_HORIZ_BAND |*/ CODEC_CAP_DR1 |
                             CODEC_CAP_DELAY | CODEC_CAP_SLICE_THREADS |
                             CODEC_CAP_FRAME_THREADS,
    .flush                 = flush_dpb,
    .long_name             = NULL_IF_CONFIG_SMALL("H.264 / AVC / MPEG-4 AVC / MPEG-4 part 10"),
    .init_thread_copy      = ONLY_IF_THREADS_ENABLED(decode_init_thread_copy),
    .update_thread_context = ONLY_IF_THREADS_ENABLED(decode_update_thread_context),
    .profiles              = NULL_IF_CONFIG_SMALL(profiles),
    .priv_class            = &h264_class,
};

#if CONFIG_H264_VDPAU_DECODER
AVCodec ff_h264_vdpau_decoder = {
    .name           = "h264_vdpau",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_H264,
    .priv_data_size = sizeof(H264Context),
    .init           = ff_h264_decode_init,
    .close          = h264_decode_end,
    .decode         = decode_frame,
    .capabilities   = CODEC_CAP_DR1 | CODEC_CAP_DELAY | CODEC_CAP_HWACCEL_VDPAU,
    .flush          = flush_dpb,
    .long_name      = NULL_IF_CONFIG_SMALL("H.264 / AVC / MPEG-4 AVC / MPEG-4 part 10 (VDPAU acceleration)"),
    .pix_fmts       = (const enum AVPixelFormat[]) { AV_PIX_FMT_VDPAU_H264,
                                                     AV_PIX_FMT_NONE},
    .profiles       = NULL_IF_CONFIG_SMALL(profiles),
    .priv_class     = &h264_vdpau_class,
};
#endif
