/*
 * DCA encoder
 * Copyright (C) 2008-2012 Alexander E. Patrakov
 *               2010 Benjamin Larsson
 *               2011 Xiang Wang
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

#define FFT_FLOAT 0
#define FFT_FIXED_32 1

#include "libavutil/avassert.h"
#include "libavutil/channel_layout.h"
#include "libavutil/common.h"
#include "libavutil/ffmath.h"
#include "libavutil/mem_internal.h"
#include "libavutil/opt.h"
#include "avcodec.h"
#include "dca.h"
#include "dcaadpcm.h"
#include "dcamath.h"
#include "dca_core.h"
#include "dcadata.h"
#include "dcaenc.h"
#include "fft.h"
#include "internal.h"
#include "mathops.h"
#include "put_bits.h"

#define MAX_CHANNELS 6
#define DCA_MAX_FRAME_SIZE 16384
#define DCA_HEADER_SIZE 13
#define DCA_LFE_SAMPLES 8

#define DCAENC_SUBBANDS 32
#define SUBFRAMES 1
#define SUBSUBFRAMES 2
#define SUBBAND_SAMPLES (SUBFRAMES * SUBSUBFRAMES * 8)
#define AUBANDS 25

#define COS_T(x) (c->cos_table[(x) & 2047])

typedef struct CompressionOptions {
    int adpcm_mode;
} CompressionOptions;

typedef struct DCAEncContext {
    AVClass *class;
    PutBitContext pb;
    DCAADPCMEncContext adpcm_ctx;
    FFTContext mdct;
    CompressionOptions options;
    int frame_size;
    int frame_bits;
    int fullband_channels;
    int channels;
    int lfe_channel;
    int samplerate_index;
    int bitrate_index;
    int channel_config;
    const int32_t *band_interpolation;
    const int32_t *band_spectrum;
    int lfe_scale_factor;
    softfloat lfe_quant;
    int32_t lfe_peak_cb;
    const int8_t *channel_order_tab;  ///< channel reordering table, lfe and non lfe

    int32_t prediction_mode[MAX_CHANNELS][DCAENC_SUBBANDS];
    int32_t adpcm_history[MAX_CHANNELS][DCAENC_SUBBANDS][DCA_ADPCM_COEFFS * 2];
    int32_t history[MAX_CHANNELS][512]; /* This is a circular buffer */
    int32_t *subband[MAX_CHANNELS][DCAENC_SUBBANDS];
    int32_t quantized[MAX_CHANNELS][DCAENC_SUBBANDS][SUBBAND_SAMPLES];
    int32_t peak_cb[MAX_CHANNELS][DCAENC_SUBBANDS];
    int32_t diff_peak_cb[MAX_CHANNELS][DCAENC_SUBBANDS]; ///< expected peak of residual signal
    int32_t downsampled_lfe[DCA_LFE_SAMPLES];
    int32_t masking_curve_cb[SUBSUBFRAMES][256];
    int32_t bit_allocation_sel[MAX_CHANNELS];
    int abits[MAX_CHANNELS][DCAENC_SUBBANDS];
    int scale_factor[MAX_CHANNELS][DCAENC_SUBBANDS];
    softfloat quant[MAX_CHANNELS][DCAENC_SUBBANDS];
    int32_t quant_index_sel[MAX_CHANNELS][DCA_CODE_BOOKS];
    int32_t eff_masking_curve_cb[256];
    int32_t band_masking_cb[32];
    int32_t worst_quantization_noise;
    int32_t worst_noise_ever;
    int consumed_bits;
    int consumed_adpcm_bits; ///< Number of bits to transmit ADPCM related info

    int32_t cos_table[2048];
    int32_t band_interpolation_tab[2][512];
    int32_t band_spectrum_tab[2][8];
    int32_t auf[9][AUBANDS][256];
    int32_t cb_to_add[256];
    int32_t cb_to_level[2048];
    int32_t lfe_fir_64i[512];
} DCAEncContext;

/* Transfer function of outer and middle ear, Hz -> dB */
static double hom(double f)
{
    double f1 = f / 1000;

    return -3.64 * pow(f1, -0.8)
           + 6.8 * exp(-0.6 * (f1 - 3.4) * (f1 - 3.4))
           - 6.0 * exp(-0.15 * (f1 - 8.7) * (f1 - 8.7))
           - 0.0006 * (f1 * f1) * (f1 * f1);
}

static double gammafilter(int i, double f)
{
    double h = (f - fc[i]) / erb[i];

    h = 1 + h * h;
    h = 1 / (h * h);
    return 20 * log10(h);
}

static int subband_bufer_alloc(DCAEncContext *c)
{
    int ch, band;
    int32_t *bufer = av_calloc(MAX_CHANNELS * DCAENC_SUBBANDS *
                               (SUBBAND_SAMPLES + DCA_ADPCM_COEFFS),
                               sizeof(int32_t));
    if (!bufer)
        return AVERROR(ENOMEM);

    /* we need a place for DCA_ADPCM_COEFF samples from previous frame
     * to calc prediction coefficients for each subband */
    for (ch = 0; ch < MAX_CHANNELS; ch++) {
        for (band = 0; band < DCAENC_SUBBANDS; band++) {
            c->subband[ch][band] = bufer +
                                   ch * DCAENC_SUBBANDS * (SUBBAND_SAMPLES + DCA_ADPCM_COEFFS) +
                                   band * (SUBBAND_SAMPLES + DCA_ADPCM_COEFFS) + DCA_ADPCM_COEFFS;
        }
    }
    return 0;
}

static void subband_bufer_free(DCAEncContext *c)
{
    if (c->subband[0][0]) {
        int32_t *bufer = c->subband[0][0] - DCA_ADPCM_COEFFS;
        av_free(bufer);
        c->subband[0][0] = NULL;
    }
}

