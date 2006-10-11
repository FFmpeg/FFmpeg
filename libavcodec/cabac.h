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
#define BRANCHLESS_CABAC_DECODER 1
#define CMOV_IS_FAST 1

typedef struct CABACContext{
    int low;
    int range;
    int outstanding_count;
#ifdef STRICT_LIMITS
    int symCount;
#endif
    const uint8_t *bytestream_start;
    const uint8_t *bytestream;
    const uint8_t *bytestream_end;
    PutBitContext pb;
}CABACContext;

extern uint8_t ff_h264_lps_range[2*65][4];  ///< rangeTabLPS
extern uint8_t ff_h264_mps_state[2*64];     ///< transIdxMPS
extern uint8_t ff_h264_lps_state[2*64];     ///< transIdxLPS
extern const uint8_t ff_h264_norm_shift[128];


void ff_init_cabac_encoder(CABACContext *c, uint8_t *buf, int buf_size);
void ff_init_cabac_decoder(CABACContext *c, const uint8_t *buf, int buf_size);
void ff_init_cabac_states(CABACContext *c);


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
    int RangeLPS= ff_h264_lps_range[*state][c->range>>6];

    if(bit == ((*state)&1)){
        c->range -= RangeLPS;
        *state= ff_h264_mps_state[*state];
    }else{
        c->low += c->range - RangeLPS;
        c->range = RangeLPS;
        *state= ff_h264_lps_state[*state];
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
#if CABAC_BITS == 16
        c->low+= (c->bytestream[0]<<9) + (c->bytestream[1]<<1);
#else
        c->low+= c->bytestream[0]<<1;
#endif
    c->low -= CABAC_MASK;
    c->bytestream+= CABAC_BITS/8;
}

static void refill2(CABACContext *c){
    int i, x;

    x= c->low ^ (c->low-1);
    i= 7 - ff_h264_norm_shift[x>>(CABAC_BITS+1)];

    x= -CABAC_MASK;

#if CABAC_BITS == 16
        x+= (c->bytestream[0]<<9) + (c->bytestream[1]<<1);
#else
        x+= c->bytestream[0]<<1;
#endif

    c->low += x<<i;
    c->bytestream+= CABAC_BITS/8;
}

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
#ifdef ARCH_X86
    int bit;

#define LOW          "0"
#define RANGE        "4"
#define BYTESTART   "12"
#define BYTE        "16"
#define BYTEEND     "20"
#ifndef BRANCHLESS_CABAC_DECODER
    asm volatile(
        "movzbl (%1), %%eax                     \n\t"
        "movl "RANGE    "(%2), %%ebx            \n\t"
        "movl "RANGE    "(%2), %%edx            \n\t"
        "shrl $23, %%ebx                        \n\t"
        "movzbl "MANGLE(ff_h264_lps_range)"(%%ebx, %%eax, 4), %%esi\n\t"
        "shll $17, %%esi                        \n\t"
        "movl "LOW      "(%2), %%ebx            \n\t"
//eax:state ebx:low, edx:range, esi:RangeLPS
        "subl %%esi, %%edx                      \n\t"
        "cmpl %%edx, %%ebx                      \n\t"
        " ja 1f                                 \n\t"
        "cmp $0x2000000, %%edx                  \n\t" //FIXME avoidable
        "setb %%cl                              \n\t"
        "shl %%cl, %%edx                        \n\t"
        "shl %%cl, %%ebx                        \n\t"
        "movzbl "MANGLE(ff_h264_mps_state)"(%%eax), %%ecx   \n\t"
        "movb %%cl, (%1)                        \n\t"
//eax:state ebx:low, edx:range, esi:RangeLPS
        "test %%bx, %%bx                        \n\t"
        " jnz 2f                                \n\t"
        "movl "BYTE     "(%2), %%esi            \n\t"
        "subl $0xFFFF, %%ebx                    \n\t"
        "movzwl (%%esi), %%ecx                  \n\t"
        "bswap %%ecx                            \n\t"
        "shrl $15, %%ecx                        \n\t"
        "addl $2, %%esi                         \n\t"
        "addl %%ecx, %%ebx                      \n\t"
        "movl %%esi, "BYTE    "(%2)             \n\t"
        "jmp 2f                                 \n\t"
        "1:                                     \n\t"
//eax:state ebx:low, edx:range, esi:RangeLPS
        "subl %%edx, %%ebx                      \n\t"
        "movl %%esi, %%edx                      \n\t"
        "shr $19, %%esi                         \n\t"
        "movzbl " MANGLE(ff_h264_norm_shift) "(%%esi), %%ecx   \n\t"
        "shll %%cl, %%ebx                       \n\t"
        "shll %%cl, %%edx                       \n\t"
        "movzbl "MANGLE(ff_h264_lps_state)"(%%eax), %%ecx   \n\t"
        "movb %%cl, (%1)                        \n\t"
        "addl $1, %%eax                         \n\t"
        "test %%bx, %%bx                        \n\t"
        " jnz 2f                                \n\t"

        "movl "BYTE     "(%2), %%ecx            \n\t"
        "movzwl (%%ecx), %%esi                  \n\t"
        "bswap %%esi                            \n\t"
        "shrl $15, %%esi                        \n\t"
        "subl $0xFFFF, %%esi                    \n\t"
        "addl $2, %%ecx                         \n\t"
        "movl %%ecx, "BYTE    "(%2)             \n\t"

        "leal -1(%%ebx), %%ecx                  \n\t"
        "xorl %%ebx, %%ecx                      \n\t"
        "shrl $17, %%ecx                        \n\t"
        "movzbl " MANGLE(ff_h264_norm_shift) "(%%ecx), %%ecx   \n\t"
        "neg %%ecx                              \n\t"
        "add $7, %%ecx                          \n\t"

        "shll %%cl , %%esi                      \n\t"
        "addl %%esi, %%ebx                      \n\t"
        "2:                                     \n\t"
        "movl %%edx, "RANGE    "(%2)            \n\t"
        "movl %%ebx, "LOW      "(%2)            \n\t"
        :"=&a"(bit) //FIXME this is fragile gcc either runs out of registers or misscompiles it (for example if "+a"(bit) or "+m"(*state) is used
        :"r"(state), "r"(c)
        : "%ecx", "%ebx", "%edx", "%esi"
    );
    bit&=1;
