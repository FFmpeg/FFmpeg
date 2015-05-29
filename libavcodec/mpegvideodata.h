/*
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

#ifndef AVCODEC_MPEGVIDEODATA_H
#define AVCODEC_MPEGVIDEODATA_H

#include <stdint.h>

/* encoding scans */
extern const uint8_t ff_alternate_horizontal_scan[64];
extern const uint8_t ff_alternate_vertical_scan[64];

extern const uint8_t ff_mpeg1_dc_scale_table[128];
extern const uint8_t * const ff_mpeg2_dc_scale_table[4];

extern const uint8_t ff_default_chroma_qscale_table[32];

#endif /* AVCODEC_MPEGVIDEODATA_H */
