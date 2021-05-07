/*
 * MQ-coder encoder and decoder common functions
 * Copyright (c) 2007 Kamil Nowosad
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

/**
 * MQ-coder common (decoder/encoder) functions
 * @file
 * @author Kamil Nowosad
 */

#include <string.h>
#include <stdint.h>

#include "mqc.h"

const uint16_t ff_mqc_qe[2 * 47] = {
    0x5601, 0x5601, 0x3401, 0x3401, 0x1801, 0x1801, 0x0ac1, 0x0ac1,
    0x0521, 0x0521, 0x0221, 0x0221, 0x5601, 0x5601, 0x5401, 0x5401,
    0x4801, 0x4801, 0x3801, 0x3801, 0x3001, 0x3001, 0x2401, 0x2401,
    0x1c01, 0x1c01, 0x1601, 0x1601, 0x5601, 0x5601, 0x5401, 0x5401,
    0x5101, 0x5101, 0x4801, 0x4801, 0x3801, 0x3801, 0x3401, 0x3401,
    0x3001, 0x3001, 0x2801, 0x2801, 0x2401, 0x2401, 0x2201, 0x2201,
    0x1c01, 0x1c01, 0x1801, 0x1801, 0x1601, 0x1601, 0x1401, 0x1401,
    0x1201, 0x1201, 0x1101, 0x1101, 0x0ac1, 0x0ac1, 0x09c1, 0x09c1,
    0x08a1, 0x08a1, 0x0521, 0x0521, 0x0441, 0x0441, 0x02a1, 0x02a1,
    0x0221, 0x0221, 0x0141, 0x0141, 0x0111, 0x0111, 0x0085, 0x0085,
    0x0049, 0x0049, 0x0025, 0x0025, 0x0015, 0x0015, 0x0009, 0x0009,
    0x0005, 0x0005, 0x0001, 0x0001, 0x5601, 0x5601
};
const uint8_t ff_mqc_nlps[2 * 47] = {
     3,  2, 12, 13, 18, 19, 24, 25, 58, 59, 66, 67, 13, 12, 28, 29,
    28, 29, 28, 29, 34, 35, 36, 37, 40, 41, 42, 43, 29, 28, 28, 29,
    30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 38, 39, 40, 41, 42, 43,
    44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59,
    60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75,
    76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 92, 93
};
const uint8_t ff_mqc_nmps[2 * 47] = {
     2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 76, 77, 14, 15, 16, 17,
    18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 58, 59, 30, 31, 32, 33,
    34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49,
    50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65,
    66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81,
    82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 90, 91, 92, 93
};

void ff_mqc_init_contexts(MqcState *mqc)
{
    memset(mqc->cx_states, 0, sizeof(mqc->cx_states));
    mqc->cx_states[MQC_CX_UNI] = 2 * 46;
    mqc->cx_states[MQC_CX_RL]  = 2 * 3;
    mqc->cx_states[0]          = 2 * 4;
}
