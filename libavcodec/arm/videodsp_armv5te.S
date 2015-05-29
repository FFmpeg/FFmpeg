@
@ ARMv5te-optimized core video DSP functions
@ Copyright (c) 2004 AGAWA Koji <i (AT) atty (DOT) jp>
@
@ This file is part of FFmpeg
@
@ FFmpeg is free software; you can redistribute it and/or
@ modify it under the terms of the GNU Lesser General Public
@ License as published by the Free Software Foundation; either
@ version 2.1 of the License, or (at your option) any later version.
@
@ FFmpeg is distributed in the hope that it will be useful,
@ but WITHOUT ANY WARRANTY; without even the implied warranty of
@ MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
@ Lesser General Public License for more details.
@
@ You should have received a copy of the GNU Lesser General Public
@ License along with FFmpeg; if not, write to the Free Software
@ Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
@

#include "config.h"
#include "libavutil/arm/asm.S"

function ff_prefetch_arm, export=1
1:
        subs            r2,  r2,  #1
        pld             [r0]
        add             r0,  r0,  r1
        bne             1b
        bx              lr
endfunc
