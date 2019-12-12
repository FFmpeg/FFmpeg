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

#include <stdint.h>

#include "libavutil/internal.h"
#include "libavutil/intmath.h"
#include "libavutil/mathematics.h"
#include "libavutil/pixdesc.h"
#include "libavutil/opt.h"
#include "libavutil/timer.h"
#include "avcodec.h"
#include "dct.h"
#include "idctdsp.h"
#include "mpeg12.h"
#include "mpegvideo.h"
#include "mpegvideodata.h"
#include "h261.h"
#include "h263.h"
#include "h263data.h"
#include "mjpegenc_common.h"
#include "mathops.h"
#include "mpegutils.h"
#include "mjpegenc.h"
#include "msmpeg4.h"
#include "pixblockdsp.h"
#include "qpeldsp.h"
#include "faandct.h"
#include "thread.h"
#include "aandcttab.h"
#include "flv.h"
#include "mpeg4video.h"
#include "internal.h"
#include "bytestream.h"
#include "wmv2.h"
#include "rv10.h"
#include "libxvid.h"
#include <limits.h>
#include "sp5x.h"

#define QUANT_BIAS_SHIFT 8

#define QMAT_SHIFT_MMX 16
#define QMAT_SHIFT 21

static int encode_picture(MpegEncContext *s, int picture_number);
static int dct_quantize_refine(MpegEncContext *s, int16_t *block, int16_t *weight, int16_t *orig, int n, int qscale);
static int sse_mb(MpegEncContext *s);
static void denoise_dct_c(MpegEncContext *s, int16_t *block);
static int dct_quantize_trellis_c(MpegEncContext *s, int16_t *block, int n, int qscale, int *overflow);

static uint8_t default_mv_penalty[MAX_FCODE + 1][MAX_DMV * 2 + 1];
static uint8_t default_fcode_tab[MAX_MV * 2 + 1];

const AVOption ff_mpv_generic_options[] = {
    FF_MPV_COMMON_OPTS
    { NULL },
};

