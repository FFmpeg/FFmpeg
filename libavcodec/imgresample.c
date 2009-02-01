/*
 * High quality image resampling with polyphase filters
 * Copyright (c) 2001 Fabrice Bellard
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

/**
 * @file libavcodec/imgresample.c
 * High quality image resampling with polyphase filters .
 */

#include "avcodec.h"
#include "dsputil.h"
#include "imgconvert.h"
#include "libswscale/swscale.h"

#if HAVE_ALTIVEC
#include "ppc/imgresample_altivec.h"
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

struct SwsContext {
    const AVClass *av_class;
    struct ImgReSampleContext *resampling_ctx;
    enum PixelFormat src_pix_fmt, dst_pix_fmt;
};

typedef struct ImgReSampleContext {
    int iwidth, iheight, owidth, oheight;
    int topBand, bottomBand, leftBand, rightBand;
    int padtop, padbottom, padleft, padright;
    int pad_owidth, pad_oheight;
    int h_incr, v_incr;
    DECLARE_ALIGNED_8(int16_t, h_filters[NB_PHASES][NB_TAPS]); /* horizontal filters */
    DECLARE_ALIGNED_8(int16_t, v_filters[NB_PHASES][NB_TAPS]); /* vertical filters */
    uint8_t *line_buf;
} ImgReSampleContext;

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

#if HAVE_MMX

#include "x86/mmx.h"

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

#define DUMP(reg) movq_r2m(reg, tmp); printf(#reg "=%016"PRIx64"\n", tmp.uq);

/* XXX: do four pixels at a time */
static void h_resample_fast4_mmx(uint8_t *dst, int dst_width,
                                 const uint8_t *src, int src_width,
                                 int src_start, int src_incr, int16_t *filters)
{
    int src_pos, phase;
    const uint8_t *s;
    int16_t *filter;
    uint64_t tmp;

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
        dst[0] = tmp & 0xFF;
        movq_r2m(mm1, tmp);
        dst[1] = tmp & 0xFF;
        movq_r2m(mm2, tmp);
        dst[2] = tmp & 0xFF;
        movq_r2m(mm3, tmp);
        dst[3] = tmp & 0xFF;
        dst += 4;
        dst_width -= 4;
    }
    while (dst_width > 0) {
        FILTER4(mm0);
        packuswb_r2r(mm7, mm0);
        movq_r2m(mm0, tmp);
        dst[0] = tmp & 0xFF;
        dst++;
        dst_width--;
    }
    emms();
}

static void v_resample4_mmx(uint8_t *dst, int dst_width, const uint8_t *src,
                            int wrap, int16_t *filter)
{
    int sum, i;
    const uint8_t *s;
    uint64_t tmp;
    uint64_t coefs[4];

    for(i=0;i<4;i++) {
        tmp = filter[i];
        coefs[i] = (tmp<<48) + (tmp<<32) + (tmp<<16) + tmp;
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

        *(uint32_t *)dst = tmp & 0xFFFFFFFF;
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
#endif /* HAVE_MMX */

/* slow version to handle limit cases. Does not need optimization */
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
#if HAVE_MMX
    if ((mm_flags & FF_MM_MMX) && NB_TAPS == 4)
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
            /* handle ring buffer wrapping */
            if (ring_y >= LINE_BUF_HEIGHT) {
                memcpy(s->line_buf + (ring_y - LINE_BUF_HEIGHT) * owidth,
                       new_line, owidth);
            }
        }
        /* apply vertical filter */
        phase_y = get_phase(src_y);
#if HAVE_MMX
        /* desactivated MMX because loss of precision */
        if ((mm_flags & FF_MM_MMX) && NB_TAPS == 4 && 0)
            v_resample4_mmx(output, owidth,
                            s->line_buf + (ring_y - NB_TAPS + 1) * owidth, owidth,
                            &s->v_filters[phase_y][0]);
        else
#endif
#if HAVE_ALTIVEC
        if ((mm_flags & FF_MM_ALTIVEC) && NB_TAPS == 4 && FILTER_BITS <= 6)
            v_resample16_altivec(output, owidth,
                                 s->line_buf + (ring_y - NB_TAPS + 1) * owidth,
                                 owidth, &s->v_filters[phase_y][0]);
        else
#endif
            v_resample(output, owidth,
                       s->line_buf + (ring_y - NB_TAPS + 1) * owidth, owidth,
                       &s->v_filters[phase_y][0]);

        src_y += s->v_incr;

        output += owrap;
    }
}

