/*
 * Copyright (c) 2012 Justin Ruggles <justin.ruggles@gmail.com>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <stdint.h>

#include "libavutil/libm.h"
#include "libavutil/samplefmt.h"
#include "avresample.h"
#include "internal.h"
#include "audio_data.h"
#include "audio_mix.h"

static const char *coeff_type_names[] = { "q8", "q15", "flt" };

void ff_audio_mix_set_func(AudioMix *am, enum AVSampleFormat fmt,
                           enum AVMixCoeffType coeff_type, int in_channels,
                           int out_channels, int ptr_align, int samples_align,
                           const char *descr, void *mix_func)
{
    if (fmt == am->fmt && coeff_type == am->coeff_type &&
        ( in_channels ==  am->in_channels ||  in_channels == 0) &&
        (out_channels == am->out_channels || out_channels == 0)) {
        char chan_str[16];
        am->mix           = mix_func;
        am->func_descr    = descr;
        am->ptr_align     = ptr_align;
        am->samples_align = samples_align;
        if (ptr_align == 1 && samples_align == 1) {
            am->mix_generic        = mix_func;
            am->func_descr_generic = descr;
        } else {
            am->has_optimized_func = 1;
        }
        if (in_channels) {
            if (out_channels)
                snprintf(chan_str, sizeof(chan_str), "[%d to %d] ",
                         in_channels, out_channels);
            else
                snprintf(chan_str, sizeof(chan_str), "[%d to any] ",
                         in_channels);
        } else if (out_channels) {
                snprintf(chan_str, sizeof(chan_str), "[any to %d] ",
                         out_channels);
        }
        av_log(am->avr, AV_LOG_DEBUG, "audio_mix: found function: [fmt=%s] "
               "[c=%s] %s(%s)\n", av_get_sample_fmt_name(fmt),
               coeff_type_names[coeff_type],
               (in_channels || out_channels) ? chan_str : "", descr);
    }
}

#define MIX_FUNC_NAME(fmt, cfmt) mix_any_ ## fmt ##_## cfmt ##_c

#define MIX_FUNC_GENERIC(fmt, cfmt, stype, ctype, sumtype, expr)            \
static void MIX_FUNC_NAME(fmt, cfmt)(stype **samples, ctype **matrix,       \
                                     int len, int out_ch, int in_ch)        \
{                                                                           \
    int i, in, out;                                                         \
    stype temp[AVRESAMPLE_MAX_CHANNELS];                                    \
    for (i = 0; i < len; i++) {                                             \
        for (out = 0; out < out_ch; out++) {                                \
            sumtype sum = 0;                                                \
            for (in = 0; in < in_ch; in++)                                  \
                sum += samples[in][i] * matrix[out][in];                    \
            temp[out] = expr;                                               \
        }                                                                   \
        for (out = 0; out < out_ch; out++)                                  \
            samples[out][i] = temp[out];                                    \
    }                                                                       \
}

MIX_FUNC_GENERIC(FLTP, FLT, float,   float,   float,   sum)
MIX_FUNC_GENERIC(S16P, FLT, int16_t, float,   float,   av_clip_int16(lrintf(sum)))
MIX_FUNC_GENERIC(S16P, Q15, int16_t, int32_t, int64_t, av_clip_int16(sum >> 15))
MIX_FUNC_GENERIC(S16P, Q8,  int16_t, int16_t, int32_t, av_clip_int16(sum >>  8))

/* TODO: templatize the channel-specific C functions */

static void mix_2_to_1_fltp_flt_c(float **samples, float **matrix, int len,
                                  int out_ch, int in_ch)
{
    float *src0 = samples[0];
    float *src1 = samples[1];
    float *dst  = src0;
    float m0    = matrix[0][0];
    float m1    = matrix[0][1];

    while (len > 4) {
        *dst++ = *src0++ * m0 + *src1++ * m1;
        *dst++ = *src0++ * m0 + *src1++ * m1;
        *dst++ = *src0++ * m0 + *src1++ * m1;
        *dst++ = *src0++ * m0 + *src1++ * m1;
        len -= 4;
    }
    while (len > 0) {
        *dst++ = *src0++ * m0 + *src1++ * m1;
        len--;
    }
}