static int encode_init(AVCodecContext *avctx)
{
    DCAEncContext *c = avctx->priv_data;
    uint64_t layout = avctx->channel_layout;
    int i, j, k, min_frame_bits;
    int ret;

    if ((ret = subband_bufer_alloc(c)) < 0)
        return ret;

    c->fullband_channels = c->channels = avctx->channels;
    c->lfe_channel = (avctx->channels == 3 || avctx->channels == 6);
    c->band_interpolation = c->band_interpolation_tab[1];
    c->band_spectrum = c->band_spectrum_tab[1];
    c->worst_quantization_noise = -2047;
    c->worst_noise_ever = -2047;
    c->consumed_adpcm_bits = 0;

    if (ff_dcaadpcm_init(&c->adpcm_ctx))
        return AVERROR(ENOMEM);

    if (!layout) {
        av_log(avctx, AV_LOG_WARNING, "No channel layout specified. The "
                                      "encoder will guess the layout, but it "
                                      "might be incorrect.\n");
        layout = av_get_default_channel_layout(avctx->channels);
    }
    switch (layout) {
    case AV_CH_LAYOUT_MONO:         c->channel_config = 0; break;
    case AV_CH_LAYOUT_STEREO:       c->channel_config = 2; break;
    case AV_CH_LAYOUT_2_2:          c->channel_config = 8; break;
    case AV_CH_LAYOUT_5POINT0:      c->channel_config = 9; break;
    case AV_CH_LAYOUT_5POINT1:      c->channel_config = 9; break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Unsupported channel layout!\n");
        return AVERROR_PATCHWELCOME;
    }

    if (c->lfe_channel) {
        c->fullband_channels--;
        c->channel_order_tab = channel_reorder_lfe[c->channel_config];
    } else {
        c->channel_order_tab = channel_reorder_nolfe[c->channel_config];
    }

    for (i = 0; i < MAX_CHANNELS; i++) {
        for (j = 0; j < DCA_CODE_BOOKS; j++) {
            c->quant_index_sel[i][j] = ff_dca_quant_index_group_size[j];
        }
        /* 6 - no Huffman */
        c->bit_allocation_sel[i] = 6;

        for (j = 0; j < DCAENC_SUBBANDS; j++) {
            /* -1 - no ADPCM */
            c->prediction_mode[i][j] = -1;
            memset(c->adpcm_history[i][j], 0, sizeof(int32_t)*DCA_ADPCM_COEFFS);
        }
    }

    for (i = 0; i < 9; i++) {
        if (sample_rates[i] == avctx->sample_rate)
            break;
    }
    if (i == 9)
        return AVERROR(EINVAL);
    c->samplerate_index = i;

    if (avctx->bit_rate < 32000 || avctx->bit_rate > 3840000) {
        av_log(avctx, AV_LOG_ERROR, "Bit rate %"PRId64" not supported.", avctx->bit_rate);
        return AVERROR(EINVAL);
    }
    for (i = 0; ff_dca_bit_rates[i] < avctx->bit_rate; i++)
        ;
    c->bitrate_index = i;
    c->frame_bits = FFALIGN((avctx->bit_rate * 512 + avctx->sample_rate - 1) / avctx->sample_rate, 32);
    min_frame_bits = 132 + (493 + 28 * 32) * c->fullband_channels + c->lfe_channel * 72;
    if (c->frame_bits < min_frame_bits || c->frame_bits > (DCA_MAX_FRAME_SIZE << 3))
        return AVERROR(EINVAL);

    c->frame_size = (c->frame_bits + 7) / 8;

    avctx->frame_size = 32 * SUBBAND_SAMPLES;

    if ((ret = ff_mdct_init(&c->mdct, 9, 0, 1.0)) < 0)
        return ret;

    /* Init all tables */
    c->cos_table[0] = 0x7fffffff;
    c->cos_table[512] = 0;
    c->cos_table[1024] = -c->cos_table[0];
    for (i = 1; i < 512; i++) {
        c->cos_table[i]   = (int32_t)(0x7fffffff * cos(M_PI * i / 1024));
        c->cos_table[1024-i] = -c->cos_table[i];
        c->cos_table[1024+i] = -c->cos_table[i];
        c->cos_table[2048-i] = +c->cos_table[i];
    }

    for (i = 0; i < 2048; i++)
        c->cb_to_level[i] = (int32_t)(0x7fffffff * ff_exp10(-0.005 * i));

    for (k = 0; k < 32; k++) {
        for (j = 0; j < 8; j++) {
            c->lfe_fir_64i[64 * j + k] = (int32_t)(0xffffff800000ULL * ff_dca_lfe_fir_64[8 * k + j]);
            c->lfe_fir_64i[64 * (7-j) + (63 - k)] = (int32_t)(0xffffff800000ULL * ff_dca_lfe_fir_64[8 * k + j]);
        }
    }

    for (i = 0; i < 512; i++) {
        c->band_interpolation_tab[0][i] = (int32_t)(0x1000000000ULL * ff_dca_fir_32bands_perfect[i]);
        c->band_interpolation_tab[1][i] = (int32_t)(0x1000000000ULL * ff_dca_fir_32bands_nonperfect[i]);
    }

    for (i = 0; i < 9; i++) {
        for (j = 0; j < AUBANDS; j++) {
            for (k = 0; k < 256; k++) {
                double freq = sample_rates[i] * (k + 0.5) / 512;

                c->auf[i][j][k] = (int32_t)(10 * (hom(freq) + gammafilter(j, freq)));
            }
        }
    }

    for (i = 0; i < 256; i++) {
        double add = 1 + ff_exp10(-0.01 * i);
        c->cb_to_add[i] = (int32_t)(100 * log10(add));
    }
    for (j = 0; j < 8; j++) {
        double accum = 0;
        for (i = 0; i < 512; i++) {
            double reconst = ff_dca_fir_32bands_perfect[i] * ((i & 64) ? (-1) : 1);
            accum += reconst * cos(2 * M_PI * (i + 0.5 - 256) * (j + 0.5) / 512);
        }
        c->band_spectrum_tab[0][j] = (int32_t)(200 * log10(accum));
    }
    for (j = 0; j < 8; j++) {
        double accum = 0;
        for (i = 0; i < 512; i++) {
            double reconst = ff_dca_fir_32bands_nonperfect[i] * ((i & 64) ? (-1) : 1);
            accum += reconst * cos(2 * M_PI * (i + 0.5 - 256) * (j + 0.5) / 512);
        }
        c->band_spectrum_tab[1][j] = (int32_t)(200 * log10(accum));
    }

    return 0;
}

