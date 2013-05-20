/*
 * Rate control for video encoders
 *
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
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
 * Rate control for video encoders.
 */

#include "libavutil/attributes.h"
#include "avcodec.h"
#include "ratecontrol.h"
#include "mpegvideo.h"
#include "libavutil/eval.h"

#undef NDEBUG // Always check asserts, the speed effect is far too small to disable them.
#include <assert.h>

#ifndef M_E
#define M_E 2.718281828
#endif

static int init_pass2(MpegEncContext *s);
static double get_qscale(MpegEncContext *s, RateControlEntry *rce,
                         double rate_factor, int frame_num);

void ff_write_pass1_stats(MpegEncContext *s)
{
    snprintf(s->avctx->stats_out, 256,
             "in:%d out:%d type:%d q:%d itex:%d ptex:%d mv:%d misc:%d "
             "fcode:%d bcode:%d mc-var:%d var:%d icount:%d skipcount:%d hbits:%d;\n",
             s->current_picture_ptr->f.display_picture_number,
             s->current_picture_ptr->f.coded_picture_number,
             s->pict_type,
             s->current_picture.f.quality,
             s->i_tex_bits,
             s->p_tex_bits,
             s->mv_bits,
             s->misc_bits,
             s->f_code,
             s->b_code,
             s->current_picture.mc_mb_var_sum,
             s->current_picture.mb_var_sum,
             s->i_count, s->skip_count,
             s->header_bits);
}

static double get_fps(AVCodecContext *avctx)
{
    return 1.0 / av_q2d(avctx->time_base) / FFMAX(avctx->ticks_per_frame, 1);
}

static inline double qp2bits(RateControlEntry *rce, double qp)
{
    if (qp <= 0.0) {
        av_log(NULL, AV_LOG_ERROR, "qp<=0.0\n");
    }
    return rce->qscale * (double)(rce->i_tex_bits + rce->p_tex_bits + 1) / qp;
}

static inline double bits2qp(RateControlEntry *rce, double bits)
{
    if (bits < 0.9) {
        av_log(NULL, AV_LOG_ERROR, "bits<0.9\n");
    }
    return rce->qscale * (double)(rce->i_tex_bits + rce->p_tex_bits + 1) / bits;
}

