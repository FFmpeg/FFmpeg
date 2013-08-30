/*
 * The simplest mpeg encoder (well, it was the simplest!)
 * Copyright (c) 2000,2001 Fabrice Bellard
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
 *
 * 4MV & hq & B-frame encoding stuff by Michael Niedermayer <michaelni@gmx.at>
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
 * The simplest mpeg encoder (well, it was the simplest!).
 */

#include "libavutil/attributes.h"
#include "libavutil/avassert.h"
#include "libavutil/imgutils.h"
#include "avcodec.h"
#include "dsputil.h"
#include "h264chroma.h"
#include "internal.h"
#include "mathops.h"
#include "mpegvideo.h"
#include "mjpegenc.h"
#include "msmpeg4.h"
#include "xvmc_internal.h"
#include "thread.h"
#include <limits.h>

static void dct_unquantize_mpeg1_intra_c(MpegEncContext *s,
                                   int16_t *block, int n, int qscale);
static void dct_unquantize_mpeg1_inter_c(MpegEncContext *s,
                                   int16_t *block, int n, int qscale);
static void dct_unquantize_mpeg2_intra_c(MpegEncContext *s,
                                   int16_t *block, int n, int qscale);
static void dct_unquantize_mpeg2_intra_bitexact(MpegEncContext *s,
                                   int16_t *block, int n, int qscale);
static void dct_unquantize_mpeg2_inter_c(MpegEncContext *s,
                                   int16_t *block, int n, int qscale);
static void dct_unquantize_h263_intra_c(MpegEncContext *s,
                                  int16_t *block, int n, int qscale);
static void dct_unquantize_h263_inter_c(MpegEncContext *s,
                                  int16_t *block, int n, int qscale);

static const uint8_t ff_default_chroma_qscale_table[32] = {
//   0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
     0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31
};

const uint8_t ff_mpeg1_dc_scale_table[128] = {
//  0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
};

static const uint8_t mpeg2_dc_scale_table1[128] = {
//  0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
};

static const uint8_t mpeg2_dc_scale_table2[128] = {
//  0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
};

static const uint8_t mpeg2_dc_scale_table3[128] = {
//  0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
};

const uint8_t *const ff_mpeg2_dc_scale_table[4] = {
    ff_mpeg1_dc_scale_table,
    mpeg2_dc_scale_table1,
    mpeg2_dc_scale_table2,
    mpeg2_dc_scale_table3,
};

const enum AVPixelFormat ff_pixfmt_list_420[] = {
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_NONE
};

static void mpeg_er_decode_mb(void *opaque, int ref, int mv_dir, int mv_type,
                              int (*mv)[2][4][2],
                              int mb_x, int mb_y, int mb_intra, int mb_skipped)
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

    s->dsp.clear_blocks(s->block[0]);

    s->dest[0] = s->current_picture.f.data[0] + (s->mb_y *  16                       * s->linesize)   + s->mb_x *  16;
    s->dest[1] = s->current_picture.f.data[1] + (s->mb_y * (16 >> s->chroma_y_shift) * s->uvlinesize) + s->mb_x * (16 >> s->chroma_x_shift);
    s->dest[2] = s->current_picture.f.data[2] + (s->mb_y * (16 >> s->chroma_y_shift) * s->uvlinesize) + s->mb_x * (16 >> s->chroma_x_shift);

    if (ref)
        av_log(s->avctx, AV_LOG_DEBUG, "Interlaced error concealment is not fully implemented\n");
    ff_MPV_decode_mb(s, s->block);
}

/* init common dct for both encoder and decoder */
av_cold int ff_dct_common_init(MpegEncContext *s)
{
    ff_dsputil_init(&s->dsp, s->avctx);
    ff_h264chroma_init(&s->h264chroma, 8); //for lowres
    ff_hpeldsp_init(&s->hdsp, s->avctx->flags);
    ff_videodsp_init(&s->vdsp, s->avctx->bits_per_raw_sample);

    s->dct_unquantize_h263_intra = dct_unquantize_h263_intra_c;
    s->dct_unquantize_h263_inter = dct_unquantize_h263_inter_c;
    s->dct_unquantize_mpeg1_intra = dct_unquantize_mpeg1_intra_c;
    s->dct_unquantize_mpeg1_inter = dct_unquantize_mpeg1_inter_c;
    s->dct_unquantize_mpeg2_intra = dct_unquantize_mpeg2_intra_c;
    if (s->flags & CODEC_FLAG_BITEXACT)
        s->dct_unquantize_mpeg2_intra = dct_unquantize_mpeg2_intra_bitexact;
    s->dct_unquantize_mpeg2_inter = dct_unquantize_mpeg2_inter_c;

    if (ARCH_ALPHA)
        ff_MPV_common_init_axp(s);
    if (ARCH_ARM)
        ff_MPV_common_init_arm(s);
    if (ARCH_BFIN)
        ff_MPV_common_init_bfin(s);
    if (ARCH_PPC)
        ff_MPV_common_init_ppc(s);
    if (ARCH_X86)
        ff_MPV_common_init_x86(s);

    /* load & permutate scantables
     * note: only wmv uses different ones
     */
    if (s->alternate_scan) {
        ff_init_scantable(s->dsp.idct_permutation, &s->inter_scantable  , ff_alternate_vertical_scan);
        ff_init_scantable(s->dsp.idct_permutation, &s->intra_scantable  , ff_alternate_vertical_scan);
    } else {
        ff_init_scantable(s->dsp.idct_permutation, &s->inter_scantable  , ff_zigzag_direct);
        ff_init_scantable(s->dsp.idct_permutation, &s->intra_scantable  , ff_zigzag_direct);
    }
    ff_init_scantable(s->dsp.idct_permutation, &s->intra_h_scantable, ff_alternate_horizontal_scan);
    ff_init_scantable(s->dsp.idct_permutation, &s->intra_v_scantable, ff_alternate_vertical_scan);

    return 0;
}

int ff_mpv_frame_size_alloc(MpegEncContext *s, int linesize)
{
    int alloc_size = FFALIGN(FFABS(linesize) + 64, 32);

    // edge emu needs blocksize + filter length - 1
    // (= 17x17 for  halfpel / 21x21 for  h264)
    // VC1 computes luma and chroma simultaneously and needs 19X19 + 9x9
    // at uvlinesize. It supports only YUV420 so 24x24 is enough
    // linesize * interlaced * MBsize
    FF_ALLOCZ_OR_GOTO(s->avctx, s->edge_emu_buffer, alloc_size * 4 * 24,
                      fail);

    FF_ALLOCZ_OR_GOTO(s->avctx, s->me.scratchpad, alloc_size * 4 * 16 * 2,
                      fail)
    s->me.temp         = s->me.scratchpad;
    s->rd_scratchpad   = s->me.scratchpad;
    s->b_scratchpad    = s->me.scratchpad;
    s->obmc_scratchpad = s->me.scratchpad + 16;

    return 0;
fail:
    av_freep(&s->edge_emu_buffer);
    return AVERROR(ENOMEM);
}

/**
 * Allocate a frame buffer
 */
static int alloc_frame_buffer(MpegEncContext *s, Picture *pic)
{
    int r, ret;

    pic->tf.f = &pic->f;
    if (s->codec_id != AV_CODEC_ID_WMV3IMAGE &&
        s->codec_id != AV_CODEC_ID_VC1IMAGE  &&
        s->codec_id != AV_CODEC_ID_MSS2)
        r = ff_thread_get_buffer(s->avctx, &pic->tf,
                                 pic->reference ? AV_GET_BUFFER_FLAG_REF : 0);
    else {
        pic->f.width  = s->avctx->width;
        pic->f.height = s->avctx->height;
        pic->f.format = s->avctx->pix_fmt;
        r = avcodec_default_get_buffer2(s->avctx, &pic->f, 0);
    }

    if (r < 0 || !pic->f.data[0]) {
        av_log(s->avctx, AV_LOG_ERROR, "get_buffer() failed (%d %p)\n",
               r, pic->f.data[0]);
        return -1;
    }

    if (s->avctx->hwaccel) {
        assert(!pic->hwaccel_picture_private);
        if (s->avctx->hwaccel->priv_data_size) {
            pic->hwaccel_priv_buf = av_buffer_allocz(s->avctx->hwaccel->priv_data_size);
            if (!pic->hwaccel_priv_buf) {
                av_log(s->avctx, AV_LOG_ERROR, "alloc_frame_buffer() failed (hwaccel private data allocation)\n");
                return -1;
            }
            pic->hwaccel_picture_private = pic->hwaccel_priv_buf->data;
        }
    }

    if (s->linesize && (s->linesize   != pic->f.linesize[0] ||
                        s->uvlinesize != pic->f.linesize[1])) {
        av_log(s->avctx, AV_LOG_ERROR,
               "get_buffer() failed (stride changed)\n");
        ff_mpeg_unref_picture(s, pic);
        return -1;
    }

    if (pic->f.linesize[1] != pic->f.linesize[2]) {
        av_log(s->avctx, AV_LOG_ERROR,
               "get_buffer() failed (uv stride mismatch)\n");
        ff_mpeg_unref_picture(s, pic);
        return -1;
    }

    if (!s->edge_emu_buffer &&
        (ret = ff_mpv_frame_size_alloc(s, pic->f.linesize[0])) < 0) {
        av_log(s->avctx, AV_LOG_ERROR,
               "get_buffer() failed to allocate context scratch buffers.\n");
        ff_mpeg_unref_picture(s, pic);
        return ret;
    }

    return 0;
}

static void free_picture_tables(Picture *pic)
{
    int i;

    pic->alloc_mb_width  =
    pic->alloc_mb_height = 0;

    av_buffer_unref(&pic->mb_var_buf);
    av_buffer_unref(&pic->mc_mb_var_buf);
    av_buffer_unref(&pic->mb_mean_buf);
    av_buffer_unref(&pic->mbskip_table_buf);
    av_buffer_unref(&pic->qscale_table_buf);
    av_buffer_unref(&pic->mb_type_buf);

    for (i = 0; i < 2; i++) {
        av_buffer_unref(&pic->motion_val_buf[i]);
        av_buffer_unref(&pic->ref_index_buf[i]);
    }
}

static int alloc_picture_tables(MpegEncContext *s, Picture *pic)
{
    const int big_mb_num    = s->mb_stride * (s->mb_height + 1) + 1;
    const int mb_array_size = s->mb_stride * s->mb_height;
    const int b8_array_size = s->b8_stride * s->mb_height * 2;
    int i;


    pic->mbskip_table_buf = av_buffer_allocz(mb_array_size + 2);
    pic->qscale_table_buf = av_buffer_allocz(big_mb_num + s->mb_stride);
    pic->mb_type_buf      = av_buffer_allocz((big_mb_num + s->mb_stride) *
                                             sizeof(uint32_t));
    if (!pic->mbskip_table_buf || !pic->qscale_table_buf || !pic->mb_type_buf)
        return AVERROR(ENOMEM);

    if (s->encoding) {
        pic->mb_var_buf    = av_buffer_allocz(mb_array_size * sizeof(int16_t));
        pic->mc_mb_var_buf = av_buffer_allocz(mb_array_size * sizeof(int16_t));
        pic->mb_mean_buf   = av_buffer_allocz(mb_array_size);
        if (!pic->mb_var_buf || !pic->mc_mb_var_buf || !pic->mb_mean_buf)
            return AVERROR(ENOMEM);
    }

    if (s->out_format == FMT_H263 || s->encoding ||
               (s->avctx->debug & FF_DEBUG_MV) || s->avctx->debug_mv) {
        int mv_size        = 2 * (b8_array_size + 4) * sizeof(int16_t);
        int ref_index_size = 4 * mb_array_size;

        for (i = 0; mv_size && i < 2; i++) {
            pic->motion_val_buf[i] = av_buffer_allocz(mv_size);
            pic->ref_index_buf[i]  = av_buffer_allocz(ref_index_size);
            if (!pic->motion_val_buf[i] || !pic->ref_index_buf[i])
                return AVERROR(ENOMEM);
        }
    }

    pic->alloc_mb_width  = s->mb_width;
    pic->alloc_mb_height = s->mb_height;

    return 0;
}

static int make_tables_writable(Picture *pic)
{
    int ret, i;
#define MAKE_WRITABLE(table) \
do {\
    if (pic->table &&\
       (ret = av_buffer_make_writable(&pic->table)) < 0)\
    return ret;\
} while (0)

    MAKE_WRITABLE(mb_var_buf);
    MAKE_WRITABLE(mc_mb_var_buf);
    MAKE_WRITABLE(mb_mean_buf);
    MAKE_WRITABLE(mbskip_table_buf);
    MAKE_WRITABLE(qscale_table_buf);
    MAKE_WRITABLE(mb_type_buf);

    for (i = 0; i < 2; i++) {
        MAKE_WRITABLE(motion_val_buf[i]);
        MAKE_WRITABLE(ref_index_buf[i]);
    }

    return 0;
}

/**
 * Allocate a Picture.
 * The pixels are allocated/set by calling get_buffer() if shared = 0
 */
int ff_alloc_picture(MpegEncContext *s, Picture *pic, int shared)
{
    int i, ret;

    if (pic->qscale_table_buf)
        if (   pic->alloc_mb_width  != s->mb_width
            || pic->alloc_mb_height != s->mb_height)
            free_picture_tables(pic);

    if (shared) {
        av_assert0(pic->f.data[0]);
        pic->shared = 1;
    } else {
        av_assert0(!pic->f.data[0]);

        if (alloc_frame_buffer(s, pic) < 0)
            return -1;

        s->linesize   = pic->f.linesize[0];
        s->uvlinesize = pic->f.linesize[1];
    }

    if (!pic->qscale_table_buf)
        ret = alloc_picture_tables(s, pic);
    else
        ret = make_tables_writable(pic);
    if (ret < 0)
        goto fail;

    if (s->encoding) {
        pic->mb_var    = (uint16_t*)pic->mb_var_buf->data;
        pic->mc_mb_var = (uint16_t*)pic->mc_mb_var_buf->data;
        pic->mb_mean   = pic->mb_mean_buf->data;
    }

    pic->mbskip_table = pic->mbskip_table_buf->data;
    pic->qscale_table = pic->qscale_table_buf->data + 2 * s->mb_stride + 1;
    pic->mb_type      = (uint32_t*)pic->mb_type_buf->data + 2 * s->mb_stride + 1;

    if (pic->motion_val_buf[0]) {
        for (i = 0; i < 2; i++) {
            pic->motion_val[i] = (int16_t (*)[2])pic->motion_val_buf[i]->data + 4;
            pic->ref_index[i]  = pic->ref_index_buf[i]->data;
        }
    }

    return 0;
fail:
    av_log(s->avctx, AV_LOG_ERROR, "Error allocating a picture.\n");
    ff_mpeg_unref_picture(s, pic);
    free_picture_tables(pic);
    return AVERROR(ENOMEM);
}

/**
 * Deallocate a picture.
 */
void ff_mpeg_unref_picture(MpegEncContext *s, Picture *pic)
{
    int off = offsetof(Picture, mb_mean) + sizeof(pic->mb_mean);

    pic->tf.f = &pic->f;
    /* WM Image / Screen codecs allocate internal buffers with different
     * dimensions / colorspaces; ignore user-defined callbacks for these. */
    if (s->codec_id != AV_CODEC_ID_WMV3IMAGE &&
        s->codec_id != AV_CODEC_ID_VC1IMAGE  &&
        s->codec_id != AV_CODEC_ID_MSS2)
        ff_thread_release_buffer(s->avctx, &pic->tf);
    else
        av_frame_unref(&pic->f);

    av_buffer_unref(&pic->hwaccel_priv_buf);

    if (pic->needs_realloc)
        free_picture_tables(pic);

    memset((uint8_t*)pic + off, 0, sizeof(*pic) - off);
}

static int update_picture_tables(Picture *dst, Picture *src)
{
     int i;

#define UPDATE_TABLE(table)\
do {\
    if (src->table &&\
        (!dst->table || dst->table->buffer != src->table->buffer)) {\
        av_buffer_unref(&dst->table);\
        dst->table = av_buffer_ref(src->table);\
        if (!dst->table) {\
            free_picture_tables(dst);\
            return AVERROR(ENOMEM);\
        }\
    }\
} while (0)

    UPDATE_TABLE(mb_var_buf);
    UPDATE_TABLE(mc_mb_var_buf);
    UPDATE_TABLE(mb_mean_buf);
    UPDATE_TABLE(mbskip_table_buf);
    UPDATE_TABLE(qscale_table_buf);
    UPDATE_TABLE(mb_type_buf);
    for (i = 0; i < 2; i++) {
        UPDATE_TABLE(motion_val_buf[i]);
        UPDATE_TABLE(ref_index_buf[i]);
    }

    dst->mb_var        = src->mb_var;
    dst->mc_mb_var     = src->mc_mb_var;
    dst->mb_mean       = src->mb_mean;
    dst->mbskip_table  = src->mbskip_table;
    dst->qscale_table  = src->qscale_table;
    dst->mb_type       = src->mb_type;
    for (i = 0; i < 2; i++) {
        dst->motion_val[i] = src->motion_val[i];
        dst->ref_index[i]  = src->ref_index[i];
    }

    dst->alloc_mb_width  = src->alloc_mb_width;
    dst->alloc_mb_height = src->alloc_mb_height;

    return 0;
}

