/*****************************************************************************
 *
 *  XVID MPEG-4 VIDEO CODEC
 *  - Inverse DCT  -
 *
 *  Copyright (C) 2006-2011 Xvid Solutions GmbH
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
 *
 *
 ****************************************************************************/

/**
 * @file
 * Walken IDCT
 * Alternative idct implementations for decoding compatibility
 *
 * @author Skal
 * @note this "C" version is not the original one, but is modified to
 *       yield the same error profile as the MMX version.
 */


#include "xvididct.h"


#define ROW_SHIFT 11
#define COL_SHIFT 6

// #define FIX(x)   (int)((x) * (1<<ROW_SHIFT))
#define Rnd0 65536 // 1<<(COL_SHIFT+ROW_SHIFT-1);
#define Rnd1 3597  // FIX (1.75683487303);
#define Rnd2 2260  // FIX (1.10355339059);
#define Rnd3 1203  // FIX (0.587788325588);
#define Rnd4 0
#define Rnd5 120   // FIX (0.058658283817);
#define Rnd6 512   // FIX (0.25);
#define Rnd7 512   // FIX (0.25);

static const int Tab04[] = { 22725, 21407, 19266, 16384, 12873,  8867, 4520 };
static const int Tab17[] = { 31521, 29692, 26722, 22725, 17855, 12299, 6270 };
static const int Tab26[] = { 29692, 27969, 25172, 21407, 16819, 11585, 5906 };
static const int Tab35[] = { 26722, 25172, 22654, 19266, 15137, 10426, 5315 };

static int Idct_Row(short * In, const int * const Tab, int Rnd)
{
  const int C1 = Tab[0];
  const int C2 = Tab[1];
  const int C3 = Tab[2];
  const int C4 = Tab[3];
  const int C5 = Tab[4];
  const int C6 = Tab[5];
  const int C7 = Tab[6];

  const int Right = In[5]|In[6]|In[7];
  const int Left  = In[1]|In[2]|In[3];
  if (!(Right | In[4]))
  {
    const int K = C4*In[0] + Rnd;
    if (Left)
    {
      const int a0 = K + C2*In[2];
      const int a1 = K + C6*In[2];
      const int a2 = K - C6*In[2];
      const int a3 = K - C2*In[2];

      const int b0 = C1*In[1] + C3*In[3];
      const int b1 = C3*In[1] - C7*In[3];
      const int b2 = C5*In[1] - C1*In[3];
      const int b3 = C7*In[1] - C5*In[3];

      In[0] = (a0 + b0) >> ROW_SHIFT;
      In[1] = (a1 + b1) >> ROW_SHIFT;
      In[2] = (a2 + b2) >> ROW_SHIFT;
      In[3] = (a3 + b3) >> ROW_SHIFT;
      In[4] = (a3 - b3) >> ROW_SHIFT;
      In[5] = (a2 - b2) >> ROW_SHIFT;
      In[6] = (a1 - b1) >> ROW_SHIFT;
      In[7] = (a0 - b0) >> ROW_SHIFT;
    }
    else
    {
      const int a0 = K >> ROW_SHIFT;
      if (a0) {
        In[0] = In[1] = In[2] = In[3] =
        In[4] = In[5] = In[6] = In[7] = a0;
      }
      else return 0;
    }
  }
  else if (!(Left|Right))
  {
    const int a0 = (Rnd + C4*(In[0]+In[4])) >> ROW_SHIFT;
    const int a1 = (Rnd + C4*(In[0]-In[4])) >> ROW_SHIFT;

    In[0] = a0;
    In[3] = a0;
    In[4] = a0;
    In[7] = a0;
    In[1] = a1;
    In[2] = a1;
    In[5] = a1;
    In[6] = a1;
  }
  else
  {
    const int K = C4*In[0] + Rnd;
    const int a0 = K + C2*In[2] + C4*In[4] + C6*In[6];
    const int a1 = K + C6*In[2] - C4*In[4] - C2*In[6];
    const int a2 = K - C6*In[2] - C4*In[4] + C2*In[6];
    const int a3 = K - C2*In[2] + C4*In[4] - C6*In[6];

    const int b0 = C1*In[1] + C3*In[3] + C5*In[5] + C7*In[7];
    const int b1 = C3*In[1] - C7*In[3] - C1*In[5] - C5*In[7];
    const int b2 = C5*In[1] - C1*In[3] + C7*In[5] + C3*In[7];
    const int b3 = C7*In[1] - C5*In[3] + C3*In[5] - C1*In[7];

    In[0] = (a0 + b0) >> ROW_SHIFT;
    In[1] = (a1 + b1) >> ROW_SHIFT;
    In[2] = (a2 + b2) >> ROW_SHIFT;
    In[3] = (a3 + b3) >> ROW_SHIFT;
    In[4] = (a3 - b3) >> ROW_SHIFT;
    In[5] = (a2 - b2) >> ROW_SHIFT;
    In[6] = (a1 - b1) >> ROW_SHIFT;
    In[7] = (a0 - b0) >> ROW_SHIFT;
  }
  return 1;
}