av_cold int ff_rate_control_init(MpegEncContext *s)
{
    RateControlContext *rcc = &s->rc_context;
    int i, res;
    static const char * const const_names[] = {
        "PI",
        "E",
        "iTex",
        "pTex",
        "tex",
        "mv",
        "fCode",
        "iCount",
        "mcVar",
        "var",
        "isI",
        "isP",
        "isB",
        "avgQP",
        "qComp",
#if 0
        "lastIQP",
        "lastPQP",
        "lastBQP",
        "nextNonBQP",
#endif
        "avgIITex",
        "avgPITex",
        "avgPPTex",
        "avgBPTex",
        "avgTex",
        NULL
    };
    static double (* const func1[])(void *, double) = {
        (void *)bits2qp,
        (void *)qp2bits,
        NULL
    };
    static const char * const func1_names[] = {
        "bits2qp",
        "qp2bits",
        NULL
    };
    emms_c();

    if (!s->avctx->rc_max_available_vbv_use && s->avctx->rc_buffer_size) {
        if (s->avctx->rc_max_rate) {
            s->avctx->rc_max_available_vbv_use = av_clipf(s->avctx->rc_max_rate/(s->avctx->rc_buffer_size*get_fps(s->avctx)), 1.0/3, 1.0);
        } else
            s->avctx->rc_max_available_vbv_use = 1.0;
    }

    res = av_expr_parse(&rcc->rc_eq_eval,
                        s->avctx->rc_eq ? s->avctx->rc_eq : "tex^qComp",
                        const_names, func1_names, func1,
                        NULL, NULL, 0, s->avctx);
    if (res < 0) {
        av_log(s->avctx, AV_LOG_ERROR, "Error parsing rc_eq \"%s\"\n", s->avctx->rc_eq);
        return res;
    }

    for (i = 0; i < 5; i++) {
        rcc->pred[i].coeff = FF_QP2LAMBDA * 7.0;
        rcc->pred[i].count = 1.0;
        rcc->pred[i].decay = 0.4;

        rcc->i_cplx_sum [i] =
        rcc->p_cplx_sum [i] =
        rcc->mv_bits_sum[i] =
        rcc->qscale_sum [i] =
        rcc->frame_count[i] = 1; // 1 is better because of 1/0 and such

        rcc->last_qscale_for[i] = FF_QP2LAMBDA * 5;
    }
    rcc->buffer_index = s->avctx->rc_initial_buffer_occupancy;
    if (!rcc->buffer_index)
        rcc->buffer_index = s->avctx->rc_buffer_size * 3 / 4;

    if (s->flags & CODEC_FLAG_PASS2) {
        int i;
        char *p;

        /* find number of pics */
        p = s->avctx->stats_in;
        for (i = -1; p; i++)
            p = strchr(p + 1, ';');
        i += s->max_b_frames;
        if (i <= 0 || i >= INT_MAX / sizeof(RateControlEntry))
            return -1;
        rcc->entry       = av_mallocz(i * sizeof(RateControlEntry));
        rcc->num_entries = i;

        /* init all to skipped p frames
         * (with b frames we might have a not encoded frame at the end FIXME) */
        for (i = 0; i < rcc->num_entries; i++) {
            RateControlEntry *rce = &rcc->entry[i];

            rce->pict_type  = rce->new_pict_type = AV_PICTURE_TYPE_P;
            rce->qscale     = rce->new_qscale    = FF_QP2LAMBDA * 2;
            rce->misc_bits  = s->mb_num + 10;
            rce->mb_var_sum = s->mb_num * 100;
        }

        /* read stats */
        p = s->avctx->stats_in;
        for (i = 0; i < rcc->num_entries - s->max_b_frames; i++) {
            RateControlEntry *rce;
            int picture_number;
            int e;
            char *next;

            next = strchr(p, ';');
            if (next) {
                (*next) = 0; // sscanf in unbelievably slow on looong strings // FIXME copy / do not write
                next++;
            }
            e = sscanf(p, " in:%d ", &picture_number);

            assert(picture_number >= 0);
            assert(picture_number < rcc->num_entries);
            rce = &rcc->entry[picture_number];

            e += sscanf(p, " in:%*d out:%*d type:%d q:%f itex:%d ptex:%d mv:%d misc:%d fcode:%d bcode:%d mc-var:%d var:%d icount:%d skipcount:%d hbits:%d",
                        &rce->pict_type, &rce->qscale, &rce->i_tex_bits, &rce->p_tex_bits,
                        &rce->mv_bits, &rce->misc_bits,
                        &rce->f_code, &rce->b_code,
                        &rce->mc_mb_var_sum, &rce->mb_var_sum,
                        &rce->i_count, &rce->skip_count, &rce->header_bits);
            if (e != 14) {
                av_log(s->avctx, AV_LOG_ERROR,
                       "statistics are damaged at line %d, parser out=%d\n",
                       i, e);
                return -1;
            }

            p = next;
        }

        if (init_pass2(s) < 0)
            return -1;

        // FIXME maybe move to end
        if ((s->flags & CODEC_FLAG_PASS2) && s->avctx->rc_strategy == FF_RC_STRATEGY_XVID) {
#if CONFIG_LIBXVID
            return ff_xvid_rate_control_init(s);
#else
            av_log(s->avctx, AV_LOG_ERROR,
                   "Xvid ratecontrol requires libavcodec compiled with Xvid support.\n");
            return -1;
#endif
        }
    }

    if (!(s->flags & CODEC_FLAG_PASS2)) {
        rcc->short_term_qsum   = 0.001;
        rcc->short_term_qcount = 0.001;

        rcc->pass1_rc_eq_output_sum = 0.001;
        rcc->pass1_wanted_bits      = 0.001;

        if (s->avctx->qblur > 1.0) {
            av_log(s->avctx, AV_LOG_ERROR, "qblur too large\n");
            return -1;
        }
        /* init stuff with the user specified complexity */
        if (s->avctx->rc_initial_cplx) {
            for (i = 0; i < 60 * 30; i++) {
                double bits = s->avctx->rc_initial_cplx * (i / 10000.0 + 1.0) * s->mb_num;
                RateControlEntry rce;

                if (i % ((s->gop_size + 3) / 4) == 0)
                    rce.pict_type = AV_PICTURE_TYPE_I;
                else if (i % (s->max_b_frames + 1))
                    rce.pict_type = AV_PICTURE_TYPE_B;
                else
                    rce.pict_type = AV_PICTURE_TYPE_P;

                rce.new_pict_type = rce.pict_type;
                rce.mc_mb_var_sum = bits * s->mb_num / 100000;
                rce.mb_var_sum    = s->mb_num;

                rce.qscale    = FF_QP2LAMBDA * 2;
                rce.f_code    = 2;
                rce.b_code    = 1;
                rce.misc_bits = 1;

                if (s->pict_type == AV_PICTURE_TYPE_I) {
                    rce.i_count    = s->mb_num;
                    rce.i_tex_bits = bits;
                    rce.p_tex_bits = 0;
                    rce.mv_bits    = 0;
                } else {
                    rce.i_count    = 0; // FIXME we do know this approx
                    rce.i_tex_bits = 0;
                    rce.p_tex_bits = bits * 0.9;
                    rce.mv_bits    = bits * 0.1;
                }
                rcc->i_cplx_sum[rce.pict_type]  += rce.i_tex_bits * rce.qscale;
                rcc->p_cplx_sum[rce.pict_type]  += rce.p_tex_bits * rce.qscale;
                rcc->mv_bits_sum[rce.pict_type] += rce.mv_bits;
                rcc->frame_count[rce.pict_type]++;

                get_qscale(s, &rce, rcc->pass1_wanted_bits / rcc->pass1_rc_eq_output_sum, i);

                // FIXME misbehaves a little for variable fps
                rcc->pass1_wanted_bits += s->bit_rate / get_fps(s->avctx);
            }
        }
    }

    return 0;
}

