/*
 * Loongson SIMD optimized vp8dsp
 *
 * Copyright (c) 2016 Loongson Technology Corporation Limited
 * Copyright (c) 2016 Zhou Xiaoyong <zhouxiaoyong@loongson.cn>
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

#include "vp8dsp_mips.h"
#include "constants.h"
#include "libavutil/mips/mmiutils.h"

#define PUT_VP8_EPEL4_H6_MMI(src, dst)                                      \
        MMI_ULWC1(%[ftmp1], src, 0x00)                                      \
        "punpcklbh  %[ftmp2],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "pmullh     %[ftmp3],   %[ftmp2],       %[filter2]          \n\t"   \
                                                                            \
        MMI_ULWC1(%[ftmp1], src, -0x01)                                     \
        "punpcklbh  %[ftmp2],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "pmullh     %[ftmp2],   %[ftmp2],       %[filter1]          \n\t"   \
        "psubsh     %[ftmp3],   %[ftmp3],       %[ftmp2]            \n\t"   \
                                                                            \
        MMI_ULWC1(%[ftmp1], src, -0x02)                                     \
        "punpcklbh  %[ftmp2],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "pmullh     %[ftmp2],   %[ftmp2],       %[filter0]          \n\t"   \
        "paddsh     %[ftmp5],   %[ftmp3],       %[ftmp2]            \n\t"   \
                                                                            \
        MMI_ULWC1(%[ftmp1], src, 0x01)                                      \
        "punpcklbh  %[ftmp2],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "pmullh     %[ftmp3],   %[ftmp2],       %[filter3]          \n\t"   \
                                                                            \
        MMI_ULWC1(%[ftmp1], src, 0x02)                                      \
        "punpcklbh  %[ftmp2],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "pmullh     %[ftmp2],   %[ftmp2],       %[filter4]          \n\t"   \
        "psubsh     %[ftmp3],   %[ftmp3],       %[ftmp2]            \n\t"   \
                                                                            \
        MMI_ULWC1(%[ftmp1], src, 0x03)                                      \
        "punpcklbh  %[ftmp2],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "pmullh     %[ftmp2],   %[ftmp2],       %[filter5]          \n\t"   \
        "paddsh     %[ftmp3],   %[ftmp3],       %[ftmp2]            \n\t"   \
                                                                            \
        "paddsh     %[ftmp3],   %[ftmp3],       %[ftmp5]            \n\t"   \
        "paddsh     %[ftmp3],   %[ftmp3],       %[ff_pw_64]         \n\t"   \
        "psrah      %[ftmp3],   %[ftmp3],       %[ftmp4]            \n\t"   \
        "packushb   %[ftmp1],   %[ftmp3],       %[ftmp0]            \n\t"   \
                                                                            \
        MMI_SWC1(%[ftmp1], dst, 0x00)


#define PUT_VP8_EPEL4_H4_MMI(src, dst)                                      \
        MMI_ULWC1(%[ftmp1], src, 0x00)                                      \
        "punpcklbh  %[ftmp2],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "pmullh     %[ftmp3],   %[ftmp2],       %[filter2]          \n\t"   \
                                                                            \
        MMI_ULWC1(%[ftmp1], src, -0x01)                                     \
        "punpcklbh  %[ftmp2],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "pmullh     %[ftmp2],   %[ftmp2],       %[filter1]          \n\t"   \
        "psubsh     %[ftmp5],   %[ftmp3],       %[ftmp2]            \n\t"   \
                                                                            \
        MMI_ULWC1(%[ftmp1], src, 0x01)                                      \
        "punpcklbh  %[ftmp2],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "pmullh     %[ftmp3],   %[ftmp2],       %[filter3]          \n\t"   \
                                                                            \
        MMI_ULWC1(%[ftmp1], src, 0x02)                                      \
        "punpcklbh  %[ftmp2],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "pmullh     %[ftmp2],   %[ftmp2],       %[filter4]          \n\t"   \
        "psubh      %[ftmp3],   %[ftmp3],       %[ftmp2]            \n\t"   \
                                                                            \
        "paddsh     %[ftmp3],   %[ftmp3],       %[ftmp5]            \n\t"   \
                                                                            \
        "paddsh     %[ftmp3],   %[ftmp3],       %[ff_pw_64]         \n\t"   \
        "psrah      %[ftmp3],   %[ftmp3],       %[ftmp4]            \n\t"   \
                                                                            \
        "packushb   %[ftmp1],   %[ftmp3],       %[ftmp0]            \n\t"   \
        MMI_SWC1(%[ftmp1], dst, 0x00)


#define PUT_VP8_EPEL4_V6_MMI(src, src1, dst, srcstride)                     \
        MMI_ULWC1(%[ftmp1], src, 0x00)                                      \
        "punpcklbh  %[ftmp2],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "pmullh     %[ftmp3],   %[ftmp2],       %[filter2]          \n\t"   \
                                                                            \
        PTR_SUBU   ""#src1",    "#src",         "#srcstride"        \n\t"   \
        MMI_ULWC1(%[ftmp1], src1, 0x00)                                     \
        "punpcklbh  %[ftmp2],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "pmullh     %[ftmp2],   %[ftmp2],       %[filter1]          \n\t"   \
        "psubsh     %[ftmp3],   %[ftmp3],       %[ftmp2]            \n\t"   \
                                                                            \
        PTR_SUBU   ""#src1",    "#src1",        "#srcstride"        \n\t"   \
        MMI_ULWC1(%[ftmp1], src1, 0x00)                                     \
        "punpcklbh  %[ftmp2],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "pmullh     %[ftmp2],   %[ftmp2],       %[filter0]          \n\t"   \
        "paddsh     %[ftmp5],   %[ftmp3],       %[ftmp2]            \n\t"   \
                                                                            \
        PTR_ADDU   ""#src1",    "#src",         "#srcstride"        \n\t"   \
        MMI_ULWC1(%[ftmp1], src1, 0x00)                                     \
        "punpcklbh  %[ftmp2],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "pmullh     %[ftmp3],   %[ftmp2],       %[filter3]          \n\t"   \
                                                                            \
        PTR_ADDU   ""#src1",    "#src1",        "#srcstride"        \n\t"   \
        MMI_ULWC1(%[ftmp1], src1, 0x00)                                     \
        "punpcklbh  %[ftmp2],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "pmullh     %[ftmp2],   %[ftmp2],       %[filter4]          \n\t"   \
        "psubsh     %[ftmp3],   %[ftmp3],       %[ftmp2]            \n\t"   \
                                                                            \
        PTR_ADDU   ""#src1",    "#src1",        "#srcstride"        \n\t"   \
        MMI_ULWC1(%[ftmp1], src1, 0x00)                                     \
        "punpcklbh  %[ftmp2],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "pmullh     %[ftmp2],   %[ftmp2],       %[filter5]          \n\t"   \
        "paddsh     %[ftmp3],   %[ftmp3],       %[ftmp2]            \n\t"   \
                                                                            \
        "paddsh     %[ftmp3],   %[ftmp3],       %[ftmp5]            \n\t"   \
                                                                            \
        "paddsh     %[ftmp3],   %[ftmp3],       %[ff_pw_64]         \n\t"   \
        "psrah      %[ftmp3],   %[ftmp3],       %[ftmp4]            \n\t"   \
        "packushb   %[ftmp1],   %[ftmp3],       %[ftmp0]            \n\t"   \
                                                                            \
        MMI_SWC1(%[ftmp1], dst, 0x00)


#define PUT_VP8_EPEL4_V4_MMI(src, src1, dst, srcstride)                     \
        MMI_ULWC1(%[ftmp1], src, 0x00)                                      \
        "punpcklbh  %[ftmp2],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "pmullh     %[ftmp3],   %[ftmp2],       %[filter2]          \n\t"   \
                                                                            \
        PTR_SUBU   ""#src1",    "#src",         "#srcstride"        \n\t"   \
        MMI_ULWC1(%[ftmp1], src1, 0x00)                                     \
        "punpcklbh  %[ftmp2],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "pmullh     %[ftmp2],   %[ftmp2],       %[filter1]          \n\t"   \
        "psubsh     %[ftmp5],   %[ftmp3],       %[ftmp2]            \n\t"   \
                                                                            \
        PTR_ADDU   ""#src1",    "#src",         "#srcstride"        \n\t"   \
        MMI_ULWC1(%[ftmp1], src1, 0x00)                                     \
        "punpcklbh  %[ftmp2],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "pmullh     %[ftmp3],   %[ftmp2],       %[filter3]          \n\t"   \
                                                                            \
        PTR_ADDU   ""#src1",    "#src1",        "#srcstride"        \n\t"   \
        MMI_ULWC1(%[ftmp1], src1, 0x00)                                     \
        "punpcklbh  %[ftmp2],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "pmullh     %[ftmp2],   %[ftmp2],       %[filter4]          \n\t"   \
        "psubsh     %[ftmp3],   %[ftmp3],       %[ftmp2]            \n\t"   \
                                                                            \
        "paddsh     %[ftmp3],   %[ftmp3],       %[ftmp5]            \n\t"   \
                                                                            \
        "paddsh     %[ftmp3],   %[ftmp3],       %[ff_pw_64]         \n\t"   \
        "psrah      %[ftmp3],   %[ftmp3],       %[ftmp4]            \n\t"   \
        "packushb   %[ftmp1],   %[ftmp3],       %[ftmp0]            \n\t"   \
                                                                            \
        MMI_SWC1(%[ftmp1], dst, 0x00)


#define PUT_VP8_EPEL8_H6_MMI(src, dst)                                      \
        MMI_ULDC1(%[ftmp1], src, 0x00)                                      \
        "punpcklbh  %[ftmp2],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "punpckhbh  %[ftmp3],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "pmullh     %[ftmp5],   %[ftmp2],       %[filter2]          \n\t"   \
        "pmullh     %[ftmp6],   %[ftmp3],       %[filter2]          \n\t"   \
                                                                            \
        MMI_ULDC1(%[ftmp1], src, -0x01)                                     \
        "punpcklbh  %[ftmp2],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "punpckhbh  %[ftmp3],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "pmullh     %[ftmp2],   %[ftmp2],       %[filter1]          \n\t"   \
        "pmullh     %[ftmp3],   %[ftmp3],       %[filter1]          \n\t"   \
        "psubsh     %[ftmp5],   %[ftmp5],       %[ftmp2]            \n\t"   \
        "psubsh     %[ftmp6],   %[ftmp6],       %[ftmp3]            \n\t"   \
                                                                            \
        MMI_ULDC1(%[ftmp1], src, -0x02)                                     \
        "punpcklbh  %[ftmp2],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "punpckhbh  %[ftmp3],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "pmullh     %[ftmp2],   %[ftmp2],       %[filter0]          \n\t"   \
        "pmullh     %[ftmp3],   %[ftmp3],       %[filter0]          \n\t"   \
        "paddsh     %[ftmp7],   %[ftmp5],       %[ftmp2]            \n\t"   \
        "paddsh     %[ftmp8],   %[ftmp6],       %[ftmp3]            \n\t"   \
                                                                            \
        MMI_ULDC1(%[ftmp1], src, 0x01)                                      \
        "punpcklbh  %[ftmp2],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "punpckhbh  %[ftmp3],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "pmullh     %[ftmp5],   %[ftmp2],       %[filter3]          \n\t"   \
        "pmullh     %[ftmp6],   %[ftmp3],       %[filter3]          \n\t"   \
                                                                            \
        MMI_ULDC1(%[ftmp1], src, 0x02)                                      \
        "punpcklbh  %[ftmp2],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "punpckhbh  %[ftmp3],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "pmullh     %[ftmp2],   %[ftmp2],       %[filter4]          \n\t"   \
        "pmullh     %[ftmp3],   %[ftmp3],       %[filter4]          \n\t"   \
        "psubsh     %[ftmp5],   %[ftmp5],       %[ftmp2]            \n\t"   \
        "psubsh     %[ftmp6],   %[ftmp6],       %[ftmp3]            \n\t"   \
                                                                            \
        MMI_ULDC1(%[ftmp1], src, 0x03)                                      \
        "punpcklbh  %[ftmp2],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "punpckhbh  %[ftmp3],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "pmullh     %[ftmp2],   %[ftmp2],       %[filter5]          \n\t"   \
        "pmullh     %[ftmp3],   %[ftmp3],       %[filter5]          \n\t"   \
        "paddsh     %[ftmp5],   %[ftmp5],       %[ftmp2]            \n\t"   \
        "paddsh     %[ftmp6],   %[ftmp6],       %[ftmp3]            \n\t"   \
                                                                            \
        "paddsh     %[ftmp5],   %[ftmp5],       %[ftmp7]            \n\t"   \
        "paddsh     %[ftmp6],   %[ftmp6],       %[ftmp8]            \n\t"   \
                                                                            \
        "paddsh     %[ftmp5],   %[ftmp5],       %[ff_pw_64]         \n\t"   \
        "paddsh     %[ftmp6],   %[ftmp6],       %[ff_pw_64]         \n\t"   \
        "psrah      %[ftmp5],   %[ftmp5],       %[ftmp4]            \n\t"   \
        "psrah      %[ftmp6],   %[ftmp6],       %[ftmp4]            \n\t"   \
        "packushb   %[ftmp1],   %[ftmp5],       %[ftmp6]            \n\t"   \
                                                                            \
        MMI_SDC1(%[ftmp1], dst, 0x00)


#define PUT_VP8_EPEL8_H4_MMI(src, dst)                                      \
        MMI_ULDC1(%[ftmp1], src, 0x00)                                      \
        "punpcklbh  %[ftmp2],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "punpckhbh  %[ftmp3],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "pmullh     %[ftmp5],   %[ftmp2],       %[filter2]          \n\t"   \
        "pmullh     %[ftmp6],   %[ftmp3],       %[filter2]          \n\t"   \
                                                                            \
        MMI_ULDC1(%[ftmp1], src, -0x01)                                     \
        "punpcklbh  %[ftmp2],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "punpckhbh  %[ftmp3],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "pmullh     %[ftmp2],   %[ftmp2],       %[filter1]          \n\t"   \
        "pmullh     %[ftmp3],   %[ftmp3],       %[filter1]          \n\t"   \
        "psubsh     %[ftmp7],   %[ftmp5],       %[ftmp2]            \n\t"   \
        "psubsh     %[ftmp8],   %[ftmp6],       %[ftmp3]            \n\t"   \
                                                                            \
        MMI_ULDC1(%[ftmp1], src, 0x01)                                      \
        "punpcklbh  %[ftmp2],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "punpckhbh  %[ftmp3],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "pmullh     %[ftmp5],   %[ftmp2],       %[filter3]          \n\t"   \
        "pmullh     %[ftmp6],   %[ftmp3],       %[filter3]          \n\t"   \
                                                                            \
        MMI_ULDC1(%[ftmp1], src, 0x02)                                      \
        "punpcklbh  %[ftmp2],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "punpckhbh  %[ftmp3],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "pmullh     %[ftmp2],   %[ftmp2],       %[filter4]          \n\t"   \
        "pmullh     %[ftmp3],   %[ftmp3],       %[filter4]          \n\t"   \
        "psubsh     %[ftmp5],   %[ftmp5],       %[ftmp2]            \n\t"   \
        "psubsh     %[ftmp6],   %[ftmp6],       %[ftmp3]            \n\t"   \
                                                                            \
        "paddsh     %[ftmp5],   %[ftmp5],       %[ftmp7]            \n\t"   \
        "paddsh     %[ftmp6],   %[ftmp6],       %[ftmp8]            \n\t"   \
                                                                            \
        "paddsh     %[ftmp5],   %[ftmp5],       %[ff_pw_64]         \n\t"   \
        "paddsh     %[ftmp6],   %[ftmp6],       %[ff_pw_64]         \n\t"   \
        "psrah      %[ftmp5],   %[ftmp5],       %[ftmp4]            \n\t"   \
        "psrah      %[ftmp6],   %[ftmp6],       %[ftmp4]            \n\t"   \
                                                                            \
        "packushb   %[ftmp1],   %[ftmp5],       %[ftmp6]            \n\t"   \
        MMI_SDC1(%[ftmp1], dst, 0x00)


#define PUT_VP8_EPEL8_V6_MMI(src, src1, dst, srcstride)                     \
        MMI_ULDC1(%[ftmp1], src, 0x00)                                      \
        "punpcklbh  %[ftmp2],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "punpckhbh  %[ftmp3],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "pmullh     %[ftmp5],   %[ftmp2],       %[filter2]          \n\t"   \
        "pmullh     %[ftmp6],   %[ftmp3],       %[filter2]          \n\t"   \
                                                                            \
        PTR_SUBU   ""#src1",    "#src",         "#srcstride"        \n\t"   \
        MMI_ULDC1(%[ftmp1], src1, 0x00)                                     \
        "punpcklbh  %[ftmp2],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "punpckhbh  %[ftmp3],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "pmullh     %[ftmp2],   %[ftmp2],       %[filter1]          \n\t"   \
        "pmullh     %[ftmp3],   %[ftmp3],       %[filter1]          \n\t"   \
        "psubsh     %[ftmp5],   %[ftmp5],       %[ftmp2]            \n\t"   \
        "psubsh     %[ftmp6],   %[ftmp6],       %[ftmp3]            \n\t"   \
                                                                            \
        PTR_SUBU   ""#src1",    "#src1",        "#srcstride"        \n\t"   \
        MMI_ULDC1(%[ftmp1], src1, 0x00)                                     \
        "punpcklbh  %[ftmp2],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "punpckhbh  %[ftmp3],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "pmullh     %[ftmp2],   %[ftmp2],       %[filter0]          \n\t"   \
        "pmullh     %[ftmp3],   %[ftmp3],       %[filter0]          \n\t"   \
        "paddsh     %[ftmp7],   %[ftmp5],       %[ftmp2]            \n\t"   \
        "paddsh     %[ftmp8],   %[ftmp6],       %[ftmp3]            \n\t"   \
                                                                            \
        PTR_ADDU   ""#src1",    "#src",         "#srcstride"        \n\t"   \
        MMI_ULDC1(%[ftmp1], src1, 0x00)                                     \
        "punpcklbh  %[ftmp2],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "punpckhbh  %[ftmp3],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "pmullh     %[ftmp5],   %[ftmp2],       %[filter3]          \n\t"   \
        "pmullh     %[ftmp6],   %[ftmp3],       %[filter3]          \n\t"   \
                                                                            \
        PTR_ADDU   ""#src1",    "#src1",        "#srcstride"        \n\t"   \
        MMI_ULDC1(%[ftmp1], src1, 0x00)                                     \
        "punpcklbh  %[ftmp2],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "punpckhbh  %[ftmp3],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "pmullh     %[ftmp2],   %[ftmp2],       %[filter4]          \n\t"   \
        "pmullh     %[ftmp3],   %[ftmp3],       %[filter4]          \n\t"   \
        "psubsh     %[ftmp5],   %[ftmp5],       %[ftmp2]            \n\t"   \
        "psubsh     %[ftmp6],   %[ftmp6],       %[ftmp3]            \n\t"   \
                                                                            \
        PTR_ADDU   ""#src1",    "#src1",        "#srcstride"        \n\t"   \
        MMI_ULDC1(%[ftmp1], src1, 0x00)                                     \
        "punpcklbh  %[ftmp2],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "punpckhbh  %[ftmp3],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "pmullh     %[ftmp2],   %[ftmp2],       %[filter5]          \n\t"   \
        "pmullh     %[ftmp3],   %[ftmp3],       %[filter5]          \n\t"   \
        "paddsh     %[ftmp5],   %[ftmp5],       %[ftmp2]            \n\t"   \
        "paddsh     %[ftmp6],   %[ftmp6],       %[ftmp3]            \n\t"   \
                                                                            \
        "paddsh     %[ftmp5],   %[ftmp5],       %[ftmp7]            \n\t"   \
        "paddsh     %[ftmp6],   %[ftmp6],       %[ftmp8]            \n\t"   \
                                                                            \
        "paddsh     %[ftmp5],   %[ftmp5],       %[ff_pw_64]         \n\t"   \
        "paddsh     %[ftmp6],   %[ftmp6],       %[ff_pw_64]         \n\t"   \
        "psrah      %[ftmp5],   %[ftmp5],       %[ftmp4]            \n\t"   \
        "psrah      %[ftmp6],   %[ftmp6],       %[ftmp4]            \n\t"   \
        "packushb   %[ftmp1],   %[ftmp5],       %[ftmp6]            \n\t"   \
                                                                            \
        MMI_SDC1(%[ftmp1], dst, 0x00)


#define PUT_VP8_EPEL8_V4_MMI(src, src1, dst, srcstride)                     \
        MMI_ULDC1(%[ftmp1], src, 0x00)                                      \
        "punpcklbh  %[ftmp2],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "punpckhbh  %[ftmp3],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "pmullh     %[ftmp5],   %[ftmp2],       %[filter2]          \n\t"   \
        "pmullh     %[ftmp6],   %[ftmp3],       %[filter2]          \n\t"   \
                                                                            \
        PTR_SUBU   ""#src1",    "#src",         "#srcstride"        \n\t"   \
        MMI_ULDC1(%[ftmp1], src1, 0x00)                                     \
        "punpcklbh  %[ftmp2],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "punpckhbh  %[ftmp3],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "pmullh     %[ftmp2],   %[ftmp2],       %[filter1]          \n\t"   \
        "pmullh     %[ftmp3],   %[ftmp3],       %[filter1]          \n\t"   \
        "psubsh     %[ftmp7],   %[ftmp5],       %[ftmp2]            \n\t"   \
        "psubsh     %[ftmp8],   %[ftmp6],       %[ftmp3]            \n\t"   \
                                                                            \
        PTR_ADDU   ""#src1",    "#src",         "#srcstride"        \n\t"   \
        MMI_ULDC1(%[ftmp1], src1, 0x00)                                     \
        "punpcklbh  %[ftmp2],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "punpckhbh  %[ftmp3],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "pmullh     %[ftmp5],   %[ftmp2],       %[filter3]          \n\t"   \
        "pmullh     %[ftmp6],   %[ftmp3],       %[filter3]          \n\t"   \
                                                                            \
        PTR_ADDU   ""#src1",    "#src1",        "#srcstride"        \n\t"   \
        MMI_ULDC1(%[ftmp1], src1, 0x00)                                     \
        "punpcklbh  %[ftmp2],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "punpckhbh  %[ftmp3],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "pmullh     %[ftmp2],   %[ftmp2],       %[filter4]          \n\t"   \
        "pmullh     %[ftmp3],   %[ftmp3],       %[filter4]          \n\t"   \
        "psubsh     %[ftmp5],   %[ftmp5],       %[ftmp2]            \n\t"   \
        "psubsh     %[ftmp6],   %[ftmp6],       %[ftmp3]            \n\t"   \
                                                                            \
        "paddsh     %[ftmp5],   %[ftmp5],       %[ftmp7]            \n\t"   \
        "paddsh     %[ftmp6],   %[ftmp6],       %[ftmp8]            \n\t"   \
                                                                            \
        "paddsh     %[ftmp5],   %[ftmp5],       %[ff_pw_64]         \n\t"   \
        "paddsh     %[ftmp6],   %[ftmp6],       %[ff_pw_64]         \n\t"   \
        "psrah      %[ftmp5],   %[ftmp5],       %[ftmp4]            \n\t"   \
        "psrah      %[ftmp6],   %[ftmp6],       %[ftmp4]            \n\t"   \
        "packushb   %[ftmp1],   %[ftmp5],       %[ftmp6]            \n\t"   \
                                                                            \
        MMI_SDC1(%[ftmp1], dst, 0x00)


#define PUT_VP8_BILINEAR8_H_MMI(src, dst)                                   \
        MMI_ULDC1(%[ftmp1], src, 0x00)                                      \
        "punpcklbh  %[ftmp2],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "punpckhbh  %[ftmp3],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "pmullh     %[ftmp5],   %[ftmp2],       %[a]                \n\t"   \
        "pmullh     %[ftmp6],   %[ftmp3],       %[a]                \n\t"   \
                                                                            \
        MMI_ULDC1(%[ftmp1], src, 0x01)                                      \
        "punpcklbh  %[ftmp2],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "punpckhbh  %[ftmp3],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "pmullh     %[ftmp2],   %[ftmp2],       %[b]                \n\t"   \
        "pmullh     %[ftmp3],   %[ftmp3],       %[b]                \n\t"   \
        "paddsh     %[ftmp5],   %[ftmp5],       %[ftmp2]            \n\t"   \
        "paddsh     %[ftmp6],   %[ftmp6],       %[ftmp3]            \n\t"   \
                                                                            \
        "paddsh     %[ftmp5],   %[ftmp5],       %[ff_pw_4]          \n\t"   \
        "paddsh     %[ftmp6],   %[ftmp6],       %[ff_pw_4]          \n\t"   \
        "psrah      %[ftmp5],   %[ftmp5],       %[ftmp4]            \n\t"   \
        "psrah      %[ftmp6],   %[ftmp6],       %[ftmp4]            \n\t"   \
                                                                            \
        "packushb   %[ftmp1],   %[ftmp5],       %[ftmp6]            \n\t"   \
        MMI_SDC1(%[ftmp1], dst, 0x00)


#define PUT_VP8_BILINEAR4_H_MMI(src, dst)                                   \
        MMI_ULWC1(%[ftmp1], src, 0x00)                                      \
        "punpcklbh  %[ftmp2],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "pmullh     %[ftmp3],   %[ftmp2],       %[a]                \n\t"   \
                                                                            \
        MMI_ULWC1(%[ftmp1], src, 0x01)                                      \
        "punpcklbh  %[ftmp2],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "pmullh     %[ftmp2],   %[ftmp2],       %[b]                \n\t"   \
        "paddsh     %[ftmp3],   %[ftmp3],       %[ftmp2]            \n\t"   \
                                                                            \
        "paddsh     %[ftmp3],   %[ftmp3],       %[ff_pw_4]          \n\t"   \
        "psrah      %[ftmp3],   %[ftmp3],       %[ftmp4]            \n\t"   \
                                                                            \
        "packushb   %[ftmp1],   %[ftmp3],       %[ftmp0]            \n\t"   \
        MMI_SWC1(%[ftmp1], dst, 0x00)


#define PUT_VP8_BILINEAR8_V_MMI(src, src1, dst, sstride)                    \
        MMI_ULDC1(%[ftmp1], src, 0x00)                                      \
        "punpcklbh  %[ftmp2],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "punpckhbh  %[ftmp3],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "pmullh     %[ftmp5],   %[ftmp2],       %[c]                \n\t"   \
        "pmullh     %[ftmp6],   %[ftmp3],       %[c]                \n\t"   \
                                                                            \
        PTR_ADDU   ""#src1",    "#src",         "#sstride"          \n\t"   \
        MMI_ULDC1(%[ftmp1], src1, 0x00)                                     \
        "punpcklbh  %[ftmp2],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "punpckhbh  %[ftmp3],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "pmullh     %[ftmp2],   %[ftmp2],       %[d]                \n\t"   \
        "pmullh     %[ftmp3],   %[ftmp3],       %[d]                \n\t"   \
        "paddsh     %[ftmp5],   %[ftmp5],       %[ftmp2]            \n\t"   \
        "paddsh     %[ftmp6],   %[ftmp6],       %[ftmp3]            \n\t"   \
                                                                            \
        "paddsh     %[ftmp5],   %[ftmp5],       %[ff_pw_4]          \n\t"   \
        "paddsh     %[ftmp6],   %[ftmp6],       %[ff_pw_4]          \n\t"   \
        "psrah      %[ftmp5],   %[ftmp5],       %[ftmp4]            \n\t"   \
        "psrah      %[ftmp6],   %[ftmp6],       %[ftmp4]            \n\t"   \
                                                                            \
        "packushb   %[ftmp1],   %[ftmp5],       %[ftmp6]            \n\t"   \
        MMI_SDC1(%[ftmp1], dst, 0x00)


#define PUT_VP8_BILINEAR4_V_MMI(src, src1, dst, sstride)                    \
        MMI_ULWC1(%[ftmp1], src, 0x00)                                      \
        "punpcklbh  %[ftmp2],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "pmullh     %[ftmp3],   %[ftmp2],       %[c]                \n\t"   \
                                                                            \
        PTR_ADDU   ""#src1",    "#src",         "#sstride"          \n\t"   \
        MMI_ULWC1(%[ftmp1], src1, 0x00)                                     \
        "punpcklbh  %[ftmp2],   %[ftmp1],       %[ftmp0]            \n\t"   \
        "pmullh     %[ftmp2],   %[ftmp2],       %[d]                \n\t"   \
        "paddsh     %[ftmp3],   %[ftmp3],       %[ftmp2]            \n\t"   \
                                                                            \
        "paddsh     %[ftmp3],   %[ftmp3],       %[ff_pw_4]          \n\t"   \
        "psrah      %[ftmp3],   %[ftmp3],       %[ftmp4]            \n\t"   \
                                                                            \
        "packushb   %[ftmp1],   %[ftmp3],       %[ftmp0]            \n\t"   \
        MMI_SWC1(%[ftmp1], dst, 0x00)


DECLARE_ALIGNED(8, static const uint64_t, fourtap_subpel_filters[7][6]) = {
   {0x0000000000000000, 0x0006000600060006, 0x007b007b007b007b,
    0x000c000c000c000c, 0x0001000100010001, 0x0000000000000000},

   {0x0002000200020002, 0x000b000b000b000b, 0x006c006c006c006c,
    0x0024002400240024, 0x0008000800080008, 0x0001000100010001},

   {0x0000000000000000, 0x0009000900090009, 0x005d005d005d005d,
    0x0032003200320032, 0x0006000600060006, 0x0000000000000000},

   {0x0003000300030003, 0x0010001000100010, 0x004d004d004d004d,
    0x004d004d004d004d, 0x0010001000100010, 0x0003000300030003},

   {0x0000000000000000, 0x0006000600060006, 0x0032003200320032,
    0x005d005d005d005d, 0x0009000900090009, 0x0000000000000000},

   {0x0001000100010001, 0x0008000800080008, 0x0024002400240024,
    0x006c006c006c006c, 0x000b000b000b000b, 0x0002000200020002},

   {0x0000000000000000, 0x0001000100010001, 0x000c000c000c000c,
    0x007b007b007b007b, 0x0006000600060006, 0x0000000000000000}
};

#if 0
#define FILTER_6TAP(src, F, stride)                                           \
    cm[(F[2] * src[x + 0 * stride] - F[1] * src[x - 1 * stride] +             \
        F[0] * src[x - 2 * stride] + F[3] * src[x + 1 * stride] -             \
        F[4] * src[x + 2 * stride] + F[5] * src[x + 3 * stride] + 64) >> 7]

#define FILTER_4TAP(src, F, stride)                                           \
    cm[(F[2] * src[x + 0 * stride] - F[1] * src[x - 1 * stride] +             \
        F[3] * src[x + 1 * stride] - F[4] * src[x + 2 * stride] + 64) >> 7]

static const uint8_t subpel_filters[7][6] = {
    { 0,  6, 123,  12,  1, 0 },
    { 2, 11, 108,  36,  8, 1 },
    { 0,  9,  93,  50,  6, 0 },
    { 3, 16,  77,  77, 16, 3 },
    { 0,  6,  50,  93,  9, 0 },
    { 1,  8,  36, 108, 11, 2 },
    { 0,  1,  12, 123,  6, 0 },
};

#define MUL_20091(a) ((((a) * 20091) >> 16) + (a))
#define MUL_35468(a)  (((a) * 35468) >> 16)
#endif

#define clip_int8(n) (cm[(n) + 0x80] - 0x80)
static av_always_inline void vp8_filter_common_is4tap(uint8_t *p,
        ptrdiff_t stride)
{
    int av_unused p1 = p[-2 * stride];
    int av_unused p0 = p[-1 * stride];
    int av_unused q0 = p[ 0 * stride];
    int av_unused q1 = p[ 1 * stride];
    int a, f1, f2;
    const uint8_t *cm = ff_crop_tab + MAX_NEG_CROP;

    a = 3 * (q0 - p0);
    a += clip_int8(p1 - q1);
    a = clip_int8(a);

    // We deviate from the spec here with c(a+3) >> 3
    // since that's what libvpx does.
    f1 = FFMIN(a + 4, 127) >> 3;
    f2 = FFMIN(a + 3, 127) >> 3;

    // Despite what the spec says, we do need to clamp here to
    // be bitexact with libvpx.
    p[-1 * stride] = cm[p0 + f2];
    p[ 0 * stride] = cm[q0 - f1];
}

static av_always_inline void vp8_filter_common_isnot4tap(uint8_t *p,
        ptrdiff_t stride)
{
    int av_unused p1 = p[-2 * stride];
    int av_unused p0 = p[-1 * stride];
    int av_unused q0 = p[ 0 * stride];
    int av_unused q1 = p[ 1 * stride];
    int a, f1, f2;
    const uint8_t *cm = ff_crop_tab + MAX_NEG_CROP;

    a = 3 * (q0 - p0);
    a = clip_int8(a);

    // We deviate from the spec here with c(a+3) >> 3
    // since that's what libvpx does.
    f1 = FFMIN(a + 4, 127) >> 3;
    f2 = FFMIN(a + 3, 127) >> 3;

    // Despite what the spec says, we do need to clamp here to
    // be bitexact with libvpx.
    p[-1 * stride] = cm[p0 + f2];
    p[ 0 * stride] = cm[q0 - f1];
    a              = (f1 + 1) >> 1;
    p[-2 * stride] = cm[p1 + a];
    p[ 1 * stride] = cm[q1 - a];
}

static av_always_inline int vp8_simple_limit(uint8_t *p, ptrdiff_t stride,
        int flim)
{
    int av_unused p1 = p[-2 * stride];
    int av_unused p0 = p[-1 * stride];
    int av_unused q0 = p[ 0 * stride];
    int av_unused q1 = p[ 1 * stride];

    return 2 * FFABS(p0 - q0) + (FFABS(p1 - q1) >> 1) <= flim;
}

static av_always_inline int hev(uint8_t *p, ptrdiff_t stride, int thresh)
{
    int av_unused p1 = p[-2 * stride];
    int av_unused p0 = p[-1 * stride];
    int av_unused q0 = p[ 0 * stride];
    int av_unused q1 = p[ 1 * stride];

    return FFABS(p1 - p0) > thresh || FFABS(q1 - q0) > thresh;
}

static av_always_inline void filter_mbedge(uint8_t *p, ptrdiff_t stride)
{
    int a0, a1, a2, w;
    const uint8_t *cm = ff_crop_tab + MAX_NEG_CROP;

    int av_unused p2 = p[-3 * stride];
    int av_unused p1 = p[-2 * stride];
    int av_unused p0 = p[-1 * stride];
    int av_unused q0 = p[ 0 * stride];
    int av_unused q1 = p[ 1 * stride];
    int av_unused q2 = p[ 2 * stride];

    w = clip_int8(p1 - q1);
    w = clip_int8(w + 3 * (q0 - p0));

    a0 = (27 * w + 63) >> 7;
    a1 = (18 * w + 63) >> 7;
    a2 =  (9 * w + 63) >> 7;

    p[-3 * stride] = cm[p2 + a2];
    p[-2 * stride] = cm[p1 + a1];
    p[-1 * stride] = cm[p0 + a0];
    p[ 0 * stride] = cm[q0 - a0];
    p[ 1 * stride] = cm[q1 - a1];
    p[ 2 * stride] = cm[q2 - a2];
}

static av_always_inline int vp8_normal_limit(uint8_t *p, ptrdiff_t stride,
        int E, int I)
{
    int av_unused p3 = p[-4 * stride];
    int av_unused p2 = p[-3 * stride];
    int av_unused p1 = p[-2 * stride];
    int av_unused p0 = p[-1 * stride];
    int av_unused q0 = p[ 0 * stride];
    int av_unused q1 = p[ 1 * stride];
    int av_unused q2 = p[ 2 * stride];
    int av_unused q3 = p[ 3 * stride];

    return vp8_simple_limit(p, stride, E) &&
           FFABS(p3 - p2) <= I && FFABS(p2 - p1) <= I &&
           FFABS(p1 - p0) <= I && FFABS(q3 - q2) <= I &&
           FFABS(q2 - q1) <= I && FFABS(q1 - q0) <= I;
}

static av_always_inline void vp8_v_loop_filter8_mmi(uint8_t *dst,
        ptrdiff_t stride, int flim_E, int flim_I, int hev_thresh)
{
    int i;

    for (i = 0; i < 8; i++)
        if (vp8_normal_limit(dst + i * 1, stride, flim_E, flim_I)) {
            if (hev(dst + i * 1, stride, hev_thresh))
                vp8_filter_common_is4tap(dst + i * 1, stride);
            else
                filter_mbedge(dst + i * 1, stride);
        }
}

static av_always_inline void vp8_v_loop_filter8_inner_mmi(uint8_t *dst,
        ptrdiff_t stride, int flim_E, int flim_I, int hev_thresh)
{
    int i;

    for (i = 0; i < 8; i++)
        if (vp8_normal_limit(dst + i * 1, stride, flim_E, flim_I)) {
            int hv = hev(dst + i * 1, stride, hev_thresh);
            if (hv)
                vp8_filter_common_is4tap(dst + i * 1, stride);
            else
                vp8_filter_common_isnot4tap(dst + i * 1, stride);
        }
}

static av_always_inline void vp8_h_loop_filter8_mmi(uint8_t *dst,
        ptrdiff_t stride, int flim_E, int flim_I, int hev_thresh)
{
    int i;

    for (i = 0; i < 8; i++)
        if (vp8_normal_limit(dst + i * stride, 1, flim_E, flim_I)) {
            if (hev(dst + i * stride, 1, hev_thresh))
                vp8_filter_common_is4tap(dst + i * stride, 1);
            else
                filter_mbedge(dst + i * stride, 1);
        }
}

static av_always_inline void vp8_h_loop_filter8_inner_mmi(uint8_t *dst,
        ptrdiff_t stride, int flim_E, int flim_I, int hev_thresh)
{
    int i;

    for (i = 0; i < 8; i++)
        if (vp8_normal_limit(dst + i * stride, 1, flim_E, flim_I)) {
            int hv = hev(dst + i * stride, 1, hev_thresh);
            if (hv)
                vp8_filter_common_is4tap(dst + i * stride, 1);
            else
                vp8_filter_common_isnot4tap(dst + i * stride, 1);
        }
}

void ff_vp8_luma_dc_wht_mmi(int16_t block[4][4][16], int16_t dc[16])
{
#if 1
    double ftmp[8];
    DECLARE_VAR_ALL64;

    __asm__ volatile (
        MMI_LDC1(%[ftmp0], %[dc], 0x00)
        MMI_LDC1(%[ftmp1], %[dc], 0x08)
        MMI_LDC1(%[ftmp2], %[dc], 0x10)
        MMI_LDC1(%[ftmp3], %[dc], 0x18)
        "paddsh     %[ftmp4],   %[ftmp0],       %[ftmp3]            \n\t"
        "psubsh     %[ftmp5],   %[ftmp0],       %[ftmp3]            \n\t"
        "paddsh     %[ftmp6],   %[ftmp1],       %[ftmp2]            \n\t"
        "psubsh     %[ftmp7],   %[ftmp1],       %[ftmp2]            \n\t"
        "paddsh     %[ftmp0],   %[ftmp4],       %[ftmp6]            \n\t"
        "paddsh     %[ftmp1],   %[ftmp5],       %[ftmp7]            \n\t"
        "psubsh     %[ftmp2],   %[ftmp4],       %[ftmp6]            \n\t"
        "psubsh     %[ftmp3],   %[ftmp5],       %[ftmp7]            \n\t"
        MMI_SDC1(%[ftmp0], %[dc], 0x00)
        MMI_SDC1(%[ftmp1], %[dc], 0x08)
        MMI_SDC1(%[ftmp2], %[dc], 0x10)
        MMI_SDC1(%[ftmp3], %[dc], 0x18)
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),
          RESTRICT_ASM_ALL64
          [ftmp7]"=&f"(ftmp[7])
        : [dc]"r"((uint8_t*)dc)
        : "memory"
    );

    block[0][0][0] = (dc[0] + dc[3] + 3 + dc[1] + dc[2]) >> 3;
    block[0][1][0] = (dc[0] - dc[3] + 3 + dc[1] - dc[2]) >> 3;
    block[0][2][0] = (dc[0] + dc[3] + 3 - dc[1] - dc[2]) >> 3;
    block[0][3][0] = (dc[0] - dc[3] + 3 - dc[1] + dc[2]) >> 3;

    block[1][0][0] = (dc[4] + dc[7] + 3 + dc[5] + dc[6]) >> 3;
    block[1][1][0] = (dc[4] - dc[7] + 3 + dc[5] - dc[6]) >> 3;
    block[1][2][0] = (dc[4] + dc[7] + 3 - dc[5] - dc[6]) >> 3;
    block[1][3][0] = (dc[4] - dc[7] + 3 - dc[5] + dc[6]) >> 3;

    block[2][0][0] = (dc[8] + dc[11] + 3 + dc[9] + dc[10]) >> 3;
    block[2][1][0] = (dc[8] - dc[11] + 3 + dc[9] - dc[10]) >> 3;
    block[2][2][0] = (dc[8] + dc[11] + 3 - dc[9] - dc[10]) >> 3;
    block[2][3][0] = (dc[8] - dc[11] + 3 - dc[9] + dc[10]) >> 3;

    block[3][0][0] = (dc[12] + dc[15] + 3 + dc[13] + dc[14]) >> 3;
    block[3][1][0] = (dc[12] - dc[15] + 3 + dc[13] - dc[14]) >> 3;
    block[3][2][0] = (dc[12] + dc[15] + 3 - dc[13] - dc[14]) >> 3;
    block[3][3][0] = (dc[12] - dc[15] + 3 - dc[13] + dc[14]) >> 3;

    __asm__ volatile (
        "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]            \n\t"
        MMI_SDC1(%[ftmp0], %[dc], 0x00)
        MMI_SDC1(%[ftmp0], %[dc], 0x08)
        MMI_SDC1(%[ftmp0], %[dc], 0x10)
        MMI_SDC1(%[ftmp0], %[dc], 0x18)
        : RESTRICT_ASM_ALL64
          [ftmp0]"=&f"(ftmp[0])
        : [dc]"r"((uint8_t *)dc)
        : "memory"
    );
#else
    int t00, t01, t02, t03, t10, t11, t12, t13, t20, t21, t22, t23, t30, t31, t32, t33;

    t00 = dc[0] + dc[12];
    t10 = dc[1] + dc[13];
    t20 = dc[2] + dc[14];
    t30 = dc[3] + dc[15];

    t03 = dc[0] - dc[12];
    t13 = dc[1] - dc[13];
    t23 = dc[2] - dc[14];
    t33 = dc[3] - dc[15];

    t01 = dc[4] + dc[ 8];
    t11 = dc[5] + dc[ 9];
    t21 = dc[6] + dc[10];
    t31 = dc[7] + dc[11];

    t02 = dc[4] - dc[ 8];
    t12 = dc[5] - dc[ 9];
    t22 = dc[6] - dc[10];
    t32 = dc[7] - dc[11];

    dc[ 0] = t00 + t01;
    dc[ 1] = t10 + t11;
    dc[ 2] = t20 + t21;
    dc[ 3] = t30 + t31;

    dc[ 4] = t03 + t02;
    dc[ 5] = t13 + t12;
    dc[ 6] = t23 + t22;
    dc[ 7] = t33 + t32;

    dc[ 8] = t00 - t01;
    dc[ 9] = t10 - t11;
    dc[10] = t20 - t21;
    dc[11] = t30 - t31;

    dc[12] = t03 - t02;
    dc[13] = t13 - t12;
    dc[14] = t23 - t22;
    dc[15] = t33 - t32;

    block[0][0][0] = (dc[0] + dc[3] + 3 + dc[1] + dc[2]) >> 3;
    block[0][1][0] = (dc[0] - dc[3] + 3 + dc[1] - dc[2]) >> 3;
    block[0][2][0] = (dc[0] + dc[3] + 3 - dc[1] - dc[2]) >> 3;
    block[0][3][0] = (dc[0] - dc[3] + 3 - dc[1] + dc[2]) >> 3;

    block[1][0][0] = (dc[4] + dc[7] + 3 + dc[5] + dc[6]) >> 3;
    block[1][1][0] = (dc[4] - dc[7] + 3 + dc[5] - dc[6]) >> 3;
    block[1][2][0] = (dc[4] + dc[7] + 3 - dc[5] - dc[6]) >> 3;
    block[1][3][0] = (dc[4] - dc[7] + 3 - dc[5] + dc[6]) >> 3;

    block[2][0][0] = (dc[8] + dc[11] + 3 + dc[9] + dc[10]) >> 3;
    block[2][1][0] = (dc[8] - dc[11] + 3 + dc[9] - dc[10]) >> 3;
    block[2][2][0] = (dc[8] + dc[11] + 3 - dc[9] - dc[10]) >> 3;
    block[2][3][0] = (dc[8] - dc[11] + 3 - dc[9] + dc[10]) >> 3;

    block[3][0][0] = (dc[12] + dc[15] + 3 + dc[13] + dc[14]) >> 3;
    block[3][1][0] = (dc[12] - dc[15] + 3 + dc[13] - dc[14]) >> 3;
    block[3][2][0] = (dc[12] + dc[15] + 3 - dc[13] - dc[14]) >> 3;
    block[3][3][0] = (dc[12] - dc[15] + 3 - dc[13] + dc[14]) >> 3;

    AV_ZERO64(dc + 0);
    AV_ZERO64(dc + 4);
    AV_ZERO64(dc + 8);
    AV_ZERO64(dc + 12);
#endif
}

void ff_vp8_luma_dc_wht_dc_mmi(int16_t block[4][4][16], int16_t dc[16])
{
    int val = (dc[0] + 3) >> 3;

    dc[0] = 0;

    block[0][0][0] = val;
    block[0][1][0] = val;
    block[0][2][0] = val;
    block[0][3][0] = val;
    block[1][0][0] = val;
    block[1][1][0] = val;
    block[1][2][0] = val;
    block[1][3][0] = val;
    block[2][0][0] = val;
    block[2][1][0] = val;
    block[2][2][0] = val;
    block[2][3][0] = val;
    block[3][0][0] = val;
    block[3][1][0] = val;
    block[3][2][0] = val;
    block[3][3][0] = val;
}

void ff_vp8_idct_add_mmi(uint8_t *dst, int16_t block[16], ptrdiff_t stride)
{
#if 1
    DECLARE_ALIGNED(8, const uint64_t, ff_ph_4e7b) = {0x4e7b4e7b4e7b4e7bULL};
    DECLARE_ALIGNED(8, const uint64_t, ff_ph_22a3) = {0x22a322a322a322a3ULL};
    double ftmp[12];
    uint32_t tmp[1];
    DECLARE_VAR_LOW32;
    DECLARE_VAR_ALL64;

    __asm__ volatile (
        "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]            \n\t"
        MMI_LDC1(%[ftmp1], %[block], 0x00)
        MMI_LDC1(%[ftmp2], %[block], 0x08)
        MMI_LDC1(%[ftmp3], %[block], 0x10)
        MMI_LDC1(%[ftmp4], %[block], 0x18)

        "li         %[tmp0],    0x02                                \n\t"
        "mtc1       %[tmp0],    %[ftmp11]                           \n\t"

        // block[0...3] + block[8...11]
        "paddh      %[ftmp5],   %[ftmp1],       %[ftmp3]            \n\t"
        // block[0...3] - block[8...11]
        "psubh      %[ftmp6],   %[ftmp1],       %[ftmp3]            \n\t"
        // MUL_35468(block[12...15])
        "psllh      %[ftmp9],   %[ftmp4],       %[ftmp11]           \n\t"
        "pmulhh     %[ftmp7],   %[ftmp9],       %[ff_ph_22a3]       \n\t"
        // MUL_35468(block[4...7])
        "psllh      %[ftmp9],   %[ftmp2],       %[ftmp11]           \n\t"
        "pmulhh     %[ftmp8],   %[ftmp9],       %[ff_ph_22a3]       \n\t"
        // MUL_20091(block[4...7]
        "pmulhh     %[ftmp9],   %[ftmp2],       %[ff_ph_4e7b]       \n\t"
        "paddh      %[ftmp9],   %[ftmp9],       %[ftmp2]            \n\t"
        // MUL_20091(block[12...15])
        "pmulhh     %[ftmp10],  %[ftmp4],       %[ff_ph_4e7b]       \n\t"
        "paddh      %[ftmp10],  %[ftmp10],      %[ftmp4]            \n\t"

        // tmp[0 4  8 12]
        "paddh      %[ftmp1],   %[ftmp5],       %[ftmp7]            \n\t"
        "paddh      %[ftmp1],   %[ftmp1],       %[ftmp9]            \n\t"
        // tmp[1 5  9 13]
        "paddh      %[ftmp2],   %[ftmp6],       %[ftmp8]            \n\t"
        "psubh      %[ftmp2],   %[ftmp2],       %[ftmp10]           \n\t"
        // tmp[2 6 10 14]
        "psubh      %[ftmp3],   %[ftmp6],       %[ftmp8]            \n\t"
        "paddh      %[ftmp3],   %[ftmp3],       %[ftmp10]           \n\t"
        // tmp[3 7 11 15]
        "psubh      %[ftmp4],   %[ftmp5],       %[ftmp7]            \n\t"
        "psubh      %[ftmp4],   %[ftmp4],       %[ftmp9]            \n\t"

        MMI_SDC1(%[ftmp0], %[block], 0x00)
        MMI_SDC1(%[ftmp0], %[block], 0x08)
        MMI_SDC1(%[ftmp0], %[block], 0x10)
        MMI_SDC1(%[ftmp0], %[block], 0x18)

        TRANSPOSE_4H(%[ftmp1], %[ftmp2], %[ftmp3], %[ftmp4],
                     %[ftmp5], %[ftmp6], %[ftmp7], %[ftmp8],
                     %[ftmp9], %[tmp0],  %[ftmp0], %[ftmp10])

        // t[0 4  8 12]
        "paddh      %[ftmp5],   %[ftmp1],       %[ftmp3]            \n\t"
        // t[1 5  9 13]
        "psubh      %[ftmp6],   %[ftmp1],       %[ftmp3]            \n\t"
        // t[2 6 10 14]
        "psllh      %[ftmp9],   %[ftmp2],       %[ftmp11]           \n\t"
        "pmulhh     %[ftmp9],   %[ftmp9],       %[ff_ph_22a3]       \n\t"
        "psubh      %[ftmp7],   %[ftmp9],       %[ftmp4]            \n\t"
        "pmulhh     %[ftmp10],  %[ftmp4],       %[ff_ph_4e7b]       \n\t"
        "psubh      %[ftmp7],   %[ftmp7],       %[ftmp10]           \n\t"
        // t[3 7 11 15]
        "psllh      %[ftmp9],   %[ftmp4],       %[ftmp11]           \n\t"
        "pmulhh     %[ftmp9],   %[ftmp9],       %[ff_ph_22a3]       \n\t"
        "paddh      %[ftmp8],   %[ftmp9],       %[ftmp2]            \n\t"
        "pmulhh     %[ftmp10],  %[ftmp2],       %[ff_ph_4e7b]       \n\t"
        "paddh      %[ftmp8],   %[ftmp8],       %[ftmp10]           \n\t"

        "li         %[tmp0],    0x03                                \n\t"
        "mtc1       %[tmp0],    %[ftmp11]                           \n\t"
        "paddh      %[ftmp1],   %[ftmp5],       %[ftmp8]            \n\t"
        "paddh      %[ftmp1],   %[ftmp1],       %[ff_pw_4]          \n\t"
        "psrah      %[ftmp1],   %[ftmp1],       %[ftmp11]           \n\t"
        "paddh      %[ftmp2],   %[ftmp6],       %[ftmp7]            \n\t"
        "paddh      %[ftmp2],   %[ftmp2],       %[ff_pw_4]          \n\t"
        "psrah      %[ftmp2],   %[ftmp2],       %[ftmp11]           \n\t"
        "psubh      %[ftmp3],   %[ftmp6],       %[ftmp7]            \n\t"
        "paddh      %[ftmp3],   %[ftmp3],       %[ff_pw_4]          \n\t"
        "psrah      %[ftmp3],   %[ftmp3],       %[ftmp11]           \n\t"
        "psubh      %[ftmp4],   %[ftmp5],       %[ftmp8]            \n\t"
        "paddh      %[ftmp4],   %[ftmp4],       %[ff_pw_4]          \n\t"
        "psrah      %[ftmp4],   %[ftmp4],       %[ftmp11]           \n\t"

        TRANSPOSE_4H(%[ftmp1], %[ftmp2], %[ftmp3], %[ftmp4],
                     %[ftmp5], %[ftmp6], %[ftmp7], %[ftmp8],
                     %[ftmp9], %[tmp0],  %[ftmp0], %[ftmp10])

        MMI_LWC1(%[ftmp5], %[dst0], 0x00)
        MMI_LWC1(%[ftmp6], %[dst1], 0x00)
        MMI_LWC1(%[ftmp7], %[dst2], 0x00)
        MMI_LWC1(%[ftmp8], %[dst3], 0x00)

        "punpcklbh  %[ftmp5],   %[ftmp5],       %[ftmp0]            \n\t"
        "punpcklbh  %[ftmp6],   %[ftmp6],       %[ftmp0]            \n\t"
        "punpcklbh  %[ftmp7],   %[ftmp7],       %[ftmp0]            \n\t"
        "punpcklbh  %[ftmp8],   %[ftmp8],       %[ftmp0]            \n\t"

        "paddh      %[ftmp1],   %[ftmp1],       %[ftmp5]            \n\t"
        "paddh      %[ftmp2],   %[ftmp2],       %[ftmp6]            \n\t"
        "paddh      %[ftmp3],   %[ftmp3],       %[ftmp7]            \n\t"
        "paddh      %[ftmp4],   %[ftmp4],       %[ftmp8]            \n\t"

        "packushb   %[ftmp1],   %[ftmp1],       %[ftmp0]            \n\t"
        "packushb   %[ftmp2],   %[ftmp2],       %[ftmp0]            \n\t"
        "packushb   %[ftmp3],   %[ftmp3],       %[ftmp0]            \n\t"
        "packushb   %[ftmp4],   %[ftmp4],       %[ftmp0]            \n\t"

        MMI_SWC1(%[ftmp1], %[dst0], 0x00)
        MMI_SWC1(%[ftmp2], %[dst1], 0x00)
        MMI_SWC1(%[ftmp3], %[dst2], 0x00)
        MMI_SWC1(%[ftmp4], %[dst3], 0x00)
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),            [ftmp7]"=&f"(ftmp[7]),
          [ftmp8]"=&f"(ftmp[8]),            [ftmp9]"=&f"(ftmp[9]),
          [ftmp10]"=&f"(ftmp[10]),          [ftmp11]"=&f"(ftmp[11]),
          RESTRICT_ASM_LOW32
          RESTRICT_ASM_ALL64
          [tmp0]"=&r"(tmp[0])
        : [dst0]"r"(dst),                   [dst1]"r"(dst+stride),
          [dst2]"r"(dst+2*stride),          [dst3]"r"(dst+3*stride),
          [block]"r"(block),                [ff_pw_4]"f"(ff_pw_4),
          [ff_ph_4e7b]"f"(ff_ph_4e7b),      [ff_ph_22a3]"f"(ff_ph_22a3)
        : "memory"
    );
#else
    int i, t0, t1, t2, t3;
    int16_t tmp[16];

    for (i = 0; i < 4; i++) {
        t0 = block[0 + i] + block[8 + i];
        t1 = block[0 + i] - block[8 + i];
        t2 = MUL_35468(block[4 + i]) - MUL_20091(block[12 + i]);
        t3 = MUL_20091(block[4 + i]) + MUL_35468(block[12 + i]);
        block[ 0 + i] = 0;
        block[ 4 + i] = 0;
        block[ 8 + i] = 0;
        block[12 + i] = 0;

        tmp[i * 4 + 0] = t0 + t3;
        tmp[i * 4 + 1] = t1 + t2;
        tmp[i * 4 + 2] = t1 - t2;
        tmp[i * 4 + 3] = t0 - t3;
    }

    for (i = 0; i < 4; i++) {
        t0 = tmp[0 + i] + tmp[8 + i];
        t1 = tmp[0 + i] - tmp[8 + i];
        t2 = MUL_35468(tmp[4 + i]) - MUL_20091(tmp[12 + i]);
        t3 = MUL_20091(tmp[4 + i]) + MUL_35468(tmp[12 + i]);

        dst[0] = av_clip_uint8(dst[0] + ((t0 + t3 + 4) >> 3));
        dst[1] = av_clip_uint8(dst[1] + ((t1 + t2 + 4) >> 3));
        dst[2] = av_clip_uint8(dst[2] + ((t1 - t2 + 4) >> 3));
        dst[3] = av_clip_uint8(dst[3] + ((t0 - t3 + 4) >> 3));
        dst   += stride;
    }
#endif
}

void ff_vp8_idct_dc_add_mmi(uint8_t *dst, int16_t block[16], ptrdiff_t stride)
{
#if 1
    int dc = (block[0] + 4) >> 3;
    double ftmp[6];
    DECLARE_VAR_LOW32;

    block[0] = 0;

    __asm__ volatile (
        "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]            \n\t"
        "mtc1       %[dc],      %[ftmp5]                            \n\t"
        MMI_LWC1(%[ftmp1], %[dst0], 0x00)
        MMI_LWC1(%[ftmp2], %[dst1], 0x00)
        MMI_LWC1(%[ftmp3], %[dst2], 0x00)
        MMI_LWC1(%[ftmp4], %[dst3], 0x00)
        "pshufh     %[ftmp5],   %[ftmp5],       %[ftmp0]            \n\t"
        "punpcklbh  %[ftmp1],   %[ftmp1],       %[ftmp0]            \n\t"
        "punpcklbh  %[ftmp2],   %[ftmp2],       %[ftmp0]            \n\t"
        "punpcklbh  %[ftmp3],   %[ftmp3],       %[ftmp0]            \n\t"
        "punpcklbh  %[ftmp4],   %[ftmp4],       %[ftmp0]            \n\t"
        "paddsh     %[ftmp1],   %[ftmp1],       %[ftmp5]            \n\t"
        "paddsh     %[ftmp2],   %[ftmp2],       %[ftmp5]            \n\t"
        "paddsh     %[ftmp3],   %[ftmp3],       %[ftmp5]            \n\t"
        "paddsh     %[ftmp4],   %[ftmp4],       %[ftmp5]            \n\t"
        "packushb   %[ftmp1],   %[ftmp1],       %[ftmp0]            \n\t"
        "packushb   %[ftmp2],   %[ftmp2],       %[ftmp0]            \n\t"
        "packushb   %[ftmp3],   %[ftmp3],       %[ftmp0]            \n\t"
        "packushb   %[ftmp4],   %[ftmp4],       %[ftmp0]            \n\t"
        MMI_SWC1(%[ftmp1], %[dst0], 0x00)
        MMI_SWC1(%[ftmp2], %[dst1], 0x00)
        MMI_SWC1(%[ftmp3], %[dst2], 0x00)
        MMI_SWC1(%[ftmp4], %[dst3], 0x00)
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),
          RESTRICT_ASM_LOW32
          [ftmp5]"=&f"(ftmp[5])
        : [dst0]"r"(dst),                   [dst1]"r"(dst+stride),
          [dst2]"r"(dst+2*stride),          [dst3]"r"(dst+3*stride),
          [dc]"r"(dc)
        : "memory"
    );
#else
    int i, dc = (block[0] + 4) >> 3;

    block[0] = 0;

    for (i = 0; i < 4; i++) {
        dst[0] = av_clip_uint8(dst[0] + dc);
        dst[1] = av_clip_uint8(dst[1] + dc);
        dst[2] = av_clip_uint8(dst[2] + dc);
        dst[3] = av_clip_uint8(dst[3] + dc);
        dst   += stride;
    }
#endif
}

void ff_vp8_idct_dc_add4y_mmi(uint8_t *dst, int16_t block[4][16],
        ptrdiff_t stride)
{
    ff_vp8_idct_dc_add_mmi(dst +  0, block[0], stride);
    ff_vp8_idct_dc_add_mmi(dst +  4, block[1], stride);
    ff_vp8_idct_dc_add_mmi(dst +  8, block[2], stride);
    ff_vp8_idct_dc_add_mmi(dst + 12, block[3], stride);
}

void ff_vp8_idct_dc_add4uv_mmi(uint8_t *dst, int16_t block[4][16],
        ptrdiff_t stride)
{
    ff_vp8_idct_dc_add_mmi(dst + stride * 0 + 0, block[0], stride);
    ff_vp8_idct_dc_add_mmi(dst + stride * 0 + 4, block[1], stride);
    ff_vp8_idct_dc_add_mmi(dst + stride * 4 + 0, block[2], stride);
    ff_vp8_idct_dc_add_mmi(dst + stride * 4 + 4, block[3], stride);
}

// loop filter applied to edges between macroblocks
void ff_vp8_v_loop_filter16_mmi(uint8_t *dst, ptrdiff_t stride, int flim_E,
        int flim_I, int hev_thresh)
{
    int i;

    for (i = 0; i < 16; i++)
        if (vp8_normal_limit(dst + i * 1, stride, flim_E, flim_I)) {
            if (hev(dst + i * 1, stride, hev_thresh))
                vp8_filter_common_is4tap(dst + i * 1, stride);
            else
                filter_mbedge(dst + i * 1, stride);
        }
}

void ff_vp8_h_loop_filter16_mmi(uint8_t *dst, ptrdiff_t stride, int flim_E,
        int flim_I, int hev_thresh)
{
    int i;

    for (i = 0; i < 16; i++)
        if (vp8_normal_limit(dst + i * stride, 1, flim_E, flim_I)) {
            if (hev(dst + i * stride, 1, hev_thresh))
                vp8_filter_common_is4tap(dst + i * stride, 1);
            else
                filter_mbedge(dst + i * stride, 1);
        }
}

void ff_vp8_v_loop_filter8uv_mmi(uint8_t *dstU, uint8_t *dstV, ptrdiff_t stride,
        int flim_E, int flim_I, int hev_thresh)
{
    vp8_v_loop_filter8_mmi(dstU, stride, flim_E, flim_I, hev_thresh);
    vp8_v_loop_filter8_mmi(dstV, stride, flim_E, flim_I, hev_thresh);
}

void ff_vp8_h_loop_filter8uv_mmi(uint8_t *dstU, uint8_t *dstV, ptrdiff_t stride,
        int flim_E, int flim_I, int hev_thresh)
{
    vp8_h_loop_filter8_mmi(dstU, stride, flim_E, flim_I, hev_thresh);
    vp8_h_loop_filter8_mmi(dstV, stride, flim_E, flim_I, hev_thresh);
}

// loop filter applied to inner macroblock edges
void ff_vp8_v_loop_filter16_inner_mmi(uint8_t *dst, ptrdiff_t stride,
        int flim_E, int flim_I, int hev_thresh)
{
    int i;

    for (i = 0; i < 16; i++)
        if (vp8_normal_limit(dst + i * 1, stride, flim_E, flim_I)) {
            int hv = hev(dst + i * 1, stride, hev_thresh);
            if (hv)
                vp8_filter_common_is4tap(dst + i * 1, stride);
            else
                vp8_filter_common_isnot4tap(dst + i * 1, stride);
        }
}

void ff_vp8_h_loop_filter16_inner_mmi(uint8_t *dst, ptrdiff_t stride,
        int flim_E, int flim_I, int hev_thresh)
{
    int i;

    for (i = 0; i < 16; i++)
        if (vp8_normal_limit(dst + i * stride, 1, flim_E, flim_I)) {
            int hv = hev(dst + i * stride, 1, hev_thresh);
            if (hv)
                vp8_filter_common_is4tap(dst + i * stride, 1);
            else
                vp8_filter_common_isnot4tap(dst + i * stride, 1);
        }
}

void ff_vp8_v_loop_filter8uv_inner_mmi(uint8_t *dstU, uint8_t *dstV,
        ptrdiff_t stride, int flim_E, int flim_I, int hev_thresh)
{
    vp8_v_loop_filter8_inner_mmi(dstU, stride, flim_E, flim_I, hev_thresh);
    vp8_v_loop_filter8_inner_mmi(dstV, stride, flim_E, flim_I, hev_thresh);
}

void ff_vp8_h_loop_filter8uv_inner_mmi(uint8_t *dstU, uint8_t *dstV,
        ptrdiff_t stride, int flim_E, int flim_I, int hev_thresh)
{
    vp8_h_loop_filter8_inner_mmi(dstU, stride, flim_E, flim_I, hev_thresh);
    vp8_h_loop_filter8_inner_mmi(dstV, stride, flim_E, flim_I, hev_thresh);
}

void ff_vp8_v_loop_filter_simple_mmi(uint8_t *dst, ptrdiff_t stride, int flim)
{
    int i;

    for (i = 0; i < 16; i++)
        if (vp8_simple_limit(dst + i, stride, flim))
            vp8_filter_common_is4tap(dst + i, stride);
}

void ff_vp8_h_loop_filter_simple_mmi(uint8_t *dst, ptrdiff_t stride, int flim)
{
    int i;

    for (i = 0; i < 16; i++)
        if (vp8_simple_limit(dst + i * stride, 1, flim))
            vp8_filter_common_is4tap(dst + i * stride, 1);
}

void ff_put_vp8_pixels16_mmi(uint8_t *dst, ptrdiff_t dststride, uint8_t *src,
        ptrdiff_t srcstride, int h, int x, int y)
{
#if 1
    double ftmp[2];
    uint64_t tmp[2];
    mips_reg addr[2];
    DECLARE_VAR_ALL64;

    __asm__ volatile (
        "1:                                                         \n\t"
        PTR_ADDU   "%[addr0],   %[src],         %[srcstride]        \n\t"
        MMI_ULDC1(%[ftmp0], %[src], 0x00)
        "ldl        %[tmp0],    0x0f(%[src])                        \n\t"
        "ldr        %[tmp0],    0x08(%[src])                        \n\t"
        MMI_ULDC1(%[ftmp1], %[addr0], 0x00)
        "ldl        %[tmp1],    0x0f(%[addr0])                      \n\t"
        "ldr        %[tmp1],    0x08(%[addr0])                      \n\t"
        PTR_ADDU   "%[addr1],   %[dst],         %[dststride]        \n\t"
        MMI_SDC1(%[ftmp0], %[dst], 0x00)
        "sdl        %[tmp0],    0x0f(%[dst])                        \n\t"
        "sdr        %[tmp0],    0x08(%[dst])                        \n\t"
        "addiu      %[h],       %[h],           -0x02               \n\t"
        MMI_SDC1(%[ftmp1], %[addr1], 0x00)
        PTR_ADDU   "%[src],     %[addr0],       %[srcstride]        \n\t"
        "sdl        %[tmp1],    0x0f(%[addr1])                      \n\t"
        "sdr        %[tmp1],    0x08(%[addr1])                      \n\t"
        PTR_ADDU   "%[dst],     %[addr1],       %[dststride]        \n\t"
        "bnez       %[h],       1b                                  \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [tmp0]"=&r"(tmp[0]),              [tmp1]"=&r"(tmp[1]),
          RESTRICT_ASM_ALL64
          [addr0]"=&r"(addr[0]),            [addr1]"=&r"(addr[1]),
          [dst]"+&r"(dst),                  [src]"+&r"(src),
          [h]"+&r"(h)
        : [dststride]"r"((mips_reg)dststride),
          [srcstride]"r"((mips_reg)srcstride)
        : "memory"
    );
#else
    int i;

    for (i = 0; i < h; i++, dst += dststride, src += srcstride)
        memcpy(dst, src, 16);
#endif
}

void ff_put_vp8_pixels8_mmi(uint8_t *dst, ptrdiff_t dststride, uint8_t *src,
        ptrdiff_t srcstride, int h, int x, int y)
{
#if 1
    double ftmp[1];
    uint64_t tmp[1];
    mips_reg addr[2];
    DECLARE_VAR_ALL64;

    __asm__ volatile (
        "1:                                                         \n\t"
        PTR_ADDU   "%[addr0],   %[src],         %[srcstride]        \n\t"
        MMI_ULDC1(%[ftmp0], %[src], 0x00)
        "ldl        %[tmp0],    0x07(%[addr0])                      \n\t"
        "ldr        %[tmp0],    0x00(%[addr0])                      \n\t"
        PTR_ADDU   "%[addr1],   %[dst],         %[dststride]        \n\t"
        MMI_SDC1(%[ftmp0], %[dst], 0x00)
        "addiu      %[h],       %[h],           -0x02               \n\t"
        "sdl        %[tmp0],    0x07(%[addr1])                      \n\t"
        "sdr        %[tmp0],    0x00(%[addr1])                      \n\t"
        PTR_ADDU   "%[src],     %[addr0],       %[srcstride]        \n\t"
        PTR_ADDU   "%[dst],     %[addr1],       %[dststride]        \n\t"
        "bnez       %[h],       1b                                  \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [tmp0]"=&r"(tmp[0]),
          RESTRICT_ASM_ALL64
          [addr0]"=&r"(addr[0]),            [addr1]"=&r"(addr[1]),
          [dst]"+&r"(dst),                  [src]"+&r"(src),
          [h]"+&r"(h)
        : [dststride]"r"((mips_reg)dststride),
          [srcstride]"r"((mips_reg)srcstride)
        : "memory"
    );
#else
    int i;

    for (i = 0; i < h; i++, dst += dststride, src += srcstride)
        memcpy(dst, src, 8);
#endif
}

void ff_put_vp8_pixels4_mmi(uint8_t *dst, ptrdiff_t dststride, uint8_t *src,
        ptrdiff_t srcstride, int h, int x, int y)
{
#if 1
    double ftmp[1];
    uint64_t tmp[1];
    mips_reg addr[2];
    DECLARE_VAR_LOW32;

    __asm__ volatile (
        "1:                                                         \n\t"
        PTR_ADDU   "%[addr0],   %[src],         %[srcstride]        \n\t"
        MMI_LWC1(%[ftmp0], %[src], 0x00)
        "lwl        %[tmp0],    0x03(%[addr0])                      \n\t"
        "lwr        %[tmp0],    0x00(%[addr0])                      \n\t"
        PTR_ADDU   "%[addr1],   %[dst],         %[dststride]        \n\t"
        MMI_SWC1(%[ftmp0], %[dst], 0x00)
        "addiu      %[h],       %[h],           -0x02               \n\t"
        "swl        %[tmp0],    0x03(%[addr1])                      \n\t"
        "swr        %[tmp0],    0x00(%[addr1])                      \n\t"
        PTR_ADDU   "%[src],     %[addr0],       %[srcstride]        \n\t"
        PTR_ADDU   "%[dst],     %[addr1],       %[dststride]        \n\t"
        "bnez       %[h],       1b                                  \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [tmp0]"=&r"(tmp[0]),
          RESTRICT_ASM_LOW32
          [addr0]"=&r"(addr[0]),            [addr1]"=&r"(addr[1]),
          [dst]"+&r"(dst),                  [src]"+&r"(src),
          [h]"+&r"(h)
        : [dststride]"r"((mips_reg)dststride),
          [srcstride]"r"((mips_reg)srcstride)
        : "memory"
    );
#else
    int i;

    for (i = 0; i < h; i++, dst += dststride, src += srcstride)
        memcpy(dst, src, 4);
#endif
}

void ff_put_vp8_epel16_h4_mmi(uint8_t *dst, ptrdiff_t dststride, uint8_t *src,
        ptrdiff_t srcstride, int h, int mx, int my)
{
#if 1
    const uint64_t *filter = fourtap_subpel_filters[mx - 1];
    double ftmp[9];
    uint32_t tmp[1];
    mips_reg src1, dst1;
    DECLARE_VAR_ALL64;

    /*
    dst[0] = cm[(filter[2] * src[0] - filter[1] * src[-1] + filter[3] * src[1] - filter[4] * src[2] + 64) >> 7];
    dst[1] = cm[(filter[2] * src[1] - filter[1] * src[ 0] + filter[3] * src[2] - filter[4] * src[3] + 64) >> 7];
    dst[2] = cm[(filter[2] * src[2] - filter[1] * src[ 1] + filter[3] * src[3] - filter[4] * src[4] + 64) >> 7];
    dst[3] = cm[(filter[2] * src[3] - filter[1] * src[ 2] + filter[3] * src[4] - filter[4] * src[5] + 64) >> 7];
    dst[4] = cm[(filter[2] * src[4] - filter[1] * src[ 3] + filter[3] * src[5] - filter[4] * src[6] + 64) >> 7];
    dst[5] = cm[(filter[2] * src[5] - filter[1] * src[ 4] + filter[3] * src[6] - filter[4] * src[7] + 64) >> 7];
    dst[6] = cm[(filter[2] * src[6] - filter[1] * src[ 5] + filter[3] * src[7] - filter[4] * src[8] + 64) >> 7];
    dst[7] = cm[(filter[2] * src[7] - filter[1] * src[ 6] + filter[3] * src[8] - filter[4] * src[9] + 64) >> 7];

    dst[ 8] = cm[(filter[2] * src[ 8] - filter[1] * src[ 7] + filter[3] * src[ 9] - filter[4] * src[10] + 64) >> 7];
    dst[ 9] = cm[(filter[2] * src[ 9] - filter[1] * src[ 8] + filter[3] * src[10] - filter[4] * src[11] + 64) >> 7];
    dst[10] = cm[(filter[2] * src[10] - filter[1] * src[ 9] + filter[3] * src[11] - filter[4] * src[12] + 64) >> 7];
    dst[11] = cm[(filter[2] * src[11] - filter[1] * src[10] + filter[3] * src[12] - filter[4] * src[13] + 64) >> 7];
    dst[12] = cm[(filter[2] * src[12] - filter[1] * src[11] + filter[3] * src[13] - filter[4] * src[14] + 64) >> 7];
    dst[13] = cm[(filter[2] * src[13] - filter[1] * src[12] + filter[3] * src[14] - filter[4] * src[15] + 64) >> 7];
    dst[14] = cm[(filter[2] * src[14] - filter[1] * src[13] + filter[3] * src[15] - filter[4] * src[16] + 64) >> 7];
    dst[15] = cm[(filter[2] * src[15] - filter[1] * src[14] + filter[3] * src[16] - filter[4] * src[17] + 64) >> 7];
    */
    __asm__ volatile (
        "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]            \n\t"
        "li         %[tmp0],    0x07                                \n\t"
        "mtc1       %[tmp0],    %[ftmp4]                            \n\t"

        "1:                                                         \n\t"
        // 0 - 7
        PUT_VP8_EPEL8_H4_MMI(%[src], %[dst])
        PTR_ADDIU  "%[src1],    %[src],         0x08                \n\t"
        PTR_ADDIU  "%[dst1],    %[dst],         0x08                \n\t"
        // 8 - 15
        PUT_VP8_EPEL8_H4_MMI(%[src1], %[dst1])

        "addiu      %[h],       %[h],           -0x01               \n\t"
        PTR_ADDU   "%[src],     %[src],         %[srcstride]        \n\t"
        PTR_ADDU   "%[dst],     %[dst],         %[dststride]        \n\t"
        "bnez       %[h],       1b                                  \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),            [ftmp7]"=&f"(ftmp[7]),
          [ftmp8]"=&f"(ftmp[8]),
          [tmp0]"=&r"(tmp[0]),
          RESTRICT_ASM_ALL64
          [dst1]"=&r"(dst1),                [src1]"=&r"(src1),
          [h]"+&r"(h),
          [dst]"+&r"(dst),                  [src]"+&r"(src)
        : [ff_pw_64]"f"(ff_pw_64),
          [srcstride]"r"((mips_reg)srcstride),
          [dststride]"r"((mips_reg)dststride),
          [filter1]"f"(filter[1]),          [filter2]"f"(filter[2]),
          [filter3]"f"(filter[3]),          [filter4]"f"(filter[4])
        : "memory"
    );