int ff_mpeg_ref_picture(MpegEncContext *s, Picture *dst, Picture *src)
{
    int ret;

    av_assert0(!dst->f.buf[0]);
    av_assert0(src->f.buf[0]);

    src->tf.f = &src->f;
    dst->tf.f = &dst->f;
    ret = ff_thread_ref_frame(&dst->tf, &src->tf);
    if (ret < 0)
        goto fail;

    ret = update_picture_tables(dst, src);
    if (ret < 0)
        goto fail;

    if (src->hwaccel_picture_private) {
        dst->hwaccel_priv_buf = av_buffer_ref(src->hwaccel_priv_buf);
        if (!dst->hwaccel_priv_buf)
            goto fail;
        dst->hwaccel_picture_private = dst->hwaccel_priv_buf->data;
    }

    dst->field_picture           = src->field_picture;
    dst->mb_var_sum              = src->mb_var_sum;
    dst->mc_mb_var_sum           = src->mc_mb_var_sum;
    dst->b_frame_score           = src->b_frame_score;
    dst->needs_realloc           = src->needs_realloc;
    dst->reference               = src->reference;
    dst->shared                  = src->shared;

    return 0;
fail:
    ff_mpeg_unref_picture(s, dst);
    return ret;
}

static int init_duplicate_context(MpegEncContext *s)
{
    int y_size = s->b8_stride * (2 * s->mb_height + 1);
    int c_size = s->mb_stride * (s->mb_height + 1);
    int yc_size = y_size + 2 * c_size;
    int i;

    s->edge_emu_buffer =
    s->me.scratchpad   =
    s->me.temp         =
    s->rd_scratchpad   =
    s->b_scratchpad    =
    s->obmc_scratchpad = NULL;

    if (s->encoding) {
        FF_ALLOCZ_OR_GOTO(s->avctx, s->me.map,
                          ME_MAP_SIZE * sizeof(uint32_t), fail)
        FF_ALLOCZ_OR_GOTO(s->avctx, s->me.score_map,
                          ME_MAP_SIZE * sizeof(uint32_t), fail)
        if (s->avctx->noise_reduction) {
            FF_ALLOCZ_OR_GOTO(s->avctx, s->dct_error_sum,
                              2 * 64 * sizeof(int), fail)
        }
    }
    FF_ALLOCZ_OR_GOTO(s->avctx, s->blocks, 64 * 12 * 2 * sizeof(int16_t), fail)
    s->block = s->blocks[0];

    for (i = 0; i < 12; i++) {
        s->pblocks[i] = &s->block[i];
    }

    if (s->out_format == FMT_H263) {
        /* ac values */
        FF_ALLOCZ_OR_GOTO(s->avctx, s->ac_val_base,
                          yc_size * sizeof(int16_t) * 16, fail);
        s->ac_val[0] = s->ac_val_base + s->b8_stride + 1;
        s->ac_val[1] = s->ac_val_base + y_size + s->mb_stride + 1;
        s->ac_val[2] = s->ac_val[1] + c_size;
    }

    return 0;
fail:
    return -1; // free() through ff_MPV_common_end()
}

static void free_duplicate_context(MpegEncContext *s)
{
    if (s == NULL)
        return;

    av_freep(&s->edge_emu_buffer);
    av_freep(&s->me.scratchpad);
    s->me.temp =
    s->rd_scratchpad =
    s->b_scratchpad =
    s->obmc_scratchpad = NULL;

    av_freep(&s->dct_error_sum);
    av_freep(&s->me.map);
    av_freep(&s->me.score_map);
    av_freep(&s->blocks);
    av_freep(&s->ac_val_base);
    s->block = NULL;
}

static void backup_duplicate_context(MpegEncContext *bak, MpegEncContext *src)
{
#define COPY(a) bak->a = src->a
    COPY(edge_emu_buffer);
    COPY(me.scratchpad);
    COPY(me.temp);
    COPY(rd_scratchpad);
    COPY(b_scratchpad);
    COPY(obmc_scratchpad);
    COPY(me.map);
    COPY(me.score_map);
    COPY(blocks);
    COPY(block);
    COPY(start_mb_y);
    COPY(end_mb_y);
    COPY(me.map_generation);
    COPY(pb);
    COPY(dct_error_sum);
    COPY(dct_count[0]);
    COPY(dct_count[1]);
    COPY(ac_val_base);
    COPY(ac_val[0]);
    COPY(ac_val[1]);
    COPY(ac_val[2]);
#undef COPY
}

int ff_update_duplicate_context(MpegEncContext *dst, MpegEncContext *src)
{
    MpegEncContext bak;
    int i, ret;
    // FIXME copy only needed parts
    // START_TIMER
    backup_duplicate_context(&bak, dst);
    memcpy(dst, src, sizeof(MpegEncContext));
    backup_duplicate_context(dst, &bak);
    for (i = 0; i < 12; i++) {
        dst->pblocks[i] = &dst->block[i];
    }
    if (!dst->edge_emu_buffer &&
        (ret = ff_mpv_frame_size_alloc(dst, dst->linesize)) < 0) {
        av_log(dst->avctx, AV_LOG_ERROR, "failed to allocate context "
               "scratch buffers.\n");
        return ret;
    }
    // STOP_TIMER("update_duplicate_context")
    // about 10k cycles / 0.01 sec for  1000frames on 1ghz with 2 threads
    return 0;
}

int ff_mpeg_update_thread_context(AVCodecContext *dst,
                                  const AVCodecContext *src)
{
    int i, ret;
    MpegEncContext *s = dst->priv_data, *s1 = src->priv_data;

    if (dst == src)
        return 0;

    av_assert0(s != s1);

    // FIXME can parameters change on I-frames?
    // in that case dst may need a reinit
    if (!s->context_initialized) {
        memcpy(s, s1, sizeof(MpegEncContext));

        s->avctx                 = dst;
        s->bitstream_buffer      = NULL;
        s->bitstream_buffer_size = s->allocated_bitstream_buffer_size = 0;

        if (s1->context_initialized){
//             s->picture_range_start  += MAX_PICTURE_COUNT;
//             s->picture_range_end    += MAX_PICTURE_COUNT;
            if((ret = ff_MPV_common_init(s)) < 0){
                memset(s, 0, sizeof(MpegEncContext));
                s->avctx = dst;
                return ret;
            }
        }
    }

    if (s->height != s1->height || s->width != s1->width || s->context_reinit) {
        s->context_reinit = 0;
        s->height = s1->height;
        s->width  = s1->width;
        if ((ret = ff_MPV_common_frame_size_change(s)) < 0)
            return ret;
    }

    s->avctx->coded_height  = s1->avctx->coded_height;
    s->avctx->coded_width   = s1->avctx->coded_width;
    s->avctx->width         = s1->avctx->width;
    s->avctx->height        = s1->avctx->height;

    s->coded_picture_number = s1->coded_picture_number;
    s->picture_number       = s1->picture_number;
    s->input_picture_number = s1->input_picture_number;

    av_assert0(!s->picture || s->picture != s1->picture);
    if(s->picture)
    for (i = 0; i < MAX_PICTURE_COUNT; i++) {
        ff_mpeg_unref_picture(s, &s->picture[i]);
        if (s1->picture[i].f.data[0] &&
            (ret = ff_mpeg_ref_picture(s, &s->picture[i], &s1->picture[i])) < 0)
            return ret;
    }

#define UPDATE_PICTURE(pic)\
do {\
    ff_mpeg_unref_picture(s, &s->pic);\
    if (s1->pic.f.data[0])\
        ret = ff_mpeg_ref_picture(s, &s->pic, &s1->pic);\
    else\
        ret = update_picture_tables(&s->pic, &s1->pic);\
    if (ret < 0)\
        return ret;\
} while (0)

    UPDATE_PICTURE(current_picture);
    UPDATE_PICTURE(last_picture);
    UPDATE_PICTURE(next_picture);

    s->last_picture_ptr    = REBASE_PICTURE(s1->last_picture_ptr,    s, s1);
    s->current_picture_ptr = REBASE_PICTURE(s1->current_picture_ptr, s, s1);
    s->next_picture_ptr    = REBASE_PICTURE(s1->next_picture_ptr,    s, s1);

    // Error/bug resilience
    s->next_p_frame_damaged = s1->next_p_frame_damaged;
    s->workaround_bugs      = s1->workaround_bugs;
    s->padding_bug_score    = s1->padding_bug_score;

    // MPEG4 timing info
    memcpy(&s->time_increment_bits, &s1->time_increment_bits,
           (char *) &s1->shape - (char *) &s1->time_increment_bits);

    // B-frame info
    s->max_b_frames = s1->max_b_frames;
    s->low_delay    = s1->low_delay;
    s->droppable    = s1->droppable;

    // DivX handling (doesn't work)
    s->divx_packed  = s1->divx_packed;

    if (s1->bitstream_buffer) {
        if (s1->bitstream_buffer_size +
            FF_INPUT_BUFFER_PADDING_SIZE > s->allocated_bitstream_buffer_size)
            av_fast_malloc(&s->bitstream_buffer,
                           &s->allocated_bitstream_buffer_size,
                           s1->allocated_bitstream_buffer_size);
            s->bitstream_buffer_size = s1->bitstream_buffer_size;
        memcpy(s->bitstream_buffer, s1->bitstream_buffer,
               s1->bitstream_buffer_size);
        memset(s->bitstream_buffer + s->bitstream_buffer_size, 0,
               FF_INPUT_BUFFER_PADDING_SIZE);
    }

    // linesize dependend scratch buffer allocation
    if (!s->edge_emu_buffer)
        if (s1->linesize) {
            if (ff_mpv_frame_size_alloc(s, s1->linesize) < 0) {
                av_log(s->avctx, AV_LOG_ERROR, "Failed to allocate context "
                       "scratch buffers.\n");
                return AVERROR(ENOMEM);
            }
        } else {
            av_log(s->avctx, AV_LOG_ERROR, "Context scratch buffers could not "
                   "be allocated due to unknown size.\n");
        }

    // MPEG2/interlacing info
    memcpy(&s->progressive_sequence, &s1->progressive_sequence,
           (char *) &s1->rtp_mode - (char *) &s1->progressive_sequence);

    if (!s1->first_field) {
        s->last_pict_type = s1->pict_type;
        if (s1->current_picture_ptr)
            s->last_lambda_for[s1->pict_type] = s1->current_picture_ptr->f.quality;

        if (s1->pict_type != AV_PICTURE_TYPE_B) {
            s->last_non_b_pict_type = s1->pict_type;
        }
    }

    return 0;
}

/**
 * Set the given MpegEncContext to common defaults
 * (same for encoding and decoding).
 * The changed fields will not depend upon the
 * prior state of the MpegEncContext.
 */
void ff_MPV_common_defaults(MpegEncContext *s)
{
    s->y_dc_scale_table      =
    s->c_dc_scale_table      = ff_mpeg1_dc_scale_table;
    s->chroma_qscale_table   = ff_default_chroma_qscale_table;
    s->progressive_frame     = 1;
    s->progressive_sequence  = 1;
    s->picture_structure     = PICT_FRAME;

    s->coded_picture_number  = 0;
    s->picture_number        = 0;
    s->input_picture_number  = 0;

    s->picture_in_gop_number = 0;

    s->f_code                = 1;
    s->b_code                = 1;

    s->slice_context_count   = 1;
}

/**
 * Set the given MpegEncContext to defaults for decoding.
 * the changed fields will not depend upon
 * the prior state of the MpegEncContext.
 */
void ff_MPV_decode_defaults(MpegEncContext *s)
{
    ff_MPV_common_defaults(s);
}

