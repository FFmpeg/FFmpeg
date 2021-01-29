/*
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

#include "libavcodec/cabac.c"

#define SIZE 10240

#include "libavutil/lfg.h"
#include "libavcodec/avcodec.h"
#include "libavcodec/put_bits.h"

typedef struct CABACTestContext {
    CABACContext dec;
    int outstanding_count;
    PutBitContext pb;
} CABACTestContext;

static inline void put_cabac_bit(CABACTestContext *c, int b)
{
    put_bits(&c->pb, 1, b);
    for(;c->outstanding_count; c->outstanding_count--){
        put_bits(&c->pb, 1, 1-b);
    }
}

static inline void renorm_cabac_encoder(CABACTestContext *c)
{
    while (c->dec.range < 0x100) {
        //FIXME optimize
        if (c->dec.low < 0x100) {
            put_cabac_bit(c, 0);
        } else if (c->dec.low < 0x200) {
            c->outstanding_count++;
            c->dec.low -= 0x100;
        }else{
            put_cabac_bit(c, 1);
            c->dec.low -= 0x200;
        }

        c->dec.range += c->dec.range;
        c->dec.low   += c->dec.low;
    }
}

static void put_cabac(CABACTestContext *c, uint8_t * const state, int bit)
{
    int RangeLPS = ff_h264_lps_range[2 * (c->dec.range & 0xC0) + *state];

    if(bit == ((*state)&1)){
        c->dec.range -= RangeLPS;
        *state    = ff_h264_mlps_state[128 + *state];
    }else{
        c->dec.low  += c->dec.range - RangeLPS;
        c->dec.range = RangeLPS;
        *state= ff_h264_mlps_state[127 - *state];
    }

    renorm_cabac_encoder(c);
}

/**
 * @param bit 0 -> write zero bit, !=0 write one bit
 */
static void put_cabac_bypass(CABACTestContext *c, int bit)
{
    c->dec.low += c->dec.low;

    if(bit){
        c->dec.low += c->dec.range;
    }
//FIXME optimize
    if (c->dec.low < 0x200) {
        put_cabac_bit(c, 0);
    } else if (c->dec.low < 0x400) {
        c->outstanding_count++;
        c->dec.low -= 0x200;
    }else{
        put_cabac_bit(c, 1);
        c->dec.low -= 0x400;
    }
}

/**
 *
 * @return the number of bytes written
 */
static int put_cabac_terminate(CABACTestContext *c, int bit)
{
    c->dec.range -= 2;

    if(!bit){
        renorm_cabac_encoder(c);
    }else{
        c->dec.low  += c->dec.range;
        c->dec.range = 2;

        renorm_cabac_encoder(c);

        av_assert0(c->dec.low <= 0x1FF);
        put_cabac_bit(c, c->dec.low >> 9);
        put_bits(&c->pb, 2, ((c->dec.low >> 7) & 3) | 1);

        flush_put_bits(&c->pb); //FIXME FIXME FIXME XXX wrong
    }

    return (put_bits_count(&c->pb)+7)>>3;
}

/**
 * @param buf_size size of buf in bits
 */
static void init_cabac_encoder(CABACTestContext *c, uint8_t *buf, int buf_size)
{
    init_put_bits(&c->pb, buf, buf_size);

    c->dec.low   = 0;
    c->dec.range = 0x1FE;
    c->outstanding_count = 0;
    c->pb.bit_left++; //avoids firstBitFlag
}

int main(void){
    CABACTestContext c;
    uint8_t b[9*SIZE];
    uint8_t r[9*SIZE];
    int i, ret = 0;
    uint8_t state[10]= {0};
    AVLFG prng;

    av_lfg_init(&prng, 1);
    init_cabac_encoder(&c, b, SIZE);

    for(i=0; i<SIZE; i++){
        if(2*i<SIZE) r[i] = av_lfg_get(&prng) % 7;
        else         r[i] = (i>>8)&1;
    }

    for(i=0; i<SIZE; i++){
        put_cabac_bypass(&c, r[i]&1);
    }

    for(i=0; i<SIZE; i++){
        put_cabac(&c, state, r[i]&1);
    }

    i= put_cabac_terminate(&c, 1);
    b[i++] = av_lfg_get(&prng);
    b[i  ] = av_lfg_get(&prng);

    ff_init_cabac_decoder(&c.dec, b, SIZE);

    memset(state, 0, sizeof(state));

    for(i=0; i<SIZE; i++){
        if ((r[i] & 1) != get_cabac_bypass(&c.dec)) {
            av_log(NULL, AV_LOG_ERROR, "CABAC bypass failure at %d\n", i);
            ret = 1;
        }
    }

    for(i=0; i<SIZE; i++){
        if ((r[i] & 1) != get_cabac_noinline(&c.dec, state)) {
            av_log(NULL, AV_LOG_ERROR, "CABAC failure at %d\n", i);
            ret = 1;
        }
    }
    if (!get_cabac_terminate(&c.dec)) {
        av_log(NULL, AV_LOG_ERROR, "where's the Terminator?\n");
        ret = 1;
    }

    return ret;
}
