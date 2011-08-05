/*
 * Copyright (C) 2003  James Klicman <james@klicman.org>
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

#include "config.h"
#if HAVE_ALTIVEC_H
#include <altivec.h>
#endif
#include "libavutil/common.h"
#include "libavcodec/dsputil.h"
#include "dsputil_altivec.h"

#define vs16(v) ((vector signed short)(v))
#define vs32(v) ((vector signed int)(v))
#define vu8(v)  ((vector unsigned char)(v))
#define vu16(v) ((vector unsigned short)(v))
#define vu32(v) ((vector unsigned int)(v))


#define C1     0.98078525066375732421875000 /* cos(1*PI/16) */
#define C2     0.92387950420379638671875000 /* cos(2*PI/16) */
#define C3     0.83146959543228149414062500 /* cos(3*PI/16) */
#define C4     0.70710676908493041992187500 /* cos(4*PI/16) */
#define C5     0.55557024478912353515625000 /* cos(5*PI/16) */
#define C6     0.38268342614173889160156250 /* cos(6*PI/16) */
#define C7     0.19509032368659973144531250 /* cos(7*PI/16) */
#define SQRT_2 1.41421353816986083984375000 /* sqrt(2)      */


#define W0 -(2 * C2)
#define W1 (2 * C6)
#define W2 (SQRT_2 * C6)
#define W3 (SQRT_2 * C3)
#define W4 (SQRT_2 * (-C1 + C3 + C5 - C7))
#define W5 (SQRT_2 * ( C1 + C3 - C5 + C7))
#define W6 (SQRT_2 * ( C1 + C3 + C5 - C7))
#define W7 (SQRT_2 * ( C1 + C3 - C5 - C7))
#define W8 (SQRT_2 * ( C7 - C3))
#define W9 (SQRT_2 * (-C1 - C3))
#define WA (SQRT_2 * (-C3 - C5))
#define WB (SQRT_2 * ( C5 - C3))


static vector float fdctconsts[3] = {
    { W0, W1, W2, W3 },
    { W4, W5, W6, W7 },
    { W8, W9, WA, WB }
};

#define LD_W0 vec_splat(cnsts0, 0)
#define LD_W1 vec_splat(cnsts0, 1)
#define LD_W2 vec_splat(cnsts0, 2)
#define LD_W3 vec_splat(cnsts0, 3)
#define LD_W4 vec_splat(cnsts1, 0)
#define LD_W5 vec_splat(cnsts1, 1)
#define LD_W6 vec_splat(cnsts1, 2)
#define LD_W7 vec_splat(cnsts1, 3)
#define LD_W8 vec_splat(cnsts2, 0)
#define LD_W9 vec_splat(cnsts2, 1)
#define LD_WA vec_splat(cnsts2, 2)
#define LD_WB vec_splat(cnsts2, 3)


