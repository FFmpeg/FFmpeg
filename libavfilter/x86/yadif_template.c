/*
 * Copyright (C) 2006 Michael Niedermayer <michaelni@gmx.at>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifdef COMPILE_TEMPLATE_SSE2
#define MM "%%xmm"
#define MOV  "movq"
#define MOVQ "movdqa"
#define MOVQU "movdqu"
#define STEP 8
#define LOAD(mem,dst) \
            MOV"       "mem", "dst" \n\t"\
            "punpcklbw "MM"7, "dst" \n\t"
#define PSRL1(reg) "psrldq $1, "reg" \n\t"
#define PSRL2(reg) "psrldq $2, "reg" \n\t"
#define PSHUF(src,dst) "movdqa "dst", "src" \n\t"\
                       "psrldq $2, "src"     \n\t"
#else
#define MM "%%mm"
#define MOV  "movd"
#define MOVQ "movq"
#define MOVQU "movq"
#define STEP 4
#define LOAD(mem,dst) \
            MOV"       "mem", "dst" \n\t"\
            "punpcklbw "MM"7, "dst" \n\t"
#define PSRL1(reg) "psrlq $8, "reg" \n\t"
#define PSRL2(reg) "psrlq $16, "reg" \n\t"
#define PSHUF(src,dst) "pshufw $9, "dst", "src" \n\t"
#endif

#ifdef COMPILE_TEMPLATE_SSSE3
#define PABS(tmp,dst) \
            "pabsw     "dst", "dst" \n\t"
#else
#define PABS(tmp,dst) \
            "pxor     "tmp", "tmp" \n\t"\
            "psubw    "dst", "tmp" \n\t"\
            "pmaxsw   "tmp", "dst" \n\t"
#endif

#define CHECK(pj,mj) \
            MOVQU" "#pj"(%[cur],%[mrefs]), "MM"2 \n\t" /* cur[x-refs-1+j] */\
            MOVQU" "#mj"(%[cur],%[prefs]), "MM"3 \n\t" /* cur[x+refs-1-j] */\
            MOVQ"      "MM"2, "MM"4 \n\t"\
            MOVQ"      "MM"2, "MM"5 \n\t"\
            "pxor      "MM"3, "MM"4 \n\t"\
            "pavgb     "MM"3, "MM"5 \n\t"\
            "pand     "MANGLE(pb_1)", "MM"4 \n\t"\
            "psubusb   "MM"4, "MM"5 \n\t"\
            PSRL1(MM"5")                 \
            "punpcklbw "MM"7, "MM"5 \n\t" /* (cur[x-refs+j] + cur[x+refs-j])>>1 */\
            MOVQ"      "MM"2, "MM"4 \n\t"\
            "psubusb   "MM"3, "MM"2 \n\t"\
            "psubusb   "MM"4, "MM"3 \n\t"\
            "pmaxub    "MM"3, "MM"2 \n\t"\
            MOVQ"      "MM"2, "MM"3 \n\t"\
            MOVQ"      "MM"2, "MM"4 \n\t" /* ABS(cur[x-refs-1+j] - cur[x+refs-1-j]) */\
            PSRL1(MM"3")                  /* ABS(cur[x-refs  +j] - cur[x+refs  -j]) */\
            PSRL2(MM"4")                  /* ABS(cur[x-refs+1+j] - cur[x+refs+1-j]) */\
            "punpcklbw "MM"7, "MM"2 \n\t"\
            "punpcklbw "MM"7, "MM"3 \n\t"\
            "punpcklbw "MM"7, "MM"4 \n\t"\
            "paddw     "MM"3, "MM"2 \n\t"\
            "paddw     "MM"4, "MM"2 \n\t" /* score */

#define CHECK1 \
            MOVQ"      "MM"0, "MM"3 \n\t"\
            "pcmpgtw   "MM"2, "MM"3 \n\t" /* if(score < spatial_score) */\
            "pminsw    "MM"2, "MM"0 \n\t" /* spatial_score= score; */\
            MOVQ"      "MM"3, "MM"6 \n\t"\
            "pand      "MM"3, "MM"5 \n\t"\
            "pandn     "MM"1, "MM"3 \n\t"\
            "por       "MM"5, "MM"3 \n\t"\
            MOVQ"      "MM"3, "MM"1 \n\t" /* spatial_pred= (cur[x-refs+j] + cur[x+refs-j])>>1; */

