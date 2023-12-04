/*
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

#include "libavutil/tx.h"
#include "avfilter.h"
#include "internal.h"
#include "audio.h"

#undef ctype
#undef ftype
#undef SQRT
#undef HYPOT
#undef SAMPLE_FORMAT
#undef TX_TYPE
#undef ONE
#undef ZERO
#undef HALF
#undef SIN
#undef CLIP
#undef EPSILON
#if DEPTH == 32
#define SAMPLE_FORMAT float
#define SQRT sqrtf
#define HYPOT hypotf
#define ctype AVComplexFloat
#define ftype float
#define TX_TYPE AV_TX_FLOAT_RDFT
#define ONE 1.f
#define ZERO 0.f
#define HALF 0.5f
#define SIN sinf
#define CLIP av_clipf
#define EPSILON FLT_EPSILON
#else
#define SAMPLE_FORMAT double
#define SQRT sqrt
#define HYPOT hypot
#define ctype AVComplexDouble
#define ftype double
#define TX_TYPE AV_TX_DOUBLE_RDFT
#define ONE 1.0
#define ZERO 0.0
#define HALF 0.5
#define SIN sin
#define CLIP av_clipd
#define EPSILON DBL_EPSILON
#endif

#define fn3(a,b)   a##_##b
#define fn2(a,b)   fn3(a,b)
#define fn(a)      fn2(a, SAMPLE_FORMAT)

static int fn(de_tx_init)(AVFilterContext *ctx)
{
    AudioDialogueEnhanceContext *s = ctx->priv;
    ftype scale = ONE, iscale = ONE / (s->fft_size * 1.5f);
    int ret;

    s->window = av_calloc(s->fft_size, sizeof(ftype));
    if (!s->window)
        return AVERROR(ENOMEM);
    fn(s->window) = s->window;
    for (int n = 0; n < s->fft_size; n++)
        fn(s->window)[n] = SIN(M_PI*n/(s->fft_size-1));

    ret = av_tx_init(&s->tx_ctx[0], &s->tx_fn, TX_TYPE, 0, s->fft_size, &scale, 0);
    if (ret < 0)
        return ret;

    ret = av_tx_init(&s->tx_ctx[1], &s->tx_fn, TX_TYPE, 0, s->fft_size, &scale, 0);
    if (ret < 0)
        return ret;

    ret = av_tx_init(&s->itx_ctx, &s->itx_fn, TX_TYPE, 1, s->fft_size, &iscale, 0);
    if (ret < 0)
        return ret;

    return 0;
}

static void fn(apply_window)(AudioDialogueEnhanceContext *s,
                             const ftype *in_frame, ftype *out_frame, const int add_to_out_frame)
{
    const ftype *window = fn(s->window);
    const int fft_size = s->fft_size;

    if (add_to_out_frame) {
        for (int i = 0; i < fft_size; i++)
            out_frame[i] += in_frame[i] * window[i];
    } else {
        for (int i = 0; i < fft_size; i++)
            out_frame[i] = in_frame[i] * window[i];
    }
}

static ftype fn(sqr)(ftype x)
{
    return x * x;
}

static void fn(get_centere)(ctype *left, ctype *right,
                            ctype *center, int N)
{
    for (int i = 0; i < N; i++) {
        const ftype l_re = left[i].re;
        const ftype l_im = left[i].im;
        const ftype r_re = right[i].re;
        const ftype r_im = right[i].im;
        const ftype a = HALF * (ONE - SQRT((fn(sqr)(l_re - r_re) + fn(sqr)(l_im - r_im))/
                                           (fn(sqr)(l_re + r_re) + fn(sqr)(l_im + r_im) + EPSILON)));

        center[i].re = a * (l_re + r_re);
        center[i].im = a * (l_im + r_im);
    }
}

static ftype fn(flux)(ftype *curf, ftype *prevf, int N)
{
    ctype *cur  = (ctype *)curf;
    ctype *prev = (ctype *)prevf;
    ftype sum = ZERO;

    for (int i = 0; i < N; i++) {
        ftype c_re = cur[i].re;
        ftype c_im = cur[i].im;
        ftype p_re = prev[i].re;
        ftype p_im = prev[i].im;

        sum += fn(sqr)(HYPOT(c_re, c_im) - HYPOT(p_re, p_im));
    }

    return sum;
}

static ftype fn(fluxlr)(ftype *lf, ftype *lpf,
                        ftype *rf, ftype *rpf,
                        int N)
{
    ctype *l  = (ctype *)lf;
    ctype *lp = (ctype *)lpf;
    ctype *r  = (ctype *)rf;
    ctype *rp = (ctype *)rpf;
    ftype sum = ZERO;

    for (int i = 0; i < N; i++) {
        ftype c_re = l[i].re - r[i].re;
        ftype c_im = l[i].im - r[i].im;
        ftype p_re = lp[i].re - rp[i].re;
        ftype p_im = lp[i].im - rp[i].im;

        sum += fn(sqr)(HYPOT(c_re, c_im) - HYPOT(p_re, p_im));
    }

    return sum;
}

static ftype fn(calc_vad)(ftype fc, ftype flr, ftype a)
{
    const ftype vad = a * (fc / (fc + flr) - HALF);

    return CLIP(vad, ZERO, ONE);
}

static void fn(get_final)(ftype *c, ftype *l,
                          ftype *r, ftype vad, int N,
                          ftype original, ftype enhance)
{
    ctype *center = (ctype *)c;
    ctype *left   = (ctype *)l;
    ctype *right  = (ctype *)r;

    for (int i = 0; i < N; i++) {
        ftype cP = fn(sqr)(center[i].re) + fn(sqr)(center[i].im);
        ftype lrP = fn(sqr)(left[i].re - right[i].re) + fn(sqr)(left[i].im - right[i].im);
        ftype G = cP / (cP + lrP + EPSILON);
        ftype re, im;

        re = center[i].re * (original + vad * G * enhance);
        im = center[i].im * (original + vad * G * enhance);

        center[i].re = re;
        center[i].im = im;
    }
}

static int fn(de_stereo)(AVFilterContext *ctx, AVFrame *out)
{
    AudioDialogueEnhanceContext *s = ctx->priv;
    ftype *center          = (ftype *)s->center_frame->extended_data[0];
    ftype *center_prev     = (ftype *)s->center_frame->extended_data[1];
    ftype *left_in         = (ftype *)s->in_frame->extended_data[0];
    ftype *right_in        = (ftype *)s->in_frame->extended_data[1];
    ftype *left_out        = (ftype *)s->out_dist_frame->extended_data[0];
    ftype *right_out       = (ftype *)s->out_dist_frame->extended_data[1];
    ftype *left_samples    = (ftype *)s->in->extended_data[0];
    ftype *right_samples   = (ftype *)s->in->extended_data[1];
    ftype *windowed_left   = (ftype *)s->windowed_frame->extended_data[0];
    ftype *windowed_right  = (ftype *)s->windowed_frame->extended_data[1];
    ftype *windowed_oleft  = (ftype *)s->windowed_out->extended_data[0];
    ftype *windowed_oright = (ftype *)s->windowed_out->extended_data[1];
    ftype *windowed_pleft  = (ftype *)s->windowed_prev->extended_data[0];
    ftype *windowed_pright = (ftype *)s->windowed_prev->extended_data[1];
    ftype *left_osamples   = (ftype *)out->extended_data[0];
    ftype *right_osamples  = (ftype *)out->extended_data[1];
    ftype *center_osamples = (ftype *)out->extended_data[2];
    const int overlap = s->overlap;
    const int offset = s->fft_size - overlap;
    const int nb_samples = FFMIN(overlap, s->in->nb_samples);
    ftype vad;

    // shift in/out buffers
    memmove(left_in, &left_in[overlap], offset * sizeof(ftype));
    memmove(right_in, &right_in[overlap], offset * sizeof(ftype));
    memmove(left_out, &left_out[overlap], offset * sizeof(ftype));
    memmove(right_out, &right_out[overlap], offset * sizeof(ftype));

    memcpy(&left_in[offset], left_samples, nb_samples * sizeof(ftype));
    memcpy(&right_in[offset], right_samples, nb_samples * sizeof(ftype));
    memset(&left_out[offset], 0, overlap * sizeof(ftype));
    memset(&right_out[offset], 0, overlap * sizeof(ftype));

    fn(apply_window)(s, left_in,  windowed_left,  0);
    fn(apply_window)(s, right_in, windowed_right, 0);

    s->tx_fn(s->tx_ctx[0], windowed_oleft,  windowed_left,  sizeof(ftype));
    s->tx_fn(s->tx_ctx[1], windowed_oright, windowed_right, sizeof(ftype));

    fn(get_centere)((ctype *)windowed_oleft,
                    (ctype *)windowed_oright,
                    (ctype *)center,
                    s->fft_size / 2 + 1);

    vad = fn(calc_vad)(fn(flux)(center, center_prev, s->fft_size / 2 + 1),
                       fn(fluxlr)(windowed_oleft, windowed_pleft,
                                  windowed_oright, windowed_pright, s->fft_size / 2 + 1), s->voice);
    vad = vad * 0.1 + 0.9 * fn(s->prev_vad);
    fn(s->prev_vad) = vad;

    memcpy(center_prev,     center,          s->fft_size * sizeof(ftype));
    memcpy(windowed_pleft,  windowed_oleft,  s->fft_size * sizeof(ftype));
    memcpy(windowed_pright, windowed_oright, s->fft_size * sizeof(ftype));

    fn(get_final)(center, windowed_oleft, windowed_oright, vad, s->fft_size / 2 + 1,
                  s->original, s->enhance);

    s->itx_fn(s->itx_ctx, windowed_oleft, center, sizeof(ctype));

    fn(apply_window)(s, windowed_oleft, left_out, 1);

    memcpy(left_osamples, left_in, overlap * sizeof(ftype));
    memcpy(right_osamples, right_in, overlap * sizeof(ftype));

    if (ctx->is_disabled)
        memset(center_osamples, 0, overlap * sizeof(ftype));
    else
        memcpy(center_osamples, left_out, overlap * sizeof(ftype));

    return 0;
}
