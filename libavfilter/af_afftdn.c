/*
 * Copyright (c) 2018 The FFmpeg Project
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

#include <float.h>

#include "libavutil/audio_fifo.h"
#include "libavutil/avstring.h"
#include "libavutil/channel_layout.h"
#include "libavutil/opt.h"
#include "libavcodec/avfft.h"
#include "avfilter.h"
#include "audio.h"
#include "formats.h"
#include "filters.h"

#define C       (M_LN10 * 0.1)
#define RATIO    0.98
#define RRATIO  (1.0 - RATIO)

enum OutModes {
    IN_MODE,
    OUT_MODE,
    NOISE_MODE,
    NB_MODES
};

enum NoiseType {
    WHITE_NOISE,
    VINYL_NOISE,
    SHELLAC_NOISE,
    CUSTOM_NOISE,
    NB_NOISE
};

typedef struct DeNoiseChannel {
    int         band_noise[15];
    double      noise_band_auto_var[15];
    double      noise_band_sample[15];
    double     *amt;
    double     *band_amt;
    double     *band_excit;
    double     *gain;
    double     *prior;
    double     *prior_band_excit;
    double     *clean_data;
    double     *noisy_data;
    double     *out_samples;
    double     *spread_function;
    double     *abs_var;
    double     *rel_var;
    double     *min_abs_var;
    FFTComplex *fft_data;
    FFTContext *fft, *ifft;

    double      noise_band_norm[15];
    double      noise_band_avr[15];
    double      noise_band_avi[15];
    double      noise_band_var[15];

    double      sfm_threshold;
    double      sfm_alpha;
    double      sfm_results[3];
    int         sfm_fail_flags[512];
    int         sfm_fail_total;
} DeNoiseChannel;

typedef struct AudioFFTDeNoiseContext {
    const AVClass *class;

    float   noise_reduction;
    float   noise_floor;
    int     noise_type;
    char   *band_noise_str;
    float   residual_floor;
    int     track_noise;
    int     track_residual;
    int     output_mode;

    float   last_residual_floor;
    float   last_noise_floor;
    float   last_noise_reduction;
    float   last_noise_balance;
    int64_t block_count;

    int64_t pts;
    int     channels;
    int     sample_noise;
    int     sample_noise_start;
    int     sample_noise_end;
    float   sample_rate;
    int     buffer_length;
    int     fft_length;
    int     fft_length2;
    int     bin_count;
    int     window_length;
    int     sample_advance;
    int     number_of_bands;

    int     band_centre[15];

    int    *bin2band;
    double *window;
    double *band_alpha;
    double *band_beta;

    DeNoiseChannel *dnch;

    double  max_gain;
    double  max_var;
    double  gain_scale;
    double  window_weight;
    double  floor;
    double  sample_floor;
    double  auto_floor;

    int     noise_band_edge[17];
    int     noise_band_count;
    double  matrix_a[25];
    double  vector_b[5];
    double  matrix_b[75];
    double  matrix_c[75];

    AVAudioFifo *fifo;
} AudioFFTDeNoiseContext;

#define OFFSET(x) offsetof(AudioFFTDeNoiseContext, x)
#define A AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption afftdn_options[] = {
    { "nr", "set the noise reduction",    OFFSET(noise_reduction), AV_OPT_TYPE_FLOAT,  {.dbl = 12},          .01, 97, A },
    { "nf", "set the noise floor",        OFFSET(noise_floor),     AV_OPT_TYPE_FLOAT,  {.dbl =-50},          -80,-20, A },
    { "nt", "set the noise type",         OFFSET(noise_type),      AV_OPT_TYPE_INT,    {.i64 = WHITE_NOISE}, WHITE_NOISE, NB_NOISE-1, A, "type" },
    {  "w", "white noise",                0,                       AV_OPT_TYPE_CONST,  {.i64 = WHITE_NOISE},   0,  0, A, "type" },
    {  "v", "vinyl noise",                0,                       AV_OPT_TYPE_CONST,  {.i64 = VINYL_NOISE},   0,  0, A, "type" },
    {  "s", "shellac noise",              0,                       AV_OPT_TYPE_CONST,  {.i64 = SHELLAC_NOISE}, 0,  0, A, "type" },
    {  "c", "custom noise",               0,                       AV_OPT_TYPE_CONST,  {.i64 = CUSTOM_NOISE},  0,  0, A, "type" },
    { "bn", "set the custom bands noise", OFFSET(band_noise_str),  AV_OPT_TYPE_STRING, {.str = 0},             0,  0, A },
    { "rf", "set the residual floor",     OFFSET(residual_floor),  AV_OPT_TYPE_FLOAT,  {.dbl =-38},          -80,-20, A },
    { "tn", "track noise",                OFFSET(track_noise),     AV_OPT_TYPE_BOOL,   {.i64 =  0},            0,  1, A },
    { "tr", "track residual",             OFFSET(track_residual),  AV_OPT_TYPE_BOOL,   {.i64 =  0},            0,  1, A },
    { "om", "set output mode",            OFFSET(output_mode),     AV_OPT_TYPE_INT,    {.i64 = OUT_MODE},      0,  NB_MODES-1, A, "mode" },
    {  "i", "input",                      0,                       AV_OPT_TYPE_CONST,  {.i64 = IN_MODE},       0,  0, A, "mode" },
    {  "o", "output",                     0,                       AV_OPT_TYPE_CONST,  {.i64 = OUT_MODE},      0,  0, A, "mode" },
    {  "n", "noise",                      0,                       AV_OPT_TYPE_CONST,  {.i64 = NOISE_MODE},    0,  0, A, "mode" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(afftdn);

static int get_band_noise(AudioFFTDeNoiseContext *s,
                          int band, double a,
                          double b, double c)
{
    double d1, d2, d3;

    d1 = a / s->band_centre[band];
    d1 = 10.0 * log(1.0 + d1 * d1) / M_LN10;
    d2 = b / s->band_centre[band];
    d2 = 10.0 * log(1.0 + d2 * d2) / M_LN10;
    d3 = s->band_centre[band] / c;
    d3 = 10.0 * log(1.0 + d3 * d3) / M_LN10;

    return lrint(-d1 + d2 - d3);
}

static void factor(double *array, int size)
{
    for (int i = 0; i < size - 1; i++) {
        for (int j = i + 1; j < size; j++) {
            double d = array[j + i * size] / array[i + i * size];

            array[j + i * size] = d;
            for (int k = i + 1; k < size; k++) {
                array[j + k * size] -= d * array[i + k * size];
            }
        }
    }
}

static void solve(double *matrix, double *vector, int size)
{
    for (int i = 0; i < size - 1; i++) {
        for (int j = i + 1; j < size; j++) {
            double d = matrix[j + i * size];
            vector[j] -= d * vector[i];
        }
    }

    vector[size - 1] /= matrix[size * size - 1];

    for (int i = size - 2; i >= 0; i--) {
        double d = vector[i];
        for (int j = i + 1; j < size; j++)
            d -= matrix[i + j * size] * vector[j];
        vector[i] = d / matrix[i + i * size];
    }
}

static int process_get_band_noise(AudioFFTDeNoiseContext *s,
                                  DeNoiseChannel *dnch,
                                  int band)
{
    double product, sum, f;
    int i = 0;

    if (band < 15)
        return dnch->band_noise[band];

    for (int j = 0; j < 5; j++) {
        sum = 0.0;
        for (int k = 0; k < 15; k++)
            sum += s->matrix_b[i++] * dnch->band_noise[k];
        s->vector_b[j] = sum;
    }

    solve(s->matrix_a, s->vector_b, 5);
    f = (0.5 * s->sample_rate) / s->band_centre[14];
    f = 15.0 + log(f / 1.5) / log(1.5);
    sum = 0.0;
    product = 1.0;
    for (int j = 0; j < 5; j++) {
        sum += product * s->vector_b[j];
        product *= f;
    }

    return lrint(sum);
}

static void calculate_sfm(AudioFFTDeNoiseContext *s,
                          DeNoiseChannel *dnch,
                          int start, int end)
{
    double d1 = 0.0, d2 = 1.0;
    int i = 0, j = 0;

    for (int k = start; k < end; k++) {
        if (dnch->noisy_data[k] > s->sample_floor) {
            j++;
            d1 += dnch->noisy_data[k];
            d2 *= dnch->noisy_data[k];
            if (d2 > 1.0E100) {
                d2 *= 1.0E-100;
                i++;
            } else if (d2 < 1.0E-100) {
                d2 *= 1.0E100;
                i--;
            }
        }
    }
    if (j > 1) {
        d1 /= j;
        dnch->sfm_results[0] = d1;
        d2 = log(d2) + 230.2585 * i;
        d2 /= j;
        d1 = log(d1);
        dnch->sfm_results[1] = d1;
        dnch->sfm_results[2] = d1 - d2;
    } else {
        dnch->sfm_results[0] = s->auto_floor;
        dnch->sfm_results[1] = dnch->sfm_threshold;
        dnch->sfm_results[2] = dnch->sfm_threshold;
    }
}

static double limit_gain(double a, double b)
{
    if (a > 1.0)
        return (b * a - 1.0) / (b + a - 2.0);
    if (a < 1.0)
        return (b * a - 2.0 * a + 1.0) / (b - a);
    return 1.0;
}

static void process_frame(AudioFFTDeNoiseContext *s, DeNoiseChannel *dnch,
                          FFTComplex *fft_data,
                          double *prior, double *prior_band_excit, int track_noise)
{
    double d1, d2, d3, gain;
    int n, i1;

    d1 = fft_data[0].re * fft_data[0].re;
    dnch->noisy_data[0] = d1;
    d2 = d1 / dnch->abs_var[0];
    d3 = RATIO * prior[0] + RRATIO * fmax(d2 - 1.0, 0.0);
    gain = d3 / (1.0 + d3);
    gain *= (gain + M_PI_4 / fmax(d2, 1.0E-6));
    prior[0] = (d2 * gain);
    dnch->clean_data[0] = (d1 * gain);
    gain = sqrt(gain);
    dnch->gain[0] = gain;
    n = 0;
    for (int i = 1; i < s->fft_length2; i++) {
        d1 = fft_data[i].re * fft_data[i].re + fft_data[i].im * fft_data[i].im;
        if (d1 > s->sample_floor)
            n = i;

        dnch->noisy_data[i] = d1;
        d2 = d1 / dnch->abs_var[i];
        d3 = RATIO * prior[i] + RRATIO * fmax(d2 - 1.0, 0.0);
        gain = d3 / (1.0 + d3);
        gain *= (gain + M_PI_4 / fmax(d2, 1.0E-6));
        prior[i] = d2 * gain;
        dnch->clean_data[i] = d1 * gain;
        gain = sqrt(gain);
        dnch->gain[i] = gain;
    }
    d1 = fft_data[0].im * fft_data[0].im;
    if (d1 > s->sample_floor)
        n = s->fft_length2;

    dnch->noisy_data[s->fft_length2] = d1;
    d2 = d1 / dnch->abs_var[s->fft_length2];
    d3 = RATIO * prior[s->fft_length2] + RRATIO * fmax(d2 - 1.0, 0.0);
    gain = d3 / (1.0 + d3);
    gain *= gain + M_PI_4 / fmax(d2, 1.0E-6);
    prior[s->fft_length2] = d2 * gain;
    dnch->clean_data[s->fft_length2] = d1 * gain;
    gain = sqrt(gain);
    dnch->gain[s->fft_length2] = gain;
    if (n > s->fft_length2 - 2) {
        n = s->bin_count;
        i1 = s->noise_band_count;
    } else {
        i1 = 0;
        for (int i = 0; i <= s->noise_band_count; i++) {
            if (n > 1.1 * s->noise_band_edge[i]) {
                i1 = i;
            }
        }
    }

    if (track_noise && (i1 > s->noise_band_count / 2)) {
        int j = FFMIN(n, s->noise_band_edge[i1]);
        int m = 3, k;

        for (k = i1 - 1; k >= 0; k--) {
            int i = s->noise_band_edge[k];
            calculate_sfm(s, dnch, i, j);
            dnch->noise_band_sample[k] = dnch->sfm_results[0];
            if (dnch->sfm_results[2] + 0.013 * m * fmax(0.0, dnch->sfm_results[1] - 20.53) >= dnch->sfm_threshold) {
                break;
            }
            j = i;
            m++;
        }

        if (k < i1 - 1) {
            double sum = 0.0, min, max;
            int i;

            for (i = i1 - 1; i > k; i--) {
                min = log(dnch->noise_band_sample[i] / dnch->noise_band_auto_var[i]);
                sum += min;
            }

            i = i1 - k - 1;
            if (i < 5) {
                min = 3.0E-4 * i * i;
            } else {
                min = 3.0E-4 * (8 * i - 16);
            }
            if (i < 3) {
                max = 2.0E-4 * i * i;
            } else {
                max = 2.0E-4 * (4 * i - 4);
            }

            if (s->track_residual) {
                if (s->last_noise_floor > s->last_residual_floor + 9) {
                    min *= 0.5;
                    max *= 0.75;
                } else if (s->last_noise_floor > s->last_residual_floor + 6) {
                    min *= 0.4;
                    max *= 1.0;
                } else if (s->last_noise_floor > s->last_residual_floor + 4) {
                    min *= 0.3;
                    max *= 1.3;
                } else if (s->last_noise_floor > s->last_residual_floor + 2) {
                    min *= 0.2;
                    max *= 1.6;
                } else if (s->last_noise_floor > s->last_residual_floor) {
                    min *= 0.1;
                    max *= 2.0;
                } else {
                    min = 0.0;
                    max *= 2.5;
                }
            }

            sum = av_clipd(sum, -min, max);
            sum = exp(sum);
            for (int i = 0; i < 15; i++)
                dnch->noise_band_auto_var[i] *= sum;
        } else if (dnch->sfm_results[2] >= dnch->sfm_threshold) {
            dnch->sfm_fail_flags[s->block_count & 0x1FF] = 1;
            dnch->sfm_fail_total += 1;
        }
    }

    for (int i = 0; i < s->number_of_bands; i++) {
        dnch->band_excit[i] = 0.0;
        dnch->band_amt[i] = 0.0;
    }

    for (int i = 0; i < s->bin_count; i++) {
        dnch->band_excit[s->bin2band[i]] += dnch->clean_data[i];
    }

    for (int i = 0; i < s->number_of_bands; i++) {
        dnch->band_excit[i] = fmax(dnch->band_excit[i],
                                s->band_alpha[i] * dnch->band_excit[i] +
                                s->band_beta[i] * prior_band_excit[i]);
        prior_band_excit[i] = dnch->band_excit[i];
    }

    for (int j = 0, i = 0; j < s->number_of_bands; j++) {
        for (int k = 0; k < s->number_of_bands; k++) {
            dnch->band_amt[j] += dnch->spread_function[i++] * dnch->band_excit[k];
        }
    }

    for (int i = 0; i < s->bin_count; i++)
        dnch->amt[i] = dnch->band_amt[s->bin2band[i]];

    if (dnch->amt[0] > dnch->abs_var[0]) {
        dnch->gain[0] = 1.0;
    } else if (dnch->amt[0] > dnch->min_abs_var[0]) {
        double limit = sqrt(dnch->abs_var[0] / dnch->amt[0]);
        dnch->gain[0] = limit_gain(dnch->gain[0], limit);
    } else {
        dnch->gain[0] = limit_gain(dnch->gain[0], s->max_gain);
    }
    if (dnch->amt[s->fft_length2] > dnch->abs_var[s->fft_length2]) {
        dnch->gain[s->fft_length2] = 1.0;
    } else if (dnch->amt[s->fft_length2] > dnch->min_abs_var[s->fft_length2]) {
        double limit = sqrt(dnch->abs_var[s->fft_length2] / dnch->amt[s->fft_length2]);
        dnch->gain[s->fft_length2] = limit_gain(dnch->gain[s->fft_length2], limit);
    } else {
        dnch->gain[s->fft_length2] = limit_gain(dnch->gain[s->fft_length2], s->max_gain);
    }

    for (int i = 1; i < s->fft_length2; i++) {
        if (dnch->amt[i] > dnch->abs_var[i]) {
            dnch->gain[i] = 1.0;
        } else if (dnch->amt[i] > dnch->min_abs_var[i]) {
            double limit = sqrt(dnch->abs_var[i] / dnch->amt[i]);
            dnch->gain[i] = limit_gain(dnch->gain[i], limit);
        } else {
            dnch->gain[i] = limit_gain(dnch->gain[i], s->max_gain);
        }
    }

    gain = dnch->gain[0];
    dnch->clean_data[0] = (gain * gain * dnch->noisy_data[0]);
    fft_data[0].re *= gain;
    gain = dnch->gain[s->fft_length2];
    dnch->clean_data[s->fft_length2] = (gain * gain * dnch->noisy_data[s->fft_length2]);
    fft_data[0].im *= gain;
    for (int i = 1; i < s->fft_length2; i++) {
        gain = dnch->gain[i];
        dnch->clean_data[i] = (gain * gain * dnch->noisy_data[i]);
        fft_data[i].re *= gain;
        fft_data[i].im *= gain;
    }
}

static double freq2bark(double x)
{
    double d = x / 7500.0;

    return 13.0 * atan(7.6E-4 * x) + 3.5 * atan(d * d);
}

static int get_band_centre(AudioFFTDeNoiseContext *s, int band)
{
    if (band == -1)
        return lrint(s->band_centre[0] / 1.5);

    return s->band_centre[band];
}

static int get_band_edge(AudioFFTDeNoiseContext *s, int band)
{
    int i;

    if (band == 15) {
        i = lrint(s->band_centre[14] * 1.224745);
    } else {
        i = lrint(s->band_centre[band] / 1.224745);
    }

    return FFMIN(i, s->sample_rate / 2);
}

static void set_band_parameters(AudioFFTDeNoiseContext *s,
                                DeNoiseChannel *dnch)
{
    double band_noise, d2, d3, d4, d5;
    int i = 0, j = 0, k = 0;

    d5 = 0.0;
    band_noise = process_get_band_noise(s, dnch, 0);
    for (int m = j; m <= s->fft_length2; m++) {
        if (m == j) {
            i = j;
            d5 = band_noise;
            if (k == 15) {
                j = s->bin_count;
            } else {
                j = s->fft_length * get_band_centre(s, k) / s->sample_rate;
            }
            d2 = j - i;
            band_noise = process_get_band_noise(s, dnch, k);
            k++;
        }
        d3 = (j - m) / d2;
        d4 = (m - i) / d2;
        dnch->rel_var[m] = exp((d5 * d3 + band_noise * d4) * C);
    }
    dnch->rel_var[s->fft_length2] = exp(band_noise * C);

    for (i = 0; i < 15; i++)
        dnch->noise_band_auto_var[i] = s->max_var * exp((process_get_band_noise(s, dnch, i) - 2.0) * C);

    for (i = 0; i <= s->fft_length2; i++) {
        dnch->abs_var[i] = fmax(s->max_var * dnch->rel_var[i], 1.0);
        dnch->min_abs_var[i] = s->gain_scale * dnch->abs_var[i];
    }
}

static void read_custom_noise(AudioFFTDeNoiseContext *s, int ch)
{
    DeNoiseChannel *dnch = &s->dnch[ch];
    char *p, *arg, *saveptr = NULL;
    int i, ret, band_noise[15] = { 0 };

    if (!s->band_noise_str)
        return;

    p = av_strdup(s->band_noise_str);
    if (!p)
        return;

    for (i = 0; i < 15; i++) {
        if (!(arg = av_strtok(p, "| ", &saveptr)))
            break;

        p = NULL;

        ret = sscanf(arg, "%d", &band_noise[i]);
        if (ret != 1) {
            av_log(s, AV_LOG_ERROR, "Custom band noise must be integer.\n");
            break;
        }

        band_noise[i] = av_clip(band_noise[i], -24, 24);
    }

    av_free(p);
    memcpy(dnch->band_noise, band_noise, sizeof(band_noise));
}

static void set_parameters(AudioFFTDeNoiseContext *s)
{
    if (s->last_noise_floor != s->noise_floor)
        s->last_noise_floor = s->noise_floor;

    if (s->track_residual)
        s->last_noise_floor = fmaxf(s->last_noise_floor, s->residual_floor);

    s->max_var = s->floor * exp((100.0 + s->last_noise_floor) * C);

    if (s->track_residual) {
        s->last_residual_floor = s->residual_floor;
        s->last_noise_reduction = fmax(s->last_noise_floor - s->last_residual_floor, 0);
        s->max_gain = exp(s->last_noise_reduction * (0.5 * C));
    } else if (s->noise_reduction != s->last_noise_reduction) {
        s->last_noise_reduction = s->noise_reduction;
        s->last_residual_floor = av_clipf(s->last_noise_floor - s->last_noise_reduction, -80, -20);
        s->max_gain = exp(s->last_noise_reduction * (0.5 * C));
    }

    s->gain_scale = 1.0 / (s->max_gain * s->max_gain);

    for (int ch = 0; ch < s->channels; ch++) {
        DeNoiseChannel *dnch = &s->dnch[ch];

        set_band_parameters(s, dnch);
    }
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    AudioFFTDeNoiseContext *s = ctx->priv;
    double wscale, sar, sum, sdiv;
    int i, j, k, m, n;

    s->dnch = av_calloc(inlink->channels, sizeof(*s->dnch));
    if (!s->dnch)
        return AVERROR(ENOMEM);

    s->pts = AV_NOPTS_VALUE;
    s->channels = inlink->channels;
    s->sample_rate = inlink->sample_rate;
    s->sample_advance = s->sample_rate / 80;
    s->window_length = 3 * s->sample_advance;
    s->fft_length2 = 1 << (32 - ff_clz(s->window_length));
    s->fft_length = s->fft_length2 * 2;
    s->buffer_length = s->fft_length * 2;
    s->bin_count = s->fft_length2 + 1;

    s->band_centre[0] = 80;
    for (i = 1; i < 15; i++) {
        s->band_centre[i] = lrint(1.5 * s->band_centre[i - 1] + 5.0);
        if (s->band_centre[i] < 1000) {
            s->band_centre[i] = 10 * (s->band_centre[i] / 10);
        } else if (s->band_centre[i] < 5000) {
            s->band_centre[i] = 50 * ((s->band_centre[i] + 20) / 50);
        } else if (s->band_centre[i] < 15000) {
            s->band_centre[i] = 100 * ((s->band_centre[i] + 45) / 100);
        } else {
            s->band_centre[i] = 1000 * ((s->band_centre[i] + 495) / 1000);
        }
    }

    for (j = 0; j < 5; j++) {
        for (k = 0; k < 5; k++) {
            s->matrix_a[j + k * 5] = 0.0;
            for (m = 0; m < 15; m++)
                s->matrix_a[j + k * 5] += pow(m, j + k);
        }
    }

    factor(s->matrix_a, 5);

    i = 0;
    for (j = 0; j < 5; j++)
        for (k = 0; k < 15; k++)
            s->matrix_b[i++] = pow(k, j);

    i = 0;
    for (j = 0; j < 15; j++)
        for (k = 0; k < 5; k++)
            s->matrix_c[i++] = pow(j, k);

    s->window = av_calloc(s->window_length, sizeof(*s->window));
    s->bin2band = av_calloc(s->bin_count, sizeof(*s->bin2band));
    if (!s->window || !s->bin2band)
        return AVERROR(ENOMEM);

    sdiv = s->sample_rate / 17640.0;
    for (i = 0; i <= s->fft_length2; i++)
        s->bin2band[i] = lrint(sdiv * freq2bark((0.5 * i * s->sample_rate) / s->fft_length2));

    s->number_of_bands = s->bin2band[s->fft_length2] + 1;

    s->band_alpha = av_calloc(s->number_of_bands, sizeof(*s->band_alpha));
    s->band_beta = av_calloc(s->number_of_bands, sizeof(*s->band_beta));
    if (!s->band_alpha || !s->band_beta)
        return AVERROR(ENOMEM);

    for (int ch = 0; ch < inlink->channels; ch++) {
        DeNoiseChannel *dnch = &s->dnch[ch];

        switch (s->noise_type) {
        case WHITE_NOISE:
            for (i = 0; i < 15; i++)
                dnch->band_noise[i] = 0;
            break;
        case VINYL_NOISE:
            for (i = 0; i < 15; i++)
                dnch->band_noise[i] = get_band_noise(s, i, 50.0, 500.5, 2125.0) + FFMAX(i - 7, 0);
            break;
        case SHELLAC_NOISE:
            for (i = 0; i < 15; i++)
                dnch->band_noise[i] = get_band_noise(s, i, 1.0, 500.0, 1.0E10) + FFMAX(i - 12, -5);
            break;
        case CUSTOM_NOISE:
            read_custom_noise(s, ch);
            break;
        default:
            return AVERROR_BUG;
        }


        dnch->sfm_threshold = 0.8;
        dnch->sfm_alpha = 0.05;
        for (i = 0; i < 512; i++)
            dnch->sfm_fail_flags[i] = 0;

        dnch->sfm_fail_total = 0;
        j = FFMAX((int)(10.0 * (1.3 - dnch->sfm_threshold)), 1);

        for (i = 0; i < 512; i += j) {
            dnch->sfm_fail_flags[i] = 1;
            dnch->sfm_fail_total += 1;
        }

        dnch->amt = av_calloc(s->bin_count, sizeof(*dnch->amt));
        dnch->band_amt = av_calloc(s->number_of_bands, sizeof(*dnch->band_amt));
        dnch->band_excit = av_calloc(s->number_of_bands, sizeof(*dnch->band_excit));
        dnch->gain = av_calloc(s->bin_count, sizeof(*dnch->gain));
        dnch->prior = av_calloc(s->bin_count, sizeof(*dnch->prior));
        dnch->prior_band_excit = av_calloc(s->number_of_bands, sizeof(*dnch->prior_band_excit));
        dnch->clean_data = av_calloc(s->bin_count, sizeof(*dnch->clean_data));
        dnch->noisy_data = av_calloc(s->bin_count, sizeof(*dnch->noisy_data));
        dnch->out_samples = av_calloc(s->buffer_length, sizeof(*dnch->out_samples));
        dnch->abs_var = av_calloc(s->bin_count, sizeof(*dnch->abs_var));
        dnch->rel_var = av_calloc(s->bin_count, sizeof(*dnch->rel_var));
        dnch->min_abs_var = av_calloc(s->bin_count, sizeof(*dnch->min_abs_var));
        dnch->fft_data = av_calloc(s->fft_length2 + 1, sizeof(*dnch->fft_data));
        dnch->fft  = av_fft_init(av_log2(s->fft_length2), 0);
        dnch->ifft = av_fft_init(av_log2(s->fft_length2), 1);
        dnch->spread_function = av_calloc(s->number_of_bands * s->number_of_bands,
                                          sizeof(*dnch->spread_function));

        if (!dnch->amt ||
            !dnch->band_amt ||
            !dnch->band_excit ||
            !dnch->gain ||
            !dnch->prior ||
            !dnch->prior_band_excit ||
            !dnch->clean_data ||
            !dnch->noisy_data ||
            !dnch->out_samples ||
            !dnch->fft_data ||
            !dnch->abs_var ||
            !dnch->rel_var ||
            !dnch->min_abs_var ||
            !dnch->spread_function ||
            !dnch->fft ||
            !dnch->ifft)
            return AVERROR(ENOMEM);
    }

    for (int ch = 0; ch < inlink->channels; ch++) {
        DeNoiseChannel *dnch = &s->dnch[ch];
        double *prior_band_excit = dnch->prior_band_excit;
        double *prior = dnch->prior;
        double min, max;
        double p1, p2;

        p1 = pow(0.1, 2.5 / sdiv);
        p2 = pow(0.1, 1.0 / sdiv);
        j = 0;
        for (m = 0; m < s->number_of_bands; m++) {
            for (n = 0; n < s->number_of_bands; n++) {
                if (n < m) {
                    dnch->spread_function[j++] = pow(p2, m - n);
                } else if (n > m) {
                    dnch->spread_function[j++] = pow(p1, n - m);
                } else {
                    dnch->spread_function[j++] = 1.0;
                }
            }
        }

        for (m = 0; m < s->number_of_bands; m++) {
            dnch->band_excit[m] = 0.0;
            prior_band_excit[m] = 0.0;
        }

        for (m = 0; m <= s->fft_length2; m++)
            dnch->band_excit[s->bin2band[m]] += 1.0;

        j = 0;
        for (m = 0; m < s->number_of_bands; m++) {
            for (n = 0; n < s->number_of_bands; n++)
                prior_band_excit[m] += dnch->spread_function[j++] * dnch->band_excit[n];
        }

        min = pow(0.1, 2.5);
        max = pow(0.1, 1.0);
        for (int i = 0; i < s->number_of_bands; i++) {
            if (i < lrint(12.0 * sdiv)) {
                dnch->band_excit[i] = pow(0.1, 1.45 + 0.1 * i / sdiv);
            } else {
                dnch->band_excit[i] = pow(0.1, 2.5 - 0.2 * (i / sdiv - 14.0));
            }
            dnch->band_excit[i] = av_clipd(dnch->band_excit[i], min, max);
        }

        for (int i = 0; i <= s->fft_length2; i++)
            prior[i] = RRATIO;
        for (int i = 0; i < s->buffer_length; i++)
            dnch->out_samples[i] = 0;

        j = 0;
        for (int i = 0; i < s->number_of_bands; i++)
            for (int k = 0; k < s->number_of_bands; k++)
                dnch->spread_function[j++] *= dnch->band_excit[i] / prior_band_excit[i];
    }

    j = 0;
    sar = s->sample_advance / s->sample_rate;
    for (int i = 0; i <= s->fft_length2; i++) {
        if ((i == s->fft_length2) || (s->bin2band[i] > j)) {
            double d6 = (i - 1) * s->sample_rate / s->fft_length;
            double d7 = fmin(0.008 + 2.2 / d6, 0.03);
            s->band_alpha[j] = exp(-sar / d7);
            s->band_beta[j] = 1.0 - s->band_alpha[j];
            j = s->bin2band[i];
        }
    }

    wscale = sqrt(16.0 / (9.0 * s->fft_length));
    sum = 0.0;
    for (int i = 0; i < s->window_length; i++) {
        double d10 = sin(i * M_PI / s->window_length);
        d10 *= wscale * d10;
        s->window[i] = d10;
        sum += d10 * d10;
    }

    s->window_weight = 0.5 * sum;
    s->floor = (1LL << 48) * exp(-23.025558369790467) * s->window_weight;
    s->sample_floor = s->floor * exp(4.144600506562284);
    s->auto_floor = s->floor * exp(6.907667510937141);

    set_parameters(s);

    s->noise_band_edge[0] = FFMIN(s->fft_length2, s->fft_length * get_band_edge(s, 0) / s->sample_rate);
    i = 0;
    for (int j = 1; j < 16; j++) {
        s->noise_band_edge[j] = FFMIN(s->fft_length2, s->fft_length * get_band_edge(s, j) / s->sample_rate);
        if (s->noise_band_edge[j] > lrint(1.1 * s->noise_band_edge[j - 1]))
            i++;
        s->noise_band_edge[16] = i;
    }
    s->noise_band_count = s->noise_band_edge[16];

    s->fifo = av_audio_fifo_alloc(inlink->format, inlink->channels, s->fft_length);
    if (!s->fifo)
        return AVERROR(ENOMEM);

    return 0;
}

static void preprocess(FFTComplex *in, int len)
{
    double d1, d2, d3, d4, d5, d6, d7, d8, d9, d10;
    int n, i, k;

    d5 = 2.0 * M_PI / len;
    d8 = sin(0.5 * d5);
    d8 = -2.0 * d8 * d8;
    d7 = sin(d5);
    d9 = 1.0 + d8;
    d6 = d7;
    n = len / 2;

    for (i = 1; i < len / 4; i++) {
        k = n - i;
        d2 = 0.5 * (in[i].re + in[k].re);
        d1 = 0.5 * (in[i].im - in[k].im);
        d4 = 0.5 * (in[i].im + in[k].im);
        d3 = 0.5 * (in[k].re - in[i].re);
        in[i].re = d2 + d9 * d4 + d6 * d3;
        in[i].im = d1 + d9 * d3 - d6 * d4;
        in[k].re = d2 - d9 * d4 - d6 * d3;
        in[k].im = -d1 + d9 * d3 - d6 * d4;
        d10 = d9;
        d9 += d9 * d8 - d6 * d7;
        d6 += d6 * d8 + d10 * d7;
    }

    d2 = in[0].re;
    in[0].re = d2 + in[0].im;
    in[0].im = d2 - in[0].im;
}

static void postprocess(FFTComplex *in, int len)
{
    double d1, d2, d3, d4, d5, d6, d7, d8, d9, d10;
    int n, i, k;

    d5 = 2.0 * M_PI / len;
    d8 = sin(0.5 * d5);
    d8 = -2.0 * d8 * d8;
    d7 = sin(d5);
    d9 = 1.0 + d8;
    d6 = d7;
    n = len / 2;
    for (i = 1; i < len / 4; i++) {
        k = n - i;
        d2 = 0.5 * (in[i].re + in[k].re);
        d1 = 0.5 * (in[i].im - in[k].im);
        d4 = 0.5 * (in[i].re - in[k].re);
        d3 = 0.5 * (in[i].im + in[k].im);
        in[i].re = d2 - d9 * d3 - d6 * d4;
        in[i].im = d1 + d9 * d4 - d6 * d3;
        in[k].re = d2 + d9 * d3 + d6 * d4;
        in[k].im = -d1 + d9 * d4 - d6 * d3;
        d10 = d9;
        d9 += d9 * d8 - d6 * d7;
        d6 += d6 * d8 + d10 * d7;
    }
    d2 = in[0].re;
    in[0].re = 0.5 * (d2 + in[0].im);
    in[0].im = 0.5 * (d2 - in[0].im);
}

static void init_sample_noise(DeNoiseChannel *dnch)
{
    for (int i = 0; i < 15; i++) {
        dnch->noise_band_norm[i] = 0.0;
        dnch->noise_band_avr[i] = 0.0;
        dnch->noise_band_avi[i] = 0.0;
        dnch->noise_band_var[i] = 0.0;
    }
}

static void sample_noise_block(AudioFFTDeNoiseContext *s,
                               DeNoiseChannel *dnch,
                               AVFrame *in, int ch)
{
    float *src = (float *)in->extended_data[ch];
    double mag2, var = 0.0, avr = 0.0, avi = 0.0;
    int edge, j, k, n, edgemax;

    for (int i = 0; i < s->window_length; i++) {
        dnch->fft_data[i].re = s->window[i] * src[i] * (1LL << 24);
        dnch->fft_data[i].im = 0.0;
    }

    for (int i = s->window_length; i < s->fft_length2; i++) {
        dnch->fft_data[i].re = 0.0;
        dnch->fft_data[i].im = 0.0;
    }

    av_fft_permute(dnch->fft, dnch->fft_data);
    av_fft_calc(dnch->fft, dnch->fft_data);

    preprocess(dnch->fft_data, s->fft_length);

    edge = s->noise_band_edge[0];
    j = edge;
    k = 0;
    n = j;
    edgemax = fmin(s->fft_length2, s->noise_band_edge[15]);
    dnch->fft_data[s->fft_length2].re = dnch->fft_data[0].im;
    dnch->fft_data[0].im = 0.0;
    dnch->fft_data[s->fft_length2].im = 0.0;

    for (int i = j; i <= edgemax; i++) {
        if ((i == j) && (i < edgemax)) {
            if (j > edge) {
                dnch->noise_band_norm[k - 1] += j - edge;
                dnch->noise_band_avr[k - 1] += avr;
                dnch->noise_band_avi[k - 1] += avi;
                dnch->noise_band_var[k - 1] += var;
            }
            k++;
            edge = j;
            j = s->noise_band_edge[k];
            if (k == 15) {
                j++;
            }
            var = 0.0;
            avr = 0.0;
            avi = 0.0;
        }
        avr += dnch->fft_data[n].re;
        avi += dnch->fft_data[n].im;
        mag2 = dnch->fft_data[n].re * dnch->fft_data[n].re +
               dnch->fft_data[n].im * dnch->fft_data[n].im;

        mag2 = fmax(mag2, s->sample_floor);

        dnch->noisy_data[i] = mag2;
        var += mag2;
        n++;
    }

    dnch->noise_band_norm[k - 1] += j - edge;
    dnch->noise_band_avr[k - 1] += avr;
    dnch->noise_band_avi[k - 1] += avi;
    dnch->noise_band_var[k - 1] += var;
}

static void finish_sample_noise(AudioFFTDeNoiseContext *s,
                                DeNoiseChannel *dnch,
                                double *sample_noise)
{
    for (int i = 0; i < s->noise_band_count; i++) {
        dnch->noise_band_avr[i] /= dnch->noise_band_norm[i];
        dnch->noise_band_avi[i] /= dnch->noise_band_norm[i];
        dnch->noise_band_var[i] /= dnch->noise_band_norm[i];
        dnch->noise_band_var[i] -= dnch->noise_band_avr[i] * dnch->noise_band_avr[i] +
                                   dnch->noise_band_avi[i] * dnch->noise_band_avi[i];
        dnch->noise_band_auto_var[i] = dnch->noise_band_var[i];
        sample_noise[i] = (1.0 / C) * log(dnch->noise_band_var[i] / s->floor) - 100.0;
    }
    if (s->noise_band_count < 15) {
        for (int i = s->noise_band_count; i < 15; i++)
            sample_noise[i] = sample_noise[i - 1];
    }
}

static void set_noise_profile(AudioFFTDeNoiseContext *s,
                              DeNoiseChannel *dnch,
                              double *sample_noise,
                              int new_profile)
{
    int new_band_noise[15];
    double temp[15];
    double sum = 0.0, d1;
    float new_noise_floor;
    int i, n;

    for (int m = 0; m < 15; m++)
        temp[m] = sample_noise[m];

    if (new_profile) {
        i = 0;
        for (int m = 0; m < 5; m++) {
            sum = 0.0;
            for (n = 0; n < 15; n++)
                sum += s->matrix_b[i++] * temp[n];
            s->vector_b[m] = sum;
        }
        solve(s->matrix_a, s->vector_b, 5);
        i = 0;
        for (int m = 0; m < 15; m++) {
            sum = 0.0;
            for (n = 0; n < 5; n++)
                sum += s->matrix_c[i++] * s->vector_b[n];
            temp[m] = sum;
        }
    }

    sum = 0.0;
    for (int m = 0; m < 15; m++)
        sum += temp[m];

    d1 = (int)(sum / 15.0 - 0.5);
    if (!new_profile)
        i = lrint(temp[7] - d1);

    for (d1 -= dnch->band_noise[7] - i; d1 > -20.0; d1 -= 1.0)
        ;

    for (int m = 0; m < 15; m++)
        temp[m] -= d1;

    new_noise_floor = d1 + 2.5;

    if (new_profile) {
        av_log(s, AV_LOG_INFO, "bn=");
        for (int m = 0; m < 15; m++) {
            new_band_noise[m] = lrint(temp[m]);
            new_band_noise[m] = av_clip(new_band_noise[m], -24, 24);
            av_log(s, AV_LOG_INFO, "%d ", new_band_noise[m]);
        }
        av_log(s, AV_LOG_INFO, "\n");
        memcpy(dnch->band_noise, new_band_noise, sizeof(new_band_noise));
    }

    if (s->track_noise)
        s->noise_floor = new_noise_floor;
}

typedef struct ThreadData {
    AVFrame *in;
} ThreadData;

static int filter_channel(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    AudioFFTDeNoiseContext *s = ctx->priv;
    ThreadData *td = arg;
    AVFrame *in = td->in;
    const int start = (in->channels * jobnr) / nb_jobs;
    const int end = (in->channels * (jobnr+1)) / nb_jobs;

    for (int ch = start; ch < end; ch++) {
        DeNoiseChannel *dnch = &s->dnch[ch];
        const float *src = (const float *)in->extended_data[ch];
        double *dst = dnch->out_samples;

        if (s->track_noise) {
            int i = s->block_count & 0x1FF;

            if (dnch->sfm_fail_flags[i])
                dnch->sfm_fail_total--;
            dnch->sfm_fail_flags[i] = 0;
            dnch->sfm_threshold *= 1.0 - dnch->sfm_alpha;
            dnch->sfm_threshold += dnch->sfm_alpha * (0.5 + (1.0 / 640) * dnch->sfm_fail_total);
        }

        for (int m = 0; m < s->window_length; m++) {
            dnch->fft_data[m].re = s->window[m] * src[m] * (1LL << 24);
            dnch->fft_data[m].im = 0;
        }

        for (int m = s->window_length; m < s->fft_length2; m++) {
            dnch->fft_data[m].re = 0;
            dnch->fft_data[m].im = 0;
        }

        av_fft_permute(dnch->fft, dnch->fft_data);
        av_fft_calc(dnch->fft, dnch->fft_data);

        preprocess(dnch->fft_data, s->fft_length);
        process_frame(s, dnch, dnch->fft_data,
                      dnch->prior,
                      dnch->prior_band_excit,
                      s->track_noise);
        postprocess(dnch->fft_data, s->fft_length);

        av_fft_permute(dnch->ifft, dnch->fft_data);
        av_fft_calc(dnch->ifft, dnch->fft_data);

        for (int m = 0; m < s->window_length; m++)
            dst[m] += s->window[m] * dnch->fft_data[m].re / (1LL << 24);
    }

    return 0;
}

static void get_auto_noise_levels(AudioFFTDeNoiseContext *s,
                                  DeNoiseChannel *dnch,
                                  double *levels)
{
    if (s->noise_band_count > 0) {
        for (int i = 0; i < s->noise_band_count; i++) {
            levels[i] = (1.0 / C) * log(dnch->noise_band_auto_var[i] / s->floor) - 100.0;
        }
        if (s->noise_band_count < 15) {
            for (int i = s->noise_band_count; i < 15; i++)
                levels[i] = levels[i - 1];
        }
    } else {
        for (int i = 0; i < 15; i++) {
            levels[i] = -100.0;
        }
    }
}

static int output_frame(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    AudioFFTDeNoiseContext *s = ctx->priv;
    AVFrame *out = NULL, *in = NULL;
    ThreadData td;
    int ret = 0;

    in = ff_get_audio_buffer(outlink, s->window_length);
    if (!in)
        return AVERROR(ENOMEM);

    ret = av_audio_fifo_peek(s->fifo, (void **)in->extended_data, s->window_length);
    if (ret < 0)
        goto end;

    if (s->track_noise) {
        for (int ch = 0; ch < inlink->channels; ch++) {
            DeNoiseChannel *dnch = &s->dnch[ch];
            double levels[15];

            get_auto_noise_levels(s, dnch, levels);
            set_noise_profile(s, dnch, levels, 0);
        }

        if (s->noise_floor != s->last_noise_floor)
            set_parameters(s);
    }

    if (s->sample_noise_start) {
        for (int ch = 0; ch < inlink->channels; ch++) {
            DeNoiseChannel *dnch = &s->dnch[ch];

            init_sample_noise(dnch);
        }
        s->sample_noise_start = 0;
        s->sample_noise = 1;
    }

    if (s->sample_noise) {
        for (int ch = 0; ch < inlink->channels; ch++) {
            DeNoiseChannel *dnch = &s->dnch[ch];

            sample_noise_block(s, dnch, in, ch);
        }
    }

    if (s->sample_noise_end) {
        for (int ch = 0; ch < inlink->channels; ch++) {
            DeNoiseChannel *dnch = &s->dnch[ch];
            double sample_noise[15];

            finish_sample_noise(s, dnch, sample_noise);
            set_noise_profile(s, dnch, sample_noise, 1);
            set_band_parameters(s, dnch);
        }
        s->sample_noise = 0;
        s->sample_noise_end = 0;
    }

    s->block_count++;
    td.in = in;
    ctx->internal->execute(ctx, filter_channel, &td, NULL,
                           FFMIN(outlink->channels, ff_filter_get_nb_threads(ctx)));

    out = ff_get_audio_buffer(outlink, s->sample_advance);
    if (!out) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    for (int ch = 0; ch < inlink->channels; ch++) {
        DeNoiseChannel *dnch = &s->dnch[ch];
        double *src = dnch->out_samples;
        float *orig = (float *)in->extended_data[ch];
        float *dst = (float *)out->extended_data[ch];

        switch (s->output_mode) {
        case IN_MODE:
            for (int m = 0; m < s->sample_advance; m++)
                dst[m] = orig[m];
            break;
        case OUT_MODE:
            for (int m = 0; m < s->sample_advance; m++)
                dst[m] = src[m];
            break;
        case NOISE_MODE:
            for (int m = 0; m < s->sample_advance; m++)
                dst[m] = orig[m] - src[m];
            break;
        default:
            av_frame_free(&out);
            ret = AVERROR_BUG;
            goto end;
        }
        memmove(src, src + s->sample_advance, (s->window_length - s->sample_advance) * sizeof(*src));
        memset(src + (s->window_length - s->sample_advance), 0, s->sample_advance * sizeof(*src));
    }

    av_audio_fifo_drain(s->fifo, s->sample_advance);

    out->pts = s->pts;
    ret = ff_filter_frame(outlink, out);
    if (ret < 0)
        goto end;
    s->pts += s->sample_advance;
end:
    av_frame_free(&in);

    return ret;
}

static int activate(AVFilterContext *ctx)
{
    AVFilterLink *inlink = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];
    AudioFFTDeNoiseContext *s = ctx->priv;
    AVFrame *frame = NULL;
    int ret;

    FF_FILTER_FORWARD_STATUS_BACK(outlink, inlink);

    ret = ff_inlink_consume_frame(inlink, &frame);
    if (ret < 0)
        return ret;

    if (ret > 0) {
        if (s->pts == AV_NOPTS_VALUE)
            s->pts = frame->pts;

        ret = av_audio_fifo_write(s->fifo, (void **)frame->extended_data, frame->nb_samples);
        av_frame_free(&frame);
        if (ret < 0)
            return ret;
    }

    if (av_audio_fifo_size(s->fifo) >= s->window_length)
        return output_frame(inlink);

    FF_FILTER_FORWARD_STATUS(inlink, outlink);
    if (ff_outlink_frame_wanted(outlink) &&
        av_audio_fifo_size(s->fifo) < s->window_length) {
        ff_inlink_request_frame(inlink);
        return 0;
    }

    return FFERROR_NOT_READY;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    AudioFFTDeNoiseContext *s = ctx->priv;

    av_freep(&s->window);
    av_freep(&s->bin2band);
    av_freep(&s->band_alpha);
    av_freep(&s->band_beta);

    if (s->dnch) {
        for (int ch = 0; ch < s->channels; ch++) {
            DeNoiseChannel *dnch = &s->dnch[ch];
            av_freep(&dnch->amt);
            av_freep(&dnch->band_amt);
            av_freep(&dnch->band_excit);
            av_freep(&dnch->gain);
            av_freep(&dnch->prior);
            av_freep(&dnch->prior_band_excit);
            av_freep(&dnch->clean_data);
            av_freep(&dnch->noisy_data);
            av_freep(&dnch->out_samples);
            av_freep(&dnch->spread_function);
            av_freep(&dnch->abs_var);
            av_freep(&dnch->rel_var);
            av_freep(&dnch->min_abs_var);
            av_freep(&dnch->fft_data);
            av_fft_end(dnch->fft);
            dnch->fft = NULL;
            av_fft_end(dnch->ifft);
            dnch->ifft = NULL;
        }
        av_freep(&s->dnch);
    }

    av_audio_fifo_free(s->fifo);
}

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *formats = NULL;
    AVFilterChannelLayouts *layouts = NULL;
    static const enum AVSampleFormat sample_fmts[] = {
        AV_SAMPLE_FMT_FLTP,
        AV_SAMPLE_FMT_NONE
    };
    int ret;

    formats = ff_make_format_list(sample_fmts);
    if (!formats)
        return AVERROR(ENOMEM);
    ret = ff_set_common_formats(ctx, formats);
    if (ret < 0)
        return ret;

    layouts = ff_all_channel_counts();
    if (!layouts)
        return AVERROR(ENOMEM);

    ret = ff_set_common_channel_layouts(ctx, layouts);
    if (ret < 0)
        return ret;

    formats = ff_all_samplerates();
    return ff_set_common_samplerates(ctx, formats);
}

static int process_command(AVFilterContext *ctx, const char *cmd, const char *args,
                           char *res, int res_len, int flags)
{
    AudioFFTDeNoiseContext *s = ctx->priv;
    int need_reset = 0;

    if (!strcmp(cmd, "sample_noise") ||
        !strcmp(cmd, "sn")) {
        if (!strcmp(args, "start")) {
            s->sample_noise_start = 1;
            s->sample_noise_end = 0;
        } else if (!strcmp(args, "end") ||
                   !strcmp(args, "stop")) {
            s->sample_noise_start = 0;
            s->sample_noise_end = 1;
        }
    } else if (!strcmp(cmd, "nr") ||
               !strcmp(cmd, "noise_reduction")) {
        float nr;

        if (sscanf(args, "%f", &nr) == 1) {
            s->noise_reduction = av_clipf(nr, 0.01, 97);
            need_reset = 1;
        }
    } else if (!strcmp(cmd, "nf") ||
               !strcmp(cmd, "noise_floor")) {
        float nf;

        if (sscanf(args, "%f", &nf) == 1) {
            s->noise_floor = av_clipf(nf, -80, -20);
            need_reset = 1;
        }
    } else if (!strcmp(cmd, "output_mode") ||
               !strcmp(cmd, "om")) {
        if (!strcmp(args, "i")) {
            s->output_mode = IN_MODE;
        } else if (!strcmp(args, "o")) {
            s->output_mode = OUT_MODE;
        } else if (!strcmp(args, "n")) {
            s->output_mode = NOISE_MODE;
        }
    }

    if (need_reset)
        set_parameters(s);

    return 0;
}

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .config_props = config_input,
    },
    { NULL }
};

static const AVFilterPad outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_AUDIO,
    },
    { NULL }
};

AVFilter ff_af_afftdn = {
    .name            = "afftdn",
    .description     = NULL_IF_CONFIG_SMALL("Denoise audio samples using FFT."),
    .query_formats   = query_formats,
    .priv_size       = sizeof(AudioFFTDeNoiseContext),
    .priv_class      = &afftdn_class,
    .activate        = activate,
    .uninit          = uninit,
    .inputs          = inputs,
    .outputs         = outputs,
    .process_command = process_command,
    .flags           = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC |
                       AVFILTER_FLAG_SLICE_THREADS,
};