#else
    const uint8_t *filter = subpel_filters[mx - 1];
    const uint8_t *cm     = ff_crop_tab + MAX_NEG_CROP;
    int x, y;

    for (y = 0; y < h; y++) {
        for (x = 0; x < 16; x++)
            dst[x] = FILTER_4TAP(src, filter, 1);
        dst += dststride;
        src += srcstride;
    }
#endif
}

void ff_put_vp8_epel8_h4_mmi(uint8_t *dst, ptrdiff_t dststride, uint8_t *src,
        ptrdiff_t srcstride, int h, int mx, int my)
{
#if 1
    const uint64_t *filter = fourtap_subpel_filters[mx - 1];
    double ftmp[9];
    uint32_t tmp[1];
    DECLARE_VAR_ALL64;

    /*
    dst[0] = cm[(filter[2] * src[0] - filter[1] * src[-1] + filter[3] * src[1] - filter[4] * src[2] + 64) >> 7];
    dst[1] = cm[(filter[2] * src[1] - filter[1] * src[ 0] + filter[3] * src[2] - filter[4] * src[3] + 64) >> 7];
    dst[2] = cm[(filter[2] * src[2] - filter[1] * src[ 1] + filter[3] * src[3] - filter[4] * src[4] + 64) >> 7];
    dst[3] = cm[(filter[2] * src[3] - filter[1] * src[ 2] + filter[3] * src[4] - filter[4] * src[5] + 64) >> 7];
    dst[4] = cm[(filter[2] * src[4] - filter[1] * src[ 3] + filter[3] * src[5] - filter[4] * src[6] + 64) >> 7];
    dst[5] = cm[(filter[2] * src[5] - filter[1] * src[ 4] + filter[3] * src[6] - filter[4] * src[7] + 64) >> 7];
    dst[6] = cm[(filter[2] * src[6] - filter[1] * src[ 5] + filter[3] * src[7] - filter[4] * src[8] + 64) >> 7];
    dst[7] = cm[(filter[2] * src[7] - filter[1] * src[ 6] + filter[3] * src[8] - filter[4] * src[9] + 64) >> 7];
    */
    __asm__ volatile (
        "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]            \n\t"
        "li         %[tmp0],    0x07                                \n\t"
        "mtc1       %[tmp0],    %[ftmp4]                            \n\t"

        "1:                                                         \n\t"
        PUT_VP8_EPEL8_H4_MMI(%[src], %[dst])

        "addiu      %[h],       %[h],           -0x01               \n\t"
        PTR_ADDU   "%[src],     %[src],         %[srcstride]        \n\t"
        PTR_ADDU   "%[dst],     %[dst],         %[dststride]        \n\t"
        "bnez       %[h],       1b                                  \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),            [ftmp7]"=&f"(ftmp[7]),
          [ftmp8]"=&f"(ftmp[8]),
          [tmp0]"=&r"(tmp[0]),
          RESTRICT_ASM_ALL64
          [h]"+&r"(h),
          [dst]"+&r"(dst),                  [src]"+&r"(src)
        : [ff_pw_64]"f"(ff_pw_64),
          [srcstride]"r"((mips_reg)srcstride),
          [dststride]"r"((mips_reg)dststride),
          [filter1]"f"(filter[1]),          [filter2]"f"(filter[2]),
          [filter3]"f"(filter[3]),          [filter4]"f"(filter[4])
        : "memory"
    );
#else
    const uint8_t *filter = subpel_filters[mx - 1];
    const uint8_t *cm     = ff_crop_tab + MAX_NEG_CROP;
    int x, y;

    for (y = 0; y < h; y++) {
        for (x = 0; x < 8; x++)
            dst[x] = FILTER_4TAP(src, filter, 1);
        dst += dststride;
        src += srcstride;
    }
#endif
}

