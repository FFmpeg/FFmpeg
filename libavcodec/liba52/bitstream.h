/*
 * bitstream.h
 * Copyright (C) 2000-2003 Michel Lespinasse <walken@zoy.org>
 * Copyright (C) 1999-2000 Aaron Holtzman <aholtzma@ess.engr.uvic.ca>
 *
 * This file is part of a52dec, a free ATSC A-52 stream decoder.
 * See http://liba52.sourceforge.net/ for updates.
 *
 * a52dec is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * a52dec is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/* (stolen from the kernel) */
#ifdef WORDS_BIGENDIAN

#	define swab32(x) (x)

#else

#	if 0 && defined (__i386__)

#	define swab32(x) __i386_swab32(x)
	static inline const uint32_t __i386_swab32(uint32_t x)
	{
		__asm__("bswap %0" : "=r" (x) : "0" (x));
		return x;
	}

#	else

#	define swab32(x)\
((((uint8_t*)&x)[0] << 24) | (((uint8_t*)&x)[1] << 16) |  \
 (((uint8_t*)&x)[2] << 8)  | (((uint8_t*)&x)[3]))

#	endif
#endif

void a52_bitstream_set_ptr (a52_state_t * state, uint8_t * buf);
uint32_t a52_bitstream_get_bh (a52_state_t * state, uint32_t num_bits);
int32_t a52_bitstream_get_bh_2 (a52_state_t * state, uint32_t num_bits);

static inline uint32_t bitstream_get (a52_state_t * state, uint32_t num_bits)
{
    uint32_t result;
	
    if (num_bits < state->bits_left) {
	result = (state->current_word << (32 - state->bits_left)) >> (32 - num_bits);
	state->bits_left -= num_bits;
	return result;
    }

    return a52_bitstream_get_bh (state, num_bits);
}

static inline int32_t bitstream_get_2 (a52_state_t * state, uint32_t num_bits)
{
    int32_t result;
	
    if (num_bits < state->bits_left) {
	result = (((int32_t)state->current_word) << (32 - state->bits_left)) >> (32 - num_bits);
	state->bits_left -= num_bits;
	return result;
    }

    return a52_bitstream_get_bh_2 (state, num_bits);
}
