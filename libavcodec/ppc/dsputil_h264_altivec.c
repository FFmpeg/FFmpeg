/*
 * Copyright (c) 2004 Romain Dolbeau <romain@dolbeau.org>
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
 
#include "../dsputil.h"

#include "gcc_fixes.h"

#include "dsputil_altivec.h"

#define PUT_OP_U8_ALTIVEC(d, s, dst) d = s
#define AVG_OP_U8_ALTIVEC(d, s, dst) d = vec_avg(dst, s)

#define OP_U8_ALTIVEC                          PUT_OP_U8_ALTIVEC
#define PREFIX_h264_chroma_mc8_altivec         put_h264_chroma_mc8_altivec
#define PREFIX_h264_chroma_mc8_num             altivec_put_h264_chroma_mc8_num
#define PREFIX_h264_qpel16_h_lowpass_altivec   put_h264_qpel16_h_lowpass_altivec
#define PREFIX_h264_qpel16_h_lowpass_num       altivec_put_h264_qpel16_h_lowpass_num
#define PREFIX_h264_qpel16_v_lowpass_altivec   put_h264_qpel16_v_lowpass_altivec
#define PREFIX_h264_qpel16_v_lowpass_num       altivec_put_h264_qpel16_v_lowpass_num
#define PREFIX_h264_qpel16_hv_lowpass_altivec  put_h264_qpel16_hv_lowpass_altivec
#define PREFIX_h264_qpel16_hv_lowpass_num      altivec_put_h264_qpel16_hv_lowpass_num
#include "dsputil_h264_template_altivec.c"
#undef OP_U8_ALTIVEC
#undef PREFIX_h264_chroma_mc8_altivec
#undef PREFIX_h264_chroma_mc8_num
#undef PREFIX_h264_qpel16_h_lowpass_altivec
#undef PREFIX_h264_qpel16_h_lowpass_num
#undef PREFIX_h264_qpel16_v_lowpass_altivec
#undef PREFIX_h264_qpel16_v_lowpass_num
#undef PREFIX_h264_qpel16_hv_lowpass_altivec
#undef PREFIX_h264_qpel16_hv_lowpass_num

#define OP_U8_ALTIVEC                          AVG_OP_U8_ALTIVEC
#define PREFIX_h264_chroma_mc8_altivec         avg_h264_chroma_mc8_altivec
#define PREFIX_h264_chroma_mc8_num             altivec_avg_h264_chroma_mc8_num
#define PREFIX_h264_qpel16_h_lowpass_altivec   avg_h264_qpel16_h_lowpass_altivec
#define PREFIX_h264_qpel16_h_lowpass_num       altivec_avg_h264_qpel16_h_lowpass_num
#define PREFIX_h264_qpel16_v_lowpass_altivec   avg_h264_qpel16_v_lowpass_altivec
#define PREFIX_h264_qpel16_v_lowpass_num       altivec_avg_h264_qpel16_v_lowpass_num
#define PREFIX_h264_qpel16_hv_lowpass_altivec  avg_h264_qpel16_hv_lowpass_altivec
#define PREFIX_h264_qpel16_hv_lowpass_num      altivec_avg_h264_qpel16_hv_lowpass_num
#include "dsputil_h264_template_altivec.c"
#undef OP_U8_ALTIVEC
#undef PREFIX_h264_chroma_mc8_altivec
#undef PREFIX_h264_chroma_mc8_num
#undef PREFIX_h264_qpel16_h_lowpass_altivec
#undef PREFIX_h264_qpel16_h_lowpass_num
#undef PREFIX_h264_qpel16_v_lowpass_altivec
#undef PREFIX_h264_qpel16_v_lowpass_num
#undef PREFIX_h264_qpel16_hv_lowpass_altivec
#undef PREFIX_h264_qpel16_hv_lowpass_num

#define H264_MC(OPNAME, SIZE, CODETYPE) \
static void OPNAME ## h264_qpel ## SIZE ## _mc00_ ## CODETYPE (uint8_t *dst, uint8_t *src, int stride){\
    OPNAME ## pixels ## SIZE ## _ ## CODETYPE(dst, src, stride, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc10_ ## CODETYPE(uint8_t *dst, uint8_t *src, int stride){ \
    uint64_t temp[SIZE*SIZE/8] __align16;\
    uint8_t * const half= (uint8_t*)temp;\
    put_h264_qpel ## SIZE ## _h_lowpass_ ## CODETYPE(half, src, SIZE, stride);\
    OPNAME ## pixels ## SIZE ## _l2_ ## CODETYPE(dst, src, half, stride, stride, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc20_ ## CODETYPE(uint8_t *dst, uint8_t *src, int stride){\
    OPNAME ## h264_qpel ## SIZE ## _h_lowpass_ ## CODETYPE(dst, src, stride, stride);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc30_ ## CODETYPE(uint8_t *dst, uint8_t *src, int stride){\
    uint64_t temp[SIZE*SIZE/8] __align16;\
    uint8_t * const half= (uint8_t*)temp;\
    put_h264_qpel ## SIZE ## _h_lowpass_ ## CODETYPE(half, src, SIZE, stride);\
    OPNAME ## pixels ## SIZE ## _l2_ ## CODETYPE(dst, src+1, half, stride, stride, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc01_ ## CODETYPE(uint8_t *dst, uint8_t *src, int stride){\
    uint64_t temp[SIZE*SIZE/8] __align16;\
    uint8_t * const half= (uint8_t*)temp;\
    put_h264_qpel ## SIZE ## _v_lowpass_ ## CODETYPE(half, src, SIZE, stride);\
    OPNAME ## pixels ## SIZE ## _l2_ ## CODETYPE(dst, src, half, stride, stride, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc02_ ## CODETYPE(uint8_t *dst, uint8_t *src, int stride){\
    OPNAME ## h264_qpel ## SIZE ## _v_lowpass_ ## CODETYPE(dst, src, stride, stride);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc03_ ## CODETYPE(uint8_t *dst, uint8_t *src, int stride){\
    uint64_t temp[SIZE*SIZE/8] __align16;\
    uint8_t * const half= (uint8_t*)temp;\
    put_h264_qpel ## SIZE ## _v_lowpass_ ## CODETYPE(half, src, SIZE, stride);\
    OPNAME ## pixels ## SIZE ## _l2_ ## CODETYPE(dst, src+stride, half, stride, stride, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc11_ ## CODETYPE(uint8_t *dst, uint8_t *src, int stride){\
    uint64_t temp[SIZE*SIZE/4] __align16;\
    uint8_t * const halfH= (uint8_t*)temp;\
    uint8_t * const halfV= ((uint8_t*)temp) + SIZE*SIZE;\
    put_h264_qpel ## SIZE ## _h_lowpass_ ## CODETYPE(halfH, src, SIZE, stride);\
    put_h264_qpel ## SIZE ## _v_lowpass_ ## CODETYPE(halfV, src, SIZE, stride);\
    OPNAME ## pixels ## SIZE ## _l2_ ## CODETYPE(dst, halfH, halfV, stride, SIZE, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc31_ ## CODETYPE(uint8_t *dst, uint8_t *src, int stride){\
    uint64_t temp[SIZE*SIZE/4] __align16;\
    uint8_t * const halfH= (uint8_t*)temp;\
    uint8_t * const halfV= ((uint8_t*)temp) + SIZE*SIZE;\
    put_h264_qpel ## SIZE ## _h_lowpass_ ## CODETYPE(halfH, src, SIZE, stride);\
    put_h264_qpel ## SIZE ## _v_lowpass_ ## CODETYPE(halfV, src+1, SIZE, stride);\
    OPNAME ## pixels ## SIZE ## _l2_ ## CODETYPE(dst, halfH, halfV, stride, SIZE, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc13_ ## CODETYPE(uint8_t *dst, uint8_t *src, int stride){\
    uint64_t temp[SIZE*SIZE/4] __align16;\
    uint8_t * const halfH= (uint8_t*)temp;\
    uint8_t * const halfV= ((uint8_t*)temp) + SIZE*SIZE;\
    put_h264_qpel ## SIZE ## _h_lowpass_ ## CODETYPE(halfH, src + stride, SIZE, stride);\
    put_h264_qpel ## SIZE ## _v_lowpass_ ## CODETYPE(halfV, src, SIZE, stride);\
    OPNAME ## pixels ## SIZE ## _l2_ ## CODETYPE(dst, halfH, halfV, stride, SIZE, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc33_ ## CODETYPE(uint8_t *dst, uint8_t *src, int stride){\
    uint64_t temp[SIZE*SIZE/4] __align16;\
    uint8_t * const halfH= (uint8_t*)temp;\
    uint8_t * const halfV= ((uint8_t*)temp) + SIZE*SIZE;\
    put_h264_qpel ## SIZE ## _h_lowpass_ ## CODETYPE(halfH, src + stride, SIZE, stride);\
    put_h264_qpel ## SIZE ## _v_lowpass_ ## CODETYPE(halfV, src+1, SIZE, stride);\
    OPNAME ## pixels ## SIZE ## _l2_ ## CODETYPE(dst, halfH, halfV, stride, SIZE, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc22_ ## CODETYPE(uint8_t *dst, uint8_t *src, int stride){\
    uint64_t temp[SIZE*(SIZE+8)/4] __align16;\
    int16_t * const tmp= (int16_t*)temp;\
    OPNAME ## h264_qpel ## SIZE ## _hv_lowpass_ ## CODETYPE(dst, tmp, src, stride, SIZE, stride);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc21_ ## CODETYPE(uint8_t *dst, uint8_t *src, int stride){\
    uint64_t temp[SIZE*(SIZE+8)/4 + SIZE*SIZE/4] __align16;\
    uint8_t * const halfH= (uint8_t*)temp;\
    uint8_t * const halfHV= ((uint8_t*)temp) + SIZE*SIZE;\
    int16_t * const tmp= ((int16_t*)temp) + SIZE*SIZE;\
    put_h264_qpel ## SIZE ## _h_lowpass_ ## CODETYPE(halfH, src, SIZE, stride);\
    put_h264_qpel ## SIZE ## _hv_lowpass_ ## CODETYPE(halfHV, tmp, src, SIZE, SIZE, stride);\
    OPNAME ## pixels ## SIZE ## _l2_ ## CODETYPE(dst, halfH, halfHV, stride, SIZE, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc23_ ## CODETYPE(uint8_t *dst, uint8_t *src, int stride){\
    uint64_t temp[SIZE*(SIZE+8)/4 + SIZE*SIZE/4] __align16;\
    uint8_t * const halfH= (uint8_t*)temp;\
    uint8_t * const halfHV= ((uint8_t*)temp) + SIZE*SIZE;\
    int16_t * const tmp= ((int16_t*)temp) + SIZE*SIZE;\
    put_h264_qpel ## SIZE ## _h_lowpass_ ## CODETYPE(halfH, src + stride, SIZE, stride);\
    put_h264_qpel ## SIZE ## _hv_lowpass_ ## CODETYPE(halfHV, tmp, src, SIZE, SIZE, stride);\
    OPNAME ## pixels ## SIZE ## _l2_ ## CODETYPE(dst, halfH, halfHV, stride, SIZE, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc12_ ## CODETYPE(uint8_t *dst, uint8_t *src, int stride){\
    uint64_t temp[SIZE*(SIZE+8)/4 + SIZE*SIZE/4] __align16;\
    uint8_t * const halfV= (uint8_t*)temp;\
    uint8_t * const halfHV= ((uint8_t*)temp) + SIZE*SIZE;\
    int16_t * const tmp= ((int16_t*)temp) + SIZE*SIZE;\
    put_h264_qpel ## SIZE ## _v_lowpass_ ## CODETYPE(halfV, src, SIZE, stride);\
    put_h264_qpel ## SIZE ## _hv_lowpass_ ## CODETYPE(halfHV, tmp, src, SIZE, SIZE, stride);\
    OPNAME ## pixels ## SIZE ## _l2_ ## CODETYPE(dst, halfV, halfHV, stride, SIZE, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc32_ ## CODETYPE(uint8_t *dst, uint8_t *src, int stride){\
    uint64_t temp[SIZE*(SIZE+8)/4 + SIZE*SIZE/4] __align16;\
    uint8_t * const halfV= (uint8_t*)temp;\
    uint8_t * const halfHV= ((uint8_t*)temp) + SIZE*SIZE;\
    int16_t * const tmp= ((int16_t*)temp) + SIZE*SIZE;\
    put_h264_qpel ## SIZE ## _v_lowpass_ ## CODETYPE(halfV, src+1, SIZE, stride);\
    put_h264_qpel ## SIZE ## _hv_lowpass_ ## CODETYPE(halfHV, tmp, src, SIZE, SIZE, stride);\
    OPNAME ## pixels ## SIZE ## _l2_ ## CODETYPE(dst, halfV, halfHV, stride, SIZE, SIZE);\
}\


/* from dsputil.c */
static inline void put_pixels8_l2(uint8_t * dst, const uint8_t * src1, const uint8_t * src2, int dst_stride, int src_stride1, int src_stride2, int h) {
	int             i;
	for (i = 0; i < h; i++) {
		uint32_t        a, b;
		a = (((const struct unaligned_32 *) (&src1[i * src_stride1]))->l);
		b = (((const struct unaligned_32 *) (&src2[i * src_stride2]))->l);
		*((uint32_t *) & dst[i * dst_stride]) = rnd_avg32(a, b);
		a = (((const struct unaligned_32 *) (&src1[i * src_stride1 + 4]))->l);
		b = (((const struct unaligned_32 *) (&src2[i * src_stride2 + 4]))->l);
		*((uint32_t *) & dst[i * dst_stride + 4]) = rnd_avg32(a, b);
	}
} static inline void avg_pixels8_l2(uint8_t * dst, const uint8_t * src1, const uint8_t * src2, int dst_stride, int src_stride1, int src_stride2, int h) {
	int             i;
	for (i = 0; i < h; i++) {
		uint32_t        a, b;
		a = (((const struct unaligned_32 *) (&src1[i * src_stride1]))->l);
		b = (((const struct unaligned_32 *) (&src2[i * src_stride2]))->l);
		*((uint32_t *) & dst[i * dst_stride]) = rnd_avg32(*((uint32_t *) & dst[i * dst_stride]), rnd_avg32(a, b));
		a = (((const struct unaligned_32 *) (&src1[i * src_stride1 + 4]))->l);
		b = (((const struct unaligned_32 *) (&src2[i * src_stride2 + 4]))->l);
		*((uint32_t *) & dst[i * dst_stride + 4]) = rnd_avg32(*((uint32_t *) & dst[i * dst_stride + 4]), rnd_avg32(a, b));
	}
} static inline void put_pixels16_l2(uint8_t * dst, const uint8_t * src1, const uint8_t * src2, int dst_stride, int src_stride1, int src_stride2, int h) {
	put_pixels8_l2(dst, src1, src2, dst_stride, src_stride1, src_stride2, h);
	put_pixels8_l2(dst + 8, src1 + 8, src2 + 8, dst_stride, src_stride1, src_stride2, h);
} static inline void avg_pixels16_l2(uint8_t * dst, const uint8_t * src1, const uint8_t * src2, int dst_stride, int src_stride1, int src_stride2, int h) {
	avg_pixels8_l2(dst, src1, src2, dst_stride, src_stride1, src_stride2, h);
	avg_pixels8_l2(dst + 8, src1 + 8, src2 + 8, dst_stride, src_stride1, src_stride2, h);
}

