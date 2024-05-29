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

#undef ONE
#undef ftype
#undef SAMPLE_FORMAT
#if DEPTH == 32
#define SAMPLE_FORMAT float
#define ftype float
#define ONE 1.f
#else
#define SAMPLE_FORMAT double
#define ftype double
#define ONE 1.0
#endif

#define fn3(a,b)   a##_##b
#define fn2(a,b)   fn3(a,b)
#define fn(a)      fn2(a, SAMPLE_FORMAT)

static ftype fn(fir_sample)(AudioNLMSContext *s, ftype sample, ftype *delay,
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

static ftype fn(process_sample)(AudioNLMSContext *s, ftype input, ftype desired,
                                ftype *delay, ftype *coeffs, ftype *tmp, int *offsetp)
{
    const int order = s->order;
    const ftype leakage = s->leakage;
    const ftype mu = s->mu;
    const ftype a = ONE - leakage;
    ftype sum, output, e, norm, b;
    int offset = *offsetp;

    delay[offset + order] = input;

    output = fn(fir_sample)(s, input, delay, coeffs, tmp, offsetp);
    e = desired - output;

#if DEPTH == 32
    sum = s->fdsp->scalarproduct_float(delay, delay, s->kernel_size);
#else
    sum = s->fdsp->scalarproduct_double(delay, delay, s->kernel_size);
#endif
    norm = s->eps + sum;
    b = mu * e / norm;
    if (s->anlmf)
        b *= e * e;

    memcpy(tmp, delay + offset, order * sizeof(ftype));

#if DEPTH == 32
    s->fdsp->vector_fmul_scalar(coeffs, coeffs, a, s->kernel_size);
    s->fdsp->vector_fmac_scalar(coeffs, tmp, b, s->kernel_size);
#else
    s->fdsp->vector_dmul_scalar(coeffs, coeffs, a, s->kernel_size);
    s->fdsp->vector_dmac_scalar(coeffs, tmp, b, s->kernel_size);
#endif

    memcpy(coeffs + order, coeffs, order * sizeof(ftype));

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
    AudioNLMSContext *s = ctx->priv;
    AVFrame *out = arg;
    const int start = (out->ch_layout.nb_channels * jobnr) / nb_jobs;
    const int end = (out->ch_layout.nb_channels * (jobnr+1)) / nb_jobs;

    for (int c = start; c < end; c++) {
        const ftype *input = (const ftype *)s->frame[0]->extended_data[c];
        const ftype *desired = (const ftype *)s->frame[1]->extended_data[c];
        ftype *delay = (ftype *)s->delay->extended_data[c];
        ftype *coeffs = (ftype *)s->coeffs->extended_data[c];
        ftype *tmp = (ftype *)s->tmp->extended_data[c];
        int *offset = (int *)s->offset->extended_data[c];
        ftype *output = (ftype *)out->extended_data[c];

        for (int n = 0; n < out->nb_samples; n++) {
            output[n] = fn(process_sample)(s, input[n], desired[n], delay, coeffs, tmp, offset);
            if (ctx->is_disabled)
                output[n] = input[n];
        }
    }

    return 0;
}
