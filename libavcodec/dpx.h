/*
 * DPX (.dpx) image decoder
 * Copyright (c) 2009 Jimmy Christensen
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

#ifndef AVCODEC_DPX_H
#define AVCODEC_DPX_H

enum DPX_TRC {
    DPX_TRC_USER_DEFINED       = 0,
    DPX_TRC_PRINTING_DENSITY   = 1,
    DPX_TRC_LINEAR             = 2,
    DPX_TRC_LOGARITHMIC        = 3,
    DPX_TRC_UNSPECIFIED_VIDEO  = 4,
    DPX_TRC_SMPTE_274          = 5,
    DPX_TRC_ITU_R_709_4        = 6,
    DPX_TRC_ITU_R_601_625      = 7,
    DPX_TRC_ITU_R_601_525      = 8,
    DPX_TRC_SMPTE_170          = 9,
    DPX_TRC_ITU_R_624_4_PAL    = 10,
    DPX_TRC_Z_LINEAR           = 11,
    DPX_TRC_Z_HOMOGENEOUS      = 12,
};

enum DPX_COL_SPEC {
    DPX_COL_SPEC_USER_DEFINED       = 0,
    DPX_COL_SPEC_PRINTING_DENSITY   = 1,
    /* 2 = N/A */
    /* 3 = N/A */
    DPX_COL_SPEC_UNSPECIFIED_VIDEO  = 4,
    DPX_COL_SPEC_SMPTE_274          = 5,
    DPX_COL_SPEC_ITU_R_709_4        = 6,
    DPX_COL_SPEC_ITU_R_601_625      = 7,
    DPX_COL_SPEC_ITU_R_601_525      = 8,
    DPX_COL_SPEC_SMPTE_170          = 9,
    DPX_COL_SPEC_ITU_R_624_4_PAL    = 10,
    /* 11 = N/A */
    /* 12 = N/A */
};

#endif /* AVCODEC_DPX_H */