static int init_er(MpegEncContext *s)
{
    ERContext *er = &s->er;
    int mb_array_size = s->mb_height * s->mb_stride;
    int i;

    er->avctx       = s->avctx;
    er->dsp         = &s->dsp;

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

/**
 * Initialize and allocates MpegEncContext fields dependent on the resolution.
 */
static int init_context_frame(MpegEncContext *s)
{
    int y_size, c_size, yc_size, i, mb_array_size, mv_table_size, x, y;

    s->mb_width   = (s->width + 15) / 16;
    s->mb_stride  = s->mb_width + 1;
    s->b8_stride  = s->mb_width * 2 + 1;
    s->b4_stride  = s->mb_width * 4 + 1;
    mb_array_size = s->mb_height * s->mb_stride;
    mv_table_size = (s->mb_height + 2) * s->mb_stride + 1;

    /* set default edge pos, will be overriden
     * in decode_header if needed */
    s->h_edge_pos = s->mb_width * 16;
    s->v_edge_pos = s->mb_height * 16;

    s->mb_num     = s->mb_width * s->mb_height;

    s->block_wrap[0] =
    s->block_wrap[1] =
    s->block_wrap[2] =
    s->block_wrap[3] = s->b8_stride;
    s->block_wrap[4] =
    s->block_wrap[5] = s->mb_stride;

    y_size  = s->b8_stride * (2 * s->mb_height + 1);
    c_size  = s->mb_stride * (s->mb_height + 1);
    yc_size = y_size + 2   * c_size;

    FF_ALLOCZ_OR_GOTO(s->avctx, s->mb_index2xy, (s->mb_num + 1) * sizeof(int), fail); // error ressilience code looks cleaner with this
    for (y = 0; y < s->mb_height; y++)
        for (x = 0; x < s->mb_width; x++)
            s->mb_index2xy[x + y * s->mb_width] = x + y * s->mb_stride;

    s->mb_index2xy[s->mb_height * s->mb_width] = (s->mb_height - 1) * s->mb_stride + s->mb_width; // FIXME really needed?

    if (s->encoding) {
        /* Allocate MV tables */
        FF_ALLOCZ_OR_GOTO(s->avctx, s->p_mv_table_base,                 mv_table_size * 2 * sizeof(int16_t), fail)
        FF_ALLOCZ_OR_GOTO(s->avctx, s->b_forw_mv_table_base,            mv_table_size * 2 * sizeof(int16_t), fail)
        FF_ALLOCZ_OR_GOTO(s->avctx, s->b_back_mv_table_base,            mv_table_size * 2 * sizeof(int16_t), fail)
        FF_ALLOCZ_OR_GOTO(s->avctx, s->b_bidir_forw_mv_table_base,      mv_table_size * 2 * sizeof(int16_t), fail)
        FF_ALLOCZ_OR_GOTO(s->avctx, s->b_bidir_back_mv_table_base,      mv_table_size * 2 * sizeof(int16_t), fail)
        FF_ALLOCZ_OR_GOTO(s->avctx, s->b_direct_mv_table_base,          mv_table_size * 2 * sizeof(int16_t), fail)
        s->p_mv_table            = s->p_mv_table_base + s->mb_stride + 1;
        s->b_forw_mv_table       = s->b_forw_mv_table_base + s->mb_stride + 1;
        s->b_back_mv_table       = s->b_back_mv_table_base + s->mb_stride + 1;
        s->b_bidir_forw_mv_table = s->b_bidir_forw_mv_table_base + s->mb_stride + 1;
        s->b_bidir_back_mv_table = s->b_bidir_back_mv_table_base + s->mb_stride + 1;
        s->b_direct_mv_table     = s->b_direct_mv_table_base + s->mb_stride + 1;

        /* Allocate MB type table */
        FF_ALLOCZ_OR_GOTO(s->avctx, s->mb_type, mb_array_size * sizeof(uint16_t), fail) // needed for encoding

        FF_ALLOCZ_OR_GOTO(s->avctx, s->lambda_table, mb_array_size * sizeof(int), fail)

        FF_ALLOC_OR_GOTO(s->avctx, s->cplx_tab,
                         mb_array_size * sizeof(float), fail);
        FF_ALLOC_OR_GOTO(s->avctx, s->bits_tab,
                         mb_array_size * sizeof(float), fail);

    }

    if (s->codec_id == AV_CODEC_ID_MPEG4 ||
        (s->flags & CODEC_FLAG_INTERLACED_ME)) {
        /* interlaced direct mode decoding tables */
        for (i = 0; i < 2; i++) {
            int j, k;
            for (j = 0; j < 2; j++) {
                for (k = 0; k < 2; k++) {
                    FF_ALLOCZ_OR_GOTO(s->avctx,
                                      s->b_field_mv_table_base[i][j][k],
                                      mv_table_size * 2 * sizeof(int16_t),
                                      fail);
                    s->b_field_mv_table[i][j][k] = s->b_field_mv_table_base[i][j][k] +
                                                   s->mb_stride + 1;
                }
                FF_ALLOCZ_OR_GOTO(s->avctx, s->b_field_select_table [i][j], mb_array_size * 2 * sizeof(uint8_t), fail)
                FF_ALLOCZ_OR_GOTO(s->avctx, s->p_field_mv_table_base[i][j], mv_table_size * 2 * sizeof(int16_t), fail)
                s->p_field_mv_table[i][j] = s->p_field_mv_table_base[i][j] + s->mb_stride + 1;
            }
            FF_ALLOCZ_OR_GOTO(s->avctx, s->p_field_select_table[i], mb_array_size * 2 * sizeof(uint8_t), fail)
        }
    }
    if (s->out_format == FMT_H263) {
        /* cbp values */
        FF_ALLOCZ_OR_GOTO(s->avctx, s->coded_block_base, y_size, fail);
        s->coded_block = s->coded_block_base + s->b8_stride + 1;

        /* cbp, ac_pred, pred_dir */
        FF_ALLOCZ_OR_GOTO(s->avctx, s->cbp_table     , mb_array_size * sizeof(uint8_t), fail);
        FF_ALLOCZ_OR_GOTO(s->avctx, s->pred_dir_table, mb_array_size * sizeof(uint8_t), fail);
    }

    if (s->h263_pred || s->h263_plus || !s->encoding) {
        /* dc values */
        // MN: we need these for  error resilience of intra-frames
        FF_ALLOCZ_OR_GOTO(s->avctx, s->dc_val_base, yc_size * sizeof(int16_t), fail);
        s->dc_val[0] = s->dc_val_base + s->b8_stride + 1;
        s->dc_val[1] = s->dc_val_base + y_size + s->mb_stride + 1;
        s->dc_val[2] = s->dc_val[1] + c_size;
        for (i = 0; i < yc_size; i++)
            s->dc_val_base[i] = 1024;
    }

    /* which mb is a intra block */
    FF_ALLOCZ_OR_GOTO(s->avctx, s->mbintra_table, mb_array_size, fail);
    memset(s->mbintra_table, 1, mb_array_size);

    /* init macroblock skip table */
    FF_ALLOCZ_OR_GOTO(s->avctx, s->mbskip_table, mb_array_size + 2, fail);
    // Note the + 1 is for  a quicker mpeg4 slice_end detection

    return init_er(s);
fail:
    return AVERROR(ENOMEM);
}

/**
 * init common structure for both encoder and decoder.
 * this assumes that some variables like width/height are already set
 */
av_cold int ff_MPV_common_init(MpegEncContext *s)
{
    int i;
    int nb_slices = (HAVE_THREADS &&
                     s->avctx->active_thread_type & FF_THREAD_SLICE) ?
                    s->avctx->thread_count : 1;

    if (s->encoding && s->avctx->slices)
        nb_slices = s->avctx->slices;

    if (s->codec_id == AV_CODEC_ID_MPEG2VIDEO && !s->progressive_sequence)
        s->mb_height = (s->height + 31) / 32 * 2;
    else
        s->mb_height = (s->height + 15) / 16;

    if (s->avctx->pix_fmt == AV_PIX_FMT_NONE) {
        av_log(s->avctx, AV_LOG_ERROR,
               "decoding to AV_PIX_FMT_NONE is not supported.\n");
        return -1;
    }

    if (nb_slices > MAX_THREADS || (nb_slices > s->mb_height && s->mb_height)) {
        int max_slices;
        if (s->mb_height)
            max_slices = FFMIN(MAX_THREADS, s->mb_height);
        else
            max_slices = MAX_THREADS;
        av_log(s->avctx, AV_LOG_WARNING, "too many threads/slices (%d),"
               " reducing to %d\n", nb_slices, max_slices);
        nb_slices = max_slices;
    }

    if ((s->width || s->height) &&
        av_image_check_size(s->width, s->height, 0, s->avctx))
        return -1;

    ff_dct_common_init(s);

    s->flags  = s->avctx->flags;
    s->flags2 = s->avctx->flags2;

    /* set chroma shifts */
    avcodec_get_chroma_sub_sample(s->avctx->pix_fmt, &s->chroma_x_shift, &s->chroma_y_shift);

    /* convert fourcc to upper case */
    s->codec_tag        = avpriv_toupper4(s->avctx->codec_tag);
    s->stream_codec_tag = avpriv_toupper4(s->avctx->stream_codec_tag);

    s->avctx->coded_frame = &s->current_picture.f;

    if (s->encoding) {
        if (s->msmpeg4_version) {
            FF_ALLOCZ_OR_GOTO(s->avctx, s->ac_stats,
                                2 * 2 * (MAX_LEVEL + 1) *
                                (MAX_RUN + 1) * 2 * sizeof(int), fail);
        }
        FF_ALLOCZ_OR_GOTO(s->avctx, s->avctx->stats_out, 256, fail);

        FF_ALLOCZ_OR_GOTO(s->avctx, s->q_intra_matrix,          64 * 32   * sizeof(int), fail)
        FF_ALLOCZ_OR_GOTO(s->avctx, s->q_chroma_intra_matrix,   64 * 32   * sizeof(int), fail)
        FF_ALLOCZ_OR_GOTO(s->avctx, s->q_inter_matrix,          64 * 32   * sizeof(int), fail)
        FF_ALLOCZ_OR_GOTO(s->avctx, s->q_intra_matrix16,        64 * 32 * 2 * sizeof(uint16_t), fail)
        FF_ALLOCZ_OR_GOTO(s->avctx, s->q_chroma_intra_matrix16, 64 * 32 * 2 * sizeof(uint16_t), fail)
        FF_ALLOCZ_OR_GOTO(s->avctx, s->q_inter_matrix16,        64 * 32 * 2 * sizeof(uint16_t), fail)
        FF_ALLOCZ_OR_GOTO(s->avctx, s->input_picture,           MAX_PICTURE_COUNT * sizeof(Picture *), fail)
        FF_ALLOCZ_OR_GOTO(s->avctx, s->reordered_input_picture, MAX_PICTURE_COUNT * sizeof(Picture *), fail)

        if (s->avctx->noise_reduction) {
            FF_ALLOCZ_OR_GOTO(s->avctx, s->dct_offset, 2 * 64 * sizeof(uint16_t), fail);
        }
    }

    FF_ALLOCZ_OR_GOTO(s->avctx, s->picture,
                      MAX_PICTURE_COUNT * sizeof(Picture), fail);
    for (i = 0; i < MAX_PICTURE_COUNT; i++) {
        avcodec_get_frame_defaults(&s->picture[i].f);
    }
    memset(&s->next_picture, 0, sizeof(s->next_picture));
    memset(&s->last_picture, 0, sizeof(s->last_picture));
    memset(&s->current_picture, 0, sizeof(s->current_picture));
    avcodec_get_frame_defaults(&s->next_picture.f);
    avcodec_get_frame_defaults(&s->last_picture.f);
    avcodec_get_frame_defaults(&s->current_picture.f);

        if (init_context_frame(s))
            goto fail;

        s->parse_context.state = -1;

        s->context_initialized = 1;
        s->thread_context[0]   = s;

//     if (s->width && s->height) {
        if (nb_slices > 1) {
            for (i = 1; i < nb_slices; i++) {
                s->thread_context[i] = av_malloc(sizeof(MpegEncContext));
                memcpy(s->thread_context[i], s, sizeof(MpegEncContext));
            }

            for (i = 0; i < nb_slices; i++) {
                if (init_duplicate_context(s->thread_context[i]) < 0)
                    goto fail;
                    s->thread_context[i]->start_mb_y =
                        (s->mb_height * (i) + nb_slices / 2) / nb_slices;
                    s->thread_context[i]->end_mb_y   =
                        (s->mb_height * (i + 1) + nb_slices / 2) / nb_slices;
            }
        } else {
            if (init_duplicate_context(s) < 0)
                goto fail;
            s->start_mb_y = 0;
            s->end_mb_y   = s->mb_height;
        }
        s->slice_context_count = nb_slices;
//     }

    return 0;
 fail:
    ff_MPV_common_end(s);
    return -1;
}

/**
 * Frees and resets MpegEncContext fields depending on the resolution.
 * Is used during resolution changes to avoid a full reinitialization of the
 * codec.
 */
static int free_context_frame(MpegEncContext *s)
{
    int i, j, k;

    av_freep(&s->mb_type);
    av_freep(&s->p_mv_table_base);
    av_freep(&s->b_forw_mv_table_base);
    av_freep(&s->b_back_mv_table_base);
    av_freep(&s->b_bidir_forw_mv_table_base);
    av_freep(&s->b_bidir_back_mv_table_base);
    av_freep(&s->b_direct_mv_table_base);
    s->p_mv_table            = NULL;
    s->b_forw_mv_table       = NULL;
    s->b_back_mv_table       = NULL;
    s->b_bidir_forw_mv_table = NULL;
    s->b_bidir_back_mv_table = NULL;
    s->b_direct_mv_table     = NULL;
    for (i = 0; i < 2; i++) {
        for (j = 0; j < 2; j++) {
            for (k = 0; k < 2; k++) {
                av_freep(&s->b_field_mv_table_base[i][j][k]);
                s->b_field_mv_table[i][j][k] = NULL;
            }
            av_freep(&s->b_field_select_table[i][j]);
            av_freep(&s->p_field_mv_table_base[i][j]);
            s->p_field_mv_table[i][j] = NULL;
        }
        av_freep(&s->p_field_select_table[i]);
    }

    av_freep(&s->dc_val_base);
    av_freep(&s->coded_block_base);
    av_freep(&s->mbintra_table);
    av_freep(&s->cbp_table);
    av_freep(&s->pred_dir_table);

    av_freep(&s->mbskip_table);

    av_freep(&s->er.error_status_table);
    av_freep(&s->er.er_temp_buffer);
    av_freep(&s->mb_index2xy);
    av_freep(&s->lambda_table);

    av_freep(&s->cplx_tab);
    av_freep(&s->bits_tab);

    s->linesize = s->uvlinesize = 0;

    return 0;
}

int ff_MPV_common_frame_size_change(MpegEncContext *s)
{
    int i, err = 0;

    if (s->slice_context_count > 1) {
        for (i = 0; i < s->slice_context_count; i++) {
            free_duplicate_context(s->thread_context[i]);
        }
        for (i = 1; i < s->slice_context_count; i++) {
            av_freep(&s->thread_context[i]);
        }
    } else
        free_duplicate_context(s);

    if ((err = free_context_frame(s)) < 0)
        return err;

    if (s->picture)
        for (i = 0; i < MAX_PICTURE_COUNT; i++) {
                s->picture[i].needs_realloc = 1;
        }

    s->last_picture_ptr         =
    s->next_picture_ptr         =
    s->current_picture_ptr      = NULL;

    // init
    if (s->codec_id == AV_CODEC_ID_MPEG2VIDEO && !s->progressive_sequence)
        s->mb_height = (s->height + 31) / 32 * 2;
    else
        s->mb_height = (s->height + 15) / 16;

    if ((s->width || s->height) &&
        av_image_check_size(s->width, s->height, 0, s->avctx))
        return AVERROR_INVALIDDATA;

    if ((err = init_context_frame(s)))
        goto fail;

    s->thread_context[0]   = s;

    if (s->width && s->height) {
        int nb_slices = s->slice_context_count;
        if (nb_slices > 1) {
            for (i = 1; i < nb_slices; i++) {
                s->thread_context[i] = av_malloc(sizeof(MpegEncContext));
                memcpy(s->thread_context[i], s, sizeof(MpegEncContext));
            }

            for (i = 0; i < nb_slices; i++) {
                if (init_duplicate_context(s->thread_context[i]) < 0)
                    goto fail;
                    s->thread_context[i]->start_mb_y =
                        (s->mb_height * (i) + nb_slices / 2) / nb_slices;
                    s->thread_context[i]->end_mb_y   =
                        (s->mb_height * (i + 1) + nb_slices / 2) / nb_slices;
            }
        } else {
            err = init_duplicate_context(s);
            if (err < 0)
                goto fail;
            s->start_mb_y = 0;
            s->end_mb_y   = s->mb_height;
        }
        s->slice_context_count = nb_slices;
    }

    return 0;
 fail:
    ff_MPV_common_end(s);
    return err;
}

/* init common structure for both encoder and decoder */
void ff_MPV_common_end(MpegEncContext *s)
{
    int i;

    if (s->slice_context_count > 1) {
        for (i = 0; i < s->slice_context_count; i++) {
            free_duplicate_context(s->thread_context[i]);
        }
        for (i = 1; i < s->slice_context_count; i++) {
            av_freep(&s->thread_context[i]);
        }
        s->slice_context_count = 1;
    } else free_duplicate_context(s);

    av_freep(&s->parse_context.buffer);
    s->parse_context.buffer_size = 0;

    av_freep(&s->bitstream_buffer);
    s->allocated_bitstream_buffer_size = 0;

    av_freep(&s->avctx->stats_out);
    av_freep(&s->ac_stats);

    if(s->q_chroma_intra_matrix   != s->q_intra_matrix  ) av_freep(&s->q_chroma_intra_matrix);
    if(s->q_chroma_intra_matrix16 != s->q_intra_matrix16) av_freep(&s->q_chroma_intra_matrix16);
    s->q_chroma_intra_matrix=   NULL;
    s->q_chroma_intra_matrix16= NULL;
    av_freep(&s->q_intra_matrix);
    av_freep(&s->q_inter_matrix);
    av_freep(&s->q_intra_matrix16);
    av_freep(&s->q_inter_matrix16);
    av_freep(&s->input_picture);
    av_freep(&s->reordered_input_picture);
    av_freep(&s->dct_offset);

    if (s->picture) {
        for (i = 0; i < MAX_PICTURE_COUNT; i++) {
            free_picture_tables(&s->picture[i]);
            ff_mpeg_unref_picture(s, &s->picture[i]);
        }
    }
    av_freep(&s->picture);
    free_picture_tables(&s->last_picture);
    ff_mpeg_unref_picture(s, &s->last_picture);
    free_picture_tables(&s->current_picture);
    ff_mpeg_unref_picture(s, &s->current_picture);
    free_picture_tables(&s->next_picture);
    ff_mpeg_unref_picture(s, &s->next_picture);
    free_picture_tables(&s->new_picture);
    ff_mpeg_unref_picture(s, &s->new_picture);

    free_context_frame(s);

    s->context_initialized      = 0;
    s->last_picture_ptr         =
    s->next_picture_ptr         =
    s->current_picture_ptr      = NULL;
    s->linesize = s->uvlinesize = 0;
}

av_cold void ff_init_rl(RLTable *rl,
                        uint8_t static_store[2][2 * MAX_RUN + MAX_LEVEL + 3])
{
    int8_t  max_level[MAX_RUN + 1], max_run[MAX_LEVEL + 1];
    uint8_t index_run[MAX_RUN + 1];
    int last, run, level, start, end, i;

    /* If table is static, we can quit if rl->max_level[0] is not NULL */
    if (static_store && rl->max_level[0])
        return;

    /* compute max_level[], max_run[] and index_run[] */
    for (last = 0; last < 2; last++) {
        if (last == 0) {
            start = 0;
            end = rl->last;
        } else {
            start = rl->last;
            end = rl->n;
        }

        memset(max_level, 0, MAX_RUN + 1);
        memset(max_run, 0, MAX_LEVEL + 1);
        memset(index_run, rl->n, MAX_RUN + 1);
        for (i = start; i < end; i++) {
            run   = rl->table_run[i];
            level = rl->table_level[i];
            if (index_run[run] == rl->n)
                index_run[run] = i;
            if (level > max_level[run])
                max_level[run] = level;
            if (run > max_run[level])
                max_run[level] = run;
        }
        if (static_store)
            rl->max_level[last] = static_store[last];
        else
            rl->max_level[last] = av_malloc(MAX_RUN + 1);
        memcpy(rl->max_level[last], max_level, MAX_RUN + 1);
        if (static_store)
            rl->max_run[last]   = static_store[last] + MAX_RUN + 1;
        else
            rl->max_run[last]   = av_malloc(MAX_LEVEL + 1);
        memcpy(rl->max_run[last], max_run, MAX_LEVEL + 1);
        if (static_store)
            rl->index_run[last] = static_store[last] + MAX_RUN + MAX_LEVEL + 2;
        else
            rl->index_run[last] = av_malloc(MAX_RUN + 1);
        memcpy(rl->index_run[last], index_run, MAX_RUN + 1);
    }
}

av_cold void ff_init_vlc_rl(RLTable *rl)
{
    int i, q;

    for (q = 0; q < 32; q++) {
        int qmul = q * 2;
        int qadd = (q - 1) | 1;

        if (q == 0) {
            qmul = 1;
            qadd = 0;
        }
        for (i = 0; i < rl->vlc.table_size; i++) {
            int code = rl->vlc.table[i][0];
            int len  = rl->vlc.table[i][1];
            int level, run;

            if (len == 0) { // illegal code
                run   = 66;
                level = MAX_LEVEL;
            } else if (len < 0) { // more bits needed
                run   = 0;
                level = code;
            } else {
                if (code == rl->n) { // esc
                    run   = 66;
                    level =  0;
                } else {
                    run   = rl->table_run[code] + 1;
                    level = rl->table_level[code] * qmul + qadd;
                    if (code >= rl->last) run += 192;
                }
            }
            rl->rl_vlc[q][i].len   = len;
            rl->rl_vlc[q][i].level = level;
            rl->rl_vlc[q][i].run   = run;
        }
    }
}

void ff_release_unused_pictures(MpegEncContext*s, int remove_current)
{
    int i;

    /* release non reference frames */
    for (i = 0; i < MAX_PICTURE_COUNT; i++) {
        if (!s->picture[i].reference &&
            (remove_current || &s->picture[i] !=  s->current_picture_ptr)) {
            ff_mpeg_unref_picture(s, &s->picture[i]);
        }
    }
}

static inline int pic_is_unused(MpegEncContext *s, Picture *pic)
{
    if (pic == s->last_picture_ptr)
        return 0;
    if (pic->f.data[0] == NULL)
        return 1;
    if (pic->needs_realloc && !(pic->reference & DELAYED_PIC_REF))
        return 1;
    return 0;
}

static int find_unused_picture(MpegEncContext *s, int shared)
{
    int i;

    if (shared) {
        for (i = 0; i < MAX_PICTURE_COUNT; i++) {
            if (s->picture[i].f.data[0] == NULL && &s->picture[i] != s->last_picture_ptr)
                return i;
        }
    } else {
        for (i = 0; i < MAX_PICTURE_COUNT; i++) {
            if (pic_is_unused(s, &s->picture[i]))
                return i;
        }
    }

    av_log(s->avctx, AV_LOG_FATAL,
           "Internal error, picture buffer overflow\n");
    /* We could return -1, but the codec would crash trying to draw into a
     * non-existing frame anyway. This is safer than waiting for a random crash.
     * Also the return of this is never useful, an encoder must only allocate
     * as much as allowed in the specification. This has no relationship to how
     * much libavcodec could allocate (and MAX_PICTURE_COUNT is always large
     * enough for such valid streams).
     * Plus, a decoder has to check stream validity and remove frames if too
     * many reference frames are around. Waiting for "OOM" is not correct at
     * all. Similarly, missing reference frames have to be replaced by
     * interpolated/MC frames, anything else is a bug in the codec ...
     */
    abort();
    return -1;
}

int ff_find_unused_picture(MpegEncContext *s, int shared)
{
    int ret = find_unused_picture(s, shared);

    if (ret >= 0 && ret < MAX_PICTURE_COUNT) {
        if (s->picture[ret].needs_realloc) {
            s->picture[ret].needs_realloc = 0;
            free_picture_tables(&s->picture[ret]);
            ff_mpeg_unref_picture(s, &s->picture[ret]);
            avcodec_get_frame_defaults(&s->picture[ret].f);
        }
    }
    return ret;
}

static void update_noise_reduction(MpegEncContext *s)
{
    int intra, i;

    for (intra = 0; intra < 2; intra++) {
        if (s->dct_count[intra] > (1 << 16)) {
            for (i = 0; i < 64; i++) {
                s->dct_error_sum[intra][i] >>= 1;
            }
            s->dct_count[intra] >>= 1;
        }

        for (i = 0; i < 64; i++) {
            s->dct_offset[intra][i] = (s->avctx->noise_reduction *
                                       s->dct_count[intra] +
                                       s->dct_error_sum[intra][i] / 2) /
                                      (s->dct_error_sum[intra][i] + 1);
        }
    }
}

/**
 * generic function for encode/decode called after coding/decoding
 * the header and before a frame is coded/decoded.
 */
int ff_MPV_frame_start(MpegEncContext *s, AVCodecContext *avctx)
{
    int i, ret;
    Picture *pic;
    s->mb_skipped = 0;

    if (!ff_thread_can_start_frame(avctx)) {
        av_log(avctx, AV_LOG_ERROR, "Attempt to start a frame outside SETUP state\n");
        return -1;
    }

    /* mark & release old frames */
    if (s->pict_type != AV_PICTURE_TYPE_B && s->last_picture_ptr &&
        s->last_picture_ptr != s->next_picture_ptr &&
        s->last_picture_ptr->f.data[0]) {
        ff_mpeg_unref_picture(s, s->last_picture_ptr);
    }

    /* release forgotten pictures */
    /* if (mpeg124/h263) */
    if (!s->encoding) {
        for (i = 0; i < MAX_PICTURE_COUNT; i++) {
            if (&s->picture[i] != s->last_picture_ptr &&
                &s->picture[i] != s->next_picture_ptr &&
                s->picture[i].reference && !s->picture[i].needs_realloc) {
                if (!(avctx->active_thread_type & FF_THREAD_FRAME))
                    av_log(avctx, AV_LOG_ERROR,
                           "releasing zombie picture\n");
                ff_mpeg_unref_picture(s, &s->picture[i]);
            }
        }
    }

    ff_mpeg_unref_picture(s, &s->current_picture);

    if (!s->encoding) {
        ff_release_unused_pictures(s, 1);

        if (s->current_picture_ptr &&
            s->current_picture_ptr->f.data[0] == NULL) {
            // we already have a unused image
            // (maybe it was set before reading the header)
            pic = s->current_picture_ptr;
        } else {
            i   = ff_find_unused_picture(s, 0);
            if (i < 0) {
                av_log(s->avctx, AV_LOG_ERROR, "no frame buffer available\n");
                return i;
            }
            pic = &s->picture[i];
        }

        pic->reference = 0;
        if (!s->droppable) {
            if (s->pict_type != AV_PICTURE_TYPE_B)
                pic->reference = 3;
        }

        pic->f.coded_picture_number = s->coded_picture_number++;

        if (ff_alloc_picture(s, pic, 0) < 0)
            return -1;

        s->current_picture_ptr = pic;
        // FIXME use only the vars from current_pic
        s->current_picture_ptr->f.top_field_first = s->top_field_first;
        if (s->codec_id == AV_CODEC_ID_MPEG1VIDEO ||
            s->codec_id == AV_CODEC_ID_MPEG2VIDEO) {
            if (s->picture_structure != PICT_FRAME)
                s->current_picture_ptr->f.top_field_first =
                    (s->picture_structure == PICT_TOP_FIELD) == s->first_field;
        }
        s->current_picture_ptr->f.interlaced_frame = !s->progressive_frame &&
                                                     !s->progressive_sequence;
        s->current_picture_ptr->field_picture      =  s->picture_structure != PICT_FRAME;
    }

    s->current_picture_ptr->f.pict_type = s->pict_type;
    // if (s->flags && CODEC_FLAG_QSCALE)
    //     s->current_picture_ptr->quality = s->new_picture_ptr->quality;
    s->current_picture_ptr->f.key_frame = s->pict_type == AV_PICTURE_TYPE_I;

    if ((ret = ff_mpeg_ref_picture(s, &s->current_picture,
                                   s->current_picture_ptr)) < 0)
        return ret;

    if (s->pict_type != AV_PICTURE_TYPE_B) {
        s->last_picture_ptr = s->next_picture_ptr;
        if (!s->droppable)
            s->next_picture_ptr = s->current_picture_ptr;
    }
    av_dlog(s->avctx, "L%p N%p C%p L%p N%p C%p type:%d drop:%d\n",
            s->last_picture_ptr, s->next_picture_ptr,s->current_picture_ptr,
            s->last_picture_ptr    ? s->last_picture_ptr->f.data[0]    : NULL,
            s->next_picture_ptr    ? s->next_picture_ptr->f.data[0]    : NULL,
            s->current_picture_ptr ? s->current_picture_ptr->f.data[0] : NULL,
            s->pict_type, s->droppable);

    if ((s->last_picture_ptr == NULL ||
         s->last_picture_ptr->f.data[0] == NULL) &&
        (s->pict_type != AV_PICTURE_TYPE_I ||
         s->picture_structure != PICT_FRAME)) {
        int h_chroma_shift, v_chroma_shift;
        av_pix_fmt_get_chroma_sub_sample(s->avctx->pix_fmt,
                                         &h_chroma_shift, &v_chroma_shift);
        if (s->pict_type == AV_PICTURE_TYPE_B && s->next_picture_ptr && s->next_picture_ptr->f.data[0])
            av_log(avctx, AV_LOG_DEBUG,
                   "allocating dummy last picture for B frame\n");
        else if (s->pict_type != AV_PICTURE_TYPE_I)
            av_log(avctx, AV_LOG_ERROR,
                   "warning: first frame is no keyframe\n");
        else if (s->picture_structure != PICT_FRAME)
            av_log(avctx, AV_LOG_DEBUG,
                   "allocate dummy last picture for field based first keyframe\n");

        /* Allocate a dummy frame */
        i = ff_find_unused_picture(s, 0);
        if (i < 0) {
            av_log(s->avctx, AV_LOG_ERROR, "no frame buffer available\n");
            return i;
        }
        s->last_picture_ptr = &s->picture[i];
        s->last_picture_ptr->f.key_frame = 0;
        if (ff_alloc_picture(s, s->last_picture_ptr, 0) < 0) {
            s->last_picture_ptr = NULL;
            return -1;
        }

        memset(s->last_picture_ptr->f.data[0], 0x80,
               avctx->height * s->last_picture_ptr->f.linesize[0]);
        memset(s->last_picture_ptr->f.data[1], 0x80,
               (avctx->height >> v_chroma_shift) *
               s->last_picture_ptr->f.linesize[1]);
        memset(s->last_picture_ptr->f.data[2], 0x80,
               (avctx->height >> v_chroma_shift) *
               s->last_picture_ptr->f.linesize[2]);

        if(s->codec_id == AV_CODEC_ID_FLV1 || s->codec_id == AV_CODEC_ID_H263){
            for(i=0; i<avctx->height; i++)
            memset(s->last_picture_ptr->f.data[0] + s->last_picture_ptr->f.linesize[0]*i, 16, avctx->width);
        }

        ff_thread_report_progress(&s->last_picture_ptr->tf, INT_MAX, 0);
        ff_thread_report_progress(&s->last_picture_ptr->tf, INT_MAX, 1);
    }
    if ((s->next_picture_ptr == NULL ||
         s->next_picture_ptr->f.data[0] == NULL) &&
        s->pict_type == AV_PICTURE_TYPE_B) {
        /* Allocate a dummy frame */
        i = ff_find_unused_picture(s, 0);
        if (i < 0) {
            av_log(s->avctx, AV_LOG_ERROR, "no frame buffer available\n");
            return i;
        }
        s->next_picture_ptr = &s->picture[i];
        s->next_picture_ptr->f.key_frame = 0;
        if (ff_alloc_picture(s, s->next_picture_ptr, 0) < 0) {
            s->next_picture_ptr = NULL;
            return -1;
        }
        ff_thread_report_progress(&s->next_picture_ptr->tf, INT_MAX, 0);
        ff_thread_report_progress(&s->next_picture_ptr->tf, INT_MAX, 1);
    }

#if 0 // BUFREF-FIXME
    memset(s->last_picture.f.data, 0, sizeof(s->last_picture.f.data));
    memset(s->next_picture.f.data, 0, sizeof(s->next_picture.f.data));
#endif
    if (s->last_picture_ptr) {
        ff_mpeg_unref_picture(s, &s->last_picture);
        if (s->last_picture_ptr->f.data[0] &&
            (ret = ff_mpeg_ref_picture(s, &s->last_picture,
                                       s->last_picture_ptr)) < 0)
            return ret;
    }
    if (s->next_picture_ptr) {
        ff_mpeg_unref_picture(s, &s->next_picture);
        if (s->next_picture_ptr->f.data[0] &&
            (ret = ff_mpeg_ref_picture(s, &s->next_picture,
                                       s->next_picture_ptr)) < 0)
            return ret;
    }

    av_assert0(s->pict_type == AV_PICTURE_TYPE_I || (s->last_picture_ptr &&
                                                 s->last_picture_ptr->f.data[0]));

    if (s->picture_structure!= PICT_FRAME) {
        int i;
        for (i = 0; i < 4; i++) {
            if (s->picture_structure == PICT_BOTTOM_FIELD) {
                s->current_picture.f.data[i] +=
                    s->current_picture.f.linesize[i];
            }
            s->current_picture.f.linesize[i] *= 2;
            s->last_picture.f.linesize[i]    *= 2;
            s->next_picture.f.linesize[i]    *= 2;
        }
    }

    s->err_recognition = avctx->err_recognition;

    /* set dequantizer, we can't do it during init as
     * it might change for mpeg4 and we can't do it in the header
     * decode as init is not called for mpeg4 there yet */
    if (s->mpeg_quant || s->codec_id == AV_CODEC_ID_MPEG2VIDEO) {
        s->dct_unquantize_intra = s->dct_unquantize_mpeg2_intra;
        s->dct_unquantize_inter = s->dct_unquantize_mpeg2_inter;
    } else if (s->out_format == FMT_H263 || s->out_format == FMT_H261) {
        s->dct_unquantize_intra = s->dct_unquantize_h263_intra;
        s->dct_unquantize_inter = s->dct_unquantize_h263_inter;
    } else {
        s->dct_unquantize_intra = s->dct_unquantize_mpeg1_intra;
        s->dct_unquantize_inter = s->dct_unquantize_mpeg1_inter;
    }

    if (s->dct_error_sum) {
        av_assert2(s->avctx->noise_reduction && s->encoding);
        update_noise_reduction(s);
    }

    if (CONFIG_MPEG_XVMC_DECODER && s->avctx->xvmc_acceleration)
        return ff_xvmc_field_start(s, avctx);

    return 0;
}

/* generic function for encode/decode called after a
 * frame has been coded/decoded. */
void ff_MPV_frame_end(MpegEncContext *s)
{
    /* redraw edges for the frame if decoding didn't complete */
    // just to make sure that all data is rendered.
    if (CONFIG_MPEG_XVMC_DECODER && s->avctx->xvmc_acceleration) {
        ff_xvmc_field_end(s);
   } else if ((s->er.error_count || s->encoding || !(s->avctx->codec->capabilities&CODEC_CAP_DRAW_HORIZ_BAND)) &&
              !s->avctx->hwaccel &&
              !(s->avctx->codec->capabilities & CODEC_CAP_HWACCEL_VDPAU) &&
              s->unrestricted_mv &&
              s->current_picture.reference &&
              !s->intra_only &&
              !(s->flags & CODEC_FLAG_EMU_EDGE) &&
              !s->avctx->lowres
            ) {
        const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(s->avctx->pix_fmt);
        int hshift = desc->log2_chroma_w;
        int vshift = desc->log2_chroma_h;
        s->dsp.draw_edges(s->current_picture.f.data[0], s->current_picture.f.linesize[0],
                          s->h_edge_pos, s->v_edge_pos,
                          EDGE_WIDTH, EDGE_WIDTH,
                          EDGE_TOP | EDGE_BOTTOM);
        s->dsp.draw_edges(s->current_picture.f.data[1], s->current_picture.f.linesize[1],
                          s->h_edge_pos >> hshift, s->v_edge_pos >> vshift,
                          EDGE_WIDTH >> hshift, EDGE_WIDTH >> vshift,
                          EDGE_TOP | EDGE_BOTTOM);
        s->dsp.draw_edges(s->current_picture.f.data[2], s->current_picture.f.linesize[2],
                          s->h_edge_pos >> hshift, s->v_edge_pos >> vshift,
                          EDGE_WIDTH >> hshift, EDGE_WIDTH >> vshift,
                          EDGE_TOP | EDGE_BOTTOM);
    }

    emms_c();

    s->last_pict_type                 = s->pict_type;
    s->last_lambda_for [s->pict_type] = s->current_picture_ptr->f.quality;
    if (s->pict_type!= AV_PICTURE_TYPE_B) {
        s->last_non_b_pict_type = s->pict_type;
    }
#if 0
    /* copy back current_picture variables */
    for (i = 0; i < MAX_PICTURE_COUNT; i++) {
        if (s->picture[i].f.data[0] == s->current_picture.f.data[0]) {
            s->picture[i] = s->current_picture;
            break;
        }
    }
    av_assert0(i < MAX_PICTURE_COUNT);
#endif

    // clear copies, to avoid confusion
#if 0
    memset(&s->last_picture,    0, sizeof(Picture));
    memset(&s->next_picture,    0, sizeof(Picture));
    memset(&s->current_picture, 0, sizeof(Picture));
#endif
    s->avctx->coded_frame = &s->current_picture_ptr->f;

    if (s->current_picture.reference)
        ff_thread_report_progress(&s->current_picture_ptr->tf, INT_MAX, 0);
}

/**
 * Draw a line from (ex, ey) -> (sx, sy).
 * @param w width of the image
 * @param h height of the image
 * @param stride stride/linesize of the image
 * @param color color of the arrow
 */
static void draw_line(uint8_t *buf, int sx, int sy, int ex, int ey,
                      int w, int h, int stride, int color)
{
    int x, y, fr, f;

    sx = av_clip(sx, 0, w - 1);
    sy = av_clip(sy, 0, h - 1);
    ex = av_clip(ex, 0, w - 1);
    ey = av_clip(ey, 0, h - 1);

    buf[sy * stride + sx] += color;

    if (FFABS(ex - sx) > FFABS(ey - sy)) {
        if (sx > ex) {
            FFSWAP(int, sx, ex);
            FFSWAP(int, sy, ey);
        }
        buf += sx + sy * stride;
        ex  -= sx;
        f    = ((ey - sy) << 16) / ex;
        for (x = 0; x <= ex; x++) {
            y  = (x * f) >> 16;
            fr = (x * f) & 0xFFFF;
            buf[y * stride + x]       += (color * (0x10000 - fr)) >> 16;
            if(fr) buf[(y + 1) * stride + x] += (color *            fr ) >> 16;
        }
    } else {
        if (sy > ey) {
            FFSWAP(int, sx, ex);
            FFSWAP(int, sy, ey);
        }
        buf += sx + sy * stride;
        ey  -= sy;
        if (ey)
            f = ((ex - sx) << 16) / ey;
        else
            f = 0;
        for(y= 0; y <= ey; y++){
            x  = (y*f) >> 16;
            fr = (y*f) & 0xFFFF;
            buf[y * stride + x]     += (color * (0x10000 - fr)) >> 16;
            if(fr) buf[y * stride + x + 1] += (color *            fr ) >> 16;
        }
    }
}

/**
 * Draw an arrow from (ex, ey) -> (sx, sy).
 * @param w width of the image
 * @param h height of the image
 * @param stride stride/linesize of the image
 * @param color color of the arrow
 */
static void draw_arrow(uint8_t *buf, int sx, int sy, int ex,
                       int ey, int w, int h, int stride, int color)
{
    int dx,dy;

    sx = av_clip(sx, -100, w + 100);
    sy = av_clip(sy, -100, h + 100);
    ex = av_clip(ex, -100, w + 100);
    ey = av_clip(ey, -100, h + 100);

    dx = ex - sx;
    dy = ey - sy;

    if (dx * dx + dy * dy > 3 * 3) {
        int rx =  dx + dy;
        int ry = -dx + dy;
        int length = ff_sqrt((rx * rx + ry * ry) << 8);

        // FIXME subpixel accuracy
        rx = ROUNDED_DIV(rx * 3 << 4, length);
        ry = ROUNDED_DIV(ry * 3 << 4, length);

        draw_line(buf, sx, sy, sx + rx, sy + ry, w, h, stride, color);
        draw_line(buf, sx, sy, sx - ry, sy + rx, w, h, stride, color);
    }
    draw_line(buf, sx, sy, ex, ey, w, h, stride, color);
}

/**
 * Print debugging info for the given picture.
 */
void ff_print_debug_info2(AVCodecContext *avctx, Picture *p, AVFrame *pict, uint8_t *mbskip_table,
                         int *low_delay,
                         int mb_width, int mb_height, int mb_stride, int quarter_sample)
{
    if (avctx->hwaccel || !p || !p->mb_type
        || (avctx->codec->capabilities&CODEC_CAP_HWACCEL_VDPAU))
        return;


    if (avctx->debug & (FF_DEBUG_SKIP | FF_DEBUG_QP | FF_DEBUG_MB_TYPE)) {
        int x,y;

        av_log(avctx, AV_LOG_DEBUG, "New frame, type: %c\n",
               av_get_picture_type_char(pict->pict_type));
        for (y = 0; y < mb_height; y++) {
            for (x = 0; x < mb_width; x++) {
                if (avctx->debug & FF_DEBUG_SKIP) {
                    int count = mbskip_table[x + y * mb_stride];
                    if (count > 9)
                        count = 9;
                    av_log(avctx, AV_LOG_DEBUG, "%1d", count);
                }
                if (avctx->debug & FF_DEBUG_QP) {
                    av_log(avctx, AV_LOG_DEBUG, "%2d",
                           p->qscale_table[x + y * mb_stride]);
                }
                if (avctx->debug & FF_DEBUG_MB_TYPE) {
                    int mb_type = p->mb_type[x + y * mb_stride];
                    // Type & MV direction
                    if (IS_PCM(mb_type))
                        av_log(avctx, AV_LOG_DEBUG, "P");
                    else if (IS_INTRA(mb_type) && IS_ACPRED(mb_type))
                        av_log(avctx, AV_LOG_DEBUG, "A");
                    else if (IS_INTRA4x4(mb_type))
                        av_log(avctx, AV_LOG_DEBUG, "i");
                    else if (IS_INTRA16x16(mb_type))
                        av_log(avctx, AV_LOG_DEBUG, "I");
                    else if (IS_DIRECT(mb_type) && IS_SKIP(mb_type))
                        av_log(avctx, AV_LOG_DEBUG, "d");
                    else if (IS_DIRECT(mb_type))
                        av_log(avctx, AV_LOG_DEBUG, "D");
                    else if (IS_GMC(mb_type) && IS_SKIP(mb_type))
                        av_log(avctx, AV_LOG_DEBUG, "g");
                    else if (IS_GMC(mb_type))
                        av_log(avctx, AV_LOG_DEBUG, "G");
                    else if (IS_SKIP(mb_type))
                        av_log(avctx, AV_LOG_DEBUG, "S");
                    else if (!USES_LIST(mb_type, 1))
                        av_log(avctx, AV_LOG_DEBUG, ">");
                    else if (!USES_LIST(mb_type, 0))
                        av_log(avctx, AV_LOG_DEBUG, "<");
                    else {
                        av_assert2(USES_LIST(mb_type, 0) && USES_LIST(mb_type, 1));
                        av_log(avctx, AV_LOG_DEBUG, "X");
                    }

                    // segmentation
                    if (IS_8X8(mb_type))
                        av_log(avctx, AV_LOG_DEBUG, "+");
                    else if (IS_16X8(mb_type))
                        av_log(avctx, AV_LOG_DEBUG, "-");
                    else if (IS_8X16(mb_type))
                        av_log(avctx, AV_LOG_DEBUG, "|");
                    else if (IS_INTRA(mb_type) || IS_16X16(mb_type))
                        av_log(avctx, AV_LOG_DEBUG, " ");
                    else
                        av_log(avctx, AV_LOG_DEBUG, "?");


                    if (IS_INTERLACED(mb_type))
                        av_log(avctx, AV_LOG_DEBUG, "=");
                    else
                        av_log(avctx, AV_LOG_DEBUG, " ");
                }
            }
            av_log(avctx, AV_LOG_DEBUG, "\n");
        }
    }

    if ((avctx->debug & (FF_DEBUG_VIS_QP | FF_DEBUG_VIS_MB_TYPE)) ||
        (avctx->debug_mv)) {
        const int shift = 1 + quarter_sample;
        int mb_y;
        uint8_t *ptr;
        int i;
        int h_chroma_shift, v_chroma_shift, block_height;
        const int width          = avctx->width;
        const int height         = avctx->height;
        const int mv_sample_log2 = avctx->codec_id == AV_CODEC_ID_H264 || avctx->codec_id == AV_CODEC_ID_SVQ3 ? 2 : 1;
        const int mv_stride      = (mb_width << mv_sample_log2) +
                                   (avctx->codec->id == AV_CODEC_ID_H264 ? 0 : 1);

        *low_delay = 0; // needed to see the vectors without trashing the buffers

        avcodec_get_chroma_sub_sample(avctx->pix_fmt, &h_chroma_shift, &v_chroma_shift);

        av_frame_make_writable(pict);

        pict->opaque = NULL;
        ptr          = pict->data[0];
        block_height = 16 >> v_chroma_shift;

        for (mb_y = 0; mb_y < mb_height; mb_y++) {
            int mb_x;
            for (mb_x = 0; mb_x < mb_width; mb_x++) {
                const int mb_index = mb_x + mb_y * mb_stride;
                if ((avctx->debug_mv) && p->motion_val[0]) {
                    int type;
                    for (type = 0; type < 3; type++) {
                        int direction = 0;
                        switch (type) {
                        case 0:
                            if ((!(avctx->debug_mv & FF_DEBUG_VIS_MV_P_FOR)) ||
                                (pict->pict_type!= AV_PICTURE_TYPE_P))
                                continue;
                            direction = 0;
                            break;
                        case 1:
                            if ((!(avctx->debug_mv & FF_DEBUG_VIS_MV_B_FOR)) ||
                                (pict->pict_type!= AV_PICTURE_TYPE_B))
                                continue;
                            direction = 0;
                            break;
                        case 2:
                            if ((!(avctx->debug_mv & FF_DEBUG_VIS_MV_B_BACK)) ||
                                (pict->pict_type!= AV_PICTURE_TYPE_B))
                                continue;
                            direction = 1;
                            break;
                        }
                        if (!USES_LIST(p->mb_type[mb_index], direction))
                            continue;

                        if (IS_8X8(p->mb_type[mb_index])) {
                            int i;
                            for (i = 0; i < 4; i++) {
                                int sx = mb_x * 16 + 4 + 8 * (i & 1);
                                int sy = mb_y * 16 + 4 + 8 * (i >> 1);
                                int xy = (mb_x * 2 + (i & 1) +
                                          (mb_y * 2 + (i >> 1)) * mv_stride) << (mv_sample_log2 - 1);
                                int mx = (p->motion_val[direction][xy][0] >> shift) + sx;
                                int my = (p->motion_val[direction][xy][1] >> shift) + sy;
                                draw_arrow(ptr, sx, sy, mx, my, width,
                                           height, pict->linesize[0], 100);
                            }
                        } else if (IS_16X8(p->mb_type[mb_index])) {
                            int i;
                            for (i = 0; i < 2; i++) {
                                int sx = mb_x * 16 + 8;
                                int sy = mb_y * 16 + 4 + 8 * i;
                                int xy = (mb_x * 2 + (mb_y * 2 + i) * mv_stride) << (mv_sample_log2 - 1);
                                int mx = (p->motion_val[direction][xy][0] >> shift);
                                int my = (p->motion_val[direction][xy][1] >> shift);

                                if (IS_INTERLACED(p->mb_type[mb_index]))
                                    my *= 2;

                            draw_arrow(ptr, sx, sy, mx + sx, my + sy, width,
                                       height, pict->linesize[0], 100);
                            }
                        } else if (IS_8X16(p->mb_type[mb_index])) {
                            int i;
                            for (i = 0; i < 2; i++) {
                                int sx = mb_x * 16 + 4 + 8 * i;
                                int sy = mb_y * 16 + 8;
                                int xy = (mb_x * 2 + i + mb_y * 2 * mv_stride) << (mv_sample_log2 - 1);
                                int mx = p->motion_val[direction][xy][0] >> shift;
                                int my = p->motion_val[direction][xy][1] >> shift;

                                if (IS_INTERLACED(p->mb_type[mb_index]))
                                    my *= 2;

                                draw_arrow(ptr, sx, sy, mx + sx, my + sy, width,
                                           height, pict->linesize[0], 100);
                            }
                        } else {
                              int sx= mb_x * 16 + 8;
                              int sy= mb_y * 16 + 8;
                              int xy= (mb_x + mb_y * mv_stride) << mv_sample_log2;
                              int mx= (p->motion_val[direction][xy][0]>>shift) + sx;
                              int my= (p->motion_val[direction][xy][1]>>shift) + sy;
                              draw_arrow(ptr, sx, sy, mx, my, width, height, pict->linesize[0], 100);
                        }
                    }
                }
                if ((avctx->debug & FF_DEBUG_VIS_QP)) {
                    uint64_t c = (p->qscale_table[mb_index] * 128 / 31) *
                                 0x0101010101010101ULL;
                    int y;
                    for (y = 0; y < block_height; y++) {
                        *(uint64_t *)(pict->data[1] + 8 * mb_x +
                                      (block_height * mb_y + y) *
                                      pict->linesize[1]) = c;
                        *(uint64_t *)(pict->data[2] + 8 * mb_x +
                                      (block_height * mb_y + y) *
                                      pict->linesize[2]) = c;
                    }
                }
                if ((avctx->debug & FF_DEBUG_VIS_MB_TYPE) &&
                    p->motion_val[0]) {
                    int mb_type = p->mb_type[mb_index];
                    uint64_t u,v;
                    int y;
#define COLOR(theta, r) \
    u = (int)(128 + r * cos(theta * 3.141592 / 180)); \
    v = (int)(128 + r * sin(theta * 3.141592 / 180));


                    u = v = 128;
                    if (IS_PCM(mb_type)) {
                        COLOR(120, 48)
                    } else if ((IS_INTRA(mb_type) && IS_ACPRED(mb_type)) ||
                               IS_INTRA16x16(mb_type)) {
                        COLOR(30, 48)
                    } else if (IS_INTRA4x4(mb_type)) {
                        COLOR(90, 48)
                    } else if (IS_DIRECT(mb_type) && IS_SKIP(mb_type)) {
                        // COLOR(120, 48)
                    } else if (IS_DIRECT(mb_type)) {
                        COLOR(150, 48)
                    } else if (IS_GMC(mb_type) && IS_SKIP(mb_type)) {
                        COLOR(170, 48)
                    } else if (IS_GMC(mb_type)) {
                        COLOR(190, 48)
                    } else if (IS_SKIP(mb_type)) {
                        // COLOR(180, 48)
                    } else if (!USES_LIST(mb_type, 1)) {
                        COLOR(240, 48)
                    } else if (!USES_LIST(mb_type, 0)) {
                        COLOR(0, 48)
                    } else {
                        av_assert2(USES_LIST(mb_type, 0) && USES_LIST(mb_type, 1));
                        COLOR(300,48)
                    }

                    u *= 0x0101010101010101ULL;
                    v *= 0x0101010101010101ULL;
                    for (y = 0; y < block_height; y++) {
                        *(uint64_t *)(pict->data[1] + 8 * mb_x +
                                      (block_height * mb_y + y) * pict->linesize[1]) = u;
                        *(uint64_t *)(pict->data[2] + 8 * mb_x +
                                      (block_height * mb_y + y) * pict->linesize[2]) = v;
                    }

                    // segmentation
                    if (IS_8X8(mb_type) || IS_16X8(mb_type)) {
                        *(uint64_t *)(pict->data[0] + 16 * mb_x + 0 +
                                      (16 * mb_y + 8) * pict->linesize[0]) ^= 0x8080808080808080ULL;
                        *(uint64_t *)(pict->data[0] + 16 * mb_x + 8 +
                                      (16 * mb_y + 8) * pict->linesize[0]) ^= 0x8080808080808080ULL;
                    }
                    if (IS_8X8(mb_type) || IS_8X16(mb_type)) {
                        for (y = 0; y < 16; y++)
                            pict->data[0][16 * mb_x + 8 + (16 * mb_y + y) *
                                          pict->linesize[0]] ^= 0x80;
                    }
                    if (IS_8X8(mb_type) && mv_sample_log2 >= 2) {
                        int dm = 1 << (mv_sample_log2 - 2);
                        for (i = 0; i < 4; i++) {
                            int sx = mb_x * 16 + 8 * (i & 1);
                            int sy = mb_y * 16 + 8 * (i >> 1);
                            int xy = (mb_x * 2 + (i & 1) +
                                     (mb_y * 2 + (i >> 1)) * mv_stride) << (mv_sample_log2 - 1);
                            // FIXME bidir
                            int32_t *mv = (int32_t *) &p->motion_val[0][xy];
                            if (mv[0] != mv[dm] ||
                                mv[dm * mv_stride] != mv[dm * (mv_stride + 1)])
                                for (y = 0; y < 8; y++)
                                    pict->data[0][sx + 4 + (sy + y) * pict->linesize[0]] ^= 0x80;
                            if (mv[0] != mv[dm * mv_stride] || mv[dm] != mv[dm * (mv_stride + 1)])
                                *(uint64_t *)(pict->data[0] + sx + (sy + 4) *
                                              pict->linesize[0]) ^= 0x8080808080808080ULL;
                        }
                    }

                    if (IS_INTERLACED(mb_type) &&
                        avctx->codec->id == AV_CODEC_ID_H264) {
                        // hmm
                    }
                }
                mbskip_table[mb_index] = 0;
            }
        }
    }
}

void ff_print_debug_info(MpegEncContext *s, Picture *p, AVFrame *pict)
{
    ff_print_debug_info2(s->avctx, p, pict, s->mbskip_table, &s->low_delay,
                         s->mb_width, s->mb_height, s->mb_stride, s->quarter_sample);
}

int ff_mpv_export_qp_table(MpegEncContext *s, AVFrame *f, Picture *p, int qp_type)
{
    AVBufferRef *ref = av_buffer_ref(p->qscale_table_buf);
    int offset = 2*s->mb_stride + 1;
    if(!ref)
        return AVERROR(ENOMEM);
    av_assert0(ref->size >= offset + s->mb_stride * ((f->height+15)/16));
    ref->size -= offset;
    ref->data += offset;
    return av_frame_set_qp_table(f, ref, s->mb_stride, qp_type);
}

static inline int hpel_motion_lowres(MpegEncContext *s,
                                     uint8_t *dest, uint8_t *src,
                                     int field_based, int field_select,
                                     int src_x, int src_y,
                                     int width, int height, int stride,
                                     int h_edge_pos, int v_edge_pos,
                                     int w, int h, h264_chroma_mc_func *pix_op,
                                     int motion_x, int motion_y)
{
    const int lowres   = s->avctx->lowres;
    const int op_index = FFMIN(lowres, 3);
    const int s_mask   = (2 << lowres) - 1;
    int emu = 0;
    int sx, sy;

    if (s->quarter_sample) {
        motion_x /= 2;
        motion_y /= 2;
    }

    sx = motion_x & s_mask;
    sy = motion_y & s_mask;
    src_x += motion_x >> lowres + 1;
    src_y += motion_y >> lowres + 1;

    src   += src_y * stride + src_x;

    if ((unsigned)src_x > FFMAX( h_edge_pos - (!!sx) - w,                 0) ||
        (unsigned)src_y > FFMAX((v_edge_pos >> field_based) - (!!sy) - h, 0)) {
        s->vdsp.emulated_edge_mc(s->edge_emu_buffer, src, s->linesize, w + 1,
                                (h + 1) << field_based, src_x,
                                src_y   << field_based,
                                h_edge_pos,
                                v_edge_pos);
        src = s->edge_emu_buffer;
        emu = 1;
    }

    sx = (sx << 2) >> lowres;
    sy = (sy << 2) >> lowres;
    if (field_select)
        src += s->linesize;
    pix_op[op_index](dest, src, stride, h, sx, sy);
    return emu;
}

/* apply one mpeg motion vector to the three components */
static av_always_inline void mpeg_motion_lowres(MpegEncContext *s,
                                                uint8_t *dest_y,
                                                uint8_t *dest_cb,
                                                uint8_t *dest_cr,
                                                int field_based,
                                                int bottom_field,
                                                int field_select,
                                                uint8_t **ref_picture,
                                                h264_chroma_mc_func *pix_op,
                                                int motion_x, int motion_y,
                                                int h, int mb_y)
{
    uint8_t *ptr_y, *ptr_cb, *ptr_cr;
    int mx, my, src_x, src_y, uvsrc_x, uvsrc_y, uvlinesize, linesize, sx, sy,
        uvsx, uvsy;
    const int lowres     = s->avctx->lowres;
    const int op_index   = FFMIN(lowres-1+s->chroma_x_shift, 3);
    const int block_s    = 8>>lowres;
    const int s_mask     = (2 << lowres) - 1;
    const int h_edge_pos = s->h_edge_pos >> lowres;
    const int v_edge_pos = s->v_edge_pos >> lowres;
    linesize   = s->current_picture.f.linesize[0] << field_based;
    uvlinesize = s->current_picture.f.linesize[1] << field_based;

    // FIXME obviously not perfect but qpel will not work in lowres anyway
    if (s->quarter_sample) {
        motion_x /= 2;
        motion_y /= 2;
    }

    if(field_based){
        motion_y += (bottom_field - field_select)*((1 << lowres)-1);
    }

    sx = motion_x & s_mask;
    sy = motion_y & s_mask;
    src_x = s->mb_x * 2 * block_s + (motion_x >> lowres + 1);
    src_y = (mb_y * 2 * block_s >> field_based) + (motion_y >> lowres + 1);

    if (s->out_format == FMT_H263) {
        uvsx    = ((motion_x >> 1) & s_mask) | (sx & 1);
        uvsy    = ((motion_y >> 1) & s_mask) | (sy & 1);
        uvsrc_x = src_x >> 1;
        uvsrc_y = src_y >> 1;
    } else if (s->out_format == FMT_H261) {
        // even chroma mv's are full pel in H261
        mx      = motion_x / 4;
        my      = motion_y / 4;
        uvsx    = (2 * mx) & s_mask;
        uvsy    = (2 * my) & s_mask;
        uvsrc_x = s->mb_x * block_s + (mx >> lowres);
        uvsrc_y =    mb_y * block_s + (my >> lowres);
    } else {
        if(s->chroma_y_shift){
            mx      = motion_x / 2;
            my      = motion_y / 2;
            uvsx    = mx & s_mask;
            uvsy    = my & s_mask;
            uvsrc_x = s->mb_x * block_s                 + (mx >> lowres + 1);
            uvsrc_y =   (mb_y * block_s >> field_based) + (my >> lowres + 1);
        } else {
            if(s->chroma_x_shift){
            //Chroma422
                mx = motion_x / 2;
                uvsx = mx & s_mask;
                uvsy = motion_y & s_mask;
                uvsrc_y = src_y;
                uvsrc_x = s->mb_x*block_s               + (mx >> (lowres+1));
            } else {
            //Chroma444
                uvsx = motion_x & s_mask;
                uvsy = motion_y & s_mask;
                uvsrc_x = src_x;
                uvsrc_y = src_y;
            }
        }
    }

    ptr_y  = ref_picture[0] + src_y   * linesize   + src_x;
    ptr_cb = ref_picture[1] + uvsrc_y * uvlinesize + uvsrc_x;
    ptr_cr = ref_picture[2] + uvsrc_y * uvlinesize + uvsrc_x;

    if ((unsigned) src_x > FFMAX( h_edge_pos - (!!sx) - 2 * block_s,       0) || uvsrc_y<0 ||
        (unsigned) src_y > FFMAX((v_edge_pos >> field_based) - (!!sy) - h, 0)) {
        s->vdsp.emulated_edge_mc(s->edge_emu_buffer, ptr_y,
                                linesize >> field_based, 17, 17 + field_based,
                                src_x, src_y << field_based, h_edge_pos,
                                v_edge_pos);
        ptr_y = s->edge_emu_buffer;
        if (!CONFIG_GRAY || !(s->flags & CODEC_FLAG_GRAY)) {
            uint8_t *uvbuf = s->edge_emu_buffer + 18 * s->linesize;
            s->vdsp.emulated_edge_mc(uvbuf , ptr_cb, uvlinesize >> field_based, 9,
                                    9 + field_based,
                                    uvsrc_x, uvsrc_y << field_based,
                                    h_edge_pos >> 1, v_edge_pos >> 1);
            s->vdsp.emulated_edge_mc(uvbuf + 16, ptr_cr, uvlinesize >> field_based, 9,
                                    9 + field_based,
                                    uvsrc_x, uvsrc_y << field_based,
                                    h_edge_pos >> 1, v_edge_pos >> 1);
            ptr_cb = uvbuf;
            ptr_cr = uvbuf + 16;
        }
    }

    // FIXME use this for field pix too instead of the obnoxious hack which changes picture.f.data
    if (bottom_field) {
        dest_y  += s->linesize;
        dest_cb += s->uvlinesize;
        dest_cr += s->uvlinesize;
    }

    if (field_select) {
        ptr_y   += s->linesize;
        ptr_cb  += s->uvlinesize;
        ptr_cr  += s->uvlinesize;
    }

    sx = (sx << 2) >> lowres;
    sy = (sy << 2) >> lowres;
    pix_op[lowres - 1](dest_y, ptr_y, linesize, h, sx, sy);

    if (!CONFIG_GRAY || !(s->flags & CODEC_FLAG_GRAY)) {
        int hc = s->chroma_y_shift ? (h+1-bottom_field)>>1 : h;
        uvsx = (uvsx << 2) >> lowres;
        uvsy = (uvsy << 2) >> lowres;
        if (hc) {
            pix_op[op_index](dest_cb, ptr_cb, uvlinesize, hc, uvsx, uvsy);
            pix_op[op_index](dest_cr, ptr_cr, uvlinesize, hc, uvsx, uvsy);
        }
    }
    // FIXME h261 lowres loop filter
}

static inline void chroma_4mv_motion_lowres(MpegEncContext *s,
                                            uint8_t *dest_cb, uint8_t *dest_cr,
                                            uint8_t **ref_picture,
                                            h264_chroma_mc_func * pix_op,
                                            int mx, int my)
{
    const int lowres     = s->avctx->lowres;
    const int op_index   = FFMIN(lowres, 3);
    const int block_s    = 8 >> lowres;
    const int s_mask     = (2 << lowres) - 1;
    const int h_edge_pos = s->h_edge_pos >> lowres + 1;
    const int v_edge_pos = s->v_edge_pos >> lowres + 1;
    int emu = 0, src_x, src_y, offset, sx, sy;
    uint8_t *ptr;

    if (s->quarter_sample) {
        mx /= 2;
        my /= 2;
    }

    /* In case of 8X8, we construct a single chroma motion vector
       with a special rounding */
    mx = ff_h263_round_chroma(mx);
    my = ff_h263_round_chroma(my);

    sx = mx & s_mask;
    sy = my & s_mask;
    src_x = s->mb_x * block_s + (mx >> lowres + 1);
    src_y = s->mb_y * block_s + (my >> lowres + 1);

    offset = src_y * s->uvlinesize + src_x;
    ptr = ref_picture[1] + offset;
    if (s->flags & CODEC_FLAG_EMU_EDGE) {
        if ((unsigned) src_x > FFMAX(h_edge_pos - (!!sx) - block_s, 0) ||
            (unsigned) src_y > FFMAX(v_edge_pos - (!!sy) - block_s, 0)) {
            s->vdsp.emulated_edge_mc(s->edge_emu_buffer, ptr, s->uvlinesize,
                                    9, 9, src_x, src_y, h_edge_pos, v_edge_pos);
            ptr = s->edge_emu_buffer;
            emu = 1;
        }
    }
    sx = (sx << 2) >> lowres;
    sy = (sy << 2) >> lowres;
    pix_op[op_index](dest_cb, ptr, s->uvlinesize, block_s, sx, sy);

    ptr = ref_picture[2] + offset;
    if (emu) {
        s->vdsp.emulated_edge_mc(s->edge_emu_buffer, ptr, s->uvlinesize, 9, 9,
                                src_x, src_y, h_edge_pos, v_edge_pos);
        ptr = s->edge_emu_buffer;
    }
    pix_op[op_index](dest_cr, ptr, s->uvlinesize, block_s, sx, sy);
}

/**
 * motion compensation of a single macroblock
 * @param s context
 * @param dest_y luma destination pointer
 * @param dest_cb chroma cb/u destination pointer
 * @param dest_cr chroma cr/v destination pointer
 * @param dir direction (0->forward, 1->backward)
 * @param ref_picture array[3] of pointers to the 3 planes of the reference picture
 * @param pix_op halfpel motion compensation function (average or put normally)
 * the motion vectors are taken from s->mv and the MV type from s->mv_type
 */
static inline void MPV_motion_lowres(MpegEncContext *s,
                                     uint8_t *dest_y, uint8_t *dest_cb,
                                     uint8_t *dest_cr,
                                     int dir, uint8_t **ref_picture,
                                     h264_chroma_mc_func *pix_op)
{
    int mx, my;
    int mb_x, mb_y, i;
    const int lowres  = s->avctx->lowres;
    const int block_s = 8 >>lowres;

    mb_x = s->mb_x;
    mb_y = s->mb_y;

    switch (s->mv_type) {
    case MV_TYPE_16X16:
        mpeg_motion_lowres(s, dest_y, dest_cb, dest_cr,
                           0, 0, 0,
                           ref_picture, pix_op,
                           s->mv[dir][0][0], s->mv[dir][0][1],
                           2 * block_s, mb_y);
        break;
    case MV_TYPE_8X8:
        mx = 0;
        my = 0;
        for (i = 0; i < 4; i++) {
            hpel_motion_lowres(s, dest_y + ((i & 1) + (i >> 1) *
                               s->linesize) * block_s,
                               ref_picture[0], 0, 0,
                               (2 * mb_x + (i & 1)) * block_s,
                               (2 * mb_y + (i >> 1)) * block_s,
                               s->width, s->height, s->linesize,
                               s->h_edge_pos >> lowres, s->v_edge_pos >> lowres,
                               block_s, block_s, pix_op,
                               s->mv[dir][i][0], s->mv[dir][i][1]);

            mx += s->mv[dir][i][0];
            my += s->mv[dir][i][1];
        }

        if (!CONFIG_GRAY || !(s->flags & CODEC_FLAG_GRAY))
            chroma_4mv_motion_lowres(s, dest_cb, dest_cr, ref_picture,
                                     pix_op, mx, my);
        break;
    case MV_TYPE_FIELD:
        if (s->picture_structure == PICT_FRAME) {
            /* top field */
            mpeg_motion_lowres(s, dest_y, dest_cb, dest_cr,
                               1, 0, s->field_select[dir][0],
                               ref_picture, pix_op,
                               s->mv[dir][0][0], s->mv[dir][0][1],
                               block_s, mb_y);
            /* bottom field */
            mpeg_motion_lowres(s, dest_y, dest_cb, dest_cr,
                               1, 1, s->field_select[dir][1],
                               ref_picture, pix_op,
                               s->mv[dir][1][0], s->mv[dir][1][1],
                               block_s, mb_y);
        } else {
            if (s->picture_structure != s->field_select[dir][0] + 1 &&
                s->pict_type != AV_PICTURE_TYPE_B && !s->first_field) {
                ref_picture = s->current_picture_ptr->f.data;

            }
            mpeg_motion_lowres(s, dest_y, dest_cb, dest_cr,
                               0, 0, s->field_select[dir][0],
                               ref_picture, pix_op,
                               s->mv[dir][0][0],
                               s->mv[dir][0][1], 2 * block_s, mb_y >> 1);
            }
        break;
    case MV_TYPE_16X8:
        for (i = 0; i < 2; i++) {
            uint8_t **ref2picture;

            if (s->picture_structure == s->field_select[dir][i] + 1 ||
                s->pict_type == AV_PICTURE_TYPE_B || s->first_field) {
                ref2picture = ref_picture;
            } else {
                ref2picture = s->current_picture_ptr->f.data;
            }

            mpeg_motion_lowres(s, dest_y, dest_cb, dest_cr,
                               0, 0, s->field_select[dir][i],
                               ref2picture, pix_op,
                               s->mv[dir][i][0], s->mv[dir][i][1] +
                               2 * block_s * i, block_s, mb_y >> 1);

            dest_y  +=  2 * block_s *  s->linesize;
            dest_cb += (2 * block_s >> s->chroma_y_shift) * s->uvlinesize;
            dest_cr += (2 * block_s >> s->chroma_y_shift) * s->uvlinesize;
        }
        break;
    case MV_TYPE_DMV:
        if (s->picture_structure == PICT_FRAME) {
            for (i = 0; i < 2; i++) {
                int j;
                for (j = 0; j < 2; j++) {
                    mpeg_motion_lowres(s, dest_y, dest_cb, dest_cr,
                                       1, j, j ^ i,
                                       ref_picture, pix_op,
                                       s->mv[dir][2 * i + j][0],
                                       s->mv[dir][2 * i + j][1],
                                       block_s, mb_y);
                }
                pix_op = s->h264chroma.avg_h264_chroma_pixels_tab;
            }
        } else {
            for (i = 0; i < 2; i++) {
                mpeg_motion_lowres(s, dest_y, dest_cb, dest_cr,
                                   0, 0, s->picture_structure != i + 1,
                                   ref_picture, pix_op,
                                   s->mv[dir][2 * i][0],s->mv[dir][2 * i][1],
                                   2 * block_s, mb_y >> 1);

                // after put we make avg of the same block
                pix_op = s->h264chroma.avg_h264_chroma_pixels_tab;

                // opposite parity is always in the same
                // frame if this is second field
                if (!s->first_field) {
                    ref_picture = s->current_picture_ptr->f.data;
                }
            }
        }
        break;
    default:
        av_assert2(0);
    }
}

/**
 * find the lowest MB row referenced in the MVs
 */
int ff_MPV_lowest_referenced_row(MpegEncContext *s, int dir)
{
    int my_max = INT_MIN, my_min = INT_MAX, qpel_shift = !s->quarter_sample;
    int my, off, i, mvs;

    if (s->picture_structure != PICT_FRAME || s->mcsel)
        goto unhandled;

    switch (s->mv_type) {
        case MV_TYPE_16X16:
            mvs = 1;
            break;
        case MV_TYPE_16X8:
            mvs = 2;
            break;
        case MV_TYPE_8X8:
            mvs = 4;
            break;
        default:
            goto unhandled;
    }

    for (i = 0; i < mvs; i++) {
        my = s->mv[dir][i][1]<<qpel_shift;
        my_max = FFMAX(my_max, my);
        my_min = FFMIN(my_min, my);
    }

    off = (FFMAX(-my_min, my_max) + 63) >> 6;

    return FFMIN(FFMAX(s->mb_y + off, 0), s->mb_height-1);
unhandled:
    return s->mb_height-1;
}

/* put block[] to dest[] */
static inline void put_dct(MpegEncContext *s,
                           int16_t *block, int i, uint8_t *dest, int line_size, int qscale)
{
    s->dct_unquantize_intra(s, block, i, qscale);
    s->dsp.idct_put (dest, line_size, block);
}

/* add block[] to dest[] */
static inline void add_dct(MpegEncContext *s,
                           int16_t *block, int i, uint8_t *dest, int line_size)
{
    if (s->block_last_index[i] >= 0) {
        s->dsp.idct_add (dest, line_size, block);
    }
}

static inline void add_dequant_dct(MpegEncContext *s,
                           int16_t *block, int i, uint8_t *dest, int line_size, int qscale)
{
    if (s->block_last_index[i] >= 0) {
        s->dct_unquantize_inter(s, block, i, qscale);

        s->dsp.idct_add (dest, line_size, block);
    }
}

/**
 * Clean dc, ac, coded_block for the current non-intra MB.
 */
void ff_clean_intra_table_entries(MpegEncContext *s)
{
    int wrap = s->b8_stride;
    int xy = s->block_index[0];

    s->dc_val[0][xy           ] =
    s->dc_val[0][xy + 1       ] =
    s->dc_val[0][xy     + wrap] =
    s->dc_val[0][xy + 1 + wrap] = 1024;
    /* ac pred */
    memset(s->ac_val[0][xy       ], 0, 32 * sizeof(int16_t));
    memset(s->ac_val[0][xy + wrap], 0, 32 * sizeof(int16_t));
    if (s->msmpeg4_version>=3) {
        s->coded_block[xy           ] =
        s->coded_block[xy + 1       ] =
        s->coded_block[xy     + wrap] =
        s->coded_block[xy + 1 + wrap] = 0;
    }
    /* chroma */
    wrap = s->mb_stride;
    xy = s->mb_x + s->mb_y * wrap;
    s->dc_val[1][xy] =
    s->dc_val[2][xy] = 1024;
    /* ac pred */
    memset(s->ac_val[1][xy], 0, 16 * sizeof(int16_t));
    memset(s->ac_val[2][xy], 0, 16 * sizeof(int16_t));

    s->mbintra_table[xy]= 0;
}

/* generic function called after a macroblock has been parsed by the
   decoder or after it has been encoded by the encoder.

   Important variables used:
   s->mb_intra : true if intra macroblock
   s->mv_dir   : motion vector direction
   s->mv_type  : motion vector type
   s->mv       : motion vector
   s->interlaced_dct : true if interlaced dct used (mpeg2)
 */
static av_always_inline
void MPV_decode_mb_internal(MpegEncContext *s, int16_t block[12][64],
                            int lowres_flag, int is_mpeg12)
{
    const int mb_xy = s->mb_y * s->mb_stride + s->mb_x;
    if(CONFIG_MPEG_XVMC_DECODER && s->avctx->xvmc_acceleration){
        ff_xvmc_decode_mb(s);//xvmc uses pblocks
        return;
    }

    if(s->avctx->debug&FF_DEBUG_DCT_COEFF) {
       /* print DCT coefficients */
       int i,j;
       av_log(s->avctx, AV_LOG_DEBUG, "DCT coeffs of MB at %dx%d:\n", s->mb_x, s->mb_y);
       for(i=0; i<6; i++){
           for(j=0; j<64; j++){
               av_log(s->avctx, AV_LOG_DEBUG, "%5d", block[i][s->dsp.idct_permutation[j]]);
           }
           av_log(s->avctx, AV_LOG_DEBUG, "\n");
       }
    }

    s->current_picture.qscale_table[mb_xy] = s->qscale;

    /* update DC predictors for P macroblocks */
    if (!s->mb_intra) {
        if (!is_mpeg12 && (s->h263_pred || s->h263_aic)) {
            if(s->mbintra_table[mb_xy])
                ff_clean_intra_table_entries(s);
        } else {
            s->last_dc[0] =
            s->last_dc[1] =
            s->last_dc[2] = 128 << s->intra_dc_precision;
        }
    }
    else if (!is_mpeg12 && (s->h263_pred || s->h263_aic))
        s->mbintra_table[mb_xy]=1;

    if ((s->flags&CODEC_FLAG_PSNR) || !(s->encoding && (s->intra_only || s->pict_type==AV_PICTURE_TYPE_B) && s->avctx->mb_decision != FF_MB_DECISION_RD)) { //FIXME precalc
        uint8_t *dest_y, *dest_cb, *dest_cr;
        int dct_linesize, dct_offset;
        op_pixels_func (*op_pix)[4];
        qpel_mc_func (*op_qpix)[16];
        const int linesize   = s->current_picture.f.linesize[0]; //not s->linesize as this would be wrong for field pics
        const int uvlinesize = s->current_picture.f.linesize[1];
        const int readable= s->pict_type != AV_PICTURE_TYPE_B || s->encoding || s->avctx->draw_horiz_band || lowres_flag;
        const int block_size= lowres_flag ? 8>>s->avctx->lowres : 8;

        /* avoid copy if macroblock skipped in last frame too */
        /* skip only during decoding as we might trash the buffers during encoding a bit */
        if(!s->encoding){
            uint8_t *mbskip_ptr = &s->mbskip_table[mb_xy];

            if (s->mb_skipped) {
                s->mb_skipped= 0;
                av_assert2(s->pict_type!=AV_PICTURE_TYPE_I);
                *mbskip_ptr = 1;
            } else if(!s->current_picture.reference) {
                *mbskip_ptr = 1;
            } else{
                *mbskip_ptr = 0; /* not skipped */
            }
        }

        dct_linesize = linesize << s->interlaced_dct;
        dct_offset   = s->interlaced_dct ? linesize : linesize * block_size;

        if(readable){
            dest_y=  s->dest[0];
            dest_cb= s->dest[1];
            dest_cr= s->dest[2];
        }else{
            dest_y = s->b_scratchpad;
            dest_cb= s->b_scratchpad+16*linesize;
            dest_cr= s->b_scratchpad+32*linesize;
        }

        if (!s->mb_intra) {
            /* motion handling */
            /* decoding or more than one mb_type (MC was already done otherwise) */
            if(!s->encoding){

                if(HAVE_THREADS && s->avctx->active_thread_type&FF_THREAD_FRAME) {
                    if (s->mv_dir & MV_DIR_FORWARD) {
                        ff_thread_await_progress(&s->last_picture_ptr->tf,
                                                 ff_MPV_lowest_referenced_row(s, 0),
                                                 0);
                    }
                    if (s->mv_dir & MV_DIR_BACKWARD) {
                        ff_thread_await_progress(&s->next_picture_ptr->tf,
                                                 ff_MPV_lowest_referenced_row(s, 1),
                                                 0);
                    }
                }

                if(lowres_flag){
                    h264_chroma_mc_func *op_pix = s->h264chroma.put_h264_chroma_pixels_tab;

                    if (s->mv_dir & MV_DIR_FORWARD) {
                        MPV_motion_lowres(s, dest_y, dest_cb, dest_cr, 0, s->last_picture.f.data, op_pix);
                        op_pix = s->h264chroma.avg_h264_chroma_pixels_tab;
                    }
                    if (s->mv_dir & MV_DIR_BACKWARD) {
                        MPV_motion_lowres(s, dest_y, dest_cb, dest_cr, 1, s->next_picture.f.data, op_pix);
                    }
                }else{
                    op_qpix= s->me.qpel_put;
                    if ((!s->no_rounding) || s->pict_type==AV_PICTURE_TYPE_B){
                        op_pix = s->hdsp.put_pixels_tab;
                    }else{
                        op_pix = s->hdsp.put_no_rnd_pixels_tab;
                    }
                    if (s->mv_dir & MV_DIR_FORWARD) {
                        ff_MPV_motion(s, dest_y, dest_cb, dest_cr, 0, s->last_picture.f.data, op_pix, op_qpix);
                        op_pix = s->hdsp.avg_pixels_tab;
                        op_qpix= s->me.qpel_avg;
                    }
                    if (s->mv_dir & MV_DIR_BACKWARD) {
                        ff_MPV_motion(s, dest_y, dest_cb, dest_cr, 1, s->next_picture.f.data, op_pix, op_qpix);
                    }
                }
            }

            /* skip dequant / idct if we are really late ;) */
            if(s->avctx->skip_idct){
                if(  (s->avctx->skip_idct >= AVDISCARD_NONREF && s->pict_type == AV_PICTURE_TYPE_B)
                   ||(s->avctx->skip_idct >= AVDISCARD_NONKEY && s->pict_type != AV_PICTURE_TYPE_I)
                   || s->avctx->skip_idct >= AVDISCARD_ALL)
                    goto skip_idct;
            }

            /* add dct residue */
            if(s->encoding || !(   s->msmpeg4_version || s->codec_id==AV_CODEC_ID_MPEG1VIDEO || s->codec_id==AV_CODEC_ID_MPEG2VIDEO
                                || (s->codec_id==AV_CODEC_ID_MPEG4 && !s->mpeg_quant))){
                add_dequant_dct(s, block[0], 0, dest_y                          , dct_linesize, s->qscale);
                add_dequant_dct(s, block[1], 1, dest_y              + block_size, dct_linesize, s->qscale);
                add_dequant_dct(s, block[2], 2, dest_y + dct_offset             , dct_linesize, s->qscale);
                add_dequant_dct(s, block[3], 3, dest_y + dct_offset + block_size, dct_linesize, s->qscale);

                if(!CONFIG_GRAY || !(s->flags&CODEC_FLAG_GRAY)){
                    if (s->chroma_y_shift){
                        add_dequant_dct(s, block[4], 4, dest_cb, uvlinesize, s->chroma_qscale);
                        add_dequant_dct(s, block[5], 5, dest_cr, uvlinesize, s->chroma_qscale);
                    }else{
                        dct_linesize >>= 1;
                        dct_offset >>=1;
                        add_dequant_dct(s, block[4], 4, dest_cb,              dct_linesize, s->chroma_qscale);
                        add_dequant_dct(s, block[5], 5, dest_cr,              dct_linesize, s->chroma_qscale);
                        add_dequant_dct(s, block[6], 6, dest_cb + dct_offset, dct_linesize, s->chroma_qscale);
                        add_dequant_dct(s, block[7], 7, dest_cr + dct_offset, dct_linesize, s->chroma_qscale);
                    }
                }
            } else if(is_mpeg12 || (s->codec_id != AV_CODEC_ID_WMV2)){
                add_dct(s, block[0], 0, dest_y                          , dct_linesize);
                add_dct(s, block[1], 1, dest_y              + block_size, dct_linesize);
                add_dct(s, block[2], 2, dest_y + dct_offset             , dct_linesize);
                add_dct(s, block[3], 3, dest_y + dct_offset + block_size, dct_linesize);

                if(!CONFIG_GRAY || !(s->flags&CODEC_FLAG_GRAY)){
                    if(s->chroma_y_shift){//Chroma420
                        add_dct(s, block[4], 4, dest_cb, uvlinesize);
                        add_dct(s, block[5], 5, dest_cr, uvlinesize);
                    }else{
                        //chroma422
                        dct_linesize = uvlinesize << s->interlaced_dct;
                        dct_offset   = s->interlaced_dct ? uvlinesize : uvlinesize*block_size;

                        add_dct(s, block[4], 4, dest_cb, dct_linesize);
                        add_dct(s, block[5], 5, dest_cr, dct_linesize);
                        add_dct(s, block[6], 6, dest_cb+dct_offset, dct_linesize);
                        add_dct(s, block[7], 7, dest_cr+dct_offset, dct_linesize);
                        if(!s->chroma_x_shift){//Chroma444
                            add_dct(s, block[8], 8, dest_cb+block_size, dct_linesize);
                            add_dct(s, block[9], 9, dest_cr+block_size, dct_linesize);
                            add_dct(s, block[10], 10, dest_cb+block_size+dct_offset, dct_linesize);
                            add_dct(s, block[11], 11, dest_cr+block_size+dct_offset, dct_linesize);
                        }
                    }
                }//fi gray
            }
            else if (CONFIG_WMV2_DECODER || CONFIG_WMV2_ENCODER) {
                ff_wmv2_add_mb(s, block, dest_y, dest_cb, dest_cr);
            }
        } else {
            /* dct only in intra block */
            if(s->encoding || !(s->codec_id==AV_CODEC_ID_MPEG1VIDEO || s->codec_id==AV_CODEC_ID_MPEG2VIDEO)){
                put_dct(s, block[0], 0, dest_y                          , dct_linesize, s->qscale);
                put_dct(s, block[1], 1, dest_y              + block_size, dct_linesize, s->qscale);
                put_dct(s, block[2], 2, dest_y + dct_offset             , dct_linesize, s->qscale);
                put_dct(s, block[3], 3, dest_y + dct_offset + block_size, dct_linesize, s->qscale);

                if(!CONFIG_GRAY || !(s->flags&CODEC_FLAG_GRAY)){
                    if(s->chroma_y_shift){
                        put_dct(s, block[4], 4, dest_cb, uvlinesize, s->chroma_qscale);
                        put_dct(s, block[5], 5, dest_cr, uvlinesize, s->chroma_qscale);
                    }else{
                        dct_offset >>=1;
                        dct_linesize >>=1;
                        put_dct(s, block[4], 4, dest_cb,              dct_linesize, s->chroma_qscale);
                        put_dct(s, block[5], 5, dest_cr,              dct_linesize, s->chroma_qscale);
                        put_dct(s, block[6], 6, dest_cb + dct_offset, dct_linesize, s->chroma_qscale);
                        put_dct(s, block[7], 7, dest_cr + dct_offset, dct_linesize, s->chroma_qscale);
                    }
                }
            }else{
                s->dsp.idct_put(dest_y                          , dct_linesize, block[0]);
                s->dsp.idct_put(dest_y              + block_size, dct_linesize, block[1]);
                s->dsp.idct_put(dest_y + dct_offset             , dct_linesize, block[2]);
                s->dsp.idct_put(dest_y + dct_offset + block_size, dct_linesize, block[3]);

                if(!CONFIG_GRAY || !(s->flags&CODEC_FLAG_GRAY)){
                    if(s->chroma_y_shift){
                        s->dsp.idct_put(dest_cb, uvlinesize, block[4]);
                        s->dsp.idct_put(dest_cr, uvlinesize, block[5]);
                    }else{

                        dct_linesize = uvlinesize << s->interlaced_dct;
                        dct_offset   = s->interlaced_dct? uvlinesize : uvlinesize*block_size;

                        s->dsp.idct_put(dest_cb,              dct_linesize, block[4]);
                        s->dsp.idct_put(dest_cr,              dct_linesize, block[5]);
                        s->dsp.idct_put(dest_cb + dct_offset, dct_linesize, block[6]);
                        s->dsp.idct_put(dest_cr + dct_offset, dct_linesize, block[7]);
                        if(!s->chroma_x_shift){//Chroma444
                            s->dsp.idct_put(dest_cb + block_size,              dct_linesize, block[8]);
                            s->dsp.idct_put(dest_cr + block_size,              dct_linesize, block[9]);
                            s->dsp.idct_put(dest_cb + block_size + dct_offset, dct_linesize, block[10]);
                            s->dsp.idct_put(dest_cr + block_size + dct_offset, dct_linesize, block[11]);
                        }
                    }
                }//gray
            }
        }
skip_idct:
        if(!readable){
            s->hdsp.put_pixels_tab[0][0](s->dest[0], dest_y ,   linesize,16);
            s->hdsp.put_pixels_tab[s->chroma_x_shift][0](s->dest[1], dest_cb, uvlinesize,16 >> s->chroma_y_shift);
            s->hdsp.put_pixels_tab[s->chroma_x_shift][0](s->dest[2], dest_cr, uvlinesize,16 >> s->chroma_y_shift);
        }
    }
}

void ff_MPV_decode_mb(MpegEncContext *s, int16_t block[12][64]){
#if !CONFIG_SMALL
    if(s->out_format == FMT_MPEG1) {
        if(s->avctx->lowres) MPV_decode_mb_internal(s, block, 1, 1);
        else                 MPV_decode_mb_internal(s, block, 0, 1);
    } else
#endif
    if(s->avctx->lowres) MPV_decode_mb_internal(s, block, 1, 0);
    else                  MPV_decode_mb_internal(s, block, 0, 0);
}

/**
 * @param h is the normal height, this will be reduced automatically if needed for the last row
 */
void ff_draw_horiz_band(AVCodecContext *avctx, DSPContext *dsp, Picture *cur,
                        Picture *last, int y, int h, int picture_structure,
                        int first_field, int draw_edges, int low_delay,
                        int v_edge_pos, int h_edge_pos)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(avctx->pix_fmt);
    int hshift = desc->log2_chroma_w;
    int vshift = desc->log2_chroma_h;
    const int field_pic = picture_structure != PICT_FRAME;
    if(field_pic){
        h <<= 1;
        y <<= 1;
    }

    if (!avctx->hwaccel &&
        !(avctx->codec->capabilities & CODEC_CAP_HWACCEL_VDPAU) &&
        draw_edges &&
        cur->reference &&
        !(avctx->flags & CODEC_FLAG_EMU_EDGE)) {
        int *linesize = cur->f.linesize;
        int sides = 0, edge_h;
        if (y==0) sides |= EDGE_TOP;
        if (y + h >= v_edge_pos)
            sides |= EDGE_BOTTOM;

        edge_h= FFMIN(h, v_edge_pos - y);

        dsp->draw_edges(cur->f.data[0] + y * linesize[0],
                        linesize[0], h_edge_pos, edge_h,
                        EDGE_WIDTH, EDGE_WIDTH, sides);
        dsp->draw_edges(cur->f.data[1] + (y >> vshift) * linesize[1],
                        linesize[1], h_edge_pos >> hshift, edge_h >> vshift,
                        EDGE_WIDTH >> hshift, EDGE_WIDTH >> vshift, sides);
        dsp->draw_edges(cur->f.data[2] + (y >> vshift) * linesize[2],
                        linesize[2], h_edge_pos >> hshift, edge_h >> vshift,
                        EDGE_WIDTH >> hshift, EDGE_WIDTH >> vshift, sides);
    }

    h = FFMIN(h, avctx->height - y);

    if(field_pic && first_field && !(avctx->slice_flags&SLICE_FLAG_ALLOW_FIELD)) return;

    if (avctx->draw_horiz_band) {
        AVFrame *src;
        int offset[AV_NUM_DATA_POINTERS];
        int i;

        if(cur->f.pict_type == AV_PICTURE_TYPE_B || low_delay ||
           (avctx->slice_flags & SLICE_FLAG_CODED_ORDER))
            src = &cur->f;
        else if (last)
            src = &last->f;
        else
            return;

        if (cur->f.pict_type == AV_PICTURE_TYPE_B &&
            picture_structure == PICT_FRAME &&
            avctx->codec_id != AV_CODEC_ID_SVQ3) {
            for (i = 0; i < AV_NUM_DATA_POINTERS; i++)
                offset[i] = 0;
        }else{
            offset[0]= y * src->linesize[0];
            offset[1]=
            offset[2]= (y >> vshift) * src->linesize[1];
            for (i = 3; i < AV_NUM_DATA_POINTERS; i++)
                offset[i] = 0;
        }

        emms_c();

        avctx->draw_horiz_band(avctx, src, offset,
                               y, picture_structure, h);
    }
}

