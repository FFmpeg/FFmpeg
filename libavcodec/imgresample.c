/*
 * High quality image resampling with polyphase filters 
 * Copyright (c) 2001 Fabrice Bellard.
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
 
/**
 * @file imgresample.c
 * High quality image resampling with polyphase filters .
 */
 
#include "avcodec.h"
#include "dsputil.h"

#ifdef USE_FASTMEMCPY
#include "fastmemcpy.h"
#endif

#define NB_COMPONENTS 3

#define PHASE_BITS 4
#define NB_PHASES  (1 << PHASE_BITS)
#define NB_TAPS    4
#define FCENTER    1  /* index of the center of the filter */
//#define TEST    1  /* Test it */

#define POS_FRAC_BITS 16
#define POS_FRAC      (1 << POS_FRAC_BITS)
/* 6 bits precision is needed for MMX */
#define FILTER_BITS   8

#define LINE_BUF_HEIGHT (NB_TAPS * 4)

struct ImgReSampleContext {
    int iwidth, iheight, owidth, oheight;
    int topBand, bottomBand, leftBand, rightBand;
    int padtop, padbottom, padleft, padright;
    int pad_owidth, pad_oheight;
    int h_incr, v_incr;
    int16_t h_filters[NB_PHASES][NB_TAPS] __align8; /* horizontal filters */
    int16_t v_filters[NB_PHASES][NB_TAPS] __align8; /* vertical filters */
    uint8_t *line_buf;
};

void av_build_filter(int16_t *filter, double factor, int tap_count, int phase_count, int scale, int type);

static inline int get_phase(int pos)
{
    return ((pos) >> (POS_FRAC_BITS - PHASE_BITS)) & ((1 << PHASE_BITS) - 1);
}

/* This function must be optimized */
static void h_resample_fast(uint8_t *dst, int dst_width, const uint8_t *src,
			    int src_width, int src_start, int src_incr,
			    int16_t *filters)
{
    int src_pos, phase, sum, i;
    const uint8_t *s;
    int16_t *filter;

    src_pos = src_start;
    for(i=0;i<dst_width;i++) {
#ifdef TEST
        /* test */
        if ((src_pos >> POS_FRAC_BITS) < 0 ||
            (src_pos >> POS_FRAC_BITS) > (src_width - NB_TAPS))
            av_abort();
#endif
        s = src + (src_pos >> POS_FRAC_BITS);
        phase = get_phase(src_pos);
        filter = filters + phase * NB_TAPS;
#if NB_TAPS == 4
        sum = s[0] * filter[0] +
            s[1] * filter[1] +
            s[2] * filter[2] +
            s[3] * filter[3];
#else
        {
            int j;
            sum = 0;
            for(j=0;j<NB_TAPS;j++)
                sum += s[j] * filter[j];
        }
#endif
        sum = sum >> FILTER_BITS;
        if (sum < 0)
            sum = 0;
        else if (sum > 255)
            sum = 255;
        dst[0] = sum;
        src_pos += src_incr;
        dst++;
    }
}

/* This function must be optimized */
static void v_resample(uint8_t *dst, int dst_width, const uint8_t *src,
		       int wrap, int16_t *filter)
{
    int sum, i;
    const uint8_t *s;

    s = src;
    for(i=0;i<dst_width;i++) {
#if NB_TAPS == 4
        sum = s[0 * wrap] * filter[0] +
            s[1 * wrap] * filter[1] +
            s[2 * wrap] * filter[2] +
            s[3 * wrap] * filter[3];
#else
        {
            int j;
            uint8_t *s1 = s;

            sum = 0;
            for(j=0;j<NB_TAPS;j++) {
                sum += s1[0] * filter[j];
                s1 += wrap;
            }
        }
#endif
        sum = sum >> FILTER_BITS;
        if (sum < 0)
            sum = 0;
        else if (sum > 255)
            sum = 255;
        dst[0] = sum;
        dst++;
        s++;
    }
}

#ifdef HAVE_MMX

#include "i386/mmx.h"

#define FILTER4(reg) \
{\
        s = src + (src_pos >> POS_FRAC_BITS);\
        phase = get_phase(src_pos);\
        filter = filters + phase * NB_TAPS;\
        movq_m2r(*s, reg);\
        punpcklbw_r2r(mm7, reg);\
        movq_m2r(*filter, mm6);\
        pmaddwd_r2r(reg, mm6);\
        movq_r2r(mm6, reg);\
        psrlq_i2r(32, reg);\
        paddd_r2r(mm6, reg);\
        psrad_i2r(FILTER_BITS, reg);\
        src_pos += src_incr;\
}