void ff_put_vp8_epel4_h4_mmi(uint8_t *dst, ptrdiff_t dststride, uint8_t *src,
        ptrdiff_t srcstride, int h, int mx, int my)
{
#if 1
    const uint64_t *filter = fourtap_subpel_filters[mx - 1];
    double ftmp[6];
    uint32_t tmp[1];
    DECLARE_VAR_LOW32;

    /*
    dst[0] = cm[(filter[2] * src[0] - filter[1] * src[-1] + filter[3] * src[1] - filter[4] * src[2] + 64) >> 7];
    dst[1] = cm[(filter[2] * src[1] - filter[1] * src[ 0] + filter[3] * src[2] - filter[4] * src[3] + 64) >> 7];
    dst[2] = cm[(filter[2] * src[2] - filter[1] * src[ 1] + filter[3] * src[3] - filter[4] * src[4] + 64) >> 7];
    dst[3] = cm[(filter[2] * src[3] - filter[1] * src[ 2] + filter[3] * src[4] - filter[4] * src[5] + 64) >> 7];
    */
    __asm__ volatile (
        "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]            \n\t"
        "li         %[tmp0],    0x07                                \n\t"
        "mtc1       %[tmp0],    %[ftmp4]                            \n\t"

        "1:                                                         \n\t"
        PUT_VP8_EPEL4_H4_MMI(%[src], %[dst])

        "addiu      %[h],       %[h],           -0x01               \n\t"
        PTR_ADDU   "%[src],     %[src],         %[srcstride]        \n\t"
        PTR_ADDU   "%[dst],     %[dst],         %[dststride]        \n\t"
        "bnez       %[h],       1b                                  \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
          [tmp0]"=&r"(tmp[0]),
          RESTRICT_ASM_LOW32
          [h]"+&r"(h),
          [dst]"+&r"(dst),                  [src]"+&r"(src)
        : [ff_pw_64]"f"(ff_pw_64),
          [srcstride]"r"((mips_reg)srcstride),
          [dststride]"r"((mips_reg)dststride),
          [filter1]"f"(filter[1]),          [filter2]"f"(filter[2]),
          [filter3]"f"(filter[3]),          [filter4]"f"(filter[4])
        : "memory"
    );
#else
    const uint8_t *filter = subpel_filters[mx - 1];
    const uint8_t *cm     = ff_crop_tab + MAX_NEG_CROP;
    int x, y;

    for (y = 0; y < h; y++) {
        for (x = 0; x < 4; x++)
            dst[x] = FILTER_4TAP(src, filter, 1);
        dst += dststride;
        src += srcstride;
    }
#endif
}