static av_cold int encode_close(AVCodecContext *avctx)
{
    DCAEncContext *c = avctx->priv_data;
    ff_mdct_end(&c->mdct);
    subband_bufer_free(c);
    ff_dcaadpcm_free(&c->adpcm_ctx);

    return 0;
}

static void subband_transform(DCAEncContext *c, const int32_t *input)
{
    int ch, subs, i, k, j;

    for (ch = 0; ch < c->fullband_channels; ch++) {
        /* History is copied because it is also needed for PSY */
        int32_t hist[512];
        int hist_start = 0;
        const int chi = c->channel_order_tab[ch];

        memcpy(hist, &c->history[ch][0], 512 * sizeof(int32_t));

        for (subs = 0; subs < SUBBAND_SAMPLES; subs++) {
            int32_t accum[64];
            int32_t resp;
            int band;

            /* Calculate the convolutions at once */
            memset(accum, 0, 64 * sizeof(int32_t));

            for (k = 0, i = hist_start, j = 0;
                    i < 512; k = (k + 1) & 63, i++, j++)
                accum[k] += mul32(hist[i], c->band_interpolation[j]);
            for (i = 0; i < hist_start; k = (k + 1) & 63, i++, j++)
                accum[k] += mul32(hist[i], c->band_interpolation[j]);

            for (k = 16; k < 32; k++)
                accum[k] = accum[k] - accum[31 - k];
            for (k = 32; k < 48; k++)
                accum[k] = accum[k] + accum[95 - k];

            for (band = 0; band < 32; band++) {
                resp = 0;
                for (i = 16; i < 48; i++) {
                    int s = (2 * band + 1) * (2 * (i + 16) + 1);
                    resp += mul32(accum[i], COS_T(s << 3)) >> 3;
                }

                c->subband[ch][band][subs] = ((band + 1) & 2) ? -resp : resp;
            }

            /* Copy in 32 new samples from input */
            for (i = 0; i < 32; i++)
                hist[i + hist_start] = input[(subs * 32 + i) * c->channels + chi];

            hist_start = (hist_start + 32) & 511;
        }
    }
}

static void lfe_downsample(DCAEncContext *c, const int32_t *input)
{
    /* FIXME: make 128x LFE downsampling possible */
    const int lfech = lfe_index[c->channel_config];
    int i, j, lfes;
    int32_t hist[512];
    int32_t accum;
    int hist_start = 0;

    memcpy(hist, &c->history[c->channels - 1][0], 512 * sizeof(int32_t));

    for (lfes = 0; lfes < DCA_LFE_SAMPLES; lfes++) {
        /* Calculate the convolution */
        accum = 0;

        for (i = hist_start, j = 0; i < 512; i++, j++)
            accum += mul32(hist[i], c->lfe_fir_64i[j]);
        for (i = 0; i < hist_start; i++, j++)
            accum += mul32(hist[i], c->lfe_fir_64i[j]);

        c->downsampled_lfe[lfes] = accum;

        /* Copy in 64 new samples from input */
        for (i = 0; i < 64; i++)
            hist[i + hist_start] = input[(lfes * 64 + i) * c->channels + lfech];

        hist_start = (hist_start + 64) & 511;
    }
}

static int32_t get_cb(DCAEncContext *c, int32_t in)
{
    int i, res = 0;
    in = FFABS(in);

    for (i = 1024; i > 0; i >>= 1) {
        if (c->cb_to_level[i + res] >= in)
            res += i;
    }
    return -res;
}

static int32_t add_cb(DCAEncContext *c, int32_t a, int32_t b)
{
    if (a < b)
        FFSWAP(int32_t, a, b);

    if (a - b >= 256)
        return a;
    return a + c->cb_to_add[a - b];
}

static void calc_power(DCAEncContext *c,
                       const int32_t in[2 * 256], int32_t power[256])
{
    int i;
    LOCAL_ALIGNED_32(int32_t, data,  [512]);
    LOCAL_ALIGNED_32(int32_t, coeff, [256]);

    for (i = 0; i < 512; i++)
        data[i] = norm__(mul32(in[i], 0x3fffffff - (COS_T(4 * i + 2) >> 1)), 4);

    c->mdct.mdct_calc(&c->mdct, coeff, data);
    for (i = 0; i < 256; i++) {
        const int32_t cb = get_cb(c, coeff[i]);
        power[i] = add_cb(c, cb, cb);
    }
}

static void adjust_jnd(DCAEncContext *c,
                       const int32_t in[512], int32_t out_cb[256])
{
    int32_t power[256];
    int32_t out_cb_unnorm[256];
    int32_t denom;
    const int32_t ca_cb = -1114;
    const int32_t cs_cb = 928;
    const int samplerate_index = c->samplerate_index;
    int i, j;

    calc_power(c, in, power);

    for (j = 0; j < 256; j++)
        out_cb_unnorm[j] = -2047; /* and can only grow */

    for (i = 0; i < AUBANDS; i++) {
        denom = ca_cb; /* and can only grow */
        for (j = 0; j < 256; j++)
            denom = add_cb(c, denom, power[j] + c->auf[samplerate_index][i][j]);
        for (j = 0; j < 256; j++)
            out_cb_unnorm[j] = add_cb(c, out_cb_unnorm[j],
                                      -denom + c->auf[samplerate_index][i][j]);
    }

    for (j = 0; j < 256; j++)
        out_cb[j] = add_cb(c, out_cb[j], -out_cb_unnorm[j] - ca_cb - cs_cb);
}

typedef void (*walk_band_t)(DCAEncContext *c, int band1, int band2, int f,
                            int32_t spectrum1, int32_t spectrum2, int channel,
                            int32_t * arg);

