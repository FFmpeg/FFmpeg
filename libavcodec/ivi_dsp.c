/*
 * DSP functions for Indeo Video Interactive codecs (Indeo4 and Indeo5)
 *
 * Copyright (c) 2009-2011 Maxim Poliakovski
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * DSP functions (inverse transforms, motion compensation, wavelet recompositions)
 * for Indeo Video Interactive codecs.
 */

#include "avcodec.h"
#include "ivi.h"
#include "ivi_dsp.h"

void ff_ivi_recompose53(const IVIPlaneDesc *plane, uint8_t *dst,
                        const int dst_pitch)
{
    int             x, y, indx;
    int32_t         p0, p1, p2, p3, tmp0, tmp1, tmp2;
    int32_t         b0_1, b0_2, b1_1, b1_2, b1_3, b2_1, b2_2, b2_3, b2_4, b2_5, b2_6;
    int32_t         b3_1, b3_2, b3_3, b3_4, b3_5, b3_6, b3_7, b3_8, b3_9;
    int32_t         pitch, back_pitch;
    const short    *b0_ptr, *b1_ptr, *b2_ptr, *b3_ptr;
    const int       num_bands = 4;

    /* all bands should have the same pitch */
    pitch = plane->bands[0].pitch;

    /* pixels at the position "y-1" will be set to pixels at the "y" for the 1st iteration */
    back_pitch = 0;

    /* get pointers to the wavelet bands */
    b0_ptr = plane->bands[0].buf;
    b1_ptr = plane->bands[1].buf;
    b2_ptr = plane->bands[2].buf;
    b3_ptr = plane->bands[3].buf;

    for (y = 0; y < plane->height; y += 2) {
        /* load storage variables with values */
        if (num_bands > 0) {
            b0_1 = b0_ptr[0];
            b0_2 = b0_ptr[pitch];
        }

        if (num_bands > 1) {
            b1_1 = b1_ptr[back_pitch];
            b1_2 = b1_ptr[0];
            b1_3 = b1_1 - b1_2*6 + b1_ptr[pitch];
        }

        if (num_bands > 2) {
            b2_2 = b2_ptr[0];     // b2[x,  y  ]
            b2_3 = b2_2;          // b2[x+1,y  ] = b2[x,y]
            b2_5 = b2_ptr[pitch]; // b2[x  ,y+1]
            b2_6 = b2_5;          // b2[x+1,y+1] = b2[x,y+1]
        }

        if (num_bands > 3) {
            b3_2 = b3_ptr[back_pitch]; // b3[x  ,y-1]
            b3_3 = b3_2;               // b3[x+1,y-1] = b3[x  ,y-1]
            b3_5 = b3_ptr[0];          // b3[x  ,y  ]
            b3_6 = b3_5;               // b3[x+1,y  ] = b3[x  ,y  ]
            b3_8 = b3_2 - b3_5*6 + b3_ptr[pitch];
            b3_9 = b3_8;
        }

        for (x = 0, indx = 0; x < plane->width; x+=2, indx++) {
            /* some values calculated in the previous iterations can */
            /* be reused in the next ones, so do appropriate copying */
            b2_1 = b2_2; // b2[x-1,y  ] = b2[x,  y  ]
            b2_2 = b2_3; // b2[x  ,y  ] = b2[x+1,y  ]
            b2_4 = b2_5; // b2[x-1,y+1] = b2[x  ,y+1]
            b2_5 = b2_6; // b2[x  ,y+1] = b2[x+1,y+1]
            b3_1 = b3_2; // b3[x-1,y-1] = b3[x  ,y-1]
            b3_2 = b3_3; // b3[x  ,y-1] = b3[x+1,y-1]
            b3_4 = b3_5; // b3[x-1,y  ] = b3[x  ,y  ]
            b3_5 = b3_6; // b3[x  ,y  ] = b3[x+1,y  ]
            b3_7 = b3_8; // vert_HPF(x-1)
            b3_8 = b3_9; // vert_HPF(x  )

            p0 = p1 = p2 = p3 = 0;

            /* process the LL-band by applying LPF both vertically and horizontally */
            if (num_bands > 0) {
                tmp0 = b0_1;
                tmp2 = b0_2;
                b0_1 = b0_ptr[indx+1];
                b0_2 = b0_ptr[pitch+indx+1];
                tmp1 = tmp0 + b0_1;

                p0 =  tmp0 << 4;
                p1 =  tmp1 << 3;
                p2 = (tmp0 + tmp2) << 3;
                p3 = (tmp1 + tmp2 + b0_2) << 2;
            }

            /* process the HL-band by applying HPF vertically and LPF horizontally */
            if (num_bands > 1) {
                tmp0 = b1_2;
                tmp1 = b1_1;
                b1_2 = b1_ptr[indx+1];
                b1_1 = b1_ptr[back_pitch+indx+1];

                tmp2 = tmp1 - tmp0*6 + b1_3;
                b1_3 = b1_1 - b1_2*6 + b1_ptr[pitch+indx+1];

                p0 += (tmp0 + tmp1) << 3;
                p1 += (tmp0 + tmp1 + b1_1 + b1_2) << 2;
                p2 +=  tmp2 << 2;
                p3 += (tmp2 + b1_3) << 1;
            }

            /* process the LH-band by applying LPF vertically and HPF horizontally */
            if (num_bands > 2) {
                b2_3 = b2_ptr[indx+1];
                b2_6 = b2_ptr[pitch+indx+1];

                tmp0 = b2_1 + b2_2;
                tmp1 = b2_1 - b2_2*6 + b2_3;

                p0 += tmp0 << 3;
                p1 += tmp1 << 2;
                p2 += (tmp0 + b2_4 + b2_5) << 2;
                p3 += (tmp1 + b2_4 - b2_5*6 + b2_6) << 1;
            }

            /* process the HH-band by applying HPF both vertically and horizontally */
            if (num_bands > 3) {
                b3_6 = b3_ptr[indx+1];            // b3[x+1,y  ]
                b3_3 = b3_ptr[back_pitch+indx+1]; // b3[x+1,y-1]

                tmp0 = b3_1 + b3_4;
                tmp1 = b3_2 + b3_5;
                tmp2 = b3_3 + b3_6;

                b3_9 = b3_3 - b3_6*6 + b3_ptr[pitch+indx+1];

                p0 += (tmp0 + tmp1) << 2;
                p1 += (tmp0 - tmp1*6 + tmp2) << 1;
                p2 += (b3_7 + b3_8) << 1;
                p3 +=  b3_7 - b3_8*6 + b3_9;
            }

            /* output four pixels */
            dst[x]             = av_clip_uint8((p0 >> 6) + 128);
            dst[x+1]           = av_clip_uint8((p1 >> 6) + 128);
            dst[dst_pitch+x]   = av_clip_uint8((p2 >> 6) + 128);
            dst[dst_pitch+x+1] = av_clip_uint8((p3 >> 6) + 128);
        }// for x

        dst += dst_pitch << 1;

        back_pitch = -pitch;

        b0_ptr += pitch;
        b1_ptr += pitch;
        b2_ptr += pitch;
        b3_ptr += pitch;
    }
}

