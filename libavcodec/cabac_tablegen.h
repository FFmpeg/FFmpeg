/*
 * Header file for hardcoded CABAC table
 *
 * Copyright (c) 2014 Reimar Döffinger <Reimar.Doeffinger@gmx.de>
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

#ifndef AVCODEC_CABAC_TABLEGEN_H
#define AVCODEC_CABAC_TABLEGEN_H

#if CONFIG_HARDCODED_TABLES
#define cabac_tableinit()
#include "libavcodec/cabac_tables.h"
#else
uint8_t ff_h264_cabac_tables[512 + 4*2*64 + 4*64 + 63];

static const uint8_t lps_range[64][4]= {
{128,176,208,240}, {128,167,197,227}, {128,158,187,216}, {123,150,178,205},
{116,142,169,195}, {111,135,160,185}, {105,128,152,175}, {100,122,144,166},
{ 95,116,137,158}, { 90,110,130,150}, { 85,104,123,142}, { 81, 99,117,135},
{ 77, 94,111,128}, { 73, 89,105,122}, { 69, 85,100,116}, { 66, 80, 95,110},
{ 62, 76, 90,104}, { 59, 72, 86, 99}, { 56, 69, 81, 94}, { 53, 65, 77, 89},
{ 51, 62, 73, 85}, { 48, 59, 69, 80}, { 46, 56, 66, 76}, { 43, 53, 63, 72},
{ 41, 50, 59, 69}, { 39, 48, 56, 65}, { 37, 45, 54, 62}, { 35, 43, 51, 59},
{ 33, 41, 48, 56}, { 32, 39, 46, 53}, { 30, 37, 43, 50}, { 29, 35, 41, 48},
{ 27, 33, 39, 45}, { 26, 31, 37, 43}, { 24, 30, 35, 41}, { 23, 28, 33, 39},
{ 22, 27, 32, 37}, { 21, 26, 30, 35}, { 20, 24, 29, 33}, { 19, 23, 27, 31},
{ 18, 22, 26, 30}, { 17, 21, 25, 28}, { 16, 20, 23, 27}, { 15, 19, 22, 25},
{ 14, 18, 21, 24}, { 14, 17, 20, 23}, { 13, 16, 19, 22}, { 12, 15, 18, 21},
{ 12, 14, 17, 20}, { 11, 14, 16, 19}, { 11, 13, 15, 18}, { 10, 12, 15, 17},
{ 10, 12, 14, 16}, {  9, 11, 13, 15}, {  9, 11, 12, 14}, {  8, 10, 12, 14},
{  8,  9, 11, 13}, {  7,  9, 11, 12}, {  7,  9, 10, 12}, {  7,  8, 10, 11},
{  6,  8,  9, 11}, {  6,  7,  9, 10}, {  6,  7,  8,  9}, {  2,  2,  2,  2},
};

static const uint8_t mps_state[64]= {
  1, 2, 3, 4, 5, 6, 7, 8,
  9,10,11,12,13,14,15,16,
 17,18,19,20,21,22,23,24,
 25,26,27,28,29,30,31,32,
 33,34,35,36,37,38,39,40,
 41,42,43,44,45,46,47,48,
 49,50,51,52,53,54,55,56,
 57,58,59,60,61,62,62,63,
};

static const uint8_t lps_state[64]= {
  0, 0, 1, 2, 2, 4, 4, 5,
  6, 7, 8, 9, 9,11,11,12,
 13,13,15,15,16,16,18,18,
 19,19,21,21,22,22,23,24,
 24,25,26,26,27,27,28,29,
 29,30,30,30,31,32,32,33,
 33,33,34,34,35,35,35,36,
 36,36,37,37,37,38,38,63,
};

static const uint8_t last_coeff_flag_offset_8x8[63] = {
 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4,
 5, 5, 5, 5, 6, 6, 6, 6, 7, 7, 7, 7, 8, 8, 8
};

static av_cold void cabac_tableinit(void)
{
    int i, j;
    for (i = 0; i < 512; i++)
        ff_h264_norm_shift[i] = i ? 8 - av_log2(i) : 9;

    for(i=0; i<64; i++){
        for(j=0; j<4; j++){ //FIXME check if this is worth the 1 shift we save
            ff_h264_lps_range[j*2*64+2*i+0]=
            ff_h264_lps_range[j*2*64+2*i+1]= lps_range[i][j];
        }
        ff_h264_mlps_state[128 + 2 * i + 0] = 2 * mps_state[i] + 0;
        ff_h264_mlps_state[128 + 2 * i + 1] = 2 * mps_state[i] + 1;

        if( i ){
            ff_h264_mlps_state[128-2*i-1]= 2*lps_state[i]+0;
            ff_h264_mlps_state[128-2*i-2]= 2*lps_state[i]+1;
        }else{
            ff_h264_mlps_state[128-2*i-1]= 1;
            ff_h264_mlps_state[128-2*i-2]= 0;
        }
    }
    for(i=0; i< 63; i++){
      ff_h264_last_coeff_flag_offset_8x8[i] = last_coeff_flag_offset_8x8[i];
    }
}
#endif /* CONFIG_HARDCODED_TABLES */

#endif /* AVCODEC_CABAC_TABLEGEN_H */
