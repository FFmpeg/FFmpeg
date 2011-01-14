/*
 * This file is part of MPlayer.
 *
 * MPlayer is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * MPlayer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with MPlayer; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "config.h"
#include "mp_msg.h"
#include "cpudetect.h"

#include "img_format.h"
#include "mp_image.h"
#include "vd.h"
#include "vf.h"
#include "cmmx.h"

#include "libvo/fastmemcpy.h"

#define NUM_STORED 4

enum pu_field_type_t {
    PU_1ST_OF_3,
    PU_2ND_OF_3,
    PU_3RD_OF_3,
    PU_1ST_OF_2,
    PU_2ND_OF_2,
    PU_INTERLACED
};

struct metrics {
    /* This struct maps to a packed word 64-bit MMX register */
    unsigned short int even;
    unsigned short int odd;
    unsigned short int noise;
    unsigned short int temp;
} __attribute__ ((aligned (8)));

struct frame_stats {
    struct metrics tiny, low, high, bigger, twox, max;
    struct { unsigned int even, odd, noise, temp; } sad;
    unsigned short interlaced_high;
    unsigned short interlaced_low;
    unsigned short num_blocks;
};

struct vf_priv_s {
    unsigned long inframes;
    unsigned long outframes;
    enum pu_field_type_t prev_type;
    unsigned swapped, chroma_swapped;
    unsigned luma_only;
    unsigned verbose;
    unsigned fast;
    unsigned long w, h, cw, ch, stride, chroma_stride, nplanes;
    unsigned long sad_thres;
    unsigned long dint_thres;
    unsigned char *memory_allocated;
    unsigned char *planes[2*NUM_STORED][4];
    unsigned char **old_planes;
    unsigned long static_idx;
    unsigned long temp_idx;
    unsigned long crop_x, crop_y, crop_cx, crop_cy;
    unsigned long export_count, merge_count;
    unsigned long num_breaks;
    unsigned long num_copies;
    long in_inc, out_dec, iosync;
    long num_fields;
    long prev_fields;
    long notout;
    long mmx2;
    unsigned small_bytes[2];
    unsigned mmx_temp[2];
    struct frame_stats stats[2];
    struct metrics thres;
    char chflag;
    double diff_time, merge_time, decode_time, vo_time, filter_time;
};

#define PPZ { 2000, 2000, 0, 2000 }
#define PPR { 2000, 2000, 0, 2000 }
static const struct frame_stats ppzs = {PPZ,PPZ,PPZ,PPZ,PPZ,PPZ,PPZ,0,0,9999};
static const struct frame_stats pprs = {PPR,PPR,PPR,PPR,PPR,PPR,PPR,0,0,9999};

#ifndef MIN
#define        MIN(a,b) (((a)<(b))?(a):(b))
#endif
#ifndef MAX
#define        MAX(a,b) (((a)>(b))?(a):(b))
#endif

#define PDIFFUB(X,Y,T) "movq "    #X "," #T "\n\t" \
                       "psubusb " #Y "," #T "\n\t" \
                       "psubusb " #X "," #Y "\n\t" \
                       "paddusb " #Y "," #T "\n\t"

#define PDIFFUBT(X,Y,T) "movq "    #X "," #T "\n\t" \
                        "psubusb " #Y "," #T "\n\t" \
                        "psubusb " #X "," #Y "\n\t" \
                        "paddusb " #T "," #Y "\n\t"

#define PSUMBW(X,T,Z)        "movq " #X "," #T "\n\t" \
                        "punpcklbw " #Z "," #X "\n\t" \
                        "punpckhbw " #Z "," #T "\n\t" \
                        "paddw " #T "," #X "\n\t" \
                        "movq " #X "," #T "\n\t" \
                        "psllq $32, " #T "\n\t" \
                        "paddw " #T "," #X "\n\t" \
                        "movq " #X "," #T "\n\t" \
                        "psllq $16, " #T "\n\t" \
                        "paddw " #T "," #X "\n\t" \
                        "psrlq $48, " #X "\n\t"

#define PSADBW(X,Y,T,Z)        PDIFFUBT(X,Y,T) PSUMBW(Y,T,Z)

#define PMAXUB(X,Y) "psubusb " #X "," #Y "\n\tpaddusb " #X "," #Y "\n\t"
#define PMAXUW(X,Y) "psubusw " #X "," #Y "\n\tpaddusw " #X "," #Y "\n\t"
#define PMINUBT(X,Y,T)        "movq " #Y "," #T "\n\t" \
                        "psubusb " #X "," #T "\n\t" \
                        "psubusb " #T "," #Y "\n\t"
#define PAVGB(X,Y)        "pavgusb " #X "," #Y "\n\t"

static inline void
get_metrics_c(unsigned char *a, unsigned char *b, int as, int bs, int lines,
              struct metrics *m)
{
    a -= as;
    b -= bs;
    do {
        cmmx_t old_po = *(cmmx_t*)(a      );
        cmmx_t     po = *(cmmx_t*)(b      );
        cmmx_t      e = *(cmmx_t*)(b +   bs);
        cmmx_t  old_o = *(cmmx_t*)(a + 2*as);
        cmmx_t      o = *(cmmx_t*)(b + 2*bs);
        cmmx_t     ne = *(cmmx_t*)(b + 3*bs);
        cmmx_t old_no = *(cmmx_t*)(a + 4*as);
        cmmx_t     no = *(cmmx_t*)(b + 4*bs);

        cmmx_t   qup_old_odd = p31avgb(old_o, old_po);
        cmmx_t       qup_odd = p31avgb(    o,     po);
        cmmx_t qdown_old_odd = p31avgb(old_o, old_no);
        cmmx_t     qdown_odd = p31avgb(    o,     no);

        cmmx_t   qup_even = p31avgb(ne, e);
        cmmx_t qdown_even = p31avgb(e, ne);

        cmmx_t    temp_up_diff = pdiffub(qdown_even, qup_old_odd);
        cmmx_t   noise_up_diff = pdiffub(qdown_even, qup_odd);
        cmmx_t  temp_down_diff = pdiffub(qup_even, qdown_old_odd);
        cmmx_t noise_down_diff = pdiffub(qup_even, qdown_odd);

        cmmx_t odd_diff = pdiffub(o, old_o);
        m->odd  += psumbw(odd_diff);
        m->even += psadbw(e, *(cmmx_t*)(a+as));

        temp_up_diff  = pminub(temp_up_diff, temp_down_diff);
        temp_up_diff  = pminub(temp_up_diff, odd_diff);
        m->temp  += psumbw(temp_up_diff);
        noise_up_diff = pminub(noise_up_diff, odd_diff);
        noise_up_diff = pminub(noise_up_diff, noise_down_diff);

        m->noise += psumbw(noise_up_diff);
        a += 2*as;
        b += 2*bs;
    } while (--lines);
}