void ff_mpeg_draw_horiz_band(MpegEncContext *s, int y, int h)
{
    int draw_edges = s->unrestricted_mv && !s->intra_only;
    ff_draw_horiz_band(s->avctx, &s->dsp, s->current_picture_ptr,
                       s->last_picture_ptr, y, h, s->picture_structure,
                       s->first_field, draw_edges, s->low_delay,
                       s->v_edge_pos, s->h_edge_pos);
}

void ff_init_block_index(MpegEncContext *s){ //FIXME maybe rename
    const int linesize   = s->current_picture.f.linesize[0]; //not s->linesize as this would be wrong for field pics
    const int uvlinesize = s->current_picture.f.linesize[1];
    const int mb_size= 4 - s->avctx->lowres;

    s->block_index[0]= s->b8_stride*(s->mb_y*2    ) - 2 + s->mb_x*2;
    s->block_index[1]= s->b8_stride*(s->mb_y*2    ) - 1 + s->mb_x*2;
    s->block_index[2]= s->b8_stride*(s->mb_y*2 + 1) - 2 + s->mb_x*2;
    s->block_index[3]= s->b8_stride*(s->mb_y*2 + 1) - 1 + s->mb_x*2;
    s->block_index[4]= s->mb_stride*(s->mb_y + 1)                + s->b8_stride*s->mb_height*2 + s->mb_x - 1;
    s->block_index[5]= s->mb_stride*(s->mb_y + s->mb_height + 2) + s->b8_stride*s->mb_height*2 + s->mb_x - 1;
    //block_index is not used by mpeg2, so it is not affected by chroma_format

    s->dest[0] = s->current_picture.f.data[0] + ((s->mb_x - 1) <<  mb_size);
    s->dest[1] = s->current_picture.f.data[1] + ((s->mb_x - 1) << (mb_size - s->chroma_x_shift));
    s->dest[2] = s->current_picture.f.data[2] + ((s->mb_x - 1) << (mb_size - s->chroma_x_shift));

    if(!(s->pict_type==AV_PICTURE_TYPE_B && s->avctx->draw_horiz_band && s->picture_structure==PICT_FRAME))
    {
        if(s->picture_structure==PICT_FRAME){
        s->dest[0] += s->mb_y *   linesize << mb_size;
        s->dest[1] += s->mb_y * uvlinesize << (mb_size - s->chroma_y_shift);
        s->dest[2] += s->mb_y * uvlinesize << (mb_size - s->chroma_y_shift);
        }else{
            s->dest[0] += (s->mb_y>>1) *   linesize << mb_size;
            s->dest[1] += (s->mb_y>>1) * uvlinesize << (mb_size - s->chroma_y_shift);
            s->dest[2] += (s->mb_y>>1) * uvlinesize << (mb_size - s->chroma_y_shift);
            av_assert1((s->mb_y&1) == (s->picture_structure == PICT_BOTTOM_FIELD));
        }
    }
}

