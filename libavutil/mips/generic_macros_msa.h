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

#define STORE_UB(vec, pdest) *((v16u8 *)(pdest)) = (vec)
#define STORE_SB(vec, pdest) *((v16i8 *)(pdest)) = (vec)

#define STORE_SH(vec, pdest)       \
{                                  \
    *((v8i16 *) (pdest)) = (vec);  \
}

#define STORE_SW(vec, pdest)       \
{                                  \
    *((v4i32 *) (pdest)) = (vec);  \
}

#if (__mips_isa_rev >= 6)
    #define LOAD_WORD(psrc)                   \
    ( {                                       \
        uint8_t *src_m = (uint8_t *) (psrc);  \
        uint32_t val_m;                       \
                                              \
        __asm__ volatile (                    \
            "lw  %[val_m],  %[src_m]  \n\t"   \
                                              \
            : [val_m] "=r" (val_m)            \
            : [src_m] "m" (*src_m)            \
        );                                    \
                                              \
        val_m;                                \
    } )

    #if (__mips == 64)
        #define LOAD_DWORD(psrc)                  \
        ( {                                       \
            uint8_t *src_m = (uint8_t *) (psrc);  \
            uint64_t val_m = 0;                   \
                                                  \
            __asm__ volatile (                    \
                "ld  %[val_m],  %[src_m]  \n\t"   \
                                                  \
                : [val_m] "=r" (val_m)            \
                : [src_m] "m" (*src_m)            \
            );                                    \
                                                  \
            val_m;                                \
        } )
    #else
        #define LOAD_DWORD(psrc)                                            \
        ( {                                                                 \
            uint8_t *src1_m = (uint8_t *) (psrc);                           \
            uint8_t *src2_m = ((uint8_t *) (psrc)) + 4;                     \
            uint32_t val0_m, val1_m;                                        \
            uint64_t genval_m = 0;                                          \
                                                                            \
            __asm__ volatile (                                              \
                "lw  %[val0_m],  %[src1_m]  \n\t"                           \
                                                                            \
                : [val0_m] "=r" (val0_m)                                    \
                : [src1_m] "m" (*src1_m)                                    \
            );                                                              \
                                                                            \
            __asm__ volatile (                                              \
                "lw  %[val1_m],  %[src2_m]  \n\t"                           \
                                                                            \
                : [val1_m] "=r" (val1_m)                                    \
                : [src2_m] "m" (*src2_m)                                    \
            );                                                              \
                                                                            \
            genval_m = (uint64_t) (val1_m);                                 \
            genval_m = (uint64_t) ((genval_m << 32) & 0xFFFFFFFF00000000);  \
            genval_m = (uint64_t) (genval_m | (uint64_t) val0_m);           \
                                                                            \
            genval_m;                                                       \
        } )
    #endif

    #define STORE_WORD(pdst, val)                 \
    {                                             \
        uint8_t *dst_ptr_m = (uint8_t *) (pdst);  \
        uint32_t val_m = (val);                   \
                                                  \
        __asm__ volatile (                        \
            "sw  %[val_m],  %[dst_ptr_m]  \n\t"   \
                                                  \
            : [dst_ptr_m] "=m" (*dst_ptr_m)       \
            : [val_m] "r" (val_m)                 \
        );                                        \
    }

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
    #define STORE_HWORD(pdst, val)                \
    {                                             \
        uint8_t *dst_ptr_m = (uint8_t *) (pdst);  \
        uint16_t val_m = (val);                   \
                                                  \
        __asm__ volatile (                        \
            "sh  %[val_m],  %[dst_ptr_m]  \n\t"   \
                                                  \
            : [dst_ptr_m] "=m" (*dst_ptr_m)       \
            : [val_m] "r" (val_m)                 \
        );                                        \
    }

