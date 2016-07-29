/*
 * The simplest mpeg encoder (well, it was the simplest!)
 * Copyright (c) 2000,2001 Fabrice Bellard
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
 *
 * 4MV & hq & B-frame encoding stuff by Michael Niedermayer <michaelni@gmx.at>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * The simplest mpeg encoder (well, it was the simplest!).
 */

#include "libavutil/attributes.h"
#include "libavutil/avassert.h"
#include "libavutil/imgutils.h"
#include "libavutil/internal.h"
#include "libavutil/timer.h"
#include "avcodec.h"
#include "blockdsp.h"
#include "idctdsp.h"
#include "internal.h"
#include "mathops.h"
#include "mpeg_er.h"
#include "mpegutils.h"
#include "mpegvideo.h"
#include "mpegvideodata.h"
#include "mjpegenc.h"
#include "msmpeg4.h"
#include "qpeldsp.h"
#include "xvmc_internal.h"
#include "thread.h"
#include "wmv2.h"
#include <limits.h>

static void dct_unquantize_mpeg1_intra_c(MpegEncContext *s,
                                   int16_t *block, int n, int qscale)
{
    int i, level, nCoeffs;
    const uint16_t *quant_matrix;

    nCoeffs= s->block_last_index[n];

    if (n < 4)
        block[0] = block[0] * s->y_dc_scale;
    else
        block[0] = block[0] * s->c_dc_scale;
    /* XXX: only MPEG-1 */
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

    if (n < 4)
        block[0] = block[0] * s->y_dc_scale;
    else
        block[0] = block[0] * s->c_dc_scale;
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

    if (n < 4)
        block[0] = block[0] * s->y_dc_scale;
    else
        block[0] = block[0] * s->c_dc_scale;
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

    assert(s->block_last_index[n]>=0);

    qmul = qscale << 1;

    if (!s->h263_aic) {
        if (n < 4)
            block[0] = block[0] * s->y_dc_scale;
        else
            block[0] = block[0] * s->c_dc_scale;
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

    assert(s->block_last_index[n]>=0);

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

/* init common dct for both encoder and decoder */
static av_cold int dct_init(MpegEncContext *s)
{
    ff_blockdsp_init(&s->bdsp, s->avctx);
    ff_hpeldsp_init(&s->hdsp, s->avctx->flags);
    ff_mpegvideodsp_init(&s->mdsp);
    ff_videodsp_init(&s->vdsp, s->avctx->bits_per_raw_sample);

    s->dct_unquantize_h263_intra = dct_unquantize_h263_intra_c;
    s->dct_unquantize_h263_inter = dct_unquantize_h263_inter_c;
    s->dct_unquantize_mpeg1_intra = dct_unquantize_mpeg1_intra_c;
    s->dct_unquantize_mpeg1_inter = dct_unquantize_mpeg1_inter_c;
    s->dct_unquantize_mpeg2_intra = dct_unquantize_mpeg2_intra_c;
    if (s->avctx->flags & AV_CODEC_FLAG_BITEXACT)
        s->dct_unquantize_mpeg2_intra = dct_unquantize_mpeg2_intra_bitexact;
    s->dct_unquantize_mpeg2_inter = dct_unquantize_mpeg2_inter_c;

    if (HAVE_INTRINSICS_NEON)
        ff_mpv_common_init_neon(s);

    if (ARCH_ARM)
        ff_mpv_common_init_arm(s);
    if (ARCH_PPC)
        ff_mpv_common_init_ppc(s);
    if (ARCH_X86)
        ff_mpv_common_init_x86(s);

    return 0;
}

av_cold void ff_mpv_idct_init(MpegEncContext *s)
{
    ff_idctdsp_init(&s->idsp, s->avctx);

    /* load & permutate scantables
     * note: only wmv uses different ones
     */
    if (s->alternate_scan) {
        ff_init_scantable(s->idsp.idct_permutation, &s->inter_scantable, ff_alternate_vertical_scan);
        ff_init_scantable(s->idsp.idct_permutation, &s->intra_scantable, ff_alternate_vertical_scan);
    } else {
        ff_init_scantable(s->idsp.idct_permutation, &s->inter_scantable, ff_zigzag_direct);
        ff_init_scantable(s->idsp.idct_permutation, &s->intra_scantable, ff_zigzag_direct);
    }
    ff_init_scantable(s->idsp.idct_permutation, &s->intra_h_scantable, ff_alternate_horizontal_scan);
    ff_init_scantable(s->idsp.idct_permutation, &s->intra_v_scantable, ff_alternate_vertical_scan);
}

static int alloc_picture(MpegEncContext *s, Picture *pic, int shared)
{
    return ff_alloc_picture(s->avctx, pic, &s->me, &s->sc, shared, 0,
                            s->chroma_x_shift, s->chroma_y_shift, s->out_format,
                            s->mb_stride, s->mb_height, s->b8_stride,
                            &s->linesize, &s->uvlinesize);
}

static int init_duplicate_context(MpegEncContext *s)
{
    int y_size = s->b8_stride * (2 * s->mb_height + 1);
    int c_size = s->mb_stride * (s->mb_height + 1);
    int yc_size = y_size + 2 * c_size;
    int i;

    s->sc.edge_emu_buffer =
    s->me.scratchpad   =
    s->me.temp         =
    s->sc.rd_scratchpad   =
    s->sc.b_scratchpad    =
    s->sc.obmc_scratchpad = NULL;

    if (s->encoding) {
        FF_ALLOCZ_OR_GOTO(s->avctx, s->me.map,
                          ME_MAP_SIZE * sizeof(uint32_t), fail)
        FF_ALLOCZ_OR_GOTO(s->avctx, s->me.score_map,
                          ME_MAP_SIZE * sizeof(uint32_t), fail)
        if (s->noise_reduction) {
            FF_ALLOCZ_OR_GOTO(s->avctx, s->dct_error_sum,
                              2 * 64 * sizeof(int), fail)
        }
    }
    FF_ALLOCZ_OR_GOTO(s->avctx, s->blocks, 64 * 12 * 2 * sizeof(int16_t), fail)
    s->block = s->blocks[0];

    for (i = 0; i < 12; i++) {
        s->pblocks[i] = &s->block[i];
    }
    if (s->avctx->codec_tag == AV_RL32("VCR2")) {
        // exchange uv
        int16_t (*tmp)[64];
        tmp           = s->pblocks[4];
        s->pblocks[4] = s->pblocks[5];
        s->pblocks[5] = tmp;
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
    return -1; // free() through ff_mpv_common_end()
}

static void free_duplicate_context(MpegEncContext *s)
{
    if (!s)
        return;

    av_freep(&s->sc.edge_emu_buffer);
    av_freep(&s->me.scratchpad);
    s->me.temp =
    s->sc.rd_scratchpad =
    s->sc.b_scratchpad =
    s->sc.obmc_scratchpad = NULL;

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
    COPY(sc.edge_emu_buffer);
    COPY(me.scratchpad);
    COPY(me.temp);
    COPY(sc.rd_scratchpad);
    COPY(sc.b_scratchpad);
    COPY(sc.obmc_scratchpad);
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
    if (dst->avctx->codec_tag == AV_RL32("VCR2")) {
        // exchange uv
        int16_t (*tmp)[64];
        tmp             = dst->pblocks[4];
        dst->pblocks[4] = dst->pblocks[5];
        dst->pblocks[5] = tmp;
    }
    if (!dst->sc.edge_emu_buffer &&
        (ret = ff_mpeg_framesize_alloc(dst->avctx, &dst->me,
                                       &dst->sc, dst->linesize)) < 0) {
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

    if (dst == src || !s1->context_initialized)
        return 0;

    // FIXME can parameters change on I-frames?
    // in that case dst may need a reinit
    if (!s->context_initialized) {
        int err;
        memcpy(s, s1, sizeof(MpegEncContext));

        s->avctx                 = dst;
        s->bitstream_buffer      = NULL;
        s->bitstream_buffer_size = s->allocated_bitstream_buffer_size = 0;

        ff_mpv_idct_init(s);
        if ((err = ff_mpv_common_init(s)) < 0)
            return err;
    }

    if (s->height != s1->height || s->width != s1->width || s->context_reinit) {
        int err;
        s->context_reinit = 0;
        s->height = s1->height;
        s->width  = s1->width;
        if ((err = ff_mpv_common_frame_size_change(s)) < 0)
            return err;
    }

    s->avctx->coded_height  = s1->avctx->coded_height;
    s->avctx->coded_width   = s1->avctx->coded_width;
    s->avctx->width         = s1->avctx->width;
    s->avctx->height        = s1->avctx->height;

    s->coded_picture_number = s1->coded_picture_number;
    s->picture_number       = s1->picture_number;

    for (i = 0; i < MAX_PICTURE_COUNT; i++) {
        ff_mpeg_unref_picture(s->avctx, &s->picture[i]);
        if (s1->picture[i].f->buf[0] &&
            (ret = ff_mpeg_ref_picture(s->avctx, &s->picture[i], &s1->picture[i])) < 0)
            return ret;
    }

#define UPDATE_PICTURE(pic)\
do {\
    ff_mpeg_unref_picture(s->avctx, &s->pic);\
    if (s1->pic.f->buf[0])\
        ret = ff_mpeg_ref_picture(s->avctx, &s->pic, &s1->pic);\
    else\
        ret = ff_update_picture_tables(&s->pic, &s1->pic);\
    if (ret < 0)\
        return ret;\
} while (0)

    UPDATE_PICTURE(current_picture);
    UPDATE_PICTURE(last_picture);
    UPDATE_PICTURE(next_picture);

#define REBASE_PICTURE(pic, new_ctx, old_ctx)                                 \
    ((pic && pic >= old_ctx->picture &&                                       \
      pic < old_ctx->picture + MAX_PICTURE_COUNT) ?                           \
        &new_ctx->picture[pic - old_ctx->picture] : NULL)

    s->last_picture_ptr    = REBASE_PICTURE(s1->last_picture_ptr,    s, s1);
    s->current_picture_ptr = REBASE_PICTURE(s1->current_picture_ptr, s, s1);
    s->next_picture_ptr    = REBASE_PICTURE(s1->next_picture_ptr,    s, s1);

    // Error/bug resilience
    s->next_p_frame_damaged = s1->next_p_frame_damaged;
    s->workaround_bugs      = s1->workaround_bugs;

    // MPEG-4 timing info
    memcpy(&s->last_time_base, &s1->last_time_base,
           (char *) &s1->pb_field_time + sizeof(s1->pb_field_time) -
           (char *) &s1->last_time_base);

    // B-frame info
    s->max_b_frames = s1->max_b_frames;
    s->low_delay    = s1->low_delay;
    s->droppable    = s1->droppable;

    // DivX handling (doesn't work)
    s->divx_packed  = s1->divx_packed;

    if (s1->bitstream_buffer) {
        if (s1->bitstream_buffer_size +
            AV_INPUT_BUFFER_PADDING_SIZE > s->allocated_bitstream_buffer_size)
            av_fast_malloc(&s->bitstream_buffer,
                           &s->allocated_bitstream_buffer_size,
                           s1->allocated_bitstream_buffer_size);
        s->bitstream_buffer_size = s1->bitstream_buffer_size;
        memcpy(s->bitstream_buffer, s1->bitstream_buffer,
               s1->bitstream_buffer_size);
        memset(s->bitstream_buffer + s->bitstream_buffer_size, 0,
               AV_INPUT_BUFFER_PADDING_SIZE);
    }

    // linesize-dependent scratch buffer allocation
    if (!s->sc.edge_emu_buffer)
        if (s1->linesize) {
            if (ff_mpeg_framesize_alloc(s->avctx, &s->me,
                                        &s->sc, s1->linesize) < 0) {
                av_log(s->avctx, AV_LOG_ERROR, "Failed to allocate context "
                       "scratch buffers.\n");
                return AVERROR(ENOMEM);
            }
        } else {
            av_log(s->avctx, AV_LOG_ERROR, "Context scratch buffers could not "
                   "be allocated due to unknown size.\n");
            return AVERROR_BUG;
        }

    // MPEG-2/interlacing info
    memcpy(&s->progressive_sequence, &s1->progressive_sequence,
           (char *) &s1->rtp_mode - (char *) &s1->progressive_sequence);

    if (!s1->first_field) {
        s->last_pict_type = s1->pict_type;
        if (s1->current_picture_ptr)
            s->last_lambda_for[s1->pict_type] = s1->current_picture_ptr->f->quality;
    }

    return 0;
}

/**
 * Set the given MpegEncContext to common defaults
 * (same for encoding and decoding).
 * The changed fields will not depend upon the
 * prior state of the MpegEncContext.
 */
void ff_mpv_common_defaults(MpegEncContext *s)
{
    s->y_dc_scale_table      =
    s->c_dc_scale_table      = ff_mpeg1_dc_scale_table;
    s->chroma_qscale_table   = ff_default_chroma_qscale_table;
    s->progressive_frame     = 1;
    s->progressive_sequence  = 1;
    s->picture_structure     = PICT_FRAME;

    s->coded_picture_number  = 0;
    s->picture_number        = 0;

    s->f_code                = 1;
    s->b_code                = 1;

    s->slice_context_count   = 1;
}

/**
 * Set the given MpegEncContext to defaults for decoding.
 * the changed fields will not depend upon
 * the prior state of the MpegEncContext.
 */
void ff_mpv_decode_defaults(MpegEncContext *s)
{
    ff_mpv_common_defaults(s);
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
    mb_array_size = s->mb_height * s->mb_stride;
    mv_table_size = (s->mb_height + 2) * s->mb_stride + 1;

    /* set default edge pos, will be overridden
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

    FF_ALLOCZ_OR_GOTO(s->avctx, s->mb_index2xy, (s->mb_num + 1) * sizeof(int),
                      fail); // error resilience code looks cleaner with this
    for (y = 0; y < s->mb_height; y++)
        for (x = 0; x < s->mb_width; x++)
            s->mb_index2xy[x + y * s->mb_width] = x + y * s->mb_stride;

    s->mb_index2xy[s->mb_height * s->mb_width] =
        (s->mb_height - 1) * s->mb_stride + s->mb_width; // FIXME really needed?

    if (s->encoding) {
        /* Allocate MV tables */
        FF_ALLOCZ_OR_GOTO(s->avctx, s->p_mv_table_base,
                          mv_table_size * 2 * sizeof(int16_t), fail);
        FF_ALLOCZ_OR_GOTO(s->avctx, s->b_forw_mv_table_base,
                          mv_table_size * 2 * sizeof(int16_t), fail);
        FF_ALLOCZ_OR_GOTO(s->avctx, s->b_back_mv_table_base,
                          mv_table_size * 2 * sizeof(int16_t), fail);
        FF_ALLOCZ_OR_GOTO(s->avctx, s->b_bidir_forw_mv_table_base,
                          mv_table_size * 2 * sizeof(int16_t), fail);
        FF_ALLOCZ_OR_GOTO(s->avctx, s->b_bidir_back_mv_table_base,
                          mv_table_size * 2 * sizeof(int16_t), fail);
        FF_ALLOCZ_OR_GOTO(s->avctx, s->b_direct_mv_table_base,
                          mv_table_size * 2 * sizeof(int16_t), fail);
        s->p_mv_table            = s->p_mv_table_base + s->mb_stride + 1;
        s->b_forw_mv_table       = s->b_forw_mv_table_base + s->mb_stride + 1;
        s->b_back_mv_table       = s->b_back_mv_table_base + s->mb_stride + 1;
        s->b_bidir_forw_mv_table = s->b_bidir_forw_mv_table_base +
                                   s->mb_stride + 1;
        s->b_bidir_back_mv_table = s->b_bidir_back_mv_table_base +
                                   s->mb_stride + 1;
        s->b_direct_mv_table     = s->b_direct_mv_table_base + s->mb_stride + 1;

        /* Allocate MB type table */
        FF_ALLOCZ_OR_GOTO(s->avctx, s->mb_type, mb_array_size *
                          sizeof(uint16_t), fail); // needed for encoding

        FF_ALLOCZ_OR_GOTO(s->avctx, s->lambda_table, mb_array_size *
                          sizeof(int), fail);

        FF_ALLOC_OR_GOTO(s->avctx, s->cplx_tab,
                         mb_array_size * sizeof(float), fail);
        FF_ALLOC_OR_GOTO(s->avctx, s->bits_tab,
                         mb_array_size * sizeof(float), fail);

    }

    if (s->codec_id == AV_CODEC_ID_MPEG4 ||
        (s->avctx->flags & AV_CODEC_FLAG_INTERLACED_ME)) {
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
                FF_ALLOCZ_OR_GOTO(s->avctx, s->b_field_select_table [i][j],
                                  mb_array_size * 2 * sizeof(uint8_t), fail);
                FF_ALLOCZ_OR_GOTO(s->avctx, s->p_field_mv_table_base[i][j],
                                  mv_table_size * 2 * sizeof(int16_t), fail);
                s->p_field_mv_table[i][j] = s->p_field_mv_table_base[i][j]
                                            + s->mb_stride + 1;
            }
            FF_ALLOCZ_OR_GOTO(s->avctx, s->p_field_select_table[i],
                              mb_array_size * 2 * sizeof(uint8_t), fail);
        }
    }
    if (s->out_format == FMT_H263) {
        /* cbp values */
        FF_ALLOCZ_OR_GOTO(s->avctx, s->coded_block_base, y_size, fail);
        s->coded_block = s->coded_block_base + s->b8_stride + 1;

        /* cbp, ac_pred, pred_dir */
        FF_ALLOCZ_OR_GOTO(s->avctx, s->cbp_table,
                          mb_array_size * sizeof(uint8_t), fail);
        FF_ALLOCZ_OR_GOTO(s->avctx, s->pred_dir_table,
                          mb_array_size * sizeof(uint8_t), fail);
    }

    if (s->h263_pred || s->h263_plus || !s->encoding) {
        /* dc values */
        // MN: we need these for  error resilience of intra-frames
        FF_ALLOCZ_OR_GOTO(s->avctx, s->dc_val_base,
                          yc_size * sizeof(int16_t), fail);
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
    // Note the + 1 is for  a quicker MPEG-4 slice_end detection

    return ff_mpeg_er_init(s);
fail:
    return AVERROR(ENOMEM);
}

/**
 * init common structure for both encoder and decoder.
 * this assumes that some variables like width/height are already set
 */
av_cold int ff_mpv_common_init(MpegEncContext *s)
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

    dct_init(s);

    /* set chroma shifts */
    av_pix_fmt_get_chroma_sub_sample(s->avctx->pix_fmt,
                                     &s->chroma_x_shift,
                                     &s->chroma_y_shift);

    /* convert fourcc to upper case */
    s->codec_tag          = avpriv_toupper4(s->avctx->codec_tag);

    FF_ALLOCZ_OR_GOTO(s->avctx, s->picture,
                      MAX_PICTURE_COUNT * sizeof(Picture), fail);
    for (i = 0; i < MAX_PICTURE_COUNT; i++) {
        s->picture[i].f = av_frame_alloc();
        if (!s->picture[i].f)
            goto fail;
    }
    memset(&s->next_picture, 0, sizeof(s->next_picture));
    memset(&s->last_picture, 0, sizeof(s->last_picture));
    memset(&s->current_picture, 0, sizeof(s->current_picture));
    memset(&s->new_picture, 0, sizeof(s->new_picture));
    s->next_picture.f = av_frame_alloc();
    if (!s->next_picture.f)
        goto fail;
    s->last_picture.f = av_frame_alloc();
    if (!s->last_picture.f)
        goto fail;
    s->current_picture.f = av_frame_alloc();
    if (!s->current_picture.f)
        goto fail;
    s->new_picture.f = av_frame_alloc();
    if (!s->new_picture.f)
        goto fail;

    if (s->width && s->height) {
        if (init_context_frame(s))
            goto fail;

        s->parse_context.state = -1;
    }

    s->context_initialized = 1;
    s->thread_context[0]   = s;

    if (s->width && s->height) {
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
    }

    return 0;
 fail:
    ff_mpv_common_end(s);
    return -1;
}

/**
 * Frees and resets MpegEncContext fields depending on the resolution.
 * Is used during resolution changes to avoid a full reinitialization of the
 * codec.
 */
static void free_context_frame(MpegEncContext *s)
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
}

