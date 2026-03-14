/*
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
#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/mem_internal.h"
#include "libavutil/x86/asm.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/mpeg4videodsp.h"
#include "videodsp.h"

#if HAVE_SSSE3_INLINE

#define SPLATW(reg) "pshuflw  $0, %%" #reg ", %%" #reg "\n\t" \
                    "punpcklqdq   %%" #reg ", %%" #reg "\n\t"

typedef struct {
    DECLARE_ALIGNED_16(uint16_t, u16)[8];
} xmm_u16;

DECLARE_ASM_CONST(16, xmm_u16, pw_0to7) = { { 0, 1, 2, 3, 4, 5, 6, 7 } };

static void gmc_ssse3(uint8_t *dst, const uint8_t *src,
                      int stride, int h, int ox, int oy,
                      int dxx, int dxy, int dyx, int dyy,
                      int shift, int r, int width, int height)
{
    enum {
        W               = 8,
        EDGE_EMU_STRIDE = 16, //< anything >= W+1 will do
        MAX_H           = 16,
    };
    const int w    = 8;
    const int ix   = ox  >> (16 + shift);
    const int iy   = oy  >> (16 + shift);
    const int ox2  = ox & (1 << (16 + shift)) - 1;
    const int oy2  = oy & (1 << (16 + shift)) - 1;
    const int oxs  = ox2 >> 4;
    const int oys  = oy2 >> 4;
    const int dxx2 = dxx - (1 << (16 + shift));
    const int dyy2 = dyy - (1 << (16 + shift));
    const int dxxs = dxx2 >> 4;
    const int dxys = dxy >> 4;
    const int dyxs = dyx >> 4;
    const int dyys = dyy2 >> 4;
    uint8_t edge_buf[(MAX_H + 1) * EDGE_EMU_STRIDE];

    const int dxw = dxx2 * (w - 1);
    const int dyh = dyy2 * (h - 1);
    const int dxh = dxy * (h - 1);
    const int dyw = dyx * (w - 1);
    int need_emu  =  (unsigned) ix >= width  - w || width < w ||
                     (unsigned) iy >= height - h || height< h
                     ;

    if ( // non-constant fullpel offset (3% of blocks)
        ((ox2 + dxw) | (ox2 + dxh) | (ox2 + dxw + dxh) |
         (oy2 + dyw) | (oy2 + dyh) | (oy2 + dyw + dyh)) >> (16 + shift) ||
        // uses more than 16 bits of subpel mv (only at huge resolution)
        (dxx | dxy | dyx | dyy) & 15) {
        ff_gmc_c(dst, src, stride, h, ox, oy, dxx, dxy, dyx, dyy,
                 shift, r, width, height);
        return;
    }

    src += ix + iy * stride;
    const ptrdiff_t dst_stride = stride;
    ptrdiff_t src_stride = stride;
    if (need_emu) {
        ff_emulated_edge_mc_sse2(edge_buf, src, EDGE_EMU_STRIDE, src_stride,
                                 w + 1, h + 1, ix, iy, width, height);
        src        = edge_buf;
        src_stride = EDGE_EMU_STRIDE;
    }

#if ARCH_X86_32
    xmm_u16 dxy8, dyy8, r8;
    DECLARE_ALIGNED_16(uint64_t, shift2) = 2 * shift;
#endif

    __asm__ volatile (
        "movd             %[dxxs], %%xmm2     \n\t"
        "movd             %[dyxs], %%xmm3     \n\t"
        "movd              %[oxs], %%xmm1     \n\t"
        SPLATW(xmm2)
        "movd              %[oys], %%xmm7     \n\t"
        SPLATW(xmm3)
        "pmullw "MANGLE(pw_0to7)", %%xmm2     \n\t"
        SPLATW(xmm1)
        "movd                %[s], %%xmm6     \n\t"
        "pmullw "MANGLE(pw_0to7)", %%xmm3     \n\t"
        "movq            (%[src]), %%xmm5     \n\t"
        SPLATW(xmm7)
#if ARCH_X86_32
        "movd             %[dxys], %%xmm0     \n\t"
#else
        "movd             %[dxys], %%xmm11    \n\t"
#endif
        "paddw             %%xmm2, %%xmm1     \n\t"
        "movq           1(%[src]), %%xmm2     \n\t"
        SPLATW(xmm6)
#if ARCH_X86_32
        "movd             %[dyys], %%xmm4     \n\t"
#else
        "movd             %[dyys], %%xmm9     \n\t"
#endif
        "paddw             %%xmm3, %%xmm7     \n\t"
        "punpcklbw         %%xmm2, %%xmm5     \n\t"
#if ARCH_X86_32
        SPLATW(xmm0)
        "movd                %[r], %%xmm2     \n\t"
        SPLATW(xmm4)
        "movdqa            %%xmm0, %[dxy8]    \n\t"
        SPLATW(xmm2)
        "movdqa            %%xmm4, %[dyy8]    \n\t"
        "movdqa            %%xmm2, %[r8]      \n\t"
#else
        SPLATW(xmm11)
        "movd                %[r], %%xmm8     \n\t"
        SPLATW(xmm9)
        SPLATW(xmm8)
        "movd           %[shift2], %%xmm12    \n\t"
#endif

        "1:                                   \n\t"
        "add        %[src_stride], %[src]     \n\t"
        "movq            (%[src]), %%xmm3     \n\t"
        "movq           1(%[src]), %%xmm0     \n\t"
        "movdqa            %%xmm1, %%xmm4     \n\t"
        "psrlw                $12, %%xmm4     \n\t" // dx
        "movdqa            %%xmm6, %%xmm2     \n\t"
        "psubw             %%xmm4, %%xmm2     \n\t" // (s-dx)
        "psllw                 $8, %%xmm4     \n\t"
        "por               %%xmm4, %%xmm2     \n\t" // s-dx,dx,s-dx,dx (bytes)
        "pmaddubsw         %%xmm2, %%xmm5     \n\t" // src[0, 0] * (s - dx) + src[1,0] * dx
        "punpcklbw         %%xmm0, %%xmm3     \n\t"
        "movdqa            %%xmm3, %%xmm0     \n\t"
        "pmaddubsw         %%xmm2, %%xmm3     \n\t" // src[0, 1] * (s - dx) + src[1,1] * dx
#if ARCH_X86_32
        "paddw            %[dxy8], %%xmm1     \n\t"
#else
        "paddw            %%xmm11, %%xmm1     \n\t"
#endif
        "movdqa            %%xmm7, %%xmm4     \n\t"
        "movdqa            %%xmm6, %%xmm2     \n\t"
        "psrlw                $12, %%xmm4     \n\t" // dy
        "psubw             %%xmm4, %%xmm2     \n\t" // (s-dy)
        "pmullw            %%xmm5, %%xmm2     \n\t" // (src[0, 0] * (s - dx) + src[1,0] * dx) * (s - dy)
#if ARCH_X86_32
        "paddw            %[dyy8], %%xmm7     \n\t"
#else
        "paddw             %%xmm9, %%xmm7     \n\t"
#endif
        "pmullw            %%xmm3, %%xmm4     \n\t" // (src[0, 1] * (s - dx) + src[1,1] * dx) * dy

#if ARCH_X86_32
        "paddw              %[r8], %%xmm2     \n\t"
#else
        "paddw             %%xmm8, %%xmm2     \n\t"
#endif
        "paddw             %%xmm2, %%xmm4     \n\t"

#if ARCH_X86_32
        "psrlw          %[shift2], %%xmm4     \n\t"
#else
        "psrlw            %%xmm12, %%xmm4     \n\t"
#endif
        "packuswb          %%xmm4, %%xmm4     \n\t"
        "movq              %%xmm4, (%[dst])   \n\t"
        "movdqa            %%xmm0, %%xmm5     \n\t"
        "add        %[dst_stride], %[dst]     \n\t"

        "decl                %[h]             \n\t"
        "jnz                   1b             \n\t"
        : [dst]"+r"(dst), [src]"+r"(src),
#if HAVE_6REGS || HAVE_INLINE_ASM_DIRECT_SYMBOL_REFS
        [h]"+r"(h)
#else
        [h]"+m"(h)
#endif
#if ARCH_X86_32
          , [dxy8]"=m" (dxy8), [dyy8]"=m" (dyy8), [r8]"=m" (r8)
#endif
        : [dst_stride]"r"(dst_stride), [src_stride]"r"(src_stride),
          [s]"g" (1 << shift),
#if ARCH_X86_32
          [shift2]"m" (shift2),
#else
          [shift2]"g" (2*shift),
#endif
          [oxs]"g"(oxs),  [oys]"g"(oys),  [dxxs]"g"(dxxs), [dyxs]"g"(dyxs),
          [dxys]"g"(dxys), [dyys]"g"(dyys), [r]"g"(r) NAMED_CONSTRAINTS_ADD(pw_0to7)
        : XMM_CLOBBERS("xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7",)
#if ARCH_X86_64
          XMM_CLOBBERS("xmm8", "xmm9", "xmm10", "xmm11", "xmm12",)
#endif
         "memory");
}

#endif /* HAVE_SSSE3_INLINE */

av_cold void ff_mpeg4videodsp_init_x86(Mpeg4VideoDSPContext *c)
{
#if HAVE_SSSE3_INLINE
    int cpu_flags = av_get_cpu_flags();

    if (INLINE_SSSE3(cpu_flags))
        c->gmc = gmc_ssse3;
#endif /* HAVE_SSSE3_INLINE */
}