void ff_ivi_recompose_haar(const IVIPlaneDesc *plane, uint8_t *dst,
                           const int dst_pitch)
{
    int             x, y, indx, b0, b1, b2, b3, p0, p1, p2, p3;
    const short    *b0_ptr, *b1_ptr, *b2_ptr, *b3_ptr;
    int32_t         pitch;

    /* all bands should have the same pitch */
    pitch = plane->bands[0].pitch;

    /* get pointers to the wavelet bands */
    b0_ptr = plane->bands[0].buf;
    b1_ptr = plane->bands[1].buf;
    b2_ptr = plane->bands[2].buf;
    b3_ptr = plane->bands[3].buf;

    for (y = 0; y < plane->height; y += 2) {
        for (x = 0, indx = 0; x < plane->width; x += 2, indx++) {
            /* load coefficients */
            b0 = b0_ptr[indx]; //should be: b0 = (num_bands > 0) ? b0_ptr[indx] : 0;
            b1 = b1_ptr[indx]; //should be: b1 = (num_bands > 1) ? b1_ptr[indx] : 0;
            b2 = b2_ptr[indx]; //should be: b2 = (num_bands > 2) ? b2_ptr[indx] : 0;
            b3 = b3_ptr[indx]; //should be: b3 = (num_bands > 3) ? b3_ptr[indx] : 0;

            /* haar wavelet recomposition */
            p0 = (b0 + b1 + b2 + b3 + 2) >> 2;
            p1 = (b0 + b1 - b2 - b3 + 2) >> 2;
            p2 = (b0 - b1 + b2 - b3 + 2) >> 2;
            p3 = (b0 - b1 - b2 + b3 + 2) >> 2;

            /* bias, convert and output four pixels */
            dst[x]                 = av_clip_uint8(p0 + 128);
            dst[x + 1]             = av_clip_uint8(p1 + 128);
            dst[dst_pitch + x]     = av_clip_uint8(p2 + 128);
            dst[dst_pitch + x + 1] = av_clip_uint8(p3 + 128);
        }// for x

        dst += dst_pitch << 1;

        b0_ptr += pitch;
        b1_ptr += pitch;
        b2_ptr += pitch;
        b3_ptr += pitch;
    }// for y
}