#else
    #define LOAD_WORD(psrc)                   \
    ( {                                       \
        uint8_t *src_m = (uint8_t *) (psrc);  \
        uint32_t val_m;                       \
                                              \
        __asm__ volatile (                    \
            "ulw  %[val_m],  %[src_m]  \n\t"  \
                                              \
            : [val_m] "=r" (val_m)            \
            : [src_m] "m" (*src_m)            \
        );                                    \
                                              \
        val_m;                                \
    } )

    #if (__mips == 64)
        #define LOAD_DWORD(psrc)                  \
        ( {                                       \
            uint8_t *src_m = (uint8_t *) (psrc);  \
            uint64_t val_m = 0;                   \
                                                  \
            __asm__ volatile (                    \
                "uld  %[val_m],  %[src_m]  \n\t"  \
                                                  \
                : [val_m] "=r" (val_m)            \
                : [src_m] "m" (*src_m)            \
            );                                    \
                                                  \
            val_m;                                \
        } )
    #else
        #define LOAD_DWORD(psrc)                                            \
        ( {                                                                 \
            uint8_t *src1_m = (uint8_t *) (psrc);                           \
            uint8_t *src2_m = ((uint8_t *) (psrc)) + 4;                     \
            uint32_t val0_m, val1_m;                                        \
            uint64_t genval_m = 0;                                          \
                                                                            \
            __asm__ volatile (                                              \
                "ulw  %[val0_m],  %[src1_m]  \n\t"                          \
                                                                            \
                : [val0_m] "=r" (val0_m)                                    \
                : [src1_m] "m" (*src1_m)                                    \
            );                                                              \
                                                                            \
            __asm__ volatile (                                              \
                "ulw  %[val1_m],  %[src2_m]  \n\t"                          \
                                                                            \
                : [val1_m] "=r" (val1_m)                                    \
                : [src2_m] "m" (*src2_m)                                    \
            );                                                              \
                                                                            \
            genval_m = (uint64_t) (val1_m);                                 \
            genval_m = (uint64_t) ((genval_m << 32) & 0xFFFFFFFF00000000);  \
            genval_m = (uint64_t) (genval_m | (uint64_t) val0_m);           \
                                                                            \
            genval_m;                                                       \
        } )
    #endif

    #define STORE_WORD(pdst, val)                 \
    {                                             \
        uint8_t *dst_ptr_m = (uint8_t *) (pdst);  \
        uint32_t val_m = (val);                   \
                                                  \
        __asm__ volatile (                        \
            "usw  %[val_m],  %[dst_ptr_m]  \n\t"  \
                                                  \
            : [dst_ptr_m] "=m" (*dst_ptr_m)       \
            : [val_m] "r" (val_m)                 \
        );                                        \
    }

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

    #define STORE_HWORD(pdst, val)                \
    {                                             \
        uint8_t *dst_ptr_m = (uint8_t *) (pdst);  \
        uint16_t val_m = (val);                   \
                                                  \
        __asm__ volatile (                        \
            "ush  %[val_m],  %[dst_ptr_m]  \n\t"  \
                                                  \
            : [dst_ptr_m] "=m" (*dst_ptr_m)       \
            : [val_m] "r" (val_m)                 \
        );                                        \
    }

#endif

#define LOAD_4WORDS_WITH_STRIDE(psrc, src_stride,        \
                                src0, src1, src2, src3)  \
{                                                        \
    src0 = LOAD_WORD(psrc + 0 * src_stride);             \
    src1 = LOAD_WORD(psrc + 1 * src_stride);             \
    src2 = LOAD_WORD(psrc + 2 * src_stride);             \
    src3 = LOAD_WORD(psrc + 3 * src_stride);             \
}

#define LOAD_2VECS_SB(psrc, stride,     \
                      val0, val1)       \
{                                       \
    val0 = LOAD_SB(psrc + 0 * stride);  \
    val1 = LOAD_SB(psrc + 1 * stride);  \
}

#define LOAD_4VECS_UB(psrc, stride,            \
                      val0, val1, val2, val3)  \
{                                              \
    val0 = LOAD_UB(psrc + 0 * stride);         \
    val1 = LOAD_UB(psrc + 1 * stride);         \
    val2 = LOAD_UB(psrc + 2 * stride);         \
    val3 = LOAD_UB(psrc + 3 * stride);         \
}

