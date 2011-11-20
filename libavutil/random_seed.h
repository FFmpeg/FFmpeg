/*
 * Copyright (c) 2009 Baptiste Coudurier <baptiste.coudurier@gmail.com>
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

#ifndef AVUTIL_RANDOM_SEED_H
#define AVUTIL_RANDOM_SEED_H

#include <stdint.h>
/**
 * @addtogroup lavu_crypto
 * @{
 */

/**
 * Get random data.
 *
 * This function can be called repeatedly to generate more random bits
 * as needed. It is generally quite slow, and usually used to seed a
 * PRNG.  As it uses /dev/urandom and /dev/random, the quality of the
 * returned random data depends on the platform.
 */
uint32_t av_get_random_seed(void);

/**
 * @}
 */

#endif /* AVUTIL_RANDOM_SEED_H */
