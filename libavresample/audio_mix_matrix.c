/*
 * Copyright (C) 2011 Michael Niedermayer (michaelni@gmx.at)
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

/* channel positions */
#define FRONT_LEFT              0
#define FRONT_RIGHT             1
#define FRONT_CENTER            2
#define LOW_FREQUENCY           3
#define BACK_LEFT               4
#define BACK_RIGHT              5
#define FRONT_LEFT_OF_CENTER    6
#define FRONT_RIGHT_OF_CENTER   7
#define BACK_CENTER             8
#define SIDE_LEFT               9
#define SIDE_RIGHT             10
#define TOP_CENTER             11
#define TOP_FRONT_LEFT         12
#define TOP_FRONT_CENTER       13
#define TOP_FRONT_RIGHT        14
#define TOP_BACK_LEFT          15
#define TOP_BACK_CENTER        16
#define TOP_BACK_RIGHT         17
#define STEREO_LEFT            29
#define STEREO_RIGHT           30
#define WIDE_LEFT              31
#define WIDE_RIGHT             32
#define SURROUND_DIRECT_LEFT   33
#define SURROUND_DIRECT_RIGHT  34
#define LOW_FREQUENCY_2        35

#define SQRT3_2      1.22474487139158904909  /* sqrt(3/2) */

static av_always_inline int even(uint64_t layout)
{
    return (!layout || (layout & (layout - 1)));
}

static int sane_layout(uint64_t layout)
{
    /* check that there is at least 1 front speaker */
    if (!(layout & AV_CH_LAYOUT_SURROUND))
        return 0;

    /* check for left/right symmetry */
    if (!even(layout & (AV_CH_FRONT_LEFT           | AV_CH_FRONT_RIGHT))           ||
        !even(layout & (AV_CH_SIDE_LEFT            | AV_CH_SIDE_RIGHT))            ||
        !even(layout & (AV_CH_BACK_LEFT            | AV_CH_BACK_RIGHT))            ||
        !even(layout & (AV_CH_FRONT_LEFT_OF_CENTER | AV_CH_FRONT_RIGHT_OF_CENTER)) ||
        !even(layout & (AV_CH_TOP_FRONT_LEFT       | AV_CH_TOP_FRONT_RIGHT))       ||
        !even(layout & (AV_CH_TOP_BACK_LEFT        | AV_CH_TOP_BACK_RIGHT))        ||
        !even(layout & (AV_CH_STEREO_LEFT          | AV_CH_STEREO_RIGHT))          ||
        !even(layout & (AV_CH_WIDE_LEFT            | AV_CH_WIDE_RIGHT))            ||
        !even(layout & (AV_CH_SURROUND_DIRECT_LEFT | AV_CH_SURROUND_DIRECT_RIGHT)))
        return 0;

    return 1;
}

int avresample_build_matrix(uint64_t in_layout, uint64_t out_layout,
                            double center_mix_level, double surround_mix_level,
                            double lfe_mix_level, int normalize,
                            double *matrix_out, int stride,
                            enum AVMatrixEncoding matrix_encoding)
{
    int i, j, out_i, out_j;
    double matrix[64][64] = {{0}};
    int64_t unaccounted;
    double maxcoef = 0;
    int in_channels, out_channels;

    if ((out_layout & AV_CH_LAYOUT_STEREO_DOWNMIX) == AV_CH_LAYOUT_STEREO_DOWNMIX) {
        out_layout = AV_CH_LAYOUT_STEREO;
    }

    unaccounted = in_layout & ~out_layout;

    in_channels  = av_get_channel_layout_nb_channels( in_layout);
    out_channels = av_get_channel_layout_nb_channels(out_layout);

    memset(matrix_out, 0, out_channels * stride * sizeof(*matrix_out));

