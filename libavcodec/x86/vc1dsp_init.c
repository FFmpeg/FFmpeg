/*
 * VC-1 and WMV3 - DSP functions MMX-optimized
 * Copyright (c) 2007 Christophe GISQUET <christophe.gisquet@free.fr>
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/x86/cpu.h"
#include "libavutil/x86/asm.h"
#include "libavcodec/vc1dsp.h"
#include "fpel.h"
#include "vc1dsp.h"
#include "config.h"

#define LOOP_FILTER4(EXT) \
void ff_vc1_v_loop_filter4_ ## EXT(uint8_t *src, ptrdiff_t stride, int pq); \
void ff_vc1_h_loop_filter4_ ## EXT(uint8_t *src, ptrdiff_t stride, int pq);
#define LOOP_FILTER816(EXT) \
void ff_vc1_v_loop_filter8_ ## EXT(uint8_t *src, ptrdiff_t stride, int pq); \
void ff_vc1_h_loop_filter8_ ## EXT(uint8_t *src, ptrdiff_t stride, int pq); \
\
static void vc1_v_loop_filter16_ ## EXT(uint8_t *src, ptrdiff_t stride, int pq) \
{ \
    ff_vc1_v_loop_filter8_ ## EXT(src,   stride, pq); \
    ff_vc1_v_loop_filter8_ ## EXT(src+8, stride, pq); \
} \
\
static void vc1_h_loop_filter16_ ## EXT(uint8_t *src, ptrdiff_t stride, int pq) \
{ \
    ff_vc1_h_loop_filter8_ ## EXT(src,          stride, pq); \
    ff_vc1_h_loop_filter8_ ## EXT(src+8*stride, stride, pq); \
}

#if HAVE_X86ASM
LOOP_FILTER4(mmxext)
LOOP_FILTER816(sse2)
LOOP_FILTER4(ssse3)
LOOP_FILTER816(ssse3)

void ff_vc1_h_loop_filter8_sse4(uint8_t *src, ptrdiff_t stride, int pq);

static void vc1_h_loop_filter16_sse4(uint8_t *src, ptrdiff_t stride, int pq)
{
    ff_vc1_h_loop_filter8_sse4(src,          stride, pq);
    ff_vc1_h_loop_filter8_sse4(src+8*stride, stride, pq);
}

#define DECLARE_FUNCTION(OP, DEPTH, INSN)                       \
    static void OP##vc1_mspel_mc00_##DEPTH##INSN(uint8_t *dst,          \
                             const uint8_t *src, ptrdiff_t stride, int rnd) \
    {                                                                       \
        ff_ ## OP ## pixels ## DEPTH ## INSN(dst, src, stride, DEPTH);     \
    }

DECLARE_FUNCTION(put_,  8, _mmx)
DECLARE_FUNCTION(avg_,  8, _mmxext)
DECLARE_FUNCTION(put_, 16, _sse2)
DECLARE_FUNCTION(avg_, 16, _sse2)

#endif /* HAVE_X86ASM */

void ff_put_vc1_chroma_mc8_nornd_mmx  (uint8_t *dst, uint8_t *src,
                                       ptrdiff_t stride, int h, int x, int y);
void ff_avg_vc1_chroma_mc8_nornd_mmxext(uint8_t *dst, uint8_t *src,
                                        ptrdiff_t stride, int h, int x, int y);
void ff_put_vc1_chroma_mc8_nornd_ssse3(uint8_t *dst, uint8_t *src,
                                       ptrdiff_t stride, int h, int x, int y);
void ff_avg_vc1_chroma_mc8_nornd_ssse3(uint8_t *dst, uint8_t *src,
                                       ptrdiff_t stride, int h, int x, int y);
void ff_vc1_inv_trans_4x4_dc_mmxext(uint8_t *dest, ptrdiff_t linesize,
                                    int16_t *block);
void ff_vc1_inv_trans_4x8_dc_mmxext(uint8_t *dest, ptrdiff_t linesize,
                                    int16_t *block);
