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
#include "libavutil/emms.h"
#include "libavutil/internal.h"
#include "libavutil/mem.h"

#include "avcodec.h"
#include "ratecontrol.h"
#include "mpegvideoenc.h"
#include "libavutil/eval.h"

void ff_write_pass1_stats(MPVMainEncContext *const m)
{
    const MPVEncContext *const s = &m->s;
    snprintf(s->c.avctx->stats_out, 256,
             "in:%d out:%d type:%d q:%d itex:%d ptex:%d mv:%d misc:%d "
             "fcode:%d bcode:%d mc-var:%"PRId64" var:%"PRId64" icount:%d hbits:%d;\n",
             s->c.cur_pic.ptr->display_picture_number,
             s->c.cur_pic.ptr->coded_picture_number,
             s->c.pict_type,
             s->c.cur_pic.ptr->f->quality,
             s->i_tex_bits,
             s->p_tex_bits,
             s->mv_bits,
             s->misc_bits,
             s->f_code,
             s->b_code,
             m->mc_mb_var_sum,
             m->mb_var_sum,
             s->i_count,
             m->header_bits);
}

static AVRational get_fpsQ(AVCodecContext *avctx)
{
    if (avctx->framerate.num > 0 && avctx->framerate.den > 0)
        return avctx->framerate;

    return av_inv_q(avctx->time_base);
}

static double get_fps(AVCodecContext *avctx)
{
    return av_q2d(get_fpsQ(avctx));
}

static inline double qp2bits(const RateControlEntry *rce, double qp)
{
    if (qp <= 0.0) {
        av_log(NULL, AV_LOG_ERROR, "qp<=0.0\n");
    }
    return rce->qscale * (double)(rce->i_tex_bits + rce->p_tex_bits + 1) / qp;
}

static double qp2bits_cb(void *rce, double qp)
{
    return qp2bits(rce, qp);
}

static inline double bits2qp(const RateControlEntry *rce, double bits)
{
    if (bits < 0.9) {
        av_log(NULL, AV_LOG_ERROR, "bits<0.9\n");
    }
    return rce->qscale * (double)(rce->i_tex_bits + rce->p_tex_bits + 1) / bits;
}

static double bits2qp_cb(void *rce, double qp)
{
    return bits2qp(rce, qp);
}

