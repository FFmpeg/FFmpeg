/*
 * Copyright (c) 2016 Ronald S. Bultje <rsbultje@gmail.com>
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

#ifndef AVUTIL_CSP_H
#define AVUTIL_CSP_H

#include "pixfmt.h"
#include "rational.h"

/**
 * @file
 * Colorspace value utility functions for libavutil.
 * @ingroup lavu_math_csp
 * @author Ronald S. Bultje <rsbultje@gmail.com>
 * @author Leo Izen <leo.izen@gmail.com>
 */

/**
 * @defgroup lavu_math_csp Colorspace Utility
 * @ingroup lavu_math
 * @{
 */

/**
 * Struct containing luma coefficients to be used for RGB to YUV/YCoCg, or similar
 * calculations.
 */
typedef struct AVLumaCoefficients {
    AVRational cr, cg, cb;
} AVLumaCoefficients;

/**
 * Struct containing chromaticity x and y values for the standard CIE 1931
 * chromaticity definition.
 */
typedef struct AVCIExy {
    AVRational x, y;
} AVCIExy;

/**
 * Struct defining the red, green, and blue primary locations in terms of CIE
 * 1931 chromaticity x and y.
 */
typedef struct AVPrimaryCoefficients {
    AVCIExy r, g, b;
} AVPrimaryCoefficients;

/**
 * Struct defining white point location in terms of CIE 1931 chromaticity x
 * and y.
 */
typedef AVCIExy AVWhitepointCoefficients;

/**
 * Struct that contains both white point location and primaries location, providing
 * the complete description of a color gamut.
 */
typedef struct AVColorPrimariesDesc {
    AVWhitepointCoefficients wp;
    AVPrimaryCoefficients prim;
} AVColorPrimariesDesc;

/**
 * Retrieves the Luma coefficients necessary to construct a conversion matrix
 * from an enum constant describing the colorspace.
 * @param csp An enum constant indicating YUV or similar colorspace.
 * @return The Luma coefficients associated with that colorspace, or NULL
 *     if the constant is unknown to libavutil.
 */
const AVLumaCoefficients *av_csp_luma_coeffs_from_avcsp(enum AVColorSpace csp);

/**
 * Retrieves a complete gamut description from an enum constant describing the
 * color primaries.
 * @param prm An enum constant indicating primaries
 * @return A description of the colorspace gamut associated with that enum
 *     constant, or NULL if the constant is unknown to libavutil.
 */
const AVColorPrimariesDesc *av_csp_primaries_desc_from_id(enum AVColorPrimaries prm);

/**
 * Detects which enum AVColorPrimaries constant corresponds to the given complete
 * gamut description.
 * @see enum AVColorPrimaries
 * @param prm A description of the colorspace gamut
 * @return The enum constant associated with this gamut, or
 *     AVCOL_PRI_UNSPECIFIED if no clear match can be idenitified.
 */
enum AVColorPrimaries av_csp_primaries_id_from_desc(const AVColorPrimariesDesc *prm);

/**
 * @}
 */

#endif /* AVUTIL_CSP_H */
