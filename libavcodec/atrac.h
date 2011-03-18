/*
 * Atrac common data
 * Copyright (c) 2009 Maxim Poliakovski
 * Copyright (c) 2009 Benjamin Larsson
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * Atrac common header
 */

#ifndef AVCODEC_ATRAC_H
#define AVCODEC_ATRAC_H

extern float ff_atrac_sf_table[64];

void atrac_generate_tables(void);
void atrac_iqmf (float *inlo, float *inhi, unsigned int nIn, float *pOut, float *delayBuf, float *temp);

#endif /* AVCODEC_ATRAC_H */