#define DUMP(reg) movq_r2m(reg, tmp); printf(#reg "=%016Lx\n", tmp.uq);

/* XXX: do four pixels at a time */
static void h_resample_fast4_mmx(uint8_t *dst, int dst_width,
				 const uint8_t *src, int src_width,
                                 int src_start, int src_incr, int16_t *filters)
{
    int src_pos, phase;
    const uint8_t *s;
    int16_t *filter;
    mmx_t tmp;
    
    src_pos = src_start;
    pxor_r2r(mm7, mm7);

    while (dst_width >= 4) {

        FILTER4(mm0);
        FILTER4(mm1);
        FILTER4(mm2);
        FILTER4(mm3);

        packuswb_r2r(mm7, mm0);
        packuswb_r2r(mm7, mm1);
        packuswb_r2r(mm7, mm3);
        packuswb_r2r(mm7, mm2);
        movq_r2m(mm0, tmp);
        dst[0] = tmp.ub[0];
        movq_r2m(mm1, tmp);
        dst[1] = tmp.ub[0];
        movq_r2m(mm2, tmp);
        dst[2] = tmp.ub[0];
        movq_r2m(mm3, tmp);
        dst[3] = tmp.ub[0];
        dst += 4;
        dst_width -= 4;
    }
    while (dst_width > 0) {
        FILTER4(mm0);
        packuswb_r2r(mm7, mm0);
        movq_r2m(mm0, tmp);
        dst[0] = tmp.ub[0];
        dst++;
        dst_width--;
    }
    emms();
}

static void v_resample4_mmx(uint8_t *dst, int dst_width, const uint8_t *src,
			    int wrap, int16_t *filter)
{
    int sum, i, v;
    const uint8_t *s;
    mmx_t tmp;
    mmx_t coefs[4];
    
    for(i=0;i<4;i++) {
        v = filter[i];
        coefs[i].uw[0] = v;
        coefs[i].uw[1] = v;
        coefs[i].uw[2] = v;
        coefs[i].uw[3] = v;
    }
    
    pxor_r2r(mm7, mm7);
    s = src;
    while (dst_width >= 4) {
        movq_m2r(s[0 * wrap], mm0);
        punpcklbw_r2r(mm7, mm0);
        movq_m2r(s[1 * wrap], mm1);
        punpcklbw_r2r(mm7, mm1);
        movq_m2r(s[2 * wrap], mm2);
        punpcklbw_r2r(mm7, mm2);
        movq_m2r(s[3 * wrap], mm3);
        punpcklbw_r2r(mm7, mm3);

        pmullw_m2r(coefs[0], mm0);
        pmullw_m2r(coefs[1], mm1);
        pmullw_m2r(coefs[2], mm2);
        pmullw_m2r(coefs[3], mm3);

        paddw_r2r(mm1, mm0);
        paddw_r2r(mm3, mm2);
        paddw_r2r(mm2, mm0);
        psraw_i2r(FILTER_BITS, mm0);
        
        packuswb_r2r(mm7, mm0);
        movq_r2m(mm0, tmp);

        *(uint32_t *)dst = tmp.ud[0];
        dst += 4;
        s += 4;
        dst_width -= 4;
    }
    while (dst_width > 0) {
        sum = s[0 * wrap] * filter[0] +
            s[1 * wrap] * filter[1] +
            s[2 * wrap] * filter[2] +
            s[3 * wrap] * filter[3];
        sum = sum >> FILTER_BITS;
        if (sum < 0)
            sum = 0;
        else if (sum > 255)
            sum = 255;
        dst[0] = sum;
        dst++;
        s++;
        dst_width--;
    }
    emms();
}
#endif

#ifdef HAVE_ALTIVEC
typedef	union {
    vector unsigned char v;
    unsigned char c[16];
} vec_uc_t;

typedef	union {
    vector signed short v;
    signed short s[8];
} vec_ss_t;

