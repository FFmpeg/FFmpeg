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

#ifndef AVCODEC_APV_H
#define AVCODEC_APV_H

// Signature value in APV bitstreams (section 5.3.1).
#define APV_SIGNATURE MKBETAG('a', 'P', 'v', '1')

// PBU types (section 5.3.3).
enum {
    APV_PBU_PRIMARY_FRAME           = 1,
    APV_PBU_NON_PRIMARY_FRAME       = 2,
    APV_PBU_PREVIEW_FRAME           = 25,
    APV_PBU_DEPTH_FRAME             = 26,
    APV_PBU_ALPHA_FRAME             = 27,
    APV_PBU_ACCESS_UNIT_INFORMATION = 65,
    APV_PBU_METADATA                = 66,
    APV_PBU_FILLER                  = 67,
};

// Format parameters (section 4.2).
enum {
    APV_MAX_NUM_COMP = 4,
    APV_MB_WIDTH     = 16,
    APV_MB_HEIGHT    = 16,
    APV_TR_SIZE      = 8,
};

// Chroma formats (section 4.2).
enum {
    APV_CHROMA_FORMAT_400  = 0,
    APV_CHROMA_FORMAT_422  = 2,
    APV_CHROMA_FORMAT_444  = 3,
    APV_CHROMA_FORMAT_4444 = 4,
};

// Coefficient limits (section 5.3.15).
enum {
    APV_BLK_COEFFS      = (APV_TR_SIZE * APV_TR_SIZE),
    APV_MIN_TRANS_COEFF = -32768,
    APV_MAX_TRANS_COEFF =  32767,
};

// Profiles (section 10.1.3).
enum {
    APV_PROFILE_422_10  = 33,
    APV_PROFILE_422_12  = 44,
    APV_PROFILE_444_10  = 55,
    APV_PROFILE_444_12  = 66,
    APV_PROFILE_4444_10 = 77,
    APV_PROFILE_4444_12 = 88,
    APV_PROFILE_400_10  = 99,
};

// General level limits for tiles (section 10.1.4.1).
enum {
    APV_MIN_TILE_WIDTH_IN_MBS  = 16,
    APV_MIN_TILE_HEIGHT_IN_MBS = 8,
    APV_MAX_TILE_COLS          = 20,
    APV_MAX_TILE_ROWS          = 20,
    APV_MAX_TILE_COUNT         = APV_MAX_TILE_COLS * APV_MAX_TILE_ROWS,
};

// Metadata types (section 10.3.1).
enum {
    APV_METADATA_ITU_T_T35    = 4,
    APV_METADATA_MDCV         = 5,
    APV_METADATA_CLL          = 6,
    APV_METADATA_FILLER       = 10,
    APV_METADATA_USER_DEFINED = 170,
};

#endif /* AVCODEC_APV_H */
