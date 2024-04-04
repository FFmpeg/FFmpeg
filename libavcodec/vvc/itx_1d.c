/*
 * VVC 1D transform
 *
 * Copyright (C) 2023 Nuo Mi
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

/* The copyright in this software is being made available under the BSD
 * License, included below. This software may be subject to other third party
 * and contributor rights, including patent rights, and no such rights are
 * granted under this license.
 *
 * Copyright (c) 2010-2021, ITU/ISO/IEC
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *  * Neither the name of the ITU/ISO/IEC nor the names of its contributors may
 *    be used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

/* optimizaed with partial butterfly, see Hung C-Y, Landman P (1997)
   Compact inverse discrete cosine transform circuit for MPEG video decoding.
 */

#include "data.h"
#include "itx_1d.h"
#include "libavutil/avutil.h"

#define G2(m)  ((nz >  2) ? (m) : 0)
#define G4(m)  ((nz >  4) ? (m) : 0)
#define G8(m)  ((nz >  8) ? (m) : 0)
#define G16(m) ((nz > 16) ? (m) : 0)

/*
transmatrix[2][2] = {
    { a,  a },
    { a, -a },
}
 */
void ff_vvc_inv_dct2_2(int *coeffs, const ptrdiff_t stride, const size_t nz)
{
    const int a = 64;
    const int x0 = coeffs[0 * stride], x1 = coeffs[1 * stride];

    coeffs[0 * stride] = a * (x0 + x1);
    coeffs[1 * stride] = a * (x0 - x1);
}

/*
transmatrix[4][4] = {
    { a,  a,  a,  a},
    { b,  c, -c, -b},
    { a, -a, -a,  a},
    { c, -b,  b, -c},
}
 */
void ff_vvc_inv_dct2_4(int *coeffs, const ptrdiff_t stride, const size_t nz)
{
    const int a = 64, b = 83, c = 36;
    const int x0 = coeffs[0 * stride], x1 = coeffs[1 * stride];
    const int x2 = coeffs[2 * stride], x3 = coeffs[3 * stride];
    const int E[2] = {
        a * (x0 + G2(+x2)),
        a * (x0 + G2(-x2)),
    };
    const int O[2] = {
        b * x1 + G2(+c * x3),
        c * x1 + G2(-b * x3),
    };

    coeffs[0 * stride] = E[0] + O[0];
    coeffs[1 * stride] = E[1] + O[1];
    coeffs[2 * stride] = E[1] - O[1];
    coeffs[3 * stride] = E[0] - O[0];
}

/*
transmatrix[8][8] = {
    { a,  a,  a,  a,  a,  a,  a,  a},
    { d,  e,  f,  g, -g, -f, -e, -d},
    { b,  c, -c, -b, -b, -c,  c,  b},
    { e, -g, -d, -f,  f,  d,  g, -e},
    { a, -a, -a,  a,  a, -a, -a,  a},
    { f, -d,  g,  e, -e, -g,  d, -f},
    { c, -b,  b, -c, -c,  b, -b,  c},
    { g, -f,  e, -d,  d, -e,  f, -g},
}
 */
void ff_vvc_inv_dct2_8(int *coeffs, const ptrdiff_t stride, const size_t nz)
{
    const int a = 64, b = 83, c = 36, d = 89, e = 75, f = 50, g = 18;
    const int x0 = coeffs[0 * stride], x1 = coeffs[1 * stride];
    const int x2 = coeffs[2 * stride], x3 = coeffs[3 * stride];
    const int x4 = coeffs[4 * stride], x5 = coeffs[5 * stride];
    const int x6 = coeffs[6 * stride], x7 = coeffs[7 * stride];
    const int EE[2] = {
        a * (x0 + G4(+x4)),
        a * (x0 + G4(-x4)),
    };
    const int EO[2] = {
        G2(b * x2) + G4(+c * x6),
        G2(c * x2) + G4(-b * x6),
    };
    const int E[4] = {
        EE[0] + EO[0], EE[1] + EO[1],
        EE[1] - EO[1], EE[0] - EO[0],
    };
    const int O[4] = {
        d * x1 + G2(+e * x3) + G4(+f * x5 + g * x7),
        e * x1 + G2(-g * x3) + G4(-d * x5 - f * x7),
        f * x1 + G2(-d * x3) + G4(+g * x5 + e * x7),
        g * x1 + G2(-f * x3) + G4(+e * x5 - d * x7),
    };

    coeffs[0 * stride] = E[0] + O[0];
    coeffs[1 * stride] = E[1] + O[1];
    coeffs[2 * stride] = E[2] + O[2];
    coeffs[3 * stride] = E[3] + O[3];
    coeffs[4 * stride] = E[3] - O[3];
    coeffs[5 * stride] = E[2] - O[2];
    coeffs[6 * stride] = E[1] - O[1];
    coeffs[7 * stride] = E[0] - O[0];
}

/*
transmatrix[16][16] = {
    { a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a},
    { h,  i,  j,  k,  l,  m,  n,  o, -o, -n, -m, -l, -k, -j, -i, -h},
    { d,  e,  f,  g, -g, -f, -e, -d, -d, -e, -f, -g,  g,  f,  e,  d},
    { i,  l,  o, -m, -j, -h, -k, -n,  n,  k,  h,  j,  m, -o, -l, -i},
    { b,  c, -c, -b, -b, -c,  c,  b,  b,  c, -c, -b, -b, -c,  c,  b},
    { j,  o, -k, -i, -n,  l,  h,  m, -m, -h, -l,  n,  i,  k, -o, -j},
    { e, -g, -d, -f,  f,  d,  g, -e, -e,  g,  d,  f, -f, -d, -g,  e},
    { k, -m, -i,  o,  h,  n, -j, -l,  l,  j, -n, -h, -o,  i,  m, -k},
    { a, -a, -a,  a,  a, -a, -a,  a,  a, -a, -a,  a,  a, -a, -a,  a},
    { l, -j, -n,  h, -o, -i,  m,  k, -k, -m,  i,  o, -h,  n,  j, -l},
    { f, -d,  g,  e, -e, -g,  d, -f, -f,  d, -g, -e,  e,  g, -d,  f},
    { m, -h,  l,  n, -i,  k,  o, -j,  j, -o, -k,  i, -n, -l,  h, -m},
    { c, -b,  b, -c, -c,  b, -b,  c,  c, -b,  b, -c, -c,  b, -b,  c},
    { n, -k,  h, -j,  m,  o, -l,  i, -i,  l, -o, -m,  j, -h,  k, -n},
    { g, -f,  e, -d,  d, -e,  f, -g, -g,  f, -e,  d, -d,  e, -f,  g},
    { o, -n,  m, -l,  k, -j,  i, -h,  h, -i,  j, -k,  l, -m,  n, -o},
}
 */
void ff_vvc_inv_dct2_16(int *coeffs, const ptrdiff_t stride, const size_t nz)
{
    const int a = 64, b = 83, c = 36, d = 89, e = 75, f = 50, g = 18, h = 90;
    const int i = 87, j = 80, k = 70, l = 57, m = 43, n = 25, o =  9;
    const int x0  = coeffs[0  * stride], x1  = coeffs[1  * stride];
    const int x2  = coeffs[2  * stride], x3  = coeffs[3  * stride];
    const int x4  = coeffs[4  * stride], x5  = coeffs[5  * stride];
    const int x6  = coeffs[6  * stride], x7  = coeffs[7  * stride];
    const int x8  = coeffs[8  * stride], x9  = coeffs[9  * stride];
    const int x10 = coeffs[10 * stride], x11 = coeffs[11 * stride];
    const int x12 = coeffs[12 * stride], x13 = coeffs[13 * stride];
    const int x14 = coeffs[14 * stride], x15 = coeffs[15 * stride];
    const int EEE[2] = {
        a * (x0 + G8(+x8)),
        a * (x0 + G8(-x8)),
    };
    const int EEO[2] = {
        G4(b * x4) + G8(+c * x12),
        G4(c * x4) + G8(-b * x12),
    };
    const int EE[4] = {
        EEE[0] + EEO[0], EEE[1] + EEO[1],
        EEE[1] - EEO[1], EEE[0] - EEO[0],
    };
    const int EO[4] = {
        G2(d * x2)  + G4(+e * x6) + G8(+f * x10 + g * x14),
        G2(e * x2)  + G4(-g * x6) + G8(-d * x10 - f * x14),
        G2(f * x2)  + G4(-d * x6) + G8(+g * x10 + e * x14),
        G2(g * x2)  + G4(-f * x6) + G8(+e * x10 - d * x14),
    };
    const int E[8] = {
        EE[0] + EO[0], EE[1] + EO[1], EE[2] + EO[2], EE[3] + EO[3],
        EE[3] - EO[3], EE[2] - EO[2], EE[1] - EO[1], EE[0] - EO[0],
    };
    const int O[8] = {
        h * x1 + G2(+i * x3) + G4(+j * x5 + k * x7) + G8(+l * x9 + m * x11 + n * x13 + o * x15),
        i * x1 + G2(+l * x3) + G4(+o * x5 - m * x7) + G8(-j * x9 - h * x11 - k * x13 - n * x15),
        j * x1 + G2(+o * x3) + G4(-k * x5 - i * x7) + G8(-n * x9 + l * x11 + h * x13 + m * x15),
        k * x1 + G2(-m * x3) + G4(-i * x5 + o * x7) + G8(+h * x9 + n * x11 - j * x13 - l * x15),
        l * x1 + G2(-j * x3) + G4(-n * x5 + h * x7) + G8(-o * x9 - i * x11 + m * x13 + k * x15),
        m * x1 + G2(-h * x3) + G4(+l * x5 + n * x7) + G8(-i * x9 + k * x11 + o * x13 - j * x15),
        n * x1 + G2(-k * x3) + G4(+h * x5 - j * x7) + G8(+m * x9 + o * x11 - l * x13 + i * x15),
        o * x1 + G2(-n * x3) + G4(+m * x5 - l * x7) + G8(+k * x9 - j * x11 + i * x13 - h * x15),
    };

    coeffs[0  * stride] = E[0] + O[0];
    coeffs[1  * stride] = E[1] + O[1];
    coeffs[2  * stride] = E[2] + O[2];
    coeffs[3  * stride] = E[3] + O[3];
    coeffs[4  * stride] = E[4] + O[4];
    coeffs[5  * stride] = E[5] + O[5];
    coeffs[6  * stride] = E[6] + O[6];
    coeffs[7  * stride] = E[7] + O[7];
    coeffs[8  * stride] = E[7] - O[7];
    coeffs[9  * stride] = E[6] - O[6];
    coeffs[10 * stride] = E[5] - O[5];
    coeffs[11 * stride] = E[4] - O[4];
    coeffs[12 * stride] = E[3] - O[3];
    coeffs[13 * stride] = E[2] - O[2];
    coeffs[14 * stride] = E[1] - O[1];
    coeffs[15 * stride] = E[0] - O[0];
}

