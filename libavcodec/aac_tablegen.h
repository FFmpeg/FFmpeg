/*
 * Header file for hardcoded AAC tables
 *
 * Copyright (c) 2010 Alex Converse <alex.converse@gmail.com>
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

#ifndef AAC_TABLEGEN_H
#define AAC_TABLEGEN_H

#include "aac_tablegen_decl.h"

#if CONFIG_HARDCODED_TABLES
#include "libavcodec/aac_tables.h"
#else
#include "libavutil/mathematics.h"
float ff_aac_pow2sf_tab[428];

void ff_aac_tableinit(void)
{
    int i;
    for (i = 0; i < 428; i++)
        ff_aac_pow2sf_tab[i] = pow(2, (i - 200) / 4.);
}
#endif /* CONFIG_HARDCODED_TABLES */

#endif /* AAC_TABLEGEN_H */
