/*
 * High quality image resampling with polyphase filters 
 * Copyright (c) 2001 Gerard Lantau.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "dsputil.h"
#include "avcodec.h"

#ifdef USE_FASTMEMCPY
#include "fastmemcpy.h"
#endif


#define NB_COMPONENTS 3

#define PHASE_BITS 4
#define NB_PHASES  (1 << PHASE_BITS)
#define NB_TAPS    4
#define FCENTER    1  /* index of the center of the filter */

#define POS_FRAC_BITS 16
#define POS_FRAC      (1 << POS_FRAC_BITS)
/* 6 bits precision is needed for MMX */
#define FILTER_BITS   8

#define LINE_BUF_HEIGHT (NB_TAPS * 4)

struct ImgReSampleContext {
    int iwidth, iheight, owidth, oheight;
    int h_incr, v_incr;
    INT16 h_filters[NB_PHASES][NB_TAPS] __align8; /* horizontal filters */
    INT16 v_filters[NB_PHASES][NB_TAPS] __align8; /* vertical filters */
    UINT8 *line_buf;
};

static inline int get_phase(int pos)
{
    return ((pos) >> (POS_FRAC_BITS - PHASE_BITS)) & ((1 << PHASE_BITS) - 1);
}

/* This function must be optimized */
static void h_resample_fast(UINT8 *dst, int dst_width, UINT8 *src, int src_width,
                            int src_start, int src_incr, INT16 *filters)
{
    int src_pos, phase, sum, i;
    UINT8 *s;
    INT16 *filter;

    src_pos = src_start;
    for(i=0;i<dst_width;i++) {
#ifdef TEST
        /* test */
        if ((src_pos >> POS_FRAC_BITS) < 0 ||
            (src_pos >> POS_FRAC_BITS) > (src_width - NB_TAPS))
            abort();
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
static void v_resample(UINT8 *dst, int dst_width, UINT8 *src, int wrap, 
                       INT16 *filter)
{
    int sum, i;
    UINT8 *s;

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
            UINT8 *s1 = s;

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
static void h_resample_fast4_mmx(UINT8 *dst, int dst_width, UINT8 *src, int src_width,
                                 int src_start, int src_incr, INT16 *filters)
{
    int src_pos, phase;
    UINT8 *s;
    INT16 *filter;
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

static void v_resample4_mmx(UINT8 *dst, int dst_width, UINT8 *src, int wrap, 
                            INT16 *filter)
{
    int sum, i, v;
    UINT8 *s;
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

        *(UINT32 *)dst = tmp.ud[0];
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

/* slow version to handle limit cases. Does not need optimisation */
static void h_resample_slow(UINT8 *dst, int dst_width, UINT8 *src, int src_width,
                            int src_start, int src_incr, INT16 *filters)
{
    int src_pos, phase, sum, j, v, i;
    UINT8 *s, *src_end;
    INT16 *filter;

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

static void h_resample(UINT8 *dst, int dst_width, UINT8 *src, int src_width,
                       int src_start, int src_incr, INT16 *filters)
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
                               UINT8 *output, int owrap, int owidth, int oheight,
                               UINT8 *input, int iwrap, int iwidth, int iheight)
{
    int src_y, src_y1, last_src_y, ring_y, phase_y, y1, y;
    UINT8 *new_line, *src_line;

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
            /* handle limit conditions : replicate line (slighly
               inefficient because we filter multiple times */
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
            v_resample(output, owidth, 
                       s->line_buf + (ring_y - NB_TAPS + 1) * owidth, owidth, 
                       &s->v_filters[phase_y][0]);
            
        src_y += s->v_incr;
        output += owrap;
    }
}

/* XXX: the following filter is quite naive, but it seems to suffice
   for 4 taps */
static void build_filter(INT16 *filter, float factor)
{
    int ph, i, v;
    float x, y, tab[NB_TAPS], norm, mult;

    /* if upsampling, only need to interpolate, no filter */
    if (factor > 1.0)
        factor = 1.0;

    for(ph=0;ph<NB_PHASES;ph++) {
        norm = 0;
        for(i=0;i<NB_TAPS;i++) {
            
            x = M_PI * ((float)(i - FCENTER) - (float)ph / NB_PHASES) * factor;
            if (x == 0)
                y = 1.0;
            else
                y = sin(x) / x;
            tab[i] = y;
            norm += y;
        }

        /* normalize so that an uniform color remains the same */
        mult = (float)(1 << FILTER_BITS) / norm;
        for(i=0;i<NB_TAPS;i++) {
            v = (int)(tab[i] * mult);
            filter[ph * NB_TAPS + i] = v;
        }
    }
}

ImgReSampleContext *img_resample_init(int owidth, int oheight,
                                      int iwidth, int iheight)
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
    
    s->h_incr = (iwidth * POS_FRAC) / owidth;
    s->v_incr = (iheight * POS_FRAC) / oheight;
    
    build_filter(&s->h_filters[0][0], (float)owidth / (float)iwidth);
    build_filter(&s->v_filters[0][0], (float)oheight / (float)iheight);

    return s;
 fail:
    free(s);
    return NULL;
}

void img_resample(ImgReSampleContext *s, 
                  AVPicture *output, AVPicture *input)
{
    int i, shift;

    for(i=0;i<3;i++) {
        shift = (i == 0) ? 0 : 1;
        component_resample(s, output->data[i], output->linesize[i], 
                           s->owidth >> shift, s->oheight >> shift,
                           input->data[i], input->linesize[i], 
                           s->iwidth >> shift, s->iheight >> shift);
    }
}

void img_resample_close(ImgReSampleContext *s)
{
    free(s->line_buf);
    free(s);
}

#ifdef TEST

void *av_mallocz(int size)
{
    void *ptr;
    ptr = malloc(size);
    memset(ptr, 0, size);
    return ptr;
}

/* input */
#define XSIZE 256
#define YSIZE 256
UINT8 img[XSIZE * YSIZE];

/* output */
#define XSIZE1 512
#define YSIZE1 512
UINT8 img1[XSIZE1 * YSIZE1];
UINT8 img2[XSIZE1 * YSIZE1];

void save_pgm(const char *filename, UINT8 *img, int xsize, int ysize)
{
    FILE *f;
    f=fopen(filename,"w");
    fprintf(f,"P5\n%d %d\n%d\n", xsize, ysize, 255);
    fwrite(img,1, xsize * ysize,f);
    fclose(f);
}

static void dump_filter(INT16 *filter)
{
    int i, ph;

    for(ph=0;ph<NB_PHASES;ph++) {
        printf("%2d: ", ph);
        for(i=0;i<NB_TAPS;i++) {
            printf(" %5.2f", filter[ph * NB_TAPS + i] / 256.0);
        }
        printf("\n");
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
            img[y * XSIZE + x] = v;
        }
    }
    save_pgm("/tmp/in.pgm", img, XSIZE, YSIZE);
    for(i=0;i<sizeof(factors)/sizeof(float);i++) {
        fact = factors[i];
        xsize = (int)(XSIZE * fact);
        ysize = (int)(YSIZE * fact);
        s = img_resample_init(xsize, ysize, XSIZE, YSIZE);
        printf("Factor=%0.2f\n", fact);
        dump_filter(&s->h_filters[0][0]);
        component_resample(s, img1, xsize, xsize, ysize,
                           img, XSIZE, XSIZE, YSIZE);
        img_resample_close(s);

        sprintf(buf, "/tmp/out%d.pgm", i);
        save_pgm(buf, img1, xsize, ysize);
    }

    /* mmx test */
#ifdef HAVE_MMX
    printf("MMX test\n");
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
        fprintf(stderr, "mmx error\n");
        exit(1);
    }
    printf("MMX OK\n");
#endif
    return 0;
}

#endif