/**
 * Permute an 8x8 block.
 * @param block the block which will be permuted according to the given permutation vector
 * @param permutation the permutation vector
 * @param last the last non zero coefficient in scantable order, used to speed the permutation up
 * @param scantable the used scantable, this is only used to speed the permutation up, the block is not
 *                  (inverse) permutated to scantable order!
 */
void ff_block_permute(int16_t *block, uint8_t *permutation, const uint8_t *scantable, int last)
{
    int i;
    int16_t temp[64];

    if(last<=0) return;
    //if(permutation[1]==1) return; //FIXME it is ok but not clean and might fail for some permutations

    for(i=0; i<=last; i++){
        const int j= scantable[i];
        temp[j]= block[j];
        block[j]=0;
    }

    for(i=0; i<=last; i++){
        const int j= scantable[i];
        const int perm_j= permutation[j];
        block[perm_j]= temp[j];
    }
}

void ff_mpeg_flush(AVCodecContext *avctx){
    int i;
    MpegEncContext *s = avctx->priv_data;

    if(s==NULL || s->picture==NULL)
        return;

    for (i = 0; i < MAX_PICTURE_COUNT; i++)
        ff_mpeg_unref_picture(s, &s->picture[i]);
    s->current_picture_ptr = s->last_picture_ptr = s->next_picture_ptr = NULL;

    ff_mpeg_unref_picture(s, &s->current_picture);
    ff_mpeg_unref_picture(s, &s->last_picture);
    ff_mpeg_unref_picture(s, &s->next_picture);

    s->mb_x= s->mb_y= 0;
    s->closed_gop= 0;

    s->parse_context.state= -1;
    s->parse_context.frame_start_found= 0;
    s->parse_context.overread= 0;
    s->parse_context.overread_index= 0;
    s->parse_context.index= 0;
    s->parse_context.last_index= 0;
    s->bitstream_buffer_size=0;
    s->pp_time=0;
}

