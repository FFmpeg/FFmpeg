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

#ifndef AVUTIL_FILM_GRAIN_PARAMS_H
#define AVUTIL_FILM_GRAIN_PARAMS_H

#include "frame.h"

enum AVFilmGrainParamsType {
    AV_FILM_GRAM_PARAMS_NONE = 0,

    /**
     * The union is valid when interpreted as AVFilmGrainAOMParams (codec.aom)
     */
    AV_FILM_GRAM_PARAMS_AV1,
};

/**
 * This structure describes how to handle film grain synthesis for AOM codecs.
 *
 * @note The struct must be allocated as part of AVFilmGrainParams using
 *       av_film_grain_params_alloc(). Its size is not a part of the public ABI.
 */
typedef struct AVFilmGrainAOMParams {
    /**
     * Number of points, and the scale and value for each point of the
     * piecewise linear scaling function for the uma plane.
     */
    int num_y_points;
    uint8_t y_points[14][2 /* value, scaling */];

    /**
     * Signals whether to derive the chroma scaling function from the luma.
     * Not equivalent to copying the luma values and scales.
     */
    int chroma_scaling_from_luma;

    /**
     * If chroma_scaling_from_luma is set to 0, signals the chroma scaling
     * function parameters.
     */
    int num_uv_points[2];
    uint8_t uv_points[2][10][2 /* value, scaling */];

    /**
     * Specifies the shift applied to the chroma components. For AV1, its within
     * [8; 11] and determines the range and quantization of the film grain.
     */
    int scaling_shift;

    /**
     * Specifies the auto-regression lag. The number of coefficients is given by
     * 2*ar_coeff_lag(ar_coeff_lag - 1), with an extra one for the chroma.
     */
    int ar_coeff_lag;

    /**
     * Luma auto-regression coefficients.
     */
    int8_t ar_coeffs_y[24];

    /**
     * Chroma auto-regression coefficients.
     */
    int8_t ar_coeffs_uv[2][25];

    /**
     * Specifies the range of the auto-regressive coefficients. Values of 6,
     * 7, 8 and so on represent a range of [-2, 2), [-1, 1), [-0.5, 0.5) and
     * so on. For AV1 must be between 6 and 9.
     */
    int ar_coeff_shift;

    /**
     * Signals the down shift applied to the generated gaussian numbers during
     * synthesis.
     */
    int grain_scale_shift;

    /**
     * Specifies the luma/chroma multipliers for the index to the component
     * scaling function.
     */
    int uv_mult[2];
    int uv_mult_luma[2];

    /**
     * Offset used for component scaling function. For AV1 its a 9-bit value
     * with a range [-256, 255]
     */
    int uv_offset[2];

    /**
     * Signals whether to overlap film grain blocks.
     */
    int overlap_flag;

    /**
     * Signals to clip to limited color levels after film grain application.
     */
    int limit_output_range;
} AVFilmGrainAOMParams;

/**
 * This structure describes how to handle film grain synthesis in video
 * for specific codecs. Must be present on every frame where film grain is
 * meant to be synthesised for correct presentation.
 *
 * @note The struct must be allocated with av_film_grain_params_alloc() and
 *       its size is not a part of the public ABI.
 */
typedef struct AVFilmGrainParams {
    /**
     * Specifies the codec for which this structure is valid.
     */
    enum AVFilmGrainParamsType type;

    /**
     * Seed to use for the synthesis process, if the codec allows for it.
     */
    uint64_t seed;

    /**
     * Additional fields may be added both here and in any structure included.
     * If a codec's film grain structure differs slightly over another
     * codec's, fields within may change meaning depending on the type.
     */
    union {
        AVFilmGrainAOMParams aom;
    } codec;
} AVFilmGrainParams;

/**
 * Allocate an AVFilmGrainParams structure and set its fields to
 * default values. The resulting struct can be freed using av_freep().
 * If size is not NULL it will be set to the number of bytes allocated.
 *
 * @return An AVFilmGrainParams filled with default values or NULL
 *         on failure.
 */
AVFilmGrainParams *av_film_grain_params_alloc(size_t *size);

/**
 * Allocate a complete AVFilmGrainParams and add it to the frame.
 *
 * @param frame The frame which side data is added to.
 *
 * @return The AVFilmGrainParams structure to be filled by caller.
 */
AVFilmGrainParams *av_film_grain_params_create_side_data(AVFrame *frame);

#endif /* AVUTIL_FILM_GRAIN_PARAMS_H */