void ff_put_vp8_epel16_h6_mmi(uint8_t *dst, ptrdiff_t dststride, uint8_t *src,
        ptrdiff_t srcstride, int h, int mx, int my)
{
#if 1
    const uint64_t *filter = fourtap_subpel_filters[mx - 1];
    double ftmp[9];
    uint32_t tmp[1];
    mips_reg src1, dst1;
    DECLARE_VAR_ALL64;

    /*
    dst[ 0] = cm[(filter[2]*src[ 0] - filter[1]*src[-1] + filter[0]*src[-2] + filter[3]*src[ 1] - filter[4]*src[ 2] + filter[5]*src[ 3] + 64) >> 7];
    dst[ 1] = cm[(filter[2]*src[ 1] - filter[1]*src[ 0] + filter[0]*src[-1] + filter[3]*src[ 2] - filter[4]*src[ 3] + filter[5]*src[ 4] + 64) >> 7];
    dst[ 2] = cm[(filter[2]*src[ 2] - filter[1]*src[ 1] + filter[0]*src[ 0] + filter[3]*src[ 3] - filter[4]*src[ 4] + filter[5]*src[ 5] + 64) >> 7];
    dst[ 3] = cm[(filter[2]*src[ 3] - filter[1]*src[ 2] + filter[0]*src[ 1] + filter[3]*src[ 4] - filter[4]*src[ 5] + filter[5]*src[ 6] + 64) >> 7];
    dst[ 4] = cm[(filter[2]*src[ 4] - filter[1]*src[ 3] + filter[0]*src[ 2] + filter[3]*src[ 5] - filter[4]*src[ 6] + filter[5]*src[ 7] + 64) >> 7];
    dst[ 5] = cm[(filter[2]*src[ 5] - filter[1]*src[ 4] + filter[0]*src[ 3] + filter[3]*src[ 6] - filter[4]*src[ 7] + filter[5]*src[ 8] + 64) >> 7];
    dst[ 6] = cm[(filter[2]*src[ 6] - filter[1]*src[ 5] + filter[0]*src[ 4] + filter[3]*src[ 7] - filter[4]*src[ 8] + filter[5]*src[ 9] + 64) >> 7];
    dst[ 7] = cm[(filter[2]*src[ 7] - filter[1]*src[ 6] + filter[0]*src[ 5] + filter[3]*src[ 8] - filter[4]*src[ 9] + filter[5]*src[10] + 64) >> 7];

    dst[ 8] = cm[(filter[2]*src[ 8] - filter[1]*src[ 7] + filter[0]*src[ 6] + filter[3]*src[ 9] - filter[4]*src[10] + filter[5]*src[11] + 64) >> 7];
    dst[ 9] = cm[(filter[2]*src[ 9] - filter[1]*src[ 8] + filter[0]*src[ 7] + filter[3]*src[10] - filter[4]*src[11] + filter[5]*src[12] + 64) >> 7];
    dst[10] = cm[(filter[2]*src[10] - filter[1]*src[ 9] + filter[0]*src[ 8] + filter[3]*src[11] - filter[4]*src[12] + filter[5]*src[13] + 64) >> 7];
    dst[11] = cm[(filter[2]*src[11] - filter[1]*src[10] + filter[0]*src[ 9] + filter[3]*src[12] - filter[4]*src[13] + filter[5]*src[14] + 64) >> 7];
    dst[12] = cm[(filter[2]*src[12] - filter[1]*src[11] + filter[0]*src[10] + filter[3]*src[13] - filter[4]*src[14] + filter[5]*src[15] + 64) >> 7];
    dst[13] = cm[(filter[2]*src[13] - filter[1]*src[12] + filter[0]*src[11] + filter[3]*src[14] - filter[4]*src[15] + filter[5]*src[16] + 64) >> 7];
    dst[14] = cm[(filter[2]*src[14] - filter[1]*src[13] + filter[0]*src[12] + filter[3]*src[15] - filter[4]*src[16] + filter[5]*src[17] + 64) >> 7];
    dst[15] = cm[(filter[2]*src[15] - filter[1]*src[14] + filter[0]*src[13] + filter[3]*src[16] - filter[4]*src[17] + filter[5]*src[18] + 64) >> 7];
    */
    __asm__ volatile (
        "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]            \n\t"
        "li         %[tmp0],    0x07                                \n\t"
        "mtc1       %[tmp0],    %[ftmp4]                            \n\t"

        "1:                                                         \n\t"
        // 0 - 7
        PUT_VP8_EPEL8_H6_MMI(%[src], %[dst])
        PTR_ADDIU  "%[src1],    %[src],         0x08                \n\t"
        PTR_ADDIU  "%[dst1],    %[dst],         0x08                \n\t"
        // 8 - 15
        PUT_VP8_EPEL8_H6_MMI(%[src1], %[dst1])

        "addiu      %[h],       %[h],           -0x01               \n\t"
        PTR_ADDU   "%[src],     %[src],         %[srcstride]        \n\t"
        PTR_ADDU   "%[dst],     %[dst],         %[dststride]        \n\t"
        "bnez       %[h],       1b                                  \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),            [ftmp7]"=&f"(ftmp[7]),
          [ftmp8]"=&f"(ftmp[8]),
          [tmp0]"=&r"(tmp[0]),
          RESTRICT_ASM_ALL64
          [dst1]"=&r"(dst1),                [src1]"=&r"(src1),
          [h]"+&r"(h),
          [dst]"+&r"(dst),                  [src]"+&r"(src)
        : [ff_pw_64]"f"(ff_pw_64),
          [srcstride]"r"((mips_reg)srcstride),
          [dststride]"r"((mips_reg)dststride),
          [filter0]"f"(filter[0]),          [filter1]"f"(filter[1]),
          [filter2]"f"(filter[2]),          [filter3]"f"(filter[3]),
          [filter4]"f"(filter[4]),          [filter5]"f"(filter[5])
        : "memory"
    );
#else
    const uint8_t *filter = subpel_filters[mx - 1];
    const uint8_t *cm     = ff_crop_tab + MAX_NEG_CROP;
    int x, y;

    for (y = 0; y < h; y++) {
        for (x = 0; x < 16; x++)
            dst[x] = FILTER_6TAP(src, filter, 1);
        dst += dststride;
        src += srcstride;
    }
#endif
}

