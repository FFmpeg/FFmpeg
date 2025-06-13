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

/*
 * non linear quantizers with large QPs and VBV with restrictive qmin fixes sponsored by NOA GmbH
 */

/**
 * @file
 * The simplest mpeg encoder (well, it was the simplest!).
 */

#include "config_components.h"

#include <assert.h>
#include <stdint.h>

#include "libavutil/emms.h"
#include "libavutil/internal.h"
#include "libavutil/intmath.h"
#include "libavutil/mathematics.h"
#include "libavutil/mem.h"
#include "libavutil/mem_internal.h"
#include "libavutil/opt.h"
#include "libavutil/thread.h"
#include "avcodec.h"
#include "encode.h"
#include "idctdsp.h"
#include "mpeg12codecs.h"
#include "mpeg12data.h"
#include "mpeg12enc.h"
#include "mpegvideo.h"
#include "mpegvideodata.h"
#include "mpegvideoenc.h"
#include "h261enc.h"
#include "h263.h"
#include "h263data.h"
#include "h263enc.h"
#include "mjpegenc_common.h"
#include "mathops.h"
#include "mpegutils.h"
#include "mpegvideo_unquantize.h"
#include "mjpegenc.h"
#include "speedhqenc.h"
#include "msmpeg4enc.h"
#include "pixblockdsp.h"
#include "qpeldsp.h"
#include "faandct.h"
#include "aandcttab.h"
#include "mpeg4video.h"
#include "mpeg4videodata.h"
#include "mpeg4videoenc.h"
#include "internal.h"
#include "bytestream.h"
#include "rv10enc.h"
#include "packet_internal.h"
#include "libavutil/refstruct.h"
#include <limits.h>
#include "sp5x.h"

#define QUANT_BIAS_SHIFT 8

#define QMAT_SHIFT_MMX 16
#define QMAT_SHIFT 21

static int encode_picture(MPVMainEncContext *const s, const AVPacket *pkt);
static int dct_quantize_refine(MPVEncContext *const s, int16_t *block, int16_t *weight, int16_t *orig, int n, int qscale);
static int sse_mb(MPVEncContext *const s);
static void denoise_dct_c(MPVEncContext *const s, int16_t *block);
static int dct_quantize_c(MPVEncContext *const s,
                          int16_t *block, int n,
                          int qscale, int *overflow);
static int dct_quantize_trellis_c(MPVEncContext *const s, int16_t *block, int n, int qscale, int *overflow);

static uint8_t default_fcode_tab[MAX_MV * 2 + 1];

static const AVOption mpv_generic_options[] = {
    FF_MPV_COMMON_OPTS
    FF_MPV_COMMON_MOTION_EST_OPTS
    { NULL },
};