#define LOAD_4VECS_SB(psrc, stride,            \
                      val0, val1, val2, val3)  \
{                                              \
    val0 = LOAD_SB(psrc + 0 * stride);         \
    val1 = LOAD_SB(psrc + 1 * stride);         \
    val2 = LOAD_SB(psrc + 2 * stride);         \
    val3 = LOAD_SB(psrc + 3 * stride);         \
}

#define LOAD_6VECS_SB(psrc, stride,                        \
                      out0, out1, out2, out3, out4, out5)  \
{                                                          \
    LOAD_4VECS_SB((psrc), (stride),                        \
                  (out0), (out1), (out2), (out3));         \
    LOAD_2VECS_SB((psrc + 4 * stride), (stride),           \
                  (out4), (out5));                         \
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

#define LOAD_8VECS_UB(psrc, stride,                 \
                      out0, out1, out2, out3,       \
                      out4, out5, out6, out7)       \
{                                                   \
    LOAD_4VECS_UB((psrc), (stride),                 \
                  (out0), (out1), (out2), (out3));  \
    LOAD_4VECS_UB((psrc + 4 * stride), (stride),    \
                  (out4), (out5), (out6), (out7));  \
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

#define STORE_4VECS_UB(dst_out, pitch,           \
                       in0, in1, in2, in3)       \
{                                                \
    STORE_UB((in0), (dst_out));                  \
    STORE_UB((in1), ((dst_out) + (pitch)));      \
    STORE_UB((in2), ((dst_out) + 2 * (pitch)));  \
    STORE_UB((in3), ((dst_out) + 3 * (pitch)));  \
}

#define STORE_4VECS_SB(dst_out, pitch,           \
                       in0, in1, in2, in3)       \
{                                                \
    STORE_SB((in0), (dst_out));                  \
    STORE_SB((in1), ((dst_out) + (pitch)));      \
    STORE_SB((in2), ((dst_out) + 2 * (pitch)));  \
    STORE_SB((in3), ((dst_out) + 3 * (pitch)));  \
}

#define STORE_2VECS_SH(ptr, stride,       \
                       in0, in1)          \
{                                         \
    STORE_SH(in0, ((ptr) + 0 * stride));  \
    STORE_SH(in1, ((ptr) + 1 * stride));  \
}

#define STORE_4VECS_SH(ptr, stride,         \
                       in0, in1, in2, in3)  \
{                                           \
    STORE_SH(in0, ((ptr) + 0 * stride));    \
    STORE_SH(in1, ((ptr) + 1 * stride));    \
    STORE_SH(in2, ((ptr) + 2 * stride));    \
    STORE_SH(in3, ((ptr) + 3 * stride));    \
}

#define STORE_6VECS_SH(ptr, stride,         \
                       in0, in1, in2, in3,  \
                       in4, in5)            \
{                                           \
    STORE_SH(in0, ((ptr) + 0 * stride));    \
    STORE_SH(in1, ((ptr) + 1 * stride));    \
    STORE_SH(in2, ((ptr) + 2 * stride));    \
    STORE_SH(in3, ((ptr) + 3 * stride));    \
    STORE_SH(in4, ((ptr) + 4 * stride));    \
    STORE_SH(in5, ((ptr) + 5 * stride));    \
}

#define STORE_8VECS_SH(ptr, stride,         \
                       in0, in1, in2, in3,  \
                       in4, in5, in6, in7)  \
{                                           \
    STORE_SH(in0, ((ptr) + 0 * stride));    \
    STORE_SH(in1, ((ptr) + 1 * stride));    \
    STORE_SH(in2, ((ptr) + 2 * stride));    \
    STORE_SH(in3, ((ptr) + 3 * stride));    \
    STORE_SH(in4, ((ptr) + 4 * stride));    \
    STORE_SH(in5, ((ptr) + 5 * stride));    \
    STORE_SH(in6, ((ptr) + 6 * stride));    \
    STORE_SH(in7, ((ptr) + 7 * stride));    \
}

