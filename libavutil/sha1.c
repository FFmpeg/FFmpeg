// SHA-1 code Copyright 2007 Michael Nidermayer <michaelni@gmx.at>
// license LGPL
// based on public domain SHA-1 code by Steve Reid <steve@edmweb.com>

#include "common.h"
#include "sha1.h"

typedef struct AVSHA1 {
    uint32_t state[5];
    uint64_t count;
    uint8_t buffer[64];
} AVSHA1;

#define rol(value, bits) (((value) << (bits)) | ((value) >> (32 - (bits))))

/* blk0() and blk() perform the initial expand. */
/* I got the idea of expanding during the round function from SSLeay */
#define blk0(i) (block[i] = be2me_32(block[i]))
#define blk(i) (block[i&15] = rol(block[(i+13)&15]^block[(i+8)&15]^block[(i+2)&15]^block[i&15],1))

/* (R0+R1), R2, R3, R4 are the different operations used in SHA1 */
#define R0(v,w,x,y,z,i) z+=((w&(x^y))^y)    +blk0(i)+0x5A827999+rol(v,5);w=rol(w,30);
#define R1(v,w,x,y,z,i) z+=((w&(x^y))^y)    +blk (i)+0x5A827999+rol(v,5);w=rol(w,30);
#define R2(v,w,x,y,z,i) z+=( w^x     ^y)    +blk (i)+0x6ED9EBA1+rol(v,5);w=rol(w,30);
#define R3(v,w,x,y,z,i) z+=(((w|x)&y)|(w&x))+blk (i)+0x8F1BBCDC+rol(v,5);w=rol(w,30);
#define R4(v,w,x,y,z,i) z+=( w^x     ^y)    +blk (i)+0xCA62C1D6+rol(v,5);w=rol(w,30);

/* Hash a single 512-bit block. This is the core of the algorithm. */

static void transform(uint32_t state[5], uint8_t buffer[64]){
    unsigned int a, b, c, d, e, i;
    uint32_t block[16];

    memcpy(block, buffer, 64);

    /* Copy context->state[] to working vars */
    a = state[0];
    b = state[1];
    c = state[2];
    d = state[3];
    e = state[4];
#if 1
    for(i=0; i<15; i+=5){
        R0(a,b,c,d,e,0+i); R0(e,a,b,c,d,1+i); R0(d,e,a,b,c,2+i); R0(c,d,e,a,b,3+i); R0(b,c,d,e,a,4+i);
    }
    R0(a,b,c,d,e,15); R1(e,a,b,c,d,16); R1(d,e,a,b,c,17); R1(c,d,e,a,b,18); R1(b,c,d,e,a,19);
    for(i=20; i<40; i+=5){
        R2(a,b,c,d,e,0+i); R2(e,a,b,c,d,1+i); R2(d,e,a,b,c,2+i); R2(c,d,e,a,b,3+i); R2(b,c,d,e,a,4+i);
    }
    for(; i<60; i+=5){
        R3(a,b,c,d,e,0+i); R3(e,a,b,c,d,1+i); R3(d,e,a,b,c,2+i); R3(c,d,e,a,b,3+i); R3(b,c,d,e,a,4+i);
    }
    for(; i<80; i+=5){
        R4(a,b,c,d,e,0+i); R4(e,a,b,c,d,1+i); R4(d,e,a,b,c,2+i); R4(c,d,e,a,b,3+i); R4(b,c,d,e,a,4+i);
    }
#else
    R0(a,b,c,d,e, 0); R0(e,a,b,c,d, 1); R0(d,e,a,b,c, 2); R0(c,d,e,a,b, 3);
    R0(b,c,d,e,a, 4); R0(a,b,c,d,e, 5); R0(e,a,b,c,d, 6); R0(d,e,a,b,c, 7);
    R0(c,d,e,a,b, 8); R0(b,c,d,e,a, 9); R0(a,b,c,d,e,10); R0(e,a,b,c,d,11);
    R0(d,e,a,b,c,12); R0(c,d,e,a,b,13); R0(b,c,d,e,a,14); R0(a,b,c,d,e,15);
    R1(e,a,b,c,d,16); R1(d,e,a,b,c,17); R1(c,d,e,a,b,18); R1(b,c,d,e,a,19);
    R2(a,b,c,d,e,20); R2(e,a,b,c,d,21); R2(d,e,a,b,c,22); R2(c,d,e,a,b,23);
    R2(b,c,d,e,a,24); R2(a,b,c,d,e,25); R2(e,a,b,c,d,26); R2(d,e,a,b,c,27);
    R2(c,d,e,a,b,28); R2(b,c,d,e,a,29); R2(a,b,c,d,e,30); R2(e,a,b,c,d,31);
    R2(d,e,a,b,c,32); R2(c,d,e,a,b,33); R2(b,c,d,e,a,34); R2(a,b,c,d,e,35);
    R2(e,a,b,c,d,36); R2(d,e,a,b,c,37); R2(c,d,e,a,b,38); R2(b,c,d,e,a,39);
    R3(a,b,c,d,e,40); R3(e,a,b,c,d,41); R3(d,e,a,b,c,42); R3(c,d,e,a,b,43);
    R3(b,c,d,e,a,44); R3(a,b,c,d,e,45); R3(e,a,b,c,d,46); R3(d,e,a,b,c,47);
    R3(c,d,e,a,b,48); R3(b,c,d,e,a,49); R3(a,b,c,d,e,50); R3(e,a,b,c,d,51);
    R3(d,e,a,b,c,52); R3(c,d,e,a,b,53); R3(b,c,d,e,a,54); R3(a,b,c,d,e,55);
    R3(e,a,b,c,d,56); R3(d,e,a,b,c,57); R3(c,d,e,a,b,58); R3(b,c,d,e,a,59);
    R4(a,b,c,d,e,60); R4(e,a,b,c,d,61); R4(d,e,a,b,c,62); R4(c,d,e,a,b,63);
    R4(b,c,d,e,a,64); R4(a,b,c,d,e,65); R4(e,a,b,c,d,66); R4(d,e,a,b,c,67);
    R4(c,d,e,a,b,68); R4(b,c,d,e,a,69); R4(a,b,c,d,e,70); R4(e,a,b,c,d,71);
    R4(d,e,a,b,c,72); R4(c,d,e,a,b,73); R4(b,c,d,e,a,74); R4(a,b,c,d,e,75);
    R4(e,a,b,c,d,76); R4(d,e,a,b,c,77); R4(c,d,e,a,b,78); R4(b,c,d,e,a,79);
#endif
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
}

