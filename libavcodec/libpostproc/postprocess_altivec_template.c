/*
    AltiVec optimizations (C) 2004 Romain Dolbeau <romain@dolbeau.org>

    based on code by Copyright (C) 2001-2003 Michael Niedermayer (michaelni@gmx.at)

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/


#ifdef CONFIG_DARWIN
#define AVV(x...) (x)
#else
#define AVV(x...) {x}
#endif

static inline int vertClassify_altivec(uint8_t src[], int stride, PPContext *c) {
  /*
    this code makes no assumption on src or stride.
    One could remove the recomputation of the perm
    vector by assuming (stride % 16) == 0, unfortunately
    this is not always true.
  */
  register int y;
  short __attribute__ ((aligned(16))) data[8];
  int numEq;
  uint8_t *src2 = src;
  vector signed short v_dcOffset;
  vector signed short v2QP;
  vector unsigned short v4QP;
  vector unsigned short v_dcThreshold;
  int two_vectors = ((((unsigned long)src2 % 16) > 8) || (stride % 16)) ? 1 : 0;
  const vector signed int zero = vec_splat_s32(0);
  const vector signed short mask = vec_splat_s16(1);
  vector signed int v_numEq = vec_splat_s32(0);
	
  data[0] = ((c->nonBQP*c->ppMode.baseDcDiff)>>8) + 1;
  data[1] = data[0] * 2 + 1;
  data[2] = c->QP * 2;
  data[3] = c->QP * 4;
  vector signed short v_data = vec_ld(0, data);
  v_dcOffset = vec_splat(v_data, 0);
  v_dcThreshold = (vector unsigned short)vec_splat(v_data, 1);
  v2QP = vec_splat(v_data, 2);
  v4QP = (vector unsigned short)vec_splat(v_data, 3);

  src2 += stride * 4;