void v_resample16_altivec(uint8_t *dst, int dst_width, const uint8_t *src,
			  int wrap, int16_t *filter)
{
    int sum, i;
    const uint8_t *s;
    vector unsigned char *tv, tmp, dstv, zero;
    vec_ss_t srchv[4], srclv[4], fv[4];
    vector signed short zeros, sumhv, sumlv;    
    s = src;

    for(i=0;i<4;i++)
    {
        /*
           The vec_madds later on does an implicit >>15 on the result.
           Since FILTER_BITS is 8, and we have 15 bits of magnitude in
           a signed short, we have just enough bits to pre-shift our
           filter constants <<7 to compensate for vec_madds.
        */
        fv[i].s[0] = filter[i] << (15-FILTER_BITS);
        fv[i].v = vec_splat(fv[i].v, 0);
    }
    
    zero = vec_splat_u8(0);
    zeros = vec_splat_s16(0);


    /*
       When we're resampling, we'd ideally like both our input buffers,
       and output buffers to be 16-byte aligned, so we can do both aligned
       reads and writes. Sadly we can't always have this at the moment, so
       we opt for aligned writes, as unaligned writes have a huge overhead.
       To do this, do enough scalar resamples to get dst 16-byte aligned.
    */
    i = (-(int)dst) & 0xf;
    while(i>0) {
        sum = s[0 * wrap] * filter[0] +
        s[1 * wrap] * filter[1] +
        s[2 * wrap] * filter[2] +
        s[3 * wrap] * filter[3];
        sum = sum >> FILTER_BITS;
        if (sum<0) sum = 0; else if (sum>255) sum=255;
        dst[0] = sum;
        dst++;
        s++;
        dst_width--;
        i--;
    }
    
    /* Do our altivec resampling on 16 pixels at once. */
    while(dst_width>=16) {
        /*
           Read 16 (potentially unaligned) bytes from each of
           4 lines into 4 vectors, and split them into shorts.
           Interleave the multipy/accumulate for the resample
           filter with the loads to hide the 3 cycle latency
           the vec_madds have.
        */
        tv = (vector unsigned char *) &s[0 * wrap];
        tmp = vec_perm(tv[0], tv[1], vec_lvsl(0, &s[i * wrap]));
        srchv[0].v = (vector signed short) vec_mergeh(zero, tmp);
        srclv[0].v = (vector signed short) vec_mergel(zero, tmp);
        sumhv = vec_madds(srchv[0].v, fv[0].v, zeros);
        sumlv = vec_madds(srclv[0].v, fv[0].v, zeros);

        tv = (vector unsigned char *) &s[1 * wrap];
        tmp = vec_perm(tv[0], tv[1], vec_lvsl(0, &s[1 * wrap]));
        srchv[1].v = (vector signed short) vec_mergeh(zero, tmp);
        srclv[1].v = (vector signed short) vec_mergel(zero, tmp);
        sumhv = vec_madds(srchv[1].v, fv[1].v, sumhv);
        sumlv = vec_madds(srclv[1].v, fv[1].v, sumlv);

        tv = (vector unsigned char *) &s[2 * wrap];
        tmp = vec_perm(tv[0], tv[1], vec_lvsl(0, &s[2 * wrap]));
        srchv[2].v = (vector signed short) vec_mergeh(zero, tmp);
        srclv[2].v = (vector signed short) vec_mergel(zero, tmp);
        sumhv = vec_madds(srchv[2].v, fv[2].v, sumhv);
        sumlv = vec_madds(srclv[2].v, fv[2].v, sumlv);

        tv = (vector unsigned char *) &s[3 * wrap];
        tmp = vec_perm(tv[0], tv[1], vec_lvsl(0, &s[3 * wrap]));
        srchv[3].v = (vector signed short) vec_mergeh(zero, tmp);
        srclv[3].v = (vector signed short) vec_mergel(zero, tmp);
        sumhv = vec_madds(srchv[3].v, fv[3].v, sumhv);
        sumlv = vec_madds(srclv[3].v, fv[3].v, sumlv);
    
        /*
           Pack the results into our destination vector,
           and do an aligned write of that back to memory.
        */
        dstv = vec_packsu(sumhv, sumlv) ;
        vec_st(dstv, 0, (vector unsigned char *) dst);
        
        dst+=16;
        s+=16;
        dst_width-=16;
    }

    /*
       If there are any leftover pixels, resample them
       with the slow scalar method.
    */
    while(dst_width>0) {
        sum = s[0 * wrap] * filter[0] +
        s[1 * wrap] * filter[1] +
        s[2 * wrap] * filter[2] +
        s[3 * wrap] * filter[3];
        sum = sum >> FILTER_BITS;
        if (sum<0) sum = 0; else if (sum>255) sum=255;
        dst[0] = sum;
        dst++;
        s++;
        dst_width--;
    }
}
#endif

