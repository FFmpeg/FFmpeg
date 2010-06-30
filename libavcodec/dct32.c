/*
 * Template for the Discrete Cosine Transform for 32 samples
 * Copyright (c) 2001, 2002 Fabrice Bellard
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

#ifdef DCT32_FLOAT
#   define FIXHR(x)       ((float)(x))
#   define MULH3(x, y, s) ((s)*(y)*(x))
#   define INTFLOAT float
#endif


/* tab[i][j] = 1.0 / (2.0 * cos(pi*(2*k+1) / 2^(6 - j))) */

/* cos(i*pi/64) */

#define COS0_0  FIXHR(0.50060299823519630134/2)
#define COS0_1  FIXHR(0.50547095989754365998/2)
#define COS0_2  FIXHR(0.51544730992262454697/2)
#define COS0_3  FIXHR(0.53104259108978417447/2)
#define COS0_4  FIXHR(0.55310389603444452782/2)
#define COS0_5  FIXHR(0.58293496820613387367/2)
#define COS0_6  FIXHR(0.62250412303566481615/2)
#define COS0_7  FIXHR(0.67480834145500574602/2)
#define COS0_8  FIXHR(0.74453627100229844977/2)
#define COS0_9  FIXHR(0.83934964541552703873/2)
#define COS0_10 FIXHR(0.97256823786196069369/2)
#define COS0_11 FIXHR(1.16943993343288495515/4)
#define COS0_12 FIXHR(1.48416461631416627724/4)
#define COS0_13 FIXHR(2.05778100995341155085/8)
#define COS0_14 FIXHR(3.40760841846871878570/8)
#define COS0_15 FIXHR(10.19000812354805681150/32)

#define COS1_0 FIXHR(0.50241928618815570551/2)
#define COS1_1 FIXHR(0.52249861493968888062/2)
#define COS1_2 FIXHR(0.56694403481635770368/2)
#define COS1_3 FIXHR(0.64682178335999012954/2)
#define COS1_4 FIXHR(0.78815462345125022473/2)
#define COS1_5 FIXHR(1.06067768599034747134/4)
#define COS1_6 FIXHR(1.72244709823833392782/4)
#define COS1_7 FIXHR(5.10114861868916385802/16)

#define COS2_0 FIXHR(0.50979557910415916894/2)
#define COS2_1 FIXHR(0.60134488693504528054/2)
#define COS2_2 FIXHR(0.89997622313641570463/2)
#define COS2_3 FIXHR(2.56291544774150617881/8)

#define COS3_0 FIXHR(0.54119610014619698439/2)
#define COS3_1 FIXHR(1.30656296487637652785/4)

#define COS4_0 FIXHR(0.70710678118654752439/2)

/* butterfly operator */
#define BF(a, b, c, s)\
{\
    tmp0 = val##a + val##b;\
    tmp1 = val##a - val##b;\
    val##a = tmp0;\
    val##b = MULH3(tmp1, c, 1<<(s));\
}

#define BF0(a, b, c, s)\
{\
    tmp0 = tab[a] + tab[b];\
    tmp1 = tab[a] - tab[b];\
    val##a = tmp0;\
    val##b = MULH3(tmp1, c, 1<<(s));\
}

#define BF1(a, b, c, d)\
{\
    BF(a, b, COS4_0, 1);\
    BF(c, d,-COS4_0, 1);\
    val##c += val##d;\
}

#define BF2(a, b, c, d)\
{\
    BF(a, b, COS4_0, 1);\
    BF(c, d,-COS4_0, 1);\
    val##c += val##d;\
    val##a += val##c;\
    val##c += val##b;\
    val##b += val##d;\
}

#define ADD(a, b) val##a += val##b

