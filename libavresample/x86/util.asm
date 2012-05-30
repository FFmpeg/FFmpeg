;******************************************************************************
;* x86 utility macros for libavresample
;* Copyright (c) 2012 Justin Ruggles <justin.ruggles@gmail.com>
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

%macro S16_TO_S32_SX 2 ; src/low dst, high dst
%if cpuflag(sse4)
    pmovsxwd     m%2, m%1
    psrldq       m%1, 8
    pmovsxwd     m%1, m%1
    SWAP %1, %2
%else
    punpckhwd    m%2, m%1
    punpcklwd    m%1, m%1
    psrad        m%2, 16
    psrad        m%1, 16
%endif
%endmacro