/* slow version to handle limit cases. Does not need optimisation */
static void h_resample_slow(uint8_t *dst, int dst_width,
			    const uint8_t *src, int src_width,
                            int src_start, int src_incr, int16_t *filters)
{
    int src_pos, phase, sum, j, v, i;
    const uint8_t *s, *src_end;
    int16_t *filter;

    src_end = src + src_width;
    src_pos = src_start;
    for(i=0;i<dst_width;i++) {
        s = src + (src_pos >> POS_FRAC_BITS);
        phase = get_phase(src_pos);
        filter = filters + phase * NB_TAPS;
        sum = 0;
        for(j=0;j<NB_TAPS;j++) {
            if (s < src)
                v = src[0];
            else if (s >= src_end)
                v = src_end[-1];
            else
                v = s[0];
            sum += v * filter[j];
            s++;
        }
        sum = sum >> FILTER_BITS;
        if (sum < 0)
            sum = 0;
        else if (sum > 255)
            sum = 255;
        dst[0] = sum;
        src_pos += src_incr;
        dst++;
    }
}

static void h_resample(uint8_t *dst, int dst_width, const uint8_t *src,
		       int src_width, int src_start, int src_incr,
		       int16_t *filters)
{
    int n, src_end;

    if (src_start < 0) {
        n = (0 - src_start + src_incr - 1) / src_incr;
        h_resample_slow(dst, n, src, src_width, src_start, src_incr, filters);
        dst += n;
        dst_width -= n;
        src_start += n * src_incr;
    }
    src_end = src_start + dst_width * src_incr;
    if (src_end > ((src_width - NB_TAPS) << POS_FRAC_BITS)) {
        n = (((src_width - NB_TAPS + 1) << POS_FRAC_BITS) - 1 - src_start) / 
            src_incr;
    } else {
        n = dst_width;
    }
#ifdef HAVE_MMX
    if ((mm_flags & MM_MMX) && NB_TAPS == 4)
        h_resample_fast4_mmx(dst, n, 
                             src, src_width, src_start, src_incr, filters);
    else
#endif
        h_resample_fast(dst, n, 
                        src, src_width, src_start, src_incr, filters);
    if (n < dst_width) {
        dst += n;
        dst_width -= n;
        src_start += n * src_incr;
        h_resample_slow(dst, dst_width, 
                        src, src_width, src_start, src_incr, filters);
    }
}

static void component_resample(ImgReSampleContext *s, 
                               uint8_t *output, int owrap, int owidth, int oheight,
                               uint8_t *input, int iwrap, int iwidth, int iheight)
{
    int src_y, src_y1, last_src_y, ring_y, phase_y, y1, y;
    uint8_t *new_line, *src_line;

    last_src_y = - FCENTER - 1;
    /* position of the bottom of the filter in the source image */
    src_y = (last_src_y + NB_TAPS) * POS_FRAC; 
    ring_y = NB_TAPS; /* position in ring buffer */
    for(y=0;y<oheight;y++) {
        /* apply horizontal filter on new lines from input if needed */
        src_y1 = src_y >> POS_FRAC_BITS;
        while (last_src_y < src_y1) {
            if (++ring_y >= LINE_BUF_HEIGHT + NB_TAPS)
                ring_y = NB_TAPS;
            last_src_y++;
            /* handle limit conditions : replicate line (slightly
               inefficient because we filter multiple times) */
            y1 = last_src_y;
            if (y1 < 0) {
                y1 = 0;
            } else if (y1 >= iheight) {
                y1 = iheight - 1;
            }
            src_line = input + y1 * iwrap;
            new_line = s->line_buf + ring_y * owidth;
            /* apply filter and handle limit cases correctly */
            h_resample(new_line, owidth, 
                       src_line, iwidth, - FCENTER * POS_FRAC, s->h_incr, 
                       &s->h_filters[0][0]);
            /* handle ring buffer wraping */
            if (ring_y >= LINE_BUF_HEIGHT) {
                memcpy(s->line_buf + (ring_y - LINE_BUF_HEIGHT) * owidth,
                       new_line, owidth);
            }
        }
        /* apply vertical filter */
        phase_y = get_phase(src_y);
#ifdef HAVE_MMX
        /* desactivated MMX because loss of precision */
        if ((mm_flags & MM_MMX) && NB_TAPS == 4 && 0)
            v_resample4_mmx(output, owidth, 
                            s->line_buf + (ring_y - NB_TAPS + 1) * owidth, owidth, 
                            &s->v_filters[phase_y][0]);
        else
#endif
#ifdef HAVE_ALTIVEC
            if ((mm_flags & MM_ALTIVEC) && NB_TAPS == 4 && FILTER_BITS <= 6)
                v_resample16_altivec(output, owidth,
                                s->line_buf + (ring_y - NB_TAPS + 1) * owidth, owidth,
                                &s->v_filters[phase_y][0]);
        else
#endif
            v_resample(output, owidth, 
                       s->line_buf + (ring_y - NB_TAPS + 1) * owidth, owidth, 
                       &s->v_filters[phase_y][0]);
            
        src_y += s->v_incr;
        
        output += owrap;
    }
}