static void walk_band_low(DCAEncContext *c, int band, int channel,
                          walk_band_t walk, int32_t *arg)
{
    int f;

    if (band == 0) {
        for (f = 0; f < 4; f++)
            walk(c, 0, 0, f, 0, -2047, channel, arg);
    } else {
        for (f = 0; f < 8; f++)
            walk(c, band, band - 1, 8 * band - 4 + f,
                    c->band_spectrum[7 - f], c->band_spectrum[f], channel, arg);
    }
}

static void walk_band_high(DCAEncContext *c, int band, int channel,
                           walk_band_t walk, int32_t *arg)
{
    int f;

    if (band == 31) {
        for (f = 0; f < 4; f++)
            walk(c, 31, 31, 256 - 4 + f, 0, -2047, channel, arg);
    } else {
        for (f = 0; f < 8; f++)
            walk(c, band, band + 1, 8 * band + 4 + f,
                    c->band_spectrum[f], c->band_spectrum[7 - f], channel, arg);
    }
}

static void update_band_masking(DCAEncContext *c, int band1, int band2,
                                int f, int32_t spectrum1, int32_t spectrum2,
                                int channel, int32_t * arg)
{
    int32_t value = c->eff_masking_curve_cb[f] - spectrum1;

    if (value < c->band_masking_cb[band1])
        c->band_masking_cb[band1] = value;
}

static void calc_masking(DCAEncContext *c, const int32_t *input)
{
    int i, k, band, ch, ssf;
    int32_t data[512];

    for (i = 0; i < 256; i++)
        for (ssf = 0; ssf < SUBSUBFRAMES; ssf++)
            c->masking_curve_cb[ssf][i] = -2047;

    for (ssf = 0; ssf < SUBSUBFRAMES; ssf++)
        for (ch = 0; ch < c->fullband_channels; ch++) {
            const int chi = c->channel_order_tab[ch];

            for (i = 0, k = 128 + 256 * ssf; k < 512; i++, k++)
                data[i] = c->history[ch][k];
            for (k -= 512; i < 512; i++, k++)
                data[i] = input[k * c->channels + chi];
            adjust_jnd(c, data, c->masking_curve_cb[ssf]);
        }
    for (i = 0; i < 256; i++) {
        int32_t m = 2048;

        for (ssf = 0; ssf < SUBSUBFRAMES; ssf++)
            if (c->masking_curve_cb[ssf][i] < m)
                m = c->masking_curve_cb[ssf][i];
        c->eff_masking_curve_cb[i] = m;
    }

    for (band = 0; band < 32; band++) {
        c->band_masking_cb[band] = 2048;
        walk_band_low(c, band, 0, update_band_masking, NULL);
        walk_band_high(c, band, 0, update_band_masking, NULL);
    }
}

static inline int32_t find_peak(DCAEncContext *c, const int32_t *in, int len)
{
    int sample;
    int32_t m = 0;
    for (sample = 0; sample < len; sample++) {
        int32_t s = abs(in[sample]);
        if (m < s)
            m = s;
    }
    return get_cb(c, m);
}

static void find_peaks(DCAEncContext *c)
{
    int band, ch;

    for (ch = 0; ch < c->fullband_channels; ch++) {
        for (band = 0; band < 32; band++)
            c->peak_cb[ch][band] = find_peak(c, c->subband[ch][band],
                                             SUBBAND_SAMPLES);
    }

    if (c->lfe_channel)
        c->lfe_peak_cb = find_peak(c, c->downsampled_lfe, DCA_LFE_SAMPLES);
}

static void adpcm_analysis(DCAEncContext *c)
{
    int ch, band;
    int pred_vq_id;
    int32_t *samples;
    int32_t estimated_diff[SUBBAND_SAMPLES];

    c->consumed_adpcm_bits = 0;
    for (ch = 0; ch < c->fullband_channels; ch++) {
        for (band = 0; band < 32; band++) {
            samples = c->subband[ch][band] - DCA_ADPCM_COEFFS;
            pred_vq_id = ff_dcaadpcm_subband_analysis(&c->adpcm_ctx, samples,
                                                      SUBBAND_SAMPLES, estimated_diff);
            if (pred_vq_id >= 0) {
                c->prediction_mode[ch][band] = pred_vq_id;
                c->consumed_adpcm_bits += 12; //12 bits to transmit prediction vq index
                c->diff_peak_cb[ch][band] = find_peak(c, estimated_diff, 16);
            } else {
                c->prediction_mode[ch][band] = -1;
            }
        }
    }
}

static const int snr_fudge = 128;
#define USED_1ABITS 1
#define USED_26ABITS 4

static inline int32_t get_step_size(DCAEncContext *c, int ch, int band)
{
    int32_t step_size;

    if (c->bitrate_index == 3)
        step_size = ff_dca_lossless_quant[c->abits[ch][band]];
    else
        step_size = ff_dca_lossy_quant[c->abits[ch][band]];

    return step_size;
}

static int calc_one_scale(DCAEncContext *c, int32_t peak_cb, int abits,
                          softfloat *quant)
{
    int32_t peak;
    int our_nscale, try_remove;
    softfloat our_quant;

    av_assert0(peak_cb <= 0);
    av_assert0(peak_cb >= -2047);

    our_nscale = 127;
    peak = c->cb_to_level[-peak_cb];

    for (try_remove = 64; try_remove > 0; try_remove >>= 1) {
        if (scalefactor_inv[our_nscale - try_remove].e + stepsize_inv[abits].e <= 17)
            continue;
        our_quant.m = mul32(scalefactor_inv[our_nscale - try_remove].m, stepsize_inv[abits].m);
        our_quant.e = scalefactor_inv[our_nscale - try_remove].e + stepsize_inv[abits].e - 17;
        if ((ff_dca_quant_levels[abits] - 1) / 2 < quantize_value(peak, our_quant))
            continue;
        our_nscale -= try_remove;
    }

    if (our_nscale >= 125)
        our_nscale = 124;

    quant->m = mul32(scalefactor_inv[our_nscale].m, stepsize_inv[abits].m);
    quant->e = scalefactor_inv[our_nscale].e + stepsize_inv[abits].e - 17;
    av_assert0((ff_dca_quant_levels[abits] - 1) / 2 >= quantize_value(peak, *quant));

    return our_nscale;
}