int ff_mpv_common_frame_size_change(MpegEncContext *s)
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

    free_context_frame(s);

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
        (err = av_image_check_size(s->width, s->height, 0, s->avctx)) < 0)
        goto fail;

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
                if ((err = init_duplicate_context(s->thread_context[i])) < 0)
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
    }

    return 0;
 fail:
    ff_mpv_common_end(s);
    return err;
}

/* init common structure for both encoder and decoder */
void ff_mpv_common_end(MpegEncContext *s)
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

    if (s->picture) {
        for (i = 0; i < MAX_PICTURE_COUNT; i++) {
            ff_free_picture_tables(&s->picture[i]);
            ff_mpeg_unref_picture(s->avctx, &s->picture[i]);
            av_frame_free(&s->picture[i].f);
        }
    }
    av_freep(&s->picture);
    ff_free_picture_tables(&s->last_picture);
    ff_mpeg_unref_picture(s->avctx, &s->last_picture);
    av_frame_free(&s->last_picture.f);
    ff_free_picture_tables(&s->current_picture);
    ff_mpeg_unref_picture(s->avctx, &s->current_picture);
    av_frame_free(&s->current_picture.f);
    ff_free_picture_tables(&s->next_picture);
    ff_mpeg_unref_picture(s->avctx, &s->next_picture);
    av_frame_free(&s->next_picture.f);
    ff_free_picture_tables(&s->new_picture);
    ff_mpeg_unref_picture(s->avctx, &s->new_picture);
    av_frame_free(&s->new_picture.f);

    free_context_frame(s);

    s->context_initialized      = 0;
    s->last_picture_ptr         =
    s->next_picture_ptr         =
    s->current_picture_ptr      = NULL;
    s->linesize = s->uvlinesize = 0;
}

