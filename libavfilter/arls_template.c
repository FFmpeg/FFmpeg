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
#undef HALF
#undef ONE
#undef ftype
#undef SAMPLE_FORMAT
#if DEPTH == 32
#define SAMPLE_FORMAT float
#define ftype float
#define ONE 1.f
#define HALF 0.5f
#define ZERO 0.f
#else
#define SAMPLE_FORMAT double
#define ftype double
#define ONE 1.0
#define HALF 0.5
#define ZERO 0.0
#endif

#define fn3(a,b)   a##_##b
#define fn2(a,b)   fn3(a,b)
#define fn(a)      fn2(a, SAMPLE_FORMAT)

static ftype fn(fir_sample)(AudioRLSContext *s, ftype sample, ftype *delay,
                            ftype *coeffs, ftype *tmp, int *offset)
{
    const int order = s->order;
    ftype output;

    delay[*offset] = sample;

    memcpy(tmp, coeffs + order - *offset, order * sizeof(ftype));

#if DEPTH == 32
    output = s->fdsp->scalarproduct_float(delay, tmp, s->kernel_size);
#else
    output = s->fdsp->scalarproduct_double(delay, tmp, s->kernel_size);
#endif

    if (--(*offset) < 0)
        *offset = order - 1;

    return output;
}

static ftype fn(process_sample)(AudioRLSContext *s, ftype input, ftype desired, int ch)
{
    ftype *coeffs = (ftype *)s->coeffs->extended_data[ch];
    ftype *delay = (ftype *)s->delay->extended_data[ch];
    ftype *gains = (ftype *)s->gains->extended_data[ch];
    ftype *tmp = (ftype *)s->tmp->extended_data[ch];
    ftype *u = (ftype *)s->u->extended_data[ch];
    ftype *p = (ftype *)s->p->extended_data[ch];
    ftype *dp = (ftype *)s->dp->extended_data[ch];
    int *offsetp = (int *)s->offset->extended_data[ch];
    const int kernel_size = s->kernel_size;
    const int order = s->order;
    const ftype lambda = s->lambda;
    int offset = *offsetp;
    ftype g = lambda;
    ftype output, e;

    delay[offset + order] = input;

    output = fn(fir_sample)(s, input, delay, coeffs, tmp, offsetp);
    e = desired - output;

    for (int i = 0, pos = offset; i < order; i++, pos++) {
        const int ikernel_size = i * kernel_size;

        u[i] = ZERO;
        for (int k = 0, pos = offset; k < order; k++, pos++)
            u[i] += p[ikernel_size + k] * delay[pos];

        g += u[i] * delay[pos];
    }

    g = ONE / g;

    for (int i = 0; i < order; i++) {
        const int ikernel_size = i * kernel_size;

        gains[i] = u[i] * g;
        coeffs[i] = coeffs[order + i] = coeffs[i] + gains[i] * e;
        tmp[i] = ZERO;
        for (int k = 0, pos = offset; k < order; k++, pos++)
            tmp[i] += p[ikernel_size + k] * delay[pos];
    }

    for (int i = 0; i < order; i++) {
        const int ikernel_size = i * kernel_size;

        for (int k = 0; k < order; k++)
            dp[ikernel_size + k] = gains[i] * tmp[k];
    }

    for (int i = 0; i < order; i++) {
        const int ikernel_size = i * kernel_size;

        for (int k = 0; k < order; k++)
            p[ikernel_size + k] = (p[ikernel_size + k] - (dp[ikernel_size + k] + dp[kernel_size * k + i]) * HALF) * lambda;
    }

    switch (s->output_mode) {
    case IN_MODE:       output = input;         break;
    case DESIRED_MODE:  output = desired;       break;
    case OUT_MODE:   output = desired - output; break;
    case NOISE_MODE: output = input - output;   break;
    case ERROR_MODE:                            break;
    }
    return output;
}

static int fn(filter_channels)(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    AudioRLSContext *s = ctx->priv;
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