#define Tan1  0x32ec
#define Tan2  0x6a0a
#define Tan3  0xab0e
#define Sqrt2 0x5a82

#define MULT(c,x, n)  ( ((c) * (x)) >> (n) )
// 12b version => #define MULT(c,x, n)  ( (((c)>>3) * (x)) >> ((n)-3) )
// 12b zero-testing version:

#define BUTF(a, b, tmp) \
  (tmp) = (a)+(b);      \
  (b)   = (a)-(b);      \
  (a)   = (tmp)

#define LOAD_BUTF(m1, m2, a, b, tmp, S) \
  (m1) = (S)[(a)] + (S)[(b)];           \
  (m2) = (S)[(a)] - (S)[(b)]

static void Idct_Col_8(short * const In)
{
  int mm0, mm1, mm2, mm3, mm4, mm5, mm6, mm7, Spill;

    // odd

  mm4 = (int)In[7*8];
  mm5 = (int)In[5*8];
  mm6 = (int)In[3*8];
  mm7 = (int)In[1*8];

  mm0 = MULT(Tan1, mm4, 16) + mm7;
  mm1 = MULT(Tan1, mm7, 16) - mm4;
  mm2 = MULT(Tan3, mm5, 16) + mm6;
  mm3 = MULT(Tan3, mm6, 16) - mm5;

  mm7 = mm0 + mm2;
  mm4 = mm1 - mm3;
  mm0 = mm0 - mm2;
  mm1 = mm1 + mm3;
  mm6 = mm0 + mm1;
  mm5 = mm0 - mm1;
  mm5 = 2*MULT(Sqrt2, mm5, 16);  // 2*sqrt2
  mm6 = 2*MULT(Sqrt2, mm6, 16);  // Watch out: precision loss but done to match
                                 // the pmulhw used in mmx/sse versions

    // even

  mm1 = (int)In[2*8];
  mm2 = (int)In[6*8];
  mm3 = MULT(Tan2,mm2, 16) + mm1;
  mm2 = MULT(Tan2,mm1, 16) - mm2;

  LOAD_BUTF(mm0, mm1, 0*8, 4*8, Spill, In);

  BUTF(mm0, mm3, Spill);
  BUTF(mm0, mm7, Spill);
  In[8*0] = (int16_t) (mm0 >> COL_SHIFT);
  In[8*7] = (int16_t) (mm7 >> COL_SHIFT);
  BUTF(mm3, mm4, mm0);
  In[8*3] = (int16_t) (mm3 >> COL_SHIFT);
  In[8*4] = (int16_t) (mm4 >> COL_SHIFT);

  BUTF(mm1, mm2, mm0);
  BUTF(mm1, mm6, mm0);
  In[8*1] = (int16_t) (mm1 >> COL_SHIFT);
  In[8*6] = (int16_t) (mm6 >> COL_SHIFT);
  BUTF(mm2, mm5, mm0);
  In[8*2] = (int16_t) (mm2 >> COL_SHIFT);
  In[8*5] = (int16_t) (mm5 >> COL_SHIFT);
}

