/*
 * Copyright (c) 2026 Lynne <dev@lynne.ee>
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

#ifndef AVUTIL_RAW_COLOR_PARAMS_H
#define AVUTIL_RAW_COLOR_PARAMS_H

#include <stddef.h>
#include <stdint.h>

#include "frame.h"
#include "rational.h"

enum AVRawColorParamsType {
    AV_RAW_COLOR_PARAMS_NONE = 0,

    /**
     * The union is valid when interpreted as AVProResRawColorParams
     * (codec.prores_raw).
     */
    AV_RAW_COLOR_PARAMS_PRORES_RAW,
};

/**
 * ProRes RAW per-frame color transform, parsed from the prrf frame header.
 *
 * The correct rendering pipeline is:
 *  -> (sample - black_level) / (white_level - black_level)
 *  -> per-channel white balance (wb_red, 1.0 for G, wb_blue) pre-debayer
 *  -> debayer
 *  -> color_matrix (camera RGB -> CIE 1931 XYZ relative to D65)
 *  -> gain (scene-linear scale)
 *
 * Black/white levels live on the outer AVRawColorParams. The matrix output
 * is linear-light CIE XYZ D65; convert to a working RGB space downstream.
 *
 * @note The struct must be allocated as part of AVRawColorParams using
 *       av_raw_color_params_alloc(). Its size is not a part of the public ABI.
 */
typedef struct AVProResRawColorParams {
    /**
     * White balance multiplier for the red channel, applied pre-debayer.
     */
    AVRational wb_red;

    /**
     * White balance multiplier for the blue channel, applied pre-debayer.
     * (The green channel is implicit 1.0 for ProRes RAW)
     */
    AVRational wb_blue;

    /**
     * 3x3 row-major color matrix from camera RGB to linear-light CIE 1931
     * XYZ relative to the D65 illuminant, applied post-debayer.
     * out[i] = sum_j color_matrix[i][j] * in[j].
     */
    AVRational color_matrix[3][3];

    /**
     * Post-matrix scene-linear scaling factor. Encodes highlight headroom the
     * encoder reserved; multiply the matrixed values by this to recover
     * scene-linear light.
     */
    AVRational gain;
} AVProResRawColorParams;

/**
 * Per-frame color information for a RAW camera codec. Carried as side data of
 * type AV_FRAME_DATA_RAW_COLOR_PARAMS.
 *
 * The outer struct carries the fields every RAW codec exposes: the sensor's
 * valid sample range and the white-balance correlated color temperature.
 * The codec union holds the codec-specific transform parameters; `type`
 * selects which member of the union is valid.
 *
 * The codec-specific transform (color_matrix or equivalent) always lands in
 * linear-light CIE 1931 XYZ relative to the D65 illuminant, cameras don't
 * have standard primaries, so XYZ is the only common target.
 *
 * @note The struct must be allocated using av_raw_color_params_alloc() or
 *       av_raw_color_params_create_side_data(). Its size is not a part of the
 *       public ABI.
 */
typedef struct AVRawColorParams {
    /**
     * Selects which member of `codec` is valid.
     */
    enum AVRawColorParamsType type;

    /**
     * Lowest valid raw sample code (sensor black point)
     */
    AVRational black_level;

    /**
     * Highest valid raw sample code (sensor white point)
     */
    AVRational white_level;

    /**
     * Color temperature in Kelvin from with the camera's white balance.
     * Informational; the math uses the codec-specific white-balance fields.
     * 0 if not signaled.
     */
    uint32_t wb_cct;

    /**
     * Additional codec-specific fields.
     */
    union {
        AVProResRawColorParams prores_raw;
    } codec;
} AVRawColorParams;

/**
 * Allocate an AVRawColorParams structure and zero-initialize it.
 *
 * @param size if non-NULL, set to sizeof(AVRawColorParams)
 * @return the newly allocated struct or NULL on failure
 */
AVRawColorParams *av_raw_color_params_alloc(size_t *size);

/**
 * Allocate and add an AVRawColorParams structure to an existing AVFrame as
 * AV_FRAME_DATA_RAW_COLOR_PARAMS side data.
 *
 * @return the newly allocated struct, or NULL on failure
 */
AVRawColorParams *av_raw_color_params_create_side_data(AVFrame *frame);

#endif /* AVUTIL_RAW_COLOR_PARAMS_H */