#define CLIP_MIN_TO_MAX_H(in, min, max)                   \
( {                                                       \
    v8i16 out_m;                                          \
                                                          \
    out_m = __msa_max_s_h((v8i16) (min), (v8i16) (in));   \
    out_m = __msa_min_s_h((v8i16) (max), (v8i16) out_m);  \
    out_m;                                                \
} )

#define CLIP_UNSIGNED_CHAR_H(in)                          \
( {                                                       \
    v8i16 max_m = __msa_ldi_h(255);                       \
    v8i16 out_m;                                          \
                                                          \
    out_m = __msa_maxi_s_h((v8i16) (in), 0);              \
    out_m = __msa_min_s_h((v8i16) max_m, (v8i16) out_m);  \
    out_m;                                                \
} )

#define TRANSPOSE4x4_B_UB(in0, in1, in2, in3,                   \
                          out0, out1, out2, out3)               \
{                                                               \
    v16i8 zero_m = { 0 };                                       \
    v16i8 s0_m, s1_m, s2_m, s3_m;                               \
                                                                \
    s0_m = (v16i8) __msa_ilvr_d((v2i64) (in1), (v2i64) (in0));  \
    s1_m = (v16i8) __msa_ilvr_d((v2i64) (in3), (v2i64) (in2));  \
    s2_m = __msa_ilvr_b(s1_m, s0_m);                            \
    s3_m = __msa_ilvl_b(s1_m, s0_m);                            \
                                                                \
    out0 = (v16u8) __msa_ilvr_b(s3_m, s2_m);                    \
    out1 = (v16u8) __msa_sldi_b(zero_m, (v16i8) out0, 4);       \
    out2 = (v16u8) __msa_sldi_b(zero_m, (v16i8) out1, 4);       \
    out3 = (v16u8) __msa_sldi_b(zero_m, (v16i8) out2, 4);       \
}

#define TRANSPOSE8x4_B_UB(in0, in1, in2, in3,                       \
                          in4, in5, in6, in7,                       \
                          out0, out1, out2, out3)                   \
{                                                                   \
    v16i8 tmp0_m, tmp1_m, tmp2_m, tmp3_m;                           \
                                                                    \
    tmp0_m = (v16i8) __msa_ilvev_w((v4i32) (in4), (v4i32) (in0));   \
    tmp1_m = (v16i8) __msa_ilvev_w((v4i32) (in5), (v4i32) (in1));   \
    tmp2_m = __msa_ilvr_b(tmp1_m, tmp0_m);                          \
    tmp0_m = (v16i8) __msa_ilvev_w((v4i32) (in6), (v4i32) (in2));   \
    tmp1_m = (v16i8) __msa_ilvev_w((v4i32) (in7), (v4i32) (in3));   \
                                                                    \
    tmp3_m = __msa_ilvr_b(tmp1_m, tmp0_m);                          \
    tmp0_m = (v16i8) __msa_ilvr_h((v8i16) tmp3_m, (v8i16) tmp2_m);  \
    tmp1_m = (v16i8) __msa_ilvl_h((v8i16) tmp3_m, (v8i16) tmp2_m);  \
                                                                    \
    out0 = (v16u8) __msa_ilvr_w((v4i32) tmp1_m, (v4i32) tmp0_m);    \
    out2 = (v16u8) __msa_ilvl_w((v4i32) tmp1_m, (v4i32) tmp0_m);    \
    out1 = (v16u8) __msa_ilvl_d((v2i64) out2, (v2i64) out0);        \
    out3 = (v16u8) __msa_ilvl_d((v2i64) out0, (v2i64) out2);        \
}

