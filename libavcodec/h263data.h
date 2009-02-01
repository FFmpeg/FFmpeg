/*
 * copyright (c) 2000,2001 Fabrice Bellard
 * H263+ support
 * copyright (c) 2001 Juan J. Sierralta P
 * copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
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
 * @file libavcodec/h263data.h
 * H.263 tables.
 */

#ifndef AVCODEC_H263DATA_H
#define AVCODEC_H263DATA_H

#include <stdint.h>
#include "mpegvideo.h"

/* intra MCBPC, mb_type = (intra), then (intraq) */
const uint8_t intra_MCBPC_code[9] = { 1, 1, 2, 3, 1, 1, 2, 3, 1 };
const uint8_t intra_MCBPC_bits[9] = { 1, 3, 3, 3, 4, 6, 6, 6, 9 };

/* inter MCBPC, mb_type = (inter), (intra), (interq), (intraq), (inter4v) */
/* Changed the tables for interq and inter4v+q, following the standard ** Juanjo ** */
const uint8_t inter_MCBPC_code[28] = {
    1, 3, 2, 5,
    3, 4, 3, 3,
    3, 7, 6, 5,
    4, 4, 3, 2,
    2, 5, 4, 5,
    1, 0, 0, 0, /* Stuffing */
    2, 12, 14, 15,
};
const uint8_t inter_MCBPC_bits[28] = {
    1, 4, 4, 6, /* inter  */
    5, 8, 8, 7, /* intra  */
    3, 7, 7, 9, /* interQ */
    6, 9, 9, 9, /* intraQ */
    3, 7, 7, 8, /* inter4 */
    9, 0, 0, 0, /* Stuffing */
    11, 13, 13, 13,/* inter4Q*/
};

static const uint8_t h263_mbtype_b_tab[15][2] = {
 {1, 1},
 {3, 3},
 {1, 5},
 {4, 4},
 {5, 4},
 {6, 6},
 {2, 4},
 {3, 4},
 {7, 6},
 {4, 6},
 {5, 6},
 {1, 6},
 {1,10},
 {1, 7},
 {1, 8},
};

static const int h263_mb_type_b_map[15]= {
    MB_TYPE_DIRECT2 | MB_TYPE_L0L1,
    MB_TYPE_DIRECT2 | MB_TYPE_L0L1 | MB_TYPE_CBP,
    MB_TYPE_DIRECT2 | MB_TYPE_L0L1 | MB_TYPE_CBP | MB_TYPE_QUANT,
                      MB_TYPE_L0                                 | MB_TYPE_16x16,
                      MB_TYPE_L0   | MB_TYPE_CBP                 | MB_TYPE_16x16,
                      MB_TYPE_L0   | MB_TYPE_CBP | MB_TYPE_QUANT | MB_TYPE_16x16,
                      MB_TYPE_L1                                 | MB_TYPE_16x16,
                      MB_TYPE_L1   | MB_TYPE_CBP                 | MB_TYPE_16x16,
                      MB_TYPE_L1   | MB_TYPE_CBP | MB_TYPE_QUANT | MB_TYPE_16x16,
                      MB_TYPE_L0L1                               | MB_TYPE_16x16,
                      MB_TYPE_L0L1 | MB_TYPE_CBP                 | MB_TYPE_16x16,
                      MB_TYPE_L0L1 | MB_TYPE_CBP | MB_TYPE_QUANT | MB_TYPE_16x16,
    0, //stuffing
    MB_TYPE_INTRA4x4                | MB_TYPE_CBP,
    MB_TYPE_INTRA4x4                | MB_TYPE_CBP | MB_TYPE_QUANT,
};

static const uint8_t cbpc_b_tab[4][2] = {
{0, 1},
{2, 2},
{7, 3},
{6, 3},
};

const uint8_t cbpy_tab[16][2] =
{
  {3,4}, {5,5}, {4,5}, {9,4}, {3,5}, {7,4}, {2,6}, {11,4},
  {2,5}, {3,6}, {5,4}, {10,4}, {4,4}, {8,4}, {6,4}, {3,2}
};