const AVClass ff_mpv_enc_class = {
    .class_name = "generic mpegvideo encoder",
    .item_name  = av_default_item_name,
    .option     = mpv_generic_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

void ff_convert_matrix(MPVEncContext *const s, int (*qmat)[64],
                       uint16_t (*qmat16)[2][64],
                       const uint16_t *quant_matrix,
                       int bias, int qmin, int qmax, int intra)
{
    FDCTDSPContext *fdsp = &s->fdsp;
    int qscale;
    int shift = 0;

    for (qscale = qmin; qscale <= qmax; qscale++) {
        int i;
        int qscale2;

        if (s->c.q_scale_type) qscale2 = ff_mpeg2_non_linear_qscale[qscale];
        else                 qscale2 = qscale << 1;

        if (fdsp->fdct == ff_jpeg_fdct_islow_8  ||
#if CONFIG_FAANDCT
            fdsp->fdct == ff_faandct            ||
#endif /* CONFIG_FAANDCT */
            fdsp->fdct == ff_jpeg_fdct_islow_10) {
            for (i = 0; i < 64; i++) {
                const int j = s->c.idsp.idct_permutation[i];
                int64_t den = (int64_t) qscale2 * quant_matrix[j];
                /* 1 * 1 <= qscale2 * quant_matrix[j] <= 112 * 255
                 * Assume x = qscale2 * quant_matrix[j]
                 *                 1 <=              x  <= 28560
                 *     (1 << 22) / 1 >= (1 << 22) / (x) >= (1 << 22) / 28560
                 *           4194304 >= (1 << 22) / (x) >= 146 */

                qmat[qscale][i] = (int)((UINT64_C(2) << QMAT_SHIFT) / den);
            }
        } else if (fdsp->fdct == ff_fdct_ifast) {
            for (i = 0; i < 64; i++) {
                const int j = s->c.idsp.idct_permutation[i];
                int64_t den = ff_aanscales[i] * (int64_t) qscale2 * quant_matrix[j];
                /* 1247 * 1 * 1 <= ff_aanscales[i] * qscale2 * quant_matrix[j] <= 31521 * 112 * 255
                 * Assume x = ff_aanscales[i] * qscale2 * quant_matrix[j]
                 *              1247 <=              x  <= 900239760
                 *  (1 << 36) / 1247 >= (1 << 36) / (x) >= (1 << 36) / 900239760
                 *          55107840 >= (1 << 36) / (x) >= 76 */

                qmat[qscale][i] = (int)((UINT64_C(2) << (QMAT_SHIFT + 14)) / den);
            }
        } else {
            for (i = 0; i < 64; i++) {
                const int j = s->c.idsp.idct_permutation[i];
                int64_t den = (int64_t) qscale2 * quant_matrix[j];
                /* 1 * 1 <= qscale2 * quant_matrix[j] <= 112 * 255
                 * Assume x = qscale2 * quant_matrix[j]
                 *                 1 <=              x  <= 28560
                 *     (1 << 22) / 1 >= (1 << 22) / (x) >= (1 << 22) / 28560
                 *           4194304 >= (1 << 22) / (x) >= 146
                 *
                 *                 1 <=              x  <= 28560
                 *     (1 << 17) / 1 >= (1 << 17) / (x) >= (1 << 17) / 28560
                 *            131072 >= (1 << 17) / (x) >= 4 */

                qmat[qscale][i] = (int)((UINT64_C(2) << QMAT_SHIFT) / den);
                qmat16[qscale][0][i] = (2 << QMAT_SHIFT_MMX) / den;

                if (qmat16[qscale][0][i] == 0 ||
                    qmat16[qscale][0][i] == 128 * 256)
                    qmat16[qscale][0][i] = 128 * 256 - 1;
                qmat16[qscale][1][i] =
                    ROUNDED_DIV(bias * (1<<(16 - QUANT_BIAS_SHIFT)),
                                qmat16[qscale][0][i]);
            }
        }

        for (i = intra; i < 64; i++) {
            int64_t max = 8191;
            if (fdsp->fdct == ff_fdct_ifast) {
                max = (8191LL * ff_aanscales[i]) >> 14;
            }
            while (((max * qmat[qscale][i]) >> shift) > INT_MAX) {
                shift++;
            }
        }
    }
    if (shift) {
        av_log(s->c.avctx, AV_LOG_INFO,
               "Warning, QMAT_SHIFT is larger than %d, overflows possible\n",
               QMAT_SHIFT - shift);
    }
}

static inline void update_qscale(MPVMainEncContext *const m)
{
    MPVEncContext *const s = &m->s;

    if (s->c.q_scale_type == 1 && 0) {
        int i;
        int bestdiff=INT_MAX;
        int best = 1;

        for (i = 0 ; i<FF_ARRAY_ELEMS(ff_mpeg2_non_linear_qscale); i++) {
            int diff = FFABS((ff_mpeg2_non_linear_qscale[i]<<(FF_LAMBDA_SHIFT + 6)) - (int)s->lambda * 139);
            if (ff_mpeg2_non_linear_qscale[i] < s->c.avctx->qmin ||
                (ff_mpeg2_non_linear_qscale[i] > s->c.avctx->qmax && !m->vbv_ignore_qmax))
                continue;
            if (diff < bestdiff) {
                bestdiff = diff;
                best = i;
            }
        }
        s->c.qscale = best;
    } else {
        s->c.qscale = (s->lambda * 139 + FF_LAMBDA_SCALE * 64) >>
                    (FF_LAMBDA_SHIFT + 7);
        s->c.qscale = av_clip(s->c.qscale, s->c.avctx->qmin, m->vbv_ignore_qmax ? 31 : s->c.avctx->qmax);
    }

    s->lambda2 = (s->lambda * s->lambda + FF_LAMBDA_SCALE / 2) >>
                 FF_LAMBDA_SHIFT;
}

void ff_write_quant_matrix(PutBitContext *pb, uint16_t *matrix)
{
    int i;

    if (matrix) {
        put_bits(pb, 1, 1);
        for (i = 0; i < 64; i++) {
            put_bits(pb, 8, matrix[ff_zigzag_direct[i]]);
        }
    } else
        put_bits(pb, 1, 0);
}

/**
 * init s->c.cur_pic.qscale_table from s->lambda_table
 */
static void init_qscale_tab(MPVEncContext *const s)
{
    int8_t *const qscale_table = s->c.cur_pic.qscale_table;

    for (int i = 0; i < s->c.mb_num; i++) {
        unsigned int lam = s->lambda_table[s->c.mb_index2xy[i]];
        int qp = (lam * 139 + FF_LAMBDA_SCALE * 64) >> (FF_LAMBDA_SHIFT + 7);
        qscale_table[s->c.mb_index2xy[i]] = av_clip(qp, s->c.avctx->qmin,
                                                  s->c.avctx->qmax);
    }
}

static void update_duplicate_context_after_me(MPVEncContext *const dst,
                                              const MPVEncContext *const src)
{
#define COPY(a) dst->a = src->a
    COPY(c.pict_type);
    COPY(f_code);
    COPY(b_code);
    COPY(c.qscale);
    COPY(lambda);
    COPY(lambda2);
    COPY(c.frame_pred_frame_dct); // FIXME don't set in encode_header
    COPY(c.progressive_frame);    // FIXME don't set in encode_header
    COPY(c.partitioned_frame);    // FIXME don't set in encode_header
#undef COPY
}

static av_cold void mpv_encode_init_static(void)
{
   for (int i = -16; i < 16; i++)
        default_fcode_tab[i + MAX_MV] = 1;
}

/**
 * Set the given MPVEncContext to defaults for encoding.
 */
static av_cold void mpv_encode_defaults(MPVMainEncContext *const m)
{
    MPVEncContext *const s = &m->s;
    static AVOnce init_static_once = AV_ONCE_INIT;

    ff_mpv_common_defaults(&s->c);

    s->f_code = 1;
    s->b_code = 1;

    if (!m->fcode_tab) {
        m->fcode_tab = default_fcode_tab + MAX_MV;
        ff_thread_once(&init_static_once, mpv_encode_init_static);
    }
    if (!s->c.y_dc_scale_table) {
        s->c.y_dc_scale_table =
        s->c.c_dc_scale_table = ff_mpeg1_dc_scale_table;
    }
}

av_cold void ff_dct_encode_init(MPVEncContext *const s)
{
    s->dct_quantize = dct_quantize_c;
    s->denoise_dct  = denoise_dct_c;

#if ARCH_MIPS
    ff_mpvenc_dct_init_mips(s);
#elif ARCH_X86
    ff_dct_encode_init_x86(s);
#endif

    if (s->c.avctx->trellis)
        s->dct_quantize  = dct_quantize_trellis_c;
}

static av_cold void init_unquantize(MPVEncContext *const s2, AVCodecContext *avctx)
{
    MpegEncContext *const s = &s2->c;
    MPVUnquantDSPContext unquant_dsp_ctx;

    ff_mpv_unquantize_init(&unquant_dsp_ctx,
                           avctx->flags & AV_CODEC_FLAG_BITEXACT, s->q_scale_type);

    if (s2->mpeg_quant || s->codec_id == AV_CODEC_ID_MPEG2VIDEO) {
        s->dct_unquantize_intra = unquant_dsp_ctx.dct_unquantize_mpeg2_intra;
        s->dct_unquantize_inter = unquant_dsp_ctx.dct_unquantize_mpeg2_inter;
    } else if (s->out_format == FMT_H263 || s->out_format == FMT_H261) {
        s->dct_unquantize_intra = unquant_dsp_ctx.dct_unquantize_h263_intra;
        s->dct_unquantize_inter = unquant_dsp_ctx.dct_unquantize_h263_inter;
    } else {
        s->dct_unquantize_intra = unquant_dsp_ctx.dct_unquantize_mpeg1_intra;
        s->dct_unquantize_inter = unquant_dsp_ctx.dct_unquantize_mpeg1_inter;
    }
}

static av_cold int me_cmp_init(MPVMainEncContext *const m, AVCodecContext *avctx)
{
    MPVEncContext *const s = &m->s;
    MECmpContext mecc;
    me_cmp_func me_cmp[6];
    int ret;

    ff_me_cmp_init(&mecc, avctx);
    ret = ff_me_init(&s->me, avctx, &mecc, 1);
    if (ret < 0)
        return ret;
    ret = ff_set_cmp(&mecc, me_cmp, m->frame_skip_cmp, 1);
    if (ret < 0)
        return ret;
    m->frame_skip_cmp_fn = me_cmp[1];
    if (avctx->flags & AV_CODEC_FLAG_INTERLACED_DCT) {
        ret = ff_set_cmp(&mecc, me_cmp, avctx->ildct_cmp, 1);
        if (ret < 0)
            return ret;
        if (!me_cmp[0] || !me_cmp[4])
            return AVERROR(EINVAL);
        s->ildct_cmp[0] = me_cmp[0];
        s->ildct_cmp[1] = me_cmp[4];
    }

    s->sum_abs_dctelem = mecc.sum_abs_dctelem;

    s->sse_cmp[0] = mecc.sse[0];
    s->sse_cmp[1] = mecc.sse[1];
    s->sad_cmp[0] = mecc.sad[0];
    s->sad_cmp[1] = mecc.sad[1];
    if (avctx->mb_cmp == FF_CMP_NSSE) {
        s->n_sse_cmp[0] = mecc.nsse[0];
        s->n_sse_cmp[1] = mecc.nsse[1];
    } else {
        s->n_sse_cmp[0] = mecc.sse[0];
        s->n_sse_cmp[1] = mecc.sse[1];
    }

    return 0;
}

#define ALLOCZ_ARRAYS(p, mult, numb) ((p) = av_calloc(numb, mult * sizeof(*(p))))
static av_cold int init_matrices(MPVMainEncContext *const m, AVCodecContext *avctx)
{
    MPVEncContext *const s = &m->s;
    const int nb_matrices = 1 + (s->c.out_format == FMT_MJPEG) + !m->intra_only;
    const uint16_t *intra_matrix, *inter_matrix;
    int ret;

    if (!ALLOCZ_ARRAYS(s->q_intra_matrix,   32, nb_matrices) ||
        !ALLOCZ_ARRAYS(s->q_intra_matrix16, 32, nb_matrices))
        return AVERROR(ENOMEM);

    if (s->c.out_format == FMT_MJPEG) {
        s->q_chroma_intra_matrix   = s->q_intra_matrix   + 32;
        s->q_chroma_intra_matrix16 = s->q_intra_matrix16 + 32;
        // No need to set q_inter_matrix
        av_assert1(m->intra_only);
        // intra_matrix, chroma_intra_matrix will be set later for MJPEG.
        return 0;
    } else {
        s->q_chroma_intra_matrix   = s->q_intra_matrix;
        s->q_chroma_intra_matrix16 = s->q_intra_matrix16;
    }
    if (!m->intra_only) {
        s->q_inter_matrix   = s->q_intra_matrix   + 32;
        s->q_inter_matrix16 = s->q_intra_matrix16 + 32;
    }

    if (CONFIG_MPEG4_ENCODER && s->c.codec_id == AV_CODEC_ID_MPEG4 &&
        s->mpeg_quant) {
        intra_matrix = ff_mpeg4_default_intra_matrix;
        inter_matrix = ff_mpeg4_default_non_intra_matrix;
    } else if (s->c.out_format == FMT_H263 || s->c.out_format == FMT_H261) {
        intra_matrix =
        inter_matrix = ff_mpeg1_default_non_intra_matrix;
    } else {
        /* MPEG-1/2, SpeedHQ */
        intra_matrix = ff_mpeg1_default_intra_matrix;
        inter_matrix = ff_mpeg1_default_non_intra_matrix;
    }
    if (avctx->intra_matrix)
        intra_matrix = avctx->intra_matrix;
    if (avctx->inter_matrix)
        inter_matrix = avctx->inter_matrix;

    /* init q matrix */
    for (int i = 0; i < 64; i++) {
        int j = s->c.idsp.idct_permutation[i];

        s->c.intra_matrix[j] = s->c.chroma_intra_matrix[j] = intra_matrix[i];
        s->c.inter_matrix[j] = inter_matrix[i];
    }

    /* precompute matrix */
    ret = ff_check_codec_matrices(avctx, FF_MATRIX_TYPE_INTRA | FF_MATRIX_TYPE_INTER, 1, 255);
    if (ret < 0)
        return ret;

    ff_convert_matrix(s, s->q_intra_matrix, s->q_intra_matrix16,
                      s->c.intra_matrix, s->intra_quant_bias, avctx->qmin,
                      31, 1);
    if (s->q_inter_matrix)
        ff_convert_matrix(s, s->q_inter_matrix, s->q_inter_matrix16,
                          s->c.inter_matrix, s->inter_quant_bias, avctx->qmin,
                          31, 0);

    return 0;
}

static av_cold int init_buffers(MPVMainEncContext *const m)
{
    MPVEncContext *const s = &m->s;
    int has_b_frames = !!m->max_b_frames;
    int16_t (*mv_table)[2];

    /* Allocate MB type table */
    unsigned mb_array_size = s->c.mb_stride * s->c.mb_height;
    s->mb_type = av_calloc(mb_array_size, 3 * sizeof(*s->mb_type) + sizeof(*s->mb_mean));
    if (!s->mb_type)
        return AVERROR(ENOMEM);
    s->mc_mb_var = s->mb_type   + mb_array_size;
    s->mb_var    = s->mc_mb_var + mb_array_size;
    s->mb_mean   = (uint8_t*)(s->mb_var + mb_array_size);

    if (!FF_ALLOCZ_TYPED_ARRAY(s->lambda_table, mb_array_size))
        return AVERROR(ENOMEM);

    unsigned mv_table_size = (s->c.mb_height + 2) * s->c.mb_stride + 1;
    unsigned nb_mv_tables = 1 + 5 * has_b_frames;
    if (s->c.codec_id == AV_CODEC_ID_MPEG4 ||
        (s->c.avctx->flags & AV_CODEC_FLAG_INTERLACED_ME)) {
        nb_mv_tables += 8 * has_b_frames;
        s->p_field_select_table[0] = av_calloc(mv_table_size, 2 * (2 + 4 * has_b_frames));
        if (!s->p_field_select_table[0])
            return AVERROR(ENOMEM);
        s->p_field_select_table[1] = s->p_field_select_table[0] + 2 * mv_table_size;
    }

    mv_table = av_calloc(mv_table_size, nb_mv_tables * sizeof(*mv_table));
    if (!mv_table)
        return AVERROR(ENOMEM);
    m->mv_table_base = mv_table;
    mv_table += s->c.mb_stride + 1;

    s->p_mv_table = mv_table;
    if (has_b_frames) {
        s->b_forw_mv_table       = mv_table += mv_table_size;
        s->b_back_mv_table       = mv_table += mv_table_size;
        s->b_bidir_forw_mv_table = mv_table += mv_table_size;
        s->b_bidir_back_mv_table = mv_table += mv_table_size;
        s->b_direct_mv_table     = mv_table += mv_table_size;

        if (s->p_field_select_table[1]) { // MPEG-4 or INTERLACED_ME above
            uint8_t *field_select = s->p_field_select_table[1];
            for (int j = 0; j < 2; j++) {
                for (int k = 0; k < 2; k++) {
                    for (int l = 0; l < 2; l++)
                        s->b_field_mv_table[j][k][l] = mv_table += mv_table_size;
                    s->b_field_select_table[j][k] = field_select += 2 * mv_table_size;
                }
            }
        }
    }

    return 0;
}

static av_cold int init_slice_buffers(MPVMainEncContext *const m)
{
    MPVEncContext *const s = &m->s;
    // Align the following per-thread buffers to avoid false sharing.
    enum {
#ifndef _MSC_VER
        /// The number is supposed to match/exceed the cache-line size.
        ALIGN = FFMAX(128, _Alignof(max_align_t)),
#else
        ALIGN = 128,
#endif
        DCT_ERROR_SIZE    = FFALIGN(2 * sizeof(*s->dct_error_sum), ALIGN),
    };
    static_assert(DCT_ERROR_SIZE * MAX_THREADS + ALIGN - 1 <= SIZE_MAX,
                  "Need checks for potential overflow.");
    unsigned nb_slices = s->c.slice_context_count;
    char *dct_error = NULL;

    if (m->noise_reduction) {
        if (!FF_ALLOCZ_TYPED_ARRAY(s->dct_offset, 2))
            return AVERROR(ENOMEM);
        dct_error = av_mallocz(ALIGN - 1 + nb_slices * DCT_ERROR_SIZE);
        if (!dct_error)
            return AVERROR(ENOMEM);
        m->dct_error_sum_base = dct_error;
        dct_error += FFALIGN((uintptr_t)dct_error, ALIGN) - (uintptr_t)dct_error;
    }

    const int y_size  = s->c.b8_stride * (2 * s->c.mb_height + 1);
    const int c_size  = s->c.mb_stride * (s->c.mb_height + 1);
    const int yc_size = y_size + 2 * c_size;
    ptrdiff_t offset = 0;

    for (unsigned i = 0; i < nb_slices; ++i) {
        MPVEncContext *const s2 = s->c.enc_contexts[i];

        if (dct_error) {
            s2->dct_offset    = s->dct_offset;
            s2->dct_error_sum = (void*)dct_error;
            dct_error        += DCT_ERROR_SIZE;
        }

        if (s2->c.ac_val) {
            s2->c.dc_val += offset + i;
            s2->c.ac_val += offset;
            offset       += yc_size;
        }
    }
    return 0;
}

/* init video encoder */
av_cold int ff_mpv_encode_init(AVCodecContext *avctx)
{
    MPVMainEncContext *const m = avctx->priv_data;
    MPVEncContext    *const s = &m->s;
    AVCPBProperties *cpb_props;
    int gcd, ret;

    mpv_encode_defaults(m);

    switch (avctx->pix_fmt) {
    case AV_PIX_FMT_YUVJ444P:
    case AV_PIX_FMT_YUV444P:
        s->c.chroma_format = CHROMA_444;
        break;
    case AV_PIX_FMT_YUVJ422P:
    case AV_PIX_FMT_YUV422P:
        s->c.chroma_format = CHROMA_422;
        break;
    default:
        av_unreachable("Already checked via CODEC_PIXFMTS");
    case AV_PIX_FMT_YUVJ420P:
    case AV_PIX_FMT_YUV420P:
        s->c.chroma_format = CHROMA_420;
        break;
    }

    avctx->bits_per_raw_sample = av_clip(avctx->bits_per_raw_sample, 0, 8);

    m->bit_rate = avctx->bit_rate;
    s->c.width  = avctx->width;
    s->c.height = avctx->height;
    if (avctx->gop_size > 600 &&
        avctx->strict_std_compliance > FF_COMPLIANCE_EXPERIMENTAL) {
        av_log(avctx, AV_LOG_WARNING,
               "keyframe interval too large!, reducing it from %d to %d\n",
               avctx->gop_size, 600);
        avctx->gop_size = 600;
    }
    m->gop_size     = avctx->gop_size;
    s->c.avctx      = avctx;
    if (avctx->max_b_frames > MPVENC_MAX_B_FRAMES) {
        av_log(avctx, AV_LOG_ERROR, "Too many B-frames requested, maximum "
               "is " AV_STRINGIFY(MPVENC_MAX_B_FRAMES) ".\n");
        avctx->max_b_frames = MPVENC_MAX_B_FRAMES;
    } else if (avctx->max_b_frames < 0) {
        av_log(avctx, AV_LOG_ERROR,
               "max b frames must be 0 or positive for mpegvideo based encoders\n");
        return AVERROR(EINVAL);
    }
    m->max_b_frames = avctx->max_b_frames;
    s->c.codec_id   = avctx->codec->id;
    if (m->max_b_frames && !(avctx->codec->capabilities & AV_CODEC_CAP_DELAY)) {
        av_log(avctx, AV_LOG_ERROR, "B-frames not supported by codec\n");
        return AVERROR(EINVAL);
    }

    s->c.quarter_sample     = (avctx->flags & AV_CODEC_FLAG_QPEL) != 0;
    s->rtp_mode           = !!s->rtp_payload_size;
    s->c.intra_dc_precision = avctx->intra_dc_precision;

    // workaround some differences between how applications specify dc precision
    if (s->c.intra_dc_precision < 0) {
        s->c.intra_dc_precision += 8;
    } else if (s->c.intra_dc_precision >= 8)
        s->c.intra_dc_precision -= 8;

    if (s->c.intra_dc_precision < 0) {
        av_log(avctx, AV_LOG_ERROR,
                "intra dc precision must be positive, note some applications use"
                " 0 and some 8 as base meaning 8bit, the value must not be smaller than that\n");
        return AVERROR(EINVAL);
    }

    if (s->c.intra_dc_precision > (avctx->codec_id == AV_CODEC_ID_MPEG2VIDEO ? 3 : 0)) {
        av_log(avctx, AV_LOG_ERROR, "intra dc precision too large\n");
        return AVERROR(EINVAL);
    }
    m->user_specified_pts = AV_NOPTS_VALUE;

    if (m->gop_size <= 1) {
        m->intra_only = 1;
        m->gop_size   = 12;
    } else {
        m->intra_only = 0;
    }

    /* Fixed QSCALE */
    m->fixed_qscale = !!(avctx->flags & AV_CODEC_FLAG_QSCALE);

    s->adaptive_quant = (avctx->lumi_masking ||
                         avctx->dark_masking ||
                         avctx->temporal_cplx_masking ||
                         avctx->spatial_cplx_masking  ||
                         avctx->p_masking      ||
                         m->border_masking ||
                         (s->mpv_flags & FF_MPV_FLAG_QP_RD)) &&
                        !m->fixed_qscale;

    s->c.loop_filter = !!(avctx->flags & AV_CODEC_FLAG_LOOP_FILTER);

    if (avctx->rc_max_rate && !avctx->rc_buffer_size) {
        switch(avctx->codec_id) {
        case AV_CODEC_ID_MPEG1VIDEO:
        case AV_CODEC_ID_MPEG2VIDEO:
            avctx->rc_buffer_size = FFMAX(avctx->rc_max_rate, 15000000) * 112LL / 15000000 * 16384;
            break;
        case AV_CODEC_ID_MPEG4:
        case AV_CODEC_ID_MSMPEG4V1:
        case AV_CODEC_ID_MSMPEG4V2:
        case AV_CODEC_ID_MSMPEG4V3:
            if       (avctx->rc_max_rate >= 15000000) {
                avctx->rc_buffer_size = 320 + (avctx->rc_max_rate - 15000000LL) * (760-320) / (38400000 - 15000000);
            } else if(avctx->rc_max_rate >=  2000000) {
                avctx->rc_buffer_size =  80 + (avctx->rc_max_rate -  2000000LL) * (320- 80) / (15000000 -  2000000);
            } else if(avctx->rc_max_rate >=   384000) {
                avctx->rc_buffer_size =  40 + (avctx->rc_max_rate -   384000LL) * ( 80- 40) / ( 2000000 -   384000);
            } else
                avctx->rc_buffer_size = 40;
            avctx->rc_buffer_size *= 16384;
            break;
        }
        if (avctx->rc_buffer_size) {
            av_log(avctx, AV_LOG_INFO, "Automatically choosing VBV buffer size of %d kbyte\n", avctx->rc_buffer_size/8192);
        }
    }

    if ((!avctx->rc_max_rate) != (!avctx->rc_buffer_size)) {
        av_log(avctx, AV_LOG_ERROR, "Either both buffer size and max rate or neither must be specified\n");
        return AVERROR(EINVAL);
    }

    if (avctx->rc_min_rate && avctx->rc_max_rate != avctx->rc_min_rate) {
        av_log(avctx, AV_LOG_INFO,
               "Warning min_rate > 0 but min_rate != max_rate isn't recommended!\n");
    }

    if (avctx->rc_min_rate && avctx->rc_min_rate > avctx->bit_rate) {
        av_log(avctx, AV_LOG_ERROR, "bitrate below min bitrate\n");
        return AVERROR(EINVAL);
    }

    if (avctx->rc_max_rate && avctx->rc_max_rate < avctx->bit_rate) {
        av_log(avctx, AV_LOG_ERROR, "bitrate above max bitrate\n");
        return AVERROR(EINVAL);
    }

    if (avctx->rc_max_rate &&
        avctx->rc_max_rate == avctx->bit_rate &&
        avctx->rc_max_rate != avctx->rc_min_rate) {
        av_log(avctx, AV_LOG_INFO,
               "impossible bitrate constraints, this will fail\n");
    }

    if (avctx->rc_buffer_size &&
        avctx->bit_rate * (int64_t)avctx->time_base.num >
            avctx->rc_buffer_size * (int64_t)avctx->time_base.den) {
        av_log(avctx, AV_LOG_ERROR, "VBV buffer too small for bitrate\n");
        return AVERROR(EINVAL);
    }

    if (!m->fixed_qscale &&
        avctx->bit_rate * av_q2d(avctx->time_base) >
            avctx->bit_rate_tolerance) {
        double nbt = avctx->bit_rate * av_q2d(avctx->time_base) * 5;
        av_log(avctx, AV_LOG_WARNING,
               "bitrate tolerance %d too small for bitrate %"PRId64", overriding\n", avctx->bit_rate_tolerance, avctx->bit_rate);
        if (nbt <= INT_MAX) {
            avctx->bit_rate_tolerance = nbt;
        } else
            avctx->bit_rate_tolerance = INT_MAX;
    }

    if ((avctx->flags & AV_CODEC_FLAG_4MV) && s->c.codec_id != AV_CODEC_ID_MPEG4 &&
        s->c.codec_id != AV_CODEC_ID_H263 && s->c.codec_id != AV_CODEC_ID_H263P &&
        s->c.codec_id != AV_CODEC_ID_FLV1) {
        av_log(avctx, AV_LOG_ERROR, "4MV not supported by codec\n");
        return AVERROR(EINVAL);
    }

    if (s->c.obmc && avctx->mb_decision != FF_MB_DECISION_SIMPLE) {
        av_log(avctx, AV_LOG_ERROR,
               "OBMC is only supported with simple mb decision\n");
        return AVERROR(EINVAL);
    }

    if (s->c.quarter_sample && s->c.codec_id != AV_CODEC_ID_MPEG4) {
        av_log(avctx, AV_LOG_ERROR, "qpel not supported by codec\n");
        return AVERROR(EINVAL);
    }

    if ((s->c.codec_id == AV_CODEC_ID_MPEG4 ||
         s->c.codec_id == AV_CODEC_ID_H263  ||
         s->c.codec_id == AV_CODEC_ID_H263P) &&
        (avctx->sample_aspect_ratio.num > 255 ||
         avctx->sample_aspect_ratio.den > 255)) {
        av_log(avctx, AV_LOG_WARNING,
               "Invalid pixel aspect ratio %i/%i, limit is 255/255 reducing\n",
               avctx->sample_aspect_ratio.num, avctx->sample_aspect_ratio.den);
        av_reduce(&avctx->sample_aspect_ratio.num, &avctx->sample_aspect_ratio.den,
                   avctx->sample_aspect_ratio.num,  avctx->sample_aspect_ratio.den, 255);
    }

    if ((s->c.codec_id == AV_CODEC_ID_H263  ||
         s->c.codec_id == AV_CODEC_ID_H263P) &&
        (avctx->width  > 2048 ||
         avctx->height > 1152 )) {
        av_log(avctx, AV_LOG_ERROR, "H.263 does not support resolutions above 2048x1152\n");
        return AVERROR(EINVAL);
    }
    if (s->c.codec_id == AV_CODEC_ID_FLV1 &&
        (avctx->width  > 65535 ||
         avctx->height > 65535 )) {
        av_log(avctx, AV_LOG_ERROR, "FLV does not support resolutions above 16bit\n");
        return AVERROR(EINVAL);
    }
    if ((s->c.codec_id == AV_CODEC_ID_H263  ||
         s->c.codec_id == AV_CODEC_ID_H263P ||
         s->c.codec_id == AV_CODEC_ID_RV20) &&
        ((avctx->width &3) ||
         (avctx->height&3) )) {
        av_log(avctx, AV_LOG_ERROR, "width and height must be a multiple of 4\n");
        return AVERROR(EINVAL);
    }

    if (s->c.codec_id == AV_CODEC_ID_RV10 &&
        (avctx->width &15 ||
         avctx->height&15 )) {
        av_log(avctx, AV_LOG_ERROR, "width and height must be a multiple of 16\n");
        return AVERROR(EINVAL);
    }

    if ((s->c.codec_id == AV_CODEC_ID_WMV1 ||
         s->c.codec_id == AV_CODEC_ID_WMV2) &&
         avctx->width & 1) {
        av_log(avctx, AV_LOG_ERROR, "width must be multiple of 2\n");
        return AVERROR(EINVAL);
    }

    if ((avctx->flags & (AV_CODEC_FLAG_INTERLACED_DCT | AV_CODEC_FLAG_INTERLACED_ME)) &&
        s->c.codec_id != AV_CODEC_ID_MPEG4 && s->c.codec_id != AV_CODEC_ID_MPEG2VIDEO) {
        av_log(avctx, AV_LOG_ERROR, "interlacing not supported by codec\n");
        return AVERROR(EINVAL);
    }

    if ((s->mpv_flags & FF_MPV_FLAG_CBP_RD) && !avctx->trellis) {
        av_log(avctx, AV_LOG_ERROR, "CBP RD needs trellis quant\n");
        return AVERROR(EINVAL);
    }

    if ((s->mpv_flags & FF_MPV_FLAG_QP_RD) &&
        avctx->mb_decision != FF_MB_DECISION_RD) {
        av_log(avctx, AV_LOG_ERROR, "QP RD needs mbd=rd\n");
        return AVERROR(EINVAL);
    }

    if (m->scenechange_threshold < 1000000000 &&
        (avctx->flags & AV_CODEC_FLAG_CLOSED_GOP)) {
        av_log(avctx, AV_LOG_ERROR,
               "closed gop with scene change detection are not supported yet, "
               "set threshold to 1000000000\n");
        return AVERROR_PATCHWELCOME;
    }

    if (avctx->flags & AV_CODEC_FLAG_LOW_DELAY) {
        if (s->c.codec_id != AV_CODEC_ID_MPEG2VIDEO &&
            avctx->strict_std_compliance >= FF_COMPLIANCE_NORMAL) {
            av_log(avctx, AV_LOG_ERROR,
                   "low delay forcing is only available for mpeg2, "
                   "set strict_std_compliance to 'unofficial' or lower in order to allow it\n");
            return AVERROR(EINVAL);
        }
        if (m->max_b_frames != 0) {
            av_log(avctx, AV_LOG_ERROR,
                   "B-frames cannot be used with low delay\n");
            return AVERROR(EINVAL);
        }
    }

    if (avctx->slices > 1 &&
        !(avctx->codec->capabilities & AV_CODEC_CAP_SLICE_THREADS)) {
        av_log(avctx, AV_LOG_ERROR, "Multiple slices are not supported by this codec\n");
        return AVERROR(EINVAL);
    }

    if (m->b_frame_strategy && (avctx->flags & AV_CODEC_FLAG_PASS2)) {
        av_log(avctx, AV_LOG_INFO,
               "notice: b_frame_strategy only affects the first pass\n");
        m->b_frame_strategy = 0;
    }

    gcd = av_gcd(avctx->time_base.den, avctx->time_base.num);
    if (gcd > 1) {
        av_log(avctx, AV_LOG_INFO, "removing common factors from framerate\n");
        avctx->time_base.den /= gcd;
        avctx->time_base.num /= gcd;
        //return -1;
    }

    if (s->mpeg_quant || s->c.codec_id == AV_CODEC_ID_MPEG1VIDEO || s->c.codec_id == AV_CODEC_ID_MPEG2VIDEO || s->c.codec_id == AV_CODEC_ID_MJPEG || s->c.codec_id == AV_CODEC_ID_AMV || s->c.codec_id == AV_CODEC_ID_SPEEDHQ) {
        // (a + x * 3 / 8) / x
        s->intra_quant_bias = 3 << (QUANT_BIAS_SHIFT - 3);
        s->inter_quant_bias = 0;
    } else {
        s->intra_quant_bias = 0;
        // (a - x / 4) / x
        s->inter_quant_bias = -(1 << (QUANT_BIAS_SHIFT - 2));
    }

    if (avctx->qmin > avctx->qmax || avctx->qmin <= 0) {
        av_log(avctx, AV_LOG_ERROR, "qmin and or qmax are invalid, they must be 0 < min <= max\n");
        return AVERROR(EINVAL);
    }

    av_log(avctx, AV_LOG_DEBUG, "intra_quant_bias = %d inter_quant_bias = %d\n",s->intra_quant_bias,s->inter_quant_bias);

    switch (avctx->codec->id) {
#if CONFIG_MPEG1VIDEO_ENCODER || CONFIG_MPEG2VIDEO_ENCODER
    case AV_CODEC_ID_MPEG2VIDEO:
        s->rtp_mode   = 1;
        /* fallthrough */
    case AV_CODEC_ID_MPEG1VIDEO:
        s->c.out_format = FMT_MPEG1;
        s->c.low_delay  = !!(avctx->flags & AV_CODEC_FLAG_LOW_DELAY);
        avctx->delay  = s->c.low_delay ? 0 : (m->max_b_frames + 1);
        ff_mpeg1_encode_init(s);
        break;
#endif
#if CONFIG_MJPEG_ENCODER || CONFIG_AMV_ENCODER
    case AV_CODEC_ID_MJPEG:
    case AV_CODEC_ID_AMV:
        s->c.out_format = FMT_MJPEG;
        m->intra_only = 1; /* force intra only for jpeg */
        avctx->delay = 0;
        s->c.low_delay = 1;
        break;
#endif
    case AV_CODEC_ID_SPEEDHQ:
        s->c.out_format = FMT_SPEEDHQ;
        m->intra_only = 1; /* force intra only for SHQ */
        avctx->delay = 0;
        s->c.low_delay = 1;
        break;
    case AV_CODEC_ID_H261:
        s->c.out_format = FMT_H261;
        avctx->delay  = 0;
        s->c.low_delay  = 1;
        s->rtp_mode   = 0; /* Sliced encoding not supported */
        break;
    case AV_CODEC_ID_H263:
        if (!CONFIG_H263_ENCODER)
            return AVERROR_ENCODER_NOT_FOUND;
        if (ff_match_2uint16(ff_h263_format, FF_ARRAY_ELEMS(ff_h263_format),
                             s->c.width, s->c.height) == 8) {
            av_log(avctx, AV_LOG_ERROR,
                   "The specified picture size of %dx%d is not valid for "
                   "the H.263 codec.\nValid sizes are 128x96, 176x144, "
                   "352x288, 704x576, and 1408x1152. "
                   "Try H.263+.\n", s->c.width, s->c.height);
            return AVERROR(EINVAL);
        }
        s->c.out_format = FMT_H263;
        avctx->delay  = 0;
        s->c.low_delay  = 1;
        break;
    case AV_CODEC_ID_H263P:
        s->c.out_format = FMT_H263;
        /* Fx */
        s->c.h263_aic        = (avctx->flags & AV_CODEC_FLAG_AC_PRED) ? 1 : 0;
        s->c.modified_quant  = s->c.h263_aic;
        s->c.loop_filter     = (avctx->flags & AV_CODEC_FLAG_LOOP_FILTER) ? 1 : 0;
        s->c.unrestricted_mv = s->c.obmc || s->c.loop_filter || s->c.umvplus;
        s->c.flipflop_rounding = 1;

        /* /Fx */
        /* These are just to be sure */
        avctx->delay = 0;
        s->c.low_delay = 1;
        break;
    case AV_CODEC_ID_FLV1:
        s->c.out_format      = FMT_H263;
        s->c.h263_flv        = 2; /* format = 1; 11-bit codes */
        s->c.unrestricted_mv = 1;
        s->rtp_mode  = 0; /* don't allow GOB */
        avctx->delay = 0;
        s->c.low_delay = 1;
        break;
#if CONFIG_RV10_ENCODER
    case AV_CODEC_ID_RV10:
        m->encode_picture_header = ff_rv10_encode_picture_header;
        s->c.out_format = FMT_H263;
        avctx->delay  = 0;
        s->c.low_delay  = 1;
        break;
#endif
#if CONFIG_RV20_ENCODER
    case AV_CODEC_ID_RV20:
        m->encode_picture_header = ff_rv20_encode_picture_header;
        s->c.out_format      = FMT_H263;
        avctx->delay       = 0;
        s->c.low_delay       = 1;
        s->c.modified_quant  = 1;
        // Set here to force allocation of dc_val;
        // will be set later on a per-frame basis.
        s->c.h263_aic        = 1;
        s->c.loop_filter     = 1;
        s->c.unrestricted_mv = 0;
        break;
#endif
    case AV_CODEC_ID_MPEG4:
        s->c.out_format      = FMT_H263;
        s->c.h263_pred       = 1;
        s->c.unrestricted_mv = 1;
        s->c.flipflop_rounding = 1;
        s->c.low_delay       = m->max_b_frames ? 0 : 1;
        avctx->delay       = s->c.low_delay ? 0 : (m->max_b_frames + 1);
        break;
    case AV_CODEC_ID_MSMPEG4V2:
        s->c.out_format      = FMT_H263;
        s->c.h263_pred       = 1;
        s->c.unrestricted_mv = 1;
        s->c.msmpeg4_version = MSMP4_V2;
        avctx->delay       = 0;
        s->c.low_delay       = 1;
        break;
    case AV_CODEC_ID_MSMPEG4V3:
        s->c.out_format        = FMT_H263;
        s->c.h263_pred         = 1;
        s->c.unrestricted_mv   = 1;
        s->c.msmpeg4_version   = MSMP4_V3;
        s->c.flipflop_rounding = 1;
        avctx->delay         = 0;
        s->c.low_delay         = 1;
        break;
    case AV_CODEC_ID_WMV1:
        s->c.out_format        = FMT_H263;
        s->c.h263_pred         = 1;
        s->c.unrestricted_mv   = 1;
        s->c.msmpeg4_version   = MSMP4_WMV1;
        s->c.flipflop_rounding = 1;
        avctx->delay         = 0;
        s->c.low_delay         = 1;
        break;
    case AV_CODEC_ID_WMV2:
        s->c.out_format        = FMT_H263;
        s->c.h263_pred         = 1;
        s->c.unrestricted_mv   = 1;
        s->c.msmpeg4_version   = MSMP4_WMV2;
        s->c.flipflop_rounding = 1;
        avctx->delay         = 0;
        s->c.low_delay         = 1;
        break;
    default:
        av_unreachable("List contains all codecs using ff_mpv_encode_init()");
    }

    avctx->has_b_frames = !s->c.low_delay;

    s->c.encoding = 1;

    s->c.progressive_frame    =
    s->c.progressive_sequence = !(avctx->flags & (AV_CODEC_FLAG_INTERLACED_DCT |
                                                  AV_CODEC_FLAG_INTERLACED_ME) ||
                                s->c.alternate_scan);

    if (avctx->flags & AV_CODEC_FLAG_PSNR || avctx->mb_decision == FF_MB_DECISION_RD ||
        m->frame_skip_threshold || m->frame_skip_factor) {
        s->frame_reconstruction_bitfield = (1 << AV_PICTURE_TYPE_I) |
                                           (1 << AV_PICTURE_TYPE_P) |
                                           (1 << AV_PICTURE_TYPE_B);
    } else if (!m->intra_only) {
        s->frame_reconstruction_bitfield = (1 << AV_PICTURE_TYPE_I) |
                                           (1 << AV_PICTURE_TYPE_P);
    } else {
        s->frame_reconstruction_bitfield = 0;
    }

    if (m->lmin > m->lmax) {
        av_log(avctx, AV_LOG_WARNING, "Clipping lmin value to %d\n", m->lmax);
        m->lmin = m->lmax;
    }

    /* ff_mpv_init_duplicate_contexts() will copy (memdup) the contents of the
     * main slice to the slice contexts, so we initialize various fields of it
     * before calling ff_mpv_init_duplicate_contexts(). */
    s->parent = m;
    ff_mpv_idct_init(&s->c);
    init_unquantize(s, avctx);
    ff_fdctdsp_init(&s->fdsp, avctx);
    ff_mpegvideoencdsp_init(&s->mpvencdsp, avctx);
    ff_pixblockdsp_init(&s->pdsp, 8);
    ret = me_cmp_init(m, avctx);
    if (ret < 0)
        return ret;

    if (!(avctx->stats_out = av_mallocz(256))               ||
        !(s->new_pic = av_frame_alloc()) ||
        !(s->c.picture_pool = ff_mpv_alloc_pic_pool(0)))
        return AVERROR(ENOMEM);

    ret = init_matrices(m, avctx);
    if (ret < 0)
        return ret;

    ff_dct_encode_init(s);

    if (CONFIG_H263_ENCODER && s->c.out_format == FMT_H263) {
        ff_h263_encode_init(m);
#if CONFIG_MSMPEG4ENC
        if (s->c.msmpeg4_version != MSMP4_UNUSED)
            ff_msmpeg4_encode_init(m);
#endif
    }

    s->c.slice_ctx_size = sizeof(*s);
    ret = ff_mpv_common_init(&s->c);
    if (ret < 0)
        return ret;
    ret = init_buffers(m);
    if (ret < 0)
        return ret;
    if (s->c.slice_context_count > 1) {
        s->rtp_mode = 1;
        if (avctx->codec_id == AV_CODEC_ID_H263P)
            s->c.h263_slice_structured = 1;
    }
    ret = ff_mpv_init_duplicate_contexts(&s->c);
    if (ret < 0)
        return ret;

    ret = init_slice_buffers(m);
    if (ret < 0)
        return ret;

    ret = ff_rate_control_init(m);
    if (ret < 0)
        return ret;

    if (m->b_frame_strategy == 2) {
        for (int i = 0; i < m->max_b_frames + 2; i++) {
            m->tmp_frames[i] = av_frame_alloc();
            if (!m->tmp_frames[i])
                return AVERROR(ENOMEM);

            m->tmp_frames[i]->format = AV_PIX_FMT_YUV420P;
            m->tmp_frames[i]->width  = s->c.width  >> m->brd_scale;
            m->tmp_frames[i]->height = s->c.height >> m->brd_scale;

            ret = av_frame_get_buffer(m->tmp_frames[i], 0);
            if (ret < 0)
                return ret;
        }
    }

    cpb_props = ff_encode_add_cpb_side_data(avctx);
    if (!cpb_props)
        return AVERROR(ENOMEM);
    cpb_props->max_bitrate = avctx->rc_max_rate;
    cpb_props->min_bitrate = avctx->rc_min_rate;
    cpb_props->avg_bitrate = avctx->bit_rate;
    cpb_props->buffer_size = avctx->rc_buffer_size;

    return 0;
}

av_cold int ff_mpv_encode_end(AVCodecContext *avctx)
{
    MPVMainEncContext *const m = avctx->priv_data;
    MPVEncContext    *const s = &m->s;

    ff_rate_control_uninit(&m->rc_context);

    ff_mpv_common_end(&s->c);
    av_refstruct_pool_uninit(&s->c.picture_pool);

    for (int i = 0; i < MPVENC_MAX_B_FRAMES + 1; i++) {
        av_refstruct_unref(&m->input_picture[i]);
        av_refstruct_unref(&m->reordered_input_picture[i]);
    }
    for (int i = 0; i < FF_ARRAY_ELEMS(m->tmp_frames); i++)
        av_frame_free(&m->tmp_frames[i]);

    av_frame_free(&s->new_pic);

    av_freep(&avctx->stats_out);

    av_freep(&m->mv_table_base);
    av_freep(&s->p_field_select_table[0]);
    av_freep(&m->dct_error_sum_base);

    av_freep(&s->mb_type);
    av_freep(&s->lambda_table);

    av_freep(&s->q_intra_matrix);
    av_freep(&s->q_intra_matrix16);
    av_freep(&s->dct_offset);

    return 0;
}

/* put block[] to dest[] */
static inline void put_dct(MPVEncContext *const s,
                           int16_t *block, int i, uint8_t *dest, int line_size, int qscale)
{
    s->c.dct_unquantize_intra(&s->c, block, i, qscale);
    s->c.idsp.idct_put(dest, line_size, block);
}

static inline void add_dequant_dct(MPVEncContext *const s,
                           int16_t *block, int i, uint8_t *dest, int line_size, int qscale)
{
    if (s->c.block_last_index[i] >= 0) {
        s->c.dct_unquantize_inter(&s->c, block, i, qscale);

        s->c.idsp.idct_add(dest, line_size, block);
    }
}

/**
 * Performs dequantization and IDCT (if necessary)
 */
static void mpv_reconstruct_mb(MPVEncContext *const s, int16_t block[12][64])
{
    if (s->c.avctx->debug & FF_DEBUG_DCT_COEFF) {
       /* print DCT coefficients */
       av_log(s->c.avctx, AV_LOG_DEBUG, "DCT coeffs of MB at %dx%d:\n", s->c.mb_x, s->c.mb_y);
       for (int i = 0; i < 6; i++) {
           for (int j = 0; j < 64; j++) {
               av_log(s->c.avctx, AV_LOG_DEBUG, "%5d",
                      block[i][s->c.idsp.idct_permutation[j]]);
           }
           av_log(s->c.avctx, AV_LOG_DEBUG, "\n");
       }
    }

    if ((1 << s->c.pict_type) & s->frame_reconstruction_bitfield) {
        uint8_t *dest_y = s->c.dest[0], *dest_cb = s->c.dest[1], *dest_cr = s->c.dest[2];
        int dct_linesize, dct_offset;
        const int linesize   = s->c.cur_pic.linesize[0];
        const int uvlinesize = s->c.cur_pic.linesize[1];
        const int block_size = 8;

        dct_linesize = linesize << s->c.interlaced_dct;
        dct_offset   = s->c.interlaced_dct ? linesize : linesize * block_size;

        if (!s->c.mb_intra) {
            /* No MC, as that was already done otherwise */
            add_dequant_dct(s, block[0], 0, dest_y                          , dct_linesize, s->c.qscale);
            add_dequant_dct(s, block[1], 1, dest_y              + block_size, dct_linesize, s->c.qscale);
            add_dequant_dct(s, block[2], 2, dest_y + dct_offset             , dct_linesize, s->c.qscale);
            add_dequant_dct(s, block[3], 3, dest_y + dct_offset + block_size, dct_linesize, s->c.qscale);

            if (!CONFIG_GRAY || !(s->c.avctx->flags & AV_CODEC_FLAG_GRAY)) {
                if (s->c.chroma_y_shift) {
                    add_dequant_dct(s, block[4], 4, dest_cb, uvlinesize, s->c.chroma_qscale);
                    add_dequant_dct(s, block[5], 5, dest_cr, uvlinesize, s->c.chroma_qscale);
                } else {
                    dct_linesize >>= 1;
                    dct_offset   >>= 1;
                    add_dequant_dct(s, block[4], 4, dest_cb,              dct_linesize, s->c.chroma_qscale);
                    add_dequant_dct(s, block[5], 5, dest_cr,              dct_linesize, s->c.chroma_qscale);
                    add_dequant_dct(s, block[6], 6, dest_cb + dct_offset, dct_linesize, s->c.chroma_qscale);
                    add_dequant_dct(s, block[7], 7, dest_cr + dct_offset, dct_linesize, s->c.chroma_qscale);
                }
            }
        } else {
            /* dct only in intra block */
            put_dct(s, block[0], 0, dest_y                          , dct_linesize, s->c.qscale);
            put_dct(s, block[1], 1, dest_y              + block_size, dct_linesize, s->c.qscale);
            put_dct(s, block[2], 2, dest_y + dct_offset             , dct_linesize, s->c.qscale);
            put_dct(s, block[3], 3, dest_y + dct_offset + block_size, dct_linesize, s->c.qscale);

            if (!CONFIG_GRAY || !(s->c.avctx->flags & AV_CODEC_FLAG_GRAY)) {
                if (s->c.chroma_y_shift) {
                    put_dct(s, block[4], 4, dest_cb, uvlinesize, s->c.chroma_qscale);
                    put_dct(s, block[5], 5, dest_cr, uvlinesize, s->c.chroma_qscale);
                } else {
                    dct_offset   >>= 1;
                    dct_linesize >>= 1;
                    put_dct(s, block[4], 4, dest_cb,              dct_linesize, s->c.chroma_qscale);
                    put_dct(s, block[5], 5, dest_cr,              dct_linesize, s->c.chroma_qscale);
                    put_dct(s, block[6], 6, dest_cb + dct_offset, dct_linesize, s->c.chroma_qscale);
                    put_dct(s, block[7], 7, dest_cr + dct_offset, dct_linesize, s->c.chroma_qscale);
                }
            }
        }
    }
}

static int get_sae(const uint8_t *src, int ref, int stride)
{
    int x,y;
    int acc = 0;

    for (y = 0; y < 16; y++) {
        for (x = 0; x < 16; x++) {
            acc += FFABS(src[x + y * stride] - ref);
        }
    }

    return acc;
}

static int get_intra_count(MPVEncContext *const s, const uint8_t *src,
                           const uint8_t *ref, int stride)
{
    int x, y, w, h;
    int acc = 0;

    w = s->c.width  & ~15;
    h = s->c.height & ~15;

    for (y = 0; y < h; y += 16) {
        for (x = 0; x < w; x += 16) {
            int offset = x + y * stride;
            int sad  = s->sad_cmp[0](NULL, src + offset, ref + offset,
                                     stride, 16);
            int mean = (s->mpvencdsp.pix_sum(src + offset, stride) + 128) >> 8;
            int sae  = get_sae(src + offset, mean, stride);

            acc += sae + 500 < sad;
        }
    }
    return acc;
}

/**
 * Allocates new buffers for an AVFrame and copies the properties
 * from another AVFrame.
 */
static int prepare_picture(MPVEncContext *const s, AVFrame *f, const AVFrame *props_frame)
{
    AVCodecContext *avctx = s->c.avctx;
    int ret;

    f->width  = avctx->width  + 2 * EDGE_WIDTH;
    f->height = avctx->height + 2 * EDGE_WIDTH;

    ret = ff_encode_alloc_frame(avctx, f);
    if (ret < 0)
        return ret;

    ret = ff_mpv_pic_check_linesize(avctx, f, &s->c.linesize, &s->c.uvlinesize);
    if (ret < 0)
        return ret;

    for (int i = 0; f->data[i]; i++) {
        int offset = (EDGE_WIDTH >> (i ? s->c.chroma_y_shift : 0)) *
                     f->linesize[i] +
                     (EDGE_WIDTH >> (i ? s->c.chroma_x_shift : 0));
        f->data[i] += offset;
    }
    f->width  = avctx->width;
    f->height = avctx->height;

    ret = av_frame_copy_props(f, props_frame);
    if (ret < 0)
        return ret;

    return 0;
}

static int load_input_picture(MPVMainEncContext *const m, const AVFrame *pic_arg)
{
    MPVEncContext *const s = &m->s;
    MPVPicture *pic = NULL;
    int64_t pts;
    int display_picture_number = 0, ret;
    int encoding_delay = m->max_b_frames ? m->max_b_frames
                                         : (s->c.low_delay ? 0 : 1);
    int flush_offset = 1;
    int direct = 1;

    av_assert1(!m->input_picture[0]);

    if (pic_arg) {
        pts = pic_arg->pts;
        display_picture_number = m->input_picture_number++;

        if (pts != AV_NOPTS_VALUE) {
            if (m->user_specified_pts != AV_NOPTS_VALUE) {
                int64_t last = m->user_specified_pts;

                if (pts <= last) {
                    av_log(s->c.avctx, AV_LOG_ERROR,
                           "Invalid pts (%"PRId64") <= last (%"PRId64")\n",
                           pts, last);
                    return AVERROR(EINVAL);
                }

                if (!s->c.low_delay && display_picture_number == 1)
                    m->dts_delta = pts - last;
            }
            m->user_specified_pts = pts;
        } else {
            if (m->user_specified_pts != AV_NOPTS_VALUE) {
                m->user_specified_pts =
                pts = m->user_specified_pts + 1;
                av_log(s->c.avctx, AV_LOG_INFO,
                       "Warning: AVFrame.pts=? trying to guess (%"PRId64")\n",
                       pts);
            } else {
                pts = display_picture_number;
            }
        }

        if (pic_arg->linesize[0] != s->c.linesize ||
            pic_arg->linesize[1] != s->c.uvlinesize ||
            pic_arg->linesize[2] != s->c.uvlinesize)
            direct = 0;
        if ((s->c.width & 15) || (s->c.height & 15))
            direct = 0;
        if (((intptr_t)(pic_arg->data[0])) & (STRIDE_ALIGN-1))
            direct = 0;
        if (s->c.linesize & (STRIDE_ALIGN-1))
            direct = 0;

        ff_dlog(s->c.avctx, "%d %d %"PTRDIFF_SPECIFIER" %"PTRDIFF_SPECIFIER"\n", pic_arg->linesize[0],
                pic_arg->linesize[1], s->c.linesize, s->c.uvlinesize);

        pic = av_refstruct_pool_get(s->c.picture_pool);
        if (!pic)
            return AVERROR(ENOMEM);

        if (direct) {
            if ((ret = av_frame_ref(pic->f, pic_arg)) < 0)
                goto fail;
            pic->shared = 1;
        } else {
            ret = prepare_picture(s, pic->f, pic_arg);
            if (ret < 0)
                goto fail;

            for (int i = 0; i < 3; i++) {
                ptrdiff_t src_stride = pic_arg->linesize[i];
                ptrdiff_t dst_stride = i ? s->c.uvlinesize : s->c.linesize;
                int h_shift = i ? s->c.chroma_x_shift : 0;
                int v_shift = i ? s->c.chroma_y_shift : 0;
                int w = AV_CEIL_RSHIFT(s->c.width , h_shift);
                int h = AV_CEIL_RSHIFT(s->c.height, v_shift);
                const uint8_t *src = pic_arg->data[i];
                uint8_t *dst = pic->f->data[i];
                int vpad = 16;

                if (   s->c.codec_id == AV_CODEC_ID_MPEG2VIDEO
                    && !s->c.progressive_sequence
                    && FFALIGN(s->c.height, 32) - s->c.height > 16)
                    vpad = 32;

                if (!s->c.avctx->rc_buffer_size)
                    dst += INPLACE_OFFSET;

                if (src_stride == dst_stride)
                    memcpy(dst, src, src_stride * h - src_stride + w);
                else {
                    int h2 = h;
                    uint8_t *dst2 = dst;
                    while (h2--) {
                        memcpy(dst2, src, w);
                        dst2 += dst_stride;
                        src += src_stride;
                    }
                }
                if ((s->c.width & 15) || (s->c.height & (vpad-1))) {
                    s->mpvencdsp.draw_edges(dst, dst_stride,
                                            w, h,
                                            16 >> h_shift,
                                            vpad >> v_shift,
                                            EDGE_BOTTOM);
                }
            }
            emms_c();
        }

        pic->display_picture_number = display_picture_number;
        pic->f->pts = pts; // we set this here to avoid modifying pic_arg
    } else if (!m->reordered_input_picture[1]) {
        /* Flushing: When the above check is true, the encoder is about to run
         * out of frames to encode. Check if there are input_pictures left;
         * if so, ensure m->input_picture[0] contains the first picture.
         * A flush_offset != 1 will only happen if we did not receive enough
         * input frames. */
        for (flush_offset = 0; flush_offset < encoding_delay + 1; flush_offset++)
            if (m->input_picture[flush_offset])
                break;

        encoding_delay -= flush_offset - 1;
    }

    /* shift buffer entries */
    for (int i = flush_offset; i <= MPVENC_MAX_B_FRAMES; i++)
        m->input_picture[i - flush_offset] = m->input_picture[i];
    for (int i = MPVENC_MAX_B_FRAMES + 1 - flush_offset; i <= MPVENC_MAX_B_FRAMES; i++)
        m->input_picture[i] = NULL;

    m->input_picture[encoding_delay] = pic;

    return 0;
fail:
    av_refstruct_unref(&pic);
    return ret;
}

static int skip_check(MPVMainEncContext *const m,
                      const MPVPicture *p, const MPVPicture *ref)
{
    MPVEncContext *const s = &m->s;
    int score = 0;
    int64_t score64 = 0;

    for (int plane = 0; plane < 3; plane++) {
        const int stride = p->f->linesize[plane];
        const int bw = plane ? 1 : 2;
        for (int y = 0; y < s->c.mb_height * bw; y++) {
            for (int x = 0; x < s->c.mb_width * bw; x++) {
                int off = p->shared ? 0 : 16;
                const uint8_t *dptr = p->f->data[plane] + 8 * (x + y * stride) + off;
                const uint8_t *rptr = ref->f->data[plane] + 8 * (x + y * stride);
                int v = m->frame_skip_cmp_fn(s, dptr, rptr, stride, 8);

                switch (FFABS(m->frame_skip_exp)) {
                case 0: score    =  FFMAX(score, v);          break;
                case 1: score   += FFABS(v);                  break;
                case 2: score64 += v * (int64_t)v;                       break;
                case 3: score64 += FFABS(v * (int64_t)v * v);            break;
                case 4: score64 += (v * (int64_t)v) * (v * (int64_t)v);  break;
                }
            }
        }
    }
    emms_c();

    if (score)
        score64 = score;
    if (m->frame_skip_exp < 0)
        score64 = pow(score64 / (double)(s->c.mb_width * s->c.mb_height),
                      -1.0/m->frame_skip_exp);

    if (score64 < m->frame_skip_threshold)
        return 1;
    if (score64 < ((m->frame_skip_factor * (int64_t) s->lambda) >> 8))
        return 1;
    return 0;
}

static int encode_frame(AVCodecContext *c, const AVFrame *frame, AVPacket *pkt)
{
    int ret;
    int size = 0;

    ret = avcodec_send_frame(c, frame);
    if (ret < 0)
        return ret;

    do {
        ret = avcodec_receive_packet(c, pkt);
        if (ret >= 0) {
            size += pkt->size;
            av_packet_unref(pkt);
        } else if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
            return ret;
    } while (ret >= 0);

    return size;
}

static int estimate_best_b_count(MPVMainEncContext *const m)
{
    MPVEncContext *const s = &m->s;
    AVPacket *pkt;
    const int scale = m->brd_scale;
    int width  = s->c.width  >> scale;
    int height = s->c.height >> scale;
    int out_size, p_lambda, b_lambda, lambda2;
    int64_t best_rd  = INT64_MAX;
    int best_b_count = -1;
    int ret = 0;

    av_assert0(scale >= 0 && scale <= 3);

    pkt = av_packet_alloc();
    if (!pkt)
        return AVERROR(ENOMEM);

    //emms_c();
    p_lambda = m->last_lambda_for[AV_PICTURE_TYPE_P];
    //p_lambda * FFABS(s->c.avctx->b_quant_factor) + s->c.avctx->b_quant_offset;
    b_lambda = m->last_lambda_for[AV_PICTURE_TYPE_B];
    if (!b_lambda) // FIXME we should do this somewhere else
        b_lambda = p_lambda;
    lambda2  = (b_lambda * b_lambda + (1 << FF_LAMBDA_SHIFT) / 2) >>
               FF_LAMBDA_SHIFT;

    for (int i = 0; i < m->max_b_frames + 2; i++) {
        const MPVPicture *pre_input_ptr = i ? m->input_picture[i - 1] :
                                           s->c.next_pic.ptr;

        if (pre_input_ptr) {
            const uint8_t *data[4];
            memcpy(data, pre_input_ptr->f->data, sizeof(data));

            if (!pre_input_ptr->shared && i) {
                data[0] += INPLACE_OFFSET;
                data[1] += INPLACE_OFFSET;
                data[2] += INPLACE_OFFSET;
            }

            s->mpvencdsp.shrink[scale](m->tmp_frames[i]->data[0],
                                       m->tmp_frames[i]->linesize[0],
                                       data[0],
                                       pre_input_ptr->f->linesize[0],
                                       width, height);
            s->mpvencdsp.shrink[scale](m->tmp_frames[i]->data[1],
                                       m->tmp_frames[i]->linesize[1],
                                       data[1],
                                       pre_input_ptr->f->linesize[1],
                                       width >> 1, height >> 1);
            s->mpvencdsp.shrink[scale](m->tmp_frames[i]->data[2],
                                       m->tmp_frames[i]->linesize[2],
                                       data[2],
                                       pre_input_ptr->f->linesize[2],
                                       width >> 1, height >> 1);
        }
    }

    for (int j = 0; j < m->max_b_frames + 1; j++) {
        AVCodecContext *c;
        int64_t rd = 0;

        if (!m->input_picture[j])
            break;

        c = avcodec_alloc_context3(NULL);
        if (!c) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        c->width        = width;
        c->height       = height;
        c->flags        = AV_CODEC_FLAG_QSCALE | AV_CODEC_FLAG_PSNR;
        c->flags       |= s->c.avctx->flags & AV_CODEC_FLAG_QPEL;
        c->mb_decision  = s->c.avctx->mb_decision;
        c->me_cmp       = s->c.avctx->me_cmp;
        c->mb_cmp       = s->c.avctx->mb_cmp;
        c->me_sub_cmp   = s->c.avctx->me_sub_cmp;
        c->pix_fmt      = AV_PIX_FMT_YUV420P;
        c->time_base    = s->c.avctx->time_base;
        c->max_b_frames = m->max_b_frames;

        ret = avcodec_open2(c, s->c.avctx->codec, NULL);
        if (ret < 0)
            goto fail;


        m->tmp_frames[0]->pict_type = AV_PICTURE_TYPE_I;
        m->tmp_frames[0]->quality   = 1 * FF_QP2LAMBDA;

        out_size = encode_frame(c, m->tmp_frames[0], pkt);
        if (out_size < 0) {
            ret = out_size;
            goto fail;
        }

        //rd += (out_size * lambda2) >> FF_LAMBDA_SHIFT;

        for (int i = 0; i < m->max_b_frames + 1; i++) {
            int is_p = i % (j + 1) == j || i == m->max_b_frames;

            m->tmp_frames[i + 1]->pict_type = is_p ?
                                     AV_PICTURE_TYPE_P : AV_PICTURE_TYPE_B;
            m->tmp_frames[i + 1]->quality   = is_p ? p_lambda : b_lambda;

            out_size = encode_frame(c, m->tmp_frames[i + 1], pkt);
            if (out_size < 0) {
                ret = out_size;
                goto fail;
            }

            rd += (out_size * (uint64_t)lambda2) >> (FF_LAMBDA_SHIFT - 3);
        }

        /* get the delayed frames */
        out_size = encode_frame(c, NULL, pkt);
        if (out_size < 0) {
            ret = out_size;
            goto fail;
        }
        rd += (out_size * (uint64_t)lambda2) >> (FF_LAMBDA_SHIFT - 3);

        rd += c->error[0] + c->error[1] + c->error[2];

        if (rd < best_rd) {
            best_rd = rd;
            best_b_count = j;
        }

fail:
        avcodec_free_context(&c);
        av_packet_unref(pkt);
        if (ret < 0) {
            best_b_count = ret;
            break;
        }
    }

    av_packet_free(&pkt);

    return best_b_count;
}

/**
 * Determines whether an input picture is discarded or not
 * and if not determines the length of the next chain of B frames
 * and moves these pictures (including the P frame) into
 * reordered_input_picture.
 * input_picture[0] is always NULL when exiting this function, even on error;
 * reordered_input_picture[0] is always NULL when exiting this function on error.
 */
static int set_bframe_chain_length(MPVMainEncContext *const m)
{
    MPVEncContext *const s = &m->s;

    /* Either nothing to do or can't do anything */
    if (m->reordered_input_picture[0] || !m->input_picture[0])
        return 0;

    /* set next picture type & ordering */
    if (m->frame_skip_threshold || m->frame_skip_factor) {
        if (m->picture_in_gop_number < m->gop_size &&
            s->c.next_pic.ptr &&
            skip_check(m, m->input_picture[0], s->c.next_pic.ptr)) {
            // FIXME check that the gop check above is +-1 correct
            av_refstruct_unref(&m->input_picture[0]);

            ff_vbv_update(m, 0);

            return 0;
        }
    }

    if (/* m->picture_in_gop_number >= m->gop_size || */
        !s->c.next_pic.ptr || m->intra_only) {
        m->reordered_input_picture[0] = m->input_picture[0];
        m->input_picture[0] = NULL;
        m->reordered_input_picture[0]->f->pict_type = AV_PICTURE_TYPE_I;
        m->reordered_input_picture[0]->coded_picture_number =
            m->coded_picture_number++;
    } else {
        int b_frames = 0;

        if (s->c.avctx->flags & AV_CODEC_FLAG_PASS2) {
            for (int i = 0; i < m->max_b_frames + 1; i++) {
                int pict_num = m->input_picture[0]->display_picture_number + i;

                if (pict_num >= m->rc_context.num_entries)
                    break;
                if (!m->input_picture[i]) {
                    m->rc_context.entry[pict_num - 1].new_pict_type = AV_PICTURE_TYPE_P;
                    break;
                }

                m->input_picture[i]->f->pict_type =
                    m->rc_context.entry[pict_num].new_pict_type;
            }
        }

        if (m->b_frame_strategy == 0) {
            b_frames = m->max_b_frames;
            while (b_frames && !m->input_picture[b_frames])
                b_frames--;
        } else if (m->b_frame_strategy == 1) {
            for (int i = 1; i < m->max_b_frames + 1; i++) {
                if (m->input_picture[i] &&
                    m->input_picture[i]->b_frame_score == 0) {
                    m->input_picture[i]->b_frame_score =
                        get_intra_count(s,
                                        m->input_picture[i    ]->f->data[0],
                                        m->input_picture[i - 1]->f->data[0],
                                        s->c.linesize) + 1;
                }
            }
            for (int i = 0;; i++) {
                if (i >= m->max_b_frames + 1 ||
                    !m->input_picture[i] ||
                    m->input_picture[i]->b_frame_score - 1 >
                        s->c.mb_num / m->b_sensitivity) {
                    b_frames = FFMAX(0, i - 1);
                    break;
                }
            }

            /* reset scores */
            for (int i = 0; i < b_frames + 1; i++)
                m->input_picture[i]->b_frame_score = 0;
        } else if (m->b_frame_strategy == 2) {
            b_frames = estimate_best_b_count(m);
            if (b_frames < 0) {
                av_refstruct_unref(&m->input_picture[0]);
                return b_frames;
            }
        }

        emms_c();

        for (int i = b_frames - 1; i >= 0; i--) {
            int type = m->input_picture[i]->f->pict_type;
            if (type && type != AV_PICTURE_TYPE_B)
                b_frames = i;
        }
        if (m->input_picture[b_frames]->f->pict_type == AV_PICTURE_TYPE_B &&
            b_frames == m->max_b_frames) {
            av_log(s->c.avctx, AV_LOG_ERROR,
                    "warning, too many B-frames in a row\n");
        }

        if (m->picture_in_gop_number + b_frames >= m->gop_size) {
            if ((s->mpv_flags & FF_MPV_FLAG_STRICT_GOP) &&
                m->gop_size > m->picture_in_gop_number) {
                b_frames = m->gop_size - m->picture_in_gop_number - 1;
            } else {
                if (s->c.avctx->flags & AV_CODEC_FLAG_CLOSED_GOP)
                    b_frames = 0;
                m->input_picture[b_frames]->f->pict_type = AV_PICTURE_TYPE_I;
            }
        }

        if ((s->c.avctx->flags & AV_CODEC_FLAG_CLOSED_GOP) && b_frames &&
            m->input_picture[b_frames]->f->pict_type == AV_PICTURE_TYPE_I)
            b_frames--;

        m->reordered_input_picture[0] = m->input_picture[b_frames];
        m->input_picture[b_frames]    = NULL;
        if (m->reordered_input_picture[0]->f->pict_type != AV_PICTURE_TYPE_I)
            m->reordered_input_picture[0]->f->pict_type = AV_PICTURE_TYPE_P;
        m->reordered_input_picture[0]->coded_picture_number =
            m->coded_picture_number++;
        for (int i = 0; i < b_frames; i++) {
            m->reordered_input_picture[i + 1] = m->input_picture[i];
            m->input_picture[i]               = NULL;
            m->reordered_input_picture[i + 1]->f->pict_type =
                AV_PICTURE_TYPE_B;
            m->reordered_input_picture[i + 1]->coded_picture_number =
                m->coded_picture_number++;
        }
    }

    return 0;
}

static int select_input_picture(MPVMainEncContext *const m)
{
    MPVEncContext *const s = &m->s;
    int ret;

    av_assert1(!m->reordered_input_picture[0]);

    for (int i = 1; i <= MPVENC_MAX_B_FRAMES; i++)
        m->reordered_input_picture[i - 1] = m->reordered_input_picture[i];
    m->reordered_input_picture[MPVENC_MAX_B_FRAMES] = NULL;

    ret = set_bframe_chain_length(m);
    av_assert1(!m->input_picture[0]);
    if (ret < 0)
        return ret;

    av_frame_unref(s->new_pic);

    if (m->reordered_input_picture[0]) {
        m->reordered_input_picture[0]->reference =
           m->reordered_input_picture[0]->f->pict_type != AV_PICTURE_TYPE_B;

        if (m->reordered_input_picture[0]->shared || s->c.avctx->rc_buffer_size) {
            // input is a shared pix, so we can't modify it -> allocate a new
            // one & ensure that the shared one is reuseable
            av_frame_move_ref(s->new_pic, m->reordered_input_picture[0]->f);

            ret = prepare_picture(s, m->reordered_input_picture[0]->f, s->new_pic);
            if (ret < 0)
                goto fail;
        } else {
            // input is not a shared pix -> reuse buffer for current_pix
            ret = av_frame_ref(s->new_pic, m->reordered_input_picture[0]->f);
            if (ret < 0)
                goto fail;
            for (int i = 0; i < MPV_MAX_PLANES; i++)
                s->new_pic->data[i] += INPLACE_OFFSET;
        }
        s->c.cur_pic.ptr = m->reordered_input_picture[0];
        m->reordered_input_picture[0] = NULL;
        av_assert1(s->c.mb_width  == s->c.buffer_pools.alloc_mb_width);
        av_assert1(s->c.mb_height == s->c.buffer_pools.alloc_mb_height);
        av_assert1(s->c.mb_stride == s->c.buffer_pools.alloc_mb_stride);
        ret = ff_mpv_alloc_pic_accessories(s->c.avctx, &s->c.cur_pic,
                                           &s->c.sc, &s->c.buffer_pools, s->c.mb_height);
        if (ret < 0) {
            ff_mpv_unref_picture(&s->c.cur_pic);
            return ret;
        }
        s->c.picture_number = s->c.cur_pic.ptr->display_picture_number;

    }
    return 0;
fail:
    av_refstruct_unref(&m->reordered_input_picture[0]);
    return ret;
}

static void frame_end(MPVMainEncContext *const m)
{
    MPVEncContext *const s = &m->s;

    if (s->c.unrestricted_mv &&
        s->c.cur_pic.reference &&
        !m->intra_only) {
        int hshift = s->c.chroma_x_shift;
        int vshift = s->c.chroma_y_shift;
        s->mpvencdsp.draw_edges(s->c.cur_pic.data[0],
                                s->c.cur_pic.linesize[0],
                                s->c.h_edge_pos, s->c.v_edge_pos,
                                EDGE_WIDTH, EDGE_WIDTH,
                                EDGE_TOP | EDGE_BOTTOM);
        s->mpvencdsp.draw_edges(s->c.cur_pic.data[1],
                                s->c.cur_pic.linesize[1],
                                s->c.h_edge_pos >> hshift,
                                s->c.v_edge_pos >> vshift,
                                EDGE_WIDTH >> hshift,
                                EDGE_WIDTH >> vshift,
                                EDGE_TOP | EDGE_BOTTOM);
        s->mpvencdsp.draw_edges(s->c.cur_pic.data[2],
                                s->c.cur_pic.linesize[2],
                                s->c.h_edge_pos >> hshift,
                                s->c.v_edge_pos >> vshift,
                                EDGE_WIDTH >> hshift,
                                EDGE_WIDTH >> vshift,
                                EDGE_TOP | EDGE_BOTTOM);
    }

    emms_c();

    m->last_pict_type                  = s->c.pict_type;
    m->last_lambda_for[s->c.pict_type] = s->c.cur_pic.ptr->f->quality;
    if (s->c.pict_type != AV_PICTURE_TYPE_B)
        m->last_non_b_pict_type = s->c.pict_type;
}

static void update_noise_reduction(MPVMainEncContext *const m)
{
    MPVEncContext *const s = &m->s;
    int intra, i;

    for (intra = 0; intra < 2; intra++) {
        if (s->dct_count[intra] > (1 << 16)) {
            for (i = 0; i < 64; i++) {
                s->dct_error_sum[intra][i] >>= 1;
            }
            s->dct_count[intra] >>= 1;
        }

        for (i = 0; i < 64; i++) {
            s->dct_offset[intra][i] = (m->noise_reduction *
                                       s->dct_count[intra] +
                                       s->dct_error_sum[intra][i] / 2) /
                                      (s->dct_error_sum[intra][i] + 1);
        }
    }
}

static void frame_start(MPVMainEncContext *const m)
{
    MPVEncContext *const s = &m->s;

    s->c.cur_pic.ptr->f->pict_type = s->c.pict_type;

    if (s->c.pict_type != AV_PICTURE_TYPE_B) {
        ff_mpv_replace_picture(&s->c.last_pic, &s->c.next_pic);
        ff_mpv_replace_picture(&s->c.next_pic, &s->c.cur_pic);
    }

    av_assert2(!!m->noise_reduction == !!s->dct_error_sum);
    if (s->dct_error_sum) {
        update_noise_reduction(m);
    }
}

int ff_mpv_encode_picture(AVCodecContext *avctx, AVPacket *pkt,
                          const AVFrame *pic_arg, int *got_packet)
{
    MPVMainEncContext *const m = avctx->priv_data;
    MPVEncContext    *const s = &m->s;
    int stuffing_count, ret;
    int context_count = s->c.slice_context_count;

    ff_mpv_unref_picture(&s->c.cur_pic);

    m->vbv_ignore_qmax = 0;

    m->picture_in_gop_number++;

    ret = load_input_picture(m, pic_arg);
    if (ret < 0)
        return ret;

    ret = select_input_picture(m);
    if (ret < 0)
        return ret;

    /* output? */
    if (s->new_pic->data[0]) {
        int growing_buffer = context_count == 1 && !s->c.data_partitioning;
        size_t pkt_size = 10000 + s->c.mb_width * s->c.mb_height *
                                  (growing_buffer ? 64 : (MAX_MB_BYTES + 100));
        if (CONFIG_MJPEG_ENCODER && avctx->codec_id == AV_CODEC_ID_MJPEG) {
            ret = ff_mjpeg_add_icc_profile_size(avctx, s->new_pic, &pkt_size);
            if (ret < 0)
                return ret;
        }
        if ((ret = ff_alloc_packet(avctx, pkt, pkt_size)) < 0)
            return ret;
        pkt->size = avctx->internal->byte_buffer_size - AV_INPUT_BUFFER_PADDING_SIZE;
        if (s->mb_info) {
            s->mb_info_ptr = av_packet_new_side_data(pkt,
                                 AV_PKT_DATA_H263_MB_INFO,
                                 s->c.mb_width*s->c.mb_height*12);
            if (!s->mb_info_ptr)
                return AVERROR(ENOMEM);
            s->prev_mb_info = s->last_mb_info = s->mb_info_size = 0;
        }

        s->c.pict_type = s->new_pic->pict_type;
        //emms_c();
        frame_start(m);
vbv_retry:
        ret = encode_picture(m, pkt);
        if (growing_buffer) {
            av_assert0(s->pb.buf == avctx->internal->byte_buffer);
            pkt->data = s->pb.buf;
            pkt->size = avctx->internal->byte_buffer_size;
        }
        if (ret < 0)
            return -1;

        frame_end(m);

       if ((CONFIG_MJPEG_ENCODER || CONFIG_AMV_ENCODER) && s->c.out_format == FMT_MJPEG)
            ff_mjpeg_encode_picture_trailer(&s->pb, m->header_bits);

        if (avctx->rc_buffer_size) {
            RateControlContext *rcc = &m->rc_context;
            int max_size = FFMAX(rcc->buffer_index * avctx->rc_max_available_vbv_use, rcc->buffer_index - 500);
            int hq = (avctx->mb_decision == FF_MB_DECISION_RD || avctx->trellis);
            int min_step = hq ? 1 : (1<<(FF_LAMBDA_SHIFT + 7))/139;

            if (put_bits_count(&s->pb) > max_size &&
                s->lambda < m->lmax) {
                m->next_lambda = FFMAX(s->lambda + min_step, s->lambda *
                                       (s->c.qscale + 1) / s->c.qscale);
                if (s->adaptive_quant) {
                    for (int i = 0; i < s->c.mb_height * s->c.mb_stride; i++)
                        s->lambda_table[i] =
                            FFMAX(s->lambda_table[i] + min_step,
                                  s->lambda_table[i] * (s->c.qscale + 1) /
                                  s->c.qscale);
                }
                s->c.mb_skipped = 0;        // done in frame_start()
                // done in encode_picture() so we must undo it
                if (s->c.pict_type == AV_PICTURE_TYPE_P) {
                    s->c.no_rounding ^= s->c.flipflop_rounding;
                }
                if (s->c.pict_type != AV_PICTURE_TYPE_B) {
                    s->c.time_base       = s->c.last_time_base;
                    s->c.last_non_b_time = s->c.time - s->c.pp_time;
                }
                m->vbv_ignore_qmax = 1;
                av_log(avctx, AV_LOG_VERBOSE, "reencoding frame due to VBV\n");
                goto vbv_retry;
            }

            av_assert0(avctx->rc_max_rate);
        }

        if (avctx->flags & AV_CODEC_FLAG_PASS1)
            ff_write_pass1_stats(m);

        for (int i = 0; i < MPV_MAX_PLANES; i++)
            avctx->error[i] += s->encoding_error[i];
        ff_side_data_set_encoder_stats(pkt, s->c.cur_pic.ptr->f->quality,
                                       s->encoding_error,
                                       (avctx->flags&AV_CODEC_FLAG_PSNR) ? MPV_MAX_PLANES : 0,
                                       s->c.pict_type);

        if (avctx->flags & AV_CODEC_FLAG_PASS1)
            assert(put_bits_count(&s->pb) == m->header_bits + s->mv_bits +
                                             s->misc_bits + s->i_tex_bits +
                                             s->p_tex_bits);
        flush_put_bits(&s->pb);
        m->frame_bits  = put_bits_count(&s->pb);

        stuffing_count = ff_vbv_update(m, m->frame_bits);
        m->stuffing_bits = 8*stuffing_count;
        if (stuffing_count) {
            if (put_bytes_left(&s->pb, 0) < stuffing_count + 50) {
                av_log(avctx, AV_LOG_ERROR, "stuffing too large\n");
                return -1;
            }

            switch (s->c.codec_id) {
            case AV_CODEC_ID_MPEG1VIDEO:
            case AV_CODEC_ID_MPEG2VIDEO:
                while (stuffing_count--) {
                    put_bits(&s->pb, 8, 0);
                }
            break;
            case AV_CODEC_ID_MPEG4:
                put_bits(&s->pb, 16, 0);
                put_bits(&s->pb, 16, 0x1C3);
                stuffing_count -= 4;
                while (stuffing_count--) {
                    put_bits(&s->pb, 8, 0xFF);
                }
            break;
            default:
                av_log(avctx, AV_LOG_ERROR, "vbv buffer overflow\n");
                m->stuffing_bits = 0;
            }
            flush_put_bits(&s->pb);
            m->frame_bits  = put_bits_count(&s->pb);
        }

        /* update MPEG-1/2 vbv_delay for CBR */
        if (avctx->rc_max_rate                          &&
            avctx->rc_min_rate == avctx->rc_max_rate &&
            s->c.out_format == FMT_MPEG1             &&
            90000LL * (avctx->rc_buffer_size - 1) <=
                avctx->rc_max_rate * 0xFFFFLL) {
            AVCPBProperties *props;
            size_t props_size;

            int vbv_delay, min_delay;
            double inbits  = avctx->rc_max_rate *
                             av_q2d(avctx->time_base);
            int    minbits = m->frame_bits - 8 *
                             (m->vbv_delay_pos - 1);
            double bits    = m->rc_context.buffer_index + minbits - inbits;
            uint8_t *const vbv_delay_ptr = s->pb.buf + m->vbv_delay_pos;

            if (bits < 0)
                av_log(avctx, AV_LOG_ERROR,
                       "Internal error, negative bits\n");

            av_assert1(s->c.repeat_first_field == 0);

            vbv_delay = bits * 90000 / avctx->rc_max_rate;
            min_delay = (minbits * 90000LL + avctx->rc_max_rate - 1) /
                        avctx->rc_max_rate;

            vbv_delay = FFMAX(vbv_delay, min_delay);

            av_assert0(vbv_delay < 0xFFFF);

            vbv_delay_ptr[0] &= 0xF8;
            vbv_delay_ptr[0] |= vbv_delay >> 13;
            vbv_delay_ptr[1]  = vbv_delay >> 5;
            vbv_delay_ptr[2] &= 0x07;
            vbv_delay_ptr[2] |= vbv_delay << 3;

            props = av_cpb_properties_alloc(&props_size);
            if (!props)
                return AVERROR(ENOMEM);
            props->vbv_delay = vbv_delay * 300;

            ret = av_packet_add_side_data(pkt, AV_PKT_DATA_CPB_PROPERTIES,
                                          (uint8_t*)props, props_size);
            if (ret < 0) {
                av_freep(&props);
                return ret;
            }
        }
        m->total_bits += m->frame_bits;

        pkt->pts = s->c.cur_pic.ptr->f->pts;
        pkt->duration = s->c.cur_pic.ptr->f->duration;
        if (!s->c.low_delay && s->c.pict_type != AV_PICTURE_TYPE_B) {
            if (!s->c.cur_pic.ptr->coded_picture_number)
                pkt->dts = pkt->pts - m->dts_delta;
            else
                pkt->dts = m->reordered_pts;
            m->reordered_pts = pkt->pts;
        } else
            pkt->dts = pkt->pts;

        // the no-delay case is handled in generic code
        if (avctx->codec->capabilities & AV_CODEC_CAP_DELAY) {
            ret = ff_encode_reordered_opaque(avctx, pkt, s->c.cur_pic.ptr->f);
            if (ret < 0)
                return ret;
        }

        if (s->c.cur_pic.ptr->f->flags & AV_FRAME_FLAG_KEY)
            pkt->flags |= AV_PKT_FLAG_KEY;
        if (s->mb_info)
            av_packet_shrink_side_data(pkt, AV_PKT_DATA_H263_MB_INFO, s->mb_info_size);
    } else {
        m->frame_bits = 0;
    }

    ff_mpv_unref_picture(&s->c.cur_pic);

    av_assert1((m->frame_bits & 7) == 0);

    pkt->size = m->frame_bits / 8;
    *got_packet = !!pkt->size;
    return 0;
}

static inline void dct_single_coeff_elimination(MPVEncContext *const s,
                                                int n, int threshold)
{
    static const char tab[64] = {
        3, 2, 2, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1,
        0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0
    };
    int score = 0;
    int run = 0;
    int i;
    int16_t *block = s->c.block[n];
    const int last_index = s->c.block_last_index[n];
    int skip_dc;

    if (threshold < 0) {
        skip_dc = 0;
        threshold = -threshold;
    } else
        skip_dc = 1;

    /* Are all we could set to zero already zero? */
    if (last_index <= skip_dc - 1)
        return;

    for (i = 0; i <= last_index; i++) {
        const int j = s->c.intra_scantable.permutated[i];
        const int level = FFABS(block[j]);
        if (level == 1) {
            if (skip_dc && i == 0)
                continue;
            score += tab[run];
            run = 0;
        } else if (level > 1) {
            return;
        } else {
            run++;
        }
    }
    if (score >= threshold)
        return;
    for (i = skip_dc; i <= last_index; i++) {
        const int j = s->c.intra_scantable.permutated[i];
        block[j] = 0;
    }
    if (block[0])
        s->c.block_last_index[n] = 0;
    else
        s->c.block_last_index[n] = -1;
}

static inline void clip_coeffs(const MPVEncContext *const s, int16_t block[],
                               int last_index)
{
    int i;
    const int maxlevel = s->max_qcoeff;
    const int minlevel = s->min_qcoeff;
    int overflow = 0;

    if (s->c.mb_intra) {
        i = 1; // skip clipping of intra dc
    } else
        i = 0;

    for (; i <= last_index; i++) {
        const int j = s->c.intra_scantable.permutated[i];
        int level = block[j];

        if (level > maxlevel) {
            level = maxlevel;
            overflow++;
        } else if (level < minlevel) {
            level = minlevel;
            overflow++;
        }

        block[j] = level;
    }

    if (overflow && s->c.avctx->mb_decision == FF_MB_DECISION_SIMPLE)
        av_log(s->c.avctx, AV_LOG_INFO,
               "warning, clipping %d dct coefficients to %d..%d\n",
               overflow, minlevel, maxlevel);
}

static void get_visual_weight(int16_t *weight, const uint8_t *ptr, int stride)
{
    int x, y;
    // FIXME optimize
    for (y = 0; y < 8; y++) {
        for (x = 0; x < 8; x++) {
            int x2, y2;
            int sum = 0;
            int sqr = 0;
            int count = 0;

            for (y2 = FFMAX(y - 1, 0); y2 < FFMIN(8, y + 2); y2++) {
                for (x2= FFMAX(x - 1, 0); x2 < FFMIN(8, x + 2); x2++) {
                    int v = ptr[x2 + y2 * stride];
                    sum += v;
                    sqr += v * v;
                    count++;
                }
            }
            weight[x + 8 * y]= (36 * ff_sqrt(count * sqr - sum * sum)) / count;
        }
    }
}

static av_always_inline void encode_mb_internal(MPVEncContext *const s,
                                                int motion_x, int motion_y,
                                                int mb_block_height,
                                                int mb_block_width,
                                                int mb_block_count,
                                                int chroma_x_shift,
                                                int chroma_y_shift,
                                                int chroma_format)
{
/* Interlaced DCT is only possible with MPEG-2 and MPEG-4
 * and neither of these encoders currently supports 444. */
#define INTERLACED_DCT(s) ((chroma_format == CHROMA_420 || chroma_format == CHROMA_422) && \
                           (s)->c.avctx->flags & AV_CODEC_FLAG_INTERLACED_DCT)
    int16_t weight[12][64];
    int16_t orig[12][64];
    const int mb_x = s->c.mb_x;
    const int mb_y = s->c.mb_y;
    int i;
    int skip_dct[12];
    int dct_offset = s->c.linesize * 8; // default for progressive frames
    int uv_dct_offset = s->c.uvlinesize * 8;
    const uint8_t *ptr_y, *ptr_cb, *ptr_cr;
    ptrdiff_t wrap_y, wrap_c;

    for (i = 0; i < mb_block_count; i++)
        skip_dct[i] = s->skipdct;

    if (s->adaptive_quant) {
        const int last_qp = s->c.qscale;
        const int mb_xy = mb_x + mb_y * s->c.mb_stride;

        s->lambda  = s->lambda_table[mb_xy];
        s->lambda2 = (s->lambda * s->lambda + FF_LAMBDA_SCALE / 2) >>
                       FF_LAMBDA_SHIFT;

        if (!(s->mpv_flags & FF_MPV_FLAG_QP_RD)) {
            s->dquant = s->c.cur_pic.qscale_table[mb_xy] - last_qp;

            if (s->c.out_format == FMT_H263) {
                s->dquant = av_clip(s->dquant, -2, 2);

                if (s->c.codec_id == AV_CODEC_ID_MPEG4) {
                    if (!s->c.mb_intra) {
                        if (s->c.pict_type == AV_PICTURE_TYPE_B) {
                            if (s->dquant & 1 || s->c.mv_dir & MV_DIRECT)
                                s->dquant = 0;
                        }
                        if (s->c.mv_type == MV_TYPE_8X8)
                            s->dquant = 0;
                    }
                }
            }
        }
        ff_set_qscale(&s->c, last_qp + s->dquant);
    } else if (s->mpv_flags & FF_MPV_FLAG_QP_RD)
        ff_set_qscale(&s->c, s->c.qscale + s->dquant);

    wrap_y = s->c.linesize;
    wrap_c = s->c.uvlinesize;
    ptr_y  = s->new_pic->data[0] +
             (mb_y * 16 * wrap_y)              + mb_x * 16;
    ptr_cb = s->new_pic->data[1] +
             (mb_y * mb_block_height * wrap_c) + mb_x * mb_block_width;
    ptr_cr = s->new_pic->data[2] +
             (mb_y * mb_block_height * wrap_c) + mb_x * mb_block_width;

    if ((mb_x * 16 + 16 > s->c.width || mb_y * 16 + 16 > s->c.height) &&
        s->c.codec_id != AV_CODEC_ID_AMV) {
        uint8_t *ebuf = s->c.sc.edge_emu_buffer + 38 * wrap_y;
        int cw = (s->c.width  + chroma_x_shift) >> chroma_x_shift;
        int ch = (s->c.height + chroma_y_shift) >> chroma_y_shift;
        s->c.vdsp.emulated_edge_mc(ebuf, ptr_y,
                                 wrap_y, wrap_y,
                                 16, 16, mb_x * 16, mb_y * 16,
                                 s->c.width, s->c.height);
        ptr_y = ebuf;
        s->c.vdsp.emulated_edge_mc(ebuf + 16 * wrap_y, ptr_cb,
                                 wrap_c, wrap_c,
                                 mb_block_width, mb_block_height,
                                 mb_x * mb_block_width, mb_y * mb_block_height,
                                 cw, ch);
        ptr_cb = ebuf + 16 * wrap_y;
        s->c.vdsp.emulated_edge_mc(ebuf + 16 * wrap_y + 16, ptr_cr,
                                 wrap_c, wrap_c,
                                 mb_block_width, mb_block_height,
                                 mb_x * mb_block_width, mb_y * mb_block_height,
                                 cw, ch);
        ptr_cr = ebuf + 16 * wrap_y + 16;
    }

    if (s->c.mb_intra) {
        if (INTERLACED_DCT(s)) {
            int progressive_score, interlaced_score;

            s->c.interlaced_dct = 0;
            progressive_score = s->ildct_cmp[1](s, ptr_y, NULL, wrap_y, 8) +
                                s->ildct_cmp[1](s, ptr_y + wrap_y * 8,
                                                NULL, wrap_y, 8) - 400;

            if (progressive_score > 0) {
                interlaced_score = s->ildct_cmp[1](s, ptr_y,
                                                   NULL, wrap_y * 2, 8) +
                                   s->ildct_cmp[1](s, ptr_y + wrap_y,
                                                   NULL, wrap_y * 2, 8);
                if (progressive_score > interlaced_score) {
                    s->c.interlaced_dct = 1;

                    dct_offset = wrap_y;
                    uv_dct_offset = wrap_c;
                    wrap_y <<= 1;
                    if (chroma_format == CHROMA_422 ||
                        chroma_format == CHROMA_444)
                        wrap_c <<= 1;
                }
            }
        }

        s->pdsp.get_pixels(s->c.block[0], ptr_y,                  wrap_y);
        s->pdsp.get_pixels(s->c.block[1], ptr_y + 8,              wrap_y);
        s->pdsp.get_pixels(s->c.block[2], ptr_y + dct_offset,     wrap_y);
        s->pdsp.get_pixels(s->c.block[3], ptr_y + dct_offset + 8, wrap_y);

        if (s->c.avctx->flags & AV_CODEC_FLAG_GRAY) {
            skip_dct[4] = 1;
            skip_dct[5] = 1;
        } else {
            s->pdsp.get_pixels(s->c.block[4], ptr_cb, wrap_c);
            s->pdsp.get_pixels(s->c.block[5], ptr_cr, wrap_c);
            if (chroma_format == CHROMA_422) {
                s->pdsp.get_pixels(s->c.block[6], ptr_cb + uv_dct_offset, wrap_c);
                s->pdsp.get_pixels(s->c.block[7], ptr_cr + uv_dct_offset, wrap_c);
            } else if (chroma_format == CHROMA_444) {
                s->pdsp.get_pixels(s->c.block[ 6], ptr_cb + 8, wrap_c);
                s->pdsp.get_pixels(s->c.block[ 7], ptr_cr + 8, wrap_c);
                s->pdsp.get_pixels(s->c.block[ 8], ptr_cb + uv_dct_offset, wrap_c);
                s->pdsp.get_pixels(s->c.block[ 9], ptr_cr + uv_dct_offset, wrap_c);
                s->pdsp.get_pixels(s->c.block[10], ptr_cb + uv_dct_offset + 8, wrap_c);
                s->pdsp.get_pixels(s->c.block[11], ptr_cr + uv_dct_offset + 8, wrap_c);
            }
        }
    } else {
        op_pixels_func (*op_pix)[4];
        qpel_mc_func (*op_qpix)[16];
        uint8_t *dest_y, *dest_cb, *dest_cr;

        dest_y  = s->c.dest[0];
        dest_cb = s->c.dest[1];
        dest_cr = s->c.dest[2];

        if ((!s->c.no_rounding) || s->c.pict_type == AV_PICTURE_TYPE_B) {
            op_pix  = s->c.hdsp.put_pixels_tab;
            op_qpix = s->c.qdsp.put_qpel_pixels_tab;
        } else {
            op_pix  = s->c.hdsp.put_no_rnd_pixels_tab;
            op_qpix = s->c.qdsp.put_no_rnd_qpel_pixels_tab;
        }

        if (s->c.mv_dir & MV_DIR_FORWARD) {
            ff_mpv_motion(&s->c, dest_y, dest_cb, dest_cr, 0,
                          s->c.last_pic.data,
                          op_pix, op_qpix);
            op_pix  = s->c.hdsp.avg_pixels_tab;
            op_qpix = s->c.qdsp.avg_qpel_pixels_tab;
        }
        if (s->c.mv_dir & MV_DIR_BACKWARD) {
            ff_mpv_motion(&s->c, dest_y, dest_cb, dest_cr, 1,
                          s->c.next_pic.data,
                          op_pix, op_qpix);
        }

        if (INTERLACED_DCT(s)) {
            int progressive_score, interlaced_score;

            s->c.interlaced_dct = 0;
            progressive_score = s->ildct_cmp[0](s, dest_y, ptr_y, wrap_y, 8) +
                                s->ildct_cmp[0](s, dest_y + wrap_y * 8,
                                                ptr_y + wrap_y * 8,
                                                wrap_y, 8) - 400;

            if (s->c.avctx->ildct_cmp == FF_CMP_VSSE)
                progressive_score -= 400;

            if (progressive_score > 0) {
                interlaced_score = s->ildct_cmp[0](s, dest_y, ptr_y,
                                                   wrap_y * 2, 8) +
                                   s->ildct_cmp[0](s, dest_y + wrap_y,
                                                   ptr_y + wrap_y,
                                                   wrap_y * 2, 8);

                if (progressive_score > interlaced_score) {
                    s->c.interlaced_dct = 1;

                    dct_offset = wrap_y;
                    uv_dct_offset = wrap_c;
                    wrap_y <<= 1;
                    if (chroma_format == CHROMA_422)
                        wrap_c <<= 1;
                }
            }
        }

        s->pdsp.diff_pixels(s->c.block[0], ptr_y, dest_y, wrap_y);
        s->pdsp.diff_pixels(s->c.block[1], ptr_y + 8, dest_y + 8, wrap_y);
        s->pdsp.diff_pixels(s->c.block[2], ptr_y + dct_offset,
                            dest_y + dct_offset, wrap_y);
        s->pdsp.diff_pixels(s->c.block[3], ptr_y + dct_offset + 8,
                            dest_y + dct_offset + 8, wrap_y);

        if (s->c.avctx->flags & AV_CODEC_FLAG_GRAY) {
            skip_dct[4] = 1;
            skip_dct[5] = 1;
        } else {
            s->pdsp.diff_pixels(s->c.block[4], ptr_cb, dest_cb, wrap_c);
            s->pdsp.diff_pixels(s->c.block[5], ptr_cr, dest_cr, wrap_c);
            if (!chroma_y_shift) { /* 422 */
                s->pdsp.diff_pixels(s->c.block[6], ptr_cb + uv_dct_offset,
                                    dest_cb + uv_dct_offset, wrap_c);
                s->pdsp.diff_pixels(s->c.block[7], ptr_cr + uv_dct_offset,
                                    dest_cr + uv_dct_offset, wrap_c);
            }
        }
        /* pre quantization */
        if (s->mc_mb_var[s->c.mb_stride * mb_y + mb_x] < 2 * s->c.qscale * s->c.qscale) {
            // FIXME optimize
            if (s->sad_cmp[1](NULL, ptr_y, dest_y, wrap_y, 8) < 20 * s->c.qscale)
                skip_dct[0] = 1;
            if (s->sad_cmp[1](NULL, ptr_y + 8, dest_y + 8, wrap_y, 8) < 20 * s->c.qscale)
                skip_dct[1] = 1;
            if (s->sad_cmp[1](NULL, ptr_y + dct_offset, dest_y + dct_offset,
                              wrap_y, 8) < 20 * s->c.qscale)
                skip_dct[2] = 1;
            if (s->sad_cmp[1](NULL, ptr_y + dct_offset + 8, dest_y + dct_offset + 8,
                              wrap_y, 8) < 20 * s->c.qscale)
                skip_dct[3] = 1;
            if (s->sad_cmp[1](NULL, ptr_cb, dest_cb, wrap_c, 8) < 20 * s->c.qscale)
                skip_dct[4] = 1;
            if (s->sad_cmp[1](NULL, ptr_cr, dest_cr, wrap_c, 8) < 20 * s->c.qscale)
                skip_dct[5] = 1;
            if (!chroma_y_shift) { /* 422 */
                if (s->sad_cmp[1](NULL, ptr_cb + uv_dct_offset,
                                  dest_cb + uv_dct_offset,
                                  wrap_c, 8) < 20 * s->c.qscale)
                    skip_dct[6] = 1;
                if (s->sad_cmp[1](NULL, ptr_cr + uv_dct_offset,
                                  dest_cr + uv_dct_offset,
                                  wrap_c, 8) < 20 * s->c.qscale)
                    skip_dct[7] = 1;
            }
        }
    }

    if (s->quantizer_noise_shaping) {
        if (!skip_dct[0])
            get_visual_weight(weight[0], ptr_y                 , wrap_y);
        if (!skip_dct[1])
            get_visual_weight(weight[1], ptr_y              + 8, wrap_y);
        if (!skip_dct[2])
            get_visual_weight(weight[2], ptr_y + dct_offset    , wrap_y);
        if (!skip_dct[3])
            get_visual_weight(weight[3], ptr_y + dct_offset + 8, wrap_y);
        if (!skip_dct[4])
            get_visual_weight(weight[4], ptr_cb                , wrap_c);
        if (!skip_dct[5])
            get_visual_weight(weight[5], ptr_cr                , wrap_c);
        if (!chroma_y_shift) { /* 422 */
            if (!skip_dct[6])
                get_visual_weight(weight[6], ptr_cb + uv_dct_offset,
                                  wrap_c);
            if (!skip_dct[7])
                get_visual_weight(weight[7], ptr_cr + uv_dct_offset,
                                  wrap_c);
        }
        memcpy(orig[0], s->c.block[0], sizeof(int16_t) * 64 * mb_block_count);
    }

    /* DCT & quantize */
    av_assert2(s->c.out_format != FMT_MJPEG || s->c.qscale == 8);
    {
        for (i = 0; i < mb_block_count; i++) {
            if (!skip_dct[i]) {
                int overflow;
                s->c.block_last_index[i] = s->dct_quantize(s, s->c.block[i], i, s->c.qscale, &overflow);
                // FIXME we could decide to change to quantizer instead of
                // clipping
                // JS: I don't think that would be a good idea it could lower
                //     quality instead of improve it. Just INTRADC clipping
                //     deserves changes in quantizer
                if (overflow)
                    clip_coeffs(s, s->c.block[i], s->c.block_last_index[i]);
            } else
                s->c.block_last_index[i] = -1;
        }
        if (s->quantizer_noise_shaping) {
            for (i = 0; i < mb_block_count; i++) {
                if (!skip_dct[i]) {
                    s->c.block_last_index[i] =
                        dct_quantize_refine(s, s->c.block[i], weight[i],
                                            orig[i], i, s->c.qscale);
                }
            }
        }

        if (s->luma_elim_threshold && !s->c.mb_intra)
            for (i = 0; i < 4; i++)
                dct_single_coeff_elimination(s, i, s->luma_elim_threshold);
        if (s->chroma_elim_threshold && !s->c.mb_intra)
            for (i = 4; i < mb_block_count; i++)
                dct_single_coeff_elimination(s, i, s->chroma_elim_threshold);

        if (s->mpv_flags & FF_MPV_FLAG_CBP_RD) {
            for (i = 0; i < mb_block_count; i++) {
                if (s->c.block_last_index[i] == -1)
                    s->coded_score[i] = INT_MAX / 256;
            }
        }
    }

    if ((s->c.avctx->flags & AV_CODEC_FLAG_GRAY) && s->c.mb_intra) {
        s->c.block_last_index[4] =
        s->c.block_last_index[5] = 0;
        s->c.block[4][0] =
        s->c.block[5][0] = (1024 + s->c.c_dc_scale / 2) / s->c.c_dc_scale;
        if (!chroma_y_shift) { /* 422 / 444 */
            for (i=6; i<12; i++) {
                s->c.block_last_index[i] = 0;
                s->c.block[i][0] = s->c.block[4][0];
            }
        }
    }

    // non c quantize code returns incorrect block_last_index FIXME
    if (s->c.alternate_scan && s->dct_quantize != dct_quantize_c) {
        for (i = 0; i < mb_block_count; i++) {
            int j;
            if (s->c.block_last_index[i] > 0) {
                for (j = 63; j > 0; j--) {
                    if (s->c.block[i][s->c.intra_scantable.permutated[j]])
                        break;
                }
                s->c.block_last_index[i] = j;
            }
        }
    }

    s->encode_mb(s, s->c.block, motion_x, motion_y);
}

