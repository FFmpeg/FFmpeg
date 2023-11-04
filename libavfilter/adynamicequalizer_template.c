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
#undef EPSILON
#undef FABS
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
#define ftype float
#define EPSILON (1.f / (1 << 22))
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
#define ftype double
#define EPSILON (1.0 / (1LL << 51))
#endif

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

    s->attack_coef = get_coef(s->attack, sample_rate);
    s->release_coef = get_coef(s->release, sample_rate);

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

static int fn(filter_channels)(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    AudioDynamicEqualizerContext *s = ctx->priv;
    ThreadData *td = arg;
    AVFrame *in = td->in;
    AVFrame *out = td->out;
    const ftype sample_rate = in->sample_rate;
    const ftype makeup = s->makeup;
    const ftype ratio = s->ratio;
    const ftype range = s->range;
    const ftype tfrequency = FMIN(s->tfrequency, sample_rate * 0.5);
    const ftype release = s->release_coef;
    const ftype attack = s->attack_coef;
    const ftype tqfactor = s->tqfactor;
    const ftype itqfactor = ONE / tqfactor;
    const ftype fg = TAN(M_PI * tfrequency / sample_rate);
    const int start = (in->ch_layout.nb_channels * jobnr) / nb_jobs;
    const int end = (in->ch_layout.nb_channels * (jobnr+1)) / nb_jobs;
    const int detection = s->detection;
    const int tftype = s->tftype;
    const int mode = s->mode;
    const ftype *da = fn(s->da);
    const ftype *dm = fn(s->dm);

    for (int ch = start; ch < end; ch++) {
        const ftype *src = (const ftype *)in->extended_data[ch];
        ftype *dst = (ftype *)out->extended_data[ch];
        ChannelContext *cc = &s->cc[ch];
        const ftype threshold = detection == 0 ? fn(cc->threshold) : s->threshold;
        ftype *fa = fn(cc->fa), *fm = fn(cc->fm);
        ftype *fstate = fn(cc->fstate);
        ftype *dstate = fn(cc->dstate);
        ftype gain = fn(cc->gain);

        if (detection < 0)
            fn(cc->threshold) = threshold;

        for (int n = 0; n < out->nb_samples; n++) {
            ftype detect, v, listen, new_gain = ONE;
            ftype k, g;

            detect = listen = fn(get_svf)(src[n], dm, da, dstate);
            detect = FABS(detect);

            if (detection > 0)
                fn(cc->threshold) = FMAX(fn(cc->threshold), detect);

            switch (mode) {
            case LISTEN:
                break;
            case CUT_BELOW:
                if (detect < threshold)
                    new_gain = ONE / CLIP(ONE + makeup + (threshold - detect) * ratio, ONE, range);
                break;
            case CUT_ABOVE:
                if (detect > threshold)
                    new_gain = ONE / CLIP(ONE + makeup + (detect - threshold) * ratio, ONE, range);
                break;
            case BOOST_BELOW:
                if (detect < threshold)
                    new_gain = CLIP(ONE + makeup + (threshold - detect) * ratio, ONE, range);
                break;
            case BOOST_ABOVE:
                if (detect > threshold)
                    new_gain = CLIP(ONE + makeup + (detect - threshold) * ratio, ONE, range);
                break;
            }

            if (mode > LISTEN) {
                ftype delta = new_gain - gain;

                if (delta > EPSILON)
                    new_gain = gain + attack * delta;
                else if (delta < -EPSILON)
                    new_gain = gain + release * delta;
            }

            if (gain != new_gain) {
                gain = new_gain;

                switch (tftype) {
                case 0:
                    k = itqfactor / gain;

                    fa[0] = ONE / (ONE + fg * (fg + k));
                    fa[1] = fg * fa[0];
                    fa[2] = fg * fa[1];

                    fm[0] = ONE;
                    fm[1] = k * (gain * gain - ONE);
                    fm[2] = ZERO;
                    break;
                case 1:
                    k = itqfactor;
                    g = fg / SQRT(gain);

                    fa[0] = ONE / (ONE + g * (g + k));
                    fa[1] = g * fa[0];
                    fa[2] = g * fa[1];

                    fm[0] = ONE;
                    fm[1] = k * (gain - ONE);
                    fm[2] = gain * gain - ONE;
                    break;
                case 2:
                    k = itqfactor;
                    g = fg * SQRT(gain);

                    fa[0] = ONE / (ONE + g * (g + k));
                    fa[1] = g * fa[0];
                    fa[2] = g * fa[1];

                    fm[0] = gain * gain;
                    fm[1] = k * (ONE - gain) * gain;
                    fm[2] = ONE - gain * gain;
                    break;
                }
            }

            v = fn(get_svf)(src[n], fm, fa, fstate);
            v = mode == -1 ? listen : v;
            dst[n] = ctx->is_disabled ? src[n] : v;
        }

        fn(cc->gain) = gain;
    }

    return 0;
}
