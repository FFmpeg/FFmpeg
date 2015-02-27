/*
 * Helpers for generating hard-coded VLC tables
 *
 * Copyright (c) 2014 Reimar Döffinger <Reimar.Doeffinger@gmx.de>
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

#ifndef AVCODEC_TABLEPRINT_VLC_H
#define AVCODEC_TABLEPRINT_VLC_H

#define FFMPEG_CONFIG_H
#define AVUTIL_LOG_H
#define av_log(a, ...)
#define av_dlog(a, ...)
#define AVUTIL_MEM_H
#define av_malloc(s) NULL
#define av_malloc_array(a, b) NULL
#define av_realloc_f(p, o, n) NULL
#define av_free(p)
#define av_freep(p)
#define AVCODEC_AVCODEC_H
#include "tableprint.h"
#include "get_bits.h"
#include "mathtables.c"
#include "bitstream.c"

#define REPLACE_DEFINE2(type) write_##type##_array
#define REPLACE_DEFINE(type) REPLACE_DEFINE2(type)
static void write_VLC_TYPE_array(const VLC_TYPE *p, int s) {
    REPLACE_DEFINE(VLC_TYPE)(p, s);
}

WRITE_2D_FUNC(VLC_TYPE)

static void write_vlc_type(const VLC *vlc, VLC_TYPE (*base_table)[2], const char *base_table_name)
{
    printf("    .bits = %i,\n", vlc->bits);
    // Unfortunately need to cast away const currently
    printf("    .table = (VLC_TYPE (*)[2])(%s + 0x%x),\n", base_table_name, (int)(vlc->table - base_table));
    printf("    .table_size = 0x%x,\n", vlc->table_size);
    printf("    .table_allocated = 0x%x,\n", vlc->table_allocated);
}

#define WRITE_VLC_TYPE(prefix, name, base_table)        \
    do {                                                \
        printf(prefix" VLC "#name" = {\n");             \
        write_vlc_type(&name, base_table, #base_table); \
        printf("};\n");                                 \
    } while(0)

#define WRITE_VLC_ARRAY(prefix, name, base_table)       \
    do {                                                \
        int i;                                          \
        const size_t array_size = FF_ARRAY_ELEMS(name); \
        printf(prefix" VLC "#name"[%"FMT"] = {{\n",     \
               array_size);                             \
        for (i = 0; i < array_size; i++) {              \
            write_vlc_type(name + i,                    \
                           base_table, #base_table);    \
            if (i != array_size - 1) printf("}, {\n");  \
        }                                               \
        printf("}};\n");                                \
    } while(0)

#endif /* AVCODEC_TABLEPRINT_VLC_H */
