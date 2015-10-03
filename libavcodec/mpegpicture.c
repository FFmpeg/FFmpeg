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

#include "avcodec.h"
#include "motion_est.h"
#include "mpegpicture.h"
#include "mpegutils.h"

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

int ff_mpeg_framesize_alloc(AVCodecContext *avctx, MotionEstContext *me,
                            ScratchpadContext *sc, int linesize)
{
    int alloc_size = FFALIGN(FFABS(linesize) + 64, 32);

    if (avctx->hwaccel
#if FF_API_CAP_VDPAU
        || avctx->codec->capabilities & AV_CODEC_CAP_HWACCEL_VDPAU
#endif
        )
        return 0;

    if (linesize < 24) {
        av_log(avctx, AV_LOG_ERROR, "Image too small, temporary buffers cannot function\n");
        return AVERROR_PATCHWELCOME;
    }

    // edge emu needs blocksize + filter length - 1
    // (= 17x17 for  halfpel / 21x21 for  h264)
    // VC1 computes luma and chroma simultaneously and needs 19X19 + 9x9
    // at uvlinesize. It supports only YUV420 so 24x24 is enough
    // linesize * interlaced * MBsize
    // we also use this buffer for encoding in encode_mb_internal() needig an additional 32 lines
    FF_ALLOCZ_ARRAY_OR_GOTO(avctx, sc->edge_emu_buffer, alloc_size, 4 * 68,
                      fail);

    FF_ALLOCZ_ARRAY_OR_GOTO(avctx, me->scratchpad, alloc_size, 4 * 16 * 2,
                      fail)
    me->temp            = me->scratchpad;
    sc->rd_scratchpad   = me->scratchpad;
    sc->b_scratchpad    = me->scratchpad;
    sc->obmc_scratchpad = me->scratchpad + 16;

    return 0;
fail:
    av_freep(&sc->edge_emu_buffer);
    return AVERROR(ENOMEM);
}

/**
 * Allocate a frame buffer
 */