void ff_put_vp8_epel8_h6_mmi(uint8_t *dst, ptrdiff_t dststride, uint8_t *src,
        ptrdiff_t srcstride, int h, int mx, int my)
{
#if 1
    const uint64_t *filter = fourtap_subpel_filters[mx - 1];
    double ftmp[9];
    uint32_t tmp[1];
    DECLARE_VAR_ALL64;

    /*
    dst[0] = cm[(filter[2]*src[0] - filter[1]*src[-1] + filter[0]*src[-2] + filter[3]*src[1] - filter[4]*src[2] + filter[5]*src[ 3] + 64) >> 7];
    dst[1] = cm[(filter[2]*src[1] - filter[1]*src[ 0] + filter[0]*src[-1] + filter[3]*src[2] - filter[4]*src[3] + filter[5]*src[ 4] + 64) >> 7];
    dst[2] = cm[(filter[2]*src[2] - filter[1]*src[ 1] + filter[0]*src[ 0] + filter[3]*src[3] - filter[4]*src[4] + filter[5]*src[ 5] + 64) >> 7];
    dst[3] = cm[(filter[2]*src[3] - filter[1]*src[ 2] + filter[0]*src[ 1] + filter[3]*src[4] - filter[4]*src[5] + filter[5]*src[ 6] + 64) >> 7];
    dst[4] = cm[(filter[2]*src[4] - filter[1]*src[ 3] + filter[0]*src[ 2] + filter[3]*src[5] - filter[4]*src[6] + filter[5]*src[ 7] + 64) >> 7];
    dst[5] = cm[(filter[2]*src[5] - filter[1]*src[ 4] + filter[0]*src[ 3] + filter[3]*src[6] - filter[4]*src[7] + filter[5]*src[ 8] + 64) >> 7];
    dst[6] = cm[(filter[2]*src[6] - filter[1]*src[ 5] + filter[0]*src[ 4] + filter[3]*src[7] - filter[4]*src[8] + filter[5]*src[ 9] + 64) >> 7];
    dst[7] = cm[(filter[2]*src[7] - filter[1]*src[ 6] + filter[0]*src[ 5] + filter[3]*src[8] - filter[4]*src[9] + filter[5]*src[10] + 64) >> 7];
    */
    __asm__ volatile (
        "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]            \n\t"
        "li         %[tmp0],    0x07                                \n\t"
        "mtc1       %[tmp0],    %[ftmp4]                            \n\t"

        "1:                                                         \n\t"
        PUT_VP8_EPEL8_H6_MMI(%[src], %[dst])

        "addiu      %[h],       %[h],           -0x01               \n\t"
        PTR_ADDU   "%[src],     %[src],         %[srcstride]        \n\t"
        PTR_ADDU   "%[dst],     %[dst],         %[dststride]        \n\t"
        "bnez       %[h],       1b                                  \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),            [ftmp7]"=&f"(ftmp[7]),
          [ftmp8]"=&f"(ftmp[8]),
          [tmp0]"=&r"(tmp[0]),
          RESTRICT_ASM_ALL64
          [h]"+&r"(h),
          [dst]"+&r"(dst),                  [src]"+&r"(src)
        : [ff_pw_64]"f"(ff_pw_64),
          [srcstride]"r"((mips_reg)srcstride),
          [dststride]"r"((mips_reg)dststride),
          [filter0]"f"(filter[0]),          [filter1]"f"(filter[1]),
          [filter2]"f"(filter[2]),          [filter3]"f"(filter[3]),
          [filter4]"f"(filter[4]),          [filter5]"f"(filter[5])
        : "memory"
    );
#else
    const uint8_t *filter = subpel_filters[mx - 1];
    const uint8_t *cm     = ff_crop_tab + MAX_NEG_CROP;
    int x, y;

    for (y = 0; y < h; y++) {
        for (x = 0; x < 8; x++)
            dst[x] = FILTER_6TAP(src, filter, 1);
        dst += dststride;
        src += srcstride;
    }
#endif
}

void ff_put_vp8_epel4_h6_mmi(uint8_t *dst, ptrdiff_t dststride, uint8_t *src,
        ptrdiff_t srcstride, int h, int mx, int my)
{
#if 1
    const uint64_t *filter = fourtap_subpel_filters[mx - 1];
    double ftmp[6];
    uint32_t tmp[1];
    DECLARE_VAR_LOW32;

    /*
    dst[0] = cm[(filter[2]*src[0] - filter[1]*src[-1] + filter[0]*src[-2] + filter[3]*src[1] - filter[4]*src[2] + filter[5]*src[ 3] + 64) >> 7];
    dst[1] = cm[(filter[2]*src[1] - filter[1]*src[ 0] + filter[0]*src[-1] + filter[3]*src[2] - filter[4]*src[3] + filter[5]*src[ 4] + 64) >> 7];
    dst[2] = cm[(filter[2]*src[2] - filter[1]*src[ 1] + filter[0]*src[ 0] + filter[3]*src[3] - filter[4]*src[4] + filter[5]*src[ 5] + 64) >> 7];
    dst[3] = cm[(filter[2]*src[3] - filter[1]*src[ 2] + filter[0]*src[ 1] + filter[3]*src[4] - filter[4]*src[5] + filter[5]*src[ 6] + 64) >> 7];
    */
    __asm__ volatile (
        "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]            \n\t"
        "li         %[tmp0],    0x07                                \n\t"
        "mtc1       %[tmp0],    %[ftmp4]                            \n\t"

        "1:                                                         \n\t"
        PUT_VP8_EPEL4_H6_MMI(%[src], %[dst])

        "addiu      %[h],       %[h],           -0x01               \n\t"
        PTR_ADDU   "%[src],     %[src],         %[srcstride]        \n\t"
        PTR_ADDU   "%[dst],     %[dst],         %[dststride]        \n\t"
        "bnez       %[h],       1b                                  \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
          [tmp0]"=&r"(tmp[0]),
          RESTRICT_ASM_LOW32
          [h]"+&r"(h),
          [dst]"+&r"(dst),                  [src]"+&r"(src)
        : [ff_pw_64]"f"(ff_pw_64),
          [srcstride]"r"((mips_reg)srcstride),
          [dststride]"r"((mips_reg)dststride),
          [filter0]"f"(filter[0]),          [filter1]"f"(filter[1]),
          [filter2]"f"(filter[2]),          [filter3]"f"(filter[3]),
          [filter4]"f"(filter[4]),          [filter5]"f"(filter[5])
        : "memory"
    );
#else
    const uint8_t *filter = subpel_filters[mx - 1];
    const uint8_t *cm     = ff_crop_tab + MAX_NEG_CROP;
    int x, y;

    for (y = 0; y < h; y++) {
        for (x = 0; x < 4; x++)
            dst[x] = FILTER_6TAP(src, filter, 1);
        dst += dststride;
        src += srcstride;
    }
#endif
}

void ff_put_vp8_epel16_v4_mmi(uint8_t *dst, ptrdiff_t dststride, uint8_t *src,
        ptrdiff_t srcstride, int h, int mx, int my)
{
#if 1
    const uint64_t *filter = fourtap_subpel_filters[my - 1];
    double ftmp[9];
    uint32_t tmp[1];
    mips_reg src0, src1, dst0;
    DECLARE_VAR_ALL64;

    /*
    dst[0] = cm[(filter[2] * src[0] - filter[1] * src[ -srcstride] + filter[3] * src[  srcstride] - filter[4] * src[  2*srcstride] + 64) >> 7];
    dst[1] = cm[(filter[2] * src[1] - filter[1] * src[1-srcstride] + filter[3] * src[1+srcstride] - filter[4] * src[1+2*srcstride] + 64) >> 7];
    dst[2] = cm[(filter[2] * src[2] - filter[1] * src[2-srcstride] + filter[3] * src[2+srcstride] - filter[4] * src[2+2*srcstride] + 64) >> 7];
    dst[3] = cm[(filter[2] * src[3] - filter[1] * src[3-srcstride] + filter[3] * src[3+srcstride] - filter[4] * src[3+2*srcstride] + 64) >> 7];
    dst[4] = cm[(filter[2] * src[4] - filter[1] * src[4-srcstride] + filter[3] * src[4+srcstride] - filter[4] * src[4+2*srcstride] + 64) >> 7];
    dst[5] = cm[(filter[2] * src[5] - filter[1] * src[5-srcstride] + filter[3] * src[5+srcstride] - filter[4] * src[5+2*srcstride] + 64) >> 7];
    dst[6] = cm[(filter[2] * src[6] - filter[1] * src[6-srcstride] + filter[3] * src[6+srcstride] - filter[4] * src[6+2*srcstride] + 64) >> 7];
    dst[7] = cm[(filter[2] * src[7] - filter[1] * src[7-srcstride] + filter[3] * src[7+srcstride] - filter[4] * src[7+2*srcstride] + 64) >> 7];

    dst[ 8] = cm[(filter[2] * src[ 8] - filter[1] * src[ 8-srcstride] + filter[3] * src[ 8+srcstride] - filter[4] * src[ 8+2*srcstride] + 64) >> 7];
    dst[ 9] = cm[(filter[2] * src[ 9] - filter[1] * src[ 9-srcstride] + filter[3] * src[ 9+srcstride] - filter[4] * src[ 9+2*srcstride] + 64) >> 7];
    dst[10] = cm[(filter[2] * src[10] - filter[1] * src[10-srcstride] + filter[3] * src[10+srcstride] - filter[4] * src[10+2*srcstride] + 64) >> 7];
    dst[11] = cm[(filter[2] * src[11] - filter[1] * src[11-srcstride] + filter[3] * src[11+srcstride] - filter[4] * src[11+2*srcstride] + 64) >> 7];
    dst[12] = cm[(filter[2] * src[12] - filter[1] * src[12-srcstride] + filter[3] * src[12+srcstride] - filter[4] * src[12+2*srcstride] + 64) >> 7];
    dst[13] = cm[(filter[2] * src[13] - filter[1] * src[13-srcstride] + filter[3] * src[13+srcstride] - filter[4] * src[13+2*srcstride] + 64) >> 7];
    dst[14] = cm[(filter[2] * src[14] - filter[1] * src[14-srcstride] + filter[3] * src[14+srcstride] - filter[4] * src[14+2*srcstride] + 64) >> 7];
    dst[15] = cm[(filter[2] * src[15] - filter[1] * src[15-srcstride] + filter[3] * src[15+srcstride] - filter[4] * src[15+2*srcstride] + 64) >> 7];
    */
    __asm__ volatile (
        "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]            \n\t"
        "li         %[tmp0],    0x07                                \n\t"
        "mtc1       %[tmp0],    %[ftmp4]                            \n\t"

        "1:                                                         \n\t"
        // 0 - 7
        PUT_VP8_EPEL8_V4_MMI(%[src], %[src1], %[dst], %[srcstride])
        PTR_ADDIU  "%[src0],    %[src],         0x08                \n\t"
        PTR_ADDIU  "%[dst0],    %[dst],         0x08                \n\t"
        // 8 - 15
        PUT_VP8_EPEL8_V4_MMI(%[src0], %[src1], %[dst], %[srcstride])

        "addiu      %[h],       %[h],           -0x01               \n\t"
        PTR_ADDU   "%[src],     %[src],         %[srcstride]        \n\t"
        PTR_ADDU   "%[dst],     %[dst],         %[dststride]        \n\t"
        "bnez       %[h],       1b                                  \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),            [ftmp7]"=&f"(ftmp[7]),
          [ftmp8]"=&f"(ftmp[8]),
          [tmp0]"=&r"(tmp[0]),
          RESTRICT_ASM_ALL64
          [src0]"=&r"(src0),                [dst0]"=&r"(dst0),
          [src1]"=&r"(src1),
          [h]"+&r"(h),
          [dst]"+&r"(dst),                  [src]"+&r"(src)
        : [ff_pw_64]"f"(ff_pw_64),
          [srcstride]"r"((mips_reg)srcstride),
          [dststride]"r"((mips_reg)dststride),
          [filter1]"f"(filter[1]),          [filter2]"f"(filter[2]),
          [filter3]"f"(filter[3]),          [filter4]"f"(filter[4])
        : "memory"
    );
#else
    const uint8_t *filter = subpel_filters[my - 1];
    const uint8_t *cm     = ff_crop_tab + MAX_NEG_CROP;
    int x, y;

    for (y = 0; y < h; y++) {
        for (x = 0; x < 16; x++)
            dst[x] = FILTER_4TAP(src, filter, srcstride);
        dst += dststride;
        src += srcstride;
    }
#endif
}

