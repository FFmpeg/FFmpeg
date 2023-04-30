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

#undef ZERO
#undef ONE
#undef ftype
#undef SAMPLE_FORMAT
#if DEPTH == 32
#define SAMPLE_FORMAT float
#define ftype float
#define ONE 1.f
#define ZERO 0.f
#else
#define SAMPLE_FORMAT double
#define ftype double
#define ONE 1.0
#define ZERO 0.0
#endif

#define fn3(a,b)   a##_##b
#define fn2(a,b)   fn3(a,b)
#define fn(a)      fn2(a, SAMPLE_FORMAT)

#if DEPTH == 64
static double scalarproduct_double(const double *v1, const double *v2, int len)
{
    double p = 0.0;

    for (int i = 0; i < len; i++)
        p += v1[i] * v2[i];

    return p;
}
#endif

static ftype fn(fir_sample)(AudioAPContext *s, ftype sample, ftype *delay,
                            ftype *coeffs, ftype *tmp, int *offset)
{
    const int order = s->order;
    ftype output;

    delay[*offset] = sample;

    memcpy(tmp, coeffs + order - *offset, order * sizeof(ftype));
#if DEPTH == 32
    output = s->fdsp->scalarproduct_float(delay, tmp, s->kernel_size);
#else
    output = scalarproduct_double(delay, tmp, s->kernel_size);
#endif

    if (--(*offset) < 0)
        *offset = order - 1;

    return output;
}

static int fn(lup_decompose)(ftype **MA, const int N, const ftype tol, int *P)
{
    for (int i = 0; i <= N; i++)
        P[i] = i;

    for (int i = 0; i < N; i++) {
        ftype maxA = ZERO;
        int imax = i;

        for (int k = i; k < N; k++) {
            ftype absA = fabs(MA[k][i]);
            if (absA > maxA) {
                maxA = absA;
                imax = k;
            }
        }

        if (maxA < tol)
            return 0;

        if (imax != i) {
            FFSWAP(int, P[i], P[imax]);
            FFSWAP(ftype *, MA[i], MA[imax]);
            P[N]++;
        }

        for (int j = i + 1; j < N; j++) {
            MA[j][i] /= MA[i][i];

            for (int k = i + 1; k < N; k++)
                MA[j][k] -= MA[j][i] * MA[i][k];
        }
    }

    return 1;
}

static void fn(lup_invert)(ftype *const *MA, const int *P, const int N, ftype **IA)
{
    for (int j = 0; j < N; j++) {
        for (int i = 0; i < N; i++) {
            IA[i][j] = P[i] == j ? ONE : ZERO;

            for (int k = 0; k < i; k++)
                IA[i][j] -= MA[i][k] * IA[k][j];
        }

        for (int i = N - 1; i >= 0; i--) {
            for (int k = i + 1; k < N; k++)
                IA[i][j] -= MA[i][k] * IA[k][j];

            IA[i][j] /= MA[i][i];
        }
    }
}

static ftype fn(process_sample)(AudioAPContext *s, ftype input, ftype desired, int ch)
{
    ftype *dcoeffs = (ftype *)s->dcoeffs->extended_data[ch];
    ftype *coeffs = (ftype *)s->coeffs->extended_data[ch];
    ftype *delay = (ftype *)s->delay->extended_data[ch];
    ftype **itmpmp = (ftype **)&s->itmpmp[s->projection * ch];
    ftype **tmpmp = (ftype **)&s->tmpmp[s->projection * ch];
    ftype *tmpm = (ftype *)s->tmpm->extended_data[ch];
    ftype *tmp = (ftype *)s->tmp->extended_data[ch];
    ftype *e = (ftype *)s->e->extended_data[ch];
    ftype *x = (ftype *)s->x->extended_data[ch];
    ftype *w = (ftype *)s->w->extended_data[ch];
    int *p = (int *)s->p->extended_data[ch];
    int *offset = (int *)s->offset->extended_data[ch];
    const int projection = s->projection;
    const ftype delta = s->delta;
    const int order = s->order;
    const int length = projection + order;
    const ftype mu = s->mu;
    const ftype tol = 0.00001f;
    ftype output;

    x[offset[2] + length] = x[offset[2]] = input;
    delay[offset[0] + order] = input;

    output = fn(fir_sample)(s, input, delay, coeffs, tmp, offset);
    e[offset[1]] = e[offset[1] + projection] = desired - output;

    for (int i = 0; i < projection; i++) {
        const int iprojection = i * projection;

        for (int j = i; j < projection; j++) {
            ftype sum = ZERO;
            for (int k = 0; k < order; k++)
                sum += x[offset[2] + i + k] * x[offset[2] + j + k];
            tmpm[iprojection + j] = sum;
            if (i != j)
                tmpm[j * projection + i] = sum;
        }

        tmpm[iprojection + i] += delta;
    }

    fn(lup_decompose)(tmpmp, projection, tol, p);
    fn(lup_invert)(tmpmp, p, projection, itmpmp);

    for (int i = 0; i < projection; i++) {
        ftype sum = ZERO;
        for (int j = 0; j < projection; j++)
            sum += itmpmp[i][j] * e[j + offset[1]];
        w[i] = sum;
    }

    for (int i = 0; i < order; i++) {
        ftype sum = ZERO;
        for (int j = 0; j < projection; j++)
            sum += x[offset[2] + i + j] * w[j];
        dcoeffs[i] = sum;
    }

    for (int i = 0; i < order; i++)
        coeffs[i] = coeffs[i + order] = coeffs[i] + mu * dcoeffs[i];

    if (--offset[1] < 0)
        offset[1] = projection - 1;

    if (--offset[2] < 0)
        offset[2] = length - 1;

    switch (s->output_mode) {
    case IN_MODE:      output = input;            break;
    case DESIRED_MODE: output = desired;          break;
    case OUT_MODE:     output = desired - output; break;
    case NOISE_MODE:   output = input - output;   break;
    case ERROR_MODE:                              break;
    }
    return output;
}

static int fn(filter_channels)(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    AudioAPContext *s = ctx->priv;
    AVFrame *out = arg;
    const int start = (out->ch_layout.nb_channels * jobnr) / nb_jobs;
    const int end = (out->ch_layout.nb_channels * (jobnr+1)) / nb_jobs;

    for (int c = start; c < end; c++) {
        const ftype *input = (const ftype *)s->frame[0]->extended_data[c];
        const ftype *desired = (const ftype *)s->frame[1]->extended_data[c];
        ftype *output = (ftype *)out->extended_data[c];

        for (int n = 0; n < out->nb_samples; n++) {
            output[n] = fn(process_sample)(s, input[n], desired[n], c);
            if (ctx->is_disabled)
                output[n] = input[n];
        }
    }

    return 0;
}
