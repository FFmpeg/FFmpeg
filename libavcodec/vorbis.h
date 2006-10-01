/*
 * copyright (c) 2006 Oded Shimon <ods15@ods15.dyndns.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef VORBIS_H
#define VORBIS_H

#include "avcodec.h"

extern const float ff_vorbis_floor1_inverse_db_table[256];
extern const float * ff_vorbis_vwin[8];

typedef struct {
    uint_fast16_t x;
    uint_fast16_t sort;
    uint_fast16_t low;
    uint_fast16_t high;
} floor1_entry_t;

void ff_vorbis_ready_floor1_list(floor1_entry_t * list, int values);
unsigned int ff_vorbis_nth_root(unsigned int x, unsigned int n); // x^(1/n)
int ff_vorbis_len2vlc(uint8_t *bits, uint32_t *codes, uint_fast32_t num);

static void attribute_unused render_line(int x0, int y0, int x1, int y1, float * buf, int n) {
    int dy = y1 - y0;
    int adx = x1 - x0;
    int ady = ABS(dy);
    int base = dy / adx;
    int x = x0;
    int y = y0;
    int err = 0;
    int sy;
    if (dy < 0) sy = base - 1;
    else        sy = base + 1;
    ady = ady - ABS(base) * adx;
    if (x >= n) return;
    buf[x] = ff_vorbis_floor1_inverse_db_table[y];
    for (x = x0 + 1; x < x1; x++) {
        if (x >= n) return;
        err += ady;
        if (err >= adx) {
            err -= adx;
            y += sy;
        } else {
            y += base;
        }
        buf[x] = ff_vorbis_floor1_inverse_db_table[y];
    }
}

#define ilog(i) av_log2(2*(i))

#endif
