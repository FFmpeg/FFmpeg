/*
 * copyright (c) 2007 Michael Niedermayer <michaelni@gmx.at>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * some optimization ideas from aes128.c by Reimar Doeffinger
 */

#include "common.h"
#include "aes.h"

typedef struct AVAES{
    uint8_t round_key[15][4][4];
    uint8_t state[4][4];
    int rounds;
}AVAES;

const int av_aes_size= sizeof(AVAES);

static const uint8_t rcon[10] = {
  0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36
};

static uint8_t     sbox[256];
static uint8_t inv_sbox[256];
#ifdef CONFIG_SMALL
static uint32_t enc_multbl[1][256];
static uint32_t dec_multbl[1][256];
#else
static uint32_t enc_multbl[4][256];
static uint32_t dec_multbl[4][256];
#endif

static inline void addkey(uint64_t state[2], uint64_t round_key[2]){
    state[0] ^= round_key[0];
    state[1] ^= round_key[1];
}

static void subshift(uint8_t s0[4], uint8_t s1[4], uint8_t s2[4], uint8_t s3[4], uint8_t *box){
    int t;
             s0[0]=box[s0[ 0]]; s0[ 4]=box[s0[ 4]];          s0[ 8]=box[s0[ 8]]; s0[12]=box[s0[12]];
    t=s1[0]; s1[0]=box[s1[ 4]]; s1[ 4]=box[s1[ 8]];          s1[ 8]=box[s1[12]]; s1[12]=box[t];
    t=s2[0]; s2[0]=box[s2[ 8]]; s2[ 8]=box[    t]; t=s2[ 4]; s2[ 4]=box[s2[12]]; s2[12]=box[t];
    t=s3[0]; s3[0]=box[s3[12]]; s3[12]=box[s3[ 8]];          s3[ 8]=box[s3[ 4]]; s3[ 4]=box[t];
}

#define ROT(x,s) ((x<<s)|(x>>(32-s)))
#if 0
static inline void mix(uint8_t state[4][4], uint32_t multbl[4][256]){
    int i;
    for(i=0; i<4; i++)
#ifdef CONFIG_SMALL
        ((uint32_t *)(state))[i] =     multbl[0][state[i][0]]     ^ ROT(multbl[0][state[i][1]], 8)
                                  ^ROT(multbl[0][state[i][2]],16) ^ ROT(multbl[0][state[i][3]],24);
#else
        ((uint32_t *)(state))[i] = multbl[0][state[i][0]] ^ multbl[1][state[i][1]]
                                  ^multbl[2][state[i][2]] ^ multbl[3][state[i][3]];
#endif
}
#endif
static inline void mix2(uint8_t state[4][4], uint32_t multbl[4][256], int s1, int s3){
    int a = multbl[0][state[0][0]] ^ multbl[1][state[s1  ][1]]
           ^multbl[2][state[2][2]] ^ multbl[3][state[s3  ][3]];
    int b = multbl[0][state[1][0]] ^ multbl[1][state[s3-1][1]]
           ^multbl[2][state[3][2]] ^ multbl[3][state[s1-1][3]];
    int c = multbl[0][state[2][0]] ^ multbl[1][state[s3  ][1]]
           ^multbl[2][state[0][2]] ^ multbl[3][state[s1  ][3]];
    int d = multbl[0][state[3][0]] ^ multbl[1][state[s1-1][1]]
           ^multbl[2][state[1][2]] ^ multbl[3][state[s3-1][3]];

    ((uint32_t *)(state))[0]=a;
    ((uint32_t *)(state))[1]=b;
    ((uint32_t *)(state))[2]=c;
    ((uint32_t *)(state))[3]=d;
}

static inline void crypt(AVAES *a, int s, uint8_t *sbox, uint32_t *multbl){
    int t, r;

    for(r=a->rounds; r>1; r--){
        addkey(a->state, a->round_key[r]);
        mix2(a->state, multbl, 3-s, 1+s);
    }
    addkey(a->state, a->round_key[1]);
    subshift(a->state[0], a->state[0]+3-s, a->state[0]+2, a->state[0]+1+s, sbox);
    addkey(a->state, a->round_key[0]);
}

static void aes_decrypt(AVAES *a){
    crypt(a, 0, inv_sbox, dec_multbl);
}

static void aes_encrypt(AVAES *a){
    crypt(a, 2, sbox, enc_multbl);
}

static void init_multbl2(uint8_t tbl[1024], int c[4], uint8_t *log8, uint8_t *alog8, uint8_t *sbox){
    int i, j;
    for(i=0; i<1024; i++){
        int x= sbox[i>>2];
        if(x) tbl[i]= alog8[ log8[x] + log8[c[i&3]] ];
    }
#ifndef CONFIG_SMALL
    for(j=256; j<1024; j++)
        for(i=0; i<4; i++)
            tbl[4*j+i]= tbl[4*j + ((i-1)&3) - 1024];
#endif
}