av_cold void ff_rate_control_uninit(MpegEncContext *s)
{
    RateControlContext *rcc = &s->rc_context;
    emms_c();

    av_expr_free(rcc->rc_eq_eval);
    av_freep(&rcc->entry);

#if CONFIG_LIBXVID
    if ((s->flags & CODEC_FLAG_PASS2) && s->avctx->rc_strategy == FF_RC_STRATEGY_XVID)
        ff_xvid_rate_control_uninit(s);
#endif
}

int ff_vbv_update(MpegEncContext *s, int frame_size)
{
    RateControlContext *rcc = &s->rc_context;
    const double fps        = get_fps(s->avctx);
    const int buffer_size   = s->avctx->rc_buffer_size;
    const double min_rate   = s->avctx->rc_min_rate / fps;
    const double max_rate   = s->avctx->rc_max_rate / fps;

    av_dlog(s, "%d %f %d %f %f\n",
            buffer_size, rcc->buffer_index, frame_size, min_rate, max_rate);

    if (buffer_size) {
        int left;

        rcc->buffer_index -= frame_size;
        if (rcc->buffer_index < 0) {
            av_log(s->avctx, AV_LOG_ERROR, "rc buffer underflow\n");
            rcc->buffer_index = 0;
        }

        left = buffer_size - rcc->buffer_index - 1;
        rcc->buffer_index += av_clip(left, min_rate, max_rate);

        if (rcc->buffer_index > buffer_size) {
            int stuffing = ceil((rcc->buffer_index - buffer_size) / 8);

            if (stuffing < 4 && s->codec_id == AV_CODEC_ID_MPEG4)
                stuffing = 4;
            rcc->buffer_index -= 8 * stuffing;

            if (s->avctx->debug & FF_DEBUG_RC)
                av_log(s->avctx, AV_LOG_DEBUG, "stuffing %d bytes\n", stuffing);

            return stuffing;
        }
    }
    return 0;
}

/**
 * Modify the bitrate curve from pass1 for one frame.
 */
static double get_qscale(MpegEncContext *s, RateControlEntry *rce,
                         double rate_factor, int frame_num)
{
    RateControlContext *rcc = &s->rc_context;
    AVCodecContext *a       = s->avctx;
    const int pict_type     = rce->new_pict_type;
    const double mb_num     = s->mb_num;
    double q, bits;
    int i;

    double const_values[] = {
        M_PI,
        M_E,
        rce->i_tex_bits * rce->qscale,
        rce->p_tex_bits * rce->qscale,
        (rce->i_tex_bits + rce->p_tex_bits) * (double)rce->qscale,
        rce->mv_bits / mb_num,
        rce->pict_type == AV_PICTURE_TYPE_B ? (rce->f_code + rce->b_code) * 0.5 : rce->f_code,
        rce->i_count / mb_num,
        rce->mc_mb_var_sum / mb_num,
        rce->mb_var_sum / mb_num,
        rce->pict_type == AV_PICTURE_TYPE_I,
        rce->pict_type == AV_PICTURE_TYPE_P,
        rce->pict_type == AV_PICTURE_TYPE_B,
        rcc->qscale_sum[pict_type] / (double)rcc->frame_count[pict_type],
        a->qcompress,
#if 0
        rcc->last_qscale_for[AV_PICTURE_TYPE_I],
        rcc->last_qscale_for[AV_PICTURE_TYPE_P],
        rcc->last_qscale_for[AV_PICTURE_TYPE_B],
        rcc->next_non_b_qscale,
#endif
        rcc->i_cplx_sum[AV_PICTURE_TYPE_I] / (double)rcc->frame_count[AV_PICTURE_TYPE_I],
        rcc->i_cplx_sum[AV_PICTURE_TYPE_P] / (double)rcc->frame_count[AV_PICTURE_TYPE_P],
        rcc->p_cplx_sum[AV_PICTURE_TYPE_P] / (double)rcc->frame_count[AV_PICTURE_TYPE_P],
        rcc->p_cplx_sum[AV_PICTURE_TYPE_B] / (double)rcc->frame_count[AV_PICTURE_TYPE_B],
        (rcc->i_cplx_sum[pict_type] + rcc->p_cplx_sum[pict_type]) / (double)rcc->frame_count[pict_type],
        0
    };

    bits = av_expr_eval(rcc->rc_eq_eval, const_values, rce);
    if (isnan(bits)) {
        av_log(s->avctx, AV_LOG_ERROR, "Error evaluating rc_eq \"%s\"\n", s->avctx->rc_eq);
        return -1;
    }

    rcc->pass1_rc_eq_output_sum += bits;
    bits *= rate_factor;
    if (bits < 0.0)
        bits = 0.0;
    bits += 1.0; // avoid 1/0 issues

    /* user override */
    for (i = 0; i < s->avctx->rc_override_count; i++) {
        RcOverride *rco = s->avctx->rc_override;
        if (rco[i].start_frame > frame_num)
            continue;
        if (rco[i].end_frame < frame_num)
            continue;

        if (rco[i].qscale)
            bits = qp2bits(rce, rco[i].qscale);  // FIXME move at end to really force it?
        else
            bits *= rco[i].quality_factor;
    }

    q = bits2qp(rce, bits);

    /* I/B difference */
    if (pict_type == AV_PICTURE_TYPE_I && s->avctx->i_quant_factor < 0.0)
        q = -q * s->avctx->i_quant_factor + s->avctx->i_quant_offset;
    else if (pict_type == AV_PICTURE_TYPE_B && s->avctx->b_quant_factor < 0.0)
        q = -q * s->avctx->b_quant_factor + s->avctx->b_quant_offset;
    if (q < 1)
        q = 1;

    return q;
}