#define LOAD_LINE(i)							\
  register int j##i = i * stride;					\
  vector unsigned char perm##i = vec_lvsl(j##i, src2);			\
  const vector unsigned char v_srcA1##i = vec_ld(j##i, src2);		\
  vector unsigned char v_srcA2##i;					\
  if (two_vectors)							\
    v_srcA2##i = vec_ld(j##i + 16, src2);				\
  const vector unsigned char v_srcA##i =				\
    vec_perm(v_srcA1##i, v_srcA2##i, perm##i);				\
  vector signed short v_srcAss##i =					\
    (vector signed short)vec_mergeh((vector signed char)zero,		\
				    (vector signed char)v_srcA##i)

  LOAD_LINE(0);
  LOAD_LINE(1);
  LOAD_LINE(2);
  LOAD_LINE(3);
  LOAD_LINE(4);
  LOAD_LINE(5);
  LOAD_LINE(6);
  LOAD_LINE(7);
#undef LOAD_LINE

#define ITER(i, j)							\
  const vector signed short v_diff##i =					\
    vec_sub(v_srcAss##i, v_srcAss##j);					\
  const vector signed short v_sum##i =					\
    vec_add(v_diff##i, v_dcOffset);					\
  const vector signed short v_comp##i =					\
    (vector signed short)vec_cmplt((vector unsigned short)v_sum##i,	\
				   v_dcThreshold);			\
  const vector signed short v_part##i = vec_and(mask, v_comp##i);	\
  v_numEq = vec_sum4s(v_part##i, v_numEq);

  ITER(0, 1);
  ITER(1, 2);
  ITER(2, 3);
  ITER(3, 4);
  ITER(4, 5);
  ITER(5, 6);
  ITER(6, 7);
#undef ITER

  v_numEq = vec_sums(v_numEq, zero);
	
  v_numEq = vec_splat(v_numEq, 3);
  vec_ste(v_numEq, 0, &numEq);

  if (numEq > c->ppMode.flatnessThreshold)
    {
      const vector unsigned char mmoP1 = (const vector unsigned char)
	AVV(0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f,
	    0x00, 0x01, 0x12, 0x13, 0x08, 0x09, 0x1A, 0x1B);
      const vector unsigned char mmoP2 = (const vector unsigned char)
	AVV(0x04, 0x05, 0x16, 0x17, 0x0C, 0x0D, 0x1E, 0x1F,
	    0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f);
      const vector unsigned char mmoP = (const vector unsigned char)
	vec_lvsl(8, (unsigned char*)0);
      
      vector signed short mmoL1 = vec_perm(v_srcAss0, v_srcAss2, mmoP1);
      vector signed short mmoL2 = vec_perm(v_srcAss4, v_srcAss6, mmoP2);
      vector signed short mmoL = vec_perm(mmoL1, mmoL2, mmoP);
      vector signed short mmoR1 = vec_perm(v_srcAss5, v_srcAss7, mmoP1);
      vector signed short mmoR2 = vec_perm(v_srcAss1, v_srcAss3, mmoP2);
      vector signed short mmoR = vec_perm(mmoR1, mmoR2, mmoP);
      vector signed short mmoDiff = vec_sub(mmoL, mmoR);
      vector unsigned short mmoSum = (vector unsigned short)vec_add(mmoDiff, v2QP);
      
      if (vec_any_gt(mmoSum, v4QP))
	return 0;
      else
	return 1;
    }
  else return 2; 
}


static inline void doVertLowPass_altivec(uint8_t *src, int stride, PPContext *c) {
  /*
    this code makes no assumption on src or stride.
    One could remove the recomputation of the perm
    vector by assuming (stride % 16) == 0, unfortunately
    this is not always true. Quite a lot of load/stores
    can be removed by assuming proper alignement of
    src & stride :-(
  */
  uint8_t *src2 = src;
  const vector signed int zero = vec_splat_s32(0);
  short __attribute__ ((aligned(16))) qp[8];
  qp[0] = c->QP;
  vector signed short vqp = vec_ld(0, qp);
  vqp = vec_splat(vqp, 0);
	
#define LOAD_LINE(i)                                                    \
  const vector unsigned char perml##i =					\
    vec_lvsl(i * stride, src2);						\
  const vector unsigned char vbA##i =					\
    vec_ld(i * stride, src2);						\
  const vector unsigned char vbB##i =					\
    vec_ld(i * stride + 16, src2);					\
  const vector unsigned char vbT##i =					\
    vec_perm(vbA##i, vbB##i, perml##i);					\
  const vector signed short vb##i =					\
    (vector signed short)vec_mergeh((vector unsigned char)zero,		\
				    (vector unsigned char)vbT##i)
	
  src2 += stride*3;

  LOAD_LINE(0);
  LOAD_LINE(1);
  LOAD_LINE(2);
  LOAD_LINE(3);
  LOAD_LINE(4);
  LOAD_LINE(5);
  LOAD_LINE(6);
  LOAD_LINE(7);
  LOAD_LINE(8);
  LOAD_LINE(9);
#undef LOAD_LINE

  const vector unsigned short v_1 = vec_splat_u16(1);
  const vector unsigned short v_2 = vec_splat_u16(2);
  const vector unsigned short v_4 = vec_splat_u16(4);
  const vector signed short v_8 = vec_splat_s16(8);

  const vector signed short v_first = vec_sel(vb1, vb0,
                                              vec_cmplt(vec_abs(vec_sub(vb0, vb1)),
                                                        vqp));
  const vector signed short v_last = vec_sel(vb8, vb9,
                                             vec_cmplt(vec_abs(vec_sub(vb8, vb9)),
                                                       vqp));

  const vector signed short v_sums0 = vec_add(v_first, vb1);
  const vector signed short v_sums1 = vec_add(vb1, vb2);
  const vector signed short v_sums2 = vec_add(vb2, vb3);
  const vector signed short v_sums3 = vec_add(vb3, vb4);
  const vector signed short v_sums4 = vec_add(vb4, vb5);
  const vector signed short v_sums5 = vec_add(vb5, vb6);
  const vector signed short v_sums6 = vec_add(vb6, vb7);
  const vector signed short v_sums7 = vec_add(vb7, vb8);
  const vector signed short v_sums8 = vec_add(vb8, v_last);

  const vector signed short vr1 = vec_sra(vec_add(vec_add(vec_sl(v_sums0, v_2),
                                                          vec_sl(vec_add(v_first, v_sums2), v_1)),
                                                  vec_add(v_sums4, v_8)),
                                          v_4);
  const vector signed short vr2 = vec_sra(vec_add(vec_add(vec_sl(vb2, v_2),
                                                          v_sums5),
                                                  vec_add(v_8,
                                                          vec_sl(vec_add(v_first,
                                                                         vec_add(v_sums0, v_sums3)),
                                                                 v_1))),
                                          v_4);
  const vector signed short vr3 = vec_sra(vec_add(vec_add(vec_sl(vb3, v_2),
                                                          v_sums6),
                                                  vec_add(v_8,
                                                          vec_sl(vec_add(v_first,
                                                                         vec_add(v_sums1, v_sums4)),
                                                                 v_1))),
                                          v_4);
  const vector signed short vr4 = vec_sra(vec_add(vec_add(vec_sl(vb4, v_2),
                                                          v_sums7),
                                                  vec_add(v_8,
                                                          vec_add(v_sums0,
                                                                  vec_sl(vec_add(v_sums2, v_sums5),
                                                                         v_1)))),
                                          v_4);
  const vector signed short vr5 = vec_sra(vec_add(vec_add(vec_sl(vb5, v_2),
                                                          v_sums8),
                                                  vec_add(v_8,
                                                          vec_add(v_sums1,
                                                                  vec_sl(vec_add(v_sums3, v_sums6),
                                                                         v_1)))),
                                          v_4);
  const vector signed short vr6 = vec_sra(vec_add(vec_add(vec_sl(vb6, v_2),
                                                          v_sums2),
                                                  vec_add(v_8,
                                                          vec_sl(vec_add(v_last,
                                                                         vec_add(v_sums7, v_sums4)),
                                                                 v_1))),
                                          v_4);
  const vector signed short vr7 = vec_sra(vec_add(vec_add(vec_sl(vec_add(v_last, vb7), v_2),
                                                          vec_sl(vec_add(vb8, v_sums5), v_1)),
                                                  vec_add(v_8, v_sums3)),
                                          v_4);
  const vector signed short vr8 = vec_sra(vec_add(vec_add(vec_sl(v_sums8, v_2),
                                                          vec_sl(vec_add(v_last, v_sums6), v_1)),
                                                  vec_add(v_sums4, v_8)),
                                          v_4);

  const vector unsigned char neg1 = (vector unsigned char)AVV(-1, -1, -1, -1, -1, -1, -1, -1,
							      -1, -1, -1, -1, -1, -1, -1, -1);
  const vector unsigned char permHH = (vector unsigned char)AVV(0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
								0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F);

#define PACK_AND_STORE(i)					\
  const vector unsigned char perms##i =				\
    vec_lvsr(i * stride, src2);					\
  const vector unsigned char vf##i =				\
    vec_packsu(vr##i, (vector signed short)zero);		\
  const vector unsigned char vg##i =				\
    vec_perm(vf##i, vbT##i, permHH);				\
  const vector unsigned char mask##i =				\
    vec_perm((vector unsigned char)zero, neg1, perms##i);	\
  const vector unsigned char vg2##i =				\
    vec_perm(vg##i, vg##i, perms##i);				\
  const vector unsigned char svA##i =				\
    vec_sel(vbA##i, vg2##i, mask##i);				\
  const vector unsigned char svB##i =				\
    vec_sel(vg2##i, vbB##i, mask##i);				\
  vec_st(svA##i, i * stride, src2);				\
  vec_st(svB##i, i * stride + 16, src2)

  PACK_AND_STORE(1);
  PACK_AND_STORE(2);
  PACK_AND_STORE(3);
  PACK_AND_STORE(4);
  PACK_AND_STORE(5);
  PACK_AND_STORE(6);
  PACK_AND_STORE(7);
  PACK_AND_STORE(8);

#undef PACK_AND_STORE
}



static inline void doVertDefFilter_altivec(uint8_t src[], int stride, PPContext *c) {
  /*
    this code makes no assumption on src or stride.
    One could remove the recomputation of the perm
    vector by assuming (stride % 16) == 0, unfortunately
    this is not always true. Quite a lot of load/stores
    can be removed by assuming proper alignement of
    src & stride :-(
  */
  uint8_t *src2 = src;
  const vector signed int zero = vec_splat_s32(0);
  short __attribute__ ((aligned(16))) qp[8];
  qp[0] = 8*c->QP;
  vector signed short vqp = vec_ld(0, qp);
  vqp = vec_splat(vqp, 0);

#define LOAD_LINE(i)                                                    \
  const vector unsigned char perm##i =					\
    vec_lvsl(i * stride, src2);						\
  const vector unsigned char vbA##i =					\
    vec_ld(i * stride, src2);						\
  const vector unsigned char vbB##i =					\
    vec_ld(i * stride + 16, src2);					\
  const vector unsigned char vbT##i =					\
    vec_perm(vbA##i, vbB##i, perm##i);					\
  const vector signed short vb##i =					\
    (vector signed short)vec_mergeh((vector unsigned char)zero,		\
				    (vector unsigned char)vbT##i)
  
  src2 += stride*3;
  
  LOAD_LINE(1);
  LOAD_LINE(2);
  LOAD_LINE(3);
  LOAD_LINE(4);
  LOAD_LINE(5);
  LOAD_LINE(6);
  LOAD_LINE(7);
  LOAD_LINE(8);
#undef LOAD_LINE
  
  const vector signed short v_1 = vec_splat_s16(1);
  const vector signed short v_2 = vec_splat_s16(2);
  const vector signed short v_5 = vec_splat_s16(5);
  const vector signed short v_32 = vec_sl(v_1,
					  (vector unsigned short)v_5);
  /* middle energy */
  const vector signed short l3minusl6 = vec_sub(vb3, vb6);
  const vector signed short l5minusl4 = vec_sub(vb5, vb4);
  const vector signed short twotimes_l3minusl6 = vec_mladd(v_2, l3minusl6, (vector signed short)zero);
  const vector signed short mE = vec_mladd(v_5, l5minusl4, twotimes_l3minusl6);
  const vector signed short absmE = vec_abs(mE);
  /* left & right energy */
  const vector signed short l1minusl4 = vec_sub(vb1, vb4);
  const vector signed short l3minusl2 = vec_sub(vb3, vb2);
  const vector signed short l5minusl8 = vec_sub(vb5, vb8);
  const vector signed short l7minusl6 = vec_sub(vb7, vb6);
  const vector signed short twotimes_l1minusl4 = vec_mladd(v_2, l1minusl4, (vector signed short)zero);
  const vector signed short twotimes_l5minusl8 = vec_mladd(v_2, l5minusl8, (vector signed short)zero);
  const vector signed short lE = vec_mladd(v_5, l3minusl2, twotimes_l1minusl4);
  const vector signed short rE = vec_mladd(v_5, l7minusl6, twotimes_l5minusl8);
  /* d */
  const vector signed short ddiff = vec_sub(absmE,
                                            vec_min(vec_abs(lE),
                                                    vec_abs(rE)));
  const vector signed short ddiffclamp = vec_max(ddiff, (vector signed short)zero);
  const vector signed short dtimes64 = vec_mladd(v_5, ddiffclamp, v_32);
  const vector signed short d = vec_sra(dtimes64, vec_splat_u16(6));
  const vector signed short minusd = vec_sub((vector signed short)zero, d);
  const vector signed short finald = vec_sel(minusd,
                                             d,
                                             vec_cmpgt(vec_sub((vector signed short)zero, mE),
                                                       (vector signed short)zero));
  /* q */
  const vector signed short qtimes2 = vec_sub(vb4, vb5);
  /* for a shift right to behave like /2, we need to add one
     to all negative integer */
  const vector signed short rounddown = vec_sel((vector signed short)zero,
                                                v_1,
                                                vec_cmplt(qtimes2, (vector signed short)zero));
  const vector signed short q = vec_sra(vec_add(qtimes2, rounddown), vec_splat_u16(1));
  /* clamp */
  const vector signed short dclamp_P1 = vec_max((vector signed short)zero, finald);
  const vector signed short dclamp_P = vec_min(dclamp_P1, q);
  const vector signed short dclamp_N1 = vec_min((vector signed short)zero, finald);
  const vector signed short dclamp_N = vec_max(dclamp_N1, q);

  const vector signed short dclampedfinal = vec_sel(dclamp_N,
                                                    dclamp_P,
                                                    vec_cmpgt(q, (vector signed short)zero));
  const vector signed short dornotd = vec_sel((vector signed short)zero,
                                              dclampedfinal,
                                              vec_cmplt(absmE, vqp));
  /* add/substract to l4 and l5 */
  const vector signed short vb4minusd = vec_sub(vb4, dornotd);
  const vector signed short vb5plusd = vec_add(vb5, dornotd);
  /* finally, stores */
  const vector unsigned char st4 = vec_packsu(vb4minusd, (vector signed short)zero);
  const vector unsigned char st5 = vec_packsu(vb5plusd, (vector signed short)zero);
	
  const vector unsigned char neg1 = (vector unsigned char)AVV(-1, -1, -1, -1, -1, -1, -1, -1,
							      -1, -1, -1, -1, -1, -1, -1, -1);
	
  const vector unsigned char permHH = (vector unsigned char)AVV(0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
								0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F);
	
#define STORE(i)						\
  const vector unsigned char perms##i =				\
    vec_lvsr(i * stride, src2);					\
  const vector unsigned char vg##i =				\
    vec_perm(st##i, vbT##i, permHH);				\
  const vector unsigned char mask##i =				\
    vec_perm((vector unsigned char)zero, neg1, perms##i);	\
  const vector unsigned char vg2##i =				\
    vec_perm(vg##i, vg##i, perms##i);				\
  const vector unsigned char svA##i =				\
    vec_sel(vbA##i, vg2##i, mask##i);				\
  const vector unsigned char svB##i =				\
    vec_sel(vg2##i, vbB##i, mask##i);				\
  vec_st(svA##i, i * stride, src2);				\
  vec_st(svB##i, i * stride + 16, src2)
	
  STORE(4);
  STORE(5);
}

static inline void dering_altivec(uint8_t src[], int stride, PPContext *c) {
  /*
    this code makes no assumption on src or stride.
    One could remove the recomputation of the perm
    vector by assuming (stride % 16) == 0, unfortunately
    this is not always true. Quite a lot of load/stores
    can be removed by assuming proper alignement of
    src & stride :-(
  */
  uint8_t *srcCopy = src;
  uint8_t __attribute__((aligned(16))) dt[16];
  const vector unsigned char vuint8_1 = vec_splat_u8(1);
  const vector signed int zero = vec_splat_s32(0);
  vector unsigned char v_dt;
  dt[0] = deringThreshold;
  v_dt = vec_splat(vec_ld(0, dt), 0);

#define LOAD_LINE(i)							\
  const vector unsigned char perm##i =					\
    vec_lvsl(i * stride, srcCopy);					\
  vector unsigned char sA##i = vec_ld(i * stride, srcCopy);		\
  vector unsigned char sB##i = vec_ld(i * stride + 16, srcCopy);	\
  vector unsigned char src##i = vec_perm(sA##i, sB##i, perm##i)
	
  LOAD_LINE(0);
  LOAD_LINE(1);
  LOAD_LINE(2);
  LOAD_LINE(3);
  LOAD_LINE(4);
  LOAD_LINE(5);
  LOAD_LINE(6);
  LOAD_LINE(7);
  LOAD_LINE(8);
  LOAD_LINE(9);
#undef LOAD_LINE

  vector unsigned char v_avg;
  {
    const vector unsigned char trunc_perm = (vector unsigned char)
      AVV(0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
	  0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18);
    const vector unsigned char trunc_src12 = vec_perm(src1, src2, trunc_perm);
    const vector unsigned char trunc_src34 = vec_perm(src3, src4, trunc_perm);
    const vector unsigned char trunc_src56 = vec_perm(src5, src6, trunc_perm);
    const vector unsigned char trunc_src78 = vec_perm(src7, src8, trunc_perm);
	  
#define EXTRACT(op) do {						\
      const vector unsigned char s##op##_1 = vec_##op(trunc_src12, trunc_src34); \
      const vector unsigned char s##op##_2 = vec_##op(trunc_src56, trunc_src78); \
      const vector unsigned char s##op##_6 = vec_##op(s##op##_1, s##op##_2); \
      const vector unsigned char s##op##_8h = vec_mergeh(s##op##_6, s##op##_6); \
      const vector unsigned char s##op##_8l = vec_mergel(s##op##_6, s##op##_6); \
      const vector unsigned char s##op##_9 = vec_##op(s##op##_8h, s##op##_8l); \
      const vector unsigned char s##op##_9h = vec_mergeh(s##op##_9, s##op##_9); \
      const vector unsigned char s##op##_9l = vec_mergel(s##op##_9, s##op##_9); \
      const vector unsigned char s##op##_10 = vec_##op(s##op##_9h, s##op##_9l); \
      const vector unsigned char s##op##_10h = vec_mergeh(s##op##_10, s##op##_10); \
      const vector unsigned char s##op##_10l = vec_mergel(s##op##_10, s##op##_10); \
      const vector unsigned char s##op##_11 = vec_##op(s##op##_10h, s##op##_10l); \
      const vector unsigned char s##op##_11h = vec_mergeh(s##op##_11, s##op##_11); \
      const vector unsigned char s##op##_11l = vec_mergel(s##op##_11, s##op##_11); \
      v_##op = vec_##op(s##op##_11h, s##op##_11l); } while (0)
	  
    vector unsigned char v_min;
    vector unsigned char v_max;
    EXTRACT(min);
    EXTRACT(max);
#undef EXTRACT
	  
    if (vec_all_lt(vec_sub(v_max, v_min), v_dt))
      return;
	  
    v_avg = vec_avg(v_min, v_max);
  }
	
  signed int __attribute__((aligned(16))) S[8];
  {
    const vector unsigned short mask1 = (vector unsigned short)
      AVV(0x0001, 0x0002, 0x0004, 0x0008,
	  0x0010, 0x0020, 0x0040, 0x0080);
    const vector unsigned short mask2 = (vector unsigned short)
      AVV(0x0100, 0x0200, 0x0000, 0x0000,
	  0x0000, 0x0000, 0x0000, 0x0000);
	  
    const vector unsigned int vuint32_16 = vec_sl(vec_splat_u32(1), vec_splat_u32(4));
    const vector unsigned int vuint32_1 = vec_splat_u32(1);
	  
#define COMPARE(i)							\
    vector signed int sum##i;						\
    do {								\
      const vector unsigned char cmp##i =				\
	(vector unsigned char)vec_cmpgt(src##i, v_avg);			\
      const vector unsigned short cmpHi##i =				\
	(vector unsigned short)vec_mergeh(cmp##i, cmp##i);		\
      const vector unsigned short cmpLi##i =				\
	(vector unsigned short)vec_mergel(cmp##i, cmp##i);		\
      const vector signed short cmpHf##i =				\
	(vector signed short)vec_and(cmpHi##i, mask1);			\
      const vector signed short cmpLf##i =				\
	(vector signed short)vec_and(cmpLi##i, mask2);			\
      const vector signed int sump##i = vec_sum4s(cmpHf##i, zero);	\
      const vector signed int sumq##i = vec_sum4s(cmpLf##i, sump##i);	\
      sum##i  = vec_sums(sumq##i, zero); } while (0)
	  
    COMPARE(0);
    COMPARE(1);
    COMPARE(2);
    COMPARE(3);
    COMPARE(4);
    COMPARE(5);
    COMPARE(6);
    COMPARE(7);
    COMPARE(8);
    COMPARE(9);
#undef COMPARE
	  
    vector signed int sumA2;
    vector signed int sumB2;
    {
      const vector signed int sump02 = vec_mergel(sum0, sum2);
      const vector signed int sump13 = vec_mergel(sum1, sum3);
      const vector signed int sumA = vec_mergel(sump02, sump13);
	      
      const vector signed int sump46 = vec_mergel(sum4, sum6);
      const vector signed int sump57 = vec_mergel(sum5, sum7);
      const vector signed int sumB = vec_mergel(sump46, sump57);
	      
      const vector signed int sump8A = vec_mergel(sum8, zero);
      const vector signed int sump9B = vec_mergel(sum9, zero);
      const vector signed int sumC = vec_mergel(sump8A, sump9B);
	      
      const vector signed int tA = vec_sl(vec_nor(zero, sumA), vuint32_16);
      const vector signed int tB = vec_sl(vec_nor(zero, sumB), vuint32_16);
      const vector signed int tC = vec_sl(vec_nor(zero, sumC), vuint32_16);
      const vector signed int t2A = vec_or(sumA, tA);
      const vector signed int t2B = vec_or(sumB, tB);
      const vector signed int t2C = vec_or(sumC, tC);
      const vector signed int t3A = vec_and(vec_sra(t2A, vuint32_1),
					    vec_sl(t2A, vuint32_1));
      const vector signed int t3B = vec_and(vec_sra(t2B, vuint32_1),
					    vec_sl(t2B, vuint32_1));
      const vector signed int t3C = vec_and(vec_sra(t2C, vuint32_1),
					    vec_sl(t2C, vuint32_1));
      const vector signed int yA = vec_and(t2A, t3A);
      const vector signed int yB = vec_and(t2B, t3B);
      const vector signed int yC = vec_and(t2C, t3C);
	      
      const vector unsigned char strangeperm1 = vec_lvsl(4, (unsigned char*)0);
      const vector unsigned char strangeperm2 = vec_lvsl(8, (unsigned char*)0);
      const vector signed int sumAd4 = vec_perm(yA, yB, strangeperm1);
      const vector signed int sumAd8 = vec_perm(yA, yB, strangeperm2);
      const vector signed int sumBd4 = vec_perm(yB, yC, strangeperm1);
      const vector signed int sumBd8 = vec_perm(yB, yC, strangeperm2);
      const vector signed int sumAp = vec_and(yA,
					      vec_and(sumAd4,sumAd8));
      const vector signed int sumBp = vec_and(yB,
					      vec_and(sumBd4,sumBd8));
      sumA2 = vec_or(sumAp,
		     vec_sra(sumAp,
			     vuint32_16));
      sumB2  = vec_or(sumBp,
		      vec_sra(sumBp,
			      vuint32_16));
    }	
    vec_st(sumA2, 0, S);
    vec_st(sumB2, 16, S);
  }

  /* I'm not sure the following is actually faster
     than straight, unvectorized C code :-( */
	
  int __attribute__((aligned(16))) tQP2[4];
  tQP2[0]= c->QP/2 + 1;
  vector signed int vQP2 = vec_ld(0, tQP2);
  vQP2 = vec_splat(vQP2, 0);
  const vector unsigned char vuint8_2 = vec_splat_u8(2);
  const vector signed int vsint32_8 = vec_splat_s32(8);
  const vector unsigned int vuint32_4 = vec_splat_u32(4);

  const vector unsigned char permA1 = (vector unsigned char)
    AVV(0x00, 0x01, 0x02, 0x10, 0x11, 0x12, 0x1F, 0x1F,
	0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F);
  const vector unsigned char permA2 = (vector unsigned char)
    AVV(0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x10, 0x11,
	0x12, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F);
  const vector unsigned char permA1inc = (vector unsigned char)
    AVV(0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
  const vector unsigned char permA2inc = (vector unsigned char)
    AVV(0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01,
	0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
  const vector unsigned char magic = (vector unsigned char)
    AVV(0x01, 0x02, 0x01, 0x02, 0x04, 0x02, 0x01, 0x02,
	0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
  const vector unsigned char extractPerm = (vector unsigned char)
    AVV(0x10, 0x10, 0x10, 0x01, 0x10, 0x10, 0x10, 0x01,
	0x10, 0x10, 0x10, 0x01, 0x10, 0x10, 0x10, 0x01);
  const vector unsigned char extractPermInc = (vector unsigned char)
    AVV(0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
	0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01);
  const vector unsigned char identity = vec_lvsl(0,(unsigned char *)0);
  const vector unsigned char tenRight = (vector unsigned char)
    AVV(0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
  const vector unsigned char eightLeft = (vector unsigned char)
    AVV(0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08);


#define F_INIT(i)					\
  vector unsigned char tenRightM##i = tenRight;		\
  vector unsigned char permA1M##i = permA1;		\
  vector unsigned char permA2M##i = permA2;		\
  vector unsigned char extractPermM##i = extractPerm

#define F2(i, j, k, l)							\
  if (S[i] & (1 << (l+1))) {						\
    const vector unsigned char a_##j##_A##l =				\
      vec_perm(src##i, src##j, permA1M##i);				\
    const vector unsigned char a_##j##_B##l =				\
      vec_perm(a_##j##_A##l, src##k, permA2M##i);			\
    const vector signed int a_##j##_sump##l =				\
      (vector signed int)vec_msum(a_##j##_B##l, magic,			\
				  (vector unsigned int)zero);		\
    vector signed int F_##j##_##l =					\
      vec_sr(vec_sums(a_##j##_sump##l, vsint32_8), vuint32_4);		\
    F_##j##_##l = vec_splat(F_##j##_##l, 3);				\
    const vector signed int p_##j##_##l =				\
      (vector signed int)vec_perm(src##j,				\
				  (vector unsigned char)zero,		\
				  extractPermM##i);			\
    const vector signed int sum_##j##_##l = vec_add( p_##j##_##l, vQP2); \
    const vector signed int diff_##j##_##l = vec_sub( p_##j##_##l, vQP2); \
    vector signed int newpm_##j##_##l;					\
    if (vec_all_lt(sum_##j##_##l, F_##j##_##l))				\
      newpm_##j##_##l = sum_##j##_##l;					\
    else if (vec_all_gt(diff_##j##_##l, F_##j##_##l))			\
      newpm_##j##_##l = diff_##j##_##l;					\
    else newpm_##j##_##l = F_##j##_##l;					\
    const vector unsigned char newpm2_##j##_##l =			\
      vec_splat((vector unsigned char)newpm_##j##_##l, 15);		\
    const vector unsigned char mask##j##l = vec_add(identity,		\
						    tenRightM##i);	\
    src##j = vec_perm(src##j, newpm2_##j##_##l, mask##j##l);		\
  }									\
  permA1M##i = vec_add(permA1M##i, permA1inc);				\
  permA2M##i = vec_add(permA2M##i, permA2inc);				\
  tenRightM##i = vec_sro(tenRightM##i, eightLeft);			\
  extractPermM##i = vec_add(extractPermM##i, extractPermInc)

#define ITER(i, j, k)				\
  F_INIT(i);					\
  F2(i, j, k, 0);				\
  F2(i, j, k, 1);				\
  F2(i, j, k, 2);				\
  F2(i, j, k, 3);				\
  F2(i, j, k, 4);				\
  F2(i, j, k, 5);				\
  F2(i, j, k, 6);				\
  F2(i, j, k, 7)

  ITER(0, 1, 2);
  ITER(1, 2, 3);
  ITER(2, 3, 4);
  ITER(3, 4, 5);
  ITER(4, 5, 6);
  ITER(5, 6, 7);
  ITER(6, 7, 8);
  ITER(7, 8, 9);

  const vector signed char neg1 = vec_splat_s8( -1 );
	
#define STORE_LINE(i)					\
  const vector unsigned char permST##i =		\
    vec_lvsr(i * stride, srcCopy);			\
  const vector unsigned char maskST##i =		\
    vec_perm((vector unsigned char)zero,		\
	     (vector unsigned char)neg1, permST##i);	\
  src##i = vec_perm(src##i ,src##i, permST##i);		\
  sA##i= vec_sel(sA##i, src##i, maskST##i);		\
  sB##i= vec_sel(src##i, sB##i, maskST##i);		\
  vec_st(sA##i, i * stride, srcCopy);			\
  vec_st(sB##i, i * stride + 16, srcCopy)
	
  STORE_LINE(1);
  STORE_LINE(2);
  STORE_LINE(3);
  STORE_LINE(4);
  STORE_LINE(5);
  STORE_LINE(6);
  STORE_LINE(7);
  STORE_LINE(8);

#undef STORE_LINE
#undef ITER
#undef F2
}

#define horizClassify_altivec(a...) horizClassify_C(a)
#define doHorizLowPass_altivec(a...) doHorizLowPass_C(a)
#define doHorizDefFilter_altivec(a...) doHorizDefFilter_C(a)