/*
transMatrix[32][32] = {
    { a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a},
    { p,  q,  r,  s,  t,  u,  v,  w,  x,  y,  z,  A,  B,  C,  D,  E, -E, -D, -C, -B, -A, -z, -y, -x, -w, -v, -u, -t, -s, -r, -q, -p},
    { h,  i,  j,  k,  l,  m,  n,  o, -o, -n, -m, -l, -k, -j, -i, -h, -h, -i, -j, -k, -l, -m, -n, -o,  o,  n,  m,  l,  k,  j,  i,  h},
    { q,  t,  w,  z,  C, -E, -B, -y, -v, -s, -p, -r, -u, -x, -A, -D,  D,  A,  x,  u,  r,  p,  s,  v,  y,  B,  E, -C, -z, -w, -t, -q},
    { d,  e,  f,  g, -g, -f, -e, -d, -d, -e, -f, -g,  g,  f,  e,  d,  d,  e,  f,  g, -g, -f, -e, -d, -d, -e, -f, -g,  g,  f,  e,  d},
    { r,  w,  B, -D, -y, -t, -p, -u, -z, -E,  A,  v,  q,  s,  x,  C, -C, -x, -s, -q, -v, -A,  E,  z,  u,  p,  t,  y,  D, -B, -w, -r},
    { i,  l,  o, -m, -j, -h, -k, -n,  n,  k,  h,  j,  m, -o, -l, -i, -i, -l, -o,  m,  j,  h,  k,  n, -n, -k, -h, -j, -m,  o,  l,  i},
    { s,  z, -D, -w, -p, -v, -C,  A,  t,  r,  y, -E, -x, -q, -u, -B,  B,  u,  q,  x,  E, -y, -r, -t, -A,  C,  v,  p,  w,  D, -z, -s},
    { b,  c, -c, -b, -b, -c,  c,  b,  b,  c, -c, -b, -b, -c,  c,  b,  b,  c, -c, -b, -b, -c,  c,  b,  b,  c, -c, -b, -b, -c,  c,  b},
    { t,  C, -y, -p, -x,  D,  u,  s,  B, -z, -q, -w,  E,  v,  r,  A, -A, -r, -v, -E,  w,  q,  z, -B, -s, -u, -D,  x,  p,  y, -C, -t},
    { j,  o, -k, -i, -n,  l,  h,  m, -m, -h, -l,  n,  i,  k, -o, -j, -j, -o,  k,  i,  n, -l, -h, -m,  m,  h,  l, -n, -i, -k,  o,  j},
    { u, -E, -t, -v,  D,  s,  w, -C, -r, -x,  B,  q,  y, -A, -p, -z,  z,  p,  A, -y, -q, -B,  x,  r,  C, -w, -s, -D,  v,  t,  E, -u},
    { e, -g, -d, -f,  f,  d,  g, -e, -e,  g,  d,  f, -f, -d, -g,  e,  e, -g, -d, -f,  f,  d,  g, -e, -e,  g,  d,  f, -f, -d, -g,  e},
    { v, -B, -p, -C,  u,  w, -A, -q, -D,  t,  x, -z, -r, -E,  s,  y, -y, -s,  E,  r,  z, -x, -t,  D,  q,  A, -w, -u,  C,  p,  B, -v},
    { k, -m, -i,  o,  h,  n, -j, -l,  l,  j, -n, -h, -o,  i,  m, -k, -k,  m,  i, -o, -h, -n,  j,  l, -l, -j,  n,  h,  o, -i, -m,  k},
    { w, -y, -u,  A,  s, -C, -q,  E,  p,  D, -r, -B,  t,  z, -v, -x,  x,  v, -z, -t,  B,  r, -D, -p, -E,  q,  C, -s, -A,  u,  y, -w},
    { a, -a, -a,  a,  a, -a, -a,  a,  a, -a, -a,  a,  a, -a, -a,  a,  a, -a, -a,  a,  a, -a, -a,  a,  a, -a, -a,  a,  a, -a, -a,  a},
    { x, -v, -z,  t,  B, -r, -D,  p, -E, -q,  C,  s, -A, -u,  y,  w, -w, -y,  u,  A, -s, -C,  q,  E, -p,  D,  r, -B, -t,  z,  v, -x},
    { l, -j, -n,  h, -o, -i,  m,  k, -k, -m,  i,  o, -h,  n,  j, -l, -l,  j,  n, -h,  o,  i, -m, -k,  k,  m, -i, -o,  h, -n, -j,  l},
    { y, -s, -E,  r, -z, -x,  t,  D, -q,  A,  w, -u, -C,  p, -B, -v,  v,  B, -p,  C,  u, -w, -A,  q, -D, -t,  x,  z, -r,  E,  s, -y},
    { f, -d,  g,  e, -e, -g,  d, -f, -f,  d, -g, -e,  e,  g, -d,  f,  f, -d,  g,  e, -e, -g,  d, -f, -f,  d, -g, -e,  e,  g, -d,  f},
    { z, -p,  A,  y, -q,  B,  x, -r,  C,  w, -s,  D,  v, -t,  E,  u, -u, -E,  t, -v, -D,  s, -w, -C,  r, -x, -B,  q, -y, -A,  p, -z},
    { m, -h,  l,  n, -i,  k,  o, -j,  j, -o, -k,  i, -n, -l,  h, -m, -m,  h, -l, -n,  i, -k, -o,  j, -j,  o,  k, -i,  n,  l, -h,  m},
    { A, -r,  v, -E, -w,  q, -z, -B,  s, -u,  D,  x, -p,  y,  C, -t,  t, -C, -y,  p, -x, -D,  u, -s,  B,  z, -q,  w,  E, -v,  r, -A},
    { c, -b,  b, -c, -c,  b, -b,  c,  c, -b,  b, -c, -c,  b, -b,  c,  c, -b,  b, -c, -c,  b, -b,  c,  c, -b,  b, -c, -c,  b, -b,  c},
    { B, -u,  q, -x,  E,  y, -r,  t, -A, -C,  v, -p,  w, -D, -z,  s, -s,  z,  D, -w,  p, -v,  C,  A, -t,  r, -y, -E,  x, -q,  u, -B},
    { n, -k,  h, -j,  m,  o, -l,  i, -i,  l, -o, -m,  j, -h,  k, -n, -n,  k, -h,  j, -m, -o,  l, -i,  i, -l,  o,  m, -j,  h, -k,  n},
    { C, -x,  s, -q,  v, -A, -E,  z, -u,  p, -t,  y, -D, -B,  w, -r,  r, -w,  B,  D, -y,  t, -p,  u, -z,  E,  A, -v,  q, -s,  x, -C},
    { g, -f,  e, -d,  d, -e,  f, -g, -g,  f, -e,  d, -d,  e, -f,  g,  g, -f,  e, -d,  d, -e,  f, -g, -g,  f, -e,  d, -d,  e, -f,  g},
    { D, -A,  x, -u,  r, -p,  s, -v,  y, -B,  E,  C, -z,  w, -t,  q, -q,  t, -w,  z, -C, -E,  B, -y,  v, -s,  p, -r,  u, -x,  A, -D},
    { o, -n,  m, -l,  k, -j,  i, -h,  h, -i,  j, -k,  l, -m,  n, -o, -o,  n, -m,  l, -k,  j, -i,  h, -h,  i, -j,  k, -l,  m, -n,  o},
    { E, -D,  C, -B,  A, -z,  y, -x,  w, -v,  u, -t,  s, -r,  q, -p,  p, -q,  r, -s,  t, -u,  v, -w,  x, -y,  z, -A,  B, -C,  D, -E},
}
 */
