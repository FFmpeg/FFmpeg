/*
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

#include "lcevctab.h"

const struct FFLCEVCDim ff_lcevc_resolution_type[63] = {
    { 0, 0},
    { 360,  200 },  { 400,  240 },  { 480,  320 },  { 640,  360 },
    { 640,  480 },  { 768,  480 },  { 800,  600 },  { 852,  480 },
    { 854,  480 },  { 856,  480 },  { 960,  540 },  { 960,  640 },
    { 1024, 576 },  { 1024, 600 },  { 1024, 768 },  { 1152, 864 },
    { 1280, 720 },  { 1280, 800 },  { 1280, 1024 }, { 1360, 768 },
    { 1366, 768 },  { 1920, 1200 }, { 2048, 1080 }, { 2048, 1152 },
    { 2048, 1536 }, { 2160, 1440 }, { 2560, 1440 }, { 2560, 1600 },
    { 2560, 2048 }, { 3200, 1800 }, { 3200, 2048 }, { 3200, 2400 },
    { 3440, 1440 }, { 3840, 1600 }, { 3840, 2160 }, { 3840, 2400 },
    { 4096, 2160 }, { 4096, 3072 }, { 5120, 2880 }, { 5120, 3200 },
    { 5120, 4096 }, { 6400, 4096 }, { 6400, 4800 }, { 7680, 4320 },
    { 7680, 4800 },
};
