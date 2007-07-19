/*
 * H.26L/H.264/AVC/JVT/14496-10/... encoder/decoder
 * Copyright (c) 2003 Michael Niedermayer <michaelni@gmx.at>
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
 * @file cabac.c
 * Context Adaptive Binary Arithmetic Coder.
 */

#include <string.h>

#include "common.h"
#include "bitstream.h"
#include "cabac.h"

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

uint8_t ff_h264_mlps_state[4*64];
uint8_t ff_h264_lps_range[4*2*64];
uint8_t ff_h264_lps_state[2*64];
uint8_t ff_h264_mps_state[2*64];

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
#if 0
const uint8_t ff_h264_norm_shift_old[128]= {
 7,6,5,5,4,4,4,4,3,3,3,3,3,3,3,3,
 2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};
#endif
const uint8_t ff_h264_norm_shift[512]= {
 9,8,7,7,6,6,6,6,5,5,5,5,5,5,5,5,
 4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
 3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,
 3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,
 2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
 2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
 2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
 2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};

/**
 *
 * @param buf_size size of buf in bits
 */
void ff_init_cabac_encoder(CABACContext *c, uint8_t *buf, int buf_size){
    init_put_bits(&c->pb, buf, buf_size);

    c->low= 0;
    c->range= 0x1FE;
    c->outstanding_count= 0;
#ifdef STRICT_LIMITS
    c->sym_count =0;
#endif

    c->pb.bit_left++; //avoids firstBitFlag
}

/**
 *
 * @param buf_size size of buf in bits
 */
void ff_init_cabac_decoder(CABACContext *c, const uint8_t *buf, int buf_size){
    c->bytestream_start=
    c->bytestream= buf;
    c->bytestream_end= buf + buf_size;

#if CABAC_BITS == 16
    c->low =  (*c->bytestream++)<<18;
    c->low+=  (*c->bytestream++)<<10;
#else
    c->low =  (*c->bytestream++)<<10;
#endif
    c->low+= ((*c->bytestream++)<<2) + 2;
    c->range= 0x1FE;
}

void ff_init_cabac_states(CABACContext *c){
    int i, j;

    for(i=0; i<64; i++){
        for(j=0; j<4; j++){ //FIXME check if this is worth the 1 shift we save
            ff_h264_lps_range[j*2*64+2*i+0]=
            ff_h264_lps_range[j*2*64+2*i+1]= lps_range[i][j];
        }

        ff_h264_mlps_state[128+2*i+0]=
        ff_h264_mps_state[2*i+0]= 2*mps_state[i]+0;
        ff_h264_mlps_state[128+2*i+1]=
        ff_h264_mps_state[2*i+1]= 2*mps_state[i]+1;

        if( i ){
#ifdef BRANCHLESS_CABAC_DECODER
            ff_h264_mlps_state[128-2*i-1]= 2*lps_state[i]+0;
            ff_h264_mlps_state[128-2*i-2]= 2*lps_state[i]+1;
        }else{
            ff_h264_mlps_state[128-2*i-1]= 1;
            ff_h264_mlps_state[128-2*i-2]= 0;
#else
            ff_h264_lps_state[2*i+0]= 2*lps_state[i]+0;
            ff_h264_lps_state[2*i+1]= 2*lps_state[i]+1;
        }else{
            ff_h264_lps_state[2*i+0]= 1;
            ff_h264_lps_state[2*i+1]= 0;
#endif
        }
    }
}

#if 0 //selftest
#undef random
#define SIZE 10240

#include "avcodec.h"

int main(){
    CABACContext c;
    uint8_t b[9*SIZE];
    uint8_t r[9*SIZE];
    int i;
    uint8_t state[10]= {0};

    ff_init_cabac_encoder(&c, b, SIZE);
    ff_init_cabac_states(&c, ff_h264_lps_range, ff_h264_mps_state, ff_h264_lps_state, 64);

    for(i=0; i<SIZE; i++){
        r[i]= random()%7;
    }

    for(i=0; i<SIZE; i++){
START_TIMER
        put_cabac_bypass(&c, r[i]&1);
STOP_TIMER("put_cabac_bypass")
    }

    for(i=0; i<SIZE; i++){
START_TIMER
        put_cabac(&c, state, r[i]&1);
STOP_TIMER("put_cabac")
    }

    for(i=0; i<SIZE; i++){
START_TIMER
        put_cabac_u(&c, state, r[i], 6, 3, i&1);
STOP_TIMER("put_cabac_u")
    }

    for(i=0; i<SIZE; i++){
START_TIMER
        put_cabac_ueg(&c, state, r[i], 3, 0, 1, 2);
STOP_TIMER("put_cabac_ueg")
    }

    put_cabac_terminate(&c, 1);

    ff_init_cabac_decoder(&c, b, SIZE);

    memset(state, 0, sizeof(state));

    for(i=0; i<SIZE; i++){
START_TIMER
        if( (r[i]&1) != get_cabac_bypass(&c) )
            av_log(NULL, AV_LOG_ERROR, "CABAC bypass failure at %d\n", i);
STOP_TIMER("get_cabac_bypass")
    }

    for(i=0; i<SIZE; i++){
START_TIMER
        if( (r[i]&1) != get_cabac(&c, state) )
            av_log(NULL, AV_LOG_ERROR, "CABAC failure at %d\n", i);
STOP_TIMER("get_cabac")
    }
#if 0
    for(i=0; i<SIZE; i++){
START_TIMER
        if( r[i] != get_cabac_u(&c, state, (i&1) ? 6 : 7, 3, i&1) )
            av_log(NULL, AV_LOG_ERROR, "CABAC unary (truncated) binarization failure at %d\n", i);
STOP_TIMER("get_cabac_u")
    }

    for(i=0; i<SIZE; i++){
START_TIMER
        if( r[i] != get_cabac_ueg(&c, state, 3, 0, 1, 2))
            av_log(NULL, AV_LOG_ERROR, "CABAC unary (truncated) binarization failure at %d\n", i);
STOP_TIMER("get_cabac_ueg")
    }
#endif
    if(!get_cabac_terminate(&c))
        av_log(NULL, AV_LOG_ERROR, "where's the Terminator?\n");

    return 0;
}

#endif