void ff_put_vp8_epel8_v4_mmi(uint8_t *dst, ptrdiff_t dststride, uint8_t *src,
        ptrdiff_t srcstride, int h, int mx, int my)
{
#if 1
    const uint64_t *filter = fourtap_subpel_filters[my - 1];
    double ftmp[9];
    uint32_t tmp[1];
    mips_reg src1;
    DECLARE_VAR_ALL64;

    /*
    dst[0] = cm[(filter[2] * src[0] - filter[1] * src[ -srcstride] + filter[3] * src[  srcstride] - filter[4] * src[  2*srcstride] + 64) >> 7];
    dst[1] = cm[(filter[2] * src[1] - filter[1] * src[1-srcstride] + filter[3] * src[1+srcstride] - filter[4] * src[1+2*srcstride] + 64) >> 7];
    dst[2] = cm[(filter[2] * src[2] - filter[1] * src[2-srcstride] + filter[3] * src[2+srcstride] - filter[4] * src[2+2*srcstride] + 64) >> 7];
    dst[3] = cm[(filter[2] * src[3] - filter[1] * src[3-srcstride] + filter[3] * src[3+srcstride] - filter[4] * src[3+2*srcstride] + 64) >> 7];
    dst[4] = cm[(filter[2] * src[4] - filter[1] * src[4-srcstride] + filter[3] * src[4+srcstride] - filter[4] * src[4+2*srcstride] + 64) >> 7];
    dst[5] = cm[(filter[2] * src[5] - filter[1] * src[5-srcstride] + filter[3] * src[5+srcstride] - filter[4] * src[5+2*srcstride] + 64) >> 7];
    dst[6] = cm[(filter[2] * src[6] - filter[1] * src[6-srcstride] + filter[3] * src[6+srcstride] - filter[4] * src[6+2*srcstride] + 64) >> 7];
    dst[7] = cm[(filter[2] * src[7] - filter[1] * src[7-srcstride] + filter[3] * src[7+srcstride] - filter[4] * src[7+2*srcstride] + 64) >> 7];
    */
    __asm__ volatile (
        "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]            \n\t"
        "li         %[tmp0],    0x07                                \n\t"
        "mtc1       %[tmp0],    %[ftmp4]                            \n\t"

        "1:                                                         \n\t"
        PUT_VP8_EPEL8_V4_MMI(%[src], %[src1], %[dst], %[srcstride])

        "addiu      %[h],       %[h],           -0x01               \n\t"
        PTR_ADDU   "%[src],     %[src],         %[srcstride]        \n\t"
        PTR_ADDU   "%[dst],     %[dst],         %[dststride]        \n\t"
        "bnez       %[h],       1b                                  \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),            [ftmp7]"=&f"(ftmp[7]),
          [ftmp8]"=&f"(ftmp[8]),
          [tmp0]"=&r"(tmp[0]),
          RESTRICT_ASM_ALL64
          [src1]"=&r"(src1),
          [h]"+&r"(h),
          [dst]"+&r"(dst),                  [src]"+&r"(src)
        : [ff_pw_64]"f"(ff_pw_64),
          [srcstride]"r"((mips_reg)srcstride),
          [dststride]"r"((mips_reg)dststride),
          [filter1]"f"(filter[1]),          [filter2]"f"(filter[2]),
          [filter3]"f"(filter[3]),          [filter4]"f"(filter[4])
        : "memory"
    );
#else
    const uint8_t *filter = subpel_filters[my - 1];
    const uint8_t *cm     = ff_crop_tab + MAX_NEG_CROP;
    int x, y;

    for (y = 0; y < h; y++) {
        for (x = 0; x < 8; x++)
            dst[x] = FILTER_4TAP(src, filter, srcstride);
        dst += dststride;
        src += srcstride;
    }
#endif
}

void ff_put_vp8_epel4_v4_mmi(uint8_t *dst, ptrdiff_t dststride, uint8_t *src,
        ptrdiff_t srcstride, int h, int mx, int my)
{
#if 1
    const uint64_t *filter = fourtap_subpel_filters[my - 1];
    double ftmp[6];
    uint32_t tmp[1];
    mips_reg src1;
    DECLARE_VAR_LOW32;

    /*
    dst[0] = cm[(filter[2] * src[0] - filter[1] * src[ -srcstride] + filter[3] * src[  srcstride] - filter[4] * src[  2*srcstride] + 64) >> 7];
    dst[1] = cm[(filter[2] * src[1] - filter[1] * src[1-srcstride] + filter[3] * src[1+srcstride] - filter[4] * src[1+2*srcstride] + 64) >> 7];
    dst[2] = cm[(filter[2] * src[2] - filter[1] * src[2-srcstride] + filter[3] * src[2+srcstride] - filter[4] * src[2+2*srcstride] + 64) >> 7];
    dst[3] = cm[(filter[2] * src[3] - filter[1] * src[3-srcstride] + filter[3] * src[3+srcstride] - filter[4] * src[3+2*srcstride] + 64) >> 7];
    */
    __asm__ volatile (
        "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]            \n\t"
        "li         %[tmp0],    0x07                                \n\t"
        "mtc1       %[tmp0],    %[ftmp4]                            \n\t"

        "1:                                                         \n\t"
        PUT_VP8_EPEL4_V4_MMI(%[src], %[src1], %[dst], %[srcstride])

        "addiu      %[h],       %[h],           -0x01               \n\t"
        PTR_ADDU   "%[src],     %[src],         %[srcstride]        \n\t"
        PTR_ADDU   "%[dst],     %[dst],         %[dststride]        \n\t"
        "bnez       %[h],       1b                                  \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
          [tmp0]"=&r"(tmp[0]),
          RESTRICT_ASM_LOW32
          [src1]"=&r"(src1),
          [h]"+&r"(h),
          [dst]"+&r"(dst),                  [src]"+&r"(src)
        : [ff_pw_64]"f"(ff_pw_64),
          [srcstride]"r"((mips_reg)srcstride),
          [dststride]"r"((mips_reg)dststride),
          [filter1]"f"(filter[1]),          [filter2]"f"(filter[2]),
          [filter3]"f"(filter[3]),          [filter4]"f"(filter[4])
        : "memory"
    );
#else
    const uint8_t *filter = subpel_filters[my - 1];
    const uint8_t *cm     = ff_crop_tab + MAX_NEG_CROP;
    int x, y;

    for (y = 0; y < h; y++) {
        for (x = 0; x < 4; x++)
            dst[x] = FILTER_4TAP(src, filter, srcstride);
        dst += dststride;
        src += srcstride;
    }
#endif
}

void ff_put_vp8_epel16_v6_mmi(uint8_t *dst, ptrdiff_t dststride, uint8_t *src,
        ptrdiff_t srcstride, int h, int mx, int my)
{
#if 1
    const uint64_t *filter = fourtap_subpel_filters[my - 1];
    double ftmp[9];
    uint32_t tmp[1];
    mips_reg src0, src1, dst0;
    DECLARE_VAR_ALL64;

    /*
    dst[0] = cm[(filter[2]*src[0] - filter[1]*src[0-srcstride] + filter[0]*src[0-2*srcstride] + filter[3]*src[0+srcstride] - filter[4]*src[0+2*srcstride] + filter[5]*src[0+3*srcstride] + 64) >> 7];
    dst[1] = cm[(filter[2]*src[1] - filter[1]*src[1-srcstride] + filter[0]*src[1-2*srcstride] + filter[3]*src[1+srcstride] - filter[4]*src[1+2*srcstride] + filter[5]*src[1+3*srcstride] + 64) >> 7];
    dst[2] = cm[(filter[2]*src[2] - filter[1]*src[2-srcstride] + filter[0]*src[2-2*srcstride] + filter[3]*src[2+srcstride] - filter[4]*src[2+2*srcstride] + filter[5]*src[2+3*srcstride] + 64) >> 7];
    dst[3] = cm[(filter[2]*src[3] - filter[1]*src[3-srcstride] + filter[0]*src[3-2*srcstride] + filter[3]*src[3+srcstride] - filter[4]*src[3+2*srcstride] + filter[5]*src[3+3*srcstride] + 64) >> 7];
    dst[4] = cm[(filter[2]*src[4] - filter[1]*src[4-srcstride] + filter[0]*src[4-2*srcstride] + filter[3]*src[4+srcstride] - filter[4]*src[4+2*srcstride] + filter[5]*src[4+3*srcstride] + 64) >> 7];
    dst[5] = cm[(filter[2]*src[5] - filter[1]*src[5-srcstride] + filter[0]*src[5-2*srcstride] + filter[3]*src[5+srcstride] - filter[4]*src[5+2*srcstride] + filter[5]*src[5+3*srcstride] + 64) >> 7];
    dst[6] = cm[(filter[2]*src[6] - filter[1]*src[6-srcstride] + filter[0]*src[6-2*srcstride] + filter[3]*src[6+srcstride] - filter[4]*src[6+2*srcstride] + filter[5]*src[6+3*srcstride] + 64) >> 7];
    dst[7] = cm[(filter[2]*src[7] - filter[1]*src[7-srcstride] + filter[0]*src[7-2*srcstride] + filter[3]*src[7+srcstride] - filter[4]*src[7+2*srcstride] + filter[5]*src[7+3*srcstride] + 64) >> 7];

    dst[ 8] = cm[(filter[2]*src[ 8] - filter[1]*src[ 8-srcstride] + filter[0]*src[ 8-2*srcstride] + filter[3]*src[ 8+srcstride] - filter[4]*src[ 8+2*srcstride] + filter[5]*src[ 8+3*srcstride] + 64) >> 7];
    dst[ 9] = cm[(filter[2]*src[ 9] - filter[1]*src[ 9-srcstride] + filter[0]*src[ 9-2*srcstride] + filter[3]*src[ 9+srcstride] - filter[4]*src[ 9+2*srcstride] + filter[5]*src[ 9+3*srcstride] + 64) >> 7];
    dst[10] = cm[(filter[2]*src[10] - filter[1]*src[10-srcstride] + filter[0]*src[10-2*srcstride] + filter[3]*src[10+srcstride] - filter[4]*src[10+2*srcstride] + filter[5]*src[10+3*srcstride] + 64) >> 7];
    dst[11] = cm[(filter[2]*src[11] - filter[1]*src[11-srcstride] + filter[0]*src[11-2*srcstride] + filter[3]*src[11+srcstride] - filter[4]*src[11+2*srcstride] + filter[5]*src[11+3*srcstride] + 64) >> 7];
    dst[12] = cm[(filter[2]*src[12] - filter[1]*src[12-srcstride] + filter[0]*src[12-2*srcstride] + filter[3]*src[12+srcstride] - filter[4]*src[12+2*srcstride] + filter[5]*src[12+3*srcstride] + 64) >> 7];
    dst[13] = cm[(filter[2]*src[13] - filter[1]*src[13-srcstride] + filter[0]*src[13-2*srcstride] + filter[3]*src[13+srcstride] - filter[4]*src[13+2*srcstride] + filter[5]*src[13+3*srcstride] + 64) >> 7];
    dst[14] = cm[(filter[2]*src[14] - filter[1]*src[14-srcstride] + filter[0]*src[14-2*srcstride] + filter[3]*src[14+srcstride] - filter[4]*src[14+2*srcstride] + filter[5]*src[14+3*srcstride] + 64) >> 7];
    dst[15] = cm[(filter[2]*src[15] - filter[1]*src[15-srcstride] + filter[0]*src[15-2*srcstride] + filter[3]*src[15+srcstride] - filter[4]*src[15+2*srcstride] + filter[5]*src[15+3*srcstride] + 64) >> 7];
    */
    __asm__ volatile (
        "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]            \n\t"
        "li         %[tmp0],    0x07                                \n\t"
        "mtc1       %[tmp0],    %[ftmp4]                            \n\t"

        "1:                                                         \n\t"
        // 0 - 7
        PUT_VP8_EPEL8_V6_MMI(%[src], %[src1], %[dst], %[srcstride])
        PTR_ADDIU  "%[src0],    %[src],         0x08                \n\t"
        PTR_ADDIU  "%[dst0],    %[dst],         0x08                \n\t"
        // 8 - 15
        PUT_VP8_EPEL8_V6_MMI(%[src0], %[src1], %[dst0], %[srcstride])

        "addiu      %[h],       %[h],           -0x01               \n\t"
        PTR_ADDU   "%[src],     %[src],         %[srcstride]        \n\t"
        PTR_ADDU   "%[dst],     %[dst],         %[dststride]        \n\t"
        "bnez       %[h],       1b                                  \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),            [ftmp7]"=&f"(ftmp[7]),
          [ftmp8]"=&f"(ftmp[8]),
          [tmp0]"=&r"(tmp[0]),
          RESTRICT_ASM_ALL64
          [src0]"=&r"(src0),                [dst0]"=&r"(dst0),
          [src1]"=&r"(src1),
          [h]"+&r"(h),
          [dst]"+&r"(dst),                  [src]"+&r"(src)
        : [ff_pw_64]"f"(ff_pw_64),
          [srcstride]"r"((mips_reg)srcstride),
          [dststride]"r"((mips_reg)dststride),
          [filter0]"f"(filter[0]),          [filter1]"f"(filter[1]),
          [filter2]"f"(filter[2]),          [filter3]"f"(filter[3]),
          [filter4]"f"(filter[4]),          [filter5]"f"(filter[5])
        : "memory"
    );
#else
    const uint8_t *filter = subpel_filters[my - 1];
    const uint8_t *cm     = ff_crop_tab + MAX_NEG_CROP;
    int x, y;

    for (y = 0; y < h; y++) {
        for (x = 0; x < 16; x++)
            dst[x] = FILTER_6TAP(src, filter, srcstride);
        dst += dststride;
        src += srcstride;
    }
#endif
}

void ff_put_vp8_epel8_v6_mmi(uint8_t *dst, ptrdiff_t dststride, uint8_t *src,
        ptrdiff_t srcstride, int h, int mx, int my)
{
#if 1
    const uint64_t *filter = fourtap_subpel_filters[my - 1];
    double ftmp[9];
    uint32_t tmp[1];
    mips_reg src1;
    DECLARE_VAR_ALL64;

    /*
    dst[0] = cm[(filter[2]*src[0] - filter[1]*src[0-srcstride] + filter[0]*src[0-2*srcstride] + filter[3]*src[0+srcstride] - filter[4]*src[0+2*srcstride] + filter[5]*src[0+3*srcstride] + 64) >> 7];
    dst[1] = cm[(filter[2]*src[1] - filter[1]*src[1-srcstride] + filter[0]*src[1-2*srcstride] + filter[3]*src[1+srcstride] - filter[4]*src[1+2*srcstride] + filter[5]*src[1+3*srcstride] + 64) >> 7];
    dst[2] = cm[(filter[2]*src[2] - filter[1]*src[2-srcstride] + filter[0]*src[2-2*srcstride] + filter[3]*src[2+srcstride] - filter[4]*src[2+2*srcstride] + filter[5]*src[2+3*srcstride] + 64) >> 7];
    dst[3] = cm[(filter[2]*src[3] - filter[1]*src[3-srcstride] + filter[0]*src[3-2*srcstride] + filter[3]*src[3+srcstride] - filter[4]*src[3+2*srcstride] + filter[5]*src[3+3*srcstride] + 64) >> 7];
    dst[4] = cm[(filter[2]*src[4] - filter[1]*src[4-srcstride] + filter[0]*src[4-2*srcstride] + filter[3]*src[4+srcstride] - filter[4]*src[4+2*srcstride] + filter[5]*src[4+3*srcstride] + 64) >> 7];
    dst[5] = cm[(filter[2]*src[5] - filter[1]*src[5-srcstride] + filter[0]*src[5-2*srcstride] + filter[3]*src[5+srcstride] - filter[4]*src[5+2*srcstride] + filter[5]*src[5+3*srcstride] + 64) >> 7];
    dst[6] = cm[(filter[2]*src[6] - filter[1]*src[6-srcstride] + filter[0]*src[6-2*srcstride] + filter[3]*src[6+srcstride] - filter[4]*src[6+2*srcstride] + filter[5]*src[6+3*srcstride] + 64) >> 7];
    dst[7] = cm[(filter[2]*src[7] - filter[1]*src[7-srcstride] + filter[0]*src[7-2*srcstride] + filter[3]*src[7+srcstride] - filter[4]*src[7+2*srcstride] + filter[5]*src[7+3*srcstride] + 64) >> 7];
    */
    __asm__ volatile (
        "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]            \n\t"
        "li         %[tmp0],    0x07                                \n\t"
        "mtc1       %[tmp0],    %[ftmp4]                            \n\t"

        "1:                                                         \n\t"
        PUT_VP8_EPEL8_V6_MMI(%[src], %[src1], %[dst], %[srcstride])

        "addiu      %[h],       %[h],           -0x01               \n\t"
        PTR_ADDU   "%[src],     %[src],         %[srcstride]        \n\t"
        PTR_ADDU   "%[dst],     %[dst],         %[dststride]        \n\t"
        "bnez       %[h],       1b                                  \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),            [ftmp7]"=&f"(ftmp[7]),
          [ftmp8]"=&f"(ftmp[8]),
          [tmp0]"=&r"(tmp[0]),
          RESTRICT_ASM_ALL64
          [src1]"=&r"(src1),
          [h]"+&r"(h),
          [dst]"+&r"(dst),                  [src]"+&r"(src)
        : [ff_pw_64]"f"(ff_pw_64),
          [srcstride]"r"((mips_reg)srcstride),
          [dststride]"r"((mips_reg)dststride),
          [filter0]"f"(filter[0]),          [filter1]"f"(filter[1]),
          [filter2]"f"(filter[2]),          [filter3]"f"(filter[3]),
          [filter4]"f"(filter[4]),          [filter5]"f"(filter[5])
        : "memory"
    );
#else
    const uint8_t *filter = subpel_filters[my - 1];
    const uint8_t *cm     = ff_crop_tab + MAX_NEG_CROP;
    int x, y;

    for (y = 0; y < h; y++) {
        for (x = 0; x < 8; x++)
            dst[x] = FILTER_6TAP(src, filter, srcstride);
        dst += dststride;
        src += srcstride;
    }
#endif
}

