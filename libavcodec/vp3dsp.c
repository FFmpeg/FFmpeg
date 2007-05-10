/*
 * Copyright (C) 2004 the ffmpeg project
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

/**
 * @file vp3dsp.c
 * Standard C DSP-oriented functions cribbed from the original VP3
 * source code.
 */

#include "avcodec.h"
#include "dsputil.h"

#define IdctAdjustBeforeShift 8
#define xC1S7 64277
#define xC2S6 60547
#define xC3S5 54491
#define xC4S4 46341
#define xC5S3 36410
#define xC6S2 25080
#define xC7S1 12785

#define M(a,b) (((a) * (b))>>16)

static av_always_inline void idct(uint8_t *dst, int stride, int16_t *input, int type)
{
    int16_t *ip = input;
    uint8_t *cm = ff_cropTbl + MAX_NEG_CROP;

    int A, B, C, D, Ad, Bd, Cd, Dd, E, F, G, H;
    int Ed, Gd, Add, Bdd, Fd, Hd;

    int i;

    /* Inverse DCT on the rows now */
    for (i = 0; i < 8; i++) {
        /* Check for non-zero values */
        if ( ip[0] | ip[1] | ip[2] | ip[3] | ip[4] | ip[5] | ip[6] | ip[7] ) {
            A = M(xC1S7, ip[1]) + M(xC7S1, ip[7]);
            B = M(xC7S1, ip[1]) - M(xC1S7, ip[7]);
            C = M(xC3S5, ip[3]) + M(xC5S3, ip[5]);
            D = M(xC3S5, ip[5]) - M(xC5S3, ip[3]);

            Ad = M(xC4S4, (A - C));
            Bd = M(xC4S4, (B - D));

            Cd = A + C;
            Dd = B + D;

            E = M(xC4S4, (ip[0] + ip[4]));
            F = M(xC4S4, (ip[0] - ip[4]));

            G = M(xC2S6, ip[2]) + M(xC6S2, ip[6]);
            H = M(xC6S2, ip[2]) - M(xC2S6, ip[6]);

            Ed = E - G;
            Gd = E + G;

            Add = F + Ad;
            Bdd = Bd - H;

            Fd = F - Ad;
            Hd = Bd + H;

            /*  Final sequence of operations over-write original inputs. */
            ip[0] = Gd + Cd ;
            ip[7] = Gd - Cd ;

            ip[1] = Add + Hd;
            ip[2] = Add - Hd;

            ip[3] = Ed + Dd ;
            ip[4] = Ed - Dd ;

            ip[5] = Fd + Bdd;
            ip[6] = Fd - Bdd;
        }

        ip += 8;            /* next row */
    }

    ip = input;

    for ( i = 0; i < 8; i++) {
        /* Check for non-zero values (bitwise or faster than ||) */
        if ( ip[1 * 8] | ip[2 * 8] | ip[3 * 8] |
             ip[4 * 8] | ip[5 * 8] | ip[6 * 8] | ip[7 * 8] ) {

            A = M(xC1S7, ip[1*8]) + M(xC7S1, ip[7*8]);
            B = M(xC7S1, ip[1*8]) - M(xC1S7, ip[7*8]);
            C = M(xC3S5, ip[3*8]) + M(xC5S3, ip[5*8]);
            D = M(xC3S5, ip[5*8]) - M(xC5S3, ip[3*8]);

            Ad = M(xC4S4, (A - C));
            Bd = M(xC4S4, (B - D));

            Cd = A + C;
            Dd = B + D;

            E = M(xC4S4, (ip[0*8] + ip[4*8])) + 8;
            F = M(xC4S4, (ip[0*8] - ip[4*8])) + 8;

            if(type==1){  //HACK
                E += 16*128;
                F += 16*128;
            }

            G = M(xC2S6, ip[2*8]) + M(xC6S2, ip[6*8]);
            H = M(xC6S2, ip[2*8]) - M(xC2S6, ip[6*8]);

            Ed = E - G;
            Gd = E + G;

            Add = F + Ad;
            Bdd = Bd - H;

            Fd = F - Ad;
            Hd = Bd + H;

            /* Final sequence of operations over-write original inputs. */
            if(type==0){
                ip[0*8] = (Gd + Cd )  >> 4;
                ip[7*8] = (Gd - Cd )  >> 4;

                ip[1*8] = (Add + Hd ) >> 4;
                ip[2*8] = (Add - Hd ) >> 4;

                ip[3*8] = (Ed + Dd )  >> 4;
                ip[4*8] = (Ed - Dd )  >> 4;

                ip[5*8] = (Fd + Bdd ) >> 4;
                ip[6*8] = (Fd - Bdd ) >> 4;
            }else if(type==1){
                dst[0*stride] = cm[(Gd + Cd )  >> 4];
                dst[7*stride] = cm[(Gd - Cd )  >> 4];

                dst[1*stride] = cm[(Add + Hd ) >> 4];
                dst[2*stride] = cm[(Add - Hd ) >> 4];

                dst[3*stride] = cm[(Ed + Dd )  >> 4];
                dst[4*stride] = cm[(Ed - Dd )  >> 4];

                dst[5*stride] = cm[(Fd + Bdd ) >> 4];
                dst[6*stride] = cm[(Fd - Bdd ) >> 4];
            }else{
                dst[0*stride] = cm[dst[0*stride] + ((Gd + Cd )  >> 4)];
                dst[7*stride] = cm[dst[7*stride] + ((Gd - Cd )  >> 4)];

                dst[1*stride] = cm[dst[1*stride] + ((Add + Hd ) >> 4)];
                dst[2*stride] = cm[dst[2*stride] + ((Add - Hd ) >> 4)];

                dst[3*stride] = cm[dst[3*stride] + ((Ed + Dd )  >> 4)];
                dst[4*stride] = cm[dst[4*stride] + ((Ed - Dd )  >> 4)];

                dst[5*stride] = cm[dst[5*stride] + ((Fd + Bdd ) >> 4)];
                dst[6*stride] = cm[dst[6*stride] + ((Fd - Bdd ) >> 4)];
            }

        } else {
            if(type==0){
                ip[0*8] =
                ip[1*8] =
                ip[2*8] =
                ip[3*8] =
                ip[4*8] =
                ip[5*8] =
                ip[6*8] =
                ip[7*8] = ((xC4S4 * ip[0*8] + (IdctAdjustBeforeShift<<16))>>20);
            }else if(type==1){
                dst[0*stride]=
                dst[1*stride]=
                dst[2*stride]=
                dst[3*stride]=
                dst[4*stride]=
                dst[5*stride]=
                dst[6*stride]=
                dst[7*stride]= 128 + ((xC4S4 * ip[0*8] + (IdctAdjustBeforeShift<<16))>>20);
            }else{
                if(ip[0*8]){
                    int v= ((xC4S4 * ip[0*8] + (IdctAdjustBeforeShift<<16))>>20);
                    dst[0*stride] = cm[dst[0*stride] + v];
                    dst[1*stride] = cm[dst[1*stride] + v];
                    dst[2*stride] = cm[dst[2*stride] + v];
                    dst[3*stride] = cm[dst[3*stride] + v];
                    dst[4*stride] = cm[dst[4*stride] + v];
                    dst[5*stride] = cm[dst[5*stride] + v];
                    dst[6*stride] = cm[dst[6*stride] + v];
                    dst[7*stride] = cm[dst[7*stride] + v];
                }
            }
        }

        ip++;            /* next column */
        dst++;
    }
}

void ff_vp3_idct_c(DCTELEM *block/* align 16*/){
    idct(NULL, 0, block, 0);
}

void ff_vp3_idct_put_c(uint8_t *dest/*align 8*/, int line_size, DCTELEM *block/*align 16*/){
    idct(dest, line_size, block, 1);
}

void ff_vp3_idct_add_c(uint8_t *dest/*align 8*/, int line_size, DCTELEM *block/*align 16*/){
    idct(dest, line_size, block, 2);
}