static double get_diff_limited_q(MpegEncContext *s, RateControlEntry *rce, double q)
{
    RateControlContext *rcc   = &s->rc_context;
    AVCodecContext *a         = s->avctx;
    const int pict_type       = rce->new_pict_type;
    const double last_p_q     = rcc->last_qscale_for[AV_PICTURE_TYPE_P];
    const double last_non_b_q = rcc->last_qscale_for[rcc->last_non_b_pict_type];

    if (pict_type == AV_PICTURE_TYPE_I &&
        (a->i_quant_factor > 0.0 || rcc->last_non_b_pict_type == AV_PICTURE_TYPE_P))
        q = last_p_q * FFABS(a->i_quant_factor) + a->i_quant_offset;
    else if (pict_type == AV_PICTURE_TYPE_B &&
             a->b_quant_factor > 0.0)
        q = last_non_b_q * a->b_quant_factor + a->b_quant_offset;
    if (q < 1)
        q = 1;

    /* last qscale / qdiff stuff */
    if (rcc->last_non_b_pict_type == pict_type || pict_type != AV_PICTURE_TYPE_I) {
        double last_q     = rcc->last_qscale_for[pict_type];
        const int maxdiff = FF_QP2LAMBDA * a->max_qdiff;

        if (q > last_q + maxdiff)
            q = last_q + maxdiff;
        else if (q < last_q - maxdiff)
            q = last_q - maxdiff;
    }

    rcc->last_qscale_for[pict_type] = q; // Note we cannot do that after blurring

    if (pict_type != AV_PICTURE_TYPE_B)
        rcc->last_non_b_pict_type = pict_type;

    return q;
}

/**
 * Get the qmin & qmax for pict_type.
 */
static void get_qminmax(int *qmin_ret, int *qmax_ret, MpegEncContext *s, int pict_type)
{
    int qmin = s->avctx->lmin;
    int qmax = s->avctx->lmax;

    assert(qmin <= qmax);

    switch (pict_type) {
    case AV_PICTURE_TYPE_B:
        qmin = (int)(qmin * FFABS(s->avctx->b_quant_factor) + s->avctx->b_quant_offset + 0.5);
        qmax = (int)(qmax * FFABS(s->avctx->b_quant_factor) + s->avctx->b_quant_offset + 0.5);
        break;
    case AV_PICTURE_TYPE_I:
        qmin = (int)(qmin * FFABS(s->avctx->i_quant_factor) + s->avctx->i_quant_offset + 0.5);
        qmax = (int)(qmax * FFABS(s->avctx->i_quant_factor) + s->avctx->i_quant_offset + 0.5);
        break;
    }

    qmin = av_clip(qmin, 1, FF_LAMBDA_MAX);
    qmax = av_clip(qmax, 1, FF_LAMBDA_MAX);

    if (qmax < qmin)
        qmax = qmin;

    *qmin_ret = qmin;
    *qmax_ret = qmax;
}

