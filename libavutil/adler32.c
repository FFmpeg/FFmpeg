/* adler32.c -- compute the Adler-32 checksum of a data stream
 * Copyright (C) 1995 Mark Adler
 * For conditions of distribution and use, see copyright notice in zlib.h
 */

#include "common.h"
#include "adler32.h"

#define BASE 65521L /* largest prime smaller than 65536 */

#define DO1(buf)  {s1 += *buf++; s2 += s1;}
#define DO4(buf)  DO1(buf); DO1(buf); DO1(buf); DO1(buf);
#define DO16(buf) DO4(buf); DO4(buf); DO4(buf); DO4(buf);

unsigned long av_adler32_update(unsigned long adler, const uint8_t *buf, unsigned int len)
{
    unsigned long s1 = adler & 0xffff;
    unsigned long s2 = adler >> 16;

    while (len>0) {
#ifdef CONFIG_SMALL
        while(len>4 && s2 < (1U<<31)){
            DO4(buf); len-=4;
#else
        while(len>16 && s2 < (1U<<31)){
            DO16(buf); len-=16;
#endif
        }
        DO1(buf); len--;
        s1 %= BASE;
        s2 %= BASE;
    }
    return (s2 << 16) | s1;
}

#ifdef TEST
#include "log.h"
#define LEN 7001
volatile int checksum;
int main(){
    int i;
    char data[LEN];
    av_log_level = AV_LOG_DEBUG;
    for(i=0; i<LEN; i++)
        data[i]= ((i*i)>>3) + 123*i;
    for(i=0; i<1000; i++){
        START_TIMER
        checksum= av_adler32_update(1, data, LEN);
        STOP_TIMER("adler")
    }
    av_log(NULL, AV_LOG_DEBUG, "%X == 50E6E508\n", checksum);
}
#endif
