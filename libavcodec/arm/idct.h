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

#ifndef AVCODEC_ARM_IDCT_H
#define AVCODEC_ARM_IDCT_H

#include <stdint.h>

void ff_j_rev_dct_arm(int16_t *data);

void ff_simple_idct_arm(int16_t *data);

void ff_simple_idct_armv5te(int16_t *data);
void ff_simple_idct_put_armv5te(uint8_t *dest, int line_size, int16_t *data);
void ff_simple_idct_add_armv5te(uint8_t *dest, int line_size, int16_t *data);

void ff_simple_idct_armv6(int16_t *data);
void ff_simple_idct_put_armv6(uint8_t *dest, int line_size, int16_t *data);
void ff_simple_idct_add_armv6(uint8_t *dest, int line_size, int16_t *data);

void ff_simple_idct_neon(int16_t *data);
void ff_simple_idct_put_neon(uint8_t *dest, int line_size, int16_t *data);
void ff_simple_idct_add_neon(uint8_t *dest, int line_size, int16_t *data);

#endif /* AVCODEC_ARM_IDCT_H */
