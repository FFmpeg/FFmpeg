/*
 * copyright (c) 2007 Michael Niedermayer <michaelni@gmx.at>
 *
 * some optimization ideas from aes128.c by Reimar Doeffinger
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
 */

#include "common.h"
#include "aes.h"

typedef struct AVAES{
    // Note: round_key[16] is accessed in the init code, but this only
    // overwrites state, which does not matter (see also r7471).
    uint8_t round_key[15][4][4];
    uint8_t state[2][4][4];
    int rounds;
}AVAES;

const int av_aes_size= sizeof(AVAES);

static const uint8_t rcon[10] = {
  0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36
};

static uint8_t     sbox[256];
static uint8_t inv_sbox[256];
#if CONFIG_SMALL
static uint32_t enc_multbl[1][256];
static uint32_t dec_multbl[1][256];
#else
static uint32_t enc_multbl[4][256];
static uint32_t dec_multbl[4][256];
#endif

static inline void addkey(uint64_t dst[2], const uint64_t src[2], const uint64_t round_key[2]){
    dst[0] = src[0] ^ round_key[0];
    dst[1] = src[1] ^ round_key[1];
}

static void subshift(uint8_t s0[2][16], int s, const uint8_t *box){
    uint8_t (*s1)[16]= s0[0] - s;
    uint8_t (*s3)[16]= s0[0] + s;
    s0[0][0]=box[s0[1][ 0]]; s0[0][ 4]=box[s0[1][ 4]]; s0[0][ 8]=box[s0[1][ 8]]; s0[0][12]=box[s0[1][12]];
    s1[0][3]=box[s1[1][ 7]]; s1[0][ 7]=box[s1[1][11]]; s1[0][11]=box[s1[1][15]]; s1[0][15]=box[s1[1][ 3]];
    s0[0][2]=box[s0[1][10]]; s0[0][10]=box[s0[1][ 2]]; s0[0][ 6]=box[s0[1][14]]; s0[0][14]=box[s0[1][ 6]];
    s3[0][1]=box[s3[1][13]]; s3[0][13]=box[s3[1][ 9]]; s3[0][ 9]=box[s3[1][ 5]]; s3[0][ 5]=box[s3[1][ 1]];
}

static inline int mix_core(uint32_t multbl[4][256], int a, int b, int c, int d){
#if CONFIG_SMALL
#define ROT(x,s) ((x<<s)|(x>>(32-s)))
    return multbl[0][a] ^ ROT(multbl[0][b], 8) ^ ROT(multbl[0][c], 16) ^ ROT(multbl[0][d], 24);
#else
    return multbl[0][a] ^ multbl[1][b] ^ multbl[2][c] ^ multbl[3][d];
#endif
}

static inline void mix(uint8_t state[2][4][4], uint32_t multbl[4][256], int s1, int s3){
    ((uint32_t *)(state))[0] = mix_core(multbl, state[1][0][0], state[1][s1  ][1], state[1][2][2], state[1][s3  ][3]);
    ((uint32_t *)(state))[1] = mix_core(multbl, state[1][1][0], state[1][s3-1][1], state[1][3][2], state[1][s1-1][3]);
    ((uint32_t *)(state))[2] = mix_core(multbl, state[1][2][0], state[1][s3  ][1], state[1][0][2], state[1][s1  ][3]);
    ((uint32_t *)(state))[3] = mix_core(multbl, state[1][3][0], state[1][s1-1][1], state[1][1][2], state[1][s3-1][3]);
}

static inline void crypt(AVAES *a, int s, const uint8_t *sbox, const uint32_t *multbl){
    int r;

    for(r=a->rounds-1; r>0; r--){
        mix(a->state, multbl, 3-s, 1+s);
        addkey(a->state[1], a->state[0], a->round_key[r]);
    }
    subshift(a->state[0][0], s, sbox);
}

