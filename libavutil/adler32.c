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

#include "config.h"
#include "adler32.h"

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
#if CONFIG_SMALL
        while (len > 4  && s2 < (1U << 31)) {
            DO4(buf);
            len -= 4;
        }
#else
        while (len > 16 && s2 < (1U << 31)) {
            DO16(buf);
            len -= 16;
        }
#endif
        DO1(buf); len--;
        s1 %= BASE;
        s2 %= BASE;
    }
    return (s2 << 16) | s1;
}

#ifdef TEST
#include <string.h>
#include "log.h"
#include "timer.h"
#define LEN 7001

static volatile int checksum;

int main(int argc, char **argv)
{
    int i;
    char data[LEN];

    av_log_set_level(AV_LOG_DEBUG);

    for (i = 0; i < LEN; i++)
        data[i] = ((i * i) >> 3) + 123 * i;

    if (argc > 1 && !strcmp(argv[1], "-t")) {
        for (i = 0; i < 1000; i++) {
            START_TIMER;
            checksum = av_adler32_update(1, data, LEN);
            STOP_TIMER("adler");
        }
    } else {
        checksum = av_adler32_update(1, data, LEN);
    }

    av_log(NULL, AV_LOG_DEBUG, "%X (expected 50E6E508)\n", checksum);
    return checksum == 0x50e6e508 ? 0 : 1;
}
#endif