    /* check if layouts are supported */
    if (!in_layout || in_channels > AVRESAMPLE_MAX_CHANNELS)
        return AVERROR(EINVAL);
    if (!out_layout || out_channels > AVRESAMPLE_MAX_CHANNELS)
        return AVERROR(EINVAL);

    /* check if layouts are unbalanced or abnormal */
    if (!sane_layout(in_layout) || !sane_layout(out_layout))
        return AVERROR_PATCHWELCOME;

    /* route matching input/output channels */
    for (i = 0; i < 64; i++) {
        if (in_layout & out_layout & (1ULL << i))
            matrix[i][i] = 1.0;
    }

    /* mix front center to front left/right */
    if (unaccounted & AV_CH_FRONT_CENTER) {
        if ((out_layout & AV_CH_LAYOUT_STEREO) == AV_CH_LAYOUT_STEREO) {
            matrix[FRONT_LEFT ][FRONT_CENTER] += M_SQRT1_2;
            matrix[FRONT_RIGHT][FRONT_CENTER] += M_SQRT1_2;
        } else
            return AVERROR_PATCHWELCOME;
    }
    /* mix front left/right to center */
    if (unaccounted & AV_CH_LAYOUT_STEREO) {
        if (out_layout & AV_CH_FRONT_CENTER) {
            matrix[FRONT_CENTER][FRONT_LEFT ] += M_SQRT1_2;
            matrix[FRONT_CENTER][FRONT_RIGHT] += M_SQRT1_2;
            /* mix left/right/center to center */
            if (in_layout & AV_CH_FRONT_CENTER)
                matrix[FRONT_CENTER][FRONT_CENTER] = center_mix_level * M_SQRT2;
        } else
            return AVERROR_PATCHWELCOME;
    }
    /* mix back center to back, side, or front */
    if (unaccounted & AV_CH_BACK_CENTER) {
        if (out_layout & AV_CH_BACK_LEFT) {
            matrix[BACK_LEFT ][BACK_CENTER] += M_SQRT1_2;
            matrix[BACK_RIGHT][BACK_CENTER] += M_SQRT1_2;
        } else if (out_layout & AV_CH_SIDE_LEFT) {
            matrix[SIDE_LEFT ][BACK_CENTER] += M_SQRT1_2;
            matrix[SIDE_RIGHT][BACK_CENTER] += M_SQRT1_2;
        } else if (out_layout & AV_CH_FRONT_LEFT) {
            if (matrix_encoding == AV_MATRIX_ENCODING_DOLBY ||
                matrix_encoding == AV_MATRIX_ENCODING_DPLII) {
                if (unaccounted & (AV_CH_BACK_LEFT | AV_CH_SIDE_LEFT)) {
                    matrix[FRONT_LEFT ][BACK_CENTER] -= surround_mix_level * M_SQRT1_2;
                    matrix[FRONT_RIGHT][BACK_CENTER] += surround_mix_level * M_SQRT1_2;
                } else {
                    matrix[FRONT_LEFT ][BACK_CENTER] -= surround_mix_level;
                    matrix[FRONT_RIGHT][BACK_CENTER] += surround_mix_level;
                }
            } else {
                matrix[FRONT_LEFT ][BACK_CENTER] += surround_mix_level * M_SQRT1_2;
                matrix[FRONT_RIGHT][BACK_CENTER] += surround_mix_level * M_SQRT1_2;
            }
        } else if (out_layout & AV_CH_FRONT_CENTER) {
            matrix[FRONT_CENTER][BACK_CENTER] += surround_mix_level * M_SQRT1_2;
        } else
            return AVERROR_PATCHWELCOME;
    }
    /* mix back left/right to back center, side, or front */
    if (unaccounted & AV_CH_BACK_LEFT) {
        if (out_layout & AV_CH_BACK_CENTER) {
            matrix[BACK_CENTER][BACK_LEFT ] += M_SQRT1_2;
            matrix[BACK_CENTER][BACK_RIGHT] += M_SQRT1_2;
        } else if (out_layout & AV_CH_SIDE_LEFT) {
            /* if side channels do not exist in the input, just copy back
               channels to side channels, otherwise mix back into side */
            if (in_layout & AV_CH_SIDE_LEFT) {
                matrix[SIDE_LEFT ][BACK_LEFT ] += M_SQRT1_2;
                matrix[SIDE_RIGHT][BACK_RIGHT] += M_SQRT1_2;
            } else {
                matrix[SIDE_LEFT ][BACK_LEFT ] += 1.0;
                matrix[SIDE_RIGHT][BACK_RIGHT] += 1.0;
            }
        } else if (out_layout & AV_CH_FRONT_LEFT) {
            if (matrix_encoding == AV_MATRIX_ENCODING_DOLBY) {
                matrix[FRONT_LEFT ][BACK_LEFT ] -= surround_mix_level * M_SQRT1_2;
                matrix[FRONT_LEFT ][BACK_RIGHT] -= surround_mix_level * M_SQRT1_2;
                matrix[FRONT_RIGHT][BACK_LEFT ] += surround_mix_level * M_SQRT1_2;
                matrix[FRONT_RIGHT][BACK_RIGHT] += surround_mix_level * M_SQRT1_2;
            } else if (matrix_encoding == AV_MATRIX_ENCODING_DPLII) {
                matrix[FRONT_LEFT ][BACK_LEFT ] -= surround_mix_level * SQRT3_2;
                matrix[FRONT_LEFT ][BACK_RIGHT] -= surround_mix_level * M_SQRT1_2;
                matrix[FRONT_RIGHT][BACK_LEFT ] += surround_mix_level * M_SQRT1_2;
                matrix[FRONT_RIGHT][BACK_RIGHT] += surround_mix_level * SQRT3_2;
            } else {
                matrix[FRONT_LEFT ][BACK_LEFT ] += surround_mix_level;
                matrix[FRONT_RIGHT][BACK_RIGHT] += surround_mix_level;
            }
        } else if (out_layout & AV_CH_FRONT_CENTER) {
            matrix[FRONT_CENTER][BACK_LEFT ] += surround_mix_level * M_SQRT1_2;
            matrix[FRONT_CENTER][BACK_RIGHT] += surround_mix_level * M_SQRT1_2;
        } else
            return AVERROR_PATCHWELCOME;
    }
    /* mix side left/right into back or front */
    if (unaccounted & AV_CH_SIDE_LEFT) {
        if (out_layout & AV_CH_BACK_LEFT) {
            /* if back channels do not exist in the input, just copy side
               channels to back channels, otherwise mix side into back */
            if (in_layout & AV_CH_BACK_LEFT) {
                matrix[BACK_LEFT ][SIDE_LEFT ] += M_SQRT1_2;
                matrix[BACK_RIGHT][SIDE_RIGHT] += M_SQRT1_2;
            } else {
                matrix[BACK_LEFT ][SIDE_LEFT ] += 1.0;
                matrix[BACK_RIGHT][SIDE_RIGHT] += 1.0;
            }
        } else if (out_layout & AV_CH_BACK_CENTER) {
            matrix[BACK_CENTER][SIDE_LEFT ] += M_SQRT1_2;
            matrix[BACK_CENTER][SIDE_RIGHT] += M_SQRT1_2;
        } else if (out_layout & AV_CH_FRONT_LEFT) {
            if (matrix_encoding == AV_MATRIX_ENCODING_DOLBY) {
                matrix[FRONT_LEFT ][SIDE_LEFT ] -= surround_mix_level * M_SQRT1_2;
                matrix[FRONT_LEFT ][SIDE_RIGHT] -= surround_mix_level * M_SQRT1_2;
                matrix[FRONT_RIGHT][SIDE_LEFT ] += surround_mix_level * M_SQRT1_2;
                matrix[FRONT_RIGHT][SIDE_RIGHT] += surround_mix_level * M_SQRT1_2;
            } else if (matrix_encoding == AV_MATRIX_ENCODING_DPLII) {
                matrix[FRONT_LEFT ][SIDE_LEFT ] -= surround_mix_level * SQRT3_2;
                matrix[FRONT_LEFT ][SIDE_RIGHT] -= surround_mix_level * M_SQRT1_2;
                matrix[FRONT_RIGHT][SIDE_LEFT ] += surround_mix_level * M_SQRT1_2;
                matrix[FRONT_RIGHT][SIDE_RIGHT] += surround_mix_level * SQRT3_2;
            } else {
                matrix[FRONT_LEFT ][SIDE_LEFT ] += surround_mix_level;
                matrix[FRONT_RIGHT][SIDE_RIGHT] += surround_mix_level;
            }
        } else if (out_layout & AV_CH_FRONT_CENTER) {
            matrix[FRONT_CENTER][SIDE_LEFT ] += surround_mix_level * M_SQRT1_2;
            matrix[FRONT_CENTER][SIDE_RIGHT] += surround_mix_level * M_SQRT1_2;
        } else
            return AVERROR_PATCHWELCOME;
    }
    /* mix left-of-center/right-of-center into front left/right or center */
    if (unaccounted & AV_CH_FRONT_LEFT_OF_CENTER) {
        if (out_layout & AV_CH_FRONT_LEFT) {
            matrix[FRONT_LEFT ][FRONT_LEFT_OF_CENTER ] += 1.0;
            matrix[FRONT_RIGHT][FRONT_RIGHT_OF_CENTER] += 1.0;
        } else if (out_layout & AV_CH_FRONT_CENTER) {
            matrix[FRONT_CENTER][FRONT_LEFT_OF_CENTER ] += M_SQRT1_2;
            matrix[FRONT_CENTER][FRONT_RIGHT_OF_CENTER] += M_SQRT1_2;
        } else
            return AVERROR_PATCHWELCOME;
    }
    /* mix LFE into front left/right or center */
    if (unaccounted & AV_CH_LOW_FREQUENCY) {
        if (out_layout & AV_CH_FRONT_CENTER) {
            matrix[FRONT_CENTER][LOW_FREQUENCY] += lfe_mix_level;
        } else if (out_layout & AV_CH_FRONT_LEFT) {
            matrix[FRONT_LEFT ][LOW_FREQUENCY] += lfe_mix_level * M_SQRT1_2;
            matrix[FRONT_RIGHT][LOW_FREQUENCY] += lfe_mix_level * M_SQRT1_2;
        } else
            return AVERROR_PATCHWELCOME;
    }

