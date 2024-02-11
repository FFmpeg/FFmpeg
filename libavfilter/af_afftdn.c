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

#include "libavutil/avstring.h"
#include "libavutil/channel_layout.h"
#include "libavutil/opt.h"
#include "libavutil/tx.h"
#include "avfilter.h"
#include "audio.h"
#include "filters.h"

#define C       (M_LN10 * 0.1)
#define SOLVE_SIZE (5)
#define NB_PROFILE_BANDS (15)

enum SampleNoiseModes {
    SAMPLE_NONE,
    SAMPLE_START,
    SAMPLE_STOP,
    NB_SAMPLEMODES
};

enum OutModes {
    IN_MODE,
    OUT_MODE,
    NOISE_MODE,
    NB_MODES
};

enum NoiseLinkType {
    NONE_LINK,
    MIN_LINK,
    MAX_LINK,
    AVERAGE_LINK,
    NB_LINK
};

enum NoiseType {
    WHITE_NOISE,
    VINYL_NOISE,
    SHELLAC_NOISE,
    CUSTOM_NOISE,
    NB_NOISE
};

typedef struct DeNoiseChannel {
    double      band_noise[NB_PROFILE_BANDS];
    double      noise_band_auto_var[NB_PROFILE_BANDS];
    double      noise_band_sample[NB_PROFILE_BANDS];
    double     *amt;
    double     *band_amt;
    double     *band_excit;
    double     *gain;
    double     *smoothed_gain;
    double     *prior;
    double     *prior_band_excit;
    double     *clean_data;
    double     *noisy_data;
    double     *out_samples;
    double     *spread_function;
    double     *abs_var;
    double     *rel_var;
    double     *min_abs_var;
    void       *fft_in;
    void       *fft_out;
    AVTXContext *fft, *ifft;
    av_tx_fn   tx_fn, itx_fn;

    double      noise_band_norm[NB_PROFILE_BANDS];
    double      noise_band_avr[NB_PROFILE_BANDS];
    double      noise_band_avi[NB_PROFILE_BANDS];
    double      noise_band_var[NB_PROFILE_BANDS];

    double      noise_reduction;
    double      last_noise_reduction;
    double      noise_floor;
    double      last_noise_floor;
    double      residual_floor;
    double      last_residual_floor;
    double      max_gain;
    double      max_var;
    double      gain_scale;
} DeNoiseChannel;

typedef struct AudioFFTDeNoiseContext {
    const AVClass *class;

    int     format;
    size_t  sample_size;
    size_t  complex_sample_size;

    float   noise_reduction;
    float   noise_floor;
    int     noise_type;
    char   *band_noise_str;
    float   residual_floor;
    int     track_noise;
    int     track_residual;
    int     output_mode;
    int     noise_floor_link;
    float   ratio;
    int     gain_smooth;
    float   band_multiplier;
    float   floor_offset;

    int     channels;
    int     sample_noise;
    int     sample_noise_blocks;
    int     sample_noise_mode;
    float   sample_rate;
    int     buffer_length;
    int     fft_length;
    int     fft_length2;
    int     bin_count;
    int     window_length;
    int     sample_advance;
    int     number_of_bands;

    int     band_centre[NB_PROFILE_BANDS];

    int    *bin2band;
    double *window;
    double *band_alpha;
    double *band_beta;

    DeNoiseChannel *dnch;

    AVFrame *winframe;

    double  window_weight;
    double  floor;
    double  sample_floor;

    int     noise_band_edge[NB_PROFILE_BANDS + 2];
    int     noise_band_count;
    double  matrix_a[SOLVE_SIZE * SOLVE_SIZE];
    double  vector_b[SOLVE_SIZE];
    double  matrix_b[SOLVE_SIZE * NB_PROFILE_BANDS];
    double  matrix_c[SOLVE_SIZE * NB_PROFILE_BANDS];
} AudioFFTDeNoiseContext;

