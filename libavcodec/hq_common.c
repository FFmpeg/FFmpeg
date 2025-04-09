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

#include "hq_common.h"

#define REPEAT(x) x x
#define ELEM(_sym, _len) {.sym = _sym << 4 | _sym, .len = _len },
#define LEN5(sym) ELEM(sym, 5)
#define LEN4(sym) REPEAT(ELEM(sym, 4))
#define LEN2(sym) REPEAT(REPEAT(REPEAT(ELEM(sym, 2))))

const VLCElem ff_hq_cbp_vlc[1 << HQ_CBP_VLC_BITS] = {
    LEN2(0xF)
    LEN4(0x0)
    LEN4(0xE)
    LEN4(0xD)
    LEN4(0xB)
    LEN4(0x7)
    LEN4(0x3)
    LEN4(0xC)
    LEN4(0x5)
    LEN4(0xA)
    LEN5(0x9)
    LEN5(0x6)
    LEN5(0x1)
    LEN5(0x2)
    LEN5(0x4)
    LEN5(0x8)
};