#define TRANSPOSE16x8_B_UB(in0, in1, in2, in3,                       \
                           in4, in5, in6, in7,                       \
                           in8, in9, in10, in11,                     \
                           in12, in13, in14, in15,                   \
                           out0, out1, out2, out3,                   \
                           out4, out5, out6, out7)                   \
{                                                                    \
    v16u8 tmp0_m, tmp1_m, tmp2_m, tmp3_m;                            \
    v16u8 tmp4_m, tmp5_m, tmp6_m, tmp7_m;                            \
                                                                     \
    (out7) = (v16u8) __msa_ilvev_d((v2i64) (in8), (v2i64) (in0));    \
    (out6) = (v16u8) __msa_ilvev_d((v2i64) (in9), (v2i64) (in1));    \
    (out5) = (v16u8) __msa_ilvev_d((v2i64) (in10), (v2i64) (in2));   \
    (out4) = (v16u8) __msa_ilvev_d((v2i64) (in11), (v2i64) (in3));   \
    (out3) = (v16u8) __msa_ilvev_d((v2i64) (in12), (v2i64) (in4));   \
    (out2) = (v16u8) __msa_ilvev_d((v2i64) (in13), (v2i64) (in5));   \
    (out1) = (v16u8) __msa_ilvev_d((v2i64) (in14), (v2i64) (in6));   \
    (out0) = (v16u8) __msa_ilvev_d((v2i64) (in15), (v2i64) (in7));   \
                                                                     \
    tmp0_m = (v16u8) __msa_ilvev_b((v16i8) (out6), (v16i8) (out7));  \
    tmp4_m = (v16u8) __msa_ilvod_b((v16i8) (out6), (v16i8) (out7));  \
    tmp1_m = (v16u8) __msa_ilvev_b((v16i8) (out4), (v16i8) (out5));  \
    tmp5_m = (v16u8) __msa_ilvod_b((v16i8) (out4), (v16i8) (out5));  \
    (out5) = (v16u8) __msa_ilvev_b((v16i8) (out2), (v16i8) (out3));  \
    tmp6_m = (v16u8) __msa_ilvod_b((v16i8) (out2), (v16i8) (out3));  \
    (out7) = (v16u8) __msa_ilvev_b((v16i8) (out0), (v16i8) (out1));  \
    tmp7_m = (v16u8) __msa_ilvod_b((v16i8) (out0), (v16i8) (out1));  \
                                                                     \
    tmp2_m = (v16u8) __msa_ilvev_h((v8i16) tmp1_m, (v8i16) tmp0_m);  \
    tmp3_m = (v16u8) __msa_ilvev_h((v8i16) (out7), (v8i16) (out5));  \
    (out0) = (v16u8) __msa_ilvev_w((v4i32) tmp3_m, (v4i32) tmp2_m);  \
    (out4) = (v16u8) __msa_ilvod_w((v4i32) tmp3_m, (v4i32) tmp2_m);  \
                                                                     \
    tmp2_m = (v16u8) __msa_ilvod_h((v8i16) tmp1_m, (v8i16) tmp0_m);  \
    tmp3_m = (v16u8) __msa_ilvod_h((v8i16) (out7), (v8i16) (out5));  \
    (out2) = (v16u8) __msa_ilvev_w((v4i32) tmp3_m, (v4i32) tmp2_m);  \
    (out6) = (v16u8) __msa_ilvod_w((v4i32) tmp3_m, (v4i32) tmp2_m);  \
                                                                     \
    tmp2_m = (v16u8) __msa_ilvev_h((v8i16) tmp5_m, (v8i16) tmp4_m);  \
    tmp3_m = (v16u8) __msa_ilvev_h((v8i16) tmp7_m, (v8i16) tmp6_m);  \
    (out1) = (v16u8) __msa_ilvev_w((v4i32) tmp3_m, (v4i32) tmp2_m);  \
    (out5) = (v16u8) __msa_ilvod_w((v4i32) tmp3_m, (v4i32) tmp2_m);  \
                                                                     \
    tmp2_m = (v16u8) __msa_ilvod_h((v8i16) tmp5_m, (v8i16) tmp4_m);  \
    tmp2_m = (v16u8) __msa_ilvod_h((v8i16) tmp5_m, (v8i16) tmp4_m);  \
    tmp3_m = (v16u8) __msa_ilvod_h((v8i16) tmp7_m, (v8i16) tmp6_m);  \
    tmp3_m = (v16u8) __msa_ilvod_h((v8i16) tmp7_m, (v8i16) tmp6_m);  \
    (out3) = (v16u8) __msa_ilvev_w((v4i32) tmp3_m, (v4i32) tmp2_m);  \
    (out7) = (v16u8) __msa_ilvod_w((v4i32) tmp3_m, (v4i32) tmp2_m);  \
}