static int alloc_frame_buffer(AVCodecContext *avctx,  Picture *pic,
                              MotionEstContext *me, ScratchpadContext *sc,
                              int chroma_x_shift, int chroma_y_shift,
                              int linesize, int uvlinesize)
{
    int edges_needed = av_codec_is_encoder(avctx->codec);
    int r, ret;

    pic->tf.f = pic->f;
    if (avctx->codec_id != AV_CODEC_ID_WMV3IMAGE &&
        avctx->codec_id != AV_CODEC_ID_VC1IMAGE  &&
        avctx->codec_id != AV_CODEC_ID_MSS2) {
        if (edges_needed) {
            pic->f->width  = avctx->width  + 2 * EDGE_WIDTH;
            pic->f->height = avctx->height + 2 * EDGE_WIDTH;
        }

        r = ff_thread_get_buffer(avctx, &pic->tf,
                                 pic->reference ? AV_GET_BUFFER_FLAG_REF : 0);
    } else {
        pic->f->width  = avctx->width;
        pic->f->height = avctx->height;
        pic->f->format = avctx->pix_fmt;
        r = avcodec_default_get_buffer2(avctx, pic->f, 0);
    }

    if (r < 0 || !pic->f->buf[0]) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed (%d %p)\n",
               r, pic->f->data[0]);
        return -1;
    }

    if (edges_needed) {
        int i;
        for (i = 0; pic->f->data[i]; i++) {
            int offset = (EDGE_WIDTH >> (i ? chroma_y_shift : 0)) *
                         pic->f->linesize[i] +
                         (EDGE_WIDTH >> (i ? chroma_x_shift : 0));
            pic->f->data[i] += offset;
        }
        pic->f->width  = avctx->width;
        pic->f->height = avctx->height;
    }

    if (avctx->hwaccel) {
        assert(!pic->hwaccel_picture_private);
        if (avctx->hwaccel->frame_priv_data_size) {
            pic->hwaccel_priv_buf = av_buffer_allocz(avctx->hwaccel->frame_priv_data_size);
            if (!pic->hwaccel_priv_buf) {
                av_log(avctx, AV_LOG_ERROR, "alloc_frame_buffer() failed (hwaccel private data allocation)\n");
                return -1;
            }
            pic->hwaccel_picture_private = pic->hwaccel_priv_buf->data;
        }
    }

    if (linesize && (linesize   != pic->f->linesize[0] ||
                     uvlinesize != pic->f->linesize[1])) {
        av_log(avctx, AV_LOG_ERROR,
               "get_buffer() failed (stride changed)\n");
        ff_mpeg_unref_picture(avctx, pic);
        return -1;
    }

    if (pic->f->linesize[1] != pic->f->linesize[2]) {
        av_log(avctx, AV_LOG_ERROR,
               "get_buffer() failed (uv stride mismatch)\n");
        ff_mpeg_unref_picture(avctx, pic);
        return -1;
    }

    if (!sc->edge_emu_buffer &&
        (ret = ff_mpeg_framesize_alloc(avctx, me, sc,
                                       pic->f->linesize[0])) < 0) {
        av_log(avctx, AV_LOG_ERROR,
               "get_buffer() failed to allocate context scratch buffers.\n");
        ff_mpeg_unref_picture(avctx, pic);
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

    if (encoding) {
        pic->mb_var_buf    = av_buffer_allocz(mb_array_size * sizeof(int16_t));
        pic->mc_mb_var_buf = av_buffer_allocz(mb_array_size * sizeof(int16_t));
        pic->mb_mean_buf   = av_buffer_allocz(mb_array_size);
        if (!pic->mb_var_buf || !pic->mc_mb_var_buf || !pic->mb_mean_buf)
            return AVERROR(ENOMEM);
    }

    if (out_format == FMT_H263 || encoding || avctx->debug_mv ||
        (avctx->flags2 & AV_CODEC_FLAG2_EXPORT_MVS)) {
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

    return 0;
}

/**
 * Allocate a Picture.
 * The pixels are allocated/set by calling get_buffer() if shared = 0
 */
int ff_alloc_picture(AVCodecContext *avctx, Picture *pic, MotionEstContext *me,
                     ScratchpadContext *sc, int shared, int encoding,
                     int chroma_x_shift, int chroma_y_shift, int out_format,
                     int mb_stride, int mb_width, int mb_height, int b8_stride,
                     ptrdiff_t *linesize, ptrdiff_t *uvlinesize)
{
    int i, ret;

    if (pic->qscale_table_buf)
        if (   pic->alloc_mb_width  != mb_width
            || pic->alloc_mb_height != mb_height)
            ff_free_picture_tables(pic);

    if (shared) {
        av_assert0(pic->f->data[0]);
        pic->shared = 1;
    } else {
        av_assert0(!pic->f->buf[0]);
        if (alloc_frame_buffer(avctx, pic, me, sc,
                               chroma_x_shift, chroma_y_shift,
                               *linesize, *uvlinesize) < 0)
            return -1;

        *linesize   = pic->f->linesize[0];
        *uvlinesize = pic->f->linesize[1];
    }

    if (!pic->qscale_table_buf)
        ret = alloc_picture_tables(avctx, pic, encoding, out_format,
                                   mb_stride, mb_width, mb_height, b8_stride);
    else
        ret = make_tables_writable(pic);
    if (ret < 0)
        goto fail;

    if (encoding) {
        pic->mb_var    = (uint16_t*)pic->mb_var_buf->data;
        pic->mc_mb_var = (uint16_t*)pic->mc_mb_var_buf->data;
        pic->mb_mean   = pic->mb_mean_buf->data;
    }

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
    ff_mpeg_unref_picture(avctx, pic);
    ff_free_picture_tables(pic);
    return AVERROR(ENOMEM);
}

/**
 * Deallocate a picture.
 */
void ff_mpeg_unref_picture(AVCodecContext *avctx, Picture *pic)
{
    int off = offsetof(Picture, mb_mean) + sizeof(pic->mb_mean);

    pic->tf.f = pic->f;
    /* WM Image / Screen codecs allocate internal buffers with different
     * dimensions / colorspaces; ignore user-defined callbacks for these. */
    if (avctx->codec_id != AV_CODEC_ID_WMV3IMAGE &&
        avctx->codec_id != AV_CODEC_ID_VC1IMAGE  &&
        avctx->codec_id != AV_CODEC_ID_MSS2)
        ff_thread_release_buffer(avctx, &pic->tf);
    else if (pic->f)
        av_frame_unref(pic->f);

    av_buffer_unref(&pic->hwaccel_priv_buf);

    if (pic->needs_realloc)
        ff_free_picture_tables(pic);

    memset((uint8_t*)pic + off, 0, sizeof(*pic) - off);
}

int ff_update_picture_tables(Picture *dst, Picture *src)
{
     int i;

#define UPDATE_TABLE(table)                                                   \
do {                                                                          \
    if (src->table &&                                                         \
        (!dst->table || dst->table->buffer != src->table->buffer)) {          \
        av_buffer_unref(&dst->table);                                         \
        dst->table = av_buffer_ref(src->table);                               \
        if (!dst->table) {                                                    \
            ff_free_picture_tables(dst);                                      \
            return AVERROR(ENOMEM);                                           \
        }                                                                     \
    }                                                                         \
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

int ff_mpeg_ref_picture(AVCodecContext *avctx, Picture *dst, Picture *src)
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

    memcpy(dst->encoding_error, src->encoding_error,
           sizeof(dst->encoding_error));

    return 0;
fail:
    ff_mpeg_unref_picture(avctx, dst);
    return ret;
}

static inline int pic_is_unused(Picture *pic)
{
    if (!pic->f->buf[0])
        return 1;
    if (pic->needs_realloc && !(pic->reference & DELAYED_PIC_REF))
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
            picture[ret].needs_realloc = 0;
            ff_free_picture_tables(&picture[ret]);
            ff_mpeg_unref_picture(avctx, &picture[ret]);
        }
    }
    return ret;
}

void ff_free_picture_tables(Picture *pic)
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