#define OFFSET(x) offsetof(AudioFFTDeNoiseContext, x)
#define AF  AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM
#define AFR AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption afftdn_options[] = {
    { "noise_reduction", "set the noise reduction",OFFSET(noise_reduction), AV_OPT_TYPE_FLOAT,{.dbl = 12},   .01, 97, AFR },
    { "nr", "set the noise reduction",    OFFSET(noise_reduction), AV_OPT_TYPE_FLOAT,  {.dbl = 12},          .01, 97, AFR },
    { "noise_floor", "set the noise floor",OFFSET(noise_floor),    AV_OPT_TYPE_FLOAT,  {.dbl =-50},          -80,-20, AFR },
    { "nf", "set the noise floor",        OFFSET(noise_floor),     AV_OPT_TYPE_FLOAT,  {.dbl =-50},          -80,-20, AFR },
    { "noise_type", "set the noise type", OFFSET(noise_type),      AV_OPT_TYPE_INT,    {.i64 = WHITE_NOISE}, WHITE_NOISE, NB_NOISE-1, AF, .unit = "type" },
    { "nt", "set the noise type",         OFFSET(noise_type),      AV_OPT_TYPE_INT,    {.i64 = WHITE_NOISE}, WHITE_NOISE, NB_NOISE-1, AF, .unit = "type" },
    {  "white", "white noise",            0,                       AV_OPT_TYPE_CONST,  {.i64 = WHITE_NOISE},   0,  0, AF, .unit = "type" },
    {  "w", "white noise",                0,                       AV_OPT_TYPE_CONST,  {.i64 = WHITE_NOISE},   0,  0, AF, .unit = "type" },
    {  "vinyl", "vinyl noise",            0,                       AV_OPT_TYPE_CONST,  {.i64 = VINYL_NOISE},   0,  0, AF, .unit = "type" },
    {  "v", "vinyl noise",                0,                       AV_OPT_TYPE_CONST,  {.i64 = VINYL_NOISE},   0,  0, AF, .unit = "type" },
    {  "shellac", "shellac noise",        0,                       AV_OPT_TYPE_CONST,  {.i64 = SHELLAC_NOISE}, 0,  0, AF, .unit = "type" },
    {  "s", "shellac noise",              0,                       AV_OPT_TYPE_CONST,  {.i64 = SHELLAC_NOISE}, 0,  0, AF, .unit = "type" },
    {  "custom", "custom noise",          0,                       AV_OPT_TYPE_CONST,  {.i64 = CUSTOM_NOISE},  0,  0, AF, .unit = "type" },
    {  "c", "custom noise",               0,                       AV_OPT_TYPE_CONST,  {.i64 = CUSTOM_NOISE},  0,  0, AF, .unit = "type" },
    { "band_noise", "set the custom bands noise", OFFSET(band_noise_str),  AV_OPT_TYPE_STRING, {.str = 0},     0,  0, AF },
    { "bn", "set the custom bands noise", OFFSET(band_noise_str),  AV_OPT_TYPE_STRING, {.str = 0},             0,  0, AF },
    { "residual_floor", "set the residual floor",OFFSET(residual_floor),  AV_OPT_TYPE_FLOAT, {.dbl =-38},    -80,-20, AFR },
    { "rf", "set the residual floor",     OFFSET(residual_floor),  AV_OPT_TYPE_FLOAT,  {.dbl =-38},          -80,-20, AFR },
    { "track_noise", "track noise",       OFFSET(track_noise),     AV_OPT_TYPE_BOOL,   {.i64 =  0},            0,  1, AFR },
    { "tn", "track noise",                OFFSET(track_noise),     AV_OPT_TYPE_BOOL,   {.i64 =  0},            0,  1, AFR },
    { "track_residual", "track residual", OFFSET(track_residual),  AV_OPT_TYPE_BOOL,   {.i64 =  0},            0,  1, AFR },
    { "tr", "track residual",             OFFSET(track_residual),  AV_OPT_TYPE_BOOL,   {.i64 =  0},            0,  1, AFR },
    { "output_mode", "set output mode",   OFFSET(output_mode),     AV_OPT_TYPE_INT,    {.i64 = OUT_MODE},      0,  NB_MODES-1, AFR, .unit = "mode" },
    { "om", "set output mode",            OFFSET(output_mode),     AV_OPT_TYPE_INT,    {.i64 = OUT_MODE},      0,  NB_MODES-1, AFR, .unit = "mode" },
    {  "input", "input",                  0,                       AV_OPT_TYPE_CONST,  {.i64 = IN_MODE},       0,  0, AFR, .unit = "mode" },
    {  "i", "input",                      0,                       AV_OPT_TYPE_CONST,  {.i64 = IN_MODE},       0,  0, AFR, .unit = "mode" },
    {  "output", "output",                0,                       AV_OPT_TYPE_CONST,  {.i64 = OUT_MODE},      0,  0, AFR, .unit = "mode" },
    {  "o", "output",                     0,                       AV_OPT_TYPE_CONST,  {.i64 = OUT_MODE},      0,  0, AFR, .unit = "mode" },
    {  "noise", "noise",                  0,                       AV_OPT_TYPE_CONST,  {.i64 = NOISE_MODE},    0,  0, AFR, .unit = "mode" },
    {  "n", "noise",                      0,                       AV_OPT_TYPE_CONST,  {.i64 = NOISE_MODE},    0,  0, AFR, .unit = "mode" },
    { "adaptivity", "set adaptivity factor",OFFSET(ratio),         AV_OPT_TYPE_FLOAT,  {.dbl = 0.5},           0,  1, AFR },
    { "ad",         "set adaptivity factor",OFFSET(ratio),         AV_OPT_TYPE_FLOAT,  {.dbl = 0.5},           0,  1, AFR },
    { "floor_offset", "set noise floor offset factor",OFFSET(floor_offset), AV_OPT_TYPE_FLOAT, {.dbl = 1.0},  -2,  2, AFR },
    { "fo",           "set noise floor offset factor",OFFSET(floor_offset), AV_OPT_TYPE_FLOAT, {.dbl = 1.0},  -2,  2, AFR },
    { "noise_link", "set the noise floor link",OFFSET(noise_floor_link),AV_OPT_TYPE_INT,{.i64 = MIN_LINK},     0,  NB_LINK-1, AFR, .unit = "link" },
    { "nl", "set the noise floor link",        OFFSET(noise_floor_link),AV_OPT_TYPE_INT,{.i64 = MIN_LINK},     0,  NB_LINK-1, AFR, .unit = "link" },
    {  "none",    "none",                 0,                       AV_OPT_TYPE_CONST,  {.i64 = NONE_LINK},     0,  0, AFR, .unit = "link" },
    {  "min",     "min",                  0,                       AV_OPT_TYPE_CONST,  {.i64 = MIN_LINK},      0,  0, AFR, .unit = "link" },
    {  "max",     "max",                  0,                       AV_OPT_TYPE_CONST,  {.i64 = MAX_LINK},      0,  0, AFR, .unit = "link" },
    {  "average", "average",              0,                       AV_OPT_TYPE_CONST,  {.i64 = AVERAGE_LINK},  0,  0, AFR, .unit = "link" },
    { "band_multiplier", "set band multiplier",OFFSET(band_multiplier), AV_OPT_TYPE_FLOAT,{.dbl = 1.25},       0.2,5, AF  },
    { "bm",       "set band multiplier",       OFFSET(band_multiplier), AV_OPT_TYPE_FLOAT,{.dbl = 1.25},       0.2,5, AF  },
    { "sample_noise", "set sample noise mode",OFFSET(sample_noise_mode),AV_OPT_TYPE_INT,{.i64 = SAMPLE_NONE},  0,  NB_SAMPLEMODES-1, AFR, .unit = "sample" },
    { "sn",           "set sample noise mode",OFFSET(sample_noise_mode),AV_OPT_TYPE_INT,{.i64 = SAMPLE_NONE},  0,  NB_SAMPLEMODES-1, AFR, .unit = "sample" },
    {  "none",    "none",                 0,                       AV_OPT_TYPE_CONST,  {.i64 = SAMPLE_NONE},   0,  0, AFR, .unit = "sample" },
    {  "start",   "start",                0,                       AV_OPT_TYPE_CONST,  {.i64 = SAMPLE_START},  0,  0, AFR, .unit = "sample" },
    {  "begin",   "start",                0,                       AV_OPT_TYPE_CONST,  {.i64 = SAMPLE_START},  0,  0, AFR, .unit = "sample" },
    {  "stop",    "stop",                 0,                       AV_OPT_TYPE_CONST,  {.i64 = SAMPLE_STOP},   0,  0, AFR, .unit = "sample" },
    {  "end",     "stop",                 0,                       AV_OPT_TYPE_CONST,  {.i64 = SAMPLE_STOP},   0,  0, AFR, .unit = "sample" },
    { "gain_smooth", "set gain smooth radius",OFFSET(gain_smooth), AV_OPT_TYPE_INT,    {.i64 = 0},             0, 50, AFR },
    { "gs",          "set gain smooth radius",OFFSET(gain_smooth), AV_OPT_TYPE_INT,    {.i64 = 0},             0, 50, AFR },
    { NULL }
};