/** butterfly operation for the inverse Haar transform */
#define IVI_HAAR_BFLY(s1, s2, o1, o2, t) \
    t  = (s1 - s2) >> 1;\
    o1 = (s1 + s2) >> 1;\
    o2 = t;\

/** inverse 8-point Haar transform */
#define INV_HAAR8(s1, s5, s3, s7, s2, s4, s6, s8,\
                  d1, d2, d3, d4, d5, d6, d7, d8,\
                  t0, t1, t2, t3, t4, t5, t6, t7, t8) {\
    t1 = s1 << 1; t5 = s5 << 1;\
    IVI_HAAR_BFLY(t1, t5, t1, t5, t0); IVI_HAAR_BFLY(t1, s3, t1, t3, t0);\
    IVI_HAAR_BFLY(t5, s7, t5, t7, t0); IVI_HAAR_BFLY(t1, s2, t1, t2, t0);\
    IVI_HAAR_BFLY(t3, s4, t3, t4, t0); IVI_HAAR_BFLY(t5, s6, t5, t6, t0);\
    IVI_HAAR_BFLY(t7, s8, t7, t8, t0);\
    d1 = COMPENSATE(t1);\
    d2 = COMPENSATE(t2);\
    d3 = COMPENSATE(t3);\
    d4 = COMPENSATE(t4);\
    d5 = COMPENSATE(t5);\
    d6 = COMPENSATE(t6);\
    d7 = COMPENSATE(t7);\
    d8 = COMPENSATE(t8); }

/** inverse 4-point Haar transform */
#define INV_HAAR4(s1, s3, s5, s7, d1, d2, d3, d4, t0, t1, t2, t3, t4) {\
    IVI_HAAR_BFLY(s1, s3, t0, t1, t4);\
    IVI_HAAR_BFLY(t0, s5, t2, t3, t4);\
    d1 = COMPENSATE(t2);\
    d2 = COMPENSATE(t3);\
    IVI_HAAR_BFLY(t1, s7, t2, t3, t4);\
    d3 = COMPENSATE(t2);\
    d4 = COMPENSATE(t3); }

void ff_ivi_inverse_haar_8x8(const int32_t *in, int16_t *out, uint32_t pitch,
                             const uint8_t *flags)
{
    int     i, shift, sp1, sp2, sp3, sp4;
    const int32_t *src;
    int32_t *dst;
    int     tmp[64];
    int     t0, t1, t2, t3, t4, t5, t6, t7, t8;

    /* apply the InvHaar8 to all columns */
#define COMPENSATE(x) (x)
    src = in;
    dst = tmp;
    for (i = 0; i < 8; i++) {
        if (flags[i]) {
            /* pre-scaling */
            shift = !(i & 4);
            sp1 = src[ 0] << shift;
            sp2 = src[ 8] << shift;
            sp3 = src[16] << shift;
            sp4 = src[24] << shift;
            INV_HAAR8(    sp1,     sp2,     sp3,     sp4,
                      src[32], src[40], src[48], src[56],
                      dst[ 0], dst[ 8], dst[16], dst[24],
                      dst[32], dst[40], dst[48], dst[56],
                      t0, t1, t2, t3, t4, t5, t6, t7, t8);
        } else
            dst[ 0] = dst[ 8] = dst[16] = dst[24] =
            dst[32] = dst[40] = dst[48] = dst[56] = 0;

        src++;
        dst++;
    }
#undef  COMPENSATE

    /* apply the InvHaar8 to all rows */
#define COMPENSATE(x) (x)
    src = tmp;
    for (i = 0; i < 8; i++) {
        if (   !src[0] && !src[1] && !src[2] && !src[3]
            && !src[4] && !src[5] && !src[6] && !src[7]) {
            memset(out, 0, 8 * sizeof(out[0]));
        } else {
            INV_HAAR8(src[0], src[1], src[2], src[3],
                      src[4], src[5], src[6], src[7],
                      out[0], out[1], out[2], out[3],
                      out[4], out[5], out[6], out[7],
                      t0, t1, t2, t3, t4, t5, t6, t7, t8);
        }
        src += 8;
        out += pitch;
    }
#undef  COMPENSATE
}