void ff_vvc_inv_dct2_32(int *coeffs, const ptrdiff_t stride, const size_t nz)
{
    const int a = 64, b = 83, c = 36, d = 89, e = 75, f = 50, g = 18, h = 90;
    const int i = 87, j = 80, k = 70, l = 57, m = 43, n = 25, o =  9, p = 90;
    const int q = 90, r = 88, s = 85, t = 82, u = 78, v = 73, w = 67, x = 61;
    const int y = 54, z = 46, A = 38, B = 31, C = 22, D = 13, E_=  4;
    const int x0  = coeffs[0  * stride], x1  = coeffs[1  * stride];
    const int x2  = coeffs[2  * stride], x3  = coeffs[3  * stride];
    const int x4  = coeffs[4  * stride], x5  = coeffs[5  * stride];
    const int x6  = coeffs[6  * stride], x7  = coeffs[7  * stride];
    const int x8  = coeffs[8  * stride], x9  = coeffs[9  * stride];
    const int x10 = coeffs[10 * stride], x11 = coeffs[11 * stride];
    const int x12 = coeffs[12 * stride], x13 = coeffs[13 * stride];
    const int x14 = coeffs[14 * stride], x15 = coeffs[15 * stride];
    const int x16 = coeffs[16 * stride], x17 = coeffs[17 * stride];
    const int x18 = coeffs[18 * stride], x19 = coeffs[19 * stride];
    const int x20 = coeffs[20 * stride], x21 = coeffs[21 * stride];
    const int x22 = coeffs[22 * stride], x23 = coeffs[23 * stride];
    const int x24 = coeffs[24 * stride], x25 = coeffs[25 * stride];
    const int x26 = coeffs[26 * stride], x27 = coeffs[27 * stride];
    const int x28 = coeffs[28 * stride], x29 = coeffs[29 * stride];
    const int x30 = coeffs[30 * stride], x31 = coeffs[31 * stride];
    const int EEEE[2] = {
        a * (x0 + G16(+x16)),
        a * (x0 + G16(-x16)),
    };
    const int EEEO[2] = {
        G8(b * x8) + G16(+c * x24),
        G8(c * x8) + G16(-b * x24),
    };
    const int EEE[4] = {
        EEEE[0] + EEEO[0], EEEE[1] + EEEO[1],
        EEEE[1] - EEEO[1], EEEE[0] - EEEO[0],
    };
    const int EEO[4] = {
        G4(d * x4) + G8(+e * x12) + G16(+f * x20 + g * x28),
        G4(e * x4) + G8(-g * x12) + G16(-d * x20 - f * x28),
        G4(f * x4) + G8(-d * x12) + G16(+g * x20 + e * x28),
        G4(g * x4) + G8(-f * x12) + G16(+e * x20 - d * x28),
    };
    const int EE[8] = {
        EEE[0] + EEO[0], EEE[1] + EEO[1], EEE[2] + EEO[2], EEE[3] + EEO[3],
        EEE[3] - EEO[3], EEE[2] - EEO[2], EEE[1] - EEO[1], EEE[0] - EEO[0],
    };
    const int EO[8] = {
        G2(h * x2) + G4(+i * x6) + G8(+ j * x10 + k * x14) + G16(+l * x18 + m * x22 + n * x26 + o * x30),
        G2(i * x2) + G4(+l * x6) + G8(+ o * x10 - m * x14) + G16(-j * x18 - h * x22 - k * x26 - n * x30),
        G2(j * x2) + G4(+o * x6) + G8(- k * x10 - i * x14) + G16(-n * x18 + l * x22 + h * x26 + m * x30),
        G2(k * x2) + G4(-m * x6) + G8(- i * x10 + o * x14) + G16(+h * x18 + n * x22 - j * x26 - l * x30),
        G2(l * x2) + G4(-j * x6) + G8(- n * x10 + h * x14) + G16(-o * x18 - i * x22 + m * x26 + k * x30),
        G2(m * x2) + G4(-h * x6) + G8(+ l * x10 + n * x14) + G16(-i * x18 + k * x22 + o * x26 - j * x30),
        G2(n * x2) + G4(-k * x6) + G8(+ h * x10 - j * x14) + G16(+m * x18 + o * x22 - l * x26 + i * x30),
        G2(o * x2) + G4(-n * x6) + G8(+ m * x10 - l * x14) + G16(+k * x18 - j * x22 + i * x26 - h * x30),
    };
    const int E[16] = {
        EE[0] + EO[0], EE[1] + EO[1], EE[2] + EO[2], EE[3] + EO[3], EE[4] + EO[4], EE[5] + EO[5], EE[6] + EO[6], EE[7] + EO[7],
        EE[7] - EO[7], EE[6] - EO[6], EE[5] - EO[5], EE[4] - EO[4], EE[3] - EO[3], EE[2] - EO[2], EE[1] - EO[1], EE[0] - EO[0],
    };
    const int O[16] = {
        p * x1 + G2(+q * x3) + G4(+r * x5 + s * x7) + G8(+t * x9 + u * x11 + v * x13 + w * x15) + G16(+x * x17 + y * x19 + z * x21 + A * x23 + B * x25 + C * x27 + D * x29 + E_* x31),
        q * x1 + G2(+t * x3) + G4(+w * x5 + z * x7) + G8(+C * x9 - E_* x11 - B * x13 - y * x15) + G16(-v * x17 - s * x19 - p * x21 - r * x23 - u * x25 - x * x27 - A * x29 - D * x31),
        r * x1 + G2(+w * x3) + G4(+B * x5 - D * x7) + G8(-y * x9 - t * x11 - p * x13 - u * x15) + G16(-z * x17 - E_* x19 + A * x21 + v * x23 + q * x25 + s * x27 + x * x29 + C * x31),
        s * x1 + G2(+z * x3) + G4(-D * x5 - w * x7) + G8(-p * x9 - v * x11 - C * x13 + A * x15) + G16(+t * x17 + r * x19 + y * x21 - E_* x23 - x * x25 - q * x27 - u * x29 - B * x31),
        t * x1 + G2(+C * x3) + G4(-y * x5 - p * x7) + G8(-x * x9 + D * x11 + u * x13 + s * x15) + G16(+B * x17 - z * x19 - q * x21 - w * x23 + E_* x25 + v * x27 + r * x29 + A * x31),
        u * x1 + G2(-E_* x3) + G4(-t * x5 - v * x7) + G8(+D * x9 + s * x11 + w * x13 - C * x15) + G16(-r * x17 - x * x19 + B * x21 + q * x23 + y * x25 - A * x27 - p * x29 - z * x31),
        v * x1 + G2(-B * x3) + G4(-p * x5 - C * x7) + G8(+u * x9 + w * x11 - A * x13 - q * x15) + G16(-D * x17 + t * x19 + x * x21 - z * x23 - r * x25 - E_* x27 + s * x29 + y * x31),
        w * x1 + G2(-y * x3) + G4(-u * x5 + A * x7) + G8(+s * x9 - C * x11 - q * x13 + E_* x15) + G16(+p * x17 + D * x19 - r * x21 - B * x23 + t * x25 + z * x27 - v * x29 - x * x31),
        x * x1 + G2(-v * x3) + G4(-z * x5 + t * x7) + G8(+B * x9 - r * x11 - D * x13 + p * x15) + G16(-E_* x17 - q * x19 + C * x21 + s * x23 - A * x25 - u * x27 + y * x29 + w * x31),
        y * x1 + G2(-s * x3) + G4(-E_* x5 + r * x7) + G8(-z * x9 - x * x11 + t * x13 + D * x15) + G16(-q * x17 + A * x19 + w * x21 - u * x23 - C * x25 + p * x27 - B * x29 - v * x31),
        z * x1 + G2(-p * x3) + G4(+A * x5 + y * x7) + G8(-q * x9 + B * x11 + x * x13 - r * x15) + G16(+C * x17 + w * x19 - s * x21 + D * x23 + v * x25 - t * x27 + E_* x29 + u * x31),
        A * x1 + G2(-r * x3) + G4(+v * x5 - E_* x7) + G8(-w * x9 + q * x11 - z * x13 - B * x15) + G16(+s * x17 - u * x19 + D * x21 + x * x23 - p * x25 + y * x27 + C * x29 - t * x31),
        B * x1 + G2(-u * x3) + G4(+q * x5 - x * x7) + G8(+E_* x9 + y * x11 - r * x13 + t * x15) + G16(-A * x17 - C * x19 + v * x21 - p * x23 + w * x25 - D * x27 - z * x29 + s * x31),
        C * x1 + G2(-x * x3) + G4(+s * x5 - q * x7) + G8(+v * x9 - A * x11 - E_* x13 + z * x15) + G16(-u * x17 + p * x19 - t * x21 + y * x23 - D * x25 - B * x27 + w * x29 - r * x31),
        D * x1 + G2(-A * x3) + G4(+x * x5 - u * x7) + G8(+r * x9 - p * x11 + s * x13 - v * x15) + G16(+y * x17 - B * x19 + E_* x21 + C * x23 - z * x25 + w * x27 - t * x29 + q * x31),
        E_* x1 + G2(-D * x3) + G4(+C * x5 - B * x7) + G8(+A * x9 - z * x11 + y * x13 - x * x15) + G16(+w * x17 - v * x19 + u * x21 - t * x23 + s * x25 - r * x27 + q * x29 - p * x31),
    };

    coeffs[0  * stride] = E[0]  + O[0];
    coeffs[1  * stride] = E[1]  + O[1];
    coeffs[2  * stride] = E[2]  + O[2];
    coeffs[3  * stride] = E[3]  + O[3];
    coeffs[4  * stride] = E[4]  + O[4];
    coeffs[5  * stride] = E[5]  + O[5];
    coeffs[6  * stride] = E[6]  + O[6];
    coeffs[7  * stride] = E[7]  + O[7];
    coeffs[8  * stride] = E[8]  + O[8];
    coeffs[9  * stride] = E[9]  + O[9];
    coeffs[10 * stride] = E[10] + O[10];
    coeffs[11 * stride] = E[11] + O[11];
    coeffs[12 * stride] = E[12] + O[12];
    coeffs[13 * stride] = E[13] + O[13];
    coeffs[14 * stride] = E[14] + O[14];
    coeffs[15 * stride] = E[15] + O[15];
    coeffs[16 * stride] = E[15] - O[15];
    coeffs[17 * stride] = E[14] - O[14];
    coeffs[18 * stride] = E[13] - O[13];
    coeffs[19 * stride] = E[12] - O[12];
    coeffs[20 * stride] = E[11] - O[11];
    coeffs[21 * stride] = E[10] - O[10];
    coeffs[22 * stride] = E[9]  - O[9];
    coeffs[23 * stride] = E[8]  - O[8];
    coeffs[24 * stride] = E[7]  - O[7];
    coeffs[25 * stride] = E[6]  - O[6];
    coeffs[26 * stride] = E[5]  - O[5];
    coeffs[27 * stride] = E[4]  - O[4];
    coeffs[28 * stride] = E[3]  - O[3];
    coeffs[29 * stride] = E[2]  - O[2];
    coeffs[30 * stride] = E[1]  - O[1];
    coeffs[31 * stride] = E[0]  - O[0];
}

