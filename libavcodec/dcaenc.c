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

#include "libavutil/avassert.h"
#include "libavutil/channel_layout.h"
#include "libavutil/common.h"
#include "libavutil/ffmath.h"
#include "avcodec.h"
#include "dca.h"
#include "dcadata.h"
#include "dcaenc.h"
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

typedef struct DCAEncContext {
    PutBitContext pb;
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

    int32_t history[512][MAX_CHANNELS]; /* This is a circular buffer */
    int32_t subband[SUBBAND_SAMPLES][DCAENC_SUBBANDS][MAX_CHANNELS];
    int32_t quantized[SUBBAND_SAMPLES][DCAENC_SUBBANDS][MAX_CHANNELS];
    int32_t peak_cb[DCAENC_SUBBANDS][MAX_CHANNELS];
    int32_t downsampled_lfe[DCA_LFE_SAMPLES];
    int32_t masking_curve_cb[SUBSUBFRAMES][256];
    int abits[DCAENC_SUBBANDS][MAX_CHANNELS];
    int scale_factor[DCAENC_SUBBANDS][MAX_CHANNELS];
    softfloat quant[DCAENC_SUBBANDS][MAX_CHANNELS];
    int32_t eff_masking_curve_cb[256];
    int32_t band_masking_cb[32];
    int32_t worst_quantization_noise;
    int32_t worst_noise_ever;
    int consumed_bits;
} DCAEncContext;

static int32_t cos_table[2048];
static int32_t band_interpolation[2][512];
static int32_t band_spectrum[2][8];
static int32_t auf[9][AUBANDS][256];
static int32_t cb_to_add[256];
static int32_t cb_to_level[2048];
static int32_t lfe_fir_64i[512];

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

static int encode_init(AVCodecContext *avctx)
{
    DCAEncContext *c = avctx->priv_data;
    uint64_t layout = avctx->channel_layout;
    int i, min_frame_bits;

    c->fullband_channels = c->channels = avctx->channels;
    c->lfe_channel = (avctx->channels == 3 || avctx->channels == 6);
    c->band_interpolation = band_interpolation[1];
    c->band_spectrum = band_spectrum[1];
    c->worst_quantization_noise = -2047;
    c->worst_noise_ever = -2047;

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
        c->channel_order_tab = ff_dca_channel_reorder_lfe[c->channel_config];
    } else {
        c->channel_order_tab = ff_dca_channel_reorder_nolfe[c->channel_config];
    }

    for (i = 0; i < 9; i++) {
        if (sample_rates[i] == avctx->sample_rate)
            break;
    }
    if (i == 9)
        return AVERROR(EINVAL);
    c->samplerate_index = i;

    if (avctx->bit_rate < 32000 || avctx->bit_rate > 3840000) {
        av_log(avctx, AV_LOG_ERROR, "Bit rate %"PRId64" not supported.", (int64_t)avctx->bit_rate);
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

    if (!cos_table[0]) {
        int j, k;

        cos_table[0] = 0x7fffffff;
        cos_table[512] = 0;
        cos_table[1024] = -cos_table[0];
        for (i = 1; i < 512; i++) {
            cos_table[i]   = (int32_t)(0x7fffffff * cos(M_PI * i / 1024));
            cos_table[1024-i] = -cos_table[i];
            cos_table[1024+i] = -cos_table[i];
            cos_table[2048-i] = cos_table[i];
        }
        for (i = 0; i < 2048; i++) {
            cb_to_level[i] = (int32_t)(0x7fffffff * ff_exp10(-0.005 * i));
        }

        for (k = 0; k < 32; k++) {
            for (j = 0; j < 8; j++) {
                lfe_fir_64i[64 * j + k] = (int32_t)(0xffffff800000ULL * ff_dca_lfe_fir_64[8 * k + j]);
                lfe_fir_64i[64 * (7-j) + (63 - k)] = (int32_t)(0xffffff800000ULL * ff_dca_lfe_fir_64[8 * k + j]);
            }
        }

        for (i = 0; i < 512; i++) {
            band_interpolation[0][i] = (int32_t)(0x1000000000ULL * ff_dca_fir_32bands_perfect[i]);
            band_interpolation[1][i] = (int32_t)(0x1000000000ULL * ff_dca_fir_32bands_nonperfect[i]);
        }

        for (i = 0; i < 9; i++) {
            for (j = 0; j < AUBANDS; j++) {
                for (k = 0; k < 256; k++) {
                    double freq = sample_rates[i] * (k + 0.5) / 512;

                    auf[i][j][k] = (int32_t)(10 * (hom(freq) + gammafilter(j, freq)));
                }
            }
        }

        for (i = 0; i < 256; i++) {
            double add = 1 + ff_exp10(-0.01 * i);
            cb_to_add[i] = (int32_t)(100 * log10(add));
        }
        for (j = 0; j < 8; j++) {
            double accum = 0;
            for (i = 0; i < 512; i++) {
                double reconst = ff_dca_fir_32bands_perfect[i] * ((i & 64) ? (-1) : 1);
                accum += reconst * cos(2 * M_PI * (i + 0.5 - 256) * (j + 0.5) / 512);
            }
            band_spectrum[0][j] = (int32_t)(200 * log10(accum));
        }
        for (j = 0; j < 8; j++) {
            double accum = 0;
            for (i = 0; i < 512; i++) {
                double reconst = ff_dca_fir_32bands_nonperfect[i] * ((i & 64) ? (-1) : 1);
                accum += reconst * cos(2 * M_PI * (i + 0.5 - 256) * (j + 0.5) / 512);
            }
            band_spectrum[1][j] = (int32_t)(200 * log10(accum));
        }
    }
    return 0;
}

