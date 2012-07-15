;******************************************************************************
;* MMX optimized deinterlacing functions
;* Copyright (c) 2010 Vitor Sessak
;* Copyright (c) 2002 Michael Niedermayer
;*
;* This file is part of Libav.
;*
;* Libav is free software; you can redistribute it and/or
;* modify it under the terms of the GNU Lesser General Public
;* License as published by the Free Software Foundation; either
;* version 2.1 of the License, or (at your option) any later version.
;*
;* Libav is distributed in the hope that it will be useful,
;* but WITHOUT ANY WARRANTY; without even the implied warranty of
;* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
;* Lesser General Public License for more details.
;*
;* You should have received a copy of the GNU Lesser General Public
;* License along with Libav; if not, write to the Free Software
;* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
;******************************************************************************

%include "libavutil/x86/x86util.asm"

SECTION_RODATA

cextern pw_4

SECTION .text

%macro DEINTERLACE 1
%ifidn %1, inplace
;void ff_deinterlace_line_inplace_mmx(const uint8_t *lum_m4, const uint8_t *lum_m3, const uint8_t *lum_m2, const uint8_t *lum_m1, const uint8_t *lum,  int size)
cglobal deinterlace_line_inplace_mmx, 6,6,7,      lum_m4, lum_m3, lum_m2, lum_m1, lum, size
%else
;void ff_deinterlace_line_mmx(uint8_t *dst, const uint8_t *lum_m4, const uint8_t *lum_m3, const uint8_t *lum_m2, const uint8_t *lum_m1, const uint8_t *lum,  int size)
cglobal deinterlace_line_mmx,         7,7,7, dst, lum_m4, lum_m3, lum_m2, lum_m1, lum, size
%endif
    pxor  mm7, mm7
    movq  mm6, [pw_4]
.nextrow:
    movd  mm0, [lum_m4q]
    movd  mm1, [lum_m3q]
    movd  mm2, [lum_m2q]
%ifidn %1, inplace
    movd [lum_m4q], mm2
%endif
    movd  mm3, [lum_m1q]
    movd  mm4, [lumq]
    punpcklbw mm0, mm7
    punpcklbw mm1, mm7
    punpcklbw mm2, mm7
    punpcklbw mm3, mm7
    punpcklbw mm4, mm7
    paddw     mm1, mm3
    psllw     mm2, 1
    paddw     mm0, mm4
    psllw     mm1, 2
    paddw     mm2, mm6
    paddw     mm1, mm2
    psubusw   mm1, mm0
    psrlw     mm1, 3
    packuswb  mm1, mm7
%ifidn %1, inplace
    movd [lum_m2q], mm1
%else
    movd   [dstq], mm1
    add       dstq, 4
%endif
    add    lum_m4q, 4
    add    lum_m3q, 4
    add    lum_m2q, 4
    add    lum_m1q, 4
    add       lumq, 4
    sub      sized, 4
    jg .nextrow
    REP_RET
%endmacro

DEINTERLACE ""

DEINTERLACE inplace