void ff_put_vp8_epel4_v6_mmi(uint8_t *dst, ptrdiff_t dststride, uint8_t *src,
        ptrdiff_t srcstride, int h, int mx, int my)
{
#if 1
    const uint64_t *filter = fourtap_subpel_filters[my - 1];
    double ftmp[6];
    uint32_t tmp[1];
    mips_reg src1;
    DECLARE_VAR_LOW32;

    /*
    dst[0] = cm[(filter[2]*src[0] - filter[1]*src[0-srcstride] + filter[0]*src[0-2*srcstride] + filter[3]*src[0+srcstride] - filter[4]*src[0+2*srcstride] + filter[5]*src[0+3*srcstride] + 64) >> 7];
    dst[1] = cm[(filter[2]*src[1] - filter[1]*src[1-srcstride] + filter[0]*src[1-2*srcstride] + filter[3]*src[1+srcstride] - filter[4]*src[1+2*srcstride] + filter[5]*src[1+3*srcstride] + 64) >> 7];
    dst[2] = cm[(filter[2]*src[2] - filter[1]*src[2-srcstride] + filter[0]*src[2-2*srcstride] + filter[3]*src[2+srcstride] - filter[4]*src[2+2*srcstride] + filter[5]*src[2+3*srcstride] + 64) >> 7];
    dst[3] = cm[(filter[2]*src[3] - filter[1]*src[3-srcstride] + filter[0]*src[3-2*srcstride] + filter[3]*src[3+srcstride] - filter[4]*src[3+2*srcstride] + filter[5]*src[3+3*srcstride] + 64) >> 7];
    */
    __asm__ volatile (
        "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]            \n\t"
        "li         %[tmp0],    0x07                                \n\t"
        "mtc1       %[tmp0],    %[ftmp4]                            \n\t"

        "1:                                                         \n\t"
        PUT_VP8_EPEL4_V6_MMI(%[src], %[src1], %[dst], %[srcstride])

        "addiu      %[h],       %[h],           -0x01               \n\t"
        PTR_ADDU   "%[src],     %[src],         %[srcstride]        \n\t"
        PTR_ADDU   "%[dst],     %[dst],         %[dststride]        \n\t"
        "bnez       %[h],       1b                                  \n\t"
        : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
          [tmp0]"=&r"(tmp[0]),
          RESTRICT_ASM_LOW32
          [src1]"=&r"(src1),
          [h]"+&r"(h),
          [dst]"+&r"(dst),                  [src]"+&r"(src)
        : [ff_pw_64]"f"(ff_pw_64),
          [srcstride]"r"((mips_reg)srcstride),
          [dststride]"r"((mips_reg)dststride),
          [filter0]"f"(filter[0]),          [filter1]"f"(filter[1]),
          [filter2]"f"(filter[2]),          [filter3]"f"(filter[3]),
          [filter4]"f"(filter[4]),          [filter5]"f"(filter[5])
        : "memory"
    );
#else
    const uint8_t *filter = subpel_filters[my - 1];
    const uint8_t *cm     = ff_crop_tab + MAX_NEG_CROP;
    int x, y;

    for (y = 0; y < h; y++) {
        for (x = 0; x < 4; x++)
            dst[x] = FILTER_6TAP(src, filter, srcstride);
        dst += dststride;
        src += srcstride;
    }
#endif
}

void ff_put_vp8_epel16_h4v4_mmi(uint8_t *dst, ptrdiff_t dststride, uint8_t *src,
        ptrdiff_t srcstride, int h, int mx, int my)
{
#if 1
    DECLARE_ALIGNED(8, uint8_t, tmp_array[560]);
    uint8_t *tmp = tmp_array;

    src -= srcstride;
    ff_put_vp8_epel16_h4_mmi(tmp, 16, src, srcstride, h + 3, mx, my);
    tmp = tmp_array + 16;
    ff_put_vp8_epel16_v4_mmi(dst, dststride, tmp, 16, h, mx, my);
#else
    const uint8_t *filter = subpel_filters[mx - 1];
    const uint8_t *cm     = ff_crop_tab + MAX_NEG_CROP;
    int x, y;
    uint8_t tmp_array[560];
    uint8_t *tmp = tmp_array;

    src -= srcstride;

    for (y = 0; y < h + 3; y++) {
        for (x = 0; x < 16; x++)
            tmp[x] = FILTER_4TAP(src, filter, 1);
        tmp += 16;
        src += srcstride;
    }

    tmp    = tmp_array + 16;
    filter = subpel_filters[my - 1];

    for (y = 0; y < h; y++) {
        for (x = 0; x < 16; x++)
            dst[x] = FILTER_4TAP(tmp, filter, 16);
        dst += dststride;
        tmp += 16;
    }
#endif
}

void ff_put_vp8_epel8_h4v4_mmi(uint8_t *dst, ptrdiff_t dststride, uint8_t *src,
        ptrdiff_t srcstride, int h, int mx, int my)
{
#if 1
    DECLARE_ALIGNED(8, uint8_t, tmp_array[152]);
    uint8_t *tmp = tmp_array;

    src -= srcstride;
    ff_put_vp8_epel8_h4_mmi(tmp, 8, src, srcstride, h + 3, mx, my);
    tmp = tmp_array + 8;
    ff_put_vp8_epel8_v4_mmi(dst, dststride, tmp, 8, h, mx, my);
#else
    const uint8_t *filter = subpel_filters[mx - 1];
    const uint8_t *cm     = ff_crop_tab + MAX_NEG_CROP;
    int x, y;
    uint8_t tmp_array[152];
    uint8_t *tmp = tmp_array;

    src -= srcstride;

    for (y = 0; y < h + 3; y++) {
        for (x = 0; x < 8; x++)
            tmp[x] = FILTER_4TAP(src, filter, 1);
        tmp += 8;
        src += srcstride;
    }

    tmp    = tmp_array + 8;
    filter = subpel_filters[my - 1];

    for (y = 0; y < h; y++) {
        for (x = 0; x < 8; x++)
            dst[x] = FILTER_4TAP(tmp, filter, 8);
        dst += dststride;
        tmp += 8;
    }
#endif
}

void ff_put_vp8_epel4_h4v4_mmi(uint8_t *dst, ptrdiff_t dststride, uint8_t *src,
        ptrdiff_t srcstride, int h, int mx, int my)
{
#if 1
    DECLARE_ALIGNED(4, uint8_t, tmp_array[44]);
    uint8_t *tmp = tmp_array;

    src -= srcstride;
    ff_put_vp8_epel4_h4_mmi(tmp, 4, src, srcstride, h + 3, mx, my);
    tmp = tmp_array + 4;
    ff_put_vp8_epel4_v4_mmi(dst, dststride, tmp, 4, h, mx, my);
#else
    const uint8_t *filter = subpel_filters[mx - 1];
    const uint8_t *cm     = ff_crop_tab + MAX_NEG_CROP;
    int x, y;
    uint8_t tmp_array[44];
    uint8_t *tmp = tmp_array;

    src -= srcstride;

    for (y = 0; y < h + 3; y++) {
        for (x = 0; x < 4; x++)
            tmp[x] = FILTER_4TAP(src, filter, 1);
        tmp += 4;
        src += srcstride;
    }
    tmp    = tmp_array + 4;
    filter = subpel_filters[my - 1];

    for (y = 0; y < h; y++) {
        for (x = 0; x < 4; x++)
            dst[x] = FILTER_4TAP(tmp, filter, 4);
        dst += dststride;
        tmp += 4;
    }
#endif
}

void ff_put_vp8_epel16_h4v6_mmi(uint8_t *dst, ptrdiff_t dststride, uint8_t *src,
        ptrdiff_t srcstride, int h, int mx, int my)
{
#if 1
    DECLARE_ALIGNED(8, uint8_t, tmp_array[592]);
    uint8_t *tmp = tmp_array;

    src -= 2 * srcstride;
    ff_put_vp8_epel16_h4_mmi(tmp, 16, src, srcstride, h + 5, mx, my);
    tmp    = tmp_array + 32;
    ff_put_vp8_epel16_v6_mmi(dst, dststride, tmp, 16, h, mx, my);
#else
    const uint8_t *filter = subpel_filters[mx - 1];
    const uint8_t *cm     = ff_crop_tab + MAX_NEG_CROP;
    int x, y;
    uint8_t tmp_array[592];
    uint8_t *tmp = tmp_array;

    src -= 2 * srcstride;

    for (y = 0; y < h + 5; y++) {
        for (x = 0; x < 16; x++)
            tmp[x] = FILTER_4TAP(src, filter, 1);
        tmp += 16;
        src += srcstride;
    }

    tmp    = tmp_array + 32;
    filter = subpel_filters[my - 1];

    for (y = 0; y < h; y++) {
        for (x = 0; x < 16; x++)
            dst[x] = FILTER_6TAP(tmp, filter, 16);
        dst += dststride;
        tmp += 16;
    }
#endif
}

void ff_put_vp8_epel8_h4v6_mmi(uint8_t *dst, ptrdiff_t dststride, uint8_t *src,
        ptrdiff_t srcstride, int h, int mx, int my)
{
#if 1
    DECLARE_ALIGNED(8, uint8_t, tmp_array[168]);
    uint8_t *tmp = tmp_array;

    src -= 2 * srcstride;
    ff_put_vp8_epel8_h4_mmi(tmp, 8, src, srcstride, h + 5, mx, my);
    tmp    = tmp_array + 16;
    ff_put_vp8_epel8_v6_mmi(dst, dststride, tmp, 8, h, mx, my);
#else
    const uint8_t *filter = subpel_filters[mx - 1];
    const uint8_t *cm     = ff_crop_tab + MAX_NEG_CROP;
    int x, y;
    uint8_t tmp_array[168];
    uint8_t *tmp = tmp_array;

    src -= 2 * srcstride;

    for (y = 0; y < h + 5; y++) {
        for (x = 0; x < 8; x++)
            tmp[x] = FILTER_4TAP(src, filter, 1);
        tmp += 8;
        src += srcstride;
    }

    tmp    = tmp_array + 16;
    filter = subpel_filters[my - 1];

    for (y = 0; y < h; y++) {
        for (x = 0; x < 8; x++)
            dst[x] = FILTER_6TAP(tmp, filter, 8);
        dst += dststride;
        tmp += 8;
    }
#endif
}

void ff_put_vp8_epel4_h4v6_mmi(uint8_t *dst, ptrdiff_t dststride, uint8_t *src,
        ptrdiff_t srcstride, int h, int mx, int my)
{
#if 1
    DECLARE_ALIGNED(4, uint8_t, tmp_array[52]);
    uint8_t *tmp = tmp_array;

    src -= 2 * srcstride;
    ff_put_vp8_epel4_h4_mmi(tmp, 4, src, srcstride, h + 5, mx, my);
    tmp    = tmp_array + 8;
    ff_put_vp8_epel4_v6_mmi(dst, dststride, tmp, 4, h, mx, my);
#else
    const uint8_t *filter = subpel_filters[mx - 1];
    const uint8_t *cm     = ff_crop_tab + MAX_NEG_CROP;
    int x, y;
    uint8_t tmp_array[52];
    uint8_t *tmp = tmp_array;

    src -= 2 * srcstride;

    for (y = 0; y < h + 5; y++) {
        for (x = 0; x < 4; x++)
            tmp[x] = FILTER_4TAP(src, filter, 1);
        tmp += 4;
        src += srcstride;
    }

    tmp    = tmp_array + 8;
    filter = subpel_filters[my - 1];

    for (y = 0; y < h; y++) {
        for (x = 0; x < 4; x++)
            dst[x] = FILTER_6TAP(tmp, filter, 4);
        dst += dststride;
        tmp += 4;
    }
#endif
}

void ff_put_vp8_epel16_h6v4_mmi(uint8_t *dst, ptrdiff_t dststride, uint8_t *src,
        ptrdiff_t srcstride, int h, int mx, int my)
{
#if 1
    DECLARE_ALIGNED(8, uint8_t, tmp_array[560]);
    uint8_t *tmp = tmp_array;

    src -= srcstride;
    ff_put_vp8_epel16_h6_mmi(tmp, 16, src, srcstride, h + 3, mx, my);
    tmp    = tmp_array + 16;
    ff_put_vp8_epel16_v4_mmi(dst, dststride, tmp, 16, h, mx, my);
#else
    const uint8_t *filter = subpel_filters[mx - 1];
    const uint8_t *cm     = ff_crop_tab + MAX_NEG_CROP;
    int x, y;
    uint8_t tmp_array[560];
    uint8_t *tmp = tmp_array;

    src -= srcstride;

    for (y = 0; y < h + 3; y++) {
        for (x = 0; x < 16; x++)
            tmp[x] = FILTER_6TAP(src, filter, 1);
        tmp += 16;
        src += srcstride;
    }

    tmp    = tmp_array + 16;
    filter = subpel_filters[my - 1];

    for (y = 0; y < h; y++) {
        for (x = 0; x < 16; x++)
            dst[x] = FILTER_4TAP(tmp, filter, 16);
        dst += dststride;
        tmp += 16;
    }
#endif
}

void ff_put_vp8_epel8_h6v4_mmi(uint8_t *dst, ptrdiff_t dststride, uint8_t *src,
        ptrdiff_t srcstride, int h, int mx, int my)
{
#if 1
    DECLARE_ALIGNED(8, uint8_t, tmp_array[152]);
    uint8_t *tmp = tmp_array;

    src -= srcstride;
    ff_put_vp8_epel8_h6_mmi(tmp, 8, src, srcstride, h + 3, mx, my);
    tmp    = tmp_array + 8;
    ff_put_vp8_epel8_v4_mmi(dst, dststride, tmp, 8, h, mx, my);
#else
    const uint8_t *filter = subpel_filters[mx - 1];
    const uint8_t *cm     = ff_crop_tab + MAX_NEG_CROP;
    int x, y;
    uint8_t tmp_array[152];
    uint8_t *tmp = tmp_array;

    src -= srcstride;

    for (y = 0; y < h + 3; y++) {
        for (x = 0; x < 8; x++)
            tmp[x] = FILTER_6TAP(src, filter, 1);
        tmp += 8;
        src += srcstride;
    }

    tmp    = tmp_array + 8;
    filter = subpel_filters[my - 1];

    for (y = 0; y < h; y++) {
        for (x = 0; x < 8; x++)
            dst[x] = FILTER_4TAP(tmp, filter, 8);
        dst += dststride;
        tmp += 8;
    }
#endif
}

void ff_put_vp8_epel4_h6v4_mmi(uint8_t *dst, ptrdiff_t dststride, uint8_t *src,
        ptrdiff_t srcstride, int h, int mx, int my)
{
#if 1
    DECLARE_ALIGNED(4, uint8_t, tmp_array[44]);
    uint8_t *tmp = tmp_array;

    src -= srcstride;
    ff_put_vp8_epel4_h6_mmi(tmp, 4, src, srcstride, h + 3, mx, my);
    tmp    = tmp_array + 4;
    ff_put_vp8_epel4_v4_mmi(dst, dststride, tmp, 4, h, mx, my);
#else
    const uint8_t *filter = subpel_filters[mx - 1];
    const uint8_t *cm     = ff_crop_tab + MAX_NEG_CROP;
    int x, y;
    uint8_t tmp_array[44];
    uint8_t *tmp = tmp_array;

    src -= srcstride;

    for (y = 0; y < h + 3; y++) {
        for (x = 0; x < 4; x++)
            tmp[x] = FILTER_6TAP(src, filter, 1);
        tmp += 4;
        src += srcstride;
    }

    tmp    = tmp_array + 4;
    filter = subpel_filters[my - 1];

    for (y = 0; y < h; y++) {
        for (x = 0; x < 4; x++)
            dst[x] = FILTER_4TAP(tmp, filter, 4);
        dst += dststride;
        tmp += 4;
    }
#endif
}

void ff_put_vp8_epel16_h6v6_mmi(uint8_t *dst, ptrdiff_t dststride, uint8_t *src,
        ptrdiff_t srcstride, int h, int mx, int my)
{
#if 1
    DECLARE_ALIGNED(8, uint8_t, tmp_array[592]);
    uint8_t *tmp = tmp_array;

    src -= 2 * srcstride;
    ff_put_vp8_epel16_h6_mmi(tmp, 16, src, srcstride, h + 5, mx, my);
    tmp    = tmp_array + 32;
    ff_put_vp8_epel16_v6_mmi(dst, dststride, tmp, 16, h, mx, my);
#else
    const uint8_t *filter = subpel_filters[mx - 1];
    const uint8_t *cm     = ff_crop_tab + MAX_NEG_CROP;
    int x, y;
    uint8_t tmp_array[592];
    uint8_t *tmp = tmp_array;

    src -= 2 * srcstride;

    for (y = 0; y < h + 5; y++) {
        for (x = 0; x < 16; x++)
            tmp[x] = FILTER_6TAP(src, filter, 1);
        tmp += 16;
        src += srcstride;
    }

    tmp    = tmp_array + 32;
    filter = subpel_filters[my - 1];

    for (y = 0; y < h; y++) {
        for (x = 0; x < 16; x++)
            dst[x] = FILTER_6TAP(tmp, filter, 16);
        dst += dststride;
        tmp += 16;
    }
#endif
}

void ff_put_vp8_epel8_h6v6_mmi(uint8_t *dst, ptrdiff_t dststride, uint8_t *src,
        ptrdiff_t srcstride, int h, int mx, int my)
{
#if 1
    DECLARE_ALIGNED(8, uint8_t, tmp_array[168]);
    uint8_t *tmp = tmp_array;

    src -= 2 * srcstride;
    ff_put_vp8_epel8_h6_mmi(tmp, 8, src, srcstride, h + 5, mx, my);
    tmp    = tmp_array + 16;
    ff_put_vp8_epel8_v6_mmi(dst, dststride, tmp, 8, h, mx, my);
#else
    const uint8_t *filter = subpel_filters[mx - 1];
    const uint8_t *cm     = ff_crop_tab + MAX_NEG_CROP;
    int x, y;
    uint8_t tmp_array[168];
    uint8_t *tmp = tmp_array;

    src -= 2 * srcstride;

    for (y = 0; y < h + 5; y++) {
        for (x = 0; x < 8; x++)
            tmp[x] = FILTER_6TAP(src, filter, 1);
        tmp += 8;
        src += srcstride;
    }

    tmp    = tmp_array + 16;
    filter = subpel_filters[my - 1];

    for (y = 0; y < h; y++) {
        for (x = 0; x < 8; x++)
            dst[x] = FILTER_6TAP(tmp, filter, 8);
        dst += dststride;
        tmp += 8;
    }
#endif
}

void ff_put_vp8_epel4_h6v6_mmi(uint8_t *dst, ptrdiff_t dststride, uint8_t *src,
        ptrdiff_t srcstride, int h, int mx, int my)
{
#if 1
    DECLARE_ALIGNED(4, uint8_t, tmp_array[52]);
    uint8_t *tmp = tmp_array;

    src -= 2 * srcstride;
    ff_put_vp8_epel4_h6_mmi(tmp, 4, src, srcstride, h + 5, mx, my);
    tmp    = tmp_array + 8;
    ff_put_vp8_epel4_v6_mmi(dst, dststride, tmp, 4, h, mx, my);
#else
    const uint8_t *filter = subpel_filters[mx - 1];
    const uint8_t *cm     = ff_crop_tab + MAX_NEG_CROP;
    int x, y;
    uint8_t tmp_array[52];
    uint8_t *tmp = tmp_array;

    src -= 2 * srcstride;

    for (y = 0; y < h + 5; y++) {
        for (x = 0; x < 4; x++)
            tmp[x] = FILTER_6TAP(src, filter, 1);
        tmp += 4;
        src += srcstride;
    }

    tmp    = tmp_array + 8;
    filter = subpel_filters[my - 1];

    for (y = 0; y < h; y++) {
        for (x = 0; x < 4; x++)
            dst[x] = FILTER_6TAP(tmp, filter, 4);
        dst += dststride;
        tmp += 4;
    }
#endif
}

