/* 
 *  bitstream.c
 *
 *	Copyright (C) Aaron Holtzman - Dec 1999
 *
 *  This file is part of ac3dec, a free AC-3 audio decoder
 *	
 *  ac3dec is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *   
 *  ac3dec is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *   
 *  You should have received a copy of the GNU General Public License
 *  along with GNU Make; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA. 
 *
 */

#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>

#include "ac3.h"
#include "ac3_internal.h"
#include "bitstream.h"

#define BUFFER_SIZE 4096

static uint8_t *buffer_start;

uint32_t bits_left;
uint32_t current_word;

void bitstream_set_ptr (uint8_t * buf)
{
    buffer_start = buf;
    bits_left = 0;
}

static inline void
bitstream_fill_current()
{
    current_word = *((uint32_t*)buffer_start)++;
    current_word = swab32(current_word);
}

//
// The fast paths for _get is in the
// bitstream.h header file so it can be inlined.
//
// The "bottom half" of this routine is suffixed _bh
//
// -ah
//

uint32_t
bitstream_get_bh(uint32_t num_bits)
{
    uint32_t result;

    num_bits -= bits_left;
    result = (current_word << (32 - bits_left)) >> (32 - bits_left);

    bitstream_fill_current();

    if(num_bits != 0)
	result = (result << num_bits) | (current_word >> (32 - num_bits));
	
    bits_left = 32 - num_bits;

    return result;
}
