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
 *
 */

/**
 * @file cabac.h
 * Context Adaptive Binary Arithmetic Coder.
 */


//#undef NDEBUG
#include <assert.h>

#define CABAC_BITS 16
#define CABAC_MASK ((1<<CABAC_BITS)-1)

typedef struct CABACContext{
    int low;
    int range;
    int outstanding_count;
#ifdef STRICT_LIMITS
    int symCount;
#endif
    uint8_t lps_range[2*65][4];   ///< rangeTabLPS
    uint8_t lps_state[2*64];      ///< transIdxLPS
    uint8_t mps_state[2*64];      ///< transIdxMPS
    const uint8_t *bytestream_start;
    const uint8_t *bytestream;
    const uint8_t *bytestream_end;
    PutBitContext pb;
}CABACContext;

extern const uint8_t ff_h264_lps_range[64][4];
extern const uint8_t ff_h264_mps_state[64];
extern const uint8_t ff_h264_lps_state[64];
extern const uint8_t ff_h264_norm_shift[256];


void ff_init_cabac_encoder(CABACContext *c, uint8_t *buf, int buf_size);
void ff_init_cabac_decoder(CABACContext *c, const uint8_t *buf, int buf_size);
void ff_init_cabac_states(CABACContext *c, uint8_t const (*lps_range)[4],
                          uint8_t const *mps_state, uint8_t const *lps_state, int state_count);


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
    int RangeLPS= c->lps_range[*state][c->range>>6];

    if(bit == ((*state)&1)){
        c->range -= RangeLPS;
        *state= c->mps_state[*state];
    }else{
        c->low += c->range - RangeLPS;
        c->range = RangeLPS;
        *state= c->lps_state[*state];
    }

    renorm_cabac_encoder(c);

#ifdef STRICT_LIMITS
    c->symCount++;
#endif
}

static void put_cabac_static(CABACContext *c, int RangeLPS, int bit){
    assert(c->range > RangeLPS);

    if(!bit){
        c->range -= RangeLPS;
    }else{
        c->low += c->range - RangeLPS;
        c->range = RangeLPS;
    }

    renorm_cabac_encoder(c);

#ifdef STRICT_LIMITS
    c->symCount++;
#endif
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

#ifdef STRICT_LIMITS
    c->symCount++;
#endif
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

        assert(c->low <= 0x1FF);
        put_cabac_bit(c, c->low>>9);
        put_bits(&c->pb, 2, ((c->low>>7)&3)|1);

        flush_put_bits(&c->pb); //FIXME FIXME FIXME XXX wrong
    }

#ifdef STRICT_LIMITS
    c->symCount++;
#endif

    return (put_bits_count(&c->pb)+7)>>3;
}

/**
 * put (truncated) unary binarization.
 */
static void put_cabac_u(CABACContext *c, uint8_t * state, int v, int max, int max_index, int truncated){
    int i;

    assert(v <= max);

#if 1
    for(i=0; i<v; i++){
        put_cabac(c, state, 1);
        if(i < max_index) state++;
    }
    if(truncated==0 || v<max)
        put_cabac(c, state, 0);
#else
    if(v <= max_index){
        for(i=0; i<v; i++){
            put_cabac(c, state+i, 1);
        }
        if(truncated==0 || v<max)
            put_cabac(c, state+i, 0);
    }else{
        for(i=0; i<=max_index; i++){
            put_cabac(c, state+i, 1);
        }
        for(; i<v; i++){
            put_cabac(c, state+max_index, 1);
        }
        if(truncated==0 || v<max)
            put_cabac(c, state+max_index, 0);
    }
#endif
}

/**
 * put unary exp golomb k-th order binarization.
 */