static void dct_unquantize_mpeg1_intra_c(MpegEncContext *s,
                                   int16_t *block, int n, int qscale)
{
    int i, level, nCoeffs;
    const uint16_t *quant_matrix;

    nCoeffs= s->block_last_index[n];

    block[0] *= n < 4 ? s->y_dc_scale : s->c_dc_scale;
    /* XXX: only mpeg1 */
    quant_matrix = s->intra_matrix;
    for(i=1;i<=nCoeffs;i++) {
        int j= s->intra_scantable.permutated[i];
        level = block[j];
        if (level) {
            if (level < 0) {
                level = -level;
                level = (int)(level * qscale * quant_matrix[j]) >> 3;
                level = (level - 1) | 1;
                level = -level;
            } else {
                level = (int)(level * qscale * quant_matrix[j]) >> 3;
                level = (level - 1) | 1;
            }
            block[j] = level;
        }
    }
}

static void dct_unquantize_mpeg1_inter_c(MpegEncContext *s,
                                   int16_t *block, int n, int qscale)
{
    int i, level, nCoeffs;
    const uint16_t *quant_matrix;

    nCoeffs= s->block_last_index[n];

    quant_matrix = s->inter_matrix;
    for(i=0; i<=nCoeffs; i++) {
        int j= s->intra_scantable.permutated[i];
        level = block[j];
        if (level) {
            if (level < 0) {
                level = -level;
                level = (((level << 1) + 1) * qscale *
                         ((int) (quant_matrix[j]))) >> 4;
                level = (level - 1) | 1;
                level = -level;
            } else {
                level = (((level << 1) + 1) * qscale *
                         ((int) (quant_matrix[j]))) >> 4;
                level = (level - 1) | 1;
            }
            block[j] = level;
        }
    }
}

