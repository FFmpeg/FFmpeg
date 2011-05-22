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

#ifndef AVCODEC_AAC_TABLEGEN_DECL_H
#define AVCODEC_AAC_TABLEGEN_DECL_H

#if CONFIG_HARDCODED_TABLES
#define ff_aac_tableinit()
extern const float ff_aac_pow2sf_tab[428];
#else
void ff_aac_tableinit(void);
extern       float ff_aac_pow2sf_tab[428];
#endif /* CONFIG_HARDCODED_TABLES */

#endif /* AVCODEC_AAC_TABLEGEN_DECL_H */