void ff_ivi_row_haar8(const int32_t *in, int16_t *out, uint32_t pitch,
                      const uint8_t *flags)
{
    int     i;
    int     t0, t1, t2, t3, t4, t5, t6, t7, t8;

    /* apply the InvHaar8 to all rows */
#define COMPENSATE(x) (x)
    for (i = 0; i < 8; i++) {
        if (   !in[0] && !in[1] && !in[2] && !in[3]
            && !in[4] && !in[5] && !in[6] && !in[7]) {
            memset(out, 0, 8 * sizeof(out[0]));
        } else {
            INV_HAAR8(in[0],  in[1],  in[2],  in[3],
                      in[4],  in[5],  in[6],  in[7],
                      out[0], out[1], out[2], out[3],
                      out[4], out[5], out[6], out[7],
                      t0, t1, t2, t3, t4, t5, t6, t7, t8);
        }
        in  += 8;
        out += pitch;
    }
#undef  COMPENSATE
}

void ff_ivi_col_haar8(const int32_t *in, int16_t *out, uint32_t pitch,
                      const uint8_t *flags)
{
    int     i;
    int     t0, t1, t2, t3, t4, t5, t6, t7, t8;

    /* apply the InvHaar8 to all columns */
#define COMPENSATE(x) (x)
    for (i = 0; i < 8; i++) {
        if (flags[i]) {
            INV_HAAR8(in[ 0], in[ 8], in[16], in[24],
                      in[32], in[40], in[48], in[56],
                      out[0 * pitch], out[1 * pitch],
                      out[2 * pitch], out[3 * pitch],
                      out[4 * pitch], out[5 * pitch],
                      out[6 * pitch], out[7 * pitch],
                      t0, t1, t2, t3, t4, t5, t6, t7, t8);
        } else
            out[0 * pitch] = out[1 * pitch] =
            out[2 * pitch] = out[3 * pitch] =
            out[4 * pitch] = out[5 * pitch] =
            out[6 * pitch] = out[7 * pitch] = 0;

        in++;
        out++;
    }
#undef  COMPENSATE
}

void ff_ivi_inverse_haar_4x4(const int32_t *in, int16_t *out, uint32_t pitch,
                             const uint8_t *flags)
{
    int     i, shift, sp1, sp2;
    const int32_t *src;
    int32_t *dst;
    int     tmp[16];
    int     t0, t1, t2, t3, t4;

    /* apply the InvHaar4 to all columns */
#define COMPENSATE(x) (x)
    src = in;
    dst = tmp;
    for (i = 0; i < 4; i++) {
        if (flags[i]) {
            /* pre-scaling */
            shift = !(i & 2);
            sp1 = src[0] << shift;
            sp2 = src[4] << shift;
            INV_HAAR4(   sp1,    sp2, src[8], src[12],
                      dst[0], dst[4], dst[8], dst[12],
                      t0, t1, t2, t3, t4);
        } else
            dst[0] = dst[4] = dst[8] = dst[12] = 0;

        src++;
        dst++;
    }
#undef  COMPENSATE

    /* apply the InvHaar8 to all rows */
#define COMPENSATE(x) (x)
    src = tmp;
    for (i = 0; i < 4; i++) {
        if (!src[0] && !src[1] && !src[2] && !src[3]) {
            memset(out, 0, 4 * sizeof(out[0]));
        } else {
            INV_HAAR4(src[0], src[1], src[2], src[3],
                      out[0], out[1], out[2], out[3],
                      t0, t1, t2, t3, t4);
        }
        src += 4;
        out += pitch;
    }
#undef  COMPENSATE
}

