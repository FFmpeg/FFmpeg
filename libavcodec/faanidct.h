/*
 * Floating point AAN IDCT
 * Copyright (c) 2008 Michael Niedermayer <michaelni@gmx.at>
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

#ifndef AVCODEC_FAANIDCT_H
#define AVCODEC_FAANIDCT_H

#include <stdint.h>
#include "dsputil.h"

void ff_faanidct(DCTELEM block[64]);
void ff_faanidct_add(uint8_t *dest, int line_size, DCTELEM block[64]);
void ff_faanidct_put(uint8_t *dest, int line_size, DCTELEM block[64]);

#endif /* AVCODEC_FAANIDCT_H */