ImgReSampleContext *img_resample_init(int owidth, int oheight,
                                      int iwidth, int iheight)
{
    return img_resample_full_init(owidth, oheight, iwidth, iheight, 
            0, 0, 0, 0, 0, 0, 0, 0);
}

ImgReSampleContext *img_resample_full_init(int owidth, int oheight,
                                      int iwidth, int iheight,
                                      int topBand, int bottomBand,
        int leftBand, int rightBand,
        int padtop, int padbottom,
        int padleft, int padright)
{
    ImgReSampleContext *s;

    s = av_mallocz(sizeof(ImgReSampleContext));
    if (!s)
        return NULL;
    s->line_buf = av_mallocz(owidth * (LINE_BUF_HEIGHT + NB_TAPS));
    if (!s->line_buf) 
        goto fail;
    
    s->owidth = owidth;
    s->oheight = oheight;
    s->iwidth = iwidth;
    s->iheight = iheight;
  
    s->topBand = topBand;
    s->bottomBand = bottomBand;
    s->leftBand = leftBand;
    s->rightBand = rightBand;
    
    s->padtop = padtop;
    s->padbottom = padbottom;
    s->padleft = padleft;
    s->padright = padright;

    s->pad_owidth = owidth - (padleft + padright);
    s->pad_oheight = oheight - (padtop + padbottom);

    s->h_incr = ((iwidth - leftBand - rightBand) * POS_FRAC) / s->pad_owidth;
    s->v_incr = ((iheight - topBand - bottomBand) * POS_FRAC) / s->pad_oheight; 

    av_build_filter(&s->h_filters[0][0], (float) s->pad_owidth  / 
            (float) (iwidth - leftBand - rightBand), NB_TAPS, NB_PHASES, 1<<FILTER_BITS, 0);
    av_build_filter(&s->v_filters[0][0], (float) s->pad_oheight / 
            (float) (iheight - topBand - bottomBand), NB_TAPS, NB_PHASES, 1<<FILTER_BITS, 0);

    return s;
fail:
    av_free(s);
    return NULL;
}

void img_resample(ImgReSampleContext *s, 
                  AVPicture *output, const AVPicture *input)
{
    int i, shift;
    uint8_t* optr;

    for (i=0;i<3;i++) {
        shift = (i == 0) ? 0 : 1;

        optr = output->data[i] + (((output->linesize[i] * 
                        s->padtop) + s->padleft) >> shift);

        component_resample(s, optr, output->linesize[i], 
                s->pad_owidth >> shift, s->pad_oheight >> shift,
                input->data[i] + (input->linesize[i] * 
                    (s->topBand >> shift)) + (s->leftBand >> shift),
                input->linesize[i], ((s->iwidth - s->leftBand - 
                        s->rightBand) >> shift),
                           (s->iheight - s->topBand - s->bottomBand) >> shift);
    }
}

void img_resample_close(ImgReSampleContext *s)
{
    av_free(s->line_buf);
    av_free(s);
}

#ifdef TEST
#include <stdio.h>

/* input */
#define XSIZE 256
#define YSIZE 256
uint8_t img[XSIZE * YSIZE];

/* output */
#define XSIZE1 512
#define YSIZE1 512
uint8_t img1[XSIZE1 * YSIZE1];
uint8_t img2[XSIZE1 * YSIZE1];

