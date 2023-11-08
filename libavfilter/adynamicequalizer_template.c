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

#undef ftype
#undef SQRT
#undef TAN
#undef ONE
#undef TWO
#undef ZERO
#undef FMAX
#undef FMIN
#undef CLIP
#undef SAMPLE_FORMAT
#undef FABS
#undef FLOG
#undef FEXP
#undef FLOG2
#undef FLOG10
#undef FEXP2
#undef FEXP10
#undef EPSILON
#if DEPTH == 32
#define SAMPLE_FORMAT float
#define SQRT sqrtf
#define TAN tanf
#define ONE 1.f
#define TWO 2.f
#define ZERO 0.f
#define FMIN fminf
#define FMAX fmaxf
#define CLIP av_clipf
#define FABS fabsf
#define FLOG logf
#define FEXP expf
#define FLOG2 log2f
#define FLOG10 log10f
#define FEXP2 exp2f
#define FEXP10 ff_exp10f
#define EPSILON (1.f / (1 << 23))
#define ftype float
#else
#define SAMPLE_FORMAT double
#define SQRT sqrt
#define TAN tan
#define ONE 1.0
#define TWO 2.0
#define ZERO 0.0
#define FMIN fmin
#define FMAX fmax
#define CLIP av_clipd
#define FABS fabs
#define FLOG log
#define FEXP exp
#define FLOG2 log2
#define FLOG10 log10
#define FEXP2 exp2
#define FEXP10 ff_exp10
#define EPSILON (1.0 / (1LL << 53))
#define ftype double
#endif

#define LIN2LOG(x) (20.0 * FLOG10(x))
#define LOG2LIN(x) (FEXP10(x / 20.0))

#define fn3(a,b)   a##_##b
#define fn2(a,b)   fn3(a,b)
#define fn(a)      fn2(a, SAMPLE_FORMAT)

static ftype fn(get_svf)(ftype in, const ftype *m, const ftype *a, ftype *b)
{
    const ftype v0 = in;
    const ftype v3 = v0 - b[1];
    const ftype v1 = a[0] * b[0] + a[1] * v3;
    const ftype v2 = b[1] + a[1] * b[0] + a[2] * v3;

    b[0] = TWO * v1 - b[0];
    b[1] = TWO * v2 - b[1];

    return m[0] * v0 + m[1] * v1 + m[2] * v2;
}

static int fn(filter_prepare)(AVFilterContext *ctx)
{
    AudioDynamicEqualizerContext *s = ctx->priv;
    const ftype sample_rate = ctx->inputs[0]->sample_rate;
    const ftype dfrequency = FMIN(s->dfrequency, sample_rate * 0.5);
    const ftype dg = TAN(M_PI * dfrequency / sample_rate);
    const ftype dqfactor = s->dqfactor;
    const int dftype = s->dftype;
    ftype *da = fn(s->da);
    ftype *dm = fn(s->dm);
    ftype k;

    s->threshold_log = LIN2LOG(s->threshold);
    s->dattack_coef = get_coef(s->dattack, sample_rate);
    s->drelease_coef = get_coef(s->drelease, sample_rate);
    s->gattack_coef = s->dattack_coef * 0.25;
    s->grelease_coef = s->drelease_coef * 0.25;

    switch (dftype) {
    case 0:
        k = ONE / dqfactor;

        da[0] = ONE / (ONE + dg * (dg + k));
        da[1] = dg * da[0];
        da[2] = dg * da[1];

        dm[0] = ZERO;
        dm[1] = k;
        dm[2] = ZERO;
        break;
    case 1:
        k = ONE / dqfactor;

        da[0] = ONE / (ONE + dg * (dg + k));
        da[1] = dg * da[0];
        da[2] = dg * da[1];

        dm[0] = ZERO;
        dm[1] = ZERO;
        dm[2] = ONE;
        break;
    case 2:
        k = ONE / dqfactor;

        da[0] = ONE / (ONE + dg * (dg + k));
        da[1] = dg * da[0];
        da[2] = dg * da[1];

        dm[0] = ZERO;
        dm[1] = -k;
        dm[2] = -ONE;
        break;
    case 3:
        k = ONE / dqfactor;

        da[0] = ONE / (ONE + dg * (dg + k));
        da[1] = dg * da[0];
        da[2] = dg * da[1];

        dm[0] = ONE;
        dm[1] = -k;
        dm[2] = -TWO;
        break;
    }

    return 0;
}

