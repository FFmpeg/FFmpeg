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

#include "libavutil/avassert.h"
#include "libavutil/common.h"
#include "libavutil/mem.h"
#include "libavutil/pixdesc.h"
#include "libavutil/imgutils.h"

#include "avcodec.h"
#include "motion_est.h"
#include "mpegpicture.h"
#include "refstruct.h"
#include "threadframe.h"

static void av_noinline free_picture_tables(MPVPicture *pic)
{
    ff_refstruct_unref(&pic->mbskip_table);
    ff_refstruct_unref(&pic->qscale_table_base);
    ff_refstruct_unref(&pic->mb_type_base);

    for (int i = 0; i < 2; i++) {
        ff_refstruct_unref(&pic->motion_val_base[i]);
        ff_refstruct_unref(&pic->ref_index[i]);
    }

    pic->mb_width  =
    pic->mb_height = 0;
}

int ff_mpeg_framesize_alloc(AVCodecContext *avctx, MotionEstContext *me,
                            ScratchpadContext *sc, int linesize)
{
#   define EMU_EDGE_HEIGHT (4 * 70)
    int linesizeabs = FFABS(linesize);
    int alloc_size = FFALIGN(linesizeabs + 64, 32);

    if (linesizeabs <= sc->linesize)
        return 0;

    if (avctx->hwaccel)
        return 0;

    if (linesizeabs < 24) {
        av_log(avctx, AV_LOG_ERROR, "Image too small, temporary buffers cannot function\n");
        return AVERROR_PATCHWELCOME;
    }

    if (av_image_check_size2(alloc_size, EMU_EDGE_HEIGHT, avctx->max_pixels, AV_PIX_FMT_NONE, 0, avctx) < 0)
        return AVERROR(ENOMEM);

    av_freep(&sc->edge_emu_buffer);
    av_freep(&me->scratchpad);

    // edge emu needs blocksize + filter length - 1
    // (= 17x17 for  halfpel / 21x21 for H.264)
    // VC-1 computes luma and chroma simultaneously and needs 19X19 + 9x9
    // at uvlinesize. It supports only YUV420 so 24x24 is enough
    // linesize * interlaced * MBsize
    // we also use this buffer for encoding in encode_mb_internal() needig an additional 32 lines
    if (!FF_ALLOCZ_TYPED_ARRAY(sc->edge_emu_buffer, alloc_size * EMU_EDGE_HEIGHT) ||
        !FF_ALLOCZ_TYPED_ARRAY(me->scratchpad,      alloc_size * 4 * 16 * 2)) {
        sc->linesize = 0;
        av_freep(&sc->edge_emu_buffer);
        return AVERROR(ENOMEM);
    }
    sc->linesize = linesizeabs;

    me->temp            = me->scratchpad;
    sc->rd_scratchpad   = me->scratchpad;
    sc->b_scratchpad    = me->scratchpad;
    sc->obmc_scratchpad = me->scratchpad + 16;

    return 0;
}

int ff_mpv_pic_check_linesize(void *logctx, const AVFrame *f,
                              ptrdiff_t *linesizep, ptrdiff_t *uvlinesizep)
{
    ptrdiff_t linesize = *linesizep, uvlinesize = *uvlinesizep;

    if ((linesize   &&   linesize != f->linesize[0]) ||
        (uvlinesize && uvlinesize != f->linesize[1])) {
        av_log(logctx, AV_LOG_ERROR, "Stride change unsupported: "
               "linesize=%"PTRDIFF_SPECIFIER"/%d uvlinesize=%"PTRDIFF_SPECIFIER"/%d)\n",
               linesize,   f->linesize[0],
               uvlinesize, f->linesize[1]);
        return AVERROR_PATCHWELCOME;
    }

    if (av_pix_fmt_count_planes(f->format) > 2 &&
        f->linesize[1] != f->linesize[2]) {
        av_log(logctx, AV_LOG_ERROR, "uv stride mismatch unsupported\n");
        return AVERROR_PATCHWELCOME;
    }
    *linesizep   = f->linesize[0];
    *uvlinesizep = f->linesize[1];

    return 0;
}

static int alloc_picture_tables(BufferPoolContext *pools, MPVPicture *pic,
                                int mb_height)
{
#define GET_BUFFER(name, buf_suffix, idx_suffix) do { \
    pic->name ## buf_suffix idx_suffix = ff_refstruct_pool_get(pools->name ## _pool); \
    if (!pic->name ## buf_suffix idx_suffix) \
        return AVERROR(ENOMEM); \
} while (0)
    GET_BUFFER(qscale_table, _base,);
    GET_BUFFER(mb_type, _base,);
    if (pools->motion_val_pool) {
        if (pools->mbskip_table_pool)
            GET_BUFFER(mbskip_table,,);
        for (int i = 0; i < 2; i++) {
            GET_BUFFER(ref_index,, [i]);
            GET_BUFFER(motion_val, _base, [i]);
        }
    }
#undef GET_BUFFER

    pic->mb_width  = pools->alloc_mb_width;
    pic->mb_height = mb_height;
    pic->mb_stride = pools->alloc_mb_stride;

    return 0;
}