#define FDCTROW(b0,b1,b2,b3,b4,b5,b6,b7) /* {{{ */                  \
    x0 = vec_add(b0, b7);               /* x0 = b0 + b7; */         \
    x7 = vec_sub(b0, b7);               /* x7 = b0 - b7; */         \
    x1 = vec_add(b1, b6);               /* x1 = b1 + b6; */         \
    x6 = vec_sub(b1, b6);               /* x6 = b1 - b6; */         \
    x2 = vec_add(b2, b5);               /* x2 = b2 + b5; */         \
    x5 = vec_sub(b2, b5);               /* x5 = b2 - b5; */         \
    x3 = vec_add(b3, b4);               /* x3 = b3 + b4; */         \
    x4 = vec_sub(b3, b4);               /* x4 = b3 - b4; */         \
                                                                    \
    b7 = vec_add(x0, x3);               /* b7 = x0 + x3; */         \
    b1 = vec_add(x1, x2);               /* b1 = x1 + x2; */         \
    b0 = vec_add(b7, b1);               /* b0 = b7 + b1; */         \
    b4 = vec_sub(b7, b1);               /* b4 = b7 - b1; */         \
                                                                    \
    b2 = vec_sub(x0, x3);               /* b2 = x0 - x3; */         \
    b6 = vec_sub(x1, x2);               /* b6 = x1 - x2; */         \
    b5 = vec_add(b6, b2);               /* b5 = b6 + b2; */         \
    cnst = LD_W2;                                                   \
    b5 = vec_madd(cnst, b5, mzero);     /* b5 = b5 * W2; */         \
    cnst = LD_W1;                                                   \
    b2 = vec_madd(cnst, b2, b5);        /* b2 = b5 + b2 * W1; */    \
    cnst = LD_W0;                                                   \
    b6 = vec_madd(cnst, b6, b5);        /* b6 = b5 + b6 * W0; */    \
                                                                    \
    x0 = vec_add(x4, x7);               /* x0 = x4 + x7; */         \
    x1 = vec_add(x5, x6);               /* x1 = x5 + x6; */         \
    x2 = vec_add(x4, x6);               /* x2 = x4 + x6; */         \
    x3 = vec_add(x5, x7);               /* x3 = x5 + x7; */         \
    x8 = vec_add(x2, x3);               /* x8 = x2 + x3; */         \
    cnst = LD_W3;                                                   \
    x8 = vec_madd(cnst, x8, mzero);     /* x8 = x8 * W3; */         \
                                                                    \
    cnst = LD_W8;                                                   \
    x0 = vec_madd(cnst, x0, mzero);     /* x0 *= W8; */             \
    cnst = LD_W9;                                                   \
    x1 = vec_madd(cnst, x1, mzero);     /* x1 *= W9; */             \
    cnst = LD_WA;                                                   \
    x2 = vec_madd(cnst, x2, x8);        /* x2 = x2 * WA + x8; */    \
    cnst = LD_WB;                                                   \
    x3 = vec_madd(cnst, x3, x8);        /* x3 = x3 * WB + x8; */    \
                                                                    \
    cnst = LD_W4;                                                   \
    b7 = vec_madd(cnst, x4, x0);        /* b7 = x4 * W4 + x0; */    \
    cnst = LD_W5;                                                   \
    b5 = vec_madd(cnst, x5, x1);        /* b5 = x5 * W5 + x1; */    \
    cnst = LD_W6;                                                   \
    b3 = vec_madd(cnst, x6, x1);        /* b3 = x6 * W6 + x1; */    \
    cnst = LD_W7;                                                   \
    b1 = vec_madd(cnst, x7, x0);        /* b1 = x7 * W7 + x0; */    \
                                                                    \
    b7 = vec_add(b7, x2);               /* b7 = b7 + x2; */         \
    b5 = vec_add(b5, x3);               /* b5 = b5 + x3; */         \
    b3 = vec_add(b3, x2);               /* b3 = b3 + x2; */         \
    b1 = vec_add(b1, x3);               /* b1 = b1 + x3; */         \
    /* }}} */

