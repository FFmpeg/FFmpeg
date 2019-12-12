/*
 * DCA ADPCM engine
 * Copyright (C) 2017 Daniil Cherednik
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


#include "dcaadpcm.h"
#include "dcaenc.h"
#include "dca_core.h"
#include "mathops.h"

typedef int32_t premultiplied_coeffs[10];

//assume we have DCA_ADPCM_COEFFS values before x
static inline int64_t calc_corr(const int32_t *x, int len, int j, int k)
{
    int n;
    int64_t s = 0;
    for (n = 0; n < len; n++)
        s += MUL64(x[n-j], x[n-k]);
    return s;
}

static inline int64_t apply_filter(const int16_t a[DCA_ADPCM_COEFFS], const int64_t corr[15], const int32_t aa[10])
{
    int64_t err = 0;
    int64_t tmp = 0;

    err = corr[0];

    tmp += MUL64(a[0], corr[1]);
    tmp += MUL64(a[1], corr[2]);
    tmp += MUL64(a[2], corr[3]);
    tmp += MUL64(a[3], corr[4]);

    tmp = norm__(tmp, 13);
    tmp += tmp;

    err -= tmp;
    tmp = 0;

    tmp += MUL64(corr[5], aa[0]);
    tmp += MUL64(corr[6], aa[1]);
    tmp += MUL64(corr[7], aa[2]);
    tmp += MUL64(corr[8], aa[3]);

    tmp += MUL64(corr[9], aa[4]);
    tmp += MUL64(corr[10], aa[5]);
    tmp += MUL64(corr[11], aa[6]);

    tmp += MUL64(corr[12], aa[7]);
    tmp += MUL64(corr[13], aa[8]);

    tmp += MUL64(corr[14], aa[9]);

    tmp = norm__(tmp, 26);

    err += tmp;

    return llabs(err);
}

static int64_t find_best_filter(const DCAADPCMEncContext *s, const int32_t *in, int len)
{
    const premultiplied_coeffs *precalc_data = s->private_data;
    int i, j, k = 0;
    int vq = -1;
    int64_t err;
    int64_t min_err = 1ll << 62;
    int64_t corr[15];

    for (i = 0; i <= DCA_ADPCM_COEFFS; i++)
        for (j = i; j <= DCA_ADPCM_COEFFS; j++)
            corr[k++] = calc_corr(in+4, len, i, j);

    for (i = 0; i < DCA_ADPCM_VQCODEBOOK_SZ; i++) {
        err = apply_filter(ff_dca_adpcm_vb[i], corr, *precalc_data);
        if (err < min_err) {
            min_err = err;
            vq = i;
        }
        precalc_data++;
    }

    return vq;
}

static inline int64_t calc_prediction_gain(int pred_vq, const int32_t *in, int32_t *out, int len)
{
    int i;
    int32_t error;

    int64_t signal_energy = 0;
    int64_t error_energy = 0;

    for (i = 0; i < len; i++) {
        error = in[DCA_ADPCM_COEFFS + i] - ff_dcaadpcm_predict(pred_vq, in + i);
        out[i] = error;
        signal_energy += MUL64(in[DCA_ADPCM_COEFFS + i], in[DCA_ADPCM_COEFFS + i]);
        error_energy += MUL64(error, error);
    }

    if (!error_energy)
        return -1;

    return signal_energy / error_energy;
}

int ff_dcaadpcm_subband_analysis(const DCAADPCMEncContext *s, const int32_t *in, int len, int *diff)
{
    int pred_vq, i;
    int32_t input_buffer[16 + DCA_ADPCM_COEFFS];
    int32_t input_buffer2[16 + DCA_ADPCM_COEFFS];

    int32_t max = 0;
    int shift_bits;
    uint64_t pg = 0;

    for (i = 0; i < len + DCA_ADPCM_COEFFS; i++)
        max |= FFABS(in[i]);

    // normalize input to simplify apply_filter
    shift_bits = av_log2(max) - 11;

    for (i = 0; i < len + DCA_ADPCM_COEFFS; i++) {
        input_buffer[i] = norm__(in[i], 7);
        input_buffer2[i] = norm__(in[i], shift_bits);
    }

    pred_vq = find_best_filter(s, input_buffer2, len);

    if (pred_vq < 0)
        return -1;

    pg = calc_prediction_gain(pred_vq, input_buffer, diff, len);

    // Greater than 10db (10*log(10)) prediction gain to use ADPCM.
    // TODO: Tune it.
    if (pg < 10)
        return -1;

    for (i = 0; i < len; i++)
        diff[i] <<= 7;

    return pred_vq;
}

static void precalc(premultiplied_coeffs *data)
{
    int i, j, k;

    for (i = 0; i < DCA_ADPCM_VQCODEBOOK_SZ; i++) {
        int id = 0;
        int32_t t = 0;
        for (j = 0; j < DCA_ADPCM_COEFFS; j++) {
            for (k = j; k < DCA_ADPCM_COEFFS; k++) {
                t = (int32_t)ff_dca_adpcm_vb[i][j] * (int32_t)ff_dca_adpcm_vb[i][k];
                if (j != k)
                    t *= 2;
                (*data)[id++] = t;
             }
        }
        data++;
    }
}

int ff_dcaadpcm_do_real(int pred_vq_index,
                        softfloat quant, int32_t scale_factor, int32_t step_size,
                        const int32_t *prev_hist, const int32_t *in, int32_t *next_hist, int32_t *out,
                        int len, int32_t peak)
{
    int i;
    int64_t delta;
    int32_t dequant_delta;
    int32_t work_bufer[16 + DCA_ADPCM_COEFFS];

    memcpy(work_bufer, prev_hist, sizeof(int32_t) * DCA_ADPCM_COEFFS);

    for (i = 0; i < len; i++) {
        work_bufer[DCA_ADPCM_COEFFS + i] = ff_dcaadpcm_predict(pred_vq_index, &work_bufer[i]);

        delta = (int64_t)in[i] - ((int64_t)work_bufer[DCA_ADPCM_COEFFS + i] << 7);

        out[i] = quantize_value(av_clip64(delta, -peak, peak), quant);

        ff_dca_core_dequantize(&dequant_delta, &out[i], step_size, scale_factor, 0, 1);

        work_bufer[DCA_ADPCM_COEFFS+i] += dequant_delta;
    }

    memcpy(next_hist, &work_bufer[len], sizeof(int32_t) * DCA_ADPCM_COEFFS);

    return 0;
}

av_cold int ff_dcaadpcm_init(DCAADPCMEncContext *s)
{
    if (!s)
        return -1;

    s->private_data = av_malloc(sizeof(premultiplied_coeffs) * DCA_ADPCM_VQCODEBOOK_SZ);
    if (!s->private_data)
        return AVERROR(ENOMEM);

    precalc(s->private_data);
    return 0;
}

av_cold void ff_dcaadpcm_free(DCAADPCMEncContext *s)
{
    if (!s)
        return;

    av_freep(&s->private_data);
}