AVFILTER_DEFINE_CLASS(afftdn);

static double get_band_noise(AudioFFTDeNoiseContext *s,
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

    return -d1 + d2 - d3;
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

static double process_get_band_noise(AudioFFTDeNoiseContext *s,
                                     DeNoiseChannel *dnch,
                                     int band)
{
    double product, sum, f;
    int i = 0;

    if (band < NB_PROFILE_BANDS)
        return dnch->band_noise[band];

    for (int j = 0; j < SOLVE_SIZE; j++) {
        sum = 0.0;
        for (int k = 0; k < NB_PROFILE_BANDS; k++)
            sum += s->matrix_b[i++] * dnch->band_noise[k];
        s->vector_b[j] = sum;
    }

    solve(s->matrix_a, s->vector_b, SOLVE_SIZE);
    f = (0.5 * s->sample_rate) / s->band_centre[NB_PROFILE_BANDS-1];
    f = 15.0 + log(f / 1.5) / log(1.5);
    sum = 0.0;
    product = 1.0;
    for (int j = 0; j < SOLVE_SIZE; j++) {
        sum += product * s->vector_b[j];
        product *= f;
    }

    return sum;
}

static double limit_gain(double a, double b)
{
    if (a > 1.0)
        return (b * a - 1.0) / (b + a - 2.0);
    if (a < 1.0)
        return (b * a - 2.0 * a + 1.0) / (b - a);
    return 1.0;
}

static void spectral_flatness(AudioFFTDeNoiseContext *s, const double *const spectral,
                              double floor, int len, double *rnum, double *rden)
{
    double num = 0., den = 0.;
    int size = 0;

    for (int n = 0; n < len; n++) {
        const double v = spectral[n];
        if (v > floor) {
            num += log(v);
            den += v;
            size++;
        }
    }

    size = FFMAX(size, 1);

    num /= size;
    den /= size;

    num = exp(num);

    *rnum = num;
    *rden = den;
}

static void set_parameters(AudioFFTDeNoiseContext *s, DeNoiseChannel *dnch, int update_var, int update_auto_var);

static double floor_offset(const double *S, int size, double mean)
{
    double offset = 0.0;

    for (int n = 0; n < size; n++) {
        const double p = S[n] - mean;

        offset = fmax(offset, fabs(p));
    }

    return offset / mean;
}

static void process_frame(AVFilterContext *ctx,
                          AudioFFTDeNoiseContext *s, DeNoiseChannel *dnch,
                          double *prior, double *prior_band_excit, int track_noise)
{
    AVFilterLink *outlink = ctx->outputs[0];
    const double *abs_var = dnch->abs_var;
    const double ratio = outlink->frame_count_out ? s->ratio : 1.0;
    const double rratio = 1. - ratio;
    const int *bin2band = s->bin2band;
    double *noisy_data = dnch->noisy_data;
    double *band_excit = dnch->band_excit;
    double *band_amt = dnch->band_amt;
    double *smoothed_gain = dnch->smoothed_gain;
    AVComplexDouble *fft_data_dbl = dnch->fft_out;
    AVComplexFloat *fft_data_flt = dnch->fft_out;
    double *gain = dnch->gain;

    for (int i = 0; i < s->bin_count; i++) {
        double sqr_new_gain, new_gain, power, mag, mag_abs_var, new_mag_abs_var;

        switch (s->format) {
        case AV_SAMPLE_FMT_FLTP:
            noisy_data[i] = mag = hypot(fft_data_flt[i].re, fft_data_flt[i].im);
            break;
        case AV_SAMPLE_FMT_DBLP:
            noisy_data[i] = mag = hypot(fft_data_dbl[i].re, fft_data_dbl[i].im);
            break;
        }

        power = mag * mag;
        mag_abs_var = power / abs_var[i];
        new_mag_abs_var = ratio * prior[i] + rratio * fmax(mag_abs_var - 1.0, 0.0);
        new_gain = new_mag_abs_var / (1.0 + new_mag_abs_var);
        sqr_new_gain = new_gain * new_gain;
        prior[i] = mag_abs_var * sqr_new_gain;
        dnch->clean_data[i] = power * sqr_new_gain;
        gain[i] = new_gain;
    }

    if (track_noise) {
        double flatness, num, den;

        spectral_flatness(s, noisy_data, s->floor, s->bin_count, &num, &den);

        flatness = num / den;
        if (flatness > 0.8) {
            const double offset = s->floor_offset * floor_offset(noisy_data, s->bin_count, den);
            const double new_floor = av_clipd(10.0 * log10(den) - 100.0 + offset, -90., -20.);

            dnch->noise_floor = 0.1 * new_floor + dnch->noise_floor * 0.9;
            set_parameters(s, dnch, 1, 1);
        }
    }

    for (int i = 0; i < s->number_of_bands; i++) {
        band_excit[i] = 0.0;
        band_amt[i] = 0.0;
    }

    for (int i = 0; i < s->bin_count; i++)
        band_excit[bin2band[i]] += dnch->clean_data[i];

    for (int i = 0; i < s->number_of_bands; i++) {
        band_excit[i] = fmax(band_excit[i],
                             s->band_alpha[i] * band_excit[i] +
                             s->band_beta[i] * prior_band_excit[i]);
        prior_band_excit[i] = band_excit[i];
    }

    for (int j = 0, i = 0; j < s->number_of_bands; j++) {
        for (int k = 0; k < s->number_of_bands; k++) {
            band_amt[j] += dnch->spread_function[i++] * band_excit[k];
        }
    }

    for (int i = 0; i < s->bin_count; i++)
        dnch->amt[i] = band_amt[bin2band[i]];

    for (int i = 0; i < s->bin_count; i++) {
        if (dnch->amt[i] > abs_var[i]) {
            gain[i] = 1.0;
        } else if (dnch->amt[i] > dnch->min_abs_var[i]) {
            const double limit = sqrt(abs_var[i] / dnch->amt[i]);

            gain[i] = limit_gain(gain[i], limit);
        } else {
            gain[i] = limit_gain(gain[i], dnch->max_gain);
        }
    }

    memcpy(smoothed_gain, gain, s->bin_count * sizeof(*smoothed_gain));
    if (s->gain_smooth > 0) {
        const int r = s->gain_smooth;

        for (int i = r; i < s->bin_count - r; i++) {
            const double gc = gain[i];
            double num = 0., den = 0.;

            for (int j = -r; j <= r; j++) {
                const double g = gain[i + j];
                const double d = 1. - fabs(g - gc);

                num += g * d;
                den += d;
            }

            smoothed_gain[i] = num / den;
        }
    }

    switch (s->format) {
    case AV_SAMPLE_FMT_FLTP:
        for (int i = 0; i < s->bin_count; i++) {
            const float new_gain = smoothed_gain[i];

            fft_data_flt[i].re *= new_gain;
            fft_data_flt[i].im *= new_gain;
        }
        break;
    case AV_SAMPLE_FMT_DBLP:
        for (int i = 0; i < s->bin_count; i++) {
            const double new_gain = smoothed_gain[i];

            fft_data_dbl[i].re *= new_gain;
            fft_data_dbl[i].im *= new_gain;
        }
        break;
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

    if (band == NB_PROFILE_BANDS) {
        i = lrint(s->band_centre[NB_PROFILE_BANDS - 1] * 1.224745);
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
    for (int m = j; m < s->bin_count; m++) {
        if (m == j) {
            i = j;
            d5 = band_noise;
            if (k >= NB_PROFILE_BANDS) {
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

    for (i = 0; i < NB_PROFILE_BANDS; i++)
        dnch->noise_band_auto_var[i] = dnch->max_var * exp((process_get_band_noise(s, dnch, i) - 2.0) * C);
}

static void read_custom_noise(AudioFFTDeNoiseContext *s, int ch)
{
    DeNoiseChannel *dnch = &s->dnch[ch];
    char *custom_noise_str, *p, *arg, *saveptr = NULL;
    double band_noise[NB_PROFILE_BANDS] = { 0.f };
    int ret;

    if (!s->band_noise_str)
        return;

    custom_noise_str = p = av_strdup(s->band_noise_str);
    if (!p)
        return;

    for (int i = 0; i < NB_PROFILE_BANDS; i++) {
        float noise;

        if (!(arg = av_strtok(p, "| ", &saveptr)))
            break;

        p = NULL;

        ret = av_sscanf(arg, "%f", &noise);
        if (ret != 1) {
            av_log(s, AV_LOG_ERROR, "Custom band noise must be float.\n");
            break;
        }

        band_noise[i] = av_clipd(noise, -24., 24.);
    }

    av_free(custom_noise_str);
    memcpy(dnch->band_noise, band_noise, sizeof(band_noise));
}

static void set_parameters(AudioFFTDeNoiseContext *s, DeNoiseChannel *dnch, int update_var, int update_auto_var)
{
    if (dnch->last_noise_floor != dnch->noise_floor)
        dnch->last_noise_floor = dnch->noise_floor;

    if (s->track_residual)
        dnch->last_noise_floor = fmax(dnch->last_noise_floor, dnch->residual_floor);

    dnch->max_var = s->floor * exp((100.0 + dnch->last_noise_floor) * C);
    if (update_auto_var) {
        for (int i = 0; i < NB_PROFILE_BANDS; i++)
            dnch->noise_band_auto_var[i] = dnch->max_var * exp((process_get_band_noise(s, dnch, i) - 2.0) * C);
    }

    if (s->track_residual) {
        if (update_var || dnch->last_residual_floor != dnch->residual_floor) {
            update_var = 1;
            dnch->last_residual_floor = dnch->residual_floor;
            dnch->last_noise_reduction = fmax(dnch->last_noise_floor - dnch->last_residual_floor + 100., 0);
            dnch->max_gain = exp(dnch->last_noise_reduction * (0.5 * C));
        }
    } else if (update_var || dnch->noise_reduction != dnch->last_noise_reduction) {
        update_var = 1;
        dnch->last_noise_reduction = dnch->noise_reduction;
        dnch->last_residual_floor = av_clipd(dnch->last_noise_floor - dnch->last_noise_reduction, -80, -20);
        dnch->max_gain = exp(dnch->last_noise_reduction * (0.5 * C));
    }

    dnch->gain_scale = 1.0 / (dnch->max_gain * dnch->max_gain);

    if (update_var) {
        set_band_parameters(s, dnch);

        for (int i = 0; i < s->bin_count; i++) {
            dnch->abs_var[i] = fmax(dnch->max_var * dnch->rel_var[i], 1.0);
            dnch->min_abs_var[i] = dnch->gain_scale * dnch->abs_var[i];
        }
    }
}

static void reduce_mean(double *band_noise)
{
    double mean = 0.f;

    for (int i = 0; i < NB_PROFILE_BANDS; i++)
        mean += band_noise[i];
    mean /= NB_PROFILE_BANDS;

    for (int i = 0; i < NB_PROFILE_BANDS; i++)
        band_noise[i] -= mean;
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    AudioFFTDeNoiseContext *s = ctx->priv;
    double wscale, sar, sum, sdiv;
    int i, j, k, m, n, ret, tx_type;
    double dscale = 1.;
    float fscale = 1.f;
    void *scale;

    s->format = inlink->format;

    switch (s->format) {
    case AV_SAMPLE_FMT_FLTP:
        s->sample_size = sizeof(float);
        s->complex_sample_size = sizeof(AVComplexFloat);
        tx_type = AV_TX_FLOAT_RDFT;
        scale = &fscale;
        break;
    case AV_SAMPLE_FMT_DBLP:
        s->sample_size = sizeof(double);
        s->complex_sample_size = sizeof(AVComplexDouble);
        tx_type = AV_TX_DOUBLE_RDFT;
        scale = &dscale;
        break;
    }

    s->dnch = av_calloc(inlink->ch_layout.nb_channels, sizeof(*s->dnch));
    if (!s->dnch)
        return AVERROR(ENOMEM);

    s->channels = inlink->ch_layout.nb_channels;
    s->sample_rate = inlink->sample_rate;
    s->sample_advance = s->sample_rate / 80;
    s->window_length = 3 * s->sample_advance;
    s->fft_length2 = 1 << (32 - ff_clz(s->window_length));
    s->fft_length = s->fft_length2;
    s->buffer_length = s->fft_length * 2;
    s->bin_count = s->fft_length2 / 2 + 1;

    s->band_centre[0] = 80;
    for (i = 1; i < NB_PROFILE_BANDS; i++) {
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

    for (j = 0; j < SOLVE_SIZE; j++) {
        for (k = 0; k < SOLVE_SIZE; k++) {
            s->matrix_a[j + k * SOLVE_SIZE] = 0.0;
            for (m = 0; m < NB_PROFILE_BANDS; m++)
                s->matrix_a[j + k * SOLVE_SIZE] += pow(m, j + k);
        }
    }

    factor(s->matrix_a, SOLVE_SIZE);

    i = 0;
    for (j = 0; j < SOLVE_SIZE; j++)
        for (k = 0; k < NB_PROFILE_BANDS; k++)
            s->matrix_b[i++] = pow(k, j);

    i = 0;
    for (j = 0; j < NB_PROFILE_BANDS; j++)
        for (k = 0; k < SOLVE_SIZE; k++)
            s->matrix_c[i++] = pow(j, k);

    s->window = av_calloc(s->window_length, sizeof(*s->window));
    s->bin2band = av_calloc(s->bin_count, sizeof(*s->bin2band));
    if (!s->window || !s->bin2band)
        return AVERROR(ENOMEM);

    sdiv = s->band_multiplier;
    for (i = 0; i < s->bin_count; i++)
        s->bin2band[i] = lrint(sdiv * freq2bark((0.5 * i * s->sample_rate) / s->fft_length2));

    s->number_of_bands = s->bin2band[s->bin_count - 1] + 1;

    s->band_alpha = av_calloc(s->number_of_bands, sizeof(*s->band_alpha));
    s->band_beta = av_calloc(s->number_of_bands, sizeof(*s->band_beta));
    if (!s->band_alpha || !s->band_beta)
        return AVERROR(ENOMEM);

    for (int ch = 0; ch < inlink->ch_layout.nb_channels; ch++) {
        DeNoiseChannel *dnch = &s->dnch[ch];

        switch (s->noise_type) {
        case WHITE_NOISE:
            for (i = 0; i < NB_PROFILE_BANDS; i++)
                dnch->band_noise[i] = 0.;
            break;
        case VINYL_NOISE:
            for (i = 0; i < NB_PROFILE_BANDS; i++)
                dnch->band_noise[i] = get_band_noise(s, i, 50.0, 500.5, 2125.0);
            break;
        case SHELLAC_NOISE:
            for (i = 0; i < NB_PROFILE_BANDS; i++)
                dnch->band_noise[i] = get_band_noise(s, i, 1.0, 500.0, 1.0E10);
            break;
        case CUSTOM_NOISE:
            read_custom_noise(s, ch);
            break;
        default:
            return AVERROR_BUG;
        }

        reduce_mean(dnch->band_noise);

        dnch->amt = av_calloc(s->bin_count, sizeof(*dnch->amt));
        dnch->band_amt = av_calloc(s->number_of_bands, sizeof(*dnch->band_amt));
        dnch->band_excit = av_calloc(s->number_of_bands, sizeof(*dnch->band_excit));
        dnch->gain = av_calloc(s->bin_count, sizeof(*dnch->gain));
        dnch->smoothed_gain = av_calloc(s->bin_count, sizeof(*dnch->smoothed_gain));
        dnch->prior = av_calloc(s->bin_count, sizeof(*dnch->prior));
        dnch->prior_band_excit = av_calloc(s->number_of_bands, sizeof(*dnch->prior_band_excit));
        dnch->clean_data = av_calloc(s->bin_count, sizeof(*dnch->clean_data));
        dnch->noisy_data = av_calloc(s->bin_count, sizeof(*dnch->noisy_data));
        dnch->out_samples = av_calloc(s->buffer_length, sizeof(*dnch->out_samples));
        dnch->abs_var = av_calloc(s->bin_count, sizeof(*dnch->abs_var));
        dnch->rel_var = av_calloc(s->bin_count, sizeof(*dnch->rel_var));
        dnch->min_abs_var = av_calloc(s->bin_count, sizeof(*dnch->min_abs_var));
        dnch->fft_in = av_calloc(s->fft_length2, s->sample_size);
        dnch->fft_out = av_calloc(s->fft_length2 + 1, s->complex_sample_size);
        ret = av_tx_init(&dnch->fft, &dnch->tx_fn, tx_type, 0, s->fft_length2, scale, 0);
        if (ret < 0)
            return ret;
        ret = av_tx_init(&dnch->ifft, &dnch->itx_fn, tx_type, 1, s->fft_length2, scale, 0);
        if (ret < 0)
            return ret;
        dnch->spread_function = av_calloc(s->number_of_bands * s->number_of_bands,
                                          sizeof(*dnch->spread_function));

        if (!dnch->amt ||
            !dnch->band_amt ||
            !dnch->band_excit ||
            !dnch->gain ||
            !dnch->smoothed_gain ||
            !dnch->prior ||
            !dnch->prior_band_excit ||
            !dnch->clean_data ||
            !dnch->noisy_data ||
            !dnch->out_samples ||
            !dnch->fft_in ||
            !dnch->fft_out ||
            !dnch->abs_var ||
            !dnch->rel_var ||
            !dnch->min_abs_var ||
            !dnch->spread_function ||
            !dnch->fft ||
            !dnch->ifft)
            return AVERROR(ENOMEM);
    }

    for (int ch = 0; ch < inlink->ch_layout.nb_channels; ch++) {
        DeNoiseChannel *dnch = &s->dnch[ch];
        double *prior_band_excit = dnch->prior_band_excit;
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

        for (m = 0; m < s->bin_count; m++)
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

        for (int i = 0; i < s->buffer_length; i++)
            dnch->out_samples[i] = 0;

        j = 0;
        for (int i = 0; i < s->number_of_bands; i++)
            for (int k = 0; k < s->number_of_bands; k++)
                dnch->spread_function[j++] *= dnch->band_excit[i] / prior_band_excit[i];
    }

    j = 0;
    sar = s->sample_advance / s->sample_rate;
    for (int i = 0; i < s->bin_count; i++) {
        if ((i == s->fft_length2) || (s->bin2band[i] > j)) {
            double d6 = (i - 1) * s->sample_rate / s->fft_length;
            double d7 = fmin(0.008 + 2.2 / d6, 0.03);
            s->band_alpha[j] = exp(-sar / d7);
            s->band_beta[j] = 1.0 - s->band_alpha[j];
            j = s->bin2band[i];
        }
    }

    s->winframe = ff_get_audio_buffer(inlink, s->window_length);
    if (!s->winframe)
        return AVERROR(ENOMEM);

    wscale = sqrt(8.0 / (9.0 * s->fft_length));
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

    for (int ch = 0; ch < inlink->ch_layout.nb_channels; ch++) {
        DeNoiseChannel *dnch = &s->dnch[ch];

        dnch->noise_reduction = s->noise_reduction;
        dnch->noise_floor     = s->noise_floor;
        dnch->residual_floor  = s->residual_floor;

        set_parameters(s, dnch, 1, 1);
    }

    s->noise_band_edge[0] = FFMIN(s->fft_length2, s->fft_length * get_band_edge(s, 0) / s->sample_rate);
    i = 0;
    for (int j = 1; j < NB_PROFILE_BANDS + 1; j++) {
        s->noise_band_edge[j] = FFMIN(s->fft_length2, s->fft_length * get_band_edge(s, j) / s->sample_rate);
        if (s->noise_band_edge[j] > lrint(1.1 * s->noise_band_edge[j - 1]))
            i++;
        s->noise_band_edge[NB_PROFILE_BANDS + 1] = i;
    }
    s->noise_band_count = s->noise_band_edge[NB_PROFILE_BANDS + 1];

    return 0;
}

static void init_sample_noise(DeNoiseChannel *dnch)
{
    for (int i = 0; i < NB_PROFILE_BANDS; i++) {
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
    double *src_dbl = (double *)in->extended_data[ch];
    float *src_flt = (float *)in->extended_data[ch];
    double mag2, var = 0.0, avr = 0.0, avi = 0.0;
    AVComplexDouble *fft_out_dbl = dnch->fft_out;
    AVComplexFloat *fft_out_flt = dnch->fft_out;
    double *fft_in_dbl = dnch->fft_in;
    float *fft_in_flt = dnch->fft_in;
    int edge, j, k, n, edgemax;

    switch (s->format) {
    case AV_SAMPLE_FMT_FLTP:
        for (int i = 0; i < s->window_length; i++)
            fft_in_flt[i] = s->window[i] * src_flt[i] * (1LL << 23);

        for (int i = s->window_length; i < s->fft_length2; i++)
            fft_in_flt[i] = 0.f;
        break;
    case AV_SAMPLE_FMT_DBLP:
        for (int i = 0; i < s->window_length; i++)
            fft_in_dbl[i] = s->window[i] * src_dbl[i] * (1LL << 23);

        for (int i = s->window_length; i < s->fft_length2; i++)
            fft_in_dbl[i] = 0.;
        break;
    }

    dnch->tx_fn(dnch->fft, dnch->fft_out, dnch->fft_in, s->sample_size);

    edge = s->noise_band_edge[0];
    j = edge;
    k = 0;
    n = j;
    edgemax = fmin(s->fft_length2, s->noise_band_edge[NB_PROFILE_BANDS]);
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
            if (k == NB_PROFILE_BANDS) {
                j++;
            }
            var = 0.0;
            avr = 0.0;
            avi = 0.0;
        }

        switch (s->format) {
        case AV_SAMPLE_FMT_FLTP:
            avr += fft_out_flt[n].re;
            avi += fft_out_flt[n].im;
            mag2 = fft_out_flt[n].re * fft_out_flt[n].re +
                   fft_out_flt[n].im * fft_out_flt[n].im;
            break;
        case AV_SAMPLE_FMT_DBLP:
            avr += fft_out_dbl[n].re;
            avi += fft_out_dbl[n].im;
            mag2 = fft_out_dbl[n].re * fft_out_dbl[n].re +
                   fft_out_dbl[n].im * fft_out_dbl[n].im;
            break;
        }

        mag2 = fmax(mag2, s->sample_floor);

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
        sample_noise[i] = 10.0 * log10(dnch->noise_band_var[i] / s->floor) - 100.0;
    }
    if (s->noise_band_count < NB_PROFILE_BANDS) {
        for (int i = s->noise_band_count; i < NB_PROFILE_BANDS; i++)
            sample_noise[i] = sample_noise[i - 1];
    }
}

static void set_noise_profile(AudioFFTDeNoiseContext *s,
                              DeNoiseChannel *dnch,
                              double *sample_noise)
{
    double new_band_noise[NB_PROFILE_BANDS];
    double temp[NB_PROFILE_BANDS];
    double sum = 0.0;

    for (int m = 0; m < NB_PROFILE_BANDS; m++)
        temp[m] = sample_noise[m];

    for (int m = 0, i = 0; m < SOLVE_SIZE; m++) {
        sum = 0.0;
        for (int n = 0; n < NB_PROFILE_BANDS; n++)
            sum += s->matrix_b[i++] * temp[n];
        s->vector_b[m] = sum;
    }
    solve(s->matrix_a, s->vector_b, SOLVE_SIZE);
    for (int m = 0, i = 0; m < NB_PROFILE_BANDS; m++) {
        sum = 0.0;
        for (int n = 0; n < SOLVE_SIZE; n++)
            sum += s->matrix_c[i++] * s->vector_b[n];
        temp[m] = sum;
    }

    reduce_mean(temp);

    av_log(s, AV_LOG_INFO, "bn=");
    for (int m = 0; m < NB_PROFILE_BANDS; m++) {
        new_band_noise[m] = temp[m];
        new_band_noise[m] = av_clipd(new_band_noise[m], -24.0, 24.0);
        av_log(s, AV_LOG_INFO, "%f ", new_band_noise[m]);
    }
    av_log(s, AV_LOG_INFO, "\n");
    memcpy(dnch->band_noise, new_band_noise, sizeof(new_band_noise));
}

static int filter_channel(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    AudioFFTDeNoiseContext *s = ctx->priv;
    AVFrame *in = arg;
    const int start = (in->ch_layout.nb_channels * jobnr) / nb_jobs;
    const int end = (in->ch_layout.nb_channels * (jobnr+1)) / nb_jobs;
    const int window_length = s->window_length;
    const double *window = s->window;

    for (int ch = start; ch < end; ch++) {
        DeNoiseChannel *dnch = &s->dnch[ch];
        const double *src_dbl = (const double *)in->extended_data[ch];
        const float *src_flt = (const float *)in->extended_data[ch];
        double *dst = dnch->out_samples;
        double *fft_in_dbl = dnch->fft_in;
        float *fft_in_flt = dnch->fft_in;

        switch (s->format) {
        case AV_SAMPLE_FMT_FLTP:
            for (int m = 0; m < window_length; m++)
                fft_in_flt[m] = window[m] * src_flt[m] * (1LL << 23);

            for (int m = window_length; m < s->fft_length2; m++)
                fft_in_flt[m] = 0.f;
            break;
        case AV_SAMPLE_FMT_DBLP:
            for (int m = 0; m < window_length; m++)
                fft_in_dbl[m] = window[m] * src_dbl[m] * (1LL << 23);

            for (int m = window_length; m < s->fft_length2; m++)
                fft_in_dbl[m] = 0.;
            break;
        }

        dnch->tx_fn(dnch->fft, dnch->fft_out, dnch->fft_in, s->sample_size);

        process_frame(ctx, s, dnch,
                      dnch->prior,
                      dnch->prior_band_excit,
                      s->track_noise);

        dnch->itx_fn(dnch->ifft, dnch->fft_in, dnch->fft_out, s->complex_sample_size);

        switch (s->format) {
        case AV_SAMPLE_FMT_FLTP:
            for (int m = 0; m < window_length; m++)
                dst[m] += s->window[m] * fft_in_flt[m] / (1LL << 23);
            break;
        case AV_SAMPLE_FMT_DBLP:
            for (int m = 0; m < window_length; m++)
                dst[m] += s->window[m] * fft_in_dbl[m] / (1LL << 23);
            break;
        }
    }

    return 0;
}

static int output_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    AudioFFTDeNoiseContext *s = ctx->priv;
    const int output_mode = ctx->is_disabled ? IN_MODE : s->output_mode;
    const int offset = s->window_length - s->sample_advance;
    AVFrame *out;

    for (int ch = 0; ch < s->channels; ch++) {
        uint8_t *src = (uint8_t *)s->winframe->extended_data[ch];

        memmove(src, src + s->sample_advance * s->sample_size,
                offset * s->sample_size);
        memcpy(src + offset * s->sample_size, in->extended_data[ch],
               in->nb_samples * s->sample_size);
        memset(src + s->sample_size * (offset + in->nb_samples), 0,
               (s->sample_advance - in->nb_samples) * s->sample_size);
    }

    if (s->track_noise) {
        double average = 0.0, min = DBL_MAX, max = -DBL_MAX;

        for (int ch = 0; ch < inlink->ch_layout.nb_channels; ch++) {
            DeNoiseChannel *dnch = &s->dnch[ch];

            average += dnch->noise_floor;
            max = fmax(max, dnch->noise_floor);
            min = fmin(min, dnch->noise_floor);
        }

        average /= inlink->ch_layout.nb_channels;

        for (int ch = 0; ch < inlink->ch_layout.nb_channels; ch++) {
            DeNoiseChannel *dnch = &s->dnch[ch];

            switch (s->noise_floor_link) {
            case MIN_LINK:     dnch->noise_floor = min;     break;
            case MAX_LINK:     dnch->noise_floor = max;     break;
            case AVERAGE_LINK: dnch->noise_floor = average; break;
            case NONE_LINK:
            default:
                break;
            }

            if (dnch->noise_floor != dnch->last_noise_floor)
                set_parameters(s, dnch, 1, 0);
        }
    }

    if (s->sample_noise_mode == SAMPLE_START) {
        for (int ch = 0; ch < inlink->ch_layout.nb_channels; ch++) {
            DeNoiseChannel *dnch = &s->dnch[ch];

            init_sample_noise(dnch);
        }
        s->sample_noise_mode = SAMPLE_NONE;
        s->sample_noise = 1;
        s->sample_noise_blocks = 0;
    }

    if (s->sample_noise) {
        for (int ch = 0; ch < inlink->ch_layout.nb_channels; ch++) {
            DeNoiseChannel *dnch = &s->dnch[ch];

            sample_noise_block(s, dnch, s->winframe, ch);
        }
        s->sample_noise_blocks++;
    }

    if (s->sample_noise_mode == SAMPLE_STOP) {
        for (int ch = 0; ch < inlink->ch_layout.nb_channels; ch++) {
            DeNoiseChannel *dnch = &s->dnch[ch];
            double sample_noise[NB_PROFILE_BANDS];

            if (s->sample_noise_blocks <= 0)
                break;
            finish_sample_noise(s, dnch, sample_noise);
            set_noise_profile(s, dnch, sample_noise);
            set_parameters(s, dnch, 1, 1);
        }
        s->sample_noise = 0;
        s->sample_noise_blocks = 0;
        s->sample_noise_mode = SAMPLE_NONE;
    }

    ff_filter_execute(ctx, filter_channel, s->winframe, NULL,
                      FFMIN(outlink->ch_layout.nb_channels, ff_filter_get_nb_threads(ctx)));

    if (av_frame_is_writable(in)) {
        out = in;
    } else {
        out = ff_get_audio_buffer(outlink, in->nb_samples);
        if (!out) {
            av_frame_free(&in);
            return AVERROR(ENOMEM);
        }

        av_frame_copy_props(out, in);
    }

    for (int ch = 0; ch < inlink->ch_layout.nb_channels; ch++) {
        DeNoiseChannel *dnch = &s->dnch[ch];
        double *src = dnch->out_samples;
        const double *orig_dbl = (const double *)s->winframe->extended_data[ch];
        const float *orig_flt = (const float *)s->winframe->extended_data[ch];
        double *dst_dbl = (double *)out->extended_data[ch];
        float *dst_flt = (float *)out->extended_data[ch];

        switch (output_mode) {
        case IN_MODE:
            switch (s->format) {
            case AV_SAMPLE_FMT_FLTP:
                for (int m = 0; m < out->nb_samples; m++)
                    dst_flt[m] = orig_flt[m];
                break;
            case AV_SAMPLE_FMT_DBLP:
                for (int m = 0; m < out->nb_samples; m++)
                    dst_dbl[m] = orig_dbl[m];
                break;
            }
            break;
        case OUT_MODE:
            switch (s->format) {
            case AV_SAMPLE_FMT_FLTP:
                for (int m = 0; m < out->nb_samples; m++)
                    dst_flt[m] = src[m];
                break;
            case AV_SAMPLE_FMT_DBLP:
                for (int m = 0; m < out->nb_samples; m++)
                    dst_dbl[m] = src[m];
                break;
            }
            break;
        case NOISE_MODE:
            switch (s->format) {
            case AV_SAMPLE_FMT_FLTP:
                for (int m = 0; m < out->nb_samples; m++)
                    dst_flt[m] = orig_flt[m] - src[m];
                break;
            case AV_SAMPLE_FMT_DBLP:
                for (int m = 0; m < out->nb_samples; m++)
                    dst_dbl[m] = orig_dbl[m] - src[m];
                break;
            }
            break;
        default:
            if (in != out)
                av_frame_free(&in);
            av_frame_free(&out);
            return AVERROR_BUG;
        }

        memmove(src, src + s->sample_advance, (s->window_length - s->sample_advance) * sizeof(*src));
        memset(src + (s->window_length - s->sample_advance), 0, s->sample_advance * sizeof(*src));
    }

    if (out != in)
        av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static int activate(AVFilterContext *ctx)
{
    AVFilterLink *inlink = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];
    AudioFFTDeNoiseContext *s = ctx->priv;
    AVFrame *in = NULL;
    int ret;

    FF_FILTER_FORWARD_STATUS_BACK(outlink, inlink);

    ret = ff_inlink_consume_samples(inlink, s->sample_advance, s->sample_advance, &in);
    if (ret < 0)
        return ret;
    if (ret > 0)
        return output_frame(inlink, in);

    if (ff_inlink_queued_samples(inlink) >= s->sample_advance) {
        ff_filter_set_ready(ctx, 10);
        return 0;
    }

    FF_FILTER_FORWARD_STATUS(inlink, outlink);
    FF_FILTER_FORWARD_WANTED(outlink, inlink);

    return FFERROR_NOT_READY;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    AudioFFTDeNoiseContext *s = ctx->priv;

    av_freep(&s->window);
    av_freep(&s->bin2band);
    av_freep(&s->band_alpha);
    av_freep(&s->band_beta);
    av_frame_free(&s->winframe);

    if (s->dnch) {
        for (int ch = 0; ch < s->channels; ch++) {
            DeNoiseChannel *dnch = &s->dnch[ch];
            av_freep(&dnch->amt);
            av_freep(&dnch->band_amt);
            av_freep(&dnch->band_excit);
            av_freep(&dnch->gain);
            av_freep(&dnch->smoothed_gain);
            av_freep(&dnch->prior);
            av_freep(&dnch->prior_band_excit);
            av_freep(&dnch->clean_data);
            av_freep(&dnch->noisy_data);
            av_freep(&dnch->out_samples);
            av_freep(&dnch->spread_function);
            av_freep(&dnch->abs_var);
            av_freep(&dnch->rel_var);
            av_freep(&dnch->min_abs_var);
            av_freep(&dnch->fft_in);
            av_freep(&dnch->fft_out);
            av_tx_uninit(&dnch->fft);
            av_tx_uninit(&dnch->ifft);
        }
        av_freep(&s->dnch);
    }
}

static int process_command(AVFilterContext *ctx, const char *cmd, const char *args,
                           char *res, int res_len, int flags)
{
    AudioFFTDeNoiseContext *s = ctx->priv;
    int ret = 0;

    ret = ff_filter_process_command(ctx, cmd, args, res, res_len, flags);
    if (ret < 0)
        return ret;

    if (!strcmp(cmd, "sample_noise") || !strcmp(cmd, "sn"))
        return 0;

    for (int ch = 0; ch < s->channels; ch++) {
        DeNoiseChannel *dnch = &s->dnch[ch];

        dnch->noise_reduction = s->noise_reduction;
        dnch->noise_floor     = s->noise_floor;
        dnch->residual_floor  = s->residual_floor;

        set_parameters(s, dnch, 1, 1);
    }

    return 0;
}

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .config_props = config_input,
    },
};

const AVFilter ff_af_afftdn = {
    .name            = "afftdn",
    .description     = NULL_IF_CONFIG_SMALL("Denoise audio samples using FFT."),
    .priv_size       = sizeof(AudioFFTDeNoiseContext),
    .priv_class      = &afftdn_class,
    .activate        = activate,
    .uninit          = uninit,
    FILTER_INPUTS(inputs),
    FILTER_OUTPUTS(ff_audio_default_filterpad),
    FILTER_SAMPLEFMTS(AV_SAMPLE_FMT_FLTP, AV_SAMPLE_FMT_DBLP),
    .process_command = process_command,
    .flags           = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL |
                       AVFILTER_FLAG_SLICE_THREADS,
};