#define ILV_B_LRLR_SB(in0, in1, in2, in3,               \
                      out0, out1, out2, out3)           \
{                                                       \
    out0 = __msa_ilvl_b((v16i8) (in1), (v16i8) (in0));  \
    out1 = __msa_ilvr_b((v16i8) (in1), (v16i8) (in0));  \
    out2 = __msa_ilvl_b((v16i8) (in3), (v16i8) (in2));  \
    out3 = __msa_ilvr_b((v16i8) (in3), (v16i8) (in2));  \
}

#define ILV_B_LRLR_UH(in0, in1, in2, in3,                       \
                      out0, out1, out2, out3)                   \
{                                                               \
    out0 = (v8u16) __msa_ilvl_b((v16i8) (in1), (v16i8) (in0));  \
    out1 = (v8u16) __msa_ilvr_b((v16i8) (in1), (v16i8) (in0));  \
    out2 = (v8u16) __msa_ilvl_b((v16i8) (in3), (v16i8) (in2));  \
    out3 = (v8u16) __msa_ilvr_b((v16i8) (in3), (v16i8) (in2));  \
}

#define ILVR_B_2VECS_UB(in0_r, in1_r, in0_l, in1_l,                 \
                        out0, out1)                                 \
{                                                                   \
    out0 = (v16u8) __msa_ilvr_b((v16i8) (in0_l), (v16i8) (in0_r));  \
    out1 = (v16u8) __msa_ilvr_b((v16i8) (in1_l), (v16i8) (in1_r));  \
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

#define ILVR_B_2VECS_UH(in0_r, in1_r, in0_l, in1_l,                 \
                        out0, out1)                                 \
{                                                                   \
    out0 = (v8u16) __msa_ilvr_b((v16i8) (in0_l), (v16i8) (in0_r));  \
    out1 = (v8u16) __msa_ilvr_b((v16i8) (in1_l), (v16i8) (in1_r));  \
}

#define ILVR_B_2VECS_SH(in0_r, in1_r, in0_l, in1_l,                 \
                        out0, out1)                                 \
{                                                                   \
    out0 = (v8i16) __msa_ilvr_b((v16i8) (in0_l), (v16i8) (in0_r));  \
    out1 = (v8i16) __msa_ilvr_b((v16i8) (in1_l), (v16i8) (in1_r));  \
}

#define ILVR_B_4VECS_UH(in0_r, in1_r, in2_r, in3_r,  \
                        in0_l, in1_l, in2_l, in3_l,  \
                        out0, out1, out2, out3)      \
{                                                    \
    ILVR_B_2VECS_UH(in0_r, in1_r, in0_l, in1_l,      \
                    out0, out1);                     \
    ILVR_B_2VECS_UH(in2_r, in3_r, in2_l, in3_l,      \
                    out2, out3);                     \
}

#define ILVR_B_4VECS_SH(in0_r, in1_r, in2_r, in3_r,  \
                        in0_l, in1_l, in2_l, in3_l,  \
                        out0, out1, out2, out3)      \
{                                                    \
    ILVR_B_2VECS_SH(in0_r, in1_r, in0_l, in1_l,      \
                    out0, out1);                     \
    ILVR_B_2VECS_SH(in2_r, in3_r, in2_l, in3_l,      \
                    out2, out3);                     \
}

#define ILVR_H_2VECS_SH(in0_r, in1_r, in0_l, in1_l,         \
                        out0, out1)                         \
{                                                           \
    out0 = __msa_ilvr_h((v8i16) (in0_l), (v8i16) (in0_r));  \
    out1 = __msa_ilvr_h((v8i16) (in1_l), (v8i16) (in1_r));  \
}

