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

extern const float ff_vorbis_floor1_inverse_db_table[256];
extern const float * ff_vorbis_vwin[8];

typedef struct {
    uint_fast16_t x;
    uint_fast16_t sort;
    uint_fast16_t low;
    uint_fast16_t high;
} floor1_entry_t;

void ff_vorbis_ready_floor1_list(floor1_entry_t * list, int values);

#endif
