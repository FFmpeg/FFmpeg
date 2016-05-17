/*
 * An implementation of the CAMELLIA algorithm as mentioned in RFC3713
 * Copyright (c) 2014 Supraja Meedinti
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
#include "camellia.h"
#include "common.h"
#include "intreadwrite.h"
#include "attributes.h"

#define LR32(x,c) ((x) << (c) | (x) >> (32 - (c)))
#define RR32(x,c) ((x) >> (c) | (x) << (32 - (c)))

#define MASK8 0xff
#define MASK32 0xffffffff
#define MASK64 0xffffffffffffffff

#define Sigma1  0xA09E667F3BCC908B
#define Sigma2  0xB67AE8584CAA73B2
#define Sigma3  0xC6EF372FE94F82BE
#define Sigma4  0x54FF53A5F1D36F1C
#define Sigma5  0x10E527FADE682D1D
#define Sigma6  0xB05688C2B3E6C1FD

static uint64_t SP[8][256];

typedef struct AVCAMELLIA {
    uint64_t Kw[4];
    uint64_t Ke[6];
    uint64_t K[24];
    int key_bits;
} AVCAMELLIA;

static const uint8_t SBOX1[256] = {
112, 130,  44, 236, 179,  39, 192, 229, 228, 133,  87,  53, 234,  12, 174,  65,
 35, 239, 107, 147,  69,  25, 165,  33, 237,  14,  79,  78,  29, 101, 146, 189,
134, 184, 175, 143, 124, 235,  31, 206,  62,  48, 220,  95,  94, 197,  11,  26,
166, 225,  57, 202, 213,  71,  93,  61, 217,   1,  90, 214,  81,  86, 108,  77,
139,  13, 154, 102, 251, 204, 176,  45, 116,  18,  43,  32, 240, 177, 132, 153,
223,  76, 203, 194,  52, 126, 118,   5, 109, 183, 169,  49, 209,  23,   4, 215,
 20,  88,  58,  97, 222,  27,  17,  28,  50,  15, 156,  22,  83,  24, 242,  34,
254,  68, 207, 178, 195, 181, 122, 145,  36,   8, 232, 168,  96, 252, 105,  80,
170, 208, 160, 125, 161, 137,  98, 151,  84,  91,  30, 149, 224, 255, 100, 210,
 16, 196,   0,  72, 163, 247, 117, 219, 138,   3, 230, 218,   9,  63, 221, 148,
135,  92, 131,   2, 205,  74, 144,  51, 115, 103, 246, 243, 157, 127, 191, 226,
 82, 155, 216,  38, 200,  55, 198,  59, 129, 150, 111,  75,  19, 190,  99,  46,
233, 121, 167, 140, 159, 110, 188, 142,  41, 245, 249, 182,  47, 253, 180,  89,
120, 152,   6, 106, 231,  70, 113, 186, 212,  37, 171,  66, 136, 162, 141, 250,
114,   7, 185,  85, 248, 238, 172,  10,  54,  73,  42, 104,  60,  56, 241, 164,
 64,  40, 211, 123, 187, 201,  67, 193,  21, 227, 173, 244, 119, 199, 128, 158
};

static const uint8_t SBOX2[256] = {
224,   5,  88, 217, 103,  78, 129, 203, 201,  11, 174, 106, 213,  24,  93, 130,
 70, 223, 214,  39, 138,  50,  75,  66, 219,  28, 158, 156,  58, 202,  37, 123,
 13, 113,  95,  31, 248, 215,  62, 157, 124,  96, 185, 190, 188, 139,  22,  52,
 77, 195, 114, 149, 171, 142, 186, 122, 179,   2, 180, 173, 162, 172, 216, 154,
 23,  26,  53, 204, 247, 153,  97,  90, 232,  36,  86,  64, 225,  99,   9,  51,
191, 152, 151, 133, 104, 252, 236,  10, 218, 111,  83,  98, 163,  46,   8, 175,
 40, 176, 116, 194, 189,  54,  34,  56, 100,  30,  57,  44, 166,  48, 229,  68,
253, 136, 159, 101, 135, 107, 244,  35,  72,  16, 209,  81, 192, 249, 210, 160,
 85, 161,  65, 250,  67,  19, 196,  47, 168, 182,  60,  43, 193, 255, 200, 165,
 32, 137,   0, 144,  71, 239, 234, 183,  21,   6, 205, 181,  18, 126, 187,  41,
 15, 184,   7,   4, 155, 148,  33, 102, 230, 206, 237, 231,  59, 254, 127, 197,
164,  55, 177,  76, 145, 110, 141, 118,   3,  45, 222, 150,  38, 125, 198,  92,
211, 242,  79,  25,  63, 220, 121,  29,  82, 235, 243, 109,  94, 251, 105, 178,
240,  49,  12, 212, 207, 140, 226, 117, 169,  74,  87, 132,  17,  69,  27, 245,
228,  14, 115, 170, 241, 221,  89,  20, 108, 146,  84, 208, 120, 112, 227,  73,
128,  80, 167, 246, 119, 147, 134, 131,  42, 199,  91, 233, 238, 143,   1,  61
};

static const uint8_t SBOX3[256] = {
 56,  65,  22, 118, 217, 147,  96, 242, 114, 194, 171, 154, 117,   6,  87, 160,
145, 247, 181, 201, 162, 140, 210, 144, 246,   7, 167,  39, 142, 178,  73, 222,
 67,  92, 215, 199,  62, 245, 143, 103,  31,  24, 110, 175,  47, 226, 133,  13,
 83, 240, 156, 101, 234, 163, 174, 158, 236, 128,  45, 107, 168,  43,  54, 166,
197, 134,  77,  51, 253, 102,  88, 150,  58,   9, 149,  16, 120, 216,  66, 204,
239,  38, 229,  97,  26,  63,  59, 130, 182, 219, 212, 152, 232, 139,   2, 235,
 10,  44,  29, 176, 111, 141, 136,  14,  25, 135,  78,  11, 169,  12, 121,  17,
127,  34, 231,  89, 225, 218,  61, 200,  18,   4, 116,  84,  48, 126, 180,  40,
 85, 104,  80, 190, 208, 196,  49, 203,  42, 173,  15, 202, 112, 255,  50, 105,
  8,  98,   0,  36, 209, 251, 186, 237,  69, 129, 115, 109, 132, 159, 238,  74,
195,  46, 193,   1, 230,  37,  72, 153, 185, 179, 123, 249, 206, 191, 223, 113,
 41, 205, 108,  19, 100, 155,  99, 157, 192,  75, 183, 165, 137,  95, 177,  23,
244, 188, 211,  70, 207,  55,  94,  71, 148, 250, 252,  91, 151, 254,  90, 172,
 60,  76,   3,  53, 243,  35, 184,  93, 106, 146, 213,  33,  68,  81, 198, 125,
 57, 131, 220, 170, 124, 119,  86,   5,  27, 164,  21,  52,  30,  28, 248,  82,
 32,  20, 233, 189, 221, 228, 161, 224, 138, 241, 214, 122, 187, 227,  64,  79
};

static const uint8_t SBOX4[256] = {
112,  44, 179, 192, 228,  87, 234, 174,  35, 107,  69, 165, 237,  79,  29, 146,
134, 175, 124,  31,  62, 220,  94,  11, 166,  57, 213,  93, 217,  90,  81, 108,
139, 154, 251, 176, 116,  43, 240, 132, 223, 203,  52, 118, 109, 169, 209,   4,
 20,  58, 222,  17,  50, 156,  83, 242, 254, 207, 195, 122,  36, 232,  96, 105,
170, 160, 161,  98,  84,  30, 224, 100,  16,   0, 163, 117, 138, 230,   9, 221,
135, 131, 205, 144, 115, 246, 157, 191,  82, 216, 200, 198, 129, 111,  19,  99,
233, 167, 159, 188,  41, 249,  47, 180, 120,   6, 231, 113, 212, 171, 136, 141,
114, 185, 248, 172,  54,  42,  60, 241,  64, 211, 187,  67,  21, 173, 119, 128,
130, 236,  39, 229, 133,  53,  12,  65, 239, 147,  25,  33,  14,  78, 101, 189,
184, 143, 235, 206,  48,  95, 197,  26, 225, 202,  71,  61,   1, 214,  86,  77,
 13, 102, 204,  45,  18,  32, 177, 153,  76, 194, 126,   5, 183,  49,  23, 215,
 88,  97,  27,  28,  15,  22,  24,  34,  68, 178, 181, 145,   8, 168, 252,  80,
208, 125, 137, 151,  91, 149, 255, 210, 196,  72, 247, 219,   3, 218,  63, 148,
 92,   2,  74,  51, 103, 243, 127, 226, 155,  38,  55,  59, 150,  75, 190,  46,
121, 140, 110, 142, 245, 182, 253,  89, 152, 106,  70, 186,  37,  66, 162, 250,
  7,  85, 238,  10,  73, 104,  56, 164,  40, 123, 201, 193, 227, 244, 199, 158
};

const int av_camellia_size = sizeof(AVCAMELLIA);

static void LR128(uint64_t d[2], const uint64_t K[2], int x)
{
    int i = 0;
    if (64 <= x && x < 128) {
        i = 1;
        x -= 64;
    }
    if (x <= 0 || x >= 128) {
        d[0] = K[i];
        d[1] = K[!i];
        return;
    }
    d[0] = (K[i] << x | K[!i] >> (64 - x));
    d[1] = (K[!i] << x | K[i] >> (64 - x));
}

static uint64_t F(uint64_t F_IN, uint64_t KE)
{
    KE ^= F_IN;
    F_IN=SP[0][KE >> 56]^SP[1][(KE >> 48) & MASK8]^SP[2][(KE >> 40) & MASK8]^SP[3][(KE >> 32) & MASK8]^SP[4][(KE >> 24) & MASK8]^SP[5][(KE >> 16) & MASK8]^SP[6][(KE >> 8) & MASK8]^SP[7][KE & MASK8];
    return F_IN;
}

static uint64_t FL(uint64_t FL_IN, uint64_t KE)
{
    uint32_t x1, x2, k1, k2;
    x1 = FL_IN >> 32;
    x2 = FL_IN & MASK32;
    k1 = KE >> 32;
    k2 = KE & MASK32;
    x2 = x2 ^ LR32((x1 & k1), 1);
    x1 = x1 ^ (x2 | k2);
    return ((uint64_t)x1 << 32) | (uint64_t)x2;
}

static uint64_t FLINV(uint64_t FLINV_IN, uint64_t KE)
{
    uint32_t x1, x2, k1, k2;
    x1 = FLINV_IN >> 32;
    x2 = FLINV_IN & MASK32;
    k1 = KE >> 32;
    k2 = KE & MASK32;
    x1 = x1 ^ (x2 | k2);
    x2 = x2 ^ LR32((x1 & k1), 1);
    return ((uint64_t)x1 << 32) | (uint64_t)x2;
}

static const uint8_t shifts[2][12] = {
    {0, 15, 15, 45, 45, 60, 94, 94, 111},
    {0, 15, 15, 30, 45, 45, 60, 60,  77, 94, 94, 111}
};

static const uint8_t vars[2][12] = {
    {2, 0, 2, 0, 2, 2, 0, 2, 0},
    {3, 1, 2, 3, 0, 2, 1, 3, 0, 1, 2, 0}
};

static void generate_round_keys(AVCAMELLIA *cs, uint64_t Kl[2], uint64_t Kr[2], uint64_t Ka[2], uint64_t Kb[2])
{
    int i;
    uint64_t *Kd[4], d[2];
    Kd[0] = Kl;
    Kd[1] = Kr;
    Kd[2] = Ka;
    Kd[3] = Kb;
    cs->Kw[0] = Kl[0];
    cs->Kw[1] = Kl[1];
    if (cs->key_bits == 128) {
        for (i = 0; i < 9; i++) {
            LR128(d, Kd[vars[0][i]], shifts[0][i]);
            cs->K[2*i] = d[0];
            cs->K[2*i+1] = d[1];
        }
        LR128(d, Kd[0], 60);
        cs->K[9] = d[1];
        LR128(d, Kd[2], 30);
        cs->Ke[0] = d[0];
        cs->Ke[1] = d[1];
        LR128(d, Kd[0], 77);
        cs->Ke[2] = d[0];
        cs->Ke[3] = d[1];
        LR128(d, Kd[2], 111);
        cs->Kw[2] = d[0];
        cs->Kw[3] = d[1];
    } else {
        for (i = 0; i < 12; i++) {
            LR128(d, Kd[vars[1][i]], shifts[1][i]);
            cs->K[2*i] = d[0];
            cs->K[2*i+1] = d[1];
        }
        LR128(d, Kd[1], 30);
        cs->Ke[0] = d[0];
        cs->Ke[1] = d[1];
        LR128(d, Kd[0], 60);
        cs->Ke[2] = d[0];
        cs->Ke[3] = d[1];
        LR128(d, Kd[2], 77);
        cs->Ke[4] = d[0];
        cs->Ke[5] = d[1];
        LR128(d, Kd[3], 111);
        cs->Kw[2] = d[0];
        cs->Kw[3] = d[1];
    }
}

static void camellia_encrypt(AVCAMELLIA *cs, uint8_t *dst, const uint8_t *src)
{
    uint64_t D1, D2;
    D1 = AV_RB64(src);
    D2 = AV_RB64(src + 8);
    D1 ^= cs->Kw[0];
    D2 ^= cs->Kw[1];
    D2 ^= F(D1, cs->K[0]);
    D1 ^= F(D2, cs->K[1]);
    D2 ^= F(D1, cs->K[2]);
    D1 ^= F(D2, cs->K[3]);
    D2 ^= F(D1, cs->K[4]);
    D1 ^= F(D2, cs->K[5]);
    D1 = FL(D1, cs->Ke[0]);
    D2 = FLINV(D2, cs->Ke[1]);
    D2 ^= F(D1, cs->K[6]);
    D1 ^= F(D2, cs->K[7]);
    D2 ^= F(D1, cs->K[8]);
    D1 ^= F(D2, cs->K[9]);
    D2 ^= F(D1, cs->K[10]);
    D1 ^= F(D2, cs->K[11]);
    D1 = FL(D1, cs->Ke[2]);
    D2 = FLINV(D2, cs->Ke[3]);
    D2 ^= F(D1, cs->K[12]);
    D1 ^= F(D2, cs->K[13]);
    D2 ^= F(D1, cs->K[14]);
    D1 ^= F(D2, cs->K[15]);
    D2 ^= F(D1, cs->K[16]);
    D1 ^= F(D2, cs->K[17]);
    if (cs->key_bits != 128) {
        D1 = FL(D1, cs->Ke[4]);
        D2 = FLINV(D2, cs->Ke[5]);
        D2 ^= F(D1, cs->K[18]);
        D1 ^= F(D2, cs->K[19]);
        D2 ^= F(D1, cs->K[20]);
        D1 ^= F(D2, cs->K[21]);
        D2 ^= F(D1, cs->K[22]);
        D1 ^= F(D2, cs->K[23]);
    }
    D2 ^= cs->Kw[2];
    D1 ^= cs->Kw[3];
    AV_WB64(dst, D2);
    AV_WB64(dst + 8, D1);
}

static void camellia_decrypt(AVCAMELLIA *cs, uint8_t *dst, const uint8_t *src, uint8_t *iv)
{
    uint64_t D1, D2;
    D1 = AV_RB64(src);
    D2 = AV_RB64(src + 8);
    D1 ^= cs->Kw[2];
    D2 ^= cs->Kw[3];
    if (cs->key_bits != 128) {
        D2 ^= F(D1, cs->K[23]);
        D1 ^= F(D2, cs->K[22]);
        D2 ^= F(D1, cs->K[21]);
        D1 ^= F(D2, cs->K[20]);
        D2 ^= F(D1, cs->K[19]);
        D1 ^= F(D2, cs->K[18]);
        D1 = FL(D1, cs->Ke[5]);
        D2 = FLINV(D2, cs->Ke[4]);
    }
    D2 ^= F(D1, cs->K[17]);
    D1 ^= F(D2, cs->K[16]);
    D2 ^= F(D1, cs->K[15]);
    D1 ^= F(D2, cs->K[14]);
    D2 ^= F(D1, cs->K[13]);
    D1 ^= F(D2, cs->K[12]);
    D1 = FL(D1, cs->Ke[3]);
    D2 = FLINV(D2, cs->Ke[2]);
    D2 ^= F(D1, cs->K[11]);
    D1 ^= F(D2, cs->K[10]);
    D2 ^= F(D1, cs->K[9]);
    D1 ^= F(D2, cs->K[8]);
    D2 ^= F(D1, cs->K[7]);
    D1 ^= F(D2, cs->K[6]);
    D1 = FL(D1, cs->Ke[1]);
    D2 = FLINV(D2, cs->Ke[0]);
    D2 ^= F(D1, cs->K[5]);
    D1 ^= F(D2, cs->K[4]);
    D2 ^= F(D1, cs->K[3]);
    D1 ^= F(D2, cs->K[2]);
    D2 ^= F(D1, cs->K[1]);
    D1 ^= F(D2, cs->K[0]);
    D2 ^= cs->Kw[0];
    D1 ^= cs->Kw[1];
    if (iv) {
        D2 ^= AV_RB64(iv);
        D1 ^= AV_RB64(iv + 8);
        memcpy(iv, src, 16);
    }
    AV_WB64(dst, D2);
    AV_WB64(dst + 8, D1);
}

static void computeSP(void)
{
    uint64_t z;
    int i;
    for (i = 0; i < 256; i++) {
        z = SBOX1[i];
        SP[0][i] = (z << 56) ^ (z << 48) ^ (z << 40) ^ (z << 24) ^ z;
        SP[7][i] = (z << 56) ^ (z << 48) ^ (z << 40) ^ (z << 24) ^ (z << 16) ^ (z << 8);
        z = SBOX2[i];
        SP[1][i] = (z << 48) ^ (z << 40) ^ (z << 32) ^ (z << 24) ^ (z << 16);
        SP[4][i] = (z << 48) ^ (z << 40) ^ (z << 32) ^ (z << 16) ^ (z << 8) ^ z;
        z = SBOX3[i];
        SP[2][i] = (z << 56) ^ (z << 40) ^ (z << 32) ^ (z << 16) ^ (z << 8);
        SP[5][i] = (z << 56) ^ (z << 40) ^ (z << 32) ^ (z << 24) ^ (z << 8) ^ z;
        z = SBOX4[i];
        SP[3][i] = (z << 56) ^ (z << 48) ^ (z << 32) ^ (z << 8) ^ z;
        SP[6][i] = (z << 56) ^ (z << 48) ^ (z << 32) ^ (z << 24) ^ (z << 16) ^ z;
    }
}

struct AVCAMELLIA *av_camellia_alloc(void)
{
    return av_mallocz(sizeof(struct AVCAMELLIA));
}

av_cold int av_camellia_init(AVCAMELLIA *cs, const uint8_t *key, int key_bits)
{
    uint64_t Kl[2], Kr[2], Ka[2], Kb[2];
    uint64_t D1, D2;
    if (key_bits != 128 && key_bits != 192 && key_bits != 256)
        return AVERROR(EINVAL);
    memset(Kb, 0, sizeof(Kb));
    memset(Kr, 0, sizeof(Kr));
    cs->key_bits = key_bits;
    Kl[0] = AV_RB64(key);
    Kl[1] = AV_RB64(key + 8);
    if (key_bits == 192) {
        Kr[0] = AV_RB64(key + 16);
        Kr[1] = ~Kr[0];
    } else if (key_bits == 256) {
        Kr[0] = AV_RB64(key + 16);
        Kr[1] = AV_RB64(key + 24);
    }
    computeSP();
    D1 = Kl[0] ^ Kr[0];
    D2 = Kl[1] ^ Kr[1];
    D2 ^= F(D1, Sigma1);
    D1 ^= F(D2, Sigma2);
    D1 ^= Kl[0];
    D2 ^= Kl[1];
    D2 ^= F(D1, Sigma3);
    D1 ^= F(D2, Sigma4);
    Ka[0] = D1;
    Ka[1] = D2;
    if (key_bits != 128) {
        D1 = Ka[0] ^ Kr[0];
        D2 = Ka[1] ^ Kr[1];
        D2 ^= F(D1, Sigma5);
        D1 ^= F(D2, Sigma6);
        Kb[0] = D1;
        Kb[1] = D2;
    }
    generate_round_keys(cs, Kl, Kr, Ka, Kb);
    return 0;
}

void av_camellia_crypt(AVCAMELLIA *cs, uint8_t *dst, const uint8_t *src, int count, uint8_t *iv, int decrypt)
{
    int i;
    while (count--) {
        if (decrypt) {
            camellia_decrypt(cs, dst, src, iv);
        } else {
            if (iv) {
                for (i = 0; i < 16; i++)
                    dst[i] = src[i] ^ iv[i];
                camellia_encrypt(cs, dst, dst);
                memcpy(iv, dst, 16);
            } else {
                camellia_encrypt(cs, dst, src);
            }
        }
        src = src + 16;
        dst = dst + 16;
    }
}