static void encode_mb(MPVEncContext *const s, int motion_x, int motion_y)
{
    if (s->c.chroma_format == CHROMA_420)
        encode_mb_internal(s, motion_x, motion_y,  8, 8, 6, 1, 1, CHROMA_420);
    else if (s->c.chroma_format == CHROMA_422)
        encode_mb_internal(s, motion_x, motion_y, 16, 8, 8, 1, 0, CHROMA_422);
    else
        encode_mb_internal(s, motion_x, motion_y, 16, 16, 12, 0, 0, CHROMA_444);
}

typedef struct MBBackup {
    struct {
        int mv[2][4][2];
        int last_mv[2][2][2];
        int mv_type, mv_dir;
        int last_dc[3];
        int mb_intra, mb_skipped, mb_skip_run;
        int qscale;
        int block_last_index[8];
        int interlaced_dct;
        int16_t (*block)[64];
    } c;
    int mv_bits, i_tex_bits, p_tex_bits, i_count, misc_bits, last_bits;
    int dquant;
    int esc3_level_length;
    PutBitContext pb, pb2, tex_pb;
} MBBackup;

#define COPY_CONTEXT(BEFORE, AFTER, DST_TYPE, SRC_TYPE)                     \
static inline void BEFORE ##_context_before_encode(DST_TYPE *const d,       \
                                                   const SRC_TYPE *const s) \
{                                                                           \
    /* FIXME is memcpy faster than a loop? */                               \
    memcpy(d->c.last_mv, s->c.last_mv, 2*2*2*sizeof(int));                  \
                                                                            \
    /* MPEG-1 */                                                            \
    d->c.mb_skip_run = s->c.mb_skip_run;                                    \
    for (int i = 0; i < 3; i++)                                             \
        d->c.last_dc[i] = s->c.last_dc[i];                                  \
                                                                            \
    /* statistics */                                                        \
    d->mv_bits    = s->mv_bits;                                             \
    d->i_tex_bits = s->i_tex_bits;                                          \
    d->p_tex_bits = s->p_tex_bits;                                          \
    d->i_count    = s->i_count;                                             \
    d->misc_bits  = s->misc_bits;                                           \
    d->last_bits  = 0;                                                      \
                                                                            \
    d->c.mb_skipped = 0;                                                    \
    d->c.qscale = s->c.qscale;                                              \
    d->dquant   = s->dquant;                                                \
                                                                            \
    d->esc3_level_length = s->esc3_level_length;                            \
}                                                                           \
                                                                            \
