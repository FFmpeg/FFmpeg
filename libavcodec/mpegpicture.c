/*
 * Mpeg video formats-related picture management functions
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

#include <stdint.h>

#include "libavutil/avassert.h"
#include "libavutil/common.h"
#include "libavutil/pixdesc.h"
#include "libavutil/imgutils.h"

#include "avcodec.h"
#include "motion_est.h"
#include "mpegpicture.h"
#include "mpegutils.h"
#include "refstruct.h"
#include "threadframe.h"

static void av_noinline free_picture_tables(Picture *pic)
{
    pic->alloc_mb_width  =
    pic->alloc_mb_height = 0;

    av_buffer_unref(&pic->mbskip_table_buf);
    av_buffer_unref(&pic->qscale_table_buf);
    av_buffer_unref(&pic->mb_type_buf);

    for (int i = 0; i < 2; i++) {
        av_buffer_unref(&pic->motion_val_buf[i]);
        av_buffer_unref(&pic->ref_index_buf[i]);
    }
}

static int make_table_writable(AVBufferRef **ref)
{
    AVBufferRef *old = *ref, *new;

    if (av_buffer_is_writable(old))
        return 0;
    new = av_buffer_allocz(old->size);
    if (!new)
        return AVERROR(ENOMEM);
    av_buffer_unref(ref);
    *ref = new;
    return 0;
}

static int make_tables_writable(Picture *pic)
{
#define MAKE_WRITABLE(table) \
do {\
    int ret = make_table_writable(&pic->table); \
    if (ret < 0) \
        return ret; \
} while (0)

    MAKE_WRITABLE(mbskip_table_buf);
    MAKE_WRITABLE(qscale_table_buf);
    MAKE_WRITABLE(mb_type_buf);

    if (pic->motion_val_buf[0]) {
        for (int i = 0; i < 2; i++) {
            MAKE_WRITABLE(motion_val_buf[i]);
            MAKE_WRITABLE(ref_index_buf[i]);
        }
    }

    return 0;
}

int ff_mpeg_framesize_alloc(AVCodecContext *avctx, MotionEstContext *me,
                            ScratchpadContext *sc, int linesize)
{
#   define EMU_EDGE_HEIGHT (4 * 70)
    int alloc_size = FFALIGN(FFABS(linesize) + 64, 32);

    if (avctx->hwaccel)
        return 0;

    if (linesize < 24) {
        av_log(avctx, AV_LOG_ERROR, "Image too small, temporary buffers cannot function\n");
        return AVERROR_PATCHWELCOME;
    }

    if (av_image_check_size2(alloc_size, EMU_EDGE_HEIGHT, avctx->max_pixels, AV_PIX_FMT_NONE, 0, avctx) < 0)
        return AVERROR(ENOMEM);

    // edge emu needs blocksize + filter length - 1
    // (= 17x17 for  halfpel / 21x21 for H.264)
    // VC-1 computes luma and chroma simultaneously and needs 19X19 + 9x9
    // at uvlinesize. It supports only YUV420 so 24x24 is enough
    // linesize * interlaced * MBsize
    // we also use this buffer for encoding in encode_mb_internal() needig an additional 32 lines
    if (!FF_ALLOCZ_TYPED_ARRAY(sc->edge_emu_buffer, alloc_size * EMU_EDGE_HEIGHT) ||
        !FF_ALLOCZ_TYPED_ARRAY(me->scratchpad,      alloc_size * 4 * 16 * 2)) {
        av_freep(&sc->edge_emu_buffer);
        return AVERROR(ENOMEM);
    }

    me->temp            = me->scratchpad;
    sc->rd_scratchpad   = me->scratchpad;
    sc->b_scratchpad    = me->scratchpad;
    sc->obmc_scratchpad = me->scratchpad + 16;

    return 0;
}

/**
 * Check the pic's linesize and allocate linesize dependent scratch buffers
 */