/**
 * generic function called after decoding
 * the header and before a frame is decoded.
 */
int ff_mpv_frame_start(MpegEncContext *s, AVCodecContext *avctx)
{
    int i, ret;
    Picture *pic;
    s->mb_skipped = 0;

    /* mark & release old frames */
    if (s->pict_type != AV_PICTURE_TYPE_B && s->last_picture_ptr &&
        s->last_picture_ptr != s->next_picture_ptr &&
        s->last_picture_ptr->f->buf[0]) {
        ff_mpeg_unref_picture(s->avctx, s->last_picture_ptr);
    }

    /* release forgotten pictures */
    /* if (MPEG-124 / H.263) */
    for (i = 0; i < MAX_PICTURE_COUNT; i++) {
        if (&s->picture[i] != s->last_picture_ptr &&
            &s->picture[i] != s->next_picture_ptr &&
            s->picture[i].reference && !s->picture[i].needs_realloc) {
            ff_mpeg_unref_picture(s->avctx, &s->picture[i]);
        }
    }

    ff_mpeg_unref_picture(s->avctx, &s->current_picture);

    /* release non reference frames */
    for (i = 0; i < MAX_PICTURE_COUNT; i++) {
        if (!s->picture[i].reference)
            ff_mpeg_unref_picture(s->avctx, &s->picture[i]);
    }

    if (s->current_picture_ptr && !s->current_picture_ptr->f->buf[0]) {
        // we already have a unused image
        // (maybe it was set before reading the header)
        pic = s->current_picture_ptr;
    } else {
        i   = ff_find_unused_picture(s->avctx, s->picture, 0);
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

    pic->f->coded_picture_number = s->coded_picture_number++;

    if (alloc_picture(s, pic, 0) < 0)
        return -1;

    s->current_picture_ptr = pic;
    // FIXME use only the vars from current_pic
    s->current_picture_ptr->f->top_field_first = s->top_field_first;
    if (s->codec_id == AV_CODEC_ID_MPEG1VIDEO ||
        s->codec_id == AV_CODEC_ID_MPEG2VIDEO) {
        if (s->picture_structure != PICT_FRAME)
            s->current_picture_ptr->f->top_field_first =
                (s->picture_structure == PICT_TOP_FIELD) == s->first_field;
    }
    s->current_picture_ptr->f->interlaced_frame = !s->progressive_frame &&
                                                 !s->progressive_sequence;
    s->current_picture_ptr->field_picture      =  s->picture_structure != PICT_FRAME;

    s->current_picture_ptr->f->pict_type = s->pict_type;
    // if (s->avctx->flags && AV_CODEC_FLAG_QSCALE)
    //     s->current_picture_ptr->quality = s->new_picture_ptr->quality;
    s->current_picture_ptr->f->key_frame = s->pict_type == AV_PICTURE_TYPE_I;

    if ((ret = ff_mpeg_ref_picture(s->avctx, &s->current_picture,
                                   s->current_picture_ptr)) < 0)
        return ret;

    if (s->pict_type != AV_PICTURE_TYPE_B) {
        s->last_picture_ptr = s->next_picture_ptr;
        if (!s->droppable)
            s->next_picture_ptr = s->current_picture_ptr;
    }
    ff_dlog(s->avctx, "L%p N%p C%p L%p N%p C%p type:%d drop:%d\n",
            s->last_picture_ptr, s->next_picture_ptr,s->current_picture_ptr,
            s->last_picture_ptr    ? s->last_picture_ptr->f->data[0]    : NULL,
            s->next_picture_ptr    ? s->next_picture_ptr->f->data[0]    : NULL,
            s->current_picture_ptr ? s->current_picture_ptr->f->data[0] : NULL,
            s->pict_type, s->droppable);

    if ((!s->last_picture_ptr || !s->last_picture_ptr->f->buf[0]) &&
        (s->pict_type != AV_PICTURE_TYPE_I ||
         s->picture_structure != PICT_FRAME)) {
        int h_chroma_shift, v_chroma_shift;
        av_pix_fmt_get_chroma_sub_sample(s->avctx->pix_fmt,
                                         &h_chroma_shift, &v_chroma_shift);
        if (s->pict_type != AV_PICTURE_TYPE_I)
            av_log(avctx, AV_LOG_ERROR,
                   "warning: first frame is no keyframe\n");
        else if (s->picture_structure != PICT_FRAME)
            av_log(avctx, AV_LOG_INFO,
                   "allocate dummy last picture for field based first keyframe\n");

        /* Allocate a dummy frame */
        i = ff_find_unused_picture(s->avctx, s->picture, 0);
        if (i < 0) {
            av_log(s->avctx, AV_LOG_ERROR, "no frame buffer available\n");
            return i;
        }
        s->last_picture_ptr = &s->picture[i];

        s->last_picture_ptr->reference   = 3;
        s->last_picture_ptr->f->pict_type = AV_PICTURE_TYPE_I;

        if (alloc_picture(s, s->last_picture_ptr, 0) < 0) {
            s->last_picture_ptr = NULL;
            return -1;
        }

        memset(s->last_picture_ptr->f->data[0], 0,
               avctx->height * s->last_picture_ptr->f->linesize[0]);
        memset(s->last_picture_ptr->f->data[1], 0x80,
               (avctx->height >> v_chroma_shift) *
               s->last_picture_ptr->f->linesize[1]);
        memset(s->last_picture_ptr->f->data[2], 0x80,
               (avctx->height >> v_chroma_shift) *
               s->last_picture_ptr->f->linesize[2]);

        ff_thread_report_progress(&s->last_picture_ptr->tf, INT_MAX, 0);
        ff_thread_report_progress(&s->last_picture_ptr->tf, INT_MAX, 1);
    }
    if ((!s->next_picture_ptr || !s->next_picture_ptr->f->buf[0]) &&
        s->pict_type == AV_PICTURE_TYPE_B) {
        /* Allocate a dummy frame */
        i = ff_find_unused_picture(s->avctx, s->picture, 0);
        if (i < 0) {
            av_log(s->avctx, AV_LOG_ERROR, "no frame buffer available\n");
            return i;
        }
        s->next_picture_ptr = &s->picture[i];

        s->next_picture_ptr->reference   = 3;
        s->next_picture_ptr->f->pict_type = AV_PICTURE_TYPE_I;

        if (alloc_picture(s, s->next_picture_ptr, 0) < 0) {
            s->next_picture_ptr = NULL;
            return -1;
        }
        ff_thread_report_progress(&s->next_picture_ptr->tf, INT_MAX, 0);
        ff_thread_report_progress(&s->next_picture_ptr->tf, INT_MAX, 1);
    }

    if (s->last_picture_ptr) {
        ff_mpeg_unref_picture(s->avctx, &s->last_picture);
        if (s->last_picture_ptr->f->buf[0] &&
            (ret = ff_mpeg_ref_picture(s->avctx, &s->last_picture,
                                       s->last_picture_ptr)) < 0)
            return ret;
    }
    if (s->next_picture_ptr) {
        ff_mpeg_unref_picture(s->avctx, &s->next_picture);
        if (s->next_picture_ptr->f->buf[0] &&
            (ret = ff_mpeg_ref_picture(s->avctx, &s->next_picture,
                                       s->next_picture_ptr)) < 0)
            return ret;
    }

    if (s->pict_type != AV_PICTURE_TYPE_I &&
        !(s->last_picture_ptr && s->last_picture_ptr->f->buf[0])) {
        av_log(s, AV_LOG_ERROR,
               "Non-reference picture received and no reference available\n");
        return AVERROR_INVALIDDATA;
    }

    if (s->picture_structure!= PICT_FRAME) {
        int i;
        for (i = 0; i < 4; i++) {
            if (s->picture_structure == PICT_BOTTOM_FIELD) {
                s->current_picture.f->data[i] +=
                    s->current_picture.f->linesize[i];
            }
            s->current_picture.f->linesize[i] *= 2;
            s->last_picture.f->linesize[i]    *= 2;
            s->next_picture.f->linesize[i]    *= 2;
        }
    }

    /* set dequantizer, we can't do it during init as
     * it might change for MPEG-4 and we can't do it in the header
     * decode as init is not called for MPEG-4 there yet */
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

#if FF_API_XVMC
FF_DISABLE_DEPRECATION_WARNINGS
    if (CONFIG_MPEG_XVMC_DECODER && s->avctx->xvmc_acceleration)
        return ff_xvmc_field_start(s, avctx);
FF_ENABLE_DEPRECATION_WARNINGS
#endif /* FF_API_XVMC */

    return 0;
}

/* called after a frame has been decoded. */
void ff_mpv_frame_end(MpegEncContext *s)
{
#if FF_API_XVMC
FF_DISABLE_DEPRECATION_WARNINGS
    /* redraw edges for the frame if decoding didn't complete */
    // just to make sure that all data is rendered.
    if (CONFIG_MPEG_XVMC_DECODER && s->avctx->xvmc_acceleration) {
        ff_xvmc_field_end(s);
    } else
FF_ENABLE_DEPRECATION_WARNINGS
#endif /* FF_API_XVMC */

    emms_c();

    if (s->current_picture.reference)
        ff_thread_report_progress(&s->current_picture_ptr->tf, INT_MAX, 0);
}

/**
 * Print debugging info for the given picture.
 */
void ff_print_debug_info(MpegEncContext *s, Picture *p)
{
    AVFrame *pict;
    if (s->avctx->hwaccel || !p || !p->mb_type)
        return;
    pict = p->f;

    if (s->avctx->debug & (FF_DEBUG_SKIP | FF_DEBUG_QP | FF_DEBUG_MB_TYPE)) {
        int x,y;

        av_log(s->avctx,AV_LOG_DEBUG,"New frame, type: ");
        switch (pict->pict_type) {
        case AV_PICTURE_TYPE_I:
            av_log(s->avctx,AV_LOG_DEBUG,"I\n");
            break;
        case AV_PICTURE_TYPE_P:
            av_log(s->avctx,AV_LOG_DEBUG,"P\n");
            break;
        case AV_PICTURE_TYPE_B:
            av_log(s->avctx,AV_LOG_DEBUG,"B\n");
            break;
        case AV_PICTURE_TYPE_S:
            av_log(s->avctx,AV_LOG_DEBUG,"S\n");
            break;
        case AV_PICTURE_TYPE_SI:
            av_log(s->avctx,AV_LOG_DEBUG,"SI\n");
            break;
        case AV_PICTURE_TYPE_SP:
            av_log(s->avctx,AV_LOG_DEBUG,"SP\n");
            break;
        }
        for (y = 0; y < s->mb_height; y++) {
            for (x = 0; x < s->mb_width; x++) {
                if (s->avctx->debug & FF_DEBUG_SKIP) {
                    int count = s->mbskip_table[x + y * s->mb_stride];
                    if (count > 9)
                        count = 9;
                    av_log(s->avctx, AV_LOG_DEBUG, "%1d", count);
                }
                if (s->avctx->debug & FF_DEBUG_QP) {
                    av_log(s->avctx, AV_LOG_DEBUG, "%2d",
                           p->qscale_table[x + y * s->mb_stride]);
                }
                if (s->avctx->debug & FF_DEBUG_MB_TYPE) {
                    int mb_type = p->mb_type[x + y * s->mb_stride];
                    // Type & MV direction
                    if (IS_PCM(mb_type))
                        av_log(s->avctx, AV_LOG_DEBUG, "P");
                    else if (IS_INTRA(mb_type) && IS_ACPRED(mb_type))
                        av_log(s->avctx, AV_LOG_DEBUG, "A");
                    else if (IS_INTRA4x4(mb_type))
                        av_log(s->avctx, AV_LOG_DEBUG, "i");
                    else if (IS_INTRA16x16(mb_type))
                        av_log(s->avctx, AV_LOG_DEBUG, "I");
                    else if (IS_DIRECT(mb_type) && IS_SKIP(mb_type))
                        av_log(s->avctx, AV_LOG_DEBUG, "d");
                    else if (IS_DIRECT(mb_type))
                        av_log(s->avctx, AV_LOG_DEBUG, "D");
                    else if (IS_GMC(mb_type) && IS_SKIP(mb_type))
                        av_log(s->avctx, AV_LOG_DEBUG, "g");
                    else if (IS_GMC(mb_type))
                        av_log(s->avctx, AV_LOG_DEBUG, "G");
                    else if (IS_SKIP(mb_type))
                        av_log(s->avctx, AV_LOG_DEBUG, "S");
                    else if (!USES_LIST(mb_type, 1))
                        av_log(s->avctx, AV_LOG_DEBUG, ">");
                    else if (!USES_LIST(mb_type, 0))
                        av_log(s->avctx, AV_LOG_DEBUG, "<");
                    else {
                        assert(USES_LIST(mb_type, 0) && USES_LIST(mb_type, 1));
                        av_log(s->avctx, AV_LOG_DEBUG, "X");
                    }

                    // segmentation
                    if (IS_8X8(mb_type))
                        av_log(s->avctx, AV_LOG_DEBUG, "+");
                    else if (IS_16X8(mb_type))
                        av_log(s->avctx, AV_LOG_DEBUG, "-");
                    else if (IS_8X16(mb_type))
                        av_log(s->avctx, AV_LOG_DEBUG, "|");
                    else if (IS_INTRA(mb_type) || IS_16X16(mb_type))
                        av_log(s->avctx, AV_LOG_DEBUG, " ");
                    else
                        av_log(s->avctx, AV_LOG_DEBUG, "?");


                    if (IS_INTERLACED(mb_type))
                        av_log(s->avctx, AV_LOG_DEBUG, "=");
                    else
                        av_log(s->avctx, AV_LOG_DEBUG, " ");
                }
            }
            av_log(s->avctx, AV_LOG_DEBUG, "\n");
        }
    }
}

/**
 * find the lowest MB row referenced in the MVs
 */
static int lowest_referenced_row(MpegEncContext *s, int dir)
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
    s->idsp.idct_put(dest, line_size, block);
}

