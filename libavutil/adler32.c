/*
 * Compute the Adler-32 checksum of a data stream.
 * This is a modified version based on adler32.c from the zlib library.
 *
 * Copyright (C) 1995 Mark Adler
 *
 * This software is provided 'as-is', without any express or implied
 * warranty.  In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 */

/**
 * @file
 * Computes the Adler-32 checksum of a data stream
 *
 * This is a modified version based on adler32.c from the zlib library.
 * @author Mark Adler
 * @ingroup lavu_adler32
 */

#include "config.h"
#include "adler32.h"
#include "common.h"
#include "intreadwrite.h"

#define BASE 65521L /* largest prime smaller than 65536 */

#define DO1(buf)  { s1 += *buf++; s2 += s1; }
#define DO4(buf)  DO1(buf); DO1(buf); DO1(buf); DO1(buf);
#define DO16(buf) DO4(buf); DO4(buf); DO4(buf); DO4(buf);

unsigned long av_adler32_update(unsigned long adler, const uint8_t * buf,
                                unsigned int len)
{
    unsigned long s1 = adler & 0xffff;
    unsigned long s2 = adler >> 16;

    while (len > 0) {
#if HAVE_FAST_64BIT && HAVE_FAST_UNALIGNED && !CONFIG_SMALL
        unsigned len2 = FFMIN((len-1) & ~7, 23*8);
        if (len2) {
            uint64_t a1= 0;
            uint64_t a2= 0;
            uint64_t b1= 0;
            uint64_t b2= 0;
            len -= len2;
            s2 += s1*len2;
            while (len2 >= 8) {
                uint64_t v = AV_RN64(buf);
                a2 += a1;
                b2 += b1;
                a1 +=  v    &0x00FF00FF00FF00FF;
                b1 += (v>>8)&0x00FF00FF00FF00FF;
                len2 -= 8;
                buf+=8;
            }

            //We combine the 8 interleaved adler32 checksums without overflows
            //Decreasing the number of iterations would allow below code to be
            //simplified but would likely be slower due to the fewer iterations
            //of the inner loop
            s1 += ((a1+b1)*0x1000100010001)>>48;
            s2 += ((((a2&0xFFFF0000FFFF)+(b2&0xFFFF0000FFFF)+((a2>>16)&0xFFFF0000FFFF)+((b2>>16)&0xFFFF0000FFFF))*0x800000008)>>32)
#if HAVE_BIGENDIAN
                 + 2*((b1*0x1000200030004)>>48)
                 +   ((a1*0x1000100010001)>>48)
                 + 2*((a1*0x0000100020003)>>48);
#else
                 + 2*((a1*0x4000300020001)>>48)
                 +   ((b1*0x1000100010001)>>48)
                 + 2*((b1*0x3000200010000)>>48);
#endif
        }
#else
        while (len > 4  && s2 < (1U << 31)) {
            DO4(buf);
            len -= 4;
        }
#endif
        DO1(buf); len--;
        s1 %= BASE;
        s2 %= BASE;
    }
    return (s2 << 16) | s1;
}
