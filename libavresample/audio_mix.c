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

#include "libavutil/common.h"
#include "libavutil/libm.h"
#include "libavutil/samplefmt.h"
#include "avresample.h"
#include "internal.h"
#include "audio_data.h"
#include "audio_mix.h"

static const char *coeff_type_names[] = { "q8", "q15", "flt" };

struct AudioMix {
    AVAudioResampleContext *avr;
    enum AVSampleFormat fmt;
    enum AVMixCoeffType coeff_type;
    uint64_t in_layout;
    uint64_t out_layout;
    int in_channels;
    int out_channels;

    int ptr_align;
    int samples_align;
    int has_optimized_func;
    const char *func_descr;
    const char *func_descr_generic;
    mix_func *mix;
    mix_func *mix_generic;

    int in_matrix_channels;
    int out_matrix_channels;
    int output_zero[AVRESAMPLE_MAX_CHANNELS];
    int input_skip[AVRESAMPLE_MAX_CHANNELS];
    int output_skip[AVRESAMPLE_MAX_CHANNELS];
    int16_t *matrix_q8[AVRESAMPLE_MAX_CHANNELS];
    int32_t *matrix_q15[AVRESAMPLE_MAX_CHANNELS];
    float   *matrix_flt[AVRESAMPLE_MAX_CHANNELS];
    void   **matrix;
};