static void put_cabac_ueg(CABACContext *c, uint8_t * state, int v, int max, int is_signed, int k, int max_index){
    int i;

    if(v==0)
        put_cabac(c, state, 0);
    else{
        const int sign= v < 0;

        if(is_signed) v= ABS(v);

        if(v<max){
            for(i=0; i<v; i++){
                put_cabac(c, state, 1);
                if(i < max_index) state++;
            }

            put_cabac(c, state, 0);
        }else{
            int m= 1<<k;

            for(i=0; i<max; i++){
                put_cabac(c, state, 1);
                if(i < max_index) state++;
            }

            v -= max;
            while(v >= m){ //FIXME optimize
                put_cabac_bypass(c, 1);
                v-= m;
                m+= m;
            }
            put_cabac_bypass(c, 0);
            while(m>>=1){
                put_cabac_bypass(c, v&m);
            }
        }

        if(is_signed)
            put_cabac_bypass(c, sign);
    }
}

static void refill(CABACContext *c){
    if(c->bytestream <= c->bytestream_end)
#if CABAC_BITS == 16
        c->low+= (c->bytestream[0]<<9) + (c->bytestream[1]<<1);
#else
        c->low+= c->bytestream[0]<<1;
#endif
    c->low -= CABAC_MASK;
    c->bytestream+= CABAC_BITS/8;
}

#if 0 /* all use commented */
static void refill2(CABACContext *c){
    int i, x;

    x= c->low ^ (c->low-1);
    i= 8 - ff_h264_norm_shift[x>>(CABAC_BITS+1)];

    x= -CABAC_MASK;

    if(c->bytestream < c->bytestream_end)
#if CABAC_BITS == 16
        x+= (c->bytestream[0]<<9) + (c->bytestream[1]<<1);
#else
        x+= c->bytestream[0]<<1;
#endif

    c->low += x<<i;
    c->bytestream+= CABAC_BITS/8;
}
#endif

static inline void renorm_cabac_decoder(CABACContext *c){
    while(c->range < (0x200 << CABAC_BITS)){
        c->range+= c->range;
        c->low+= c->low;
        if(!(c->low & CABAC_MASK))
            refill(c);
    }
}

static inline void renorm_cabac_decoder_once(CABACContext *c){
#ifdef ARCH_X86_DISABLED
    int temp;
#if 0
    //P3:683    athlon:475
    asm(
        "lea -0x2000000(%0), %2     \n\t"
        "shr $31, %2                \n\t"  //FIXME 31->63 for x86-64
        "shl %%cl, %0               \n\t"
        "shl %%cl, %1               \n\t"
        : "+r"(c->range), "+r"(c->low), "+c"(temp)
    );
#elif 0
    //P3:680    athlon:474
    asm(
        "cmp $0x2000000, %0         \n\t"
        "setb %%cl                  \n\t"  //FIXME 31->63 for x86-64
        "shl %%cl, %0               \n\t"
        "shl %%cl, %1               \n\t"
        : "+r"(c->range), "+r"(c->low), "+c"(temp)
    );
#elif 1
    int temp2;
    //P3:665    athlon:517
    asm(
        "lea -0x2000000(%0), %%eax  \n\t"
        "cdq                        \n\t"
        "mov %0, %%eax              \n\t"
        "and %%edx, %0              \n\t"
        "and %1, %%edx              \n\t"
        "add %%eax, %0              \n\t"
        "add %%edx, %1              \n\t"
        : "+r"(c->range), "+r"(c->low), "+a"(temp), "+d"(temp2)
    );
#elif 0
    int temp2;
    //P3:673    athlon:509
    asm(
        "cmp $0x2000000, %0         \n\t"
        "sbb %%edx, %%edx           \n\t"
        "mov %0, %%eax              \n\t"
        "and %%edx, %0              \n\t"
        "and %1, %%edx              \n\t"
        "add %%eax, %0              \n\t"
        "add %%edx, %1              \n\t"
        : "+r"(c->range), "+r"(c->low), "+a"(temp), "+d"(temp2)
    );
#else
    int temp2;
    //P3:677    athlon:511
    asm(
        "cmp $0x2000000, %0         \n\t"
        "lea (%0, %0), %%eax        \n\t"
        "lea (%1, %1), %%edx        \n\t"
        "cmovb %%eax, %0            \n\t"
        "cmovb %%edx, %1            \n\t"
        : "+r"(c->range), "+r"(c->low), "+a"(temp), "+d"(temp2)
    );
#endif
#else
    //P3:675    athlon:476
    int shift= (uint32_t)(c->range - (0x200 << CABAC_BITS))>>31;
    c->range<<= shift;
    c->low  <<= shift;
#endif
    if(!(c->low & CABAC_MASK))
        refill(c);
}