#define CHECK2 /* pretend not to have checked dir=2 if dir=1 was bad.\
                  hurts both quality and speed, but matches the C version. */\
            "paddw    "MANGLE(pw_1)", "MM"6 \n\t"\
            "psllw     $14,   "MM"6 \n\t"\
            "paddsw    "MM"6, "MM"2 \n\t"\
            MOVQ"      "MM"0, "MM"3 \n\t"\
            "pcmpgtw   "MM"2, "MM"3 \n\t"\
            "pminsw    "MM"2, "MM"0 \n\t"\
            "pand      "MM"3, "MM"5 \n\t"\
            "pandn     "MM"1, "MM"3 \n\t"\
            "por       "MM"5, "MM"3 \n\t"\
            MOVQ"      "MM"3, "MM"1 \n\t"

static void RENAME(yadif_filter_line)(uint8_t *dst, uint8_t *prev, uint8_t *cur,
                                      uint8_t *next, int w, int prefs,
                                      int mrefs, int parity, int mode)
{
    uint8_t tmpU[5*16];
    uint8_t *tmp= (uint8_t*)(((uint64_t)(tmpU+15)) & ~15);
    int x;

#define FILTER\
    for(x=0; x<w; x+=STEP){\
        __asm__ volatile(\
            "pxor      "MM"7, "MM"7 \n\t"\
            LOAD("(%[cur],%[mrefs])", MM"0") /* c = cur[x-refs] */\
            LOAD("(%[cur],%[prefs])", MM"1") /* e = cur[x+refs] */\
            LOAD("(%["prev2"])", MM"2") /* prev2[x] */\
            LOAD("(%["next2"])", MM"3") /* next2[x] */\
            MOVQ"      "MM"3, "MM"4 \n\t"\
            "paddw     "MM"2, "MM"3 \n\t"\
            "psraw     $1,    "MM"3 \n\t" /* d = (prev2[x] + next2[x])>>1 */\
            MOVQ"      "MM"0,   (%[tmp]) \n\t" /* c */\
            MOVQ"      "MM"3, 16(%[tmp]) \n\t" /* d */\
            MOVQ"      "MM"1, 32(%[tmp]) \n\t" /* e */\
            "psubw     "MM"4, "MM"2 \n\t"\
            PABS(      MM"4", MM"2") /* temporal_diff0 */\
            LOAD("(%[prev],%[mrefs])", MM"3") /* prev[x-refs] */\
            LOAD("(%[prev],%[prefs])", MM"4") /* prev[x+refs] */\
            "psubw     "MM"0, "MM"3 \n\t"\
            "psubw     "MM"1, "MM"4 \n\t"\
            PABS(      MM"5", MM"3")\
            PABS(      MM"5", MM"4")\
            "paddw     "MM"4, "MM"3 \n\t" /* temporal_diff1 */\
            "psrlw     $1,    "MM"2 \n\t"\
            "psrlw     $1,    "MM"3 \n\t"\
            "pmaxsw    "MM"3, "MM"2 \n\t"\
            LOAD("(%[next],%[mrefs])", MM"3") /* next[x-refs] */\
            LOAD("(%[next],%[prefs])", MM"4") /* next[x+refs] */\
            "psubw     "MM"0, "MM"3 \n\t"\
            "psubw     "MM"1, "MM"4 \n\t"\
            PABS(      MM"5", MM"3")\
            PABS(      MM"5", MM"4")\
            "paddw     "MM"4, "MM"3 \n\t" /* temporal_diff2 */\
            "psrlw     $1,    "MM"3 \n\t"\
            "pmaxsw    "MM"3, "MM"2 \n\t"\
            MOVQ"      "MM"2, 48(%[tmp]) \n\t" /* diff */\
\
            "paddw     "MM"0, "MM"1 \n\t"\
            "paddw     "MM"0, "MM"0 \n\t"\
            "psubw     "MM"1, "MM"0 \n\t"\
            "psrlw     $1,    "MM"1 \n\t" /* spatial_pred */\
            PABS(      MM"2", MM"0")      /* ABS(c-e) */\
\
            MOVQU" -1(%[cur],%[mrefs]), "MM"2 \n\t" /* cur[x-refs-1] */\
            MOVQU" -1(%[cur],%[prefs]), "MM"3 \n\t" /* cur[x+refs-1] */\
            MOVQ"      "MM"2, "MM"4 \n\t"\
            "psubusb   "MM"3, "MM"2 \n\t"\
            "psubusb   "MM"4, "MM"3 \n\t"\
            "pmaxub    "MM"3, "MM"2 \n\t"\
            PSHUF(MM"3", MM"2") \
            "punpcklbw "MM"7, "MM"2 \n\t" /* ABS(cur[x-refs-1] - cur[x+refs-1]) */\
            "punpcklbw "MM"7, "MM"3 \n\t" /* ABS(cur[x-refs+1] - cur[x+refs+1]) */\
            "paddw     "MM"2, "MM"0 \n\t"\
            "paddw     "MM"3, "MM"0 \n\t"\
            "psubw    "MANGLE(pw_1)", "MM"0 \n\t" /* spatial_score */\
\
            CHECK(-2,0)\
            CHECK1\
            CHECK(-3,1)\
            CHECK2\
            CHECK(0,-2)\
            CHECK1\
            CHECK(1,-3)\
            CHECK2\
\
            /* if(p->mode<2) ... */\
            MOVQ" 48(%[tmp]), "MM"6 \n\t" /* diff */\
            "cmpl      $2, %[mode] \n\t"\
            "jge       1f \n\t"\
            LOAD("(%["prev2"],%[mrefs],2)", MM"2") /* prev2[x-2*refs] */\
            LOAD("(%["next2"],%[mrefs],2)", MM"4") /* next2[x-2*refs] */\
            LOAD("(%["prev2"],%[prefs],2)", MM"3") /* prev2[x+2*refs] */\
            LOAD("(%["next2"],%[prefs],2)", MM"5") /* next2[x+2*refs] */\
            "paddw     "MM"4, "MM"2 \n\t"\
            "paddw     "MM"5, "MM"3 \n\t"\
            "psrlw     $1,    "MM"2 \n\t" /* b */\
            "psrlw     $1,    "MM"3 \n\t" /* f */\
            MOVQ"   (%[tmp]), "MM"4 \n\t" /* c */\
            MOVQ" 16(%[tmp]), "MM"5 \n\t" /* d */\
            MOVQ" 32(%[tmp]), "MM"7 \n\t" /* e */\
            "psubw     "MM"4, "MM"2 \n\t" /* b-c */\
            "psubw     "MM"7, "MM"3 \n\t" /* f-e */\
            MOVQ"      "MM"5, "MM"0 \n\t"\
            "psubw     "MM"4, "MM"5 \n\t" /* d-c */\
            "psubw     "MM"7, "MM"0 \n\t" /* d-e */\
            MOVQ"      "MM"2, "MM"4 \n\t"\
            "pminsw    "MM"3, "MM"2 \n\t"\
            "pmaxsw    "MM"4, "MM"3 \n\t"\
            "pmaxsw    "MM"5, "MM"2 \n\t"\
            "pminsw    "MM"5, "MM"3 \n\t"\
            "pmaxsw    "MM"0, "MM"2 \n\t" /* max */\
            "pminsw    "MM"0, "MM"3 \n\t" /* min */\
            "pxor      "MM"4, "MM"4 \n\t"\
            "pmaxsw    "MM"3, "MM"6 \n\t"\
            "psubw     "MM"2, "MM"4 \n\t" /* -max */\
            "pmaxsw    "MM"4, "MM"6 \n\t" /* diff= MAX3(diff, min, -max); */\
            "1: \n\t"\
\
            MOVQ" 16(%[tmp]), "MM"2 \n\t" /* d */\
            MOVQ"      "MM"2, "MM"3 \n\t"\
            "psubw     "MM"6, "MM"2 \n\t" /* d-diff */\
            "paddw     "MM"6, "MM"3 \n\t" /* d+diff */\
            "pmaxsw    "MM"2, "MM"1 \n\t"\
            "pminsw    "MM"3, "MM"1 \n\t" /* d = clip(spatial_pred, d-diff, d+diff); */\
            "packuswb  "MM"1, "MM"1 \n\t"\
\
            ::[prev] "r"(prev),\
             [cur]  "r"(cur),\
             [next] "r"(next),\
             [prefs]"r"((x86_reg)prefs),\
             [mrefs]"r"((x86_reg)mrefs),\
             [mode] "g"(mode),\
             [tmp]  "r"(tmp)\
        );\
        __asm__ volatile(MOV" "MM"1, %0" :"=m"(*dst));\
        dst += STEP;\
        prev+= STEP;\
        cur += STEP;\
        next+= STEP;\
    }

    if (parity) {
#define prev2 "prev"
#define next2 "cur"
        FILTER
#undef prev2
#undef next2
    } else {
#define prev2 "cur"
#define next2 "next"
        FILTER
#undef prev2
#undef next2
    }
}
#undef STEP
#undef MM
#undef MOV
#undef MOVQ
#undef MOVQU
#undef PSHUF
#undef PSRL1
#undef PSRL2
#undef LOAD
#undef PABS
#undef CHECK
#undef CHECK1
#undef CHECK2
#undef FILTER