#define FDCTCOL(b0,b1,b2,b3,b4,b5,b6,b7) /* {{{ */                  \
    x0 = vec_add(b0, b7);               /* x0 = b0 + b7; */         \
    x7 = vec_sub(b0, b7);               /* x7 = b0 - b7; */         \
    x1 = vec_add(b1, b6);               /* x1 = b1 + b6; */         \
    x6 = vec_sub(b1, b6);               /* x6 = b1 - b6; */         \
    x2 = vec_add(b2, b5);               /* x2 = b2 + b5; */         \
    x5 = vec_sub(b2, b5);               /* x5 = b2 - b5; */         \
    x3 = vec_add(b3, b4);               /* x3 = b3 + b4; */         \
    x4 = vec_sub(b3, b4);               /* x4 = b3 - b4; */         \
                                                                    \
    b7 = vec_add(x0, x3);               /* b7 = x0 + x3; */         \
    b1 = vec_add(x1, x2);               /* b1 = x1 + x2; */         \
    b0 = vec_add(b7, b1);               /* b0 = b7 + b1; */         \
    b4 = vec_sub(b7, b1);               /* b4 = b7 - b1; */         \
                                                                    \
    b2 = vec_sub(x0, x3);               /* b2 = x0 - x3; */         \
    b6 = vec_sub(x1, x2);               /* b6 = x1 - x2; */         \
    b5 = vec_add(b6, b2);               /* b5 = b6 + b2; */         \
    cnst = LD_W2;                                                   \
    b5 = vec_madd(cnst, b5, mzero);     /* b5 = b5 * W2; */         \
    cnst = LD_W1;                                                   \
    b2 = vec_madd(cnst, b2, b5);        /* b2 = b5 + b2 * W1; */    \
    cnst = LD_W0;                                                   \
    b6 = vec_madd(cnst, b6, b5);        /* b6 = b5 + b6 * W0; */    \
                                                                    \
    x0 = vec_add(x4, x7);               /* x0 = x4 + x7; */         \
    x1 = vec_add(x5, x6);               /* x1 = x5 + x6; */         \
    x2 = vec_add(x4, x6);               /* x2 = x4 + x6; */         \
    x3 = vec_add(x5, x7);               /* x3 = x5 + x7; */         \
    x8 = vec_add(x2, x3);               /* x8 = x2 + x3; */         \
    cnst = LD_W3;                                                   \
    x8 = vec_madd(cnst, x8, mzero);     /* x8 = x8 * W3; */         \
                                                                    \
    cnst = LD_W8;                                                   \
    x0 = vec_madd(cnst, x0, mzero);     /* x0 *= W8; */             \
    cnst = LD_W9;                                                   \
    x1 = vec_madd(cnst, x1, mzero);     /* x1 *= W9; */             \
    cnst = LD_WA;                                                   \
    x2 = vec_madd(cnst, x2, x8);        /* x2 = x2 * WA + x8; */    \
    cnst = LD_WB;                                                   \
    x3 = vec_madd(cnst, x3, x8);        /* x3 = x3 * WB + x8; */    \
                                                                    \
    cnst = LD_W4;                                                   \
    b7 = vec_madd(cnst, x4, x0);        /* b7 = x4 * W4 + x0; */    \
    cnst = LD_W5;                                                   \
    b5 = vec_madd(cnst, x5, x1);        /* b5 = x5 * W5 + x1; */    \
    cnst = LD_W6;                                                   \
    b3 = vec_madd(cnst, x6, x1);        /* b3 = x6 * W6 + x1; */    \
    cnst = LD_W7;                                                   \
    b1 = vec_madd(cnst, x7, x0);        /* b1 = x7 * W7 + x0; */    \
                                                                    \
    b7 = vec_add(b7, x2);               /* b7 += x2; */             \
    b5 = vec_add(b5, x3);               /* b5 += x3; */             \
    b3 = vec_add(b3, x2);               /* b3 += x2; */             \
    b1 = vec_add(b1, x3);               /* b1 += x3; */             \
    /* }}} */



/* two dimensional discrete cosine transform */