void ff_ivi_row_haar4(const int32_t *in, int16_t *out, uint32_t pitch,
                      const uint8_t *flags)
{
    int     i;
    int     t0, t1, t2, t3, t4;

    /* apply the InvHaar4 to all rows */
#define COMPENSATE(x) (x)
    for (i = 0; i < 4; i++) {
        if (!in[0] && !in[1] && !in[2] && !in[3]) {
            memset(out, 0, 4 * sizeof(out[0]));
        } else {
            INV_HAAR4(in[0], in[1], in[2], in[3],
                      out[0], out[1], out[2], out[3],
                      t0, t1, t2, t3, t4);
        }
        in  += 4;
        out += pitch;
    }
#undef  COMPENSATE
}

void ff_ivi_col_haar4(const int32_t *in, int16_t *out, uint32_t pitch,
                      const uint8_t *flags)
{
    int     i;
    int     t0, t1, t2, t3, t4;

    /* apply the InvHaar8 to all columns */
#define COMPENSATE(x) (x)
    for (i = 0; i < 4; i++) {
        if (flags[i]) {
            INV_HAAR4(in[0], in[4], in[8], in[12],
                      out[0 * pitch], out[1 * pitch],
                      out[2 * pitch], out[3 * pitch],
                      t0, t1, t2, t3, t4);
        } else
            out[0 * pitch] = out[1 * pitch] =
            out[2 * pitch] = out[3 * pitch] = 0;

        in++;
        out++;
    }
#undef  COMPENSATE
}

void ff_ivi_dc_haar_2d(const int32_t *in, int16_t *out, uint32_t pitch,
                       int blk_size)
{
    int     x, y;
    int16_t dc_coeff;

    dc_coeff = (*in + 0) >> 3;

    for (y = 0; y < blk_size; out += pitch, y++) {
        for (x = 0; x < blk_size; x++)
            out[x] = dc_coeff;
    }
}

/** butterfly operation for the inverse slant transform */
#define IVI_SLANT_BFLY(s1, s2, o1, o2, t) \
    t  = s1 - s2;\
    o1 = s1 + s2;\
    o2 = t;\

/** This is a reflection a,b = 1/2, 5/4 for the inverse slant transform */
#define IVI_IREFLECT(s1, s2, o1, o2, t) \
    t  = ((s1 + s2*2 + 2) >> 2) + s1;\
    o2 = ((s1*2 - s2 + 2) >> 2) - s2;\
    o1 = t;\

/** This is a reflection a,b = 1/2, 7/8 for the inverse slant transform */
#define IVI_SLANT_PART4(s1, s2, o1, o2, t) \
    t  = s2 + ((s1*4  - s2 + 4) >> 3);\
    o2 = s1 + ((-s1 - s2*4 + 4) >> 3);\
    o1 = t;\

/** inverse slant8 transform */
#define IVI_INV_SLANT8(s1, s4, s8, s5, s2, s6, s3, s7,\
                       d1, d2, d3, d4, d5, d6, d7, d8,\
                       t0, t1, t2, t3, t4, t5, t6, t7, t8) {\
    IVI_SLANT_PART4(s4, s5, t4, t5, t0);\
\
    IVI_SLANT_BFLY(s1, t5, t1, t5, t0); IVI_SLANT_BFLY(s2, s6, t2, t6, t0);\
    IVI_SLANT_BFLY(s7, s3, t7, t3, t0); IVI_SLANT_BFLY(t4, s8, t4, t8, t0);\
\
    IVI_SLANT_BFLY(t1, t2, t1, t2, t0); IVI_IREFLECT  (t4, t3, t4, t3, t0);\
    IVI_SLANT_BFLY(t5, t6, t5, t6, t0); IVI_IREFLECT  (t8, t7, t8, t7, t0);\
    IVI_SLANT_BFLY(t1, t4, t1, t4, t0); IVI_SLANT_BFLY(t2, t3, t2, t3, t0);\
    IVI_SLANT_BFLY(t5, t8, t5, t8, t0); IVI_SLANT_BFLY(t6, t7, t6, t7, t0);\
    d1 = COMPENSATE(t1);\
    d2 = COMPENSATE(t2);\
    d3 = COMPENSATE(t3);\
    d4 = COMPENSATE(t4);\
    d5 = COMPENSATE(t5);\
    d6 = COMPENSATE(t6);\
    d7 = COMPENSATE(t7);\
    d8 = COMPENSATE(t8);}

