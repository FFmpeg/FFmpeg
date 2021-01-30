/*
 * Copyright (C) 2016 foo86
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

#include "libavutil/mem.h"
#include "libavutil/mem_internal.h"

#include "dcadsp.h"
#include "dcamath.h"

static void decode_hf_c(int32_t **dst,
                        const int32_t *vq_index,
                        const int8_t hf_vq[1024][32],
                        int32_t scale_factors[32][2],
                        ptrdiff_t sb_start, ptrdiff_t sb_end,
                        ptrdiff_t ofs, ptrdiff_t len)
{
    int i, j;

    for (i = sb_start; i < sb_end; i++) {
        const int8_t *coeff = hf_vq[vq_index[i]];
        int32_t scale = scale_factors[i][0];
        for (j = 0; j < len; j++)
            dst[i][j + ofs] = clip23(coeff[j] * scale + (1 << 3) >> 4);
    }
}

static void decode_joint_c(int32_t **dst, int32_t **src,
                           const int32_t *scale_factors,
                           ptrdiff_t sb_start, ptrdiff_t sb_end,
                           ptrdiff_t ofs, ptrdiff_t len)
{
    int i, j;

    for (i = sb_start; i < sb_end; i++) {
        int32_t scale = scale_factors[i];
        for (j = 0; j < len; j++)
            dst[i][j + ofs] = clip23(mul17(src[i][j + ofs], scale));
    }
}

static void lfe_fir_float_c(float *pcm_samples, int32_t *lfe_samples,
                            const float *filter_coeff, ptrdiff_t npcmblocks,
                            int dec_select)
{
    // Select decimation factor
    int factor = 64 << dec_select;
    int ncoeffs = 8 >> dec_select;
    int nlfesamples = npcmblocks >> (dec_select + 1);
    int i, j, k;

    for (i = 0; i < nlfesamples; i++) {
        // One decimated sample generates 64 or 128 interpolated ones
        for (j = 0; j < factor / 2; j++) {
            float a = 0;
            float b = 0;

            for (k = 0; k < ncoeffs; k++) {
                a += filter_coeff[      j * ncoeffs + k] * lfe_samples[-k];
                b += filter_coeff[255 - j * ncoeffs - k] * lfe_samples[-k];
            }

            pcm_samples[             j] = a;
            pcm_samples[factor / 2 + j] = b;
        }

        lfe_samples++;
        pcm_samples += factor;
    }
}

static void lfe_fir0_float_c(float *pcm_samples, int32_t *lfe_samples,
                             const float *filter_coeff, ptrdiff_t npcmblocks)
{
    lfe_fir_float_c(pcm_samples, lfe_samples, filter_coeff, npcmblocks, 0);
}

static void lfe_fir1_float_c(float *pcm_samples, int32_t *lfe_samples,
                             const float *filter_coeff, ptrdiff_t npcmblocks)
{
    lfe_fir_float_c(pcm_samples, lfe_samples, filter_coeff, npcmblocks, 1);
}

static void lfe_x96_float_c(float *dst, const float *src,
                            float *hist, ptrdiff_t len)
{
    float prev = *hist;
    int i;

    for (i = 0; i < len; i++) {
        float a = 0.25f * src[i] + 0.75f * prev;
        float b = 0.75f * src[i] + 0.25f * prev;
        prev = src[i];
        *dst++ = a;
        *dst++ = b;
    }

    *hist = prev;
}

static void sub_qmf32_float_c(SynthFilterContext *synth,
                              FFTContext *imdct,
                              float *pcm_samples,
                              int32_t **subband_samples_lo,
                              int32_t **subband_samples_hi,
                              float *hist1, int *offset, float *hist2,
                              const float *filter_coeff, ptrdiff_t npcmblocks,
                              float scale)
{
    LOCAL_ALIGNED_32(float, input, [32]);
    int i, j;

    for (j = 0; j < npcmblocks; j++) {
        // Load in one sample from each subband
        for (i = 0; i < 32; i++) {
            if ((i - 1) & 2)
                input[i] = -subband_samples_lo[i][j];
            else
                input[i] =  subband_samples_lo[i][j];
        }

        // One subband sample generates 32 interpolated ones
        synth->synth_filter_float(imdct, hist1, offset,
                                  hist2, filter_coeff,
                                  pcm_samples, input, scale);
        pcm_samples += 32;
    }
}

static void sub_qmf64_float_c(SynthFilterContext *synth,
                              FFTContext *imdct,
                              float *pcm_samples,
                              int32_t **subband_samples_lo,
                              int32_t **subband_samples_hi,
                              float *hist1, int *offset, float *hist2,
                              const float *filter_coeff, ptrdiff_t npcmblocks,
                              float scale)
{
    LOCAL_ALIGNED_32(float, input, [64]);
    int i, j;

    if (!subband_samples_hi)
        memset(&input[32], 0, sizeof(input[0]) * 32);

    for (j = 0; j < npcmblocks; j++) {
        // Load in one sample from each subband
        if (subband_samples_hi) {
            // Full 64 subbands, first 32 are residual coded
            for (i =  0; i < 32; i++) {
                if ((i - 1) & 2)
                    input[i] = -subband_samples_lo[i][j] - subband_samples_hi[i][j];
                else
                    input[i] =  subband_samples_lo[i][j] + subband_samples_hi[i][j];
            }
            for (i = 32; i < 64; i++) {
                if ((i - 1) & 2)
                    input[i] = -subband_samples_hi[i][j];
                else
                    input[i] =  subband_samples_hi[i][j];
            }
        } else {
            // Only first 32 subbands
            for (i =  0; i < 32; i++) {
                if ((i - 1) & 2)
                    input[i] = -subband_samples_lo[i][j];
                else
                    input[i] =  subband_samples_lo[i][j];
            }
        }

        // One subband sample generates 64 interpolated ones
        synth->synth_filter_float_64(imdct, hist1, offset,
                                     hist2, filter_coeff,
                                     pcm_samples, input, scale);
        pcm_samples += 64;
    }
}

static void lfe_fir_fixed_c(int32_t *pcm_samples, int32_t *lfe_samples,
                            const int32_t *filter_coeff, ptrdiff_t npcmblocks)
{
    // Select decimation factor
    int nlfesamples = npcmblocks >> 1;
    int i, j, k;

    for (i = 0; i < nlfesamples; i++) {
        // One decimated sample generates 64 interpolated ones
        for (j = 0; j < 32; j++) {
            int64_t a = 0;
            int64_t b = 0;

            for (k = 0; k < 8; k++) {
                a += (int64_t)filter_coeff[      j * 8 + k] * lfe_samples[-k];
                b += (int64_t)filter_coeff[255 - j * 8 - k] * lfe_samples[-k];
            }

            pcm_samples[     j] = clip23(norm23(a));
            pcm_samples[32 + j] = clip23(norm23(b));
        }

        lfe_samples++;
        pcm_samples += 64;
    }
}

static void lfe_x96_fixed_c(int32_t *dst, const int32_t *src,
                            int32_t *hist, ptrdiff_t len)
{
    int32_t prev = *hist;
    int i;

    for (i = 0; i < len; i++) {
        int64_t a = INT64_C(2097471) * src[i] + INT64_C(6291137) * prev;
        int64_t b = INT64_C(6291137) * src[i] + INT64_C(2097471) * prev;
        prev = src[i];
        *dst++ = clip23(norm23(a));
        *dst++ = clip23(norm23(b));
    }

    *hist = prev;
}

static void sub_qmf32_fixed_c(SynthFilterContext *synth,
                              DCADCTContext *imdct,
                              int32_t *pcm_samples,
                              int32_t **subband_samples_lo,
                              int32_t **subband_samples_hi,
                              int32_t *hist1, int *offset, int32_t *hist2,
                              const int32_t *filter_coeff, ptrdiff_t npcmblocks)
{
    LOCAL_ALIGNED_32(int32_t, input, [32]);
    int i, j;

    for (j = 0; j < npcmblocks; j++) {
        // Load in one sample from each subband
        for (i = 0; i < 32; i++)
            input[i] = subband_samples_lo[i][j];

        // One subband sample generates 32 interpolated ones
        synth->synth_filter_fixed(imdct, hist1, offset,
                                  hist2, filter_coeff,
                                  pcm_samples, input);
        pcm_samples += 32;
    }
}

static void sub_qmf64_fixed_c(SynthFilterContext *synth,
                              DCADCTContext *imdct,
                              int32_t *pcm_samples,
                              int32_t **subband_samples_lo,
                              int32_t **subband_samples_hi,
                              int32_t *hist1, int *offset, int32_t *hist2,
                              const int32_t *filter_coeff, ptrdiff_t npcmblocks)
{
    LOCAL_ALIGNED_32(int32_t, input, [64]);
    int i, j;

    if (!subband_samples_hi)
        memset(&input[32], 0, sizeof(input[0]) * 32);

    for (j = 0; j < npcmblocks; j++) {
        // Load in one sample from each subband
        if (subband_samples_hi) {
            // Full 64 subbands, first 32 are residual coded
            for (i =  0; i < 32; i++)
                input[i] = subband_samples_lo[i][j] + subband_samples_hi[i][j];
            for (i = 32; i < 64; i++)
                input[i] = subband_samples_hi[i][j];
        } else {
            // Only first 32 subbands
            for (i =  0; i < 32; i++)
                input[i] = subband_samples_lo[i][j];
        }

        // One subband sample generates 64 interpolated ones
        synth->synth_filter_fixed_64(imdct, hist1, offset,
                                     hist2, filter_coeff,
                                     pcm_samples, input);
        pcm_samples += 64;
    }
}

static void decor_c(int32_t *dst, const int32_t *src, int coeff, ptrdiff_t len)
{
    int i;

    for (i = 0; i < len; i++)
        dst[i] += (SUINT)((int)(src[i] * (SUINT)coeff + (1 << 2)) >> 3);
}

static void dmix_sub_xch_c(int32_t *dst1, int32_t *dst2,
                           const int32_t *src, ptrdiff_t len)
{
    int i;

    for (i = 0; i < len; i++) {
        int32_t cs = mul23(src[i], 5931520 /* M_SQRT1_2 * (1 << 23) */);
        dst1[i] -= cs;
        dst2[i] -= cs;
    }
}