void av_aes_crypt(AVAES *a, uint8_t *dst, const uint8_t *src, int count, uint8_t *iv, int decrypt){
    while(count--){
        addkey(a->state[1], src, a->round_key[a->rounds]);
        if(decrypt) {
            crypt(a, 0, inv_sbox, dec_multbl);
            if(iv){
                addkey(a->state[0], a->state[0], iv);
                memcpy(iv, src, 16);
            }
            addkey(dst, a->state[0], a->round_key[0]);
        }else{
            if(iv) addkey(a->state[1], a->state[1], iv);
            crypt(a, 2,     sbox, enc_multbl);
            addkey(dst, a->state[0], a->round_key[0]);
            if(iv) memcpy(iv, dst, 16);
        }
        src+=16;
        dst+=16;
    }
}

static void init_multbl2(uint8_t tbl[1024], const int c[4], const uint8_t *log8, const uint8_t *alog8, const uint8_t *sbox){
    int i, j;
    for(i=0; i<1024; i++){
        int x= sbox[i>>2];
        if(x) tbl[i]= alog8[ log8[x] + log8[c[i&3]] ];
    }
#if !CONFIG_SMALL
    for(j=256; j<1024; j++)
        for(i=0; i<4; i++)
            tbl[4*j+i]= tbl[4*j + ((i-1)&3) - 1024];
#endif
}

// this is based on the reference AES code by Paulo Barreto and Vincent Rijmen
int av_aes_init(AVAES *a, const uint8_t *key, int key_bits, int decrypt) {
    int i, j, t, rconpointer = 0;
    uint8_t tk[8][4];
    int KC= key_bits>>5;
    int rounds= KC + 6;
    uint8_t  log8[256];
    uint8_t alog8[512];

    if(!enc_multbl[0][sizeof(enc_multbl)/sizeof(enc_multbl[0][0])-1]){
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
        init_multbl2(dec_multbl[0], (const int[4]){0xe, 0x9, 0xd, 0xb}, log8, alog8, inv_sbox);
        init_multbl2(enc_multbl[0], (const int[4]){0x2, 0x1, 0x1, 0x3}, log8, alog8, sbox);
    }

    if(key_bits!=128 && key_bits!=192 && key_bits!=256)
        return -1;

    a->rounds= rounds;

    memcpy(tk, key, KC*4);

    for(t= 0; t < (rounds+1)*16;) {
        memcpy(a->round_key[0][0]+t, tk, KC*4);
        t+= KC*4;

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
            uint8_t tmp[3][16];
            memcpy(tmp[2], a->round_key[i][0], 16);
            subshift(tmp[1], 0, sbox);
            mix(tmp, dec_multbl, 1, 3);
            memcpy(a->round_key[i][0], tmp[0], 16);
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

#undef random

int main(void){
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
    uint8_t temp[16];

    av_aes_init(&ae, "PI=3.141592654..", 128, 0);
    av_aes_init(&ad, "PI=3.141592654..", 128, 1);
    av_log_level= AV_LOG_DEBUG;

    for(i=0; i<2; i++){
        av_aes_init(&b, rkey[i], 128, 1);
        av_aes_crypt(&b, temp, rct[i], 1, NULL, 1);
        for(j=0; j<16; j++)
            if(rpt[i][j] != temp[j])
                av_log(NULL, AV_LOG_ERROR, "%d %02X %02X\n", j, rpt[i][j], temp[j]);
    }

    for(i=0; i<10000; i++){
        for(j=0; j<16; j++){
            pt[j]= random();
        }
{START_TIMER
        av_aes_crypt(&ae, temp, pt, 1, NULL, 0);
        if(!(i&(i-1)))
            av_log(NULL, AV_LOG_ERROR, "%02X %02X %02X %02X\n", temp[0], temp[5], temp[10], temp[15]);
        av_aes_crypt(&ad, temp, temp, 1, NULL, 1);
STOP_TIMER("aes")}
        for(j=0; j<16; j++){
            if(pt[j] != temp[j]){
                av_log(NULL, AV_LOG_ERROR, "%d %d %02X %02X\n", i,j, pt[j], temp[j]);
            }
        }
    }
    return 0;
}
#endif