/** inverse slant4 transform */
#define IVI_INV_SLANT4(s1, s4, s2, s3, d1, d2, d3, d4, t0, t1, t2, t3, t4) {\
    IVI_SLANT_BFLY(s1, s2, t1, t2, t0); IVI_IREFLECT  (s4, s3, t4, t3, t0);\
\
    IVI_SLANT_BFLY(t1, t4, t1, t4, t0); IVI_SLANT_BFLY(t2, t3, t2, t3, t0);\
    d1 = COMPENSATE(t1);\
    d2 = COMPENSATE(t2);\
    d3 = COMPENSATE(t3);\
    d4 = COMPENSATE(t4);}

void ff_ivi_inverse_slant_8x8(const int32_t *in, int16_t *out, uint32_t pitch, const uint8_t *flags)
{
    int     i;
    const int32_t *src;
    int32_t *dst;
    int     tmp[64];
    int     t0, t1, t2, t3, t4, t5, t6, t7, t8;

#define COMPENSATE(x) (x)
    src = in;
    dst = tmp;
    for (i = 0; i < 8; i++) {
        if (flags[i]) {
            IVI_INV_SLANT8(src[0], src[8], src[16], src[24], src[32], src[40], src[48], src[56],
                           dst[0], dst[8], dst[16], dst[24], dst[32], dst[40], dst[48], dst[56],
                           t0, t1, t2, t3, t4, t5, t6, t7, t8);
        } else
            dst[0] = dst[8] = dst[16] = dst[24] = dst[32] = dst[40] = dst[48] = dst[56] = 0;

        src++;
        dst++;
    }
#undef COMPENSATE

#define COMPENSATE(x) ((x + 1)>>1)
    src = tmp;
    for (i = 0; i < 8; i++) {
        if (!src[0] && !src[1] && !src[2] && !src[3] && !src[4] && !src[5] && !src[6] && !src[7]) {
            memset(out, 0, 8*sizeof(out[0]));
        } else {
            IVI_INV_SLANT8(src[0], src[1], src[2], src[3], src[4], src[5], src[6], src[7],
                           out[0], out[1], out[2], out[3], out[4], out[5], out[6], out[7],
                           t0, t1, t2, t3, t4, t5, t6, t7, t8);
        }
        src += 8;
        out += pitch;
    }
#undef COMPENSATE
}

void ff_ivi_inverse_slant_4x4(const int32_t *in, int16_t *out, uint32_t pitch, const uint8_t *flags)
{
    int     i;
    const int32_t *src;
    int32_t *dst;
    int     tmp[16];
    int     t0, t1, t2, t3, t4;

#define COMPENSATE(x) (x)
    src = in;
    dst = tmp;
    for (i = 0; i < 4; i++) {
        if (flags[i]) {
            IVI_INV_SLANT4(src[0], src[4], src[8], src[12],
                           dst[0], dst[4], dst[8], dst[12],
                           t0, t1, t2, t3, t4);
        } else
            dst[0] = dst[4] = dst[8] = dst[12] = 0;

        src++;
        dst++;
    }
#undef COMPENSATE

#define COMPENSATE(x) ((x + 1)>>1)
    src = tmp;
    for (i = 0; i < 4; i++) {
        if (!src[0] && !src[1] && !src[2] && !src[3]) {
            out[0] = out[1] = out[2] = out[3] = 0;
        } else {
            IVI_INV_SLANT4(src[0], src[1], src[2], src[3],
                           out[0], out[1], out[2], out[3],
                           t0, t1, t2, t3, t4);
        }
        src += 4;
        out += pitch;
    }
#undef COMPENSATE
}

void ff_ivi_dc_slant_2d(const int32_t *in, int16_t *out, uint32_t pitch, int blk_size)
{
    int     x, y;
    int16_t dc_coeff;

    dc_coeff = (*in + 1) >> 1;

    for (y = 0; y < blk_size; out += pitch, y++) {
        for (x = 0; x < blk_size; x++)
            out[x] = dc_coeff;
    }
}