static void mix_2_to_1_s16p_flt_c(int16_t **samples, float **matrix, int len,
                                  int out_ch, int in_ch)
{
    int16_t *src0 = samples[0];
    int16_t *src1 = samples[1];
    int16_t *dst  = src0;
    float m0      = matrix[0][0];
    float m1      = matrix[0][1];

    while (len > 4) {
        *dst++ = av_clip_int16(lrintf(*src0++ * m0 + *src1++ * m1));
        *dst++ = av_clip_int16(lrintf(*src0++ * m0 + *src1++ * m1));
        *dst++ = av_clip_int16(lrintf(*src0++ * m0 + *src1++ * m1));
        *dst++ = av_clip_int16(lrintf(*src0++ * m0 + *src1++ * m1));
        len -= 4;
    }
    while (len > 0) {
        *dst++ = av_clip_int16(lrintf(*src0++ * m0 + *src1++ * m1));
        len--;
    }
}

static void mix_2_to_1_s16p_q8_c(int16_t **samples, int16_t **matrix, int len,
                                 int out_ch, int in_ch)
{
    int16_t *src0 = samples[0];
    int16_t *src1 = samples[1];
    int16_t *dst  = src0;
    int16_t m0    = matrix[0][0];
    int16_t m1    = matrix[0][1];

    while (len > 4) {
        *dst++ = (*src0++ * m0 + *src1++ * m1) >> 8;
        *dst++ = (*src0++ * m0 + *src1++ * m1) >> 8;
        *dst++ = (*src0++ * m0 + *src1++ * m1) >> 8;
        *dst++ = (*src0++ * m0 + *src1++ * m1) >> 8;
        len -= 4;
    }
    while (len > 0) {
        *dst++ = (*src0++ * m0 + *src1++ * m1) >> 8;
        len--;
    }
}

static void mix_1_to_2_fltp_flt_c(float **samples, float **matrix, int len,
                                  int out_ch, int in_ch)
{
    float v;
    float *dst0 = samples[0];
    float *dst1 = samples[1];
    float *src  = dst0;
    float m0    = matrix[0][0];
    float m1    = matrix[1][0];

    while (len > 4) {
        v = *src++;
        *dst0++ = v * m1;
        *dst1++ = v * m0;
        v = *src++;
        *dst0++ = v * m1;
        *dst1++ = v * m0;
        v = *src++;
        *dst0++ = v * m1;
        *dst1++ = v * m0;
        v = *src++;
        *dst0++ = v * m1;
        *dst1++ = v * m0;
        len -= 4;
    }
    while (len > 0) {
        v = *src++;
        *dst0++ = v * m1;
        *dst1++ = v * m0;
        len--;
    }
}

static void mix_6_to_2_fltp_flt_c(float **samples, float **matrix, int len,
                                  int out_ch, int in_ch)
{
    float v0, v1;
    float *src0 = samples[0];
    float *src1 = samples[1];
    float *src2 = samples[2];
    float *src3 = samples[3];
    float *src4 = samples[4];
    float *src5 = samples[5];
    float *dst0 = src0;
    float *dst1 = src1;
    float *m0   = matrix[0];
    float *m1   = matrix[1];

    while (len > 0) {
        v0 = *src0++;
        v1 = *src1++;
        *dst0++ = v0      * m0[0] +
                  v1      * m0[1] +
                  *src2   * m0[2] +
                  *src3   * m0[3] +
                  *src4   * m0[4] +
                  *src5   * m0[5];
        *dst1++ = v0      * m1[0] +
                  v1      * m1[1] +
                  *src2++ * m1[2] +
                  *src3++ * m1[3] +
                  *src4++ * m1[4] +
                  *src5++ * m1[5];
        len--;
    }
}

