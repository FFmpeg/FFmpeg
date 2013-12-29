/*
 * Monkey's Audio lossless audio decoder
 * Copyright (c) 2007 Benjamin Zores <ben@geexbox.org>
 *  based upon libdemac from Dave Chapman.
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

#ifndef AVCODEC_APEDSP_H
#define AVCODEC_APEDSP_H

#include <stdint.h>

typedef struct APEDSPContext {
    /**
     * Calculate scalar product of v1 and v2,
     * and v1[i] += v3[i] * mul
     * @param len length of vectors, should be multiple of 16
     */
    int32_t (*scalarproduct_and_madd_int16)(int16_t *v1 /* align 16 */,
                                            const int16_t *v2,
                                            const int16_t *v3,
                                            int len, int mul);
} APEDSPContext;

void ff_apedsp_init_arm(APEDSPContext *c);
void ff_apedsp_init_ppc(APEDSPContext *c);
void ff_apedsp_init_x86(APEDSPContext *c);

#endif /* AVCODEC_APEDSP_H */