void ff_put_vp8_bilinear16_h_mmi(uint8_t *dst, ptrdiff_t dstride, uint8_t *src,
        ptrdiff_t sstride, int h, int mx, int my)
{
#if 1
    int a = 8 - mx, b = mx;
    double ftmp[7];
    uint32_t tmp[1];
    mips_reg dst0, src0;
    DECLARE_VAR_ALL64;

    /*
    dst[0] = (a * src[0] + b * src[1] + 4) >> 3;
    dst[1] = (a * src[1] + b * src[2] + 4) >> 3;
    dst[2] = (a * src[2] + b * src[3] + 4) >> 3;
    dst[3] = (a * src[3] + b * src[4] + 4) >> 3;
    dst[4] = (a * src[4] + b * src[5] + 4) >> 3;
    dst[5] = (a * src[5] + b * src[6] + 4) >> 3;
    dst[6] = (a * src[6] + b * src[7] + 4) >> 3;
    dst[7] = (a * src[7] + b * src[8] + 4) >> 3;

    dst[ 8] = (a * src[ 8] + b * src[ 9] + 4) >> 3;
    dst[ 9] = (a * src[ 9] + b * src[10] + 4) >> 3;
    dst[10] = (a * src[10] + b * src[11] + 4) >> 3;
    dst[11] = (a * src[11] + b * src[12] + 4) >> 3;
    dst[12] = (a * src[12] + b * src[13] + 4) >> 3;
    dst[13] = (a * src[13] + b * src[14] + 4) >> 3;
    dst[14] = (a * src[14] + b * src[15] + 4) >> 3;
    dst[15] = (a * src[15] + b * src[16] + 4) >> 3;
    */
    __asm__ volatile (
        "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]            \n\t"
        "li         %[tmp0],    0x03                                \n\t"
        "mtc1       %[tmp0],    %[ftmp4]                            \n\t"
        "pshufh     %[a],       %[a],           %[ftmp0]            \n\t"
        "pshufh     %[b],       %[b],           %[ftmp0]            \n\t"

        "1:                                                         \n\t"
        // 0 - 7
        PUT_VP8_BILINEAR8_H_MMI(%[src], %[dst])
        PTR_ADDIU  "%[src0],    %[src],         0x08                \n\t"
        PTR_ADDIU  "%[dst0],    %[dst],         0x08                \n\t"
        // 8 - 15
        PUT_VP8_BILINEAR8_H_MMI(%[src0], %[dst0])

        "addiu      %[h],       %[h],           -0x01               \n\t"
        PTR_ADDU   "%[src],     %[src],         %[sstride]          \n\t"
        PTR_ADDU   "%[dst],     %[dst],         %[dstride]          \n\t"
        "bnez       %[h],       1b                                  \n\t"
        : [ftmp0]"=&f"(ftmp[0]),        [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),        [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),        [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),
          [tmp0]"=&r"(tmp[0]),
          RESTRICT_ASM_ALL64
          [dst0]"=&r"(dst0),            [src0]"=&r"(src0),
          [h]"+&r"(h),
          [dst]"+&r"(dst),              [src]"+&r"(src),
          [a]"+&f"(a),                  [b]"+&f"(b)
        : [sstride]"r"((mips_reg)sstride),
          [dstride]"r"((mips_reg)dstride),
          [ff_pw_4]"f"(ff_pw_4)
        : "memory"
    );
#else
    int a = 8 - mx, b = mx;
    int x, y;

    for (y = 0; y < h; y++) {
        for (x = 0; x < 16; x++)
            dst[x] = (a * src[x] + b * src[x + 1] + 4) >> 3;
        dst += dstride;
        src += sstride;
    }
#endif
}

void ff_put_vp8_bilinear16_v_mmi(uint8_t *dst, ptrdiff_t dstride, uint8_t *src,
        ptrdiff_t sstride, int h, int mx, int my)
{
#if 1
    int c = 8 - my, d = my;
    double ftmp[7];
    uint32_t tmp[1];
    mips_reg src0, src1, dst0;
    DECLARE_VAR_ALL64;

    /*
    dst[0] = (c * src[0] + d * src[    sstride] + 4) >> 3;
    dst[1] = (c * src[1] + d * src[1 + sstride] + 4) >> 3;
    dst[2] = (c * src[2] + d * src[2 + sstride] + 4) >> 3;
    dst[3] = (c * src[3] + d * src[3 + sstride] + 4) >> 3;
    dst[4] = (c * src[4] + d * src[4 + sstride] + 4) >> 3;
    dst[5] = (c * src[5] + d * src[5 + sstride] + 4) >> 3;
    dst[6] = (c * src[6] + d * src[6 + sstride] + 4) >> 3;
    dst[7] = (c * src[7] + d * src[7 + sstride] + 4) >> 3;
    */
    __asm__ volatile (
        "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]            \n\t"
        "li         %[tmp0],    0x03                                \n\t"
        "mtc1       %[tmp0],    %[ftmp4]                            \n\t"
        "pshufh     %[c],       %[c],           %[ftmp0]            \n\t"
        "pshufh     %[d],       %[d],           %[ftmp0]            \n\t"

        "1:                                                         \n\t"
        // 0 - 7
        PUT_VP8_BILINEAR8_V_MMI(%[src], %[src1], %[dst], %[sstride])
        PTR_ADDIU  "%[src0],    %[src],         0x08                \n\t"
        PTR_ADDIU  "%[dst0],    %[dst],         0x08                \n\t"
        // 8 - 15
        PUT_VP8_BILINEAR8_V_MMI(%[src0], %[src1], %[dst0], %[sstride])

        "addiu      %[h],       %[h],           -0x01               \n\t"
        PTR_ADDU   "%[src],     %[src],         %[sstride]          \n\t"
        PTR_ADDU   "%[dst],     %[dst],         %[dstride]          \n\t"
        "bnez       %[h],       1b                                  \n\t"
        : [ftmp0]"=&f"(ftmp[0]),        [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),        [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),        [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),
          [tmp0]"=&r"(tmp[0]),
          RESTRICT_ASM_ALL64
          [src0]"=&r"(src0),            [dst0]"=&r"(dst0),
          [src1]"=&r"(src1),
          [h]"+&r"(h),
          [dst]"+&r"(dst),              [src]"+&r"(src),
          [c]"+&f"(c),                  [d]"+&f"(d)
        : [sstride]"r"((mips_reg)sstride),
          [dstride]"r"((mips_reg)dstride),
          [ff_pw_4]"f"(ff_pw_4)
        : "memory"
    );
#else
    int c = 8 - my, d = my;
    int x, y;

    for (y = 0; y < h; y++) {
        for (x = 0; x < 16; x++)
            dst[x] = (c * src[x] + d * src[x + sstride] + 4) >> 3;
        dst += dstride;
        src += sstride;
    }
#endif
}

void ff_put_vp8_bilinear16_hv_mmi(uint8_t *dst, ptrdiff_t dstride, uint8_t *src,
        ptrdiff_t sstride, int h, int mx, int my)
{
#if 1
    DECLARE_ALIGNED(8, uint8_t, tmp_array[528]);
    uint8_t *tmp = tmp_array;

    ff_put_vp8_bilinear16_h_mmi(tmp, 16, src, sstride, h + 1, mx, my);
    ff_put_vp8_bilinear16_v_mmi(dst, dstride, tmp, 16, h, mx, my);
#else
    int a = 8 - mx, b = mx;
    int c = 8 - my, d = my;
    int x, y;
    uint8_t tmp_array[528];
    uint8_t *tmp = tmp_array;

    for (y = 0; y < h + 1; y++) {
        for (x = 0; x < 16; x++)
            tmp[x] = (a * src[x] + b * src[x + 1] + 4) >> 3;
        tmp += 16;
        src += sstride;
    }

    tmp = tmp_array;

    for (y = 0; y < h; y++) {
        for (x = 0; x < 16; x++)
            dst[x] = (c * tmp[x] + d * tmp[x + 16] + 4) >> 3;
        dst += dstride;
        tmp += 16;
    }
#endif
}

void ff_put_vp8_bilinear8_h_mmi(uint8_t *dst, ptrdiff_t dstride, uint8_t *src,
        ptrdiff_t sstride, int h, int mx, int my)
{
#if 1
    int a = 8 - mx, b = mx;
    double ftmp[7];
    uint32_t tmp[1];
    DECLARE_VAR_ALL64;

    /*
    dst[0] = (a * src[0] + b * src[1] + 4) >> 3;
    dst[1] = (a * src[1] + b * src[2] + 4) >> 3;
    dst[2] = (a * src[2] + b * src[3] + 4) >> 3;
    dst[3] = (a * src[3] + b * src[4] + 4) >> 3;
    dst[4] = (a * src[4] + b * src[5] + 4) >> 3;
    dst[5] = (a * src[5] + b * src[6] + 4) >> 3;
    dst[6] = (a * src[6] + b * src[7] + 4) >> 3;
    dst[7] = (a * src[7] + b * src[8] + 4) >> 3;
    */
    __asm__ volatile (
        "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]            \n\t"
        "li         %[tmp0],    0x03                                \n\t"
        "mtc1       %[tmp0],    %[ftmp4]                            \n\t"
        "pshufh     %[a],       %[a],           %[ftmp0]            \n\t"
        "pshufh     %[b],       %[b],           %[ftmp0]            \n\t"

        "1:                                                         \n\t"
        PUT_VP8_BILINEAR8_H_MMI(%[src], %[dst])

        "addiu      %[h],       %[h],           -0x01               \n\t"
        PTR_ADDU   "%[src],     %[src],         %[sstride]          \n\t"
        PTR_ADDU   "%[dst],     %[dst],         %[dstride]          \n\t"
        "bnez       %[h],       1b                                  \n\t"
        : [ftmp0]"=&f"(ftmp[0]),        [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),        [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),        [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),
          [tmp0]"=&r"(tmp[0]),
          RESTRICT_ASM_ALL64
          [h]"+&r"(h),
          [dst]"+&r"(dst),              [src]"+&r"(src),
          [a]"+&f"(a),                  [b]"+&f"(b)
        : [sstride]"r"((mips_reg)sstride),
          [dstride]"r"((mips_reg)dstride),
          [ff_pw_4]"f"(ff_pw_4)
        : "memory"
    );
#else
    int a = 8 - mx, b = mx;
    int x, y;

    for (y = 0; y < h; y++) {
        for (x = 0; x < 8; x++)
            dst[x] = (a * src[x] + b * src[x + 1] + 4) >> 3;
        dst += dstride;
        src += sstride;
    }
#endif
}

void ff_put_vp8_bilinear8_v_mmi(uint8_t *dst, ptrdiff_t dstride, uint8_t *src,
        ptrdiff_t sstride, int h, int mx, int my)
{
#if 1
    int c = 8 - my, d = my;
    double ftmp[7];
    uint32_t tmp[1];
    mips_reg src1;
    DECLARE_VAR_ALL64;

    /*
    dst[0] = (c * src[0] + d * src[    sstride] + 4) >> 3;
    dst[1] = (c * src[1] + d * src[1 + sstride] + 4) >> 3;
    dst[2] = (c * src[2] + d * src[2 + sstride] + 4) >> 3;
    dst[3] = (c * src[3] + d * src[3 + sstride] + 4) >> 3;
    dst[4] = (c * src[4] + d * src[4 + sstride] + 4) >> 3;
    dst[5] = (c * src[5] + d * src[5 + sstride] + 4) >> 3;
    dst[6] = (c * src[6] + d * src[6 + sstride] + 4) >> 3;
    dst[7] = (c * src[7] + d * src[7 + sstride] + 4) >> 3;
    */
    __asm__ volatile (
        "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]            \n\t"
        "li         %[tmp0],    0x03                                \n\t"
        "mtc1       %[tmp0],    %[ftmp4]                            \n\t"
        "pshufh     %[c],       %[c],           %[ftmp0]            \n\t"
        "pshufh     %[d],       %[d],           %[ftmp0]            \n\t"

        "1:                                                         \n\t"
        PUT_VP8_BILINEAR8_V_MMI(%[src], %[src1], %[dst], %[sstride])

        "addiu      %[h],       %[h],           -0x01               \n\t"
        PTR_ADDU   "%[src],     %[src],         %[sstride]          \n\t"
        PTR_ADDU   "%[dst],     %[dst],         %[dstride]          \n\t"
        "bnez       %[h],       1b                                  \n\t"
        : [ftmp0]"=&f"(ftmp[0]),        [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),        [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),        [ftmp5]"=&f"(ftmp[5]),
          [ftmp6]"=&f"(ftmp[6]),
          [tmp0]"=&r"(tmp[0]),
          RESTRICT_ASM_ALL64
          [src1]"=&r"(src1),
          [h]"+&r"(h),
          [dst]"+&r"(dst),              [src]"+&r"(src),
          [c]"+&f"(c),                  [d]"+&f"(d)
        : [sstride]"r"((mips_reg)sstride),
          [dstride]"r"((mips_reg)dstride),
          [ff_pw_4]"f"(ff_pw_4)
        : "memory"
    );
#else
    int c = 8 - my, d = my;
    int x, y;

    for (y = 0; y < h; y++) {
        for (x = 0; x < 8; x++)
            dst[x] = (c * src[x] + d * src[x + sstride] + 4) >> 3;
        dst += dstride;
        src += sstride;
    }
#endif
}

void ff_put_vp8_bilinear8_hv_mmi(uint8_t *dst, ptrdiff_t dstride, uint8_t *src,
        ptrdiff_t sstride, int h, int mx, int my)
{
#if 1
    DECLARE_ALIGNED(8, uint8_t, tmp_array[136]);
    uint8_t *tmp = tmp_array;

    ff_put_vp8_bilinear8_h_mmi(tmp, 8, src, sstride, h + 1, mx, my);
    ff_put_vp8_bilinear8_v_mmi(dst, dstride, tmp, 8, h, mx, my);
#else
    int a = 8 - mx, b = mx;
    int c = 8 - my, d = my;
    int x, y;
    uint8_t tmp_array[136];
    uint8_t *tmp = tmp_array;

    for (y = 0; y < h + 1; y++) {
        for (x = 0; x < 8; x++)
            tmp[x] = (a * src[x] + b * src[x + 1] + 4) >> 3;
        tmp += 8;
        src += sstride;
    }

    tmp = tmp_array;

    for (y = 0; y < h; y++) {
        for (x = 0; x < 8; x++)
            dst[x] = (c * tmp[x] + d * tmp[x + 8] + 4) >> 3;
        dst += dstride;
        tmp += 8;
    }
#endif
}

void ff_put_vp8_bilinear4_h_mmi(uint8_t *dst, ptrdiff_t dstride, uint8_t *src,
        ptrdiff_t sstride, int h, int mx, int my)
{
#if 1
    int a = 8 - mx, b = mx;
    double ftmp[5];
    uint32_t tmp[1];
    DECLARE_VAR_LOW32;
    DECLARE_VAR_ALL64;

    /*
    dst[0] = (a * src[0] + b * src[1] + 4) >> 3;
    dst[1] = (a * src[1] + b * src[2] + 4) >> 3;
    dst[2] = (a * src[2] + b * src[3] + 4) >> 3;
    dst[3] = (a * src[3] + b * src[4] + 4) >> 3;
    */
    __asm__ volatile (
        "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]            \n\t"
        "li         %[tmp0],    0x03                                \n\t"
        "mtc1       %[tmp0],    %[ftmp4]                            \n\t"
        "pshufh     %[a],       %[a],           %[ftmp0]            \n\t"
        "pshufh     %[b],       %[b],           %[ftmp0]            \n\t"

        "1:                                                         \n\t"
        PUT_VP8_BILINEAR4_H_MMI(%[src], %[dst])

        "addiu      %[h],       %[h],           -0x01               \n\t"
        PTR_ADDU   "%[src],     %[src],         %[sstride]          \n\t"
        PTR_ADDU   "%[dst],     %[dst],         %[dstride]          \n\t"
        "bnez       %[h],       1b                                  \n\t"
        : [ftmp0]"=&f"(ftmp[0]),        [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),        [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),
          [tmp0]"=&r"(tmp[0]),
          RESTRICT_ASM_LOW32
          RESTRICT_ASM_ALL64
          [h]"+&r"(h),
          [dst]"+&r"(dst),              [src]"+&r"(src),
          [a]"+&f"(a),                  [b]"+&f"(b)
        : [sstride]"r"((mips_reg)sstride),
          [dstride]"r"((mips_reg)dstride),
          [ff_pw_4]"f"(ff_pw_4)
        : "memory"
    );
#else
    int a = 8 - mx, b = mx;
    int x, y;

    for (y = 0; y < h; y++) {
        for (x = 0; x < 4; x++)
            dst[x] = (a * src[x] + b * src[x + 1] + 4) >> 3;
        dst += dstride;
        src += sstride;
    }
#endif
}

void ff_put_vp8_bilinear4_v_mmi(uint8_t *dst, ptrdiff_t dstride, uint8_t *src,
        ptrdiff_t sstride, int h, int mx, int my)
{
#if 1
    int c = 8 - my, d = my;
    double ftmp[7];
    uint32_t tmp[1];
    mips_reg src1;
    DECLARE_VAR_LOW32;
    DECLARE_VAR_ALL64;

    /*
    dst[0] = (c * src[0] + d * src[    sstride] + 4) >> 3;
    dst[1] = (c * src[1] + d * src[1 + sstride] + 4) >> 3;
    dst[2] = (c * src[2] + d * src[2 + sstride] + 4) >> 3;
    dst[3] = (c * src[3] + d * src[3 + sstride] + 4) >> 3;
    */
    __asm__ volatile (
        "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]            \n\t"
        "li         %[tmp0],    0x03                                \n\t"
        "mtc1       %[tmp0],    %[ftmp4]                            \n\t"
        "pshufh     %[c],       %[c],           %[ftmp0]            \n\t"
        "pshufh     %[d],       %[d],           %[ftmp0]            \n\t"

        "1:                                                         \n\t"
        PUT_VP8_BILINEAR4_V_MMI(%[src], %[src1], %[dst], %[sstride])

        "addiu      %[h],       %[h],           -0x01               \n\t"
        PTR_ADDU   "%[src],     %[src],         %[sstride]          \n\t"
        PTR_ADDU   "%[dst],     %[dst],         %[dstride]          \n\t"
        "bnez       %[h],       1b                                  \n\t"
        : [ftmp0]"=&f"(ftmp[0]),        [ftmp1]"=&f"(ftmp[1]),
          [ftmp2]"=&f"(ftmp[2]),        [ftmp3]"=&f"(ftmp[3]),
          [ftmp4]"=&f"(ftmp[4]),
          [tmp0]"=&r"(tmp[0]),
          RESTRICT_ASM_LOW32
          RESTRICT_ASM_ALL64
          [src1]"=&r"(src1),
          [h]"+&r"(h),
          [dst]"+&r"(dst),              [src]"+&r"(src),
          [c]"+&f"(c),                  [d]"+&f"(d)
        : [sstride]"r"((mips_reg)sstride),
          [dstride]"r"((mips_reg)dstride),
          [ff_pw_4]"f"(ff_pw_4)
        : "memory"
    );
#else
    int c = 8 - my, d = my;
    int x, y;

    for (y = 0; y < h; y++) {
        for (x = 0; x < 4; x++)
            dst[x] = (c * src[x] + d * src[x + sstride] + 4) >> 3;
        dst += dstride;
        src += sstride;
    }
#endif
}

void ff_put_vp8_bilinear4_hv_mmi(uint8_t *dst, ptrdiff_t dstride, uint8_t *src,
        ptrdiff_t sstride, int h, int mx, int my)
{
#if 1
    DECLARE_ALIGNED(4, uint8_t, tmp_array[36]);
    uint8_t *tmp = tmp_array;

    ff_put_vp8_bilinear4_h_mmi(tmp, 4, src, sstride, h + 1, mx, my);
    ff_put_vp8_bilinear4_v_mmi(dst, dstride, tmp, 4, h, mx, my);
#else
    int a = 8 - mx, b = mx;
    int c = 8 - my, d = my;
    int x, y;
    uint8_t tmp_array[36];
    uint8_t *tmp = tmp_array;

    for (y = 0; y < h + 1; y++) {
        for (x = 0; x < 4; x++)
            tmp[x] = (a * src[x] + b * src[x + 1] + 4) >> 3;
        tmp += 4;
        src += sstride;
    }

    tmp = tmp_array;

    for (y = 0; y < h; y++) {
        for (x = 0; x < 4; x++)
            dst[x] = (c * tmp[x] + d * tmp[x + 4] + 4) >> 3;
        dst += dstride;
        tmp += 4;
    }
#endif
}