static void mix_2_to_6_fltp_flt_c(float **samples, float **matrix, int len,
                                  int out_ch, int in_ch)
{
    float v0, v1;
    float *dst0 = samples[0];
    float *dst1 = samples[1];
    float *dst2 = samples[2];
    float *dst3 = samples[3];
    float *dst4 = samples[4];
    float *dst5 = samples[5];
    float *src0 = dst0;
    float *src1 = dst1;

    while (len > 0) {
        v0 = *src0++;
        v1 = *src1++;
        *dst0++ = v0 * matrix[0][0] + v1 * matrix[0][1];
        *dst1++ = v0 * matrix[1][0] + v1 * matrix[1][1];
        *dst2++ = v0 * matrix[2][0] + v1 * matrix[2][1];
        *dst3++ = v0 * matrix[3][0] + v1 * matrix[3][1];
        *dst4++ = v0 * matrix[4][0] + v1 * matrix[4][1];
        *dst5++ = v0 * matrix[5][0] + v1 * matrix[5][1];
        len--;
    }
}

static int mix_function_init(AudioMix *am)
{
    /* any-to-any C versions */

    ff_audio_mix_set_func(am, AV_SAMPLE_FMT_FLTP, AV_MIX_COEFF_TYPE_FLT,
                          0, 0, 1, 1, "C", MIX_FUNC_NAME(FLTP, FLT));

    ff_audio_mix_set_func(am, AV_SAMPLE_FMT_S16P, AV_MIX_COEFF_TYPE_FLT,
                          0, 0, 1, 1, "C", MIX_FUNC_NAME(S16P, FLT));

    ff_audio_mix_set_func(am, AV_SAMPLE_FMT_S16P, AV_MIX_COEFF_TYPE_Q15,
                          0, 0, 1, 1, "C", MIX_FUNC_NAME(S16P, Q15));

    ff_audio_mix_set_func(am, AV_SAMPLE_FMT_S16P, AV_MIX_COEFF_TYPE_Q8,
                          0, 0, 1, 1, "C", MIX_FUNC_NAME(S16P, Q8));

    /* channel-specific C versions */

    ff_audio_mix_set_func(am, AV_SAMPLE_FMT_FLTP, AV_MIX_COEFF_TYPE_FLT,
                          2, 1, 1, 1, "C", mix_2_to_1_fltp_flt_c);

    ff_audio_mix_set_func(am, AV_SAMPLE_FMT_S16P, AV_MIX_COEFF_TYPE_FLT,
                          2, 1, 1, 1, "C", mix_2_to_1_s16p_flt_c);

    ff_audio_mix_set_func(am, AV_SAMPLE_FMT_S16P, AV_MIX_COEFF_TYPE_Q8,
                          2, 1, 1, 1, "C", mix_2_to_1_s16p_q8_c);

    ff_audio_mix_set_func(am, AV_SAMPLE_FMT_FLTP, AV_MIX_COEFF_TYPE_FLT,
                          1, 2, 1, 1, "C", mix_1_to_2_fltp_flt_c);

    ff_audio_mix_set_func(am, AV_SAMPLE_FMT_FLTP, AV_MIX_COEFF_TYPE_FLT,
                          6, 2, 1, 1, "C", mix_6_to_2_fltp_flt_c);

    ff_audio_mix_set_func(am, AV_SAMPLE_FMT_FLTP, AV_MIX_COEFF_TYPE_FLT,
                          2, 6, 1, 1, "C", mix_2_to_6_fltp_flt_c);

    if (ARCH_X86)
        ff_audio_mix_init_x86(am);

    if (!am->mix) {
        av_log(am->avr, AV_LOG_ERROR, "audio_mix: NO FUNCTION FOUND: [fmt=%s] "
               "[c=%s] [%d to %d]\n", av_get_sample_fmt_name(am->fmt),
               coeff_type_names[am->coeff_type], am->in_channels,
               am->out_channels);
        return AVERROR_PATCHWELCOME;
    }
    return 0;
}