static void dmix_sub_c(int32_t *dst, const int32_t *src, int coeff, ptrdiff_t len)
{
    int i;

    for (i = 0; i < len; i++)
        dst[i] -= (unsigned)mul15(src[i], coeff);
}

static void dmix_add_c(int32_t *dst, const int32_t *src, int coeff, ptrdiff_t len)
{
    int i;

    for (i = 0; i < len; i++)
        dst[i] += (unsigned)mul15(src[i], coeff);
}

static void dmix_scale_c(int32_t *dst, int scale, ptrdiff_t len)
{
    int i;

    for (i = 0; i < len; i++)
        dst[i] = mul15(dst[i], scale);
}

static void dmix_scale_inv_c(int32_t *dst, int scale_inv, ptrdiff_t len)
{
    int i;

    for (i = 0; i < len; i++)
        dst[i] = mul16(dst[i], scale_inv);
}

static void filter0(SUINT32 *dst, const int32_t *src, int32_t coeff, ptrdiff_t len)
{
    int i;

    for (i = 0; i < len; i++)
        dst[i] -= mul22(src[i], coeff);
}

static void filter1(SUINT32 *dst, const int32_t *src, int32_t coeff, ptrdiff_t len)
{
    int i;

    for (i = 0; i < len; i++)
        dst[i] -= mul23(src[i], coeff);
}