ImgReSampleContext *img_resample_full_init(int owidth, int oheight,
                                      int iwidth, int iheight,
                                      int topBand, int bottomBand,
        int leftBand, int rightBand,
        int padtop, int padbottom,
        int padleft, int padright)
{
    ImgReSampleContext *s;

    if (!owidth || !oheight || !iwidth || !iheight)
        return NULL;

    s = av_mallocz(sizeof(ImgReSampleContext));
    if (!s)
        return NULL;
    if((unsigned)owidth >= UINT_MAX / (LINE_BUF_HEIGHT + NB_TAPS))
        goto fail;
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

ImgReSampleContext *img_resample_init(int owidth, int oheight,
                                      int iwidth, int iheight)
{
    return img_resample_full_init(owidth, oheight, iwidth, iheight,
            0, 0, 0, 0, 0, 0, 0, 0);
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

static const char *context_to_name(void* ptr)
{
    return "imgconvert";
}

static const AVClass context_class = { "imgresample", context_to_name, NULL };

struct SwsContext *sws_getContext(int srcW, int srcH, int srcFormat,
                                  int dstW, int dstH, int dstFormat,
                                  int flags, SwsFilter *srcFilter,
                                  SwsFilter *dstFilter, double *param)
{
    struct SwsContext *ctx;

    ctx = av_malloc(sizeof(struct SwsContext));
    if (!ctx) {
        av_log(NULL, AV_LOG_ERROR, "Cannot allocate a resampling context!\n");

        return NULL;
    }
    ctx->av_class = &context_class;

    if ((srcH != dstH) || (srcW != dstW)) {
        if ((srcFormat != PIX_FMT_YUV420P) || (dstFormat != PIX_FMT_YUV420P)) {
            av_log(ctx, AV_LOG_INFO, "PIX_FMT_YUV420P will be used as an intermediate format for rescaling\n");
        }
        ctx->resampling_ctx = img_resample_init(dstW, dstH, srcW, srcH);
    } else {
        ctx->resampling_ctx = av_malloc(sizeof(ImgReSampleContext));
        ctx->resampling_ctx->iheight = srcH;
        ctx->resampling_ctx->iwidth = srcW;
        ctx->resampling_ctx->oheight = dstH;
        ctx->resampling_ctx->owidth = dstW;
    }
    ctx->src_pix_fmt = srcFormat;
    ctx->dst_pix_fmt = dstFormat;

    return ctx;
}

void sws_freeContext(struct SwsContext *ctx)
{
    if (!ctx)
        return;
    if ((ctx->resampling_ctx->iwidth != ctx->resampling_ctx->owidth) ||
        (ctx->resampling_ctx->iheight != ctx->resampling_ctx->oheight)) {
        img_resample_close(ctx->resampling_ctx);
    } else {
        av_free(ctx->resampling_ctx);
    }
    av_free(ctx);
}


/**
 * Checks if context is valid or reallocs a new one instead.
 * If context is NULL, just calls sws_getContext() to get a new one.
 * Otherwise, checks if the parameters are the same already saved in context.
 * If that is the case, returns the current context.
 * Otherwise, frees context and gets a new one.
 *
 * Be warned that srcFilter, dstFilter are not checked, they are
 * asumed to remain valid.
 */
struct SwsContext *sws_getCachedContext(struct SwsContext *ctx,
                        int srcW, int srcH, int srcFormat,
                        int dstW, int dstH, int dstFormat, int flags,
                        SwsFilter *srcFilter, SwsFilter *dstFilter, double *param)
{
    if (ctx != NULL) {
        if ((ctx->resampling_ctx->iwidth != srcW) ||
                        (ctx->resampling_ctx->iheight != srcH) ||
                        (ctx->src_pix_fmt != srcFormat) ||
                        (ctx->resampling_ctx->owidth != dstW) ||
                        (ctx->resampling_ctx->oheight != dstH) ||
                        (ctx->dst_pix_fmt != dstFormat))
        {
            sws_freeContext(ctx);
            ctx = NULL;
        }
    }
    if (ctx == NULL) {
        return sws_getContext(srcW, srcH, srcFormat,
                        dstW, dstH, dstFormat, flags,
                        srcFilter, dstFilter, param);
    }
    return ctx;
}

int sws_scale(struct SwsContext *ctx, uint8_t* src[], int srcStride[],
              int srcSliceY, int srcSliceH, uint8_t* dst[], int dstStride[])
{
    AVPicture src_pict, dst_pict;
    int i, res = 0;
    AVPicture picture_format_temp;
    AVPicture picture_resample_temp, *formatted_picture, *resampled_picture;
    uint8_t *buf1 = NULL, *buf2 = NULL;
    enum PixelFormat current_pix_fmt;

    for (i = 0; i < 4; i++) {
        src_pict.data[i] = src[i];
        src_pict.linesize[i] = srcStride[i];
        dst_pict.data[i] = dst[i];
        dst_pict.linesize[i] = dstStride[i];
    }
    if ((ctx->resampling_ctx->iwidth != ctx->resampling_ctx->owidth) ||
        (ctx->resampling_ctx->iheight != ctx->resampling_ctx->oheight)) {
        /* We have to rescale the picture, but only YUV420P rescaling is supported... */

        if (ctx->src_pix_fmt != PIX_FMT_YUV420P) {
            int size;

            /* create temporary picture for rescaling input*/
            size = avpicture_get_size(PIX_FMT_YUV420P, ctx->resampling_ctx->iwidth, ctx->resampling_ctx->iheight);
            buf1 = av_malloc(size);
            if (!buf1) {
                res = -1;
                goto the_end;
            }
            formatted_picture = &picture_format_temp;
            avpicture_fill((AVPicture*)formatted_picture, buf1,
                           PIX_FMT_YUV420P, ctx->resampling_ctx->iwidth, ctx->resampling_ctx->iheight);

            if (img_convert((AVPicture*)formatted_picture, PIX_FMT_YUV420P,
                            &src_pict, ctx->src_pix_fmt,
                            ctx->resampling_ctx->iwidth, ctx->resampling_ctx->iheight) < 0) {

                av_log(ctx, AV_LOG_ERROR, "pixel format conversion not handled\n");
                res = -1;
                goto the_end;
            }
        } else {
            formatted_picture = &src_pict;
        }

        if (ctx->dst_pix_fmt != PIX_FMT_YUV420P) {
            int size;

            /* create temporary picture for rescaling output*/
            size = avpicture_get_size(PIX_FMT_YUV420P, ctx->resampling_ctx->owidth, ctx->resampling_ctx->oheight);
            buf2 = av_malloc(size);
            if (!buf2) {
                res = -1;
                goto the_end;
            }
            resampled_picture = &picture_resample_temp;
            avpicture_fill((AVPicture*)resampled_picture, buf2,
                           PIX_FMT_YUV420P, ctx->resampling_ctx->owidth, ctx->resampling_ctx->oheight);

        } else {
            resampled_picture = &dst_pict;
        }

        /* ...and finally rescale!!! */
        img_resample(ctx->resampling_ctx, resampled_picture, formatted_picture);
        current_pix_fmt = PIX_FMT_YUV420P;
    } else {
        resampled_picture = &src_pict;
        current_pix_fmt = ctx->src_pix_fmt;
    }

    if (current_pix_fmt != ctx->dst_pix_fmt) {
        if (img_convert(&dst_pict, ctx->dst_pix_fmt,
                        resampled_picture, current_pix_fmt,
                        ctx->resampling_ctx->owidth, ctx->resampling_ctx->oheight) < 0) {

            av_log(ctx, AV_LOG_ERROR, "pixel format conversion not handled\n");

            res = -1;
            goto the_end;
        }
    } else if (resampled_picture != &dst_pict) {
        av_picture_copy(&dst_pict, resampled_picture, current_pix_fmt,
                        ctx->resampling_ctx->owidth, ctx->resampling_ctx->oheight);
    }

the_end:
    av_free(buf1);
    av_free(buf2);
    return res;
}


#ifdef TEST
#include <stdio.h>
#undef exit

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
#undef fprintf
    FILE *f;
    f=fopen(filename,"w");
    fprintf(f,"P5\n%d %d\n%d\n", xsize, ysize, 255);
    fwrite(img,1, xsize * ysize,f);
    fclose(f);
#define fprintf please_use_av_log
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

#if HAVE_MMX
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
    for(i=0;i<FF_ARRAY_ELEMS(factors);i++) {
        fact = factors[i];
        xsize = (int)(XSIZE * fact);
        ysize = (int)((YSIZE - 100) * fact);
        s = img_resample_full_init(xsize, ysize, XSIZE, YSIZE, 50 ,50, 0, 0, 0, 0, 0, 0);
        av_log(NULL, AV_LOG_INFO, "Factor=%0.2f\n", fact);
        dump_filter(&s->h_filters[0][0]);
        component_resample(s, img1, xsize, xsize, ysize,
                           img + 50 * XSIZE, XSIZE, XSIZE, YSIZE - 100);
        img_resample_close(s);

        snprintf(buf, sizeof(buf), "/tmp/out%d.pgm", i);
        save_pgm(buf, img1, xsize, ysize);
    }

    /* mmx test */
#if HAVE_MMX
    av_log(NULL, AV_LOG_INFO, "MMX test\n");
    fact = 0.72;
    xsize = (int)(XSIZE * fact);
    ysize = (int)(YSIZE * fact);
    mm_flags = FF_MM_MMX;
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
#endif /* HAVE_MMX */
    return 0;
}

#endif /* TEST */
