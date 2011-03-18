/*
 * Copyright (C) 2007 Marc Hoffman
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

/**
 * @file
 * byte swapping routines
 */

#ifndef AVUTIL_BFIN_BSWAP_H
#define AVUTIL_BFIN_BSWAP_H

#include <stdint.h>
#include "config.h"
#include "libavutil/attributes.h"

#define av_bswap32 av_bswap32
static av_always_inline av_const uint32_t av_bswap32(uint32_t x)
{
    unsigned tmp;
    __asm__("%1 = %0 >> 8 (V);      \n\t"
            "%0 = %0 << 8 (V);      \n\t"
            "%0 = %0 | %1;          \n\t"
            "%0 = PACK(%0.L, %0.H); \n\t"
            : "+d"(x), "=&d"(tmp));
    return x;
}

#endif /* AVUTIL_BFIN_BSWAP_H */