static void assemble_freq_bands_c(int32_t *dst, int32_t *src0, int32_t *src1,
                                  const int32_t *coeff, ptrdiff_t len)
{
    int i;

    filter0(src0, src1, coeff[0], len);
    filter0(src1, src0, coeff[1], len);
    filter0(src0, src1, coeff[2], len);
    filter0(src1, src0, coeff[3], len);

    for (i = 0; i < 8; i++, src0--) {
        filter1(src0, src1, coeff[i +  4], len);
        filter1(src1, src0, coeff[i + 12], len);
        filter1(src0, src1, coeff[i +  4], len);
    }

    for (i = 0; i < len; i++) {
        *dst++ = *src1++;
        *dst++ = *++src0;
    }
}

static void lbr_bank_c(float output[32][4], float **input,
                       const float *coeff, ptrdiff_t ofs, ptrdiff_t len)
{
    float SW0 = coeff[0];
    float SW1 = coeff[1];
    float SW2 = coeff[2];
    float SW3 = coeff[3];

    float C1  = coeff[4];
    float C2  = coeff[5];
    float C3  = coeff[6];
    float C4  = coeff[7];

    float AL1 = coeff[8];
    float AL2 = coeff[9];

    int i;

    // Short window and 8 point forward MDCT
    for (i = 0; i < len; i++) {
        float *src = input[i] + ofs;

        float a = src[-4] * SW0 - src[-1] * SW3;
        float b = src[-3] * SW1 - src[-2] * SW2;
        float c = src[ 2] * SW1 + src[ 1] * SW2;
        float d = src[ 3] * SW0 + src[ 0] * SW3;

        output[i][0] = C1 * b - C2 * c + C4 * a - C3 * d;
        output[i][1] = C1 * d - C2 * a - C4 * b - C3 * c;
        output[i][2] = C3 * b + C2 * d - C4 * c + C1 * a;
        output[i][3] = C3 * a - C2 * b + C4 * d - C1 * c;
    }

    // Aliasing cancellation for high frequencies
    for (i = 12; i < len - 1; i++) {
        float a = output[i  ][3] * AL1;
        float b = output[i+1][0] * AL1;
        output[i  ][3] += b - a;
        output[i+1][0] -= b + a;
        a = output[i  ][2] * AL2;
        b = output[i+1][1] * AL2;
        output[i  ][2] += b - a;
        output[i+1][1] -= b + a;
    }
}