static double modify_qscale(MpegEncContext *s, RateControlEntry *rce,
                            double q, int frame_num)
{
    RateControlContext *rcc  = &s->rc_context;
    const double buffer_size = s->avctx->rc_buffer_size;
    const double fps         = get_fps(s->avctx);
    const double min_rate    = s->avctx->rc_min_rate / fps;
    const double max_rate    = s->avctx->rc_max_rate / fps;
    const int pict_type      = rce->new_pict_type;
    int qmin, qmax;

    get_qminmax(&qmin, &qmax, s, pict_type);

    /* modulation */
    if (s->avctx->rc_qmod_freq &&
        frame_num % s->avctx->rc_qmod_freq == 0 &&
        pict_type == AV_PICTURE_TYPE_P)
        q *= s->avctx->rc_qmod_amp;

    /* buffer overflow/underflow protection */
    if (buffer_size) {
        double expected_size = rcc->buffer_index;
        double q_limit;

        if (min_rate) {
            double d = 2 * (buffer_size - expected_size) / buffer_size;
            if (d > 1.0)
                d = 1.0;
            else if (d < 0.0001)
                d = 0.0001;
            q *= pow(d, 1.0 / s->avctx->rc_buffer_aggressivity);

            q_limit = bits2qp(rce,
                              FFMAX((min_rate - buffer_size + rcc->buffer_index) *
                                    s->avctx->rc_min_vbv_overflow_use, 1));

            if (q > q_limit) {
                if (s->avctx->debug & FF_DEBUG_RC)
                    av_log(s->avctx, AV_LOG_DEBUG,
                           "limiting QP %f -> %f\n", q, q_limit);
                q = q_limit;
            }
        }

        if (max_rate) {
            double d = 2 * expected_size / buffer_size;
            if (d > 1.0)
                d = 1.0;
            else if (d < 0.0001)
                d = 0.0001;
            q /= pow(d, 1.0 / s->avctx->rc_buffer_aggressivity);

            q_limit = bits2qp(rce,
                              FFMAX(rcc->buffer_index *
                                    s->avctx->rc_max_available_vbv_use,
                                    1));
            if (q < q_limit) {
                if (s->avctx->debug & FF_DEBUG_RC)
                    av_log(s->avctx, AV_LOG_DEBUG,
                           "limiting QP %f -> %f\n", q, q_limit);
                q = q_limit;
            }
        }
    }
    av_dlog(s, "q:%f max:%f min:%f size:%f index:%f agr:%f\n",
            q, max_rate, min_rate, buffer_size, rcc->buffer_index,
            s->avctx->rc_buffer_aggressivity);
    if (s->avctx->rc_qsquish == 0.0 || qmin == qmax) {
        if (q < qmin)
            q = qmin;
        else if (q > qmax)
            q = qmax;
    } else {
        double min2 = log(qmin);
        double max2 = log(qmax);

        q  = log(q);
        q  = (q - min2) / (max2 - min2) - 0.5;
        q *= -4.0;
        q  = 1.0 / (1.0 + exp(q));
        q  = q * (max2 - min2) + min2;

        q = exp(q);
    }

    return q;
}

// ----------------------------------
// 1 Pass Code

static double predict_size(Predictor *p, double q, double var)
{
    return p->coeff * var / (q * p->count);
}

static void update_predictor(Predictor *p, double q, double var, double size)
{
    double new_coeff = size * q / (var + 1);
    if (var < 10)
        return;

    p->count *= p->decay;
    p->coeff *= p->decay;
    p->count++;
    p->coeff += new_coeff;
}

static void adaptive_quantization(MpegEncContext *s, double q)
{
    int i;
    const float lumi_masking         = s->avctx->lumi_masking / (128.0 * 128.0);
    const float dark_masking         = s->avctx->dark_masking / (128.0 * 128.0);
    const float temp_cplx_masking    = s->avctx->temporal_cplx_masking;
    const float spatial_cplx_masking = s->avctx->spatial_cplx_masking;
    const float p_masking            = s->avctx->p_masking;
    const float border_masking       = s->avctx->border_masking;
    float bits_sum                   = 0.0;
    float cplx_sum                   = 0.0;
    float *cplx_tab                  = s->cplx_tab;
    float *bits_tab                  = s->bits_tab;
    const int qmin                   = s->avctx->mb_lmin;
    const int qmax                   = s->avctx->mb_lmax;
    Picture *const pic               = &s->current_picture;
    const int mb_width               = s->mb_width;
    const int mb_height              = s->mb_height;

    for (i = 0; i < s->mb_num; i++) {
        const int mb_xy = s->mb_index2xy[i];
        float temp_cplx = sqrt(pic->mc_mb_var[mb_xy]); // FIXME merge in pow()
        float spat_cplx = sqrt(pic->mb_var[mb_xy]);
        const int lumi  = pic->mb_mean[mb_xy];
        float bits, cplx, factor;
        int mb_x = mb_xy % s->mb_stride;
        int mb_y = mb_xy / s->mb_stride;
        int mb_distance;
        float mb_factor = 0.0;
        if (spat_cplx < 4)
            spat_cplx = 4;              // FIXME finetune
        if (temp_cplx < 4)
            temp_cplx = 4;              // FIXME finetune

        if ((s->mb_type[mb_xy] & CANDIDATE_MB_TYPE_INTRA)) { // FIXME hq mode
            cplx   = spat_cplx;
            factor = 1.0 + p_masking;
        } else {
            cplx   = temp_cplx;
            factor = pow(temp_cplx, -temp_cplx_masking);
        }
        factor *= pow(spat_cplx, -spatial_cplx_masking);

        if (lumi > 127)
            factor *= (1.0 - (lumi - 128) * (lumi - 128) * lumi_masking);
        else
            factor *= (1.0 - (lumi - 128) * (lumi - 128) * dark_masking);

        if (mb_x < mb_width / 5) {
            mb_distance = mb_width / 5 - mb_x;
            mb_factor   = (float)mb_distance / (float)(mb_width / 5);
        } else if (mb_x > 4 * mb_width / 5) {
            mb_distance = mb_x - 4 * mb_width / 5;
            mb_factor   = (float)mb_distance / (float)(mb_width / 5);
        }
        if (mb_y < mb_height / 5) {
            mb_distance = mb_height / 5 - mb_y;
            mb_factor   = FFMAX(mb_factor,
                                (float)mb_distance / (float)(mb_height / 5));
        } else if (mb_y > 4 * mb_height / 5) {
            mb_distance = mb_y - 4 * mb_height / 5;
            mb_factor   = FFMAX(mb_factor,
                                (float)mb_distance / (float)(mb_height / 5));
        }

        factor *= 1.0 - border_masking * mb_factor;

        if (factor < 0.00001)
            factor = 0.00001;

        bits        = cplx * factor;
        cplx_sum   += cplx;
        bits_sum   += bits;
        cplx_tab[i] = cplx;
        bits_tab[i] = bits;
    }

    /* handle qmin/qmax clipping */
    if (s->flags & CODEC_FLAG_NORMALIZE_AQP) {
        float factor = bits_sum / cplx_sum;
        for (i = 0; i < s->mb_num; i++) {
            float newq = q * cplx_tab[i] / bits_tab[i];
            newq *= factor;

            if (newq > qmax) {
                bits_sum -= bits_tab[i];
                cplx_sum -= cplx_tab[i] * q / qmax;
            } else if (newq < qmin) {
                bits_sum -= bits_tab[i];
                cplx_sum -= cplx_tab[i] * q / qmin;
            }
        }
        if (bits_sum < 0.001)
            bits_sum = 0.001;
        if (cplx_sum < 0.001)
            cplx_sum = 0.001;
    }

    for (i = 0; i < s->mb_num; i++) {
        const int mb_xy = s->mb_index2xy[i];
        float newq      = q * cplx_tab[i] / bits_tab[i];
        int intq;

        if (s->flags & CODEC_FLAG_NORMALIZE_AQP) {
            newq *= bits_sum / cplx_sum;
        }

        intq = (int)(newq + 0.5);

        if (intq > qmax)
            intq = qmax;
        else if (intq < qmin)
            intq = qmin;
        s->lambda_table[mb_xy] = intq;
    }
}

