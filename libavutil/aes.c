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
 */

#include "common.h"
#include "log.h"
#include "aes.h"

typedef struct AVAES{
    uint8_t state[4][4];
    uint8_t round_key[15][4][4];
    int rounds;
}AVAES;

static const uint8_t rcon[30] = {
  0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80,
  0x1b, 0x36, 0x6c, 0xd8, 0xab, 0x4d, 0x9a, 0x2f,
  0x5e, 0xbc, 0x63, 0xc6, 0x97, 0x35, 0x6a, 0xd4,
  0xb3, 0x7d, 0xfa, 0xef, 0xc5, 0x91
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

static inline int mul(int a, int b, uint8_t alog8[256]){
    if(a==255) return 0;
    else       return alog8[a+b];
}

#define ROT(x,s) ((x>>s)|(x<<(32-s))

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

    for(r=a->rounds-1; r>=0; r--){
        if(r==a->rounds-1)
            addkey(a->state, a->round_key[r+1]);
        else
            mix(a->state, dec_multbl);
        SUBSHIFT0((a->state[0]+0), inv_sbox)
        SUBSHIFT3((a->state[0]+1), inv_sbox)
        SUBSHIFT2((a->state[0]+2), inv_sbox)
        SUBSHIFT1((a->state[0]+3), inv_sbox)
        addkey(a->state, a->round_key[r]);
    }
}

void av_aes_encrypt(AVAES *a){
    int r, t;

    for(r=0; r<a->rounds; r++){
        addkey(a->state, a->round_key[r]);
        SUBSHIFT0((a->state[0]+0), sbox)
        SUBSHIFT1((a->state[0]+1), sbox)
        SUBSHIFT2((a->state[0]+2), sbox)
        SUBSHIFT3((a->state[0]+3), sbox)
        if(r==a->rounds-1)
            addkey(a->state, a->round_key[r+1]);
        else
            mix(a->state, enc_multbl); //FIXME replace log8 by const / optimze mix as this can be simplified alot
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
            //FIXME multbl init
        }
    }

    if(key_bits!=128 && key_bits!=192 && key_bits!=256)
        return NULL;

    a= av_malloc(sizeof(AVAES));
    a->rounds= rounds;

    memcpy(tk, key, KC*4);

    for(t= 0; t < (rounds+1)*4; ) {
        for(j = 0; (j < KC) && (t < (rounds+1)*4); j++, t++)
            for(i = 0; i < 4; i++)
                a->round_key[0][t][i] = tk[j][i];

        for(i = 0; i < 4; i++)
                tk[0][i] ^= sbox[tk[KC-1][(i+1)&3]];
        tk[0][0] ^= rcon[rconpointer++];

        for(j = 1; j < KC; j++){
            if(KC != 8 || j != KC/2)
                for(i = 0; i < 4; i++) tk[j][i] ^= tk[j-1][i];
            else
                for(i = 0; i < 4; i++)
                    tk[KC/2][i] ^= sbox[tk[KC/2 - 1][i]];
        }
    }
    return a;
}

#ifdef TEST

int main(){
    int i,j,k;
    AVAES *a= av_aes_init("PI=3.141592654..", 128);

    for(i=0; i<10000; i++){
    }
    return 0;
}
#endif