static inline int32_t cos_t(int x)
{
    return cos_table[x & 2047];
}

static inline int32_t sin_t(int x)
{
    return cos_t(x - 512);
}

static inline int32_t half32(int32_t a)
{
    return (a + 1) >> 1;
}

static inline int32_t mul32(int32_t a, int32_t b)
{
    int64_t r = (int64_t)a * b + 0x80000000ULL;
    return r >> 32;
}

static void subband_transform(DCAEncContext *c, const int32_t *input)
{
    int ch, subs, i, k, j;

    for (ch = 0; ch < c->fullband_channels; ch++) {
        /* History is copied because it is also needed for PSY */
        int32_t hist[512];
        int hist_start = 0;
        const int chi = c->channel_order_tab[ch];

        for (i = 0; i < 512; i++)
            hist[i] = c->history[i][ch];

        for (subs = 0; subs < SUBBAND_SAMPLES; subs++) {
            int32_t accum[64];
            int32_t resp;
            int band;

            /* Calculate the convolutions at once */
            for (i = 0; i < 64; i++)
                accum[i] = 0;

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
                    resp += mul32(accum[i], cos_t(s << 3)) >> 3;
                }

                c->subband[subs][band][ch] = ((band + 1) & 2) ? -resp : resp;
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
    const int lfech = ff_dca_lfe_index[c->channel_config];
    int i, j, lfes;
    int32_t hist[512];
    int32_t accum;
    int hist_start = 0;

    for (i = 0; i < 512; i++)
        hist[i] = c->history[i][c->channels - 1];

    for (lfes = 0; lfes < DCA_LFE_SAMPLES; lfes++) {
        /* Calculate the convolution */
        accum = 0;

        for (i = hist_start, j = 0; i < 512; i++, j++)
            accum += mul32(hist[i], lfe_fir_64i[j]);
        for (i = 0; i < hist_start; i++, j++)
            accum += mul32(hist[i], lfe_fir_64i[j]);

        c->downsampled_lfe[lfes] = accum;

        /* Copy in 64 new samples from input */
        for (i = 0; i < 64; i++)
            hist[i + hist_start] = input[(lfes * 64 + i) * c->channels + lfech];

        hist_start = (hist_start + 64) & 511;
    }
}

typedef struct {
    int32_t re;
    int32_t im;
} cplx32;

static void fft(const int32_t in[2 * 256], cplx32 out[256])
{
    cplx32 buf[256], rin[256], rout[256];
    int i, j, k, l;

    /* do two transforms in parallel */
    for (i = 0; i < 256; i++) {
        /* Apply the Hann window */
        rin[i].re = mul32(in[2 * i], 0x3fffffff - (cos_t(8 * i + 2) >> 1));
        rin[i].im = mul32(in[2 * i + 1], 0x3fffffff - (cos_t(8 * i + 6) >> 1));
    }
    /* pre-rotation */
    for (i = 0; i < 256; i++) {
        buf[i].re = mul32(cos_t(4 * i + 2), rin[i].re)
                  - mul32(sin_t(4 * i + 2), rin[i].im);
        buf[i].im = mul32(cos_t(4 * i + 2), rin[i].im)
                  + mul32(sin_t(4 * i + 2), rin[i].re);
    }

    for (j = 256, l = 1; j != 1; j >>= 1, l <<= 1) {
        for (k = 0; k < 256; k += j) {
            for (i = k; i < k + j / 2; i++) {
                cplx32 sum, diff;
                int t = 8 * l * i;

                sum.re = buf[i].re + buf[i + j / 2].re;
                sum.im = buf[i].im + buf[i + j / 2].im;

                diff.re = buf[i].re - buf[i + j / 2].re;
                diff.im = buf[i].im - buf[i + j / 2].im;

                buf[i].re = half32(sum.re);
                buf[i].im = half32(sum.im);

                buf[i + j / 2].re = mul32(diff.re, cos_t(t))
                                  - mul32(diff.im, sin_t(t));
                buf[i + j / 2].im = mul32(diff.im, cos_t(t))
                                  + mul32(diff.re, sin_t(t));
            }
        }
    }
    /* post-rotation */
    for (i = 0; i < 256; i++) {
        int b = ff_reverse[i];
        rout[i].re = mul32(buf[b].re, cos_t(4 * i))
                   - mul32(buf[b].im, sin_t(4 * i));
        rout[i].im = mul32(buf[b].im, cos_t(4 * i))
                   + mul32(buf[b].re, sin_t(4 * i));
    }
    for (i = 0; i < 256; i++) {
        /* separate the results of the two transforms */
        cplx32 o1, o2;

        o1.re =  rout[i].re - rout[255 - i].re;
        o1.im =  rout[i].im + rout[255 - i].im;

        o2.re =  rout[i].im - rout[255 - i].im;
        o2.im = -rout[i].re - rout[255 - i].re;

        /* combine them into one long transform */
        out[i].re = mul32( o1.re + o2.re, cos_t(2 * i + 1))
                  + mul32( o1.im - o2.im, sin_t(2 * i + 1));
        out[i].im = mul32( o1.im + o2.im, cos_t(2 * i + 1))
                  + mul32(-o1.re + o2.re, sin_t(2 * i + 1));
    }
}

static int32_t get_cb(int32_t in)
{
    int i, res;

    res = 0;
    if (in < 0)
        in = -in;
    for (i = 1024; i > 0; i >>= 1) {
        if (cb_to_level[i + res] >= in)
            res += i;
    }
    return -res;
}

static int32_t add_cb(int32_t a, int32_t b)
{
    if (a < b)
        FFSWAP(int32_t, a, b);

    if (a - b >= 256)
        return a;
    return a + cb_to_add[a - b];
}

static void adjust_jnd(int samplerate_index,
                       const int32_t in[512], int32_t out_cb[256])
{
    int32_t power[256];
    cplx32 out[256];
    int32_t out_cb_unnorm[256];
    int32_t denom;
    const int32_t ca_cb = -1114;
    const int32_t cs_cb = 928;
    int i, j;

    fft(in, out);

    for (j = 0; j < 256; j++) {
        power[j] = add_cb(get_cb(out[j].re), get_cb(out[j].im));
        out_cb_unnorm[j] = -2047; /* and can only grow */
    }

    for (i = 0; i < AUBANDS; i++) {
        denom = ca_cb; /* and can only grow */
        for (j = 0; j < 256; j++)
            denom = add_cb(denom, power[j] + auf[samplerate_index][i][j]);
        for (j = 0; j < 256; j++)
            out_cb_unnorm[j] = add_cb(out_cb_unnorm[j],
                    -denom + auf[samplerate_index][i][j]);
    }

    for (j = 0; j < 256; j++)
        out_cb[j] = add_cb(out_cb[j], -out_cb_unnorm[j] - ca_cb - cs_cb);
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
                data[i] = c->history[k][ch];
            for (k -= 512; i < 512; i++, k++)
                data[i] = input[k * c->channels + chi];
            adjust_jnd(c->samplerate_index, data, c->masking_curve_cb[ssf]);
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

static void find_peaks(DCAEncContext *c)
{
    int band, ch;

    for (band = 0; band < 32; band++)
        for (ch = 0; ch < c->fullband_channels; ch++) {
            int sample;
            int32_t m = 0;

            for (sample = 0; sample < SUBBAND_SAMPLES; sample++) {
                int32_t s = abs(c->subband[sample][band][ch]);
                if (m < s)
                    m = s;
            }
            c->peak_cb[band][ch] = get_cb(m);
        }

    if (c->lfe_channel) {
        int sample;
        int32_t m = 0;

        for (sample = 0; sample < DCA_LFE_SAMPLES; sample++)
            if (m < abs(c->downsampled_lfe[sample]))
                m = abs(c->downsampled_lfe[sample]);
        c->lfe_peak_cb = get_cb(m);
    }
}

static const int snr_fudge = 128;
#define USED_1ABITS 1
#define USED_NABITS 2
#define USED_26ABITS 4

static int init_quantization_noise(DCAEncContext *c, int noise)
{
    int ch, band, ret = 0;

    c->consumed_bits = 132 + 493 * c->fullband_channels;
    if (c->lfe_channel)
        c->consumed_bits += 72;

    /* attempt to guess the bit distribution based on the prevoius frame */
    for (ch = 0; ch < c->fullband_channels; ch++) {
        for (band = 0; band < 32; band++) {
            int snr_cb = c->peak_cb[band][ch] - c->band_masking_cb[band] - noise;

            if (snr_cb >= 1312) {
                c->abits[band][ch] = 26;
                ret |= USED_26ABITS;
            } else if (snr_cb >= 222) {
                c->abits[band][ch] = 8 + mul32(snr_cb - 222, 69000000);
                ret |= USED_NABITS;
            } else if (snr_cb >= 0) {
                c->abits[band][ch] = 2 + mul32(snr_cb, 106000000);
                ret |= USED_NABITS;
            } else {
                c->abits[band][ch] = 1;
                ret |= USED_1ABITS;
            }
        }
    }

    for (band = 0; band < 32; band++)
        for (ch = 0; ch < c->fullband_channels; ch++) {
            c->consumed_bits += bit_consumption[c->abits[band][ch]];
        }

    return ret;
}

static void assign_bits(DCAEncContext *c)
{
    /* Find the bounds where the binary search should work */
    int low, high, down;
    int used_abits = 0;

    init_quantization_noise(c, c->worst_quantization_noise);
    low = high = c->worst_quantization_noise;
    if (c->consumed_bits > c->frame_bits) {
        while (c->consumed_bits > c->frame_bits) {
            av_assert0(used_abits != USED_1ABITS);
            low = high;
            high += snr_fudge;
            used_abits = init_quantization_noise(c, high);
        }
    } else {
        while (c->consumed_bits <= c->frame_bits) {
            high = low;
            if (used_abits == USED_26ABITS)
                goto out; /* The requested bitrate is too high, pad with zeros */
            low -= snr_fudge;
            used_abits = init_quantization_noise(c, low);
        }
    }

    /* Now do a binary search between low and high to see what fits */
    for (down = snr_fudge >> 1; down; down >>= 1) {
        init_quantization_noise(c, high - down);
        if (c->consumed_bits <= c->frame_bits)
            high -= down;
    }
    init_quantization_noise(c, high);
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

            c->history[k][ch] = input[k * c->channels + chi];
        }
}

static int32_t quantize_value(int32_t value, softfloat quant)
{
    int32_t offset = 1 << (quant.e - 1);

    value = mul32(value, quant.m) + offset;
    value = value >> quant.e;
    return value;
}

static int calc_one_scale(int32_t peak_cb, int abits, softfloat *quant)
{
    int32_t peak;
    int our_nscale, try_remove;
    softfloat our_quant;

    av_assert0(peak_cb <= 0);
    av_assert0(peak_cb >= -2047);

    our_nscale = 127;
    peak = cb_to_level[-peak_cb];

    for (try_remove = 64; try_remove > 0; try_remove >>= 1) {
        if (scalefactor_inv[our_nscale - try_remove].e + stepsize_inv[abits].e <= 17)
            continue;
        our_quant.m = mul32(scalefactor_inv[our_nscale - try_remove].m, stepsize_inv[abits].m);
        our_quant.e = scalefactor_inv[our_nscale - try_remove].e + stepsize_inv[abits].e - 17;
        if ((quant_levels[abits] - 1) / 2 < quantize_value(peak, our_quant))
            continue;
        our_nscale -= try_remove;
    }

    if (our_nscale >= 125)
        our_nscale = 124;

    quant->m = mul32(scalefactor_inv[our_nscale].m, stepsize_inv[abits].m);
    quant->e = scalefactor_inv[our_nscale].e + stepsize_inv[abits].e - 17;
    av_assert0((quant_levels[abits] - 1) / 2 >= quantize_value(peak, *quant));

    return our_nscale;
}

static void calc_scales(DCAEncContext *c)
{
    int band, ch;

    for (band = 0; band < 32; band++)
        for (ch = 0; ch < c->fullband_channels; ch++)
            c->scale_factor[band][ch] = calc_one_scale(c->peak_cb[band][ch],
                                                       c->abits[band][ch],
                                                       &c->quant[band][ch]);

    if (c->lfe_channel)
        c->lfe_scale_factor = calc_one_scale(c->lfe_peak_cb, 11, &c->lfe_quant);
}

static void quantize_all(DCAEncContext *c)
{
    int sample, band, ch;

    for (sample = 0; sample < SUBBAND_SAMPLES; sample++)
        for (band = 0; band < 32; band++)
            for (ch = 0; ch < c->fullband_channels; ch++)
                c->quantized[sample][band][ch] = quantize_value(c->subband[sample][band][ch], c->quant[band][ch]);
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
    static const int bitlen[11] = { 0, 1, 2, 2, 2, 2, 3, 3, 3, 3, 3 };
    static const int thr[11]    = { 0, 1, 3, 3, 3, 3, 7, 7, 7, 7, 7 };

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
        put_bits(&c->pb, 3, 6);

    /* Quantization index codebook select: dummy data
       to avoid transmission of scale factor adjustment */
    for (i = 1; i < 11; i++)
        for (ch = 0; ch < c->fullband_channels; ch++)
            put_bits(&c->pb, bitlen[i], thr[i]);

    /* Scale factor adjustment index: not transmitted */
    /* Audio header CRC check word: not transmitted */
}

static void put_subframe_samples(DCAEncContext *c, int ss, int band, int ch)
{
    if (c->abits[band][ch] <= 7) {
        int sum, i, j;
        for (i = 0; i < 8; i += 4) {
            sum = 0;
            for (j = 3; j >= 0; j--) {
                sum *= quant_levels[c->abits[band][ch]];
                sum += c->quantized[ss * 8 + i + j][band][ch];
                sum += (quant_levels[c->abits[band][ch]] - 1) / 2;
            }
            put_bits(&c->pb, bit_consumption[c->abits[band][ch]] / 4, sum);
        }
    } else {
        int i;
        for (i = 0; i < 8; i++) {
            int bits = bit_consumption[c->abits[band][ch]] / 16;
            put_sbits(&c->pb, bits, c->quantized[ss * 8 + i][band][ch]);
        }
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
            put_bits(&c->pb, 1, 0);

    /* Prediction VQ address: not transmitted */
    /* Bit allocation index */
    for (ch = 0; ch < c->fullband_channels; ch++)
        for (band = 0; band < DCAENC_SUBBANDS; band++)
            put_bits(&c->pb, 5, c->abits[band][ch]);

    if (SUBSUBFRAMES > 1) {
        /* Transition mode: none for each channel and subband */
        for (ch = 0; ch < c->fullband_channels; ch++)
            for (band = 0; band < DCAENC_SUBBANDS; band++)
                put_bits(&c->pb, 1, 0); /* codebook A4 */
    }

    /* Scale factors */
    for (ch = 0; ch < c->fullband_channels; ch++)
        for (band = 0; band < DCAENC_SUBBANDS; band++)
            put_bits(&c->pb, 7, c->scale_factor[band][ch]);

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
    find_peaks(c);
    assign_bits(c);
    calc_scales(c);
    quantize_all(c);
    shift_history(c, samples);

    init_put_bits(&c->pb, avpkt->data, avpkt->size);
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
    .encode2               = encode_frame,
    .capabilities          = AV_CODEC_CAP_EXPERIMENTAL,
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
};