void ff_get_2pass_fcode(MpegEncContext *s)
{
    RateControlContext *rcc = &s->rc_context;
    RateControlEntry *rce   = &rcc->entry[s->picture_number];

    s->f_code = rce->f_code;
    s->b_code = rce->b_code;
}

// FIXME rd or at least approx for dquant

float ff_rate_estimate_qscale(MpegEncContext *s, int dry_run)
{
    float q;
    int qmin, qmax;
    float br_compensation;
    double diff;
    double short_term_q;
    double fps;
    int picture_number = s->picture_number;
    int64_t wanted_bits;
    RateControlContext *rcc = &s->rc_context;
    AVCodecContext *a       = s->avctx;
    RateControlEntry local_rce, *rce;
    double bits;
    double rate_factor;
    int var;
    const int pict_type = s->pict_type;
    Picture * const pic = &s->current_picture;
    emms_c();

#if CONFIG_LIBXVID
    if ((s->flags & CODEC_FLAG_PASS2) &&
        s->avctx->rc_strategy == FF_RC_STRATEGY_XVID)
        return ff_xvid_rate_estimate_qscale(s, dry_run);
#endif

    get_qminmax(&qmin, &qmax, s, pict_type);

    fps = get_fps(s->avctx);
    /* update predictors */
    if (picture_number > 2 && !dry_run) {
        const int last_var = s->last_pict_type == AV_PICTURE_TYPE_I ? rcc->last_mb_var_sum
                                                                    : rcc->last_mc_mb_var_sum;
        av_assert1(s->frame_bits >= s->stuffing_bits);
        update_predictor(&rcc->pred[s->last_pict_type],
                         rcc->last_qscale,
                         sqrt(last_var),
                         s->frame_bits - s->stuffing_bits);
    }

    if (s->flags & CODEC_FLAG_PASS2) {
        assert(picture_number >= 0);
        if (picture_number >= rcc->num_entries) {
            av_log(s, AV_LOG_ERROR, "Input is longer than 2-pass log file\n");
            return -1;
        }
        rce         = &rcc->entry[picture_number];
        wanted_bits = rce->expected_bits;
    } else {
        Picture *dts_pic;
        rce = &local_rce;

        /* FIXME add a dts field to AVFrame and ensure it is set and use it
         * here instead of reordering but the reordering is simpler for now
         * until H.264 B-pyramid must be handled. */
        if (s->pict_type == AV_PICTURE_TYPE_B || s->low_delay)
            dts_pic = s->current_picture_ptr;
        else
            dts_pic = s->last_picture_ptr;

        if (!dts_pic || dts_pic->f.pts == AV_NOPTS_VALUE)
            wanted_bits = (uint64_t)(s->bit_rate * (double)picture_number / fps);
        else
            wanted_bits = (uint64_t)(s->bit_rate * (double)dts_pic->f.pts / fps);
    }

    diff = s->total_bits - wanted_bits;
    br_compensation = (a->bit_rate_tolerance - diff) / a->bit_rate_tolerance;
    if (br_compensation <= 0.0)
        br_compensation = 0.001;

    var = pict_type == AV_PICTURE_TYPE_I ? pic->mb_var_sum : pic->mc_mb_var_sum;

    short_term_q = 0; /* avoid warning */
    if (s->flags & CODEC_FLAG_PASS2) {
        if (pict_type != AV_PICTURE_TYPE_I)
            assert(pict_type == rce->new_pict_type);

        q = rce->new_qscale / br_compensation;
        av_dlog(s, "%f %f %f last:%d var:%d type:%d//\n", q, rce->new_qscale,
                br_compensation, s->frame_bits, var, pict_type);
    } else {
        rce->pict_type     =
        rce->new_pict_type = pict_type;
        rce->mc_mb_var_sum = pic->mc_mb_var_sum;
        rce->mb_var_sum    = pic->mb_var_sum;
        rce->qscale        = FF_QP2LAMBDA * 2;
        rce->f_code        = s->f_code;
        rce->b_code        = s->b_code;
        rce->misc_bits     = 1;

        bits = predict_size(&rcc->pred[pict_type], rce->qscale, sqrt(var));
        if (pict_type == AV_PICTURE_TYPE_I) {
            rce->i_count    = s->mb_num;
            rce->i_tex_bits = bits;
            rce->p_tex_bits = 0;
            rce->mv_bits    = 0;
        } else {
            rce->i_count    = 0;    // FIXME we do know this approx
            rce->i_tex_bits = 0;
            rce->p_tex_bits = bits * 0.9;
            rce->mv_bits    = bits * 0.1;
        }
        rcc->i_cplx_sum[pict_type]  += rce->i_tex_bits * rce->qscale;
        rcc->p_cplx_sum[pict_type]  += rce->p_tex_bits * rce->qscale;
        rcc->mv_bits_sum[pict_type] += rce->mv_bits;
        rcc->frame_count[pict_type]++;

        bits        = rce->i_tex_bits + rce->p_tex_bits;
        rate_factor = rcc->pass1_wanted_bits /
                      rcc->pass1_rc_eq_output_sum * br_compensation;

        q = get_qscale(s, rce, rate_factor, picture_number);
        if (q < 0)
            return -1;

        assert(q > 0.0);
        q = get_diff_limited_q(s, rce, q);
        assert(q > 0.0);

        // FIXME type dependent blur like in 2-pass
        if (pict_type == AV_PICTURE_TYPE_P || s->intra_only) {
            rcc->short_term_qsum   *= a->qblur;
            rcc->short_term_qcount *= a->qblur;

            rcc->short_term_qsum += q;
            rcc->short_term_qcount++;
            q = short_term_q = rcc->short_term_qsum / rcc->short_term_qcount;
        }
        assert(q > 0.0);

        q = modify_qscale(s, rce, q, picture_number);

        rcc->pass1_wanted_bits += s->bit_rate / fps;

        assert(q > 0.0);
    }

    if (s->avctx->debug & FF_DEBUG_RC) {
        av_log(s->avctx, AV_LOG_DEBUG,
               "%c qp:%d<%2.1f<%d %d want:%d total:%d comp:%f st_q:%2.2f "
               "size:%d var:%d/%d br:%d fps:%d\n",
               av_get_picture_type_char(pict_type),
               qmin, q, qmax, picture_number,
               (int)wanted_bits / 1000, (int)s->total_bits / 1000,
               br_compensation, short_term_q, s->frame_bits,
               pic->mb_var_sum, pic->mc_mb_var_sum,
               s->bit_rate / 1000, (int)fps);
    }

    if (q < qmin)
        q = qmin;
    else if (q > qmax)
        q = qmax;

    if (s->adaptive_quant)
        adaptive_quantization(s, q);
    else
        q = (int)(q + 0.5);

    if (!dry_run) {
        rcc->last_qscale        = q;
        rcc->last_mc_mb_var_sum = pic->mc_mb_var_sum;
        rcc->last_mb_var_sum    = pic->mb_var_sum;
    }
    return q;
}

