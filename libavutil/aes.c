/*
 * copyright (c) 2007 Michael Niedermayer <michaelni@gmx.at> and Reimar Doeffinger
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
#include "log.h"
#include "aes.h"

typedef struct AVAES{
    uint8_t round_enc_key[15][4][4];
    uint8_t round_dec_key[15][4][4];
    uint8_t state[4][4];
    int rounds;
}AVAES;

static const uint8_t rcon[11] = {
  0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36, 0x6c
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

#define SUBSHIFT0(s, box)         s[0]=box[s[ 0]]; s[ 4]=box[s[ 4]];          s[ 8]=box[s[ 8]]; s[12]=box[s[12]];
#define SUBSHIFT1(s, box) t=s[0]; s[0]=box[s[ 4]]; s[ 4]=box[s[ 8]];          s[ 8]=box[s[12]]; s[12]=box[t];
#define SUBSHIFT2(s, box) t=s[0]; s[0]=box[s[ 8]]; s[ 8]=box[    t]; t=s[ 4]; s[ 4]=box[s[12]]; s[12]=box[t];
#define SUBSHIFT3(s, box) t=s[0]; s[0]=box[s[12]]; s[12]=box[s[ 8]];          s[ 8]=box[s[ 4]]; s[ 4]=box[t];

#define SUBSHIFT1x(s) t=s[0]; s[0]=s[ 4]; s[ 4]=s[ 8];          s[ 8]=s[12]; s[12]=t;
#define SUBSHIFT2x(s) t=s[0]; s[0]=s[ 8]; s[ 8]=    t; t=s[ 4]; s[ 4]=s[12]; s[12]=t;
#define SUBSHIFT3x(s) t=s[0]; s[0]=s[12]; s[12]=s[ 8];          s[ 8]=s[ 4]; s[ 4]=t;

#define ROT(x,s) ((x<<s)|(x>>(32-s)))

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

void av_aes_decrypt(AVAES *a){
    int t, r;

    addkey(a->state, a->round_enc_key[a->rounds]);
    for(r=a->rounds-2; r>=0; r--){
        SUBSHIFT3x((a->state[0]+1))
        SUBSHIFT2x((a->state[0]+2))
        SUBSHIFT1x((a->state[0]+3))
        mix(a->state, dec_multbl);
        addkey(a->state, a->round_dec_key[r+1]);
    }
    SUBSHIFT0((a->state[0]+0), inv_sbox)
    SUBSHIFT3((a->state[0]+1), inv_sbox)
    SUBSHIFT2((a->state[0]+2), inv_sbox)
    SUBSHIFT1((a->state[0]+3), inv_sbox)
    addkey(a->state, a->round_enc_key[0]);
}

void av_aes_encrypt(AVAES *a){
    int r, t;

    for(r=0; r<a->rounds-1; r++){
        addkey(a->state, a->round_enc_key[r]);
        SUBSHIFT1x((a->state[0]+1))
        SUBSHIFT2x((a->state[0]+2))
        SUBSHIFT3x((a->state[0]+3))
        mix(a->state, enc_multbl); //FIXME replace log8 by const / optimze mix as this can be simplified alot
    }
    addkey(a->state, a->round_enc_key[r]);
    SUBSHIFT0((a->state[0]+0), sbox)
    SUBSHIFT1((a->state[0]+1), sbox)
    SUBSHIFT2((a->state[0]+2), sbox)
    SUBSHIFT3((a->state[0]+3), sbox)
    addkey(a->state, a->round_enc_key[r+1]);
}

static init_multbl2(uint8_t tbl[1024], int c[4], uint8_t *log8, uint8_t *alog8, uint8_t *sbox){
    int i;
    for(i=0; i<1024; i++){
        int x= sbox[i/4];
        if(x) tbl[i]= alog8[ log8[x] + log8[c[i&3]] ];
    }
}


// this is based on the reference AES code by Paulo Barreto and Vincent Rijmen
AVAES *av_aes_init(uint8_t *key, int key_bits) {
    AVAES *a;
    int i, j, t, rconpointer = 0;
    uint8_t tk[8][4];
    int KC= key_bits/32;
    int rounds= KC + 6;
    uint8_t  log8[256];
    uint8_t alog8[512];

    if(!sbox[255]){
        j=1;
        for(i=0; i<255; i++){
            alog8[i]=
            alog8[i+255]= j;
            log8[j]= i;
            j^= j+j;
            if(j>255) j^= 0x11B;
        }
        log8[0]= 255;
        for(i=0; i<256; i++){
            j= i ? alog8[255-log8[i]] : 0;
            j ^= (j<<1) ^ (j<<2) ^ (j<<3) ^ (j<<4);
            j = (j ^ (j>>8) ^ 99) & 255;
            inv_sbox[j]= i;
            sbox    [i]= j;
//            av_log(NULL, AV_LOG_ERROR, "%d, ", log8[i]);
        }
        init_multbl2(dec_multbl[0], (int[4]){0xe, 0x9, 0xd, 0xb}, log8, alog8, inv_sbox);
#ifndef CONFIG_SMALL
        init_multbl2(dec_multbl[1], (int[4]){0xb, 0xe, 0x9, 0xd}, log8, alog8, inv_sbox);
        init_multbl2(dec_multbl[2], (int[4]){0xd, 0xb, 0xe, 0x9}, log8, alog8, inv_sbox);
        init_multbl2(dec_multbl[3], (int[4]){0x9, 0xd, 0xb, 0xe}, log8, alog8, inv_sbox);
#endif
        init_multbl2(enc_multbl[0], (int[4]){0x2, 0x1, 0x1, 0x3}, log8, alog8, sbox);
#ifndef CONFIG_SMALL
        init_multbl2(enc_multbl[1], (int[4]){0x3, 0x2, 0x1, 0x1}, log8, alog8, sbox);
        init_multbl2(enc_multbl[2], (int[4]){0x1, 0x3, 0x2, 0x1}, log8, alog8, sbox);
        init_multbl2(enc_multbl[3], (int[4]){0x1, 0x1, 0x3, 0x2}, log8, alog8, sbox);
#endif
    }

    if(key_bits!=128 && key_bits!=192 && key_bits!=256)
        return NULL;

    a= av_malloc(sizeof(AVAES));
    a->rounds= rounds;

    memcpy(tk, key, KC*4);

    for(t= 0; t < (rounds+1)*4;) {
        memcpy(a->round_enc_key[0][t], tk, KC*4);
        t+= KC;

        for(i = 0; i < 4; i++)
            tk[0][i] ^= sbox[tk[KC-1][(i+1)&3]];
        tk[0][0] ^= rcon[rconpointer++];

        for(j = 1; j < KC; j++){
            if(KC != 8 || j != KC/2)
                for(i = 0; i < 4; i++) tk[j][i] ^=      tk[j-1][i];
            else
                for(i = 0; i < 4; i++) tk[j][i] ^= sbox[tk[j-1][i]];
        }
    }

    for(i=0; i<sizeof(a->round_enc_key); i++)
        a->round_dec_key[0][0][i]= sbox[a->round_enc_key[0][0][i]];
    for(i=1; i<rounds; i++)
        mix(a->round_dec_key[i], dec_multbl);

    return a;
}

#ifdef TEST

int main(){
    int i,j,k;
    AVAES *a= av_aes_init("PI=3.141592654..", 128);
    uint8_t zero[16]= {0};
    uint8_t pt[16]= {0x6a, 0x84, 0x86, 0x7c, 0xd7, 0x7e, 0x12, 0xad, 0x07, 0xea, 0x1b, 0xe8, 0x95, 0xc5, 0x3f, 0xa3};
    uint8_t ct[16]= {0x73, 0x22, 0x81, 0xc0, 0xa0, 0xaa, 0xb8, 0xf7, 0xa5, 0x4a, 0x0c, 0x67, 0xa0, 0xc4, 0x5e, 0xcf};
    AVAES *b= av_aes_init(zero, 128);

/*    uint8_t key[16]= {0x42, 0x78, 0xb8, 0x40, 0xfb, 0x44, 0xaa, 0xa7, 0x57, 0xc1, 0xbf, 0x04, 0xac, 0xbe, 0x1a, 0x3e};
    uint8_t IV[16] = {0x57, 0xf0, 0x2a, 0x5c, 0x53, 0x39, 0xda, 0xeb, 0x0a, 0x29, 0x08, 0xa0, 0x6a, 0xc6, 0x39, 0x3f};
    uint8_t pt[16] = {0x3c, 0x88, 0x8b, 0xbb, 0xb1, 0xa8, 0xeb, 0x9f, 0x3e, 0x9b, 0x87, 0xac, 0xaa, 0xd9, 0x86, 0xc4};
//            66e2f7071c83083b8a557971918850e5
    uint8_t ct[16] = {0x47, 0x9c, 0x89, 0xec, 0x14, 0xbc, 0x98, 0x99, 0x4e, 0x62, 0xb2, 0xc7, 0x05, 0xb5, 0x0, 0x14e};
//             175bd7832e7e60a1e92aac568a861eb7*/
    uint8_t ckey[16]= {0x10, 0xa5, 0x88, 0x69, 0xd7, 0x4b, 0xe5, 0xa3, 0x74, 0xcf, 0x86, 0x7c, 0xfb, 0x47, 0x38, 0x59};
    uint8_t cct[16] = {0x6d, 0x25, 0x1e, 0x69, 0x44, 0xb0, 0x51, 0xe0, 0x4e, 0xaa, 0x6f, 0xb4, 0xdb, 0xf7, 0x84, 0x65};
    AVAES *c= av_aes_init(ckey, 128);

    av_log_level= AV_LOG_DEBUG;

    memcpy(b->state, ct, 16);
    av_aes_decrypt(b);
    for(j=0; j<16; j++)
        if(pt[j] != b->state[0][j]){
            av_log(NULL, AV_LOG_ERROR, "%d %02X %02X\n", j, pt[j], b->state[0][j]);
        }

    memcpy(c->state, cct, 16);
    av_aes_decrypt(c);
    for(j=0; j<16; j++)
        if(zero[j] != c->state[0][j]){
            av_log(NULL, AV_LOG_ERROR, "%d %02X %02X\n", j, zero[j], c->state[0][j]);
        }

    for(i=0; i<10000; i++){
        for(j=0; j<16; j++){
            pt[j]= random();
        }
        memcpy(a->state, pt, 16);
{START_TIMER
        av_aes_encrypt(a);
        if(!(i&(i-1)))
            av_log(NULL, AV_LOG_ERROR, "%02X %02X %02X %02X\n", a->state[0][0], a->state[1][1], a->state[2][2], a->state[3][3]);
        av_aes_decrypt(a);
STOP_TIMER("aes")}
        for(j=0; j<16; j++){
            if(pt[j] != a->state[0][j]){
                av_log(NULL, AV_LOG_ERROR, "%d %d %02X %02X\n", i,j, pt[j], a->state[0][j]);
            }
        }
    }
    return 0;
}
#endif