void av_sha1_init(AVSHA1* context){
    context->state[0] = 0x67452301;
    context->state[1] = 0xEFCDAB89;
    context->state[2] = 0x98BADCFE;
    context->state[3] = 0x10325476;
    context->state[4] = 0xC3D2E1F0;
    context->count    = 0;
}

void av_sha1_update(AVSHA1* context, uint8_t* data, unsigned int len){
    unsigned int i, j;

    j = (context->count >> 3) & 63;
    context->count += len << 3;
    if ((j + len) > 63) {
        memcpy(&context->buffer[j], data, (i = 64-j));
        transform(context->state, context->buffer);
        for ( ; i + 63 < len; i += 64) {
            transform(context->state, &data[i]);
        }
    }
    else i = 0;
    memcpy(&context->buffer[j], &data[i], len - i);
}

void av_sha1_final(AVSHA1* context, uint8_t digest[20]){
    int i;
    uint64_t finalcount= be2me_64(context->count);

    av_sha1_update(context, "\200", 1);
    while ((context->count & 504) != 448) {
        av_sha1_update(context, "\0", 1);
    }
    av_sha1_update(context, &finalcount, 8);  /* Should cause a transform() */
    for (i = 0; i < 20; i++) {
        digest[i] = context->state[i>>2] >> ((3-(i & 3)) * 8) ;
    }
}

// use the following to test
// gcc -DTEST -DHAVE_AV_CONFIG_H -I.. sha1.c -O3 -W -Wall -o sha1 && time ./sha1
#ifdef TEST
#include <stdio.h>
#undef printf

int main(){
    int i, k;
    AVSHA1 context;
    unsigned char digest[20];

    for(k=0; k<3; k++){
        av_sha1_init(&context);
        if(k==0)
            av_sha1_update(&context, "abc", 3);
        else if(k==1)
            av_sha1_update(&context, "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56);
        else
            for(i=0; i<1000*1000; i++)
                av_sha1_update(&context, "a", 1);
        av_sha1_final(&context, digest);
        for (i = 0; i < 20; i++)
            printf("%02X", digest[i]);
        putchar('\n');
    }
    //Test Vectors (from FIPS PUB 180-1)
    printf("A9993E36 4706816A BA3E2571 7850C26C 9CD0D89D\n"
           "84983E44 1C3BD26E BAAE4AA1 F95129E5 E54670F1\n"
           "34AA973C D4C4DAA4 F61EEB2B DBAD2731 6534016F\n");

    return 0;
}
#endif
