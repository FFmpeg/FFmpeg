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
#include "mpegpicture.h"
#include "refstruct.h"

static void mpv_pic_reset(FFRefStructOpaque unused, void *obj)
{
    MPVPicture *pic = obj;

    av_frame_unref(pic->f);
    ff_thread_progress_reset(&pic->progress);

    ff_refstruct_unref(&pic->hwaccel_picture_private);

    ff_refstruct_unref(&pic->mbskip_table);
    ff_refstruct_unref(&pic->qscale_table_base);
    ff_refstruct_unref(&pic->mb_type_base);

    for (int i = 0; i < 2; i++) {
        ff_refstruct_unref(&pic->motion_val_base[i]);
        ff_refstruct_unref(&pic->ref_index[i]);

        pic->motion_val[i] = NULL;
    }

    pic->mb_type                = NULL;
    pic->qscale_table           = NULL;

    pic->mb_stride =
    pic->mb_width  =
    pic->mb_height = 0;

    pic->dummy                  = 0;
    pic->field_picture          = 0;
    pic->b_frame_score          = 0;
    pic->reference              = 0;
    pic->shared                 = 0;
    pic->display_picture_number = 0;
    pic->coded_picture_number   = 0;
}

static int av_cold mpv_pic_init(FFRefStructOpaque opaque, void *obj)
{
    MPVPicture *pic = obj;
    int ret, init_progress = (uintptr_t)opaque.nc;

    ret = ff_thread_progress_init(&pic->progress, init_progress);
    if (ret < 0)
        return ret;

    pic->f = av_frame_alloc();
    if (!pic->f)
        return AVERROR(ENOMEM);
    return 0;
}

static void av_cold mpv_pic_free(FFRefStructOpaque unused, void *obj)
{
    MPVPicture *pic = obj;

    ff_thread_progress_destroy(&pic->progress);
    av_frame_free(&pic->f);
}

av_cold FFRefStructPool *ff_mpv_alloc_pic_pool(int init_progress)
{
    return ff_refstruct_pool_alloc_ext(sizeof(MPVPicture),
                                       FF_REFSTRUCT_POOL_FLAG_FREE_ON_INIT_ERROR,
                                       (void*)(uintptr_t)init_progress,
                                       mpv_pic_init, mpv_pic_reset, mpv_pic_free, NULL);
}

void ff_mpv_unref_picture(MPVWorkPicture *pic)
{
    ff_refstruct_unref(&pic->ptr);
    memset(pic, 0, sizeof(*pic));
}

static void set_workpic_from_pic(MPVWorkPicture *wpic, const MPVPicture *pic)
{
    for (int i = 0; i < MPV_MAX_PLANES; i++) {
        wpic->data[i]     = pic->f->data[i];
        wpic->linesize[i] = pic->f->linesize[i];
    }
    wpic->qscale_table = pic->qscale_table;
    wpic->mb_type      = pic->mb_type;
    wpic->mbskip_table = pic->mbskip_table;

    for (int i = 0; i < 2; i++) {
        wpic->motion_val[i] = pic->motion_val[i];
        wpic->ref_index[i]  = pic->ref_index[i];
    }
    wpic->reference  = pic->reference;
}

void ff_mpv_replace_picture(MPVWorkPicture *dst, const MPVWorkPicture *src)
{
    av_assert1(dst != src);
    ff_refstruct_replace(&dst->ptr, src->ptr);
    memcpy(dst, src, sizeof(*dst));
}

void ff_mpv_workpic_from_pic(MPVWorkPicture *wpic, MPVPicture *pic)
{
    ff_refstruct_replace(&wpic->ptr, pic);
    if (!pic) {
        memset(wpic, 0, sizeof(*wpic));
        return;
    }
    set_workpic_from_pic(wpic, pic);
}

int ff_mpv_framesize_alloc(AVCodecContext *avctx,
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
    av_freep(&sc->scratchpad_buf);

    // edge emu needs blocksize + filter length - 1
    // (= 17x17 for  halfpel / 21x21 for H.264)
    // VC-1 computes luma and chroma simultaneously and needs 19X19 + 9x9
    // at uvlinesize. It supports only YUV420 so 24x24 is enough
    // linesize * interlaced * MBsize
    // we also use this buffer for encoding in encode_mb_internal() needig an additional 32 lines
    if (!FF_ALLOCZ_TYPED_ARRAY(sc->edge_emu_buffer, alloc_size * EMU_EDGE_HEIGHT) ||
        !FF_ALLOCZ_TYPED_ARRAY(sc->scratchpad_buf,  alloc_size * 4 * 16 * 2)) {
        sc->linesize = 0;
        av_freep(&sc->edge_emu_buffer);
        return AVERROR(ENOMEM);
    }
    sc->linesize = linesizeabs;

    sc->obmc_scratchpad = sc->scratchpad_buf + 16;

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
            pic->motion_val[i] = pic->motion_val_base[i] + 4;
        }
    }
#undef GET_BUFFER

    pic->mb_width  = pools->alloc_mb_width;
    pic->mb_height = mb_height;
    pic->mb_stride = pools->alloc_mb_stride;

    pic->qscale_table = pic->qscale_table_base + 2 * pic->mb_stride + 1;
    pic->mb_type      = pic->mb_type_base      + 2 * pic->mb_stride + 1;

    return 0;
}

int ff_mpv_alloc_pic_accessories(AVCodecContext *avctx, MPVWorkPicture *wpic,
                                 ScratchpadContext *sc,
                                 BufferPoolContext *pools, int mb_height)
{
    MPVPicture *pic = wpic->ptr;
    int ret;

    ret = ff_mpv_framesize_alloc(avctx, sc, pic->f->linesize[0]);
    if (ret < 0)
        goto fail;

    ret = alloc_picture_tables(pools, pic, mb_height);
    if (ret < 0)
        goto fail;

    set_workpic_from_pic(wpic, pic);

    return 0;
fail:
    av_log(avctx, AV_LOG_ERROR, "Error allocating picture accessories.\n");
    return ret;
}