#define ILVR_H_6VECS_SH(in0_r, in1_r, in2_r,     \
                        in3_r, in4_r, in5_r,     \
                        in0_l, in1_l, in2_l,     \
                        in3_l, in4_l, in5_l,     \
                        out0, out1, out2,        \
                        out3, out4, out5)        \
{                                                \
    ILVR_H_2VECS_SH(in0_r, in1_r, in0_l, in1_l,  \
                    out0, out1);                 \
    ILVR_H_2VECS_SH(in2_r, in3_r, in2_l, in3_l,  \
                    out2, out3);                 \
    ILVR_H_2VECS_SH(in4_r, in5_r, in4_l, in5_l,  \
                    out4, out5);                 \
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

#define ILVL_H_2VECS_SH(in0_r, in1_r, in0_l, in1_l,         \
                        out0, out1)                         \
{                                                           \
    out0 = __msa_ilvl_h((v8i16) (in0_l), (v8i16) (in0_r));  \
    out1 = __msa_ilvl_h((v8i16) (in1_l), (v8i16) (in1_r));  \
}

#define ILVL_H_6VECS_SH(in0_r, in1_r, in2_r,     \
                        in3_r, in4_r, in5_r,     \
                        in0_l, in1_l, in2_l,     \
                        in3_l, in4_l, in5_l,     \
                        out0, out1, out2,        \
                        out3, out4, out5)        \
{                                                \
    ILVL_H_2VECS_SH(in0_r, in1_r, in0_l, in1_l,  \
                    out0, out1);                 \
    ILVL_H_2VECS_SH(in2_r, in3_r, in2_l, in3_l,  \
                    out2, out3);                 \
    ILVL_H_2VECS_SH(in4_r, in5_r, in4_l, in5_l,  \
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

#define MAXI_S_H_4VECS_UH(vec0, vec1, vec2, vec3, max_value)     \
{                                                                \
    vec0 = (v8u16) __msa_maxi_s_h((v8i16) (vec0), (max_value));  \
    vec1 = (v8u16) __msa_maxi_s_h((v8i16) (vec1), (max_value));  \
    vec2 = (v8u16) __msa_maxi_s_h((v8i16) (vec2), (max_value));  \
    vec3 = (v8u16) __msa_maxi_s_h((v8i16) (vec3), (max_value));  \
}

#define SAT_U_H_4VECS_UH(vec0, vec1, vec2, vec3, sat_value) \
{                                                           \
    vec0 = __msa_sat_u_h((v8u16) (vec0), (sat_value));      \
    vec1 = __msa_sat_u_h((v8u16) (vec1), (sat_value));      \
    vec2 = __msa_sat_u_h((v8u16) (vec2), (sat_value));      \
    vec3 = __msa_sat_u_h((v8u16) (vec3), (sat_value));      \
}

#define PCKEV_B_4VECS_UB(in0_l, in1_l, in2_l, in3_l,                 \
                         in0_r, in1_r, in2_r, in3_r,                 \
                         out0, out1, out2, out3)                     \
{                                                                    \
    out0 = (v16u8) __msa_pckev_b((v16i8) (in0_l), (v16i8) (in0_r));  \
    out1 = (v16u8) __msa_pckev_b((v16i8) (in1_l), (v16i8) (in1_r));  \
    out2 = (v16u8) __msa_pckev_b((v16i8) (in2_l), (v16i8) (in2_r));  \
    out3 = (v16u8) __msa_pckev_b((v16i8) (in3_l), (v16i8) (in3_r));  \
}

#define PCKEV_B_4VECS_SB(in0_l, in1_l, in2_l, in3_l,         \
                         in0_r, in1_r, in2_r, in3_r,         \
                         out0, out1, out2, out3)             \
{                                                            \
    out0 = __msa_pckev_b((v16i8) (in0_l), (v16i8) (in0_r));  \
    out1 = __msa_pckev_b((v16i8) (in1_l), (v16i8) (in1_r));  \
    out2 = __msa_pckev_b((v16i8) (in2_l), (v16i8) (in2_r));  \
    out3 = __msa_pckev_b((v16i8) (in3_l), (v16i8) (in3_r));  \
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
#define ADDS_S_H_4VECS_UH(in0, in1, in2, in3, in4, in5, in6, in7,  \
                          out0, out1, out2, out3)                  \
{                                                                  \
    out0 = (v8u16) __msa_adds_s_h((v8i16) (in0), (v8i16) (in1));   \
    out1 = (v8u16) __msa_adds_s_h((v8i16) (in2), (v8i16) (in3));   \
    out2 = (v8u16) __msa_adds_s_h((v8i16) (in4), (v8i16) (in5));   \
    out3 = (v8u16) __msa_adds_s_h((v8i16) (in6), (v8i16) (in7));   \
}
#define SRA_4VECS(in0, in1, in2, in3,      \
                  out0, out1, out2, out3,  \
                  shift_right_vec)         \
{                                          \
    out0 = (in0) >> (shift_right_vec);     \
    out1 = (in1) >> (shift_right_vec);     \
    out2 = (in2) >> (shift_right_vec);     \
    out3 = (in3) >> (shift_right_vec);     \
}