// this is based on the reference AES code by Paulo Barreto and Vincent Rijmen
int av_aes_init(AVAES *a, uint8_t *key, int key_bits, int decrypt) {
    int i, j, t, rconpointer = 0;
    uint8_t tk[8][4];
    int KC= key_bits>>5;
    int rounds= KC + 6;
    uint8_t  log8[256];
    uint8_t alog8[512];

    if(!enc_multbl[4][1023]){
        j=1;
        for(i=0; i<255; i++){
            alog8[i]=
            alog8[i+255]= j;
            log8[j]= i;
            j^= j+j;
            if(j>255) j^= 0x11B;
        }
        for(i=0; i<256; i++){
            j= i ? alog8[255-log8[i]] : 0;
            j ^= (j<<1) ^ (j<<2) ^ (j<<3) ^ (j<<4);
            j = (j ^ (j>>8) ^ 99) & 255;
            inv_sbox[j]= i;
            sbox    [i]= j;
        }
        init_multbl2(dec_multbl[0], (int[4]){0xe, 0x9, 0xd, 0xb}, log8, alog8, inv_sbox);
        init_multbl2(enc_multbl[0], (int[4]){0x2, 0x1, 0x1, 0x3}, log8, alog8, sbox);
    }

    if(key_bits!=128 && key_bits!=192 && key_bits!=256)
        return -1;

    a->rounds= rounds;

    memcpy(tk, key, KC*4);

    for(t= 0; t < (rounds+1)*4;) {
        memcpy(a->round_key[0][t], tk, KC*4);
        t+= KC;

        for(i = 0; i < 4; i++)
            tk[0][i] ^= sbox[tk[KC-1][(i+1)&3]];
        tk[0][0] ^= rcon[rconpointer++];

        for(j = 1; j < KC; j++){
            if(KC != 8 || j != KC>>1)
                for(i = 0; i < 4; i++) tk[j][i] ^=      tk[j-1][i];
            else
                for(i = 0; i < 4; i++) tk[j][i] ^= sbox[tk[j-1][i]];
        }
    }

    if(decrypt){
        for(i=1; i<rounds; i++){
            subshift(a->round_key[i][0], a->round_key[i][0]+3, a->round_key[i][0]+2, a->round_key[i][0]+1, sbox);
            mix2(a->round_key[i], dec_multbl, 1, 3);
        }
    }else{
        for(i=0; i<(rounds+1)>>1; i++){
            for(j=0; j<16; j++)
                FFSWAP(int, a->round_key[i][0][j], a->round_key[rounds-i][0][j]);
        }
    }

    return 0;
}

#ifdef TEST
#include "log.h"

int main(){
    int i,j;
    AVAES ae, ad, b;
    uint8_t rkey[2][16]= {
        {0},
        {0x10, 0xa5, 0x88, 0x69, 0xd7, 0x4b, 0xe5, 0xa3, 0x74, 0xcf, 0x86, 0x7c, 0xfb, 0x47, 0x38, 0x59}};
    uint8_t pt[16], rpt[2][16]= {
        {0x6a, 0x84, 0x86, 0x7c, 0xd7, 0x7e, 0x12, 0xad, 0x07, 0xea, 0x1b, 0xe8, 0x95, 0xc5, 0x3f, 0xa3},
        {0}};
    uint8_t rct[2][16]= {
        {0x73, 0x22, 0x81, 0xc0, 0xa0, 0xaa, 0xb8, 0xf7, 0xa5, 0x4a, 0x0c, 0x67, 0xa0, 0xc4, 0x5e, 0xcf},
        {0x6d, 0x25, 0x1e, 0x69, 0x44, 0xb0, 0x51, 0xe0, 0x4e, 0xaa, 0x6f, 0xb4, 0xdb, 0xf7, 0x84, 0x65}};

    av_aes_init(&ae, "PI=3.141592654..", 128, 0);
    av_aes_init(&ad, "PI=3.141592654..", 128, 1);
    av_log_level= AV_LOG_DEBUG;

    for(i=0; i<2; i++){
        av_aes_init(&b, rkey[i], 128, 1);
        memcpy(b.state, rct[i], 16);
        aes_decrypt(&b);
        for(j=0; j<16; j++)
            if(rpt[i][j] != b.state[0][j])
                av_log(NULL, AV_LOG_ERROR, "%d %02X %02X\n", j, rpt[i][j], b.state[0][j]);
    }

    for(i=0; i<10000; i++){
        for(j=0; j<16; j++){
            pt[j]= random();
        }
        memcpy(ae.state, pt, 16);
{START_TIMER
        aes_encrypt(&ae);
        if(!(i&(i-1)))
            av_log(NULL, AV_LOG_ERROR, "%02X %02X %02X %02X\n", ae.state[0][0], ae.state[1][1], ae.state[2][2], ae.state[3][3]);
        memcpy(ad.state, ae.state, 16);
        aes_decrypt(&ad);
STOP_TIMER("aes")}
        for(j=0; j<16; j++){
            if(pt[j] != ad.state[0][j]){
                av_log(NULL, AV_LOG_ERROR, "%d %d %02X %02X\n", i,j, pt[j], ad.state[0][j]);
            }
        }
    }
    return 0;
}
#endif