static inline void quantize_adpcm_subband(DCAEncContext *c, int ch, int band)
{
    int32_t step_size;
    int32_t diff_peak_cb = c->diff_peak_cb[ch][band];
    c->scale_factor[ch][band] = calc_one_scale(c, diff_peak_cb,
                                               c->abits[ch][band],
                                               &c->quant[ch][band]);

    step_size = get_step_size(c, ch, band);
    ff_dcaadpcm_do_real(c->prediction_mode[ch][band],
                        c->quant[ch][band],
                        ff_dca_scale_factor_quant7[c->scale_factor[ch][band]],
                        step_size, c->adpcm_history[ch][band], c->subband[ch][band],
                        c->adpcm_history[ch][band] + 4, c->quantized[ch][band],
                        SUBBAND_SAMPLES, c->cb_to_level[-diff_peak_cb]);
}

static void quantize_adpcm(DCAEncContext *c)
{
    int band, ch;

    for (ch = 0; ch < c->fullband_channels; ch++)
        for (band = 0; band < 32; band++)
            if (c->prediction_mode[ch][band] >= 0)
                quantize_adpcm_subband(c, ch, band);
}

static void quantize_pcm(DCAEncContext *c)
{
    int sample, band, ch;

    for (ch = 0; ch < c->fullband_channels; ch++) {
        for (band = 0; band < 32; band++) {
            if (c->prediction_mode[ch][band] == -1) {
                for (sample = 0; sample < SUBBAND_SAMPLES; sample++) {
                    int32_t val = quantize_value(c->subband[ch][band][sample],
                                                 c->quant[ch][band]);
                    c->quantized[ch][band][sample] = val;
                }
            }
        }
    }
}

static void accumulate_huff_bit_consumption(int abits, int32_t *quantized,
                                            uint32_t *result)
{
    uint8_t sel, id = abits - 1;
    for (sel = 0; sel < ff_dca_quant_index_group_size[id]; sel++)
        result[sel] += ff_dca_vlc_calc_quant_bits(quantized, SUBBAND_SAMPLES,
                                                  sel, id);
}

static uint32_t set_best_code(uint32_t vlc_bits[DCA_CODE_BOOKS][7],
                              uint32_t clc_bits[DCA_CODE_BOOKS],
                              int32_t res[DCA_CODE_BOOKS])
{
    uint8_t i, sel;
    uint32_t best_sel_bits[DCA_CODE_BOOKS];
    int32_t best_sel_id[DCA_CODE_BOOKS];
    uint32_t t, bits = 0;

    for (i = 0; i < DCA_CODE_BOOKS; i++) {

        av_assert0(!((!!vlc_bits[i][0]) ^ (!!clc_bits[i])));
        if (vlc_bits[i][0] == 0) {
            /* do not transmit adjustment index for empty codebooks */
            res[i] = ff_dca_quant_index_group_size[i];
            /* and skip it */
            continue;
        }

        best_sel_bits[i] = vlc_bits[i][0];
        best_sel_id[i] = 0;
        for (sel = 0; sel < ff_dca_quant_index_group_size[i]; sel++) {
            if (best_sel_bits[i] > vlc_bits[i][sel] && vlc_bits[i][sel]) {
                best_sel_bits[i] = vlc_bits[i][sel];
                best_sel_id[i] = sel;
            }
        }

        /* 2 bits to transmit scale factor adjustment index */
        t = best_sel_bits[i] + 2;
        if (t < clc_bits[i]) {
            res[i] = best_sel_id[i];
            bits += t;
        } else {
            res[i] = ff_dca_quant_index_group_size[i];
            bits += clc_bits[i];
        }
    }
    return bits;
}

static uint32_t set_best_abits_code(int abits[DCAENC_SUBBANDS], int bands,
                                    int32_t *res)
{
    uint8_t i;
    uint32_t t;
    int32_t best_sel = 6;
    int32_t best_bits = bands * 5;

    /* Check do we have subband which cannot be encoded by Huffman tables */
    for (i = 0; i < bands; i++) {
        if (abits[i] > 12 || abits[i] == 0) {
            *res = best_sel;
            return best_bits;
        }
    }

    for (i = 0; i < DCA_BITALLOC_12_COUNT; i++) {
        t = ff_dca_vlc_calc_alloc_bits(abits, bands, i);
        if (t < best_bits) {
            best_bits = t;
            best_sel = i;
        }
    }

    *res = best_sel;
    return best_bits;
}