static inline void AFTER ## _context_after_encode(DST_TYPE *const d,        \
                                                  const SRC_TYPE *const s,  \
                                                  int data_partitioning)    \
{                                                                           \
    /* FIXME is memcpy faster than a loop? */                               \
    memcpy(d->c.mv, s->c.mv, 2*4*2*sizeof(int));                            \
    memcpy(d->c.last_mv, s->c.last_mv, 2*2*2*sizeof(int));                  \
                                                                            \
    /* MPEG-1 */                                                            \
    d->c.mb_skip_run = s->c.mb_skip_run;                                    \
    for (int i = 0; i < 3; i++)                                             \
        d->c.last_dc[i] = s->c.last_dc[i];                                  \
                                                                            \
    /* statistics */                                                        \
    d->mv_bits    = s->mv_bits;                                             \
    d->i_tex_bits = s->i_tex_bits;                                          \
    d->p_tex_bits = s->p_tex_bits;                                          \
    d->i_count    = s->i_count;                                             \
    d->misc_bits  = s->misc_bits;                                           \
                                                                            \
    d->c.mb_intra   = s->c.mb_intra;                                        \
    d->c.mb_skipped = s->c.mb_skipped;                                      \
    d->c.mv_type    = s->c.mv_type;                                         \
    d->c.mv_dir     = s->c.mv_dir;                                          \
    d->pb = s->pb;                                                          \
    if (data_partitioning) {                                                \
        d->pb2    = s->pb2;                                                 \
        d->tex_pb = s->tex_pb;                                              \
    }                                                                       \
    d->c.block = s->c.block;                                                \
    for (int i = 0; i < 8; i++)                                             \
        d->c.block_last_index[i] = s->c.block_last_index[i];                \
    d->c.interlaced_dct = s->c.interlaced_dct;                              \
    d->c.qscale = s->c.qscale;                                              \
                                                                            \
    d->esc3_level_length = s->esc3_level_length;                            \
}

