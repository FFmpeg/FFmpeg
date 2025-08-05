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

#ifndef AVCODEC_ITUT35_H
#define AVCODEC_ITUT35_H

#define ITU_T_T35_COUNTRY_CODE_CN 0x26
#define ITU_T_T35_COUNTRY_CODE_UK 0xB4
#define ITU_T_T35_COUNTRY_CODE_US 0xB5

// The Terminal Provider Code (or "Manufacturer Code") identifies the
// manufacturer within a country. An Assignment Authority appointed by the
// national body assigns this code nationally. The manufacturer code is always
// used in conjunction with a country code.
// - CN providers
#define ITU_T_T35_PROVIDER_CODE_HDR_VIVID    0x0004
// - UK providers
// V-Nova should be 0x5000 according to UK Register of Manufacturer Codes
// https://www.cix.co.uk/~bpechey/H221/h221code.htm
// but FFmpeg has been using 0x0050
#define ITU_T_T35_PROVIDER_CODE_VNOVA        0x0050
// - US providers
#define ITU_T_T35_PROVIDER_CODE_ATSC         0x0031
#define ITU_T_T35_PROVIDER_CODE_DOLBY        0x003B
#define ITU_T_T35_PROVIDER_CODE_AOM          0x5890
#define ITU_T_T35_PROVIDER_CODE_SAMSUNG      0x003C

#endif /* AVCODEC_ITUT35_H */