/*
transMatrix[64][64] = {
    { aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa },
    { bf,  bg,  bh,  bi,  bj,  bk,  bl,  bm,  bn,  bo,  bp,  bq,  br,  bs,  bt,  bu,  bv,  bw,  bx,  by,  bz,  ca,  cb,  cc,  cd,  ce,  cf,  cg,  ch,  ci,  cj,  ck, -ck, -cj, -ci, -ch, -cg, -cf, -ce, -cd, -cc, -cb, -ca, -bz, -by, -bx, -bw, -bv, -bu, -bt, -bs, -br, -bq, -bp, -bo, -bn, -bm, -bl, -bk, -bj, -bi, -bh, -bg, -bf },
    { ap,  aq,  ar,  as,  at,  au,  av,  aw,  ax,  ay,  az,  ba,  bb,  bc,  bd,  be, -be, -bd, -bc, -bb, -ba, -az, -ay, -ax, -aw, -av, -au, -at, -as, -ar, -aq, -ap, -ap, -aq, -ar, -as, -at, -au, -av, -aw, -ax, -ay, -az, -ba, -bb, -bc, -bd, -be,  be,  bd,  bc,  bb,  ba,  az,  ay,  ax,  aw,  av,  au,  at,  as,  ar,  aq,  ap },
    { bg,  bj,  bm,  bp,  bs,  bv,  by,  cb,  ce,  ch,  ck, -ci, -cf, -cc, -bz, -bw, -bt, -bq, -bn, -bk, -bh, -bf, -bi, -bl, -bo, -br, -bu, -bx, -ca, -cd, -cg, -cj,  cj,  cg,  cd,  ca,  bx,  bu,  br,  bo,  bl,  bi,  bf,  bh,  bk,  bn,  bq,  bt,  bw,  bz,  cc,  cf,  ci, -ck, -ch, -ce, -cb, -by, -bv, -bs, -bp, -bm, -bj, -bg },
    { ah,  ai,  aj,  ak,  al,  am,  an,  ao, -ao, -an, -am, -al, -ak, -aj, -ai, -ah, -ah, -ai, -aj, -ak, -al, -am, -an, -ao,  ao,  an,  am,  al,  ak,  aj,  ai,  ah,  ah,  ai,  aj,  ak,  al,  am,  an,  ao, -ao, -an, -am, -al, -ak, -aj, -ai, -ah, -ah, -ai, -aj, -ak, -al, -am, -an, -ao,  ao,  an,  am,  al,  ak,  aj,  ai,  ah },
    { bh,  bm,  br,  bw,  cb,  cg, -ck, -cf, -ca, -bv, -bq, -bl, -bg, -bi, -bn, -bs, -bx, -cc, -ch,  cj,  ce,  bz,  bu,  bp,  bk,  bf,  bj,  bo,  bt,  by,  cd,  ci, -ci, -cd, -by, -bt, -bo, -bj, -bf, -bk, -bp, -bu, -bz, -ce, -cj,  ch,  cc,  bx,  bs,  bn,  bi,  bg,  bl,  bq,  bv,  ca,  cf,  ck, -cg, -cb, -bw, -br, -bm, -bh },
    { aq,  at,  aw,  az,  bc, -be, -bb, -ay, -av, -as, -ap, -ar, -au, -ax, -ba, -bd,  bd,  ba,  ax,  au,  ar,  ap,  as,  av,  ay,  bb,  be, -bc, -az, -aw, -at, -aq, -aq, -at, -aw, -az, -bc,  be,  bb,  ay,  av,  as,  ap,  ar,  au,  ax,  ba,  bd, -bd, -ba, -ax, -au, -ar, -ap, -as, -av, -ay, -bb, -be,  bc,  az,  aw,  at,  aq },
    { bi,  bp,  bw,  cd,  ck, -ce, -bx, -bq, -bj, -bh, -bo, -bv, -cc, -cj,  cf,  by,  br,  bk,  bg,  bn,  bu,  cb,  ci, -cg, -bz, -bs, -bl, -bf, -bm, -bt, -ca, -ch,  ch,  ca,  bt,  bm,  bf,  bl,  bs,  bz,  cg, -ci, -cb, -bu, -bn, -bg, -bk, -br, -by, -cf,  cj,  cc,  bv,  bo,  bh,  bj,  bq,  bx,  ce, -ck, -cd, -bw, -bp, -bi },
    { ad,  ae,  af,  ag, -ag, -af, -ae, -ad, -ad, -ae, -af, -ag,  ag,  af,  ae,  ad,  ad,  ae,  af,  ag, -ag, -af, -ae, -ad, -ad, -ae, -af, -ag,  ag,  af,  ae,  ad,  ad,  ae,  af,  ag, -ag, -af, -ae, -ad, -ad, -ae, -af, -ag,  ag,  af,  ae,  ad,  ad,  ae,  af,  ag, -ag, -af, -ae, -ad, -ad, -ae, -af, -ag,  ag,  af,  ae,  ad },
    { bj,  bs,  cb,  ck, -cc, -bt, -bk, -bi, -br, -ca, -cj,  cd,  bu,  bl,  bh,  bq,  bz,  ci, -ce, -bv, -bm, -bg, -bp, -by, -ch,  cf,  bw,  bn,  bf,  bo,  bx,  cg, -cg, -bx, -bo, -bf, -bn, -bw, -cf,  ch,  by,  bp,  bg,  bm,  bv,  ce, -ci, -bz, -bq, -bh, -bl, -bu, -cd,  cj,  ca,  br,  bi,  bk,  bt,  cc, -ck, -cb, -bs, -bj },
    { ar,  aw,  bb, -bd, -ay, -at, -ap, -au, -az, -be,  ba,  av,  aq,  as,  ax,  bc, -bc, -ax, -as, -aq, -av, -ba,  be,  az,  au,  ap,  at,  ay,  bd, -bb, -aw, -ar, -ar, -aw, -bb,  bd,  ay,  at,  ap,  au,  az,  be, -ba, -av, -aq, -as, -ax, -bc,  bc,  ax,  as,  aq,  av,  ba, -be, -az, -au, -ap, -at, -ay, -bd,  bb,  aw,  ar },
    { bk,  bv,  cg, -ce, -bt, -bi, -bm, -bx, -ci,  cc,  br,  bg,  bo,  bz,  ck, -ca, -bp, -bf, -bq, -cb,  cj,  by,  bn,  bh,  bs,  cd, -ch, -bw, -bl, -bj, -bu, -cf,  cf,  bu,  bj,  bl,  bw,  ch, -cd, -bs, -bh, -bn, -by, -cj,  cb,  bq,  bf,  bp,  ca, -ck, -bz, -bo, -bg, -br, -cc,  ci,  bx,  bm,  bi,  bt,  ce, -cg, -bv, -bk },
    { ai,  al,  ao, -am, -aj, -ah, -ak, -an,  an,  ak,  ah,  aj,  am, -ao, -al, -ai, -ai, -al, -ao,  am,  aj,  ah,  ak,  an, -an, -ak, -ah, -aj, -am,  ao,  al,  ai,  ai,  al,  ao, -am, -aj, -ah, -ak, -an,  an,  ak,  ah,  aj,  am, -ao, -al, -ai, -ai, -al, -ao,  am,  aj,  ah,  ak,  an, -an, -ak, -ah, -aj, -am,  ao,  al,  ai },
    { bl,  by, -ck, -bx, -bk, -bm, -bz,  cj,  bw,  bj,  bn,  ca, -ci, -bv, -bi, -bo, -cb,  ch,  bu,  bh,  bp,  cc, -cg, -bt, -bg, -bq, -cd,  cf,  bs,  bf,  br,  ce, -ce, -br, -bf, -bs, -cf,  cd,  bq,  bg,  bt,  cg, -cc, -bp, -bh, -bu, -ch,  cb,  bo,  bi,  bv,  ci, -ca, -bn, -bj, -bw, -cj,  bz,  bm,  bk,  bx,  ck, -by, -bl },
    { as,  az, -bd, -aw, -ap, -av, -bc,  ba,  at,  ar,  ay, -be, -ax, -aq, -au, -bb,  bb,  au,  aq,  ax,  be, -ay, -ar, -at, -ba,  bc,  av,  ap,  aw,  bd, -az, -as, -as, -az,  bd,  aw,  ap,  av,  bc, -ba, -at, -ar, -ay,  be,  ax,  aq,  au,  bb, -bb, -au, -aq, -ax, -be,  ay,  ar,  at,  ba, -bc, -av, -ap, -aw, -bd,  az,  as },
    { bm,  cb, -cf, -bq, -bi, -bx,  cj,  bu,  bf,  bt,  ci, -by, -bj, -bp, -ce,  cc,  bn,  bl,  ca, -cg, -br, -bh, -bw,  ck,  bv,  bg,  bs,  ch, -bz, -bk, -bo, -cd,  cd,  bo,  bk,  bz, -ch, -bs, -bg, -bv, -ck,  bw,  bh,  br,  cg, -ca, -bl, -bn, -cc,  ce,  bp,  bj,  by, -ci, -bt, -bf, -bu, -cj,  bx,  bi,  bq,  cf, -cb, -bm },
    { ab,  ac, -ac, -ab, -ab, -ac,  ac,  ab,  ab,  ac, -ac, -ab, -ab, -ac,  ac,  ab,  ab,  ac, -ac, -ab, -ab, -ac,  ac,  ab,  ab,  ac, -ac, -ab, -ab, -ac,  ac,  ab,  ab,  ac, -ac, -ab, -ab, -ac,  ac,  ab,  ab,  ac, -ac, -ab, -ab, -ac,  ac,  ab,  ab,  ac, -ac, -ab, -ab, -ac,  ac,  ab,  ab,  ac, -ac, -ab, -ab, -ac,  ac,  ab },
    { bn,  ce, -ca, -bj, -br, -ci,  bw,  bf,  bv, -cj, -bs, -bi, -bz,  cf,  bo,  bm,  cd, -cb, -bk, -bq, -ch,  bx,  bg,  bu, -ck, -bt, -bh, -by,  cg,  bp,  bl,  cc, -cc, -bl, -bp, -cg,  by,  bh,  bt,  ck, -bu, -bg, -bx,  ch,  bq,  bk,  cb, -cd, -bm, -bo, -cf,  bz,  bi,  bs,  cj, -bv, -bf, -bw,  ci,  br,  bj,  ca, -ce, -bn },
    { at,  bc, -ay, -ap, -ax,  bd,  au,  as,  bb, -az, -aq, -aw,  be,  av,  ar,  ba, -ba, -ar, -av, -be,  aw,  aq,  az, -bb, -as, -au, -bd,  ax,  ap,  ay, -bc, -at, -at, -bc,  ay,  ap,  ax, -bd, -au, -as, -bb,  az,  aq,  aw, -be, -av, -ar, -ba,  ba,  ar,  av,  be, -aw, -aq, -az,  bb,  as,  au,  bd, -ax, -ap, -ay,  bc,  at },
    { bo,  ch, -bv, -bh, -ca,  cc,  bj,  bt, -cj, -bq, -bm, -cf,  bx,  bf,  by, -ce, -bl, -br, -ck,  bs,  bk,  cd, -bz, -bg, -bw,  cg,  bn,  bp,  ci, -bu, -bi, -cb,  cb,  bi,  bu, -ci, -bp, -bn, -cg,  bw,  bg,  bz, -cd, -bk, -bs,  ck,  br,  bl,  ce, -by, -bf, -bx,  cf,  bm,  bq,  cj, -bt, -bj, -cc,  ca,  bh,  bv, -ch, -bo },
    { aj,  ao, -ak, -ai, -an,  al,  ah,  am, -am, -ah, -al,  an,  ai,  ak, -ao, -aj, -aj, -ao,  ak,  ai,  an, -al, -ah, -am,  am,  ah,  al, -an, -ai, -ak,  ao,  aj,  aj,  ao, -ak, -ai, -an,  al,  ah,  am, -am, -ah, -al,  an,  ai,  ak, -ao, -aj, -aj, -ao,  ak,  ai,  an, -al, -ah, -am,  am,  ah,  al, -an, -ai, -ak,  ao,  aj },
    { bp,  ck, -bq, -bo, -cj,  br,  bn,  ci, -bs, -bm, -ch,  bt,  bl,  cg, -bu, -bk, -cf,  bv,  bj,  ce, -bw, -bi, -cd,  bx,  bh,  cc, -by, -bg, -cb,  bz,  bf,  ca, -ca, -bf, -bz,  cb,  bg,  by, -cc, -bh, -bx,  cd,  bi,  bw, -ce, -bj, -bv,  cf,  bk,  bu, -cg, -bl, -bt,  ch,  bm,  bs, -ci, -bn, -br,  cj,  bo,  bq, -ck, -bp },
    { au, -be, -at, -av,  bd,  as,  aw, -bc, -ar, -ax,  bb,  aq,  ay, -ba, -ap, -az,  az,  ap,  ba, -ay, -aq, -bb,  ax,  ar,  bc, -aw, -as, -bd,  av,  at,  be, -au, -au,  be,  at,  av, -bd, -as, -aw,  bc,  ar,  ax, -bb, -aq, -ay,  ba,  ap,  az, -az, -ap, -ba,  ay,  aq,  bb, -ax, -ar, -bc,  aw,  as,  bd, -av, -at, -be,  au },
    { bq, -ci, -bl, -bv,  cd,  bg,  ca, -by, -bi, -cf,  bt,  bn,  ck, -bo, -bs,  cg,  bj,  bx, -cb, -bf, -cc,  bw,  bk,  ch, -br, -bp,  cj,  bm,  bu, -ce, -bh, -bz,  bz,  bh,  ce, -bu, -bm, -cj,  bp,  br, -ch, -bk, -bw,  cc,  bf,  cb, -bx, -bj, -cg,  bs,  bo, -ck, -bn, -bt,  cf,  bi,  by, -ca, -bg, -cd,  bv,  bl,  ci, -bq },
    { ae, -ag, -ad, -af,  af,  ad,  ag, -ae, -ae,  ag,  ad,  af, -af, -ad, -ag,  ae,  ae, -ag, -ad, -af,  af,  ad,  ag, -ae, -ae,  ag,  ad,  af, -af, -ad, -ag,  ae,  ae, -ag, -ad, -af,  af,  ad,  ag, -ae, -ae,  ag,  ad,  af, -af, -ad, -ag,  ae,  ae, -ag, -ad, -af,  af,  ad,  ag, -ae, -ae,  ag,  ad,  af, -af, -ad, -ag,  ae },
    { br, -cf, -bg, -cc,  bu,  bo, -ci, -bj, -bz,  bx,  bl,  ck, -bm, -bw,  ca,  bi,  ch, -bp, -bt,  cd,  bf,  ce, -bs, -bq,  cg,  bh,  cb, -bv, -bn,  cj,  bk,  by, -by, -bk, -cj,  bn,  bv, -cb, -bh, -cg,  bq,  bs, -ce, -bf, -cd,  bt,  bp, -ch, -bi, -ca,  bw,  bm, -ck, -bl, -bx,  bz,  bj,  ci, -bo, -bu,  cc,  bg,  cf, -br },
    { av, -bb, -ap, -bc,  au,  aw, -ba, -aq, -bd,  at,  ax, -az, -ar, -be,  as,  ay, -ay, -as,  be,  ar,  az, -ax, -at,  bd,  aq,  ba, -aw, -au,  bc,  ap,  bb, -av, -av,  bb,  ap,  bc, -au, -aw,  ba,  aq,  bd, -at, -ax,  az,  ar,  be, -as, -ay,  ay,  as, -be, -ar, -az,  ax,  at, -bd, -aq, -ba,  aw,  au, -bc, -ap, -bb,  av },
    { bs, -cc, -bi, -cj,  bl,  bz, -bv, -bp,  cf,  bf,  cg, -bo, -bw,  by,  bm, -ci, -bh, -cd,  br,  bt, -cb, -bj, -ck,  bk,  ca, -bu, -bq,  ce,  bg,  ch, -bn, -bx,  bx,  bn, -ch, -bg, -ce,  bq,  bu, -ca, -bk,  ck,  bj,  cb, -bt, -br,  cd,  bh,  ci, -bm, -by,  bw,  bo, -cg, -bf, -cf,  bp,  bv, -bz, -bl,  cj,  bi,  cc, -bs },
    { ak, -am, -ai,  ao,  ah,  an, -aj, -al,  al,  aj, -an, -ah, -ao,  ai,  am, -ak, -ak,  am,  ai, -ao, -ah, -an,  aj,  al, -al, -aj,  an,  ah,  ao, -ai, -am,  ak,  ak, -am, -ai,  ao,  ah,  an, -aj, -al,  al,  aj, -an, -ah, -ao,  ai,  am, -ak, -ak,  am,  ai, -ao, -ah, -an,  aj,  al, -al, -aj,  an,  ah,  ao, -ai, -am,  ak },
    { bt, -bz, -bn,  cf,  bh,  ck, -bi, -ce,  bo,  by, -bu, -bs,  ca,  bm, -cg, -bg, -cj,  bj,  cd, -bp, -bx,  bv,  br, -cb, -bl,  ch,  bf,  ci, -bk, -cc,  bq,  bw, -bw, -bq,  cc,  bk, -ci, -bf, -ch,  bl,  cb, -br, -bv,  bx,  bp, -cd, -bj,  cj,  bg,  cg, -bm, -ca,  bs,  bu, -by, -bo,  ce,  bi, -ck, -bh, -cf,  bn,  bz, -bt },
    { aw, -ay, -au,  ba,  as, -bc, -aq,  be,  ap,  bd, -ar, -bb,  at,  az, -av, -ax,  ax,  av, -az, -at,  bb,  ar, -bd, -ap, -be,  aq,  bc, -as, -ba,  au,  ay, -aw, -aw,  ay,  au, -ba, -as,  bc,  aq, -be, -ap, -bd,  ar,  bb, -at, -az,  av,  ax, -ax, -av,  az,  at, -bb, -ar,  bd,  ap,  be, -aq, -bc,  as,  ba, -au, -ay,  aw },
    { bu, -bw, -bs,  by,  bq, -ca, -bo,  cc,  bm, -ce, -bk,  cg,  bi, -ci, -bg,  ck,  bf,  cj, -bh, -ch,  bj,  cf, -bl, -cd,  bn,  cb, -bp, -bz,  br,  bx, -bt, -bv,  bv,  bt, -bx, -br,  bz,  bp, -cb, -bn,  cd,  bl, -cf, -bj,  ch,  bh, -cj, -bf, -ck,  bg,  ci, -bi, -cg,  bk,  ce, -bm, -cc,  bo,  ca, -bq, -by,  bs,  bw, -bu },
    { aa, -aa, -aa,  aa,  aa, -aa, -aa,  aa,  aa, -aa, -aa,  aa,  aa, -aa, -aa,  aa,  aa, -aa, -aa,  aa,  aa, -aa, -aa,  aa,  aa, -aa, -aa,  aa,  aa, -aa, -aa,  aa,  aa, -aa, -aa,  aa,  aa, -aa, -aa,  aa,  aa, -aa, -aa,  aa,  aa, -aa, -aa,  aa,  aa, -aa, -aa,  aa,  aa, -aa, -aa,  aa,  aa, -aa, -aa,  aa,  aa, -aa, -aa,  aa },
    { bv, -bt, -bx,  br,  bz, -bp, -cb,  bn,  cd, -bl, -cf,  bj,  ch, -bh, -cj,  bf, -ck, -bg,  ci,  bi, -cg, -bk,  ce,  bm, -cc, -bo,  ca,  bq, -by, -bs,  bw,  bu, -bu, -bw,  bs,  by, -bq, -ca,  bo,  cc, -bm, -ce,  bk,  cg, -bi, -ci,  bg,  ck, -bf,  cj,  bh, -ch, -bj,  cf,  bl, -cd, -bn,  cb,  bp, -bz, -br,  bx,  bt, -bv },
    { ax, -av, -az,  at,  bb, -ar, -bd,  ap, -be, -aq,  bc,  as, -ba, -au,  ay,  aw, -aw, -ay,  au,  ba, -as, -bc,  aq,  be, -ap,  bd,  ar, -bb, -at,  az,  av, -ax, -ax,  av,  az, -at, -bb,  ar,  bd, -ap,  be,  aq, -bc, -as,  ba,  au, -ay, -aw,  aw,  ay, -au, -ba,  as,  bc, -aq, -be,  ap, -bd, -ar,  bb,  at, -az, -av,  ax },
    { bw, -bq, -cc,  bk,  ci, -bf,  ch,  bl, -cb, -br,  bv,  bx, -bp, -cd,  bj,  cj, -bg,  cg,  bm, -ca, -bs,  bu,  by, -bo, -ce,  bi,  ck, -bh,  cf,  bn, -bz, -bt,  bt,  bz, -bn, -cf,  bh, -ck, -bi,  ce,  bo, -by, -bu,  bs,  ca, -bm, -cg,  bg, -cj, -bj,  cd,  bp, -bx, -bv,  br,  cb, -bl, -ch,  bf, -ci, -bk,  cc,  bq, -bw },
    { al, -aj, -an,  ah, -ao, -ai,  am,  ak, -ak, -am,  ai,  ao, -ah,  an,  aj, -al, -al,  aj,  an, -ah,  ao,  ai, -am, -ak,  ak,  am, -ai, -ao,  ah, -an, -aj,  al,  al, -aj, -an,  ah, -ao, -ai,  am,  ak, -ak, -am,  ai,  ao, -ah,  an,  aj, -al, -al,  aj,  an, -ah,  ao,  ai, -am, -ak,  ak,  am, -ai, -ao,  ah, -an, -aj,  al },
    { bx, -bn, -ch,  bg, -ce, -bq,  bu,  ca, -bk, -ck,  bj, -cb, -bt,  br,  cd, -bh,  ci,  bm, -by, -bw,  bo,  cg, -bf,  cf,  bp, -bv, -bz,  bl,  cj, -bi,  cc,  bs, -bs, -cc,  bi, -cj, -bl,  bz,  bv, -bp, -cf,  bf, -cg, -bo,  bw,  by, -bm, -ci,  bh, -cd, -br,  bt,  cb, -bj,  ck,  bk, -ca, -bu,  bq,  ce, -bg,  ch,  bn, -bx },
    { ay, -as, -be,  ar, -az, -ax,  at,  bd, -aq,  ba,  aw, -au, -bc,  ap, -bb, -av,  av,  bb, -ap,  bc,  au, -aw, -ba,  aq, -bd, -at,  ax,  az, -ar,  be,  as, -ay, -ay,  as,  be, -ar,  az,  ax, -at, -bd,  aq, -ba, -aw,  au,  bc, -ap,  bb,  av, -av, -bb,  ap, -bc, -au,  aw,  ba, -aq,  bd,  at, -ax, -az,  ar, -be, -as,  ay },
    { by, -bk,  cj,  bn, -bv, -cb,  bh, -cg, -bq,  bs,  ce, -bf,  cd,  bt, -bp, -ch,  bi, -ca, -bw,  bm,  ck, -bl,  bx,  bz, -bj,  ci,  bo, -bu, -cc,  bg, -cf, -br,  br,  cf, -bg,  cc,  bu, -bo, -ci,  bj, -bz, -bx,  bl, -ck, -bm,  bw,  ca, -bi,  ch,  bp, -bt, -cd,  bf, -ce, -bs,  bq,  cg, -bh,  cb,  bv, -bn, -cj,  bk, -by },
    { af, -ad,  ag,  ae, -ae, -ag,  ad, -af, -af,  ad, -ag, -ae,  ae,  ag, -ad,  af,  af, -ad,  ag,  ae, -ae, -ag,  ad, -af, -af,  ad, -ag, -ae,  ae,  ag, -ad,  af,  af, -ad,  ag,  ae, -ae, -ag,  ad, -af, -af,  ad, -ag, -ae,  ae,  ag, -ad,  af,  af, -ad,  ag,  ae, -ae, -ag,  ad, -af, -af,  ad, -ag, -ae,  ae,  ag, -ad,  af },
    { bz, -bh,  ce,  bu, -bm,  cj,  bp, -br, -ch,  bk, -bw, -cc,  bf, -cb, -bx,  bj, -cg, -bs,  bo,  ck, -bn,  bt,  cf, -bi,  by,  ca, -bg,  cd,  bv, -bl,  ci,  bq, -bq, -ci,  bl, -bv, -cd,  bg, -ca, -by,  bi, -cf, -bt,  bn, -ck, -bo,  bs,  cg, -bj,  bx,  cb, -bf,  cc,  bw, -bk,  ch,  br, -bp, -cj,  bm, -bu, -ce,  bh, -bz },
    { az, -ap,  ba,  ay, -aq,  bb,  ax, -ar,  bc,  aw, -as,  bd,  av, -at,  be,  au, -au, -be,  at, -av, -bd,  as, -aw, -bc,  ar, -ax, -bb,  aq, -ay, -ba,  ap, -az, -az,  ap, -ba, -ay,  aq, -bb, -ax,  ar, -bc, -aw,  as, -bd, -av,  at, -be, -au,  au,  be, -at,  av,  bd, -as,  aw,  bc, -ar,  ax,  bb, -aq,  ay,  ba, -ap,  az },
    { ca, -bf,  bz,  cb, -bg,  by,  cc, -bh,  bx,  cd, -bi,  bw,  ce, -bj,  bv,  cf, -bk,  bu,  cg, -bl,  bt,  ch, -bm,  bs,  ci, -bn,  br,  cj, -bo,  bq,  ck, -bp,  bp, -ck, -bq,  bo, -cj, -br,  bn, -ci, -bs,  bm, -ch, -bt,  bl, -cg, -bu,  bk, -cf, -bv,  bj, -ce, -bw,  bi, -cd, -bx,  bh, -cc, -by,  bg, -cb, -bz,  bf, -ca },
    { am, -ah,  al,  an, -ai,  ak,  ao, -aj,  aj, -ao, -ak,  ai, -an, -al,  ah, -am, -am,  ah, -al, -an,  ai, -ak, -ao,  aj, -aj,  ao,  ak, -ai,  an,  al, -ah,  am,  am, -ah,  al,  an, -ai,  ak,  ao, -aj,  aj, -ao, -ak,  ai, -an, -al,  ah, -am, -am,  ah, -al, -an,  ai, -ak, -ao,  aj, -aj,  ao,  ak, -ai,  an,  al, -ah,  am },
    { cb, -bi,  bu,  ci, -bp,  bn, -cg, -bw,  bg, -bz, -cd,  bk, -bs, -ck,  br, -bl,  ce,  by, -bf,  bx,  cf, -bm,  bq, -cj, -bt,  bj, -cc, -ca,  bh, -bv, -ch,  bo, -bo,  ch,  bv, -bh,  ca,  cc, -bj,  bt,  cj, -bq,  bm, -cf, -bx,  bf, -by, -ce,  bl, -br,  ck,  bs, -bk,  cd,  bz, -bg,  bw,  cg, -bn,  bp, -ci, -bu,  bi, -cb },
    { ba, -ar,  av, -be, -aw,  aq, -az, -bb,  as, -au,  bd,  ax, -ap,  ay,  bc, -at,  at, -bc, -ay,  ap, -ax, -bd,  au, -as,  bb,  az, -aq,  aw,  be, -av,  ar, -ba, -ba,  ar, -av,  be,  aw, -aq,  az,  bb, -as,  au, -bd, -ax,  ap, -ay, -bc,  at, -at,  bc,  ay, -ap,  ax,  bd, -au,  as, -bb, -az,  aq, -aw, -be,  av, -ar,  ba },
    { cc, -bl,  bp, -cg, -by,  bh, -bt,  ck,  bu, -bg,  bx,  ch, -bq,  bk, -cb, -cd,  bm, -bo,  cf,  bz, -bi,  bs, -cj, -bv,  bf, -bw, -ci,  br, -bj,  ca,  ce, -bn,  bn, -ce, -ca,  bj, -br,  ci,  bw, -bf,  bv,  cj, -bs,  bi, -bz, -cf,  bo, -bm,  cd,  cb, -bk,  bq, -ch, -bx,  bg, -bu, -ck,  bt, -bh,  by,  cg, -bp,  bl, -cc },
    { ac, -ab,  ab, -ac, -ac,  ab, -ab,  ac,  ac, -ab,  ab, -ac, -ac,  ab, -ab,  ac,  ac, -ab,  ab, -ac, -ac,  ab, -ab,  ac,  ac, -ab,  ab, -ac, -ac,  ab, -ab,  ac,  ac, -ab,  ab, -ac, -ac,  ab, -ab,  ac,  ac, -ab,  ab, -ac, -ac,  ab, -ab,  ac,  ac, -ab,  ab, -ac, -ac,  ab, -ab,  ac,  ac, -ab,  ab, -ac, -ac,  ab, -ab,  ac },
    { cd, -bo,  bk, -bz, -ch,  bs, -bg,  bv, -ck, -bw,  bh, -br,  cg,  ca, -bl,  bn, -cc, -ce,  bp, -bj,  by,  ci, -bt,  bf, -bu,  cj,  bx, -bi,  bq, -cf, -cb,  bm, -bm,  cb,  cf, -bq,  bi, -bx, -cj,  bu, -bf,  bt, -ci, -by,  bj, -bp,  ce,  cc, -bn,  bl, -ca, -cg,  br, -bh,  bw,  ck, -bv,  bg, -bs,  ch,  bz, -bk,  bo, -cd },
    { bb, -au,  aq, -ax,  be,  ay, -ar,  at, -ba, -bc,  av, -ap,  aw, -bd, -az,  as, -as,  az,  bd, -aw,  ap, -av,  bc,  ba, -at,  ar, -ay, -be,  ax, -aq,  au, -bb, -bb,  au, -aq,  ax, -be, -ay,  ar, -at,  ba,  bc, -av,  ap, -aw,  bd,  az, -as,  as, -az, -bd,  aw, -ap,  av, -bc, -ba,  at, -ar,  ay,  be, -ax,  aq, -au,  bb },
    { ce, -br,  bf, -bs,  cf,  cd, -bq,  bg, -bt,  cg,  cc, -bp,  bh, -bu,  ch,  cb, -bo,  bi, -bv,  ci,  ca, -bn,  bj, -bw,  cj,  bz, -bm,  bk, -bx,  ck,  by, -bl,  bl, -by, -ck,  bx, -bk,  bm, -bz, -cj,  bw, -bj,  bn, -ca, -ci,  bv, -bi,  bo, -cb, -ch,  bu, -bh,  bp, -cc, -cg,  bt, -bg,  bq, -cd, -cf,  bs, -bf,  br, -ce },
    { an, -ak,  ah, -aj,  am,  ao, -al,  ai, -ai,  al, -ao, -am,  aj, -ah,  ak, -an, -an,  ak, -ah,  aj, -am, -ao,  al, -ai,  ai, -al,  ao,  am, -aj,  ah, -ak,  an,  an, -ak,  ah, -aj,  am,  ao, -al,  ai, -ai,  al, -ao, -am,  aj, -ah,  ak, -an, -an,  ak, -ah,  aj, -am, -ao,  al, -ai,  ai, -al,  ao,  am, -aj,  ah, -ak,  an },
    { cf, -bu,  bj, -bl,  bw, -ch, -cd,  bs, -bh,  bn, -by,  cj,  cb, -bq,  bf, -bp,  ca,  ck, -bz,  bo, -bg,  br, -cc, -ci,  bx, -bm,  bi, -bt,  ce,  cg, -bv,  bk, -bk,  bv, -cg, -ce,  bt, -bi,  bm, -bx,  ci,  cc, -br,  bg, -bo,  bz, -ck, -ca,  bp, -bf,  bq, -cb, -cj,  by, -bn,  bh, -bs,  cd,  ch, -bw,  bl, -bj,  bu, -cf },
    { bc, -ax,  as, -aq,  av, -ba, -be,  az, -au,  ap, -at,  ay, -bd, -bb,  aw, -ar,  ar, -aw,  bb,  bd, -ay,  at, -ap,  au, -az,  be,  ba, -av,  aq, -as,  ax, -bc, -bc,  ax, -as,  aq, -av,  ba,  be, -az,  au, -ap,  at, -ay,  bd,  bb, -aw,  ar, -ar,  aw, -bb, -bd,  ay, -at,  ap, -au,  az, -be, -ba,  av, -aq,  as, -ax,  bc },
    { cg, -bx,  bo, -bf,  bn, -bw,  cf,  ch, -by,  bp, -bg,  bm, -bv,  ce,  ci, -bz,  bq, -bh,  bl, -bu,  cd,  cj, -ca,  br, -bi,  bk, -bt,  cc,  ck, -cb,  bs, -bj,  bj, -bs,  cb, -ck, -cc,  bt, -bk,  bi, -br,  ca, -cj, -cd,  bu, -bl,  bh, -bq,  bz, -ci, -ce,  bv, -bm,  bg, -bp,  by, -ch, -cf,  bw, -bn,  bf, -bo,  bx, -cg },
    { ag, -af,  ae, -ad,  ad, -ae,  af, -ag, -ag,  af, -ae,  ad, -ad,  ae, -af,  ag,  ag, -af,  ae, -ad,  ad, -ae,  af, -ag, -ag,  af, -ae,  ad, -ad,  ae, -af,  ag,  ag, -af,  ae, -ad,  ad, -ae,  af, -ag, -ag,  af, -ae,  ad, -ad,  ae, -af,  ag,  ag, -af,  ae, -ad,  ad, -ae,  af, -ag, -ag,  af, -ae,  ad, -ad,  ae, -af,  ag },
    { ch, -ca,  bt, -bm,  bf, -bl,  bs, -bz,  cg,  ci, -cb,  bu, -bn,  bg, -bk,  br, -by,  cf,  cj, -cc,  bv, -bo,  bh, -bj,  bq, -bx,  ce,  ck, -cd,  bw, -bp,  bi, -bi,  bp, -bw,  cd, -ck, -ce,  bx, -bq,  bj, -bh,  bo, -bv,  cc, -cj, -cf,  by, -br,  bk, -bg,  bn, -bu,  cb, -ci, -cg,  bz, -bs,  bl, -bf,  bm, -bt,  ca, -ch },
    { bd, -ba,  ax, -au,  ar, -ap,  as, -av,  ay, -bb,  be,  bc, -az,  aw, -at,  aq, -aq,  at, -aw,  az, -bc, -be,  bb, -ay,  av, -as,  ap, -ar,  au, -ax,  ba, -bd, -bd,  ba, -ax,  au, -ar,  ap, -as,  av, -ay,  bb, -be, -bc,  az, -aw,  at, -aq,  aq, -at,  aw, -az,  bc,  be, -bb,  ay, -av,  as, -ap,  ar, -au,  ax, -ba,  bd },
    { ci, -cd,  by, -bt,  bo, -bj,  bf, -bk,  bp, -bu,  bz, -ce,  cj,  ch, -cc,  bx, -bs,  bn, -bi,  bg, -bl,  bq, -bv,  ca, -cf,  ck,  cg, -cb,  bw, -br,  bm, -bh,  bh, -bm,  br, -bw,  cb, -cg, -ck,  cf, -ca,  bv, -bq,  bl, -bg,  bi, -bn,  bs, -bx,  cc, -ch, -cj,  ce, -bz,  bu, -bp,  bk, -bf,  bj, -bo,  bt, -by,  cd, -ci },
    { ao, -an,  am, -al,  ak, -aj,  ai, -ah,  ah, -ai,  aj, -ak,  al, -am,  an, -ao, -ao,  an, -am,  al, -ak,  aj, -ai,  ah, -ah,  ai, -aj,  ak, -al,  am, -an,  ao,  ao, -an,  am, -al,  ak, -aj,  ai, -ah,  ah, -ai,  aj, -ak,  al, -am,  an, -ao, -ao,  an, -am,  al, -ak,  aj, -ai,  ah, -ah,  ai, -aj,  ak, -al,  am, -an,  ao },
    { cj, -cg,  cd, -ca,  bx, -bu,  br, -bo,  bl, -bi,  bf, -bh,  bk, -bn,  bq, -bt,  bw, -bz,  cc, -cf,  ci,  ck, -ch,  ce, -cb,  by, -bv,  bs, -bp,  bm, -bj,  bg, -bg,  bj, -bm,  bp, -bs,  bv, -by,  cb, -ce,  ch, -ck, -ci,  cf, -cc,  bz, -bw,  bt, -bq,  bn, -bk,  bh, -bf,  bi, -bl,  bo, -br,  bu, -bx,  ca, -cd,  cg, -cj },
    { be, -bd,  bc, -bb,  ba, -az,  ay, -ax,  aw, -av,  au, -at,  as, -ar,  aq, -ap,  ap, -aq,  ar, -as,  at, -au,  av, -aw,  ax, -ay,  az, -ba,  bb, -bc,  bd, -be, -be,  bd, -bc,  bb, -ba,  az, -ay,  ax, -aw,  av, -au,  at, -as,  ar, -aq,  ap, -ap,  aq, -ar,  as, -at,  au, -av,  aw, -ax,  ay, -az,  ba, -bb,  bc, -bd,  be },
    { ck, -cj,  ci, -ch,  cg, -cf,  ce, -cd,  cc, -cb,  ca, -bz,  by, -bx,  bw, -bv,  bu, -bt,  bs, -br,  bq, -bp,  bo, -bn,  bm, -bl,  bk, -bj,  bi, -bh,  bg, -bf,  bf, -bg,  bh, -bi,  bj, -bk,  bl, -bm,  bn, -bo,  bp, -bq,  br, -bs,  bt, -bu,  bv, -bw,  bx, -by,  bz, -ca,  cb, -cc,  cd, -ce,  cf, -cg,  ch, -ci,  cj, -ck },
}
 */