void ff_audio_mix_set_func(AudioMix *am, enum AVSampleFormat fmt,
                           enum AVMixCoeffType coeff_type, int in_channels,
                           int out_channels, int ptr_align, int samples_align,
                           const char *descr, void *mix_func)
{
    if (fmt == am->fmt && coeff_type == am->coeff_type &&
        ( in_channels ==  am->in_matrix_channels ||  in_channels == 0) &&
        (out_channels == am->out_matrix_channels || out_channels == 0)) {
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
        } else {
            snprintf(chan_str, sizeof(chan_str), "[any to any] ");
        }
        av_log(am->avr, AV_LOG_DEBUG, "audio_mix: found function: [fmt=%s] "
               "[c=%s] %s(%s)\n", av_get_sample_fmt_name(fmt),
               coeff_type_names[coeff_type], chan_str, descr);
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

static av_cold int mix_function_init(AudioMix *am)
{
    am->func_descr = am->func_descr_generic = "n/a";
    am->mix = am->mix_generic = NULL;

    /* no need to set a mix function when we're skipping mixing */
    if (!am->in_matrix_channels || !am->out_matrix_channels)
        return 0;

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

AudioMix *ff_audio_mix_alloc(AVAudioResampleContext *avr)
{
    AudioMix *am;
    int ret;

    am = av_mallocz(sizeof(*am));
    if (!am)
        return NULL;
    am->avr = avr;

    if (avr->internal_sample_fmt != AV_SAMPLE_FMT_S16P &&
        avr->internal_sample_fmt != AV_SAMPLE_FMT_FLTP) {
        av_log(avr, AV_LOG_ERROR, "Unsupported internal format for "
               "mixing: %s\n",
               av_get_sample_fmt_name(avr->internal_sample_fmt));
        goto error;
    }

    am->fmt          = avr->internal_sample_fmt;
    am->coeff_type   = avr->mix_coeff_type;
    am->in_layout    = avr->in_channel_layout;
    am->out_layout   = avr->out_channel_layout;
    am->in_channels  = avr->in_channels;
    am->out_channels = avr->out_channels;

    /* build matrix if the user did not already set one */
    if (avr->mix_matrix) {
        ret = ff_audio_mix_set_matrix(am, avr->mix_matrix, avr->in_channels);
        if (ret < 0)
            goto error;
        av_freep(&avr->mix_matrix);
    } else {
        double *matrix_dbl = av_mallocz(avr->out_channels * avr->in_channels *
                                        sizeof(*matrix_dbl));
        if (!matrix_dbl)
            goto error;

        ret = avresample_build_matrix(avr->in_channel_layout,
                                      avr->out_channel_layout,
                                      avr->center_mix_level,
                                      avr->surround_mix_level,
                                      avr->lfe_mix_level,
                                      avr->normalize_mix_level,
                                      matrix_dbl,
                                      avr->in_channels,
                                      avr->matrix_encoding);
        if (ret < 0) {
            av_free(matrix_dbl);
            goto error;
        }

        ret = ff_audio_mix_set_matrix(am, matrix_dbl, avr->in_channels);
        if (ret < 0) {
            av_log(avr, AV_LOG_ERROR, "error setting mix matrix\n");
            av_free(matrix_dbl);
            goto error;
        }

        av_free(matrix_dbl);
    }

    return am;

error:
    av_free(am);
    return NULL;
}

void ff_audio_mix_free(AudioMix **am_p)
{
    AudioMix *am;

    if (!*am_p)
        return;
    am = *am_p;

    if (am->matrix) {
        av_free(am->matrix[0]);
        am->matrix = NULL;
    }
    memset(am->matrix_q8,  0, sizeof(am->matrix_q8 ));
    memset(am->matrix_q15, 0, sizeof(am->matrix_q15));
    memset(am->matrix_flt, 0, sizeof(am->matrix_flt));

    av_freep(am_p);
}

int ff_audio_mix(AudioMix *am, AudioData *src)
{
    int use_generic = 1;
    int len = src->nb_samples;
    int i, j;

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

    if (am->in_matrix_channels && am->out_matrix_channels) {
        uint8_t **data;
        uint8_t *data0[AVRESAMPLE_MAX_CHANNELS];

        if (am->out_matrix_channels < am->out_channels ||
             am->in_matrix_channels <  am->in_channels) {
            for (i = 0, j = 0; i < FFMAX(am->in_channels, am->out_channels); i++) {
                if (am->input_skip[i] || am->output_skip[i] || am->output_zero[i])
                    continue;
                data0[j++] = src->data[i];
            }
            data = data0;
        } else {
            data = src->data;
        }

        if (use_generic)
            am->mix_generic(data, am->matrix, len, am->out_matrix_channels,
                            am->in_matrix_channels);
        else
            am->mix(data, am->matrix, len, am->out_matrix_channels,
                    am->in_matrix_channels);
    }

    if (am->out_matrix_channels < am->out_channels) {
        for (i = 0; i < am->out_channels; i++)
            if (am->output_zero[i])
                av_samples_set_silence(&src->data[i], 0, len, 1, am->fmt);
    }

    ff_audio_data_set_channels(src, am->out_channels);

    return 0;
}

int ff_audio_mix_get_matrix(AudioMix *am, double *matrix, int stride)
{
    int i, o, i0, o0;

    if ( am->in_channels <= 0 ||  am->in_channels > AVRESAMPLE_MAX_CHANNELS ||
        am->out_channels <= 0 || am->out_channels > AVRESAMPLE_MAX_CHANNELS) {
        av_log(am->avr, AV_LOG_ERROR, "Invalid channel counts\n");
        return AVERROR(EINVAL);
    }

#define GET_MATRIX_CONVERT(suffix, scale)                                   \
    if (!am->matrix_ ## suffix[0]) {                                        \
        av_log(am->avr, AV_LOG_ERROR, "matrix is not set\n");               \
        return AVERROR(EINVAL);                                             \
    }                                                                       \
    for (o = 0, o0 = 0; o < am->out_channels; o++) {                        \
        for (i = 0, i0 = 0; i < am->in_channels; i++) {                     \
            if (am->input_skip[i] || am->output_zero[o])                    \
                matrix[o * stride + i] = 0.0;                               \
            else                                                            \
                matrix[o * stride + i] = am->matrix_ ## suffix[o0][i0] *    \
                                         (scale);                           \
            if (!am->input_skip[i])                                         \
                i0++;                                                       \
        }                                                                   \
        if (!am->output_zero[o])                                            \
            o0++;                                                           \
    }

    switch (am->coeff_type) {
    case AV_MIX_COEFF_TYPE_Q8:
        GET_MATRIX_CONVERT(q8, 1.0 / 256.0);
        break;
    case AV_MIX_COEFF_TYPE_Q15:
        GET_MATRIX_CONVERT(q15, 1.0 / 32768.0);
        break;
    case AV_MIX_COEFF_TYPE_FLT:
        GET_MATRIX_CONVERT(flt, 1.0);
        break;
    default:
        av_log(am->avr, AV_LOG_ERROR, "Invalid mix coeff type\n");
        return AVERROR(EINVAL);
    }

    return 0;
}

static void reduce_matrix(AudioMix *am, const double *matrix, int stride)
{
    int i, o;

    memset(am->output_zero, 0, sizeof(am->output_zero));
    memset(am->input_skip,  0, sizeof(am->input_skip));
    memset(am->output_skip, 0, sizeof(am->output_skip));

    /* exclude output channels if they can be zeroed instead of mixed */
    for (o = 0; o < am->out_channels; o++) {
        int zero = 1;

        /* check if the output is always silent */
        for (i = 0; i < am->in_channels; i++) {
            if (matrix[o * stride + i] != 0.0) {
                zero = 0;
                break;
            }
        }
        /* check if the corresponding input channel makes a contribution to
           any output channel */
        if (o < am->in_channels) {
            for (i = 0; i < am->out_channels; i++) {
                if (matrix[i * stride + o] != 0.0) {
                    zero = 0;
                    break;
                }
            }
        }
        if (zero) {
            am->output_zero[o] = 1;
            am->out_matrix_channels--;
        }
    }
    if (am->out_matrix_channels == 0) {
        am->in_matrix_channels = 0;
        return;
    }

    /* skip input channels that contribute fully only to the corresponding
       output channel */
    for (i = 0; i < FFMIN(am->in_channels, am->out_channels); i++) {
        int skip = 1;

        for (o = 0; o < am->out_channels; o++) {
            int i0;
            if ((o != i && matrix[o * stride + i] != 0.0) ||
                (o == i && matrix[o * stride + i] != 1.0)) {
                skip = 0;
                break;
            }
            /* if the input contributes fully to the output, also check that no
               other inputs contribute to this output */
            if (o == i) {
                for (i0 = 0; i0 < am->in_channels; i0++) {
                    if (i0 != i && matrix[o * stride + i0] != 0.0) {
                        skip = 0;
                        break;
                    }
                }
            }
        }
        if (skip) {
            am->input_skip[i] = 1;
            am->in_matrix_channels--;
        }
    }
    /* skip input channels that do not contribute to any output channel */
    for (; i < am->in_channels; i++) {
        int contrib = 0;

        for (o = 0; o < am->out_channels; o++) {
            if (matrix[o * stride + i] != 0.0) {
                contrib = 1;
                break;
            }
        }
        if (!contrib) {
            am->input_skip[i] = 1;
            am->in_matrix_channels--;
        }
    }
    if (am->in_matrix_channels == 0) {
        am->out_matrix_channels = 0;
        return;
    }

    /* skip output channels that only get full contribution from the
       corresponding input channel */
    for (o = 0; o < FFMIN(am->in_channels, am->out_channels); o++) {
        int skip = 1;
        int o0;

        for (i = 0; i < am->in_channels; i++) {
            if ((o != i && matrix[o * stride + i] != 0.0) ||
                (o == i && matrix[o * stride + i] != 1.0)) {
                skip = 0;
                break;
            }
        }
        /* check if the corresponding input channel makes a contribution to
           any other output channel */
        i = o;
        for (o0 = 0; o0 < am->out_channels; o0++) {
            if (o0 != i && matrix[o0 * stride + i] != 0.0) {
                skip = 0;
                break;
            }
        }
        if (skip) {
            am->output_skip[o] = 1;
            am->out_matrix_channels--;
        }
    }
    if (am->out_matrix_channels == 0) {
        am->in_matrix_channels = 0;
        return;
    }
}

int ff_audio_mix_set_matrix(AudioMix *am, const double *matrix, int stride)
{
    int i, o, i0, o0, ret;
    char in_layout_name[128];
    char out_layout_name[128];

    if ( am->in_channels <= 0 ||  am->in_channels > AVRESAMPLE_MAX_CHANNELS ||
        am->out_channels <= 0 || am->out_channels > AVRESAMPLE_MAX_CHANNELS) {
        av_log(am->avr, AV_LOG_ERROR, "Invalid channel counts\n");
        return AVERROR(EINVAL);
    }

    if (am->matrix) {
        av_free(am->matrix[0]);
        am->matrix = NULL;
    }

    am->in_matrix_channels  = am->in_channels;
    am->out_matrix_channels = am->out_channels;

    reduce_matrix(am, matrix, stride);

#define CONVERT_MATRIX(type, expr)                                          \
    am->matrix_## type[0] = av_mallocz(am->out_matrix_channels *            \
                                       am->in_matrix_channels  *            \
                                       sizeof(*am->matrix_## type[0]));     \
    if (!am->matrix_## type[0])                                             \
        return AVERROR(ENOMEM);                                             \
    for (o = 0, o0 = 0; o < am->out_channels; o++) {                        \
        if (am->output_zero[o] || am->output_skip[o])                       \
            continue;                                                       \
        if (o0 > 0)                                                         \
            am->matrix_## type[o0] = am->matrix_## type[o0 - 1] +           \
                                     am->in_matrix_channels;                \
        for (i = 0, i0 = 0; i < am->in_channels; i++) {                     \
            double v;                                                       \
            if (am->input_skip[i])                                          \
                continue;                                                   \
            v = matrix[o * stride + i];                                     \
            am->matrix_## type[o0][i0] = expr;                              \
            i0++;                                                           \
        }                                                                   \
        o0++;                                                               \
    }                                                                       \
    am->matrix = (void **)am->matrix_## type;

    if (am->in_matrix_channels && am->out_matrix_channels) {
        switch (am->coeff_type) {
        case AV_MIX_COEFF_TYPE_Q8:
            CONVERT_MATRIX(q8, av_clip_int16(lrint(256.0 * v)))
            break;
        case AV_MIX_COEFF_TYPE_Q15:
            CONVERT_MATRIX(q15, av_clipl_int32(llrint(32768.0 * v)))
            break;
        case AV_MIX_COEFF_TYPE_FLT:
            CONVERT_MATRIX(flt, v)
            break;
        default:
            av_log(am->avr, AV_LOG_ERROR, "Invalid mix coeff type\n");
            return AVERROR(EINVAL);
        }
    }

    ret = mix_function_init(am);
    if (ret < 0)
        return ret;

    av_get_channel_layout_string(in_layout_name, sizeof(in_layout_name),
                                 am->in_channels, am->in_layout);
    av_get_channel_layout_string(out_layout_name, sizeof(out_layout_name),
                                 am->out_channels, am->out_layout);
    av_log(am->avr, AV_LOG_DEBUG, "audio_mix: %s to %s\n",
           in_layout_name, out_layout_name);
    av_log(am->avr, AV_LOG_DEBUG, "matrix size: %d x %d\n",
           am->in_matrix_channels, am->out_matrix_channels);
    for (o = 0; o < am->out_channels; o++) {
        for (i = 0; i < am->in_channels; i++) {
            if (am->output_zero[o])
                av_log(am->avr, AV_LOG_DEBUG, "  (ZERO)");
            else if (am->input_skip[i] || am->output_skip[o])
                av_log(am->avr, AV_LOG_DEBUG, "  (SKIP)");
            else
                av_log(am->avr, AV_LOG_DEBUG, "  %0.3f ",
                       matrix[o * am->in_channels + i]);
        }
        av_log(am->avr, AV_LOG_DEBUG, "\n");
    }

    return 0;
}
