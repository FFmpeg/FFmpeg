/*
 * VC1 AArch64 NEON optimisations
 *
 * Copyright (c) 2022 Ben Avison <bavison@riscosopen.org>
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

#include "libavutil/aarch64/asm.S"

// VC-1 8x8 inverse transform
// On entry:
//   x0 -> array of 16-bit inverse transform coefficients, in column-major order
// On exit:
//   array at x0 updated to hold transformed block; also now held in row-major order
function ff_vc1_inv_trans_8x8_neon, export=1
        ld1             {v1.16b, v2.16b}, [x0], #32
        ld1             {v3.16b, v4.16b}, [x0], #32
        ld1             {v5.16b, v6.16b}, [x0], #32
        shl             v1.8h, v1.8h, #2        //         8/2 * src[0]
        sub             x1, x0, #3*32
        ld1             {v16.16b, v17.16b}, [x0]
        shl             v7.8h, v2.8h, #4        //          16 * src[8]
        shl             v18.8h, v2.8h, #2       //           4 * src[8]
        shl             v19.8h, v4.8h, #4       //                        16 * src[24]
        ldr             d0, .Lcoeffs_it8
        shl             v5.8h, v5.8h, #2        //                                      8/2 * src[32]
        shl             v20.8h, v6.8h, #4       //                                       16 * src[40]
        shl             v21.8h, v6.8h, #2       //                                        4 * src[40]
        shl             v22.8h, v17.8h, #4      //                                                      16 * src[56]
        ssra            v20.8h, v19.8h, #2      //                         4 * src[24] + 16 * src[40]
        mul             v23.8h, v3.8h, v0.h[0]  //                       6/2 * src[16]
        sub             v19.8h, v19.8h, v21.8h  //                        16 * src[24] -  4 * src[40]
        ssra            v7.8h, v22.8h, #2       //          16 * src[8]                               +  4 * src[56]
        sub             v18.8h, v22.8h, v18.8h  //        -  4 * src[8]                               + 16 * src[56]
        shl             v3.8h, v3.8h, #3        //                      16/2 * src[16]
        mls             v20.8h, v2.8h, v0.h[2]  //        - 15 * src[8] +  4 * src[24] + 16 * src[40]
        ssra            v1.8h, v1.8h, #1        //        12/2 * src[0]
        ssra            v5.8h, v5.8h, #1        //                                     12/2 * src[32]
        mla             v7.8h, v4.8h, v0.h[2]   //          16 * src[8] + 15 * src[24]                +  4 * src[56]
        shl             v21.8h, v16.8h, #3      //                                                    16/2 * src[48]
        mls             v19.8h, v2.8h, v0.h[1]  //        -  9 * src[8] + 16 * src[24] -  4 * src[40]
        sub             v2.8h, v23.8h, v21.8h   // t4/2 =                6/2 * src[16]              - 16/2 * src[48]
        mla             v18.8h, v4.8h, v0.h[1]  //        -  4 * src[8] +  9 * src[24]                + 16 * src[56]
        add             v4.8h, v1.8h, v5.8h     // t1/2 = 12/2 * src[0]              + 12/2 * src[32]
        sub             v1.8h, v1.8h, v5.8h     // t2/2 = 12/2 * src[0]              - 12/2 * src[32]
        mla             v3.8h, v16.8h, v0.h[0]  // t3/2 =               16/2 * src[16]              +  6/2 * src[48]
        mla             v7.8h, v6.8h, v0.h[1]   //  t1  =   16 * src[8] + 15 * src[24] +  9 * src[40] +  4 * src[56]
        add             v5.8h, v1.8h, v2.8h     // t6/2 = t2/2 + t4/2
        sub             v16.8h, v1.8h, v2.8h    // t7/2 = t2/2 - t4/2
        mla             v20.8h, v17.8h, v0.h[1] // -t2  = - 15 * src[8] +  4 * src[24] + 16 * src[40] +  9 * src[56]
        add             v21.8h, v1.8h, v2.8h    // t6/2 = t2/2 + t4/2
        add             v22.8h, v4.8h, v3.8h    // t5/2 = t1/2 + t3/2
        mls             v19.8h, v17.8h, v0.h[2] // -t3  = -  9 * src[8] + 16 * src[24] -  4 * src[40] - 15 * src[56]
        sub             v17.8h, v4.8h, v3.8h    // t8/2 = t1/2 - t3/2
        add             v23.8h, v4.8h, v3.8h    // t5/2 = t1/2 + t3/2
        mls             v18.8h, v6.8h, v0.h[2]  // -t4  = -  4 * src[8] +  9 * src[24] - 15 * src[40] + 16 * src[56]
        sub             v1.8h, v1.8h, v2.8h     // t7/2 = t2/2 - t4/2
        sub             v2.8h, v4.8h, v3.8h     // t8/2 = t1/2 - t3/2
        neg             v3.8h, v7.8h            // -t1
        neg             v4.8h, v20.8h           // +t2
        neg             v6.8h, v19.8h           // +t3
        ssra            v22.8h, v7.8h, #1       // (t5 + t1) >> 1
        ssra            v1.8h, v19.8h, #1       // (t7 - t3) >> 1
        neg             v7.8h, v18.8h           // +t4
        ssra            v5.8h, v4.8h, #1        // (t6 + t2) >> 1
        ssra            v16.8h, v6.8h, #1       // (t7 + t3) >> 1
        ssra            v2.8h, v18.8h, #1       // (t8 - t4) >> 1
        ssra            v17.8h, v7.8h, #1       // (t8 + t4) >> 1
        ssra            v21.8h, v20.8h, #1      // (t6 - t2) >> 1
        ssra            v23.8h, v3.8h, #1       // (t5 - t1) >> 1
        srshr           v3.8h, v22.8h, #2       // (t5 + t1 + 4) >> 3
        srshr           v4.8h, v5.8h, #2        // (t6 + t2 + 4) >> 3
        srshr           v5.8h, v16.8h, #2       // (t7 + t3 + 4) >> 3
        srshr           v6.8h, v17.8h, #2       // (t8 + t4 + 4) >> 3
        srshr           v2.8h, v2.8h, #2        // (t8 - t4 + 4) >> 3
        srshr           v1.8h, v1.8h, #2        // (t7 - t3 + 4) >> 3
        srshr           v7.8h, v21.8h, #2       // (t6 - t2 + 4) >> 3
        srshr           v16.8h, v23.8h, #2      // (t5 - t1 + 4) >> 3
        trn2            v17.8h, v3.8h, v4.8h
        trn2            v18.8h, v5.8h, v6.8h
        trn2            v19.8h, v2.8h, v1.8h
        trn2            v20.8h, v7.8h, v16.8h
        trn1            v21.4s, v17.4s, v18.4s
        trn2            v17.4s, v17.4s, v18.4s
        trn1            v18.4s, v19.4s, v20.4s
        trn2            v19.4s, v19.4s, v20.4s
        trn1            v3.8h, v3.8h, v4.8h
        trn2            v4.2d, v21.2d, v18.2d
        trn1            v20.2d, v17.2d, v19.2d
        trn1            v5.8h, v5.8h, v6.8h
        trn1            v1.8h, v2.8h, v1.8h
        trn1            v2.8h, v7.8h, v16.8h
        trn1            v6.2d, v21.2d, v18.2d
        trn2            v7.2d, v17.2d, v19.2d
        shl             v16.8h, v20.8h, #4      //                        16 * src[24]
        shl             v17.8h, v4.8h, #4       //                                       16 * src[40]
        trn1            v18.4s, v3.4s, v5.4s
        trn1            v19.4s, v1.4s, v2.4s
        shl             v21.8h, v7.8h, #4       //                                                      16 * src[56]
        shl             v22.8h, v6.8h, #2       //           4 * src[8]
        shl             v23.8h, v4.8h, #2       //                                        4 * src[40]
        trn2            v3.4s, v3.4s, v5.4s
        trn2            v1.4s, v1.4s, v2.4s
        shl             v2.8h, v6.8h, #4        //          16 * src[8]
        sub             v5.8h, v16.8h, v23.8h   //                        16 * src[24] -  4 * src[40]
        ssra            v17.8h, v16.8h, #2      //                         4 * src[24] + 16 * src[40]
        sub             v16.8h, v21.8h, v22.8h  //        -  4 * src[8]                               + 16 * src[56]
        trn1            v22.2d, v18.2d, v19.2d
        trn2            v18.2d, v18.2d, v19.2d
        trn1            v19.2d, v3.2d, v1.2d
        ssra            v2.8h, v21.8h, #2       //          16 * src[8]                               +  4 * src[56]
        mls             v17.8h, v6.8h, v0.h[2]  //        - 15 * src[8] +  4 * src[24] + 16 * src[40]
        shl             v21.8h, v22.8h, #2      //         8/2 * src[0]
        shl             v18.8h, v18.8h, #2      //                                      8/2 * src[32]
        mls             v5.8h, v6.8h, v0.h[1]   //        -  9 * src[8] + 16 * src[24] -  4 * src[40]
        shl             v6.8h, v19.8h, #3       //                      16/2 * src[16]
        trn2            v1.2d, v3.2d, v1.2d
        mla             v16.8h, v20.8h, v0.h[1] //        -  4 * src[8] +  9 * src[24]                + 16 * src[56]
        ssra            v21.8h, v21.8h, #1      //        12/2 * src[0]
        ssra            v18.8h, v18.8h, #1      //                                     12/2 * src[32]
        mul             v3.8h, v19.8h, v0.h[0]  //                       6/2 * src[16]
        shl             v19.8h, v1.8h, #3       //                                                    16/2 * src[48]
        mla             v2.8h, v20.8h, v0.h[2]  //          16 * src[8] + 15 * src[24]                +  4 * src[56]
        add             v20.8h, v21.8h, v18.8h  // t1/2 = 12/2 * src[0]              + 12/2 * src[32]
        mla             v6.8h, v1.8h, v0.h[0]   // t3/2 =               16/2 * src[16]              +  6/2 * src[48]
        sub             v1.8h, v21.8h, v18.8h   // t2/2 = 12/2 * src[0]              - 12/2 * src[32]
        sub             v3.8h, v3.8h, v19.8h    // t4/2 =                6/2 * src[16]              - 16/2 * src[48]
        mla             v17.8h, v7.8h, v0.h[1]  // -t2  = - 15 * src[8] +  4 * src[24] + 16 * src[40] +  9 * src[56]
        mls             v5.8h, v7.8h, v0.h[2]   // -t3  = -  9 * src[8] + 16 * src[24] -  4 * src[40] - 15 * src[56]
        add             v7.8h, v1.8h, v3.8h     // t6/2 = t2/2 + t4/2
        add             v18.8h, v20.8h, v6.8h   // t5/2 = t1/2 + t3/2
        mls             v16.8h, v4.8h, v0.h[2]  // -t4  = -  4 * src[8] +  9 * src[24] - 15 * src[40] + 16 * src[56]
        sub             v19.8h, v1.8h, v3.8h    // t7/2 = t2/2 - t4/2
        neg             v21.8h, v17.8h          // +t2
        mla             v2.8h, v4.8h, v0.h[1]   //  t1  =   16 * src[8] + 15 * src[24] +  9 * src[40] +  4 * src[56]
        sub             v0.8h, v20.8h, v6.8h    // t8/2 = t1/2 - t3/2
        neg             v4.8h, v5.8h            // +t3
        sub             v22.8h, v1.8h, v3.8h    // t7/2 = t2/2 - t4/2
        sub             v23.8h, v20.8h, v6.8h   // t8/2 = t1/2 - t3/2
        neg             v24.8h, v16.8h          // +t4
        add             v6.8h, v20.8h, v6.8h    // t5/2 = t1/2 + t3/2
        add             v1.8h, v1.8h, v3.8h     // t6/2 = t2/2 + t4/2
        ssra            v7.8h, v21.8h, #1       // (t6 + t2) >> 1
        neg             v3.8h, v2.8h            // -t1
        ssra            v18.8h, v2.8h, #1       // (t5 + t1) >> 1
        ssra            v19.8h, v4.8h, #1       // (t7 + t3) >> 1
        ssra            v0.8h, v24.8h, #1       // (t8 + t4) >> 1
        srsra           v23.8h, v16.8h, #1      // (t8 - t4 + 1) >> 1
        srsra           v22.8h, v5.8h, #1       // (t7 - t3 + 1) >> 1
        srsra           v1.8h, v17.8h, #1       // (t6 - t2 + 1) >> 1
        srsra           v6.8h, v3.8h, #1        // (t5 - t1 + 1) >> 1
        srshr           v2.8h, v18.8h, #6       // (t5 + t1 + 64) >> 7
        srshr           v3.8h, v7.8h, #6        // (t6 + t2 + 64) >> 7
        srshr           v4.8h, v19.8h, #6       // (t7 + t3 + 64) >> 7
        srshr           v5.8h, v0.8h, #6        // (t8 + t4 + 64) >> 7
        srshr           v16.8h, v23.8h, #6      // (t8 - t4 + 65) >> 7
        srshr           v17.8h, v22.8h, #6      // (t7 - t3 + 65) >> 7
        st1             {v2.16b, v3.16b}, [x1], #32
        srshr           v0.8h, v1.8h, #6        // (t6 - t2 + 65) >> 7
        srshr           v1.8h, v6.8h, #6        // (t5 - t1 + 65) >> 7
        st1             {v4.16b, v5.16b}, [x1], #32
        st1             {v16.16b, v17.16b}, [x1], #32
        st1             {v0.16b, v1.16b}, [x1]
        ret
endfunc

// VC-1 8x4 inverse transform
// On entry:
//   x0 -> array of 8-bit samples, in row-major order
//   x1 = row stride for 8-bit sample array
//   x2 -> array of 16-bit inverse transform coefficients, in row-major order
// On exit:
//   array at x0 updated by saturated addition of (narrowed) transformed block
function ff_vc1_inv_trans_8x4_neon, export=1
        ld1             {v1.8b, v2.8b, v3.8b, v4.8b}, [x2], #32
        mov             x3, x0
        ld1             {v16.8b, v17.8b, v18.8b, v19.8b}, [x2]
        ldr             q0, .Lcoeffs_it8        // includes 4-point coefficients in upper half of vector
        ld1             {v5.8b}, [x0], x1
        trn2            v6.4h, v1.4h, v3.4h
        trn2            v7.4h, v2.4h, v4.4h
        trn1            v1.4h, v1.4h, v3.4h
        trn1            v2.4h, v2.4h, v4.4h
        trn2            v3.4h, v16.4h, v18.4h
        trn2            v4.4h, v17.4h, v19.4h
        trn1            v16.4h, v16.4h, v18.4h
        trn1            v17.4h, v17.4h, v19.4h
        ld1             {v18.8b}, [x0], x1
        trn1            v19.2s, v6.2s, v3.2s
        trn2            v3.2s, v6.2s, v3.2s
        trn1            v6.2s, v7.2s, v4.2s
        trn2            v4.2s, v7.2s, v4.2s
        trn1            v7.2s, v1.2s, v16.2s
        trn1            v20.2s, v2.2s, v17.2s
        shl             v21.4h, v19.4h, #4      //          16 * src[1]
        trn2            v1.2s, v1.2s, v16.2s
        shl             v16.4h, v3.4h, #4       //                        16 * src[3]
        trn2            v2.2s, v2.2s, v17.2s
        shl             v17.4h, v6.4h, #4       //                                      16 * src[5]
        ld1             {v22.8b}, [x0], x1
        shl             v23.4h, v4.4h, #4       //                                                    16 * src[7]
        mul             v24.4h, v1.4h, v0.h[0]  //                       6/2 * src[2]
        ld1             {v25.8b}, [x0]
        shl             v26.4h, v19.4h, #2      //           4 * src[1]
        shl             v27.4h, v6.4h, #2       //                                       4 * src[5]
        ssra            v21.4h, v23.4h, #2      //          16 * src[1]                             +  4 * src[7]
        ssra            v17.4h, v16.4h, #2      //                         4 * src[3] + 16 * src[5]
        sub             v23.4h, v23.4h, v26.4h  //        -  4 * src[1]                             + 16 * src[7]
        sub             v16.4h, v16.4h, v27.4h  //                        16 * src[3] -  4 * src[5]
        shl             v7.4h, v7.4h, #2        //         8/2 * src[0]
        shl             v20.4h, v20.4h, #2      //                                     8/2 * src[4]
        mla             v21.4h, v3.4h, v0.h[2]  //          16 * src[1] + 15 * src[3]               +  4 * src[7]
        shl             v1.4h, v1.4h, #3        //                      16/2 * src[2]
        mls             v17.4h, v19.4h, v0.h[2] //        - 15 * src[1] +  4 * src[3] + 16 * src[5]
        ssra            v7.4h, v7.4h, #1        //        12/2 * src[0]
        mls             v16.4h, v19.4h, v0.h[1] //        -  9 * src[1] + 16 * src[3] -  4 * src[5]
        ssra            v20.4h, v20.4h, #1      //                                    12/2 * src[4]
        mla             v23.4h, v3.4h, v0.h[1]  //        -  4 * src[1] +  9 * src[3]               + 16 * src[7]
        shl             v3.4h, v2.4h, #3        //                                                  16/2 * src[6]
        mla             v1.4h, v2.4h, v0.h[0]   // t3/2 =               16/2 * src[2]             +  6/2 * src[6]
        mla             v21.4h, v6.4h, v0.h[1]  //  t1  =   16 * src[1] + 15 * src[3] +  9 * src[5] +  4 * src[7]
        mla             v17.4h, v4.4h, v0.h[1]  // -t2  = - 15 * src[1] +  4 * src[3] + 16 * src[5] +  9 * src[7]
        sub             v2.4h, v24.4h, v3.4h    // t4/2 =                6/2 * src[2]             - 16/2 * src[6]
        mls             v16.4h, v4.4h, v0.h[2]  // -t3  = -  9 * src[1] + 16 * src[3] -  4 * src[5] - 15 * src[7]
        add             v3.4h, v7.4h, v20.4h    // t1/2 = 12/2 * src[0]             + 12/2 * src[4]
        mls             v23.4h, v6.4h, v0.h[2]  // -t4  = -  4 * src[1] +  9 * src[3] - 15 * src[5] + 16 * src[7]
        sub             v4.4h, v7.4h, v20.4h    // t2/2 = 12/2 * src[0]             - 12/2 * src[4]
        neg             v6.4h, v21.4h           // -t1
        add             v7.4h, v3.4h, v1.4h     // t5/2 = t1/2 + t3/2
        sub             v19.4h, v3.4h, v1.4h    // t8/2 = t1/2 - t3/2
        add             v20.4h, v4.4h, v2.4h    // t6/2 = t2/2 + t4/2
        sub             v24.4h, v4.4h, v2.4h    // t7/2 = t2/2 - t4/2
        add             v26.4h, v3.4h, v1.4h    // t5/2 = t1/2 + t3/2
        add             v27.4h, v4.4h, v2.4h    // t6/2 = t2/2 + t4/2
        sub             v2.4h, v4.4h, v2.4h     // t7/2 = t2/2 - t4/2
        sub             v1.4h, v3.4h, v1.4h     // t8/2 = t1/2 - t3/2
        neg             v3.4h, v17.4h           // +t2
        neg             v4.4h, v16.4h           // +t3
        neg             v28.4h, v23.4h          // +t4
        ssra            v7.4h, v21.4h, #1       // (t5 + t1) >> 1
        ssra            v1.4h, v23.4h, #1       // (t8 - t4) >> 1
        ssra            v20.4h, v3.4h, #1       // (t6 + t2) >> 1
        ssra            v24.4h, v4.4h, #1       // (t7 + t3) >> 1
        ssra            v19.4h, v28.4h, #1      // (t8 + t4) >> 1
        ssra            v2.4h, v16.4h, #1       // (t7 - t3) >> 1
        ssra            v27.4h, v17.4h, #1      // (t6 - t2) >> 1
        ssra            v26.4h, v6.4h, #1       // (t5 - t1) >> 1
        trn1            v1.2d, v7.2d, v1.2d
        trn1            v2.2d, v20.2d, v2.2d
        trn1            v3.2d, v24.2d, v27.2d
        trn1            v4.2d, v19.2d, v26.2d
        srshr           v1.8h, v1.8h, #2        // (t5 + t1 + 4) >> 3, (t8 - t4 + 4) >> 3
        srshr           v2.8h, v2.8h, #2        // (t6 + t2 + 4) >> 3, (t7 - t3 + 4) >> 3
        srshr           v3.8h, v3.8h, #2        // (t7 + t3 + 4) >> 3, (t6 - t2 + 4) >> 3
        srshr           v4.8h, v4.8h, #2        // (t8 + t4 + 4) >> 3, (t5 - t1 + 4) >> 3
        trn2            v6.8h, v1.8h, v2.8h
        trn1            v1.8h, v1.8h, v2.8h
        trn2            v2.8h, v3.8h, v4.8h
        trn1            v3.8h, v3.8h, v4.8h
        trn2            v4.4s, v6.4s, v2.4s
        trn1            v7.4s, v1.4s, v3.4s
        trn2            v1.4s, v1.4s, v3.4s
        mul             v3.8h, v4.8h, v0.h[5]   //                                                           22/2 * src[24]
        trn1            v2.4s, v6.4s, v2.4s
        mul             v4.8h, v4.8h, v0.h[4]   //                                                           10/2 * src[24]
        mul             v6.8h, v7.8h, v0.h[6]   //            17 * src[0]
        mul             v1.8h, v1.8h, v0.h[6]   //                                            17 * src[16]
        mls             v3.8h, v2.8h, v0.h[4]   //  t4/2 =                - 10/2 * src[8]                  + 22/2 * src[24]
        mla             v4.8h, v2.8h, v0.h[5]   //  t3/2 =                  22/2 * src[8]                  + 10/2 * src[24]
        add             v0.8h, v6.8h, v1.8h     //   t1  =    17 * src[0]                 +   17 * src[16]
        sub             v1.8h, v6.8h, v1.8h     //   t2  =    17 * src[0]                 -   17 * src[16]
        neg             v2.8h, v3.8h            // -t4/2
        neg             v6.8h, v4.8h            // -t3/2
        ssra            v4.8h, v0.8h, #1        // (t1 + t3) >> 1
        ssra            v2.8h, v1.8h, #1        // (t2 - t4) >> 1
        ssra            v3.8h, v1.8h, #1        // (t2 + t4) >> 1
        ssra            v6.8h, v0.8h, #1        // (t1 - t3) >> 1
        srshr           v0.8h, v4.8h, #6        // (t1 + t3 + 64) >> 7
        srshr           v1.8h, v2.8h, #6        // (t2 - t4 + 64) >> 7
        srshr           v2.8h, v3.8h, #6        // (t2 + t4 + 64) >> 7
        srshr           v3.8h, v6.8h, #6        // (t1 - t3 + 64) >> 7
        uaddw           v0.8h, v0.8h, v5.8b
        uaddw           v1.8h, v1.8h, v18.8b
        uaddw           v2.8h, v2.8h, v22.8b
        uaddw           v3.8h, v3.8h, v25.8b
        sqxtun          v0.8b, v0.8h
        sqxtun          v1.8b, v1.8h
        sqxtun          v2.8b, v2.8h
        sqxtun          v3.8b, v3.8h
        st1             {v0.8b}, [x3], x1
        st1             {v1.8b}, [x3], x1
        st1             {v2.8b}, [x3], x1
        st1             {v3.8b}, [x3]
        ret
endfunc

// VC-1 4x8 inverse transform
// On entry:
//   x0 -> array of 8-bit samples, in row-major order
//   x1 = row stride for 8-bit sample array
//   x2 -> array of 16-bit inverse transform coefficients, in row-major order (row stride is 8 coefficients)
// On exit:
//   array at x0 updated by saturated addition of (narrowed) transformed block
function ff_vc1_inv_trans_4x8_neon, export=1
        mov             x3, #16
        ldr             q0, .Lcoeffs_it8        // includes 4-point coefficients in upper half of vector
        mov             x4, x0
        ld1             {v1.d}[0], [x2], x3     // 00 01 02 03
        ld1             {v2.d}[0], [x2], x3     // 10 11 12 13
        ld1             {v3.d}[0], [x2], x3     // 20 21 22 23
        ld1             {v4.d}[0], [x2], x3     // 30 31 32 33
        ld1             {v1.d}[1], [x2], x3     // 40 41 42 43
        ld1             {v2.d}[1], [x2], x3     // 50 51 52 53
        ld1             {v3.d}[1], [x2], x3     // 60 61 62 63
        ld1             {v4.d}[1], [x2]         // 70 71 72 73
        ld1             {v5.s}[0], [x0], x1
        ld1             {v6.s}[0], [x0], x1
        ld1             {v7.s}[0], [x0], x1
        trn2            v16.8h, v1.8h, v2.8h    // 01 11 03 13 41 51 43 53
        trn1            v1.8h, v1.8h, v2.8h     // 00 10 02 12 40 50 42 52
        trn2            v2.8h, v3.8h, v4.8h     // 21 31 23 33 61 71 63 73
        trn1            v3.8h, v3.8h, v4.8h     // 20 30 22 32 60 70 62 72
        ld1             {v4.s}[0], [x0], x1
        trn2            v17.4s, v16.4s, v2.4s   // 03 13 23 33 43 53 63 73
        trn1            v18.4s, v1.4s, v3.4s    // 00 10 20 30 40 50 60 70
        trn1            v2.4s, v16.4s, v2.4s    // 01 11 21 31 41 51 61 71
        mul             v16.8h, v17.8h, v0.h[4] //                                                          10/2 * src[3]
        ld1             {v5.s}[1], [x0], x1
        mul             v17.8h, v17.8h, v0.h[5] //                                                          22/2 * src[3]
        ld1             {v6.s}[1], [x0], x1
        trn2            v1.4s, v1.4s, v3.4s     // 02 12 22 32 42 52 62 72
        mul             v3.8h, v18.8h, v0.h[6]  //            17 * src[0]
        ld1             {v7.s}[1], [x0], x1
        mul             v1.8h, v1.8h, v0.h[6]   //                                            17 * src[2]
        ld1             {v4.s}[1], [x0]
        mla             v16.8h, v2.8h, v0.h[5]  //  t3/2 =                  22/2 * src[1]                 + 10/2 * src[3]
        mls             v17.8h, v2.8h, v0.h[4]  //  t4/2 =                - 10/2 * src[1]                 + 22/2 * src[3]
        add             v2.8h, v3.8h, v1.8h     //   t1  =    17 * src[0]                 +   17 * src[2]
        sub             v1.8h, v3.8h, v1.8h     //   t2  =    17 * src[0]                 -   17 * src[2]
        neg             v3.8h, v16.8h           // -t3/2
        ssra            v16.8h, v2.8h, #1       // (t1 + t3) >> 1
        neg             v18.8h, v17.8h          // -t4/2
        ssra            v17.8h, v1.8h, #1       // (t2 + t4) >> 1
        ssra            v3.8h, v2.8h, #1        // (t1 - t3) >> 1
        ssra            v18.8h, v1.8h, #1       // (t2 - t4) >> 1
        srshr           v1.8h, v16.8h, #2       // (t1 + t3 + 64) >> 3
        srshr           v2.8h, v17.8h, #2       // (t2 + t4 + 64) >> 3
        srshr           v3.8h, v3.8h, #2        // (t1 - t3 + 64) >> 3
        srshr           v16.8h, v18.8h, #2      // (t2 - t4 + 64) >> 3
        trn2            v17.8h, v2.8h, v3.8h    // 12 13 32 33 52 53 72 73
        trn2            v18.8h, v1.8h, v16.8h   // 10 11 30 31 50 51 70 71
        trn1            v1.8h, v1.8h, v16.8h    // 00 01 20 21 40 41 60 61
        trn1            v2.8h, v2.8h, v3.8h     // 02 03 22 23 42 43 62 63
        trn1            v3.4s, v18.4s, v17.4s   // 10 11 12 13 50 51 52 53
        trn2            v16.4s, v18.4s, v17.4s  // 30 31 32 33 70 71 72 73
        trn1            v17.4s, v1.4s, v2.4s    // 00 01 02 03 40 41 42 43
        mov             d18, v3.d[1]            // 50 51 52 53
        shl             v19.4h, v3.4h, #4       //          16 * src[8]
        mov             d20, v16.d[1]           // 70 71 72 73
        shl             v21.4h, v16.4h, #4      //                        16 * src[24]
        mov             d22, v17.d[1]           // 40 41 42 43
        shl             v23.4h, v3.4h, #2       //           4 * src[8]
        shl             v24.4h, v18.4h, #4      //                                       16 * src[40]
        shl             v25.4h, v20.4h, #4      //                                                      16 * src[56]
        shl             v26.4h, v18.4h, #2      //                                        4 * src[40]
        trn2            v1.4s, v1.4s, v2.4s     // 20 21 22 23 60 61 62 63
        ssra            v24.4h, v21.4h, #2      //                         4 * src[24] + 16 * src[40]
        sub             v2.4h, v25.4h, v23.4h   //        -  4 * src[8]                               + 16 * src[56]
        shl             v17.4h, v17.4h, #2      //         8/2 * src[0]
        sub             v21.4h, v21.4h, v26.4h  //                        16 * src[24] -  4 * src[40]
        shl             v22.4h, v22.4h, #2      //                                      8/2 * src[32]
        mov             d23, v1.d[1]            // 60 61 62 63
        ssra            v19.4h, v25.4h, #2      //          16 * src[8]                               +  4 * src[56]
        mul             v25.4h, v1.4h, v0.h[0]  //                       6/2 * src[16]
        shl             v1.4h, v1.4h, #3        //                      16/2 * src[16]
        mls             v24.4h, v3.4h, v0.h[2]  //        - 15 * src[8] +  4 * src[24] + 16 * src[40]
        ssra            v17.4h, v17.4h, #1      //        12/2 * src[0]
        mls             v21.4h, v3.4h, v0.h[1]  //        -  9 * src[8] + 16 * src[24] -  4 * src[40]
        ssra            v22.4h, v22.4h, #1      //                                     12/2 * src[32]
        mla             v2.4h, v16.4h, v0.h[1]  //        -  4 * src[8] +  9 * src[24]                + 16 * src[56]
        shl             v3.4h, v23.4h, #3       //                                                    16/2 * src[48]
        mla             v19.4h, v16.4h, v0.h[2] //          16 * src[8] + 15 * src[24]                +  4 * src[56]
        mla             v1.4h, v23.4h, v0.h[0]  // t3/2 =               16/2 * src[16]              +  6/2 * src[48]
        mla             v24.4h, v20.4h, v0.h[1] // -t2  = - 15 * src[8] +  4 * src[24] + 16 * src[40] +  9 * src[56]
        add             v16.4h, v17.4h, v22.4h  // t1/2 = 12/2 * src[0]              + 12/2 * src[32]
        sub             v3.4h, v25.4h, v3.4h    // t4/2 =                6/2 * src[16]              - 16/2 * src[48]
        sub             v17.4h, v17.4h, v22.4h  // t2/2 = 12/2 * src[0]              - 12/2 * src[32]
        mls             v21.4h, v20.4h, v0.h[2] // -t3  = -  9 * src[8] + 16 * src[24] -  4 * src[40] - 15 * src[56]
        mla             v19.4h, v18.4h, v0.h[1] //  t1  =   16 * src[8] + 15 * src[24] +  9 * src[40] +  4 * src[56]
        add             v20.4h, v16.4h, v1.4h   // t5/2 = t1/2 + t3/2
        mls             v2.4h, v18.4h, v0.h[2]  // -t4  = -  4 * src[8] +  9 * src[24] - 15 * src[40] + 16 * src[56]
        sub             v0.4h, v16.4h, v1.4h    // t8/2 = t1/2 - t3/2
        add             v18.4h, v17.4h, v3.4h   // t6/2 = t2/2 + t4/2
        sub             v22.4h, v17.4h, v3.4h   // t7/2 = t2/2 - t4/2
        neg             v23.4h, v24.4h          // +t2
        sub             v25.4h, v17.4h, v3.4h   // t7/2 = t2/2 - t4/2
        add             v3.4h, v17.4h, v3.4h    // t6/2 = t2/2 + t4/2
        neg             v17.4h, v21.4h          // +t3
        sub             v26.4h, v16.4h, v1.4h   // t8/2 = t1/2 - t3/2
        add             v1.4h, v16.4h, v1.4h    // t5/2 = t1/2 + t3/2
        neg             v16.4h, v19.4h          // -t1
        neg             v27.4h, v2.4h           // +t4
        ssra            v20.4h, v19.4h, #1      // (t5 + t1) >> 1
        srsra           v0.4h, v2.4h, #1        // (t8 - t4 + 1) >> 1
        ssra            v18.4h, v23.4h, #1      // (t6 + t2) >> 1
        srsra           v22.4h, v21.4h, #1      // (t7 - t3 + 1) >> 1
        ssra            v25.4h, v17.4h, #1      // (t7 + t3) >> 1
        srsra           v3.4h, v24.4h, #1       // (t6 - t2 + 1) >> 1
        ssra            v26.4h, v27.4h, #1      // (t8 + t4) >> 1
        srsra           v1.4h, v16.4h, #1       // (t5 - t1 + 1) >> 1
        trn1            v0.2d, v20.2d, v0.2d
        trn1            v2.2d, v18.2d, v22.2d
        trn1            v3.2d, v25.2d, v3.2d
        trn1            v1.2d, v26.2d, v1.2d
        srshr           v0.8h, v0.8h, #6        // (t5 + t1 + 64) >> 7, (t8 - t4 + 65) >> 7
        srshr           v2.8h, v2.8h, #6        // (t6 + t2 + 64) >> 7, (t7 - t3 + 65) >> 7
        srshr           v3.8h, v3.8h, #6        // (t7 + t3 + 64) >> 7, (t6 - t2 + 65) >> 7
        srshr           v1.8h, v1.8h, #6        // (t8 + t4 + 64) >> 7, (t5 - t1 + 65) >> 7
        uaddw           v0.8h, v0.8h, v5.8b
        uaddw           v2.8h, v2.8h, v6.8b
        uaddw           v3.8h, v3.8h, v7.8b
        uaddw           v1.8h, v1.8h, v4.8b
        sqxtun          v0.8b, v0.8h
        sqxtun          v2.8b, v2.8h
        sqxtun          v3.8b, v3.8h
        sqxtun          v1.8b, v1.8h
        st1             {v0.s}[0], [x4], x1
        st1             {v2.s}[0], [x4], x1
        st1             {v3.s}[0], [x4], x1
        st1             {v1.s}[0], [x4], x1
        st1             {v0.s}[1], [x4], x1
        st1             {v2.s}[1], [x4], x1
        st1             {v3.s}[1], [x4], x1
        st1             {v1.s}[1], [x4]
        ret
endfunc

// VC-1 4x4 inverse transform
// On entry:
//   x0 -> array of 8-bit samples, in row-major order
//   x1 = row stride for 8-bit sample array
//   x2 -> array of 16-bit inverse transform coefficients, in row-major order (row stride is 8 coefficients)
// On exit:
//   array at x0 updated by saturated addition of (narrowed) transformed block
function ff_vc1_inv_trans_4x4_neon, export=1
        mov             x3, #16
        ldr             d0, .Lcoeffs_it4
        mov             x4, x0
        ld1             {v1.d}[0], [x2], x3     // 00 01 02 03
        ld1             {v2.d}[0], [x2], x3     // 10 11 12 13
        ld1             {v3.d}[0], [x2], x3     // 20 21 22 23
        ld1             {v4.d}[0], [x2]         // 30 31 32 33
        ld1             {v5.s}[0], [x0], x1
        ld1             {v5.s}[1], [x0], x1
        ld1             {v6.s}[0], [x0], x1
        trn2            v7.4h, v1.4h, v2.4h     // 01 11 03 13
        trn1            v1.4h, v1.4h, v2.4h     // 00 10 02 12
        ld1             {v6.s}[1], [x0]
        trn2            v2.4h, v3.4h, v4.4h     // 21 31 23 33
        trn1            v3.4h, v3.4h, v4.4h     // 20 30 22 32
        trn2            v4.2s, v7.2s, v2.2s     // 03 13 23 33
        trn1            v16.2s, v1.2s, v3.2s    // 00 10 20 30
        trn1            v2.2s, v7.2s, v2.2s     // 01 11 21 31
        trn2            v1.2s, v1.2s, v3.2s     // 02 12 22 32
        mul             v3.4h, v4.4h, v0.h[0]   //                                                          10/2 * src[3]
        mul             v4.4h, v4.4h, v0.h[1]   //                                                          22/2 * src[3]
        mul             v7.4h, v16.4h, v0.h[2]  //            17 * src[0]
        mul             v1.4h, v1.4h, v0.h[2]   //                                            17 * src[2]
        mla             v3.4h, v2.4h, v0.h[1]   //  t3/2 =                  22/2 * src[1]                 + 10/2 * src[3]
        mls             v4.4h, v2.4h, v0.h[0]   //  t4/2 =                - 10/2 * src[1]                 + 22/2 * src[3]
        add             v2.4h, v7.4h, v1.4h     //   t1  =    17 * src[0]                 +   17 * src[2]
        sub             v1.4h, v7.4h, v1.4h     //   t2  =    17 * src[0]                 -   17 * src[2]
        neg             v7.4h, v3.4h            // -t3/2
        neg             v16.4h, v4.4h           // -t4/2
        ssra            v3.4h, v2.4h, #1        // (t1 + t3) >> 1
        ssra            v4.4h, v1.4h, #1        // (t2 + t4) >> 1
        ssra            v16.4h, v1.4h, #1       // (t2 - t4) >> 1
        ssra            v7.4h, v2.4h, #1        // (t1 - t3) >> 1
        srshr           v1.4h, v3.4h, #2        // (t1 + t3 + 64) >> 3
        srshr           v2.4h, v4.4h, #2        // (t2 + t4 + 64) >> 3
        srshr           v3.4h, v16.4h, #2       // (t2 - t4 + 64) >> 3
        srshr           v4.4h, v7.4h, #2        // (t1 - t3 + 64) >> 3
        trn2            v7.4h, v1.4h, v3.4h     // 10 11 30 31
        trn1            v1.4h, v1.4h, v3.4h     // 00 01 20 21
        trn2            v3.4h, v2.4h, v4.4h     // 12 13 32 33
        trn1            v2.4h, v2.4h, v4.4h     // 02 03 22 23
        trn2            v4.2s, v7.2s, v3.2s     // 30 31 32 33
        trn1            v16.2s, v1.2s, v2.2s    // 00 01 02 03
        trn1            v3.2s, v7.2s, v3.2s     // 10 11 12 13
        trn2            v1.2s, v1.2s, v2.2s     // 20 21 22 23
        mul             v2.4h, v4.4h, v0.h[1]   //                                                           22/2 * src[24]
        mul             v4.4h, v4.4h, v0.h[0]   //                                                           10/2 * src[24]
        mul             v7.4h, v16.4h, v0.h[2]  //            17 * src[0]
        mul             v1.4h, v1.4h, v0.h[2]   //                                            17 * src[16]
        mls             v2.4h, v3.4h, v0.h[0]   //  t4/2 =                - 10/2 * src[8]                  + 22/2 * src[24]
        mla             v4.4h, v3.4h, v0.h[1]   //  t3/2 =                  22/2 * src[8]                  + 10/2 * src[24]
        add             v0.4h, v7.4h, v1.4h     //   t1  =    17 * src[0]                 +   17 * src[16]
        sub             v1.4h, v7.4h, v1.4h     //   t2  =    17 * src[0]                 -   17 * src[16]
        neg             v3.4h, v2.4h            // -t4/2
        neg             v7.4h, v4.4h            // -t3/2
        ssra            v4.4h, v0.4h, #1        // (t1 + t3) >> 1
        ssra            v3.4h, v1.4h, #1        // (t2 - t4) >> 1
        ssra            v2.4h, v1.4h, #1        // (t2 + t4) >> 1
        ssra            v7.4h, v0.4h, #1        // (t1 - t3) >> 1
        trn1            v0.2d, v4.2d, v3.2d
        trn1            v1.2d, v2.2d, v7.2d
        srshr           v0.8h, v0.8h, #6        // (t1 + t3 + 64) >> 7, (t2 - t4 + 64) >> 7
        srshr           v1.8h, v1.8h, #6        // (t2 + t4 + 64) >> 7, (t1 - t3 + 64) >> 7
        uaddw           v0.8h, v0.8h, v5.8b
        uaddw           v1.8h, v1.8h, v6.8b
        sqxtun          v0.8b, v0.8h
        sqxtun          v1.8b, v1.8h
        st1             {v0.s}[0], [x4], x1
        st1             {v0.s}[1], [x4], x1
        st1             {v1.s}[0], [x4], x1
        st1             {v1.s}[1], [x4]
        ret
endfunc

// VC-1 8x8 inverse transform, DC case
// On entry:
//   x0 -> array of 8-bit samples, in row-major order
//   x1 = row stride for 8-bit sample array
//   x2 -> 16-bit inverse transform DC coefficient
// On exit:
//   array at x0 updated by saturated addition of (narrowed) transformed block
function ff_vc1_inv_trans_8x8_dc_neon, export=1
        ldrsh           w2, [x2]
        mov             x3, x0
        ld1             {v0.8b}, [x0], x1
        ld1             {v1.8b}, [x0], x1
        ld1             {v2.8b}, [x0], x1
        add             w2, w2, w2, lsl #1
        ld1             {v3.8b}, [x0], x1
        ld1             {v4.8b}, [x0], x1
        add             w2, w2, #1
        ld1             {v5.8b}, [x0], x1
        asr             w2, w2, #1
        ld1             {v6.8b}, [x0], x1
        add             w2, w2, w2, lsl #1
        ld1             {v7.8b}, [x0]
        add             w0, w2, #16
        asr             w0, w0, #5
        dup             v16.8h, w0
        uaddw           v0.8h, v16.8h, v0.8b
        uaddw           v1.8h, v16.8h, v1.8b
        uaddw           v2.8h, v16.8h, v2.8b
        uaddw           v3.8h, v16.8h, v3.8b
        uaddw           v4.8h, v16.8h, v4.8b
        uaddw           v5.8h, v16.8h, v5.8b
        sqxtun          v0.8b, v0.8h
        uaddw           v6.8h, v16.8h, v6.8b
        sqxtun          v1.8b, v1.8h
        uaddw           v7.8h, v16.8h, v7.8b
        sqxtun          v2.8b, v2.8h
        sqxtun          v3.8b, v3.8h
        sqxtun          v4.8b, v4.8h
        st1             {v0.8b}, [x3], x1
        sqxtun          v0.8b, v5.8h
        st1             {v1.8b}, [x3], x1
        sqxtun          v1.8b, v6.8h
        st1             {v2.8b}, [x3], x1
        sqxtun          v2.8b, v7.8h
        st1             {v3.8b}, [x3], x1
        st1             {v4.8b}, [x3], x1
        st1             {v0.8b}, [x3], x1
        st1             {v1.8b}, [x3], x1
        st1             {v2.8b}, [x3]
        ret
endfunc

// VC-1 8x4 inverse transform, DC case
// On entry:
//   x0 -> array of 8-bit samples, in row-major order
//   x1 = row stride for 8-bit sample array
//   x2 -> 16-bit inverse transform DC coefficient
// On exit:
//   array at x0 updated by saturated addition of (narrowed) transformed block
function ff_vc1_inv_trans_8x4_dc_neon, export=1
        ldrsh           w2, [x2]
        mov             x3, x0
        ld1             {v0.8b}, [x0], x1
        ld1             {v1.8b}, [x0], x1
        ld1             {v2.8b}, [x0], x1
        add             w2, w2, w2, lsl #1
        ld1             {v3.8b}, [x0]
        add             w0, w2, #1
        asr             w0, w0, #1
        add             w0, w0, w0, lsl #4
        add             w0, w0, #64
        asr             w0, w0, #7
        dup             v4.8h, w0
        uaddw           v0.8h, v4.8h, v0.8b
        uaddw           v1.8h, v4.8h, v1.8b
        uaddw           v2.8h, v4.8h, v2.8b
        uaddw           v3.8h, v4.8h, v3.8b
        sqxtun          v0.8b, v0.8h
        sqxtun          v1.8b, v1.8h
        sqxtun          v2.8b, v2.8h
        sqxtun          v3.8b, v3.8h
        st1             {v0.8b}, [x3], x1
        st1             {v1.8b}, [x3], x1
        st1             {v2.8b}, [x3], x1
        st1             {v3.8b}, [x3]
        ret
endfunc

// VC-1 4x8 inverse transform, DC case
// On entry:
//   x0 -> array of 8-bit samples, in row-major order
//   x1 = row stride for 8-bit sample array
//   x2 -> 16-bit inverse transform DC coefficient
// On exit:
//   array at x0 updated by saturated addition of (narrowed) transformed block
function ff_vc1_inv_trans_4x8_dc_neon, export=1
        ldrsh           w2, [x2]
        mov             x3, x0
        ld1             {v0.s}[0], [x0], x1
        ld1             {v1.s}[0], [x0], x1
        ld1             {v2.s}[0], [x0], x1
        add             w2, w2, w2, lsl #4
        ld1             {v3.s}[0], [x0], x1
        add             w2, w2, #4
        asr             w2, w2, #3
        add             w2, w2, w2, lsl #1
        ld1             {v0.s}[1], [x0], x1
        add             w2, w2, #16
        asr             w2, w2, #5
        dup             v4.8h, w2
        ld1             {v1.s}[1], [x0], x1
        ld1             {v2.s}[1], [x0], x1
        ld1             {v3.s}[1], [x0]
        uaddw           v0.8h, v4.8h, v0.8b
        uaddw           v1.8h, v4.8h, v1.8b
        uaddw           v2.8h, v4.8h, v2.8b
        uaddw           v3.8h, v4.8h, v3.8b
        sqxtun          v0.8b, v0.8h
        sqxtun          v1.8b, v1.8h
        sqxtun          v2.8b, v2.8h
        sqxtun          v3.8b, v3.8h
        st1             {v0.s}[0], [x3], x1
        st1             {v1.s}[0], [x3], x1
        st1             {v2.s}[0], [x3], x1
        st1             {v3.s}[0], [x3], x1
        st1             {v0.s}[1], [x3], x1
        st1             {v1.s}[1], [x3], x1
        st1             {v2.s}[1], [x3], x1
        st1             {v3.s}[1], [x3]
        ret
endfunc

// VC-1 4x4 inverse transform, DC case
// On entry:
//   x0 -> array of 8-bit samples, in row-major order
//   x1 = row stride for 8-bit sample array
//   x2 -> 16-bit inverse transform DC coefficient
// On exit:
//   array at x0 updated by saturated addition of (narrowed) transformed block
function ff_vc1_inv_trans_4x4_dc_neon, export=1
        ldrsh           w2, [x2]
        mov             x3, x0
        ld1             {v0.s}[0], [x0], x1
        ld1             {v1.s}[0], [x0], x1
        ld1             {v0.s}[1], [x0], x1
        add             w2, w2, w2, lsl #4
        ld1             {v1.s}[1], [x0]
        add             w0, w2, #4
        asr             w0, w0, #3
        add             w0, w0, w0, lsl #4
        add             w0, w0, #64
        asr             w0, w0, #7
        dup             v2.8h, w0
        uaddw           v0.8h, v2.8h, v0.8b
        uaddw           v1.8h, v2.8h, v1.8b
        sqxtun          v0.8b, v0.8h
        sqxtun          v1.8b, v1.8h
        st1             {v0.s}[0], [x3], x1
        st1             {v1.s}[0], [x3], x1
        st1             {v0.s}[1], [x3], x1
        st1             {v1.s}[1], [x3]
        ret
endfunc

.align  5
.Lcoeffs_it8:
.quad   0x000F00090003
.Lcoeffs_it4:
.quad   0x0011000B0005
.Lcoeffs:
.quad   0x00050002

// VC-1 in-loop deblocking filter for 4 pixel pairs at boundary of vertically-neighbouring blocks
// On entry:
//   x0 -> top-left pel of lower block
//   x1 = row stride, bytes
//   w2 = PQUANT bitstream parameter
function ff_vc1_v_loop_filter4_neon, export=1
        sub             x3, x0, w1, sxtw #2
        ldr             d0, .Lcoeffs
        ld1             {v1.s}[0], [x0], x1     // P5
        ld1             {v2.s}[0], [x3], x1     // P1
        ld1             {v3.s}[0], [x3], x1     // P2
        ld1             {v4.s}[0], [x0], x1     // P6
        ld1             {v5.s}[0], [x3], x1     // P3
        ld1             {v6.s}[0], [x0], x1     // P7
        ld1             {v7.s}[0], [x3]         // P4
        ld1             {v16.s}[0], [x0]        // P8
        ushll           v17.8h, v1.8b, #1       // 2*P5
        dup             v18.8h, w2              // pq
        ushll           v2.8h, v2.8b, #1        // 2*P1
        uxtl            v3.8h, v3.8b            // P2
        uxtl            v4.8h, v4.8b            // P6
        uxtl            v19.8h, v5.8b           // P3
        mls             v2.4h, v3.4h, v0.h[1]   // 2*P1-5*P2
        uxtl            v3.8h, v6.8b            // P7
        mls             v17.4h, v4.4h, v0.h[1]  // 2*P5-5*P6
        ushll           v5.8h, v5.8b, #1        // 2*P3
        uxtl            v6.8h, v7.8b            // P4
        mla             v17.4h, v3.4h, v0.h[1]  // 2*P5-5*P6+5*P7
        uxtl            v3.8h, v16.8b           // P8
        mla             v2.4h, v19.4h, v0.h[1]  // 2*P1-5*P2+5*P3
        uxtl            v1.8h, v1.8b            // P5
        mls             v5.4h, v6.4h, v0.h[1]   // 2*P3-5*P4
        mls             v17.4h, v3.4h, v0.h[0]  // 2*P5-5*P6+5*P7-2*P8
        sub             v3.4h, v6.4h, v1.4h     // P4-P5
        mls             v2.4h, v6.4h, v0.h[0]   // 2*P1-5*P2+5*P3-2*P4
        mla             v5.4h, v1.4h, v0.h[1]   // 2*P3-5*P4+5*P5
        mls             v5.4h, v4.4h, v0.h[0]   // 2*P3-5*P4+5*P5-2*P6
        abs             v4.4h, v3.4h
        srshr           v7.4h, v17.4h, #3
        srshr           v2.4h, v2.4h, #3
        sshr            v4.4h, v4.4h, #1        // clip
        srshr           v5.4h, v5.4h, #3
        abs             v7.4h, v7.4h            // a2
        sshr            v3.4h, v3.4h, #8        // clip_sign
        abs             v2.4h, v2.4h            // a1
        cmeq            v16.4h, v4.4h, #0       // test clip == 0
        abs             v17.4h, v5.4h           // a0
        sshr            v5.4h, v5.4h, #8        // a0_sign
        cmhs            v19.4h, v2.4h, v7.4h    // test a1 >= a2
        cmhs            v18.4h, v17.4h, v18.4h  // test a0 >= pq
        sub             v3.4h, v3.4h, v5.4h     // clip_sign - a0_sign
        bsl             v19.8b, v7.8b, v2.8b    // a3
        orr             v2.8b, v16.8b, v18.8b   // test clip == 0 || a0 >= pq
        uqsub           v5.4h, v17.4h, v19.4h   // a0 >= a3 ? a0-a3 : 0  (a0 > a3 in all cases where filtering is enabled, so makes more sense to subtract this way round than the opposite and then taking the abs)
        cmhs            v7.4h, v19.4h, v17.4h   // test a3 >= a0
        mul             v0.4h, v5.4h, v0.h[1]   // a0 >= a3 ? 5*(a0-a3) : 0
        orr             v5.8b, v2.8b, v7.8b     // test clip == 0 || a0 >= pq || a3 >= a0
        mov             w0, v5.s[1]             // move to gp reg
        ushr            v0.4h, v0.4h, #3        // a0 >= a3 ? (5*(a0-a3))>>3 : 0
        cmhs            v5.4h, v0.4h, v4.4h
        tbnz            w0, #0, 1f              // none of the 4 pixel pairs should be updated if this one is not filtered
        bsl             v5.8b, v4.8b, v0.8b     // FFMIN(d, clip)
        bic             v0.8b, v5.8b, v2.8b     // set each d to zero if it should not be filtered because clip == 0 || a0 >= pq (a3 > a0 case already zeroed by saturating sub)
        mls             v6.4h, v0.4h, v3.4h     // invert d depending on clip_sign & a0_sign, or zero it if they match, and accumulate into P4
        mla             v1.4h, v0.4h, v3.4h     // invert d depending on clip_sign & a0_sign, or zero it if they match, and accumulate into P5
        sqxtun          v0.8b, v6.8h
        sqxtun          v1.8b, v1.8h
        st1             {v0.s}[0], [x3], x1
        st1             {v1.s}[0], [x3]
1:      ret
endfunc

// VC-1 in-loop deblocking filter for 4 pixel pairs at boundary of horizontally-neighbouring blocks
// On entry:
//   x0 -> top-left pel of right block
//   x1 = row stride, bytes
//   w2 = PQUANT bitstream parameter
function ff_vc1_h_loop_filter4_neon, export=1
        sub             x3, x0, #4              // where to start reading
        ldr             d0, .Lcoeffs
        ld1             {v1.8b}, [x3], x1
        sub             x0, x0, #1              // where to start writing
        ld1             {v2.8b}, [x3], x1
        ld1             {v3.8b}, [x3], x1
        ld1             {v4.8b}, [x3]
        dup             v5.8h, w2               // pq
        trn1            v6.8b, v1.8b, v2.8b
        trn2            v1.8b, v1.8b, v2.8b
        trn1            v2.8b, v3.8b, v4.8b
        trn2            v3.8b, v3.8b, v4.8b
        trn1            v4.4h, v6.4h, v2.4h     // P1, P5
        trn1            v7.4h, v1.4h, v3.4h     // P2, P6
        trn2            v2.4h, v6.4h, v2.4h     // P3, P7
        trn2            v1.4h, v1.4h, v3.4h     // P4, P8
        ushll           v3.8h, v4.8b, #1        // 2*P1, 2*P5
        uxtl            v6.8h, v7.8b            // P2, P6
        uxtl            v7.8h, v2.8b            // P3, P7
        uxtl            v1.8h, v1.8b            // P4, P8
        mls             v3.8h, v6.8h, v0.h[1]   // 2*P1-5*P2, 2*P5-5*P6
        ushll           v2.8h, v2.8b, #1        // 2*P3, 2*P7
        uxtl            v4.8h, v4.8b            // P1, P5
        mla             v3.8h, v7.8h, v0.h[1]   // 2*P1-5*P2+5*P3, 2*P5-5*P6+5*P7
        mov             d6, v6.d[1]             // P6
        mls             v3.8h, v1.8h, v0.h[0]   // 2*P1-5*P2+5*P3-2*P4, 2*P5-5*P6+5*P7-2*P8
        mov             d4, v4.d[1]             // P5
        mls             v2.4h, v1.4h, v0.h[1]   // 2*P3-5*P4
        mla             v2.4h, v4.4h, v0.h[1]   // 2*P3-5*P4+5*P5
        sub             v7.4h, v1.4h, v4.4h     // P4-P5
        mls             v2.4h, v6.4h, v0.h[0]   // 2*P3-5*P4+5*P5-2*P6
        srshr           v3.8h, v3.8h, #3
        abs             v6.4h, v7.4h
        sshr            v7.4h, v7.4h, #8        // clip_sign
        srshr           v2.4h, v2.4h, #3
        abs             v3.8h, v3.8h            // a1, a2
        sshr            v6.4h, v6.4h, #1        // clip
        mov             d16, v3.d[1]            // a2
        abs             v17.4h, v2.4h           // a0
        cmeq            v18.4h, v6.4h, #0       // test clip == 0
        sshr            v2.4h, v2.4h, #8        // a0_sign
        cmhs            v19.4h, v3.4h, v16.4h   // test a1 >= a2
        cmhs            v5.4h, v17.4h, v5.4h    // test a0 >= pq
        sub             v2.4h, v7.4h, v2.4h     // clip_sign - a0_sign
        bsl             v19.8b, v16.8b, v3.8b   // a3
        orr             v3.8b, v18.8b, v5.8b    // test clip == 0 || a0 >= pq
        uqsub           v5.4h, v17.4h, v19.4h   // a0 >= a3 ? a0-a3 : 0  (a0 > a3 in all cases where filtering is enabled, so makes more sense to subtract this way round than the opposite and then taking the abs)
        cmhs            v7.4h, v19.4h, v17.4h   // test a3 >= a0
        mul             v0.4h, v5.4h, v0.h[1]   // a0 >= a3 ? 5*(a0-a3) : 0
        orr             v5.8b, v3.8b, v7.8b     // test clip == 0 || a0 >= pq || a3 >= a0
        mov             w2, v5.s[1]             // move to gp reg
        ushr            v0.4h, v0.4h, #3        // a0 >= a3 ? (5*(a0-a3))>>3 : 0
        cmhs            v5.4h, v0.4h, v6.4h
        tbnz            w2, #0, 1f              // none of the 4 pixel pairs should be updated if this one is not filtered
        bsl             v5.8b, v6.8b, v0.8b     // FFMIN(d, clip)
        bic             v0.8b, v5.8b, v3.8b     // set each d to zero if it should not be filtered because clip == 0 || a0 >= pq (a3 > a0 case already zeroed by saturating sub)
        mla             v4.4h, v0.4h, v2.4h     // invert d depending on clip_sign & a0_sign, or zero it if they match, and accumulate into P5
        mls             v1.4h, v0.4h, v2.4h     // invert d depending on clip_sign & a0_sign, or zero it if they match, and accumulate into P4
        sqxtun          v3.8b, v4.8h
        sqxtun          v2.8b, v1.8h
        st2             {v2.b, v3.b}[0], [x0], x1
        st2             {v2.b, v3.b}[1], [x0], x1
        st2             {v2.b, v3.b}[2], [x0], x1
        st2             {v2.b, v3.b}[3], [x0]
1:      ret
endfunc

// VC-1 in-loop deblocking filter for 8 pixel pairs at boundary of vertically-neighbouring blocks
// On entry:
//   x0 -> top-left pel of lower block
//   x1 = row stride, bytes
//   w2 = PQUANT bitstream parameter
function ff_vc1_v_loop_filter8_neon, export=1
        sub             x3, x0, w1, sxtw #2
        ldr             d0, .Lcoeffs
        ld1             {v1.8b}, [x0], x1       // P5
        movi            v2.2d, #0x0000ffff00000000
        ld1             {v3.8b}, [x3], x1       // P1
        ld1             {v4.8b}, [x3], x1       // P2
        ld1             {v5.8b}, [x0], x1       // P6
        ld1             {v6.8b}, [x3], x1       // P3
        ld1             {v7.8b}, [x0], x1       // P7
        ushll           v16.8h, v1.8b, #1       // 2*P5
        ushll           v3.8h, v3.8b, #1        // 2*P1
        ld1             {v17.8b}, [x3]          // P4
        uxtl            v4.8h, v4.8b            // P2
        ld1             {v18.8b}, [x0]          // P8
        uxtl            v5.8h, v5.8b            // P6
        dup             v19.8h, w2              // pq
        uxtl            v20.8h, v6.8b           // P3
        mls             v3.8h, v4.8h, v0.h[1]   // 2*P1-5*P2
        uxtl            v4.8h, v7.8b            // P7
        ushll           v6.8h, v6.8b, #1        // 2*P3
        mls             v16.8h, v5.8h, v0.h[1]  // 2*P5-5*P6
        uxtl            v7.8h, v17.8b           // P4
        uxtl            v17.8h, v18.8b          // P8
        mla             v16.8h, v4.8h, v0.h[1]  // 2*P5-5*P6+5*P7
        uxtl            v1.8h, v1.8b            // P5
        mla             v3.8h, v20.8h, v0.h[1]  // 2*P1-5*P2+5*P3
        sub             v4.8h, v7.8h, v1.8h     // P4-P5
        mls             v6.8h, v7.8h, v0.h[1]   // 2*P3-5*P4
        mls             v16.8h, v17.8h, v0.h[0] // 2*P5-5*P6+5*P7-2*P8
        abs             v17.8h, v4.8h
        sshr            v4.8h, v4.8h, #8        // clip_sign
        mls             v3.8h, v7.8h, v0.h[0]   // 2*P1-5*P2+5*P3-2*P4
        sshr            v17.8h, v17.8h, #1      // clip
        mla             v6.8h, v1.8h, v0.h[1]   // 2*P3-5*P4+5*P5
        srshr           v16.8h, v16.8h, #3
        mls             v6.8h, v5.8h, v0.h[0]   // 2*P3-5*P4+5*P5-2*P6
        cmeq            v5.8h, v17.8h, #0       // test clip == 0
        srshr           v3.8h, v3.8h, #3
        abs             v16.8h, v16.8h          // a2
        abs             v3.8h, v3.8h            // a1
        srshr           v6.8h, v6.8h, #3
        cmhs            v18.8h, v3.8h, v16.8h   // test a1 >= a2
        abs             v20.8h, v6.8h           // a0
        sshr            v6.8h, v6.8h, #8        // a0_sign
        bsl             v18.16b, v16.16b, v3.16b // a3
        cmhs            v3.8h, v20.8h, v19.8h   // test a0 >= pq
        sub             v4.8h, v4.8h, v6.8h     // clip_sign - a0_sign
        uqsub           v6.8h, v20.8h, v18.8h   // a0 >= a3 ? a0-a3 : 0  (a0 > a3 in all cases where filtering is enabled, so makes more sense to subtract this way round than the opposite and then taking the abs)
        cmhs            v16.8h, v18.8h, v20.8h  // test a3 >= a0
        orr             v3.16b, v5.16b, v3.16b  // test clip == 0 || a0 >= pq
        mul             v0.8h, v6.8h, v0.h[1]   // a0 >= a3 ? 5*(a0-a3) : 0
        orr             v5.16b, v3.16b, v16.16b // test clip == 0 || a0 >= pq || a3 >= a0
        cmtst           v2.2d, v5.2d, v2.2d     // if 2nd of each group of is not filtered, then none of the others in the group should be either
        mov             w0, v5.s[1]             // move to gp reg
        ushr            v0.8h, v0.8h, #3        // a0 >= a3 ? (5*(a0-a3))>>3 : 0
        mov             w2, v5.s[3]
        orr             v2.16b, v3.16b, v2.16b
        cmhs            v3.8h, v0.8h, v17.8h
        and             w0, w0, w2
        bsl             v3.16b, v17.16b, v0.16b // FFMIN(d, clip)
        tbnz            w0, #0, 1f              // none of the 8 pixel pairs should be updated in this case
        bic             v0.16b, v3.16b, v2.16b  // set each d to zero if it should not be filtered
        mls             v7.8h, v0.8h, v4.8h     // invert d depending on clip_sign & a0_sign, or zero it if they match, and accumulate into P4
        mla             v1.8h, v0.8h, v4.8h     // invert d depending on clip_sign & a0_sign, or zero it if they match, and accumulate into P5
        sqxtun          v0.8b, v7.8h
        sqxtun          v1.8b, v1.8h
        st1             {v0.8b}, [x3], x1
        st1             {v1.8b}, [x3]
1:      ret
endfunc

// VC-1 in-loop deblocking filter for 8 pixel pairs at boundary of horizontally-neighbouring blocks
// On entry:
//   x0 -> top-left pel of right block
//   x1 = row stride, bytes
//   w2 = PQUANT bitstream parameter
function ff_vc1_h_loop_filter8_neon, export=1
        sub             x3, x0, #4              // where to start reading
        ldr             d0, .Lcoeffs
        ld1             {v1.8b}, [x3], x1       // P1[0], P2[0]...
        sub             x0, x0, #1              // where to start writing
        ld1             {v2.8b}, [x3], x1
        add             x4, x0, x1, lsl #2
        ld1             {v3.8b}, [x3], x1
        ld1             {v4.8b}, [x3], x1
        ld1             {v5.8b}, [x3], x1
        ld1             {v6.8b}, [x3], x1
        ld1             {v7.8b}, [x3], x1
        trn1            v16.8b, v1.8b, v2.8b    // P1[0], P1[1], P3[0]...
        ld1             {v17.8b}, [x3]
        trn2            v1.8b, v1.8b, v2.8b     // P2[0], P2[1], P4[0]...
        trn1            v2.8b, v3.8b, v4.8b     // P1[2], P1[3], P3[2]...
        trn2            v3.8b, v3.8b, v4.8b     // P2[2], P2[3], P4[2]...
        dup             v4.8h, w2               // pq
        trn1            v18.8b, v5.8b, v6.8b    // P1[4], P1[5], P3[4]...
        trn2            v5.8b, v5.8b, v6.8b     // P2[4], P2[5], P4[4]...
        trn1            v6.4h, v16.4h, v2.4h    // P1[0], P1[1], P1[2], P1[3], P5[0]...
        trn1            v19.4h, v1.4h, v3.4h    // P2[0], P2[1], P2[2], P2[3], P6[0]...
        trn1            v20.8b, v7.8b, v17.8b   // P1[6], P1[7], P3[6]...
        trn2            v7.8b, v7.8b, v17.8b    // P2[6], P2[7], P4[6]...
        trn2            v2.4h, v16.4h, v2.4h    // P3[0], P3[1], P3[2], P3[3], P7[0]...
        trn2            v1.4h, v1.4h, v3.4h     // P4[0], P4[1], P4[2], P4[3], P8[0]...
        trn1            v3.4h, v18.4h, v20.4h   // P1[4], P1[5], P1[6], P1[7], P5[4]...
        trn1            v16.4h, v5.4h, v7.4h    // P2[4], P2[5], P2[6], P2[7], P6[4]...
        trn2            v17.4h, v18.4h, v20.4h  // P3[4], P3[5], P3[6], P3[7], P7[4]...
        trn2            v5.4h, v5.4h, v7.4h     // P4[4], P4[5], P4[6], P4[7], P8[4]...
        trn1            v7.2s, v6.2s, v3.2s     // P1
        trn1            v18.2s, v19.2s, v16.2s  // P2
        trn2            v3.2s, v6.2s, v3.2s     // P5
        trn2            v6.2s, v19.2s, v16.2s   // P6
        trn1            v16.2s, v2.2s, v17.2s   // P3
        trn2            v2.2s, v2.2s, v17.2s    // P7
        ushll           v7.8h, v7.8b, #1        // 2*P1
        trn1            v17.2s, v1.2s, v5.2s    // P4
        ushll           v19.8h, v3.8b, #1       // 2*P5
        trn2            v1.2s, v1.2s, v5.2s     // P8
        uxtl            v5.8h, v18.8b           // P2
        uxtl            v6.8h, v6.8b            // P6
        uxtl            v18.8h, v16.8b          // P3
        mls             v7.8h, v5.8h, v0.h[1]   // 2*P1-5*P2
        uxtl            v2.8h, v2.8b            // P7
        ushll           v5.8h, v16.8b, #1       // 2*P3
        mls             v19.8h, v6.8h, v0.h[1]  // 2*P5-5*P6
        uxtl            v16.8h, v17.8b          // P4
        uxtl            v1.8h, v1.8b            // P8
        mla             v19.8h, v2.8h, v0.h[1]  // 2*P5-5*P6+5*P7
        uxtl            v2.8h, v3.8b            // P5
        mla             v7.8h, v18.8h, v0.h[1]  // 2*P1-5*P2+5*P3
        sub             v3.8h, v16.8h, v2.8h    // P4-P5
        mls             v5.8h, v16.8h, v0.h[1]  // 2*P3-5*P4
        mls             v19.8h, v1.8h, v0.h[0]  // 2*P5-5*P6+5*P7-2*P8
        abs             v1.8h, v3.8h
        sshr            v3.8h, v3.8h, #8        // clip_sign
        mls             v7.8h, v16.8h, v0.h[0]  // 2*P1-5*P2+5*P3-2*P4
        sshr            v1.8h, v1.8h, #1        // clip
        mla             v5.8h, v2.8h, v0.h[1]   // 2*P3-5*P4+5*P5
        srshr           v17.8h, v19.8h, #3
        mls             v5.8h, v6.8h, v0.h[0]   // 2*P3-5*P4+5*P5-2*P6
        cmeq            v6.8h, v1.8h, #0        // test clip == 0
        srshr           v7.8h, v7.8h, #3
        abs             v17.8h, v17.8h          // a2
        abs             v7.8h, v7.8h            // a1
        srshr           v5.8h, v5.8h, #3
        cmhs            v18.8h, v7.8h, v17.8h   // test a1 >= a2
        abs             v19.8h, v5.8h           // a0
        sshr            v5.8h, v5.8h, #8        // a0_sign
        bsl             v18.16b, v17.16b, v7.16b // a3
        cmhs            v4.8h, v19.8h, v4.8h    // test a0 >= pq
        sub             v3.8h, v3.8h, v5.8h     // clip_sign - a0_sign
        uqsub           v5.8h, v19.8h, v18.8h   // a0 >= a3 ? a0-a3 : 0  (a0 > a3 in all cases where filtering is enabled, so makes more sense to subtract this way round than the opposite and then taking the abs)
        cmhs            v7.8h, v18.8h, v19.8h   // test a3 >= a0
        orr             v4.16b, v6.16b, v4.16b  // test clip == 0 || a0 >= pq
        mul             v0.8h, v5.8h, v0.h[1]   // a0 >= a3 ? 5*(a0-a3) : 0
        orr             v5.16b, v4.16b, v7.16b  // test clip == 0 || a0 >= pq || a3 >= a0
        mov             w2, v5.s[1]             // move to gp reg
        ushr            v0.8h, v0.8h, #3        // a0 >= a3 ? (5*(a0-a3))>>3 : 0
        mov             w3, v5.s[3]
        cmhs            v5.8h, v0.8h, v1.8h
        and             w5, w2, w3
        bsl             v5.16b, v1.16b, v0.16b  // FFMIN(d, clip)
        tbnz            w5, #0, 2f              // none of the 8 pixel pairs should be updated in this case
        bic             v0.16b, v5.16b, v4.16b  // set each d to zero if it should not be filtered because clip == 0 || a0 >= pq (a3 > a0 case already zeroed by saturating sub)
        mla             v2.8h, v0.8h, v3.8h     // invert d depending on clip_sign & a0_sign, or zero it if they match, and accumulate into P5
        mls             v16.8h, v0.8h, v3.8h    // invert d depending on clip_sign & a0_sign, or zero it if they match, and accumulate into P4
        sqxtun          v1.8b, v2.8h
        sqxtun          v0.8b, v16.8h
        tbnz            w2, #0, 1f              // none of the first 4 pixel pairs should be updated if so
        st2             {v0.b, v1.b}[0], [x0], x1
        st2             {v0.b, v1.b}[1], [x0], x1
        st2             {v0.b, v1.b}[2], [x0], x1
        st2             {v0.b, v1.b}[3], [x0]
1:      tbnz            w3, #0, 2f              // none of the second 4 pixel pairs should be updated if so
        st2             {v0.b, v1.b}[4], [x4], x1
        st2             {v0.b, v1.b}[5], [x4], x1
        st2             {v0.b, v1.b}[6], [x4], x1
        st2             {v0.b, v1.b}[7], [x4]
2:      ret
endfunc

// VC-1 in-loop deblocking filter for 16 pixel pairs at boundary of vertically-neighbouring blocks
// On entry:
//   x0 -> top-left pel of lower block
//   x1 = row stride, bytes
//   w2 = PQUANT bitstream parameter
function ff_vc1_v_loop_filter16_neon, export=1
        sub             x3, x0, w1, sxtw #2
        ldr             d0, .Lcoeffs
        ld1             {v1.16b}, [x0], x1      // P5
        movi            v2.2d, #0x0000ffff00000000
        ld1             {v3.16b}, [x3], x1      // P1
        ld1             {v4.16b}, [x3], x1      // P2
        ld1             {v5.16b}, [x0], x1      // P6
        ld1             {v6.16b}, [x3], x1      // P3
        ld1             {v7.16b}, [x0], x1      // P7
        ushll           v16.8h, v1.8b, #1       // 2*P5[0..7]
        ushll           v17.8h, v3.8b, #1       // 2*P1[0..7]
        ld1             {v18.16b}, [x3]         // P4
        uxtl            v19.8h, v4.8b           // P2[0..7]
        ld1             {v20.16b}, [x0]         // P8
        uxtl            v21.8h, v5.8b           // P6[0..7]
        dup             v22.8h, w2              // pq
        ushll2          v3.8h, v3.16b, #1       // 2*P1[8..15]
        mls             v17.8h, v19.8h, v0.h[1] // 2*P1[0..7]-5*P2[0..7]
        ushll2          v19.8h, v1.16b, #1      // 2*P5[8..15]
        uxtl2           v4.8h, v4.16b           // P2[8..15]
        mls             v16.8h, v21.8h, v0.h[1] // 2*P5[0..7]-5*P6[0..7]
        uxtl2           v5.8h, v5.16b           // P6[8..15]
        uxtl            v23.8h, v6.8b           // P3[0..7]
        uxtl            v24.8h, v7.8b           // P7[0..7]
        mls             v3.8h, v4.8h, v0.h[1]   // 2*P1[8..15]-5*P2[8..15]
        ushll           v4.8h, v6.8b, #1        // 2*P3[0..7]
        uxtl            v25.8h, v18.8b          // P4[0..7]
        mls             v19.8h, v5.8h, v0.h[1]  // 2*P5[8..15]-5*P6[8..15]
        uxtl2           v26.8h, v6.16b          // P3[8..15]
        mla             v17.8h, v23.8h, v0.h[1] // 2*P1[0..7]-5*P2[0..7]+5*P3[0..7]
        uxtl2           v7.8h, v7.16b           // P7[8..15]
        ushll2          v6.8h, v6.16b, #1       // 2*P3[8..15]
        mla             v16.8h, v24.8h, v0.h[1] // 2*P5[0..7]-5*P6[0..7]+5*P7[0..7]
        uxtl2           v18.8h, v18.16b         // P4[8..15]
        uxtl            v23.8h, v20.8b          // P8[0..7]
        mls             v4.8h, v25.8h, v0.h[1]  // 2*P3[0..7]-5*P4[0..7]
        uxtl            v24.8h, v1.8b           // P5[0..7]
        uxtl2           v20.8h, v20.16b         // P8[8..15]
        mla             v3.8h, v26.8h, v0.h[1]  // 2*P1[8..15]-5*P2[8..15]+5*P3[8..15]
        uxtl2           v1.8h, v1.16b           // P5[8..15]
        sub             v26.8h, v25.8h, v24.8h  // P4[0..7]-P5[0..7]
        mla             v19.8h, v7.8h, v0.h[1]  // 2*P5[8..15]-5*P6[8..15]+5*P7[8..15]
        sub             v7.8h, v18.8h, v1.8h    // P4[8..15]-P5[8..15]
        mls             v6.8h, v18.8h, v0.h[1]  // 2*P3[8..15]-5*P4[8..15]
        abs             v27.8h, v26.8h
        sshr            v26.8h, v26.8h, #8      // clip_sign[0..7]
        mls             v17.8h, v25.8h, v0.h[0] // 2*P1[0..7]-5*P2[0..7]+5*P3[0..7]-2*P4[0..7]
        abs             v28.8h, v7.8h
        sshr            v27.8h, v27.8h, #1      // clip[0..7]
        mls             v16.8h, v23.8h, v0.h[0] // 2*P5[0..7]-5*P6[0..7]+5*P7[0..7]-2*P8[0..7]
        sshr            v7.8h, v7.8h, #8        // clip_sign[8..15]
        sshr            v23.8h, v28.8h, #1      // clip[8..15]
        mla             v4.8h, v24.8h, v0.h[1]  // 2*P3[0..7]-5*P4[0..7]+5*P5[0..7]
        cmeq            v28.8h, v27.8h, #0      // test clip[0..7] == 0
        srshr           v17.8h, v17.8h, #3
        mls             v3.8h, v18.8h, v0.h[0]  // 2*P1[8..15]-5*P2[8..15]+5*P3[8..15]-2*P4[8..15]
        cmeq            v29.8h, v23.8h, #0      // test clip[8..15] == 0
        srshr           v16.8h, v16.8h, #3
        mls             v19.8h, v20.8h, v0.h[0] // 2*P5[8..15]-5*P6[8..15]+5*P7[8..15]-2*P8[8..15]
        abs             v17.8h, v17.8h          // a1[0..7]
        mla             v6.8h, v1.8h, v0.h[1]   // 2*P3[8..15]-5*P4[8..15]+5*P5[8..15]
        srshr           v3.8h, v3.8h, #3
        mls             v4.8h, v21.8h, v0.h[0]  // 2*P3[0..7]-5*P4[0..7]+5*P5[0..7]-2*P6[0..7]
        abs             v16.8h, v16.8h          // a2[0..7]
        srshr           v19.8h, v19.8h, #3
        mls             v6.8h, v5.8h, v0.h[0]   // 2*P3[8..15]-5*P4[8..15]+5*P5[8..15]-2*P6[8..15]
        cmhs            v5.8h, v17.8h, v16.8h   // test a1[0..7] >= a2[0..7]
        abs             v3.8h, v3.8h            // a1[8..15]
        srshr           v4.8h, v4.8h, #3
        abs             v19.8h, v19.8h          // a2[8..15]
        bsl             v5.16b, v16.16b, v17.16b // a3[0..7]
        srshr           v6.8h, v6.8h, #3
        cmhs            v16.8h, v3.8h, v19.8h   // test a1[8..15] >= a2[8.15]
        abs             v17.8h, v4.8h           // a0[0..7]
        sshr            v4.8h, v4.8h, #8        // a0_sign[0..7]
        bsl             v16.16b, v19.16b, v3.16b // a3[8..15]
        uqsub           v3.8h, v17.8h, v5.8h    // a0[0..7] >= a3[0..7] ? a0[0..7]-a3[0..7] : 0  (a0 > a3 in all cases where filtering is enabled, so makes more sense to subtract this way round than the opposite and then taking the abs)
        abs             v19.8h, v6.8h           // a0[8..15]
        cmhs            v20.8h, v17.8h, v22.8h  // test a0[0..7] >= pq
        cmhs            v5.8h, v5.8h, v17.8h    // test a3[0..7] >= a0[0..7]
        sub             v4.8h, v26.8h, v4.8h    // clip_sign[0..7] - a0_sign[0..7]
        sshr            v6.8h, v6.8h, #8        // a0_sign[8..15]
        mul             v3.8h, v3.8h, v0.h[1]   // a0[0..7] >= a3[0..7] ? 5*(a0[0..7]-a3[0..7]) : 0
        uqsub           v17.8h, v19.8h, v16.8h  // a0[8..15] >= a3[8..15] ? a0[8..15]-a3[8..15] : 0  (a0 > a3 in all cases where filtering is enabled, so makes more sense to subtract this way round than the opposite and then taking the abs)
        orr             v20.16b, v28.16b, v20.16b // test clip[0..7] == 0 || a0[0..7] >= pq
        cmhs            v21.8h, v19.8h, v22.8h  // test a0[8..15] >= pq
        cmhs            v16.8h, v16.8h, v19.8h  // test a3[8..15] >= a0[8..15]
        mul             v0.8h, v17.8h, v0.h[1]  // a0[8..15] >= a3[8..15] ? 5*(a0[8..15]-a3[8..15]) : 0
        sub             v6.8h, v7.8h, v6.8h     // clip_sign[8..15] - a0_sign[8..15]
        orr             v5.16b, v20.16b, v5.16b // test clip[0..7] == 0 || a0[0..7] >= pq || a3[0..7] >= a0[0..7]
        ushr            v3.8h, v3.8h, #3        // a0[0..7] >= a3[0..7] ? (5*(a0[0..7]-a3[0..7]))>>3 : 0
        orr             v7.16b, v29.16b, v21.16b // test clip[8..15] == 0 || a0[8..15] >= pq
        cmtst           v17.2d, v5.2d, v2.2d    // if 2nd of each group of is not filtered, then none of the others in the group should be either
        mov             w0, v5.s[1]             // move to gp reg
        cmhs            v19.8h, v3.8h, v27.8h
        ushr            v0.8h, v0.8h, #3        // a0[8..15] >= a3[8..15] ? (5*(a0[8..15]-a3[8..15]))>>3 : 0
        mov             w2, v5.s[3]
        orr             v5.16b, v7.16b, v16.16b // test clip[8..15] == 0 || a0[8..15] >= pq || a3[8..15] >= a0[8..15]
        orr             v16.16b, v20.16b, v17.16b
        bsl             v19.16b, v27.16b, v3.16b // FFMIN(d[0..7], clip[0..7])
        cmtst           v2.2d, v5.2d, v2.2d
        cmhs            v3.8h, v0.8h, v23.8h
        mov             w4, v5.s[1]
        mov             w5, v5.s[3]
        and             w0, w0, w2
        bic             v5.16b, v19.16b, v16.16b // set each d[0..7] to zero if it should not be filtered because clip[0..7] == 0 || a0[0..7] >= pq (a3 > a0 case already zeroed by saturating sub)
        orr             v2.16b, v7.16b, v2.16b
        bsl             v3.16b, v23.16b, v0.16b // FFMIN(d[8..15], clip[8..15])
        mls             v25.8h, v5.8h, v4.8h    // invert d[0..7] depending on clip_sign[0..7] & a0_sign[0..7], or zero it if they match, and accumulate into P4[0..7]
        and             w2, w4, w5
        bic             v0.16b, v3.16b, v2.16b  // set each d[8..15] to zero if it should not be filtered because clip[8..15] == 0 || a0[8..15] >= pq (a3 > a0 case already zeroed by saturating sub)
        mla             v24.8h, v5.8h, v4.8h    // invert d[0..7] depending on clip_sign[0..7] & a0_sign[0..7], or zero it if they match, and accumulate into P5[0..7]
        and             w0, w0, w2
        mls             v18.8h, v0.8h, v6.8h    // invert d[8..15] depending on clip_sign[8..15] & a0_sign[8..15], or zero it if they match, and accumulate into P4[8..15]
        sqxtun          v2.8b, v25.8h
        tbnz            w0, #0, 1f              // none of the 16 pixel pairs should be updated in this case
        mla             v1.8h, v0.8h, v6.8h     // invert d[8..15] depending on clip_sign[8..15] & a0_sign[8..15], or zero it if they match, and accumulate into P5[8..15]
        sqxtun          v0.8b, v24.8h
        sqxtun2         v2.16b, v18.8h
        sqxtun2         v0.16b, v1.8h
        st1             {v2.16b}, [x3], x1
        st1             {v0.16b}, [x3]
1:      ret
endfunc

// VC-1 in-loop deblocking filter for 16 pixel pairs at boundary of horizontally-neighbouring blocks
// On entry:
//   x0 -> top-left pel of right block
//   x1 = row stride, bytes
//   w2 = PQUANT bitstream parameter
function ff_vc1_h_loop_filter16_neon, export=1
        sub             x3, x0, #4              // where to start reading
        ldr             d0, .Lcoeffs
        ld1             {v1.8b}, [x3], x1       // P1[0], P2[0]...
        sub             x0, x0, #1              // where to start writing
        ld1             {v2.8b}, [x3], x1
        add             x4, x0, x1, lsl #3
        ld1             {v3.8b}, [x3], x1
        add             x5, x0, x1, lsl #2
        ld1             {v4.8b}, [x3], x1
        add             x6, x4, x1, lsl #2
        ld1             {v5.8b}, [x3], x1
        ld1             {v6.8b}, [x3], x1
        ld1             {v7.8b}, [x3], x1
        trn1            v16.8b, v1.8b, v2.8b    // P1[0], P1[1], P3[0]...
        ld1             {v17.8b}, [x3], x1
        trn2            v1.8b, v1.8b, v2.8b     // P2[0], P2[1], P4[0]...
        ld1             {v2.8b}, [x3], x1
        trn1            v18.8b, v3.8b, v4.8b    // P1[2], P1[3], P3[2]...
        ld1             {v19.8b}, [x3], x1
        trn2            v3.8b, v3.8b, v4.8b     // P2[2], P2[3], P4[2]...
        ld1             {v4.8b}, [x3], x1
        trn1            v20.8b, v5.8b, v6.8b    // P1[4], P1[5], P3[4]...
        ld1             {v21.8b}, [x3], x1
        trn2            v5.8b, v5.8b, v6.8b     // P2[4], P2[5], P4[4]...
        ld1             {v6.8b}, [x3], x1
        trn1            v22.8b, v7.8b, v17.8b   // P1[6], P1[7], P3[6]...
        ld1             {v23.8b}, [x3], x1
        trn2            v7.8b, v7.8b, v17.8b    // P2[6], P2[7], P4[6]...
        ld1             {v17.8b}, [x3], x1
        trn1            v24.8b, v2.8b, v19.8b   // P1[8], P1[9], P3[8]...
        ld1             {v25.8b}, [x3]
        trn2            v2.8b, v2.8b, v19.8b    // P2[8], P2[9], P4[8]...
        trn1            v19.4h, v16.4h, v18.4h  // P1[0], P1[1], P1[2], P1[3], P5[0]...
        trn1            v26.8b, v4.8b, v21.8b   // P1[10], P1[11], P3[10]...
        trn2            v4.8b, v4.8b, v21.8b    // P2[10], P2[11], P4[10]...
        trn1            v21.4h, v1.4h, v3.4h    // P2[0], P2[1], P2[2], P2[3], P6[0]...
        trn1            v27.4h, v20.4h, v22.4h  // P1[4], P1[5], P1[6], P1[7], P5[4]...
        trn1            v28.8b, v6.8b, v23.8b   // P1[12], P1[13], P3[12]...
        trn2            v6.8b, v6.8b, v23.8b    // P2[12], P2[13], P4[12]...
        trn1            v23.4h, v5.4h, v7.4h    // P2[4], P2[5], P2[6], P2[7], P6[4]...
        trn1            v29.4h, v24.4h, v26.4h  // P1[8], P1[9], P1[10], P1[11], P5[8]...
        trn1            v30.8b, v17.8b, v25.8b  // P1[14], P1[15], P3[14]...
        trn2            v17.8b, v17.8b, v25.8b  // P2[14], P2[15], P4[14]...
        trn1            v25.4h, v2.4h, v4.4h    // P2[8], P2[9], P2[10], P2[11], P6[8]...
        trn1            v31.2s, v19.2s, v27.2s  // P1[0..7]
        trn2            v19.2s, v19.2s, v27.2s  // P5[0..7]
        trn1            v27.2s, v21.2s, v23.2s  // P2[0..7]
        trn2            v21.2s, v21.2s, v23.2s  // P6[0..7]
        trn1            v23.4h, v28.4h, v30.4h  // P1[12], P1[13], P1[14], P1[15], P5[12]...
        trn2            v16.4h, v16.4h, v18.4h  // P3[0], P3[1], P3[2], P3[3], P7[0]...
        trn1            v18.4h, v6.4h, v17.4h   // P2[12], P2[13], P2[14], P2[15], P6[12]...
        trn2            v20.4h, v20.4h, v22.4h  // P3[4], P3[5], P3[6], P3[7], P7[4]...
        trn2            v22.4h, v24.4h, v26.4h  // P3[8], P3[9], P3[10], P3[11], P7[8]...
        trn1            v24.2s, v29.2s, v23.2s  // P1[8..15]
        trn2            v23.2s, v29.2s, v23.2s  // P5[8..15]
        trn1            v26.2s, v25.2s, v18.2s  // P2[8..15]
        trn2            v18.2s, v25.2s, v18.2s  // P6[8..15]
        trn2            v25.4h, v28.4h, v30.4h  // P3[12], P3[13], P3[14], P3[15], P7[12]...
        trn2            v1.4h, v1.4h, v3.4h     // P4[0], P4[1], P4[2], P4[3], P8[0]...
        trn2            v3.4h, v5.4h, v7.4h     // P4[4], P4[5], P4[6], P4[7], P8[4]...
        trn2            v2.4h, v2.4h, v4.4h     // P4[8], P4[9], P4[10], P4[11], P8[8]...
        trn2            v4.4h, v6.4h, v17.4h    // P4[12], P4[13], P4[14], P4[15], P8[12]...
        ushll           v5.8h, v31.8b, #1       // 2*P1[0..7]
        ushll           v6.8h, v19.8b, #1       // 2*P5[0..7]
        trn1            v7.2s, v16.2s, v20.2s   // P3[0..7]
        uxtl            v17.8h, v27.8b          // P2[0..7]
        trn2            v16.2s, v16.2s, v20.2s  // P7[0..7]
        uxtl            v20.8h, v21.8b          // P6[0..7]
        trn1            v21.2s, v22.2s, v25.2s  // P3[8..15]
        ushll           v24.8h, v24.8b, #1      // 2*P1[8..15]
        trn2            v22.2s, v22.2s, v25.2s  // P7[8..15]
        ushll           v25.8h, v23.8b, #1      // 2*P5[8..15]
        trn1            v27.2s, v1.2s, v3.2s    // P4[0..7]
        uxtl            v26.8h, v26.8b          // P2[8..15]
        mls             v5.8h, v17.8h, v0.h[1]  // 2*P1[0..7]-5*P2[0..7]
        uxtl            v17.8h, v18.8b          // P6[8..15]
        mls             v6.8h, v20.8h, v0.h[1]  // 2*P5[0..7]-5*P6[0..7]
        trn1            v18.2s, v2.2s, v4.2s    // P4[8..15]
        uxtl            v28.8h, v7.8b           // P3[0..7]
        mls             v24.8h, v26.8h, v0.h[1] // 2*P1[8..15]-5*P2[8..15]
        uxtl            v16.8h, v16.8b          // P7[0..7]
        uxtl            v26.8h, v21.8b          // P3[8..15]
        mls             v25.8h, v17.8h, v0.h[1] // 2*P5[8..15]-5*P6[8..15]
        uxtl            v22.8h, v22.8b          // P7[8..15]
        ushll           v7.8h, v7.8b, #1        // 2*P3[0..7]
        uxtl            v27.8h, v27.8b          // P4[0..7]
        trn2            v1.2s, v1.2s, v3.2s     // P8[0..7]
        ushll           v3.8h, v21.8b, #1       // 2*P3[8..15]
        trn2            v2.2s, v2.2s, v4.2s     // P8[8..15]
        uxtl            v4.8h, v18.8b           // P4[8..15]
        mla             v5.8h, v28.8h, v0.h[1]  // 2*P1[0..7]-5*P2[0..7]+5*P3[0..7]
        uxtl            v1.8h, v1.8b            // P8[0..7]
        mla             v6.8h, v16.8h, v0.h[1]  // 2*P5[0..7]-5*P6[0..7]+5*P7[0..7]
        uxtl            v2.8h, v2.8b            // P8[8..15]
        uxtl            v16.8h, v19.8b          // P5[0..7]
        mla             v24.8h, v26.8h, v0.h[1] // 2*P1[8..15]-5*P2[8..15]+5*P3[8..15]
        uxtl            v18.8h, v23.8b          // P5[8..15]
        dup             v19.8h, w2              // pq
        mla             v25.8h, v22.8h, v0.h[1] // 2*P5[8..15]-5*P6[8..15]+5*P7[8..15]
        sub             v21.8h, v27.8h, v16.8h  // P4[0..7]-P5[0..7]
        sub             v22.8h, v4.8h, v18.8h   // P4[8..15]-P5[8..15]
        mls             v7.8h, v27.8h, v0.h[1]  // 2*P3[0..7]-5*P4[0..7]
        abs             v23.8h, v21.8h
        mls             v3.8h, v4.8h, v0.h[1]   // 2*P3[8..15]-5*P4[8..15]
        abs             v26.8h, v22.8h
        sshr            v21.8h, v21.8h, #8      // clip_sign[0..7]
        mls             v5.8h, v27.8h, v0.h[0]  // 2*P1[0..7]-5*P2[0..7]+5*P3[0..7]-2*P4[0..7]
        sshr            v23.8h, v23.8h, #1      // clip[0..7]
        sshr            v26.8h, v26.8h, #1      // clip[8..15]
        mls             v6.8h, v1.8h, v0.h[0]   // 2*P5[0..7]-5*P6[0..7]+5*P7[0..7]-2*P8[0..7]
        sshr            v1.8h, v22.8h, #8       // clip_sign[8..15]
        cmeq            v22.8h, v23.8h, #0      // test clip[0..7] == 0
        mls             v24.8h, v4.8h, v0.h[0]  // 2*P1[8..15]-5*P2[8..15]+5*P3[8..15]-2*P4[8..15]
        cmeq            v28.8h, v26.8h, #0      // test clip[8..15] == 0
        srshr           v5.8h, v5.8h, #3
        mls             v25.8h, v2.8h, v0.h[0]  // 2*P5[8..15]-5*P6[8..15]+5*P7[8..15]-2*P8[8..15]
        srshr           v2.8h, v6.8h, #3
        mla             v7.8h, v16.8h, v0.h[1]  // 2*P3[0..7]-5*P4[0..7]+5*P5[0..7]
        srshr           v6.8h, v24.8h, #3
        mla             v3.8h, v18.8h, v0.h[1]  // 2*P3[8..15]-5*P4[8..15]+5*P5[8..15]
        abs             v5.8h, v5.8h            // a1[0..7]
        srshr           v24.8h, v25.8h, #3
        mls             v3.8h, v17.8h, v0.h[0]  // 2*P3[8..15]-5*P4[8..15]+5*P5[8..15]-2*P6[8..15]
        abs             v2.8h, v2.8h            // a2[0..7]
        abs             v6.8h, v6.8h            // a1[8..15]
        mls             v7.8h, v20.8h, v0.h[0]  // 2*P3[0..7]-5*P4[0..7]+5*P5[0..7]-2*P6[0..7]
        abs             v17.8h, v24.8h          // a2[8..15]
        cmhs            v20.8h, v5.8h, v2.8h    // test a1[0..7] >= a2[0..7]
        srshr           v3.8h, v3.8h, #3
        cmhs            v24.8h, v6.8h, v17.8h   // test a1[8..15] >= a2[8.15]
        srshr           v7.8h, v7.8h, #3
        bsl             v20.16b, v2.16b, v5.16b // a3[0..7]
        abs             v2.8h, v3.8h            // a0[8..15]
        sshr            v3.8h, v3.8h, #8        // a0_sign[8..15]
        bsl             v24.16b, v17.16b, v6.16b // a3[8..15]
        abs             v5.8h, v7.8h            // a0[0..7]
        sshr            v6.8h, v7.8h, #8        // a0_sign[0..7]
        cmhs            v7.8h, v2.8h, v19.8h    // test a0[8..15] >= pq
        sub             v1.8h, v1.8h, v3.8h     // clip_sign[8..15] - a0_sign[8..15]
        uqsub           v3.8h, v2.8h, v24.8h    // a0[8..15] >= a3[8..15] ? a0[8..15]-a3[8..15] : 0  (a0 > a3 in all cases where filtering is enabled, so makes more sense to subtract this way round than the opposite and then taking the abs)
        cmhs            v2.8h, v24.8h, v2.8h    // test a3[8..15] >= a0[8..15]
        uqsub           v17.8h, v5.8h, v20.8h   // a0[0..7] >= a3[0..7] ? a0[0..7]-a3[0..7] : 0  (a0 > a3 in all cases where filtering is enabled, so makes more sense to subtract this way round than the opposite and then taking the abs)
        cmhs            v19.8h, v5.8h, v19.8h   // test a0[0..7] >= pq
        orr             v7.16b, v28.16b, v7.16b // test clip[8..15] == 0 || a0[8..15] >= pq
        sub             v6.8h, v21.8h, v6.8h    // clip_sign[0..7] - a0_sign[0..7]
        mul             v3.8h, v3.8h, v0.h[1]   // a0[8..15] >= a3[8..15] ? 5*(a0[8..15]-a3[8..15]) : 0
        cmhs            v5.8h, v20.8h, v5.8h    // test a3[0..7] >= a0[0..7]
        orr             v19.16b, v22.16b, v19.16b // test clip[0..7] == 0 || a0[0..7] >= pq
        mul             v0.8h, v17.8h, v0.h[1]  // a0[0..7] >= a3[0..7] ? 5*(a0[0..7]-a3[0..7]) : 0
        orr             v2.16b, v7.16b, v2.16b  // test clip[8..15] == 0 || a0[8..15] >= pq || a3[8..15] >= a0[8..15]
        orr             v5.16b, v19.16b, v5.16b // test clip[0..7] == 0 || a0[0..7] >= pq || a3[0..7] >= a0[0..7]
        ushr            v3.8h, v3.8h, #3        // a0[8..15] >= a3[8..15] ? (5*(a0[8..15]-a3[8..15]))>>3 : 0
        mov             w7, v2.s[1]
        mov             w8, v2.s[3]
        ushr            v0.8h, v0.8h, #3        // a0[0..7] >= a3[0..7] ? (5*(a0[0..7]-a3[0..7]))>>3 : 0
        mov             w2, v5.s[1]             // move to gp reg
        cmhs            v2.8h, v3.8h, v26.8h
        mov             w3, v5.s[3]
        cmhs            v5.8h, v0.8h, v23.8h
        bsl             v2.16b, v26.16b, v3.16b // FFMIN(d[8..15], clip[8..15])
        and             w9, w7, w8
        bsl             v5.16b, v23.16b, v0.16b // FFMIN(d[0..7], clip[0..7])
        and             w10, w2, w3
        bic             v0.16b, v2.16b, v7.16b  // set each d[8..15] to zero if it should not be filtered because clip[8..15] == 0 || a0[8..15] >= pq (a3 > a0 case already zeroed by saturating sub)
        and             w9, w10, w9
        bic             v2.16b, v5.16b, v19.16b // set each d[0..7] to zero if it should not be filtered because clip[0..7] == 0 || a0[0..7] >= pq (a3 > a0 case already zeroed by saturating sub)
        mls             v4.8h, v0.8h, v1.8h     // invert d[8..15] depending on clip_sign[8..15] & a0_sign[8..15], or zero it if they match, and accumulate into P4
        tbnz            w9, #0, 4f              // none of the 16 pixel pairs should be updated in this case
        mls             v27.8h, v2.8h, v6.8h    // invert d[0..7] depending on clip_sign[0..7] & a0_sign[0..7], or zero it if they match, and accumulate into P4
        mla             v16.8h, v2.8h, v6.8h    // invert d[0..7] depending on clip_sign[0..7] & a0_sign[0..7], or zero it if they match, and accumulate into P5
        sqxtun          v2.8b, v4.8h
        mla             v18.8h, v0.8h, v1.8h    // invert d[8..15] depending on clip_sign[8..15] & a0_sign[8..15], or zero it if they match, and accumulate into P5
        sqxtun          v0.8b, v27.8h
        sqxtun          v1.8b, v16.8h
        sqxtun          v3.8b, v18.8h
        tbnz            w2, #0, 1f
        st2             {v0.b, v1.b}[0], [x0], x1
        st2             {v0.b, v1.b}[1], [x0], x1
        st2             {v0.b, v1.b}[2], [x0], x1
        st2             {v0.b, v1.b}[3], [x0]
1:      tbnz            w3, #0, 2f
        st2             {v0.b, v1.b}[4], [x5], x1
        st2             {v0.b, v1.b}[5], [x5], x1
        st2             {v0.b, v1.b}[6], [x5], x1
        st2             {v0.b, v1.b}[7], [x5]
2:      tbnz            w7, #0, 3f
        st2             {v2.b, v3.b}[0], [x4], x1
        st2             {v2.b, v3.b}[1], [x4], x1
        st2             {v2.b, v3.b}[2], [x4], x1
        st2             {v2.b, v3.b}[3], [x4]
3:      tbnz            w8, #0, 4f
        st2             {v2.b, v3.b}[4], [x6], x1
        st2             {v2.b, v3.b}[5], [x6], x1
        st2             {v2.b, v3.b}[6], [x6], x1
        st2             {v2.b, v3.b}[7], [x6]
4:      ret
endfunc

// Copy at most the specified number of bytes from source to destination buffer,
// stopping at a multiple of 32 bytes, none of which are the start of an escape sequence
// On entry:
//   x0 -> source buffer
//   w1 = max number of bytes to copy
//   x2 -> destination buffer, optimally 8-byte aligned
// On exit:
//   w0 = number of bytes not copied
function ff_vc1_unescape_buffer_helper_neon, export=1
        // Offset by 80 to screen out cases that are too short for us to handle,
        // and also make it easy to test for loop termination, or to determine
        // whether we need an odd number of half-iterations of the loop.
        subs            w1, w1, #80
        b.mi            90f

        // Set up useful constants
        movi            v20.4s, #3, lsl #24
        movi            v21.4s, #3, lsl #16

        tst             w1, #32
        b.ne            1f

          ld1             {v0.16b, v1.16b, v2.16b}, [x0], #48
          ext             v25.16b, v0.16b, v1.16b, #1
          ext             v26.16b, v0.16b, v1.16b, #2
          ext             v27.16b, v0.16b, v1.16b, #3
          ext             v29.16b, v1.16b, v2.16b, #1
          ext             v30.16b, v1.16b, v2.16b, #2
          ext             v31.16b, v1.16b, v2.16b, #3
          bic             v24.16b, v0.16b, v20.16b
          bic             v25.16b, v25.16b, v20.16b
          bic             v26.16b, v26.16b, v20.16b
          bic             v27.16b, v27.16b, v20.16b
          bic             v28.16b, v1.16b, v20.16b
          bic             v29.16b, v29.16b, v20.16b
          bic             v30.16b, v30.16b, v20.16b
          bic             v31.16b, v31.16b, v20.16b
          eor             v24.16b, v24.16b, v21.16b
          eor             v25.16b, v25.16b, v21.16b
          eor             v26.16b, v26.16b, v21.16b
          eor             v27.16b, v27.16b, v21.16b
          eor             v28.16b, v28.16b, v21.16b
          eor             v29.16b, v29.16b, v21.16b
          eor             v30.16b, v30.16b, v21.16b
          eor             v31.16b, v31.16b, v21.16b
          cmeq            v24.4s, v24.4s, #0
          cmeq            v25.4s, v25.4s, #0
          cmeq            v26.4s, v26.4s, #0
          cmeq            v27.4s, v27.4s, #0
          add             w1, w1, #32
          b               3f

1:      ld1             {v3.16b, v4.16b, v5.16b}, [x0], #48
        ext             v25.16b, v3.16b, v4.16b, #1
        ext             v26.16b, v3.16b, v4.16b, #2
        ext             v27.16b, v3.16b, v4.16b, #3
        ext             v29.16b, v4.16b, v5.16b, #1
        ext             v30.16b, v4.16b, v5.16b, #2
        ext             v31.16b, v4.16b, v5.16b, #3
        bic             v24.16b, v3.16b, v20.16b
        bic             v25.16b, v25.16b, v20.16b
        bic             v26.16b, v26.16b, v20.16b
        bic             v27.16b, v27.16b, v20.16b
        bic             v28.16b, v4.16b, v20.16b
        bic             v29.16b, v29.16b, v20.16b
        bic             v30.16b, v30.16b, v20.16b
        bic             v31.16b, v31.16b, v20.16b
        eor             v24.16b, v24.16b, v21.16b
        eor             v25.16b, v25.16b, v21.16b
        eor             v26.16b, v26.16b, v21.16b
        eor             v27.16b, v27.16b, v21.16b
        eor             v28.16b, v28.16b, v21.16b
        eor             v29.16b, v29.16b, v21.16b
        eor             v30.16b, v30.16b, v21.16b
        eor             v31.16b, v31.16b, v21.16b
        cmeq            v24.4s, v24.4s, #0
        cmeq            v25.4s, v25.4s, #0
        cmeq            v26.4s, v26.4s, #0
        cmeq            v27.4s, v27.4s, #0
        // Drop through...
2:        mov             v0.16b, v5.16b
          ld1             {v1.16b, v2.16b}, [x0], #32
        cmeq            v28.4s, v28.4s, #0
        cmeq            v29.4s, v29.4s, #0
        cmeq            v30.4s, v30.4s, #0
        cmeq            v31.4s, v31.4s, #0
        orr             v24.16b, v24.16b, v25.16b
        orr             v26.16b, v26.16b, v27.16b
        orr             v28.16b, v28.16b, v29.16b
        orr             v30.16b, v30.16b, v31.16b
          ext             v25.16b, v0.16b, v1.16b, #1
        orr             v22.16b, v24.16b, v26.16b
          ext             v26.16b, v0.16b, v1.16b, #2
          ext             v27.16b, v0.16b, v1.16b, #3
          ext             v29.16b, v1.16b, v2.16b, #1
        orr             v23.16b, v28.16b, v30.16b
          ext             v30.16b, v1.16b, v2.16b, #2
          ext             v31.16b, v1.16b, v2.16b, #3
          bic             v24.16b, v0.16b, v20.16b
          bic             v25.16b, v25.16b, v20.16b
          bic             v26.16b, v26.16b, v20.16b
        orr             v22.16b, v22.16b, v23.16b
          bic             v27.16b, v27.16b, v20.16b
          bic             v28.16b, v1.16b, v20.16b
          bic             v29.16b, v29.16b, v20.16b
          bic             v30.16b, v30.16b, v20.16b
          bic             v31.16b, v31.16b, v20.16b
        addv            s22, v22.4s
          eor             v24.16b, v24.16b, v21.16b
          eor             v25.16b, v25.16b, v21.16b
          eor             v26.16b, v26.16b, v21.16b
          eor             v27.16b, v27.16b, v21.16b
          eor             v28.16b, v28.16b, v21.16b
        mov             w3, v22.s[0]
          eor             v29.16b, v29.16b, v21.16b
          eor             v30.16b, v30.16b, v21.16b
          eor             v31.16b, v31.16b, v21.16b
          cmeq            v24.4s, v24.4s, #0
          cmeq            v25.4s, v25.4s, #0
          cmeq            v26.4s, v26.4s, #0
          cmeq            v27.4s, v27.4s, #0
        cbnz            w3, 90f
        st1             {v3.16b, v4.16b}, [x2], #32
3:          mov             v3.16b, v2.16b
            ld1             {v4.16b, v5.16b}, [x0], #32
          cmeq            v28.4s, v28.4s, #0
          cmeq            v29.4s, v29.4s, #0
          cmeq            v30.4s, v30.4s, #0
          cmeq            v31.4s, v31.4s, #0
          orr             v24.16b, v24.16b, v25.16b
          orr             v26.16b, v26.16b, v27.16b
          orr             v28.16b, v28.16b, v29.16b
          orr             v30.16b, v30.16b, v31.16b
            ext             v25.16b, v3.16b, v4.16b, #1
          orr             v22.16b, v24.16b, v26.16b
            ext             v26.16b, v3.16b, v4.16b, #2
            ext             v27.16b, v3.16b, v4.16b, #3
            ext             v29.16b, v4.16b, v5.16b, #1
          orr             v23.16b, v28.16b, v30.16b
            ext             v30.16b, v4.16b, v5.16b, #2
            ext             v31.16b, v4.16b, v5.16b, #3
            bic             v24.16b, v3.16b, v20.16b
            bic             v25.16b, v25.16b, v20.16b
            bic             v26.16b, v26.16b, v20.16b
          orr             v22.16b, v22.16b, v23.16b
            bic             v27.16b, v27.16b, v20.16b
            bic             v28.16b, v4.16b, v20.16b
            bic             v29.16b, v29.16b, v20.16b
            bic             v30.16b, v30.16b, v20.16b
            bic             v31.16b, v31.16b, v20.16b
          addv            s22, v22.4s
            eor             v24.16b, v24.16b, v21.16b
            eor             v25.16b, v25.16b, v21.16b
            eor             v26.16b, v26.16b, v21.16b
            eor             v27.16b, v27.16b, v21.16b
            eor             v28.16b, v28.16b, v21.16b
          mov             w3, v22.s[0]
            eor             v29.16b, v29.16b, v21.16b
            eor             v30.16b, v30.16b, v21.16b
            eor             v31.16b, v31.16b, v21.16b
            cmeq            v24.4s, v24.4s, #0
            cmeq            v25.4s, v25.4s, #0
            cmeq            v26.4s, v26.4s, #0
            cmeq            v27.4s, v27.4s, #0
          cbnz            w3, 91f
          st1             {v0.16b, v1.16b}, [x2], #32
        subs            w1, w1, #64
        b.pl            2b

90:     add             w0, w1, #80
        ret

91:     sub             w1, w1, #32
        b               90b
endfunc
