/*
 * VVC CTU(Coding Tree Unit) parser
 *
 * Copyright (C) 2022 Nuo Mi
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

#include "vvc_ctu.h"

void ff_vvc_ep_init_stat_coeff(EntryPoint *ep,
    const int bit_depth, const int persistent_rice_adaptation_enabled_flag)
{
    for (size_t i = 0; i < FF_ARRAY_ELEMS(ep->stat_coeff); ++i) {
        ep->stat_coeff[i] =
            persistent_rice_adaptation_enabled_flag ? 2 * (av_log2(bit_depth - 10)) : 0;
    }
}
