/*
 * JPEG-related tables
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

#ifndef AVCODEC_JPEGTABLES_H
#define AVCODEC_JPEGTABLES_H

#include <stdint.h>

#include "libavutil/internal.h"

extern av_export const uint8_t avpriv_mjpeg_bits_dc_luminance[];
extern av_export const uint8_t avpriv_mjpeg_val_dc[];

extern av_export const uint8_t avpriv_mjpeg_bits_dc_chrominance[];

extern av_export const uint8_t avpriv_mjpeg_bits_ac_luminance[];
extern av_export const uint8_t avpriv_mjpeg_val_ac_luminance[];

extern av_export const uint8_t avpriv_mjpeg_bits_ac_chrominance[];
extern av_export const uint8_t avpriv_mjpeg_val_ac_chrominance[];

void ff_mjpeg_build_huffman_codes(uint8_t *huff_size, uint16_t *huff_code,
                                  const uint8_t *bits_table,
                                  const uint8_t *val_table);

#endif /* AVCODEC_JPEGTABLES_H */