void save_pgm(const char *filename, uint8_t *img, int xsize, int ysize)
{
    FILE *f;
    f=fopen(filename,"w");
    fprintf(f,"P5\n%d %d\n%d\n", xsize, ysize, 255);
    fwrite(img,1, xsize * ysize,f);
    fclose(f);
}

static void dump_filter(int16_t *filter)
{
    int i, ph;

    for(ph=0;ph<NB_PHASES;ph++) {
        av_log(NULL, AV_LOG_INFO, "%2d: ", ph);
        for(i=0;i<NB_TAPS;i++) {
            av_log(NULL, AV_LOG_INFO, " %5.2f", filter[ph * NB_TAPS + i] / 256.0);
        }
        av_log(NULL, AV_LOG_INFO, "\n");
    }
}

#ifdef HAVE_MMX
int mm_flags;
#endif

int main(int argc, char **argv)
{
    int x, y, v, i, xsize, ysize;
    ImgReSampleContext *s;
    float fact, factors[] = { 1/2.0, 3.0/4.0, 1.0, 4.0/3.0, 16.0/9.0, 2.0 };
    char buf[256];

    /* build test image */
    for(y=0;y<YSIZE;y++) {
        for(x=0;x<XSIZE;x++) {
            if (x < XSIZE/2 && y < YSIZE/2) {
                if (x < XSIZE/4 && y < YSIZE/4) {
                    if ((x % 10) <= 6 &&
                        (y % 10) <= 6)
                        v = 0xff;
                    else
                        v = 0x00;
                } else if (x < XSIZE/4) {
                    if (x & 1) 
                        v = 0xff;
                    else 
                        v = 0;
                } else if (y < XSIZE/4) {
                    if (y & 1) 
                        v = 0xff;
                    else 
                        v = 0;
                } else {
                    if (y < YSIZE*3/8) {
                        if ((y+x) & 1) 
                            v = 0xff;
                        else 
                            v = 0;
                    } else {
                        if (((x+3) % 4) <= 1 &&
                            ((y+3) % 4) <= 1)
                            v = 0xff;
                        else
                            v = 0x00;
                    }
                }
            } else if (x < XSIZE/2) {
                v = ((x - (XSIZE/2)) * 255) / (XSIZE/2);
            } else if (y < XSIZE/2) {
                v = ((y - (XSIZE/2)) * 255) / (XSIZE/2);
            } else {
                v = ((x + y - XSIZE) * 255) / XSIZE;
            }
            img[(YSIZE - y) * XSIZE + (XSIZE - x)] = v;
        }
    }
    save_pgm("/tmp/in.pgm", img, XSIZE, YSIZE);
    for(i=0;i<sizeof(factors)/sizeof(float);i++) {
        fact = factors[i];
        xsize = (int)(XSIZE * fact);
        ysize = (int)((YSIZE - 100) * fact);
        s = img_resample_full_init(xsize, ysize, XSIZE, YSIZE, 50 ,50, 0, 0, 0, 0, 0, 0);
        av_log(NULL, AV_LOG_INFO, "Factor=%0.2f\n", fact);
        dump_filter(&s->h_filters[0][0]);
        component_resample(s, img1, xsize, xsize, ysize,
                           img + 50 * XSIZE, XSIZE, XSIZE, YSIZE - 100);
        img_resample_close(s);

        sprintf(buf, "/tmp/out%d.pgm", i);
        save_pgm(buf, img1, xsize, ysize);
    }

    /* mmx test */
#ifdef HAVE_MMX
    av_log(NULL, AV_LOG_INFO, "MMX test\n");
    fact = 0.72;
    xsize = (int)(XSIZE * fact);
    ysize = (int)(YSIZE * fact);
    mm_flags = MM_MMX;
    s = img_resample_init(xsize, ysize, XSIZE, YSIZE);
    component_resample(s, img1, xsize, xsize, ysize,
                       img, XSIZE, XSIZE, YSIZE);

    mm_flags = 0;
    s = img_resample_init(xsize, ysize, XSIZE, YSIZE);
    component_resample(s, img2, xsize, xsize, ysize,
                       img, XSIZE, XSIZE, YSIZE);
    if (memcmp(img1, img2, xsize * ysize) != 0) {
        av_log(NULL, AV_LOG_ERROR, "mmx error\n");
        exit(1);
    }
    av_log(NULL, AV_LOG_INFO, "MMX OK\n");
#endif
    return 0;
}

#endif