    /* transfer internal matrix to output matrix and calculate maximum
       per-channel coefficient sum */
    for (out_i = i = 0; out_i < out_channels && i < 64; i++) {
        double sum = 0;
        for (out_j = j = 0; out_j < in_channels && j < 64; j++) {
            matrix_out[out_i * stride + out_j] = matrix[i][j];
            sum += fabs(matrix[i][j]);
            if (in_layout & (1ULL << j))
                out_j++;
        }
        maxcoef = FFMAX(maxcoef, sum);
        if (out_layout & (1ULL << i))
            out_i++;
    }

    /* normalize */
    if (normalize && maxcoef > 1.0) {
        for (i = 0; i < out_channels; i++)
            for (j = 0; j < in_channels; j++)
                matrix_out[i * stride + j] /= maxcoef;
    }

    return 0;
}

int avresample_get_matrix(AVAudioResampleContext *avr, double *matrix,
                          int stride)
{
    int in_channels, out_channels, i, o;

    in_channels  = av_get_channel_layout_nb_channels(avr->in_channel_layout);
    out_channels = av_get_channel_layout_nb_channels(avr->out_channel_layout);

    if ( in_channels <= 0 ||  in_channels > AVRESAMPLE_MAX_CHANNELS ||
        out_channels <= 0 || out_channels > AVRESAMPLE_MAX_CHANNELS) {
        av_log(avr, AV_LOG_ERROR, "Invalid channel layouts\n");
        return AVERROR(EINVAL);
    }

    switch (avr->mix_coeff_type) {
    case AV_MIX_COEFF_TYPE_Q8:
        if (!avr->am->matrix_q8[0]) {
            av_log(avr, AV_LOG_ERROR, "matrix is not set\n");
            return AVERROR(EINVAL);
        }
        for (o = 0; o < out_channels; o++)
            for (i = 0; i < in_channels; i++)
                matrix[o * stride + i] = avr->am->matrix_q8[o][i] / 256.0;
        break;
    case AV_MIX_COEFF_TYPE_Q15:
        if (!avr->am->matrix_q15[0]) {
            av_log(avr, AV_LOG_ERROR, "matrix is not set\n");
            return AVERROR(EINVAL);
        }
        for (o = 0; o < out_channels; o++)
            for (i = 0; i < in_channels; i++)
                matrix[o * stride + i] = avr->am->matrix_q15[o][i] / 32768.0;
        break;
    case AV_MIX_COEFF_TYPE_FLT:
        if (!avr->am->matrix_flt[0]) {
            av_log(avr, AV_LOG_ERROR, "matrix is not set\n");
            return AVERROR(EINVAL);
        }
        for (o = 0; o < out_channels; o++)
            for (i = 0; i < in_channels; i++)
                matrix[o * stride + i] = avr->am->matrix_flt[o][i];
        break;
    default:
        av_log(avr, AV_LOG_ERROR, "Invalid mix coeff type\n");
        return AVERROR(EINVAL);
    }

    return 0;
}