const uint8_t mvtab[33][2] =
{
  {1,1}, {1,2}, {1,3}, {1,4}, {3,6}, {5,7}, {4,7}, {3,7},
  {11,9}, {10,9}, {9,9}, {17,10}, {16,10}, {15,10}, {14,10}, {13,10},
  {12,10}, {11,10}, {10,10}, {9,10}, {8,10}, {7,10}, {6,10}, {5,10},
  {4,10}, {7,11}, {6,11}, {5,11}, {4,11}, {3,11}, {2,11}, {3,12},
  {2,12}
};

/* third non intra table */
const uint16_t inter_vlc[103][2] = {
{ 0x2, 2 },{ 0xf, 4 },{ 0x15, 6 },{ 0x17, 7 },
{ 0x1f, 8 },{ 0x25, 9 },{ 0x24, 9 },{ 0x21, 10 },
{ 0x20, 10 },{ 0x7, 11 },{ 0x6, 11 },{ 0x20, 11 },
{ 0x6, 3 },{ 0x14, 6 },{ 0x1e, 8 },{ 0xf, 10 },
{ 0x21, 11 },{ 0x50, 12 },{ 0xe, 4 },{ 0x1d, 8 },
{ 0xe, 10 },{ 0x51, 12 },{ 0xd, 5 },{ 0x23, 9 },
{ 0xd, 10 },{ 0xc, 5 },{ 0x22, 9 },{ 0x52, 12 },
{ 0xb, 5 },{ 0xc, 10 },{ 0x53, 12 },{ 0x13, 6 },
{ 0xb, 10 },{ 0x54, 12 },{ 0x12, 6 },{ 0xa, 10 },
{ 0x11, 6 },{ 0x9, 10 },{ 0x10, 6 },{ 0x8, 10 },
{ 0x16, 7 },{ 0x55, 12 },{ 0x15, 7 },{ 0x14, 7 },
{ 0x1c, 8 },{ 0x1b, 8 },{ 0x21, 9 },{ 0x20, 9 },
{ 0x1f, 9 },{ 0x1e, 9 },{ 0x1d, 9 },{ 0x1c, 9 },
{ 0x1b, 9 },{ 0x1a, 9 },{ 0x22, 11 },{ 0x23, 11 },
{ 0x56, 12 },{ 0x57, 12 },{ 0x7, 4 },{ 0x19, 9 },
{ 0x5, 11 },{ 0xf, 6 },{ 0x4, 11 },{ 0xe, 6 },
{ 0xd, 6 },{ 0xc, 6 },{ 0x13, 7 },{ 0x12, 7 },
{ 0x11, 7 },{ 0x10, 7 },{ 0x1a, 8 },{ 0x19, 8 },
{ 0x18, 8 },{ 0x17, 8 },{ 0x16, 8 },{ 0x15, 8 },
{ 0x14, 8 },{ 0x13, 8 },{ 0x18, 9 },{ 0x17, 9 },
{ 0x16, 9 },{ 0x15, 9 },{ 0x14, 9 },{ 0x13, 9 },
{ 0x12, 9 },{ 0x11, 9 },{ 0x7, 10 },{ 0x6, 10 },
{ 0x5, 10 },{ 0x4, 10 },{ 0x24, 11 },{ 0x25, 11 },
{ 0x26, 11 },{ 0x27, 11 },{ 0x58, 12 },{ 0x59, 12 },
{ 0x5a, 12 },{ 0x5b, 12 },{ 0x5c, 12 },{ 0x5d, 12 },
{ 0x5e, 12 },{ 0x5f, 12 },{ 0x3, 7 },
};

const int8_t inter_level[102] = {
  1,  2,  3,  4,  5,  6,  7,  8,
  9, 10, 11, 12,  1,  2,  3,  4,
  5,  6,  1,  2,  3,  4,  1,  2,
  3,  1,  2,  3,  1,  2,  3,  1,
  2,  3,  1,  2,  1,  2,  1,  2,
  1,  2,  1,  1,  1,  1,  1,  1,
  1,  1,  1,  1,  1,  1,  1,  1,
  1,  1,  1,  2,  3,  1,  2,  1,
  1,  1,  1,  1,  1,  1,  1,  1,
  1,  1,  1,  1,  1,  1,  1,  1,
  1,  1,  1,  1,  1,  1,  1,  1,
  1,  1,  1,  1,  1,  1,  1,  1,
  1,  1,  1,  1,  1,  1,
};