static int handle_pic_linesizes(AVCodecContext *avctx, Picture *pic,
                                MotionEstContext *me, ScratchpadContext *sc,
                                int linesize, int uvlinesize)
{
    int ret;

    if ((linesize   &&   linesize != pic->f->linesize[0]) ||
        (uvlinesize && uvlinesize != pic->f->linesize[1])) {
        av_log(avctx, AV_LOG_ERROR,
               "get_buffer() failed (stride changed: linesize=%d/%d uvlinesize=%d/%d)\n",
               linesize,   pic->f->linesize[0],
               uvlinesize, pic->f->linesize[1]);
        ff_mpeg_unref_picture(pic);
        return -1;
    }

    if (av_pix_fmt_count_planes(pic->f->format) > 2 &&
        pic->f->linesize[1] != pic->f->linesize[2]) {
        av_log(avctx, AV_LOG_ERROR,
               "get_buffer() failed (uv stride mismatch)\n");
        ff_mpeg_unref_picture(pic);
        return -1;
    }

    if (!sc->edge_emu_buffer &&
        (ret = ff_mpeg_framesize_alloc(avctx, me, sc,
                                       pic->f->linesize[0])) < 0) {
        av_log(avctx, AV_LOG_ERROR,
               "get_buffer() failed to allocate context scratch buffers.\n");
        ff_mpeg_unref_picture(pic);
        return ret;
    }

    return 0;
}

static int alloc_picture_tables(AVCodecContext *avctx, Picture *pic, int encoding, int out_format,
                                int mb_stride, int mb_width, int mb_height, int b8_stride)
{
    const int big_mb_num    = mb_stride * (mb_height + 1) + 1;
    const int mb_array_size = mb_stride * mb_height;
    const int b8_array_size = b8_stride * mb_height * 2;
    int i;


    pic->mbskip_table_buf = av_buffer_allocz(mb_array_size + 2);
    pic->qscale_table_buf = av_buffer_allocz(big_mb_num + mb_stride);
    pic->mb_type_buf      = av_buffer_allocz((big_mb_num + mb_stride) *
                                             sizeof(uint32_t));
    if (!pic->mbskip_table_buf || !pic->qscale_table_buf || !pic->mb_type_buf)
        return AVERROR(ENOMEM);

    if (out_format == FMT_H263 || encoding ||
        (avctx->export_side_data & AV_CODEC_EXPORT_DATA_MVS)) {
        int mv_size        = 2 * (b8_array_size + 4) * sizeof(int16_t);
        int ref_index_size = 4 * mb_array_size;

        for (i = 0; mv_size && i < 2; i++) {
            pic->motion_val_buf[i] = av_buffer_allocz(mv_size);
            pic->ref_index_buf[i]  = av_buffer_allocz(ref_index_size);
            if (!pic->motion_val_buf[i] || !pic->ref_index_buf[i])
                return AVERROR(ENOMEM);
        }
    }

    pic->alloc_mb_width  = mb_width;
    pic->alloc_mb_height = mb_height;
    pic->alloc_mb_stride = mb_stride;

    return 0;
}

/**
 * Allocate a Picture.
 * The pixels are allocated/set by calling get_buffer() if shared = 0
 */
int ff_alloc_picture(AVCodecContext *avctx, Picture *pic, MotionEstContext *me,
                     ScratchpadContext *sc, int encoding, int out_format,
                     int mb_stride, int mb_width, int mb_height, int b8_stride,
                     ptrdiff_t *linesize, ptrdiff_t *uvlinesize)
{
    int i, ret;

    if (pic->qscale_table_buf)
        if (   pic->alloc_mb_width  != mb_width
            || pic->alloc_mb_height != mb_height)
            free_picture_tables(pic);

    if (handle_pic_linesizes(avctx, pic, me, sc,
                             *linesize, *uvlinesize) < 0)
        return -1;

    *linesize   = pic->f->linesize[0];
    *uvlinesize = pic->f->linesize[1];

    if (!pic->qscale_table_buf)
        ret = alloc_picture_tables(avctx, pic, encoding, out_format,
                                   mb_stride, mb_width, mb_height, b8_stride);
    else
        ret = make_tables_writable(pic);
    if (ret < 0)
        goto fail;

    pic->mbskip_table = pic->mbskip_table_buf->data;
    pic->qscale_table = pic->qscale_table_buf->data + 2 * mb_stride + 1;
    pic->mb_type      = (uint32_t*)pic->mb_type_buf->data + 2 * mb_stride + 1;

    if (pic->motion_val_buf[0]) {
        for (i = 0; i < 2; i++) {
            pic->motion_val[i] = (int16_t (*)[2])pic->motion_val_buf[i]->data + 4;
            pic->ref_index[i]  = pic->ref_index_buf[i]->data;
        }
    }

    return 0;
fail:
    av_log(avctx, AV_LOG_ERROR, "Error allocating a picture.\n");
    ff_mpeg_unref_picture(pic);
    free_picture_tables(pic);
    return AVERROR(ENOMEM);
}