static inline void
get_metrics_fast_c(unsigned char *a, unsigned char *b, int as, int bs,
                   int lines, struct metrics *m)
{
    a -= as;
    b -= bs;
    do {
        cmmx_t old_po = (*(cmmx_t*)(a       ) >> 1) & ~SIGN_BITS;
        cmmx_t     po = (*(cmmx_t*)(b       ) >> 1) & ~SIGN_BITS;
        cmmx_t  old_e = (*(cmmx_t*)(a +   as) >> 1) & ~SIGN_BITS;
        cmmx_t      e = (*(cmmx_t*)(b +   bs) >> 1) & ~SIGN_BITS;
        cmmx_t  old_o = (*(cmmx_t*)(a + 2*as) >> 1) & ~SIGN_BITS;
        cmmx_t      o = (*(cmmx_t*)(b + 2*bs) >> 1) & ~SIGN_BITS;
        cmmx_t     ne = (*(cmmx_t*)(b + 3*bs) >> 1) & ~SIGN_BITS;
        cmmx_t old_no = (*(cmmx_t*)(a + 4*as) >> 1) & ~SIGN_BITS;
        cmmx_t     no = (*(cmmx_t*)(b + 4*bs) >> 1) & ~SIGN_BITS;

        cmmx_t   qup_old_odd = p31avgb_s(old_o, old_po);
        cmmx_t       qup_odd = p31avgb_s(    o,     po);
        cmmx_t qdown_old_odd = p31avgb_s(old_o, old_no);
        cmmx_t     qdown_odd = p31avgb_s(    o,     no);

        cmmx_t   qup_even = p31avgb_s(ne, e);
        cmmx_t qdown_even = p31avgb_s(e, ne);

        cmmx_t    temp_up_diff = pdiffub_s(qdown_even, qup_old_odd);
        cmmx_t   noise_up_diff = pdiffub_s(qdown_even, qup_odd);
        cmmx_t  temp_down_diff = pdiffub_s(qup_even, qdown_old_odd);
        cmmx_t noise_down_diff = pdiffub_s(qup_even, qdown_odd);

        cmmx_t odd_diff = pdiffub_s(o, old_o);
        m->odd  += psumbw_s(odd_diff) << 1;
        m->even += psadbw_s(e, old_e) << 1;

        temp_up_diff  = pminub_s(temp_up_diff, temp_down_diff);
        temp_up_diff  = pminub_s(temp_up_diff, odd_diff);
        m->temp      += psumbw_s(temp_up_diff) << 1;
        noise_up_diff = pminub_s(noise_up_diff, odd_diff);
        noise_up_diff = pminub_s(noise_up_diff, noise_down_diff);

        m->noise += psumbw_s(noise_up_diff) << 1;
        a += 2*as;
        b += 2*bs;
    } while (--lines);
}

static inline void
get_metrics_faster_c(unsigned char *a, unsigned char *b, int as, int bs,
                   int lines, struct metrics *m)
{
    a -= as;
    b -= bs;
    do {
        cmmx_t old_po = (*(cmmx_t*)(a       )>>1) & ~SIGN_BITS;
        cmmx_t     po = (*(cmmx_t*)(b       )>>1) & ~SIGN_BITS;
        cmmx_t  old_e = (*(cmmx_t*)(a +   as)>>1) & ~SIGN_BITS;
        cmmx_t      e = (*(cmmx_t*)(b +   bs)>>1) & ~SIGN_BITS;
        cmmx_t  old_o = (*(cmmx_t*)(a + 2*as)>>1) & ~SIGN_BITS;
        cmmx_t      o = (*(cmmx_t*)(b + 2*bs)>>1) & ~SIGN_BITS;
        cmmx_t     ne = (*(cmmx_t*)(b + 3*bs)>>1) & ~SIGN_BITS;

        cmmx_t  down_even = p31avgb_s(e, ne);
        cmmx_t     up_odd = p31avgb_s(o, po);
        cmmx_t up_old_odd = p31avgb_s(old_o, old_po);

        cmmx_t   odd_diff = pdiffub_s(o, old_o);
        cmmx_t  temp_diff = pdiffub_s(down_even, up_old_odd);
        cmmx_t noise_diff = pdiffub_s(down_even, up_odd);

        m->even += psadbw_s(e, old_e) << 1;
        m->odd  += psumbw_s(odd_diff) << 1;

        temp_diff  = pminub_s(temp_diff, odd_diff);
        noise_diff = pminub_s(noise_diff, odd_diff);

        m->noise += psumbw_s(noise_diff) << 1;
        m->temp  += psumbw_s(temp_diff) << 1;
        a += 2*as;
        b += 2*bs;
    } while (--lines);

}

static inline void
get_block_stats(struct metrics *m, struct vf_priv_s *p, struct frame_stats *s)
{
    unsigned two_e = m->even  + MAX(m->even , p->thres.even );
    unsigned two_o = m->odd   + MAX(m->odd  , p->thres.odd  );
    unsigned two_n = m->noise + MAX(m->noise, p->thres.noise);
    unsigned two_t = m->temp  + MAX(m->temp , p->thres.temp );

    unsigned e_big   = m->even  >= (m->odd   + two_o + 1)/2;
    unsigned o_big   = m->odd   >= (m->even  + two_e + 1)/2;
    unsigned n_big   = m->noise >= (m->temp  + two_t + 1)/2;
    unsigned t_big   = m->temp  >= (m->noise + two_n + 1)/2;

    unsigned e2x     = m->even  >= two_o;
    unsigned o2x     = m->odd   >= two_e;
    unsigned n2x     = m->noise >= two_t;
    unsigned t2x     = m->temp  >= two_n;

    unsigned ntiny_e = m->even  > p->thres.even ;
    unsigned ntiny_o = m->odd   > p->thres.odd  ;
    unsigned ntiny_n = m->noise > p->thres.noise;
    unsigned ntiny_t = m->temp  > p->thres.temp ;

    unsigned nlow_e  = m->even  > 2*p->thres.even ;
    unsigned nlow_o  = m->odd   > 2*p->thres.odd  ;
    unsigned nlow_n  = m->noise > 2*p->thres.noise;
    unsigned nlow_t  = m->temp  > 2*p->thres.temp ;

    unsigned high_e  = m->even  > 4*p->thres.even ;
    unsigned high_o  = m->odd   > 4*p->thres.odd  ;
    unsigned high_n  = m->noise > 4*p->thres.noise;
    unsigned high_t  = m->temp  > 4*p->thres.temp ;

    unsigned low_il  = !n_big && !t_big && ntiny_n && ntiny_t;
    unsigned high_il = !n_big && !t_big && nlow_n  && nlow_t;

    if (low_il | high_il) {
        s->interlaced_low  += low_il;
        s->interlaced_high += high_il;
    } else {
        s->tiny.even  += ntiny_e;
        s->tiny.odd   += ntiny_o;
        s->tiny.noise += ntiny_n;
        s->tiny.temp  += ntiny_t;

        s->low .even  += nlow_e ;
        s->low .odd   += nlow_o ;
        s->low .noise += nlow_n ;
        s->low .temp  += nlow_t ;

        s->high.even  += high_e ;
        s->high.odd   += high_o ;
        s->high.noise += high_n ;
        s->high.temp  += high_t ;

        if (m->even  >=        p->sad_thres) s->sad.even  += m->even ;
        if (m->odd   >=        p->sad_thres) s->sad.odd   += m->odd  ;
        if (m->noise >=        p->sad_thres) s->sad.noise += m->noise;
        if (m->temp  >=        p->sad_thres) s->sad.temp  += m->temp ;
    }
    s->num_blocks++;
    s->max.even  = MAX(s->max.even , m->even );
    s->max.odd   = MAX(s->max.odd  , m->odd  );
    s->max.noise = MAX(s->max.noise, m->noise);
    s->max.temp  = MAX(s->max.temp , m->temp );

    s->bigger.even  += e_big  ;
    s->bigger.odd   += o_big  ;
    s->bigger.noise += n_big  ;
    s->bigger.temp  += t_big  ;

    s->twox.even  += e2x    ;
    s->twox.odd   += o2x    ;
    s->twox.noise += n2x    ;
    s->twox.temp  += t2x    ;

}

static inline struct metrics
block_metrics_c(unsigned char *a, unsigned char *b, int as, int bs,
                int lines, struct vf_priv_s *p, struct frame_stats *s)
{
    struct metrics tm;
    tm.even = tm.odd = tm.noise = tm.temp = 0;
    get_metrics_c(a, b, as, bs, lines, &tm);
    if (sizeof(cmmx_t) < 8)
        get_metrics_c(a+4, b+4, as, bs, lines, &tm);
    get_block_stats(&tm, p, s);
    return tm;
}

static inline struct metrics
block_metrics_fast_c(unsigned char *a, unsigned char *b, int as, int bs,
                int lines, struct vf_priv_s *p, struct frame_stats *s)
{
    struct metrics tm;
    tm.even = tm.odd = tm.noise = tm.temp = 0;
    get_metrics_fast_c(a, b, as, bs, lines, &tm);
    if (sizeof(cmmx_t) < 8)
        get_metrics_fast_c(a+4, b+4, as, bs, lines, &tm);
    get_block_stats(&tm, p, s);
    return tm;
}

