/*
 * Header file for hardcoded AAC cube-root table
 *
 * Copyright (c) 2010 Reimar Döffinger <Reimar.Doeffinger@gmx.de>
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

#ifndef CBRT_TABLEGEN_H
#define CBRT_TABLEGEN_H

#include <stdint.h>
#include <math.h>

#if CONFIG_HARDCODED_TABLES
#define cbrt_tableinit()
#include "libavcodec/cbrt_tables.h"
#else
static uint32_t cbrt_tab[1 << 13];

static void cbrt_tableinit(void)
{
    if (!cbrt_tab[(1<<13) - 1]) {
        int i;
        for (i = 0; i < 1<<13; i++) {
            union {
                float f;
                uint32_t i;
            } f;
            f.f = cbrtf(i) * i;
            cbrt_tab[i] = f.i;
        }
    }
}
#endif /* CONFIG_HARDCODED_TABLES */

#endif /* CBRT_TABLEGEN_H */
