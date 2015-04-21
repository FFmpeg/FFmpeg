/*
 * Copyright (c) 2015 Manojkumar Bhosale (Manojkumar.Bhosale@imgtec.com)
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

#ifndef AVUTIL_MIPS_GENERIC_MACROS_MSA_H
#define AVUTIL_MIPS_GENERIC_MACROS_MSA_H

#include <stdint.h>
#include <msa.h>

#define LOAD_UB(psrc)             \
( {                               \
    v16u8 out_m;                  \
    out_m = *((v16u8 *) (psrc));  \
    out_m;                        \
} )

#define LOAD_SB(psrc)             \
( {                               \
    v16i8 out_m;                  \
    out_m = *((v16i8 *) (psrc));  \
    out_m;                        \
} )

#define LOAD_SH(psrc)             \
( {                               \
    v8i16 out_m;                  \
    out_m = *((v8i16 *) (psrc));  \
    out_m;                        \
} )

#define STORE_SH(vec, pdest)       \
{                                  \
    *((v8i16 *) (pdest)) = (vec);  \
}

#if (__mips_isa_rev >= 6)
    #define STORE_DWORD(pdst, val)                \
    {                                             \
        uint8_t *dst_ptr_m = (uint8_t *) (pdst);  \
        uint64_t val_m = (val);                   \
                                                  \
        __asm__ volatile (                        \
            "sd  %[val_m],  %[dst_ptr_m]  \n\t"   \
                                                  \
            : [dst_ptr_m] "=m" (*dst_ptr_m)       \
            : [val_m] "r" (val_m)                 \
        );                                        \
    }
#else
    #define STORE_DWORD(pdst, val)                                 \
    {                                                              \
        uint8_t *dst1_m = (uint8_t *) (pdst);                      \
        uint8_t *dst2_m = ((uint8_t *) (pdst)) + 4;                \
        uint32_t val0_m, val1_m;                                   \
                                                                   \
        val0_m = (uint32_t) ((val) & 0x00000000FFFFFFFF);          \
        val1_m = (uint32_t) (((val) >> 32) & 0x00000000FFFFFFFF);  \
                                                                   \
        __asm__ volatile (                                         \
            "usw  %[val0_m],  %[dst1_m]  \n\t"                     \
            "usw  %[val1_m],  %[dst2_m]  \n\t"                     \
                                                                   \
            : [dst1_m] "=m" (*dst1_m), [dst2_m] "=m" (*dst2_m)     \
            : [val0_m] "r" (val0_m), [val1_m] "r" (val1_m)         \
        );                                                         \
    }
#endif

#define LOAD_4VECS_SB(psrc, stride,            \
                      val0, val1, val2, val3)  \
{                                              \
    val0 = LOAD_SB(psrc + 0 * stride);         \
    val1 = LOAD_SB(psrc + 1 * stride);         \
    val2 = LOAD_SB(psrc + 2 * stride);         \
    val3 = LOAD_SB(psrc + 3 * stride);         \
}

#define LOAD_7VECS_SB(psrc, stride,            \
                      val0, val1, val2, val3,  \
                      val4, val5, val6)        \
{                                              \
    val0 = LOAD_SB((psrc) + 0 * (stride));     \
    val1 = LOAD_SB((psrc) + 1 * (stride));     \
    val2 = LOAD_SB((psrc) + 2 * (stride));     \
    val3 = LOAD_SB((psrc) + 3 * (stride));     \
    val4 = LOAD_SB((psrc) + 4 * (stride));     \
    val5 = LOAD_SB((psrc) + 5 * (stride));     \
    val6 = LOAD_SB((psrc) + 6 * (stride));     \
}

#define LOAD_8VECS_SB(psrc, stride,                 \
                      out0, out1, out2, out3,       \
                      out4, out5, out6, out7)       \
{                                                   \
    LOAD_4VECS_SB((psrc), (stride),                 \
                  (out0), (out1), (out2), (out3));  \
    LOAD_4VECS_SB((psrc + 4 * stride), (stride),    \
                  (out4), (out5), (out6), (out7));  \
}

#define ILVR_B_2VECS_SB(in0_r, in1_r, in0_l, in1_l,         \
                        out0, out1)                         \
{                                                           \
    out0 = __msa_ilvr_b((v16i8) (in0_l), (v16i8) (in0_r));  \
    out1 = __msa_ilvr_b((v16i8) (in1_l), (v16i8) (in1_r));  \
}

#define ILVR_B_4VECS_SB(in0_r, in1_r, in2_r, in3_r,  \
                        in0_l, in1_l, in2_l, in3_l,  \
                        out0, out1, out2, out3)      \
{                                                    \
    ILVR_B_2VECS_SB(in0_r, in1_r, in0_l, in1_l,      \
                    out0, out1);                     \
    ILVR_B_2VECS_SB(in2_r, in3_r, in2_l, in3_l,      \
                    out2, out3);                     \
}

#define ILVR_B_6VECS_SB(in0_r, in1_r, in2_r,     \
                        in3_r, in4_r, in5_r,     \
                        in0_l, in1_l, in2_l,     \
                        in3_l, in4_l, in5_l,     \
                        out0, out1, out2,        \
                        out3, out4, out5)        \
{                                                \
    ILVR_B_2VECS_SB(in0_r, in1_r, in0_l, in1_l,  \
                    out0, out1);                 \
    ILVR_B_2VECS_SB(in2_r, in3_r, in2_l, in3_l,  \
                    out2, out3);                 \
    ILVR_B_2VECS_SB(in4_r, in5_r, in4_l, in5_l,  \
                    out4, out5);                 \
}

#define ILVR_B_8VECS_SB(in0_r, in1_r, in2_r, in3_r,  \
                        in4_r, in5_r, in6_r, in7_r,  \
                        in0_l, in1_l, in2_l, in3_l,  \
                        in4_l, in5_l, in6_l, in7_l,  \
                        out0, out1, out2, out3,      \
                        out4, out5, out6, out7)      \
{                                                    \
    ILVR_B_2VECS_SB(in0_r, in1_r, in0_l, in1_l,      \
                    out0, out1);                     \
    ILVR_B_2VECS_SB(in2_r, in3_r, in2_l, in3_l,      \
                    out2, out3);                     \
    ILVR_B_2VECS_SB(in4_r, in5_r, in4_l, in5_l,      \
                    out4, out5);                     \
    ILVR_B_2VECS_SB(in6_r, in7_r, in6_l, in7_l,      \
                    out6, out7);                     \
}

#define ILVL_B_2VECS_SB(in0_r, in1_r, in0_l, in1_l,         \
                        out0, out1)                         \
{                                                           \
    out0 = __msa_ilvl_b((v16i8) (in0_l), (v16i8) (in0_r));  \
    out1 = __msa_ilvl_b((v16i8) (in1_l), (v16i8) (in1_r));  \
}

#define ILVL_B_4VECS_SB(in0_r, in1_r, in2_r, in3_r,  \
                        in0_l, in1_l, in2_l, in3_l,  \
                        out0, out1, out2, out3)      \
{                                                    \
    ILVL_B_2VECS_SB(in0_r, in1_r, in0_l, in1_l,      \
                    out0, out1);                     \
    ILVL_B_2VECS_SB(in2_r, in3_r, in2_l, in3_l,      \
                    out2, out3);                     \
}

#define ILVL_B_6VECS_SB(in0_r, in1_r, in2_r,     \
                        in3_r, in4_r, in5_r,     \
                        in0_l, in1_l, in2_l,     \
                        in3_l, in4_l, in5_l,     \
                        out0, out1, out2,        \
                        out3, out4, out5)        \
{                                                \
    ILVL_B_2VECS_SB(in0_r, in1_r, in0_l, in1_l,  \
                    out0, out1);                 \
    ILVL_B_2VECS_SB(in2_r, in3_r, in2_l, in3_l,  \
                    out2, out3);                 \
    ILVL_B_2VECS_SB(in4_r, in5_r, in4_l, in5_l,  \
                    out4, out5);                 \
}

#define ILVR_D_2VECS_SB(out0, in0_l, in0_r,                         \
                        out1, in1_l, in1_r)                         \
{                                                                   \
    out0 = (v16i8) __msa_ilvr_d((v2i64) (in0_l), (v2i64) (in0_r));  \
    out1 = (v16i8) __msa_ilvr_d((v2i64) (in1_l), (v2i64) (in1_r));  \
}

#define ILVR_D_3VECS_SB(out0, in0_l, in0_r,                         \
                        out1, in1_l, in1_r,                         \
                        out2, in2_l, in2_r)                         \
{                                                                   \
    ILVR_D_2VECS_SB(out0, in0_l, in0_r,                             \
                    out1, in1_l, in1_r);                            \
    out2 = (v16i8) __msa_ilvr_d((v2i64) (in2_l), (v2i64) (in2_r));  \
}

#define ILVR_D_4VECS_SB(out0, in0_l, in0_r,  \
                        out1, in1_l, in1_r,  \
                        out2, in2_l, in2_r,  \
                        out3, in3_l, in3_r)  \
{                                            \
    ILVR_D_2VECS_SB(out0, in0_l, in0_r,      \
                    out1, in1_l, in1_r);     \
    ILVR_D_2VECS_SB(out2, in2_l, in2_r,      \
                    out3, in3_l, in3_r);     \
}

#define XORI_B_2VECS_SB(val0, val1,                          \
                        out0, out1, xor_val)                 \
{                                                            \
    out0 = (v16i8) __msa_xori_b((v16u8) (val0), (xor_val));  \
    out1 = (v16i8) __msa_xori_b((v16u8) (val1), (xor_val));  \
}

#define XORI_B_3VECS_SB(val0, val1, val2,                    \
                        out0, out1, out2,                    \
                        xor_val)                             \
{                                                            \
    XORI_B_2VECS_SB(val0, val1,                              \
                    out0, out1, xor_val);                    \
    out2 = (v16i8) __msa_xori_b((v16u8) (val2), (xor_val));  \
}

#define XORI_B_4VECS_SB(val0, val1, val2, val3,  \
                        out0, out1, out2, out3,  \
                        xor_val)                 \
{                                                \
    XORI_B_2VECS_SB(val0, val1,                  \
                    out0, out1, xor_val);        \
    XORI_B_2VECS_SB(val2, val3,                  \
                    out2, out3, xor_val);        \
}

#define XORI_B_5VECS_SB(val0, val1, val2, val3, val4,  \
                        out0, out1, out2, out3, out4,  \
                        xor_val)                       \
{                                                      \
    XORI_B_3VECS_SB(val0, val1, val2,                  \
                    out0, out1, out2, xor_val);        \
    XORI_B_2VECS_SB(val3, val4,                        \
                    out3, out4, xor_val);              \
}

#define XORI_B_7VECS_SB(val0, val1, val2, val3,        \
                        val4, val5, val6,              \
                        out0, out1, out2, out3,        \
                        out4, out5, out6,              \
                        xor_val)                       \
{                                                      \
    XORI_B_4VECS_SB(val0, val1, val2, val3,            \
                    out0, out1, out2, out3, xor_val);  \
    XORI_B_3VECS_SB(val4, val5, val6,                  \
                    out4, out5, out6, xor_val);        \
}

#define XORI_B_8VECS_SB(val0, val1, val2, val3,           \
                        val4, val5, val6, val7,           \
                        out0, out1, out2, out3,           \
                        out4, out5, out6, out7, xor_val)  \
{                                                         \
    XORI_B_4VECS_SB(val0, val1, val2, val3,               \
                    out0, out1, out2, out3, xor_val);     \
    XORI_B_4VECS_SB(val4, val5, val6, val7,               \
                    out4, out5, out6, out7, xor_val);     \
}

#endif  /* AVUTIL_MIPS_GENERIC_MACROS_MSA_H */
