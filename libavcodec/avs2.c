/*
 * AVS2 related definitions
 *
 * Copyright (C) 2022 Zhao Zhili, <zhilizhao@tencent.com>
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

#include "avs2.h"

const AVRational ff_avs2_frame_rate_tab[16] = {
    { 0    , 0   }, // forbid
    { 24000, 1001},
    { 24   , 1   },
    { 25   , 1   },
    { 30000, 1001},
    { 30   , 1   },
    { 50   , 1   },
    { 60000, 1001},
    { 60   , 1   },
    { 100  , 1   },
    { 120  , 1   },
    { 200  , 1   },
    { 240  , 1   },
    { 300  , 1   },
    { 0    , 0   }, // reserved
    { 0    , 0   }  // reserved
};