const int8_t inter_run[102] = {
  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  1,  1,  1,  1,
  1,  1,  2,  2,  2,  2,  3,  3,
  3,  4,  4,  4,  5,  5,  5,  6,
  6,  6,  7,  7,  8,  8,  9,  9,
 10, 10, 11, 12, 13, 14, 15, 16,
 17, 18, 19, 20, 21, 22, 23, 24,
 25, 26,  0,  0,  0,  1,  1,  2,
  3,  4,  5,  6,  7,  8,  9, 10,
 11, 12, 13, 14, 15, 16, 17, 18,
 19, 20, 21, 22, 23, 24, 25, 26,
 27, 28, 29, 30, 31, 32, 33, 34,
 35, 36, 37, 38, 39, 40,
};

static RLTable rl_inter = {
    102,
    58,
    inter_vlc,
    inter_run,
    inter_level,
};

static const uint16_t intra_vlc_aic[103][2] = {
{  0x2,  2 }, {  0x6,  3 }, {  0xe,  4 }, {  0xc,  5 },
{  0xd,  5 }, { 0x10,  6 }, { 0x11,  6 }, { 0x12,  6 },
{ 0x16,  7 }, { 0x1b,  8 }, { 0x20,  9 }, { 0x21,  9 },
{ 0x1a,  9 }, { 0x1b,  9 }, { 0x1c,  9 }, { 0x1d,  9 },
{ 0x1e,  9 }, { 0x1f,  9 }, { 0x23, 11 }, { 0x22, 11 },
{ 0x57, 12 }, { 0x56, 12 }, { 0x55, 12 }, { 0x54, 12 },
{ 0x53, 12 }, {  0xf,  4 }, { 0x14,  6 }, { 0x14,  7 },
{ 0x1e,  8 }, {  0xf, 10 }, { 0x21, 11 }, { 0x50, 12 },
{  0xb,  5 }, { 0x15,  7 }, {  0xe, 10 }, {  0x9, 10 },
{ 0x15,  6 }, { 0x1d,  8 }, {  0xd, 10 }, { 0x51, 12 },
{ 0x13,  6 }, { 0x23,  9 }, {  0x7, 11 }, { 0x17,  7 },
{ 0x22,  9 }, { 0x52, 12 }, { 0x1c,  8 }, {  0xc, 10 },
{ 0x1f,  8 }, {  0xb, 10 }, { 0x25,  9 }, {  0xa, 10 },
{ 0x24,  9 }, {  0x6, 11 }, { 0x21, 10 }, { 0x20, 10 },
{  0x8, 10 }, { 0x20, 11 }, {  0x7,  4 }, {  0xc,  6 },
{ 0x10,  7 }, { 0x13,  8 }, { 0x11,  9 }, { 0x12,  9 },
{  0x4, 10 }, { 0x27, 11 }, { 0x26, 11 }, { 0x5f, 12 },
{  0xf,  6 }, { 0x13,  9 }, {  0x5, 10 }, { 0x25, 11 },
{  0xe,  6 }, { 0x14,  9 }, { 0x24, 11 }, {  0xd,  6 },
{  0x6, 10 }, { 0x5e, 12 }, { 0x11,  7 }, {  0x7, 10 },
{ 0x13,  7 }, { 0x5d, 12 }, { 0x12,  7 }, { 0x5c, 12 },
{ 0x14,  8 }, { 0x5b, 12 }, { 0x15,  8 }, { 0x1a,  8 },
{ 0x19,  8 }, { 0x18,  8 }, { 0x17,  8 }, { 0x16,  8 },
{ 0x19,  9 }, { 0x15,  9 }, { 0x16,  9 }, { 0x18,  9 },
{ 0x17,  9 }, {  0x4, 11 }, {  0x5, 11 }, { 0x58, 12 },
{ 0x59, 12 }, { 0x5a, 12 }, {  0x3,  7 },
};