/* UNIMPLEMENTED YET !! */
#define put_pixels16_l2_altivec(d,s1,s2,ds,s1s,h) put_pixels16_l2(d,s1,s2,ds,s1s,16,h)
#define avg_pixels16_l2_altivec(d,s1,s2,ds,s1s,h) avg_pixels16_l2(d,s1,s2,ds,s1s,16,h)

H264_MC(put_, 16, altivec)
     H264_MC(avg_, 16, altivec)

void dsputil_h264_init_ppc(DSPContext* c, AVCodecContext *avctx) {
    
#ifdef HAVE_ALTIVEC
  if (has_altivec()) {
    c->put_h264_chroma_pixels_tab[0] = put_h264_chroma_mc8_altivec;
    c->avg_h264_chroma_pixels_tab[0] = avg_h264_chroma_mc8_altivec;

#define dspfunc(PFX, IDX, NUM) \
    c->PFX ## _pixels_tab[IDX][ 0] = PFX ## NUM ## _mc00_altivec; \
    c->PFX ## _pixels_tab[IDX][ 1] = PFX ## NUM ## _mc10_altivec; \
    c->PFX ## _pixels_tab[IDX][ 2] = PFX ## NUM ## _mc20_altivec; \
    c->PFX ## _pixels_tab[IDX][ 3] = PFX ## NUM ## _mc30_altivec; \
    c->PFX ## _pixels_tab[IDX][ 4] = PFX ## NUM ## _mc01_altivec; \
    c->PFX ## _pixels_tab[IDX][ 5] = PFX ## NUM ## _mc11_altivec; \
    c->PFX ## _pixels_tab[IDX][ 6] = PFX ## NUM ## _mc21_altivec; \
    c->PFX ## _pixels_tab[IDX][ 7] = PFX ## NUM ## _mc31_altivec; \
    c->PFX ## _pixels_tab[IDX][ 8] = PFX ## NUM ## _mc02_altivec; \
    c->PFX ## _pixels_tab[IDX][ 9] = PFX ## NUM ## _mc12_altivec; \
    c->PFX ## _pixels_tab[IDX][10] = PFX ## NUM ## _mc22_altivec; \
    c->PFX ## _pixels_tab[IDX][11] = PFX ## NUM ## _mc32_altivec; \
    c->PFX ## _pixels_tab[IDX][12] = PFX ## NUM ## _mc03_altivec; \
    c->PFX ## _pixels_tab[IDX][13] = PFX ## NUM ## _mc13_altivec; \
    c->PFX ## _pixels_tab[IDX][14] = PFX ## NUM ## _mc23_altivec; \
    c->PFX ## _pixels_tab[IDX][15] = PFX ## NUM ## _mc33_altivec
    
    dspfunc(put_h264_qpel, 0, 16);
    dspfunc(avg_h264_qpel, 0, 16);
#undef dspfunc
    
  } else
#endif /* HAVE_ALTIVEC */
  {
    // Non-AltiVec PPC optimisations
    
    // ... pending ...
  }
}
