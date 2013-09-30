/*
 * common functions for the ATRAC family of decoders
 *
 * Copyright (c) 2009-2013 Maxim Poliakovski
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
 * ATRAC common header
 */

#ifndef AVCODEC_ATRAC_H
#define AVCODEC_ATRAC_H

extern float ff_atrac_sf_table[64];

/**
 * Generate common tables.
 */
void ff_atrac_generate_tables(void);

/**
 * Quadrature mirror synthesis filter.
 *
 * @param inlo      lower part of spectrum
 * @param inhi      higher part of spectrum
 * @param nIn       size of spectrum buffer
 * @param pOut      out buffer
 * @param delayBuf  delayBuf buffer
 * @param temp      temp buffer
 */
void ff_atrac_iqmf (float *inlo, float *inhi, unsigned int nIn, float *pOut, float *delayBuf, float *temp);

#endif /* AVCODEC_ATRAC_H */