static void dct_unquantize_mpeg2_intra_c(MpegEncContext *s,
                                   int16_t *block, int n, int qscale)
{
    int i, level, nCoeffs;
    const uint16_t *quant_matrix;

    if(s->alternate_scan) nCoeffs= 63;
    else nCoeffs= s->block_last_index[n];

    block[0] *= n < 4 ? s->y_dc_scale : s->c_dc_scale;
    quant_matrix = s->intra_matrix;
    for(i=1;i<=nCoeffs;i++) {
        int j= s->intra_scantable.permutated[i];
        level = block[j];
        if (level) {
            if (level < 0) {
                level = -level;
                level = (int)(level * qscale * quant_matrix[j]) >> 3;
                level = -level;
            } else {
                level = (int)(level * qscale * quant_matrix[j]) >> 3;
            }
            block[j] = level;
        }
    }
}

static void dct_unquantize_mpeg2_intra_bitexact(MpegEncContext *s,
                                   int16_t *block, int n, int qscale)
{
    int i, level, nCoeffs;
    const uint16_t *quant_matrix;
    int sum=-1;

    if(s->alternate_scan) nCoeffs= 63;
    else nCoeffs= s->block_last_index[n];

    block[0] *= n < 4 ? s->y_dc_scale : s->c_dc_scale;
    sum += block[0];
    quant_matrix = s->intra_matrix;
    for(i=1;i<=nCoeffs;i++) {
        int j= s->intra_scantable.permutated[i];
        level = block[j];
        if (level) {
            if (level < 0) {
                level = -level;
                level = (int)(level * qscale * quant_matrix[j]) >> 3;
                level = -level;
            } else {
                level = (int)(level * qscale * quant_matrix[j]) >> 3;
            }
            block[j] = level;
            sum+=level;
        }
    }
    block[63]^=sum&1;
}

