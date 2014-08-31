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
 * @file
 * Context Adaptive Binary Arithmetic Coder.
 */

#include <string.h>

#include "libavutil/common.h"
#include "libavutil/timer.h"
#include "get_bits.h"
#include "cabac.h"
#include "cabac_functions.h"

#include "cabac_tablegen.h"

/**
 *
 * @param buf_size size of buf in bits
 */
void ff_init_cabac_encoder(CABACContext *c, uint8_t *buf, int buf_size){
    init_put_bits(&c->pb, buf, buf_size);

    c->low= 0;
    c->range= 0x1FE;
    c->outstanding_count= 0;
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

void ff_init_cabac_states(void)
{
    static int initialized = 0;

    if (initialized)
        return;

    cabac_tableinit();

    initialized = 1;
}

#ifdef TEST
#define SIZE 10240

#include "libavutil/lfg.h"
#include "avcodec.h"

static inline void put_cabac_bit(CABACContext *c, int b){
    put_bits(&c->pb, 1, b);
    for(;c->outstanding_count; c->outstanding_count--){
        put_bits(&c->pb, 1, 1-b);
    }
}

static inline void renorm_cabac_encoder(CABACContext *c){
    while(c->range < 0x100){
        //FIXME optimize
        if(c->low<0x100){
            put_cabac_bit(c, 0);
        }else if(c->low<0x200){
            c->outstanding_count++;
            c->low -= 0x100;
        }else{
            put_cabac_bit(c, 1);
            c->low -= 0x200;
        }

        c->range+= c->range;
        c->low += c->low;
    }
}

static void put_cabac(CABACContext *c, uint8_t * const state, int bit){
    int RangeLPS= ff_h264_lps_range[2*(c->range&0xC0) + *state];

    if(bit == ((*state)&1)){
        c->range -= RangeLPS;
        *state    = ff_h264_mlps_state[128 + *state];
    }else{
        c->low += c->range - RangeLPS;
        c->range = RangeLPS;
        *state= ff_h264_mlps_state[127 - *state];
    }

    renorm_cabac_encoder(c);
}

/**
 * @param bit 0 -> write zero bit, !=0 write one bit
 */
static void put_cabac_bypass(CABACContext *c, int bit){
    c->low += c->low;

    if(bit){
        c->low += c->range;
    }
//FIXME optimize
    if(c->low<0x200){
        put_cabac_bit(c, 0);
    }else if(c->low<0x400){
        c->outstanding_count++;
        c->low -= 0x200;
    }else{
        put_cabac_bit(c, 1);
        c->low -= 0x400;
    }
}

/**
 *
 * @return the number of bytes written
 */
static int put_cabac_terminate(CABACContext *c, int bit){
    c->range -= 2;

    if(!bit){
        renorm_cabac_encoder(c);
    }else{
        c->low += c->range;
        c->range= 2;

        renorm_cabac_encoder(c);

        av_assert0(c->low <= 0x1FF);
        put_cabac_bit(c, c->low>>9);
        put_bits(&c->pb, 2, ((c->low>>7)&3)|1);

        flush_put_bits(&c->pb); //FIXME FIXME FIXME XXX wrong
    }

    return (put_bits_count(&c->pb)+7)>>3;
}

int main(void){
    CABACContext c;
    uint8_t b[9*SIZE];
    uint8_t r[9*SIZE];
    int i;
    uint8_t state[10]= {0};
    AVLFG prng;

    av_lfg_init(&prng, 1);
    ff_init_cabac_encoder(&c, b, SIZE);
    ff_init_cabac_states();

    for(i=0; i<SIZE; i++){
        if(2*i<SIZE) r[i] = av_lfg_get(&prng) % 7;
        else         r[i] = (i>>8)&1;
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
        if( (r[i]&1) != get_cabac_noinline(&c, state) )
            av_log(NULL, AV_LOG_ERROR, "CABAC failure at %d\n", i);
STOP_TIMER("get_cabac")
    }
    if(!get_cabac_terminate(&c))
        av_log(NULL, AV_LOG_ERROR, "where's the Terminator?\n");

    return 0;
}

#endif /* TEST */