#define SRL_H_4VECS_UH(in0, in1, in2, in3,                                 \
                       out0, out1, out2, out3,                             \
                       shift_right_vec)                                    \
{                                                                          \
    out0 = (v8u16) __msa_srl_h((v8i16) (in0), (v8i16) (shift_right_vec));  \
    out1 = (v8u16) __msa_srl_h((v8i16) (in1), (v8i16) (shift_right_vec));  \
    out2 = (v8u16) __msa_srl_h((v8i16) (in2), (v8i16) (shift_right_vec));  \
    out3 = (v8u16) __msa_srl_h((v8i16) (in3), (v8i16) (shift_right_vec));  \
}

#define PCKEV_B_STORE_4_BYTES_4(in1, in2, in3, in4,        \
                                pdst, stride)              \
{                                                          \
    uint32_t out0_m, out1_m, out2_m, out3_m;               \
    v16i8 tmp0_m, tmp1_m;                                  \
    uint8_t *dst_m = (uint8_t *) (pdst);                   \
                                                           \
    tmp0_m = __msa_pckev_b((v16i8) (in2), (v16i8) (in1));  \
    tmp1_m = __msa_pckev_b((v16i8) (in4), (v16i8) (in3));  \
                                                           \
    out0_m = __msa_copy_u_w((v4i32) tmp0_m, 0);            \
    out1_m = __msa_copy_u_w((v4i32) tmp0_m, 2);            \
    out2_m = __msa_copy_u_w((v4i32) tmp1_m, 0);            \
    out3_m = __msa_copy_u_w((v4i32) tmp1_m, 2);            \
                                                           \
    STORE_WORD(dst_m, out0_m);                             \
    dst_m += stride;                                       \
    STORE_WORD(dst_m, out1_m);                             \
    dst_m += stride;                                       \
    STORE_WORD(dst_m, out2_m);                             \
    dst_m += stride;                                       \
    STORE_WORD(dst_m, out3_m);                             \
}

#define PCKEV_B_STORE_8_BYTES_4(in1, in2, in3, in4,        \
                                pdst, stride)              \
{                                                          \
    uint64_t out0_m, out1_m, out2_m, out3_m;               \
    v16i8 tmp0_m, tmp1_m;                                  \
    uint8_t *dst_m = (uint8_t *) (pdst);                   \
                                                           \
    tmp0_m = __msa_pckev_b((v16i8) (in2), (v16i8) (in1));  \
    tmp1_m = __msa_pckev_b((v16i8) (in4), (v16i8) (in3));  \
                                                           \
    out0_m = __msa_copy_u_d((v2i64) tmp0_m, 0);            \
    out1_m = __msa_copy_u_d((v2i64) tmp0_m, 1);            \
    out2_m = __msa_copy_u_d((v2i64) tmp1_m, 0);            \
    out3_m = __msa_copy_u_d((v2i64) tmp1_m, 1);            \
                                                           \
    STORE_DWORD(dst_m, out0_m);                            \
    dst_m += stride;                                       \
    STORE_DWORD(dst_m, out1_m);                            \
    dst_m += stride;                                       \
    STORE_DWORD(dst_m, out2_m);                            \
    dst_m += stride;                                       \
    STORE_DWORD(dst_m, out3_m);                            \
}

#endif  /* AVUTIL_MIPS_GENERIC_MACROS_MSA_H */