static inline struct metrics
block_metrics_faster_c(unsigned char *a, unsigned char *b, int as, int bs,
                int lines, struct vf_priv_s *p, struct frame_stats *s)
{
    struct metrics tm;
    tm.even = tm.odd = tm.noise = tm.temp = 0;
    get_metrics_faster_c(a, b, as, bs, lines, &tm);
    if (sizeof(cmmx_t) < 8)
        get_metrics_faster_c(a+4, b+4, as, bs, lines, &tm);
    get_block_stats(&tm, p, s);
    return tm;
}

#define MEQ(X,Y) ((X).even == (Y).even && (X).odd == (Y).odd && (X).temp == (Y).temp && (X).noise == (Y).noise)

#define BLOCK_METRICS_TEMPLATE() \
    __asm__ volatile("pxor %mm7, %mm7\n\t"   /* The result is colleted in mm7 */ \
                 "pxor %mm6, %mm6\n\t"   /* Temp to stay at 0 */             \
        );                                                                     \
    a -= as;                                                                     \
    b -= bs;                                                                     \
    do {                                                                     \
        __asm__ volatile(                                                     \
            "movq (%0,%2), %%mm0\n\t"                                             \
            "movq (%1,%3), %%mm1\n\t"   /* mm1 = even */                     \
            PSADBW(%%mm1, %%mm0, %%mm4, %%mm6)                                     \
            "paddusw %%mm0, %%mm7\n\t"  /* even diff */                             \
            "movq (%0,%2,2), %%mm0\n\t" /* mm0 = old odd */                     \
            "movq (%1,%3,2), %%mm2\n\t" /* mm2 = odd */                             \
            "movq (%0), %%mm3\n\t"                                             \
            "psubusb %4, %%mm3\n\t"                                             \
            PAVGB(%%mm0, %%mm3)                                                     \
            PAVGB(%%mm0, %%mm3)    /* mm3 = qup old odd */                     \
            "movq %%mm0, %%mm5\n\t"                                             \
            PSADBW(%%mm2, %%mm0, %%mm4, %%mm6)                                     \
            "psllq $16, %%mm0\n\t"                                             \
            "paddusw %%mm0, %%mm7\n\t"                                             \
            "movq (%1), %%mm4\n\t"                                             \
            "lea (%0,%2,2), %0\n\t"                                             \
            "lea (%1,%3,2), %1\n\t"                                             \
            "psubusb %4, %%mm4\n\t"                                             \
            PAVGB(%%mm2, %%mm4)                                                     \
            PAVGB(%%mm2, %%mm4)    /* mm4 = qup odd */                             \
            PDIFFUBT(%%mm5, %%mm2, %%mm0) /* mm2 =abs(oldodd-odd) */             \
            "movq (%1,%3), %%mm5\n\t"                                             \
            "psubusb %4, %%mm5\n\t"                                             \
            PAVGB(%%mm1, %%mm5)                                                     \
            PAVGB(%%mm5, %%mm1)    /* mm1 = qdown even */                     \
            PAVGB((%1,%3), %%mm5)  /* mm5 = qup next even */                     \
            PDIFFUBT(%%mm1, %%mm3, %%mm0) /* mm3 = abs(qupoldo-qde) */             \
            PDIFFUBT(%%mm1, %%mm4, %%mm0) /* mm4 = abs(qupodd-qde) */             \
            PMINUBT(%%mm2, %%mm3, %%mm0)  /* limit temp to odd diff */             \
            PMINUBT(%%mm2, %%mm4, %%mm0)  /* limit noise to odd diff */             \
            "movq (%1,%3,2), %%mm2\n\t"                                             \
            "psubusb %4, %%mm2\n\t"                                             \
            PAVGB((%1), %%mm2)                                                     \
            PAVGB((%1), %%mm2)    /* mm2 = qdown odd */                             \
            "movq (%0,%2,2), %%mm1\n\t"                                             \
            "psubusb %4, %%mm1\n\t"                                             \
            PAVGB((%0), %%mm1)                                                     \
            PAVGB((%0), %%mm1)  /* mm1 = qdown old odd */                     \
            PDIFFUBT(%%mm5, %%mm2, %%mm0) /* mm2 = abs(qdo-qune) */             \
            PDIFFUBT(%%mm5, %%mm1, %%mm0) /* mm1 = abs(qdoo-qune) */             \
            PMINUBT(%%mm4, %%mm2, %%mm0)  /* current */                             \
            PMINUBT(%%mm3, %%mm1, %%mm0)  /* old */                             \
            PSUMBW(%%mm2, %%mm0, %%mm6)                                             \
            PSUMBW(%%mm1, %%mm0, %%mm6)                                             \
            "psllq $32, %%mm2\n\t"                                             \
            "psllq $48, %%mm1\n\t"                                             \
            "paddusw %%mm2, %%mm7\n\t"                                             \
            "paddusw %%mm1, %%mm7\n\t"                                             \
            : "=r" (a), "=r" (b)                                             \
            : "r"((x86_reg)as), "r"((x86_reg)bs), "m" (ones), "0"(a), "1"(b), "X"(*a), "X"(*b) \
            );                                                                     \
    } while (--lines);

static inline struct metrics
block_metrics_3dnow(unsigned char *a, unsigned char *b, int as, int bs,
                    int lines, struct vf_priv_s *p, struct frame_stats *s)
{
    struct metrics tm;
#if !HAVE_AMD3DNOW
    mp_msg(MSGT_VFILTER, MSGL_FATAL, "block_metrics_3dnow: internal error\n");
#else
    static const unsigned long long ones = 0x0101010101010101ull;

    BLOCK_METRICS_TEMPLATE();
    __asm__ volatile("movq %%mm7, %0\n\temms" : "=m" (tm));
    get_block_stats(&tm, p, s);
#endif
    return tm;
}

#undef PSUMBW
#undef PSADBW
#undef PMAXUB
#undef PMINUBT
#undef PAVGB

#define PSUMBW(X,T,Z)        "psadbw " #Z "," #X "\n\t"
#define PSADBW(X,Y,T,Z) "psadbw " #X "," #Y "\n\t"
#define PMAXUB(X,Y)        "pmaxub " #X "," #Y "\n\t"
#define PMINUBT(X,Y,T)        "pminub " #X "," #Y "\n\t"
#define PAVGB(X,Y)        "pavgb "  #X "," #Y "\n\t"