/* DCT32 without 1/sqrt(2) coef zero scaling. */
static void dct32(INTFLOAT *out, const INTFLOAT *tab)
{
    INTFLOAT tmp0, tmp1;

    INTFLOAT val0 , val1 , val2 , val3 , val4 , val5 , val6 , val7 ,
             val8 , val9 , val10, val11, val12, val13, val14, val15,
             val16, val17, val18, val19, val20, val21, val22, val23,
             val24, val25, val26, val27, val28, val29, val30, val31;

    /* pass 1 */
    BF0( 0, 31, COS0_0 , 1);
    BF0(15, 16, COS0_15, 5);
    /* pass 2 */
    BF( 0, 15, COS1_0 , 1);
    BF(16, 31,-COS1_0 , 1);
    /* pass 1 */
    BF0( 7, 24, COS0_7 , 1);
    BF0( 8, 23, COS0_8 , 1);
    /* pass 2 */
    BF( 7,  8, COS1_7 , 4);
    BF(23, 24,-COS1_7 , 4);
    /* pass 3 */
    BF( 0,  7, COS2_0 , 1);
    BF( 8, 15,-COS2_0 , 1);
    BF(16, 23, COS2_0 , 1);
    BF(24, 31,-COS2_0 , 1);
    /* pass 1 */
    BF0( 3, 28, COS0_3 , 1);
    BF0(12, 19, COS0_12, 2);
    /* pass 2 */
    BF( 3, 12, COS1_3 , 1);
    BF(19, 28,-COS1_3 , 1);
    /* pass 1 */
    BF0( 4, 27, COS0_4 , 1);
    BF0(11, 20, COS0_11, 2);
    /* pass 2 */
    BF( 4, 11, COS1_4 , 1);
    BF(20, 27,-COS1_4 , 1);
    /* pass 3 */
    BF( 3,  4, COS2_3 , 3);
    BF(11, 12,-COS2_3 , 3);
    BF(19, 20, COS2_3 , 3);
    BF(27, 28,-COS2_3 , 3);
    /* pass 4 */
    BF( 0,  3, COS3_0 , 1);
    BF( 4,  7,-COS3_0 , 1);
    BF( 8, 11, COS3_0 , 1);
    BF(12, 15,-COS3_0 , 1);
    BF(16, 19, COS3_0 , 1);
    BF(20, 23,-COS3_0 , 1);
    BF(24, 27, COS3_0 , 1);
    BF(28, 31,-COS3_0 , 1);



    /* pass 1 */
    BF0( 1, 30, COS0_1 , 1);
    BF0(14, 17, COS0_14, 3);
    /* pass 2 */
    BF( 1, 14, COS1_1 , 1);
    BF(17, 30,-COS1_1 , 1);
    /* pass 1 */
    BF0( 6, 25, COS0_6 , 1);
    BF0( 9, 22, COS0_9 , 1);
    /* pass 2 */
    BF( 6,  9, COS1_6 , 2);
    BF(22, 25,-COS1_6 , 2);
    /* pass 3 */
    BF( 1,  6, COS2_1 , 1);
    BF( 9, 14,-COS2_1 , 1);
    BF(17, 22, COS2_1 , 1);
    BF(25, 30,-COS2_1 , 1);

    /* pass 1 */
    BF0( 2, 29, COS0_2 , 1);
    BF0(13, 18, COS0_13, 3);
    /* pass 2 */
    BF( 2, 13, COS1_2 , 1);
    BF(18, 29,-COS1_2 , 1);
    /* pass 1 */
    BF0( 5, 26, COS0_5 , 1);
    BF0(10, 21, COS0_10, 1);
    /* pass 2 */
    BF( 5, 10, COS1_5 , 2);
    BF(21, 26,-COS1_5 , 2);
    /* pass 3 */
    BF( 2,  5, COS2_2 , 1);
    BF(10, 13,-COS2_2 , 1);
    BF(18, 21, COS2_2 , 1);
    BF(26, 29,-COS2_2 , 1);
    /* pass 4 */
    BF( 1,  2, COS3_1 , 2);
    BF( 5,  6,-COS3_1 , 2);
    BF( 9, 10, COS3_1 , 2);
    BF(13, 14,-COS3_1 , 2);
    BF(17, 18, COS3_1 , 2);
    BF(21, 22,-COS3_1 , 2);
    BF(25, 26, COS3_1 , 2);
    BF(29, 30,-COS3_1 , 2);

    /* pass 5 */
    BF1( 0,  1,  2,  3);
    BF2( 4,  5,  6,  7);
    BF1( 8,  9, 10, 11);
    BF2(12, 13, 14, 15);
    BF1(16, 17, 18, 19);
    BF2(20, 21, 22, 23);
    BF1(24, 25, 26, 27);
    BF2(28, 29, 30, 31);

    /* pass 6 */

    ADD( 8, 12);
    ADD(12, 10);
    ADD(10, 14);
    ADD(14,  9);
    ADD( 9, 13);
    ADD(13, 11);
    ADD(11, 15);

    out[ 0] = val0;
    out[16] = val1;
    out[ 8] = val2;
    out[24] = val3;
    out[ 4] = val4;
    out[20] = val5;
    out[12] = val6;
    out[28] = val7;
    out[ 2] = val8;
    out[18] = val9;
    out[10] = val10;
    out[26] = val11;
    out[ 6] = val12;
    out[22] = val13;
    out[14] = val14;
    out[30] = val15;

    ADD(24, 28);
    ADD(28, 26);
    ADD(26, 30);
    ADD(30, 25);
    ADD(25, 29);
    ADD(29, 27);
    ADD(27, 31);

    out[ 1] = val16 + val24;
    out[17] = val17 + val25;
    out[ 9] = val18 + val26;
    out[25] = val19 + val27;
    out[ 5] = val20 + val28;
    out[21] = val21 + val29;
    out[13] = val22 + val30;
    out[29] = val23 + val31;
    out[ 3] = val24 + val20;
    out[19] = val25 + val21;
    out[11] = val26 + val22;
    out[27] = val27 + val23;
    out[ 7] = val28 + val18;
    out[23] = val29 + val19;
    out[15] = val30 + val17;
    out[31] = val31;
}
