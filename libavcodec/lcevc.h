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

/**
 * @file
 * LCEVC common definitions
 */

#ifndef AVCODEC_LCEVC_H
#define AVCODEC_LCEVC_H

/*
 * Table 17 — NAL unit type codes and NAL unit type classes in
 * ISO/IEC 23094-2:2021
 */
enum {
    LCEVC_UNSPEC0_NUT  = 0,
    LCEVC_UNSPEC1_NUT  = 1,
    LCEVC_UNSPEC2_NUT  = 2,
    LCEVC_UNSPEC3_NUT  = 3,
    LCEVC_UNSPEC4_NUT  = 4,
    LCEVC_UNSPEC5_NUT  = 5,
    LCEVC_UNSPEC6_NUT  = 6,
    LCEVC_UNSPEC7_NUT  = 7,
    LCEVC_UNSPEC8_NUT  = 8,
    LCEVC_UNSPEC9_NUT  = 9,
    LCEVC_UNSPEC10_NUT = 10,
    LCEVC_UNSPEC11_NUT = 11,
    LCEVC_UNSPEC12_NUT = 12,
    LCEVC_UNSPEC13_NUT = 13,
    LCEVC_UNSPEC14_NUT = 14,
    LCEVC_UNSPEC15_NUT = 15,
    LCEVC_UNSPEC16_NUT = 16,
    LCEVC_UNSPEC17_NUT = 17,
    LCEVC_UNSPEC18_NUT = 18,
    LCEVC_UNSPEC19_NUT = 19,
    LCEVC_UNSPEC20_NUT = 20,
    LCEVC_UNSPEC21_NUT = 21,
    LCEVC_UNSPEC22_NUT = 22,
    LCEVC_UNSPEC23_NUT = 23,
    LCEVC_UNSPEC24_NUT = 24,
    LCEVC_UNSPEC25_NUT = 25,
    LCEVC_UNSPEC26_NUT = 26,
    LCEVC_UNSPEC27_NUT = 27,
    LCEVC_NON_IDR_NUT  = 28,
    LCEVC_IDR_NUT      = 29,
    LCEVC_RSV_NUT      = 30,
    LCEVC_UNSPEC31_NUT = 31,
};

/*
 * Table 19 — Content of payload
 */
enum {
    LCEVC_PAYLOAD_TYPE_SEQUENCE_CONFIG    = 0,
    LCEVC_PAYLOAD_TYPE_GLOBAL_CONFIG      = 1,
    LCEVC_PAYLOAD_TYPE_PICTURE_CONFIG     = 2,
    LCEVC_PAYLOAD_TYPE_ENCODED_DATA       = 3,
    LCEVC_PAYLOAD_TYPE_ENCODED_DATA_TILED = 4,
    LCEVC_PAYLOAD_TYPE_ADDITIONAL_INFO    = 5,
    LCEVC_PAYLOAD_TYPE_FILLER             = 6,
};

enum {
    LCEVC_ADDITIONAL_INFO_TYPE_SEI        = 0,
    LCEVC_ADDITIONAL_INFO_TYPE_VUI        = 1,
};

#endif /* AVCODEC_LCEVC_H */