static void lfe_iir_c(float *output, const float *input,
                      const float iir[5][4], float hist[5][2],
                      ptrdiff_t factor)
{
    float res, tmp;
    int i, j, k;

    for (i = 0; i < 64; i++) {
        res = *input++;

        for (j = 0; j < factor; j++) {
            for (k = 0; k < 5; k++) {
                tmp = hist[k][0] * iir[k][0] + hist[k][1] * iir[k][1] + res;
                res = hist[k][0] * iir[k][2] + hist[k][1] * iir[k][3] + tmp;

                hist[k][0] = hist[k][1];
                hist[k][1] = tmp;
            }

            *output++ = res;
            res = 0;
        }
    }
}

av_cold void ff_dcadsp_init(DCADSPContext *s)
{
    s->decode_hf     = decode_hf_c;
    s->decode_joint  = decode_joint_c;

    s->lfe_fir_float[0] = lfe_fir0_float_c;
    s->lfe_fir_float[1] = lfe_fir1_float_c;
    s->lfe_x96_float    = lfe_x96_float_c;
    s->sub_qmf_float[0] = sub_qmf32_float_c;
    s->sub_qmf_float[1] = sub_qmf64_float_c;

    s->lfe_fir_fixed    = lfe_fir_fixed_c;
    s->lfe_x96_fixed    = lfe_x96_fixed_c;
    s->sub_qmf_fixed[0] = sub_qmf32_fixed_c;
    s->sub_qmf_fixed[1] = sub_qmf64_fixed_c;

    s->decor   = decor_c;

    s->dmix_sub_xch   = dmix_sub_xch_c;
    s->dmix_sub       = dmix_sub_c;
    s->dmix_add       = dmix_add_c;
    s->dmix_scale     = dmix_scale_c;
    s->dmix_scale_inv = dmix_scale_inv_c;

    s->assemble_freq_bands = assemble_freq_bands_c;

    s->lbr_bank = lbr_bank_c;
    s->lfe_iir = lfe_iir_c;

    if (ARCH_X86)
        ff_dcadsp_init_x86(s);
}
