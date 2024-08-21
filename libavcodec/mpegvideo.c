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
#include "libavutil/internal.h"
#include "libavutil/mem.h"

#include "avcodec.h"
#include "blockdsp.h"
#include "idctdsp.h"
#include "mathops.h"
#include "mpeg_er.h"
#include "mpegutils.h"
#include "mpegvideo.h"
#include "mpegvideodata.h"
#include "refstruct.h"

static void dct_unquantize_mpeg1_intra_c(MpegEncContext *s,
                                   int16_t *block, int n, int qscale)
{
    int i, level, nCoeffs;
    const uint16_t *quant_matrix;

    nCoeffs= s->block_last_index[n];

    block[0] *= n < 4 ? s->y_dc_scale : s->c_dc_scale;
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

    if (s->q_scale_type) qscale = ff_mpeg2_non_linear_qscale[qscale];
    else                 qscale <<= 1;

    nCoeffs= s->block_last_index[n];

    block[0] *= n < 4 ? s->y_dc_scale : s->c_dc_scale;
    quant_matrix = s->intra_matrix;
    for(i=1;i<=nCoeffs;i++) {
        int j= s->intra_scantable.permutated[i];
        level = block[j];
        if (level) {
            if (level < 0) {
                level = -level;
                level = (int)(level * qscale * quant_matrix[j]) >> 4;
                level = -level;
            } else {
                level = (int)(level * qscale * quant_matrix[j]) >> 4;
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

    if (s->q_scale_type) qscale = ff_mpeg2_non_linear_qscale[qscale];
    else                 qscale <<= 1;

    nCoeffs= s->block_last_index[n];

    block[0] *= n < 4 ? s->y_dc_scale : s->c_dc_scale;
    sum += block[0];
    quant_matrix = s->intra_matrix;
    for(i=1;i<=nCoeffs;i++) {
        int j= s->intra_scantable.permutated[i];
        level = block[j];
        if (level) {
            if (level < 0) {
                level = -level;
                level = (int)(level * qscale * quant_matrix[j]) >> 4;
                level = -level;
            } else {
                level = (int)(level * qscale * quant_matrix[j]) >> 4;
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

    if (s->q_scale_type) qscale = ff_mpeg2_non_linear_qscale[qscale];
    else                 qscale <<= 1;

    nCoeffs= s->block_last_index[n];

    quant_matrix = s->inter_matrix;
    for(i=0; i<=nCoeffs; i++) {
        int j= s->intra_scantable.permutated[i];
        level = block[j];
        if (level) {
            if (level < 0) {
                level = -level;
                level = (((level << 1) + 1) * qscale *
                         ((int) (quant_matrix[j]))) >> 5;
                level = -level;
            } else {
                level = (((level << 1) + 1) * qscale *
                         ((int) (quant_matrix[j]))) >> 5;
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
        nCoeffs= s->intra_scantable.raster_end[ s->block_last_index[n] ];

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


static void gray16(uint8_t *dst, const uint8_t *src, ptrdiff_t linesize, int h)
{
    while(h--)
        memset(dst + h*linesize, 128, 16);
}

static void gray8(uint8_t *dst, const uint8_t *src, ptrdiff_t linesize, int h)
{
    while(h--)
        memset(dst + h*linesize, 128, 8);
}

/* init common dct for both encoder and decoder */
static av_cold void dsp_init(MpegEncContext *s)
{
    ff_blockdsp_init(&s->bdsp);
    ff_hpeldsp_init(&s->hdsp, s->avctx->flags);
    ff_videodsp_init(&s->vdsp, s->avctx->bits_per_raw_sample);

    if (s->avctx->debug & FF_DEBUG_NOMC) {
        int i;
        for (i=0; i<4; i++) {
            s->hdsp.avg_pixels_tab[0][i] = gray16;
            s->hdsp.put_pixels_tab[0][i] = gray16;
            s->hdsp.put_no_rnd_pixels_tab[0][i] = gray16;

            s->hdsp.avg_pixels_tab[1][i] = gray8;
            s->hdsp.put_pixels_tab[1][i] = gray8;
            s->hdsp.put_no_rnd_pixels_tab[1][i] = gray8;
        }
    }
}

av_cold void ff_init_scantable(const uint8_t *permutation, ScanTable *st,
                               const uint8_t *src_scantable)
{
    st->scantable = src_scantable;

    for (int i = 0, end = -1; i < 64; i++) {
        int j = src_scantable[i];
        st->permutated[i] = permutation[j];
        if (permutation[j] > end)
            end = permutation[j];
        st->raster_end[i] = end;
    }
}

av_cold void ff_mpv_idct_init(MpegEncContext *s)
{
    if (s->codec_id == AV_CODEC_ID_MPEG4)
        s->idsp.mpeg4_studio_profile = s->studio_profile;
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
    ff_permute_scantable(s->permutated_intra_h_scantable, ff_alternate_horizontal_scan,
                         s->idsp.idct_permutation);
    ff_permute_scantable(s->permutated_intra_v_scantable, ff_alternate_vertical_scan,
                         s->idsp.idct_permutation);

    s->dct_unquantize_h263_intra  = dct_unquantize_h263_intra_c;
    s->dct_unquantize_h263_inter  = dct_unquantize_h263_inter_c;
    s->dct_unquantize_mpeg1_intra = dct_unquantize_mpeg1_intra_c;
    s->dct_unquantize_mpeg1_inter = dct_unquantize_mpeg1_inter_c;
    s->dct_unquantize_mpeg2_intra = dct_unquantize_mpeg2_intra_c;
    if (s->avctx->flags & AV_CODEC_FLAG_BITEXACT)
        s->dct_unquantize_mpeg2_intra = dct_unquantize_mpeg2_intra_bitexact;
    s->dct_unquantize_mpeg2_inter = dct_unquantize_mpeg2_inter_c;

#if HAVE_INTRINSICS_NEON
    ff_mpv_common_init_neon(s);
#endif

#if ARCH_ARM
    ff_mpv_common_init_arm(s);
#elif ARCH_PPC
    ff_mpv_common_init_ppc(s);
#elif ARCH_X86
    ff_mpv_common_init_x86(s);
#elif ARCH_MIPS
    ff_mpv_common_init_mips(s);
#endif
}

static int init_duplicate_context(MpegEncContext *s)
{
    if (s->encoding) {
        s->me.map = av_mallocz(2 * ME_MAP_SIZE * sizeof(*s->me.map));
        if (!s->me.map)
            return AVERROR(ENOMEM);
        s->me.score_map = s->me.map + ME_MAP_SIZE;

        if (s->noise_reduction) {
            if (!FF_ALLOCZ_TYPED_ARRAY(s->dct_error_sum,  2))
                return AVERROR(ENOMEM);
        }
    }
    if (!FF_ALLOCZ_TYPED_ARRAY(s->blocks,  1 + s->encoding))
        return AVERROR(ENOMEM);
    s->block = s->blocks[0];

    if (s->out_format == FMT_H263) {
        int mb_height = s->msmpeg4_version == MSMP4_VC1 ?
                            FFALIGN(s->mb_height, 2) : s->mb_height;
        int y_size = s->b8_stride * (2 * mb_height + 1);
        int c_size = s->mb_stride * (mb_height + 1);
        int yc_size = y_size + 2 * c_size;
        /* ac values */
        if (!FF_ALLOCZ_TYPED_ARRAY(s->ac_val_base,  yc_size))
            return AVERROR(ENOMEM);
        s->ac_val[0] = s->ac_val_base + s->b8_stride + 1;
        s->ac_val[1] = s->ac_val_base + y_size + s->mb_stride + 1;
        s->ac_val[2] = s->ac_val[1] + c_size;
    }

    return 0;
}

int ff_mpv_init_duplicate_contexts(MpegEncContext *s)
{
    int nb_slices = s->slice_context_count, ret;

    /* We initialize the copies before the original so that
     * fields allocated in init_duplicate_context are NULL after
     * copying. This prevents double-frees upon allocation error. */
    for (int i = 1; i < nb_slices; i++) {
        s->thread_context[i] = av_memdup(s, sizeof(MpegEncContext));
        if (!s->thread_context[i])
            return AVERROR(ENOMEM);
        if ((ret = init_duplicate_context(s->thread_context[i])) < 0)
            return ret;
        s->thread_context[i]->start_mb_y =
            (s->mb_height * (i    ) + nb_slices / 2) / nb_slices;
        s->thread_context[i]->end_mb_y   =
            (s->mb_height * (i + 1) + nb_slices / 2) / nb_slices;
    }
    s->start_mb_y = 0;
    s->end_mb_y   = nb_slices > 1 ? (s->mb_height + nb_slices / 2) / nb_slices
                                  : s->mb_height;
    return init_duplicate_context(s);
}

static void free_duplicate_context(MpegEncContext *s)
{
    if (!s)
        return;

    av_freep(&s->sc.edge_emu_buffer);
    av_freep(&s->sc.scratchpad_buf);
    s->me.temp = s->me.scratchpad =
    s->sc.obmc_scratchpad = NULL;
    s->sc.linesize = 0;

    av_freep(&s->dct_error_sum);
    av_freep(&s->me.map);
    s->me.score_map = NULL;
    av_freep(&s->blocks);
    av_freep(&s->ac_val_base);
    s->block = NULL;
}

static void free_duplicate_contexts(MpegEncContext *s)
{
    for (int i = 1; i < s->slice_context_count; i++) {
        free_duplicate_context(s->thread_context[i]);
        av_freep(&s->thread_context[i]);
    }
    free_duplicate_context(s);
}

static void backup_duplicate_context(MpegEncContext *bak, MpegEncContext *src)
{
#define COPY(a) bak->a = src->a
    COPY(sc);
    COPY(me.map);
    COPY(me.score_map);
    COPY(blocks);
    COPY(block);
    COPY(start_mb_y);
    COPY(end_mb_y);
    COPY(me.map_generation);
    COPY(dct_error_sum);
    COPY(dct_count[0]);
    COPY(dct_count[1]);
    COPY(ac_val_base);
    COPY(ac_val[0]);
    COPY(ac_val[1]);
    COPY(ac_val[2]);
#undef COPY
}

int ff_update_duplicate_context(MpegEncContext *dst, const MpegEncContext *src)
{
    MpegEncContext bak;
    int ret;
    // FIXME copy only needed parts
    backup_duplicate_context(&bak, dst);
    memcpy(dst, src, sizeof(MpegEncContext));
    backup_duplicate_context(dst, &bak);

    ret = ff_mpv_framesize_alloc(dst->avctx, &dst->sc, dst->linesize);
    if (ret < 0) {
        av_log(dst->avctx, AV_LOG_ERROR, "failed to allocate context "
               "scratch buffers.\n");
        return ret;
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

    s->picture_number        = 0;

    s->f_code                = 1;
    s->b_code                = 1;

    s->slice_context_count   = 1;
}

static void free_buffer_pools(BufferPoolContext *pools)
{
    ff_refstruct_pool_uninit(&pools->mbskip_table_pool);
    ff_refstruct_pool_uninit(&pools->qscale_table_pool);
    ff_refstruct_pool_uninit(&pools->mb_type_pool);
    ff_refstruct_pool_uninit(&pools->motion_val_pool);
    ff_refstruct_pool_uninit(&pools->ref_index_pool);
    pools->alloc_mb_height = pools->alloc_mb_width = pools->alloc_mb_stride = 0;
}

int ff_mpv_init_context_frame(MpegEncContext *s)
{
    BufferPoolContext *const pools = &s->buffer_pools;
    int y_size, c_size, yc_size, i, mb_array_size, mv_table_size, x, y;
    int mb_height;

    if (s->codec_id == AV_CODEC_ID_MPEG2VIDEO && !s->progressive_sequence)
        s->mb_height = (s->height + 31) / 32 * 2;
    else
        s->mb_height = (s->height + 15) / 16;

    /* VC-1 can change from being progressive to interlaced on a per-frame
     * basis. We therefore allocate certain buffers so big that they work
     * in both instances. */
    mb_height = s->msmpeg4_version == MSMP4_VC1 ?
                    FFALIGN(s->mb_height, 2) : s->mb_height;

    s->mb_width   = (s->width + 15) / 16;
    s->mb_stride  = s->mb_width + 1;
    s->b8_stride  = s->mb_width * 2 + 1;
    mb_array_size = mb_height * s->mb_stride;
    mv_table_size = (mb_height + 2) * s->mb_stride + 1;

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

    y_size  = s->b8_stride * (2 * mb_height + 1);
    c_size  = s->mb_stride * (mb_height + 1);
    yc_size = y_size + 2   * c_size;

    if (!FF_ALLOCZ_TYPED_ARRAY(s->mb_index2xy, s->mb_num + 1))
        return AVERROR(ENOMEM);
    for (y = 0; y < s->mb_height; y++)
        for (x = 0; x < s->mb_width; x++)
            s->mb_index2xy[x + y * s->mb_width] = x + y * s->mb_stride;

    s->mb_index2xy[s->mb_height * s->mb_width] = (s->mb_height - 1) * s->mb_stride + s->mb_width; // FIXME really needed?

#define ALLOC_POOL(name, size, flags) do { \
    pools->name ##_pool = ff_refstruct_pool_alloc((size), (flags)); \
    if (!pools->name ##_pool) \
        return AVERROR(ENOMEM); \
} while (0)

    if (s->codec_id == AV_CODEC_ID_MPEG4 ||
        (s->avctx->flags & AV_CODEC_FLAG_INTERLACED_ME)) {
        /* interlaced direct mode decoding tables */
        int16_t (*tmp)[2] = av_calloc(mv_table_size, 4 * sizeof(*tmp));
        if (!tmp)
            return AVERROR(ENOMEM);
        s->p_field_mv_table_base = tmp;
        tmp += s->mb_stride + 1;
        for (int i = 0; i < 2; i++) {
            for (int j = 0; j < 2; j++) {
                s->p_field_mv_table[i][j] = tmp;
                tmp += mv_table_size;
            }
        }
        if (s->codec_id == AV_CODEC_ID_MPEG4) {
            ALLOC_POOL(mbskip_table, mb_array_size + 2,
                       !s->encoding ? FF_REFSTRUCT_POOL_FLAG_ZERO_EVERY_TIME : 0);
            if (!s->encoding) {
                /* cbp, pred_dir */
                if (!(s->cbp_table      = av_mallocz(mb_array_size)) ||
                    !(s->pred_dir_table = av_mallocz(mb_array_size)))
                    return AVERROR(ENOMEM);
            }
        }
    }

    if (s->msmpeg4_version >= MSMP4_V3) {
        s->coded_block_base = av_mallocz(y_size);
        if (!s->coded_block_base)
            return AVERROR(ENOMEM);
        s->coded_block = s->coded_block_base + s->b8_stride + 1;
    }

    if (s->h263_pred || s->h263_plus || !s->encoding) {
        /* dc values */
        // MN: we need these for error resilience of intra-frames
        if (!FF_ALLOCZ_TYPED_ARRAY(s->dc_val_base, yc_size))
            return AVERROR(ENOMEM);
        s->dc_val[0] = s->dc_val_base + s->b8_stride + 1;
        s->dc_val[1] = s->dc_val_base + y_size + s->mb_stride + 1;
        s->dc_val[2] = s->dc_val[1] + c_size;
        for (i = 0; i < yc_size; i++)
            s->dc_val_base[i] = 1024;
    }

    // Note the + 1 is for a quicker MPEG-4 slice_end detection
    if (!(s->mbskip_table  = av_mallocz(mb_array_size + 2)) ||
        /* which mb is an intra block,  init macroblock skip table */
        !(s->mbintra_table = av_malloc(mb_array_size)))
        return AVERROR(ENOMEM);
    memset(s->mbintra_table, 1, mb_array_size);

    ALLOC_POOL(qscale_table, mv_table_size, 0);
    ALLOC_POOL(mb_type, mv_table_size * sizeof(uint32_t), 0);

    if (s->out_format == FMT_H263 || s->encoding ||
        (s->avctx->export_side_data & AV_CODEC_EXPORT_DATA_MVS)) {
        const int b8_array_size = s->b8_stride * mb_height * 2;
        int mv_size        = 2 * (b8_array_size + 4) * sizeof(int16_t);
        int ref_index_size = 4 * mb_array_size;

        /* FIXME: The output of H.263 with OBMC depends upon
         * the earlier content of the buffer; therefore we set
         * the flags to always reset returned buffers here. */
        ALLOC_POOL(motion_val, mv_size, FF_REFSTRUCT_POOL_FLAG_ZERO_EVERY_TIME);
        ALLOC_POOL(ref_index, ref_index_size, 0);
    }
#undef ALLOC_POOL
    pools->alloc_mb_width  = s->mb_width;
    pools->alloc_mb_height = mb_height;
    pools->alloc_mb_stride = s->mb_stride;

    return !CONFIG_MPEGVIDEODEC || s->encoding ? 0 : ff_mpeg_er_init(s);
}

static void clear_context(MpegEncContext *s)
{
    memset(&s->buffer_pools, 0, sizeof(s->buffer_pools));
    memset(&s->next_pic, 0, sizeof(s->next_pic));
    memset(&s->last_pic, 0, sizeof(s->last_pic));
    memset(&s->cur_pic,  0, sizeof(s->cur_pic));

    memset(s->thread_context, 0, sizeof(s->thread_context));

    s->me.map = NULL;
    s->me.score_map = NULL;
    s->dct_error_sum = NULL;
    s->block = NULL;
    s->blocks = NULL;
    s->ac_val_base = NULL;
    s->ac_val[0] =
    s->ac_val[1] =
    s->ac_val[2] =NULL;
    s->me.scratchpad = NULL;
    s->me.temp = NULL;
    memset(&s->sc, 0, sizeof(s->sc));


    s->bitstream_buffer = NULL;
    s->allocated_bitstream_buffer_size = 0;
    s->p_field_mv_table_base = NULL;
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++)
            s->p_field_mv_table[i][j] = NULL;

    s->dc_val_base = NULL;
    s->coded_block_base = NULL;
    s->mbintra_table = NULL;
    s->cbp_table = NULL;
    s->pred_dir_table = NULL;

    s->mbskip_table = NULL;

    s->er.error_status_table = NULL;
    s->er.er_temp_buffer = NULL;
    s->mb_index2xy = NULL;
}

/**
 * init common structure for both encoder and decoder.
 * this assumes that some variables like width/height are already set
 */
av_cold int ff_mpv_common_init(MpegEncContext *s)
{
    int nb_slices = (HAVE_THREADS &&
                     s->avctx->active_thread_type & FF_THREAD_SLICE) ?
                    s->avctx->thread_count : 1;
    int ret;

    clear_context(s);

    if (s->encoding && s->avctx->slices)
        nb_slices = s->avctx->slices;

    if (s->avctx->pix_fmt == AV_PIX_FMT_NONE) {
        av_log(s->avctx, AV_LOG_ERROR,
               "decoding to AV_PIX_FMT_NONE is not supported.\n");
        return AVERROR(EINVAL);
    }

    if ((s->width || s->height) &&
        av_image_check_size(s->width, s->height, 0, s->avctx))
        return AVERROR(EINVAL);

    dsp_init(s);

    /* set chroma shifts */
    ret = av_pix_fmt_get_chroma_sub_sample(s->avctx->pix_fmt,
                                           &s->chroma_x_shift,
                                           &s->chroma_y_shift);
    if (ret)
        return ret;

    if ((ret = ff_mpv_init_context_frame(s)))
        goto fail;

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

    s->context_initialized = 1;
    memset(s->thread_context, 0, sizeof(s->thread_context));
    s->thread_context[0]   = s;
    s->slice_context_count = nb_slices;

//     if (s->width && s->height) {
    ret = ff_mpv_init_duplicate_contexts(s);
    if (ret < 0)
        goto fail;
//     }

    return 0;
 fail:
    ff_mpv_common_end(s);
    return ret;
}

void ff_mpv_free_context_frame(MpegEncContext *s)
{
    free_duplicate_contexts(s);

    free_buffer_pools(&s->buffer_pools);
    av_freep(&s->p_field_mv_table_base);
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++)
            s->p_field_mv_table[i][j] = NULL;

    av_freep(&s->dc_val_base);
    av_freep(&s->coded_block_base);
    av_freep(&s->mbintra_table);
    av_freep(&s->cbp_table);
    av_freep(&s->pred_dir_table);

    av_freep(&s->mbskip_table);

    av_freep(&s->er.error_status_table);
    av_freep(&s->er.er_temp_buffer);
    av_freep(&s->mb_index2xy);

    s->linesize = s->uvlinesize = 0;
}

void ff_mpv_common_end(MpegEncContext *s)
{
    ff_mpv_free_context_frame(s);
    if (s->slice_context_count > 1)
        s->slice_context_count = 1;

    av_freep(&s->bitstream_buffer);
    s->allocated_bitstream_buffer_size = 0;

    ff_mpv_unref_picture(&s->last_pic);
    ff_mpv_unref_picture(&s->cur_pic);
    ff_mpv_unref_picture(&s->next_pic);

    s->context_initialized      = 0;
    s->context_reinit           = 0;
    s->linesize = s->uvlinesize = 0;
}


/**
 * Clean dc, ac for the current non-intra MB.
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

void ff_init_block_index(MpegEncContext *s){ //FIXME maybe rename
    const int linesize   = s->cur_pic.linesize[0]; //not s->linesize as this would be wrong for field pics
    const int uvlinesize = s->cur_pic.linesize[1];
    const int width_of_mb = (4 + (s->avctx->bits_per_raw_sample > 8)) - s->avctx->lowres;
    const int height_of_mb = 4 - s->avctx->lowres;

    s->block_index[0]= s->b8_stride*(s->mb_y*2    ) - 2 + s->mb_x*2;
    s->block_index[1]= s->b8_stride*(s->mb_y*2    ) - 1 + s->mb_x*2;
    s->block_index[2]= s->b8_stride*(s->mb_y*2 + 1) - 2 + s->mb_x*2;
    s->block_index[3]= s->b8_stride*(s->mb_y*2 + 1) - 1 + s->mb_x*2;
    s->block_index[4]= s->mb_stride*(s->mb_y + 1)                + s->b8_stride*s->mb_height*2 + s->mb_x - 1;
    s->block_index[5]= s->mb_stride*(s->mb_y + s->mb_height + 2) + s->b8_stride*s->mb_height*2 + s->mb_x - 1;
    //block_index is not used by mpeg2, so it is not affected by chroma_format

    s->dest[0] = s->cur_pic.data[0] + (int)((s->mb_x - 1U) <<  width_of_mb);
    s->dest[1] = s->cur_pic.data[1] + (int)((s->mb_x - 1U) << (width_of_mb - s->chroma_x_shift));
    s->dest[2] = s->cur_pic.data[2] + (int)((s->mb_x - 1U) << (width_of_mb - s->chroma_x_shift));

    if (s->picture_structure == PICT_FRAME) {
        s->dest[0] += s->mb_y *   linesize << height_of_mb;
        s->dest[1] += s->mb_y * uvlinesize << (height_of_mb - s->chroma_y_shift);
        s->dest[2] += s->mb_y * uvlinesize << (height_of_mb - s->chroma_y_shift);
    } else {
        s->dest[0] += (s->mb_y>>1) *   linesize << height_of_mb;
        s->dest[1] += (s->mb_y>>1) * uvlinesize << (height_of_mb - s->chroma_y_shift);
        s->dest[2] += (s->mb_y>>1) * uvlinesize << (height_of_mb - s->chroma_y_shift);
        av_assert1((s->mb_y&1) == (s->picture_structure == PICT_BOTTOM_FIELD));
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