static void dct_unquantize_mpeg2_inter_c(MpegEncContext *s,
                                   int16_t *block, int n, int qscale)
{
    int i, level, nCoeffs;
    const uint16_t *quant_matrix;
    int sum=-1;

    if(s->alternate_scan) nCoeffs= 63;
    else nCoeffs= s->block_last_index[n];

    quant_matrix = s->inter_matrix;
    for(i=0; i<=nCoeffs; i++) {
        int j= s->intra_scantable.permutated[i];
        level = block[j];
        if (level) {
            if (level < 0) {
                level = -level;
                level = (((level << 1) + 1) * qscale *
                         ((int) (quant_matrix[j]))) >> 4;
                level = -level;
            } else {
                level = (((level << 1) + 1) * qscale *
                         ((int) (quant_matrix[j]))) >> 4;
            }
            block[j] = level;
            sum+=level;
        }
    }
    block[63]^=sum&1;
}

static void dct_unquantize_h263_intra_c(MpegEncContext *s,
                                  int16_t *block, int n, int qscale)
{
    int i, level, qmul, qadd;
    int nCoeffs;

    av_assert2(s->block_last_index[n]>=0 || s->h263_aic);

    qmul = qscale << 1;

    if (!s->h263_aic) {
        block[0] *= n < 4 ? s->y_dc_scale : s->c_dc_scale;
        qadd = (qscale - 1) | 1;
    }else{
        qadd = 0;
    }
    if(s->ac_pred)
        nCoeffs=63;
    else
        nCoeffs= s->inter_scantable.raster_end[ s->block_last_index[n] ];

    for(i=1; i<=nCoeffs; i++) {
        level = block[i];
        if (level) {
            if (level < 0) {
                level = level * qmul - qadd;
            } else {
                level = level * qmul + qadd;
            }
            block[i] = level;
        }
    }
}

static void dct_unquantize_h263_inter_c(MpegEncContext *s,
                                  int16_t *block, int n, int qscale)
{
    int i, level, qmul, qadd;
    int nCoeffs;

    av_assert2(s->block_last_index[n]>=0);

    qadd = (qscale - 1) | 1;
    qmul = qscale << 1;

    nCoeffs= s->inter_scantable.raster_end[ s->block_last_index[n] ];

    for(i=0; i<=nCoeffs; i++) {
        level = block[i];
        if (level) {
            if (level < 0) {
                level = level * qmul - qadd;
            } else {
                level = level * qmul + qadd;
            }
            block[i] = level;
        }
    }
}

/**
 * set qscale and update qscale dependent variables.
 */
void ff_set_qscale(MpegEncContext * s, int qscale)
{
    if (qscale < 1)
        qscale = 1;
    else if (qscale > 31)
        qscale = 31;

    s->qscale = qscale;
    s->chroma_qscale= s->chroma_qscale_table[qscale];

    s->y_dc_scale= s->y_dc_scale_table[ qscale ];
    s->c_dc_scale= s->c_dc_scale_table[ s->chroma_qscale ];
}

void ff_MPV_report_decode_progress(MpegEncContext *s)
{
    if (s->pict_type != AV_PICTURE_TYPE_B && !s->partitioned_frame && !s->er.error_occurred)
        ff_thread_report_progress(&s->current_picture_ptr->tf, s->mb_y, 0);
}

#if CONFIG_ERROR_RESILIENCE
void ff_mpeg_er_frame_start(MpegEncContext *s)
{
    ERContext *er = &s->er;

    er->cur_pic  = s->current_picture_ptr;
    er->last_pic = s->last_picture_ptr;
    er->next_pic = s->next_picture_ptr;

    er->pp_time           = s->pp_time;
    er->pb_time           = s->pb_time;
    er->quarter_sample    = s->quarter_sample;
    er->partitioned_frame = s->partitioned_frame;

    ff_er_frame_start(er);
}
#endif /* CONFIG_ERROR_RESILIENCE */