#define PEAKS(empty_value,op,sample, psample)\
    if (!empty && psample == ss[front]) {    \
        ss[front] = empty_value;             \
        if (back != front) {                 \
            front--;                         \
            if (front < 0)                   \
                front = n - 1;               \
        }                                    \
        empty = front == back;               \
    }                                        \
                                             \
    if (!empty && sample op ss[front]) {     \
        while (1) {                          \
            ss[front] = empty_value;         \
            if (back == front) {             \
                empty = 1;                   \
                break;                       \
            }                                \
            front--;                         \
            if (front < 0)                   \
                front = n - 1;               \
        }                                    \
    }                                        \
                                             \
    while (!empty && sample op ss[back]) {   \
        ss[back] = empty_value;              \
        if (back == front) {                 \
            empty = 1;                       \
            break;                           \
        }                                    \
        back++;                              \
        if (back >= n)                       \
            back = 0;                        \
    }                                        \
                                             \
    if (!empty) {                            \
        back--;                              \
        if (back < 0)                        \
            back = n - 1;                    \
    }

static void fn(queue_sample)(ChannelContext *cc,
                             const ftype x,
                             const int nb_samples)
{
    ftype *ss = cc->dqueue;
    ftype *qq = cc->queue;
    int front = cc->front;
    int back = cc->back;
    int empty, n, pos = cc->position;
    ftype px = qq[pos];

    fn(cc->sum) += x;
    fn(cc->log_sum) += FLOG2(x);
    if (cc->size >= nb_samples) {
        fn(cc->sum) -= px;
        fn(cc->log_sum) -= FLOG2(px);
    }

    qq[pos] = x;
    pos++;
    if (pos >= nb_samples)
        pos = 0;
    cc->position = pos;

    if (cc->size < nb_samples)
        cc->size++;
    n = cc->size;

    empty = (front == back) && (ss[front] == ZERO);
    PEAKS(ZERO, >, x, px)

    ss[back] = x;

    cc->front = front;
    cc->back = back;
}

static ftype fn(get_peak)(ChannelContext *cc, ftype *score)
{
    ftype s, *ss = cc->dqueue;
    s = FEXP2(fn(cc->log_sum) / cc->size) / (fn(cc->sum) / cc->size);
    *score = LIN2LOG(s);
    return ss[cc->front];
}