int ff_mpv_alloc_pic_accessories(AVCodecContext *avctx, MPVPicture *pic,
                                 MotionEstContext *me, ScratchpadContext *sc,
                                 BufferPoolContext *pools, int mb_height)
{
    int ret;

    for (int i = 0; i < MPV_MAX_PLANES; i++) {
        pic->data[i]     = pic->f->data[i];
        pic->linesize[i] = pic->f->linesize[i];
    }

    ret = ff_mpeg_framesize_alloc(avctx, me, sc,
                                  pic->f->linesize[0]);
    if (ret < 0)
        goto fail;

    ret = alloc_picture_tables(pools, pic, mb_height);
    if (ret < 0)
        goto fail;

    pic->qscale_table = pic->qscale_table_base + 2 * pic->mb_stride + 1;
    pic->mb_type      = pic->mb_type_base      + 2 * pic->mb_stride + 1;

    if (pic->motion_val_base[0]) {
        for (int i = 0; i < 2; i++)
            pic->motion_val[i] = pic->motion_val_base[i] + 4;
    }

    return 0;
fail:
    av_log(avctx, AV_LOG_ERROR, "Error allocating picture accessories.\n");
    return ret;
}

/**
 * Deallocate a picture; frees the picture tables in case they
 * need to be reallocated anyway.
 */
void ff_mpeg_unref_picture(MPVPicture *pic)
{
    pic->tf.f = pic->f;
    ff_thread_release_ext_buffer(&pic->tf);

    ff_refstruct_unref(&pic->hwaccel_picture_private);

    free_picture_tables(pic);

    memset(pic->data,     0, sizeof(pic->data));
    memset(pic->linesize, 0, sizeof(pic->linesize));

    pic->dummy         = 0;

    pic->field_picture = 0;
    pic->b_frame_score = 0;
    pic->reference     = 0;
    pic->shared        = 0;
    pic->display_picture_number = 0;
    pic->coded_picture_number   = 0;
}

static void update_picture_tables(MPVPicture *dst, const MPVPicture *src)
{
    ff_refstruct_replace(&dst->mbskip_table, src->mbskip_table);
    ff_refstruct_replace(&dst->qscale_table_base, src->qscale_table_base);
    ff_refstruct_replace(&dst->mb_type_base,      src->mb_type_base);
    for (int i = 0; i < 2; i++) {
        ff_refstruct_replace(&dst->motion_val_base[i], src->motion_val_base[i]);
        ff_refstruct_replace(&dst->ref_index[i],  src->ref_index[i]);
    }

    dst->qscale_table  = src->qscale_table;
    dst->mb_type       = src->mb_type;
    for (int i = 0; i < 2; i++)
        dst->motion_val[i] = src->motion_val[i];

    dst->mb_width  = src->mb_width;
    dst->mb_height = src->mb_height;
    dst->mb_stride = src->mb_stride;
}

int ff_mpeg_ref_picture(MPVPicture *dst, MPVPicture *src)
{
    int ret;

    av_assert0(!dst->f->buf[0]);
    av_assert0(src->f->buf[0]);

    src->tf.f = src->f;
    dst->tf.f = dst->f;
    ret = ff_thread_ref_frame(&dst->tf, &src->tf);
    if (ret < 0)
        goto fail;

    for (int i = 0; i < MPV_MAX_PLANES; i++) {
        dst->data[i]     = src->data[i];
        dst->linesize[i] = src->linesize[i];
    }

    update_picture_tables(dst, src);

    ff_refstruct_replace(&dst->hwaccel_picture_private,
                          src->hwaccel_picture_private);

    dst->dummy                   = src->dummy;
    dst->field_picture           = src->field_picture;
    dst->b_frame_score           = src->b_frame_score;
    dst->reference               = src->reference;
    dst->shared                  = src->shared;
    dst->display_picture_number  = src->display_picture_number;
    dst->coded_picture_number    = src->coded_picture_number;

    return 0;
fail:
    ff_mpeg_unref_picture(dst);
    return ret;
}

int ff_find_unused_picture(AVCodecContext *avctx, MPVPicture *picture, int shared)
{
    for (int i = 0; i < MAX_PICTURE_COUNT; i++)
        if (!picture[i].f->buf[0])
            return i;

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

void av_cold ff_mpv_picture_free(MPVPicture *pic)
{
    ff_mpeg_unref_picture(pic);
    av_frame_free(&pic->f);
}