/* add block[] to dest[] */
static inline void add_dct(MpegEncContext *s,
                           int16_t *block, int i, uint8_t *dest, int line_size)
{
    if (s->block_last_index[i] >= 0) {
        s->idsp.idct_add(dest, line_size, block);
    }
}

static inline void add_dequant_dct(MpegEncContext *s,
                           int16_t *block, int i, uint8_t *dest, int line_size, int qscale)
{
    if (s->block_last_index[i] >= 0) {
        s->dct_unquantize_inter(s, block, i, qscale);

        s->idsp.idct_add(dest, line_size, block);
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
void mpv_decode_mb_internal(MpegEncContext *s, int16_t block[12][64],
                            int is_mpeg12)
{
    const int mb_xy = s->mb_y * s->mb_stride + s->mb_x;

#if FF_API_XVMC
FF_DISABLE_DEPRECATION_WARNINGS
    if(CONFIG_MPEG_XVMC_DECODER && s->avctx->xvmc_acceleration){
        ff_xvmc_decode_mb(s);//xvmc uses pblocks
        return;
    }
FF_ENABLE_DEPRECATION_WARNINGS
#endif /* FF_API_XVMC */

    if(s->avctx->debug&FF_DEBUG_DCT_COEFF) {
       /* print DCT coefficients */
       int i,j;
       av_log(s->avctx, AV_LOG_DEBUG, "DCT coeffs of MB at %dx%d:\n", s->mb_x, s->mb_y);
       for(i=0; i<6; i++){
           for(j=0; j<64; j++){
               av_log(s->avctx, AV_LOG_DEBUG, "%5d",
                      block[i][s->idsp.idct_permutation[j]]);
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

    if ((s->avctx->flags & AV_CODEC_FLAG_PSNR) ||
        !(s->encoding && (s->intra_only || s->pict_type == AV_PICTURE_TYPE_B) &&
          s->avctx->mb_decision != FF_MB_DECISION_RD)) { // FIXME precalc
        uint8_t *dest_y, *dest_cb, *dest_cr;
        int dct_linesize, dct_offset;
        op_pixels_func (*op_pix)[4];
        qpel_mc_func (*op_qpix)[16];
        const int linesize   = s->current_picture.f->linesize[0]; //not s->linesize as this would be wrong for field pics
        const int uvlinesize = s->current_picture.f->linesize[1];
        const int readable= s->pict_type != AV_PICTURE_TYPE_B || s->encoding || s->avctx->draw_horiz_band;
        const int block_size = 8;

        /* avoid copy if macroblock skipped in last frame too */
        /* skip only during decoding as we might trash the buffers during encoding a bit */
        if(!s->encoding){
            uint8_t *mbskip_ptr = &s->mbskip_table[mb_xy];

            if (s->mb_skipped) {
                s->mb_skipped= 0;
                assert(s->pict_type!=AV_PICTURE_TYPE_I);
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
            dest_y = s->sc.b_scratchpad;
            dest_cb= s->sc.b_scratchpad+16*linesize;
            dest_cr= s->sc.b_scratchpad+32*linesize;
        }

        if (!s->mb_intra) {
            /* motion handling */
            /* decoding or more than one mb_type (MC was already done otherwise) */
            if(!s->encoding){

                if(HAVE_THREADS && s->avctx->active_thread_type&FF_THREAD_FRAME) {
                    if (s->mv_dir & MV_DIR_FORWARD) {
                        ff_thread_await_progress(&s->last_picture_ptr->tf,
                                                 lowest_referenced_row(s, 0),
                                                 0);
                    }
                    if (s->mv_dir & MV_DIR_BACKWARD) {
                        ff_thread_await_progress(&s->next_picture_ptr->tf,
                                                 lowest_referenced_row(s, 1),
                                                 0);
                    }
                }

                op_qpix= s->me.qpel_put;
                if ((!s->no_rounding) || s->pict_type==AV_PICTURE_TYPE_B){
                    op_pix = s->hdsp.put_pixels_tab;
                }else{
                    op_pix = s->hdsp.put_no_rnd_pixels_tab;
                }
                if (s->mv_dir & MV_DIR_FORWARD) {
                    ff_mpv_motion(s, dest_y, dest_cb, dest_cr, 0, s->last_picture.f->data, op_pix, op_qpix);
                    op_pix = s->hdsp.avg_pixels_tab;
                    op_qpix= s->me.qpel_avg;
                }
                if (s->mv_dir & MV_DIR_BACKWARD) {
                    ff_mpv_motion(s, dest_y, dest_cb, dest_cr, 1, s->next_picture.f->data, op_pix, op_qpix);
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

                if (!CONFIG_GRAY || !(s->avctx->flags & AV_CODEC_FLAG_GRAY)) {
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

                if (!CONFIG_GRAY || !(s->avctx->flags & AV_CODEC_FLAG_GRAY)) {
                    if(s->chroma_y_shift){//Chroma420
                        add_dct(s, block[4], 4, dest_cb, uvlinesize);
                        add_dct(s, block[5], 5, dest_cr, uvlinesize);
                    }else{
                        //chroma422
                        dct_linesize = uvlinesize << s->interlaced_dct;
                        dct_offset   = s->interlaced_dct ? uvlinesize : uvlinesize * 8;

                        add_dct(s, block[4], 4, dest_cb, dct_linesize);
                        add_dct(s, block[5], 5, dest_cr, dct_linesize);
                        add_dct(s, block[6], 6, dest_cb+dct_offset, dct_linesize);
                        add_dct(s, block[7], 7, dest_cr+dct_offset, dct_linesize);
                        if(!s->chroma_x_shift){//Chroma444
                            add_dct(s, block[8], 8, dest_cb+8, dct_linesize);
                            add_dct(s, block[9], 9, dest_cr+8, dct_linesize);
                            add_dct(s, block[10], 10, dest_cb+8+dct_offset, dct_linesize);
                            add_dct(s, block[11], 11, dest_cr+8+dct_offset, dct_linesize);
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

                if (!CONFIG_GRAY || !(s->avctx->flags & AV_CODEC_FLAG_GRAY)) {
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
                s->idsp.idct_put(dest_y,                           dct_linesize, block[0]);
                s->idsp.idct_put(dest_y              + block_size, dct_linesize, block[1]);
                s->idsp.idct_put(dest_y + dct_offset,              dct_linesize, block[2]);
                s->idsp.idct_put(dest_y + dct_offset + block_size, dct_linesize, block[3]);

                if (!CONFIG_GRAY || !(s->avctx->flags & AV_CODEC_FLAG_GRAY)) {
                    if(s->chroma_y_shift){
                        s->idsp.idct_put(dest_cb, uvlinesize, block[4]);
                        s->idsp.idct_put(dest_cr, uvlinesize, block[5]);
                    }else{

                        dct_linesize = uvlinesize << s->interlaced_dct;
                        dct_offset   = s->interlaced_dct ? uvlinesize : uvlinesize * 8;

                        s->idsp.idct_put(dest_cb,              dct_linesize, block[4]);
                        s->idsp.idct_put(dest_cr,              dct_linesize, block[5]);
                        s->idsp.idct_put(dest_cb + dct_offset, dct_linesize, block[6]);
                        s->idsp.idct_put(dest_cr + dct_offset, dct_linesize, block[7]);
                        if(!s->chroma_x_shift){//Chroma444
                            s->idsp.idct_put(dest_cb + 8,              dct_linesize, block[8]);
                            s->idsp.idct_put(dest_cr + 8,              dct_linesize, block[9]);
                            s->idsp.idct_put(dest_cb + 8 + dct_offset, dct_linesize, block[10]);
                            s->idsp.idct_put(dest_cr + 8 + dct_offset, dct_linesize, block[11]);
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

void ff_mpv_decode_mb(MpegEncContext *s, int16_t block[12][64])
{
#if !CONFIG_SMALL
    if(s->out_format == FMT_MPEG1) {
        mpv_decode_mb_internal(s, block, 1);
    } else
#endif
        mpv_decode_mb_internal(s, block, 0);
}

void ff_mpeg_draw_horiz_band(MpegEncContext *s, int y, int h)
{
    ff_draw_horiz_band(s->avctx, s->current_picture.f,
                       s->last_picture.f, y, h, s->picture_structure,
                       s->first_field, s->low_delay);
}

void ff_init_block_index(MpegEncContext *s){ //FIXME maybe rename
    const int linesize   = s->current_picture.f->linesize[0]; //not s->linesize as this would be wrong for field pics
    const int uvlinesize = s->current_picture.f->linesize[1];
    const int mb_size= 4;

    s->block_index[0]= s->b8_stride*(s->mb_y*2    ) - 2 + s->mb_x*2;
    s->block_index[1]= s->b8_stride*(s->mb_y*2    ) - 1 + s->mb_x*2;
    s->block_index[2]= s->b8_stride*(s->mb_y*2 + 1) - 2 + s->mb_x*2;
    s->block_index[3]= s->b8_stride*(s->mb_y*2 + 1) - 1 + s->mb_x*2;
    s->block_index[4]= s->mb_stride*(s->mb_y + 1)                + s->b8_stride*s->mb_height*2 + s->mb_x - 1;
    s->block_index[5]= s->mb_stride*(s->mb_y + s->mb_height + 2) + s->b8_stride*s->mb_height*2 + s->mb_x - 1;
    //block_index is not used by mpeg2, so it is not affected by chroma_format

    s->dest[0] = s->current_picture.f->data[0] + (s->mb_x - 1) * (1 << mb_size);
    s->dest[1] = s->current_picture.f->data[1] + (s->mb_x - 1) * (1 << (mb_size - s->chroma_x_shift));
    s->dest[2] = s->current_picture.f->data[2] + (s->mb_x - 1) * (1 << (mb_size - s->chroma_x_shift));

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
            assert((s->mb_y&1) == (s->picture_structure == PICT_BOTTOM_FIELD));
        }
    }
}

void ff_mpeg_flush(AVCodecContext *avctx){
    int i;
    MpegEncContext *s = avctx->priv_data;

    if (!s || !s->picture)
        return;

    for (i = 0; i < MAX_PICTURE_COUNT; i++)
        ff_mpeg_unref_picture(s->avctx, &s->picture[i]);
    s->current_picture_ptr = s->last_picture_ptr = s->next_picture_ptr = NULL;

    ff_mpeg_unref_picture(s->avctx, &s->current_picture);
    ff_mpeg_unref_picture(s->avctx, &s->last_picture);
    ff_mpeg_unref_picture(s->avctx, &s->next_picture);

    s->mb_x= s->mb_y= 0;

    s->parse_context.state= -1;
    s->parse_context.frame_start_found= 0;
    s->parse_context.overread= 0;
    s->parse_context.overread_index= 0;
    s->parse_context.index= 0;
    s->parse_context.last_index= 0;
    s->bitstream_buffer_size=0;
    s->pp_time=0;
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

void ff_mpv_report_decode_progress(MpegEncContext *s)
{
    if (s->pict_type != AV_PICTURE_TYPE_B && !s->partitioned_frame && !s->er.error_occurred)
        ff_thread_report_progress(&s->current_picture_ptr->tf, s->mb_y, 0);
}