void ff_ivi_row_slant8(const int32_t *in, int16_t *out, uint32_t pitch, const uint8_t *flags)
{
    int     i;
    int     t0, t1, t2, t3, t4, t5, t6, t7, t8;

#define COMPENSATE(x) ((x + 1)>>1)
    for (i = 0; i < 8; i++) {
        if (!in[0] && !in[1] && !in[2] && !in[3] && !in[4] && !in[5] && !in[6] && !in[7]) {
            memset(out, 0, 8*sizeof(out[0]));
        } else {
            IVI_INV_SLANT8( in[0],  in[1],  in[2],  in[3],  in[4],  in[5],  in[6],  in[7],
                           out[0], out[1], out[2], out[3], out[4], out[5], out[6], out[7],
                           t0, t1, t2, t3, t4, t5, t6, t7, t8);
        }
        in += 8;
        out += pitch;
    }
#undef COMPENSATE
}

void ff_ivi_dc_row_slant(const int32_t *in, int16_t *out, uint32_t pitch, int blk_size)
{
    int     x, y;
    int16_t dc_coeff;

    dc_coeff = (*in + 1) >> 1;

    for (x = 0; x < blk_size; x++)
        out[x] = dc_coeff;

    out += pitch;

    for (y = 1; y < blk_size; out += pitch, y++) {
        for (x = 0; x < blk_size; x++)
            out[x] = 0;
    }
}

void ff_ivi_col_slant8(const int32_t *in, int16_t *out, uint32_t pitch, const uint8_t *flags)
{
    int     i, row2, row4, row8;
    int     t0, t1, t2, t3, t4, t5, t6, t7, t8;

    row2 = pitch << 1;
    row4 = pitch << 2;
    row8 = pitch << 3;

#define COMPENSATE(x) ((x + 1)>>1)
    for (i = 0; i < 8; i++) {
        if (flags[i]) {
            IVI_INV_SLANT8(in[0], in[8], in[16], in[24], in[32], in[40], in[48], in[56],
                           out[0], out[pitch], out[row2], out[row2 + pitch], out[row4],
                           out[row4 + pitch],  out[row4 + row2], out[row8 - pitch],
                           t0, t1, t2, t3, t4, t5, t6, t7, t8);
        } else {
            out[0] = out[pitch] = out[row2] = out[row2 + pitch] = out[row4] =
            out[row4 + pitch] =  out[row4 + row2] = out[row8 - pitch] = 0;
        }

        in++;
        out++;
    }
#undef COMPENSATE
}

void ff_ivi_dc_col_slant(const int32_t *in, int16_t *out, uint32_t pitch, int blk_size)
{
    int     x, y;
    int16_t dc_coeff;

    dc_coeff = (*in + 1) >> 1;

    for (y = 0; y < blk_size; out += pitch, y++) {
        out[0] = dc_coeff;
        for (x = 1; x < blk_size; x++)
            out[x] = 0;
    }
}

void ff_ivi_row_slant4(const int32_t *in, int16_t *out, uint32_t pitch, const uint8_t *flags)
{
    int     i;
    int     t0, t1, t2, t3, t4;

#define COMPENSATE(x) ((x + 1)>>1)
    for (i = 0; i < 4; i++) {
        if (!in[0] && !in[1] && !in[2] && !in[3]) {
            memset(out, 0, 4*sizeof(out[0]));
        } else {
            IVI_INV_SLANT4( in[0],  in[1],  in[2],  in[3],
                           out[0], out[1], out[2], out[3],
                           t0, t1, t2, t3, t4);
        }
        in  += 4;
        out += pitch;
    }
#undef COMPENSATE
}

void ff_ivi_col_slant4(const int32_t *in, int16_t *out, uint32_t pitch, const uint8_t *flags)
{
    int     i, row2;
    int     t0, t1, t2, t3, t4;

    row2 = pitch << 1;

#define COMPENSATE(x) ((x + 1)>>1)
    for (i = 0; i < 4; i++) {
        if (flags[i]) {
            IVI_INV_SLANT4(in[0], in[4], in[8], in[12],
                           out[0], out[pitch], out[row2], out[row2 + pitch],
                           t0, t1, t2, t3, t4);
        } else {
            out[0] = out[pitch] = out[row2] = out[row2 + pitch] = 0;
        }

        in++;
        out++;
    }
#undef COMPENSATE
}