void ff_vvc_inv_dct2_64(int *coeffs, const ptrdiff_t stride, const size_t nz)
{
    const int aa = 64, ab = 83, ac = 36, ad = 89, ae = 75, af = 50, ag = 18, ah = 90;
    const int ai = 87, aj = 80, ak = 70, al = 57, am = 43, an = 25, ao =  9, ap = 90;
    const int aq = 90, ar = 88, as = 85, at = 82, au = 78, av = 73, aw = 67, ax = 61;
    const int ay = 54, az = 46, ba = 38, bb = 31, bc = 22, bd = 13, be =  4, bf = 91;
    const int bg = 90, bh = 90, bi = 90, bj = 88, bk = 87, bl = 86, bm = 84, bn = 83;
    const int bo = 81, bp = 79, bq = 77, br = 73, bs = 71, bt = 69, bu = 65, bv = 62;
    const int bw = 59, bx = 56, by = 52, bz = 48, ca = 44, cb = 41, cc = 37, cd = 33;
    const int ce = 28, cf = 24, cg = 20, ch = 15, ci = 11, cj =  7, ck =  2;
    const int x0  = coeffs[0  * stride], x1  = coeffs[1  * stride];
    const int x2  = coeffs[2  * stride], x3  = coeffs[3  * stride];
    const int x4  = coeffs[4  * stride], x5  = coeffs[5  * stride];
    const int x6  = coeffs[6  * stride], x7  = coeffs[7  * stride];
    const int x8  = coeffs[8  * stride], x9  = coeffs[9  * stride];
    const int x10 = coeffs[10 * stride], x11 = coeffs[11 * stride];
    const int x12 = coeffs[12 * stride], x13 = coeffs[13 * stride];
    const int x14 = coeffs[14 * stride], x15 = coeffs[15 * stride];
    const int x16 = coeffs[16 * stride], x17 = coeffs[17 * stride];
    const int x18 = coeffs[18 * stride], x19 = coeffs[19 * stride];
    const int x20 = coeffs[20 * stride], x21 = coeffs[21 * stride];
    const int x22 = coeffs[22 * stride], x23 = coeffs[23 * stride];
    const int x24 = coeffs[24 * stride], x25 = coeffs[25 * stride];
    const int x26 = coeffs[26 * stride], x27 = coeffs[27 * stride];
    const int x28 = coeffs[28 * stride], x29 = coeffs[29 * stride];
    const int x30 = coeffs[30 * stride], x31 = coeffs[31 * stride];
    //according to vvc specification, x31 to x63 are zeros
    const int EEEEE[2] = {
        aa * x0,
        aa * x0,
    };
    const int EEEEO[2] = {
        G16(ab * x16),
        G16(ac * x16),
    };
    const int EEEE[4] = {
        EEEEE[0] + EEEEO[0], EEEEE[1] + EEEEO[1],
        EEEEE[1] - EEEEO[1], EEEEE[0] - EEEEO[0],
    };
    const int EEEO[4] = {
        G8(ad * x8)  + G16(+ae * x24),
        G8(ae * x8)  + G16(-ag * x24),
        G8(af * x8)  + G16(-ad * x24),
        G8(ag * x8)  + G16(-af * x24),
    };
    const int EEE[8] = {
        EEEE[0] + EEEO[0], EEEE[1] + EEEO[1], EEEE[2] + EEEO[2], EEEE[3] + EEEO[3],
        EEEE[3] - EEEO[3], EEEE[2] - EEEO[2], EEEE[1] - EEEO[1], EEEE[0] - EEEO[0],
    };
    const int EEO[8] = {
        G4(ah * x4) + G8(+ai * x12) + G16(+aj * x20 + ak * x28),
        G4(ai * x4) + G8(+al * x12) + G16(+ao * x20 - am * x28),
        G4(aj * x4) + G8(+ao * x12) + G16(-ak * x20 - ai * x28),
        G4(ak * x4) + G8(-am * x12) + G16(-ai * x20 + ao * x28),
        G4(al * x4) + G8(-aj * x12) + G16(-an * x20 + ah * x28),
        G4(am * x4) + G8(-ah * x12) + G16(+al * x20 + an * x28),
        G4(an * x4) + G8(-ak * x12) + G16(+ah * x20 - aj * x28),
        G4(ao * x4) + G8(-an * x12) + G16(+am * x20 - al * x28),
    };
    const int EE[16] = {
        EEE[0] + EEO[0], EEE[1] + EEO[1], EEE[2] + EEO[2], EEE[3] + EEO[3], EEE[4] + EEO[4], EEE[5] + EEO[5], EEE[6] + EEO[6], EEE[7] + EEO[7],
        EEE[7] - EEO[7], EEE[6] - EEO[6], EEE[5] - EEO[5], EEE[4] - EEO[4], EEE[3] - EEO[3], EEE[2] - EEO[2], EEE[1] - EEO[1], EEE[0] - EEO[0],
    };
    const int EO[16] = {
        G2(ap * x2) + G4(+aq * x6) + G8(+ar * x10 + as * x14) + G16(+at * x18 + au * x22 + av * x26 + aw * x30),
        G2(aq * x2) + G4(+at * x6) + G8(+aw * x10 + az * x14) + G16(+bc * x18 - be * x22 - bb * x26 - ay * x30),
        G2(ar * x2) + G4(+aw * x6) + G8(+bb * x10 - bd * x14) + G16(-ay * x18 - at * x22 - ap * x26 - au * x30),
        G2(as * x2) + G4(+az * x6) + G8(-bd * x10 - aw * x14) + G16(-ap * x18 - av * x22 - bc * x26 + ba * x30),
        G2(at * x2) + G4(+bc * x6) + G8(-ay * x10 - ap * x14) + G16(-ax * x18 + bd * x22 + au * x26 + as * x30),
        G2(au * x2) + G4(-be * x6) + G8(-at * x10 - av * x14) + G16(+bd * x18 + as * x22 + aw * x26 - bc * x30),
        G2(av * x2) + G4(-bb * x6) + G8(-ap * x10 - bc * x14) + G16(+au * x18 + aw * x22 - ba * x26 - aq * x30),
        G2(aw * x2) + G4(-ay * x6) + G8(-au * x10 + ba * x14) + G16(+as * x18 - bc * x22 - aq * x26 + be * x30),
        G2(ax * x2) + G4(-av * x6) + G8(-az * x10 + at * x14) + G16(+bb * x18 - ar * x22 - bd * x26 + ap * x30),
        G2(ay * x2) + G4(-as * x6) + G8(-be * x10 + ar * x14) + G16(-az * x18 - ax * x22 + at * x26 + bd * x30),
        G2(az * x2) + G4(-ap * x6) + G8(+ba * x10 + ay * x14) + G16(-aq * x18 + bb * x22 + ax * x26 - ar * x30),
        G2(ba * x2) + G4(-ar * x6) + G8(+av * x10 - be * x14) + G16(-aw * x18 + aq * x22 - az * x26 - bb * x30),
        G2(bb * x2) + G4(-au * x6) + G8(+aq * x10 - ax * x14) + G16(+be * x18 + ay * x22 - ar * x26 + at * x30),
        G2(bc * x2) + G4(-ax * x6) + G8(+as * x10 - aq * x14) + G16(+av * x18 - ba * x22 - be * x26 + az * x30),
        G2(bd * x2) + G4(-ba * x6) + G8(+ax * x10 - au * x14) + G16(+ar * x18 - ap * x22 + as * x26 - av * x30),
        G2(be * x2) + G4(-bd * x6) + G8(+bc * x10 - bb * x14) + G16(+ba * x18 - az * x22 + ay * x26 - ax * x30),
    };
    const int E[32] = {
        EE[0]  + EO[0],  EE[1]  + EO[1],  EE[2]  + EO[2],  EE[3]  + EO[3],  EE[4]  + EO[4],  EE[5]  + EO[5],  EE[6] + EO[6], EE[7] + EO[7], EE[8] + EO[8], EE[9] + EO[9], EE[10] + EO[10], EE[11] + EO[11], EE[12] + EO[12], EE[13] + EO[13], EE[14] + EO[14], EE[15] + EO[15],
        EE[15] - EO[15], EE[14] - EO[14], EE[13] - EO[13], EE[12] - EO[12], EE[11] - EO[11], EE[10] - EO[10], EE[9] - EO[9], EE[8] - EO[8], EE[7] - EO[7], EE[6] - EO[6], EE[5]  - EO[5],  EE[4]  - EO[4],  EE[3]  - EO[3],  EE[2]  - EO[2],  EE[1]  - EO[1],  EE[0]  - EO[0],
    };
    const int O[32] = {
        bf * x1 + G2(+bg * x3) + G4(+bh * x5 + bi * x7) + G8(+bj * x9 + bk * x11 + bl * x13 + bm * x15) + G16(+bn * x17 + bo * x19 + bp * x21 + bq * x23 +  br * x25 + bs * x27 + bt * x29 + bu * x31),
        bg * x1 + G2(+bj * x3) + G4(+bm * x5 + bp * x7) + G8(+bs * x9 + bv * x11 + by * x13 + cb * x15) + G16(+ce * x17 + ch * x19 + ck * x21 - ci * x23 + -cf * x25 - cc * x27 - bz * x29 - bw * x31),
        bh * x1 + G2(+bm * x3) + G4(+br * x5 + bw * x7) + G8(+cb * x9 + cg * x11 - ck * x13 - cf * x15) + G16(-ca * x17 - bv * x19 - bq * x21 - bl * x23 + -bg * x25 - bi * x27 - bn * x29 - bs * x31),
        bi * x1 + G2(+bp * x3) + G4(+bw * x5 + cd * x7) + G8(+ck * x9 - ce * x11 - bx * x13 - bq * x15) + G16(-bj * x17 - bh * x19 - bo * x21 - bv * x23 + -cc * x25 - cj * x27 + cf * x29 + by * x31),
        bj * x1 + G2(+bs * x3) + G4(+cb * x5 + ck * x7) + G8(-cc * x9 - bt * x11 - bk * x13 - bi * x15) + G16(-br * x17 - ca * x19 - cj * x21 + cd * x23 +  bu * x25 + bl * x27 + bh * x29 + bq * x31),
        bk * x1 + G2(+bv * x3) + G4(+cg * x5 - ce * x7) + G8(-bt * x9 - bi * x11 - bm * x13 - bx * x15) + G16(-ci * x17 + cc * x19 + br * x21 + bg * x23 +  bo * x25 + bz * x27 + ck * x29 - ca * x31),
        bl * x1 + G2(+by * x3) + G4(-ck * x5 - bx * x7) + G8(-bk * x9 - bm * x11 - bz * x13 + cj * x15) + G16(+bw * x17 + bj * x19 + bn * x21 + ca * x23 + -ci * x25 - bv * x27 - bi * x29 - bo * x31),
        bm * x1 + G2(+cb * x3) + G4(-cf * x5 - bq * x7) + G8(-bi * x9 - bx * x11 + cj * x13 + bu * x15) + G16(+bf * x17 + bt * x19 + ci * x21 - by * x23 + -bj * x25 - bp * x27 - ce * x29 + cc * x31),
        bn * x1 + G2(+ce * x3) + G4(-ca * x5 - bj * x7) + G8(-br * x9 - ci * x11 + bw * x13 + bf * x15) + G16(+bv * x17 - cj * x19 - bs * x21 - bi * x23 + -bz * x25 + cf * x27 + bo * x29 + bm * x31),
        bo * x1 + G2(+ch * x3) + G4(-bv * x5 - bh * x7) + G8(-ca * x9 + cc * x11 + bj * x13 + bt * x15) + G16(-cj * x17 - bq * x19 - bm * x21 - cf * x23 +  bx * x25 + bf * x27 + by * x29 - ce * x31),
        bp * x1 + G2(+ck * x3) + G4(-bq * x5 - bo * x7) + G8(-cj * x9 + br * x11 + bn * x13 + ci * x15) + G16(-bs * x17 - bm * x19 - ch * x21 + bt * x23 +  bl * x25 + cg * x27 - bu * x29 - bk * x31),
        bq * x1 + G2(-ci * x3) + G4(-bl * x5 - bv * x7) + G8(+cd * x9 + bg * x11 + ca * x13 - by * x15) + G16(-bi * x17 - cf * x19 + bt * x21 + bn * x23 +  ck * x25 - bo * x27 - bs * x29 + cg * x31),
        br * x1 + G2(-cf * x3) + G4(-bg * x5 - cc * x7) + G8(+bu * x9 + bo * x11 - ci * x13 - bj * x15) + G16(-bz * x17 + bx * x19 + bl * x21 + ck * x23 + -bm * x25 - bw * x27 + ca * x29 + bi * x31),
        bs * x1 + G2(-cc * x3) + G4(-bi * x5 - cj * x7) + G8(+bl * x9 + bz * x11 - bv * x13 - bp * x15) + G16(+cf * x17 + bf * x19 + cg * x21 - bo * x23 + -bw * x25 + by * x27 + bm * x29 - ci * x31),
        bt * x1 + G2(-bz * x3) + G4(-bn * x5 + cf * x7) + G8(+bh * x9 + ck * x11 - bi * x13 - ce * x15) + G16(+bo * x17 + by * x19 - bu * x21 - bs * x23 +  ca * x25 + bm * x27 - cg * x29 - bg * x31),
        bu * x1 + G2(-bw * x3) + G4(-bs * x5 + by * x7) + G8(+bq * x9 - ca * x11 - bo * x13 + cc * x15) + G16(+bm * x17 - ce * x19 - bk * x21 + cg * x23 +  bi * x25 - ci * x27 - bg * x29 + ck * x31),
        bv * x1 + G2(-bt * x3) + G4(-bx * x5 + br * x7) + G8(+bz * x9 - bp * x11 - cb * x13 + bn * x15) + G16(+cd * x17 - bl * x19 - cf * x21 + bj * x23 +  ch * x25 - bh * x27 - cj * x29 + bf * x31),
        bw * x1 + G2(-bq * x3) + G4(-cc * x5 + bk * x7) + G8(+ci * x9 - bf * x11 + ch * x13 + bl * x15) + G16(-cb * x17 - br * x19 + bv * x21 + bx * x23 + -bp * x25 - cd * x27 + bj * x29 + cj * x31),
        bx * x1 + G2(-bn * x3) + G4(-ch * x5 + bg * x7) + G8(-ce * x9 - bq * x11 + bu * x13 + ca * x15) + G16(-bk * x17 - ck * x19 + bj * x21 - cb * x23 + -bt * x25 + br * x27 + cd * x29 - bh * x31),
        by * x1 + G2(-bk * x3) + G4(+cj * x5 + bn * x7) + G8(-bv * x9 - cb * x11 + bh * x13 - cg * x15) + G16(-bq * x17 + bs * x19 + ce * x21 - bf * x23 +  cd * x25 + bt * x27 - bp * x29 - ch * x31),
        bz * x1 + G2(-bh * x3) + G4(+ce * x5 + bu * x7) + G8(-bm * x9 + cj * x11 + bp * x13 - br * x15) + G16(-ch * x17 + bk * x19 - bw * x21 - cc * x23 +  bf * x25 - cb * x27 - bx * x29 + bj * x31),
        ca * x1 + G2(-bf * x3) + G4(+bz * x5 + cb * x7) + G8(-bg * x9 + by * x11 + cc * x13 - bh * x15) + G16(+bx * x17 + cd * x19 - bi * x21 + bw * x23 +  ce * x25 - bj * x27 + bv * x29 + cf * x31),
        cb * x1 + G2(-bi * x3) + G4(+bu * x5 + ci * x7) + G8(-bp * x9 + bn * x11 - cg * x13 - bw * x15) + G16(+bg * x17 - bz * x19 - cd * x21 + bk * x23 + -bs * x25 - ck * x27 + br * x29 - bl * x31),
        cc * x1 + G2(-bl * x3) + G4(+bp * x5 - cg * x7) + G8(-by * x9 + bh * x11 - bt * x13 + ck * x15) + G16(+bu * x17 - bg * x19 + bx * x21 + ch * x23 + -bq * x25 + bk * x27 - cb * x29 - cd * x31),
        cd * x1 + G2(-bo * x3) + G4(+bk * x5 - bz * x7) + G8(-ch * x9 + bs * x11 - bg * x13 + bv * x15) + G16(-ck * x17 - bw * x19 + bh * x21 - br * x23 +  cg * x25 + ca * x27 - bl * x29 + bn * x31),
        ce * x1 + G2(-br * x3) + G4(+bf * x5 - bs * x7) + G8(+cf * x9 + cd * x11 - bq * x13 + bg * x15) + G16(-bt * x17 + cg * x19 + cc * x21 - bp * x23 +  bh * x25 - bu * x27 + ch * x29 + cb * x31),
        cf * x1 + G2(-bu * x3) + G4(+bj * x5 - bl * x7) + G8(+bw * x9 - ch * x11 - cd * x13 + bs * x15) + G16(-bh * x17 + bn * x19 - by * x21 + cj * x23 +  cb * x25 - bq * x27 + bf * x29 - bp * x31),
        cg * x1 + G2(-bx * x3) + G4(+bo * x5 - bf * x7) + G8(+bn * x9 - bw * x11 + cf * x13 + ch * x15) + G16(-by * x17 + bp * x19 - bg * x21 + bm * x23 + -bv * x25 + ce * x27 + ci * x29 - bz * x31),
        ch * x1 + G2(-ca * x3) + G4(+bt * x5 - bm * x7) + G8(+bf * x9 - bl * x11 + bs * x13 - bz * x15) + G16(+cg * x17 + ci * x19 - cb * x21 + bu * x23 + -bn * x25 + bg * x27 - bk * x29 + br * x31),
        ci * x1 + G2(-cd * x3) + G4(+by * x5 - bt * x7) + G8(+bo * x9 - bj * x11 + bf * x13 - bk * x15) + G16(+bp * x17 - bu * x19 + bz * x21 - ce * x23 +  cj * x25 + ch * x27 - cc * x29 + bx * x31),
        cj * x1 + G2(-cg * x3) + G4(+cd * x5 - ca * x7) + G8(+bx * x9 - bu * x11 + br * x13 - bo * x15) + G16(+bl * x17 - bi * x19 + bf * x21 - bh * x23 +  bk * x25 - bn * x27 + bq * x29 - bt * x31),
        ck * x1 + G2(-cj * x3) + G4(+ci * x5 - ch * x7) + G8(+cg * x9 - cf * x11 + ce * x13 - cd * x15) + G16(+cc * x17 - cb * x19 + ca * x21 - bz * x23 +  by * x25 - bx * x27 + bw * x29 - bv * x31),
    };
    coeffs[0  * stride] = E[0 ] + O[0 ];
    coeffs[1  * stride] = E[1 ] + O[1 ];
    coeffs[2  * stride] = E[2 ] + O[2 ];
    coeffs[3  * stride] = E[3 ] + O[3 ];
    coeffs[4  * stride] = E[4 ] + O[4 ];
    coeffs[5  * stride] = E[5 ] + O[5 ];
    coeffs[6  * stride] = E[6 ] + O[6 ];
    coeffs[7  * stride] = E[7 ] + O[7 ];
    coeffs[8  * stride] = E[8 ] + O[8 ];
    coeffs[9  * stride] = E[9 ] + O[9 ];
    coeffs[10 * stride] = E[10] + O[10];
    coeffs[11 * stride] = E[11] + O[11];
    coeffs[12 * stride] = E[12] + O[12];
    coeffs[13 * stride] = E[13] + O[13];
    coeffs[14 * stride] = E[14] + O[14];
    coeffs[15 * stride] = E[15] + O[15];
    coeffs[16 * stride] = E[16] + O[16];
    coeffs[17 * stride] = E[17] + O[17];
    coeffs[18 * stride] = E[18] + O[18];
    coeffs[19 * stride] = E[19] + O[19];
    coeffs[20 * stride] = E[20] + O[20];
    coeffs[21 * stride] = E[21] + O[21];
    coeffs[22 * stride] = E[22] + O[22];
    coeffs[23 * stride] = E[23] + O[23];
    coeffs[24 * stride] = E[24] + O[24];
    coeffs[25 * stride] = E[25] + O[25];
    coeffs[26 * stride] = E[26] + O[26];
    coeffs[27 * stride] = E[27] + O[27];
    coeffs[28 * stride] = E[28] + O[28];
    coeffs[29 * stride] = E[29] + O[29];
    coeffs[30 * stride] = E[30] + O[30];
    coeffs[31 * stride] = E[31] + O[31];
    coeffs[32 * stride] = E[31] - O[31];
    coeffs[33 * stride] = E[30] - O[30];
    coeffs[34 * stride] = E[29] - O[29];
    coeffs[35 * stride] = E[28] - O[28];
    coeffs[36 * stride] = E[27] - O[27];
    coeffs[37 * stride] = E[26] - O[26];
    coeffs[38 * stride] = E[25] - O[25];
    coeffs[39 * stride] = E[24] - O[24];
    coeffs[40 * stride] = E[23] - O[23];
    coeffs[41 * stride] = E[22] - O[22];
    coeffs[42 * stride] = E[21] - O[21];
    coeffs[43 * stride] = E[20] - O[20];
    coeffs[44 * stride] = E[19] - O[19];
    coeffs[45 * stride] = E[18] - O[18];
    coeffs[46 * stride] = E[17] - O[17];
    coeffs[47 * stride] = E[16] - O[16];
    coeffs[48 * stride] = E[15] - O[15];
    coeffs[49 * stride] = E[14] - O[14];
    coeffs[50 * stride] = E[13] - O[13];
    coeffs[51 * stride] = E[12] - O[12];
    coeffs[52 * stride] = E[11] - O[11];
    coeffs[53 * stride] = E[10] - O[10];
    coeffs[54 * stride] = E[9]  - O[9];
    coeffs[55 * stride] = E[8]  - O[8];
    coeffs[56 * stride] = E[7]  - O[7];
    coeffs[57 * stride] = E[6]  - O[6];
    coeffs[58 * stride] = E[5]  - O[5];
    coeffs[59 * stride] = E[4]  - O[4];
    coeffs[60 * stride] = E[3]  - O[3];
    coeffs[61 * stride] = E[2]  - O[2];
    coeffs[62 * stride] = E[1]  - O[1];
    coeffs[63 * stride] = E[0]  - O[0];
}

