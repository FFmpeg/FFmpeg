/* adler32.c -- compute the Adler-32 checksum of a data stream
 * Copyright (C) 1995 Mark Adler
 * For conditions of distribution and use, see copyright notice in zlib.h
 */

#include "common.h"
#include "adler32.h"

#define BASE 65521L /* largest prime smaller than 65536 */
#define NMAX 5552
/* NMAX is the largest n such that 255n(n+1)/2 + (n+1)(BASE-1) <= 2^32-1 */

#define DO1(buf)  {s1 += *buf++; s2 += s1;}
#define DO4(buf)  DO1(buf); DO1(buf); DO1(buf); DO1(buf);
#define DO16(buf) DO4(buf); DO4(buf); DO4(buf); DO4(buf);

unsigned long av_adler32_update(unsigned long adler, const uint8_t *buf, unsigned int len)
{
    unsigned long s1 = adler & 0xffff;
    unsigned long s2 = (adler >> 16) & 0xffff;

    while (len > 0) {
        int k = FFMIN(len, NMAX);
        len -= k;
#ifndef CONFIG_SMALL
        while (k >= 16) {
            DO16(buf);
            k -= 16;
        }
#endif
        while(k--) {
            DO1(buf);
        }
        s1 %= BASE;
        s2 %= BASE;
    }
    return (s2 << 16) | s1;
}
