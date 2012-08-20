/*
 * Common Ut Video code
 * Copyright (c) 2011 Konstantin Shishkov
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
 * Common Ut Video code
 */

#include "utvideo.h"

const int ff_ut_pred_order[5] = {
    PRED_LEFT, PRED_MEDIAN, PRED_MEDIAN, PRED_NONE, PRED_GRADIENT
};

const int ff_ut_rgb_order[4]  = { 1, 2, 0, 3 }; // G, B, R, A

int ff_ut_huff_cmp_len(const void *a, const void *b)
{
    const HuffEntry *aa = a, *bb = b;
    return (aa->len - bb->len)*256 + aa->sym - bb->sym;
}
