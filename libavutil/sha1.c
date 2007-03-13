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

/* (R0+R1), R2, R3, R4 are the different operations used in SHA1 */
#define R0b(v,w,x,y,z,i) z+((w&(x^y))^y)    +block[i]+0x5A827999+rol(v,5);
#define R2b(v,w,x,y,z,i) z+( w^x     ^y)    +block[i]+0x6ED9EBA1+rol(v,5);
#define R3b(v,w,x,y,z,i) z+(((w|x)&y)|(w&x))+block[i]+0x8F1BBCDC+rol(v,5);
#define R4b(v,w,x,y,z,i) z+( w^x     ^y)    +block[i]+0xCA62C1D6+rol(v,5);

#define R0(v,w,x,y,z,i) z+=((w&(x^y))^y)    +block[i]+0x5A827999+rol(v,5);w=rol(w,30);
#define R2(v,w,x,y,z,i) z+=( w^x     ^y)    +block[i]+0x6ED9EBA1+rol(v,5);w=rol(w,30);
#define R3(v,w,x,y,z,i) z+=(((w|x)&y)|(w&x))+block[i]+0x8F1BBCDC+rol(v,5);w=rol(w,30);
#define R4(v,w,x,y,z,i) z+=( w^x     ^y)    +block[i]+0xCA62C1D6+rol(v,5);w=rol(w,30);

/* Hash a single 512-bit block. This is the core of the algorithm. */

//#define VARIANT1
//#define VARIANT2

static void transform(uint32_t state[5], uint8_t buffer[64]){
    uint32_t block[80];
    unsigned int i;
#ifdef VARIANT1
    uint32_t s[85];
#else
    unsigned int a, b, c, d, e;
#endif

    for(i=0; i<16; i++)
        block[i]= be2me_32(((uint32_t*)buffer)[i]);
    for(;i<80; i++)
        block[i]= rol(block[i-3]^block[i-8]^block[i-14]^block[i-16],1);


#ifdef VARIANT1
    s[0]= state[4];
    s[1]= state[3];
    s[2]= state[2];
    s[3]= state[1];
    s[4]= state[0];
    for(i=0; i<20; i++){
        s[5+i]= R0b(s[4+i], s[3+i], s[2+i], s[1+i], s[i], i);
        s[3+i]= rol(s[3+i],30);
    }
    for(; i<40; i++){
        s[5+i]= R2b(s[4+i], s[3+i], s[2+i], s[1+i], s[i], i);
        s[3+i]= rol(s[3+i],30);
    }
    for(; i<60; i++){
        s[5+i]= R3b(s[4+i], s[3+i], s[2+i], s[1+i], s[i], i);
        s[3+i]= rol(s[3+i],30);
    }
    for(; i<80; i++){
        s[5+i]= R4b(s[4+i], s[3+i], s[2+i], s[1+i], s[i], i);
        s[3+i]= rol(s[3+i],30);
    }
    state[0] += s[84];
    state[1] += s[83];
    state[2] += s[82];
    state[3] += s[81];
    state[4] += s[80];
#else
    a = state[0];
    b = state[1];
    c = state[2];
    d = state[3];
    e = state[4];
#ifdef VARIANT2
    for(i=0; i<20; i++){
        int t= R0b(a,b,c,d,e,i);
        e= d;
        d= c;
        c= rol(b,30);
        b= a;
        a= t;
    }
    for(; i<40; i++){
        int t= R2b(a,b,c,d,e,i);
        e= d;
        d= c;
        c= rol(b,30);
        b= a;
        a= t;
    }
    for(; i<60; i++){
        int t= R3b(a,b,c,d,e,i);
        e= d;
        d= c;
        c= rol(b,30);
        b= a;
        a= t;
    }
    for(; i<80; i++){
        int t= R4b(a,b,c,d,e,i);
        e= d;
        d= c;
        c= rol(b,30);
        b= a;
        a= t;
    }
#else
    for(i=0; i<20; i+=5){
        R0(a,b,c,d,e,0+i); R0(e,a,b,c,d,1+i); R0(d,e,a,b,c,2+i); R0(c,d,e,a,b,3+i); R0(b,c,d,e,a,4+i);
    }
    for(; i<40; i+=5){
        R2(a,b,c,d,e,0+i); R2(e,a,b,c,d,1+i); R2(d,e,a,b,c,2+i); R2(c,d,e,a,b,3+i); R2(b,c,d,e,a,4+i);
    }
    for(; i<60; i+=5){
        R3(a,b,c,d,e,0+i); R3(e,a,b,c,d,1+i); R3(d,e,a,b,c,2+i); R3(c,d,e,a,b,3+i); R3(b,c,d,e,a,4+i);
    }
    for(; i<80; i+=5){
        R4(a,b,c,d,e,0+i); R4(e,a,b,c,d,1+i); R4(d,e,a,b,c,2+i); R4(c,d,e,a,b,3+i); R4(b,c,d,e,a,4+i);
    }
#endif
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
#endif
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

    j = context->count & 63;
    context->count += len;
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
    uint64_t finalcount= be2me_64(context->count<<3);

    av_sha1_update(context, "\200", 1);
    while ((context->count & 63) != 56) {
        av_sha1_update(context, "", 1);
    }
    av_sha1_update(context, &finalcount, 8);  /* Should cause a transform() */
    for(i=0; i<5; i++)
        ((uint32_t*)digest)[i]= be2me_32(context->state[i]);
}

// use the following to test
// gcc -DTEST -DHAVE_AV_CONFIG_H -I.. sha1.c -O2 -W -Wall -o sha1 && time ./sha1
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