#else
    asm volatile(
        "movzbl (%1), %%eax                     \n\t"
        "movl "RANGE    "(%2), %%ebx            \n\t"
        "movl "RANGE    "(%2), %%edx            \n\t"
        "shrl $23, %%ebx                        \n\t"
        "movzbl "MANGLE(ff_h264_lps_range)"(%%ebx, %%eax, 4), %%esi\n\t"
        "shll $17, %%esi                        \n\t"
        "movl "LOW      "(%2), %%ebx            \n\t"
//eax:state ebx:low, edx:range, esi:RangeLPS
        "subl %%esi, %%edx                      \n\t"
#ifdef CMOV_IS_FAST //FIXME actually define this somewhere
        "cmpl %%ebx, %%edx                      \n\t"
        "cmova %%edx, %%esi                     \n\t"
        "sbbl %%ecx, %%ecx                      \n\t"
        "andl %%ecx, %%edx                      \n\t"
        "subl %%edx, %%ebx                      \n\t"
        "xorl %%ecx, %%eax                      \n\t"
#else
        "movl %%edx, %%ecx                      \n\t"
        "subl %%ebx, %%edx                      \n\t"
        "sarl $31, %%edx                        \n\t" //lps_mask
        "subl %%ecx, %%esi                      \n\t" //RangeLPS - range
        "andl %%edx, %%esi                      \n\t" //(RangeLPS - range)&lps_mask
        "addl %%ecx, %%esi                      \n\t" //new range
        "andl %%edx, %%ecx                      \n\t"
        "subl %%ecx, %%ebx                      \n\t"
        "xorl %%edx, %%eax                      \n\t"
#endif

//eax:state ebx:low edx:mask esi:range
        "movzbl "MANGLE(ff_h264_mps_state)"(%%eax), %%ecx   \n\t"
        "movb %%cl, (%1)                        \n\t"

        "movl %%esi, %%edx                      \n\t"
//eax:bit ebx:low edx:range esi:range

        "shr $19, %%esi                         \n\t"
        "movzbl " MANGLE(ff_h264_norm_shift) "(%%esi), %%ecx   \n\t"
        "shll %%cl, %%ebx                       \n\t"
        "shll %%cl, %%edx                       \n\t"
        "test %%bx, %%bx                        \n\t"
        " jnz 1f                                \n\t"

        "movl "BYTE     "(%2), %%ecx            \n\t"
        "movzwl (%%ecx), %%esi                  \n\t"
        "bswap %%esi                            \n\t"
        "shrl $15, %%esi                        \n\t"
        "subl $0xFFFF, %%esi                    \n\t"
        "addl $2, %%ecx                         \n\t"
        "movl %%ecx, "BYTE    "(%2)             \n\t"

        "leal -1(%%ebx), %%ecx                  \n\t"
        "xorl %%ebx, %%ecx                      \n\t"
        "shrl $17, %%ecx                        \n\t"
        "movzbl " MANGLE(ff_h264_norm_shift) "(%%ecx), %%ecx   \n\t"
        "neg %%ecx                              \n\t"
        "add $7, %%ecx                          \n\t"

        "shll %%cl , %%esi                      \n\t"
        "addl %%esi, %%ebx                      \n\t"
        "1:                                     \n\t"
        "movl %%edx, "RANGE    "(%2)            \n\t"
        "movl %%ebx, "LOW      "(%2)            \n\t"
        :"=&a"(bit)
        :"r"(state), "r"(c)
        : "%ecx", "%ebx", "%edx", "%esi"
    );
    bit&=1;
#endif
#else
    int s = *state;
    int RangeLPS= ff_h264_lps_range[s][c->range>>(CABAC_BITS+7)]<<(CABAC_BITS+1);
    int bit, lps_mask attribute_unused;

    c->range -= RangeLPS;
#ifndef BRANCHLESS_CABAC_DECODER
    if(c->low < c->range){
        bit= s&1;
        *state= ff_h264_mps_state[s];
        renorm_cabac_decoder_once(c);
    }else{
        bit= ff_h264_norm_shift[RangeLPS>>19];
        c->low -= c->range;
        *state= ff_h264_lps_state[s];
        c->range = RangeLPS<<bit;
        c->low <<= bit;
        bit= (s&1)^1;

        if(!(c->low & 0xFFFF)){
            refill2(c);
        }
    }
#else
    lps_mask= (c->range - c->low)>>31;

    c->low -= c->range & lps_mask;
    c->range += (RangeLPS - c->range) & lps_mask;

    s^=lps_mask;
    *state= ff_h264_mps_state[s];
    bit= s&1;

    lps_mask= ff_h264_norm_shift[c->range>>(CABAC_BITS+3)];
    c->range<<= lps_mask;
    c->low  <<= lps_mask;
    if(!(c->low & CABAC_MASK))
        refill2(c);
#endif
#endif
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
