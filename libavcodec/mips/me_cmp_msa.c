/*
 * Copyright (c) 2015 Parag Salasakar (Parag.Salasakar@imgtec.com)
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

#include "libavutil/mips/generic_macros_msa.h"
#include "me_cmp_mips.h"

static uint32_t sad_8width_msa(const uint8_t *src, int32_t src_stride,
                               const uint8_t *ref, int32_t ref_stride,
                               int32_t height)
{
    int32_t ht_cnt = height >> 2;
    int res = (height & 0x03);
    v16u8 src0, src1, src2, src3, ref0, ref1, ref2, ref3;
    v8u16 zero = { 0 };
    v8u16 sad = { 0 };

    for (; ht_cnt--; ) {
        LD_UB4(src, src_stride, src0, src1, src2, src3);
        src += (4 * src_stride);
        LD_UB4(ref, ref_stride, ref0, ref1, ref2, ref3);
        ref += (4 * ref_stride);

        PCKEV_D4_UB(src1, src0, src3, src2, ref1, ref0, ref3, ref2,
                    src0, src1, ref0, ref1);
        sad += SAD_UB2_UH(src0, src1, ref0, ref1);
    }
    for (; res--; ) {
        v16u8 diff;
        src0 = LD_UB(src);
        ref0 = LD_UB(ref);
        src += src_stride;
        ref += ref_stride;
        diff = __msa_asub_u_b((v16u8) src0, (v16u8) ref0);
        diff = (v16u8)__msa_ilvr_d((v2i64)zero, (v2i64)diff);
        sad += __msa_hadd_u_h((v16u8) diff, (v16u8) diff);
    }

    return (HADD_UH_U32(sad));
}

static uint32_t sad_16width_msa(const uint8_t *src, int32_t src_stride,
                                const uint8_t *ref, int32_t ref_stride,
                                int32_t height)
{
    int32_t ht_cnt = height >> 2;
    int res = (height & 0x03);
    v16u8 src0, src1, ref0, ref1;
    v8u16 sad = { 0 };

    for (; ht_cnt--; ) {
        LD_UB2(src, src_stride, src0, src1);
        src += (2 * src_stride);
        LD_UB2(ref, ref_stride, ref0, ref1);
        ref += (2 * ref_stride);
        sad += SAD_UB2_UH(src0, src1, ref0, ref1);

        LD_UB2(src, src_stride, src0, src1);
        src += (2 * src_stride);
        LD_UB2(ref, ref_stride, ref0, ref1);
        ref += (2 * ref_stride);
        sad += SAD_UB2_UH(src0, src1, ref0, ref1);
    }
    for (; res > 0; res--) {
        v16u8 diff;
        src0 = LD_UB(src);
        ref0 = LD_UB(ref);
        src += src_stride;
        ref += ref_stride;
        diff = __msa_asub_u_b((v16u8) src0, (v16u8) ref0);
        sad += __msa_hadd_u_h((v16u8) diff, (v16u8) diff);
    }
    return (HADD_UH_U32(sad));
}

static uint32_t sad_horiz_bilinear_filter_8width_msa(const uint8_t *src,
                                                     int32_t src_stride,
                                                     const uint8_t *ref,
                                                     int32_t ref_stride,
                                                     int32_t height)
{
    int32_t ht_cnt = height >> 3;
    int32_t res = height & 0x07;
    v16u8 src0, src1, src2, src3, comp0, comp1;
    v16u8 ref0, ref1, ref2, ref3, ref4, ref5;
    v8u16 zero = { 0 };
    v8u16 sad = { 0 };

    for (; ht_cnt--; ) {
        LD_UB4(src, src_stride, src0, src1, src2, src3);
        src += (4 * src_stride);
        LD_UB4(ref, ref_stride, ref0, ref1, ref2, ref3);
        ref += (4 * ref_stride);

        PCKEV_D2_UB(src1, src0, src3, src2, src0, src1);
        PCKEV_D2_UB(ref1, ref0, ref3, ref2, ref4, ref5);
        SLDI_B4_UB(ref0, ref0, ref1, ref1, ref2, ref2, ref3, ref3, 1,
                   ref0, ref1, ref2, ref3);
        PCKEV_D2_UB(ref1, ref0, ref3, ref2, ref0, ref1);
        AVER_UB2_UB(ref4, ref0, ref5, ref1, comp0, comp1);
        sad += SAD_UB2_UH(src0, src1, comp0, comp1);

        LD_UB4(src, src_stride, src0, src1, src2, src3);
        src += (4 * src_stride);
        LD_UB4(ref, ref_stride, ref0, ref1, ref2, ref3);
        ref += (4 * ref_stride);

        PCKEV_D2_UB(src1, src0, src3, src2, src0, src1);
        PCKEV_D2_UB(ref1, ref0, ref3, ref2, ref4, ref5);
        SLDI_B4_UB(ref0, ref0, ref1, ref1, ref2, ref2, ref3, ref3, 1,
                   ref0, ref1, ref2, ref3);
        PCKEV_D2_UB(ref1, ref0, ref3, ref2, ref0, ref1);
        AVER_UB2_UB(ref4, ref0, ref5, ref1, comp0, comp1);
        sad += SAD_UB2_UH(src0, src1, comp0, comp1);
    }

    for (; res--; ) {
        v16u8 diff;
        src0 = LD_UB(src);
        ref0 = LD_UB(ref);
        ref1 = LD_UB(ref + 1);
        src += src_stride;
        ref += ref_stride;
        comp0 = (v16u8)__msa_aver_u_b((v16u8) ref0, (v16u8) ref1);
        diff = __msa_asub_u_b((v16u8) src0, (v16u8) comp0);
        diff = (v16u8)__msa_ilvr_d((v2i64) zero, (v2i64) diff);
        sad += __msa_hadd_u_h((v16u8) diff, (v16u8) diff);
    }
    return (HADD_UH_U32(sad));
}

static uint32_t sad_horiz_bilinear_filter_16width_msa(const uint8_t *src,
                                                      int32_t src_stride,
                                                      const uint8_t *ref,
                                                      int32_t ref_stride,
                                                      int32_t height)
{
    int32_t ht_cnt = height >> 3;
    int32_t res = height & 0x07;
    v16u8 src0, src1, src2, src3, comp0, comp1;
    v16u8 ref00, ref10, ref20, ref30, ref01, ref11, ref21, ref31;
    v8u16 sad = { 0 };

    for (; ht_cnt--; ) {
        LD_UB4(src, src_stride, src0, src1, src2, src3);
        src += (4 * src_stride);
        LD_UB4(ref, ref_stride, ref00, ref10, ref20, ref30);
        LD_UB4(ref + 1, ref_stride, ref01, ref11, ref21, ref31);
        ref += (4 * ref_stride);

        AVER_UB2_UB(ref01, ref00, ref11, ref10, comp0, comp1);
        sad += SAD_UB2_UH(src0, src1, comp0, comp1);
        AVER_UB2_UB(ref21, ref20, ref31, ref30, comp0, comp1);
        sad += SAD_UB2_UH(src2, src3, comp0, comp1);

        LD_UB4(src, src_stride, src0, src1, src2, src3);
        src += (4 * src_stride);
        LD_UB4(ref, ref_stride, ref00, ref10, ref20, ref30);
        LD_UB4(ref + 1, ref_stride, ref01, ref11, ref21, ref31);
        ref += (4 * ref_stride);

        AVER_UB2_UB(ref01, ref00, ref11, ref10, comp0, comp1);
        sad += SAD_UB2_UH(src0, src1, comp0, comp1);
        AVER_UB2_UB(ref21, ref20, ref31, ref30, comp0, comp1);
        sad += SAD_UB2_UH(src2, src3, comp0, comp1);
    }

    for (; res--; ) {
        v16u8 diff;
        src0  = LD_UB(src);
        ref00 = LD_UB(ref);
        ref01 = LD_UB(ref + 1);
        src += src_stride;
        ref += ref_stride;
        comp0 = (v16u8)__msa_aver_u_b((v16u8) ref00, (v16u8) ref01);
        diff = __msa_asub_u_b((v16u8) src0, (v16u8) comp0);
        sad += __msa_hadd_u_h((v16u8) diff, (v16u8) diff);
    }
    return (HADD_UH_U32(sad));
}

static uint32_t sad_vert_bilinear_filter_8width_msa(const uint8_t *src,
                                                    int32_t src_stride,
                                                    const uint8_t *ref,
                                                    int32_t ref_stride,
                                                    int32_t height)
{
    int32_t ht_cnt = height >> 3;
    int32_t res = height & 0x07;
    v16u8 src0, src1, src2, src3, comp0, comp1;
    v16u8 ref0, ref1, ref2, ref3, ref4;
    v8u16 zero = { 0 };
    v8u16 sad = { 0 };

    for (; ht_cnt--; ) {
        LD_UB4(src, src_stride, src0, src1, src2, src3);
        src += (4 * src_stride);
        LD_UB5(ref, ref_stride, ref0, ref1, ref2, ref3, ref4);
        ref += (4 * ref_stride);

        PCKEV_D2_UB(src1, src0, src3, src2, src0, src1);
        PCKEV_D2_UB(ref1, ref0, ref2, ref1, ref0, ref1);
        PCKEV_D2_UB(ref3, ref2, ref4, ref3, ref2, ref3);
        AVER_UB2_UB(ref1, ref0, ref3, ref2, comp0, comp1);
        sad += SAD_UB2_UH(src0, src1, comp0, comp1);

        LD_UB4(src, src_stride, src0, src1, src2, src3);
        src += (4 * src_stride);
        LD_UB5(ref, ref_stride, ref0, ref1, ref2, ref3, ref4);
        ref += (4 * ref_stride);

        PCKEV_D2_UB(src1, src0, src3, src2, src0, src1);
        PCKEV_D2_UB(ref1, ref0, ref2, ref1, ref0, ref1);
        PCKEV_D2_UB(ref3, ref2, ref4, ref3, ref2, ref3);
        AVER_UB2_UB(ref1, ref0, ref3, ref2, comp0, comp1);
        sad += SAD_UB2_UH(src0, src1, comp0, comp1);
    }

    for (; res--; ) {
        v16u8 diff;
        src0 = LD_UB(src);
        LD_UB2(ref, ref_stride, ref0, ref1);
        src += src_stride;
        ref += ref_stride;
        comp0 = (v16u8)__msa_aver_u_b((v16u8) ref0, (v16u8) ref1);
        diff = __msa_asub_u_b((v16u8) src0, (v16u8) comp0);
        diff = (v16u8)__msa_ilvr_d((v2i64) zero, (v2i64) diff);
        sad += __msa_hadd_u_h((v16u8) diff, (v16u8) diff);
    }
    return (HADD_UH_U32(sad));
}

static uint32_t sad_vert_bilinear_filter_16width_msa(const uint8_t *src,
                                                     int32_t src_stride,
                                                     const uint8_t *ref,
                                                     int32_t ref_stride,
                                                     int32_t height)
{
    int32_t ht_cnt = height >> 3;
    int32_t res = height & 0x07;
    v16u8 src0, src1, src2, src3, comp0, comp1;
    v16u8 ref0, ref1, ref2, ref3, ref4;
    v8u16 sad = { 0 };

    for (; ht_cnt--; ) {
        LD_UB5(ref, ref_stride, ref4, ref0, ref1, ref2, ref3);
        ref += (5 * ref_stride);
        LD_UB4(src, src_stride, src0, src1, src2, src3);
        src += (4 * src_stride);

        AVER_UB2_UB(ref0, ref4, ref1, ref0, comp0, comp1);
        sad += SAD_UB2_UH(src0, src1, comp0, comp1);
        AVER_UB2_UB(ref2, ref1, ref3, ref2, comp0, comp1);
        sad += SAD_UB2_UH(src2, src3, comp0, comp1);

        ref4 = ref3;

        LD_UB4(ref, ref_stride, ref0, ref1, ref2, ref3);
        ref += (3 * ref_stride);
        LD_UB4(src, src_stride, src0, src1, src2, src3);
        src += (4 * src_stride);

        AVER_UB2_UB(ref0, ref4, ref1, ref0, comp0, comp1);
        sad += SAD_UB2_UH(src0, src1, comp0, comp1);
        AVER_UB2_UB(ref2, ref1, ref3, ref2, comp0, comp1);
        sad += SAD_UB2_UH(src2, src3, comp0, comp1);
    }

    for (; res--; ) {
        v16u8 diff;
        src0 = LD_UB(src);
        LD_UB2(ref, ref_stride, ref0, ref1);
        src += src_stride;
        ref += ref_stride;
        comp0 = (v16u8)__msa_aver_u_b((v16u8) ref0, (v16u8) ref1);
        diff = __msa_asub_u_b((v16u8) src0, (v16u8) comp0);
        sad += __msa_hadd_u_h((v16u8) diff, (v16u8) diff);
    }
    return (HADD_UH_U32(sad));
}

static uint32_t sad_hv_bilinear_filter_8width_msa(const uint8_t *src,
                                                  int32_t src_stride,
                                                  const uint8_t *ref,
                                                  int32_t ref_stride,
                                                  int32_t height)
{
    int32_t ht_cnt = height >> 2;
    int32_t res = height & 0x03;
    v16u8 src0, src1, src2, src3, temp0, temp1, diff;
    v16u8 ref0, ref1, ref2, ref3, ref4;
    v16i8 mask = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };
    v8u16 comp0, comp1, comp2, comp3;
    v8u16 zero = { 0 };
    v8u16 sad = { 0 };

    for (ht_cnt = (height >> 2); ht_cnt--;) {
        LD_UB5(ref, ref_stride, ref4, ref0, ref1, ref2, ref3);
        ref += (4 * ref_stride);
        LD_UB4(src, src_stride, src0, src1, src2, src3);
        src += (4 * src_stride);

        PCKEV_D2_UB(src1, src0, src3, src2, src0, src1);

        VSHF_B2_UB(ref4, ref4, ref0, ref0, mask, mask, temp0, temp1);
        comp0 = __msa_hadd_u_h(temp0, temp0);
        comp1 = __msa_hadd_u_h(temp1, temp1);
        comp0 += comp1;
        comp0 = (v8u16) __msa_srari_h((v8i16) comp0, 2);
        comp0 = (v8u16) __msa_pckev_b((v16i8) comp0, (v16i8) comp0);

        temp0 = (v16u8) __msa_vshf_b(mask, (v16i8) ref1, (v16i8) ref1);
        comp2 = __msa_hadd_u_h(temp0, temp0);
        comp1 += comp2;
        comp1 = (v8u16) __msa_srari_h((v8i16) comp1, 2);
        comp1 = (v8u16) __msa_pckev_b((v16i8) comp1, (v16i8) comp1);
        comp1 = (v8u16) __msa_pckev_d((v2i64) comp1, (v2i64) comp0);
        diff = (v16u8) __msa_asub_u_b(src0, (v16u8) comp1);
        sad += __msa_hadd_u_h(diff, diff);

        temp1 = (v16u8) __msa_vshf_b(mask, (v16i8) ref2, (v16i8) ref2);
        comp3 = __msa_hadd_u_h(temp1, temp1);
        comp2 += comp3;
        comp2 = (v8u16) __msa_srari_h((v8i16) comp2, 2);
        comp2 = (v8u16) __msa_pckev_b((v16i8) comp2, (v16i8) comp2);

        temp0 = (v16u8) __msa_vshf_b(mask, (v16i8) ref3, (v16i8) ref3);
        comp0 = __msa_hadd_u_h(temp0, temp0);
        comp3 += comp0;
        comp3 = (v8u16) __msa_srari_h((v8i16) comp3, 2);
        comp3 = (v8u16) __msa_pckev_b((v16i8) comp3, (v16i8) comp3);
        comp3 = (v8u16) __msa_pckev_d((v2i64) comp3, (v2i64) comp2);
        diff = (v16u8) __msa_asub_u_b(src1, (v16u8) comp3);
        sad += __msa_hadd_u_h(diff, diff);
    }

    for (; res--; ) {
        src0 = LD_UB(src);
        LD_UB2(ref, ref_stride, ref0, ref1);
        temp0 = (v16u8) __msa_vshf_b(mask, (v16i8) ref0, (v16i8) ref0);
        temp1 = (v16u8) __msa_vshf_b(mask, (v16i8) ref1, (v16i8) ref1);
        src += src_stride;
        ref += ref_stride;
        comp0 = __msa_hadd_u_h(temp0, temp0);
        comp2 = __msa_hadd_u_h(temp1, temp1);
        comp2 += comp0;
        comp2 = (v8u16)__msa_srari_h((v8i16) comp2, 2);
        comp0 = (v16u8) __msa_pckev_b((v16i8) zero, (v16i8) comp2);
        diff = __msa_asub_u_b(src0, comp0);
        diff = (v16u8)__msa_ilvr_d((v2i64) zero, (v2i64) diff);
        sad += __msa_hadd_u_h(diff, diff);
    }
    return (HADD_UH_U32(sad));
}

static uint32_t sad_hv_bilinear_filter_16width_msa(const uint8_t *src,
                                                   int32_t src_stride,
                                                   const uint8_t *ref,
                                                   int32_t ref_stride,
                                                   int32_t height)
{
    int32_t ht_cnt = height >> 3;
    int32_t res = height & 0x07;
    v16u8 src0, src1, src2, src3, comp, diff;
    v16u8 temp0, temp1, temp2, temp3;
    v16u8 ref00, ref01, ref02, ref03, ref04, ref10, ref11, ref12, ref13, ref14;
    v8u16 comp0, comp1, comp2, comp3;
    v8u16 sad = { 0 };

    for (; ht_cnt--; ) {
        LD_UB4(src, src_stride, src0, src1, src2, src3);
        src += (4 * src_stride);
        LD_UB5(ref, ref_stride, ref04, ref00, ref01, ref02, ref03);
        LD_UB5(ref + 1, ref_stride, ref14, ref10, ref11, ref12, ref13);
        ref += (5 * ref_stride);

        ILVRL_B2_UB(ref14, ref04, temp0, temp1);
        comp0 = __msa_hadd_u_h(temp0, temp0);
        comp1 = __msa_hadd_u_h(temp1, temp1);
        ILVRL_B2_UB(ref10, ref00, temp2, temp3);
        comp2 = __msa_hadd_u_h(temp2, temp2);
        comp3 = __msa_hadd_u_h(temp3, temp3);
        comp0 += comp2;
        comp1 += comp3;
        SRARI_H2_UH(comp0, comp1, 2);
        comp = (v16u8) __msa_pckev_b((v16i8) comp1, (v16i8) comp0);
        diff = __msa_asub_u_b(src0, comp);
        sad += __msa_hadd_u_h(diff, diff);

        ILVRL_B2_UB(ref11, ref01, temp0, temp1);
        comp0 = __msa_hadd_u_h(temp0, temp0);
        comp1 = __msa_hadd_u_h(temp1, temp1);
        comp2 += comp0;
        comp3 += comp1;
        SRARI_H2_UH(comp2, comp3, 2);
        comp = (v16u8) __msa_pckev_b((v16i8) comp3, (v16i8) comp2);
        diff = __msa_asub_u_b(src1, comp);
        sad += __msa_hadd_u_h(diff, diff);

        ILVRL_B2_UB(ref12, ref02, temp2, temp3);
        comp2 = __msa_hadd_u_h(temp2, temp2);
        comp3 = __msa_hadd_u_h(temp3, temp3);
        comp0 += comp2;
        comp1 += comp3;
        SRARI_H2_UH(comp0, comp1, 2);
        comp = (v16u8) __msa_pckev_b((v16i8) comp1, (v16i8) comp0);
        diff = __msa_asub_u_b(src2, comp);
        sad += __msa_hadd_u_h(diff, diff);

        ILVRL_B2_UB(ref13, ref03, temp0, temp1);
        comp0 = __msa_hadd_u_h(temp0, temp0);
        comp1 = __msa_hadd_u_h(temp1, temp1);
        comp2 += comp0;
        comp3 += comp1;
        SRARI_H2_UH(comp2, comp3, 2);
        comp = (v16u8) __msa_pckev_b((v16i8) comp3, (v16i8) comp2);
        diff = __msa_asub_u_b(src3, comp);
        sad += __msa_hadd_u_h(diff, diff);

        LD_UB4(src, src_stride, src0, src1, src2, src3);
        src += (4 * src_stride);
        LD_UB4(ref, ref_stride, ref00, ref01, ref02, ref03);
        LD_UB4(ref + 1, ref_stride, ref10, ref11, ref12, ref13);
        ref += (3 * ref_stride);

        ILVRL_B2_UB(ref10, ref00, temp2, temp3);
        comp2 = __msa_hadd_u_h(temp2, temp2);
        comp3 = __msa_hadd_u_h(temp3, temp3);
        comp0 += comp2;
        comp1 += comp3;
        SRARI_H2_UH(comp0, comp1, 2);
        comp = (v16u8) __msa_pckev_b((v16i8) comp1, (v16i8) comp0);
        diff = __msa_asub_u_b(src0, comp);
        sad += __msa_hadd_u_h(diff, diff);

        ILVRL_B2_UB(ref11, ref01, temp0, temp1);
        comp0 = __msa_hadd_u_h(temp0, temp0);
        comp1 = __msa_hadd_u_h(temp1, temp1);
        comp2 += comp0;
        comp3 += comp1;
        SRARI_H2_UH(comp2, comp3, 2);
        comp = (v16u8) __msa_pckev_b((v16i8) comp3, (v16i8) comp2);
        diff = __msa_asub_u_b(src1, comp);
        sad += __msa_hadd_u_h(diff, diff);

        ILVRL_B2_UB(ref12, ref02, temp2, temp3);
        comp2 = __msa_hadd_u_h(temp2, temp2);
        comp3 = __msa_hadd_u_h(temp3, temp3);
        comp0 += comp2;
        comp1 += comp3;
        SRARI_H2_UH(comp0, comp1, 2);
        comp = (v16u8) __msa_pckev_b((v16i8) comp1, (v16i8) comp0);
        diff = __msa_asub_u_b(src2, comp);
        sad += __msa_hadd_u_h(diff, diff);

        ILVRL_B2_UB(ref13, ref03, temp0, temp1);
        comp0 = __msa_hadd_u_h(temp0, temp0);
        comp1 = __msa_hadd_u_h(temp1, temp1);
        comp2 += comp0;
        comp3 += comp1;
        SRARI_H2_UH(comp2, comp3, 2);
        comp = (v16u8) __msa_pckev_b((v16i8) comp3, (v16i8) comp2);
        diff = __msa_asub_u_b(src3, comp);
        sad += __msa_hadd_u_h(diff, diff);
    }
    for (; res--; ) {
        src0 = LD_UB(src);
        LD_UB2(ref, ref_stride, ref00, ref10);
        LD_UB2(ref + 1, ref_stride, ref01, ref11);
        src += src_stride;
        ref += ref_stride;
        ILVRL_B2_UB(ref10, ref00, temp0, temp1);
        ILVRL_B2_UB(ref11, ref01, temp2, temp3);
        comp0 = __msa_hadd_u_h(temp0, temp0);
        comp1 = __msa_hadd_u_h(temp1, temp1);
        comp2 = __msa_hadd_u_h(temp2, temp2);
        comp3 = __msa_hadd_u_h(temp3, temp3);
        comp2 += comp0;
        comp3 += comp1;
        SRARI_H2_UH(comp2, comp3, 2);
        comp = (v16u8) __msa_pckev_b((v16i8) comp3, (v16i8) comp2);
        diff = __msa_asub_u_b(src0, comp);
        sad += __msa_hadd_u_h(diff, diff);
    }

    return (HADD_UH_U32(sad));
}

#define CALC_MSE_B(src, ref, var)                                    \
{                                                                    \
    v16u8 src_l0_m, src_l1_m;                                        \
    v8i16 res_l0_m, res_l1_m;                                        \
                                                                     \
    ILVRL_B2_UB(src, ref, src_l0_m, src_l1_m);                       \
    HSUB_UB2_SH(src_l0_m, src_l1_m, res_l0_m, res_l1_m);             \
    DPADD_SH2_SW(res_l0_m, res_l1_m, res_l0_m, res_l1_m, var, var);  \
}

static uint32_t sse_4width_msa(const uint8_t *src_ptr, int32_t src_stride,
                               const uint8_t *ref_ptr, int32_t ref_stride,
                               int32_t height)
{
    int32_t ht_cnt = height >> 2;
    int32_t res = height & 0x03;
    uint32_t sse;
    uint32_t src0, src1, src2, src3;
    uint32_t ref0, ref1, ref2, ref3;
    v16u8 src  = { 0 };
    v16u8 ref  = { 0 };
    v16u8 zero = { 0 };
    v4i32 var  = { 0 };

    for (; ht_cnt--; ) {
        LW4(src_ptr, src_stride, src0, src1, src2, src3);
        src_ptr += (4 * src_stride);
        LW4(ref_ptr, ref_stride, ref0, ref1, ref2, ref3);
        ref_ptr += (4 * ref_stride);

        INSERT_W4_UB(src0, src1, src2, src3, src);
        INSERT_W4_UB(ref0, ref1, ref2, ref3, ref);
        CALC_MSE_B(src, ref, var);
    }

    for (; res--; ) {
        v16u8 reg0;
        v8i16 tmp0;
        src0 = LW(src_ptr);
        ref0 = LW(ref_ptr);
        src_ptr += src_stride;
        ref_ptr += ref_stride;
        src  = (v16u8)__msa_insert_w((v4i32) src, 0, src0);
        ref  = (v16u8)__msa_insert_w((v4i32) ref, 0, ref0);
        reg0 = (v16u8)__msa_ilvr_b(src, ref);
        reg0 = (v16u8)__msa_ilvr_d((v2i64) zero, (v2i64) reg0);
        tmp0 = (v8i16)__msa_hsub_u_h((v16u8) reg0, (v16u8) reg0);
        var  = (v4i32)__msa_dpadd_s_w((v4i32) var, (v8i16) tmp0, (v8i16) tmp0);
    }
    sse = HADD_SW_S32(var);

    return sse;
}

static uint32_t sse_8width_msa(const uint8_t *src_ptr, int32_t src_stride,
                               const uint8_t *ref_ptr, int32_t ref_stride,
                               int32_t height)
{
    int32_t ht_cnt = height >> 2;
    int32_t res = height & 0x03;
    uint32_t sse;
    v16u8 src0, src1, src2, src3;
    v16u8 ref0, ref1, ref2, ref3;
    v4i32 var = { 0 };

    for (; ht_cnt--; ) {
        LD_UB4(src_ptr, src_stride, src0, src1, src2, src3);
        src_ptr += (4 * src_stride);
        LD_UB4(ref_ptr, ref_stride, ref0, ref1, ref2, ref3);
        ref_ptr += (4 * ref_stride);

        PCKEV_D4_UB(src1, src0, src3, src2, ref1, ref0, ref3, ref2,
                    src0, src1, ref0, ref1);
        CALC_MSE_B(src0, ref0, var);
        CALC_MSE_B(src1, ref1, var);
    }

    for (; res--; ) {
        v8i16 tmp0;
        src0 = LD_UB(src_ptr);
        ref0 = LD_UB(ref_ptr);
        src_ptr += src_stride;
        ref_ptr += ref_stride;
        ref1 = (v16u8)__msa_ilvr_b(src0, ref0);
        tmp0 = (v8i16)__msa_hsub_u_h((v16u8) ref1, (v16u8) ref1);
        var  = (v4i32)__msa_dpadd_s_w((v4i32) var, (v8i16) tmp0, (v8i16) tmp0);
    }
    sse = HADD_SW_S32(var);

    return sse;
}

static uint32_t sse_16width_msa(const uint8_t *src_ptr, int32_t src_stride,
                                const uint8_t *ref_ptr, int32_t ref_stride,
                                int32_t height)
{
    int32_t ht_cnt = height >> 2;
    int32_t res = height & 0x03;
    uint32_t sse;
    v16u8 src, ref;
    v4i32 var = { 0 };

    for (; ht_cnt--; ) {
        src = LD_UB(src_ptr);
        src_ptr += src_stride;
        ref = LD_UB(ref_ptr);
        ref_ptr += ref_stride;
        CALC_MSE_B(src, ref, var);

        src = LD_UB(src_ptr);
        src_ptr += src_stride;
        ref = LD_UB(ref_ptr);
        ref_ptr += ref_stride;
        CALC_MSE_B(src, ref, var);

        src = LD_UB(src_ptr);
        src_ptr += src_stride;
        ref = LD_UB(ref_ptr);
        ref_ptr += ref_stride;
        CALC_MSE_B(src, ref, var);

        src = LD_UB(src_ptr);
        src_ptr += src_stride;
        ref = LD_UB(ref_ptr);
        ref_ptr += ref_stride;
        CALC_MSE_B(src, ref, var);
    }

    for (; res--; ) {
        src = LD_UB(src_ptr);
        src_ptr += src_stride;
        ref = LD_UB(ref_ptr);
        ref_ptr += ref_stride;
        CALC_MSE_B(src, ref, var);
    }

    sse = HADD_SW_S32(var);

    return sse;
}

static int32_t hadamard_diff_8x8_msa(const uint8_t *src, int32_t src_stride,
                                     const uint8_t *ref, int32_t ref_stride)
{
    v16u8 src0, src1, src2, src3, src4, src5, src6, src7;
    v16u8 ref0, ref1, ref2, ref3, ref4, ref5, ref6, ref7;
    v8u16 diff0, diff1, diff2, diff3, diff4, diff5, diff6, diff7;
    v8u16 temp0, temp1, temp2, temp3, temp4, temp5, temp6, temp7;
    v8i16 sum = { 0 };
    v8i16 zero = { 0 };

    LD_UB8(src, src_stride, src0, src1, src2, src3, src4, src5, src6, src7);
    LD_UB8(ref, ref_stride, ref0, ref1, ref2, ref3, ref4, ref5, ref6, ref7);
    ILVR_B8_UH(src0, ref0, src1, ref1, src2, ref2, src3, ref3,
               src4, ref4, src5, ref5, src6, ref6, src7, ref7,
               diff0, diff1, diff2, diff3, diff4, diff5, diff6, diff7);
    HSUB_UB4_UH(diff0, diff1, diff2, diff3, diff0, diff1, diff2, diff3);
    HSUB_UB4_UH(diff4, diff5, diff6, diff7, diff4, diff5, diff6, diff7);
    TRANSPOSE8x8_UH_UH(diff0, diff1, diff2, diff3, diff4, diff5, diff6, diff7,
                       diff0, diff1, diff2, diff3, diff4, diff5, diff6, diff7);
    BUTTERFLY_8(diff0, diff2, diff4, diff6, diff7, diff5, diff3, diff1,
                temp0, temp2, temp4, temp6, temp7, temp5, temp3, temp1);
    BUTTERFLY_8(temp0, temp1, temp4, temp5, temp7, temp6, temp3, temp2,
                diff0, diff1, diff4, diff5, diff7, diff6, diff3, diff2);
    BUTTERFLY_8(diff0, diff1, diff2, diff3, diff7, diff6, diff5, diff4,
                temp0, temp1, temp2, temp3, temp7, temp6, temp5, temp4);
    TRANSPOSE8x8_UH_UH(temp0, temp1, temp2, temp3, temp4, temp5, temp6, temp7,
                       temp0, temp1, temp2, temp3, temp4, temp5, temp6, temp7);
    BUTTERFLY_8(temp0, temp2, temp4, temp6, temp7, temp5, temp3, temp1,
                diff0, diff2, diff4, diff6, diff7, diff5, diff3, diff1);
    BUTTERFLY_8(diff0, diff1, diff4, diff5, diff7, diff6, diff3, diff2,
                temp0, temp1, temp4, temp5, temp7, temp6, temp3, temp2);
    ADD4(temp0, temp4, temp1, temp5, temp2, temp6, temp3, temp7,
         diff0, diff1, diff2, diff3);
    sum = __msa_asub_s_h((v8i16) temp3, (v8i16) temp7);
    sum += __msa_asub_s_h((v8i16) temp2, (v8i16) temp6);
    sum += __msa_asub_s_h((v8i16) temp1, (v8i16) temp5);
    sum += __msa_asub_s_h((v8i16) temp0, (v8i16) temp4);
    sum += __msa_add_a_h((v8i16) diff0, zero);
    sum += __msa_add_a_h((v8i16) diff1, zero);
    sum += __msa_add_a_h((v8i16) diff2, zero);
    sum += __msa_add_a_h((v8i16) diff3, zero);

    return (HADD_UH_U32(sum));
}

static int32_t hadamard_intra_8x8_msa(const uint8_t *src, int32_t src_stride,
                                      const uint8_t *dumy, int32_t ref_stride)
{
    int32_t sum_res = 0;
    v16u8 src0, src1, src2, src3, src4, src5, src6, src7;
    v8u16 diff0, diff1, diff2, diff3, diff4, diff5, diff6, diff7;
    v8u16 temp0, temp1, temp2, temp3, temp4, temp5, temp6, temp7;
    v8i16 sum = { 0 };
    v16i8 zero = { 0 };

    LD_UB8(src, src_stride, src0, src1, src2, src3, src4, src5, src6, src7);
    TRANSPOSE8x8_UB_UB(src0, src1, src2, src3, src4, src5, src6, src7,
                       src0, src1, src2, src3, src4, src5, src6, src7);
    ILVR_B8_UH(zero, src0, zero, src1, zero, src2, zero, src3,
               zero, src4, zero, src5, zero, src6, zero, src7,
               diff0, diff1, diff2, diff3, diff4, diff5, diff6, diff7);
    BUTTERFLY_8(diff0, diff2, diff4, diff6, diff7, diff5, diff3, diff1,
                temp0, temp2, temp4, temp6, temp7, temp5, temp3, temp1);
    BUTTERFLY_8(temp0, temp1, temp4, temp5, temp7, temp6, temp3, temp2,
                diff0, diff1, diff4, diff5, diff7, diff6, diff3, diff2);
    BUTTERFLY_8(diff0, diff1, diff2, diff3, diff7, diff6, diff5, diff4,
                temp0, temp1, temp2, temp3, temp7, temp6, temp5, temp4);
    TRANSPOSE8x8_UH_UH(temp0, temp1, temp2, temp3, temp4, temp5, temp6, temp7,
                       temp0, temp1, temp2, temp3, temp4, temp5, temp6, temp7);
    BUTTERFLY_8(temp0, temp2, temp4, temp6, temp7, temp5, temp3, temp1,
                diff0, diff2, diff4, diff6, diff7, diff5, diff3, diff1);
    BUTTERFLY_8(diff0, diff1, diff4, diff5, diff7, diff6, diff3, diff2,
                temp0, temp1, temp4, temp5, temp7, temp6, temp3, temp2);
    ADD4(temp0, temp4, temp1, temp5, temp2, temp6, temp3, temp7,
         diff0, diff1, diff2, diff3);
    sum = __msa_asub_s_h((v8i16) temp3, (v8i16) temp7);
    sum += __msa_asub_s_h((v8i16) temp2, (v8i16) temp6);
    sum += __msa_asub_s_h((v8i16) temp1, (v8i16) temp5);
    sum += __msa_asub_s_h((v8i16) temp0, (v8i16) temp4);
    sum += __msa_add_a_h((v8i16) diff0, (v8i16) zero);
    sum += __msa_add_a_h((v8i16) diff1, (v8i16) zero);
    sum += __msa_add_a_h((v8i16) diff2, (v8i16) zero);
    sum += __msa_add_a_h((v8i16) diff3, (v8i16) zero);
    sum_res = (HADD_UH_U32(sum));
    sum_res -= abs(temp0[0] + temp4[0]);

    return sum_res;
}

int ff_pix_abs16_msa(MpegEncContext *v, const uint8_t *src, const uint8_t *ref,
                     ptrdiff_t stride, int height)
{
    return sad_16width_msa(src, stride, ref, stride, height);
}

int ff_pix_abs8_msa(MpegEncContext *v, const uint8_t *src, const uint8_t *ref,
                    ptrdiff_t stride, int height)
{
    return sad_8width_msa(src, stride, ref, stride, height);
}

int ff_pix_abs16_x2_msa(MpegEncContext *v, const uint8_t *pix1, const uint8_t *pix2,
                        ptrdiff_t stride, int h)
{
    return sad_horiz_bilinear_filter_16width_msa(pix1, stride, pix2, stride, h);
}

int ff_pix_abs16_y2_msa(MpegEncContext *v, const uint8_t *pix1, const uint8_t *pix2,
                        ptrdiff_t stride, int h)
{
    return sad_vert_bilinear_filter_16width_msa(pix1, stride, pix2, stride, h);
}

int ff_pix_abs16_xy2_msa(MpegEncContext *v, const uint8_t *pix1, const uint8_t *pix2,
                         ptrdiff_t stride, int h)
{
    return sad_hv_bilinear_filter_16width_msa(pix1, stride, pix2, stride, h);
}

int ff_pix_abs8_x2_msa(MpegEncContext *v, const uint8_t *pix1, const uint8_t *pix2,
                       ptrdiff_t stride, int h)
{
    return sad_horiz_bilinear_filter_8width_msa(pix1, stride, pix2, stride, h);
}

int ff_pix_abs8_y2_msa(MpegEncContext *v, const uint8_t *pix1, const uint8_t *pix2,
                       ptrdiff_t stride, int h)
{
    return sad_vert_bilinear_filter_8width_msa(pix1, stride, pix2, stride, h);
}

int ff_pix_abs8_xy2_msa(MpegEncContext *v, const uint8_t *pix1, const uint8_t *pix2,
                        ptrdiff_t stride, int h)
{
    return sad_hv_bilinear_filter_8width_msa(pix1, stride, pix2, stride, h);
}

int ff_sse16_msa(MpegEncContext *v, const uint8_t *src, const uint8_t *ref,
                 ptrdiff_t stride, int height)
{
    return sse_16width_msa(src, stride, ref, stride, height);
}

int ff_sse8_msa(MpegEncContext *v, const uint8_t *src, const uint8_t *ref,
                ptrdiff_t stride, int height)
{
    return sse_8width_msa(src, stride, ref, stride, height);
}

int ff_sse4_msa(MpegEncContext *v, const uint8_t *src, const uint8_t *ref,
                ptrdiff_t stride, int height)
{
    return sse_4width_msa(src, stride, ref, stride, height);
}

int ff_hadamard8_diff8x8_msa(MpegEncContext *s, const uint8_t *dst, const uint8_t *src,
                             ptrdiff_t stride, int h)
{
    return hadamard_diff_8x8_msa(src, stride, dst, stride);
}

int ff_hadamard8_intra8x8_msa(MpegEncContext *s, const uint8_t *src, const uint8_t *dummy,
                              ptrdiff_t stride, int h)
{
    return hadamard_intra_8x8_msa(src, stride, dummy, stride);
}

/* Hadamard Transform functions */
#define WRAPPER8_16_SQ(name8, name16)                      \
int name16(MpegEncContext *s, const uint8_t *dst, const uint8_t *src,  \
           ptrdiff_t stride, int h)                        \
{                                                          \
    int score = 0;                                         \
    score += name8(s, dst, src, stride, 8);                \
    score += name8(s, dst + 8, src + 8, stride, 8);        \
    if(h == 16) {                                          \
        dst += 8 * stride;                                 \
        src += 8 * stride;                                 \
        score +=name8(s, dst, src, stride, 8);             \
        score +=name8(s, dst + 8, src + 8, stride, 8);     \
    }                                                      \
    return score;                                          \
}

WRAPPER8_16_SQ(ff_hadamard8_diff8x8_msa, ff_hadamard8_diff16_msa);
WRAPPER8_16_SQ(ff_hadamard8_intra8x8_msa, ff_hadamard8_intra16_msa);