COPY_CONTEXT(backup, save, MBBackup, MPVEncContext)
COPY_CONTEXT(reset, store, MPVEncContext, MBBackup)

static void encode_mb_hq(MPVEncContext *const s, MBBackup *const backup, MBBackup *const best,
                         PutBitContext pb[2], PutBitContext pb2[2], PutBitContext tex_pb[2],
                         int *dmin, int *next_block, int motion_x, int motion_y)
{
    int score;
    uint8_t *dest_backup[3];

    reset_context_before_encode(s, backup);

    s->c.block = s->c.blocks[*next_block];
    s->pb      = pb[*next_block];
    if (s->c.data_partitioning) {
        s->pb2   = pb2   [*next_block];
        s->tex_pb= tex_pb[*next_block];
    }

    if(*next_block){
        memcpy(dest_backup, s->c.dest, sizeof(s->c.dest));
        s->c.dest[0] = s->c.sc.rd_scratchpad;
        s->c.dest[1] = s->c.sc.rd_scratchpad + 16*s->c.linesize;
        s->c.dest[2] = s->c.sc.rd_scratchpad + 16*s->c.linesize + 8;
        av_assert0(s->c.linesize >= 32); //FIXME
    }

    encode_mb(s, motion_x, motion_y);

    score= put_bits_count(&s->pb);
    if (s->c.data_partitioning) {
        score+= put_bits_count(&s->pb2);
        score+= put_bits_count(&s->tex_pb);
    }

    if (s->c.avctx->mb_decision == FF_MB_DECISION_RD) {
        mpv_reconstruct_mb(s, s->c.block);

        score *= s->lambda2;
        score += sse_mb(s) << FF_LAMBDA_SHIFT;
    }

    if(*next_block){
        memcpy(s->c.dest, dest_backup, sizeof(s->c.dest));
    }

    if(score<*dmin){
        *dmin= score;
        *next_block^=1;

        save_context_after_encode(best, s, s->c.data_partitioning);
    }
}

static int sse(const MPVEncContext *const s, const uint8_t *src1, const uint8_t *src2, int w, int h, int stride)
{
    const uint32_t *sq = ff_square_tab + 256;
    int acc=0;
    int x,y;

    if(w==16 && h==16)
        return s->sse_cmp[0](NULL, src1, src2, stride, 16);
    else if(w==8 && h==8)
        return s->sse_cmp[1](NULL, src1, src2, stride, 8);

    for(y=0; y<h; y++){
        for(x=0; x<w; x++){
            acc+= sq[src1[x + y*stride] - src2[x + y*stride]];
        }
    }

    av_assert2(acc>=0);

    return acc;
}

static int sse_mb(MPVEncContext *const s)
{
    int w= 16;
    int h= 16;
    int chroma_mb_w = w >> s->c.chroma_x_shift;
    int chroma_mb_h = h >> s->c.chroma_y_shift;

    if (s->c.mb_x*16 + 16 > s->c.width ) w = s->c.width - s->c.mb_x*16;
    if (s->c.mb_y*16 + 16 > s->c.height) h = s->c.height- s->c.mb_y*16;

    if(w==16 && h==16)
        return s->n_sse_cmp[0](s, s->new_pic->data[0] + s->c.mb_x * 16 + s->c.mb_y * s->c.linesize * 16,
                               s->c.dest[0], s->c.linesize, 16) +
               s->n_sse_cmp[1](s, s->new_pic->data[1] + s->c.mb_x * chroma_mb_w + s->c.mb_y * s->c.uvlinesize * chroma_mb_h,
                               s->c.dest[1], s->c.uvlinesize, chroma_mb_h) +
               s->n_sse_cmp[1](s, s->new_pic->data[2] + s->c.mb_x * chroma_mb_w + s->c.mb_y * s->c.uvlinesize * chroma_mb_h,
                               s->c.dest[2], s->c.uvlinesize, chroma_mb_h);
    else
        return  sse(s, s->new_pic->data[0] + s->c.mb_x * 16 + s->c.mb_y * s->c.linesize * 16,
                    s->c.dest[0], w, h, s->c.linesize) +
                sse(s, s->new_pic->data[1] + s->c.mb_x * chroma_mb_w + s->c.mb_y * s->c.uvlinesize * chroma_mb_h,
                    s->c.dest[1], w >> s->c.chroma_x_shift, h >> s->c.chroma_y_shift, s->c.uvlinesize) +
                sse(s, s->new_pic->data[2] + s->c.mb_x * chroma_mb_w + s->c.mb_y * s->c.uvlinesize * chroma_mb_h,
                    s->c.dest[2], w >> s->c.chroma_x_shift, h >> s->c.chroma_y_shift, s->c.uvlinesize);
}

static int pre_estimate_motion_thread(AVCodecContext *c, void *arg){
    MPVEncContext *const s = *(void**)arg;


    s->me.pre_pass = 1;
    s->me.dia_size = s->c.avctx->pre_dia_size;
    s->c.first_slice_line = 1;
    for (s->c.mb_y = s->c.end_mb_y - 1; s->c.mb_y >= s->c.start_mb_y; s->c.mb_y--) {
        for (s->c.mb_x = s->c.mb_width - 1; s->c.mb_x >=0 ; s->c.mb_x--)
            ff_pre_estimate_p_frame_motion(s, s->c.mb_x, s->c.mb_y);
        s->c.first_slice_line = 0;
    }

    s->me.pre_pass = 0;

    return 0;
}

static int estimate_motion_thread(AVCodecContext *c, void *arg){
    MPVEncContext *const s = *(void**)arg;

    s->me.dia_size = s->c.avctx->dia_size;
    s->c.first_slice_line = 1;
    for (s->c.mb_y = s->c.start_mb_y; s->c.mb_y < s->c.end_mb_y; s->c.mb_y++) {
        s->c.mb_x = 0; //for block init below
        ff_init_block_index(&s->c);
        for (s->c.mb_x = 0; s->c.mb_x < s->c.mb_width; s->c.mb_x++) {
            s->c.block_index[0] += 2;
            s->c.block_index[1] += 2;
            s->c.block_index[2] += 2;
            s->c.block_index[3] += 2;

            /* compute motion vector & mb_type and store in context */
            if (s->c.pict_type == AV_PICTURE_TYPE_B)
                ff_estimate_b_frame_motion(s, s->c.mb_x, s->c.mb_y);
            else
                ff_estimate_p_frame_motion(s, s->c.mb_x, s->c.mb_y);
        }
        s->c.first_slice_line = 0;
    }
    return 0;
}

static int mb_var_thread(AVCodecContext *c, void *arg){
    MPVEncContext *const s = *(void**)arg;

    for (int mb_y = s->c.start_mb_y; mb_y < s->c.end_mb_y; mb_y++) {
        for (int mb_x = 0; mb_x < s->c.mb_width; mb_x++) {
            int xx = mb_x * 16;
            int yy = mb_y * 16;
            const uint8_t *pix = s->new_pic->data[0] + (yy * s->c.linesize) + xx;
            int varc;
            int sum = s->mpvencdsp.pix_sum(pix, s->c.linesize);

            varc = (s->mpvencdsp.pix_norm1(pix, s->c.linesize) -
                    (((unsigned) sum * sum) >> 8) + 500 + 128) >> 8;

            s->mb_var [s->c.mb_stride * mb_y + mb_x] = varc;
            s->mb_mean[s->c.mb_stride * mb_y + mb_x] = (sum+128)>>8;
            s->me.mb_var_sum_temp    += varc;
        }
    }
    return 0;
}

static void write_slice_end(MPVEncContext *const s)
{
    if (CONFIG_MPEG4_ENCODER && s->c.codec_id == AV_CODEC_ID_MPEG4) {
        if (s->c.partitioned_frame)
            ff_mpeg4_merge_partitions(s);

        ff_mpeg4_stuffing(&s->pb);
    } else if ((CONFIG_MJPEG_ENCODER || CONFIG_AMV_ENCODER) &&
               s->c.out_format == FMT_MJPEG) {
        ff_mjpeg_encode_stuffing(s);
    } else if (CONFIG_SPEEDHQ_ENCODER && s->c.out_format == FMT_SPEEDHQ) {
        ff_speedhq_end_slice(s);
    }

    flush_put_bits(&s->pb);

    if ((s->c.avctx->flags & AV_CODEC_FLAG_PASS1) && !s->c.partitioned_frame)
        s->misc_bits+= get_bits_diff(s);
}

static void write_mb_info(MPVEncContext *const s)
{
    uint8_t *ptr = s->mb_info_ptr + s->mb_info_size - 12;
    int offset = put_bits_count(&s->pb);
    int mba  = s->c.mb_x + s->c.mb_width * (s->c.mb_y % s->c.gob_index);
    int gobn = s->c.mb_y / s->c.gob_index;
    int pred_x, pred_y;
    if (CONFIG_H263_ENCODER)
        ff_h263_pred_motion(&s->c, 0, 0, &pred_x, &pred_y);
    bytestream_put_le32(&ptr, offset);
    bytestream_put_byte(&ptr, s->c.qscale);
    bytestream_put_byte(&ptr, gobn);
    bytestream_put_le16(&ptr, mba);
    bytestream_put_byte(&ptr, pred_x); /* hmv1 */
    bytestream_put_byte(&ptr, pred_y); /* vmv1 */
    /* 4MV not implemented */
    bytestream_put_byte(&ptr, 0); /* hmv2 */
    bytestream_put_byte(&ptr, 0); /* vmv2 */
}

static void update_mb_info(MPVEncContext *const s, int startcode)
{
    if (!s->mb_info)
        return;
    if (put_bytes_count(&s->pb, 0) - s->prev_mb_info >= s->mb_info) {
        s->mb_info_size += 12;
        s->prev_mb_info = s->last_mb_info;
    }
    if (startcode) {
        s->prev_mb_info = put_bytes_count(&s->pb, 0);
        /* This might have incremented mb_info_size above, and we return without
         * actually writing any info into that slot yet. But in that case,
         * this will be called again at the start of the after writing the
         * start code, actually writing the mb info. */
        return;
    }

    s->last_mb_info = put_bytes_count(&s->pb, 0);
    if (!s->mb_info_size)
        s->mb_info_size += 12;
    write_mb_info(s);
}

int ff_mpv_reallocate_putbitbuffer(MPVEncContext *const s, size_t threshold, size_t size_increase)
{
    if (put_bytes_left(&s->pb, 0) < threshold
        && s->c.slice_context_count == 1
        && s->pb.buf == s->c.avctx->internal->byte_buffer) {
        int lastgob_pos = s->ptr_lastgob - s->pb.buf;

        uint8_t *new_buffer = NULL;
        int new_buffer_size = 0;

        if ((s->c.avctx->internal->byte_buffer_size + size_increase) >= INT_MAX/8) {
            av_log(s->c.avctx, AV_LOG_ERROR, "Cannot reallocate putbit buffer\n");
            return AVERROR(ENOMEM);
        }

        emms_c();

        av_fast_padded_malloc(&new_buffer, &new_buffer_size,
                              s->c.avctx->internal->byte_buffer_size + size_increase);
        if (!new_buffer)
            return AVERROR(ENOMEM);

        memcpy(new_buffer, s->c.avctx->internal->byte_buffer, s->c.avctx->internal->byte_buffer_size);
        av_free(s->c.avctx->internal->byte_buffer);
        s->c.avctx->internal->byte_buffer      = new_buffer;
        s->c.avctx->internal->byte_buffer_size = new_buffer_size;
        rebase_put_bits(&s->pb, new_buffer, new_buffer_size);
        s->ptr_lastgob   = s->pb.buf + lastgob_pos;
    }
    if (put_bytes_left(&s->pb, 0) < threshold)
        return AVERROR(EINVAL);
    return 0;
}