static int init_quantization_noise(DCAEncContext *c, int noise, int forbid_zero)
{
    int ch, band, ret = USED_26ABITS | USED_1ABITS;
    uint32_t huff_bit_count_accum[MAX_CHANNELS][DCA_CODE_BOOKS][7];
    uint32_t clc_bit_count_accum[MAX_CHANNELS][DCA_CODE_BOOKS];
    uint32_t bits_counter = 0;

    c->consumed_bits = 132 + 333 * c->fullband_channels;
    c->consumed_bits += c->consumed_adpcm_bits;
    if (c->lfe_channel)
        c->consumed_bits += 72;

    /* attempt to guess the bit distribution based on the prevoius frame */
    for (ch = 0; ch < c->fullband_channels; ch++) {
        for (band = 0; band < 32; band++) {
            int snr_cb = c->peak_cb[ch][band] - c->band_masking_cb[band] - noise;

            if (snr_cb >= 1312) {
                c->abits[ch][band] = 26;
                ret &= ~USED_1ABITS;
            } else if (snr_cb >= 222) {
                c->abits[ch][band] = 8 + mul32(snr_cb - 222, 69000000);
                ret &= ~(USED_26ABITS | USED_1ABITS);
            } else if (snr_cb >= 0) {
                c->abits[ch][band] = 2 + mul32(snr_cb, 106000000);
                ret &= ~(USED_26ABITS | USED_1ABITS);
            } else if (forbid_zero || snr_cb >= -140) {
                c->abits[ch][band] = 1;
                ret &= ~USED_26ABITS;
            } else {
                c->abits[ch][band] = 0;
                ret &= ~(USED_26ABITS | USED_1ABITS);
            }
        }
        c->consumed_bits += set_best_abits_code(c->abits[ch], 32,
                                                &c->bit_allocation_sel[ch]);
    }

    /* Recalc scale_factor each time to get bits consumption in case of Huffman coding.
       It is suboptimal solution */
    /* TODO: May be cache scaled values */
    for (ch = 0; ch < c->fullband_channels; ch++) {
        for (band = 0; band < 32; band++) {
            if (c->prediction_mode[ch][band] == -1) {
                c->scale_factor[ch][band] = calc_one_scale(c, c->peak_cb[ch][band],
                                                           c->abits[ch][band],
                                                           &c->quant[ch][band]);
            }
        }
    }
    quantize_adpcm(c);
    quantize_pcm(c);

    memset(huff_bit_count_accum, 0, MAX_CHANNELS * DCA_CODE_BOOKS * 7 * sizeof(uint32_t));
    memset(clc_bit_count_accum, 0, MAX_CHANNELS * DCA_CODE_BOOKS * sizeof(uint32_t));
    for (ch = 0; ch < c->fullband_channels; ch++) {
        for (band = 0; band < 32; band++) {
            if (c->abits[ch][band] && c->abits[ch][band] <= DCA_CODE_BOOKS) {
                accumulate_huff_bit_consumption(c->abits[ch][band],
                                                c->quantized[ch][band],
                                                huff_bit_count_accum[ch][c->abits[ch][band] - 1]);
                clc_bit_count_accum[ch][c->abits[ch][band] - 1] += bit_consumption[c->abits[ch][band]];
            } else {
                bits_counter += bit_consumption[c->abits[ch][band]];
            }
        }
    }

    for (ch = 0; ch < c->fullband_channels; ch++) {
        bits_counter += set_best_code(huff_bit_count_accum[ch],
                                      clc_bit_count_accum[ch],
                                      c->quant_index_sel[ch]);
    }

    c->consumed_bits += bits_counter;

    return ret;
}

static void assign_bits(DCAEncContext *c)
{
    /* Find the bounds where the binary search should work */
    int low, high, down;
    int used_abits = 0;
    int forbid_zero = 1;
restart:
    init_quantization_noise(c, c->worst_quantization_noise, forbid_zero);
    low = high = c->worst_quantization_noise;
    if (c->consumed_bits > c->frame_bits) {
        while (c->consumed_bits > c->frame_bits) {
            if (used_abits == USED_1ABITS && forbid_zero) {
                forbid_zero = 0;
                goto restart;
            }
            low = high;
            high += snr_fudge;
            used_abits = init_quantization_noise(c, high, forbid_zero);
        }
    } else {
        while (c->consumed_bits <= c->frame_bits) {
            high = low;
            if (used_abits == USED_26ABITS)
                goto out; /* The requested bitrate is too high, pad with zeros */
            low -= snr_fudge;
            used_abits = init_quantization_noise(c, low, forbid_zero);
        }
    }

    /* Now do a binary search between low and high to see what fits */
    for (down = snr_fudge >> 1; down; down >>= 1) {
        init_quantization_noise(c, high - down, forbid_zero);
        if (c->consumed_bits <= c->frame_bits)
            high -= down;
    }
    init_quantization_noise(c, high, forbid_zero);
out:
    c->worst_quantization_noise = high;
    if (high > c->worst_noise_ever)
        c->worst_noise_ever = high;
}

static void shift_history(DCAEncContext *c, const int32_t *input)
{
    int k, ch;

    for (k = 0; k < 512; k++)
        for (ch = 0; ch < c->channels; ch++) {
            const int chi = c->channel_order_tab[ch];

            c->history[ch][k] = input[k * c->channels + chi];
        }
}

static void fill_in_adpcm_bufer(DCAEncContext *c)
{
     int ch, band;
     int32_t step_size;
     /* We fill in ADPCM work buffer for subbands which hasn't been ADPCM coded
      * in current frame - we need this data if subband of next frame is
      * ADPCM
      */
     for (ch = 0; ch < c->channels; ch++) {
        for (band = 0; band < 32; band++) {
            int32_t *samples = c->subband[ch][band] - DCA_ADPCM_COEFFS;
            if (c->prediction_mode[ch][band] == -1) {
                step_size = get_step_size(c, ch, band);

                ff_dca_core_dequantize(c->adpcm_history[ch][band],
                                       c->quantized[ch][band]+12, step_size,
                                       ff_dca_scale_factor_quant7[c->scale_factor[ch][band]], 0, 4);
            } else {
                AV_COPY128U(c->adpcm_history[ch][band], c->adpcm_history[ch][band]+4);
            }
            /* Copy dequantized values for LPC analysis.
             * It reduces artifacts in case of extreme quantization,
             * example: in current frame abits is 1 and has no prediction flag,
             * but end of this frame is sine like signal. In this case, if LPC analysis uses
             * original values, likely LPC analysis returns good prediction gain, and sets prediction flag.
             * But there are no proper value in decoder history, so likely result will be no good.
             * Bitstream has "Predictor history flag switch", but this flag disables history for all subbands
             */
            samples[0] = c->adpcm_history[ch][band][0] << 7;
            samples[1] = c->adpcm_history[ch][band][1] << 7;
            samples[2] = c->adpcm_history[ch][band][2] << 7;
            samples[3] = c->adpcm_history[ch][band][3] << 7;
        }
     }
}

static void calc_lfe_scales(DCAEncContext *c)
{
    if (c->lfe_channel)
        c->lfe_scale_factor = calc_one_scale(c, c->lfe_peak_cb, 11, &c->lfe_quant);
}