// ----------------------------------------------
// 2-Pass code

static int init_pass2(MpegEncContext *s)
{
    RateControlContext *rcc = &s->rc_context;
    AVCodecContext *a       = s->avctx;
    int i, toobig;
    double fps             = get_fps(s->avctx);
    double complexity[5]   = { 0 }; // approximate bits at quant=1
    uint64_t const_bits[5] = { 0 }; // quantizer independent bits
    uint64_t all_const_bits;
    uint64_t all_available_bits = (uint64_t)(s->bit_rate *
                                             (double)rcc->num_entries / fps);
    double rate_factor          = 0;
    double step;
    const int filter_size = (int)(a->qblur * 4) | 1;
    double expected_bits = 0; // init to silence gcc warning
    double *qscale, *blurred_qscale, qscale_sum;

    /* find complexity & const_bits & decide the pict_types */
    for (i = 0; i < rcc->num_entries; i++) {
        RateControlEntry *rce = &rcc->entry[i];

        rce->new_pict_type                = rce->pict_type;
        rcc->i_cplx_sum[rce->pict_type]  += rce->i_tex_bits * rce->qscale;
        rcc->p_cplx_sum[rce->pict_type]  += rce->p_tex_bits * rce->qscale;
        rcc->mv_bits_sum[rce->pict_type] += rce->mv_bits;
        rcc->frame_count[rce->pict_type]++;

        complexity[rce->new_pict_type] += (rce->i_tex_bits + rce->p_tex_bits) *
                                          (double)rce->qscale;
        const_bits[rce->new_pict_type] += rce->mv_bits + rce->misc_bits;
    }

    all_const_bits = const_bits[AV_PICTURE_TYPE_I] +
                     const_bits[AV_PICTURE_TYPE_P] +
                     const_bits[AV_PICTURE_TYPE_B];

    if (all_available_bits < all_const_bits) {
        av_log(s->avctx, AV_LOG_ERROR, "requested bitrate is too low\n");
        return -1;
    }

    qscale         = av_malloc(sizeof(double) * rcc->num_entries);
    blurred_qscale = av_malloc(sizeof(double) * rcc->num_entries);
    toobig = 0;

    for (step = 256 * 256; step > 0.0000001; step *= 0.5) {
        expected_bits = 0;
        rate_factor  += step;

        rcc->buffer_index = s->avctx->rc_buffer_size / 2;

        /* find qscale */
        for (i = 0; i < rcc->num_entries; i++) {
            RateControlEntry *rce = &rcc->entry[i];

            qscale[i] = get_qscale(s, &rcc->entry[i], rate_factor, i);
            rcc->last_qscale_for[rce->pict_type] = qscale[i];
        }
        assert(filter_size % 2 == 1);

        /* fixed I/B QP relative to P mode */
        for (i = FFMAX(0, rcc->num_entries - 300); i < rcc->num_entries; i++) {
            RateControlEntry *rce = &rcc->entry[i];

            qscale[i] = get_diff_limited_q(s, rce, qscale[i]);
        }

        for (i = rcc->num_entries - 1; i >= 0; i--) {
            RateControlEntry *rce = &rcc->entry[i];

            qscale[i] = get_diff_limited_q(s, rce, qscale[i]);
        }

        /* smooth curve */
        for (i = 0; i < rcc->num_entries; i++) {
            RateControlEntry *rce = &rcc->entry[i];
            const int pict_type   = rce->new_pict_type;
            int j;
            double q = 0.0, sum = 0.0;

            for (j = 0; j < filter_size; j++) {
                int index    = i + j - filter_size / 2;
                double d     = index - i;
                double coeff = a->qblur == 0 ? 1.0 : exp(-d * d / (a->qblur * a->qblur));

                if (index < 0 || index >= rcc->num_entries)
                    continue;
                if (pict_type != rcc->entry[index].new_pict_type)
                    continue;
                q   += qscale[index] * coeff;
                sum += coeff;
            }
            blurred_qscale[i] = q / sum;
        }

        /* find expected bits */
        for (i = 0; i < rcc->num_entries; i++) {
            RateControlEntry *rce = &rcc->entry[i];
            double bits;

            rce->new_qscale = modify_qscale(s, rce, blurred_qscale[i], i);

            bits  = qp2bits(rce, rce->new_qscale) + rce->mv_bits + rce->misc_bits;
            bits += 8 * ff_vbv_update(s, bits);

            rce->expected_bits = expected_bits;
            expected_bits     += bits;
        }

        av_dlog(s->avctx,
                "expected_bits: %f all_available_bits: %d rate_factor: %f\n",
                expected_bits, (int)all_available_bits, rate_factor);
        if (expected_bits > all_available_bits) {
            rate_factor -= step;
            ++toobig;
        }
    }
    av_free(qscale);
    av_free(blurred_qscale);

    /* check bitrate calculations and print info */
    qscale_sum = 0.0;
    for (i = 0; i < rcc->num_entries; i++) {
        av_dlog(s, "[lavc rc] entry[%d].new_qscale = %.3f  qp = %.3f\n",
                i,
                rcc->entry[i].new_qscale,
                rcc->entry[i].new_qscale / FF_QP2LAMBDA);
        qscale_sum += av_clip(rcc->entry[i].new_qscale / FF_QP2LAMBDA,
                              s->avctx->qmin, s->avctx->qmax);
    }
    assert(toobig <= 40);
    av_log(s->avctx, AV_LOG_DEBUG,
           "[lavc rc] requested bitrate: %d bps  expected bitrate: %d bps\n",
           s->bit_rate,
           (int)(expected_bits / ((double)all_available_bits / s->bit_rate)));
    av_log(s->avctx, AV_LOG_DEBUG,
           "[lavc rc] estimated target average qp: %.3f\n",
           (float)qscale_sum / rcc->num_entries);
    if (toobig == 0) {
        av_log(s->avctx, AV_LOG_INFO,
               "[lavc rc] Using all of requested bitrate is not "
               "necessary for this video with these parameters.\n");
    } else if (toobig == 40) {
        av_log(s->avctx, AV_LOG_ERROR,
               "[lavc rc] Error: bitrate too low for this video "
               "with these parameters.\n");
        return -1;
    } else if (fabs(expected_bits / all_available_bits - 1.0) > 0.01) {
        av_log(s->avctx, AV_LOG_ERROR,
               "[lavc rc] Error: 2pass curve failed to converge\n");
        return -1;
    }

    return 0;
}
