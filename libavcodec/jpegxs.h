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

#ifndef AVCODEC_JPEGXS_H
#define AVCODEC_JPEGXS_H

enum {
    JPEGXS_MARKER_SOC = 0xff10, // Start of codestream
    JPEGXS_MARKER_EOC = 0xff11, // End of codestream
    JPEGXS_MARKER_PIH = 0xff12, // Picture header
    JPEGXS_MARKER_CDT = 0xff13, // Component table
    JPEGXS_MARKER_WGT = 0xff14, // Weights table
    JPEGXS_MARKER_COM = 0xff15, // Extension marker
    JPEGXS_MARKER_NLT = 0xff16, // Nonlinearity marker
    JPEGXS_MARKER_CWD = 0xff17, // Component-dependent wavelet decomposition marker
    JPEGXS_MARKER_CTS = 0xff18, // Colour transformation specification marker
    JPEGXS_MARKER_CRG = 0xff19, // Component registration marker
    JPEGXS_MARKER_SLH = 0xff20, // Slice header
    JPEGXS_MARKER_CAP = 0xff50, // Capabilities Marker
};

#endif /* AVCODEC_JPEGXS_H */