static void put_frame_header(DCAEncContext *c)
{
    /* SYNC */
    put_bits(&c->pb, 16, 0x7ffe);
    put_bits(&c->pb, 16, 0x8001);

    /* Frame type: normal */
    put_bits(&c->pb, 1, 1);

    /* Deficit sample count: none */
    put_bits(&c->pb, 5, 31);

    /* CRC is not present */
    put_bits(&c->pb, 1, 0);

    /* Number of PCM sample blocks */
    put_bits(&c->pb, 7, SUBBAND_SAMPLES - 1);

    /* Primary frame byte size */
    put_bits(&c->pb, 14, c->frame_size - 1);

    /* Audio channel arrangement */
    put_bits(&c->pb, 6, c->channel_config);

    /* Core audio sampling frequency */
    put_bits(&c->pb, 4, bitstream_sfreq[c->samplerate_index]);

    /* Transmission bit rate */
    put_bits(&c->pb, 5, c->bitrate_index);

    /* Embedded down mix: disabled */
    put_bits(&c->pb, 1, 0);

    /* Embedded dynamic range flag: not present */
    put_bits(&c->pb, 1, 0);

    /* Embedded time stamp flag: not present */
    put_bits(&c->pb, 1, 0);

    /* Auxiliary data flag: not present */
    put_bits(&c->pb, 1, 0);

    /* HDCD source: no */
    put_bits(&c->pb, 1, 0);

    /* Extension audio ID: N/A */
    put_bits(&c->pb, 3, 0);

    /* Extended audio data: not present */
    put_bits(&c->pb, 1, 0);

    /* Audio sync word insertion flag: after each sub-frame */
    put_bits(&c->pb, 1, 0);

    /* Low frequency effects flag: not present or 64x subsampling */
    put_bits(&c->pb, 2, c->lfe_channel ? 2 : 0);

    /* Predictor history switch flag: on */
    put_bits(&c->pb, 1, 1);

    /* No CRC */
    /* Multirate interpolator switch: non-perfect reconstruction */
    put_bits(&c->pb, 1, 0);

    /* Encoder software revision: 7 */
    put_bits(&c->pb, 4, 7);

    /* Copy history: 0 */
    put_bits(&c->pb, 2, 0);

    /* Source PCM resolution: 16 bits, not DTS ES */
    put_bits(&c->pb, 3, 0);

    /* Front sum/difference coding: no */
    put_bits(&c->pb, 1, 0);

    /* Surrounds sum/difference coding: no */
    put_bits(&c->pb, 1, 0);

    /* Dialog normalization: 0 dB */
    put_bits(&c->pb, 4, 0);
}

static void put_primary_audio_header(DCAEncContext *c)
{
    int ch, i;
    /* Number of subframes */
    put_bits(&c->pb, 4, SUBFRAMES - 1);

    /* Number of primary audio channels */
    put_bits(&c->pb, 3, c->fullband_channels - 1);

    /* Subband activity count */
    for (ch = 0; ch < c->fullband_channels; ch++)
        put_bits(&c->pb, 5, DCAENC_SUBBANDS - 2);

    /* High frequency VQ start subband */
    for (ch = 0; ch < c->fullband_channels; ch++)
        put_bits(&c->pb, 5, DCAENC_SUBBANDS - 1);

    /* Joint intensity coding index: 0, 0 */
    for (ch = 0; ch < c->fullband_channels; ch++)
        put_bits(&c->pb, 3, 0);

    /* Transient mode codebook: A4, A4 (arbitrary) */
    for (ch = 0; ch < c->fullband_channels; ch++)
        put_bits(&c->pb, 2, 0);

    /* Scale factor code book: 7 bit linear, 7-bit sqrt table (for each channel) */
    for (ch = 0; ch < c->fullband_channels; ch++)
        put_bits(&c->pb, 3, 6);

    /* Bit allocation quantizer select: linear 5-bit */
    for (ch = 0; ch < c->fullband_channels; ch++)
        put_bits(&c->pb, 3, c->bit_allocation_sel[ch]);

    /* Quantization index codebook select */
    for (i = 0; i < DCA_CODE_BOOKS; i++)
        for (ch = 0; ch < c->fullband_channels; ch++)
            put_bits(&c->pb, ff_dca_quant_index_sel_nbits[i], c->quant_index_sel[ch][i]);

    /* Scale factor adjustment index: transmitted in case of Huffman coding */
    for (i = 0; i < DCA_CODE_BOOKS; i++)
        for (ch = 0; ch < c->fullband_channels; ch++)
            if (c->quant_index_sel[ch][i] < ff_dca_quant_index_group_size[i])
                put_bits(&c->pb, 2, 0);

    /* Audio header CRC check word: not transmitted */
}

static void put_subframe_samples(DCAEncContext *c, int ss, int band, int ch)
{
    int i, j, sum, bits, sel;
    if (c->abits[ch][band] <= DCA_CODE_BOOKS) {
        av_assert0(c->abits[ch][band] > 0);
        sel = c->quant_index_sel[ch][c->abits[ch][band] - 1];
        // Huffman codes
        if (sel < ff_dca_quant_index_group_size[c->abits[ch][band] - 1]) {
            ff_dca_vlc_enc_quant(&c->pb, &c->quantized[ch][band][ss * 8], 8,
                                 sel, c->abits[ch][band] - 1);
            return;
        }

        // Block codes
        if (c->abits[ch][band] <= 7) {
            for (i = 0; i < 8; i += 4) {
                sum = 0;
                for (j = 3; j >= 0; j--) {
                    sum *= ff_dca_quant_levels[c->abits[ch][band]];
                    sum += c->quantized[ch][band][ss * 8 + i + j];
                    sum += (ff_dca_quant_levels[c->abits[ch][band]] - 1) / 2;
                }
                put_bits(&c->pb, bit_consumption[c->abits[ch][band]] / 4, sum);
            }
            return;
        }
    }

    for (i = 0; i < 8; i++) {
        bits = bit_consumption[c->abits[ch][band]] / 16;
        put_sbits(&c->pb, bits, c->quantized[ch][band][ss * 8 + i]);
    }
}