int avresample_set_matrix(AVAudioResampleContext *avr, const double *matrix,
                          int stride)
{
    int in_channels, out_channels, i, o;

    in_channels  = av_get_channel_layout_nb_channels(avr->in_channel_layout);
    out_channels = av_get_channel_layout_nb_channels(avr->out_channel_layout);

    if ( in_channels <= 0 ||  in_channels > AVRESAMPLE_MAX_CHANNELS ||
        out_channels <= 0 || out_channels > AVRESAMPLE_MAX_CHANNELS) {
        av_log(avr, AV_LOG_ERROR, "Invalid channel layouts\n");
        return AVERROR(EINVAL);
    }

    if (avr->am->matrix) {
        av_free(avr->am->matrix[0]);
        avr->am->matrix = NULL;
    }

#define CONVERT_MATRIX(type, expr)                                          \
    avr->am->matrix_## type[0] = av_mallocz(out_channels * in_channels *    \
                                            sizeof(*avr->am->matrix_## type[0])); \
    if (!avr->am->matrix_## type[0])                                        \
        return AVERROR(ENOMEM);                                             \
    for (o = 0; o < out_channels; o++) {                                    \
        if (o > 0)                                                          \
            avr->am->matrix_## type[o] = avr->am->matrix_## type[o - 1] +   \
                                         in_channels;                       \
        for (i = 0; i < in_channels; i++) {                                 \
            double v = matrix[o * stride + i];                              \
            avr->am->matrix_## type[o][i] = expr;                           \
        }                                                                   \
    }                                                                       \
    avr->am->matrix = (void **)avr->am->matrix_## type;

    switch (avr->mix_coeff_type) {
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
        av_log(avr, AV_LOG_ERROR, "Invalid mix coeff type\n");
        return AVERROR(EINVAL);
    }

    /* TODO: detect situations where we can just swap around pointers
             instead of doing matrix multiplications with 0.0 and 1.0 */

    /* set AudioMix params */
    avr->am->in_layout    = avr->in_channel_layout;
    avr->am->out_layout   = avr->out_channel_layout;
    avr->am->in_channels  = in_channels;
    avr->am->out_channels = out_channels;

    return 0;
}