int ff_audio_mix_init(AVAudioResampleContext *avr)
{
    int ret;

    /* build matrix if the user did not already set one */
    if (!avr->am->matrix) {
        int i, j;
        char in_layout_name[128];
        char out_layout_name[128];
        double *matrix_dbl = av_mallocz(avr->out_channels * avr->in_channels *
                                        sizeof(*matrix_dbl));
        if (!matrix_dbl)
            return AVERROR(ENOMEM);

        ret = avresample_build_matrix(avr->in_channel_layout,
                                      avr->out_channel_layout,
                                      avr->center_mix_level,
                                      avr->surround_mix_level,
                                      avr->lfe_mix_level, 1, matrix_dbl,
                                      avr->in_channels);
        if (ret < 0) {
            av_free(matrix_dbl);
            return ret;
        }

        av_get_channel_layout_string(in_layout_name, sizeof(in_layout_name),
                                     avr->in_channels, avr->in_channel_layout);
        av_get_channel_layout_string(out_layout_name, sizeof(out_layout_name),
                                     avr->out_channels, avr->out_channel_layout);
        av_log(avr, AV_LOG_DEBUG, "audio_mix: %s to %s\n",
               in_layout_name, out_layout_name);
        for (i = 0; i < avr->out_channels; i++) {
            for (j = 0; j < avr->in_channels; j++) {
                av_log(avr, AV_LOG_DEBUG, "  %0.3f ",
                       matrix_dbl[i * avr->in_channels + j]);
            }
            av_log(avr, AV_LOG_DEBUG, "\n");
        }

        ret = avresample_set_matrix(avr, matrix_dbl, avr->in_channels);
        if (ret < 0) {
            av_free(matrix_dbl);
            return ret;
        }
        av_free(matrix_dbl);
    }

    avr->am->fmt          = avr->internal_sample_fmt;
    avr->am->coeff_type   = avr->mix_coeff_type;
    avr->am->in_layout    = avr->in_channel_layout;
    avr->am->out_layout   = avr->out_channel_layout;
    avr->am->in_channels  = avr->in_channels;
    avr->am->out_channels = avr->out_channels;

    ret = mix_function_init(avr->am);
    if (ret < 0)
        return ret;

    return 0;
}

void ff_audio_mix_close(AudioMix *am)
{
    if (!am)
        return;
    if (am->matrix) {
        av_free(am->matrix[0]);
        am->matrix = NULL;
    }
    memset(am->matrix_q8,  0, sizeof(am->matrix_q8 ));
    memset(am->matrix_q15, 0, sizeof(am->matrix_q15));
    memset(am->matrix_flt, 0, sizeof(am->matrix_flt));
}

int ff_audio_mix(AudioMix *am, AudioData *src)
{
    int use_generic = 1;
    int len = src->nb_samples;

    /* determine whether to use the optimized function based on pointer and
       samples alignment in both the input and output */
    if (am->has_optimized_func) {
        int aligned_len = FFALIGN(len, am->samples_align);
        if (!(src->ptr_align % am->ptr_align) &&
            src->samples_align >= aligned_len) {
            len = aligned_len;
            use_generic = 0;
        }
    }
    av_dlog(am->avr, "audio_mix: %d samples - %d to %d channels (%s)\n",
            src->nb_samples, am->in_channels, am->out_channels,
            use_generic ? am->func_descr_generic : am->func_descr);

    if (use_generic)
        am->mix_generic(src->data, am->matrix, len, am->out_channels,
                        am->in_channels);
    else
        am->mix(src->data, am->matrix, len, am->out_channels, am->in_channels);

    ff_audio_data_set_channels(src, am->out_channels);

    return 0;
}