static void Idct_Col_4(short * const In)
{
  int mm0, mm1, mm2, mm3, mm4, mm5, mm6, mm7, Spill;

    // odd

  mm0 = (int)In[1*8];
  mm2 = (int)In[3*8];

  mm1 = MULT(Tan1, mm0, 16);
  mm3 = MULT(Tan3, mm2, 16);

  mm7 = mm0 + mm2;
  mm4 = mm1 - mm3;
  mm0 = mm0 - mm2;
  mm1 = mm1 + mm3;
  mm6 = mm0 + mm1;
  mm5 = mm0 - mm1;
  mm6 = 2*MULT(Sqrt2, mm6, 16);  // 2*sqrt2
  mm5 = 2*MULT(Sqrt2, mm5, 16);

    // even

  mm0 = mm1 = (int)In[0*8];
  mm3 = (int)In[2*8];
  mm2 = MULT(Tan2,mm3, 16);

  BUTF(mm0, mm3, Spill);
  BUTF(mm0, mm7, Spill);
  In[8*0] = (int16_t) (mm0 >> COL_SHIFT);
  In[8*7] = (int16_t) (mm7 >> COL_SHIFT);
  BUTF(mm3, mm4, mm0);
  In[8*3] = (int16_t) (mm3 >> COL_SHIFT);
  In[8*4] = (int16_t) (mm4 >> COL_SHIFT);

  BUTF(mm1, mm2, mm0);
  BUTF(mm1, mm6, mm0);
  In[8*1] = (int16_t) (mm1 >> COL_SHIFT);
  In[8*6] = (int16_t) (mm6 >> COL_SHIFT);
  BUTF(mm2, mm5, mm0);
  In[8*2] = (int16_t) (mm2 >> COL_SHIFT);
  In[8*5] = (int16_t) (mm5 >> COL_SHIFT);
}

static void Idct_Col_3(short * const In)
{
  int mm0, mm1, mm2, mm3, mm4, mm5, mm6, mm7, Spill;

    // odd

  mm7 = (int)In[1*8];
  mm4 = MULT(Tan1, mm7, 16);

  mm6 = mm7 + mm4;
  mm5 = mm7 - mm4;
  mm6 = 2*MULT(Sqrt2, mm6, 16);  // 2*sqrt2
  mm5 = 2*MULT(Sqrt2, mm5, 16);

    // even

  mm0 = mm1 = (int)In[0*8];
  mm3 = (int)In[2*8];
  mm2 = MULT(Tan2,mm3, 16);

  BUTF(mm0, mm3, Spill);
  BUTF(mm0, mm7, Spill);
  In[8*0] = (int16_t) (mm0 >> COL_SHIFT);
  In[8*7] = (int16_t) (mm7 >> COL_SHIFT);
  BUTF(mm3, mm4, mm0);
  In[8*3] = (int16_t) (mm3 >> COL_SHIFT);
  In[8*4] = (int16_t) (mm4 >> COL_SHIFT);

  BUTF(mm1, mm2, mm0);
  BUTF(mm1, mm6, mm0);
  In[8*1] = (int16_t) (mm1 >> COL_SHIFT);
  In[8*6] = (int16_t) (mm6 >> COL_SHIFT);
  BUTF(mm2, mm5, mm0);
  In[8*2] = (int16_t) (mm2 >> COL_SHIFT);
  In[8*5] = (int16_t) (mm5 >> COL_SHIFT);
}

#undef Tan1
#undef Tan2
#undef Tan3
#undef Sqrt2

#undef ROW_SHIFT
#undef COL_SHIFT

//////////////////////////////////////////////////////////

void ff_idct_xvid(int16_t *const In)
{
  int i, Rows = 0x07;

  Idct_Row(In + 0*8, Tab04, Rnd0);
  Idct_Row(In + 1*8, Tab17, Rnd1);
  Idct_Row(In + 2*8, Tab26, Rnd2);
  if (Idct_Row(In + 3*8, Tab35, Rnd3)) Rows |= 0x08;
  if (Idct_Row(In + 4*8, Tab04, Rnd4)) Rows |= 0x10;
  if (Idct_Row(In + 5*8, Tab35, Rnd5)) Rows |= 0x20;
  if (Idct_Row(In + 6*8, Tab26, Rnd6)) Rows |= 0x40;
  if (Idct_Row(In + 7*8, Tab17, Rnd7)) Rows |= 0x80;

  if (Rows&0xf0) {
    for(i=0; i<8; i++)
      Idct_Col_8(In + i);
  }
  else if (Rows&0x08) {
    for(i=0; i<8; i++)
      Idct_Col_4(In + i);
  }
  else {
    for(i=0; i<8; i++)
      Idct_Col_3(In + i);
  }
}