void ff_ivi_put_pixels_8x8(const int32_t *in, int16_t *out, uint32_t pitch,
                           const uint8_t *flags)
{
    int     x, y;

    for (y = 0; y < 8; out += pitch, in += 8, y++)
        for (x = 0; x < 8; x++)
            out[x] = in[x];
}

void ff_ivi_put_dc_pixel_8x8(const int32_t *in, int16_t *out, uint32_t pitch,
                             int blk_size)
{
    int     y;

    out[0] = in[0];
    memset(out + 1, 0, 7*sizeof(out[0]));
    out += pitch;

    for (y = 1; y < 8; out += pitch, y++)
        memset(out, 0, 8*sizeof(out[0]));
}

#define IVI_MC_TEMPLATE(size, suffix, OP) \
static void ivi_mc_ ## size ##x## size ## suffix(int16_t *buf, \
                                                 uint32_t dpitch, \
                                                 const int16_t *ref_buf, \
                                                 uint32_t pitch, int mc_type) \
{ \
    int     i, j; \
    const int16_t *wptr; \
\
    switch (mc_type) { \
    case 0: /* fullpel (no interpolation) */ \
        for (i = 0; i < size; i++, buf += dpitch, ref_buf += pitch) { \
            for (j = 0; j < size; j++) {\
                OP(buf[j], ref_buf[j]); \
            } \
        } \
        break; \
    case 1: /* horizontal halfpel interpolation */ \
        for (i = 0; i < size; i++, buf += dpitch, ref_buf += pitch) \
            for (j = 0; j < size; j++) \
                OP(buf[j], (ref_buf[j] + ref_buf[j+1]) >> 1); \
        break; \
    case 2: /* vertical halfpel interpolation */ \
        wptr = ref_buf + pitch; \
        for (i = 0; i < size; i++, buf += dpitch, wptr += pitch, ref_buf += pitch) \
            for (j = 0; j < size; j++) \
                OP(buf[j], (ref_buf[j] + wptr[j]) >> 1); \
        break; \
    case 3: /* vertical and horizontal halfpel interpolation */ \
        wptr = ref_buf + pitch; \
        for (i = 0; i < size; i++, buf += dpitch, wptr += pitch, ref_buf += pitch) \
            for (j = 0; j < size; j++) \
                OP(buf[j], (ref_buf[j] + ref_buf[j+1] + wptr[j] + wptr[j+1]) >> 2); \
        break; \
    } \
} \
\
void ff_ivi_mc_ ## size ##x## size ## suffix(int16_t *buf, const int16_t *ref_buf, \
                                             uint32_t pitch, int mc_type) \
{ \
    ivi_mc_ ## size ##x## size ## suffix(buf, pitch, ref_buf, pitch, mc_type); \
} \

#define IVI_MC_AVG_TEMPLATE(size, suffix, OP) \
void ff_ivi_mc_avg_ ## size ##x## size ## suffix(int16_t *buf, \
                                                 const int16_t *ref_buf, \
                                                 const int16_t *ref_buf2, \
                                                 uint32_t pitch, \
                                                 int mc_type, int mc_type2) \
{ \
    int16_t tmp[size * size]; \
    int i, j; \
\
    ivi_mc_ ## size ##x## size ## _no_delta(tmp, size, ref_buf, pitch, mc_type); \
    ivi_mc_ ## size ##x## size ## _delta(tmp, size, ref_buf2, pitch, mc_type2); \
    for (i = 0; i < size; i++, buf += pitch) { \
        for (j = 0; j < size; j++) {\
            OP(buf[j], tmp[i * size + j] >> 1); \
        } \
    } \
} \

#define OP_PUT(a, b)  (a) = (b)
#define OP_ADD(a, b)  (a) += (b)

IVI_MC_TEMPLATE(8, _no_delta, OP_PUT)
IVI_MC_TEMPLATE(8, _delta,    OP_ADD)
IVI_MC_TEMPLATE(4, _no_delta, OP_PUT)
IVI_MC_TEMPLATE(4, _delta,    OP_ADD)
IVI_MC_AVG_TEMPLATE(8, _no_delta, OP_PUT)
IVI_MC_AVG_TEMPLATE(8, _delta,    OP_ADD)
IVI_MC_AVG_TEMPLATE(4, _no_delta, OP_PUT)
IVI_MC_AVG_TEMPLATE(4, _delta,    OP_ADD)
