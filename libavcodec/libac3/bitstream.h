/* 
 *  bitstream.h
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

//My new and improved vego-matic endian swapping routine
//(stolen from the kernel)
#ifdef WORDS_BIGENDIAN

#	define swab32(x) (x)

#else

#	if defined (__i386__)

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

extern uint32_t bits_left;
extern uint32_t current_word;

void bitstream_set_ptr (uint8_t * buf);
uint32_t bitstream_get_bh(uint32_t num_bits);

static inline uint32_t 
bitstream_get(uint32_t num_bits)
{
    uint32_t result;
	
    if(num_bits < bits_left) {
	result = (current_word << (32 - bits_left)) >> (32 - num_bits);
	bits_left -= num_bits;
	return result;
    }

    return bitstream_get_bh(num_bits);
}