/**
 * Deallocate a picture; frees the picture tables in case they
 * need to be reallocated anyway.
 */
void ff_mpeg_unref_picture(Picture *pic)
{
    pic->tf.f = pic->f;
    ff_thread_release_ext_buffer(&pic->tf);

    ff_refstruct_unref(&pic->hwaccel_picture_private);

    if (pic->needs_realloc)
        free_picture_tables(pic);

    pic->field_picture = 0;
    pic->b_frame_score = 0;
    pic->needs_realloc = 0;
    pic->reference     = 0;
    pic->shared        = 0;
    pic->display_picture_number = 0;
    pic->coded_picture_number   = 0;
}

int ff_update_picture_tables(Picture *dst, const Picture *src)
{
    int i, ret;

    ret  = av_buffer_replace(&dst->mbskip_table_buf, src->mbskip_table_buf);
    ret |= av_buffer_replace(&dst->qscale_table_buf, src->qscale_table_buf);
    ret |= av_buffer_replace(&dst->mb_type_buf,      src->mb_type_buf);
    for (i = 0; i < 2; i++) {
        ret |= av_buffer_replace(&dst->motion_val_buf[i], src->motion_val_buf[i]);
        ret |= av_buffer_replace(&dst->ref_index_buf[i],  src->ref_index_buf[i]);
    }

    if (ret < 0) {
        free_picture_tables(dst);
        return ret;
    }

    dst->mbskip_table  = src->mbskip_table;
    dst->qscale_table  = src->qscale_table;
    dst->mb_type       = src->mb_type;
    for (i = 0; i < 2; i++) {
        dst->motion_val[i] = src->motion_val[i];
        dst->ref_index[i]  = src->ref_index[i];
    }

    dst->alloc_mb_width  = src->alloc_mb_width;
    dst->alloc_mb_height = src->alloc_mb_height;
    dst->alloc_mb_stride = src->alloc_mb_stride;

    return 0;
}

int ff_mpeg_ref_picture(Picture *dst, Picture *src)
{
    int ret;

    av_assert0(!dst->f->buf[0]);
    av_assert0(src->f->buf[0]);

    src->tf.f = src->f;
    dst->tf.f = dst->f;
    ret = ff_thread_ref_frame(&dst->tf, &src->tf);
    if (ret < 0)
        goto fail;

    ret = ff_update_picture_tables(dst, src);
    if (ret < 0)
        goto fail;

    ff_refstruct_replace(&dst->hwaccel_picture_private,
                          src->hwaccel_picture_private);

    dst->field_picture           = src->field_picture;
    dst->b_frame_score           = src->b_frame_score;
    dst->needs_realloc           = src->needs_realloc;
    dst->reference               = src->reference;
    dst->shared                  = src->shared;
    dst->display_picture_number  = src->display_picture_number;
    dst->coded_picture_number    = src->coded_picture_number;

    return 0;
fail:
    ff_mpeg_unref_picture(dst);
    return ret;
}

static inline int pic_is_unused(Picture *pic)
{
    if (!pic->f->buf[0])
        return 1;
    if (pic->needs_realloc)
        return 1;
    return 0;
}

static int find_unused_picture(AVCodecContext *avctx, Picture *picture, int shared)
{
    int i;

    if (shared) {
        for (i = 0; i < MAX_PICTURE_COUNT; i++) {
            if (!picture[i].f->buf[0])
                return i;
        }
    } else {
        for (i = 0; i < MAX_PICTURE_COUNT; i++) {
            if (pic_is_unused(&picture[i]))
                return i;
        }
    }

    av_log(avctx, AV_LOG_FATAL,
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

int ff_find_unused_picture(AVCodecContext *avctx, Picture *picture, int shared)
{
    int ret = find_unused_picture(avctx, picture, shared);

    if (ret >= 0 && ret < MAX_PICTURE_COUNT) {
        if (picture[ret].needs_realloc) {
            ff_mpeg_unref_picture(&picture[ret]);
        }
    }
    return ret;
}

void av_cold ff_mpv_picture_free(Picture *pic)
{
    free_picture_tables(pic);
    ff_mpeg_unref_picture(pic);
    av_frame_free(&pic->f);
}