static inline struct metrics
block_metrics_mmx2(unsigned char *a, unsigned char *b, int as, int bs,
                   int lines, struct vf_priv_s *p, struct frame_stats *s)
{
    struct metrics tm;
#if !HAVE_MMX
    mp_msg(MSGT_VFILTER, MSGL_FATAL, "block_metrics_mmx2: internal error\n");
#else
    static const unsigned long long ones = 0x0101010101010101ull;
    x86_reg interlaced;
    x86_reg prefetch_line = (((long)a>>3) & 7) + 10;
#ifdef DEBUG
    struct frame_stats ts = *s;
#endif
    __asm__ volatile("prefetcht0 (%0,%2)\n\t"
                 "prefetcht0 (%1,%3)\n\t" :
                 : "r" (a), "r" (b),
                 "r" (prefetch_line * as), "r" (prefetch_line * bs));

    BLOCK_METRICS_TEMPLATE();

    s->num_blocks++;
    __asm__ volatile(
        "movq %3, %%mm0\n\t"
        "movq %%mm7, %%mm1\n\t"
        "psubusw %%mm0, %%mm1\n\t"
        "movq %%mm1, %%mm2\n\t"
        "paddusw %%mm0, %%mm2\n\t"
        "paddusw %%mm7, %%mm2\n\t"
        "pshufw $0xb1, %%mm2, %%mm3\n\t"
        "pavgw %%mm7, %%mm2\n\t"
        "pshufw $0xb1, %%mm2, %%mm2\n\t"
        "psubusw %%mm7, %%mm2\n\t"
        "pcmpeqw %%mm6, %%mm2\n\t" /* 1 if >= 1.5x */
        "psubusw %%mm7, %%mm3\n\t"
        "pcmpeqw %%mm6, %%mm3\n\t" /* 1 if >= 2x */
        "movq %1, %%mm4\n\t"
        "movq %2, %%mm5\n\t"
        "psubw %%mm2, %%mm4\n\t"
        "psubw %%mm3, %%mm5\n\t"
        "movq %%mm4, %1\n\t"
        "movq %%mm5, %2\n\t"
        "pxor %%mm4, %%mm4\n\t"
        "pcmpeqw %%mm1, %%mm4\n\t" /* 1 if <= t */
        "psubusw %%mm0, %%mm1\n\t"
        "pxor %%mm5, %%mm5\n\t"
        "pcmpeqw %%mm1, %%mm5\n\t" /* 1 if <= 2t */
        "psubusw %%mm0, %%mm1\n\t"
        "psubusw %%mm0, %%mm1\n\t"
        "pcmpeqw %%mm6, %%mm1\n\t" /* 1 if <= 4t */
        "pshufw $0xb1, %%mm2, %%mm0\n\t"
        "por %%mm2, %%mm0\n\t"     /* 1 if not close */
        "punpckhdq %%mm0, %%mm0\n\t"
        "movq %%mm4, %%mm2\n\t"      /* tttt */
        "punpckhdq %%mm5, %%mm2\n\t" /* ttll */
        "por %%mm2, %%mm0\n\t"
        "pcmpeqd %%mm6, %%mm0\n\t" /* close && big */
        "psrlq $16, %%mm0\n\t"
        "psrlw $15, %%mm0\n\t"
        "movd %%mm0, %0\n\t"
        : "=r" (interlaced), "=m" (s->bigger), "=m" (s->twox)
        : "m" (p->thres)
        );

    if (interlaced) {
        s->interlaced_high += interlaced >> 16;
        s->interlaced_low += interlaced;
    } else {
        __asm__ volatile(
            "pcmpeqw %%mm0, %%mm0\n\t" /* -1 */
            "psubw         %%mm0, %%mm4\n\t"
            "psubw         %%mm0, %%mm5\n\t"
            "psubw         %%mm0, %%mm1\n\t"
            "paddw %0, %%mm4\n\t"
            "paddw %1, %%mm5\n\t"
            "paddw %2, %%mm1\n\t"
            "movq %%mm4, %0\n\t"
            "movq %%mm5, %1\n\t"
            "movq %%mm1, %2\n\t"
            : "=m" (s->tiny), "=m" (s->low), "=m" (s->high)
            );

        __asm__ volatile(
            "pshufw $0, %2, %%mm0\n\t"
            "psubusw %%mm7, %%mm0\n\t"
            "pcmpeqw %%mm6, %%mm0\n\t"   /* 0 if below sad_thres */
            "pand %%mm7, %%mm0\n\t"
            "movq %%mm0, %%mm1\n\t"
            "punpcklwd %%mm6, %%mm0\n\t" /* sad even, odd */
            "punpckhwd %%mm6, %%mm1\n\t" /* sad noise, temp */
            "paddd %0, %%mm0\n\t"
            "paddd %1, %%mm1\n\t"
            "movq %%mm0, %0\n\t"
            "movq %%mm1, %1\n\t"
            : "=m" (s->sad.even), "=m" (s->sad.noise)
            : "m" (p->sad_thres)
            );
    }

    __asm__ volatile(
        "movq %%mm7, (%1)\n\t"
        PMAXUW((%0), %%mm7)
        "movq %%mm7, (%0)\n\t"
        "emms"
        : : "r" (&s->max), "r" (&tm), "X" (s->max)
        : "memory"
        );
#ifdef DEBUG
    if (1) {
        struct metrics cm;
        a -= 7*as;
        b -= 7*bs;
        cm = block_metrics_c(a, b, as, bs, 4, p, &ts);
        if (!MEQ(tm, cm))
            mp_msg(MSGT_VFILTER, MSGL_WARN, "Bad metrics\n");
        if (s) {
#           define CHECK(X) if (!MEQ(s->X, ts.X)) \
                mp_msg(MSGT_VFILTER, MSGL_WARN, "Bad " #X "\n");
            CHECK(tiny);
            CHECK(low);
            CHECK(high);
            CHECK(sad);
            CHECK(max);
        }
    }
#endif
#endif
    return tm;
}

static inline int
dint_copy_line_mmx2(unsigned char *dst, unsigned char *a, long bos,
                    long cos, int ds, int ss, int w, int t)
{
#if !HAVE_MMX
    mp_msg(MSGT_VFILTER, MSGL_FATAL, "dint_copy_line_mmx2: internal error\n");
    return 0;
#else
    unsigned long len = (w+7) >> 3;
    int ret;
    __asm__ volatile (
        "pxor %%mm6, %%mm6 \n\t"       /* deinterlaced pixel counter */
        "movd %0, %%mm7 \n\t"
        "punpcklbw %%mm7, %%mm7 \n\t"
        "punpcklwd %%mm7, %%mm7 \n\t"
        "punpckldq %%mm7, %%mm7 \n\t"  /* mm7 = threshold */
        : /* no output */
        : "rm" (t)
        );
    do {
        __asm__ volatile (
            "movq (%0), %%mm0\n\t"
            "movq (%0,%3,2), %%mm1\n\t"
            "movq %%mm0, (%2)\n\t"
            "pmaxub %%mm1, %%mm0\n\t"
            "pavgb (%0), %%mm1\n\t"
            "psubusb %%mm1, %%mm0\n\t"
            "paddusb %%mm7, %%mm0\n\t"  /* mm0 = max-avg+thr */
            "movq (%0,%1), %%mm2\n\t"
            "movq (%0,%5), %%mm3\n\t"
            "movq %%mm2, %%mm4\n\t"
            PDIFFUBT(%%mm1, %%mm2, %%mm5)
            PDIFFUBT(%%mm1, %%mm3, %%mm5)
            "pminub %%mm2, %%mm3\n\t"
            "pcmpeqb %%mm3, %%mm2\n\t"  /* b = min */
            "pand %%mm2, %%mm4\n\t"
            "pandn (%0,%5), %%mm2\n\t"
            "por %%mm4, %%mm2\n\t"
            "pminub %%mm0, %%mm3\n\t"
            "pcmpeqb %%mm0, %%mm3\n\t"  /* set to 1s if >= threshold */
            "psubb %%mm3, %%mm6\n\t"    /* count pixels above thr. */
            "pand %%mm3, %%mm1 \n\t"
            "pandn %%mm2, %%mm3 \n\t"
            "por %%mm3, %%mm1 \n\t"     /* avg if >= threshold */
            "movq %%mm1, (%2,%4) \n\t"
            : /* no output */
            : "r" (a), "r" ((x86_reg)bos), "r" ((x86_reg)dst), "r" ((x86_reg)ss), "r" ((x86_reg)ds), "r" ((x86_reg)cos)
            );
        a += 8;
        dst += 8;
    } while (--len);

    __asm__ volatile ("pxor %%mm7, %%mm7 \n\t"
                  "psadbw %%mm6, %%mm7 \n\t"
                  "movd %%mm7, %0 \n\t"
                  "emms \n\t"
                  : "=r" (ret)
        );
    return ret;
#endif
}

static inline int
dint_copy_line(unsigned char *dst, unsigned char *a, long bos,
               long cos, int ds, int ss, int w, int t)
{
    unsigned long len = ((unsigned long)w+sizeof(cmmx_t)-1) / sizeof(cmmx_t);
    cmmx_t dint_count = 0;
    cmmx_t thr;
    t |= t <<  8;
    thr = t | (t << 16);
    if (sizeof(cmmx_t) > 4)
        thr |= thr << (sizeof(cmmx_t)*4);
    do {
        cmmx_t e = *(cmmx_t*)a;
        cmmx_t ne = *(cmmx_t*)(a+2*ss);
        cmmx_t o = *(cmmx_t*)(a+bos);
        cmmx_t oo = *(cmmx_t*)(a+cos);
        cmmx_t maxe = pmaxub(e, ne);
        cmmx_t avge = pavgb(e, ne);
        cmmx_t max_diff = maxe - avge + thr; /* 0<=max-avg<128, thr<128 */
        cmmx_t diffo  = pdiffub(avge, o);
        cmmx_t diffoo = pdiffub(avge, oo);
        cmmx_t diffcmp = pcmpgtub(diffo, diffoo);
        cmmx_t bo = ((oo ^ o) & diffcmp) ^ o;
        cmmx_t diffbo = ((diffoo ^ diffo) & diffcmp) ^ diffo;
        cmmx_t above_thr = ~pcmpgtub(max_diff, diffbo);
        cmmx_t bo_or_avg = ((avge ^ bo) & above_thr) ^ bo;
        dint_count += above_thr & ONE_BYTES;
        *(cmmx_t*)(dst) = e;
        *(cmmx_t*)(dst+ds) = bo_or_avg;
        a += sizeof(cmmx_t);
        dst += sizeof(cmmx_t);
    } while (--len);
    return psumbw(dint_count);
}

static int
dint_copy_plane(unsigned char *d, unsigned char *a, unsigned char *b,
                unsigned char *c, unsigned long w, unsigned long h,
                unsigned long ds, unsigned long ss, unsigned long threshold,
                long field, long mmx2)
{
    unsigned long ret = 0;
    long bos = b - a;
    long cos = c - a;
    if (field) {
        fast_memcpy(d, b, w);
        h--;
        d += ds;
        a += ss;
    }
    bos += ss;
    cos += ss;
    while (h > 2) {
        if (threshold >= 128) {
            fast_memcpy(d, a, w);
            fast_memcpy(d+ds, a+bos, w);
        } else if (mmx2 == 1) {
            ret += dint_copy_line_mmx2(d, a, bos, cos, ds, ss, w, threshold);
        } else
            ret += dint_copy_line(d, a, bos, cos, ds, ss, w, threshold);
        h -= 2;
        d += 2*ds;
        a += 2*ss;
    }
    fast_memcpy(d, a, w);
    if (h == 2)
        fast_memcpy(d+ds, a+bos, w);
    return ret;
}

static void
copy_merge_fields(struct vf_priv_s *p, mp_image_t *dmpi,
                  unsigned char **old, unsigned char **new, unsigned long show)
{
    unsigned long threshold = 256;
    unsigned long field = p->swapped;
    unsigned long dint_pixels = 0;
    unsigned char **other = old;
    if (show >= 12 || !(show & 3))
        show >>= 2, other = new, new = old;
    if (show <= 2) {  /* Single field: de-interlace */
        threshold = p->dint_thres;
        field ^= show & 1;
        old = new;
    } else if (show == 3)
        old = new;
    else
        field ^= 1;
    dint_pixels +=dint_copy_plane(dmpi->planes[0], old[0], new[0],
                                  other[0], p->w, p->h, dmpi->stride[0],
                                  p->stride, threshold, field, p->mmx2);
    if (dmpi->flags & MP_IMGFLAG_PLANAR) {
        if (p->luma_only)
            old = new, other = new;
        else
            threshold = threshold/2 + 1;
        field ^= p->chroma_swapped;
        dint_copy_plane(dmpi->planes[1], old[1], new[1],
                        other[1], p->cw, p->ch,        dmpi->stride[1],
                        p->chroma_stride, threshold, field, p->mmx2);
        dint_copy_plane(dmpi->planes[2], old[2], new[2],
                        other[2], p->cw, p->ch, dmpi->stride[2],
                        p->chroma_stride, threshold, field, p->mmx2);
    }
    if (dint_pixels > 0 && p->verbose)
        mp_msg(MSGT_VFILTER,MSGL_INFO,"Deinterlaced %lu pixels\n",dint_pixels);
}

static void diff_planes(struct vf_priv_s *p, struct frame_stats *s,
                        unsigned char *of, unsigned char *nf,
                        int w, int h, int os, int ns, int swapped)
{
    int i, y;
    int align = -(long)nf & 7;
    of += align;
    nf += align;
    w -= align;
    if (swapped)
        of -= os, nf -= ns;
    i = (h*3 >> 7) & ~1;
    of += i*os + 8;
    nf += i*ns + 8;
    h -= i;
    w -= 16;

    memset(s, 0, sizeof(*s));

    for (y = (h-8) >> 3; y; y--) {
        if (p->mmx2 == 1) {
            for (i = 0; i < w; i += 8)
                block_metrics_mmx2(of+i, nf+i, os, ns, 4, p, s);
        } else if (p->mmx2 == 2) {
            for (i = 0; i < w; i += 8)
                block_metrics_3dnow(of+i, nf+i, os, ns, 4, p, s);
        } else if (p->fast > 3) {
            for (i = 0; i < w; i += 8)
                block_metrics_faster_c(of+i, nf+i, os, ns, 4, p, s);
        } else if (p->fast > 1) {
            for (i = 0; i < w; i += 8)
                block_metrics_fast_c(of+i, nf+i, os, ns, 4, p, s);
        } else {
            for (i = 0; i < w; i += 8)
                block_metrics_c(of+i, nf+i, os, ns, 4, p, s);
        }
        of += 8*os;
        nf += 8*ns;
    }
}

#define METRICS(X) (X).even, (X).odd, (X).noise, (X).temp

static void diff_fields(struct vf_priv_s *p, struct frame_stats *s,
                        unsigned char **old, unsigned char **new)
{
    diff_planes(p, s, old[0], new[0], p->w, p->h,
                p->stride, p->stride, p->swapped);
    s->sad.even  = (s->sad.even  * 16ul) / s->num_blocks;
    s->sad.odd   = (s->sad.odd   * 16ul) / s->num_blocks;
    s->sad.noise = (s->sad.noise * 16ul) / s->num_blocks;
    s->sad.temp  = (s->sad.temp  * 16ul) / s->num_blocks;
    if (p->verbose)
        mp_msg(MSGT_VFILTER, MSGL_INFO, "%lu%c M:%d/%d/%d/%d - %d, "
               "t:%d/%d/%d/%d, l:%d/%d/%d/%d, h:%d/%d/%d/%d, bg:%d/%d/%d/%d, "
               "2x:%d/%d/%d/%d, sad:%d/%d/%d/%d, lil:%d, hil:%d, ios:%.1f\n",
               p->inframes, p->chflag, METRICS(s->max), s->num_blocks,
               METRICS(s->tiny), METRICS(s->low), METRICS(s->high),
               METRICS(s->bigger), METRICS(s->twox), METRICS(s->sad),
               s->interlaced_low, s->interlaced_high,
               p->iosync / (double) p->in_inc);
}

static const char *parse_args(struct vf_priv_s *p, const char *args)
{
    args--;
    while (args && *++args &&
           (sscanf(args, "io=%lu:%lu", &p->out_dec, &p->in_inc) == 2 ||
            sscanf(args, "diff_thres=%hu", &p->thres.even ) == 1 ||
            sscanf(args, "comb_thres=%hu", &p->thres.noise) == 1 ||
            sscanf(args, "sad_thres=%lu",  &p->sad_thres  ) == 1 ||
            sscanf(args, "dint_thres=%lu", &p->dint_thres ) == 1 ||
            sscanf(args, "fast=%u",        &p->fast       ) == 1 ||
            sscanf(args, "mmx2=%lu",       &p->mmx2       ) == 1 ||
            sscanf(args, "luma_only=%u",   &p->luma_only  ) == 1 ||
            sscanf(args, "verbose=%u",     &p->verbose    ) == 1 ||
            sscanf(args, "crop=%lu:%lu:%lu:%lu", &p->w,
                   &p->h, &p->crop_x, &p->crop_y) == 4))
        args = strchr(args, '/');
    return args;
}

static unsigned long gcd(unsigned long x, unsigned long y)
{
    unsigned long t;
    if (x > y)
        t = x, x = y, y = t;

    while (x) {
        t = y % x;
        y = x;
        x = t;
    }
    return y;
}

static void init(struct vf_priv_s *p, mp_image_t *mpi)
{
    unsigned long i;
    unsigned long plane_size, chroma_plane_size;
    unsigned char *plane;
    unsigned long cos, los;
    p->crop_cx = p->crop_x >> mpi->chroma_x_shift;
    p->crop_cy = p->crop_y >> mpi->chroma_y_shift;
    if (mpi->flags & MP_IMGFLAG_ACCEPT_STRIDE) {
        p->stride = (mpi->w + 15) & ~15;
        p->chroma_stride = p->stride >> mpi->chroma_x_shift;
    } else {
        p->stride = mpi->width;
        p->chroma_stride = mpi->chroma_width;
    }
    p->cw = p->w >> mpi->chroma_x_shift;
    p->ch = p->h >> mpi->chroma_y_shift;
    p->nplanes = 1;
    p->static_idx = 0;
    p->temp_idx = 0;
    p->old_planes = p->planes[0];
    plane_size = mpi->h * p->stride;
    chroma_plane_size = mpi->flags & MP_IMGFLAG_PLANAR ?
        mpi->chroma_height * p->chroma_stride : 0;
    p->memory_allocated =
        malloc(NUM_STORED * (plane_size+2*chroma_plane_size) +
               8*p->chroma_stride + 4096);
    /* align to page boundary */
    plane = p->memory_allocated + (-(long)p->memory_allocated & 4095);
    memset(plane, 0, NUM_STORED * plane_size);
    los = p->crop_x  + p->crop_y  * p->stride;
    cos = p->crop_cx + p->crop_cy * p->chroma_stride;
    for (i = 0; i != NUM_STORED; i++, plane += plane_size) {
        p->planes[i][0] = plane;
        p->planes[NUM_STORED + i][0] = plane + los;
    }
    if (mpi->flags & MP_IMGFLAG_PLANAR) {
        p->nplanes = 3;
        memset(plane, 0x80, NUM_STORED * 2 * chroma_plane_size);
        for (i = 0; i != NUM_STORED; i++) {
            p->planes[i][1] = plane;
            p->planes[NUM_STORED + i][1] = plane + cos;
            plane += chroma_plane_size;
            p->planes[i][2] = plane;
            p->planes[NUM_STORED + i][2] = plane + cos;
            plane += chroma_plane_size;
        }
    }
    p->out_dec <<= 2;
    i = gcd(p->in_inc, p->out_dec);
    p->in_inc /= i;
    p->out_dec /= i;
    p->iosync = 0;
    p->num_fields = 3;
}

static inline double get_time(void)
{
    struct timeval tv;
    gettimeofday(&tv, 0);
    return tv.tv_sec + tv.tv_usec * 1e-6;
}

static void get_image(struct vf_instance *vf, mp_image_t *mpi)
{
    struct vf_priv_s *p = vf->priv;
    static unsigned char **planes, planes_idx;

    if (mpi->type == MP_IMGTYPE_STATIC) return;

    if (!p->planes[0][0]) init(p, mpi);

    if (mpi->type == MP_IMGTYPE_TEMP ||
        (mpi->type == MP_IMGTYPE_IPB && !(mpi->flags & MP_IMGFLAG_READABLE)))
        planes_idx = NUM_STORED/2 + (++p->temp_idx % (NUM_STORED/2));
    else
        planes_idx = ++p->static_idx % (NUM_STORED/2);
    planes = p->planes[planes_idx];
    mpi->priv = p->planes[NUM_STORED + planes_idx];
    if (mpi->priv == p->old_planes) {
        unsigned char **old_planes =
            p->planes[NUM_STORED + 2 + (++p->temp_idx & 1)];
        my_memcpy_pic(old_planes[0], p->old_planes[0],
                      p->w, p->h, p->stride, p->stride);
        if (mpi->flags & MP_IMGFLAG_PLANAR) {
            my_memcpy_pic(old_planes[1], p->old_planes[1],
                          p->cw, p->ch, p->chroma_stride, p->chroma_stride);
            my_memcpy_pic(old_planes[2], p->old_planes[2],
                          p->cw, p->ch, p->chroma_stride, p->chroma_stride);
        }
        p->old_planes = old_planes;
        p->num_copies++;
    }
    mpi->planes[0] = planes[0];
    mpi->stride[0] = p->stride;
    if (mpi->flags & MP_IMGFLAG_PLANAR) {
        mpi->planes[1] = planes[1];
        mpi->planes[2] = planes[2];
        mpi->stride[1] = mpi->stride[2] = p->chroma_stride;
    }
    mpi->width = p->stride;

    mpi->flags |= MP_IMGFLAG_DIRECT;
    mpi->flags &= ~MP_IMGFLAG_DRAW_CALLBACK;
}

static inline long
cmpe(unsigned long x, unsigned long y, unsigned long err, unsigned long e)
{
    long diff = x-y;
    long unit = ((x+y+err) >> e);
    long ret = (diff > unit) - (diff < -unit);
    unit >>= 1;
    return ret + (diff > unit) - (diff < -unit);
}

static unsigned long
find_breaks(struct vf_priv_s *p, struct frame_stats *s)
{
    struct frame_stats *ps = &p->stats[(p->inframes-1) & 1];
    long notfilm = 5*p->in_inc - p->out_dec;
    unsigned long n = s->num_blocks >> 8;
    unsigned long sad_comb_cmp = cmpe(s->sad.temp, s->sad.noise, 512, 1);
    unsigned long ret = 8;

    if (cmpe(s->sad.temp, s->sad.even, 512, 1) > 0)
        mp_msg(MSGT_VFILTER, MSGL_WARN,
               "@@@@@@@@ Bottom-first field??? @@@@@@@@\n");
    if (s->sad.temp > 1000 && s->sad.noise > 1000)
        return 3;
    if (s->interlaced_high >= 2*n && s->sad.temp > 256 && s->sad.noise > 256)
        return 3;
    if (s->high.noise > s->num_blocks/4 && s->sad.noise > 10000 &&
        s->sad.noise > 2*s->sad.even && s->sad.noise > 2*ps->sad.odd) {
        // Mid-frame scene change
        if (s->tiny.temp + s->interlaced_low  < n   ||
            s->low.temp  + s->interlaced_high < n/4 ||
            s->high.temp + s->interlaced_high < n/8 ||
            s->sad.temp < 160)
            return 1;
        return 3;
    }
    if (s->high.temp > s->num_blocks/4 && s->sad.temp > 10000 &&
        s->sad.temp > 2*ps->sad.odd && s->sad.temp > 2*ps->sad.even) {
        // Start frame scene change
        if (s->tiny.noise + s->interlaced_low  < n   ||
            s->low.noise  + s->interlaced_high < n/4 ||
            s->high.noise + s->interlaced_high < n/8 ||
            s->sad.noise < 160)
            return 2;
        return 3;
    }
    if (sad_comb_cmp == 2)
        return 2;
    if (sad_comb_cmp == -2)
        return 1;

    if (s->tiny.odd > 3*MAX(n,s->tiny.even) + s->interlaced_low)
        return 1;
    if (s->tiny.even > 3*MAX(n,s->tiny.odd)+s->interlaced_low &&
        (!sad_comb_cmp || (s->low.noise <= n/4 && s->low.temp <= n/4)))
        return 4;

    if (s->sad.noise < 64 && s->sad.temp < 64 &&
        s->low.noise <= n/2 && s->high.noise <= n/4 &&
        s->low.temp  <= n/2 && s->high.temp  <= n/4)
        goto still;

    if (s->tiny.temp > 3*MAX(n,s->tiny.noise) + s->interlaced_low)
        return 2;
    if (s->tiny.noise > 3*MAX(n,s->tiny.temp) + s->interlaced_low)
        return 1;

    if (s->low.odd > 3*MAX(n/4,s->low.even) + s->interlaced_high)
        return 1;
    if (s->low.even > 3*MAX(n/4,s->low.odd)+s->interlaced_high &&
        s->sad.even > 2*s->sad.odd &&
        (!sad_comb_cmp || (s->low.noise <= n/4 && s->low.temp <= n/4)))
        return 4;

    if (s->low.temp > 3*MAX(n/4,s->low.noise) + s->interlaced_high)
        return 2;
    if (s->low.noise > 3*MAX(n/4,s->low.temp) + s->interlaced_high)
        return 1;

    if (sad_comb_cmp == 1 && s->sad.noise < 64)
        return 2;
    if (sad_comb_cmp == -1 && s->sad.temp < 64)
        return 1;

    if (s->tiny.odd <= n || (s->tiny.noise <= n/2 && s->tiny.temp <= n/2)) {
        if (s->interlaced_low <= n) {
            if (p->num_fields == 1)
                goto still;
            if (s->tiny.even <= n || ps->tiny.noise <= n/2)
                /* Still frame */
                goto still;
            if (s->bigger.even >= 2*MAX(n,s->bigger.odd) + s->interlaced_low)
                return 4;
            if (s->low.even >= 2*n + s->interlaced_low)
                return 4;
            goto still;
        }
    }
    if (s->low.odd <= n/4) {
        if (s->interlaced_high <= n/4) {
            if (p->num_fields == 1)
                goto still;
            if (s->low.even <= n/4)
                /* Still frame */
                goto still;
            if (s->bigger.even >= 2*MAX(n/4,s->bigger.odd)+s->interlaced_high)
                return 4;
            if (s->low.even >= n/2 + s->interlaced_high)
                return 4;
            goto still;
        }
    }
    if (s->bigger.temp > 2*MAX(n,s->bigger.noise) + s->interlaced_low)
        return 2;
    if (s->bigger.noise > 2*MAX(n,s->bigger.temp) + s->interlaced_low)
        return 1;
    if (s->bigger.temp > 2*MAX(n,s->bigger.noise) + s->interlaced_high)
        return 2;
    if (s->bigger.noise > 2*MAX(n,s->bigger.temp) + s->interlaced_high)
        return 1;
    if (s->twox.temp > 2*MAX(n,s->twox.noise) + s->interlaced_high)
        return 2;
    if (s->twox.noise > 2*MAX(n,s->twox.temp) + s->interlaced_high)
        return 1;
    if (s->bigger.even > 2*MAX(n,s->bigger.odd) + s->interlaced_low &&
        s->bigger.temp < n && s->bigger.noise < n)
        return 4;
    if (s->interlaced_low > MIN(2*n, s->tiny.odd))
        return 3;
    ret = 8 + (1 << (s->sad.temp > s->sad.noise));
  still:
    if (p->num_fields == 1 && p->prev_fields == 3 && notfilm >= 0 &&
        (s->tiny.temp <= s->tiny.noise || s->sad.temp < s->sad.noise+16))
        return 1;
    if (p->notout < p->num_fields && p->iosync > 2*p->in_inc && notfilm < 0)
        notfilm = 0;
    if (p->num_fields < 2 ||
        (p->num_fields == 2 && p->prev_fields == 2 && notfilm < 0))
        return ret;
    if (!notfilm && (p->prev_fields&~1) == 2) {
        if (p->prev_fields + p->num_fields == 5) {
            if (s->tiny.noise <= s->tiny.temp ||
                s->low.noise == 0 || s->low.noise < s->low.temp ||
                s->sad.noise < s->sad.temp+16)
                return 2;
        }
        if (p->prev_fields + p->num_fields == 4) {
            if (s->tiny.temp <= s->tiny.noise ||
                s->low.temp == 0 || s->low.temp < s->low.noise ||
                s->sad.temp < s->sad.noise+16)
                return 1;
        }
    }
    if (p->num_fields > 2 &&
        ps->sad.noise > s->sad.noise && ps->sad.noise > s->sad.temp)
        return 4;
    return 2 >> (s->sad.noise > s->sad.temp);
}

#define ITOC(X) (!(X) ? ' ' : (X) + ((X)>9 ? 'a'-10 : '0'))

static int put_image(struct vf_instance *vf, mp_image_t *mpi, double pts)
{
    mp_image_t *dmpi;
    struct vf_priv_s *p = vf->priv;
    unsigned char **planes, **old_planes;
    struct frame_stats *s  = &p->stats[p->inframes & 1];
    struct frame_stats *ps = &p->stats[(p->inframes-1) & 1];
    int swapped = 0;
    const int flags = mpi->fields;
    int breaks, prev;
    int show_fields = 0;
    int dropped_fields = 0;
    double start_time, diff_time;
    char prev_chflag = p->chflag;
    int keep_rate;

    if (!p->planes[0][0]) init(p, mpi);

    old_planes = p->old_planes;

    if ((mpi->flags & MP_IMGFLAG_DIRECT) && mpi->priv) {
        planes = mpi->priv;
        mpi->priv = 0;
    } else {
        planes = p->planes[2 + (++p->temp_idx & 1)];
        my_memcpy_pic(planes[0],
                      mpi->planes[0] + p->crop_x + p->crop_y * mpi->stride[0],
                      p->w, p->h, p->stride, mpi->stride[0]);
        if (mpi->flags & MP_IMGFLAG_PLANAR) {
            my_memcpy_pic(planes[1],
                          mpi->planes[1] + p->crop_cx + p->crop_cy * mpi->stride[1],
                          p->cw, p->ch, p->chroma_stride, mpi->stride[1]);
            my_memcpy_pic(planes[2],
                          mpi->planes[2] + p->crop_cx + p->crop_cy * mpi->stride[2],
                          p->cw, p->ch, p->chroma_stride, mpi->stride[2]);
            p->num_copies++;
        }
    }

    p->old_planes = planes;
    p->chflag = ';';
    if (flags & MP_IMGFIELD_ORDERED) {
        swapped = !(flags & MP_IMGFIELD_TOP_FIRST);
        p->chflag = (flags & MP_IMGFIELD_REPEAT_FIRST ? '|' :
                     flags & MP_IMGFIELD_TOP_FIRST ? ':' : '.');
    }
    p->swapped = swapped;

    start_time = get_time();
    if (p->chflag == '|') {
        *s = ppzs;
        p->iosync += p->in_inc;
    } else if ((p->fast & 1) && prev_chflag == '|')
        *s = pprs;
    else
        diff_fields(p, s, old_planes, planes);
    diff_time = get_time();
    p->diff_time += diff_time - start_time;
    breaks = p->inframes ? find_breaks(p, s) : 2;
    p->inframes++;
    keep_rate = 4*p->in_inc == p->out_dec;

    switch (breaks) {
      case 0:
      case 8:
      case 9:
      case 10:
        if (!keep_rate && p->notout < p->num_fields && p->iosync < 2*p->in_inc)
            break;
        if (p->notout < p->num_fields)
            dropped_fields = -2;
      case 4:
        if (keep_rate || p->iosync >= -2*p->in_inc)
            show_fields = (4<<p->num_fields)-1;
        break;
      case 3:
        if (keep_rate)
            show_fields = 2;
        else if (p->iosync > 0) {
            if (p->notout >= p->num_fields && p->iosync > 2*p->in_inc) {
                show_fields = 4; /* prev odd only */
                if (p->num_fields > 1)
                    show_fields |= 8; /* + prev even */
            } else {
                show_fields = 2; /* even only */
                if (p->notout >= p->num_fields)
                    dropped_fields += p->num_fields;
            }
        }
        break;
      case 2:
        if (p->iosync <= -3*p->in_inc) {
            if (p->notout >= p->num_fields)
                dropped_fields = p->num_fields;
            break;
        }
        if (p->num_fields == 1) {
            int prevbreak = ps->sad.noise >= 128;
            if (p->iosync < 4*p->in_inc) {
                show_fields = 3;
                dropped_fields = prevbreak;
            } else {
                show_fields = 4 | (!prevbreak << 3);
                if (p->notout < 1 + p->prev_fields)
                    dropped_fields = -!prevbreak;
            }
            break;
        }
      default:
        if (keep_rate)
            show_fields = 3 << (breaks & 1);
        else if (p->notout >= p->num_fields &&
            p->iosync >= (breaks == 1 ? -p->in_inc :
                          p->in_inc << (p->num_fields == 1))) {
            show_fields = (1 << (2 + p->num_fields)) - (1<<breaks);
        } else {
            if (p->notout >= p->num_fields)
                dropped_fields += p->num_fields + 2 - breaks;
            if (breaks == 1) {
                if (p->iosync >= 4*p->in_inc)
                    show_fields = 6;
            } else if (p->iosync > -3*p->in_inc)
                show_fields = 3;  /* odd+even */
        }
        break;
    }

    show_fields &= 15;
    prev = p->prev_fields;
    if (breaks < 8) {
        if (p->num_fields == 1)
            breaks &= ~4;
        if (breaks)
            p->num_breaks++;
        if (breaks == 3)
            p->prev_fields = p->num_fields = 1;
        else if (breaks) {
            p->prev_fields = p->num_fields + (breaks==1) - (breaks==4);
            p->num_fields = breaks - (breaks == 4) + (p->chflag == '|');
        } else
            p->num_fields += 2;
    } else
        p->num_fields += 2;

    p->iosync += 4 * p->in_inc;
    if (p->chflag == '|')
        p->iosync += p->in_inc;

    if (show_fields) {
        p->iosync -= p->out_dec;
        p->notout = !(show_fields & 1) + !(show_fields & 3);
        if (((show_fields &  3) ==  3 &&
             (s->low.noise + s->interlaced_low < (s->num_blocks>>8) ||
              s->sad.noise < 160)) ||
            ((show_fields & 12) == 12 &&
             (ps->low.noise + ps->interlaced_low < (s->num_blocks>>8) ||
              ps->sad.noise < 160))) {
            p->export_count++;
            dmpi = vf_get_image(vf->next, mpi->imgfmt, MP_IMGTYPE_EXPORT,
                                MP_IMGFLAG_PRESERVE|MP_IMGFLAG_READABLE,
                                p->w, p->h);
            if ((show_fields & 3) != 3) planes = old_planes;
            dmpi->planes[0] = planes[0];
            dmpi->stride[0] = p->stride;
            dmpi->width = mpi->width;
            if (mpi->flags & MP_IMGFLAG_PLANAR) {
                dmpi->planes[1] = planes[1];
                dmpi->planes[2] = planes[2];
                dmpi->stride[1] = p->chroma_stride;
                dmpi->stride[2] = p->chroma_stride;
            }
        } else {
            p->merge_count++;
            dmpi = vf_get_image(vf->next, mpi->imgfmt,
                                MP_IMGTYPE_TEMP, MP_IMGFLAG_ACCEPT_STRIDE,
                                p->w, p->h);
            copy_merge_fields(p, dmpi, old_planes, planes, show_fields);
        }
        p->outframes++;
    } else
        p->notout += 2;

    if (p->verbose)
        mp_msg(MSGT_VFILTER, MSGL_INFO, "%lu %lu: %x %c %c %lu%s%s%c%s\n",
               p->inframes, p->outframes,
               breaks, breaks<8 && breaks>0 ? (int) p->prev_fields+'0' : ' ',
               ITOC(show_fields),
               p->num_breaks, 5*p->in_inc == p->out_dec && breaks<8 &&
               breaks>0 && ((prev&~1)!=2 || prev+p->prev_fields!=5) ?
               " ######## bad telecine ########" : "",
               dropped_fields ? " ======== dropped ":"", ITOC(dropped_fields),
               !show_fields || (show_fields & (show_fields-1)) ?
               "" : " @@@@@@@@@@@@@@@@@");

    p->merge_time += get_time() - diff_time;
    return show_fields ? vf_next_put_image(vf, dmpi, MP_NOPTS_VALUE) : 0;
}

static int query_format(struct vf_instance *vf, unsigned int fmt)
{
    /* FIXME - support more formats */
    switch (fmt) {
      case IMGFMT_YV12:
      case IMGFMT_IYUV:
      case IMGFMT_I420:
      case IMGFMT_411P:
      case IMGFMT_422P:
      case IMGFMT_444P:
        return vf_next_query_format(vf, fmt);
    }
    return 0;
}

static int config(struct vf_instance *vf,
                  int width, int height, int d_width, int d_height,
                  unsigned int flags, unsigned int outfmt)
{
    unsigned long cxm = 0;
    unsigned long cym = 0;
    struct vf_priv_s *p = vf->priv;
    // rounding:
    if(!IMGFMT_IS_RGB(outfmt) && !IMGFMT_IS_BGR(outfmt)){
        switch(outfmt){
          case IMGFMT_444P:
          case IMGFMT_Y800:
          case IMGFMT_Y8:
            break;
          case IMGFMT_YVU9:
          case IMGFMT_IF09:
            cym = 3;
          case IMGFMT_411P:
            cxm = 3;
            break;
          case IMGFMT_YV12:
          case IMGFMT_I420:
          case IMGFMT_IYUV:
            cym = 1;
          default:
            cxm = 1;
        }
    }
    p->chroma_swapped = !!(p->crop_y & (cym+1));
    if (p->w) p->w += p->crop_x & cxm;
    if (p->h) p->h += p->crop_y & cym;
    p->crop_x &= ~cxm;
    p->crop_y &= ~cym;
    if (!p->w || p->w > width ) p->w = width;
    if (!p->h || p->h > height) p->h = height;
    if (p->crop_x + p->w > width ) p->crop_x = 0;
    if (p->crop_y + p->h > height) p->crop_y = 0;

    if(!opt_screen_size_x && !opt_screen_size_y){
        d_width = d_width * p->w/width;
        d_height = d_height * p->h/height;
    }
    return vf_next_config(vf, p->w, p->h, d_width, d_height, flags, outfmt);
}

static void uninit(struct vf_instance *vf)
{
    struct vf_priv_s *p = vf->priv;
    mp_msg(MSGT_VFILTER, MSGL_INFO, "diff_time: %.3f, merge_time: %.3f, "
           "export: %lu, merge: %lu, copy: %lu\n", p->diff_time, p->merge_time,
           p->export_count, p->merge_count, p->num_copies);
    free(p->memory_allocated);
    free(p);
}

static int vf_open(vf_instance_t *vf, char *args)
{
    struct vf_priv_s *p;
    vf->get_image = get_image;
    vf->put_image = put_image;
    vf->config = config;
    vf->query_format = query_format;
    vf->uninit = uninit;
    vf->default_reqs = VFCAP_ACCEPT_STRIDE;
    vf->priv = p = calloc(1, sizeof(struct vf_priv_s));
    p->out_dec = 5;
    p->in_inc = 4;
    p->thres.noise = 128;
    p->thres.even  = 128;
    p->sad_thres = 64;
    p->dint_thres = 4;
    p->luma_only = 0;
    p->fast = 3;
    p->mmx2 = gCpuCaps.hasMMX2 ? 1 : gCpuCaps.has3DNow ? 2 : 0;
    if (args) {
        const char *args_remain = parse_args(p, args);
        if (args_remain) {
            mp_msg(MSGT_VFILTER, MSGL_FATAL,
                   "filmdint: unknown suboption: %s\n", args_remain);
            return 0;
        }
        if (p->out_dec < p->in_inc) {
            mp_msg(MSGT_VFILTER, MSGL_FATAL,
                   "filmdint: increasing the frame rate is not supported\n");
            return 0;
        }
    }
    if (p->mmx2 > 2)
        p->mmx2 = 0;
#if !HAVE_MMX
    p->mmx2 = 0;
#endif
#if !HAVE_AMD3DNOW
    p->mmx2 &= 1;
#endif
    p->thres.odd  = p->thres.even;
    p->thres.temp = p->thres.noise;
    p->diff_time = 0;
    p->merge_time = 0;
    return 1;
}

const vf_info_t vf_info_filmdint = {
    "Advanced inverse telecine filer",
    "filmdint",
    "Zoltan Hidvegi",
    "",
    vf_open,
    NULL
};