void ff_vc1_inv_trans_8x4_dc_mmxext(uint8_t *dest, ptrdiff_t linesize,
                                    int16_t *block);
void ff_vc1_inv_trans_8x8_dc_mmxext(uint8_t *dest, ptrdiff_t linesize,
                                    int16_t *block);


av_cold void ff_vc1dsp_init_x86(VC1DSPContext *dsp)
{
    int cpu_flags = av_get_cpu_flags();

    if (HAVE_6REGS && INLINE_MMX(cpu_flags))
        if (EXTERNAL_MMX(cpu_flags))
        ff_vc1dsp_init_mmx(dsp);

    if (HAVE_6REGS && INLINE_MMXEXT(cpu_flags))
        if (EXTERNAL_MMXEXT(cpu_flags))
        ff_vc1dsp_init_mmxext(dsp);

#define ASSIGN_LF4(EXT) \
        dsp->vc1_v_loop_filter4  = ff_vc1_v_loop_filter4_ ## EXT; \
        dsp->vc1_h_loop_filter4  = ff_vc1_h_loop_filter4_ ## EXT
#define ASSIGN_LF816(EXT) \
        dsp->vc1_v_loop_filter8  = ff_vc1_v_loop_filter8_ ## EXT; \
        dsp->vc1_h_loop_filter8  = ff_vc1_h_loop_filter8_ ## EXT; \
        dsp->vc1_v_loop_filter16 = vc1_v_loop_filter16_ ## EXT; \
        dsp->vc1_h_loop_filter16 = vc1_h_loop_filter16_ ## EXT

#if HAVE_X86ASM
    if (EXTERNAL_MMX(cpu_flags)) {
        dsp->put_no_rnd_vc1_chroma_pixels_tab[0] = ff_put_vc1_chroma_mc8_nornd_mmx;

        dsp->put_vc1_mspel_pixels_tab[1][0]      = put_vc1_mspel_mc00_8_mmx;
    }
    if (EXTERNAL_MMXEXT(cpu_flags)) {
        ASSIGN_LF4(mmxext);
        dsp->avg_no_rnd_vc1_chroma_pixels_tab[0] = ff_avg_vc1_chroma_mc8_nornd_mmxext;

        dsp->avg_vc1_mspel_pixels_tab[1][0]      = avg_vc1_mspel_mc00_8_mmxext;

        dsp->vc1_inv_trans_8x8_dc                = ff_vc1_inv_trans_8x8_dc_mmxext;
        dsp->vc1_inv_trans_4x8_dc                = ff_vc1_inv_trans_4x8_dc_mmxext;
        dsp->vc1_inv_trans_8x4_dc                = ff_vc1_inv_trans_8x4_dc_mmxext;
        dsp->vc1_inv_trans_4x4_dc                = ff_vc1_inv_trans_4x4_dc_mmxext;
    }
    if (EXTERNAL_SSE2(cpu_flags)) {
        ASSIGN_LF816(sse2);

        dsp->put_vc1_mspel_pixels_tab[0][0]      = put_vc1_mspel_mc00_16_sse2;
        dsp->avg_vc1_mspel_pixels_tab[0][0]      = avg_vc1_mspel_mc00_16_sse2;
    }
    if (EXTERNAL_SSSE3(cpu_flags)) {
        ASSIGN_LF4(ssse3);
        ASSIGN_LF816(ssse3);
        dsp->put_no_rnd_vc1_chroma_pixels_tab[0] = ff_put_vc1_chroma_mc8_nornd_ssse3;
        dsp->avg_no_rnd_vc1_chroma_pixels_tab[0] = ff_avg_vc1_chroma_mc8_nornd_ssse3;
    }
    if (EXTERNAL_SSE4(cpu_flags)) {
        dsp->vc1_h_loop_filter8  = ff_vc1_h_loop_filter8_sse4;
        dsp->vc1_h_loop_filter16 = vc1_h_loop_filter16_sse4;
    }
#endif /* HAVE_X86ASM */
}