static int encode_thread(AVCodecContext *c, void *arg){
    MPVEncContext *const s = *(void**)arg;
    int chr_h = 16 >> s->c.chroma_y_shift;
    int i;
    MBBackup best_s = { 0 }, backup_s;
    uint8_t bit_buf[2][MAX_MB_BYTES];
    // + 2 because ff_copy_bits() overreads
    uint8_t bit_buf2[2][MAX_PB2_MB_SIZE + 2];
    uint8_t bit_buf_tex[2][MAX_AC_TEX_MB_SIZE + 2];
    PutBitContext pb[2], pb2[2], tex_pb[2];

    for(i=0; i<2; i++){
        init_put_bits(&pb    [i], bit_buf    [i], MAX_MB_BYTES);
        init_put_bits(&pb2   [i], bit_buf2   [i], MAX_PB2_MB_SIZE);
        init_put_bits(&tex_pb[i], bit_buf_tex[i], MAX_AC_TEX_MB_SIZE);
    }

    s->last_bits= put_bits_count(&s->pb);
    s->mv_bits=0;
    s->misc_bits=0;
    s->i_tex_bits=0;
    s->p_tex_bits=0;
    s->i_count=0;

    for(i=0; i<3; i++){
        /* init last dc values */
        /* note: quant matrix value (8) is implied here */
        s->c.last_dc[i] = 128 << s->c.intra_dc_precision;

        s->encoding_error[i] = 0;
    }
    if (s->c.codec_id == AV_CODEC_ID_AMV) {
        s->c.last_dc[0] = 128 * 8 / 13;
        s->c.last_dc[1] = 128 * 8 / 14;
        s->c.last_dc[2] = 128 * 8 / 14;
#if CONFIG_MPEG4_ENCODER
    } else if (s->c.partitioned_frame) {
        av_assert1(s->c.codec_id == AV_CODEC_ID_MPEG4);
        ff_mpeg4_init_partitions(s);
#endif
    }
    s->c.mb_skip_run = 0;
    memset(s->c.last_mv, 0, sizeof(s->c.last_mv));

    s->last_mv_dir = 0;

    s->c.resync_mb_x = 0;
    s->c.resync_mb_y = 0;
    s->c.first_slice_line = 1;
    s->ptr_lastgob = s->pb.buf;
    for (int mb_y_order = s->c.start_mb_y; mb_y_order < s->c.end_mb_y; mb_y_order++) {
        int mb_y;
        if (CONFIG_SPEEDHQ_ENCODER && s->c.codec_id == AV_CODEC_ID_SPEEDHQ) {
            int first_in_slice;
            mb_y = ff_speedhq_mb_y_order_to_mb(mb_y_order, s->c.mb_height, &first_in_slice);
            if (first_in_slice && mb_y_order != s->c.start_mb_y)
                ff_speedhq_end_slice(s);
            s->c.last_dc[0] = s->c.last_dc[1] = s->c.last_dc[2] = 1024 << s->c.intra_dc_precision;
        } else {
            mb_y = mb_y_order;
        }
        s->c.mb_x = 0;
        s->c.mb_y = mb_y;

        ff_set_qscale(&s->c, s->c.qscale);
        ff_init_block_index(&s->c);

        for (int mb_x = 0; mb_x < s->c.mb_width; mb_x++) {
            int mb_type, xy;
//            int d;
            int dmin= INT_MAX;
            int dir;
            int size_increase =  s->c.avctx->internal->byte_buffer_size/4
                               + s->c.mb_width*MAX_MB_BYTES;

            ff_mpv_reallocate_putbitbuffer(s, MAX_MB_BYTES, size_increase);
            if (put_bytes_left(&s->pb, 0) < MAX_MB_BYTES){
                av_log(s->c.avctx, AV_LOG_ERROR, "encoded frame too large\n");
                return -1;
            }
            if (s->c.data_partitioning) {
                if (put_bytes_left(&s->pb2,    0) < MAX_MB_BYTES ||
                    put_bytes_left(&s->tex_pb, 0) < MAX_MB_BYTES) {
                    av_log(s->c.avctx, AV_LOG_ERROR, "encoded partitioned frame too large\n");
                    return -1;
                }
            }

            s->c.mb_x = mb_x;
            s->c.mb_y = mb_y;  // moved into loop, can get changed by H.261
            ff_update_block_index(&s->c, 8, 0, s->c.chroma_x_shift);

            if (CONFIG_H261_ENCODER && s->c.codec_id == AV_CODEC_ID_H261)
                ff_h261_reorder_mb_index(s);
            xy      = s->c.mb_y * s->c.mb_stride + s->c.mb_x;
            mb_type = s->mb_type[xy];

            /* write gob / video packet header  */
            if(s->rtp_mode){
                int current_packet_size, is_gob_start;

                current_packet_size = put_bytes_count(&s->pb, 1)
                                      - (s->ptr_lastgob - s->pb.buf);

                is_gob_start = s->rtp_payload_size &&
                               current_packet_size >= s->rtp_payload_size &&
                               mb_y + mb_x > 0;

                if (s->c.start_mb_y == mb_y && mb_y > 0 && mb_x == 0) is_gob_start = 1;

                switch (s->c.codec_id) {
                case AV_CODEC_ID_H263:
                case AV_CODEC_ID_H263P:
                    if (!s->c.h263_slice_structured)
                        if (s->c.mb_x || s->c.mb_y % s->c.gob_index) is_gob_start = 0;
                    break;
                case AV_CODEC_ID_MPEG2VIDEO:
                    if (s->c.mb_x == 0 && s->c.mb_y != 0) is_gob_start = 1;
                case AV_CODEC_ID_MPEG1VIDEO:
                    if (s->c.codec_id == AV_CODEC_ID_MPEG1VIDEO && s->c.mb_y >= 175 ||
                        s->c.mb_skip_run)
                        is_gob_start=0;
                    break;
                case AV_CODEC_ID_MJPEG:
                    if (s->c.mb_x == 0 && s->c.mb_y != 0) is_gob_start = 1;
                    break;
                }

                if(is_gob_start){
                    if (s->c.start_mb_y != mb_y || mb_x != 0) {
                        write_slice_end(s);

                        if (CONFIG_MPEG4_ENCODER && s->c.codec_id == AV_CODEC_ID_MPEG4 && s->c.partitioned_frame)
                            ff_mpeg4_init_partitions(s);
                    }

                    av_assert2((put_bits_count(&s->pb)&7) == 0);
                    current_packet_size= put_bits_ptr(&s->pb) - s->ptr_lastgob;

                    if (s->error_rate && s->c.resync_mb_x + s->c.resync_mb_y > 0) {
                        int r = put_bytes_count(&s->pb, 0) + s->c.picture_number + 16 + s->c.mb_x + s->c.mb_y;
                        int d = 100 / s->error_rate;
                        if(r % d == 0){
                            current_packet_size=0;
                            s->pb.buf_ptr= s->ptr_lastgob;
                            av_assert1(put_bits_ptr(&s->pb) == s->ptr_lastgob);
                        }
                    }

                    switch (s->c.codec_id) {
                    case AV_CODEC_ID_MPEG4:
                        if (CONFIG_MPEG4_ENCODER) {
                            ff_mpeg4_encode_video_packet_header(s);
                            ff_mpeg4_clean_buffers(&s->c);
                            ff_h263_mpeg4_reset_dc(s);
                        }
                    break;
                    case AV_CODEC_ID_MPEG1VIDEO:
                    case AV_CODEC_ID_MPEG2VIDEO:
                        if (CONFIG_MPEG1VIDEO_ENCODER || CONFIG_MPEG2VIDEO_ENCODER) {
                            ff_mpeg1_encode_slice_header(s);
                            ff_mpeg1_clean_buffers(&s->c);
                        }
                    break;
#if CONFIG_H263P_ENCODER
                    case AV_CODEC_ID_H263P:
                        if (s->c.dc_val)
                            ff_h263_mpeg4_reset_dc(s);
                        // fallthrough
#endif
                    case AV_CODEC_ID_H263:
                        if (CONFIG_H263_ENCODER) {
                            update_mb_info(s, 1);
                            ff_h263_encode_gob_header(s, mb_y);
                        }
                    break;
                    }

                    if (s->c.avctx->flags & AV_CODEC_FLAG_PASS1) {
                        int bits= put_bits_count(&s->pb);
                        s->misc_bits+= bits - s->last_bits;
                        s->last_bits= bits;
                    }

                    s->ptr_lastgob       += current_packet_size;
                    s->c.first_slice_line = 1;
                    s->c.resync_mb_x      = mb_x;
                    s->c.resync_mb_y      = mb_y;
                }
            }

            if (s->c.resync_mb_x   == s->c.mb_x &&
                s->c.resync_mb_y+1 == s->c.mb_y)
                s->c.first_slice_line = 0;

            s->c.mb_skipped = 0;
            s->dquant=0; //only for QP_RD

            update_mb_info(s, 0);

            if (mb_type & (mb_type-1) || (s->mpv_flags & FF_MPV_FLAG_QP_RD)) { // more than 1 MB type possible or FF_MPV_FLAG_QP_RD
                int next_block=0;
                int pb_bits_count, pb2_bits_count, tex_pb_bits_count;

                backup_context_before_encode(&backup_s, s);
                backup_s.pb= s->pb;
                if (s->c.data_partitioning) {
                    backup_s.pb2= s->pb2;
                    backup_s.tex_pb= s->tex_pb;
                }

                if(mb_type&CANDIDATE_MB_TYPE_INTER){
                    s->c.mv_dir      = MV_DIR_FORWARD;
                    s->c.mv_type     = MV_TYPE_16X16;
                    s->c.mb_intra    = 0;
                    s->c.mv[0][0][0] = s->p_mv_table[xy][0];
                    s->c.mv[0][0][1] = s->p_mv_table[xy][1];
                    encode_mb_hq(s, &backup_s, &best_s, pb, pb2, tex_pb,
                                 &dmin, &next_block, s->c.mv[0][0][0], s->c.mv[0][0][1]);
                }
                if(mb_type&CANDIDATE_MB_TYPE_INTER_I){
                    s->c.mv_dir   = MV_DIR_FORWARD;
                    s->c.mv_type  = MV_TYPE_FIELD;
                    s->c.mb_intra = 0;
                    for(i=0; i<2; i++){
                        int j = s->c.field_select[0][i] = s->p_field_select_table[i][xy];
                        s->c.mv[0][i][0] = s->c.p_field_mv_table[i][j][xy][0];
                        s->c.mv[0][i][1] = s->c.p_field_mv_table[i][j][xy][1];
                    }
                    encode_mb_hq(s, &backup_s, &best_s, pb, pb2, tex_pb,
                                 &dmin, &next_block, 0, 0);
                }
                if(mb_type&CANDIDATE_MB_TYPE_SKIPPED){
                    s->c.mv_dir      = MV_DIR_FORWARD;
                    s->c.mv_type     = MV_TYPE_16X16;
                    s->c.mb_intra    = 0;
                    s->c.mv[0][0][0] = 0;
                    s->c.mv[0][0][1] = 0;
                    encode_mb_hq(s, &backup_s, &best_s, pb, pb2, tex_pb,
                                 &dmin, &next_block, s->c.mv[0][0][0], s->c.mv[0][0][1]);
                }
                if(mb_type&CANDIDATE_MB_TYPE_INTER4V){
                    s->c.mv_dir   = MV_DIR_FORWARD;
                    s->c.mv_type  = MV_TYPE_8X8;
                    s->c.mb_intra = 0;
                    for(i=0; i<4; i++){
                        s->c.mv[0][i][0] = s->c.cur_pic.motion_val[0][s->c.block_index[i]][0];
                        s->c.mv[0][i][1] = s->c.cur_pic.motion_val[0][s->c.block_index[i]][1];
                    }
                    encode_mb_hq(s, &backup_s, &best_s, pb, pb2, tex_pb,
                                 &dmin, &next_block, 0, 0);
                }
                if(mb_type&CANDIDATE_MB_TYPE_FORWARD){
                    s->c.mv_dir      = MV_DIR_FORWARD;
                    s->c.mv_type     = MV_TYPE_16X16;
                    s->c.mb_intra    = 0;
                    s->c.mv[0][0][0] = s->b_forw_mv_table[xy][0];
                    s->c.mv[0][0][1] = s->b_forw_mv_table[xy][1];
                    encode_mb_hq(s, &backup_s, &best_s, pb, pb2, tex_pb,
                                 &dmin, &next_block, s->c.mv[0][0][0], s->c.mv[0][0][1]);
                }
                if(mb_type&CANDIDATE_MB_TYPE_BACKWARD){
                    s->c.mv_dir      = MV_DIR_BACKWARD;
                    s->c.mv_type     = MV_TYPE_16X16;
                    s->c.mb_intra    = 0;
                    s->c.mv[1][0][0] = s->b_back_mv_table[xy][0];
                    s->c.mv[1][0][1] = s->b_back_mv_table[xy][1];
                    encode_mb_hq(s, &backup_s, &best_s, pb, pb2, tex_pb,
                                 &dmin, &next_block, s->c.mv[1][0][0], s->c.mv[1][0][1]);
                }
                if(mb_type&CANDIDATE_MB_TYPE_BIDIR){
                    s->c.mv_dir = MV_DIR_FORWARD | MV_DIR_BACKWARD;
                    s->c.mv_type = MV_TYPE_16X16;
                    s->c.mb_intra = 0;
                    s->c.mv[0][0][0] = s->b_bidir_forw_mv_table[xy][0];
                    s->c.mv[0][0][1] = s->b_bidir_forw_mv_table[xy][1];
                    s->c.mv[1][0][0] = s->b_bidir_back_mv_table[xy][0];
                    s->c.mv[1][0][1] = s->b_bidir_back_mv_table[xy][1];
                    encode_mb_hq(s, &backup_s, &best_s, pb, pb2, tex_pb,
                                 &dmin, &next_block, 0, 0);
                }
                if(mb_type&CANDIDATE_MB_TYPE_FORWARD_I){
                    s->c.mv_dir = MV_DIR_FORWARD;
                    s->c.mv_type = MV_TYPE_FIELD;
                    s->c.mb_intra = 0;
                    for(i=0; i<2; i++){
                        int j = s->c.field_select[0][i] = s->b_field_select_table[0][i][xy];
                        s->c.mv[0][i][0] = s->b_field_mv_table[0][i][j][xy][0];
                        s->c.mv[0][i][1] = s->b_field_mv_table[0][i][j][xy][1];
                    }
                    encode_mb_hq(s, &backup_s, &best_s, pb, pb2, tex_pb,
                                 &dmin, &next_block, 0, 0);
                }
                if(mb_type&CANDIDATE_MB_TYPE_BACKWARD_I){
                    s->c.mv_dir = MV_DIR_BACKWARD;
                    s->c.mv_type = MV_TYPE_FIELD;
                    s->c.mb_intra = 0;
                    for(i=0; i<2; i++){
                        int j = s->c.field_select[1][i] = s->b_field_select_table[1][i][xy];
                        s->c.mv[1][i][0] = s->b_field_mv_table[1][i][j][xy][0];
                        s->c.mv[1][i][1] = s->b_field_mv_table[1][i][j][xy][1];
                    }
                    encode_mb_hq(s, &backup_s, &best_s, pb, pb2, tex_pb,
                                 &dmin, &next_block, 0, 0);
                }
                if(mb_type&CANDIDATE_MB_TYPE_BIDIR_I){
                    s->c.mv_dir   = MV_DIR_FORWARD | MV_DIR_BACKWARD;
                    s->c.mv_type  = MV_TYPE_FIELD;
                    s->c.mb_intra = 0;
                    for(dir=0; dir<2; dir++){
                        for(i=0; i<2; i++){
                            int j = s->c.field_select[dir][i] = s->b_field_select_table[dir][i][xy];
                            s->c.mv[dir][i][0] = s->b_field_mv_table[dir][i][j][xy][0];
                            s->c.mv[dir][i][1] = s->b_field_mv_table[dir][i][j][xy][1];
                        }
                    }
                    encode_mb_hq(s, &backup_s, &best_s, pb, pb2, tex_pb,
                                 &dmin, &next_block, 0, 0);
                }
                if(mb_type&CANDIDATE_MB_TYPE_INTRA){
                    s->c.mv_dir      = 0;
                    s->c.mv_type     = MV_TYPE_16X16;
                    s->c.mb_intra    = 1;
                    s->c.mv[0][0][0] = 0;
                    s->c.mv[0][0][1] = 0;
                    encode_mb_hq(s, &backup_s, &best_s, pb, pb2, tex_pb,
                                 &dmin, &next_block, 0, 0);
                    s->c.mbintra_table[xy] = 1;
                }

                if ((s->mpv_flags & FF_MPV_FLAG_QP_RD) && dmin < INT_MAX) {
                    if (best_s.c.mv_type == MV_TYPE_16X16) { //FIXME move 4mv after QPRD
                        const int last_qp = backup_s.c.qscale;
                        int qpi, qp, dc[6];
                        int16_t ac[6][16];
                        const int mvdir = (best_s.c.mv_dir & MV_DIR_BACKWARD) ? 1 : 0;
                        static const int dquant_tab[4]={-1,1,-2,2};
                        int storecoefs = s->c.mb_intra && s->c.dc_val;

                        av_assert2(backup_s.dquant == 0);

                        //FIXME intra
                        s->c.mv_dir   = best_s.c.mv_dir;
                        s->c.mv_type  = MV_TYPE_16X16;
                        s->c.mb_intra = best_s.c.mb_intra;
                        s->c.mv[0][0][0] = best_s.c.mv[0][0][0];
                        s->c.mv[0][0][1] = best_s.c.mv[0][0][1];
                        s->c.mv[1][0][0] = best_s.c.mv[1][0][0];
                        s->c.mv[1][0][1] = best_s.c.mv[1][0][1];

                        qpi = s->c.pict_type == AV_PICTURE_TYPE_B ? 2 : 0;
                        for(; qpi<4; qpi++){
                            int dquant= dquant_tab[qpi];
                            qp= last_qp + dquant;
                            if (qp < s->c.avctx->qmin || qp > s->c.avctx->qmax)
                                continue;
                            backup_s.dquant= dquant;
                            if(storecoefs){
                                for(i=0; i<6; i++){
                                    dc[i] = s->c.dc_val[s->c.block_index[i]];
                                    memcpy(ac[i], s->c.ac_val[s->c.block_index[i]], sizeof(*s->c.ac_val));
                                }
                            }

                            encode_mb_hq(s, &backup_s, &best_s, pb, pb2, tex_pb,
                                         &dmin, &next_block, s->c.mv[mvdir][0][0], s->c.mv[mvdir][0][1]);
                            if (best_s.c.qscale != qp) {
                                if(storecoefs){
                                    for(i=0; i<6; i++){
                                        s->c.dc_val[s->c.block_index[i]] = dc[i];
                                        memcpy(s->c.ac_val[s->c.block_index[i]], ac[i], sizeof(*s->c.ac_val));
                                    }
                                }
                            }
                        }
                    }
                }
                if(CONFIG_MPEG4_ENCODER && mb_type&CANDIDATE_MB_TYPE_DIRECT){
                    int mx= s->b_direct_mv_table[xy][0];
                    int my= s->b_direct_mv_table[xy][1];

                    backup_s.dquant = 0;
                    s->c.mv_dir     = MV_DIR_FORWARD | MV_DIR_BACKWARD | MV_DIRECT;
                    s->c.mb_intra   = 0;
                    ff_mpeg4_set_direct_mv(&s->c, mx, my);
                    encode_mb_hq(s, &backup_s, &best_s, pb, pb2, tex_pb,
                                 &dmin, &next_block, mx, my);
                }
                if(CONFIG_MPEG4_ENCODER && mb_type&CANDIDATE_MB_TYPE_DIRECT0){
                    backup_s.dquant = 0;
                    s->c.mv_dir   = MV_DIR_FORWARD | MV_DIR_BACKWARD | MV_DIRECT;
                    s->c.mb_intra = 0;
                    ff_mpeg4_set_direct_mv(&s->c, 0, 0);
                    encode_mb_hq(s, &backup_s, &best_s, pb, pb2, tex_pb,
                                 &dmin, &next_block, 0, 0);
                }
                if (!best_s.c.mb_intra && s->mpv_flags & FF_MPV_FLAG_SKIP_RD) {
                    int coded=0;
                    for(i=0; i<6; i++)
                        coded |= s->c.block_last_index[i];
                    if(coded){
                        int mx,my;
                        memcpy(s->c.mv, best_s.c.mv, sizeof(s->c.mv));
                        if (CONFIG_MPEG4_ENCODER && best_s.c.mv_dir & MV_DIRECT) {
                            mx=my=0; //FIXME find the one we actually used
                            ff_mpeg4_set_direct_mv(&s->c, mx, my);
                        } else if (best_s.c.mv_dir & MV_DIR_BACKWARD) {
                            mx = s->c.mv[1][0][0];
                            my = s->c.mv[1][0][1];
                        }else{
                            mx = s->c.mv[0][0][0];
                            my = s->c.mv[0][0][1];
                        }

                        s->c.mv_dir   = best_s.c.mv_dir;
                        s->c.mv_type  = best_s.c.mv_type;
                        s->c.mb_intra = 0;
/*                        s->c.mv[0][0][0] = best_s.mv[0][0][0];
                        s->c.mv[0][0][1] = best_s.mv[0][0][1];
                        s->c.mv[1][0][0] = best_s.mv[1][0][0];
                        s->c.mv[1][0][1] = best_s.mv[1][0][1];*/
                        backup_s.dquant= 0;
                        s->skipdct=1;
                        encode_mb_hq(s, &backup_s, &best_s, pb, pb2, tex_pb,
                                        &dmin, &next_block, mx, my);
                        s->skipdct=0;
                    }
                }

                store_context_after_encode(s, &best_s, s->c.data_partitioning);

                pb_bits_count= put_bits_count(&s->pb);
                flush_put_bits(&s->pb);
                ff_copy_bits(&backup_s.pb, bit_buf[next_block^1], pb_bits_count);
                s->pb= backup_s.pb;

                if (s->c.data_partitioning) {
                    pb2_bits_count= put_bits_count(&s->pb2);
                    flush_put_bits(&s->pb2);
                    ff_copy_bits(&backup_s.pb2, bit_buf2[next_block^1], pb2_bits_count);
                    s->pb2= backup_s.pb2;

                    tex_pb_bits_count= put_bits_count(&s->tex_pb);
                    flush_put_bits(&s->tex_pb);
                    ff_copy_bits(&backup_s.tex_pb, bit_buf_tex[next_block^1], tex_pb_bits_count);
                    s->tex_pb= backup_s.tex_pb;
                }
                s->last_bits= put_bits_count(&s->pb);

                if (CONFIG_H263_ENCODER &&
                    s->c.out_format == FMT_H263 && s->c.pict_type != AV_PICTURE_TYPE_B)
                    ff_h263_update_mb(s);

                if(next_block==0){ //FIXME 16 vs linesize16
                    s->c.hdsp.put_pixels_tab[0][0](s->c.dest[0], s->c.sc.rd_scratchpad                     , s->c.linesize  ,16);
                    s->c.hdsp.put_pixels_tab[1][0](s->c.dest[1], s->c.sc.rd_scratchpad + 16*s->c.linesize    , s->c.uvlinesize, 8);
                    s->c.hdsp.put_pixels_tab[1][0](s->c.dest[2], s->c.sc.rd_scratchpad + 16*s->c.linesize + 8, s->c.uvlinesize, 8);
                }

                if (s->c.avctx->mb_decision == FF_MB_DECISION_BITS)
                    mpv_reconstruct_mb(s, s->c.block);
            } else {
                int motion_x = 0, motion_y = 0;
                s->c.mv_type = MV_TYPE_16X16;
                // only one MB-Type possible

                switch(mb_type){
                case CANDIDATE_MB_TYPE_INTRA:
                    s->c.mv_dir = 0;
                    s->c.mb_intra = 1;
                    motion_x= s->c.mv[0][0][0] = 0;
                    motion_y= s->c.mv[0][0][1] = 0;
                    s->c.mbintra_table[xy] = 1;
                    break;
                case CANDIDATE_MB_TYPE_INTER:
                    s->c.mv_dir = MV_DIR_FORWARD;
                    s->c.mb_intra = 0;
                    motion_x= s->c.mv[0][0][0] = s->p_mv_table[xy][0];
                    motion_y= s->c.mv[0][0][1] = s->p_mv_table[xy][1];
                    break;
                case CANDIDATE_MB_TYPE_INTER_I:
                    s->c.mv_dir = MV_DIR_FORWARD;
                    s->c.mv_type = MV_TYPE_FIELD;
                    s->c.mb_intra = 0;
                    for(i=0; i<2; i++){
                        int j = s->c.field_select[0][i] = s->p_field_select_table[i][xy];
                        s->c.mv[0][i][0] = s->c.p_field_mv_table[i][j][xy][0];
                        s->c.mv[0][i][1] = s->c.p_field_mv_table[i][j][xy][1];
                    }
                    break;
                case CANDIDATE_MB_TYPE_INTER4V:
                    s->c.mv_dir = MV_DIR_FORWARD;
                    s->c.mv_type = MV_TYPE_8X8;
                    s->c.mb_intra = 0;
                    for(i=0; i<4; i++){
                        s->c.mv[0][i][0] = s->c.cur_pic.motion_val[0][s->c.block_index[i]][0];
                        s->c.mv[0][i][1] = s->c.cur_pic.motion_val[0][s->c.block_index[i]][1];
                    }
                    break;
                case CANDIDATE_MB_TYPE_DIRECT:
                    if (CONFIG_MPEG4_ENCODER) {
                        s->c.mv_dir = MV_DIR_FORWARD|MV_DIR_BACKWARD|MV_DIRECT;
                        s->c.mb_intra = 0;
                        motion_x=s->b_direct_mv_table[xy][0];
                        motion_y=s->b_direct_mv_table[xy][1];
                        ff_mpeg4_set_direct_mv(&s->c, motion_x, motion_y);
                    }
                    break;
                case CANDIDATE_MB_TYPE_DIRECT0:
                    if (CONFIG_MPEG4_ENCODER) {
                        s->c.mv_dir = MV_DIR_FORWARD|MV_DIR_BACKWARD|MV_DIRECT;
                        s->c.mb_intra = 0;
                        ff_mpeg4_set_direct_mv(&s->c, 0, 0);
                    }
                    break;
                case CANDIDATE_MB_TYPE_BIDIR:
                    s->c.mv_dir = MV_DIR_FORWARD | MV_DIR_BACKWARD;
                    s->c.mb_intra = 0;
                    s->c.mv[0][0][0] = s->b_bidir_forw_mv_table[xy][0];
                    s->c.mv[0][0][1] = s->b_bidir_forw_mv_table[xy][1];
                    s->c.mv[1][0][0] = s->b_bidir_back_mv_table[xy][0];
                    s->c.mv[1][0][1] = s->b_bidir_back_mv_table[xy][1];
                    break;
                case CANDIDATE_MB_TYPE_BACKWARD:
                    s->c.mv_dir = MV_DIR_BACKWARD;
                    s->c.mb_intra = 0;
                    motion_x= s->c.mv[1][0][0] = s->b_back_mv_table[xy][0];
                    motion_y= s->c.mv[1][0][1] = s->b_back_mv_table[xy][1];
                    break;
                case CANDIDATE_MB_TYPE_FORWARD:
                    s->c.mv_dir = MV_DIR_FORWARD;
                    s->c.mb_intra = 0;
                    motion_x= s->c.mv[0][0][0] = s->b_forw_mv_table[xy][0];
                    motion_y= s->c.mv[0][0][1] = s->b_forw_mv_table[xy][1];
                    break;
                case CANDIDATE_MB_TYPE_FORWARD_I:
                    s->c.mv_dir = MV_DIR_FORWARD;
                    s->c.mv_type = MV_TYPE_FIELD;
                    s->c.mb_intra = 0;
                    for(i=0; i<2; i++){
                        int j = s->c.field_select[0][i] = s->b_field_select_table[0][i][xy];
                        s->c.mv[0][i][0] = s->b_field_mv_table[0][i][j][xy][0];
                        s->c.mv[0][i][1] = s->b_field_mv_table[0][i][j][xy][1];
                    }
                    break;
                case CANDIDATE_MB_TYPE_BACKWARD_I:
                    s->c.mv_dir = MV_DIR_BACKWARD;
                    s->c.mv_type = MV_TYPE_FIELD;
                    s->c.mb_intra = 0;
                    for(i=0; i<2; i++){
                        int j = s->c.field_select[1][i] = s->b_field_select_table[1][i][xy];
                        s->c.mv[1][i][0] = s->b_field_mv_table[1][i][j][xy][0];
                        s->c.mv[1][i][1] = s->b_field_mv_table[1][i][j][xy][1];
                    }
                    break;
                case CANDIDATE_MB_TYPE_BIDIR_I:
                    s->c.mv_dir = MV_DIR_FORWARD | MV_DIR_BACKWARD;
                    s->c.mv_type = MV_TYPE_FIELD;
                    s->c.mb_intra = 0;
                    for(dir=0; dir<2; dir++){
                        for(i=0; i<2; i++){
                            int j = s->c.field_select[dir][i] = s->b_field_select_table[dir][i][xy];
                            s->c.mv[dir][i][0] = s->b_field_mv_table[dir][i][j][xy][0];
                            s->c.mv[dir][i][1] = s->b_field_mv_table[dir][i][j][xy][1];
                        }
                    }
                    break;
                default:
                    av_unreachable("There is a case for every CANDIDATE_MB_TYPE_* "
                                   "except CANDIDATE_MB_TYPE_SKIPPED which is never "
                                   "the only candidate (always coupled with INTER) "
                                   "so that it never reaches this switch");
                }

                encode_mb(s, motion_x, motion_y);

                // RAL: Update last macroblock type
                s->last_mv_dir = s->c.mv_dir;

                if (CONFIG_H263_ENCODER &&
                    s->c.out_format == FMT_H263 && s->c.pict_type != AV_PICTURE_TYPE_B)
                    ff_h263_update_mb(s);

                mpv_reconstruct_mb(s, s->c.block);
            }

            s->c.cur_pic.qscale_table[xy] = s->c.qscale;

            /* clean the MV table in IPS frames for direct mode in B-frames */
            if (s->c.mb_intra /* && I,P,S_TYPE */) {
                s->p_mv_table[xy][0]=0;
                s->p_mv_table[xy][1]=0;
#if CONFIG_H263_ENCODER
            } else if (s->c.h263_pred || s->c.h263_aic) {
                ff_h263_clean_intra_table_entries(&s->c, xy);
#endif
            }

            if (s->c.avctx->flags & AV_CODEC_FLAG_PSNR) {
                int w= 16;
                int h= 16;

                if (s->c.mb_x*16 + 16 > s->c.width ) w = s->c.width - s->c.mb_x*16;
                if (s->c.mb_y*16 + 16 > s->c.height) h = s->c.height- s->c.mb_y*16;

                s->encoding_error[0] += sse(
                    s, s->new_pic->data[0] + s->c.mb_x*16 + s->c.mb_y*s->c.linesize*16,
                    s->c.dest[0], w, h, s->c.linesize);
                s->encoding_error[1] += sse(
                    s, s->new_pic->data[1] + s->c.mb_x*8  + s->c.mb_y*s->c.uvlinesize*chr_h,
                    s->c.dest[1], w>>1, h>>s->c.chroma_y_shift, s->c.uvlinesize);
                s->encoding_error[2] += sse(
                    s, s->new_pic->data[2] + s->c.mb_x*8  + s->c.mb_y*s->c.uvlinesize*chr_h,
                    s->c.dest[2], w>>1, h>>s->c.chroma_y_shift, s->c.uvlinesize);
            }
            if (s->c.loop_filter) {
                if (CONFIG_H263_ENCODER && s->c.out_format == FMT_H263)
                    ff_h263_loop_filter(&s->c);
            }
            ff_dlog(s->c.avctx, "MB %d %d bits\n",
                    s->c.mb_x + s->c.mb_y * s->c.mb_stride, put_bits_count(&s->pb));
        }
    }

#if CONFIG_MSMPEG4ENC
    //not beautiful here but we must write it before flushing so it has to be here
    if (s->c.msmpeg4_version != MSMP4_UNUSED && s->c.msmpeg4_version < MSMP4_WMV1 &&
        s->c.pict_type == AV_PICTURE_TYPE_I)
        ff_msmpeg4_encode_ext_header(s);
#endif

    write_slice_end(s);

    return 0;
}