static int get_cabac(CABACContext *c, uint8_t * const state){
    //FIXME gcc generates duplicate load/stores for c->low and c->range
//START_TIMER
    int s = *state;
    int RangeLPS= c->lps_range[s][c->range>>(CABAC_BITS+7)]<<(CABAC_BITS+1);
    int bit, lps_mask attribute_unused;

    c->range -= RangeLPS;
#if 1
    if(c->low < c->range){
        bit= s&1;
        *state= c->mps_state[s];
        renorm_cabac_decoder_once(c);
    }else{
//        int shift= ff_h264_norm_shift[RangeLPS>>17];
        bit= (s&1)^1;
        c->low -= c->range;
        *state= c->lps_state[s];
        c->range = RangeLPS;
        renorm_cabac_decoder(c);
/*        c->range = RangeLPS<<shift;
        c->low <<= shift;
        if(!(c->low & 0xFFFF)){
            refill2(c);
        }*/
    }
#else
    lps_mask= (c->range - c->low)>>31;

    c->low -= c->range & lps_mask;
    c->range += (RangeLPS - c->range) & lps_mask;

    bit= (s^lps_mask)&1;
    *state= c->mps_state[s - (128&lps_mask)];

    lps_mask= ff_h264_norm_shift[c->range>>(CABAC_BITS+2)];
    c->range<<= lps_mask;
    c->low  <<= lps_mask;
    if(!(c->low & CABAC_MASK))
        refill2(c);
#endif
//STOP_TIMER("get_cabac")
    return bit;
}

static int get_cabac_bypass(CABACContext *c){
    c->low += c->low;

    if(!(c->low & CABAC_MASK))
        refill(c);

    if(c->low < c->range){
        return 0;
    }else{
        c->low -= c->range;
        return 1;
    }
}

/**
 *
 * @return the number of bytes read or 0 if no end
 */
static int get_cabac_terminate(CABACContext *c){
    c->range -= 4<<CABAC_BITS;
    if(c->low < c->range){
        renorm_cabac_decoder_once(c);
        return 0;
    }else{
        return c->bytestream - c->bytestream_start;
    }
}

/**
 * get (truncated) unnary binarization.
 */
static int get_cabac_u(CABACContext *c, uint8_t * state, int max, int max_index, int truncated){
    int i;

    for(i=0; i<max; i++){
        if(get_cabac(c, state)==0)
            return i;

        if(i< max_index) state++;
    }

    return truncated ? max : -1;
}

/**
 * get unary exp golomb k-th order binarization.
 */
static int get_cabac_ueg(CABACContext *c, uint8_t * state, int max, int is_signed, int k, int max_index){
    int i, v;
    int m= 1<<k;

    if(get_cabac(c, state)==0)
        return 0;

    if(0 < max_index) state++;

    for(i=1; i<max; i++){
        if(get_cabac(c, state)==0){
            if(is_signed && get_cabac_bypass(c)){
                return -i;
            }else
                return i;
        }

        if(i < max_index) state++;
    }

    while(get_cabac_bypass(c)){
        i+= m;
        m+= m;
    }

    v=0;
    while(m>>=1){
        v+= v + get_cabac_bypass(c);
    }
    i += v;

    if(is_signed && get_cabac_bypass(c)){
        return -i;
    }else
        return i;
}