static void matrix_mul(int *coeffs, const ptrdiff_t stride, const int8_t* matrix, const int size, const size_t nz)
{
    //for dst7 and dct8, coeffs > 16 are zero out
    int tmp[16];

    for (int i = 0; i < nz; i++)
        tmp[i] = coeffs[i * stride];

    for (int i = 0; i < size; i++) {
         int o = 0;

         for (int j = 0; j < nz; j++)
              o += tmp[j] * matrix[j * size];
         *coeffs = o;
         coeffs += stride;
         matrix++;
    }
}

static void inv_dct8(int *coeffs, const ptrdiff_t stride, const int8_t *matrix, const int size, const size_t nz)
{
    matrix_mul(coeffs, stride, matrix, size, nz);
}

#define DEFINE_INV_DCT8_1D(S)                                                   \
void ff_vvc_inv_dct8_ ## S(int *coeffs, const ptrdiff_t stride, const size_t nz) \
{                                                                               \
    inv_dct8(coeffs, stride, &ff_vvc_dct8_##S##x##S[0][0], S, nz);               \
}

DEFINE_INV_DCT8_1D( 4)
DEFINE_INV_DCT8_1D( 8)
DEFINE_INV_DCT8_1D(16)
DEFINE_INV_DCT8_1D(32)

static void inv_dst7(int *coeffs, const ptrdiff_t stride, const int8_t *matrix, const int size, const size_t nz)
{
    matrix_mul(coeffs, stride, matrix, size, nz);
}

#define DEFINE_INV_DST7_1D(S)                                                   \
void ff_vvc_inv_dst7_ ## S(int *coeffs, const ptrdiff_t stride, const size_t nz) \
{                                                                               \
    inv_dst7(coeffs, stride, &ff_vvc_dst7_##S##x##S[0][0], S, nz);               \
}

DEFINE_INV_DST7_1D( 4)
DEFINE_INV_DST7_1D( 8)
DEFINE_INV_DST7_1D(16)
DEFINE_INV_DST7_1D(32)

void ff_vvc_inv_lfnst_1d(int *v, const int *u, int no_zero_size, int n_tr_s,
    int pred_mode_intra, int lfnst_idx, int log2_transform_range)
{
     int lfnst_tr_set_idx    = pred_mode_intra < 0 ? 1 : ff_vvc_lfnst_tr_set_index[pred_mode_intra];
     const int8_t *tr_mat = n_tr_s > 16 ? ff_vvc_lfnst_8x8[lfnst_tr_set_idx][lfnst_idx-1][0] : ff_vvc_lfnst_4x4[lfnst_tr_set_idx][lfnst_idx - 1][0];

     for (int j = 0; j < n_tr_s; j++, tr_mat++) {
        int t = 0;

        for (int i = 0; i < no_zero_size; i++)
            t += u[i] * tr_mat[i * n_tr_s];
        v[j] = av_clip_intp2((t + 64) >> 7 , log2_transform_range);
     }
}