#define ADD(field)   dst->field += src->field;
#define MERGE(field) dst->field += src->field; src->field=0
static void merge_context_after_me(MPVEncContext *const dst, MPVEncContext *const src)
{
    ADD(me.scene_change_score);
    ADD(me.mc_mb_var_sum_temp);
    ADD(me.mb_var_sum_temp);
}

static void merge_context_after_encode(MPVEncContext *const dst, MPVEncContext *const src)
{
    int i;

    MERGE(dct_count[0]); //note, the other dct vars are not part of the context
    MERGE(dct_count[1]);
    ADD(mv_bits);
    ADD(i_tex_bits);
    ADD(p_tex_bits);
    ADD(i_count);
    ADD(misc_bits);
    ADD(encoding_error[0]);
    ADD(encoding_error[1]);
    ADD(encoding_error[2]);

    if (dst->dct_error_sum) {
        for(i=0; i<64; i++){
            MERGE(dct_error_sum[0][i]);
            MERGE(dct_error_sum[1][i]);
        }
    }

    av_assert1(put_bits_count(&src->pb) % 8 ==0);
    av_assert1(put_bits_count(&dst->pb) % 8 ==0);
    ff_copy_bits(&dst->pb, src->pb.buf, put_bits_count(&src->pb));
    flush_put_bits(&dst->pb);
}

static int estimate_qp(MPVMainEncContext *const m, int dry_run)
{
    MPVEncContext *const s = &m->s;

    if (m->next_lambda){
        s->c.cur_pic.ptr->f->quality = m->next_lambda;
        if(!dry_run) m->next_lambda= 0;
    } else if (!m->fixed_qscale) {
        int quality = ff_rate_estimate_qscale(m, dry_run);
        s->c.cur_pic.ptr->f->quality = quality;
        if (s->c.cur_pic.ptr->f->quality < 0)
            return -1;
    }

    if(s->adaptive_quant){
        init_qscale_tab(s);

        switch (s->c.codec_id) {
        case AV_CODEC_ID_MPEG4:
            if (CONFIG_MPEG4_ENCODER)
                ff_clean_mpeg4_qscales(s);
            break;
        case AV_CODEC_ID_H263:
        case AV_CODEC_ID_H263P:
        case AV_CODEC_ID_FLV1:
            if (CONFIG_H263_ENCODER)
                ff_clean_h263_qscales(s);
            break;
        }

        s->lambda = s->lambda_table[0];
        //FIXME broken
    }else
        s->lambda = s->c.cur_pic.ptr->f->quality;
    update_qscale(m);
    return 0;
}

/* must be called before writing the header */
static void set_frame_distances(MPVEncContext *const s)
{
    av_assert1(s->c.cur_pic.ptr->f->pts != AV_NOPTS_VALUE);
    s->c.time = s->c.cur_pic.ptr->f->pts * s->c.avctx->time_base.num;

    if (s->c.pict_type == AV_PICTURE_TYPE_B) {
        s->c.pb_time = s->c.pp_time - (s->c.last_non_b_time - s->c.time);
        av_assert1(s->c.pb_time > 0 && s->c.pb_time < s->c.pp_time);
    }else{
        s->c.pp_time = s->c.time - s->c.last_non_b_time;
        s->c.last_non_b_time = s->c.time;
        av_assert1(s->c.picture_number == 0 || s->c.pp_time > 0);
    }
}

static int encode_picture(MPVMainEncContext *const m, const AVPacket *pkt)
{
    MPVEncContext *const s = &m->s;
    int i, ret;
    int bits;
    int context_count = s->c.slice_context_count;

    /* we need to initialize some time vars before we can encode B-frames */
    // RAL: Condition added for MPEG1VIDEO
    if (s->c.out_format == FMT_MPEG1 || (s->c.h263_pred && s->c.msmpeg4_version == MSMP4_UNUSED))
        set_frame_distances(s);
    if (CONFIG_MPEG4_ENCODER && s->c.codec_id == AV_CODEC_ID_MPEG4)
        ff_set_mpeg4_time(s);

//    s->lambda = s->c.cur_pic.ptr->quality; //FIXME qscale / ... stuff for ME rate distortion

    if (s->c.pict_type == AV_PICTURE_TYPE_I) {
        s->c.no_rounding = s->c.msmpeg4_version >= MSMP4_V3;
    } else if (s->c.pict_type != AV_PICTURE_TYPE_B) {
        s->c.no_rounding ^= s->c.flipflop_rounding;
    }

    if (s->c.avctx->flags & AV_CODEC_FLAG_PASS2) {
        ret = estimate_qp(m, 1);
        if (ret < 0)
            return ret;
        ff_get_2pass_fcode(m);
    } else if (!(s->c.avctx->flags & AV_CODEC_FLAG_QSCALE)) {
        if (s->c.pict_type == AV_PICTURE_TYPE_B)
            s->lambda = m->last_lambda_for[s->c.pict_type];
        else
            s->lambda = m->last_lambda_for[m->last_non_b_pict_type];
        update_qscale(m);
    }

    s->c.mb_intra = 0; //for the rate distortion & bit compare functions
    for (int i = 0; i < context_count; i++) {
        MPVEncContext *const slice = s->c.enc_contexts[i];
        int h = s->c.mb_height;
        uint8_t *start = pkt->data + (int64_t)pkt->size * slice->c.start_mb_y / h;
        uint8_t *end   = pkt->data + (int64_t)pkt->size * slice->c.  end_mb_y / h;

        init_put_bits(&slice->pb, start, end - start);

        if (i) {
            ret = ff_update_duplicate_context(&slice->c, &s->c);
            if (ret < 0)
                return ret;
            slice->lambda  = s->lambda;
            slice->lambda2 = s->lambda2;
        }
        slice->me.temp = slice->me.scratchpad = slice->c.sc.scratchpad_buf;
        ff_me_init_pic(slice);
    }

    /* Estimate motion for every MB */
    if (s->c.pict_type != AV_PICTURE_TYPE_I) {
        s->lambda  = (s->lambda  * m->me_penalty_compensation + 128) >> 8;
        s->lambda2 = (s->lambda2 * (int64_t) m->me_penalty_compensation + 128) >> 8;
        if (s->c.pict_type != AV_PICTURE_TYPE_B) {
            if ((m->me_pre && m->last_non_b_pict_type == AV_PICTURE_TYPE_I) ||
                m->me_pre == 2) {
                s->c.avctx->execute(s->c.avctx, pre_estimate_motion_thread,
                                    &s->c.enc_contexts[0], NULL,
                                    context_count, sizeof(void*));
            }
        }

        s->c.avctx->execute(s->c.avctx, estimate_motion_thread, &s->c.enc_contexts[0],
                            NULL, context_count, sizeof(void*));
    }else /* if (s->c.pict_type == AV_PICTURE_TYPE_I) */{
        /* I-Frame */
        for (int i = 0; i < s->c.mb_stride * s->c.mb_height; i++)
            s->mb_type[i]= CANDIDATE_MB_TYPE_INTRA;

        if (!m->fixed_qscale) {
            /* finding spatial complexity for I-frame rate control */
            s->c.avctx->execute(s->c.avctx, mb_var_thread, &s->c.enc_contexts[0],
                                NULL, context_count, sizeof(void*));
        }
    }
    for(i=1; i<context_count; i++){
        merge_context_after_me(s, s->c.enc_contexts[i]);
    }
    m->mc_mb_var_sum = s->me.mc_mb_var_sum_temp;
    m->mb_var_sum    = s->me.   mb_var_sum_temp;
    emms_c();

    if (s->me.scene_change_score > m->scenechange_threshold &&
        s->c.pict_type == AV_PICTURE_TYPE_P) {
        s->c.pict_type = AV_PICTURE_TYPE_I;
        for (int i = 0; i < s->c.mb_stride * s->c.mb_height; i++)
            s->mb_type[i] = CANDIDATE_MB_TYPE_INTRA;
        if (s->c.msmpeg4_version >= MSMP4_V3)
            s->c.no_rounding = 1;
        ff_dlog(s->c.avctx, "Scene change detected, encoding as I Frame %"PRId64" %"PRId64"\n",
                m->mb_var_sum, m->mc_mb_var_sum);
    }

    if (!s->c.umvplus) {
        if (s->c.pict_type == AV_PICTURE_TYPE_P || s->c.pict_type == AV_PICTURE_TYPE_S) {
            s->f_code = ff_get_best_fcode(m, s->p_mv_table, CANDIDATE_MB_TYPE_INTER);

            if (s->c.avctx->flags & AV_CODEC_FLAG_INTERLACED_ME) {
                int a,b;
                a = ff_get_best_fcode(m, s->c.p_field_mv_table[0][0], CANDIDATE_MB_TYPE_INTER_I); //FIXME field_select
                b = ff_get_best_fcode(m, s->c.p_field_mv_table[1][1], CANDIDATE_MB_TYPE_INTER_I);
                s->f_code = FFMAX3(s->f_code, a, b);
            }

            ff_fix_long_p_mvs(s, s->intra_penalty ? CANDIDATE_MB_TYPE_INTER : CANDIDATE_MB_TYPE_INTRA);
            ff_fix_long_mvs(s, NULL, 0, s->p_mv_table, s->f_code, CANDIDATE_MB_TYPE_INTER, !!s->intra_penalty);
            if (s->c.avctx->flags & AV_CODEC_FLAG_INTERLACED_ME) {
                int j;
                for(i=0; i<2; i++){
                    for(j=0; j<2; j++)
                        ff_fix_long_mvs(s, s->p_field_select_table[i], j,
                                        s->c.p_field_mv_table[i][j], s->f_code, CANDIDATE_MB_TYPE_INTER_I, !!s->intra_penalty);
                }
            }
        } else if (s->c.pict_type == AV_PICTURE_TYPE_B) {
            int a, b;

            a = ff_get_best_fcode(m, s->b_forw_mv_table, CANDIDATE_MB_TYPE_FORWARD);
            b = ff_get_best_fcode(m, s->b_bidir_forw_mv_table, CANDIDATE_MB_TYPE_BIDIR);
            s->f_code = FFMAX(a, b);

            a = ff_get_best_fcode(m, s->b_back_mv_table, CANDIDATE_MB_TYPE_BACKWARD);
            b = ff_get_best_fcode(m, s->b_bidir_back_mv_table, CANDIDATE_MB_TYPE_BIDIR);
            s->b_code = FFMAX(a, b);

            ff_fix_long_mvs(s, NULL, 0, s->b_forw_mv_table, s->f_code, CANDIDATE_MB_TYPE_FORWARD, 1);
            ff_fix_long_mvs(s, NULL, 0, s->b_back_mv_table, s->b_code, CANDIDATE_MB_TYPE_BACKWARD, 1);
            ff_fix_long_mvs(s, NULL, 0, s->b_bidir_forw_mv_table, s->f_code, CANDIDATE_MB_TYPE_BIDIR, 1);
            ff_fix_long_mvs(s, NULL, 0, s->b_bidir_back_mv_table, s->b_code, CANDIDATE_MB_TYPE_BIDIR, 1);
            if (s->c.avctx->flags & AV_CODEC_FLAG_INTERLACED_ME) {
                int dir, j;
                for(dir=0; dir<2; dir++){
                    for(i=0; i<2; i++){
                        for(j=0; j<2; j++){
                            int type= dir ? (CANDIDATE_MB_TYPE_BACKWARD_I|CANDIDATE_MB_TYPE_BIDIR_I)
                                          : (CANDIDATE_MB_TYPE_FORWARD_I |CANDIDATE_MB_TYPE_BIDIR_I);
                            ff_fix_long_mvs(s, s->b_field_select_table[dir][i], j,
                                            s->b_field_mv_table[dir][i][j], dir ? s->b_code : s->f_code, type, 1);
                        }
                    }
                }
            }
        }
    }

    ret = estimate_qp(m, 0);
    if (ret < 0)
        return ret;

    if (s->c.qscale < 3 && s->max_qcoeff <= 128 &&
        s->c.pict_type == AV_PICTURE_TYPE_I &&
        !(s->c.avctx->flags & AV_CODEC_FLAG_QSCALE))
        s->c.qscale = 3; //reduce clipping problems

    if (s->c.out_format == FMT_MJPEG) {
        ret = ff_check_codec_matrices(s->c.avctx, FF_MATRIX_TYPE_INTRA | FF_MATRIX_TYPE_CHROMA_INTRA,
                                      (7 + s->c.qscale) / s->c.qscale, 65535);
        if (ret < 0)
            return ret;

        if (s->c.codec_id != AV_CODEC_ID_AMV) {
            const uint16_t *  luma_matrix = ff_mpeg1_default_intra_matrix;
            const uint16_t *chroma_matrix = ff_mpeg1_default_intra_matrix;

            if (s->c.avctx->intra_matrix) {
                chroma_matrix =
                luma_matrix = s->c.avctx->intra_matrix;
            }
            if (s->c.avctx->chroma_intra_matrix)
                chroma_matrix = s->c.avctx->chroma_intra_matrix;

            /* for mjpeg, we do include qscale in the matrix */
            for (int i = 1; i < 64; i++) {
                int j = s->c.idsp.idct_permutation[i];

                s->c.chroma_intra_matrix[j] = av_clip_uint8((chroma_matrix[i] * s->c.qscale) >> 3);
                s->c.       intra_matrix[j] = av_clip_uint8((  luma_matrix[i] * s->c.qscale) >> 3);
            }
            s->c.y_dc_scale_table =
            s->c.c_dc_scale_table = ff_mpeg12_dc_scale_table[s->c.intra_dc_precision];
            s->c.chroma_intra_matrix[0] =
            s->c.intra_matrix[0]  = ff_mpeg12_dc_scale_table[s->c.intra_dc_precision][8];
        } else {
            static const uint8_t y[32] = {13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13};
            static const uint8_t c[32] = {14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14};
            for (int i = 1; i < 64; i++) {
                int j = s->c.idsp.idct_permutation[ff_zigzag_direct[i]];

                s->c.intra_matrix[j]        = sp5x_qscale_five_quant_table[0][i];
                s->c.chroma_intra_matrix[j] = sp5x_qscale_five_quant_table[1][i];
            }
            s->c.y_dc_scale_table = y;
            s->c.c_dc_scale_table = c;
            s->c.intra_matrix[0] = 13;
            s->c.chroma_intra_matrix[0] = 14;
        }
        ff_convert_matrix(s, s->q_intra_matrix, s->q_intra_matrix16,
                          s->c.intra_matrix, s->intra_quant_bias, 8, 8, 1);
        ff_convert_matrix(s, s->q_chroma_intra_matrix, s->q_chroma_intra_matrix16,
                          s->c.chroma_intra_matrix, s->intra_quant_bias, 8, 8, 1);
        s->c.qscale = 8;
    }

    if (s->c.pict_type == AV_PICTURE_TYPE_I) {
        s->c.cur_pic.ptr->f->flags |= AV_FRAME_FLAG_KEY;
    } else {
        s->c.cur_pic.ptr->f->flags &= ~AV_FRAME_FLAG_KEY;
    }
    s->c.cur_pic.ptr->f->pict_type = s->c.pict_type;

    if (s->c.cur_pic.ptr->f->flags & AV_FRAME_FLAG_KEY)
        m->picture_in_gop_number = 0;

    s->c.mb_x = s->c.mb_y = 0;
    s->last_bits= put_bits_count(&s->pb);
    ret = m->encode_picture_header(m);
    if (ret < 0)
        return ret;
    bits= put_bits_count(&s->pb);
    m->header_bits = bits - s->last_bits;

    for(i=1; i<context_count; i++){
        update_duplicate_context_after_me(s->c.enc_contexts[i], s);
    }
    s->c.avctx->execute(s->c.avctx, encode_thread, &s->c.enc_contexts[0],
                        NULL, context_count, sizeof(void*));
    for(i=1; i<context_count; i++){
        if (s->pb.buf_end == s->c.enc_contexts[i]->pb.buf)
            set_put_bits_buffer_size(&s->pb, FFMIN(s->c.enc_contexts[i]->pb.buf_end - s->pb.buf, INT_MAX/8-BUF_BITS));
        merge_context_after_encode(s, s->c.enc_contexts[i]);
    }
    emms_c();
    return 0;
}

static void denoise_dct_c(MPVEncContext *const s, int16_t *block)
{
    const int intra = s->c.mb_intra;
    int i;

    s->dct_count[intra]++;

    for(i=0; i<64; i++){
        int level= block[i];

        if(level){
            if(level>0){
                s->dct_error_sum[intra][i] += level;
                level -= s->dct_offset[intra][i];
                if(level<0) level=0;
            }else{
                s->dct_error_sum[intra][i] -= level;
                level += s->dct_offset[intra][i];
                if(level>0) level=0;
            }
            block[i]= level;
        }
    }
}

