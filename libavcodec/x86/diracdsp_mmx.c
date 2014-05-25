/*
 * Copyright (C) 2010 David Conrad
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

#include "libavutil/x86/cpu.h"
#include "dsputil_x86.h"
#include "diracdsp_mmx.h"
#include "fpel.h"

void ff_put_rect_clamped_mmx(uint8_t *dst, int dst_stride, const int16_t *src, int src_stride, int width, int height);
void ff_put_rect_clamped_sse2(uint8_t *dst, int dst_stride, const int16_t *src, int src_stride, int width, int height);
void ff_put_signed_rect_clamped_mmx(uint8_t *dst, int dst_stride, const int16_t *src, int src_stride, int width, int height);
void ff_put_signed_rect_clamped_sse2(uint8_t *dst, int dst_stride, const int16_t *src, int src_stride, int width, int height);

#define HPEL_FILTER(MMSIZE, EXT)                                                             \
    void ff_dirac_hpel_filter_v_ ## EXT(uint8_t *, const uint8_t *, int, int);               \
    void ff_dirac_hpel_filter_h_ ## EXT(uint8_t *, const uint8_t *, int);                    \
                                                                                             \
    static void dirac_hpel_filter_ ## EXT(uint8_t *dsth, uint8_t *dstv, uint8_t *dstc,       \
                                          const uint8_t *src, int stride, int width, int height)   \
    {                                                                                        \
        while( height-- )                                                                    \
        {                                                                                    \
            ff_dirac_hpel_filter_v_ ## EXT(dstv-MMSIZE, src-MMSIZE, stride, width+MMSIZE+5); \
            ff_dirac_hpel_filter_h_ ## EXT(dsth, src, width);                                \
            ff_dirac_hpel_filter_h_ ## EXT(dstc, dstv, width);                               \
                                                                                             \
            dsth += stride;                                                                  \
            dstv += stride;                                                                  \
            dstc += stride;                                                                  \
            src  += stride;                                                                  \
        }                                                                                    \
    }

#if !ARCH_X86_64
HPEL_FILTER(8, mmx)
#endif
HPEL_FILTER(16, sse2)

#define PIXFUNC(PFX, IDX, EXT)                                                   \
    /*MMXDISABLEDc->PFX ## _dirac_pixels_tab[0][IDX] = ff_ ## PFX ## _dirac_pixels8_ ## EXT;*/  \
    c->PFX ## _dirac_pixels_tab[1][IDX] = ff_ ## PFX ## _dirac_pixels16_ ## EXT; \
    c->PFX ## _dirac_pixels_tab[2][IDX] = ff_ ## PFX ## _dirac_pixels32_ ## EXT

#define DIRAC_PIXOP(OPNAME2, OPNAME, EXT)\
void ff_ ## OPNAME2 ## _dirac_pixels8_ ## EXT(uint8_t *dst, const uint8_t *src[5], int stride, int h)\
{\
    if (h&3)\
        ff_ ## OPNAME2 ## _dirac_pixels8_c(dst, src, stride, h);\
    else\
        OPNAME ## _pixels8_ ## EXT(dst, src[0], stride, h);\
}\
void ff_ ## OPNAME2 ## _dirac_pixels16_ ## EXT(uint8_t *dst, const uint8_t *src[5], int stride, int h)\
{\
    if (h&3)\
        ff_ ## OPNAME2 ## _dirac_pixels16_c(dst, src, stride, h);\
    else\
        OPNAME ## _pixels16_ ## EXT(dst, src[0], stride, h);\
}\
void ff_ ## OPNAME2 ## _dirac_pixels32_ ## EXT(uint8_t *dst, const uint8_t *src[5], int stride, int h)\
{\
    if (h&3) {\
        ff_ ## OPNAME2 ## _dirac_pixels32_c(dst, src, stride, h);\
    } else {\
        OPNAME ## _pixels16_ ## EXT(dst   , src[0]   , stride, h);\
        OPNAME ## _pixels16_ ## EXT(dst+16, src[0]+16, stride, h);\
    }\
}

DIRAC_PIXOP(put, ff_put, mmx)
DIRAC_PIXOP(avg, ff_avg, mmx)
DIRAC_PIXOP(avg, ff_avg, mmxext)

void ff_put_dirac_pixels16_sse2(uint8_t *dst, const uint8_t *src[5], int stride, int h)
{
    if (h&3)
        ff_put_dirac_pixels16_c(dst, src, stride, h);
    else
    ff_put_pixels16_sse2(dst, src[0], stride, h);
}
void ff_avg_dirac_pixels16_sse2(uint8_t *dst, const uint8_t *src[5], int stride, int h)
{
    if (h&3)
        ff_avg_dirac_pixels16_c(dst, src, stride, h);
    else
    ff_avg_pixels16_sse2(dst, src[0], stride, h);
}
void ff_put_dirac_pixels32_sse2(uint8_t *dst, const uint8_t *src[5], int stride, int h)
{
    if (h&3) {
        ff_put_dirac_pixels32_c(dst, src, stride, h);
    } else {
    ff_put_pixels16_sse2(dst   , src[0]   , stride, h);
    ff_put_pixels16_sse2(dst+16, src[0]+16, stride, h);
    }
}
void ff_avg_dirac_pixels32_sse2(uint8_t *dst, const uint8_t *src[5], int stride, int h)
{
    if (h&3) {
        ff_avg_dirac_pixels32_c(dst, src, stride, h);
    } else {
    ff_avg_pixels16_sse2(dst   , src[0]   , stride, h);
    ff_avg_pixels16_sse2(dst+16, src[0]+16, stride, h);
    }
}

void ff_diracdsp_init_mmx(DiracDSPContext* c)
{
    int mm_flags = av_get_cpu_flags();

    if (EXTERNAL_MMX(mm_flags)) {
    c->add_dirac_obmc[0] = ff_add_dirac_obmc8_mmx;
#if !ARCH_X86_64
    c->add_dirac_obmc[1] = ff_add_dirac_obmc16_mmx;
    c->add_dirac_obmc[2] = ff_add_dirac_obmc32_mmx;
    c->dirac_hpel_filter = dirac_hpel_filter_mmx;
    c->add_rect_clamped = ff_add_rect_clamped_mmx;
    c->put_signed_rect_clamped = ff_put_signed_rect_clamped_mmx;
#endif
    PIXFUNC(put, 0, mmx);
    PIXFUNC(avg, 0, mmx);
    }

    if (EXTERNAL_MMXEXT(mm_flags)) {
        PIXFUNC(avg, 0, mmxext);
    }

    if (EXTERNAL_SSE2(mm_flags)) {
        c->dirac_hpel_filter = dirac_hpel_filter_sse2;
        c->add_rect_clamped = ff_add_rect_clamped_sse2;
        c->put_signed_rect_clamped = ff_put_signed_rect_clamped_sse2;

        c->add_dirac_obmc[1] = ff_add_dirac_obmc16_sse2;
        c->add_dirac_obmc[2] = ff_add_dirac_obmc32_sse2;

        c->put_dirac_pixels_tab[1][0] = ff_put_dirac_pixels16_sse2;
        c->avg_dirac_pixels_tab[1][0] = ff_avg_dirac_pixels16_sse2;
        c->put_dirac_pixels_tab[2][0] = ff_put_dirac_pixels32_sse2;
        c->avg_dirac_pixels_tab[2][0] = ff_avg_dirac_pixels32_sse2;
    }
}