static void put_subframe(DCAEncContext *c, int subframe)
{
    int i, band, ss, ch;

    /* Subsubframes count */
    put_bits(&c->pb, 2, SUBSUBFRAMES -1);

    /* Partial subsubframe sample count: dummy */
    put_bits(&c->pb, 3, 0);

    /* Prediction mode: no ADPCM, in each channel and subband */
    for (ch = 0; ch < c->fullband_channels; ch++)
        for (band = 0; band < DCAENC_SUBBANDS; band++)
            put_bits(&c->pb, 1, !(c->prediction_mode[ch][band] == -1));

    /* Prediction VQ address */
    for (ch = 0; ch < c->fullband_channels; ch++)
        for (band = 0; band < DCAENC_SUBBANDS; band++)
            if (c->prediction_mode[ch][band] >= 0)
                put_bits(&c->pb, 12, c->prediction_mode[ch][band]);

    /* Bit allocation index */
    for (ch = 0; ch < c->fullband_channels; ch++) {
        if (c->bit_allocation_sel[ch] == 6) {
            for (band = 0; band < DCAENC_SUBBANDS; band++) {
                put_bits(&c->pb, 5, c->abits[ch][band]);
            }
        } else {
            ff_dca_vlc_enc_alloc(&c->pb, c->abits[ch], DCAENC_SUBBANDS,
                                 c->bit_allocation_sel[ch]);
        }
    }

    if (SUBSUBFRAMES > 1) {
        /* Transition mode: none for each channel and subband */
        for (ch = 0; ch < c->fullband_channels; ch++)
            for (band = 0; band < DCAENC_SUBBANDS; band++)
                if (c->abits[ch][band])
                    put_bits(&c->pb, 1, 0); /* codebook A4 */
    }

    /* Scale factors */
    for (ch = 0; ch < c->fullband_channels; ch++)
        for (band = 0; band < DCAENC_SUBBANDS; band++)
            if (c->abits[ch][band])
                put_bits(&c->pb, 7, c->scale_factor[ch][band]);

    /* Joint subband scale factor codebook select: not transmitted */
    /* Scale factors for joint subband coding: not transmitted */
    /* Stereo down-mix coefficients: not transmitted */
    /* Dynamic range coefficient: not transmitted */
    /* Stde information CRC check word: not transmitted */
    /* VQ encoded high frequency subbands: not transmitted */

    /* LFE data: 8 samples and scalefactor */
    if (c->lfe_channel) {
        for (i = 0; i < DCA_LFE_SAMPLES; i++)
            put_bits(&c->pb, 8, quantize_value(c->downsampled_lfe[i], c->lfe_quant) & 0xff);
        put_bits(&c->pb, 8, c->lfe_scale_factor);
    }

    /* Audio data (subsubframes) */
    for (ss = 0; ss < SUBSUBFRAMES ; ss++)
        for (ch = 0; ch < c->fullband_channels; ch++)
            for (band = 0; band < DCAENC_SUBBANDS; band++)
                if (c->abits[ch][band])
                    put_subframe_samples(c, ss, band, ch);

    /* DSYNC */
    put_bits(&c->pb, 16, 0xffff);
}

static int encode_frame(AVCodecContext *avctx, AVPacket *avpkt,
                        const AVFrame *frame, int *got_packet_ptr)
{
    DCAEncContext *c = avctx->priv_data;
    const int32_t *samples;
    int ret, i;

    if ((ret = ff_alloc_packet2(avctx, avpkt, c->frame_size, 0)) < 0)
        return ret;

    samples = (const int32_t *)frame->data[0];

    subband_transform(c, samples);
    if (c->lfe_channel)
        lfe_downsample(c, samples);

    calc_masking(c, samples);
    if (c->options.adpcm_mode)
        adpcm_analysis(c);
    find_peaks(c);
    assign_bits(c);
    calc_lfe_scales(c);
    shift_history(c, samples);

    init_put_bits(&c->pb, avpkt->data, avpkt->size);
    fill_in_adpcm_bufer(c);
    put_frame_header(c);
    put_primary_audio_header(c);
    for (i = 0; i < SUBFRAMES; i++)
        put_subframe(c, i);


    for (i = put_bits_count(&c->pb); i < 8*c->frame_size; i++)
        put_bits(&c->pb, 1, 0);

    flush_put_bits(&c->pb);

    avpkt->pts      = frame->pts;
    avpkt->duration = ff_samples_to_time_base(avctx, frame->nb_samples);
    avpkt->size     = put_bits_count(&c->pb) >> 3;
    *got_packet_ptr = 1;
    return 0;
}

#define DCAENC_FLAGS AV_OPT_FLAG_ENCODING_PARAM | AV_OPT_FLAG_AUDIO_PARAM

static const AVOption options[] = {
    { "dca_adpcm", "Use ADPCM encoding", offsetof(DCAEncContext, options.adpcm_mode), AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, DCAENC_FLAGS },
    { NULL },
};

static const AVClass dcaenc_class = {
    .class_name = "DCA (DTS Coherent Acoustics)",
    .item_name = av_default_item_name,
    .option = options,
    .version = LIBAVUTIL_VERSION_INT,
};

static const AVCodecDefault defaults[] = {
    { "b",          "1411200" },
    { NULL },
};

AVCodec ff_dca_encoder = {
    .name                  = "dca",
    .long_name             = NULL_IF_CONFIG_SMALL("DCA (DTS Coherent Acoustics)"),
    .type                  = AVMEDIA_TYPE_AUDIO,
    .id                    = AV_CODEC_ID_DTS,
    .priv_data_size        = sizeof(DCAEncContext),
    .init                  = encode_init,
    .close                 = encode_close,
    .encode2               = encode_frame,
    .capabilities          = AV_CODEC_CAP_EXPERIMENTAL,
    .caps_internal         = FF_CODEC_CAP_INIT_THREADSAFE | FF_CODEC_CAP_INIT_CLEANUP,
    .sample_fmts           = (const enum AVSampleFormat[]){ AV_SAMPLE_FMT_S32,
                                                            AV_SAMPLE_FMT_NONE },
    .supported_samplerates = sample_rates,
    .channel_layouts       = (const uint64_t[]) { AV_CH_LAYOUT_MONO,
                                                  AV_CH_LAYOUT_STEREO,
                                                  AV_CH_LAYOUT_2_2,
                                                  AV_CH_LAYOUT_5POINT0,
                                                  AV_CH_LAYOUT_5POINT1,
                                                  0 },
    .defaults              = defaults,
    .priv_class            = &dcaenc_class,
};