void fdct_altivec(int16_t *block)
{
    vector signed short *bp;
    vector float *cp;
    vector float b00, b10, b20, b30, b40, b50, b60, b70;
    vector float b01, b11, b21, b31, b41, b51, b61, b71;
    vector float mzero, cnst, cnsts0, cnsts1, cnsts2;
    vector float x0, x1, x2, x3, x4, x5, x6, x7, x8;

    /* setup constants {{{ */
    /* mzero = -0.0 */
    mzero = ((vector float)vec_splat_u32(-1));
    mzero = ((vector float)vec_sl(vu32(mzero), vu32(mzero)));
    cp = fdctconsts;
    cnsts0 = vec_ld(0, cp); cp++;
    cnsts1 = vec_ld(0, cp); cp++;
    cnsts2 = vec_ld(0, cp);
    /* }}} */


    /* 8x8 matrix transpose (vector short[8]) {{{ */
#define MERGE_S16(hl,a,b) vec_merge##hl(vs16(a), vs16(b))

    bp = (vector signed short*)block;
    b00 = ((vector float)vec_ld(0,    bp));
    b40 = ((vector float)vec_ld(16*4, bp));
    b01 = ((vector float)MERGE_S16(h, b00, b40));
    b11 = ((vector float)MERGE_S16(l, b00, b40));
    bp++;
    b10 = ((vector float)vec_ld(0,    bp));
    b50 = ((vector float)vec_ld(16*4, bp));
    b21 = ((vector float)MERGE_S16(h, b10, b50));
    b31 = ((vector float)MERGE_S16(l, b10, b50));
    bp++;
    b20 = ((vector float)vec_ld(0,    bp));
    b60 = ((vector float)vec_ld(16*4, bp));
    b41 = ((vector float)MERGE_S16(h, b20, b60));
    b51 = ((vector float)MERGE_S16(l, b20, b60));
    bp++;
    b30 = ((vector float)vec_ld(0,    bp));
    b70 = ((vector float)vec_ld(16*4, bp));
    b61 = ((vector float)MERGE_S16(h, b30, b70));
    b71 = ((vector float)MERGE_S16(l, b30, b70));

    x0 = ((vector float)MERGE_S16(h, b01, b41));
    x1 = ((vector float)MERGE_S16(l, b01, b41));
    x2 = ((vector float)MERGE_S16(h, b11, b51));
    x3 = ((vector float)MERGE_S16(l, b11, b51));
    x4 = ((vector float)MERGE_S16(h, b21, b61));
    x5 = ((vector float)MERGE_S16(l, b21, b61));
    x6 = ((vector float)MERGE_S16(h, b31, b71));
    x7 = ((vector float)MERGE_S16(l, b31, b71));

    b00 = ((vector float)MERGE_S16(h, x0, x4));
    b10 = ((vector float)MERGE_S16(l, x0, x4));
    b20 = ((vector float)MERGE_S16(h, x1, x5));
    b30 = ((vector float)MERGE_S16(l, x1, x5));
    b40 = ((vector float)MERGE_S16(h, x2, x6));
    b50 = ((vector float)MERGE_S16(l, x2, x6));
    b60 = ((vector float)MERGE_S16(h, x3, x7));
    b70 = ((vector float)MERGE_S16(l, x3, x7));

#undef MERGE_S16
    /* }}} */


/* Some of the initial calculations can be done as vector short before
 * conversion to vector float.  The following code section takes advantage
 * of this.
 */
    /* fdct rows {{{ */
    x0 = ((vector float)vec_add(vs16(b00), vs16(b70)));
    x7 = ((vector float)vec_sub(vs16(b00), vs16(b70)));
    x1 = ((vector float)vec_add(vs16(b10), vs16(b60)));
    x6 = ((vector float)vec_sub(vs16(b10), vs16(b60)));
    x2 = ((vector float)vec_add(vs16(b20), vs16(b50)));
    x5 = ((vector float)vec_sub(vs16(b20), vs16(b50)));
    x3 = ((vector float)vec_add(vs16(b30), vs16(b40)));
    x4 = ((vector float)vec_sub(vs16(b30), vs16(b40)));

    b70 = ((vector float)vec_add(vs16(x0), vs16(x3)));
    b10 = ((vector float)vec_add(vs16(x1), vs16(x2)));

    b00 = ((vector float)vec_add(vs16(b70), vs16(b10)));
    b40 = ((vector float)vec_sub(vs16(b70), vs16(b10)));

#define CTF0(n) \
    b##n##1 = ((vector float)vec_unpackl(vs16(b##n##0))); \
    b##n##0 = ((vector float)vec_unpackh(vs16(b##n##0))); \
    b##n##1 = vec_ctf(vs32(b##n##1), 0); \
    b##n##0 = vec_ctf(vs32(b##n##0), 0);

    CTF0(0);
    CTF0(4);

    b20 = ((vector float)vec_sub(vs16(x0), vs16(x3)));
    b60 = ((vector float)vec_sub(vs16(x1), vs16(x2)));

    CTF0(2);
    CTF0(6);

#undef CTF0

    x0 = vec_add(b60, b20);
    x1 = vec_add(b61, b21);

    cnst = LD_W2;
    x0 = vec_madd(cnst, x0, mzero);
    x1 = vec_madd(cnst, x1, mzero);
    cnst = LD_W1;
    b20 = vec_madd(cnst, b20, x0);
    b21 = vec_madd(cnst, b21, x1);
    cnst = LD_W0;
    b60 = vec_madd(cnst, b60, x0);
    b61 = vec_madd(cnst, b61, x1);

#define CTFX(x,b) \
    b##0 = ((vector float)vec_unpackh(vs16(x))); \
    b##1 = ((vector float)vec_unpackl(vs16(x))); \
    b##0 = vec_ctf(vs32(b##0), 0); \
    b##1 = vec_ctf(vs32(b##1), 0); \

    CTFX(x4, b7);
    CTFX(x5, b5);
    CTFX(x6, b3);
    CTFX(x7, b1);

#undef CTFX


    x0 = vec_add(b70, b10);
    x1 = vec_add(b50, b30);
    x2 = vec_add(b70, b30);
    x3 = vec_add(b50, b10);
    x8 = vec_add(x2, x3);
    cnst = LD_W3;
    x8 = vec_madd(cnst, x8, mzero);

    cnst = LD_W8;
    x0 = vec_madd(cnst, x0, mzero);
    cnst = LD_W9;
    x1 = vec_madd(cnst, x1, mzero);
    cnst = LD_WA;
    x2 = vec_madd(cnst, x2, x8);
    cnst = LD_WB;
    x3 = vec_madd(cnst, x3, x8);

    cnst = LD_W4;
    b70 = vec_madd(cnst, b70, x0);
    cnst = LD_W5;
    b50 = vec_madd(cnst, b50, x1);
    cnst = LD_W6;
    b30 = vec_madd(cnst, b30, x1);
    cnst = LD_W7;
    b10 = vec_madd(cnst, b10, x0);

    b70 = vec_add(b70, x2);
    b50 = vec_add(b50, x3);
    b30 = vec_add(b30, x2);
    b10 = vec_add(b10, x3);


    x0 = vec_add(b71, b11);
    x1 = vec_add(b51, b31);
    x2 = vec_add(b71, b31);
    x3 = vec_add(b51, b11);
    x8 = vec_add(x2, x3);
    cnst = LD_W3;
    x8 = vec_madd(cnst, x8, mzero);

    cnst = LD_W8;
    x0 = vec_madd(cnst, x0, mzero);
    cnst = LD_W9;
    x1 = vec_madd(cnst, x1, mzero);
    cnst = LD_WA;
    x2 = vec_madd(cnst, x2, x8);
    cnst = LD_WB;
    x3 = vec_madd(cnst, x3, x8);

    cnst = LD_W4;
    b71 = vec_madd(cnst, b71, x0);
    cnst = LD_W5;
    b51 = vec_madd(cnst, b51, x1);
    cnst = LD_W6;
    b31 = vec_madd(cnst, b31, x1);
    cnst = LD_W7;
    b11 = vec_madd(cnst, b11, x0);

    b71 = vec_add(b71, x2);
    b51 = vec_add(b51, x3);
    b31 = vec_add(b31, x2);
    b11 = vec_add(b11, x3);
    /* }}} */


    /* 8x8 matrix transpose (vector float[8][2]) {{{ */
    x0 = vec_mergel(b00, b20);
    x1 = vec_mergeh(b00, b20);
    x2 = vec_mergel(b10, b30);
    x3 = vec_mergeh(b10, b30);

    b00 = vec_mergeh(x1, x3);
    b10 = vec_mergel(x1, x3);
    b20 = vec_mergeh(x0, x2);
    b30 = vec_mergel(x0, x2);

    x4 = vec_mergel(b41, b61);
    x5 = vec_mergeh(b41, b61);
    x6 = vec_mergel(b51, b71);
    x7 = vec_mergeh(b51, b71);

    b41 = vec_mergeh(x5, x7);
    b51 = vec_mergel(x5, x7);
    b61 = vec_mergeh(x4, x6);
    b71 = vec_mergel(x4, x6);

    x0 = vec_mergel(b01, b21);
    x1 = vec_mergeh(b01, b21);
    x2 = vec_mergel(b11, b31);
    x3 = vec_mergeh(b11, b31);

    x4 = vec_mergel(b40, b60);
    x5 = vec_mergeh(b40, b60);
    x6 = vec_mergel(b50, b70);
    x7 = vec_mergeh(b50, b70);

    b40 = vec_mergeh(x1, x3);
    b50 = vec_mergel(x1, x3);
    b60 = vec_mergeh(x0, x2);
    b70 = vec_mergel(x0, x2);

    b01 = vec_mergeh(x5, x7);
    b11 = vec_mergel(x5, x7);
    b21 = vec_mergeh(x4, x6);
    b31 = vec_mergel(x4, x6);
    /* }}} */


    FDCTCOL(b00, b10, b20, b30, b40, b50, b60, b70);
    FDCTCOL(b01, b11, b21, b31, b41, b51, b61, b71);


    /* round, convert back to short {{{ */
#define CTS(n) \
    b##n##0 = vec_round(b##n##0); \
    b##n##1 = vec_round(b##n##1); \
    b##n##0 = ((vector float)vec_cts(b##n##0, 0)); \
    b##n##1 = ((vector float)vec_cts(b##n##1, 0)); \
    b##n##0 = ((vector float)vec_pack(vs32(b##n##0), vs32(b##n##1))); \
    vec_st(vs16(b##n##0), 0, bp);

    bp = (vector signed short*)block;
    CTS(0); bp++;
    CTS(1); bp++;
    CTS(2); bp++;
    CTS(3); bp++;
    CTS(4); bp++;
    CTS(5); bp++;
    CTS(6); bp++;
    CTS(7);

#undef CTS
    /* }}} */
}

/* vim:set foldmethod=marker foldlevel=0: */