static int fn(filter_channels)(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    AudioDynamicEqualizerContext *s = ctx->priv;
    ThreadData *td = arg;
    AVFrame *in = td->in;
    AVFrame *out = td->out;
    const ftype sample_rate = in->sample_rate;
    const int isample_rate = in->sample_rate;
    const ftype makeup = s->makeup;
    const ftype ratio = s->ratio;
    const ftype range = s->range;
    const ftype tfrequency = FMIN(s->tfrequency, sample_rate * 0.5);
    const int mode = s->mode;
    const ftype power = (mode == CUT_BELOW || mode == CUT_ABOVE) ? -ONE : ONE;
    const ftype grelease = s->grelease_coef;
    const ftype gattack = s->gattack_coef;
    const ftype drelease = s->drelease_coef;
    const ftype dattack = s->dattack_coef;
    const ftype tqfactor = s->tqfactor;
    const ftype itqfactor = ONE / tqfactor;
    const ftype fg = TAN(M_PI * tfrequency / sample_rate);
    const int start = (in->ch_layout.nb_channels * jobnr) / nb_jobs;
    const int end = (in->ch_layout.nb_channels * (jobnr+1)) / nb_jobs;
    const int is_disabled = ctx->is_disabled;
    const int detection = s->detection;
    const int tftype = s->tftype;
    const ftype *da = fn(s->da);
    const ftype *dm = fn(s->dm);

    if (detection == DET_ON) {
        for (int ch = start; ch < end; ch++) {
            const ftype *src = (const ftype *)in->extended_data[ch];
            ChannelContext *cc = &s->cc[ch];
            ftype *tstate = fn(cc->tstate);
            ftype new_threshold = ZERO;

            if (cc->detection != detection) {
                cc->detection = detection;
                fn(cc->new_threshold_log) = LIN2LOG(EPSILON);
            }

            for (int n = 0; n < in->nb_samples; n++) {
                ftype detect = FABS(fn(get_svf)(src[n], dm, da, tstate));
                new_threshold = FMAX(new_threshold, detect);
            }

            fn(cc->new_threshold_log) = FMAX(fn(cc->new_threshold_log), LIN2LOG(new_threshold));
        }
    } else if (detection == DET_ADAPTIVE) {
        for (int ch = start; ch < end; ch++) {
            const ftype *src = (const ftype *)in->extended_data[ch];
            ChannelContext *cc = &s->cc[ch];
            ftype *tstate = fn(cc->tstate);
            ftype score, peak;

            for (int n = 0; n < in->nb_samples; n++) {
                ftype detect = FMAX(FABS(fn(get_svf)(src[n], dm, da, tstate)), EPSILON);
                fn(queue_sample)(cc, detect, isample_rate);
            }

            peak = fn(get_peak)(cc, &score);

            if (score >= -3.5) {
                fn(cc->threshold_log) = LIN2LOG(peak);
            } else if (cc->detection == DET_UNSET) {
                fn(cc->threshold_log) = s->threshold_log;
            }
            cc->detection = detection;
        }
    } else if (detection == DET_DISABLED) {
        for (int ch = start; ch < end; ch++) {
            ChannelContext *cc = &s->cc[ch];
            fn(cc->threshold_log) = s->threshold_log;
            cc->detection = detection;
        }
    } else if (detection == DET_OFF) {
        for (int ch = start; ch < end; ch++) {
            ChannelContext *cc = &s->cc[ch];
            if (cc->detection == DET_ON)
                fn(cc->threshold_log) = fn(cc->new_threshold_log);
            else if (cc->detection == DET_UNSET)
                fn(cc->threshold_log) = s->threshold_log;
            cc->detection = detection;
        }
    }

    for (int ch = start; ch < end; ch++) {
        const ftype *src = (const ftype *)in->extended_data[ch];
        ftype *dst = (ftype *)out->extended_data[ch];
        ChannelContext *cc = &s->cc[ch];
        const ftype threshold_log = fn(cc->threshold_log);
        ftype *fa = fn(cc->fa), *fm = fn(cc->fm);
        ftype *fstate = fn(cc->fstate);
        ftype *dstate = fn(cc->dstate);
        ftype detect = fn(cc->detect);
        ftype lin_gain = fn(cc->lin_gain);
        int init = cc->init;

        for (int n = 0; n < out->nb_samples; n++) {
            ftype new_detect, new_lin_gain = ONE;
            ftype f, v, listen, k, g, ld;

            listen = fn(get_svf)(src[n], dm, da, dstate);
            if (mode > LISTEN) {
                new_detect = FABS(listen);
                f = (new_detect > detect) * dattack + (new_detect <= detect) * drelease;
                detect = f * new_detect + (ONE - f) * detect;
            }

            switch (mode) {
            case LISTEN:
                break;
            case CUT_BELOW:
            case BOOST_BELOW:
                ld = LIN2LOG(detect);
                if (ld < threshold_log) {
                    ftype new_log_gain = CLIP(makeup + (threshold_log - ld) * ratio, ZERO, range) * power;
                    new_lin_gain = LOG2LIN(new_log_gain);
                }
                break;
            case CUT_ABOVE:
            case BOOST_ABOVE:
                ld = LIN2LOG(detect);
                if (ld > threshold_log) {
                    ftype new_log_gain = CLIP(makeup + (ld - threshold_log) * ratio, ZERO, range) * power;
                    new_lin_gain = LOG2LIN(new_log_gain);
                }
                break;
            }

            f = (new_lin_gain > lin_gain) * gattack + (new_lin_gain <= lin_gain) * grelease;
            new_lin_gain = f * new_lin_gain + (ONE - f) * lin_gain;

            if (lin_gain != new_lin_gain || !init) {
                init = 1;
                lin_gain = new_lin_gain;

                switch (tftype) {
                case 0:
                    k = itqfactor / lin_gain;

                    fa[0] = ONE / (ONE + fg * (fg + k));
                    fa[1] = fg * fa[0];
                    fa[2] = fg * fa[1];

                    fm[0] = ONE;
                    fm[1] = k * (lin_gain * lin_gain - ONE);
                    fm[2] = ZERO;
                    break;
                case 1:
                    k = itqfactor;
                    g = fg / SQRT(lin_gain);

                    fa[0] = ONE / (ONE + g * (g + k));
                    fa[1] = g * fa[0];
                    fa[2] = g * fa[1];

                    fm[0] = ONE;
                    fm[1] = k * (lin_gain - ONE);
                    fm[2] = lin_gain * lin_gain - ONE;
                    break;
                case 2:
                    k = itqfactor;
                    g = fg * SQRT(lin_gain);

                    fa[0] = ONE / (ONE + g * (g + k));
                    fa[1] = g * fa[0];
                    fa[2] = g * fa[1];

                    fm[0] = lin_gain * lin_gain;
                    fm[1] = k * (ONE - lin_gain) * lin_gain;
                    fm[2] = ONE - lin_gain * lin_gain;
                    break;
                }
            }

            v = fn(get_svf)(src[n], fm, fa, fstate);
            v = mode == LISTEN ? listen : v;
            dst[n] = is_disabled ? src[n] : v;
        }

        fn(cc->detect) = detect;
        fn(cc->lin_gain) = lin_gain;
        cc->init = 1;
    }

    return 0;
}
