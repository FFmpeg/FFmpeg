/*
 * Copyright (C) 2004 the ffmpeg project
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/**
 * @file vp3dsp.c
 * Standard C DSP-oriented functions cribbed from the original VP3 
 * source code.
 */

#include "common.h"
#include "avcodec.h"
#include "dsputil.h"
#include "vp3data.h"

#define IdctAdjustBeforeShift 8
#define xC1S7 64277
#define xC2S6 60547
#define xC3S5 54491
#define xC4S4 46341
#define xC5S3 36410
#define xC6S2 25080
#define xC7S1 12785

void vp3_dsp_init_c(void)
{
    /* nop */
}

void vp3_idct_c(int16_t *input_data, int16_t *dequant_matrix,
    int coeff_count, int16_t *output_data)
{
    int32_t dequantized_data[64];
    int32_t *ip = dequantized_data;
    int16_t *op = output_data;

    int32_t A_, B_, C_, D_, _Ad, _Bd, _Cd, _Dd, E_, F_, G_, H_;
    int32_t _Ed, _Gd, _Add, _Bdd, _Fd, _Hd;
    int32_t t1, t2;

    int i, j;

    /* de-zigzag and dequantize */
    for (i = 0; i < coeff_count; i++) {
        j = dezigzag_index[i];
        dequantized_data[j] = dequant_matrix[i] * input_data[i];
    }

    /* Inverse DCT on the rows now */
    for (i = 0; i < 8; i++) {
        /* Check for non-zero values */
        if ( ip[0] | ip[1] | ip[2] | ip[3] | ip[4] | ip[5] | ip[6] | ip[7] ) {
            t1 = (int32_t)(xC1S7 * ip[1]);
            t2 = (int32_t)(xC7S1 * ip[7]);
            t1 >>= 16;
            t2 >>= 16;
            A_ = t1 + t2;

            t1 = (int32_t)(xC7S1 * ip[1]);
            t2 = (int32_t)(xC1S7 * ip[7]);
            t1 >>= 16;
            t2 >>= 16;
            B_ = t1 - t2;

            t1 = (int32_t)(xC3S5 * ip[3]);
            t2 = (int32_t)(xC5S3 * ip[5]);
            t1 >>= 16;
            t2 >>= 16;
            C_ = t1 + t2;

            t1 = (int32_t)(xC3S5 * ip[5]);
            t2 = (int32_t)(xC5S3 * ip[3]);
            t1 >>= 16;
            t2 >>= 16;
            D_ = t1 - t2;


            t1 = (int32_t)(xC4S4 * (A_ - C_));
            t1 >>= 16;
            _Ad = t1;

            t1 = (int32_t)(xC4S4 * (B_ - D_));
            t1 >>= 16;
            _Bd = t1;


            _Cd = A_ + C_;
            _Dd = B_ + D_;

            t1 = (int32_t)(xC4S4 * (ip[0] + ip[4]));
            t1 >>= 16;
            E_ = t1;

            t1 = (int32_t)(xC4S4 * (ip[0] - ip[4]));
            t1 >>= 16;
            F_ = t1;

            t1 = (int32_t)(xC2S6 * ip[2]);
            t2 = (int32_t)(xC6S2 * ip[6]);
            t1 >>= 16;
            t2 >>= 16;
            G_ = t1 + t2;

            t1 = (int32_t)(xC6S2 * ip[2]);
            t2 = (int32_t)(xC2S6 * ip[6]);
            t1 >>= 16;
            t2 >>= 16;
            H_ = t1 - t2;


            _Ed = E_ - G_;
            _Gd = E_ + G_;

            _Add = F_ + _Ad;
            _Bdd = _Bd - H_;

            _Fd = F_ - _Ad;
            _Hd = _Bd + H_;

            /*  Final sequence of operations over-write original inputs. */
            ip[0] = (int16_t)((_Gd + _Cd )   >> 0);
            ip[7] = (int16_t)((_Gd - _Cd )   >> 0);

            ip[1] = (int16_t)((_Add + _Hd )  >> 0);
            ip[2] = (int16_t)((_Add - _Hd )  >> 0);

            ip[3] = (int16_t)((_Ed + _Dd )   >> 0);
            ip[4] = (int16_t)((_Ed - _Dd )   >> 0);

            ip[5] = (int16_t)((_Fd + _Bdd )  >> 0);
            ip[6] = (int16_t)((_Fd - _Bdd )  >> 0);

        }

        ip += 8;            /* next row */
    }

    ip = dequantized_data;

    for ( i = 0; i < 8; i++) {
        /* Check for non-zero values (bitwise or faster than ||) */
        if ( ip[0 * 8] | ip[1 * 8] | ip[2 * 8] | ip[3 * 8] |
             ip[4 * 8] | ip[5 * 8] | ip[6 * 8] | ip[7 * 8] ) {

            t1 = (int32_t)(xC1S7 * ip[1*8]);
            t2 = (int32_t)(xC7S1 * ip[7*8]);
            t1 >>= 16;
            t2 >>= 16;
            A_ = t1 + t2;

            t1 = (int32_t)(xC7S1 * ip[1*8]);
            t2 = (int32_t)(xC1S7 * ip[7*8]);
            t1 >>= 16;
            t2 >>= 16;
            B_ = t1 - t2;

            t1 = (int32_t)(xC3S5 * ip[3*8]);
            t2 = (int32_t)(xC5S3 * ip[5*8]);
            t1 >>= 16;
            t2 >>= 16;
            C_ = t1 + t2;

            t1 = (int32_t)(xC3S5 * ip[5*8]);
            t2 = (int32_t)(xC5S3 * ip[3*8]);
            t1 >>= 16;
            t2 >>= 16;
            D_ = t1 - t2;


            t1 = (int32_t)(xC4S4 * (A_ - C_));
            t1 >>= 16;
            _Ad = t1;

            t1 = (int32_t)(xC4S4 * (B_ - D_));
            t1 >>= 16;
            _Bd = t1;


            _Cd = A_ + C_;
            _Dd = B_ + D_;

            t1 = (int32_t)(xC4S4 * (ip[0*8] + ip[4*8]));
            t1 >>= 16;
            E_ = t1;

            t1 = (int32_t)(xC4S4 * (ip[0*8] - ip[4*8]));
            t1 >>= 16;
            F_ = t1;

            t1 = (int32_t)(xC2S6 * ip[2*8]);
            t2 = (int32_t)(xC6S2 * ip[6*8]);
            t1 >>= 16;
            t2 >>= 16;
            G_ = t1 + t2;

            t1 = (int32_t)(xC6S2 * ip[2*8]);
            t2 = (int32_t)(xC2S6 * ip[6*8]);
            t1 >>= 16;
            t2 >>= 16;
            H_ = t1 - t2;


            _Ed = E_ - G_;
            _Gd = E_ + G_;

            _Add = F_ + _Ad;
            _Bdd = _Bd - H_;

            _Fd = F_ - _Ad;
            _Hd = _Bd + H_;

            _Gd += IdctAdjustBeforeShift;
            _Add += IdctAdjustBeforeShift;
            _Ed += IdctAdjustBeforeShift;
            _Fd += IdctAdjustBeforeShift;

            /* Final sequence of operations over-write original inputs. */
            op[0*8] = (int16_t)((_Gd + _Cd )   >> 4);
            op[7*8] = (int16_t)((_Gd - _Cd )   >> 4);

            op[1*8] = (int16_t)((_Add + _Hd )  >> 4);
            op[2*8] = (int16_t)((_Add - _Hd )  >> 4);

            op[3*8] = (int16_t)((_Ed + _Dd )   >> 4);
            op[4*8] = (int16_t)((_Ed - _Dd )   >> 4);

            op[5*8] = (int16_t)((_Fd + _Bdd )  >> 4);
            op[6*8] = (int16_t)((_Fd - _Bdd )  >> 4);

        } else {

            op[0*8] = 0;
            op[7*8] = 0;
            op[1*8] = 0;
            op[2*8] = 0;
            op[3*8] = 0;
            op[4*8] = 0;
            op[5*8] = 0;
            op[6*8] = 0;
        }

        ip++;            /* next column */
        op++;
    }
}