static int dct_quantize_trellis_c(MPVEncContext *const s,
                                  int16_t *block, int n,
                                  int qscale, int *overflow){
    const int *qmat;
    const uint16_t *matrix;
    const uint8_t *scantable;
    const uint8_t *perm_scantable;
    int max=0;
    unsigned int threshold1, threshold2;
    int bias=0;
    int run_tab[65];
    int level_tab[65];
    int score_tab[65];
    int survivor[65];
    int survivor_count;
    int last_run=0;
    int last_level=0;
    int last_score= 0;
    int last_i;
    int coeff[2][64];
    int coeff_count[64];
    int qmul, qadd, start_i, last_non_zero, i, dc;
    const int esc_length= s->ac_esc_length;
    const uint8_t *length, *last_length;
    const int lambda = s->lambda2 >> (FF_LAMBDA_SHIFT - 6);
    int mpeg2_qscale;

    s->fdsp.fdct(block);

    if(s->dct_error_sum)
        s->denoise_dct(s, block);
    qmul= qscale*16;
    qadd= ((qscale-1)|1)*8;

    if (s->c.q_scale_type) mpeg2_qscale = ff_mpeg2_non_linear_qscale[qscale];
    else                 mpeg2_qscale = qscale << 1;

    if (s->c.mb_intra) {
        int q;
        scantable      = s->c.intra_scantable.scantable;
        perm_scantable = s->c.intra_scantable.permutated;
        if (!s->c.h263_aic) {
            if (n < 4)
                q = s->c.y_dc_scale;
            else
                q = s->c.c_dc_scale;
            q = q << 3;
        } else{
            /* For AIC we skip quant/dequant of INTRADC */
            q = 1 << 3;
            qadd=0;
        }

        /* note: block[0] is assumed to be positive */
        block[0] = (block[0] + (q >> 1)) / q;
        start_i = 1;
        last_non_zero = 0;
        qmat = n < 4 ? s->q_intra_matrix[qscale] : s->q_chroma_intra_matrix[qscale];
        matrix = n < 4 ? s->c.intra_matrix : s->c.chroma_intra_matrix;
        if (s->mpeg_quant || s->c.out_format == FMT_MPEG1 || s->c.out_format == FMT_MJPEG)
            bias= 1<<(QMAT_SHIFT-1);

        if (n > 3 && s->intra_chroma_ac_vlc_length) {
            length     = s->intra_chroma_ac_vlc_length;
            last_length= s->intra_chroma_ac_vlc_last_length;
        } else {
            length     = s->intra_ac_vlc_length;
            last_length= s->intra_ac_vlc_last_length;
        }
    } else {
        scantable      = s->c.inter_scantable.scantable;
        perm_scantable = s->c.inter_scantable.permutated;
        start_i = 0;
        last_non_zero = -1;
        qmat = s->q_inter_matrix[qscale];
        matrix = s->c.inter_matrix;
        length     = s->inter_ac_vlc_length;
        last_length= s->inter_ac_vlc_last_length;
    }
    last_i= start_i;

    threshold1= (1<<QMAT_SHIFT) - bias - 1;
    threshold2= (threshold1<<1);

    for(i=63; i>=start_i; i--) {
        const int j = scantable[i];
        int64_t level = (int64_t)block[j] * qmat[j];

        if(((uint64_t)(level+threshold1))>threshold2){
            last_non_zero = i;
            break;
        }
    }

    for(i=start_i; i<=last_non_zero; i++) {
        const int j = scantable[i];
        int64_t level = (int64_t)block[j] * qmat[j];

//        if(   bias+level >= (1<<(QMAT_SHIFT - 3))
//           || bias-level >= (1<<(QMAT_SHIFT - 3))){
        if(((uint64_t)(level+threshold1))>threshold2){
            if(level>0){
                level= (bias + level)>>QMAT_SHIFT;
                coeff[0][i]= level;
                coeff[1][i]= level-1;
//                coeff[2][k]= level-2;
            }else{
                level= (bias - level)>>QMAT_SHIFT;
                coeff[0][i]= -level;
                coeff[1][i]= -level+1;
//                coeff[2][k]= -level+2;
            }
            coeff_count[i]= FFMIN(level, 2);
            av_assert2(coeff_count[i]);
            max |=level;
        }else{
            coeff[0][i]= (level>>31)|1;
            coeff_count[i]= 1;
        }
    }

    *overflow= s->max_qcoeff < max; //overflow might have happened

    if(last_non_zero < start_i){
        memset(block + start_i, 0, (64-start_i)*sizeof(int16_t));
        return last_non_zero;
    }

    score_tab[start_i]= 0;
    survivor[0]= start_i;
    survivor_count= 1;

    for(i=start_i; i<=last_non_zero; i++){
        int level_index, j, zero_distortion;
        int dct_coeff= FFABS(block[ scantable[i] ]);
        int best_score=256*256*256*120;

        if (s->fdsp.fdct == ff_fdct_ifast)
            dct_coeff= (dct_coeff*ff_inv_aanscales[ scantable[i] ]) >> 12;
        zero_distortion= dct_coeff*dct_coeff;

        for(level_index=0; level_index < coeff_count[i]; level_index++){
            int distortion;
            int level= coeff[level_index][i];
            const int alevel= FFABS(level);
            int unquant_coeff;

            av_assert2(level);

            if (s->c.out_format == FMT_H263 || s->c.out_format == FMT_H261) {
                unquant_coeff= alevel*qmul + qadd;
            } else if (s->c.out_format == FMT_MJPEG) {
                j = s->c.idsp.idct_permutation[scantable[i]];
                unquant_coeff = alevel * matrix[j] * 8;
            }else{ // MPEG-1
                j = s->c.idsp.idct_permutation[scantable[i]]; // FIXME: optimize
                if (s->c.mb_intra) {
                        unquant_coeff = (int)(  alevel  * mpeg2_qscale * matrix[j]) >> 4;
                        unquant_coeff =   (unquant_coeff - 1) | 1;
                }else{
                        unquant_coeff = (((  alevel  << 1) + 1) * mpeg2_qscale * ((int) matrix[j])) >> 5;
                        unquant_coeff =   (unquant_coeff - 1) | 1;
                }
                unquant_coeff<<= 3;
            }

            distortion= (unquant_coeff - dct_coeff) * (unquant_coeff - dct_coeff) - zero_distortion;
            level+=64;
            if((level&(~127)) == 0){
                for(j=survivor_count-1; j>=0; j--){
                    int run= i - survivor[j];
                    int score= distortion + length[UNI_AC_ENC_INDEX(run, level)]*lambda;
                    score += score_tab[i-run];

                    if(score < best_score){
                        best_score= score;
                        run_tab[i+1]= run;
                        level_tab[i+1]= level-64;
                    }
                }

                if (s->c.out_format == FMT_H263 || s->c.out_format == FMT_H261) {
                    for(j=survivor_count-1; j>=0; j--){
                        int run= i - survivor[j];
                        int score= distortion + last_length[UNI_AC_ENC_INDEX(run, level)]*lambda;
                        score += score_tab[i-run];
                        if(score < last_score){
                            last_score= score;
                            last_run= run;
                            last_level= level-64;
                            last_i= i+1;
                        }
                    }
                }
            }else{
                distortion += esc_length*lambda;
                for(j=survivor_count-1; j>=0; j--){
                    int run= i - survivor[j];
                    int score= distortion + score_tab[i-run];

                    if(score < best_score){
                        best_score= score;
                        run_tab[i+1]= run;
                        level_tab[i+1]= level-64;
                    }
                }

                if (s->c.out_format == FMT_H263 || s->c.out_format == FMT_H261) {
                  for(j=survivor_count-1; j>=0; j--){
                        int run= i - survivor[j];
                        int score= distortion + score_tab[i-run];
                        if(score < last_score){
                            last_score= score;
                            last_run= run;
                            last_level= level-64;
                            last_i= i+1;
                        }
                    }
                }
            }
        }

        score_tab[i+1]= best_score;

        // Note: there is a vlc code in MPEG-4 which is 1 bit shorter then another one with a shorter run and the same level
        if(last_non_zero <= 27){
            for(; survivor_count; survivor_count--){
                if(score_tab[ survivor[survivor_count-1] ] <= best_score)
                    break;
            }
        }else{
            for(; survivor_count; survivor_count--){
                if(score_tab[ survivor[survivor_count-1] ] <= best_score + lambda)
                    break;
            }
        }

        survivor[ survivor_count++ ]= i+1;
    }

    if (s->c.out_format != FMT_H263 && s->c.out_format != FMT_H261) {
        last_score= 256*256*256*120;
        for(i= survivor[0]; i<=last_non_zero + 1; i++){
            int score= score_tab[i];
            if (i)
                score += lambda * 2; // FIXME more exact?

            if(score < last_score){
                last_score= score;
                last_i= i;
                last_level= level_tab[i];
                last_run= run_tab[i];
            }
        }
    }

    s->coded_score[n] = last_score;

    dc= FFABS(block[0]);
    last_non_zero= last_i - 1;
    memset(block + start_i, 0, (64-start_i)*sizeof(int16_t));

    if(last_non_zero < start_i)
        return last_non_zero;

    if(last_non_zero == 0 && start_i == 0){
        int best_level= 0;
        int best_score= dc * dc;

        for(i=0; i<coeff_count[0]; i++){
            int level= coeff[i][0];
            int alevel= FFABS(level);
            int unquant_coeff, score, distortion;

            if (s->c.out_format == FMT_H263 || s->c.out_format == FMT_H261) {
                    unquant_coeff= (alevel*qmul + qadd)>>3;
            } else{ // MPEG-1
                    unquant_coeff = (((  alevel  << 1) + 1) * mpeg2_qscale * ((int) matrix[0])) >> 5;
                    unquant_coeff =   (unquant_coeff - 1) | 1;
            }
            unquant_coeff = (unquant_coeff + 4) >> 3;
            unquant_coeff<<= 3 + 3;

            distortion= (unquant_coeff - dc) * (unquant_coeff - dc);
            level+=64;
            if((level&(~127)) == 0) score= distortion + last_length[UNI_AC_ENC_INDEX(0, level)]*lambda;
            else                    score= distortion + esc_length*lambda;

            if(score < best_score){
                best_score= score;
                best_level= level - 64;
            }
        }
        block[0]= best_level;
        s->coded_score[n] = best_score - dc*dc;
        if(best_level == 0) return -1;
        else                return last_non_zero;
    }

    i= last_i;
    av_assert2(last_level);

    block[ perm_scantable[last_non_zero] ]= last_level;
    i -= last_run + 1;

    for(; i>start_i; i -= run_tab[i] + 1){
        block[ perm_scantable[i-1] ]= level_tab[i];
    }

    return last_non_zero;
}

static int16_t basis[64][64];

static void build_basis(uint8_t *perm){
    int i, j, x, y;
    emms_c();
    for(i=0; i<8; i++){
        for(j=0; j<8; j++){
            for(y=0; y<8; y++){
                for(x=0; x<8; x++){
                    double s= 0.25*(1<<BASIS_SHIFT);
                    int index= 8*i + j;
                    int perm_index= perm[index];
                    if(i==0) s*= sqrt(0.5);
                    if(j==0) s*= sqrt(0.5);
                    basis[perm_index][8*x + y]= lrintf(s * cos((M_PI/8.0)*i*(x+0.5)) * cos((M_PI/8.0)*j*(y+0.5)));
                }
            }
        }
    }
}

static int dct_quantize_refine(MPVEncContext *const s, //FIXME breaks denoise?
                        int16_t *block, int16_t *weight, int16_t *orig,
                        int n, int qscale){
    int16_t rem[64];
    LOCAL_ALIGNED_16(int16_t, d1, [64]);
    const uint8_t *scantable;
    const uint8_t *perm_scantable;
//    unsigned int threshold1, threshold2;
//    int bias=0;
    int run_tab[65];
    int prev_run=0;
    int prev_level=0;
    int qmul, qadd, start_i, last_non_zero, i, dc;
    const uint8_t *length;
    const uint8_t *last_length;
    int lambda;
    int rle_index, run, q = 1, sum; //q is only used when s->c.mb_intra is true

    if(basis[0][0] == 0)
        build_basis(s->c.idsp.idct_permutation);

    qmul= qscale*2;
    qadd= (qscale-1)|1;
    if (s->c.mb_intra) {
        scantable      = s->c.intra_scantable.scantable;
        perm_scantable = s->c.intra_scantable.permutated;
        if (!s->c.h263_aic) {
            if (n < 4)
                q = s->c.y_dc_scale;
            else
                q = s->c.c_dc_scale;
        } else{
            /* For AIC we skip quant/dequant of INTRADC */
            q = 1;
            qadd=0;
        }
        q <<= RECON_SHIFT-3;
        /* note: block[0] is assumed to be positive */
        dc= block[0]*q;
//        block[0] = (block[0] + (q >> 1)) / q;
        start_i = 1;
//        if (s->mpeg_quant || s->c.out_format == FMT_MPEG1)
//            bias= 1<<(QMAT_SHIFT-1);
        if (n > 3 && s->intra_chroma_ac_vlc_length) {
            length     = s->intra_chroma_ac_vlc_length;
            last_length= s->intra_chroma_ac_vlc_last_length;
        } else {
            length     = s->intra_ac_vlc_length;
            last_length= s->intra_ac_vlc_last_length;
        }
    } else {
        scantable      = s->c.inter_scantable.scantable;
        perm_scantable = s->c.inter_scantable.permutated;
        dc= 0;
        start_i = 0;
        length     = s->inter_ac_vlc_length;
        last_length= s->inter_ac_vlc_last_length;
    }
    last_non_zero = s->c.block_last_index[n];

    dc += (1<<(RECON_SHIFT-1));
    for(i=0; i<64; i++){
        rem[i] = dc - (orig[i] << RECON_SHIFT); // FIXME use orig directly instead of copying to rem[]
    }

    sum=0;
    for(i=0; i<64; i++){
        int one= 36;
        int qns=4;
        int w;

        w= FFABS(weight[i]) + qns*one;
        w= 15 + (48*qns*one + w/2)/w; // 16 .. 63

        weight[i] = w;
//        w=weight[i] = (63*qns + (w/2)) / w;

        av_assert2(w>0);
        av_assert2(w<(1<<6));
        sum += w*w;
    }
    lambda = sum*(uint64_t)s->lambda2 >> (FF_LAMBDA_SHIFT - 6 + 6 + 6 + 6);

    run=0;
    rle_index=0;
    for(i=start_i; i<=last_non_zero; i++){
        int j= perm_scantable[i];
        const int level= block[j];
        int coeff;

        if(level){
            if(level<0) coeff= qmul*level - qadd;
            else        coeff= qmul*level + qadd;
            run_tab[rle_index++]=run;
            run=0;

            s->mpvencdsp.add_8x8basis(rem, basis[j], coeff);
        }else{
            run++;
        }
    }

    for(;;){
        int best_score = s->mpvencdsp.try_8x8basis(rem, weight, basis[0], 0);
        int best_coeff=0;
        int best_change=0;
        int run2, best_unquant_change=0, analyze_gradient;
        analyze_gradient = last_non_zero > 2 || s->quantizer_noise_shaping >= 3;

        if(analyze_gradient){
            for(i=0; i<64; i++){
                int w= weight[i];

                d1[i] = (rem[i]*w*w + (1<<(RECON_SHIFT+12-1)))>>(RECON_SHIFT+12);
            }
            s->fdsp.fdct(d1);
        }

        if(start_i){
            const int level= block[0];
            int change, old_coeff;

            av_assert2(s->c.mb_intra);

            old_coeff= q*level;

            for(change=-1; change<=1; change+=2){
                int new_level= level + change;
                int score, new_coeff;

                new_coeff= q*new_level;
                if(new_coeff >= 2048 || new_coeff < 0)
                    continue;

                score = s->mpvencdsp.try_8x8basis(rem, weight, basis[0],
                                                  new_coeff - old_coeff);
                if(score<best_score){
                    best_score= score;
                    best_coeff= 0;
                    best_change= change;
                    best_unquant_change= new_coeff - old_coeff;
                }
            }
        }

        run=0;
        rle_index=0;
        run2= run_tab[rle_index++];
        prev_level=0;
        prev_run=0;

        for(i=start_i; i<64; i++){
            int j= perm_scantable[i];
            const int level= block[j];
            int change, old_coeff;

            if(s->quantizer_noise_shaping < 3 && i > last_non_zero + 1)
                break;

            if(level){
                if(level<0) old_coeff= qmul*level - qadd;
                else        old_coeff= qmul*level + qadd;
                run2= run_tab[rle_index++]; //FIXME ! maybe after last
            }else{
                old_coeff=0;
                run2--;
                av_assert2(run2>=0 || i >= last_non_zero );
            }

            for(change=-1; change<=1; change+=2){
                int new_level= level + change;
                int score, new_coeff, unquant_change;

                score=0;
                if(s->quantizer_noise_shaping < 2 && FFABS(new_level) > FFABS(level))
                   continue;

                if(new_level){
                    if(new_level<0) new_coeff= qmul*new_level - qadd;
                    else            new_coeff= qmul*new_level + qadd;
                    if(new_coeff >= 2048 || new_coeff <= -2048)
                        continue;
                    //FIXME check for overflow

                    if(level){
                        if(level < 63 && level > -63){
                            if(i < last_non_zero)
                                score +=   length[UNI_AC_ENC_INDEX(run, new_level+64)]
                                         - length[UNI_AC_ENC_INDEX(run, level+64)];
                            else
                                score +=   last_length[UNI_AC_ENC_INDEX(run, new_level+64)]
                                         - last_length[UNI_AC_ENC_INDEX(run, level+64)];
                        }
                    }else{
                        av_assert2(FFABS(new_level)==1);

                        if(analyze_gradient){
                            int g= d1[ scantable[i] ];
                            if(g && (g^new_level) >= 0)
                                continue;
                        }

                        if(i < last_non_zero){
                            int next_i= i + run2 + 1;
                            int next_level= block[ perm_scantable[next_i] ] + 64;

                            if(next_level&(~127))
                                next_level= 0;

                            if(next_i < last_non_zero)
                                score +=   length[UNI_AC_ENC_INDEX(run, 65)]
                                         + length[UNI_AC_ENC_INDEX(run2, next_level)]
                                         - length[UNI_AC_ENC_INDEX(run + run2 + 1, next_level)];
                            else
                                score +=  length[UNI_AC_ENC_INDEX(run, 65)]
                                        + last_length[UNI_AC_ENC_INDEX(run2, next_level)]
                                        - last_length[UNI_AC_ENC_INDEX(run + run2 + 1, next_level)];
                        }else{
                            score += last_length[UNI_AC_ENC_INDEX(run, 65)];
                            if(prev_level){
                                score +=  length[UNI_AC_ENC_INDEX(prev_run, prev_level)]
                                        - last_length[UNI_AC_ENC_INDEX(prev_run, prev_level)];
                            }
                        }
                    }
                }else{
                    new_coeff=0;
                    av_assert2(FFABS(level)==1);

                    if(i < last_non_zero){
                        int next_i= i + run2 + 1;
                        int next_level= block[ perm_scantable[next_i] ] + 64;

                        if(next_level&(~127))
                            next_level= 0;

                        if(next_i < last_non_zero)
                            score +=   length[UNI_AC_ENC_INDEX(run + run2 + 1, next_level)]
                                     - length[UNI_AC_ENC_INDEX(run2, next_level)]
                                     - length[UNI_AC_ENC_INDEX(run, 65)];
                        else
                            score +=   last_length[UNI_AC_ENC_INDEX(run + run2 + 1, next_level)]
                                     - last_length[UNI_AC_ENC_INDEX(run2, next_level)]
                                     - length[UNI_AC_ENC_INDEX(run, 65)];
                    }else{
                        score += -last_length[UNI_AC_ENC_INDEX(run, 65)];
                        if(prev_level){
                            score +=  last_length[UNI_AC_ENC_INDEX(prev_run, prev_level)]
                                    - length[UNI_AC_ENC_INDEX(prev_run, prev_level)];
                        }
                    }
                }

                score *= lambda;

                unquant_change= new_coeff - old_coeff;
                av_assert2((score < 100*lambda && score > -100*lambda) || lambda==0);

                score += s->mpvencdsp.try_8x8basis(rem, weight, basis[j],
                                                   unquant_change);
                if(score<best_score){
                    best_score= score;
                    best_coeff= i;
                    best_change= change;
                    best_unquant_change= unquant_change;
                }
            }
            if(level){
                prev_level= level + 64;
                if(prev_level&(~127))
                    prev_level= 0;
                prev_run= run;
                run=0;
            }else{
                run++;
            }
        }

        if(best_change){
            int j= perm_scantable[ best_coeff ];

            block[j] += best_change;

            if(best_coeff > last_non_zero){
                last_non_zero= best_coeff;
                av_assert2(block[j]);
            }else{
                for(; last_non_zero>=start_i; last_non_zero--){
                    if(block[perm_scantable[last_non_zero]])
                        break;
                }
            }

            run=0;
            rle_index=0;
            for(i=start_i; i<=last_non_zero; i++){
                int j= perm_scantable[i];
                const int level= block[j];

                 if(level){
                     run_tab[rle_index++]=run;
                     run=0;
                 }else{
                     run++;
                 }
            }

            s->mpvencdsp.add_8x8basis(rem, basis[j], best_unquant_change);
        }else{
            break;
        }
    }

    return last_non_zero;
}

/**
 * Permute an 8x8 block according to permutation.
 * @param block the block which will be permuted according to
 *              the given permutation vector
 * @param permutation the permutation vector
 * @param last the last non zero coefficient in scantable order, used to
 *             speed the permutation up
 * @param scantable the used scantable, this is only used to speed the
 *                  permutation up, the block is not (inverse) permutated
 *                  to scantable order!
 */
void ff_block_permute(int16_t *block, const uint8_t *permutation,
                      const uint8_t *scantable, int last)
{
    int i;
    int16_t temp[64];

    if (last <= 0)
        return;
    //FIXME it is ok but not clean and might fail for some permutations
    // if (permutation[1] == 1)
    // return;

    for (i = 0; i <= last; i++) {
        const int j = scantable[i];
        temp[j] = block[j];
        block[j] = 0;
    }

    for (i = 0; i <= last; i++) {
        const int j = scantable[i];
        const int perm_j = permutation[j];
        block[perm_j] = temp[j];
    }
}

static int dct_quantize_c(MPVEncContext *const s,
                          int16_t *block, int n,
                          int qscale, int *overflow)
{
    int i, last_non_zero, q, start_i;
    const int *qmat;
    const uint8_t *scantable;
    int bias;
    int max=0;
    unsigned int threshold1, threshold2;

    s->fdsp.fdct(block);

    if(s->dct_error_sum)
        s->denoise_dct(s, block);

    if (s->c.mb_intra) {
        scantable = s->c.intra_scantable.scantable;
        if (!s->c.h263_aic) {
            if (n < 4)
                q = s->c.y_dc_scale;
            else
                q = s->c.c_dc_scale;
            q = q << 3;
        } else
            /* For AIC we skip quant/dequant of INTRADC */
            q = 1 << 3;

        /* note: block[0] is assumed to be positive */
        block[0] = (block[0] + (q >> 1)) / q;
        start_i = 1;
        last_non_zero = 0;
        qmat = n < 4 ? s->q_intra_matrix[qscale] : s->q_chroma_intra_matrix[qscale];
        bias= s->intra_quant_bias*(1<<(QMAT_SHIFT - QUANT_BIAS_SHIFT));
    } else {
        scantable = s->c.inter_scantable.scantable;
        start_i = 0;
        last_non_zero = -1;
        qmat = s->q_inter_matrix[qscale];
        bias= s->inter_quant_bias*(1<<(QMAT_SHIFT - QUANT_BIAS_SHIFT));
    }
    threshold1= (1<<QMAT_SHIFT) - bias - 1;
    threshold2= (threshold1<<1);
    for(i=63;i>=start_i;i--) {
        const int j = scantable[i];
        int64_t level = (int64_t)block[j] * qmat[j];

        if(((uint64_t)(level+threshold1))>threshold2){
            last_non_zero = i;
            break;
        }else{
            block[j]=0;
        }
    }
    for(i=start_i; i<=last_non_zero; i++) {
        const int j = scantable[i];
        int64_t level = (int64_t)block[j] * qmat[j];

//        if(   bias+level >= (1<<QMAT_SHIFT)
//           || bias-level >= (1<<QMAT_SHIFT)){
        if(((uint64_t)(level+threshold1))>threshold2){
            if(level>0){
                level= (bias + level)>>QMAT_SHIFT;
                block[j]= level;
            }else{
                level= (bias - level)>>QMAT_SHIFT;
                block[j]= -level;
            }
            max |=level;
        }else{
            block[j]=0;
        }
    }
    *overflow= s->max_qcoeff < max; //overflow might have happened

    /* we need this permutation so that we correct the IDCT, we only permute the !=0 elements */
    if (s->c.idsp.perm_type != FF_IDCT_PERM_NONE)
        ff_block_permute(block, s->c.idsp.idct_permutation,
                      scantable, last_non_zero);

    return last_non_zero;
}