void ff_convert_matrix(MpegEncContext *s, int (*qmat)[64],
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

        if (s->q_scale_type) qscale2 = ff_mpeg2_non_linear_qscale[qscale];
        else                 qscale2 = qscale << 1;

        if (fdsp->fdct == ff_jpeg_fdct_islow_8  ||
#if CONFIG_FAANDCT
            fdsp->fdct == ff_faandct            ||
#endif /* CONFIG_FAANDCT */
            fdsp->fdct == ff_jpeg_fdct_islow_10) {
            for (i = 0; i < 64; i++) {
                const int j = s->idsp.idct_permutation[i];
                int64_t den = (int64_t) qscale2 * quant_matrix[j];
                /* 16 <= qscale * quant_matrix[i] <= 7905
                 * Assume x = ff_aanscales[i] * qscale * quant_matrix[i]
                 *             19952 <=              x  <= 249205026
                 * (1 << 36) / 19952 >= (1 << 36) / (x) >= (1 << 36) / 249205026
                 *           3444240 >= (1 << 36) / (x) >= 275 */

                qmat[qscale][i] = (int)((UINT64_C(2) << QMAT_SHIFT) / den);
            }
        } else if (fdsp->fdct == ff_fdct_ifast) {
            for (i = 0; i < 64; i++) {
                const int j = s->idsp.idct_permutation[i];
                int64_t den = ff_aanscales[i] * (int64_t) qscale2 * quant_matrix[j];
                /* 16 <= qscale * quant_matrix[i] <= 7905
                 * Assume x = ff_aanscales[i] * qscale * quant_matrix[i]
                 *             19952 <=              x  <= 249205026
                 * (1 << 36) / 19952 >= (1 << 36) / (x) >= (1 << 36) / 249205026
                 *           3444240 >= (1 << 36) / (x) >= 275 */

                qmat[qscale][i] = (int)((UINT64_C(2) << (QMAT_SHIFT + 14)) / den);
            }
        } else {
            for (i = 0; i < 64; i++) {
                const int j = s->idsp.idct_permutation[i];
                int64_t den = (int64_t) qscale2 * quant_matrix[j];
                /* We can safely suppose that 16 <= quant_matrix[i] <= 255
                 * Assume x = qscale * quant_matrix[i]
                 * So             16 <=              x  <= 7905
                 * so (1 << 19) / 16 >= (1 << 19) / (x) >= (1 << 19) / 7905
                 * so          32768 >= (1 << 19) / (x) >= 67 */
                qmat[qscale][i] = (int)((UINT64_C(2) << QMAT_SHIFT) / den);
                //qmat  [qscale][i] = (1 << QMAT_SHIFT_MMX) /
                //                    (qscale * quant_matrix[i]);
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
        av_log(s->avctx, AV_LOG_INFO,
               "Warning, QMAT_SHIFT is larger than %d, overflows possible\n",
               QMAT_SHIFT - shift);
    }
}

static inline void update_qscale(MpegEncContext *s)
{
    if (s->q_scale_type == 1 && 0) {
        int i;
        int bestdiff=INT_MAX;
        int best = 1;

        for (i = 0 ; i<FF_ARRAY_ELEMS(ff_mpeg2_non_linear_qscale); i++) {
            int diff = FFABS((ff_mpeg2_non_linear_qscale[i]<<(FF_LAMBDA_SHIFT + 6)) - (int)s->lambda * 139);
            if (ff_mpeg2_non_linear_qscale[i] < s->avctx->qmin ||
                (ff_mpeg2_non_linear_qscale[i] > s->avctx->qmax && !s->vbv_ignore_qmax))
                continue;
            if (diff < bestdiff) {
                bestdiff = diff;
                best = i;
            }
        }
        s->qscale = best;
    } else {
        s->qscale = (s->lambda * 139 + FF_LAMBDA_SCALE * 64) >>
                    (FF_LAMBDA_SHIFT + 7);
        s->qscale = av_clip(s->qscale, s->avctx->qmin, s->vbv_ignore_qmax ? 31 : s->avctx->qmax);
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
 * init s->current_picture.qscale_table from s->lambda_table
 */
void ff_init_qscale_tab(MpegEncContext *s)
{
    int8_t * const qscale_table = s->current_picture.qscale_table;
    int i;

    for (i = 0; i < s->mb_num; i++) {
        unsigned int lam = s->lambda_table[s->mb_index2xy[i]];
        int qp = (lam * 139 + FF_LAMBDA_SCALE * 64) >> (FF_LAMBDA_SHIFT + 7);
        qscale_table[s->mb_index2xy[i]] = av_clip(qp, s->avctx->qmin,
                                                  s->avctx->qmax);
    }
}

static void update_duplicate_context_after_me(MpegEncContext *dst,
                                              MpegEncContext *src)
{
#define COPY(a) dst->a= src->a
    COPY(pict_type);
    COPY(current_picture);
    COPY(f_code);
    COPY(b_code);
    COPY(qscale);
    COPY(lambda);
    COPY(lambda2);
    COPY(picture_in_gop_number);
    COPY(gop_picture_number);
    COPY(frame_pred_frame_dct); // FIXME don't set in encode_header
    COPY(progressive_frame);    // FIXME don't set in encode_header
    COPY(partitioned_frame);    // FIXME don't set in encode_header
#undef COPY
}

/**
 * Set the given MpegEncContext to defaults for encoding.
 * the changed fields will not depend upon the prior state of the MpegEncContext.
 */
static void mpv_encode_defaults(MpegEncContext *s)
{
    int i;
    ff_mpv_common_defaults(s);

    for (i = -16; i < 16; i++) {
        default_fcode_tab[i + MAX_MV] = 1;
    }
    s->me.mv_penalty = default_mv_penalty;
    s->fcode_tab     = default_fcode_tab;

    s->input_picture_number  = 0;
    s->picture_in_gop_number = 0;
}

av_cold int ff_dct_encode_init(MpegEncContext *s)
{
    if (ARCH_X86)
        ff_dct_encode_init_x86(s);

    if (CONFIG_H263_ENCODER)
        ff_h263dsp_init(&s->h263dsp);
    if (!s->dct_quantize)
        s->dct_quantize = ff_dct_quantize_c;
    if (!s->denoise_dct)
        s->denoise_dct  = denoise_dct_c;
    s->fast_dct_quantize = s->dct_quantize;
    if (s->avctx->trellis)
        s->dct_quantize  = dct_quantize_trellis_c;

    return 0;
}

/* init video encoder */
av_cold int ff_mpv_encode_init(AVCodecContext *avctx)
{
    MpegEncContext *s = avctx->priv_data;
    AVCPBProperties *cpb_props;
    int i, ret, format_supported;

    mpv_encode_defaults(s);

    switch (avctx->codec_id) {
    case AV_CODEC_ID_MPEG2VIDEO:
        if (avctx->pix_fmt != AV_PIX_FMT_YUV420P &&
            avctx->pix_fmt != AV_PIX_FMT_YUV422P) {
            av_log(avctx, AV_LOG_ERROR,
                   "only YUV420 and YUV422 are supported\n");
            return -1;
        }
        break;
    case AV_CODEC_ID_MJPEG:
    case AV_CODEC_ID_AMV:
        format_supported = 0;
        /* JPEG color space */
        if (avctx->pix_fmt == AV_PIX_FMT_YUVJ420P ||
            avctx->pix_fmt == AV_PIX_FMT_YUVJ422P ||
            avctx->pix_fmt == AV_PIX_FMT_YUVJ444P ||
            (avctx->color_range == AVCOL_RANGE_JPEG &&
             (avctx->pix_fmt == AV_PIX_FMT_YUV420P ||
              avctx->pix_fmt == AV_PIX_FMT_YUV422P ||
              avctx->pix_fmt == AV_PIX_FMT_YUV444P)))
            format_supported = 1;
        /* MPEG color space */
        else if (avctx->strict_std_compliance <= FF_COMPLIANCE_UNOFFICIAL &&
                 (avctx->pix_fmt == AV_PIX_FMT_YUV420P ||
                  avctx->pix_fmt == AV_PIX_FMT_YUV422P ||
                  avctx->pix_fmt == AV_PIX_FMT_YUV444P))
            format_supported = 1;

        if (!format_supported) {
            av_log(avctx, AV_LOG_ERROR, "colorspace not supported in jpeg\n");
            return -1;
        }
        break;
    default:
        if (avctx->pix_fmt != AV_PIX_FMT_YUV420P) {
            av_log(avctx, AV_LOG_ERROR, "only YUV420 is supported\n");
            return -1;
        }
    }

    switch (avctx->pix_fmt) {
    case AV_PIX_FMT_YUVJ444P:
    case AV_PIX_FMT_YUV444P:
        s->chroma_format = CHROMA_444;
        break;
    case AV_PIX_FMT_YUVJ422P:
    case AV_PIX_FMT_YUV422P:
        s->chroma_format = CHROMA_422;
        break;
    case AV_PIX_FMT_YUVJ420P:
    case AV_PIX_FMT_YUV420P:
    default:
        s->chroma_format = CHROMA_420;
        break;
    }

    avctx->bits_per_raw_sample = av_clip(avctx->bits_per_raw_sample, 0, 8);

#if FF_API_PRIVATE_OPT
FF_DISABLE_DEPRECATION_WARNINGS
    if (avctx->rtp_payload_size)
        s->rtp_payload_size = avctx->rtp_payload_size;
    if (avctx->me_penalty_compensation)
        s->me_penalty_compensation = avctx->me_penalty_compensation;
    if (avctx->pre_me)
        s->me_pre = avctx->pre_me;
FF_ENABLE_DEPRECATION_WARNINGS
#endif

    s->bit_rate = avctx->bit_rate;
    s->width    = avctx->width;
    s->height   = avctx->height;
    if (avctx->gop_size > 600 &&
        avctx->strict_std_compliance > FF_COMPLIANCE_EXPERIMENTAL) {
        av_log(avctx, AV_LOG_WARNING,
               "keyframe interval too large!, reducing it from %d to %d\n",
               avctx->gop_size, 600);
        avctx->gop_size = 600;
    }
    s->gop_size     = avctx->gop_size;
    s->avctx        = avctx;
    if (avctx->max_b_frames > MAX_B_FRAMES) {
        av_log(avctx, AV_LOG_ERROR, "Too many B-frames requested, maximum "
               "is %d.\n", MAX_B_FRAMES);
        avctx->max_b_frames = MAX_B_FRAMES;
    }
    s->max_b_frames = avctx->max_b_frames;
    s->codec_id     = avctx->codec->id;
    s->strict_std_compliance = avctx->strict_std_compliance;
    s->quarter_sample     = (avctx->flags & AV_CODEC_FLAG_QPEL) != 0;
    s->rtp_mode           = !!s->rtp_payload_size;
    s->intra_dc_precision = avctx->intra_dc_precision;

    // workaround some differences between how applications specify dc precision
    if (s->intra_dc_precision < 0) {
        s->intra_dc_precision += 8;
    } else if (s->intra_dc_precision >= 8)
        s->intra_dc_precision -= 8;

    if (s->intra_dc_precision < 0) {
        av_log(avctx, AV_LOG_ERROR,
                "intra dc precision must be positive, note some applications use"
                " 0 and some 8 as base meaning 8bit, the value must not be smaller than that\n");
        return AVERROR(EINVAL);
    }

    if (avctx->codec_id == AV_CODEC_ID_AMV || (avctx->active_thread_type & FF_THREAD_SLICE))
        s->huffman = 0;

    if (s->intra_dc_precision > (avctx->codec_id == AV_CODEC_ID_MPEG2VIDEO ? 3 : 0)) {
        av_log(avctx, AV_LOG_ERROR, "intra dc precision too large\n");
        return AVERROR(EINVAL);
    }
    s->user_specified_pts = AV_NOPTS_VALUE;

    if (s->gop_size <= 1) {
        s->intra_only = 1;
        s->gop_size   = 12;
    } else {
        s->intra_only = 0;
    }

    /* Fixed QSCALE */
    s->fixed_qscale = !!(avctx->flags & AV_CODEC_FLAG_QSCALE);

    s->adaptive_quant = (s->avctx->lumi_masking ||
                         s->avctx->dark_masking ||
                         s->avctx->temporal_cplx_masking ||
                         s->avctx->spatial_cplx_masking  ||
                         s->avctx->p_masking      ||
                         s->border_masking ||
                         (s->mpv_flags & FF_MPV_FLAG_QP_RD)) &&
                        !s->fixed_qscale;

    s->loop_filter = !!(s->avctx->flags & AV_CODEC_FLAG_LOOP_FILTER);

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
        return -1;
    }

    if (avctx->rc_min_rate && avctx->rc_max_rate != avctx->rc_min_rate) {
        av_log(avctx, AV_LOG_INFO,
               "Warning min_rate > 0 but min_rate != max_rate isn't recommended!\n");
    }

    if (avctx->rc_min_rate && avctx->rc_min_rate > avctx->bit_rate) {
        av_log(avctx, AV_LOG_ERROR, "bitrate below min bitrate\n");
        return -1;
    }

    if (avctx->rc_max_rate && avctx->rc_max_rate < avctx->bit_rate) {
        av_log(avctx, AV_LOG_ERROR, "bitrate above max bitrate\n");
        return -1;
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
        return -1;
    }

    if (!s->fixed_qscale &&
        avctx->bit_rate * av_q2d(avctx->time_base) >
            avctx->bit_rate_tolerance) {
        av_log(avctx, AV_LOG_WARNING,
               "bitrate tolerance %d too small for bitrate %"PRId64", overriding\n", avctx->bit_rate_tolerance, avctx->bit_rate);
        avctx->bit_rate_tolerance = 5 * avctx->bit_rate * av_q2d(avctx->time_base);
    }

    if (s->avctx->rc_max_rate &&
        s->avctx->rc_min_rate == s->avctx->rc_max_rate &&
        (s->codec_id == AV_CODEC_ID_MPEG1VIDEO ||
         s->codec_id == AV_CODEC_ID_MPEG2VIDEO) &&
        90000LL * (avctx->rc_buffer_size - 1) >
            s->avctx->rc_max_rate * 0xFFFFLL) {
        av_log(avctx, AV_LOG_INFO,
               "Warning vbv_delay will be set to 0xFFFF (=VBR) as the "
               "specified vbv buffer is too large for the given bitrate!\n");
    }

    if ((s->avctx->flags & AV_CODEC_FLAG_4MV) && s->codec_id != AV_CODEC_ID_MPEG4 &&
        s->codec_id != AV_CODEC_ID_H263 && s->codec_id != AV_CODEC_ID_H263P &&
        s->codec_id != AV_CODEC_ID_FLV1) {
        av_log(avctx, AV_LOG_ERROR, "4MV not supported by codec\n");
        return -1;
    }

    if (s->obmc && s->avctx->mb_decision != FF_MB_DECISION_SIMPLE) {
        av_log(avctx, AV_LOG_ERROR,
               "OBMC is only supported with simple mb decision\n");
        return -1;
    }

    if (s->quarter_sample && s->codec_id != AV_CODEC_ID_MPEG4) {
        av_log(avctx, AV_LOG_ERROR, "qpel not supported by codec\n");
        return -1;
    }

    if (s->max_b_frames                    &&
        s->codec_id != AV_CODEC_ID_MPEG4      &&
        s->codec_id != AV_CODEC_ID_MPEG1VIDEO &&
        s->codec_id != AV_CODEC_ID_MPEG2VIDEO) {
        av_log(avctx, AV_LOG_ERROR, "B-frames not supported by codec\n");
        return -1;
    }
    if (s->max_b_frames < 0) {
        av_log(avctx, AV_LOG_ERROR,
               "max b frames must be 0 or positive for mpegvideo based encoders\n");
        return -1;
    }

    if ((s->codec_id == AV_CODEC_ID_MPEG4 ||
         s->codec_id == AV_CODEC_ID_H263  ||
         s->codec_id == AV_CODEC_ID_H263P) &&
        (avctx->sample_aspect_ratio.num > 255 ||
         avctx->sample_aspect_ratio.den > 255)) {
        av_log(avctx, AV_LOG_WARNING,
               "Invalid pixel aspect ratio %i/%i, limit is 255/255 reducing\n",
               avctx->sample_aspect_ratio.num, avctx->sample_aspect_ratio.den);
        av_reduce(&avctx->sample_aspect_ratio.num, &avctx->sample_aspect_ratio.den,
                   avctx->sample_aspect_ratio.num,  avctx->sample_aspect_ratio.den, 255);
    }

    if ((s->codec_id == AV_CODEC_ID_H263  ||
         s->codec_id == AV_CODEC_ID_H263P) &&
        (avctx->width  > 2048 ||
         avctx->height > 1152 )) {
        av_log(avctx, AV_LOG_ERROR, "H.263 does not support resolutions above 2048x1152\n");
        return -1;
    }
    if ((s->codec_id == AV_CODEC_ID_H263  ||
         s->codec_id == AV_CODEC_ID_H263P) &&
        ((avctx->width &3) ||
         (avctx->height&3) )) {
        av_log(avctx, AV_LOG_ERROR, "w/h must be a multiple of 4\n");
        return -1;
    }

    if (s->codec_id == AV_CODEC_ID_MPEG1VIDEO &&
        (avctx->width  > 4095 ||
         avctx->height > 4095 )) {
        av_log(avctx, AV_LOG_ERROR, "MPEG-1 does not support resolutions above 4095x4095\n");
        return -1;
    }

    if (s->codec_id == AV_CODEC_ID_MPEG2VIDEO &&
        (avctx->width  > 16383 ||
         avctx->height > 16383 )) {
        av_log(avctx, AV_LOG_ERROR, "MPEG-2 does not support resolutions above 16383x16383\n");
        return -1;
    }

    if (s->codec_id == AV_CODEC_ID_RV10 &&
        (avctx->width &15 ||
         avctx->height&15 )) {
        av_log(avctx, AV_LOG_ERROR, "width and height must be a multiple of 16\n");
        return AVERROR(EINVAL);
    }

    if (s->codec_id == AV_CODEC_ID_RV20 &&
        (avctx->width &3 ||
         avctx->height&3 )) {
        av_log(avctx, AV_LOG_ERROR, "width and height must be a multiple of 4\n");
        return AVERROR(EINVAL);
    }

    if ((s->codec_id == AV_CODEC_ID_WMV1 ||
         s->codec_id == AV_CODEC_ID_WMV2) &&
         avctx->width & 1) {
         av_log(avctx, AV_LOG_ERROR, "width must be multiple of 2\n");
         return -1;
    }

    if ((s->avctx->flags & (AV_CODEC_FLAG_INTERLACED_DCT | AV_CODEC_FLAG_INTERLACED_ME)) &&
        s->codec_id != AV_CODEC_ID_MPEG4 && s->codec_id != AV_CODEC_ID_MPEG2VIDEO) {
        av_log(avctx, AV_LOG_ERROR, "interlacing not supported by codec\n");
        return -1;
    }

#if FF_API_PRIVATE_OPT
    FF_DISABLE_DEPRECATION_WARNINGS
    if (avctx->mpeg_quant)
        s->mpeg_quant = avctx->mpeg_quant;
    FF_ENABLE_DEPRECATION_WARNINGS
#endif

    // FIXME mpeg2 uses that too
    if (s->mpeg_quant && (   s->codec_id != AV_CODEC_ID_MPEG4
                          && s->codec_id != AV_CODEC_ID_MPEG2VIDEO)) {
        av_log(avctx, AV_LOG_ERROR,
               "mpeg2 style quantization not supported by codec\n");
        return -1;
    }

    if ((s->mpv_flags & FF_MPV_FLAG_CBP_RD) && !avctx->trellis) {
        av_log(avctx, AV_LOG_ERROR, "CBP RD needs trellis quant\n");
        return -1;
    }

    if ((s->mpv_flags & FF_MPV_FLAG_QP_RD) &&
        s->avctx->mb_decision != FF_MB_DECISION_RD) {
        av_log(avctx, AV_LOG_ERROR, "QP RD needs mbd=2\n");
        return -1;
    }

    if ((s->mpv_flags & FF_MPV_FLAG_QP_RD) &&
            (s->codec_id == AV_CODEC_ID_AMV ||
             s->codec_id == AV_CODEC_ID_MJPEG)) {
        // Used to produce garbage with MJPEG.
        av_log(avctx, AV_LOG_ERROR,
               "QP RD is no longer compatible with MJPEG or AMV\n");
        return -1;
    }

#if FF_API_PRIVATE_OPT
FF_DISABLE_DEPRECATION_WARNINGS
    if (avctx->scenechange_threshold)
        s->scenechange_threshold = avctx->scenechange_threshold;
FF_ENABLE_DEPRECATION_WARNINGS
#endif

    if (s->scenechange_threshold < 1000000000 &&
        (s->avctx->flags & AV_CODEC_FLAG_CLOSED_GOP)) {
        av_log(avctx, AV_LOG_ERROR,
               "closed gop with scene change detection are not supported yet, "
               "set threshold to 1000000000\n");
        return -1;
    }

    if (s->avctx->flags & AV_CODEC_FLAG_LOW_DELAY) {
        if (s->codec_id != AV_CODEC_ID_MPEG2VIDEO &&
            s->strict_std_compliance >= FF_COMPLIANCE_NORMAL) {
            av_log(avctx, AV_LOG_ERROR,
                   "low delay forcing is only available for mpeg2, "
                   "set strict_std_compliance to 'unofficial' or lower in order to allow it\n");
            return -1;
        }
        if (s->max_b_frames != 0) {
            av_log(avctx, AV_LOG_ERROR,
                   "B-frames cannot be used with low delay\n");
            return -1;
        }
    }

    if (s->q_scale_type == 1) {
        if (avctx->qmax > 28) {
            av_log(avctx, AV_LOG_ERROR,
                   "non linear quant only supports qmax <= 28 currently\n");
            return -1;
        }
    }

    if (avctx->slices > 1 &&
        (avctx->codec_id == AV_CODEC_ID_FLV1 || avctx->codec_id == AV_CODEC_ID_H261)) {
        av_log(avctx, AV_LOG_ERROR, "Multiple slices are not supported by this codec\n");
        return AVERROR(EINVAL);
    }

    if (s->avctx->thread_count > 1         &&
        s->codec_id != AV_CODEC_ID_MPEG4      &&
        s->codec_id != AV_CODEC_ID_MPEG1VIDEO &&
        s->codec_id != AV_CODEC_ID_MPEG2VIDEO &&
        s->codec_id != AV_CODEC_ID_MJPEG      &&
        (s->codec_id != AV_CODEC_ID_H263P)) {
        av_log(avctx, AV_LOG_ERROR,
               "multi threaded encoding not supported by codec\n");
        return -1;
    }

    if (s->avctx->thread_count < 1) {
        av_log(avctx, AV_LOG_ERROR,
               "automatic thread number detection not supported by codec, "
               "patch welcome\n");
        return -1;
    }

    if (!avctx->time_base.den || !avctx->time_base.num) {
        av_log(avctx, AV_LOG_ERROR, "framerate not set\n");
        return -1;
    }

#if FF_API_PRIVATE_OPT
FF_DISABLE_DEPRECATION_WARNINGS
    if (avctx->b_frame_strategy)
        s->b_frame_strategy = avctx->b_frame_strategy;
    if (avctx->b_sensitivity != 40)
        s->b_sensitivity = avctx->b_sensitivity;
FF_ENABLE_DEPRECATION_WARNINGS
#endif

    if (s->b_frame_strategy && (avctx->flags & AV_CODEC_FLAG_PASS2)) {
        av_log(avctx, AV_LOG_INFO,
               "notice: b_frame_strategy only affects the first pass\n");
        s->b_frame_strategy = 0;
    }

    i = av_gcd(avctx->time_base.den, avctx->time_base.num);
    if (i > 1) {
        av_log(avctx, AV_LOG_INFO, "removing common factors from framerate\n");
        avctx->time_base.den /= i;
        avctx->time_base.num /= i;
        //return -1;
    }

    if (s->mpeg_quant || s->codec_id == AV_CODEC_ID_MPEG1VIDEO || s->codec_id == AV_CODEC_ID_MPEG2VIDEO || s->codec_id == AV_CODEC_ID_MJPEG || s->codec_id==AV_CODEC_ID_AMV) {
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

    if (avctx->codec_id == AV_CODEC_ID_MPEG4 &&
        s->avctx->time_base.den > (1 << 16) - 1) {
        av_log(avctx, AV_LOG_ERROR,
               "timebase %d/%d not supported by MPEG 4 standard, "
               "the maximum admitted value for the timebase denominator "
               "is %d\n", s->avctx->time_base.num, s->avctx->time_base.den,
               (1 << 16) - 1);
        return -1;
    }
    s->time_increment_bits = av_log2(s->avctx->time_base.den - 1) + 1;

    switch (avctx->codec->id) {
    case AV_CODEC_ID_MPEG1VIDEO:
        s->out_format = FMT_MPEG1;
        s->low_delay  = !!(s->avctx->flags & AV_CODEC_FLAG_LOW_DELAY);
        avctx->delay  = s->low_delay ? 0 : (s->max_b_frames + 1);
        break;
    case AV_CODEC_ID_MPEG2VIDEO:
        s->out_format = FMT_MPEG1;
        s->low_delay  = !!(s->avctx->flags & AV_CODEC_FLAG_LOW_DELAY);
        avctx->delay  = s->low_delay ? 0 : (s->max_b_frames + 1);
        s->rtp_mode   = 1;
        break;
    case AV_CODEC_ID_MJPEG:
    case AV_CODEC_ID_AMV:
        s->out_format = FMT_MJPEG;
        s->intra_only = 1; /* force intra only for jpeg */
        if (!CONFIG_MJPEG_ENCODER ||
            ff_mjpeg_encode_init(s) < 0)
            return -1;
        avctx->delay = 0;
        s->low_delay = 1;
        break;
    case AV_CODEC_ID_H261:
        if (!CONFIG_H261_ENCODER)
            return -1;
        if (ff_h261_get_picture_format(s->width, s->height) < 0) {
            av_log(avctx, AV_LOG_ERROR,
                   "The specified picture size of %dx%d is not valid for the "
                   "H.261 codec.\nValid sizes are 176x144, 352x288\n",
                    s->width, s->height);
            return -1;
        }
        s->out_format = FMT_H261;
        avctx->delay  = 0;
        s->low_delay  = 1;
        s->rtp_mode   = 0; /* Sliced encoding not supported */
        break;
    case AV_CODEC_ID_H263:
        if (!CONFIG_H263_ENCODER)
            return -1;
        if (ff_match_2uint16(ff_h263_format, FF_ARRAY_ELEMS(ff_h263_format),
                             s->width, s->height) == 8) {
            av_log(avctx, AV_LOG_ERROR,
                   "The specified picture size of %dx%d is not valid for "
                   "the H.263 codec.\nValid sizes are 128x96, 176x144, "
                   "352x288, 704x576, and 1408x1152. "
                   "Try H.263+.\n", s->width, s->height);
            return -1;
        }
        s->out_format = FMT_H263;
        avctx->delay  = 0;
        s->low_delay  = 1;
        break;
    case AV_CODEC_ID_H263P:
        s->out_format = FMT_H263;
        s->h263_plus  = 1;
        /* Fx */
        s->h263_aic        = (avctx->flags & AV_CODEC_FLAG_AC_PRED) ? 1 : 0;
        s->modified_quant  = s->h263_aic;
        s->loop_filter     = (avctx->flags & AV_CODEC_FLAG_LOOP_FILTER) ? 1 : 0;
        s->unrestricted_mv = s->obmc || s->loop_filter || s->umvplus;

        /* /Fx */
        /* These are just to be sure */
        avctx->delay = 0;
        s->low_delay = 1;
        break;
    case AV_CODEC_ID_FLV1:
        s->out_format      = FMT_H263;
        s->h263_flv        = 2; /* format = 1; 11-bit codes */
        s->unrestricted_mv = 1;
        s->rtp_mode  = 0; /* don't allow GOB */
        avctx->delay = 0;
        s->low_delay = 1;
        break;
    case AV_CODEC_ID_RV10:
        s->out_format = FMT_H263;
        avctx->delay  = 0;
        s->low_delay  = 1;
        break;
    case AV_CODEC_ID_RV20:
        s->out_format      = FMT_H263;
        avctx->delay       = 0;
        s->low_delay       = 1;
        s->modified_quant  = 1;
        s->h263_aic        = 1;
        s->h263_plus       = 1;
        s->loop_filter     = 1;
        s->unrestricted_mv = 0;
        break;
    case AV_CODEC_ID_MPEG4:
        s->out_format      = FMT_H263;
        s->h263_pred       = 1;
        s->unrestricted_mv = 1;
        s->low_delay       = s->max_b_frames ? 0 : 1;
        avctx->delay       = s->low_delay ? 0 : (s->max_b_frames + 1);
        break;
    case AV_CODEC_ID_MSMPEG4V2:
        s->out_format      = FMT_H263;
        s->h263_pred       = 1;
        s->unrestricted_mv = 1;
        s->msmpeg4_version = 2;
        avctx->delay       = 0;
        s->low_delay       = 1;
        break;
    case AV_CODEC_ID_MSMPEG4V3:
        s->out_format        = FMT_H263;
        s->h263_pred         = 1;
        s->unrestricted_mv   = 1;
        s->msmpeg4_version   = 3;
        s->flipflop_rounding = 1;
        avctx->delay         = 0;
        s->low_delay         = 1;
        break;
    case AV_CODEC_ID_WMV1:
        s->out_format        = FMT_H263;
        s->h263_pred         = 1;
        s->unrestricted_mv   = 1;
        s->msmpeg4_version   = 4;
        s->flipflop_rounding = 1;
        avctx->delay         = 0;
        s->low_delay         = 1;
        break;
    case AV_CODEC_ID_WMV2:
        s->out_format        = FMT_H263;
        s->h263_pred         = 1;
        s->unrestricted_mv   = 1;
        s->msmpeg4_version   = 5;
        s->flipflop_rounding = 1;
        avctx->delay         = 0;
        s->low_delay         = 1;
        break;
    default:
        return -1;
    }

#if FF_API_PRIVATE_OPT
    FF_DISABLE_DEPRECATION_WARNINGS
    if (avctx->noise_reduction)
        s->noise_reduction = avctx->noise_reduction;
    FF_ENABLE_DEPRECATION_WARNINGS
#endif

    avctx->has_b_frames = !s->low_delay;

    s->encoding = 1;

    s->progressive_frame    =
    s->progressive_sequence = !(avctx->flags & (AV_CODEC_FLAG_INTERLACED_DCT |
                                                AV_CODEC_FLAG_INTERLACED_ME) ||
                                s->alternate_scan);

    /* init */
    ff_mpv_idct_init(s);
    if (ff_mpv_common_init(s) < 0)
        return -1;

    ff_fdctdsp_init(&s->fdsp, avctx);
    ff_me_cmp_init(&s->mecc, avctx);
    ff_mpegvideoencdsp_init(&s->mpvencdsp, avctx);
    ff_pixblockdsp_init(&s->pdsp, avctx);
    ff_qpeldsp_init(&s->qdsp);

    if (s->msmpeg4_version) {
        FF_ALLOCZ_OR_GOTO(s->avctx, s->ac_stats,
                          2 * 2 * (MAX_LEVEL + 1) *
                          (MAX_RUN + 1) * 2 * sizeof(int), fail);
    }
    FF_ALLOCZ_OR_GOTO(s->avctx, s->avctx->stats_out, 256, fail);

    FF_ALLOCZ_OR_GOTO(s->avctx, s->q_intra_matrix,   64 * 32 * sizeof(int), fail);
    FF_ALLOCZ_OR_GOTO(s->avctx, s->q_chroma_intra_matrix, 64 * 32 * sizeof(int), fail);
    FF_ALLOCZ_OR_GOTO(s->avctx, s->q_inter_matrix,   64 * 32 * sizeof(int), fail);
    FF_ALLOCZ_OR_GOTO(s->avctx, s->q_intra_matrix16, 64 * 32 * 2 * sizeof(uint16_t), fail);
    FF_ALLOCZ_OR_GOTO(s->avctx, s->q_chroma_intra_matrix16, 64 * 32 * 2 * sizeof(uint16_t), fail);
    FF_ALLOCZ_OR_GOTO(s->avctx, s->q_inter_matrix16, 64 * 32 * 2 * sizeof(uint16_t), fail);
    FF_ALLOCZ_OR_GOTO(s->avctx, s->input_picture,
                      MAX_PICTURE_COUNT * sizeof(Picture *), fail);
    FF_ALLOCZ_OR_GOTO(s->avctx, s->reordered_input_picture,
                      MAX_PICTURE_COUNT * sizeof(Picture *), fail);


    if (s->noise_reduction) {
        FF_ALLOCZ_OR_GOTO(s->avctx, s->dct_offset,
                          2 * 64 * sizeof(uint16_t), fail);
    }

    ff_dct_encode_init(s);

    if ((CONFIG_H263P_ENCODER || CONFIG_RV20_ENCODER) && s->modified_quant)
        s->chroma_qscale_table = ff_h263_chroma_qscale_table;

    if (s->slice_context_count > 1) {
        s->rtp_mode = 1;

        if (avctx->codec_id == AV_CODEC_ID_H263P)
            s->h263_slice_structured = 1;
    }

    s->quant_precision = 5;

#if FF_API_PRIVATE_OPT
FF_DISABLE_DEPRECATION_WARNINGS
    if (avctx->frame_skip_threshold)
        s->frame_skip_threshold = avctx->frame_skip_threshold;
    if (avctx->frame_skip_factor)
        s->frame_skip_factor = avctx->frame_skip_factor;
    if (avctx->frame_skip_exp)
        s->frame_skip_exp = avctx->frame_skip_exp;
    if (avctx->frame_skip_cmp != FF_CMP_DCTMAX)
        s->frame_skip_cmp = avctx->frame_skip_cmp;
FF_ENABLE_DEPRECATION_WARNINGS
#endif

    ff_set_cmp(&s->mecc, s->mecc.ildct_cmp,      s->avctx->ildct_cmp);
    ff_set_cmp(&s->mecc, s->mecc.frame_skip_cmp, s->frame_skip_cmp);

    if (CONFIG_H261_ENCODER && s->out_format == FMT_H261)
        ff_h261_encode_init(s);
    if (CONFIG_H263_ENCODER && s->out_format == FMT_H263)
        ff_h263_encode_init(s);
    if (CONFIG_MSMPEG4_ENCODER && s->msmpeg4_version)
        if ((ret = ff_msmpeg4_encode_init(s)) < 0)
            return ret;
    if ((CONFIG_MPEG1VIDEO_ENCODER || CONFIG_MPEG2VIDEO_ENCODER)
        && s->out_format == FMT_MPEG1)
        ff_mpeg1_encode_init(s);

    /* init q matrix */
    for (i = 0; i < 64; i++) {
        int j = s->idsp.idct_permutation[i];
        if (CONFIG_MPEG4_ENCODER && s->codec_id == AV_CODEC_ID_MPEG4 &&
            s->mpeg_quant) {
            s->intra_matrix[j] = ff_mpeg4_default_intra_matrix[i];
            s->inter_matrix[j] = ff_mpeg4_default_non_intra_matrix[i];
        } else if (s->out_format == FMT_H263 || s->out_format == FMT_H261) {
            s->intra_matrix[j] =
            s->inter_matrix[j] = ff_mpeg1_default_non_intra_matrix[i];
        } else {
            /* MPEG-1/2 */
            s->chroma_intra_matrix[j] =
            s->intra_matrix[j] = ff_mpeg1_default_intra_matrix[i];
            s->inter_matrix[j] = ff_mpeg1_default_non_intra_matrix[i];
        }
        if (s->avctx->intra_matrix)
            s->intra_matrix[j] = s->avctx->intra_matrix[i];
        if (s->avctx->inter_matrix)
            s->inter_matrix[j] = s->avctx->inter_matrix[i];
    }

    /* precompute matrix */
    /* for mjpeg, we do include qscale in the matrix */
    if (s->out_format != FMT_MJPEG) {
        ff_convert_matrix(s, s->q_intra_matrix, s->q_intra_matrix16,
                          s->intra_matrix, s->intra_quant_bias, avctx->qmin,
                          31, 1);
        ff_convert_matrix(s, s->q_inter_matrix, s->q_inter_matrix16,
                          s->inter_matrix, s->inter_quant_bias, avctx->qmin,
                          31, 0);
    }

    if (ff_rate_control_init(s) < 0)
        return -1;

#if FF_API_PRIVATE_OPT
    FF_DISABLE_DEPRECATION_WARNINGS
    if (avctx->brd_scale)
        s->brd_scale = avctx->brd_scale;

    if (avctx->prediction_method)
        s->pred = avctx->prediction_method + 1;
    FF_ENABLE_DEPRECATION_WARNINGS
#endif

    if (s->b_frame_strategy == 2) {
        for (i = 0; i < s->max_b_frames + 2; i++) {
            s->tmp_frames[i] = av_frame_alloc();
            if (!s->tmp_frames[i])
                return AVERROR(ENOMEM);

            s->tmp_frames[i]->format = AV_PIX_FMT_YUV420P;
            s->tmp_frames[i]->width  = s->width  >> s->brd_scale;
            s->tmp_frames[i]->height = s->height >> s->brd_scale;

            ret = av_frame_get_buffer(s->tmp_frames[i], 32);
            if (ret < 0)
                return ret;
        }
    }

    cpb_props = ff_add_cpb_side_data(avctx);
    if (!cpb_props)
        return AVERROR(ENOMEM);
    cpb_props->max_bitrate = avctx->rc_max_rate;
    cpb_props->min_bitrate = avctx->rc_min_rate;
    cpb_props->avg_bitrate = avctx->bit_rate;
    cpb_props->buffer_size = avctx->rc_buffer_size;

    return 0;
fail:
    ff_mpv_encode_end(avctx);
    return AVERROR_UNKNOWN;
}

av_cold int ff_mpv_encode_end(AVCodecContext *avctx)
{
    MpegEncContext *s = avctx->priv_data;
    int i;

    ff_rate_control_uninit(s);

    ff_mpv_common_end(s);
    if (CONFIG_MJPEG_ENCODER &&
        s->out_format == FMT_MJPEG)
        ff_mjpeg_encode_close(s);

    av_freep(&avctx->extradata);

    for (i = 0; i < FF_ARRAY_ELEMS(s->tmp_frames); i++)
        av_frame_free(&s->tmp_frames[i]);

    ff_free_picture_tables(&s->new_picture);
    ff_mpeg_unref_picture(s->avctx, &s->new_picture);

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

    return 0;
}

static int get_sae(uint8_t *src, int ref, int stride)
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

static int get_intra_count(MpegEncContext *s, uint8_t *src,
                           uint8_t *ref, int stride)
{
    int x, y, w, h;
    int acc = 0;

    w = s->width  & ~15;
    h = s->height & ~15;

    for (y = 0; y < h; y += 16) {
        for (x = 0; x < w; x += 16) {
            int offset = x + y * stride;
            int sad  = s->mecc.sad[0](NULL, src + offset, ref + offset,
                                      stride, 16);
            int mean = (s->mpvencdsp.pix_sum(src + offset, stride) + 128) >> 8;
            int sae  = get_sae(src + offset, mean, stride);

            acc += sae + 500 < sad;
        }
    }
    return acc;
}

static int alloc_picture(MpegEncContext *s, Picture *pic, int shared)
{
    return ff_alloc_picture(s->avctx, pic, &s->me, &s->sc, shared, 1,
                            s->chroma_x_shift, s->chroma_y_shift, s->out_format,
                            s->mb_stride, s->mb_width, s->mb_height, s->b8_stride,
                            &s->linesize, &s->uvlinesize);
}

static int load_input_picture(MpegEncContext *s, const AVFrame *pic_arg)
{
    Picture *pic = NULL;
    int64_t pts;
    int i, display_picture_number = 0, ret;
    int encoding_delay = s->max_b_frames ? s->max_b_frames
                                         : (s->low_delay ? 0 : 1);
    int flush_offset = 1;
    int direct = 1;

    if (pic_arg) {
        pts = pic_arg->pts;
        display_picture_number = s->input_picture_number++;

        if (pts != AV_NOPTS_VALUE) {
            if (s->user_specified_pts != AV_NOPTS_VALUE) {
                int64_t last = s->user_specified_pts;

                if (pts <= last) {
                    av_log(s->avctx, AV_LOG_ERROR,
                           "Invalid pts (%"PRId64") <= last (%"PRId64")\n",
                           pts, last);
                    return AVERROR(EINVAL);
                }

                if (!s->low_delay && display_picture_number == 1)
                    s->dts_delta = pts - last;
            }
            s->user_specified_pts = pts;
        } else {
            if (s->user_specified_pts != AV_NOPTS_VALUE) {
                s->user_specified_pts =
                pts = s->user_specified_pts + 1;
                av_log(s->avctx, AV_LOG_INFO,
                       "Warning: AVFrame.pts=? trying to guess (%"PRId64")\n",
                       pts);
            } else {
                pts = display_picture_number;
            }
        }

        if (!pic_arg->buf[0] ||
            pic_arg->linesize[0] != s->linesize ||
            pic_arg->linesize[1] != s->uvlinesize ||
            pic_arg->linesize[2] != s->uvlinesize)
            direct = 0;
        if ((s->width & 15) || (s->height & 15))
            direct = 0;
        if (((intptr_t)(pic_arg->data[0])) & (STRIDE_ALIGN-1))
            direct = 0;
        if (s->linesize & (STRIDE_ALIGN-1))
            direct = 0;

        ff_dlog(s->avctx, "%d %d %"PTRDIFF_SPECIFIER" %"PTRDIFF_SPECIFIER"\n", pic_arg->linesize[0],
                pic_arg->linesize[1], s->linesize, s->uvlinesize);

        i = ff_find_unused_picture(s->avctx, s->picture, direct);
        if (i < 0)
            return i;

        pic = &s->picture[i];
        pic->reference = 3;

        if (direct) {
            if ((ret = av_frame_ref(pic->f, pic_arg)) < 0)
                return ret;
        }
        ret = alloc_picture(s, pic, direct);
        if (ret < 0)
            return ret;

        if (!direct) {
            if (pic->f->data[0] + INPLACE_OFFSET == pic_arg->data[0] &&
                pic->f->data[1] + INPLACE_OFFSET == pic_arg->data[1] &&
                pic->f->data[2] + INPLACE_OFFSET == pic_arg->data[2]) {
                // empty
            } else {
                int h_chroma_shift, v_chroma_shift;
                av_pix_fmt_get_chroma_sub_sample(s->avctx->pix_fmt,
                                                 &h_chroma_shift,
                                                 &v_chroma_shift);

                for (i = 0; i < 3; i++) {
                    int src_stride = pic_arg->linesize[i];
                    int dst_stride = i ? s->uvlinesize : s->linesize;
                    int h_shift = i ? h_chroma_shift : 0;
                    int v_shift = i ? v_chroma_shift : 0;
                    int w = s->width  >> h_shift;
                    int h = s->height >> v_shift;
                    uint8_t *src = pic_arg->data[i];
                    uint8_t *dst = pic->f->data[i];
                    int vpad = 16;

                    if (   s->codec_id == AV_CODEC_ID_MPEG2VIDEO
                        && !s->progressive_sequence
                        && FFALIGN(s->height, 32) - s->height > 16)
                        vpad = 32;

                    if (!s->avctx->rc_buffer_size)
                        dst += INPLACE_OFFSET;

                    if (src_stride == dst_stride)
                        memcpy(dst, src, src_stride * h);
                    else {
                        int h2 = h;
                        uint8_t *dst2 = dst;
                        while (h2--) {
                            memcpy(dst2, src, w);
                            dst2 += dst_stride;
                            src += src_stride;
                        }
                    }
                    if ((s->width & 15) || (s->height & (vpad-1))) {
                        s->mpvencdsp.draw_edges(dst, dst_stride,
                                                w, h,
                                                16 >> h_shift,
                                                vpad >> v_shift,
                                                EDGE_BOTTOM);
                    }
                }
                emms_c();
            }
        }
        ret = av_frame_copy_props(pic->f, pic_arg);
        if (ret < 0)
            return ret;

        pic->f->display_picture_number = display_picture_number;
        pic->f->pts = pts; // we set this here to avoid modifying pic_arg
    } else {
        /* Flushing: When we have not received enough input frames,
         * ensure s->input_picture[0] contains the first picture */
        for (flush_offset = 0; flush_offset < encoding_delay + 1; flush_offset++)
            if (s->input_picture[flush_offset])
                break;

        if (flush_offset <= 1)
            flush_offset = 1;
        else
            encoding_delay = encoding_delay - flush_offset + 1;
    }

    /* shift buffer entries */
    for (i = flush_offset; i < MAX_PICTURE_COUNT /*s->encoding_delay + 1*/; i++)
        s->input_picture[i - flush_offset] = s->input_picture[i];

    s->input_picture[encoding_delay] = (Picture*) pic;

    return 0;
}

static int skip_check(MpegEncContext *s, Picture *p, Picture *ref)
{
    int x, y, plane;
    int score = 0;
    int64_t score64 = 0;

    for (plane = 0; plane < 3; plane++) {
        const int stride = p->f->linesize[plane];
        const int bw = plane ? 1 : 2;
        for (y = 0; y < s->mb_height * bw; y++) {
            for (x = 0; x < s->mb_width * bw; x++) {
                int off = p->shared ? 0 : 16;
                uint8_t *dptr = p->f->data[plane] + 8 * (x + y * stride) + off;
                uint8_t *rptr = ref->f->data[plane] + 8 * (x + y * stride);
                int v = s->mecc.frame_skip_cmp[1](s, dptr, rptr, stride, 8);

                switch (FFABS(s->frame_skip_exp)) {
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
    if (s->frame_skip_exp < 0)
        score64 = pow(score64 / (double)(s->mb_width * s->mb_height),
                      -1.0/s->frame_skip_exp);

    if (score64 < s->frame_skip_threshold)
        return 1;
    if (score64 < ((s->frame_skip_factor * (int64_t) s->lambda) >> 8))
        return 1;
    return 0;
}

static int encode_frame(AVCodecContext *c, AVFrame *frame)
{
    AVPacket pkt = { 0 };
    int ret;
    int size = 0;

    av_init_packet(&pkt);

    ret = avcodec_send_frame(c, frame);
    if (ret < 0)
        return ret;

    do {
        ret = avcodec_receive_packet(c, &pkt);
        if (ret >= 0) {
            size += pkt.size;
            av_packet_unref(&pkt);
        } else if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
            return ret;
    } while (ret >= 0);

    return size;
}

static int estimate_best_b_count(MpegEncContext *s)
{
    const AVCodec *codec = avcodec_find_encoder(s->avctx->codec_id);
    const int scale = s->brd_scale;
    int width  = s->width  >> scale;
    int height = s->height >> scale;
    int i, j, out_size, p_lambda, b_lambda, lambda2;
    int64_t best_rd  = INT64_MAX;
    int best_b_count = -1;
    int ret = 0;

    av_assert0(scale >= 0 && scale <= 3);

    //emms_c();
    //s->next_picture_ptr->quality;
    p_lambda = s->last_lambda_for[AV_PICTURE_TYPE_P];
    //p_lambda * FFABS(s->avctx->b_quant_factor) + s->avctx->b_quant_offset;
    b_lambda = s->last_lambda_for[AV_PICTURE_TYPE_B];
    if (!b_lambda) // FIXME we should do this somewhere else
        b_lambda = p_lambda;
    lambda2  = (b_lambda * b_lambda + (1 << FF_LAMBDA_SHIFT) / 2) >>
               FF_LAMBDA_SHIFT;

    for (i = 0; i < s->max_b_frames + 2; i++) {
        Picture pre_input, *pre_input_ptr = i ? s->input_picture[i - 1] :
                                                s->next_picture_ptr;
        uint8_t *data[4];

        if (pre_input_ptr && (!i || s->input_picture[i - 1])) {
            pre_input = *pre_input_ptr;
            memcpy(data, pre_input_ptr->f->data, sizeof(data));

            if (!pre_input.shared && i) {
                data[0] += INPLACE_OFFSET;
                data[1] += INPLACE_OFFSET;
                data[2] += INPLACE_OFFSET;
            }

            s->mpvencdsp.shrink[scale](s->tmp_frames[i]->data[0],
                                       s->tmp_frames[i]->linesize[0],
                                       data[0],
                                       pre_input.f->linesize[0],
                                       width, height);
            s->mpvencdsp.shrink[scale](s->tmp_frames[i]->data[1],
                                       s->tmp_frames[i]->linesize[1],
                                       data[1],
                                       pre_input.f->linesize[1],
                                       width >> 1, height >> 1);
            s->mpvencdsp.shrink[scale](s->tmp_frames[i]->data[2],
                                       s->tmp_frames[i]->linesize[2],
                                       data[2],
                                       pre_input.f->linesize[2],
                                       width >> 1, height >> 1);
        }
    }

    for (j = 0; j < s->max_b_frames + 1; j++) {
        AVCodecContext *c;
        int64_t rd = 0;

        if (!s->input_picture[j])
            break;

        c = avcodec_alloc_context3(NULL);
        if (!c)
            return AVERROR(ENOMEM);

        c->width        = width;
        c->height       = height;
        c->flags        = AV_CODEC_FLAG_QSCALE | AV_CODEC_FLAG_PSNR;
        c->flags       |= s->avctx->flags & AV_CODEC_FLAG_QPEL;
        c->mb_decision  = s->avctx->mb_decision;
        c->me_cmp       = s->avctx->me_cmp;
        c->mb_cmp       = s->avctx->mb_cmp;
        c->me_sub_cmp   = s->avctx->me_sub_cmp;
        c->pix_fmt      = AV_PIX_FMT_YUV420P;
        c->time_base    = s->avctx->time_base;
        c->max_b_frames = s->max_b_frames;

        ret = avcodec_open2(c, codec, NULL);
        if (ret < 0)
            goto fail;

        s->tmp_frames[0]->pict_type = AV_PICTURE_TYPE_I;
        s->tmp_frames[0]->quality   = 1 * FF_QP2LAMBDA;

        out_size = encode_frame(c, s->tmp_frames[0]);
        if (out_size < 0) {
            ret = out_size;
            goto fail;
        }

        //rd += (out_size * lambda2) >> FF_LAMBDA_SHIFT;

        for (i = 0; i < s->max_b_frames + 1; i++) {
            int is_p = i % (j + 1) == j || i == s->max_b_frames;

            s->tmp_frames[i + 1]->pict_type = is_p ?
                                     AV_PICTURE_TYPE_P : AV_PICTURE_TYPE_B;
            s->tmp_frames[i + 1]->quality   = is_p ? p_lambda : b_lambda;

            out_size = encode_frame(c, s->tmp_frames[i + 1]);
            if (out_size < 0) {
                ret = out_size;
                goto fail;
            }

            rd += (out_size * lambda2) >> (FF_LAMBDA_SHIFT - 3);
        }

        /* get the delayed frames */
        out_size = encode_frame(c, NULL);
        if (out_size < 0) {
            ret = out_size;
            goto fail;
        }
        rd += (out_size * lambda2) >> (FF_LAMBDA_SHIFT - 3);

        rd += c->error[0] + c->error[1] + c->error[2];

        if (rd < best_rd) {
            best_rd = rd;
            best_b_count = j;
        }

fail:
        avcodec_free_context(&c);
        if (ret < 0)
            return ret;
    }

    return best_b_count;
}

static int select_input_picture(MpegEncContext *s)
{
    int i, ret;

    for (i = 1; i < MAX_PICTURE_COUNT; i++)
        s->reordered_input_picture[i - 1] = s->reordered_input_picture[i];
    s->reordered_input_picture[MAX_PICTURE_COUNT - 1] = NULL;

    /* set next picture type & ordering */
    if (!s->reordered_input_picture[0] && s->input_picture[0]) {
        if (s->frame_skip_threshold || s->frame_skip_factor) {
            if (s->picture_in_gop_number < s->gop_size &&
                s->next_picture_ptr &&
                skip_check(s, s->input_picture[0], s->next_picture_ptr)) {
                // FIXME check that the gop check above is +-1 correct
                av_frame_unref(s->input_picture[0]->f);

                ff_vbv_update(s, 0);

                goto no_output_pic;
            }
        }

        if (/*s->picture_in_gop_number >= s->gop_size ||*/
            !s->next_picture_ptr || s->intra_only) {
            s->reordered_input_picture[0] = s->input_picture[0];
            s->reordered_input_picture[0]->f->pict_type = AV_PICTURE_TYPE_I;
            s->reordered_input_picture[0]->f->coded_picture_number =
                s->coded_picture_number++;
        } else {
            int b_frames = 0;

            if (s->avctx->flags & AV_CODEC_FLAG_PASS2) {
                for (i = 0; i < s->max_b_frames + 1; i++) {
                    int pict_num = s->input_picture[0]->f->display_picture_number + i;

                    if (pict_num >= s->rc_context.num_entries)
                        break;
                    if (!s->input_picture[i]) {
                        s->rc_context.entry[pict_num - 1].new_pict_type = AV_PICTURE_TYPE_P;
                        break;
                    }

                    s->input_picture[i]->f->pict_type =
                        s->rc_context.entry[pict_num].new_pict_type;
                }
            }

            if (s->b_frame_strategy == 0) {
                b_frames = s->max_b_frames;
                while (b_frames && !s->input_picture[b_frames])
                    b_frames--;
            } else if (s->b_frame_strategy == 1) {
                for (i = 1; i < s->max_b_frames + 1; i++) {
                    if (s->input_picture[i] &&
                        s->input_picture[i]->b_frame_score == 0) {
                        s->input_picture[i]->b_frame_score =
                            get_intra_count(s,
                                            s->input_picture[i    ]->f->data[0],
                                            s->input_picture[i - 1]->f->data[0],
                                            s->linesize) + 1;
                    }
                }
                for (i = 0; i < s->max_b_frames + 1; i++) {
                    if (!s->input_picture[i] ||
                        s->input_picture[i]->b_frame_score - 1 >
                            s->mb_num / s->b_sensitivity)
                        break;
                }

                b_frames = FFMAX(0, i - 1);

                /* reset scores */
                for (i = 0; i < b_frames + 1; i++) {
                    s->input_picture[i]->b_frame_score = 0;
                }
            } else if (s->b_frame_strategy == 2) {
                b_frames = estimate_best_b_count(s);
                if (b_frames < 0)
                    return b_frames;
            }

            emms_c();

            for (i = b_frames - 1; i >= 0; i--) {
                int type = s->input_picture[i]->f->pict_type;
                if (type && type != AV_PICTURE_TYPE_B)
                    b_frames = i;
            }
            if (s->input_picture[b_frames]->f->pict_type == AV_PICTURE_TYPE_B &&
                b_frames == s->max_b_frames) {
                av_log(s->avctx, AV_LOG_ERROR,
                       "warning, too many B-frames in a row\n");
            }

            if (s->picture_in_gop_number + b_frames >= s->gop_size) {
                if ((s->mpv_flags & FF_MPV_FLAG_STRICT_GOP) &&
                    s->gop_size > s->picture_in_gop_number) {
                    b_frames = s->gop_size - s->picture_in_gop_number - 1;
                } else {
                    if (s->avctx->flags & AV_CODEC_FLAG_CLOSED_GOP)
                        b_frames = 0;
                    s->input_picture[b_frames]->f->pict_type = AV_PICTURE_TYPE_I;
                }
            }

            if ((s->avctx->flags & AV_CODEC_FLAG_CLOSED_GOP) && b_frames &&
                s->input_picture[b_frames]->f->pict_type == AV_PICTURE_TYPE_I)
                b_frames--;

            s->reordered_input_picture[0] = s->input_picture[b_frames];
            if (s->reordered_input_picture[0]->f->pict_type != AV_PICTURE_TYPE_I)
                s->reordered_input_picture[0]->f->pict_type = AV_PICTURE_TYPE_P;
            s->reordered_input_picture[0]->f->coded_picture_number =
                s->coded_picture_number++;
            for (i = 0; i < b_frames; i++) {
                s->reordered_input_picture[i + 1] = s->input_picture[i];
                s->reordered_input_picture[i + 1]->f->pict_type =
                    AV_PICTURE_TYPE_B;
                s->reordered_input_picture[i + 1]->f->coded_picture_number =
                    s->coded_picture_number++;
            }
        }
    }
no_output_pic:
    ff_mpeg_unref_picture(s->avctx, &s->new_picture);

    if (s->reordered_input_picture[0]) {
        s->reordered_input_picture[0]->reference =
           s->reordered_input_picture[0]->f->pict_type !=
               AV_PICTURE_TYPE_B ? 3 : 0;

        if ((ret = ff_mpeg_ref_picture(s->avctx, &s->new_picture, s->reordered_input_picture[0])))
            return ret;

        if (s->reordered_input_picture[0]->shared || s->avctx->rc_buffer_size) {
            // input is a shared pix, so we can't modify it -> allocate a new
            // one & ensure that the shared one is reuseable

            Picture *pic;
            int i = ff_find_unused_picture(s->avctx, s->picture, 0);
            if (i < 0)
                return i;
            pic = &s->picture[i];

            pic->reference = s->reordered_input_picture[0]->reference;
            if (alloc_picture(s, pic, 0) < 0) {
                return -1;
            }

            ret = av_frame_copy_props(pic->f, s->reordered_input_picture[0]->f);
            if (ret < 0)
                return ret;

            /* mark us unused / free shared pic */
            av_frame_unref(s->reordered_input_picture[0]->f);
            s->reordered_input_picture[0]->shared = 0;

            s->current_picture_ptr = pic;
        } else {
            // input is not a shared pix -> reuse buffer for current_pix
            s->current_picture_ptr = s->reordered_input_picture[0];
            for (i = 0; i < 4; i++) {
                s->new_picture.f->data[i] += INPLACE_OFFSET;
            }
        }
        ff_mpeg_unref_picture(s->avctx, &s->current_picture);
        if ((ret = ff_mpeg_ref_picture(s->avctx, &s->current_picture,
                                       s->current_picture_ptr)) < 0)
            return ret;

        s->picture_number = s->new_picture.f->display_picture_number;
    }
    return 0;
}

static void frame_end(MpegEncContext *s)
{
    if (s->unrestricted_mv &&
        s->current_picture.reference &&
        !s->intra_only) {
        const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(s->avctx->pix_fmt);
        int hshift = desc->log2_chroma_w;
        int vshift = desc->log2_chroma_h;
        s->mpvencdsp.draw_edges(s->current_picture.f->data[0],
                                s->current_picture.f->linesize[0],
                                s->h_edge_pos, s->v_edge_pos,
                                EDGE_WIDTH, EDGE_WIDTH,
                                EDGE_TOP | EDGE_BOTTOM);
        s->mpvencdsp.draw_edges(s->current_picture.f->data[1],
                                s->current_picture.f->linesize[1],
                                s->h_edge_pos >> hshift,
                                s->v_edge_pos >> vshift,
                                EDGE_WIDTH >> hshift,
                                EDGE_WIDTH >> vshift,
                                EDGE_TOP | EDGE_BOTTOM);
        s->mpvencdsp.draw_edges(s->current_picture.f->data[2],
                                s->current_picture.f->linesize[2],
                                s->h_edge_pos >> hshift,
                                s->v_edge_pos >> vshift,
                                EDGE_WIDTH >> hshift,
                                EDGE_WIDTH >> vshift,
                                EDGE_TOP | EDGE_BOTTOM);
    }

    emms_c();

    s->last_pict_type                 = s->pict_type;
    s->last_lambda_for [s->pict_type] = s->current_picture_ptr->f->quality;
    if (s->pict_type!= AV_PICTURE_TYPE_B)
        s->last_non_b_pict_type = s->pict_type;

#if FF_API_CODED_FRAME
FF_DISABLE_DEPRECATION_WARNINGS
    av_frame_unref(s->avctx->coded_frame);
    av_frame_copy_props(s->avctx->coded_frame, s->current_picture.f);
FF_ENABLE_DEPRECATION_WARNINGS
#endif
#if FF_API_ERROR_FRAME
FF_DISABLE_DEPRECATION_WARNINGS
    memcpy(s->current_picture.f->error, s->current_picture.encoding_error,
           sizeof(s->current_picture.encoding_error));
FF_ENABLE_DEPRECATION_WARNINGS
#endif
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
            s->dct_offset[intra][i] = (s->noise_reduction *
                                       s->dct_count[intra] +
                                       s->dct_error_sum[intra][i] / 2) /
                                      (s->dct_error_sum[intra][i] + 1);
        }
    }
}

static int frame_start(MpegEncContext *s)
{
    int ret;

    /* mark & release old frames */
    if (s->pict_type != AV_PICTURE_TYPE_B && s->last_picture_ptr &&
        s->last_picture_ptr != s->next_picture_ptr &&
        s->last_picture_ptr->f->buf[0]) {
        ff_mpeg_unref_picture(s->avctx, s->last_picture_ptr);
    }

    s->current_picture_ptr->f->pict_type = s->pict_type;
    s->current_picture_ptr->f->key_frame = s->pict_type == AV_PICTURE_TYPE_I;

    ff_mpeg_unref_picture(s->avctx, &s->current_picture);
    if ((ret = ff_mpeg_ref_picture(s->avctx, &s->current_picture,
                                   s->current_picture_ptr)) < 0)
        return ret;

    if (s->pict_type != AV_PICTURE_TYPE_B) {
        s->last_picture_ptr = s->next_picture_ptr;
        if (!s->droppable)
            s->next_picture_ptr = s->current_picture_ptr;
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
        av_assert2(s->noise_reduction && s->encoding);
        update_noise_reduction(s);
    }

    return 0;
}

int ff_mpv_encode_picture(AVCodecContext *avctx, AVPacket *pkt,
                          const AVFrame *pic_arg, int *got_packet)
{
    MpegEncContext *s = avctx->priv_data;
    int i, stuffing_count, ret;
    int context_count = s->slice_context_count;

    s->vbv_ignore_qmax = 0;

    s->picture_in_gop_number++;

    if (load_input_picture(s, pic_arg) < 0)
        return -1;

    if (select_input_picture(s) < 0) {
        return -1;
    }

    /* output? */
    if (s->new_picture.f->data[0]) {
        int growing_buffer = context_count == 1 && !pkt->data && !s->data_partitioning;
        int pkt_size = growing_buffer ? FFMAX(s->mb_width*s->mb_height*64+10000, avctx->internal->byte_buffer_size) - AV_INPUT_BUFFER_PADDING_SIZE
                                              :
                                              s->mb_width*s->mb_height*(MAX_MB_BYTES+100)+10000;
        if ((ret = ff_alloc_packet2(avctx, pkt, pkt_size, 0)) < 0)
            return ret;
        if (s->mb_info) {
            s->mb_info_ptr = av_packet_new_side_data(pkt,
                                 AV_PKT_DATA_H263_MB_INFO,
                                 s->mb_width*s->mb_height*12);
            s->prev_mb_info = s->last_mb_info = s->mb_info_size = 0;
        }

        for (i = 0; i < context_count; i++) {
            int start_y = s->thread_context[i]->start_mb_y;
            int   end_y = s->thread_context[i]->  end_mb_y;
            int h       = s->mb_height;
            uint8_t *start = pkt->data + (size_t)(((int64_t) pkt->size) * start_y / h);
            uint8_t *end   = pkt->data + (size_t)(((int64_t) pkt->size) *   end_y / h);

            init_put_bits(&s->thread_context[i]->pb, start, end - start);
        }

        s->pict_type = s->new_picture.f->pict_type;
        //emms_c();
        ret = frame_start(s);
        if (ret < 0)
            return ret;
vbv_retry:
        ret = encode_picture(s, s->picture_number);
        if (growing_buffer) {
            av_assert0(s->pb.buf == avctx->internal->byte_buffer);
            pkt->data = s->pb.buf;
            pkt->size = avctx->internal->byte_buffer_size;
        }
        if (ret < 0)
            return -1;

#if FF_API_STAT_BITS
FF_DISABLE_DEPRECATION_WARNINGS
        avctx->header_bits = s->header_bits;
        avctx->mv_bits     = s->mv_bits;
        avctx->misc_bits   = s->misc_bits;
        avctx->i_tex_bits  = s->i_tex_bits;
        avctx->p_tex_bits  = s->p_tex_bits;
        avctx->i_count     = s->i_count;
        // FIXME f/b_count in avctx
        avctx->p_count     = s->mb_num - s->i_count - s->skip_count;
        avctx->skip_count  = s->skip_count;
FF_ENABLE_DEPRECATION_WARNINGS
#endif

        frame_end(s);

        if (CONFIG_MJPEG_ENCODER && s->out_format == FMT_MJPEG)
            ff_mjpeg_encode_picture_trailer(&s->pb, s->header_bits);

        if (avctx->rc_buffer_size) {
            RateControlContext *rcc = &s->rc_context;
            int max_size = FFMAX(rcc->buffer_index * avctx->rc_max_available_vbv_use, rcc->buffer_index - 500);
            int hq = (s->avctx->mb_decision == FF_MB_DECISION_RD || s->avctx->trellis);
            int min_step = hq ? 1 : (1<<(FF_LAMBDA_SHIFT + 7))/139;

            if (put_bits_count(&s->pb) > max_size &&
                s->lambda < s->lmax) {
                s->next_lambda = FFMAX(s->lambda + min_step, s->lambda *
                                       (s->qscale + 1) / s->qscale);
                if (s->adaptive_quant) {
                    int i;
                    for (i = 0; i < s->mb_height * s->mb_stride; i++)
                        s->lambda_table[i] =
                            FFMAX(s->lambda_table[i] + min_step,
                                  s->lambda_table[i] * (s->qscale + 1) /
                                  s->qscale);
                }
                s->mb_skipped = 0;        // done in frame_start()
                // done in encode_picture() so we must undo it
                if (s->pict_type == AV_PICTURE_TYPE_P) {
                    if (s->flipflop_rounding          ||
                        s->codec_id == AV_CODEC_ID_H263P ||
                        s->codec_id == AV_CODEC_ID_MPEG4)
                        s->no_rounding ^= 1;
                }
                if (s->pict_type != AV_PICTURE_TYPE_B) {
                    s->time_base       = s->last_time_base;
                    s->last_non_b_time = s->time - s->pp_time;
                }
                for (i = 0; i < context_count; i++) {
                    PutBitContext *pb = &s->thread_context[i]->pb;
                    init_put_bits(pb, pb->buf, pb->buf_end - pb->buf);
                }
                s->vbv_ignore_qmax = 1;
                av_log(s->avctx, AV_LOG_VERBOSE, "reencoding frame due to VBV\n");
                goto vbv_retry;
            }

            av_assert0(s->avctx->rc_max_rate);
        }

        if (s->avctx->flags & AV_CODEC_FLAG_PASS1)
            ff_write_pass1_stats(s);

        for (i = 0; i < 4; i++) {
            s->current_picture_ptr->encoding_error[i] = s->current_picture.encoding_error[i];
            avctx->error[i] += s->current_picture_ptr->encoding_error[i];
        }
        ff_side_data_set_encoder_stats(pkt, s->current_picture.f->quality,
                                       s->current_picture_ptr->encoding_error,
                                       (s->avctx->flags&AV_CODEC_FLAG_PSNR) ? 4 : 0,
                                       s->pict_type);

        if (s->avctx->flags & AV_CODEC_FLAG_PASS1)
            assert(put_bits_count(&s->pb) == s->header_bits + s->mv_bits +
                                             s->misc_bits + s->i_tex_bits +
                                             s->p_tex_bits);
        flush_put_bits(&s->pb);
        s->frame_bits  = put_bits_count(&s->pb);

        stuffing_count = ff_vbv_update(s, s->frame_bits);
        s->stuffing_bits = 8*stuffing_count;
        if (stuffing_count) {
            if (s->pb.buf_end - s->pb.buf - (put_bits_count(&s->pb) >> 3) <
                    stuffing_count + 50) {
                av_log(s->avctx, AV_LOG_ERROR, "stuffing too large\n");
                return -1;
            }

            switch (s->codec_id) {
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
                av_log(s->avctx, AV_LOG_ERROR, "vbv buffer overflow\n");
            }
            flush_put_bits(&s->pb);
            s->frame_bits  = put_bits_count(&s->pb);
        }

        /* update MPEG-1/2 vbv_delay for CBR */
        if (s->avctx->rc_max_rate                          &&
            s->avctx->rc_min_rate == s->avctx->rc_max_rate &&
            s->out_format == FMT_MPEG1                     &&
            90000LL * (avctx->rc_buffer_size - 1) <=
                s->avctx->rc_max_rate * 0xFFFFLL) {
            AVCPBProperties *props;
            size_t props_size;

            int vbv_delay, min_delay;
            double inbits  = s->avctx->rc_max_rate *
                             av_q2d(s->avctx->time_base);
            int    minbits = s->frame_bits - 8 *
                             (s->vbv_delay_ptr - s->pb.buf - 1);
            double bits    = s->rc_context.buffer_index + minbits - inbits;

            if (bits < 0)
                av_log(s->avctx, AV_LOG_ERROR,
                       "Internal error, negative bits\n");

            av_assert1(s->repeat_first_field == 0);

            vbv_delay = bits * 90000 / s->avctx->rc_max_rate;
            min_delay = (minbits * 90000LL + s->avctx->rc_max_rate - 1) /
                        s->avctx->rc_max_rate;

            vbv_delay = FFMAX(vbv_delay, min_delay);

            av_assert0(vbv_delay < 0xFFFF);

            s->vbv_delay_ptr[0] &= 0xF8;
            s->vbv_delay_ptr[0] |= vbv_delay >> 13;
            s->vbv_delay_ptr[1]  = vbv_delay >> 5;
            s->vbv_delay_ptr[2] &= 0x07;
            s->vbv_delay_ptr[2] |= vbv_delay << 3;

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

#if FF_API_VBV_DELAY
FF_DISABLE_DEPRECATION_WARNINGS
            avctx->vbv_delay     = vbv_delay * 300;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
        }
        s->total_bits     += s->frame_bits;
#if FF_API_STAT_BITS
FF_DISABLE_DEPRECATION_WARNINGS
        avctx->frame_bits  = s->frame_bits;
FF_ENABLE_DEPRECATION_WARNINGS
#endif


        pkt->pts = s->current_picture.f->pts;
        if (!s->low_delay && s->pict_type != AV_PICTURE_TYPE_B) {
            if (!s->current_picture.f->coded_picture_number)
                pkt->dts = pkt->pts - s->dts_delta;
            else
                pkt->dts = s->reordered_pts;
            s->reordered_pts = pkt->pts;
        } else
            pkt->dts = pkt->pts;
        if (s->current_picture.f->key_frame)
            pkt->flags |= AV_PKT_FLAG_KEY;
        if (s->mb_info)
            av_packet_shrink_side_data(pkt, AV_PKT_DATA_H263_MB_INFO, s->mb_info_size);
    } else {
        s->frame_bits = 0;
    }

    /* release non-reference frames */
    for (i = 0; i < MAX_PICTURE_COUNT; i++) {
        if (!s->picture[i].reference)
            ff_mpeg_unref_picture(s->avctx, &s->picture[i]);
    }

    av_assert1((s->frame_bits & 7) == 0);

    pkt->size = s->frame_bits / 8;
    *got_packet = !!pkt->size;
    return 0;
}

static inline void dct_single_coeff_elimination(MpegEncContext *s,
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
    int16_t *block = s->block[n];
    const int last_index = s->block_last_index[n];
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
        const int j = s->intra_scantable.permutated[i];
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
        const int j = s->intra_scantable.permutated[i];
        block[j] = 0;
    }
    if (block[0])
        s->block_last_index[n] = 0;
    else
        s->block_last_index[n] = -1;
}

static inline void clip_coeffs(MpegEncContext *s, int16_t *block,
                               int last_index)
{
    int i;
    const int maxlevel = s->max_qcoeff;
    const int minlevel = s->min_qcoeff;
    int overflow = 0;

    if (s->mb_intra) {
        i = 1; // skip clipping of intra dc
    } else
        i = 0;

    for (; i <= last_index; i++) {
        const int j = s->intra_scantable.permutated[i];
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

    if (overflow && s->avctx->mb_decision == FF_MB_DECISION_SIMPLE)
        av_log(s->avctx, AV_LOG_INFO,
               "warning, clipping %d dct coefficients to %d..%d\n",
               overflow, minlevel, maxlevel);
}

static void get_visual_weight(int16_t *weight, uint8_t *ptr, int stride)
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

static av_always_inline void encode_mb_internal(MpegEncContext *s,
                                                int motion_x, int motion_y,
                                                int mb_block_height,
                                                int mb_block_width,
                                                int mb_block_count)
{
    int16_t weight[12][64];
    int16_t orig[12][64];
    const int mb_x = s->mb_x;
    const int mb_y = s->mb_y;
    int i;
    int skip_dct[12];
    int dct_offset = s->linesize * 8; // default for progressive frames
    int uv_dct_offset = s->uvlinesize * 8;
    uint8_t *ptr_y, *ptr_cb, *ptr_cr;
    ptrdiff_t wrap_y, wrap_c;

    for (i = 0; i < mb_block_count; i++)
        skip_dct[i] = s->skipdct;

    if (s->adaptive_quant) {
        const int last_qp = s->qscale;
        const int mb_xy = mb_x + mb_y * s->mb_stride;

        s->lambda = s->lambda_table[mb_xy];
        update_qscale(s);

        if (!(s->mpv_flags & FF_MPV_FLAG_QP_RD)) {
            s->qscale = s->current_picture_ptr->qscale_table[mb_xy];
            s->dquant = s->qscale - last_qp;

            if (s->out_format == FMT_H263) {
                s->dquant = av_clip(s->dquant, -2, 2);

                if (s->codec_id == AV_CODEC_ID_MPEG4) {
                    if (!s->mb_intra) {
                        if (s->pict_type == AV_PICTURE_TYPE_B) {
                            if (s->dquant & 1 || s->mv_dir & MV_DIRECT)
                                s->dquant = 0;
                        }
                        if (s->mv_type == MV_TYPE_8X8)
                            s->dquant = 0;
                    }
                }
            }
        }
        ff_set_qscale(s, last_qp + s->dquant);
    } else if (s->mpv_flags & FF_MPV_FLAG_QP_RD)
        ff_set_qscale(s, s->qscale + s->dquant);

    wrap_y = s->linesize;
    wrap_c = s->uvlinesize;
    ptr_y  = s->new_picture.f->data[0] +
             (mb_y * 16 * wrap_y)              + mb_x * 16;
    ptr_cb = s->new_picture.f->data[1] +
             (mb_y * mb_block_height * wrap_c) + mb_x * mb_block_width;
    ptr_cr = s->new_picture.f->data[2] +
             (mb_y * mb_block_height * wrap_c) + mb_x * mb_block_width;

    if((mb_x * 16 + 16 > s->width || mb_y * 16 + 16 > s->height) && s->codec_id != AV_CODEC_ID_AMV){
        uint8_t *ebuf = s->sc.edge_emu_buffer + 38 * wrap_y;
        int cw = (s->width  + s->chroma_x_shift) >> s->chroma_x_shift;
        int ch = (s->height + s->chroma_y_shift) >> s->chroma_y_shift;
        s->vdsp.emulated_edge_mc(ebuf, ptr_y,
                                 wrap_y, wrap_y,
                                 16, 16, mb_x * 16, mb_y * 16,
                                 s->width, s->height);
        ptr_y = ebuf;
        s->vdsp.emulated_edge_mc(ebuf + 16 * wrap_y, ptr_cb,
                                 wrap_c, wrap_c,
                                 mb_block_width, mb_block_height,
                                 mb_x * mb_block_width, mb_y * mb_block_height,
                                 cw, ch);
        ptr_cb = ebuf + 16 * wrap_y;
        s->vdsp.emulated_edge_mc(ebuf + 16 * wrap_y + 16, ptr_cr,
                                 wrap_c, wrap_c,
                                 mb_block_width, mb_block_height,
                                 mb_x * mb_block_width, mb_y * mb_block_height,
                                 cw, ch);
        ptr_cr = ebuf + 16 * wrap_y + 16;
    }

    if (s->mb_intra) {
        if (s->avctx->flags & AV_CODEC_FLAG_INTERLACED_DCT) {
            int progressive_score, interlaced_score;

            s->interlaced_dct = 0;
            progressive_score = s->mecc.ildct_cmp[4](s, ptr_y, NULL, wrap_y, 8) +
                                s->mecc.ildct_cmp[4](s, ptr_y + wrap_y * 8,
                                                     NULL, wrap_y, 8) - 400;

            if (progressive_score > 0) {
                interlaced_score = s->mecc.ildct_cmp[4](s, ptr_y,
                                                        NULL, wrap_y * 2, 8) +
                                   s->mecc.ildct_cmp[4](s, ptr_y + wrap_y,
                                                        NULL, wrap_y * 2, 8);
                if (progressive_score > interlaced_score) {
                    s->interlaced_dct = 1;

                    dct_offset = wrap_y;
                    uv_dct_offset = wrap_c;
                    wrap_y <<= 1;
                    if (s->chroma_format == CHROMA_422 ||
                        s->chroma_format == CHROMA_444)
                        wrap_c <<= 1;
                }
            }
        }

        s->pdsp.get_pixels(s->block[0], ptr_y,                  wrap_y);
        s->pdsp.get_pixels(s->block[1], ptr_y + 8,              wrap_y);
        s->pdsp.get_pixels(s->block[2], ptr_y + dct_offset,     wrap_y);
        s->pdsp.get_pixels(s->block[3], ptr_y + dct_offset + 8, wrap_y);

        if (s->avctx->flags & AV_CODEC_FLAG_GRAY) {
            skip_dct[4] = 1;
            skip_dct[5] = 1;
        } else {
            s->pdsp.get_pixels(s->block[4], ptr_cb, wrap_c);
            s->pdsp.get_pixels(s->block[5], ptr_cr, wrap_c);
            if (!s->chroma_y_shift && s->chroma_x_shift) { /* 422 */
                s->pdsp.get_pixels(s->block[6], ptr_cb + uv_dct_offset, wrap_c);
                s->pdsp.get_pixels(s->block[7], ptr_cr + uv_dct_offset, wrap_c);
            } else if (!s->chroma_y_shift && !s->chroma_x_shift) { /* 444 */
                s->pdsp.get_pixels(s->block[ 6], ptr_cb + 8, wrap_c);
                s->pdsp.get_pixels(s->block[ 7], ptr_cr + 8, wrap_c);
                s->pdsp.get_pixels(s->block[ 8], ptr_cb + uv_dct_offset, wrap_c);
                s->pdsp.get_pixels(s->block[ 9], ptr_cr + uv_dct_offset, wrap_c);
                s->pdsp.get_pixels(s->block[10], ptr_cb + uv_dct_offset + 8, wrap_c);
                s->pdsp.get_pixels(s->block[11], ptr_cr + uv_dct_offset + 8, wrap_c);
            }
        }
    } else {
        op_pixels_func (*op_pix)[4];
        qpel_mc_func (*op_qpix)[16];
        uint8_t *dest_y, *dest_cb, *dest_cr;

        dest_y  = s->dest[0];
        dest_cb = s->dest[1];
        dest_cr = s->dest[2];

        if ((!s->no_rounding) || s->pict_type == AV_PICTURE_TYPE_B) {
            op_pix  = s->hdsp.put_pixels_tab;
            op_qpix = s->qdsp.put_qpel_pixels_tab;
        } else {
            op_pix  = s->hdsp.put_no_rnd_pixels_tab;
            op_qpix = s->qdsp.put_no_rnd_qpel_pixels_tab;
        }

        if (s->mv_dir & MV_DIR_FORWARD) {
            ff_mpv_motion(s, dest_y, dest_cb, dest_cr, 0,
                          s->last_picture.f->data,
                          op_pix, op_qpix);
            op_pix  = s->hdsp.avg_pixels_tab;
            op_qpix = s->qdsp.avg_qpel_pixels_tab;
        }
        if (s->mv_dir & MV_DIR_BACKWARD) {
            ff_mpv_motion(s, dest_y, dest_cb, dest_cr, 1,
                          s->next_picture.f->data,
                          op_pix, op_qpix);
        }

        if (s->avctx->flags & AV_CODEC_FLAG_INTERLACED_DCT) {
            int progressive_score, interlaced_score;

            s->interlaced_dct = 0;
            progressive_score = s->mecc.ildct_cmp[0](s, dest_y, ptr_y, wrap_y, 8) +
                                s->mecc.ildct_cmp[0](s, dest_y + wrap_y * 8,
                                                     ptr_y + wrap_y * 8,
                                                     wrap_y, 8) - 400;

            if (s->avctx->ildct_cmp == FF_CMP_VSSE)
                progressive_score -= 400;

            if (progressive_score > 0) {
                interlaced_score = s->mecc.ildct_cmp[0](s, dest_y, ptr_y,
                                                        wrap_y * 2, 8) +
                                   s->mecc.ildct_cmp[0](s, dest_y + wrap_y,
                                                        ptr_y + wrap_y,
                                                        wrap_y * 2, 8);

                if (progressive_score > interlaced_score) {
                    s->interlaced_dct = 1;

                    dct_offset = wrap_y;
                    uv_dct_offset = wrap_c;
                    wrap_y <<= 1;
                    if (s->chroma_format == CHROMA_422)
                        wrap_c <<= 1;
                }
            }
        }

        s->pdsp.diff_pixels(s->block[0], ptr_y, dest_y, wrap_y);
        s->pdsp.diff_pixels(s->block[1], ptr_y + 8, dest_y + 8, wrap_y);
        s->pdsp.diff_pixels(s->block[2], ptr_y + dct_offset,
                            dest_y + dct_offset, wrap_y);
        s->pdsp.diff_pixels(s->block[3], ptr_y + dct_offset + 8,
                            dest_y + dct_offset + 8, wrap_y);

        if (s->avctx->flags & AV_CODEC_FLAG_GRAY) {
            skip_dct[4] = 1;
            skip_dct[5] = 1;
        } else {
            s->pdsp.diff_pixels(s->block[4], ptr_cb, dest_cb, wrap_c);
            s->pdsp.diff_pixels(s->block[5], ptr_cr, dest_cr, wrap_c);
            if (!s->chroma_y_shift) { /* 422 */
                s->pdsp.diff_pixels(s->block[6], ptr_cb + uv_dct_offset,
                                    dest_cb + uv_dct_offset, wrap_c);
                s->pdsp.diff_pixels(s->block[7], ptr_cr + uv_dct_offset,
                                    dest_cr + uv_dct_offset, wrap_c);
            }
        }
        /* pre quantization */
        if (s->current_picture.mc_mb_var[s->mb_stride * mb_y + mb_x] <
                2 * s->qscale * s->qscale) {
            // FIXME optimize
            if (s->mecc.sad[1](NULL, ptr_y, dest_y, wrap_y, 8) < 20 * s->qscale)
                skip_dct[0] = 1;
            if (s->mecc.sad[1](NULL, ptr_y + 8, dest_y + 8, wrap_y, 8) < 20 * s->qscale)
                skip_dct[1] = 1;
            if (s->mecc.sad[1](NULL, ptr_y + dct_offset, dest_y + dct_offset,
                               wrap_y, 8) < 20 * s->qscale)
                skip_dct[2] = 1;
            if (s->mecc.sad[1](NULL, ptr_y + dct_offset + 8, dest_y + dct_offset + 8,
                               wrap_y, 8) < 20 * s->qscale)
                skip_dct[3] = 1;
            if (s->mecc.sad[1](NULL, ptr_cb, dest_cb, wrap_c, 8) < 20 * s->qscale)
                skip_dct[4] = 1;
            if (s->mecc.sad[1](NULL, ptr_cr, dest_cr, wrap_c, 8) < 20 * s->qscale)
                skip_dct[5] = 1;
            if (!s->chroma_y_shift) { /* 422 */
                if (s->mecc.sad[1](NULL, ptr_cb + uv_dct_offset,
                                   dest_cb + uv_dct_offset,
                                   wrap_c, 8) < 20 * s->qscale)
                    skip_dct[6] = 1;
                if (s->mecc.sad[1](NULL, ptr_cr + uv_dct_offset,
                                   dest_cr + uv_dct_offset,
                                   wrap_c, 8) < 20 * s->qscale)
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
        if (!s->chroma_y_shift) { /* 422 */
            if (!skip_dct[6])
                get_visual_weight(weight[6], ptr_cb + uv_dct_offset,
                                  wrap_c);
            if (!skip_dct[7])
                get_visual_weight(weight[7], ptr_cr + uv_dct_offset,
                                  wrap_c);
        }
        memcpy(orig[0], s->block[0], sizeof(int16_t) * 64 * mb_block_count);
    }

    /* DCT & quantize */
    av_assert2(s->out_format != FMT_MJPEG || s->qscale == 8);
    {
        for (i = 0; i < mb_block_count; i++) {
            if (!skip_dct[i]) {
                int overflow;
                s->block_last_index[i] = s->dct_quantize(s, s->block[i], i, s->qscale, &overflow);
                // FIXME we could decide to change to quantizer instead of
                // clipping
                // JS: I don't think that would be a good idea it could lower
                //     quality instead of improve it. Just INTRADC clipping
                //     deserves changes in quantizer
                if (overflow)
                    clip_coeffs(s, s->block[i], s->block_last_index[i]);
            } else
                s->block_last_index[i] = -1;
        }
        if (s->quantizer_noise_shaping) {
            for (i = 0; i < mb_block_count; i++) {
                if (!skip_dct[i]) {
                    s->block_last_index[i] =
                        dct_quantize_refine(s, s->block[i], weight[i],
                                            orig[i], i, s->qscale);
                }
            }
        }

        if (s->luma_elim_threshold && !s->mb_intra)
            for (i = 0; i < 4; i++)
                dct_single_coeff_elimination(s, i, s->luma_elim_threshold);
        if (s->chroma_elim_threshold && !s->mb_intra)
            for (i = 4; i < mb_block_count; i++)
                dct_single_coeff_elimination(s, i, s->chroma_elim_threshold);

        if (s->mpv_flags & FF_MPV_FLAG_CBP_RD) {
            for (i = 0; i < mb_block_count; i++) {
                if (s->block_last_index[i] == -1)
                    s->coded_score[i] = INT_MAX / 256;
            }
        }
    }

    if ((s->avctx->flags & AV_CODEC_FLAG_GRAY) && s->mb_intra) {
        s->block_last_index[4] =
        s->block_last_index[5] = 0;
        s->block[4][0] =
        s->block[5][0] = (1024 + s->c_dc_scale / 2) / s->c_dc_scale;
        if (!s->chroma_y_shift) { /* 422 / 444 */
            for (i=6; i<12; i++) {
                s->block_last_index[i] = 0;
                s->block[i][0] = s->block[4][0];
            }
        }
    }

    // non c quantize code returns incorrect block_last_index FIXME
    if (s->alternate_scan && s->dct_quantize != ff_dct_quantize_c) {
        for (i = 0; i < mb_block_count; i++) {
            int j;
            if (s->block_last_index[i] > 0) {
                for (j = 63; j > 0; j--) {
                    if (s->block[i][s->intra_scantable.permutated[j]])
                        break;
                }
                s->block_last_index[i] = j;
            }
        }
    }

    /* huffman encode */
    switch(s->codec_id){ //FIXME funct ptr could be slightly faster
    case AV_CODEC_ID_MPEG1VIDEO:
    case AV_CODEC_ID_MPEG2VIDEO:
        if (CONFIG_MPEG1VIDEO_ENCODER || CONFIG_MPEG2VIDEO_ENCODER)
            ff_mpeg1_encode_mb(s, s->block, motion_x, motion_y);
        break;
    case AV_CODEC_ID_MPEG4:
        if (CONFIG_MPEG4_ENCODER)
            ff_mpeg4_encode_mb(s, s->block, motion_x, motion_y);
        break;
    case AV_CODEC_ID_MSMPEG4V2:
    case AV_CODEC_ID_MSMPEG4V3:
    case AV_CODEC_ID_WMV1:
        if (CONFIG_MSMPEG4_ENCODER)
            ff_msmpeg4_encode_mb(s, s->block, motion_x, motion_y);
        break;
    case AV_CODEC_ID_WMV2:
        if (CONFIG_WMV2_ENCODER)
            ff_wmv2_encode_mb(s, s->block, motion_x, motion_y);
        break;
    case AV_CODEC_ID_H261:
        if (CONFIG_H261_ENCODER)
            ff_h261_encode_mb(s, s->block, motion_x, motion_y);
        break;
    case AV_CODEC_ID_H263:
    case AV_CODEC_ID_H263P:
    case AV_CODEC_ID_FLV1:
    case AV_CODEC_ID_RV10:
    case AV_CODEC_ID_RV20:
        if (CONFIG_H263_ENCODER)
            ff_h263_encode_mb(s, s->block, motion_x, motion_y);
        break;
    case AV_CODEC_ID_MJPEG:
    case AV_CODEC_ID_AMV:
        if (CONFIG_MJPEG_ENCODER)
            ff_mjpeg_encode_mb(s, s->block);
        break;
    default:
        av_assert1(0);
    }
}

static av_always_inline void encode_mb(MpegEncContext *s, int motion_x, int motion_y)
{
    if (s->chroma_format == CHROMA_420) encode_mb_internal(s, motion_x, motion_y,  8, 8, 6);
    else if (s->chroma_format == CHROMA_422) encode_mb_internal(s, motion_x, motion_y, 16, 8, 8);
    else encode_mb_internal(s, motion_x, motion_y, 16, 16, 12);
}

static inline void copy_context_before_encode(MpegEncContext *d, MpegEncContext *s, int type){
    int i;

    memcpy(d->last_mv, s->last_mv, 2*2*2*sizeof(int)); //FIXME is memcpy faster than a loop?

    /* MPEG-1 */
    d->mb_skip_run= s->mb_skip_run;
    for(i=0; i<3; i++)
        d->last_dc[i] = s->last_dc[i];

    /* statistics */
    d->mv_bits= s->mv_bits;
    d->i_tex_bits= s->i_tex_bits;
    d->p_tex_bits= s->p_tex_bits;
    d->i_count= s->i_count;
    d->f_count= s->f_count;
    d->b_count= s->b_count;
    d->skip_count= s->skip_count;
    d->misc_bits= s->misc_bits;
    d->last_bits= 0;

    d->mb_skipped= 0;
    d->qscale= s->qscale;
    d->dquant= s->dquant;

    d->esc3_level_length= s->esc3_level_length;
}

static inline void copy_context_after_encode(MpegEncContext *d, MpegEncContext *s, int type){
    int i;

    memcpy(d->mv, s->mv, 2*4*2*sizeof(int));
    memcpy(d->last_mv, s->last_mv, 2*2*2*sizeof(int)); //FIXME is memcpy faster than a loop?

    /* MPEG-1 */
    d->mb_skip_run= s->mb_skip_run;
    for(i=0; i<3; i++)
        d->last_dc[i] = s->last_dc[i];

    /* statistics */
    d->mv_bits= s->mv_bits;
    d->i_tex_bits= s->i_tex_bits;
    d->p_tex_bits= s->p_tex_bits;
    d->i_count= s->i_count;
    d->f_count= s->f_count;
    d->b_count= s->b_count;
    d->skip_count= s->skip_count;
    d->misc_bits= s->misc_bits;

    d->mb_intra= s->mb_intra;
    d->mb_skipped= s->mb_skipped;
    d->mv_type= s->mv_type;
    d->mv_dir= s->mv_dir;
    d->pb= s->pb;
    if(s->data_partitioning){
        d->pb2= s->pb2;
        d->tex_pb= s->tex_pb;
    }
    d->block= s->block;
    for(i=0; i<8; i++)
        d->block_last_index[i]= s->block_last_index[i];
    d->interlaced_dct= s->interlaced_dct;
    d->qscale= s->qscale;

    d->esc3_level_length= s->esc3_level_length;
}

static inline void encode_mb_hq(MpegEncContext *s, MpegEncContext *backup, MpegEncContext *best, int type,
                           PutBitContext pb[2], PutBitContext pb2[2], PutBitContext tex_pb[2],
                           int *dmin, int *next_block, int motion_x, int motion_y)
{
    int score;
    uint8_t *dest_backup[3];

    copy_context_before_encode(s, backup, type);

    s->block= s->blocks[*next_block];
    s->pb= pb[*next_block];
    if(s->data_partitioning){
        s->pb2   = pb2   [*next_block];
        s->tex_pb= tex_pb[*next_block];
    }

    if(*next_block){
        memcpy(dest_backup, s->dest, sizeof(s->dest));
        s->dest[0] = s->sc.rd_scratchpad;
        s->dest[1] = s->sc.rd_scratchpad + 16*s->linesize;
        s->dest[2] = s->sc.rd_scratchpad + 16*s->linesize + 8;
        av_assert0(s->linesize >= 32); //FIXME
    }

    encode_mb(s, motion_x, motion_y);

    score= put_bits_count(&s->pb);
    if(s->data_partitioning){
        score+= put_bits_count(&s->pb2);
        score+= put_bits_count(&s->tex_pb);
    }

    if(s->avctx->mb_decision == FF_MB_DECISION_RD){
        ff_mpv_reconstruct_mb(s, s->block);

        score *= s->lambda2;
        score += sse_mb(s) << FF_LAMBDA_SHIFT;
    }

    if(*next_block){
        memcpy(s->dest, dest_backup, sizeof(s->dest));
    }

    if(score<*dmin){
        *dmin= score;
        *next_block^=1;

        copy_context_after_encode(best, s, type);
    }
}

static int sse(MpegEncContext *s, uint8_t *src1, uint8_t *src2, int w, int h, int stride){
    const uint32_t *sq = ff_square_tab + 256;
    int acc=0;
    int x,y;

    if(w==16 && h==16)
        return s->mecc.sse[0](NULL, src1, src2, stride, 16);
    else if(w==8 && h==8)
        return s->mecc.sse[1](NULL, src1, src2, stride, 8);

    for(y=0; y<h; y++){
        for(x=0; x<w; x++){
            acc+= sq[src1[x + y*stride] - src2[x + y*stride]];
        }
    }

    av_assert2(acc>=0);

    return acc;
}

static int sse_mb(MpegEncContext *s){
    int w= 16;
    int h= 16;

    if(s->mb_x*16 + 16 > s->width ) w= s->width - s->mb_x*16;
    if(s->mb_y*16 + 16 > s->height) h= s->height- s->mb_y*16;

    if(w==16 && h==16)
      if(s->avctx->mb_cmp == FF_CMP_NSSE){
        return s->mecc.nsse[0](s, s->new_picture.f->data[0] + s->mb_x * 16 + s->mb_y * s->linesize   * 16, s->dest[0], s->linesize,   16) +
               s->mecc.nsse[1](s, s->new_picture.f->data[1] + s->mb_x *  8 + s->mb_y * s->uvlinesize *  8, s->dest[1], s->uvlinesize,  8) +
               s->mecc.nsse[1](s, s->new_picture.f->data[2] + s->mb_x *  8 + s->mb_y * s->uvlinesize *  8, s->dest[2], s->uvlinesize,  8);
      }else{
        return s->mecc.sse[0](NULL, s->new_picture.f->data[0] + s->mb_x * 16 + s->mb_y * s->linesize   * 16, s->dest[0], s->linesize,   16) +
               s->mecc.sse[1](NULL, s->new_picture.f->data[1] + s->mb_x *  8 + s->mb_y * s->uvlinesize *  8, s->dest[1], s->uvlinesize,  8) +
               s->mecc.sse[1](NULL, s->new_picture.f->data[2] + s->mb_x *  8 + s->mb_y * s->uvlinesize *  8, s->dest[2], s->uvlinesize,  8);
      }
    else
        return  sse(s, s->new_picture.f->data[0] + s->mb_x*16 + s->mb_y*s->linesize*16, s->dest[0], w, h, s->linesize)
               +sse(s, s->new_picture.f->data[1] + s->mb_x*8  + s->mb_y*s->uvlinesize*8,s->dest[1], w>>1, h>>1, s->uvlinesize)
               +sse(s, s->new_picture.f->data[2] + s->mb_x*8  + s->mb_y*s->uvlinesize*8,s->dest[2], w>>1, h>>1, s->uvlinesize);
}

static int pre_estimate_motion_thread(AVCodecContext *c, void *arg){
    MpegEncContext *s= *(void**)arg;


    s->me.pre_pass=1;
    s->me.dia_size= s->avctx->pre_dia_size;
    s->first_slice_line=1;
    for(s->mb_y= s->end_mb_y-1; s->mb_y >= s->start_mb_y; s->mb_y--) {
        for(s->mb_x=s->mb_width-1; s->mb_x >=0 ;s->mb_x--) {
            ff_pre_estimate_p_frame_motion(s, s->mb_x, s->mb_y);
        }
        s->first_slice_line=0;
    }

    s->me.pre_pass=0;

    return 0;
}

static int estimate_motion_thread(AVCodecContext *c, void *arg){
    MpegEncContext *s= *(void**)arg;

    ff_check_alignment();

    s->me.dia_size= s->avctx->dia_size;
    s->first_slice_line=1;
    for(s->mb_y= s->start_mb_y; s->mb_y < s->end_mb_y; s->mb_y++) {
        s->mb_x=0; //for block init below
        ff_init_block_index(s);
        for(s->mb_x=0; s->mb_x < s->mb_width; s->mb_x++) {
            s->block_index[0]+=2;
            s->block_index[1]+=2;
            s->block_index[2]+=2;
            s->block_index[3]+=2;

            /* compute motion vector & mb_type and store in context */
            if(s->pict_type==AV_PICTURE_TYPE_B)
                ff_estimate_b_frame_motion(s, s->mb_x, s->mb_y);
            else
                ff_estimate_p_frame_motion(s, s->mb_x, s->mb_y);
        }
        s->first_slice_line=0;
    }
    return 0;
}

static int mb_var_thread(AVCodecContext *c, void *arg){
    MpegEncContext *s= *(void**)arg;
    int mb_x, mb_y;

    ff_check_alignment();

    for(mb_y=s->start_mb_y; mb_y < s->end_mb_y; mb_y++) {
        for(mb_x=0; mb_x < s->mb_width; mb_x++) {
            int xx = mb_x * 16;
            int yy = mb_y * 16;
            uint8_t *pix = s->new_picture.f->data[0] + (yy * s->linesize) + xx;
            int varc;
            int sum = s->mpvencdsp.pix_sum(pix, s->linesize);

            varc = (s->mpvencdsp.pix_norm1(pix, s->linesize) -
                    (((unsigned) sum * sum) >> 8) + 500 + 128) >> 8;

            s->current_picture.mb_var [s->mb_stride * mb_y + mb_x] = varc;
            s->current_picture.mb_mean[s->mb_stride * mb_y + mb_x] = (sum+128)>>8;
            s->me.mb_var_sum_temp    += varc;
        }
    }
    return 0;
}

static void write_slice_end(MpegEncContext *s){
    if(CONFIG_MPEG4_ENCODER && s->codec_id==AV_CODEC_ID_MPEG4){
        if(s->partitioned_frame){
            ff_mpeg4_merge_partitions(s);
        }

        ff_mpeg4_stuffing(&s->pb);
    }else if(CONFIG_MJPEG_ENCODER && s->out_format == FMT_MJPEG){
        ff_mjpeg_encode_stuffing(s);
    }

    avpriv_align_put_bits(&s->pb);
    flush_put_bits(&s->pb);

    if ((s->avctx->flags & AV_CODEC_FLAG_PASS1) && !s->partitioned_frame)
        s->misc_bits+= get_bits_diff(s);
}

static void write_mb_info(MpegEncContext *s)
{
    uint8_t *ptr = s->mb_info_ptr + s->mb_info_size - 12;
    int offset = put_bits_count(&s->pb);
    int mba  = s->mb_x + s->mb_width * (s->mb_y % s->gob_index);
    int gobn = s->mb_y / s->gob_index;
    int pred_x, pred_y;
    if (CONFIG_H263_ENCODER)
        ff_h263_pred_motion(s, 0, 0, &pred_x, &pred_y);
    bytestream_put_le32(&ptr, offset);
    bytestream_put_byte(&ptr, s->qscale);
    bytestream_put_byte(&ptr, gobn);
    bytestream_put_le16(&ptr, mba);
    bytestream_put_byte(&ptr, pred_x); /* hmv1 */
    bytestream_put_byte(&ptr, pred_y); /* vmv1 */
    /* 4MV not implemented */
    bytestream_put_byte(&ptr, 0); /* hmv2 */
    bytestream_put_byte(&ptr, 0); /* vmv2 */
}

static void update_mb_info(MpegEncContext *s, int startcode)
{
    if (!s->mb_info)
        return;
    if (put_bits_count(&s->pb) - s->prev_mb_info*8 >= s->mb_info*8) {
        s->mb_info_size += 12;
        s->prev_mb_info = s->last_mb_info;
    }
    if (startcode) {
        s->prev_mb_info = put_bits_count(&s->pb)/8;
        /* This might have incremented mb_info_size above, and we return without
         * actually writing any info into that slot yet. But in that case,
         * this will be called again at the start of the after writing the
         * start code, actually writing the mb info. */
        return;
    }

    s->last_mb_info = put_bits_count(&s->pb)/8;
    if (!s->mb_info_size)
        s->mb_info_size += 12;
    write_mb_info(s);
}

int ff_mpv_reallocate_putbitbuffer(MpegEncContext *s, size_t threshold, size_t size_increase)
{
    if (   s->pb.buf_end - s->pb.buf - (put_bits_count(&s->pb)>>3) < threshold
        && s->slice_context_count == 1
        && s->pb.buf == s->avctx->internal->byte_buffer) {
        int lastgob_pos = s->ptr_lastgob - s->pb.buf;
        int vbv_pos     = s->vbv_delay_ptr - s->pb.buf;

        uint8_t *new_buffer = NULL;
        int new_buffer_size = 0;

        if ((s->avctx->internal->byte_buffer_size + size_increase) >= INT_MAX/8) {
            av_log(s->avctx, AV_LOG_ERROR, "Cannot reallocate putbit buffer\n");
            return AVERROR(ENOMEM);
        }

        emms_c();

        av_fast_padded_malloc(&new_buffer, &new_buffer_size,
                              s->avctx->internal->byte_buffer_size + size_increase);
        if (!new_buffer)
            return AVERROR(ENOMEM);

        memcpy(new_buffer, s->avctx->internal->byte_buffer, s->avctx->internal->byte_buffer_size);
        av_free(s->avctx->internal->byte_buffer);
        s->avctx->internal->byte_buffer      = new_buffer;
        s->avctx->internal->byte_buffer_size = new_buffer_size;
        rebase_put_bits(&s->pb, new_buffer, new_buffer_size);
        s->ptr_lastgob   = s->pb.buf + lastgob_pos;
        s->vbv_delay_ptr = s->pb.buf + vbv_pos;
    }
    if (s->pb.buf_end - s->pb.buf - (put_bits_count(&s->pb)>>3) < threshold)
        return AVERROR(EINVAL);
    return 0;
}

static int encode_thread(AVCodecContext *c, void *arg){
    MpegEncContext *s= *(void**)arg;
    int mb_x, mb_y;
    int chr_h= 16>>s->chroma_y_shift;
    int i, j;
    MpegEncContext best_s = { 0 }, backup_s;
    uint8_t bit_buf[2][MAX_MB_BYTES];
    uint8_t bit_buf2[2][MAX_MB_BYTES];
    uint8_t bit_buf_tex[2][MAX_MB_BYTES];
    PutBitContext pb[2], pb2[2], tex_pb[2];

    ff_check_alignment();

    for(i=0; i<2; i++){
        init_put_bits(&pb    [i], bit_buf    [i], MAX_MB_BYTES);
        init_put_bits(&pb2   [i], bit_buf2   [i], MAX_MB_BYTES);
        init_put_bits(&tex_pb[i], bit_buf_tex[i], MAX_MB_BYTES);
    }

    s->last_bits= put_bits_count(&s->pb);
    s->mv_bits=0;
    s->misc_bits=0;
    s->i_tex_bits=0;
    s->p_tex_bits=0;
    s->i_count=0;
    s->f_count=0;
    s->b_count=0;
    s->skip_count=0;

    for(i=0; i<3; i++){
        /* init last dc values */
        /* note: quant matrix value (8) is implied here */
        s->last_dc[i] = 128 << s->intra_dc_precision;

        s->current_picture.encoding_error[i] = 0;
    }
    if(s->codec_id==AV_CODEC_ID_AMV){
        s->last_dc[0] = 128*8/13;
        s->last_dc[1] = 128*8/14;
        s->last_dc[2] = 128*8/14;
    }
    s->mb_skip_run = 0;
    memset(s->last_mv, 0, sizeof(s->last_mv));

    s->last_mv_dir = 0;

    switch(s->codec_id){
    case AV_CODEC_ID_H263:
    case AV_CODEC_ID_H263P:
    case AV_CODEC_ID_FLV1:
        if (CONFIG_H263_ENCODER)
            s->gob_index = H263_GOB_HEIGHT(s->height);
        break;
    case AV_CODEC_ID_MPEG4:
        if(CONFIG_MPEG4_ENCODER && s->partitioned_frame)
            ff_mpeg4_init_partitions(s);
        break;
    }

    s->resync_mb_x=0;
    s->resync_mb_y=0;
    s->first_slice_line = 1;
    s->ptr_lastgob = s->pb.buf;
    for(mb_y= s->start_mb_y; mb_y < s->end_mb_y; mb_y++) {
        s->mb_x=0;
        s->mb_y= mb_y;

        ff_set_qscale(s, s->qscale);
        ff_init_block_index(s);

        for(mb_x=0; mb_x < s->mb_width; mb_x++) {
            int xy= mb_y*s->mb_stride + mb_x; // removed const, H261 needs to adjust this
            int mb_type= s->mb_type[xy];
//            int d;
            int dmin= INT_MAX;
            int dir;
            int size_increase =  s->avctx->internal->byte_buffer_size/4
                               + s->mb_width*MAX_MB_BYTES;

            ff_mpv_reallocate_putbitbuffer(s, MAX_MB_BYTES, size_increase);
            if(s->pb.buf_end - s->pb.buf - (put_bits_count(&s->pb)>>3) < MAX_MB_BYTES){
                av_log(s->avctx, AV_LOG_ERROR, "encoded frame too large\n");
                return -1;
            }
            if(s->data_partitioning){
                if(   s->pb2   .buf_end - s->pb2   .buf - (put_bits_count(&s->    pb2)>>3) < MAX_MB_BYTES
                   || s->tex_pb.buf_end - s->tex_pb.buf - (put_bits_count(&s->tex_pb )>>3) < MAX_MB_BYTES){
                    av_log(s->avctx, AV_LOG_ERROR, "encoded partitioned frame too large\n");
                    return -1;
                }
            }

            s->mb_x = mb_x;
            s->mb_y = mb_y;  // moved into loop, can get changed by H.261
            ff_update_block_index(s);

            if(CONFIG_H261_ENCODER && s->codec_id == AV_CODEC_ID_H261){
                ff_h261_reorder_mb_index(s);
                xy= s->mb_y*s->mb_stride + s->mb_x;
                mb_type= s->mb_type[xy];
            }

            /* write gob / video packet header  */
            if(s->rtp_mode){
                int current_packet_size, is_gob_start;

                current_packet_size= ((put_bits_count(&s->pb)+7)>>3) - (s->ptr_lastgob - s->pb.buf);

                is_gob_start = s->rtp_payload_size &&
                               current_packet_size >= s->rtp_payload_size &&
                               mb_y + mb_x > 0;

                if(s->start_mb_y == mb_y && mb_y > 0 && mb_x==0) is_gob_start=1;

                switch(s->codec_id){
                case AV_CODEC_ID_H263:
                case AV_CODEC_ID_H263P:
                    if(!s->h263_slice_structured)
                        if(s->mb_x || s->mb_y%s->gob_index) is_gob_start=0;
                    break;
                case AV_CODEC_ID_MPEG2VIDEO:
                    if(s->mb_x==0 && s->mb_y!=0) is_gob_start=1;
                case AV_CODEC_ID_MPEG1VIDEO:
                    if(s->mb_skip_run) is_gob_start=0;
                    break;
                case AV_CODEC_ID_MJPEG:
                    if(s->mb_x==0 && s->mb_y!=0) is_gob_start=1;
                    break;
                }

                if(is_gob_start){
                    if(s->start_mb_y != mb_y || mb_x!=0){
                        write_slice_end(s);

                        if(CONFIG_MPEG4_ENCODER && s->codec_id==AV_CODEC_ID_MPEG4 && s->partitioned_frame){
                            ff_mpeg4_init_partitions(s);
                        }
                    }

                    av_assert2((put_bits_count(&s->pb)&7) == 0);
                    current_packet_size= put_bits_ptr(&s->pb) - s->ptr_lastgob;

                    if (s->error_rate && s->resync_mb_x + s->resync_mb_y > 0) {
                        int r= put_bits_count(&s->pb)/8 + s->picture_number + 16 + s->mb_x + s->mb_y;
                        int d = 100 / s->error_rate;
                        if(r % d == 0){
                            current_packet_size=0;
                            s->pb.buf_ptr= s->ptr_lastgob;
                            av_assert1(put_bits_ptr(&s->pb) == s->ptr_lastgob);
                        }
                    }

#if FF_API_RTP_CALLBACK
FF_DISABLE_DEPRECATION_WARNINGS
                    if (s->avctx->rtp_callback){
                        int number_mb = (mb_y - s->resync_mb_y)*s->mb_width + mb_x - s->resync_mb_x;
                        s->avctx->rtp_callback(s->avctx, s->ptr_lastgob, current_packet_size, number_mb);
                    }
FF_ENABLE_DEPRECATION_WARNINGS
#endif
                    update_mb_info(s, 1);

                    switch(s->codec_id){
                    case AV_CODEC_ID_MPEG4:
                        if (CONFIG_MPEG4_ENCODER) {
                            ff_mpeg4_encode_video_packet_header(s);
                            ff_mpeg4_clean_buffers(s);
                        }
                    break;
                    case AV_CODEC_ID_MPEG1VIDEO:
                    case AV_CODEC_ID_MPEG2VIDEO:
                        if (CONFIG_MPEG1VIDEO_ENCODER || CONFIG_MPEG2VIDEO_ENCODER) {
                            ff_mpeg1_encode_slice_header(s);
                            ff_mpeg1_clean_buffers(s);
                        }
                    break;
                    case AV_CODEC_ID_H263:
                    case AV_CODEC_ID_H263P:
                        if (CONFIG_H263_ENCODER)
                            ff_h263_encode_gob_header(s, mb_y);
                    break;
                    }

                    if (s->avctx->flags & AV_CODEC_FLAG_PASS1) {
                        int bits= put_bits_count(&s->pb);
                        s->misc_bits+= bits - s->last_bits;
                        s->last_bits= bits;
                    }

                    s->ptr_lastgob += current_packet_size;
                    s->first_slice_line=1;
                    s->resync_mb_x=mb_x;
                    s->resync_mb_y=mb_y;
                }
            }

            if(  (s->resync_mb_x   == s->mb_x)
               && s->resync_mb_y+1 == s->mb_y){
                s->first_slice_line=0;
            }

            s->mb_skipped=0;
            s->dquant=0; //only for QP_RD

            update_mb_info(s, 0);

            if (mb_type & (mb_type-1) || (s->mpv_flags & FF_MPV_FLAG_QP_RD)) { // more than 1 MB type possible or FF_MPV_FLAG_QP_RD
                int next_block=0;
                int pb_bits_count, pb2_bits_count, tex_pb_bits_count;

                copy_context_before_encode(&backup_s, s, -1);
                backup_s.pb= s->pb;
                best_s.data_partitioning= s->data_partitioning;
                best_s.partitioned_frame= s->partitioned_frame;
                if(s->data_partitioning){
                    backup_s.pb2= s->pb2;
                    backup_s.tex_pb= s->tex_pb;
                }

                if(mb_type&CANDIDATE_MB_TYPE_INTER){
                    s->mv_dir = MV_DIR_FORWARD;
                    s->mv_type = MV_TYPE_16X16;
                    s->mb_intra= 0;
                    s->mv[0][0][0] = s->p_mv_table[xy][0];
                    s->mv[0][0][1] = s->p_mv_table[xy][1];
                    encode_mb_hq(s, &backup_s, &best_s, CANDIDATE_MB_TYPE_INTER, pb, pb2, tex_pb,
                                 &dmin, &next_block, s->mv[0][0][0], s->mv[0][0][1]);
                }
                if(mb_type&CANDIDATE_MB_TYPE_INTER_I){
                    s->mv_dir = MV_DIR_FORWARD;
                    s->mv_type = MV_TYPE_FIELD;
                    s->mb_intra= 0;
                    for(i=0; i<2; i++){
                        j= s->field_select[0][i] = s->p_field_select_table[i][xy];
                        s->mv[0][i][0] = s->p_field_mv_table[i][j][xy][0];
                        s->mv[0][i][1] = s->p_field_mv_table[i][j][xy][1];
                    }
                    encode_mb_hq(s, &backup_s, &best_s, CANDIDATE_MB_TYPE_INTER_I, pb, pb2, tex_pb,
                                 &dmin, &next_block, 0, 0);
                }
                if(mb_type&CANDIDATE_MB_TYPE_SKIPPED){
                    s->mv_dir = MV_DIR_FORWARD;
                    s->mv_type = MV_TYPE_16X16;
                    s->mb_intra= 0;
                    s->mv[0][0][0] = 0;
                    s->mv[0][0][1] = 0;
                    encode_mb_hq(s, &backup_s, &best_s, CANDIDATE_MB_TYPE_SKIPPED, pb, pb2, tex_pb,
                                 &dmin, &next_block, s->mv[0][0][0], s->mv[0][0][1]);
                }
                if(mb_type&CANDIDATE_MB_TYPE_INTER4V){
                    s->mv_dir = MV_DIR_FORWARD;
                    s->mv_type = MV_TYPE_8X8;
                    s->mb_intra= 0;
                    for(i=0; i<4; i++){
                        s->mv[0][i][0] = s->current_picture.motion_val[0][s->block_index[i]][0];
                        s->mv[0][i][1] = s->current_picture.motion_val[0][s->block_index[i]][1];
                    }
                    encode_mb_hq(s, &backup_s, &best_s, CANDIDATE_MB_TYPE_INTER4V, pb, pb2, tex_pb,
                                 &dmin, &next_block, 0, 0);
                }
                if(mb_type&CANDIDATE_MB_TYPE_FORWARD){
                    s->mv_dir = MV_DIR_FORWARD;
                    s->mv_type = MV_TYPE_16X16;
                    s->mb_intra= 0;
                    s->mv[0][0][0] = s->b_forw_mv_table[xy][0];
                    s->mv[0][0][1] = s->b_forw_mv_table[xy][1];
                    encode_mb_hq(s, &backup_s, &best_s, CANDIDATE_MB_TYPE_FORWARD, pb, pb2, tex_pb,
                                 &dmin, &next_block, s->mv[0][0][0], s->mv[0][0][1]);
                }
                if(mb_type&CANDIDATE_MB_TYPE_BACKWARD){
                    s->mv_dir = MV_DIR_BACKWARD;
                    s->mv_type = MV_TYPE_16X16;
                    s->mb_intra= 0;
                    s->mv[1][0][0] = s->b_back_mv_table[xy][0];
                    s->mv[1][0][1] = s->b_back_mv_table[xy][1];
                    encode_mb_hq(s, &backup_s, &best_s, CANDIDATE_MB_TYPE_BACKWARD, pb, pb2, tex_pb,
                                 &dmin, &next_block, s->mv[1][0][0], s->mv[1][0][1]);
                }
                if(mb_type&CANDIDATE_MB_TYPE_BIDIR){
                    s->mv_dir = MV_DIR_FORWARD | MV_DIR_BACKWARD;
                    s->mv_type = MV_TYPE_16X16;
                    s->mb_intra= 0;
                    s->mv[0][0][0] = s->b_bidir_forw_mv_table[xy][0];
                    s->mv[0][0][1] = s->b_bidir_forw_mv_table[xy][1];
                    s->mv[1][0][0] = s->b_bidir_back_mv_table[xy][0];
                    s->mv[1][0][1] = s->b_bidir_back_mv_table[xy][1];
                    encode_mb_hq(s, &backup_s, &best_s, CANDIDATE_MB_TYPE_BIDIR, pb, pb2, tex_pb,
                                 &dmin, &next_block, 0, 0);
                }
                if(mb_type&CANDIDATE_MB_TYPE_FORWARD_I){
                    s->mv_dir = MV_DIR_FORWARD;
                    s->mv_type = MV_TYPE_FIELD;
                    s->mb_intra= 0;
                    for(i=0; i<2; i++){
                        j= s->field_select[0][i] = s->b_field_select_table[0][i][xy];
                        s->mv[0][i][0] = s->b_field_mv_table[0][i][j][xy][0];
                        s->mv[0][i][1] = s->b_field_mv_table[0][i][j][xy][1];
                    }
                    encode_mb_hq(s, &backup_s, &best_s, CANDIDATE_MB_TYPE_FORWARD_I, pb, pb2, tex_pb,
                                 &dmin, &next_block, 0, 0);
                }
                if(mb_type&CANDIDATE_MB_TYPE_BACKWARD_I){
                    s->mv_dir = MV_DIR_BACKWARD;
                    s->mv_type = MV_TYPE_FIELD;
                    s->mb_intra= 0;
                    for(i=0; i<2; i++){
                        j= s->field_select[1][i] = s->b_field_select_table[1][i][xy];
                        s->mv[1][i][0] = s->b_field_mv_table[1][i][j][xy][0];
                        s->mv[1][i][1] = s->b_field_mv_table[1][i][j][xy][1];
                    }
                    encode_mb_hq(s, &backup_s, &best_s, CANDIDATE_MB_TYPE_BACKWARD_I, pb, pb2, tex_pb,
                                 &dmin, &next_block, 0, 0);
                }
                if(mb_type&CANDIDATE_MB_TYPE_BIDIR_I){
                    s->mv_dir = MV_DIR_FORWARD | MV_DIR_BACKWARD;
                    s->mv_type = MV_TYPE_FIELD;
                    s->mb_intra= 0;
                    for(dir=0; dir<2; dir++){
                        for(i=0; i<2; i++){
                            j= s->field_select[dir][i] = s->b_field_select_table[dir][i][xy];
                            s->mv[dir][i][0] = s->b_field_mv_table[dir][i][j][xy][0];
                            s->mv[dir][i][1] = s->b_field_mv_table[dir][i][j][xy][1];
                        }
                    }
                    encode_mb_hq(s, &backup_s, &best_s, CANDIDATE_MB_TYPE_BIDIR_I, pb, pb2, tex_pb,
                                 &dmin, &next_block, 0, 0);
                }
                if(mb_type&CANDIDATE_MB_TYPE_INTRA){
                    s->mv_dir = 0;
                    s->mv_type = MV_TYPE_16X16;
                    s->mb_intra= 1;
                    s->mv[0][0][0] = 0;
                    s->mv[0][0][1] = 0;
                    encode_mb_hq(s, &backup_s, &best_s, CANDIDATE_MB_TYPE_INTRA, pb, pb2, tex_pb,
                                 &dmin, &next_block, 0, 0);
                    if(s->h263_pred || s->h263_aic){
                        if(best_s.mb_intra)
                            s->mbintra_table[mb_x + mb_y*s->mb_stride]=1;
                        else
                            ff_clean_intra_table_entries(s); //old mode?
                    }
                }

                if ((s->mpv_flags & FF_MPV_FLAG_QP_RD) && dmin < INT_MAX) {
                    if(best_s.mv_type==MV_TYPE_16X16){ //FIXME move 4mv after QPRD
                        const int last_qp= backup_s.qscale;
                        int qpi, qp, dc[6];
                        int16_t ac[6][16];
                        const int mvdir= (best_s.mv_dir&MV_DIR_BACKWARD) ? 1 : 0;
                        static const int dquant_tab[4]={-1,1,-2,2};
                        int storecoefs = s->mb_intra && s->dc_val[0];

                        av_assert2(backup_s.dquant == 0);

                        //FIXME intra
                        s->mv_dir= best_s.mv_dir;
                        s->mv_type = MV_TYPE_16X16;
                        s->mb_intra= best_s.mb_intra;
                        s->mv[0][0][0] = best_s.mv[0][0][0];
                        s->mv[0][0][1] = best_s.mv[0][0][1];
                        s->mv[1][0][0] = best_s.mv[1][0][0];
                        s->mv[1][0][1] = best_s.mv[1][0][1];

                        qpi = s->pict_type == AV_PICTURE_TYPE_B ? 2 : 0;
                        for(; qpi<4; qpi++){
                            int dquant= dquant_tab[qpi];
                            qp= last_qp + dquant;
                            if(qp < s->avctx->qmin || qp > s->avctx->qmax)
                                continue;
                            backup_s.dquant= dquant;
                            if(storecoefs){
                                for(i=0; i<6; i++){
                                    dc[i]= s->dc_val[0][ s->block_index[i] ];
                                    memcpy(ac[i], s->ac_val[0][s->block_index[i]], sizeof(int16_t)*16);
                                }
                            }

                            encode_mb_hq(s, &backup_s, &best_s, CANDIDATE_MB_TYPE_INTER /* wrong but unused */, pb, pb2, tex_pb,
                                         &dmin, &next_block, s->mv[mvdir][0][0], s->mv[mvdir][0][1]);
                            if(best_s.qscale != qp){
                                if(storecoefs){
                                    for(i=0; i<6; i++){
                                        s->dc_val[0][ s->block_index[i] ]= dc[i];
                                        memcpy(s->ac_val[0][s->block_index[i]], ac[i], sizeof(int16_t)*16);
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
                    s->mv_dir = MV_DIR_FORWARD | MV_DIR_BACKWARD | MV_DIRECT;
                    s->mb_intra= 0;
                    ff_mpeg4_set_direct_mv(s, mx, my);
                    encode_mb_hq(s, &backup_s, &best_s, CANDIDATE_MB_TYPE_DIRECT, pb, pb2, tex_pb,
                                 &dmin, &next_block, mx, my);
                }
                if(CONFIG_MPEG4_ENCODER && mb_type&CANDIDATE_MB_TYPE_DIRECT0){
                    backup_s.dquant = 0;
                    s->mv_dir = MV_DIR_FORWARD | MV_DIR_BACKWARD | MV_DIRECT;
                    s->mb_intra= 0;
                    ff_mpeg4_set_direct_mv(s, 0, 0);
                    encode_mb_hq(s, &backup_s, &best_s, CANDIDATE_MB_TYPE_DIRECT, pb, pb2, tex_pb,
                                 &dmin, &next_block, 0, 0);
                }
                if (!best_s.mb_intra && s->mpv_flags & FF_MPV_FLAG_SKIP_RD) {
                    int coded=0;
                    for(i=0; i<6; i++)
                        coded |= s->block_last_index[i];
                    if(coded){
                        int mx,my;
                        memcpy(s->mv, best_s.mv, sizeof(s->mv));
                        if(CONFIG_MPEG4_ENCODER && best_s.mv_dir & MV_DIRECT){
                            mx=my=0; //FIXME find the one we actually used
                            ff_mpeg4_set_direct_mv(s, mx, my);
                        }else if(best_s.mv_dir&MV_DIR_BACKWARD){
                            mx= s->mv[1][0][0];
                            my= s->mv[1][0][1];
                        }else{
                            mx= s->mv[0][0][0];
                            my= s->mv[0][0][1];
                        }

                        s->mv_dir= best_s.mv_dir;
                        s->mv_type = best_s.mv_type;
                        s->mb_intra= 0;
/*                        s->mv[0][0][0] = best_s.mv[0][0][0];
                        s->mv[0][0][1] = best_s.mv[0][0][1];
                        s->mv[1][0][0] = best_s.mv[1][0][0];
                        s->mv[1][0][1] = best_s.mv[1][0][1];*/
                        backup_s.dquant= 0;
                        s->skipdct=1;
                        encode_mb_hq(s, &backup_s, &best_s, CANDIDATE_MB_TYPE_INTER /* wrong but unused */, pb, pb2, tex_pb,
                                        &dmin, &next_block, mx, my);
                        s->skipdct=0;
                    }
                }

                s->current_picture.qscale_table[xy] = best_s.qscale;

                copy_context_after_encode(s, &best_s, -1);

                pb_bits_count= put_bits_count(&s->pb);
                flush_put_bits(&s->pb);
                avpriv_copy_bits(&backup_s.pb, bit_buf[next_block^1], pb_bits_count);
                s->pb= backup_s.pb;

                if(s->data_partitioning){
                    pb2_bits_count= put_bits_count(&s->pb2);
                    flush_put_bits(&s->pb2);
                    avpriv_copy_bits(&backup_s.pb2, bit_buf2[next_block^1], pb2_bits_count);
                    s->pb2= backup_s.pb2;

                    tex_pb_bits_count= put_bits_count(&s->tex_pb);
                    flush_put_bits(&s->tex_pb);
                    avpriv_copy_bits(&backup_s.tex_pb, bit_buf_tex[next_block^1], tex_pb_bits_count);
                    s->tex_pb= backup_s.tex_pb;
                }
                s->last_bits= put_bits_count(&s->pb);

                if (CONFIG_H263_ENCODER &&
                    s->out_format == FMT_H263 && s->pict_type!=AV_PICTURE_TYPE_B)
                    ff_h263_update_motion_val(s);

                if(next_block==0){ //FIXME 16 vs linesize16
                    s->hdsp.put_pixels_tab[0][0](s->dest[0], s->sc.rd_scratchpad                     , s->linesize  ,16);
                    s->hdsp.put_pixels_tab[1][0](s->dest[1], s->sc.rd_scratchpad + 16*s->linesize    , s->uvlinesize, 8);
                    s->hdsp.put_pixels_tab[1][0](s->dest[2], s->sc.rd_scratchpad + 16*s->linesize + 8, s->uvlinesize, 8);
                }

                if(s->avctx->mb_decision == FF_MB_DECISION_BITS)
                    ff_mpv_reconstruct_mb(s, s->block);
            } else {
                int motion_x = 0, motion_y = 0;
                s->mv_type=MV_TYPE_16X16;
                // only one MB-Type possible

                switch(mb_type){
                case CANDIDATE_MB_TYPE_INTRA:
                    s->mv_dir = 0;
                    s->mb_intra= 1;
                    motion_x= s->mv[0][0][0] = 0;
                    motion_y= s->mv[0][0][1] = 0;
                    break;
                case CANDIDATE_MB_TYPE_INTER:
                    s->mv_dir = MV_DIR_FORWARD;
                    s->mb_intra= 0;
                    motion_x= s->mv[0][0][0] = s->p_mv_table[xy][0];
                    motion_y= s->mv[0][0][1] = s->p_mv_table[xy][1];
                    break;
                case CANDIDATE_MB_TYPE_INTER_I:
                    s->mv_dir = MV_DIR_FORWARD;
                    s->mv_type = MV_TYPE_FIELD;
                    s->mb_intra= 0;
                    for(i=0; i<2; i++){
                        j= s->field_select[0][i] = s->p_field_select_table[i][xy];
                        s->mv[0][i][0] = s->p_field_mv_table[i][j][xy][0];
                        s->mv[0][i][1] = s->p_field_mv_table[i][j][xy][1];
                    }
                    break;
                case CANDIDATE_MB_TYPE_INTER4V:
                    s->mv_dir = MV_DIR_FORWARD;
                    s->mv_type = MV_TYPE_8X8;
                    s->mb_intra= 0;
                    for(i=0; i<4; i++){
                        s->mv[0][i][0] = s->current_picture.motion_val[0][s->block_index[i]][0];
                        s->mv[0][i][1] = s->current_picture.motion_val[0][s->block_index[i]][1];
                    }
                    break;
                case CANDIDATE_MB_TYPE_DIRECT:
                    if (CONFIG_MPEG4_ENCODER) {
                        s->mv_dir = MV_DIR_FORWARD|MV_DIR_BACKWARD|MV_DIRECT;
                        s->mb_intra= 0;
                        motion_x=s->b_direct_mv_table[xy][0];
                        motion_y=s->b_direct_mv_table[xy][1];
                        ff_mpeg4_set_direct_mv(s, motion_x, motion_y);
                    }
                    break;
                case CANDIDATE_MB_TYPE_DIRECT0:
                    if (CONFIG_MPEG4_ENCODER) {
                        s->mv_dir = MV_DIR_FORWARD|MV_DIR_BACKWARD|MV_DIRECT;
                        s->mb_intra= 0;
                        ff_mpeg4_set_direct_mv(s, 0, 0);
                    }
                    break;
                case CANDIDATE_MB_TYPE_BIDIR:
                    s->mv_dir = MV_DIR_FORWARD | MV_DIR_BACKWARD;
                    s->mb_intra= 0;
                    s->mv[0][0][0] = s->b_bidir_forw_mv_table[xy][0];
                    s->mv[0][0][1] = s->b_bidir_forw_mv_table[xy][1];
                    s->mv[1][0][0] = s->b_bidir_back_mv_table[xy][0];
                    s->mv[1][0][1] = s->b_bidir_back_mv_table[xy][1];
                    break;
                case CANDIDATE_MB_TYPE_BACKWARD:
                    s->mv_dir = MV_DIR_BACKWARD;
                    s->mb_intra= 0;
                    motion_x= s->mv[1][0][0] = s->b_back_mv_table[xy][0];
                    motion_y= s->mv[1][0][1] = s->b_back_mv_table[xy][1];
                    break;
                case CANDIDATE_MB_TYPE_FORWARD:
                    s->mv_dir = MV_DIR_FORWARD;
                    s->mb_intra= 0;
                    motion_x= s->mv[0][0][0] = s->b_forw_mv_table[xy][0];
                    motion_y= s->mv[0][0][1] = s->b_forw_mv_table[xy][1];
                    break;
                case CANDIDATE_MB_TYPE_FORWARD_I:
                    s->mv_dir = MV_DIR_FORWARD;
                    s->mv_type = MV_TYPE_FIELD;
                    s->mb_intra= 0;
                    for(i=0; i<2; i++){
                        j= s->field_select[0][i] = s->b_field_select_table[0][i][xy];
                        s->mv[0][i][0] = s->b_field_mv_table[0][i][j][xy][0];
                        s->mv[0][i][1] = s->b_field_mv_table[0][i][j][xy][1];
                    }
                    break;
                case CANDIDATE_MB_TYPE_BACKWARD_I:
                    s->mv_dir = MV_DIR_BACKWARD;
                    s->mv_type = MV_TYPE_FIELD;
                    s->mb_intra= 0;
                    for(i=0; i<2; i++){
                        j= s->field_select[1][i] = s->b_field_select_table[1][i][xy];
                        s->mv[1][i][0] = s->b_field_mv_table[1][i][j][xy][0];
                        s->mv[1][i][1] = s->b_field_mv_table[1][i][j][xy][1];
                    }
                    break;
                case CANDIDATE_MB_TYPE_BIDIR_I:
                    s->mv_dir = MV_DIR_FORWARD | MV_DIR_BACKWARD;
                    s->mv_type = MV_TYPE_FIELD;
                    s->mb_intra= 0;
                    for(dir=0; dir<2; dir++){
                        for(i=0; i<2; i++){
                            j= s->field_select[dir][i] = s->b_field_select_table[dir][i][xy];
                            s->mv[dir][i][0] = s->b_field_mv_table[dir][i][j][xy][0];
                            s->mv[dir][i][1] = s->b_field_mv_table[dir][i][j][xy][1];
                        }
                    }
                    break;
                default:
                    av_log(s->avctx, AV_LOG_ERROR, "illegal MB type\n");
                }

                encode_mb(s, motion_x, motion_y);

                // RAL: Update last macroblock type
                s->last_mv_dir = s->mv_dir;

                if (CONFIG_H263_ENCODER &&
                    s->out_format == FMT_H263 && s->pict_type!=AV_PICTURE_TYPE_B)
                    ff_h263_update_motion_val(s);

                ff_mpv_reconstruct_mb(s, s->block);
            }

            /* clean the MV table in IPS frames for direct mode in B-frames */
            if(s->mb_intra /* && I,P,S_TYPE */){
                s->p_mv_table[xy][0]=0;
                s->p_mv_table[xy][1]=0;
            }

            if (s->avctx->flags & AV_CODEC_FLAG_PSNR) {
                int w= 16;
                int h= 16;

                if(s->mb_x*16 + 16 > s->width ) w= s->width - s->mb_x*16;
                if(s->mb_y*16 + 16 > s->height) h= s->height- s->mb_y*16;

                s->current_picture.encoding_error[0] += sse(
                    s, s->new_picture.f->data[0] + s->mb_x*16 + s->mb_y*s->linesize*16,
                    s->dest[0], w, h, s->linesize);
                s->current_picture.encoding_error[1] += sse(
                    s, s->new_picture.f->data[1] + s->mb_x*8  + s->mb_y*s->uvlinesize*chr_h,
                    s->dest[1], w>>1, h>>s->chroma_y_shift, s->uvlinesize);
                s->current_picture.encoding_error[2] += sse(
                    s, s->new_picture.f->data[2] + s->mb_x*8  + s->mb_y*s->uvlinesize*chr_h,
                    s->dest[2], w>>1, h>>s->chroma_y_shift, s->uvlinesize);
            }
            if(s->loop_filter){
                if(CONFIG_H263_ENCODER && s->out_format == FMT_H263)
                    ff_h263_loop_filter(s);
            }
            ff_dlog(s->avctx, "MB %d %d bits\n",
                    s->mb_x + s->mb_y * s->mb_stride, put_bits_count(&s->pb));
        }
    }

    //not beautiful here but we must write it before flushing so it has to be here
    if (CONFIG_MSMPEG4_ENCODER && s->msmpeg4_version && s->msmpeg4_version<4 && s->pict_type == AV_PICTURE_TYPE_I)
        ff_msmpeg4_encode_ext_header(s);

    write_slice_end(s);

#if FF_API_RTP_CALLBACK
FF_DISABLE_DEPRECATION_WARNINGS
    /* Send the last GOB if RTP */
    if (s->avctx->rtp_callback) {
        int number_mb = (mb_y - s->resync_mb_y)*s->mb_width - s->resync_mb_x;
        int pdif = put_bits_ptr(&s->pb) - s->ptr_lastgob;
        /* Call the RTP callback to send the last GOB */
        emms_c();
        s->avctx->rtp_callback(s->avctx, s->ptr_lastgob, pdif, number_mb);
    }
FF_ENABLE_DEPRECATION_WARNINGS
#endif

    return 0;
}

#define MERGE(field) dst->field += src->field; src->field=0
static void merge_context_after_me(MpegEncContext *dst, MpegEncContext *src){
    MERGE(me.scene_change_score);
    MERGE(me.mc_mb_var_sum_temp);
    MERGE(me.mb_var_sum_temp);
}

static void merge_context_after_encode(MpegEncContext *dst, MpegEncContext *src){
    int i;

    MERGE(dct_count[0]); //note, the other dct vars are not part of the context
    MERGE(dct_count[1]);
    MERGE(mv_bits);
    MERGE(i_tex_bits);
    MERGE(p_tex_bits);
    MERGE(i_count);
    MERGE(f_count);
    MERGE(b_count);
    MERGE(skip_count);
    MERGE(misc_bits);
    MERGE(er.error_count);
    MERGE(padding_bug_score);
    MERGE(current_picture.encoding_error[0]);
    MERGE(current_picture.encoding_error[1]);
    MERGE(current_picture.encoding_error[2]);

    if (dst->noise_reduction){
        for(i=0; i<64; i++){
            MERGE(dct_error_sum[0][i]);
            MERGE(dct_error_sum[1][i]);
        }
    }

    av_assert1(put_bits_count(&src->pb) % 8 ==0);
    av_assert1(put_bits_count(&dst->pb) % 8 ==0);
    avpriv_copy_bits(&dst->pb, src->pb.buf, put_bits_count(&src->pb));
    flush_put_bits(&dst->pb);
}

static int estimate_qp(MpegEncContext *s, int dry_run){
    if (s->next_lambda){
        s->current_picture_ptr->f->quality =
        s->current_picture.f->quality = s->next_lambda;
        if(!dry_run) s->next_lambda= 0;
    } else if (!s->fixed_qscale) {
        int quality = ff_rate_estimate_qscale(s, dry_run);
        s->current_picture_ptr->f->quality =
        s->current_picture.f->quality = quality;
        if (s->current_picture.f->quality < 0)
            return -1;
    }

    if(s->adaptive_quant){
        switch(s->codec_id){
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
        default:
            ff_init_qscale_tab(s);
        }

        s->lambda= s->lambda_table[0];
        //FIXME broken
    }else
        s->lambda = s->current_picture.f->quality;
    update_qscale(s);
    return 0;
}

/* must be called before writing the header */
static void set_frame_distances(MpegEncContext * s){
    av_assert1(s->current_picture_ptr->f->pts != AV_NOPTS_VALUE);
    s->time = s->current_picture_ptr->f->pts * s->avctx->time_base.num;

    if(s->pict_type==AV_PICTURE_TYPE_B){
        s->pb_time= s->pp_time - (s->last_non_b_time - s->time);
        av_assert1(s->pb_time > 0 && s->pb_time < s->pp_time);
    }else{
        s->pp_time= s->time - s->last_non_b_time;
        s->last_non_b_time= s->time;
        av_assert1(s->picture_number==0 || s->pp_time > 0);
    }
}

static int encode_picture(MpegEncContext *s, int picture_number)
{
    int i, ret;
    int bits;
    int context_count = s->slice_context_count;

    s->picture_number = picture_number;

    /* Reset the average MB variance */
    s->me.mb_var_sum_temp    =
    s->me.mc_mb_var_sum_temp = 0;

    /* we need to initialize some time vars before we can encode B-frames */
    // RAL: Condition added for MPEG1VIDEO
    if (s->codec_id == AV_CODEC_ID_MPEG1VIDEO || s->codec_id == AV_CODEC_ID_MPEG2VIDEO || (s->h263_pred && !s->msmpeg4_version))
        set_frame_distances(s);
    if(CONFIG_MPEG4_ENCODER && s->codec_id == AV_CODEC_ID_MPEG4)
        ff_set_mpeg4_time(s);

    s->me.scene_change_score=0;

//    s->lambda= s->current_picture_ptr->quality; //FIXME qscale / ... stuff for ME rate distortion

    if(s->pict_type==AV_PICTURE_TYPE_I){
        if(s->msmpeg4_version >= 3) s->no_rounding=1;
        else                        s->no_rounding=0;
    }else if(s->pict_type!=AV_PICTURE_TYPE_B){
        if(s->flipflop_rounding || s->codec_id == AV_CODEC_ID_H263P || s->codec_id == AV_CODEC_ID_MPEG4)
            s->no_rounding ^= 1;
    }

    if (s->avctx->flags & AV_CODEC_FLAG_PASS2) {
        if (estimate_qp(s,1) < 0)
            return -1;
        ff_get_2pass_fcode(s);
    } else if (!(s->avctx->flags & AV_CODEC_FLAG_QSCALE)) {
        if(s->pict_type==AV_PICTURE_TYPE_B)
            s->lambda= s->last_lambda_for[s->pict_type];
        else
            s->lambda= s->last_lambda_for[s->last_non_b_pict_type];
        update_qscale(s);
    }

    if(s->codec_id != AV_CODEC_ID_AMV && s->codec_id != AV_CODEC_ID_MJPEG){
        if(s->q_chroma_intra_matrix   != s->q_intra_matrix  ) av_freep(&s->q_chroma_intra_matrix);
        if(s->q_chroma_intra_matrix16 != s->q_intra_matrix16) av_freep(&s->q_chroma_intra_matrix16);
        s->q_chroma_intra_matrix   = s->q_intra_matrix;
        s->q_chroma_intra_matrix16 = s->q_intra_matrix16;
    }

    s->mb_intra=0; //for the rate distortion & bit compare functions
    for(i=1; i<context_count; i++){
        ret = ff_update_duplicate_context(s->thread_context[i], s);
        if (ret < 0)
            return ret;
    }

    if(ff_init_me(s)<0)
        return -1;

    /* Estimate motion for every MB */
    if(s->pict_type != AV_PICTURE_TYPE_I){
        s->lambda  = (s->lambda  * s->me_penalty_compensation + 128) >> 8;
        s->lambda2 = (s->lambda2 * (int64_t) s->me_penalty_compensation + 128) >> 8;
        if (s->pict_type != AV_PICTURE_TYPE_B) {
            if ((s->me_pre && s->last_non_b_pict_type == AV_PICTURE_TYPE_I) ||
                s->me_pre == 2) {
                s->avctx->execute(s->avctx, pre_estimate_motion_thread, &s->thread_context[0], NULL, context_count, sizeof(void*));
            }
        }

        s->avctx->execute(s->avctx, estimate_motion_thread, &s->thread_context[0], NULL, context_count, sizeof(void*));
    }else /* if(s->pict_type == AV_PICTURE_TYPE_I) */{
        /* I-Frame */
        for(i=0; i<s->mb_stride*s->mb_height; i++)
            s->mb_type[i]= CANDIDATE_MB_TYPE_INTRA;

        if(!s->fixed_qscale){
            /* finding spatial complexity for I-frame rate control */
            s->avctx->execute(s->avctx, mb_var_thread, &s->thread_context[0], NULL, context_count, sizeof(void*));
        }
    }
    for(i=1; i<context_count; i++){
        merge_context_after_me(s, s->thread_context[i]);
    }
    s->current_picture.mc_mb_var_sum= s->current_picture_ptr->mc_mb_var_sum= s->me.mc_mb_var_sum_temp;
    s->current_picture.   mb_var_sum= s->current_picture_ptr->   mb_var_sum= s->me.   mb_var_sum_temp;
    emms_c();

    if (s->me.scene_change_score > s->scenechange_threshold &&
        s->pict_type == AV_PICTURE_TYPE_P) {
        s->pict_type= AV_PICTURE_TYPE_I;
        for(i=0; i<s->mb_stride*s->mb_height; i++)
            s->mb_type[i]= CANDIDATE_MB_TYPE_INTRA;
        if(s->msmpeg4_version >= 3)
            s->no_rounding=1;
        ff_dlog(s, "Scene change detected, encoding as I Frame %"PRId64" %"PRId64"\n",
                s->current_picture.mb_var_sum, s->current_picture.mc_mb_var_sum);
    }

    if(!s->umvplus){
        if(s->pict_type==AV_PICTURE_TYPE_P || s->pict_type==AV_PICTURE_TYPE_S) {
            s->f_code= ff_get_best_fcode(s, s->p_mv_table, CANDIDATE_MB_TYPE_INTER);

            if (s->avctx->flags & AV_CODEC_FLAG_INTERLACED_ME) {
                int a,b;
                a= ff_get_best_fcode(s, s->p_field_mv_table[0][0], CANDIDATE_MB_TYPE_INTER_I); //FIXME field_select
                b= ff_get_best_fcode(s, s->p_field_mv_table[1][1], CANDIDATE_MB_TYPE_INTER_I);
                s->f_code= FFMAX3(s->f_code, a, b);
            }

            ff_fix_long_p_mvs(s, s->intra_penalty ? CANDIDATE_MB_TYPE_INTER : CANDIDATE_MB_TYPE_INTRA);
            ff_fix_long_mvs(s, NULL, 0, s->p_mv_table, s->f_code, CANDIDATE_MB_TYPE_INTER, !!s->intra_penalty);
            if (s->avctx->flags & AV_CODEC_FLAG_INTERLACED_ME) {
                int j;
                for(i=0; i<2; i++){
                    for(j=0; j<2; j++)
                        ff_fix_long_mvs(s, s->p_field_select_table[i], j,
                                        s->p_field_mv_table[i][j], s->f_code, CANDIDATE_MB_TYPE_INTER_I, !!s->intra_penalty);
                }
            }
        }

        if(s->pict_type==AV_PICTURE_TYPE_B){
            int a, b;

            a = ff_get_best_fcode(s, s->b_forw_mv_table, CANDIDATE_MB_TYPE_FORWARD);
            b = ff_get_best_fcode(s, s->b_bidir_forw_mv_table, CANDIDATE_MB_TYPE_BIDIR);
            s->f_code = FFMAX(a, b);

            a = ff_get_best_fcode(s, s->b_back_mv_table, CANDIDATE_MB_TYPE_BACKWARD);
            b = ff_get_best_fcode(s, s->b_bidir_back_mv_table, CANDIDATE_MB_TYPE_BIDIR);
            s->b_code = FFMAX(a, b);

            ff_fix_long_mvs(s, NULL, 0, s->b_forw_mv_table, s->f_code, CANDIDATE_MB_TYPE_FORWARD, 1);
            ff_fix_long_mvs(s, NULL, 0, s->b_back_mv_table, s->b_code, CANDIDATE_MB_TYPE_BACKWARD, 1);
            ff_fix_long_mvs(s, NULL, 0, s->b_bidir_forw_mv_table, s->f_code, CANDIDATE_MB_TYPE_BIDIR, 1);
            ff_fix_long_mvs(s, NULL, 0, s->b_bidir_back_mv_table, s->b_code, CANDIDATE_MB_TYPE_BIDIR, 1);
            if (s->avctx->flags & AV_CODEC_FLAG_INTERLACED_ME) {
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

    if (estimate_qp(s, 0) < 0)
        return -1;

    if (s->qscale < 3 && s->max_qcoeff <= 128 &&
        s->pict_type == AV_PICTURE_TYPE_I &&
        !(s->avctx->flags & AV_CODEC_FLAG_QSCALE))
        s->qscale= 3; //reduce clipping problems

    if (s->out_format == FMT_MJPEG) {
        const uint16_t *  luma_matrix = ff_mpeg1_default_intra_matrix;
        const uint16_t *chroma_matrix = ff_mpeg1_default_intra_matrix;

        if (s->avctx->intra_matrix) {
            chroma_matrix =
            luma_matrix = s->avctx->intra_matrix;
        }
        if (s->avctx->chroma_intra_matrix)
            chroma_matrix = s->avctx->chroma_intra_matrix;

        /* for mjpeg, we do include qscale in the matrix */
        for(i=1;i<64;i++){
            int j = s->idsp.idct_permutation[i];

            s->chroma_intra_matrix[j] = av_clip_uint8((chroma_matrix[i] * s->qscale) >> 3);
            s->       intra_matrix[j] = av_clip_uint8((  luma_matrix[i] * s->qscale) >> 3);
        }
        s->y_dc_scale_table=
        s->c_dc_scale_table= ff_mpeg2_dc_scale_table[s->intra_dc_precision];
        s->chroma_intra_matrix[0] =
        s->intra_matrix[0] = ff_mpeg2_dc_scale_table[s->intra_dc_precision][8];
        ff_convert_matrix(s, s->q_intra_matrix, s->q_intra_matrix16,
                       s->intra_matrix, s->intra_quant_bias, 8, 8, 1);
        ff_convert_matrix(s, s->q_chroma_intra_matrix, s->q_chroma_intra_matrix16,
                       s->chroma_intra_matrix, s->intra_quant_bias, 8, 8, 1);
        s->qscale= 8;
    }
    if(s->codec_id == AV_CODEC_ID_AMV){
        static const uint8_t y[32]={13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13};
        static const uint8_t c[32]={14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14};
        for(i=1;i<64;i++){
            int j= s->idsp.idct_permutation[ff_zigzag_direct[i]];

            s->intra_matrix[j] = sp5x_quant_table[5*2+0][i];
            s->chroma_intra_matrix[j] = sp5x_quant_table[5*2+1][i];
        }
        s->y_dc_scale_table= y;
        s->c_dc_scale_table= c;
        s->intra_matrix[0] = 13;
        s->chroma_intra_matrix[0] = 14;
        ff_convert_matrix(s, s->q_intra_matrix, s->q_intra_matrix16,
                       s->intra_matrix, s->intra_quant_bias, 8, 8, 1);
        ff_convert_matrix(s, s->q_chroma_intra_matrix, s->q_chroma_intra_matrix16,
                       s->chroma_intra_matrix, s->intra_quant_bias, 8, 8, 1);
        s->qscale= 8;
    }

    //FIXME var duplication
    s->current_picture_ptr->f->key_frame =
    s->current_picture.f->key_frame = s->pict_type == AV_PICTURE_TYPE_I; //FIXME pic_ptr
    s->current_picture_ptr->f->pict_type =
    s->current_picture.f->pict_type = s->pict_type;

    if (s->current_picture.f->key_frame)
        s->picture_in_gop_number=0;

    s->mb_x = s->mb_y = 0;
    s->last_bits= put_bits_count(&s->pb);
    switch(s->out_format) {
    case FMT_MJPEG:
        if (CONFIG_MJPEG_ENCODER && s->huffman != HUFFMAN_TABLE_OPTIMAL)
            ff_mjpeg_encode_picture_header(s->avctx, &s->pb, &s->intra_scantable,
                                           s->pred, s->intra_matrix, s->chroma_intra_matrix);
        break;
    case FMT_H261:
        if (CONFIG_H261_ENCODER)
            ff_h261_encode_picture_header(s, picture_number);
        break;
    case FMT_H263:
        if (CONFIG_WMV2_ENCODER && s->codec_id == AV_CODEC_ID_WMV2)
            ff_wmv2_encode_picture_header(s, picture_number);
        else if (CONFIG_MSMPEG4_ENCODER && s->msmpeg4_version)
            ff_msmpeg4_encode_picture_header(s, picture_number);
        else if (CONFIG_MPEG4_ENCODER && s->h263_pred) {
            ret = ff_mpeg4_encode_picture_header(s, picture_number);
            if (ret < 0)
                return ret;
        } else if (CONFIG_RV10_ENCODER && s->codec_id == AV_CODEC_ID_RV10) {
            ret = ff_rv10_encode_picture_header(s, picture_number);
            if (ret < 0)
                return ret;
        }
        else if (CONFIG_RV20_ENCODER && s->codec_id == AV_CODEC_ID_RV20)
            ff_rv20_encode_picture_header(s, picture_number);
        else if (CONFIG_FLV_ENCODER && s->codec_id == AV_CODEC_ID_FLV1)
            ff_flv_encode_picture_header(s, picture_number);
        else if (CONFIG_H263_ENCODER)
            ff_h263_encode_picture_header(s, picture_number);
        break;
    case FMT_MPEG1:
        if (CONFIG_MPEG1VIDEO_ENCODER || CONFIG_MPEG2VIDEO_ENCODER)
            ff_mpeg1_encode_picture_header(s, picture_number);
        break;
    default:
        av_assert0(0);
    }
    bits= put_bits_count(&s->pb);
    s->header_bits= bits - s->last_bits;

    for(i=1; i<context_count; i++){
        update_duplicate_context_after_me(s->thread_context[i], s);
    }
    s->avctx->execute(s->avctx, encode_thread, &s->thread_context[0], NULL, context_count, sizeof(void*));
    for(i=1; i<context_count; i++){
        if (s->pb.buf_end == s->thread_context[i]->pb.buf)
            set_put_bits_buffer_size(&s->pb, FFMIN(s->thread_context[i]->pb.buf_end - s->pb.buf, INT_MAX/8-32));
        merge_context_after_encode(s, s->thread_context[i]);
    }
    emms_c();
    return 0;
}

static void denoise_dct_c(MpegEncContext *s, int16_t *block){
    const int intra= s->mb_intra;
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

static int dct_quantize_trellis_c(MpegEncContext *s,
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
    uint8_t * length;
    uint8_t * last_length;
    const int lambda= s->lambda2 >> (FF_LAMBDA_SHIFT - 6);
    int mpeg2_qscale;

    s->fdsp.fdct(block);

    if(s->dct_error_sum)
        s->denoise_dct(s, block);
    qmul= qscale*16;
    qadd= ((qscale-1)|1)*8;

    if (s->q_scale_type) mpeg2_qscale = ff_mpeg2_non_linear_qscale[qscale];
    else                 mpeg2_qscale = qscale << 1;

    if (s->mb_intra) {
        int q;
        scantable= s->intra_scantable.scantable;
        perm_scantable= s->intra_scantable.permutated;
        if (!s->h263_aic) {
            if (n < 4)
                q = s->y_dc_scale;
            else
                q = s->c_dc_scale;
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
        matrix = n < 4 ? s->intra_matrix : s->chroma_intra_matrix;
        if(s->mpeg_quant || s->out_format == FMT_MPEG1 || s->out_format == FMT_MJPEG)
            bias= 1<<(QMAT_SHIFT-1);

        if (n > 3 && s->intra_chroma_ac_vlc_length) {
            length     = s->intra_chroma_ac_vlc_length;
            last_length= s->intra_chroma_ac_vlc_last_length;
        } else {
            length     = s->intra_ac_vlc_length;
            last_length= s->intra_ac_vlc_last_length;
        }
    } else {
        scantable= s->inter_scantable.scantable;
        perm_scantable= s->inter_scantable.permutated;
        start_i = 0;
        last_non_zero = -1;
        qmat = s->q_inter_matrix[qscale];
        matrix = s->inter_matrix;
        length     = s->inter_ac_vlc_length;
        last_length= s->inter_ac_vlc_last_length;
    }
    last_i= start_i;

    threshold1= (1<<QMAT_SHIFT) - bias - 1;
    threshold2= (threshold1<<1);

    for(i=63; i>=start_i; i--) {
        const int j = scantable[i];
        int level = block[j] * qmat[j];

        if(((unsigned)(level+threshold1))>threshold2){
            last_non_zero = i;
            break;
        }
    }

    for(i=start_i; i<=last_non_zero; i++) {
        const int j = scantable[i];
        int level = block[j] * qmat[j];

//        if(   bias+level >= (1<<(QMAT_SHIFT - 3))
//           || bias-level >= (1<<(QMAT_SHIFT - 3))){
        if(((unsigned)(level+threshold1))>threshold2){
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

            if(s->out_format == FMT_H263 || s->out_format == FMT_H261){
                unquant_coeff= alevel*qmul + qadd;
            } else if(s->out_format == FMT_MJPEG) {
                j = s->idsp.idct_permutation[scantable[i]];
                unquant_coeff = alevel * matrix[j] * 8;
            }else{ // MPEG-1
                j = s->idsp.idct_permutation[scantable[i]]; // FIXME: optimize
                if(s->mb_intra){
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

                if(s->out_format == FMT_H263 || s->out_format == FMT_H261){
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

                if(s->out_format == FMT_H263 || s->out_format == FMT_H261){
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

    if(s->out_format != FMT_H263 && s->out_format != FMT_H261){
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

            if(s->out_format == FMT_H263 || s->out_format == FMT_H261){
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

//#define REFINE_STATS 1
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

static int dct_quantize_refine(MpegEncContext *s, //FIXME breaks denoise?
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
    uint8_t * length;
    uint8_t * last_length;
    int lambda;
    int rle_index, run, q = 1, sum; //q is only used when s->mb_intra is true
#ifdef REFINE_STATS
static int count=0;
static int after_last=0;
static int to_zero=0;
static int from_zero=0;
static int raise=0;
static int lower=0;
static int messed_sign=0;
#endif

    if(basis[0][0] == 0)
        build_basis(s->idsp.idct_permutation);

    qmul= qscale*2;
    qadd= (qscale-1)|1;
    if (s->mb_intra) {
        scantable= s->intra_scantable.scantable;
        perm_scantable= s->intra_scantable.permutated;
        if (!s->h263_aic) {
            if (n < 4)
                q = s->y_dc_scale;
            else
                q = s->c_dc_scale;
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
//        if(s->mpeg_quant || s->out_format == FMT_MPEG1)
//            bias= 1<<(QMAT_SHIFT-1);
        if (n > 3 && s->intra_chroma_ac_vlc_length) {
            length     = s->intra_chroma_ac_vlc_length;
            last_length= s->intra_chroma_ac_vlc_last_length;
        } else {
            length     = s->intra_ac_vlc_length;
            last_length= s->intra_ac_vlc_last_length;
        }
    } else {
        scantable= s->inter_scantable.scantable;
        perm_scantable= s->inter_scantable.permutated;
        dc= 0;
        start_i = 0;
        length     = s->inter_ac_vlc_length;
        last_length= s->inter_ac_vlc_last_length;
    }
    last_non_zero = s->block_last_index[n];

#ifdef REFINE_STATS
{START_TIMER
#endif
    dc += (1<<(RECON_SHIFT-1));
    for(i=0; i<64; i++){
        rem[i] = dc - (orig[i] << RECON_SHIFT); // FIXME use orig directly instead of copying to rem[]
    }
#ifdef REFINE_STATS
STOP_TIMER("memset rem[]")}
#endif
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
    lambda= sum*(uint64_t)s->lambda2 >> (FF_LAMBDA_SHIFT - 6 + 6 + 6 + 6);
#ifdef REFINE_STATS
{START_TIMER
#endif
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
#ifdef REFINE_STATS
if(last_non_zero>0){
STOP_TIMER("init rem[]")
}
}

{START_TIMER
#endif
    for(;;){
        int best_score = s->mpvencdsp.try_8x8basis(rem, weight, basis[0], 0);
        int best_coeff=0;
        int best_change=0;
        int run2, best_unquant_change=0, analyze_gradient;
#ifdef REFINE_STATS
{START_TIMER
#endif
        analyze_gradient = last_non_zero > 2 || s->quantizer_noise_shaping >= 3;

        if(analyze_gradient){
#ifdef REFINE_STATS
{START_TIMER
#endif
            for(i=0; i<64; i++){
                int w= weight[i];

                d1[i] = (rem[i]*w*w + (1<<(RECON_SHIFT+12-1)))>>(RECON_SHIFT+12);
            }
#ifdef REFINE_STATS
STOP_TIMER("rem*w*w")}
{START_TIMER
#endif
            s->fdsp.fdct(d1);
#ifdef REFINE_STATS
STOP_TIMER("dct")}
#endif
        }

        if(start_i){
            const int level= block[0];
            int change, old_coeff;

            av_assert2(s->mb_intra);

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
#ifdef REFINE_STATS
STOP_TIMER("iterative step")}
#endif

        if(best_change){
            int j= perm_scantable[ best_coeff ];

            block[j] += best_change;

            if(best_coeff > last_non_zero){
                last_non_zero= best_coeff;
                av_assert2(block[j]);
#ifdef REFINE_STATS
after_last++;
#endif
            }else{
#ifdef REFINE_STATS
if(block[j]){
    if(block[j] - best_change){
        if(FFABS(block[j]) > FFABS(block[j] - best_change)){
            raise++;
        }else{
            lower++;
        }
    }else{
        from_zero++;
    }
}else{
    to_zero++;
}
#endif
                for(; last_non_zero>=start_i; last_non_zero--){
                    if(block[perm_scantable[last_non_zero]])
                        break;
                }
            }
#ifdef REFINE_STATS
count++;
if(256*256*256*64 % count == 0){
    av_log(s->avctx, AV_LOG_DEBUG, "after_last:%d to_zero:%d from_zero:%d raise:%d lower:%d sign:%d xyp:%d/%d/%d\n", after_last, to_zero, from_zero, raise, lower, messed_sign, s->mb_x, s->mb_y, s->picture_number);
}
#endif
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
#ifdef REFINE_STATS
if(last_non_zero>0){
STOP_TIMER("iterative search")
}
}
#endif

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
void ff_block_permute(int16_t *block, uint8_t *permutation,
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

int ff_dct_quantize_c(MpegEncContext *s,
                        int16_t *block, int n,
                        int qscale, int *overflow)
{
    int i, j, level, last_non_zero, q, start_i;
    const int *qmat;
    const uint8_t *scantable;
    int bias;
    int max=0;
    unsigned int threshold1, threshold2;

    s->fdsp.fdct(block);

    if(s->dct_error_sum)
        s->denoise_dct(s, block);

    if (s->mb_intra) {
        scantable= s->intra_scantable.scantable;
        if (!s->h263_aic) {
            if (n < 4)
                q = s->y_dc_scale;
            else
                q = s->c_dc_scale;
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
        scantable= s->inter_scantable.scantable;
        start_i = 0;
        last_non_zero = -1;
        qmat = s->q_inter_matrix[qscale];
        bias= s->inter_quant_bias*(1<<(QMAT_SHIFT - QUANT_BIAS_SHIFT));
    }
    threshold1= (1<<QMAT_SHIFT) - bias - 1;
    threshold2= (threshold1<<1);
    for(i=63;i>=start_i;i--) {
        j = scantable[i];
        level = block[j] * qmat[j];

        if(((unsigned)(level+threshold1))>threshold2){
            last_non_zero = i;
            break;
        }else{
            block[j]=0;
        }
    }
    for(i=start_i; i<=last_non_zero; i++) {
        j = scantable[i];
        level = block[j] * qmat[j];

//        if(   bias+level >= (1<<QMAT_SHIFT)
//           || bias-level >= (1<<QMAT_SHIFT)){
        if(((unsigned)(level+threshold1))>threshold2){
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
    if (s->idsp.perm_type != FF_IDCT_PERM_NONE)
        ff_block_permute(block, s->idsp.idct_permutation,
                      scantable, last_non_zero);

    return last_non_zero;
}

#define OFFSET(x) offsetof(MpegEncContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption h263_options[] = {
    { "obmc",         "use overlapped block motion compensation.", OFFSET(obmc), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, VE },
    { "mb_info",      "emit macroblock info for RFC 2190 packetization, the parameter value is the maximum payload size", OFFSET(mb_info), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX, VE },
    FF_MPV_COMMON_OPTS
    { NULL },
};

static const AVClass h263_class = {
    .class_name = "H.263 encoder",
    .item_name  = av_default_item_name,
    .option     = h263_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_h263_encoder = {
    .name           = "h263",
    .long_name      = NULL_IF_CONFIG_SMALL("H.263 / H.263-1996"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_H263,
    .priv_data_size = sizeof(MpegEncContext),
    .init           = ff_mpv_encode_init,
    .encode2        = ff_mpv_encode_picture,
    .close          = ff_mpv_encode_end,
    .pix_fmts= (const enum AVPixelFormat[]){AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE},
    .priv_class     = &h263_class,
};

static const AVOption h263p_options[] = {
    { "umv",        "Use unlimited motion vectors.",    OFFSET(umvplus),       AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, VE },
    { "aiv",        "Use alternative inter VLC.",       OFFSET(alt_inter_vlc), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, VE },
    { "obmc",       "use overlapped block motion compensation.", OFFSET(obmc), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, VE },
    { "structured_slices", "Write slice start position at every GOB header instead of just GOB number.", OFFSET(h263_slice_structured), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, VE},
    FF_MPV_COMMON_OPTS
    { NULL },
};
static const AVClass h263p_class = {
    .class_name = "H.263p encoder",
    .item_name  = av_default_item_name,
    .option     = h263p_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_h263p_encoder = {
    .name           = "h263p",
    .long_name      = NULL_IF_CONFIG_SMALL("H.263+ / H.263-1998 / H.263 version 2"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_H263P,
    .priv_data_size = sizeof(MpegEncContext),
    .init           = ff_mpv_encode_init,
    .encode2        = ff_mpv_encode_picture,
    .close          = ff_mpv_encode_end,
    .capabilities   = AV_CODEC_CAP_SLICE_THREADS,
    .pix_fmts       = (const enum AVPixelFormat[]){ AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE },
    .priv_class     = &h263p_class,
};

static const AVClass msmpeg4v2_class = {
    .class_name = "msmpeg4v2 encoder",
    .item_name  = av_default_item_name,
    .option     = ff_mpv_generic_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_msmpeg4v2_encoder = {
    .name           = "msmpeg4v2",
    .long_name      = NULL_IF_CONFIG_SMALL("MPEG-4 part 2 Microsoft variant version 2"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_MSMPEG4V2,
    .priv_data_size = sizeof(MpegEncContext),
    .init           = ff_mpv_encode_init,
    .encode2        = ff_mpv_encode_picture,
    .close          = ff_mpv_encode_end,
    .pix_fmts       = (const enum AVPixelFormat[]){ AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE },
    .priv_class     = &msmpeg4v2_class,
};

static const AVClass msmpeg4v3_class = {
    .class_name = "msmpeg4v3 encoder",
    .item_name  = av_default_item_name,
    .option     = ff_mpv_generic_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_msmpeg4v3_encoder = {
    .name           = "msmpeg4",
    .long_name      = NULL_IF_CONFIG_SMALL("MPEG-4 part 2 Microsoft variant version 3"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_MSMPEG4V3,
    .priv_data_size = sizeof(MpegEncContext),
    .init           = ff_mpv_encode_init,
    .encode2        = ff_mpv_encode_picture,
    .close          = ff_mpv_encode_end,
    .pix_fmts       = (const enum AVPixelFormat[]){ AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE },
    .priv_class     = &msmpeg4v3_class,
};

static const AVClass wmv1_class = {
    .class_name = "wmv1 encoder",
    .item_name  = av_default_item_name,
    .option     = ff_mpv_generic_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_wmv1_encoder = {
    .name           = "wmv1",
    .long_name      = NULL_IF_CONFIG_SMALL("Windows Media Video 7"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_WMV1,
    .priv_data_size = sizeof(MpegEncContext),
    .init           = ff_mpv_encode_init,
    .encode2        = ff_mpv_encode_picture,
    .close          = ff_mpv_encode_end,
    .pix_fmts       = (const enum AVPixelFormat[]){ AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE },
    .priv_class     = &wmv1_class,
};