static const int8_t intra_run_aic[102] = {
 0,  0,  0,  0,  0,  0,  0,  0,
 0,  0,  0,  0,  0,  0,  0,  0,
 0,  0,  0,  0,  0,  0,  0,  0,
 0,  1,  1,  1,  1,  1,  1,  1,
 2,  2,  2,  2,  3,  3,  3,  3,
 4,  4,  4,  5,  5,  5,  6,  6,
 7,  7,  8,  8,  9,  9, 10, 11,
12, 13,  0,  0,  0,  0,  0,  0,
 0,  0,  0,  0,  1,  1,  1,  1,
 2,  2,  2,  3,  3,  3,  4,  4,
 5,  5,  6,  6,  7,  7,  8,  9,
10, 11, 12, 13, 14, 15, 16, 17,
18, 19, 20, 21, 22, 23,
};

static const int8_t intra_level_aic[102] = {
 1,  2,  3,  4,  5,  6,  7,  8,
 9, 10, 11, 12, 13, 14, 15, 16,
17, 18, 19, 20, 21, 22, 23, 24,
25,  1,  2,  3,  4,  5,  6,  7,
 1,  2,  3,  4,  1,  2,  3,  4,
 1,  2,  3,  1,  2,  3,  1,  2,
 1,  2,  1,  2,  1,  2,  1,  1,
 1,  1,  1,  2,  3,  4,  5,  6,
 7,  8,  9, 10,  1,  2,  3,  4,
 1,  2,  3,  1,  2,  3,  1,  2,
 1,  2,  1,  2,  1,  2,  1,  1,
 1,  1,  1,  1,  1,  1,  1,  1,
 1,  1,  1,  1,  1,  1,
};

static RLTable rl_intra_aic = {
    102,
    58,
    intra_vlc_aic,
    intra_run_aic,
    intra_level_aic,
};

static const uint8_t wrong_run[102] = {
 1,  2,  3,  5,  4, 10,  9,  8,
11, 15, 17, 16, 23, 22, 21, 20,
19, 18, 25, 24, 27, 26, 11,  7,
 6,  1,  2, 13,  2,  2,  2,  2,
 6, 12,  3,  9,  1,  3,  4,  3,
 7,  4,  1,  1,  5,  5, 14,  6,
 1,  7,  1,  8,  1,  1,  1,  1,
10,  1,  1,  5,  9, 17, 25, 24,
29, 33, 32, 41,  2, 23, 28, 31,
 3, 22, 30,  4, 27, 40,  8, 26,
 6, 39,  7, 38, 16, 37, 15, 10,
11, 12, 13, 14,  1, 21, 20, 18,
19,  2,  1, 34, 35, 36
};

static const uint16_t h263_format[8][2] = {
    { 0, 0 },
    { 128, 96 },
    { 176, 144 },
    { 352, 288 },
    { 704, 576 },
    { 1408, 1152 },
};

const uint8_t ff_aic_dc_scale_table[32]={
//  0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31
    0, 2, 4, 6, 8,10,12,14,16,18,20,22,24,26,28,30,32,34,36,38,40,42,44,46,48,50,52,54,56,58,60,62
};

static const uint8_t modified_quant_tab[2][32]={
//  0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31
{
    0, 3, 1, 2, 3, 4, 5, 6, 7, 8, 9, 9,10,11,12,13,14,15,16,17,18,18,19,20,21,22,23,24,25,26,27,28
},{
    0, 2, 3, 4, 5, 6, 7, 8, 9,10,11,13,14,15,16,17,18,19,20,21,22,24,25,26,27,28,29,30,31,31,31,26
}
};

const uint8_t ff_h263_chroma_qscale_table[32]={
//  0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31
    0, 1, 2, 3, 4, 5, 6, 6, 7, 8, 9, 9,10,10,11,11,12,12,12,13,13,13,14,14,14,14,14,15,15,15,15,15
};

const uint16_t ff_mba_max[6]={
     47,  98, 395,1583,6335,9215
};

const uint8_t ff_mba_length[7]={
      6,   7,   9,  11,  13,  14,  14
};

const uint8_t ff_h263_loop_filter_strength[32]={
//  0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31
    0, 1, 1, 2, 2, 3, 3, 4, 4, 4, 5, 5, 6, 6, 7, 7, 7, 8, 8, 8, 9, 9, 9,10,10,10,11,11,11,12,12,12
};

#endif /* AVCODEC_H263DATA_H */
