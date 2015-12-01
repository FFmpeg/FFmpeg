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

#ifndef AVCODEC_AARCH64_ASM_OFFSETS_H
#define AVCODEC_AARCH64_ASM_OFFSETS_H

/* CeltIMDCTContext */
#define CELT_EXPTAB                     0x20
#define CELT_FFT_N                      0x00
#define CELT_LEN2                       0x04
#define CELT_LEN4                       (CELT_LEN2 + 0x4)   // loaded as pair
#define CELT_TMP                        0x10
#define CELT_TWIDDLE                    (CELT_TMP + 0x8)    // loaded as pair

/* FFTContext */
#define IMDCT_HALF                      0x48

#endif /* AVCODEC_AARCH64_ASM_OFFSETS_H */