static double get_diff_limited_q(MPVMainEncContext *m, const RateControlEntry *rce, double q)
{
    MPVEncContext      *const   s = &m->s;
    RateControlContext *const rcc = &m->rc_context;
    AVCodecContext     *const   a = s->c.avctx;
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
static void get_qminmax(int *qmin_ret, int *qmax_ret, MPVMainEncContext *const m, int pict_type)
{
    MPVEncContext *const s = &m->s;
    int qmin = m->lmin;
    int qmax = m->lmax;

    av_assert0(qmin <= qmax);

    switch (pict_type) {
    case AV_PICTURE_TYPE_B:
        qmin = (int)(qmin * FFABS(s->c.avctx->b_quant_factor) + s->c.avctx->b_quant_offset + 0.5);
        qmax = (int)(qmax * FFABS(s->c.avctx->b_quant_factor) + s->c.avctx->b_quant_offset + 0.5);
        break;
    case AV_PICTURE_TYPE_I:
        qmin = (int)(qmin * FFABS(s->c.avctx->i_quant_factor) + s->c.avctx->i_quant_offset + 0.5);
        qmax = (int)(qmax * FFABS(s->c.avctx->i_quant_factor) + s->c.avctx->i_quant_offset + 0.5);
        break;
    }

    qmin = av_clip(qmin, 1, FF_LAMBDA_MAX);
    qmax = av_clip(qmax, 1, FF_LAMBDA_MAX);

    if (qmax < qmin)
        qmax = qmin;

    *qmin_ret = qmin;
    *qmax_ret = qmax;
}

static double modify_qscale(MPVMainEncContext *const m, const RateControlEntry *rce,
                            double q, int frame_num)
{
    MPVEncContext      *const   s = &m->s;
    RateControlContext *const rcc = &m->rc_context;
    const double buffer_size = s->c.avctx->rc_buffer_size;
    const double fps         = get_fps(s->c.avctx);
    const double min_rate    = s->c.avctx->rc_min_rate / fps;
    const double max_rate    = s->c.avctx->rc_max_rate / fps;
    const int pict_type      = rce->new_pict_type;
    int qmin, qmax;

    get_qminmax(&qmin, &qmax, m, pict_type);

    /* modulation */
    if (rcc->qmod_freq &&
        frame_num % rcc->qmod_freq == 0 &&
        pict_type == AV_PICTURE_TYPE_P)
        q *= rcc->qmod_amp;

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
            q *= pow(d, 1.0 / rcc->buffer_aggressivity);

            q_limit = bits2qp(rce,
                              FFMAX((min_rate - buffer_size + rcc->buffer_index) *
                                    s->c.avctx->rc_min_vbv_overflow_use, 1));

            if (q > q_limit) {
                if (s->c.avctx->debug & FF_DEBUG_RC)
                    av_log(s->c.avctx, AV_LOG_DEBUG,
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
            q /= pow(d, 1.0 / rcc->buffer_aggressivity);

            q_limit = bits2qp(rce,
                              FFMAX(rcc->buffer_index *
                                    s->c.avctx->rc_max_available_vbv_use,
                                    1));
            if (q < q_limit) {
                if (s->c.avctx->debug & FF_DEBUG_RC)
                    av_log(s->c.avctx, AV_LOG_DEBUG,
                           "limiting QP %f -> %f\n", q, q_limit);
                q = q_limit;
            }
        }
    }
    ff_dlog(s->c.avctx, "q:%f max:%f min:%f size:%f index:%f agr:%f\n",
            q, max_rate, min_rate, buffer_size, rcc->buffer_index,
            rcc->buffer_aggressivity);
    if (rcc->qsquish == 0.0 || qmin == qmax) {
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

/**
 * Modify the bitrate curve from pass1 for one frame.
 */
static double get_qscale(MPVMainEncContext *const m, RateControlEntry *rce,
                         double rate_factor, int frame_num)
{
    MPVEncContext  *const s = &m->s;
    RateControlContext *rcc = &m->rc_context;
    AVCodecContext *const avctx = s->c.avctx;
    const int pict_type     = rce->new_pict_type;
    const double mb_num     = s->c.mb_num;
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
        avctx->qcompress,
        rcc->i_cplx_sum[AV_PICTURE_TYPE_I] / (double)rcc->frame_count[AV_PICTURE_TYPE_I],
        rcc->i_cplx_sum[AV_PICTURE_TYPE_P] / (double)rcc->frame_count[AV_PICTURE_TYPE_P],
        rcc->p_cplx_sum[AV_PICTURE_TYPE_P] / (double)rcc->frame_count[AV_PICTURE_TYPE_P],
        rcc->p_cplx_sum[AV_PICTURE_TYPE_B] / (double)rcc->frame_count[AV_PICTURE_TYPE_B],
        (rcc->i_cplx_sum[pict_type] + rcc->p_cplx_sum[pict_type]) / (double)rcc->frame_count[pict_type],
        0
    };

    bits = av_expr_eval(rcc->rc_eq_eval, const_values, rce);
    if (isnan(bits)) {
        av_log(avctx, AV_LOG_ERROR, "Error evaluating rc_eq \"%s\"\n", rcc->rc_eq);
        return -1;
    }

    rcc->pass1_rc_eq_output_sum += bits;
    bits *= rate_factor;
    if (bits < 0.0)
        bits = 0.0;
    bits += 1.0; // avoid 1/0 issues

    /* user override */
    for (i = 0; i < avctx->rc_override_count; i++) {
        RcOverride *rco = avctx->rc_override;
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
    if (pict_type == AV_PICTURE_TYPE_I && avctx->i_quant_factor < 0.0)
        q = -q * avctx->i_quant_factor + avctx->i_quant_offset;
    else if (pict_type == AV_PICTURE_TYPE_B && avctx->b_quant_factor < 0.0)
        q = -q * avctx->b_quant_factor + avctx->b_quant_offset;
    if (q < 1)
        q = 1;

    return q;
}

static int init_pass2(MPVMainEncContext *const m)
{
    RateControlContext *const rcc = &m->rc_context;
    MPVEncContext      *const   s = &m->s;
    AVCodecContext     *const avctx = s->c.avctx;
    int i, toobig;
    AVRational fps         = get_fpsQ(avctx);
    double complexity[5]   = { 0 }; // approximate bits at quant=1
    uint64_t const_bits[5] = { 0 }; // quantizer independent bits
    uint64_t all_const_bits;
    uint64_t all_available_bits = av_rescale_q(m->bit_rate,
                                               (AVRational){rcc->num_entries,1},
                                               fps);
    double rate_factor          = 0;
    double step;
    const int filter_size = (int)(avctx->qblur * 4) | 1;
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
        av_log(avctx, AV_LOG_ERROR, "requested bitrate is too low\n");
        return -1;
    }

    qscale         = av_malloc_array(rcc->num_entries, sizeof(double));
    blurred_qscale = av_malloc_array(rcc->num_entries, sizeof(double));
    if (!qscale || !blurred_qscale) {
        av_free(qscale);
        av_free(blurred_qscale);
        return AVERROR(ENOMEM);
    }
    toobig = 0;

    for (step = 256 * 256; step > 0.0000001; step *= 0.5) {
        expected_bits = 0;
        rate_factor  += step;

        rcc->buffer_index = avctx->rc_buffer_size / 2;

        /* find qscale */
        for (i = 0; i < rcc->num_entries; i++) {
            const RateControlEntry *rce = &rcc->entry[i];

            qscale[i] = get_qscale(m, &rcc->entry[i], rate_factor, i);
            rcc->last_qscale_for[rce->pict_type] = qscale[i];
        }
        av_assert0(filter_size % 2 == 1);

        /* fixed I/B QP relative to P mode */
        for (i = FFMAX(0, rcc->num_entries - 300); i < rcc->num_entries; i++) {
            const RateControlEntry *rce = &rcc->entry[i];

            qscale[i] = get_diff_limited_q(m, rce, qscale[i]);
        }

        for (i = rcc->num_entries - 1; i >= 0; i--) {
            const RateControlEntry *rce = &rcc->entry[i];

            qscale[i] = get_diff_limited_q(m, rce, qscale[i]);
        }

        /* smooth curve */
        for (i = 0; i < rcc->num_entries; i++) {
            const RateControlEntry *rce = &rcc->entry[i];
            const int pict_type   = rce->new_pict_type;
            int j;
            double q = 0.0, sum = 0.0;

            for (j = 0; j < filter_size; j++) {
                int index    = i + j - filter_size / 2;
                double d     = index - i;
                double coeff = avctx->qblur == 0 ? 1.0 : exp(-d * d / (avctx->qblur * avctx->qblur));

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

            rce->new_qscale = modify_qscale(m, rce, blurred_qscale[i], i);

            bits  = qp2bits(rce, rce->new_qscale) + rce->mv_bits + rce->misc_bits;
            bits += 8 * ff_vbv_update(m, bits);

            rce->expected_bits = expected_bits;
            expected_bits     += bits;
        }

        ff_dlog(avctx,
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
        ff_dlog(avctx, "[lavc rc] entry[%d].new_qscale = %.3f  qp = %.3f\n",
                i,
                rcc->entry[i].new_qscale,
                rcc->entry[i].new_qscale / FF_QP2LAMBDA);
        qscale_sum += av_clip(rcc->entry[i].new_qscale / FF_QP2LAMBDA,
                              avctx->qmin, avctx->qmax);
    }
    av_assert0(toobig <= 40);
    av_log(avctx, AV_LOG_DEBUG,
           "[lavc rc] requested bitrate: %"PRId64" bps  expected bitrate: %"PRId64" bps\n",
           m->bit_rate,
           (int64_t)(expected_bits / ((double)all_available_bits / m->bit_rate)));
    av_log(avctx, AV_LOG_DEBUG,
           "[lavc rc] estimated target average qp: %.3f\n",
           (float)qscale_sum / rcc->num_entries);
    if (toobig == 0) {
        av_log(avctx, AV_LOG_INFO,
               "[lavc rc] Using all of requested bitrate is not "
               "necessary for this video with these parameters.\n");
    } else if (toobig == 40) {
        av_log(avctx, AV_LOG_ERROR,
               "[lavc rc] Error: bitrate too low for this video "
               "with these parameters.\n");
        return -1;
    } else if (fabs(expected_bits / all_available_bits - 1.0) > 0.01) {
        av_log(avctx, AV_LOG_ERROR,
               "[lavc rc] Error: 2pass curve failed to converge\n");
        return -1;
    }

    return 0;
}

av_cold int ff_rate_control_init(MPVMainEncContext *const m)
{
    MPVEncContext  *const s = &m->s;
    RateControlContext *rcc = &m->rc_context;
    AVCodecContext *const avctx = s->c.avctx;
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
        "avgIITex",
        "avgPITex",
        "avgPPTex",
        "avgBPTex",
        "avgTex",
        NULL
    };
    static double (* const func1[])(void *, double) = {
        bits2qp_cb,
        qp2bits_cb,
        NULL
    };
    static const char * const func1_names[] = {
        "bits2qp",
        "qp2bits",
        NULL
    };
    emms_c();

    if (!avctx->rc_max_available_vbv_use && avctx->rc_buffer_size) {
        if (avctx->rc_max_rate) {
            avctx->rc_max_available_vbv_use = av_clipf(avctx->rc_max_rate/(avctx->rc_buffer_size*get_fps(avctx)), 1.0/3, 1.0);
        } else
            avctx->rc_max_available_vbv_use = 1.0;
    }

    res = av_expr_parse(&rcc->rc_eq_eval,
                        rcc->rc_eq ? rcc->rc_eq : "tex^qComp",
                        const_names, func1_names, func1,
                        NULL, NULL, 0, avctx);
    if (res < 0) {
        av_log(avctx, AV_LOG_ERROR, "Error parsing rc_eq \"%s\"\n", rcc->rc_eq);
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
    rcc->buffer_index = avctx->rc_initial_buffer_occupancy;
    if (!rcc->buffer_index)
        rcc->buffer_index = avctx->rc_buffer_size * 3 / 4;

    if (avctx->flags & AV_CODEC_FLAG_PASS2) {
        int i;
        char *p;

        /* find number of pics */
        p = avctx->stats_in;
        for (i = -1; p; i++)
            p = strchr(p + 1, ';');
        i += m->max_b_frames;
        if (i <= 0 || i >= INT_MAX / sizeof(RateControlEntry))
            return -1;
        rcc->entry       = av_mallocz(i * sizeof(RateControlEntry));
        if (!rcc->entry)
            return AVERROR(ENOMEM);
        rcc->num_entries = i;

        /* init all to skipped P-frames
         * (with B-frames we might have a not encoded frame at the end FIXME) */
        for (i = 0; i < rcc->num_entries; i++) {
            RateControlEntry *rce = &rcc->entry[i];

            rce->pict_type  = rce->new_pict_type = AV_PICTURE_TYPE_P;
            rce->qscale     = rce->new_qscale    = FF_QP2LAMBDA * 2;
            rce->misc_bits  = s->c.mb_num + 10;
            rce->mb_var_sum = s->c.mb_num * 100;
        }

        /* read stats */
        p = avctx->stats_in;
        for (i = 0; i < rcc->num_entries - m->max_b_frames; i++) {
            RateControlEntry *rce;
            int picture_number;
            int e;
            char *next;

            next = strchr(p, ';');
            if (next) {
                (*next) = 0; // sscanf is unbelievably slow on looong strings // FIXME copy / do not write
                next++;
            }
            e = sscanf(p, " in:%d ", &picture_number);

            av_assert0(picture_number >= 0);
            av_assert0(picture_number < rcc->num_entries);
            rce = &rcc->entry[picture_number];

            e += sscanf(p, " in:%*d out:%*d type:%d q:%f itex:%d ptex:%d "
                        "mv:%d misc:%d "
                        "fcode:%d bcode:%d "
                        "mc-var:%"SCNd64" var:%"SCNd64" "
                        "icount:%d hbits:%d",
                        &rce->pict_type, &rce->qscale, &rce->i_tex_bits, &rce->p_tex_bits,
                        &rce->mv_bits, &rce->misc_bits,
                        &rce->f_code, &rce->b_code,
                        &rce->mc_mb_var_sum, &rce->mb_var_sum,
                        &rce->i_count, &rce->header_bits);
            if (e != 13) {
                av_log(avctx, AV_LOG_ERROR,
                       "statistics are damaged at line %d, parser out=%d\n",
                       i, e);
                return -1;
            }

            p = next;
        }

        res = init_pass2(m);
        if (res < 0)
            return res;
    }

    if (!(avctx->flags & AV_CODEC_FLAG_PASS2)) {
        rcc->short_term_qsum   = 0.001;
        rcc->short_term_qcount = 0.001;

        rcc->pass1_rc_eq_output_sum = 0.001;
        rcc->pass1_wanted_bits      = 0.001;

        if (avctx->qblur > 1.0) {
            av_log(avctx, AV_LOG_ERROR, "qblur too large\n");
            return -1;
        }
        /* init stuff with the user specified complexity */
        if (rcc->initial_cplx) {
            for (i = 0; i < 60 * 30; i++) {
                double bits = rcc->initial_cplx * (i / 10000.0 + 1.0) * s->c.mb_num;
                RateControlEntry rce;

                if (i % ((m->gop_size + 3) / 4) == 0)
                    rce.pict_type = AV_PICTURE_TYPE_I;
                else if (i % (m->max_b_frames + 1))
                    rce.pict_type = AV_PICTURE_TYPE_B;
                else
                    rce.pict_type = AV_PICTURE_TYPE_P;

                rce.new_pict_type = rce.pict_type;
                rce.mc_mb_var_sum = bits * s->c.mb_num / 100000;
                rce.mb_var_sum    = s->c.mb_num;

                rce.qscale    = FF_QP2LAMBDA * 2;
                rce.f_code    = 2;
                rce.b_code    = 1;
                rce.misc_bits = 1;

                if (s->c.pict_type == AV_PICTURE_TYPE_I) {
                    rce.i_count    = s->c.mb_num;
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

                get_qscale(m, &rce, rcc->pass1_wanted_bits / rcc->pass1_rc_eq_output_sum, i);

                // FIXME misbehaves a little for variable fps
                rcc->pass1_wanted_bits += m->bit_rate / get_fps(avctx);
            }
        }
    }

    if (s->adaptive_quant) {
        unsigned mb_array_size = s->c.mb_stride * s->c.mb_height;

        rcc->cplx_tab = av_malloc_array(mb_array_size, 2 * sizeof(*rcc->cplx_tab));
        if (!rcc->cplx_tab)
            return AVERROR(ENOMEM);
        rcc->bits_tab = rcc->cplx_tab + mb_array_size;
    }

    return 0;
}

av_cold void ff_rate_control_uninit(RateControlContext *rcc)
{
    emms_c();

    // rc_eq is always managed via an AVOption and therefore not freed here.
    av_expr_free(rcc->rc_eq_eval);
    rcc->rc_eq_eval = NULL;
    av_freep(&rcc->entry);
    av_freep(&rcc->cplx_tab);
}

int ff_vbv_update(MPVMainEncContext *m, int frame_size)
{
    MPVEncContext      *const   s = &m->s;
    RateControlContext *const rcc = &m->rc_context;
    AVCodecContext   *const avctx = s->c.avctx;
    const double fps      = get_fps(avctx);
    const int buffer_size = avctx->rc_buffer_size;
    const double min_rate = avctx->rc_min_rate / fps;
    const double max_rate = avctx->rc_max_rate / fps;

    ff_dlog(avctx, "%d %f %d %f %f\n",
            buffer_size, rcc->buffer_index, frame_size, min_rate, max_rate);

    if (buffer_size) {
        int left;

        rcc->buffer_index -= frame_size;
        if (rcc->buffer_index < 0) {
            av_log(avctx, AV_LOG_ERROR, "rc buffer underflow\n");
            if (frame_size > max_rate && s->c.qscale == avctx->qmax) {
                av_log(avctx, AV_LOG_ERROR, "max bitrate possibly too small or try trellis with large lmax or increase qmax\n");
            }
            rcc->buffer_index = 0;
        }

        left = buffer_size - rcc->buffer_index - 1;
        rcc->buffer_index += av_clip(left, min_rate, max_rate);

        if (rcc->buffer_index > buffer_size) {
            int stuffing = ceil((rcc->buffer_index - buffer_size) / 8);

            if (stuffing < 4 && s->c.codec_id == AV_CODEC_ID_MPEG4)
                stuffing = 4;
            rcc->buffer_index -= 8 * stuffing;

            if (avctx->debug & FF_DEBUG_RC)
                av_log(avctx, AV_LOG_DEBUG, "stuffing %d bytes\n", stuffing);

            return stuffing;
        }
    }
    return 0;
}

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

static void adaptive_quantization(RateControlContext *const rcc,
                                  MPVMainEncContext *const m, double q)
{
    MPVEncContext *const s = &m->s;
    const float lumi_masking         = s->c.avctx->lumi_masking / (128.0 * 128.0);
    const float dark_masking         = s->c.avctx->dark_masking / (128.0 * 128.0);
    const float temp_cplx_masking    = s->c.avctx->temporal_cplx_masking;
    const float spatial_cplx_masking = s->c.avctx->spatial_cplx_masking;
    const float p_masking            = s->c.avctx->p_masking;
    const float border_masking       = m->border_masking;
    float bits_sum                   = 0.0;
    float cplx_sum                   = 0.0;
    float *cplx_tab                  = rcc->cplx_tab;
    float *bits_tab                  = rcc->bits_tab;
    const int qmin                   = s->c.avctx->mb_lmin;
    const int qmax                   = s->c.avctx->mb_lmax;
    const int mb_width               = s->c.mb_width;
    const int mb_height              = s->c.mb_height;

    for (int i = 0; i < s->c.mb_num; i++) {
        const int mb_xy = s->c.mb_index2xy[i];
        float temp_cplx = sqrt(s->mc_mb_var[mb_xy]); // FIXME merge in pow()
        float spat_cplx = sqrt(s->mb_var[mb_xy]);
        const int lumi  = s->mb_mean[mb_xy];
        float bits, cplx, factor;
        int mb_x = mb_xy % s->c.mb_stride;
        int mb_y = mb_xy / s->c.mb_stride;
        int mb_distance;
        float mb_factor = 0.0;
        if (spat_cplx < 4)
            spat_cplx = 4;              // FIXME fine-tune
        if (temp_cplx < 4)
            temp_cplx = 4;              // FIXME fine-tune

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
    if (s->mpv_flags & FF_MPV_FLAG_NAQ) {
        float factor = bits_sum / cplx_sum;
        for (int i = 0; i < s->c.mb_num; i++) {
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

    for (int i = 0; i < s->c.mb_num; i++) {
        const int mb_xy = s->c.mb_index2xy[i];
        float newq      = q * cplx_tab[i] / bits_tab[i];
        int intq;

        if (s->mpv_flags & FF_MPV_FLAG_NAQ) {
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

void ff_get_2pass_fcode(MPVMainEncContext *const m)
{
    MPVEncContext *const s = &m->s;
    const RateControlContext *rcc = &m->rc_context;
    const RateControlEntry   *rce = &rcc->entry[s->c.picture_number];

    s->f_code = rce->f_code;
    s->b_code = rce->b_code;
}

// FIXME rd or at least approx for dquant

float ff_rate_estimate_qscale(MPVMainEncContext *const m, int dry_run)
{
    MPVEncContext  *const s = &m->s;
    RateControlContext *rcc = &m->rc_context;
    AVCodecContext *const a = s->c.avctx;
    float q;
    int qmin, qmax;
    float br_compensation;
    double diff;
    double short_term_q;
    double fps;
    int picture_number = s->c.picture_number;
    int64_t wanted_bits;
    RateControlEntry local_rce, *rce;
    double bits;
    double rate_factor;
    int64_t var;
    const int pict_type = s->c.pict_type;
    emms_c();

    get_qminmax(&qmin, &qmax, m, pict_type);

    fps = get_fps(s->c.avctx);
    /* update predictors */
    if (picture_number > 2 && !dry_run) {
        const int64_t last_var =
            m->last_pict_type == AV_PICTURE_TYPE_I ? rcc->last_mb_var_sum
                                                   : rcc->last_mc_mb_var_sum;
        av_assert1(m->frame_bits >= m->stuffing_bits);
        update_predictor(&rcc->pred[m->last_pict_type],
                         rcc->last_qscale,
                         sqrt(last_var),
                         m->frame_bits - m->stuffing_bits);
    }

    if (s->c.avctx->flags & AV_CODEC_FLAG_PASS2) {
        av_assert0(picture_number >= 0);
        if (picture_number >= rcc->num_entries) {
            av_log(s->c.avctx, AV_LOG_ERROR, "Input is longer than 2-pass log file\n");
            return -1;
        }
        rce         = &rcc->entry[picture_number];
        wanted_bits = rce->expected_bits;
    } else {
        const MPVPicture *dts_pic;
        double wanted_bits_double;
        rce = &local_rce;

        /* FIXME add a dts field to AVFrame and ensure it is set and use it
         * here instead of reordering but the reordering is simpler for now
         * until H.264 B-pyramid must be handled. */
        if (s->c.pict_type == AV_PICTURE_TYPE_B || s->c.low_delay)
            dts_pic = s->c.cur_pic.ptr;
        else
            dts_pic = s->c.last_pic.ptr;

        if (!dts_pic || dts_pic->f->pts == AV_NOPTS_VALUE)
            wanted_bits_double = m->bit_rate * (double)picture_number / fps;
        else
            wanted_bits_double = m->bit_rate * (double)dts_pic->f->pts / fps;
        if (wanted_bits_double > INT64_MAX) {
            av_log(s->c.avctx, AV_LOG_WARNING, "Bits exceed 64bit range\n");
            wanted_bits = INT64_MAX;
        } else
            wanted_bits = (int64_t)wanted_bits_double;
    }

    diff = m->total_bits - wanted_bits;
    br_compensation = (a->bit_rate_tolerance - diff) / a->bit_rate_tolerance;
    if (br_compensation <= 0.0)
        br_compensation = 0.001;

    var = pict_type == AV_PICTURE_TYPE_I ? m->mb_var_sum : m->mc_mb_var_sum;

    short_term_q = 0; /* avoid warning */
    if (s->c.avctx->flags & AV_CODEC_FLAG_PASS2) {
        if (pict_type != AV_PICTURE_TYPE_I)
            av_assert0(pict_type == rce->new_pict_type);

        q = rce->new_qscale / br_compensation;
        ff_dlog(s->c.avctx, "%f %f %f last:%d var:%"PRId64" type:%d//\n", q, rce->new_qscale,
                br_compensation, m->frame_bits, var, pict_type);
    } else {
        rce->pict_type     =
        rce->new_pict_type = pict_type;
        rce->mc_mb_var_sum = m->mc_mb_var_sum;
        rce->mb_var_sum    = m->mb_var_sum;
        rce->qscale        = FF_QP2LAMBDA * 2;
        rce->f_code        = s->f_code;
        rce->b_code        = s->b_code;
        rce->misc_bits     = 1;

        bits = predict_size(&rcc->pred[pict_type], rce->qscale, sqrt(var));
        if (pict_type == AV_PICTURE_TYPE_I) {
            rce->i_count    = s->c.mb_num;
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

        rate_factor = rcc->pass1_wanted_bits /
                      rcc->pass1_rc_eq_output_sum * br_compensation;

        q = get_qscale(m, rce, rate_factor, picture_number);
        if (q < 0)
            return -1;

        av_assert0(q > 0.0);
        q = get_diff_limited_q(m, rce, q);
        av_assert0(q > 0.0);

        // FIXME type dependent blur like in 2-pass
        if (pict_type == AV_PICTURE_TYPE_P || m->intra_only) {
            rcc->short_term_qsum   *= a->qblur;
            rcc->short_term_qcount *= a->qblur;

            rcc->short_term_qsum += q;
            rcc->short_term_qcount++;
            q = short_term_q = rcc->short_term_qsum / rcc->short_term_qcount;
        }
        av_assert0(q > 0.0);

        q = modify_qscale(m, rce, q, picture_number);

        rcc->pass1_wanted_bits += m->bit_rate / fps;

        av_assert0(q > 0.0);
    }

    if (s->c.avctx->debug & FF_DEBUG_RC) {
        av_log(s->c.avctx, AV_LOG_DEBUG,
               "%c qp:%d<%2.1f<%d %d want:%"PRId64" total:%"PRId64" comp:%f st_q:%2.2f "
               "size:%d var:%"PRId64"/%"PRId64" br:%"PRId64" fps:%d\n",
               av_get_picture_type_char(pict_type),
               qmin, q, qmax, picture_number,
               wanted_bits / 1000, m->total_bits / 1000,
               br_compensation, short_term_q, m->frame_bits,
               m->mb_var_sum, m->mc_mb_var_sum,
               m->bit_rate / 1000, (int)fps);
    }

    if (q < qmin)
        q = qmin;
    else if (q > qmax)
        q = qmax;

    if (s->adaptive_quant)
        adaptive_quantization(rcc, m, q);
    else
        q = (int)(q + 0.5);

    if (!dry_run) {
        rcc->last_qscale        = q;
        rcc->last_mc_mb_var_sum = m->mc_mb_var_sum;
        rcc->last_mb_var_sum    = m->mb_var_sum;
    }
    return q;
}
